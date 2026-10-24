/**
 *******************************************************************************
 *  @file           api_open_paths.c
 *  @brief          通信種別ごとのソケットと送受信アドレスを準備します。
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

#include "api_open_paths.h"

#include "api_open_paths_udp.h"
#include "api_open_paths_tcp.h"
#include "api_open_paths_common.h"

/* Doxygen コメントは、ヘッダーに記載 */

int api_open_paths_by_type(potr_context *ctx, potr_role role)
{
    switch (raw_base_type(ctx->service.type))
    {
    case POTR_TYPE_UNICAST:
        return api_open_paths_unicast(ctx, role);
    case POTR_TYPE_MULTICAST:
        return api_open_paths_multicast(ctx, role);
    case POTR_TYPE_BROADCAST:
        return api_open_paths_broadcast(ctx, role);
    case POTR_TYPE_UNICAST_BIDIR:
        return api_open_paths_unicast_bidir(ctx, role);
    case POTR_TYPE_UNICAST_BIDIR_N1:
        return api_open_paths_unicast_bidir_n1(ctx);
    case POTR_TYPE_TCP:
    case POTR_TYPE_TCP_BIDIR:
        return api_open_paths_tcp(ctx, role);
    case POTR_TYPE_UNICAST_RAW:
    case POTR_TYPE_MULTICAST_RAW:
    case POTR_TYPE_BROADCAST_RAW:
        /* raw_base_type() は RAW 型をベース型に変換するため、ここには到達しない */
        /* fall through */
    default:
        return POTR_ERR_UNSUPPORTED;
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

int api_setup_dest_addr(potr_context *ctx, potr_role role)
{
    int i;

    if (role != POTR_ROLE_SENDER && ctx->service.type != POTR_TYPE_UNICAST_BIDIR)
    {
        return POTR_OK;
    }

    switch (raw_base_type(ctx->service.type))
    {
    case POTR_TYPE_UNICAST_BIDIR:
    case POTR_TYPE_UNICAST_BIDIR_N1:
        for (i = 0; i < ctx->n_path; i++)
        {
            endpoint_clear(&ctx->dest_addr[i]);
            if (role == POTR_ROLE_SENDER)
            {
                /* SENDER: dst_addr:dst_port (RECEIVER の bind アドレス) へ送信 */
                ctx->dest_addr[i].address = ctx->dst_addr_resolved[i];
                ctx->dest_addr[i].port = cplat_hton16(ctx->service.dst_port);
            }
            else
            {
                /* RECEIVER: src_addr:src_port (SENDER の bind アドレス) へ送信 */
                ctx->dest_addr[i].address = ctx->src_addr_resolved[i];
                ctx->dest_addr[i].port = cplat_hton16(ctx->service.src_port);
            }
        }
        break;

    case POTR_TYPE_UNICAST:
        for (i = 0; i < ctx->n_path; i++)
        {
            endpoint_clear(&ctx->dest_addr[i]);
            ctx->dest_addr[i].address = ctx->dst_addr_resolved[i];
            ctx->dest_addr[i].port = cplat_hton16(ctx->service.dst_port);
        }
        break;

    case POTR_TYPE_MULTICAST:
    {
        uint32_t mcast_ip;
        if (api_paths_parse_ipv4(ctx->service.multicast_group, &mcast_ip) != POTR_OK)
        {
            return POTR_ERR_INVALID_ARGUMENT;
        }
        for (i = 0; i < ctx->n_path; i++)
        {
            endpoint_clear(&ctx->dest_addr[i]);
            ctx->dest_addr[i].address = mcast_ip;
            ctx->dest_addr[i].port = cplat_hton16(ctx->service.dst_port);
        }
        break;
    }

    case POTR_TYPE_BROADCAST:
    {
        uint32_t bcast_ip;
        if (api_paths_parse_ipv4(ctx->service.broadcast_addr, &bcast_ip) != POTR_OK)
        {
            return POTR_ERR_INVALID_ARGUMENT;
        }
        for (i = 0; i < ctx->n_path; i++)
        {
            endpoint_clear(&ctx->dest_addr[i]);
            ctx->dest_addr[i].address = bcast_ip;
            ctx->dest_addr[i].port = cplat_hton16(ctx->service.dst_port);
        }
        break;
    }

    case POTR_TYPE_TCP:
    case POTR_TYPE_TCP_BIDIR:
        /* TCP 接続ソケットは connect スレッドが管理するため dest_addr 設定不要 */
        break;
    case POTR_TYPE_UNICAST_RAW:
    case POTR_TYPE_MULTICAST_RAW:
    case POTR_TYPE_BROADCAST_RAW:
        /* raw_base_type() は RAW 型をベース型に変換するため、ここには到達しない */
        /* fall through */
    default:
        break;
    }

    return POTR_OK;
}
