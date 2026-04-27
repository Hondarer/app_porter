/**
 *******************************************************************************
 *  @file           potrCloseService.c
 *  @brief          potrCloseService 関数の実装。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <com_util/base/platform.h>

#include <porter.h>
#include <porter_const.h>

#include <com_util/crypto/crypto.h>
#include "../infra/potrTrace.h"
#include "../infra/potrSendQueue.h"
#include "../potrContext.h"
#include "../potrPathEvent.h"
#include "../potrPeerTable.h"
#include "../protocol/packet.h"
#include "../protocol/window.h"
#include "../thread/potrConnectThread.h"
#include "../thread/potrHealthThread.h"
#include "../thread/potrRecvThread.h"
#include "../thread/potrSendThread.h"
#include "../util/potrIpAddr.h"

/* FIN パケットを全パスへ送信する */
static void send_fin(struct PotrContext_ *ctx)
{
    PotrPacket fin_pkt;
    PotrPacketSessionHdr shdr;
    uint32_t wire_target_seq = 0U;
    int has_data = 0;
    size_t wire_len;
    int i;

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = ctx->session_id;
    shdr.session_tv_sec = ctx->session_tv_sec;
    shdr.session_tv_nsec = ctx->session_tv_nsec;

    if (packet_build_fin(&fin_pkt, &shdr) != POTR_SUCCESS)
        return;

    /* 現セッションで DATA を送っている場合のみ FIN target を有効化する。
     * ack_num は send_window.next_seq をそのまま運び、0 も通常値として扱う。 */
    COM_UTIL_MUTEX_LOCK(&ctx->send_window_mutex);
    wire_target_seq = ctx->send_window.next_seq;
    has_data        = ctx->send_has_data;
    COM_UTIL_MUTEX_UNLOCK(&ctx->send_window_mutex);

    if (has_data)
    {
        fin_pkt.flags  |= htons(POTR_FLAG_FIN_TARGET_VALID);
        fin_pkt.ack_num = htonl(wire_target_seq);
    }

    if (ctx->service.encrypt_enabled)
    {
        uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t enc_out = POTR_CRYPTO_TAG_SIZE;

        fin_pkt.flags |= htons(POTR_FLAG_ENCRYPTED);
        fin_pkt.payload_len = htons((uint16_t)POTR_CRYPTO_TAG_SIZE);

        /* ノンス: session_id(4B) + flags(2B, FIN|ENCRYPTED NBO) + 0(4B) + padding(2B) */
        memcpy(nonce, &fin_pkt.session_id, 4);
        memcpy(nonce + 4, &fin_pkt.flags, 2);
        memset(nonce + 6, 0, 4);
        memset(nonce + 10, 0, 2);

        memcpy(wire_buf, &fin_pkt, PACKET_HEADER_SIZE);
        if (com_util_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out, NULL, 0, ctx->service.encrypt_key, nonce, wire_buf,
                         PACKET_HEADER_SIZE) != 0)
        {
            return;
        }
        wire_len = PACKET_HEADER_SIZE + enc_out;

        for (i = 0; i < ctx->n_path; i++)
        {
            if (ctx->sock[i] == POTR_INVALID_SOCKET)
                continue;
            potr_sendto(ctx->sock[i], wire_buf, wire_len, 0,
                        (const struct sockaddr *)&ctx->dest_addr[i],
                        (int)sizeof(ctx->dest_addr[i]));
        }
    }
    else
    {
        wire_len = packet_wire_size(&fin_pkt);

        for (i = 0; i < ctx->n_path; i++)
        {
            if (ctx->sock[i] == POTR_INVALID_SOCKET)
                continue;
            potr_sendto(ctx->sock[i], (const uint8_t *)&fin_pkt, wire_len, 0,
                        (const struct sockaddr *)&ctx->dest_addr[i],
                        (int)sizeof(ctx->dest_addr[i]));
        }
    }
}

static uint32_t get_fin_target_seq(struct PotrContext_ *ctx, int *has_data)
{
    uint32_t wire_target_seq;

    COM_UTIL_MUTEX_LOCK(&ctx->send_window_mutex);
    wire_target_seq = ctx->send_window.next_seq;
    if (has_data != NULL)
    {
        *has_data = ctx->send_has_data;
    }
    COM_UTIL_MUTEX_UNLOCK(&ctx->send_window_mutex);

    return wire_target_seq;
}

static int tcp_send_all_locked(PotrSocket fd, com_util_mutex_t *mtx,
                               const uint8_t *buf, size_t len)
{
    int result;

    COM_UTIL_MUTEX_LOCK(mtx);
    result = potr_tcp_send(fd, buf, len);
    COM_UTIL_MUTEX_UNLOCK(mtx);

    return (result == 0) ? POTR_SUCCESS : POTR_ERROR;
}

static int send_tcp_control_packet(struct PotrContext_ *ctx, PotrPacket *pkt, uint32_t nonce_val)
{
    uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
    size_t  wire_len;
    int     sent_any = 0;
    int     i;

    if (ctx == NULL || pkt == NULL)
    {
        return POTR_ERROR;
    }

    if (ctx->service.encrypt_enabled)
    {
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t  enc_out = POTR_CRYPTO_TAG_SIZE;
        uint32_t nonce_nbo = htonl(nonce_val);

        pkt->flags      |= htons(POTR_FLAG_ENCRYPTED);
        pkt->payload_len = htons((uint16_t)POTR_CRYPTO_TAG_SIZE);

        memcpy(nonce,      &pkt->session_id, 4);
        memcpy(nonce + 4,  &pkt->flags,      2);
        memcpy(nonce + 6,  &nonce_nbo,       4);
        memset(nonce + 10, 0,                2);

        memcpy(wire_buf, pkt, PACKET_HEADER_SIZE);
        if (com_util_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out,
                         NULL, 0,
                         ctx->service.encrypt_key,
                         nonce,
                         wire_buf, PACKET_HEADER_SIZE) != 0)
        {
            return POTR_ERROR;
        }
        wire_len = PACKET_HEADER_SIZE + enc_out;
    }
    else
    {
        memcpy(wire_buf, pkt, PACKET_HEADER_SIZE);
        wire_len = PACKET_HEADER_SIZE;
    }

    for (i = 0; i < ctx->n_path; i++)
    {
        if (ctx->tcp_conn_fd[i] == POTR_INVALID_SOCKET)
        {
            continue;
        }
        if (tcp_send_all_locked(ctx->tcp_conn_fd[i], &ctx->tcp_send_mutex[i],
                                wire_buf, wire_len) == POTR_SUCCESS)
        {
            sent_any = 1;
        }
    }

    return sent_any ? POTR_SUCCESS : POTR_ERROR;
}

static int send_tcp_fin(struct PotrContext_ *ctx, uint32_t fin_target_seq)
{
    PotrPacket           fin_pkt;
    PotrPacketSessionHdr shdr;

    shdr.service_id      = ctx->service.service_id;
    shdr.session_id      = ctx->session_id;
    shdr.session_tv_sec  = ctx->session_tv_sec;
    shdr.session_tv_nsec = ctx->session_tv_nsec;

    if (packet_build_fin(&fin_pkt, &shdr) != POTR_SUCCESS)
    {
        return POTR_ERROR;
    }

    fin_pkt.flags  |= htons(POTR_FLAG_FIN_TARGET_VALID);
    fin_pkt.ack_num = htonl(fin_target_seq);

    return send_tcp_control_packet(ctx, &fin_pkt, 0U);
}

static void reset_tcp_close_wait(struct PotrContext_ *ctx)
{
    COM_UTIL_MUTEX_LOCK(&ctx->tcp_close_mutex);
    ctx->tcp_close_waiting_ack   = 0;
    ctx->tcp_close_ack_received  = 0;
    ctx->tcp_close_wait_target_seq = 0U;
    ctx->tcp_close_ack_seq       = 0U;
    COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_close_mutex);
}

static void begin_tcp_close_wait(struct PotrContext_ *ctx, uint32_t fin_target_seq)
{
    COM_UTIL_MUTEX_LOCK(&ctx->tcp_close_mutex);
    ctx->tcp_close_waiting_ack    = 1;
    ctx->tcp_close_ack_received   = 0;
    ctx->tcp_close_wait_target_seq = fin_target_seq;
    ctx->tcp_close_ack_seq        = 0U;
    COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_close_mutex);
}

static int wait_for_tcp_close_ack(struct PotrContext_ *ctx, uint32_t timeout_ms)
{
    int result = POTR_SUCCESS;

    if (timeout_ms == 0U)
    {
        reset_tcp_close_wait(ctx);
        return POTR_SUCCESS;
    }

    COM_UTIL_MUTEX_LOCK(&ctx->tcp_close_mutex);
    while (ctx->tcp_close_waiting_ack && !ctx->tcp_close_ack_received)
    {
        if (com_util_condvar_timedwait(&ctx->tcp_close_cv, &ctx->tcp_close_mutex, timeout_ms) != 0)
        {
            result = POTR_ERROR;
            break;
        }
    }
    if (ctx->tcp_close_waiting_ack && !ctx->tcp_close_ack_received)
    {
        result = POTR_ERROR;
    }
    ctx->tcp_close_waiting_ack   = 0;
    ctx->tcp_close_ack_received  = 0;
    ctx->tcp_close_wait_target_seq = 0U;
    ctx->tcp_close_ack_seq       = 0U;
    COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_close_mutex);

    return result;
}

static void stop_tcp_health_threads(struct PotrContext_ *ctx)
{
    int i;

    for (i = 0; i < ctx->n_path; i++)
    {
        if (ctx->health_running[i])
        {
            (void)potr_tcp_health_thread_stop(ctx, i);
        }
    }
}

/* doxygen コメントは、ヘッダに記載 */
POTR_EXPORT int POTR_API potrCloseService(PotrHandle handle)
{
    struct PotrContext_ *ctx = (struct PotrContext_ *)handle;
    int                  ret = POTR_SUCCESS;

    if (ctx == NULL)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR, "potrCloseService: handle is NULL");
        return POTR_ERROR;
    }

    POTR_LOG(COM_UTIL_LOG_LEVEL_INFO, "potrCloseService: service_id=%" PRId64 " closing", ctx->service.service_id);
    ctx->close_requested = 1;

    /* TCP: 接続管理スレッドを停止する (send/recv/health スレッドは connect スレッド内で停止) */
    if (potr_is_tcp_type(ctx->service.type))
    {
        if (ctx->send_thread_running)
        {
            uint32_t fin_target_seq;

            stop_tcp_health_threads(ctx);

            POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                     "potrCloseService: service_id=%" PRId64 " flushing send queue and sending TCP FIN",
                     ctx->service.service_id);
            potr_send_queue_wait_drained(&ctx->send_queue);
            fin_target_seq = get_fin_target_seq(ctx, NULL);

            if (ctx->tcp_active_paths > 0)
            {
                begin_tcp_close_wait(ctx, fin_target_seq);
                if (send_tcp_fin(ctx, fin_target_seq) != POTR_SUCCESS)
                {
                    POTR_LOG(COM_UTIL_LOG_LEVEL_WARNING,
                             "potrCloseService: service_id=%" PRId64 " TCP FIN send failed",
                             ctx->service.service_id);
                    reset_tcp_close_wait(ctx);
                    ret = POTR_ERROR;
                }
                else if (wait_for_tcp_close_ack(ctx, ctx->global.tcp_close_timeout_ms) != POTR_SUCCESS)
                {
                    POTR_LOG(COM_UTIL_LOG_LEVEL_WARNING,
                             "potrCloseService: service_id=%" PRId64 " TCP FIN_ACK wait timed out (%ums)",
                             ctx->service.service_id,
                             (unsigned)ctx->global.tcp_close_timeout_ms);
                    ret = POTR_ERROR;
                }
                else
                {
                    POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,
                             "potrCloseService: service_id=%" PRId64 " TCP FIN_ACK received",
                             ctx->service.service_id);
                }
            }
            else if (ctx->send_has_data)
            {
                POTR_LOG(COM_UTIL_LOG_LEVEL_WARNING,
                         "potrCloseService: service_id=%" PRId64 " TCP close without active path",
                         ctx->service.service_id);
                ret = POTR_ERROR;
            }
            else
            {
                reset_tcp_close_wait(ctx);
            }
        }

        POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE, "potrCloseService: service_id=%" PRId64 " stopping connect thread (TCP)",
                 ctx->service.service_id);
        potr_connect_thread_stop(ctx);

        /* 送信キューを破棄 (SENDER / TCP_BIDIR のみ) */
        if (ctx->role == POTR_ROLE_SENDER || ctx->service.type == POTR_TYPE_TCP_BIDIR)
        {
            potr_send_queue_destroy(&ctx->send_queue);
        }

        /* TCP mutex / condvar を解放 */
        {
            int i;
            COM_UTIL_MUTEX_DESTROY(&ctx->tcp_state_mutex);
            COM_UTIL_COND_DESTROY(&ctx->tcp_state_cv);
            COM_UTIL_MUTEX_DESTROY(&ctx->tcp_close_mutex);
            COM_UTIL_COND_DESTROY(&ctx->tcp_close_cv);
            for (i = 0; i < (int)POTR_MAX_PATH; i++)
            {
                COM_UTIL_MUTEX_DESTROY(&ctx->tcp_send_mutex[i]);
                COM_UTIL_MUTEX_DESTROY(&ctx->health_mutex[i]);
                COM_UTIL_COND_DESTROY(&ctx->health_wakeup[i]);
            }
            COM_UTIL_MUTEX_DESTROY(&ctx->recv_window_mutex);
        }

        /* 送受信ウィンドウと動的バッファを解放 */
        window_destroy(&ctx->send_window);
        window_destroy(&ctx->recv_window);
        free(ctx->frag_buf);
        free(ctx->compress_buf);
        free(ctx->crypto_buf);
        free(ctx->recv_buf);
        free(ctx->send_wire_buf);

    potr_socket_lib_cleanup();

        POTR_LOG(COM_UTIL_LOG_LEVEL_INFO, "potrCloseService: service closed (TCP)");
        potr_callback_mutex_destroy(ctx);
        free(ctx);
        return ret;
    }

    /* 非 TCP: ヘルスチェックスレッドを停止 (送信者のみ) */
    if (ctx->health_running[0])
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE, "potrCloseService: service_id=%" PRId64 " stopping health thread",
                 ctx->service.service_id);
        potr_health_thread_stop(ctx);
    }

    ctx->last_ping_send_ms       = 0U;
    ctx->last_valid_data_send_ms = 0U;

    /* 送信スレッドを停止してキューを破棄 (送信者のみ) */
    if (ctx->send_thread_running)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE, "potrCloseService: service_id=%" PRId64 " flushing send queue and sending FIN",
                 ctx->service.service_id);
        potr_send_queue_wait_drained(&ctx->send_queue);
        if (ctx->is_multi_peer)
        {
            /* N:1: 全アクティブピアへ FIN を送信してピアテーブルを破棄 */
            peer_table_destroy(ctx);
        }
        else
        {
            /* 1:1: 単一宛先へ FIN を送信 */
            send_fin(ctx);
        }
        /* recv_thread は NACK 処理で send_window_mutex を参照する。
         * send_thread_stop() が send_window_mutex を破棄する前に停止する必要がある。 */
        if (ctx->running[0])
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE, "potrCloseService: service_id=%" PRId64 " stopping recv thread",
                     ctx->service.service_id);
            comm_recv_thread_stop(ctx);
        }
        potr_send_thread_stop(ctx);
        potr_send_queue_destroy(&ctx->send_queue);
    }

    /* 送信スレッド未起動の受信専用サービスではここで受信スレッドを停止する */
    if (ctx->running[0])
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE, "potrCloseService: service_id=%" PRId64 " stopping recv thread",
                 ctx->service.service_id);
        comm_recv_thread_stop(ctx);
    }

    /* ソケットをクローズ (recv スレッドの有無によらず実施)。
       Windows: comm_recv_thread_stop 内で closesocket 済みの場合は INVALID_SOCKET になっているため
                guard により二重 close を回避する。
       Linux: comm_recv_thread_stop は shutdown のみで close しないため、ここで必ず close する。 */
    {
        int i;
        for (i = 0; i < ctx->n_path; i++)
        {
            if (ctx->sock[i] == POTR_INVALID_SOCKET)
                continue;
            if (potr_raw_base_type(ctx->service.type) == POTR_TYPE_MULTICAST)
            {
                struct ip_mreq mreq;
                memset(&mreq, 0, sizeof(mreq));
                if (parse_ipv4_addr(ctx->service.multicast_group, &mreq.imr_multiaddr) == POTR_SUCCESS)
                {
                    mreq.imr_interface = ctx->src_addr_resolved[i];
                    potr_setsockopt(ctx->sock[i], IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
                }
            }
            potr_close_socket(ctx->sock[i]);
            ctx->sock[i] = POTR_INVALID_SOCKET;
        }
    }

    potr_socket_lib_cleanup();

    /* 送受信ウィンドウと動的バッファを解放 */
    if (!ctx->is_multi_peer)
    {
        /* 1:1 モード: コンテキストレベルのウィンドウを解放する */
        window_destroy(&ctx->send_window);
        window_destroy(&ctx->recv_window);
    }
    else if (ctx->peers != NULL)
    {
        /* N:1 モード: 送信スレッド未起動の場合もピアテーブルを解放する
           (すでに peer_table_destroy 済みの場合は ctx->peers が NULL になっている) */
        peer_table_destroy(ctx);
    }
    free(ctx->frag_buf);
    free(ctx->compress_buf);
    free(ctx->crypto_buf);
    free(ctx->recv_buf);
    free(ctx->send_wire_buf);
    POTR_LOG(COM_UTIL_LOG_LEVEL_INFO, "potrCloseService: service closed");

    potr_callback_mutex_destroy(ctx);
    free(ctx);
    return POTR_SUCCESS;
}
