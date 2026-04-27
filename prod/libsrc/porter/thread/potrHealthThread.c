/**
 *******************************************************************************
 *  @file           potrHealthThread.c
 *  @brief          ヘルスチェックスレッドモジュール。
 *  @author         Tetsuo Honda
 *  @date           2026/03/08
 *  @version        1.0.0
 *
 *  @details
 *  非 TCP 通信種別で動作する定周期 PING 送信スレッドです。\n
 *  health_interval_ms 周期で PING パケットを送信します。\n
 *  一方向通信では送信のみ、双方向通信では request/reply 形式の PING 送信を担います。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>
#include <string.h>
#include <inttypes.h>

#include <porter_const.h>
#include <porter.h>

#include "../protocol/packet.h"
#include "../protocol/window.h"
#include "../potrContext.h"
#include "../potrPeerTable.h"
#include "potrHealthThread.h"
#include "../infra/potrTrace.h"
#include "../infra/potrPlatform.h"
#include <com_util/crypto/crypto.h>

/* TCP health スレッドに渡す引数 (path ごと) */
typedef struct
{
    struct PotrContext_ *ctx;
    int                  path_idx;
    int                  _pad;
} HealthArg;

static HealthArg s_health_args[POTR_MAX_PATH];

static uint64_t get_last_health_signal_send_ms(const struct PotrContext_ *ctx)
{
    uint64_t last_ping = ctx->last_ping_send_ms;
    uint64_t last_data = ctx->last_valid_data_send_ms;

    return (last_ping > last_data) ? last_ping : last_data;
}

static void signal_health_thread(struct PotrContext_ *ctx, int path_idx)
{
    if (ctx == NULL
        || path_idx < 0
        || path_idx >= (int)POTR_MAX_PATH
        || !ctx->health_running[path_idx])
    {
        return;
    }

    COM_UTIL_MUTEX_LOCK(&ctx->health_mutex[path_idx]);
    if (ctx->health_running[path_idx])
    {
        COM_UTIL_COND_SIGNAL(&ctx->health_wakeup[path_idx]);
    }
    COM_UTIL_MUTEX_UNLOCK(&ctx->health_mutex[path_idx]);
}

/* health_interval_ms ミリ秒、または停止シグナルが来るまでスリープする (path_idx 版) */
static void health_sleep(struct PotrContext_ *ctx, int path_idx, uint32_t interval_ms)
{
    /* オープン時割り込み PING フラグが立っていれば即リターン (初回スリープをスキップ) */
    if (ctx->health_send_immediate[path_idx])
    {
        ctx->health_send_immediate[path_idx] = 0;
        return;
    }

    COM_UTIL_MUTEX_LOCK(&ctx->health_mutex[path_idx]);
    if (ctx->health_running[path_idx])
    {
        com_util_condvar_timedwait(&ctx->health_wakeup[path_idx],
                               &ctx->health_mutex[path_idx], interval_ms);
    }
    COM_UTIL_MUTEX_UNLOCK(&ctx->health_mutex[path_idx]);
}

static int wait_oneway_udp_ping_due(struct PotrContext_ *ctx,
                                    uint64_t             initial_ping_due_ms,
                                    uint64_t            *last_logged_data_ms)
{
    while (ctx->health_running[0])
    {
        uint64_t now      = com_util_get_monotonic_ms();
        uint64_t last_ping = ctx->last_ping_send_ms;
        uint64_t last_data = ctx->last_valid_data_send_ms;
        uint64_t due_ms;

        if (last_ping == 0U && last_data == 0U)
        {
            due_ms = initial_ping_due_ms;
        }
        else
        {
            due_ms = get_last_health_signal_send_ms(ctx)
                   + (uint64_t)ctx->health_interval_ms;
        }

        if (now >= due_ms)
        {
            return 1;
        }

        if (last_data != 0U
            && last_data >= last_ping
            && last_data != *last_logged_data_ms)
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                     "health[service_id=%" PRId64 "]: suppress PING due to recent DATA"
                     " (remaining=%" PRIu64 "ms)",
                     ctx->service.service_id,
                     due_ms - now);
            *last_logged_data_ms = last_data;
        }

        health_sleep(ctx, 0, (uint32_t)(due_ms - now));
    }

    return 0;
}

static int tcp_send_ping_packet(struct PotrContext_ *ctx, int path_idx)
{
    PotrPacket           ping_pkt;
    PotrPacketSessionHdr shdr;
    uint8_t              health_states[POTR_MAX_PATH];
    uint32_t             seq;
    size_t               wire_len;
    int                  build_result;
    int                  send_ok = 0;

    if (ctx == NULL
        || path_idx < 0
        || path_idx >= (int)POTR_MAX_PATH
        || ctx->close_requested
        || ctx->tcp_active_paths == 0
        || ctx->tcp_conn_fd[path_idx] == POTR_INVALID_SOCKET)
    {
        return POTR_ERROR;
    }

    potr_copy_path_ping_state(health_states, ctx->path_ping_state, POTR_MAX_PATH);

    COM_UTIL_MUTEX_LOCK(&ctx->send_window_mutex);
    shdr.service_id      = ctx->service.service_id;
    shdr.session_id      = ctx->session_id;
    shdr.session_tv_sec  = ctx->session_tv_sec;
    shdr.session_tv_nsec = ctx->session_tv_nsec;
    seq                  = ctx->send_window.next_seq;
    build_result = packet_build_ping(&ping_pkt, &shdr, seq,
                                     health_states, (uint16_t)POTR_MAX_PATH);
    COM_UTIL_MUTEX_UNLOCK(&ctx->send_window_mutex);

    if (build_result != POTR_SUCCESS)
    {
        return POTR_ERROR;
    }

    if (ctx->service.encrypt_enabled)
    {
        uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE];
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t  enc_out = POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE;

        ping_pkt.flags      |= htons(POTR_FLAG_ENCRYPTED);
        ping_pkt.payload_len = htons((uint16_t)(POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE));

        memcpy(nonce,      &ping_pkt.session_id, 4);
        memcpy(nonce + 4,  &ping_pkt.flags,      2);
        memcpy(nonce + 6,  &ping_pkt.seq_num,    4);
        memset(nonce + 10, 0,                    2);

        memcpy(wire_buf, &ping_pkt, PACKET_HEADER_SIZE);
        memcpy(wire_buf + PACKET_HEADER_SIZE, health_states, POTR_MAX_PATH);
        if (com_util_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out,
                         wire_buf + PACKET_HEADER_SIZE, POTR_MAX_PATH,
                         ctx->service.encrypt_key,
                         nonce,
                         wire_buf, PACKET_HEADER_SIZE) != 0)
        {
            return POTR_ERROR;
        }
        wire_len = PACKET_HEADER_SIZE + enc_out;

        COM_UTIL_MUTEX_LOCK(&ctx->tcp_send_mutex[path_idx]);
        if (ctx->tcp_conn_fd[path_idx] != POTR_INVALID_SOCKET)
        {
            send_ok = (potr_tcp_send(ctx->tcp_conn_fd[path_idx],
                                     wire_buf, wire_len) == 0);
        }
        COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_send_mutex[path_idx]);
    }
    else
    {
        uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_MAX_PATH];
        memcpy(wire_buf, &ping_pkt, PACKET_HEADER_SIZE);
        memcpy(wire_buf + PACKET_HEADER_SIZE, health_states, POTR_MAX_PATH);
        wire_len = PACKET_HEADER_SIZE + POTR_MAX_PATH;

        COM_UTIL_MUTEX_LOCK(&ctx->tcp_send_mutex[path_idx]);
        if (ctx->tcp_conn_fd[path_idx] != POTR_INVALID_SOCKET)
        {
            send_ok = (potr_tcp_send(ctx->tcp_conn_fd[path_idx],
                                     wire_buf, wire_len) == 0);
        }
        COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_send_mutex[path_idx]);
    }

    return send_ok ? POTR_SUCCESS : POTR_ERROR;
}

/* ====================================================================
 * 非 TCP (UDP/マルチキャスト) 用ヘルスチェックスレッド本体
 * ==================================================================== */
COM_UTIL_THREAD_FUNC(health_thread_func)
{
    struct PotrContext_ *ctx = (struct PotrContext_ *)arg;
    PotrPacketSessionHdr shdr;
    int                  is_oneway_udp = potr_is_oneway_udp_type(ctx->service.type);
    uint64_t             initial_ping_due_ms = com_util_get_monotonic_ms() + (uint64_t)ctx->health_interval_ms;
    uint64_t             last_logged_data_ms = 0U;

    shdr.service_id = ctx->service.service_id;
    /* 1:1 モード: session はコンテキストから取得 (N:1 は後述のループで各ピアから取得) */
    if (!ctx->is_multi_peer)
    {
        shdr.session_id      = ctx->session_id;
        shdr.session_tv_sec  = ctx->session_tv_sec;
        shdr.session_tv_nsec = ctx->session_tv_nsec;
    }

    while (ctx->health_running[0])
    {
        if (is_oneway_udp)
        {
            if (!wait_oneway_udp_ping_due(ctx, initial_ping_due_ms, &last_logged_data_ms))
            {
                break;
            }
        }
        else
        {
            health_sleep(ctx, 0, ctx->health_interval_ms);

            if (!ctx->health_running[0]) break;
        }

        /* PING を送信する */
        if (ctx->is_multi_peer)
        {
            /* N:1 モード: アクティブ全ピアへ PING を送信する */
            int i;

            COM_UTIL_MUTEX_LOCK(&ctx->peers_mutex);

            for (i = 0; i < ctx->max_peers && ctx->health_running[0]; i++)
            {
                PotrPacket           ping_pkt;
                PotrPacketSessionHdr peer_shdr;
                uint32_t             seq;
                size_t               wire_len;
                uint8_t              health_states[POTR_MAX_PATH];
                int                  k;

                if (!ctx->peers[i].active) continue;

                peer_shdr.service_id      = ctx->service.service_id;
                peer_shdr.session_id      = ctx->peers[i].session_id;
                peer_shdr.session_tv_sec  = ctx->peers[i].session_tv_sec;
                peer_shdr.session_tv_nsec = ctx->peers[i].session_tv_nsec;

                COM_UTIL_MUTEX_LOCK(&ctx->peers[i].send_window_mutex);
                seq = ctx->peers[i].send_window.next_seq;
                /* N:1 (UNICAST_BIDIR_N1) は双方向 PING。ピアごとの自端パス受信状態をペイロードに設定する。 */
                potr_copy_path_ping_state(health_states, ctx->peers[i].path_ping_state,
                                          POTR_MAX_PATH);
                packet_build_ping(&ping_pkt, &peer_shdr, seq, health_states,
                                  (uint16_t)POTR_MAX_PATH);
                COM_UTIL_MUTEX_UNLOCK(&ctx->peers[i].send_window_mutex);

                if (ctx->service.encrypt_enabled)
                {
                    uint8_t  wire_buf[PACKET_HEADER_SIZE + POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE];
                    uint8_t  nonce[POTR_CRYPTO_NONCE_SIZE];
                    size_t   enc_out = POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE;

                    ping_pkt.flags      |= htons(POTR_FLAG_ENCRYPTED);
                    ping_pkt.payload_len = htons((uint16_t)(POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE));

                    memcpy(nonce,      &ping_pkt.session_id, 4);
                    memcpy(nonce + 4,  &ping_pkt.flags,      2);
                    memcpy(nonce + 6,  &ping_pkt.seq_num,    4);
                    memset(nonce + 10, 0,                    2);

                    memcpy(wire_buf, &ping_pkt, PACKET_HEADER_SIZE);
                    memcpy(wire_buf + PACKET_HEADER_SIZE, health_states, POTR_MAX_PATH);
                    if (com_util_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out,
                                     wire_buf + PACKET_HEADER_SIZE, POTR_MAX_PATH,
                                     ctx->service.encrypt_key,
                                     nonce,
                                     wire_buf, PACKET_HEADER_SIZE) != 0)
                    {
                        continue;
                    }
                    wire_len = PACKET_HEADER_SIZE + enc_out;

                    for (k = 0; k < (int)POTR_MAX_PATH; k++)
                    {
                        if (ctx->peers[i].dest_addr[k].sin_family == 0) continue;
                        potr_sendto(ctx->sock[k], wire_buf, wire_len, 0,
                                    (const struct sockaddr *)&ctx->peers[i].dest_addr[k],
                                    (int)sizeof(ctx->peers[i].dest_addr[k]));
                    }
                }
                else
                {
                    uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_MAX_PATH];
                    memcpy(wire_buf, &ping_pkt, PACKET_HEADER_SIZE);
                    memcpy(wire_buf + PACKET_HEADER_SIZE, health_states, POTR_MAX_PATH);
                    wire_len = PACKET_HEADER_SIZE + POTR_MAX_PATH;

                    for (k = 0; k < (int)POTR_MAX_PATH; k++)
                    {
                        if (ctx->peers[i].dest_addr[k].sin_family == 0) continue;
                        potr_sendto(ctx->sock[k], wire_buf, wire_len, 0,
                                    (const struct sockaddr *)&ctx->peers[i].dest_addr[k],
                                    (int)sizeof(ctx->peers[i].dest_addr[k]));
                    }
                }

                POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                         "health[service_id=%" PRId64 "]: PING peer=%u seq=%u",
                         ctx->service.service_id,
                         (unsigned)ctx->peers[i].peer_id, (unsigned)seq);
            }

            COM_UTIL_MUTEX_UNLOCK(&ctx->peers_mutex);
        }
        else
        {
            /* 1:1 UDP/マルチキャスト: 単一宛先へ PING を送信する */
            PotrPacket ping_pkt;
            uint32_t   seq;
            size_t     wire_len;
            int        build_result;
            int        sent_any = 0;
            uint8_t    health_states[POTR_MAX_PATH];
            int        k;

            /* 双方向 PING (UNICAST_BIDIR) のみ自端のパス受信状態を設定する。
               それ以外 (UNICAST/MULTICAST/BROADCAST/RAW 系) は PING を受信しないため UNDEFINED を設定する。 */
            if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
            {
                potr_copy_path_ping_state(health_states, ctx->path_ping_state, POTR_MAX_PATH);
            }
            else
            {
                memset(health_states, POTR_PING_STATE_UNDEFINED, POTR_MAX_PATH);
            }

            COM_UTIL_MUTEX_LOCK(&ctx->send_window_mutex);
            seq          = ctx->send_window.next_seq;
            build_result = packet_build_ping(&ping_pkt, &shdr, seq,
                                             health_states, (uint16_t)POTR_MAX_PATH);
            COM_UTIL_MUTEX_UNLOCK(&ctx->send_window_mutex);

            if (build_result != POTR_SUCCESS) { continue; }

            POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                     "health[service_id=%" PRId64 "]: PING seq=%u",
                     ctx->service.service_id, (unsigned)seq);

            if (ctx->service.encrypt_enabled)
            {
                uint8_t  wire_buf[PACKET_HEADER_SIZE + POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE];
                uint8_t  nonce[POTR_CRYPTO_NONCE_SIZE];
                size_t   enc_out = POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE;

                ping_pkt.flags      |= htons(POTR_FLAG_ENCRYPTED);
                ping_pkt.payload_len = htons((uint16_t)(POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE));

                memcpy(nonce,      &ping_pkt.session_id, 4);
                memcpy(nonce + 4,  &ping_pkt.flags,      2);
                memcpy(nonce + 6,  &ping_pkt.seq_num,    4);
                memset(nonce + 10, 0,                    2);

                memcpy(wire_buf, &ping_pkt, PACKET_HEADER_SIZE);
                memcpy(wire_buf + PACKET_HEADER_SIZE, health_states, POTR_MAX_PATH);
                if (com_util_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out,
                                 wire_buf + PACKET_HEADER_SIZE, POTR_MAX_PATH,
                                 ctx->service.encrypt_key,
                                 nonce,
                                 wire_buf, PACKET_HEADER_SIZE) != 0) { continue; }
                wire_len = PACKET_HEADER_SIZE + enc_out;

                for (k = 0; k < ctx->n_path; k++)
                {
                    int sent_len;
                    sent_len = potr_sendto(ctx->sock[k], wire_buf, wire_len, 0,
                                           (const struct sockaddr *)&ctx->dest_addr[k],
                                           (int)sizeof(ctx->dest_addr[k]));
                    if (sent_len == (int)wire_len)
                    {
                        sent_any = 1;
                    }
                }
            }
            else
            {
                uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_MAX_PATH];
                memcpy(wire_buf, &ping_pkt, PACKET_HEADER_SIZE);
                memcpy(wire_buf + PACKET_HEADER_SIZE, health_states, POTR_MAX_PATH);
                wire_len = PACKET_HEADER_SIZE + POTR_MAX_PATH;

                for (k = 0; k < ctx->n_path; k++)
                {
                    int sent_len;
                    sent_len = potr_sendto(ctx->sock[k], wire_buf, wire_len, 0,
                                           (const struct sockaddr *)&ctx->dest_addr[k],
                                           (int)sizeof(ctx->dest_addr[k]));
                    if (sent_len == (int)wire_len)
                    {
                        sent_any = 1;
                    }
                }
            }

            if (is_oneway_udp && sent_any)
            {
                ctx->last_ping_send_ms = com_util_get_monotonic_ms();
                last_logged_data_ms    = 0U;
            }
        }
    }

    COM_UTIL_THREAD_RETURN;
}

/* ====================================================================
 * TCP 用ヘルスチェックスレッド本体 (path ごと)
 * ==================================================================== */
COM_UTIL_THREAD_FUNC(tcp_health_thread_func)
{
    HealthArg           *harg     = (HealthArg *)arg;
    struct PotrContext_ *ctx      = harg->ctx;
    int                  path_idx = harg->path_idx;

    POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
             "tcp_health[service_id=%" PRId64 " path=%d]: starting",
             ctx->service.service_id, path_idx);

    while (ctx->health_running[path_idx])
    {
        /* 固定間隔でスリープ */
        health_sleep(ctx, path_idx, ctx->health_interval_ms);

        if (!ctx->health_running[path_idx]) break;
        (void)tcp_send_ping_packet(ctx, path_idx);
    }

    POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
             "tcp_health[service_id=%" PRId64 " path=%d]: exited",
             ctx->service.service_id, path_idx);

    COM_UTIL_THREAD_RETURN;
}

int potr_tcp_send_ping_now(struct PotrContext_ *ctx, int path_idx)
{
    return tcp_send_ping_packet(ctx, path_idx);
}

/**
 *******************************************************************************
 *  @brief          非 TCP ヘルスチェックスレッドを起動します。
 *  @param[in,out]  ctx  セッションコンテキストへのポインタ。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *
 *  @details
 *  ctx->health_interval_ms が 0 の場合は起動しません (POTR_SUCCESS を返します)。
 *******************************************************************************
 */
int potr_health_thread_start(struct PotrContext_ *ctx)
{
    if (ctx == NULL) { return POTR_ERROR; }

    if (ctx->health_interval_ms == 0)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                 "health_thread[service_id=%" PRId64 "]: disabled (health_interval_ms=0)",
                 ctx->service.service_id);
        return POTR_SUCCESS;
    }

    POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
             "health_thread[service_id=%" PRId64 "]: starting (interval=%ums)",
             ctx->service.service_id,
             (unsigned)ctx->health_interval_ms);

    COM_UTIL_MUTEX_INIT(&ctx->health_mutex[0]);
    COM_UTIL_COND_INIT(&ctx->health_wakeup[0]);

    ctx->health_running[0] = 1;

    if (com_util_thread_create(&ctx->health_thread[0], health_thread_func, ctx) != 0)
    {
        ctx->health_running[0] = 0;
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "health_thread[service_id=%" PRId64 "]: thread create failed",
                 ctx->service.service_id);
        return POTR_ERROR;
    }

    return POTR_SUCCESS;
}

/**
 *******************************************************************************
 *  @brief          非 TCP ヘルスチェックスレッドを停止します。
 *  @param[in,out]  ctx  セッションコンテキストへのポインタ。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *******************************************************************************
 */
int potr_health_thread_stop(struct PotrContext_ *ctx)
{
    if (ctx == NULL) { return POTR_ERROR; }
    if (!ctx->health_running[0]) { return POTR_SUCCESS; }

    ctx->health_running[0] = 0;

    COM_UTIL_MUTEX_LOCK(&ctx->health_mutex[0]);
    COM_UTIL_COND_SIGNAL(&ctx->health_wakeup[0]);
    COM_UTIL_MUTEX_UNLOCK(&ctx->health_mutex[0]);

    com_util_thread_join(&ctx->health_thread[0]);

    COM_UTIL_COND_DESTROY(&ctx->health_wakeup[0]);
    COM_UTIL_MUTEX_DESTROY(&ctx->health_mutex[0]);

    return POTR_SUCCESS;
}

void potr_health_thread_wake(struct PotrContext_ *ctx)
{
    signal_health_thread(ctx, 0);
}

/**
 *******************************************************************************
 *  @brief          TCP ヘルスチェックスレッドを path ごとに起動します。
 *  @param[in,out]  ctx       セッションコンテキストへのポインタ。
 *  @param[in]      path_idx  パスインデックス (0 ～ n_path-1)。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *
 *  @details
 *  ctx->health_interval_ms が 0 の場合は起動しません (POTR_SUCCESS を返します)。
 *******************************************************************************
 */
int potr_tcp_health_thread_start(struct PotrContext_ *ctx, int path_idx)
{
    if (ctx == NULL) { return POTR_ERROR; }

    if (ctx->health_interval_ms == 0)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                 "tcp_health_thread[service_id=%" PRId64 " path=%d]: disabled",
                 ctx->service.service_id, path_idx);
        return POTR_SUCCESS;
    }

    s_health_args[path_idx].ctx      = ctx;
    s_health_args[path_idx].path_idx = path_idx;

    ctx->health_running[path_idx] = 1;

    if (com_util_thread_create(&ctx->health_thread[path_idx],
                           tcp_health_thread_func,
                           &s_health_args[path_idx]) != 0)
    {
        ctx->health_running[path_idx] = 0;
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "tcp_health_thread[service_id=%" PRId64 " path=%d]: thread create failed",
                 ctx->service.service_id, path_idx);
        return POTR_ERROR;
    }

    return POTR_SUCCESS;
}

/**
 *******************************************************************************
 *  @brief          TCP ヘルスチェックスレッドを停止します。
 *  @param[in,out]  ctx       セッションコンテキストへのポインタ。
 *  @param[in]      path_idx  パスインデックス (0 ～ n_path-1)。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *******************************************************************************
 */
int potr_tcp_health_thread_stop(struct PotrContext_ *ctx, int path_idx)
{
    if (ctx == NULL) { return POTR_ERROR; }
    if (!ctx->health_running[path_idx]) { return POTR_SUCCESS; }

    ctx->health_running[path_idx] = 0;

    COM_UTIL_MUTEX_LOCK(&ctx->health_mutex[path_idx]);
    COM_UTIL_COND_SIGNAL(&ctx->health_wakeup[path_idx]);
    COM_UTIL_MUTEX_UNLOCK(&ctx->health_mutex[path_idx]);

    com_util_thread_join(&ctx->health_thread[path_idx]);

    return POTR_SUCCESS;
}

void potr_tcp_health_thread_wake(struct PotrContext_ *ctx, int path_idx)
{
    signal_health_thread(ctx, path_idx);
}

void potr_tcp_health_thread_wake_all(struct PotrContext_ *ctx)
{
    int i;

    if (ctx == NULL)
    {
        return;
    }

    for (i = 0; i < ctx->n_path; i++)
    {
        signal_health_thread(ctx, i);
    }
}
