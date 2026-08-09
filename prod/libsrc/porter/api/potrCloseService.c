/**
 *******************************************************************************
 *  @file           potrCloseService.c
 *  @brief          porter サービスを終了する potrCloseService 関数を提供します。
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

#include <porter/porter_spec.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>

#include <com_util/crypto/crypto.h>
#include <com_util/runtime/memory_lock.h>
#include <porter/infra/potrTrace.h>
#include <porter/infra/potrSendQueue.h>
#include <porter/infra/potrTcpControl.h>
#include <porter/potrContext.h>
#include <porter/potrPathEvent.h>
#include <porter/potrPeerTable.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/window.h>
#include <porter/thread/potrConnectThread.h>
#include <porter/thread/potrHealthThread.h>
#include <porter/thread/potrRecvThread.h>
#include <porter/thread/potrSendThread.h>
#include <porter/util/potrIpAddr.h>

/* 平文と鍵を保持する動的バッファーを消去して解放する。
 * crypto_buf は復号済み平文、compress_buf と frag_buf と recv_buf は
 * 展開後または結合後の平文を保持しうるため、解放前に消去する。 */
static void dispose_secret_buffers(PotrContext *ctx)
{
    if (ctx->frag_buf != NULL)
    {
        com_util_secure_zero(ctx->frag_buf, ctx->global.max_message_size);
        free(ctx->frag_buf);
        ctx->frag_buf = NULL;
    }
    if (ctx->compress_buf != NULL)
    {
        com_util_secure_zero(ctx->compress_buf, ctx->compress_buf_size);
        free(ctx->compress_buf);
        ctx->compress_buf = NULL;
    }
    if (ctx->crypto_buf != NULL)
    {
        com_util_secure_zero(ctx->crypto_buf, ctx->crypto_buf_size);
        free(ctx->crypto_buf);
        ctx->crypto_buf = NULL;
    }
    if (ctx->recv_buf != NULL)
    {
        com_util_secure_zero(ctx->recv_buf, PACKET_HEADER_SIZE + ctx->global.max_payload);
        free(ctx->recv_buf);
        ctx->recv_buf = NULL;
    }

    com_util_secure_zero(ctx->service.encrypt_key, sizeof(ctx->service.encrypt_key));
}

/* FIN パケットを全パスへ送信する */
static void send_fin(PotrContext *ctx)
{
    PotrPacket fin_pkt;
    PotrPacketSessionHdr shdr;
    uint32_t wire_target_seq = 0U;
    int has_data = 0;
    size_t wire_len;
    int i;

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = ctx->session_id;
    potr_session_ts_to_hdr(&ctx->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);

    if (packet_build_fin(&fin_pkt, &shdr) != POTR_OK)
        return;

    /* 現セッションで DATA を送っている場合のみ FIN target を有効化する。
     * ack_num は send_window.next_seq をそのまま運び、0 も通常値として扱う。 */
    com_util_local_lock_lock(ctx->send_window_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
    wire_target_seq = ctx->send_window.next_seq;
    has_data = ctx->send_has_data;
    com_util_local_lock_unlock(ctx->send_window_mutex);

    if (has_data)
    {
        fin_pkt.flags |= potr_hton16(POTR_FLAG_FIN_TARGET_VALID);
        fin_pkt.ack_num = potr_hton32(wire_target_seq);
    }

    if (ctx->service.encrypt_enabled)
    {
        uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t enc_out = POTR_CRYPTO_TAG_SIZE;

        fin_pkt.flags |= potr_hton16(POTR_FLAG_ENCRYPTED);
        fin_pkt.payload_len = potr_hton16((uint16_t)POTR_CRYPTO_TAG_SIZE);

        /* ノンス: session_id(4B) + flags(2B, FIN|ENCRYPTED NBO) + 0(4B) + padding(2B) */
        memcpy(nonce, &fin_pkt.session_id, 4);
        memcpy(nonce + 4, &fin_pkt.flags, 2);
        memset(nonce + 6, 0, 4);
        memset(nonce + 10, 0, 2);

        memcpy(wire_buf, &fin_pkt, PACKET_HEADER_SIZE);
        if (com_util_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out, NULL, 0, ctx->service.encrypt_key, nonce,
                             wire_buf, PACKET_HEADER_SIZE) != COM_UTIL_OK)
        {
            return;
        }
        wire_len = PACKET_HEADER_SIZE + enc_out;

        for (i = 0; i < ctx->n_path; i++)
        {
            if (ctx->sock[i] == POTR_INVALID_SOCKET)
                continue;
            potr_sendto(ctx->sock[i], wire_buf, wire_len, 0, (const struct sockaddr *)&ctx->dest_addr[i],
                        (int)sizeof(ctx->dest_addr[i]), NULL);
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
                        (const struct sockaddr *)&ctx->dest_addr[i], (int)sizeof(ctx->dest_addr[i]), NULL);
        }
    }
}

static uint32_t get_fin_target_seq(PotrContext *ctx, int *has_data)
{
    uint32_t wire_target_seq;

    com_util_local_lock_lock(ctx->send_window_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
    wire_target_seq = ctx->send_window.next_seq;
    if (has_data != NULL)
    {
        *has_data = ctx->send_has_data;
    }
    com_util_local_lock_unlock(ctx->send_window_mutex);

    return wire_target_seq;
}

static int send_tcp_fin(PotrContext *ctx, uint32_t fin_target_seq)
{
    PotrPacket fin_pkt;
    PotrPacketSessionHdr shdr;
    int result;

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = ctx->session_id;
    potr_session_ts_to_hdr(&ctx->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);

    result = packet_build_fin(&fin_pkt, &shdr);
    if (result != POTR_OK)
    {
        return result;
    }

    fin_pkt.flags |= potr_hton16(POTR_FLAG_FIN_TARGET_VALID);
    fin_pkt.ack_num = potr_hton32(fin_target_seq);

    return potr_tcp_send_control_packet(ctx, &fin_pkt, 0U);
}

static void reset_tcp_close_wait(PotrContext *ctx)
{
    com_util_local_lock_lock(ctx->tcp_close_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
    ctx->tcp_close_waiting_ack = 0;
    ctx->tcp_close_ack_received = 0;
    ctx->tcp_close_wait_target_seq = 0U;
    ctx->tcp_close_ack_seq = 0U;
    com_util_local_lock_unlock(ctx->tcp_close_mutex);
}

static void begin_tcp_close_wait(PotrContext *ctx, uint32_t fin_target_seq)
{
    com_util_local_lock_lock(ctx->tcp_close_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
    ctx->tcp_close_waiting_ack = 1;
    ctx->tcp_close_ack_received = 0;
    ctx->tcp_close_wait_target_seq = fin_target_seq;
    ctx->tcp_close_ack_seq = 0U;
    com_util_local_lock_unlock(ctx->tcp_close_mutex);
}

static int wait_for_tcp_close_ack(PotrContext *ctx, int timeout_ms)
{
    int result = POTR_OK;
    int wait_result;

    if (timeout_ms == 0)
    {
        reset_tcp_close_wait(ctx);
        return POTR_OK;
    }

    com_util_local_lock_lock(ctx->tcp_close_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
    while (ctx->tcp_close_waiting_ack && !ctx->tcp_close_ack_received)
    {
        wait_result = com_util_condvar_wait(ctx->tcp_close_cv, ctx->tcp_close_mutex, timeout_ms);
        if (wait_result != COM_UTIL_OK)
        {
            if (wait_result == COM_UTIL_ERR_TIMEOUT)
            {
                result = POTR_ERR_TIMEOUT;
            }
            else
            {
                /* com_util の同期失敗には、porter の分類へ変換できる詳細コードがありません。 */
                result = POTR_ERR_UNKNOWN;
            }
            break;
        }
    }
    if (ctx->tcp_close_waiting_ack && !ctx->tcp_close_ack_received)
    {
        result = POTR_ERR_TIMEOUT;
    }
    ctx->tcp_close_waiting_ack = 0;
    ctx->tcp_close_ack_received = 0;
    ctx->tcp_close_wait_target_seq = 0U;
    ctx->tcp_close_ack_seq = 0U;
    com_util_local_lock_unlock(ctx->tcp_close_mutex);

    return result;
}

static void stop_tcp_health_threads(PotrContext *ctx)
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

/* Doxygen コメントは、ヘッダーに記載 */

int potrCloseService(PotrContext *handle)
{
    PotrContext *ctx = (PotrContext *)handle;
    int ret = POTR_OK;

    if (ctx == NULL)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR, "potrCloseService: handle is NULL");
        return POTR_ERR_INVALID_ARGUMENT;
    }

    POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO, "potrCloseService: service_id=%" PRId64 " closing", ctx->service.service_id);
    ctx->close_requested = 1;

    /* TCP: 接続管理スレッドを停止する (send/recv/health スレッドは connect スレッド内で停止) */
    if (potr_is_tcp_type(ctx->service.type))
    {
        if (ctx->send_thread_running)
        {
            uint32_t fin_target_seq;

            stop_tcp_health_threads(ctx);

            POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
                       "potrCloseService: service_id=%" PRId64 " flushing send queue and sending TCP FIN",
                       ctx->service.service_id);
            potr_send_queue_wait_drained(&ctx->send_queue);
            fin_target_seq = get_fin_target_seq(ctx, NULL);

            if (ctx->tcp_active_paths > 0)
            {
                int close_result;

                begin_tcp_close_wait(ctx, fin_target_seq);
                close_result = send_tcp_fin(ctx, fin_target_seq);
                if (close_result != POTR_OK)
                {
                    POTR_TRACE(COM_UTIL_TRACE_LEVEL_WARNING,
                               "potrCloseService: service_id=%" PRId64 " TCP FIN send failed", ctx->service.service_id);
                    reset_tcp_close_wait(ctx);
                    ret = close_result;
                }
                else
                {
                    /* タイムアウト ms は設定値。実用範囲は INT_MAX 以下 */
                    close_result = wait_for_tcp_close_ack(ctx, (int)ctx->global.tcp_close_timeout_ms);
                    if (close_result != POTR_OK)
                    {
                        POTR_TRACE(COM_UTIL_TRACE_LEVEL_WARNING,
                                   "potrCloseService: service_id=%" PRId64 " TCP FIN_ACK wait failed (%ums)",
                                   ctx->service.service_id, (unsigned)ctx->global.tcp_close_timeout_ms);
                        ret = close_result;
                    }
                    else
                    {
                        POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
                                   "potrCloseService: service_id=%" PRId64 " TCP FIN_ACK received",
                                   ctx->service.service_id);
                    }
                }
            }
            else if (ctx->send_has_data)
            {
                POTR_TRACE(COM_UTIL_TRACE_LEVEL_WARNING,
                           "potrCloseService: service_id=%" PRId64 " TCP close without active path",
                           ctx->service.service_id);
                ret = POTR_ERR_DISCONNECTED;
            }
            else
            {
                reset_tcp_close_wait(ctx);
            }
        }

        POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
                   "potrCloseService: service_id=%" PRId64 " stopping connect thread (TCP)", ctx->service.service_id);
        potr_connect_thread_stop(ctx);

        /* 送信キューを破棄 (SENDER / TCP_BIDIR のみ) */
        if (ctx->role == POTR_ROLE_SENDER || ctx->service.type == POTR_TYPE_TCP_BIDIR)
        {
            potr_send_queue_dispose(&ctx->send_queue);
        }

        /* TCP mutex / condvar を解放 */
        {
            int i;
            com_util_local_lock_destroy(ctx->tcp_state_mutex);
            com_util_condvar_destroy(ctx->tcp_state_cv);
            com_util_local_lock_destroy(ctx->tcp_close_mutex);
            com_util_condvar_destroy(ctx->tcp_close_cv);
            for (i = 0; i < (int)POTR_MAX_PATH; i++)
            {
                com_util_local_lock_destroy(ctx->tcp_send_mutex[i]);
                com_util_local_lock_destroy(ctx->health_mutex[i]);
                com_util_condvar_destroy(ctx->health_wakeup[i]);
            }
            com_util_local_lock_destroy(ctx->recv_window_mutex);
        }

        /* 送受信ウィンドウと動的バッファーを解放 */
        window_dispose(&ctx->send_window);
        window_dispose(&ctx->recv_window);
        dispose_secret_buffers(ctx);
        free(ctx->send_wire_buf);

        potr_socket_lib_cleanup();

        POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO, "potrCloseService: service closed (TCP)");
        potr_callback_mutex_dispose(ctx);
        free(ctx);
        return ret;
    }

    /* 非 TCP: ヘルスチェック スレッドを停止 (送信者のみ) */
    if (ctx->health_running[0])
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE, "potrCloseService: service_id=%" PRId64 " stopping health thread",
                   ctx->service.service_id);
        potr_health_thread_stop(ctx);
    }

    ctx->last_ping_send_ms = 0U;
    ctx->last_valid_data_send_ms = 0U;

    /* 送信スレッドを停止してキューを破棄 (送信者のみ) */
    if (ctx->send_thread_running)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
                   "potrCloseService: service_id=%" PRId64 " flushing send queue and sending FIN",
                   ctx->service.service_id);
        potr_send_queue_wait_drained(&ctx->send_queue);
        if (ctx->is_multi_peer)
        {
            /* N:1: 全アクティブ ピアへ FIN を送信してピア テーブルを破棄 */
            peer_table_dispose(ctx);
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
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE, "potrCloseService: service_id=%" PRId64 " stopping recv thread",
                       ctx->service.service_id);
            comm_recv_thread_stop(ctx);
        }
        potr_send_thread_stop(ctx);
        potr_send_queue_dispose(&ctx->send_queue);
    }

    /* 送信スレッド未起動の受信専用サービスではここで受信スレッドを停止する */
    if (ctx->running[0])
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE, "potrCloseService: service_id=%" PRId64 " stopping recv thread",
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
                if (parse_ipv4_addr(ctx->service.multicast_group, &mreq.imr_multiaddr) == POTR_OK)
                {
                    mreq.imr_interface = ctx->src_addr_resolved[i];
                    potr_setsockopt(ctx->sock[i], IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq), NULL);
                }
            }
            potr_close_socket(ctx->sock[i]);
            ctx->sock[i] = POTR_INVALID_SOCKET;
        }
    }

    potr_socket_lib_cleanup();

    /* 送受信ウィンドウと動的バッファーを解放 */
    if (!ctx->is_multi_peer)
    {
        /* 1:1 モード: コンテキスト レベルのウィンドウを解放する */
        window_dispose(&ctx->send_window);
        window_dispose(&ctx->recv_window);
    }
    else if (ctx->peers != NULL)
    {
        /* N:1 モード: 送信スレッド未起動の場合もピア テーブルを解放する
           (すでに peer_table_dispose 済みの場合は ctx->peers が NULL になっている) */
        peer_table_dispose(ctx);
    }
    dispose_secret_buffers(ctx);
    free(ctx->send_wire_buf);
    POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO, "potrCloseService: service closed");

    potr_callback_mutex_dispose(ctx);
    free(ctx);
    return POTR_OK;
}
