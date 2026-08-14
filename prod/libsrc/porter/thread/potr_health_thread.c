/**
 *******************************************************************************
 *  @file           potr_health_thread.c
 *  @brief          ヘルスチェックを行うスレッドを実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/08
 *  @version        1.0.0
 *
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

#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/porter_spec.h>

#include <porter/protocol/packet.h>
#include <porter/protocol/window.h>
#include <porter/potr_context.h>
#include <porter/potr_peer_table.h>
#include <porter/thread/potr_health_thread.h>
#include <porter/infra/potr_trace.h>
#include <porter/infra/potr_result.h>
#include <com_util/crypto/crypto.h>
#include <com_util/net/byteorder.h>
#include <com_util/net/socket.h>

static uint64_t get_last_health_signal_send_ms(const potr_context *ctx)
{
    uint64_t last_ping = ctx->last_ping_send_ms;
    uint64_t last_data = ctx->last_valid_data_send_ms;

    if (last_ping > last_data)
    {
        return last_ping;
    }

    return last_data;
}

static void signal_health_thread(potr_context *ctx, int path_idx)
{
    if (ctx == NULL || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH || !ctx->health_running[path_idx])
    {
        return;
    }

    com_util_local_lock_lock(ctx->health_mutex[path_idx], COM_UTIL_SYNC_WAIT_FOREVER);
    if (ctx->health_running[path_idx])
    {
        com_util_condvar_signal(ctx->health_wakeup[path_idx]);
    }
    com_util_local_lock_unlock(ctx->health_mutex[path_idx]);
}

/* health_interval_ms ミリ秒、または停止シグナルが来るまでスリープする (path_idx 版) */
static void health_sleep(potr_context *ctx, int path_idx, uint32_t interval_ms)
{
    /* オープン時割り込み PING フラグが立っていれば即リターン (初回スリープをスキップ) */
    if (ctx->health_send_immediate[path_idx])
    {
        ctx->health_send_immediate[path_idx] = 0;
        return;
    }

    com_util_local_lock_lock(ctx->health_mutex[path_idx], COM_UTIL_SYNC_WAIT_FOREVER);
    if (ctx->health_running[path_idx])
    {
        /* interval_ms は PING 間隔の設定値。実用範囲は INT_MAX 以下 */
        com_util_condvar_wait(ctx->health_wakeup[path_idx], ctx->health_mutex[path_idx], (int)interval_ms);
    }
    com_util_local_lock_unlock(ctx->health_mutex[path_idx]);
}

/* 次回 PING 送信時刻まで待機する。PING を送るべきなら 1、health_running 停止なら 0 を返す。
   待機結果の判定 (真偽値) を返す述語のため共通結果コードの適用対象外。 */
static int wait_oneway_udp_ping_due(potr_context *ctx, uint64_t initial_ping_due_ms, uint64_t *last_logged_data_ms)
{
    while (ctx->health_running[0])
    {
        uint64_t now = com_util_get_monotonic_ms();
        uint64_t last_ping = ctx->last_ping_send_ms;
        uint64_t last_data = ctx->last_valid_data_send_ms;
        uint64_t due_ms;

        if (last_ping == 0U && last_data == 0U)
        {
            due_ms = initial_ping_due_ms;
        }
        else
        {
            due_ms = get_last_health_signal_send_ms(ctx) + (uint64_t)ctx->health_interval_ms;
        }

        if (now >= due_ms)
        {
            return 1;
        }

        if (last_data != 0U && last_data >= last_ping && last_data != *last_logged_data_ms)
        {
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
                       "health[service_id=%" PRId64 "]: suppress PING due to recent DATA"
                       " (remaining=%" PRIu64 "ms)",
                       ctx->service.service_id, due_ms - now);
            *last_logged_data_ms = last_data;
        }

        health_sleep(ctx, 0, (uint32_t)(due_ms - now));
    }

    return 0;
}

static int tcp_send_ping_packet(potr_context *ctx, int path_idx)
{
    potr_packet ping_pkt;
    potr_internal_packet_session_hdr shdr;
    uint8_t health_states[POTR_MAX_PATH];
    uint32_t seq;
    size_t wire_len;
    int build_result;
    int send_result = POTR_ERR_DISCONNECTED;

    if (ctx == NULL || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (ctx->close_requested)
    {
        return POTR_ERR_CANCELED;
    }
    if (ctx->tcp_active_paths == 0 || ctx->tcp_conn_fd[path_idx] == COM_UTIL_INVALID_SOCKET)
    {
        return POTR_ERR_DISCONNECTED;
    }

    com_util_local_lock_lock(ctx->send_window_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
    shdr.service_id = ctx->service.service_id;
    shdr.session_id = ctx->session_id;
    potr_session_ts_to_hdr(&ctx->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);
    seq = ctx->send_window.next_seq;
    build_result = potr_internal_packet_build_ping(&ping_pkt, &shdr, seq, health_states, (uint16_t)POTR_MAX_PATH);
    com_util_local_lock_unlock(ctx->send_window_mutex);

    if (build_result != POTR_OK)
    {
        return build_result;
    }

    /* path_ping_state スナップショットの採取と TCP 書き込みを同一クリティカル セクション
     * に閉じ込め、同一 path で送る PING が「古いスナップショットを後に届ける」 順序逆転を
     * 起こさないようにする。順序が崩れると peer 側の remote_path_ping_state が
     * NORMAL から UNDEFINED へ後退し、瞬間的に DISCONNECTED と判定される。 */
    if (ctx->service.encrypt_enabled)
    {
        uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE];
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t enc_out = POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE;
        int encrypt_failed = 0;

        ping_pkt.flags |= com_util_hton16(POTR_FLAG_ENCRYPTED);
        ping_pkt.payload_len = com_util_hton16((uint16_t)(POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE));

        memcpy(nonce, &ping_pkt.session_id, 4);
        memcpy(nonce + 4, &ping_pkt.flags, 2);
        memcpy(nonce + 6, &ping_pkt.seq_num, 4);
        memset(nonce + 10, 0, 2);

        memcpy(wire_buf, &ping_pkt, PACKET_HEADER_SIZE);

        com_util_local_lock_lock(ctx->tcp_send_mutex[path_idx], COM_UTIL_SYNC_WAIT_FOREVER);
        if (ctx->tcp_conn_fd[path_idx] != COM_UTIL_INVALID_SOCKET)
        {
            potr_copy_path_ping_state(health_states, ctx->path_ping_state, POTR_MAX_PATH);
            memcpy(wire_buf + PACKET_HEADER_SIZE, health_states, POTR_MAX_PATH);
            if (com_util_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out, wire_buf + PACKET_HEADER_SIZE, POTR_MAX_PATH,
                                 ctx->service.encrypt_key, nonce, wire_buf, PACKET_HEADER_SIZE) != COM_UTIL_OK)
            {
                encrypt_failed = 1;
            }
            else
            {
                com_util_error detail;

                wire_len = PACKET_HEADER_SIZE + enc_out;
                if (com_util_socket_send_all(ctx->tcp_conn_fd[path_idx], wire_buf, wire_len, &detail) == COM_UTIL_OK)
                {
                    send_result = POTR_OK;
                }
                else
                {
                    send_result = potr_internal_result_from_error(&detail);
                }
            }
        }
        com_util_local_lock_unlock(ctx->tcp_send_mutex[path_idx]);

        if (encrypt_failed)
        {
            /* com_util の暗号化失敗には、porter の分類へ変換できる詳細コードがありません。 */
            return POTR_ERR_UNKNOWN;
        }
    }
    else
    {
        uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_MAX_PATH];
        memcpy(wire_buf, &ping_pkt, PACKET_HEADER_SIZE);
        wire_len = PACKET_HEADER_SIZE + POTR_MAX_PATH;

        com_util_local_lock_lock(ctx->tcp_send_mutex[path_idx], COM_UTIL_SYNC_WAIT_FOREVER);
        if (ctx->tcp_conn_fd[path_idx] != COM_UTIL_INVALID_SOCKET)
        {
            com_util_error detail;

            potr_copy_path_ping_state(health_states, ctx->path_ping_state, POTR_MAX_PATH);
            memcpy(wire_buf + PACKET_HEADER_SIZE, health_states, POTR_MAX_PATH);
            if (com_util_socket_send_all(ctx->tcp_conn_fd[path_idx], wire_buf, wire_len, &detail) == COM_UTIL_OK)
            {
                send_result = POTR_OK;
            }
            else
            {
                send_result = potr_internal_result_from_error(&detail);
            }
        }
        com_util_local_lock_unlock(ctx->tcp_send_mutex[path_idx]);
    }

    return send_result;
}

/* ====================================================================
 * 非 TCP (UDP/マルチキャスト) 用ヘルスチェック スレッド本体
 * ==================================================================== */
static void health_thread_func(void *arg)
{
    potr_context *ctx = (potr_context *)arg;
    potr_internal_packet_session_hdr shdr;
    int is_oneway_udp = potr_is_oneway_udp_type(ctx->service.type);
    uint64_t initial_ping_due_ms = com_util_get_monotonic_ms() + (uint64_t)ctx->health_interval_ms;
    uint64_t last_logged_data_ms = 0U;

    shdr.service_id = ctx->service.service_id;
    /* 1:1 モード: session はコンテキストから取得 (N:1 は後述のループで各ピアから取得) */
    if (!ctx->is_multi_peer)
    {
        shdr.session_id = ctx->session_id;
        potr_session_ts_to_hdr(&ctx->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);
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

            if (!ctx->health_running[0])
                break;
        }

        /* PING を送信する */
        if (ctx->is_multi_peer)
        {
            /* N:1 モード: アクティブ全ピアへ PING を送信する */
            int i;

            com_util_local_lock_lock(ctx->peers_mutex, COM_UTIL_SYNC_WAIT_FOREVER);

            for (i = 0; i < ctx->max_peers && ctx->health_running[0]; i++)
            {
                potr_packet ping_pkt;
                potr_internal_packet_session_hdr peer_shdr;
                uint32_t seq;
                size_t wire_len;
                uint8_t health_states[POTR_MAX_PATH];
                int k;

                if (!ctx->peers[i].active)
                    continue;

                peer_shdr.service_id = ctx->service.service_id;
                peer_shdr.session_id = ctx->peers[i].session_id;
                potr_session_ts_to_hdr(&ctx->peers[i].session_ts, &peer_shdr.session_tv_sec,
                                       &peer_shdr.session_tv_nsec);

                com_util_local_lock_lock(ctx->peers[i].send_window_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
                seq = ctx->peers[i].send_window.next_seq;
                /* N:1 (UNICAST_BIDIR_N1) は双方向 PING。ピアごとの自端パス受信状態をペイロードに設定する。 */
                potr_copy_path_ping_state(health_states, ctx->peers[i].path_ping_state, POTR_MAX_PATH);
                potr_internal_packet_build_ping(&ping_pkt, &peer_shdr, seq, health_states, (uint16_t)POTR_MAX_PATH);
                com_util_local_lock_unlock(ctx->peers[i].send_window_mutex);

                if (ctx->service.encrypt_enabled)
                {
                    uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE];
                    uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
                    size_t enc_out = POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE;

                    ping_pkt.flags |= com_util_hton16(POTR_FLAG_ENCRYPTED);
                    ping_pkt.payload_len = com_util_hton16((uint16_t)(POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE));

                    memcpy(nonce, &ping_pkt.session_id, 4);
                    memcpy(nonce + 4, &ping_pkt.flags, 2);
                    memcpy(nonce + 6, &ping_pkt.seq_num, 4);
                    memset(nonce + 10, 0, 2);

                    memcpy(wire_buf, &ping_pkt, PACKET_HEADER_SIZE);
                    memcpy(wire_buf + PACKET_HEADER_SIZE, health_states, POTR_MAX_PATH);
                    if (com_util_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out, wire_buf + PACKET_HEADER_SIZE,
                                         POTR_MAX_PATH, ctx->service.encrypt_key, nonce, wire_buf,
                                         PACKET_HEADER_SIZE) != COM_UTIL_OK)
                    {
                        continue;
                    }
                    wire_len = PACKET_HEADER_SIZE + enc_out;

                    for (k = 0; k < (int)POTR_MAX_PATH; k++)
                    {
                        size_t sent = 0;

                        if (potr_endpoint_is_unset(&ctx->peers[i].dest_addr[k]))
                            continue;
                        (void)com_util_socket_sendto(ctx->sock[k], wire_buf, wire_len, &ctx->peers[i].dest_addr[k],
                                                     &sent, NULL);
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
                        size_t sent = 0;

                        if (potr_endpoint_is_unset(&ctx->peers[i].dest_addr[k]))
                            continue;
                        (void)com_util_socket_sendto(ctx->sock[k], wire_buf, wire_len, &ctx->peers[i].dest_addr[k],
                                                     &sent, NULL);
                    }
                }

                POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE, "health[service_id=%" PRId64 "]: PING peer=%u seq=%u",
                           ctx->service.service_id, (unsigned)ctx->peers[i].peer_id, (unsigned)seq);
            }

            com_util_local_lock_unlock(ctx->peers_mutex);
        }
        else
        {
            /* 1:1 UDP/マルチキャスト: 単一宛先へ PING を送信する */
            potr_packet ping_pkt;
            uint32_t seq;
            size_t wire_len;
            int build_result;
            int sent_any = 0;
            uint8_t health_states[POTR_MAX_PATH];
            int k;

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

            com_util_local_lock_lock(ctx->send_window_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
            seq = ctx->send_window.next_seq;
            build_result = potr_internal_packet_build_ping(&ping_pkt, &shdr, seq, health_states, (uint16_t)POTR_MAX_PATH);
            com_util_local_lock_unlock(ctx->send_window_mutex);

            if (build_result != POTR_OK)
            {
                continue;
            }

            POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE, "health[service_id=%" PRId64 "]: PING seq=%u",
                       ctx->service.service_id, (unsigned)seq);

            if (ctx->service.encrypt_enabled)
            {
                uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE];
                uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
                size_t enc_out = POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE;

                ping_pkt.flags |= com_util_hton16(POTR_FLAG_ENCRYPTED);
                ping_pkt.payload_len = com_util_hton16((uint16_t)(POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE));

                memcpy(nonce, &ping_pkt.session_id, 4);
                memcpy(nonce + 4, &ping_pkt.flags, 2);
                memcpy(nonce + 6, &ping_pkt.seq_num, 4);
                memset(nonce + 10, 0, 2);

                memcpy(wire_buf, &ping_pkt, PACKET_HEADER_SIZE);
                memcpy(wire_buf + PACKET_HEADER_SIZE, health_states, POTR_MAX_PATH);
                if (com_util_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out, wire_buf + PACKET_HEADER_SIZE,
                                     POTR_MAX_PATH, ctx->service.encrypt_key, nonce, wire_buf,
                                     PACKET_HEADER_SIZE) != COM_UTIL_OK)
                {
                    continue;
                }
                wire_len = PACKET_HEADER_SIZE + enc_out;

                for (k = 0; k < ctx->n_path; k++)
                {
                    size_t sent = 0;
                    int send_ret;

                    send_ret =
                        com_util_socket_sendto(ctx->sock[k], wire_buf, wire_len, &ctx->dest_addr[k], &sent, NULL);
                    if (send_ret == COM_UTIL_OK)
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
                    size_t sent = 0;
                    int send_ret;

                    send_ret =
                        com_util_socket_sendto(ctx->sock[k], wire_buf, wire_len, &ctx->dest_addr[k], &sent, NULL);
                    if (send_ret == COM_UTIL_OK)
                    {
                        sent_any = 1;
                    }
                }
            }

            if (is_oneway_udp && sent_any)
            {
                ctx->last_ping_send_ms = com_util_get_monotonic_ms();
                last_logged_data_ms = 0U;
            }
        }
    }

    return;
}

/* ====================================================================
 * TCP 用ヘルスチェック スレッド本体 (path ごと)
 * ==================================================================== */
static void tcp_health_thread_func(void *arg)
{
    potr_internal_path_thread_arg *harg = (potr_internal_path_thread_arg *)arg;
    potr_context *ctx = harg->ctx;
    int path_idx = harg->path_idx;

    POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE, "tcp_health[service_id=%" PRId64 " path=%d]: starting",
               ctx->service.service_id, path_idx);

    while (ctx->health_running[path_idx])
    {
        /* 固定間隔でスリープ */
        health_sleep(ctx, path_idx, ctx->health_interval_ms);

        if (!ctx->health_running[path_idx])
            break;
        (void)tcp_send_ping_packet(ctx, path_idx);
    }

    POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE, "tcp_health[service_id=%" PRId64 " path=%d]: exited",
               ctx->service.service_id, path_idx);

    return;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_tcp_send_ping_now(potr_context *ctx, int path_idx)
{
    return tcp_send_ping_packet(ctx, path_idx);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_health_thread_start(potr_context *ctx)
{
    if (ctx == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (ctx->health_interval_ms == 0)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
                   "health_thread[service_id=%" PRId64 "]: disabled (health_interval_ms=0)", ctx->service.service_id);
        return POTR_OK;
    }

    POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE, "health_thread[service_id=%" PRId64 "]: starting (interval=%ums)",
               ctx->service.service_id, (unsigned)ctx->health_interval_ms);

    com_util_local_lock_create(&ctx->health_mutex[0]);
    com_util_condvar_create(&ctx->health_wakeup[0]);

    ctx->health_running[0] = 1;

    if (com_util_thread_create(&ctx->health_thread[0], health_thread_func, ctx) != COM_UTIL_OK)
    {
        ctx->health_running[0] = 0;
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR, "health_thread[service_id=%" PRId64 "]: thread create failed",
                   ctx->service.service_id);
        /* com_util のスレッド生成失敗には、porter の分類へ変換できる詳細コードがありません。 */
        return POTR_ERR_UNKNOWN;
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_health_thread_stop(potr_context *ctx)
{
    if (ctx == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (!ctx->health_running[0])
    {
        return POTR_OK;
    }

    ctx->health_running[0] = 0;

    com_util_local_lock_lock(ctx->health_mutex[0], COM_UTIL_SYNC_WAIT_FOREVER);
    com_util_condvar_signal(ctx->health_wakeup[0]);
    com_util_local_lock_unlock(ctx->health_mutex[0]);

    com_util_thread_join(ctx->health_thread[0], COM_UTIL_SYNC_WAIT_FOREVER);

    com_util_condvar_destroy(ctx->health_wakeup[0]);
    com_util_local_lock_destroy(ctx->health_mutex[0]);

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_health_thread_wake(potr_context *ctx)
{
    signal_health_thread(ctx, 0);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_tcp_health_thread_start(potr_context *ctx, int path_idx)
{
    if (ctx == NULL || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (ctx->health_interval_ms == 0)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE, "tcp_health_thread[service_id=%" PRId64 " path=%d]: disabled",
                   ctx->service.service_id, path_idx);
        return POTR_OK;
    }

    ctx->health_args[path_idx].ctx = ctx;
    ctx->health_args[path_idx].path_idx = path_idx;

    ctx->health_running[path_idx] = 1;

    if (com_util_thread_create(&ctx->health_thread[path_idx], tcp_health_thread_func, &ctx->health_args[path_idx]) !=
        COM_UTIL_OK)
    {
        ctx->health_running[path_idx] = 0;
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                   "tcp_health_thread[service_id=%" PRId64 " path=%d]: thread create failed", ctx->service.service_id,
                   path_idx);
        /* com_util のスレッド生成失敗には、porter の分類へ変換できる詳細コードがありません。 */
        return POTR_ERR_UNKNOWN;
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_tcp_health_thread_stop(potr_context *ctx, int path_idx)
{
    if (ctx == NULL || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (!ctx->health_running[path_idx])
    {
        return POTR_OK;
    }

    ctx->health_running[path_idx] = 0;

    com_util_local_lock_lock(ctx->health_mutex[path_idx], COM_UTIL_SYNC_WAIT_FOREVER);
    com_util_condvar_signal(ctx->health_wakeup[path_idx]);
    com_util_local_lock_unlock(ctx->health_mutex[path_idx]);

    com_util_thread_join(ctx->health_thread[path_idx], COM_UTIL_SYNC_WAIT_FOREVER);

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_tcp_health_thread_wake(potr_context *ctx, int path_idx)
{
    signal_health_thread(ctx, path_idx);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_tcp_health_thread_wake_all(potr_context *ctx)
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
