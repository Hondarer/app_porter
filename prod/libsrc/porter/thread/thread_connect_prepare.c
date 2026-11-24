/**
 *******************************************************************************
 *  @file           thread_connect_prepare.c
 *  @brief          TCP 接続成立前の読み取り・照合と接続待機を実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/23
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/base/platform.h>
#include <cplat/crt/stdlib.h>
#include <cplat/runtime/memory_lock.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>

#include <porter/potr_context.h>
#include <porter/potr_path_event.h>
#include <porter/protocol/packet.h>
#include <porter/infra/potr_send_queue.h>
#include <porter/protocol/window.h>
#include <porter/infra/potr_trace.h>
#include <porter/infra/potr_result.h>
#include <porter/infra/potr_socket_io.h>
#include <porter/thread/potr_connect_thread.h>
#include <porter/thread/potr_connected_threads.h>
#include <porter/thread/potr_recv_thread.h>
#include <porter/thread/potr_send_thread.h>
#include <porter/thread/potr_health_thread.h>
#include <cplat/clock/clock.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/endpoint.h>
#include <cplat/net/socket.h>
#include "thread_recv_validate.h"

#include "thread_connect_prepare.h"

/* Doxygen コメントは、ヘッダーに記載 */
int thread_connect_read_first_packet(const cplat_socket fd, cplat_socket_watch *watch,
                                     cplat_socket_wait_cancel *cancel, uint8_t *buf, const size_t max_buf,
                                     size_t *len_out, const int timeout_ms)
{
    potr_internal_tcp_frame_reader reader;
    uint64_t deadline_ms = 0U;
    int has_deadline = 0;

    if (fd == CPLAT_INVALID_SOCKET || watch == NULL || buf == NULL || len_out == NULL ||
        max_buf < POTR_PACKET_HEADER_SIZE)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (timeout_ms != CPLAT_WAIT_FOREVER)
    {
        uint64_t now_ms;

        if (timeout_ms < 0)
        {
            return POTR_ERR_INVALID_ARGUMENT;
        }
        if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
        {
            return POTR_ERR_IO;
        }
        deadline_ms = now_ms + (uint64_t)timeout_ms;
        has_deadline = 1;
    }

    potr_internal_tcp_frame_reader_reset(&reader);

    for (;;)
    {
        int wait_ms = CPLAT_WAIT_FOREVER;
        unsigned char ready = 0U;
        cplat_error detail;
        int wait_result;
        int complete = 0;
        int step_result;

        if (has_deadline)
        {
            uint64_t now_ms;
            uint64_t remaining;

            if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
            {
                return POTR_ERR_IO;
            }
            if (now_ms >= deadline_ms)
            {
                return POTR_ERR_TIMEOUT;
            }
            remaining = deadline_ms - now_ms;
            if (remaining > (uint64_t)POTR_WAIT_MS_MAX)
            {
                remaining = (uint64_t)POTR_WAIT_MS_MAX;
            }
            wait_ms = (int)remaining;
        }

        cplat_error_clear(&detail);
        wait_result = cplat_socket_watch_wait_readable_multi(&watch, 1U, cancel, wait_ms, &ready, &detail);
        if (wait_result != CPLAT_OK)
        {
            return potr_internal_result_from_socket_result(wait_result, &detail);
        }

        cplat_error_clear(&detail);
        step_result = potr_internal_tcp_frame_reader_step(&reader, fd, buf, max_buf, &complete, &detail);
        if (step_result != POTR_OK)
        {
            return step_result;
        }

        if (complete != 0)
        {
            *len_out = reader.received;
            return POTR_OK;
        }
    }
}

/* Doxygen コメントは、ヘッダーに記載 */
tcp_session_cmp thread_connect_session_compare(const potr_context *ctx, const potr_packet *pkt)
{
    cplat_timespec pkt_session_ts;
    int ts_cmp;

    if (!ctx->tcp_accepted_session_known)
    {
        return TCP_SESSION_NEW;
    }

    session_ts_from_hdr(pkt->session_tv_sec, pkt->session_tv_nsec, &pkt_session_ts);
    ts_cmp = cplat_timespec_cmp(&pkt_session_ts, &ctx->tcp_accepted_session_ts);

    if (ts_cmp > 0)
    {
        return TCP_SESSION_NEW;
    }
    else if (ts_cmp < 0)
    {
        return TCP_SESSION_OLD;
    }
    else if (pkt->session_id > ctx->tcp_accepted_session_id)
    {
        return TCP_SESSION_NEW;
    }
    else if (pkt->session_id < ctx->tcp_accepted_session_id)
    {
        return TCP_SESSION_OLD;
    }

    return TCP_SESSION_SAME;
}

/* Doxygen コメントは、ヘッダーに記載 */
cplat_socket thread_connect_with_timeout(potr_context *ctx, const int path_idx, cplat_socket_wait_cancel *cancel,
                                         cplat_socket_watch **watch_out)
{
    cplat_socket sock;
    cplat_socket_watch *watch = NULL;
    cplat_ipv4_endpoint addr = {0};
    uint32_t timeout_ms = ctx->service.connect_timeout_ms;
    int connect_timeout_ms;
    int connect_result;
    cplat_error detail;

    if (watch_out != NULL)
    {
        *watch_out = NULL;
    }

    if (watch_out == NULL)
    {
        return CPLAT_INVALID_SOCKET;
    }

    if (cplat_socket_open(CPLAT_SOCKET_TCP, &sock, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail,
                                  "connect_thread[service_id=%" PRId64 " path=%d]: socket() failed",
                                  ctx->service.service_id, path_idx);
        return CPLAT_INVALID_SOCKET;
    }

    /*
     * 自動割り当て (src_port == 0) ではローカル ポートの重複を避けるため SO_REUSEADDR を有効化しない。
     * 固定送信元ポート (src_port != 0) の場合のみ、再接続時の互換性向上のための best-effort 設定として有効化する。
     * 失敗しても bind を試行するため、戻り値は明示的に破棄する。
     * see: https://man7.org/linux/man-pages/man7/socket.7.html
     * see: https://learn.microsoft.com/en-us/windows/win32/winsock/using-so-reuseaddr-and-so-exclusiveaddruse
     */
    if (ctx->service.src_port != 0)
    {
        (void)cplat_socket_set_reuse_address(sock, 1, NULL);
    }

    /*
     * 接続の開始前に Nagle を抑止する。TCP_NODELAY はソケットのオプションであり、
     * Windows の setsockopt は任意の状態のソケットへ設定できる。
     * Linux の tcp(7) も、このオプションに接続済みであることを要求していない。
     * listen 側からの継承には依存せず、接続するソケットごとに設定する。
     * 失敗しても接続は続ける。この設定は送信の遅れを縮めるためのもので、データの整合や認証は変えない。
     * see: https://man7.org/linux/man-pages/man7/tcp.7.html
     * see: https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-setsockopt
     * see: https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-tcp-socket-options
     */
    if (cplat_socket_set_nodelay(sock, 1, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_WARNING, &detail,
                                  "connect_thread[service_id=%" PRId64 " path=%d]: TCP_NODELAY failed",
                                  ctx->service.service_id, path_idx);
    }

    if (ctx->service.src_addr[path_idx][0] != '\0' || ctx->service.src_port != 0)
    {
        cplat_ipv4_endpoint bind_addr = {0};

        if (ctx->service.src_addr[path_idx][0] != '\0')
        {
            bind_addr.address = ctx->src_addr_resolved[path_idx];
        }
        else
        {
            bind_addr.address = CPLAT_IPV4_ADDR_ANY;
        }

        bind_addr.port = cplat_hton16(ctx->service.src_port); /* 0 = エフェメラル */

        if (cplat_socket_bind(sock, &bind_addr, &detail) != CPLAT_OK)
        {
            POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail,
                                      "connect_thread[service_id=%" PRId64 " path=%d]: bind() failed",
                                      ctx->service.service_id, path_idx);
            cplat_socket_close(sock);
            return CPLAT_INVALID_SOCKET;
        }
    }

    if (cplat_socket_watch_attach(sock, &watch, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail,
                                  "connect_thread[service_id=%" PRId64 " path=%d]: watch_attach() failed",
                                  ctx->service.service_id, path_idx);
        cplat_socket_close(sock);
        return CPLAT_INVALID_SOCKET;
    }

    addr.address = ctx->dst_addr_resolved[path_idx];
    addr.port = cplat_hton16(ctx->service.dst_port);

    if (timeout_ms == 0U)
    {
        connect_timeout_ms = CPLAT_WAIT_FOREVER;
    }
    else
    {
        connect_timeout_ms = (int)timeout_ms;
    }

    cplat_error_clear(&detail);
    connect_result = cplat_socket_watch_connect(watch, &addr, cancel, connect_timeout_ms, &detail);
    if (connect_result != CPLAT_OK)
    {
        if (connect_result == CPLAT_ERR_TIMEOUT)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "connect_thread[service_id=%" PRId64 " path=%d]: connect() timed out",
                       ctx->service.service_id, path_idx);
        }
        else if (connect_result != CPLAT_ERR_CANCELED)
        {
            POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_VERBOSE, &detail,
                                      "connect_thread[service_id=%" PRId64 " path=%d]: watch_connect() failed",
                                      ctx->service.service_id, path_idx);
        }

        cplat_socket_watch_detach(watch);
        cplat_socket_close(sock);
        return CPLAT_INVALID_SOCKET;
    }

    *watch_out = watch;
    return sock;
}

/* Doxygen コメントは、ヘッダーに記載 */
int thread_connect_peer_allowed(const potr_context *ctx, const int path_idx, const cplat_ipv4_endpoint *peer_addr)
{
    if (ctx->service.src_addr[path_idx][0] != '\0' && peer_addr->address != ctx->src_addr_resolved[path_idx])
    {
        return 0;
    }

    if (ctx->service.src_port != 0 && cplat_ntoh16(peer_addr->port) != ctx->service.src_port)
    {
        return 0;
    }

    return 1;
}
