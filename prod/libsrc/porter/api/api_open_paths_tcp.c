/**
 *******************************************************************************
 *  @file           api_open_paths_tcp.c
 *  @brief          TCP の listen ソケットと接続先アドレスを準備します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <inttypes.h>
#include <string.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/potr_peer_table.h>
#include <porter/infra/potr_trace.h>
#include <porter/infra/potr_result.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/endpoint.h>
#include <cplat/net/socket.h>

#include "api_open_paths_tcp.h"
#include "api_open_paths_common.h"

/* TCP RECEIVER: path_idx 番目の listen ソケットを作成して bind・listen する。
   dst_addr[path_idx] が指定されていれば dst_addr_resolved[path_idx] に解決する。
   src_addr[path_idx] が指定されていれば src_addr_resolved[path_idx] にも解決する (接続元フィルター用)。
   成功時は ctx->tcp_listen_sock[path_idx] に格納して POTR_OK を返す。 */
static int open_socket_tcp_receiver(potr_context *ctx, const int path_idx)
{
    cplat_socket sock;
    cplat_ipv4_endpoint addr = {0};
    uint32_t bind_ip;
    int result;
    cplat_error detail;

    if (ctx->service.dst_addr[path_idx][0] != '\0')
    {
        result = api_paths_resolve_ipv4(ctx->service.dst_addr[path_idx], &bind_ip);
        if (result != POTR_OK)
        {
            return result;
        }
        ctx->dst_addr_resolved[path_idx] = bind_ip;
    }
    else
    {
        bind_ip = CPLAT_IPV4_ADDR_ANY;
    }

    if (ctx->service.src_addr[path_idx][0] != '\0')
    {
        result = api_paths_resolve_ipv4(ctx->service.src_addr[path_idx], &ctx->src_addr_resolved[path_idx]);
        if (result != POTR_OK)
        {
            return result;
        }
    }

    result = cplat_socket_open(CPLAT_SOCKET_TCP, &sock, &detail);
    if (result != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "socket failed");
        return potr_internal_result_from_error(&detail);
    }

    /* SO_REUSEADDR は互換性向上のための best-effort 設定であり、失敗しても bind を試行します。 */
    (void)cplat_socket_set_reuse_address(sock, 1, NULL);

    addr.address = bind_ip;
    addr.port = cplat_hton16(ctx->service.dst_port);

    result = cplat_socket_bind(sock, &addr, &detail);
    if (result != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "bind failed");
        cplat_socket_close(sock);
        return potr_internal_result_from_error(&detail);
    }

    result = cplat_socket_listen(sock, CPLAT_SOCKET_BACKLOG_DEFAULT, &detail);
    if (result != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "listen failed");
        cplat_socket_close(sock);
        return potr_internal_result_from_error(&detail);
    }

    ctx->tcp_listen_sock[path_idx] = sock;
    return POTR_OK;
}

/* TCP SENDER: path_idx 番目の接続先 dst_addr を解決して dst_addr_resolved[path_idx] に格納する。
   src_addr[path_idx] が指定されていれば src_addr_resolved[path_idx] にも解決する。
   実際の TCP 接続は connect スレッドが行う。 */
static int open_socket_tcp_sender(potr_context *ctx, const int path_idx)
{
    int result;

    if (ctx->service.dst_addr[path_idx][0] == '\0')
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "open_socket_tcp_sender: dst_addr[%d] is empty", path_idx);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    result = api_paths_resolve_ipv4(ctx->service.dst_addr[path_idx], &ctx->dst_addr_resolved[path_idx]);
    if (result != POTR_OK)
    {
        return result;
    }

    if (ctx->service.src_addr[path_idx][0] != '\0')
    {
        result = api_paths_resolve_ipv4(ctx->service.src_addr[path_idx], &ctx->src_addr_resolved[path_idx]);
        if (result != POTR_OK)
        {
            return result;
        }
    }

    return POTR_OK;
}

/* TCP / TCP_BIDIR: RECEIVER は listen ソケット作成、SENDER はアドレス解決のみ行う */
/* Doxygen コメントは、ヘッダーに記載 */
int api_open_paths_tcp(potr_context *ctx, const potr_role role)
{
    int i;
    int result;

    if (ctx->service.dst_port == 0)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_service_open: service_id=%" PRId64 " TCP requires dst_port",
                   ctx->service.service_id);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (role == POTR_ROLE_RECEIVER)
    {
        /* 非空の dst_addr[i] ごとに listen ソケットを作成する */
        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            if (ctx->service.dst_addr[i][0] == '\0')
                break;

            result = open_socket_tcp_receiver(ctx, i);
            if (result != POTR_OK)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                           "potr_service_open: service_id=%" PRId64 " TCP listen failed"
                           " (path=%d dst_addr=%s dst_port=%u)",
                           ctx->service.service_id, i, ctx->service.dst_addr[i], (unsigned)ctx->service.dst_port);
                return result;
            }
            POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                       "potr_service_open: service_id=%" PRId64 " TCP path[%d] listening"
                       " on %s:%u",
                       ctx->service.service_id, i, ctx->service.dst_addr[i], (unsigned)ctx->service.dst_port);
            ctx->n_path = i + 1;
        }
        if (ctx->n_path == 0)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "potr_service_open: service_id=%" PRId64 " TCP RECEIVER requires"
                       " at least one dst_addr",
                       ctx->service.service_id);
            return POTR_ERR_INVALID_ARGUMENT;
        }
    }
    else
    {
        /* 非空の dst_addr[i] ごとにアドレスを解決する */
        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            if (ctx->service.dst_addr[i][0] == '\0')
                break;

            result = open_socket_tcp_sender(ctx, i);
            if (result != POTR_OK)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                           "potr_service_open: service_id=%" PRId64 " TCP sender"
                           " dst_addr resolve failed (path=%d %s)",
                           ctx->service.service_id, i, ctx->service.dst_addr[i]);
                return result;
            }
            ctx->n_path = i + 1;
        }
        if (ctx->n_path == 0)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "potr_service_open: service_id=%" PRId64 " TCP SENDER requires"
                       " at least one dst_addr",
                       ctx->service.service_id);
            return POTR_ERR_INVALID_ARGUMENT;
        }
    }

    return POTR_OK;
}
