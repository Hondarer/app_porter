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
#include <porter/thread/potr_connect_thread.h>
#include <porter/thread/potr_connected_threads.h>
#include <porter/thread/potr_recv_thread.h>
#include <porter/thread/potr_send_thread.h>
#include <porter/thread/potr_health_thread.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/endpoint.h>
#include <cplat/net/socket.h>
#include "thread_recv_validate.h"

#include "thread_connect_prepare.h"

/* Doxygen コメントは、ヘッダーに記載 */
int thread_connect_read_first_packet(const cplat_socket fd, uint8_t *buf, const size_t max_buf, size_t *len_out,
                                     const int timeout_ms)
{
    int ready = 0;
    uint16_t wire_payload_len;
    cplat_error detail;

    /* タイムアウト付き待機 */
    if (cplat_socket_wait_readable(fd, timeout_ms, &ready, &detail) != CPLAT_OK)
        return potr_internal_result_from_error(&detail);
    if (!ready)
        return POTR_ERR_TIMEOUT;

    /* 固定長ヘッダー読み取り */
    {
        int recv_result = cplat_socket_recv_all(fd, buf, POTR_PACKET_HEADER_SIZE, &detail);
        if (recv_result != CPLAT_OK)
            return potr_internal_result_from_socket_result(recv_result, &detail);
    }

    /* payload_len は固定長ヘッダー末尾の offset 34 に格納される */
    {
        uint16_t wpl;
        memcpy(&wpl, buf + 34, sizeof(wpl));
        wire_payload_len = cplat_ntoh16(wpl);
    }

    /* ペイロード長バリデーション */
    if (POTR_PACKET_HEADER_SIZE + (size_t)wire_payload_len > max_buf)
        return POTR_ERR_PROTOCOL;

    /* ペイロード読み取り */
    if (wire_payload_len > 0)
    {
        int recv_result = cplat_socket_recv_all(fd, buf + POTR_PACKET_HEADER_SIZE, (size_t)wire_payload_len, &detail);
        if (recv_result != CPLAT_OK)
            return potr_internal_result_from_socket_result(recv_result, &detail);
    }

    *len_out = POTR_PACKET_HEADER_SIZE + (size_t)wire_payload_len;
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */
int thread_connect_session_compare(const potr_context *ctx, const potr_packet *pkt)
{
    cplat_timespec pkt_session_ts;
    int ts_cmp;

    if (!ctx->tcp_accepted_session_known)
        return TCP_SESSION_NEW;

    session_ts_from_hdr(pkt->session_tv_sec, pkt->session_tv_nsec, &pkt_session_ts);
    ts_cmp = cplat_timespec_cmp(&pkt_session_ts, &ctx->tcp_accepted_session_ts);

    if (ts_cmp > 0)
        return TCP_SESSION_NEW;
    else if (ts_cmp < 0)
        return TCP_SESSION_OLD;
    else if (pkt->session_id > ctx->tcp_accepted_session_id)
        return TCP_SESSION_NEW;
    else if (pkt->session_id < ctx->tcp_accepted_session_id)
        return TCP_SESSION_OLD;
    return TCP_SESSION_SAME;
}

/* Doxygen コメントは、ヘッダーに記載 */
cplat_socket thread_connect_with_timeout(potr_context *ctx, const int path_idx)
{
    cplat_socket sock;
    cplat_ipv4_endpoint addr = {0};
    uint32_t timeout_ms = ctx->service.connect_timeout_ms;
    cplat_error detail;

    if (cplat_socket_open(CPLAT_SOCKET_TCP, &sock, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail,
                                  "connect_thread[service_id=%" PRId64 " path=%d]: socket() failed",
                                  ctx->service.service_id, path_idx);
        return CPLAT_INVALID_SOCKET;
    }

    (void)cplat_socket_set_reuse_address(sock, 1, NULL);

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

    addr.address = ctx->dst_addr_resolved[path_idx];
    addr.port = cplat_hton16(ctx->service.dst_port);

    if (timeout_ms == 0U)
    {
        /* ブロッキング接続 */
        if (cplat_socket_connect(sock, &addr, &detail) != CPLAT_OK)
        {
            POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_VERBOSE, &detail,
                                      "connect_thread[service_id=%" PRId64 " path=%d]: connect() failed (blocking)",
                                      ctx->service.service_id, path_idx);
            cplat_socket_close(sock);
            return CPLAT_INVALID_SOCKET;
        }
        return sock;
    }

    /* タイムアウト付き接続: 非ブロッキング モードで writable ポーリングを使う。
       停止シグナルに素早く応答するため 100ms 単位でポーリングする。 */
    (void)cplat_socket_set_nonblocking(sock, 1, NULL);
    {
        const int connect_result = cplat_socket_connect(sock, &addr, &detail);
        if (connect_result == CPLAT_OK)
        {
            /* 即座に接続成功 */
            (void)cplat_socket_set_nonblocking(sock, 0, NULL);
            return sock;
        }
        if (connect_result != CPLAT_ERR_IN_PROGRESS)
        {
            POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_VERBOSE, &detail,
                                      "connect_thread[service_id=%" PRId64 " path=%d]: connect() failed",
                                      ctx->service.service_id, path_idx);
            cplat_socket_close(sock);
            return CPLAT_INVALID_SOCKET;
        }
    }

    /* writable ポーリング ループ (共通) */
    {
        uint32_t elapsed_ms = 0U;
        int ready = 0;

        while (elapsed_ms < timeout_ms &&
               cplat_atomic_load_i32(&ctx->connect_thread_running[path_idx], CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
        {
            uint32_t poll_ms = timeout_ms - elapsed_ms;
            int wait_ready = 0;
            int wait_result;
            if (poll_ms > 100U)
                poll_ms = 100U;
            wait_result = cplat_socket_wait_writable(sock, (int)poll_ms, &wait_ready, NULL);
            if (wait_result != CPLAT_OK)
                break;
            if (wait_ready)
            {
                ready = 1;
                break;
            }
            elapsed_ms += poll_ms;
        }

        if (!ready)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "connect_thread[service_id=%" PRId64 "]: connect() timed out",
                       ctx->service.service_id);
            cplat_socket_close(sock);
            return CPLAT_INVALID_SOCKET;
        }
    }

    if (cplat_socket_get_pending_error(sock, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_VERBOSE, &detail,
                                  "connect_thread[service_id=%" PRId64 " path=%d]: SO_ERROR", ctx->service.service_id,
                                  path_idx);
        cplat_socket_close(sock);
        return CPLAT_INVALID_SOCKET;
    }

    (void)cplat_socket_set_nonblocking(sock, 0, NULL);
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
