/**
 *******************************************************************************
 *  @file           api_open_paths_udp.c
 *  @brief          UDP 各通信方式のソケットと経路を準備します。
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

#include "api_open_paths_udp.h"
#include "api_open_paths_common.h"

/* ソケットを作成して bind する。成功時はソケットを返す。失敗時は CPLAT_INVALID_SOCKET。
   bind_addr: bind する IPv4 アドレス (ネットワーク バイト オーダー)。port: bind するポート番号 (0 = OS 自動選定)。 */
static cplat_socket open_socket_unicast(const uint32_t bind_addr, const uint16_t port)
{
    cplat_socket sock;
    cplat_ipv4_endpoint addr = {0};
    cplat_error detail;

    if (cplat_socket_open(CPLAT_SOCKET_UDP, &sock, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "socket failed");
        return CPLAT_INVALID_SOCKET;
    }

    /* SO_REUSEADDR は互換性向上のための best-effort 設定であり、失敗しても bind を試行します。 */
    (void)cplat_socket_set_reuse_address(sock, 1, NULL);

    addr.address = bind_addr;
    addr.port = cplat_hton16(port);

    if (cplat_socket_bind(sock, &addr, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "bind failed");
        cplat_socket_close(sock);
        return CPLAT_INVALID_SOCKET;
    }

    return sock;
}

/* マルチキャスト ソケットを作成して bind・グループ参加する。
   src_if: 使用するローカル インターフェイス (INADDR_ANY = OS 自動選択)。
   is_receiver: 1 = 受信者、0 = 送信者。 */
static cplat_socket open_socket_multicast(const potr_service_def *def, const uint32_t src_if, const int is_receiver)
{
    cplat_socket sock;
    cplat_ipv4_endpoint addr = {0};
    uint32_t group_addr;
    cplat_error detail;
    /* 受信者: dst_port で bind する。送信者: src_port で bind する (送信元ポート)。 */
    uint16_t bind_port;
    if (is_receiver)
    {
        bind_port = def->dst_port;
    }
    else
    {
        bind_port = def->src_port;
    }

    if (cplat_socket_open(CPLAT_SOCKET_UDP, &sock, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "socket failed");
        return CPLAT_INVALID_SOCKET;
    }

    /* SO_REUSEADDR は互換性向上のための best-effort 設定であり、失敗しても bind を試行します。 */
    (void)cplat_socket_set_reuse_address(sock, 1, NULL);

    addr.address = CPLAT_IPV4_ADDR_ANY;
    addr.port = cplat_hton16(bind_port);

    if (cplat_socket_bind(sock, &addr, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "bind failed");
        cplat_socket_close(sock);
        return CPLAT_INVALID_SOCKET;
    }

    /* マルチキャスト グループへ参加 (送受信ともに参加する) */
    if (api_paths_parse_ipv4(def->multicast_group, &group_addr) != POTR_OK)
    {
        cplat_socket_close(sock);
        return CPLAT_INVALID_SOCKET;
    }

    if (cplat_socket_join_multicast_group(sock, group_addr, src_if, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "join_multicast_group failed");
        cplat_socket_close(sock);
        return CPLAT_INVALID_SOCKET;
    }

    /* 送信者: マルチキャスト送信インターフェイスを設定する */
    if (!is_receiver)
    {
        (void)cplat_socket_set_multicast_interface(sock, src_if, NULL);
    }

    return sock;
}

/* ブロードキャスト ソケットを作成して bind する。
   src_port: 送信者の送信元 bind ポート (0 = OS 自動選定)。
   dst_port: 受信者の listen ポート / 送信者の送信先ポート (省略不可)。
   src_if: 送信者が使用するローカル インターフェイス (INADDR_ANY = OS 自動選択)。
   is_receiver: 1 = 受信者 (INADDR_ANY で bind)、0 = 送信者 (src_if で bind)。 */
static cplat_socket open_socket_broadcast(uint16_t src_port, uint16_t dst_port, const uint32_t src_if,
                                          const int is_receiver)
{
    cplat_socket sock;
    cplat_ipv4_endpoint addr = {0};
    cplat_error detail;
    /* 受信者: dst_port で bind する。送信者: src_port で bind する (送信元ポート)。 */
    uint16_t bind_port;
    if (is_receiver)
    {
        bind_port = dst_port;
    }
    else
    {
        bind_port = src_port;
    }

    if (cplat_socket_open(CPLAT_SOCKET_UDP, &sock, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "socket failed");
        return CPLAT_INVALID_SOCKET;
    }

    /* SO_REUSEADDR は互換性向上のための best-effort 設定であり、失敗しても bind を試行します。 */
    (void)cplat_socket_set_reuse_address(sock, 1, NULL);
    if (cplat_socket_set_broadcast(sock, 1, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "set_broadcast failed");
        cplat_socket_close(sock);
        return CPLAT_INVALID_SOCKET;
    }

    /* 送信者: src_addr で bind してインターフェイスを選択する。受信者: INADDR_ANY で bind する。 */
    if (!is_receiver)
    {
        addr.address = src_if;
    }
    else
    {
        addr.address = CPLAT_IPV4_ADDR_ANY;
    }
    addr.port = cplat_hton16(bind_port);

    if (cplat_socket_bind(sock, &addr, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "bind failed");
        cplat_socket_close(sock);
        return CPLAT_INVALID_SOCKET;
    }

    return sock;
}

/* UNICAST: src_addr/dst_addr ペアごとにソケットを作成する */
/* Doxygen コメントは、ヘッダーに記載 */
int api_open_paths_unicast(potr_context *ctx, const potr_role role)
{
    int i;
    int result;

    if (ctx->service.dst_port == 0)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    for (i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        uint32_t bind_addr;
        uint16_t bind_port;

        if (ctx->service.src_addr[i][0] == '\0' || ctx->service.dst_addr[i][0] == '\0')
        {
            break;
        }

        result = api_paths_resolve_ipv4(ctx->service.src_addr[i], &ctx->src_addr_resolved[i]);
        if (result != POTR_OK)
        {
            return result;
        }
        result = api_paths_resolve_ipv4(ctx->service.dst_addr[i], &ctx->dst_addr_resolved[i]);
        if (result != POTR_OK)
        {
            return result;
        }

        if (role == POTR_ROLE_RECEIVER)
        {
            bind_addr = ctx->dst_addr_resolved[i];
            bind_port = ctx->service.dst_port;
        }
        else
        {
            bind_addr = ctx->src_addr_resolved[i];
            bind_port = ctx->service.src_port;
        }

        ctx->sock[i] = open_socket_unicast(bind_addr, bind_port);
        if (ctx->sock[i] == CPLAT_INVALID_SOCKET)
        {
            return POTR_ERR_IO;
        }

        ctx->n_path++;
    }

    if (ctx->n_path == 0)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    return POTR_OK;
}

/* MULTICAST: src_addr ごとにグループ参加済みソケットを作成する */
/* Doxygen コメントは、ヘッダーに記載 */
int api_open_paths_multicast(potr_context *ctx, const potr_role role)
{
    int i;
    int result;

    if (ctx->service.dst_port == 0 || ctx->service.multicast_group[0] == '\0')
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    for (i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        if (ctx->service.src_addr[i][0] == '\0')
            break;

        result = api_paths_resolve_ipv4(ctx->service.src_addr[i], &ctx->src_addr_resolved[i]);
        if (result != POTR_OK)
        {
            return result;
        }

        ctx->sock[i] = open_socket_multicast(&ctx->service, ctx->src_addr_resolved[i], role == POTR_ROLE_RECEIVER);
        if (ctx->sock[i] == CPLAT_INVALID_SOCKET)
        {
            return POTR_ERR_IO;
        }

        ctx->n_path++;
    }

    if (ctx->n_path == 0)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    return POTR_OK;
}

/* BROADCAST: src_addr ごとにブロードキャスト許可ソケットを作成する */
/* Doxygen コメントは、ヘッダーに記載 */
int api_open_paths_broadcast(potr_context *ctx, const potr_role role)
{
    int i;
    int result;

    if (ctx->service.dst_port == 0)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    /* broadcast_addr 省略時は限定ブロードキャスト (255.255.255.255) を使用する */
    if (ctx->service.broadcast_addr[0] == '\0')
    {
        const char *dflt = "255.255.255.255";
        size_t len = strlen(dflt);
        memcpy(ctx->service.broadcast_addr, dflt, len + 1);
    }

    for (i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        if (ctx->service.src_addr[i][0] == '\0')
            break;

        result = api_paths_resolve_ipv4(ctx->service.src_addr[i], &ctx->src_addr_resolved[i]);
        if (result != POTR_OK)
        {
            return result;
        }

        ctx->sock[i] = open_socket_broadcast(ctx->service.src_port, ctx->service.dst_port, ctx->src_addr_resolved[i],
                                             role == POTR_ROLE_RECEIVER);
        if (ctx->sock[i] == CPLAT_INVALID_SOCKET)
        {
            return POTR_ERR_IO;
        }

        ctx->n_path++;
    }

    if (ctx->n_path == 0)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    return POTR_OK;
}

/* UNICAST_BIDIR (1:1): 動的 RECEIVER または src/dst ペア ループでソケットを作成する */
/* Doxygen コメントは、ヘッダーに記載 */
int api_open_paths_unicast_bidir(potr_context *ctx, const potr_role role)
{
    int result;

    ctx->is_multi_peer = 0;

    /* dst_port は必須。 */
    if (ctx->service.dst_port == 0)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "potr_service_open: service_id=%" PRId64 " UNICAST_BIDIR requires"
                   " dst_port (non-zero)",
                   ctx->service.service_id);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (role == POTR_ROLE_RECEIVER && ctx->service.src_addr[0][0] == '\0')
    {
        /* 動的 1:1 RECEIVER: src_addr 省略 → dst_addr:dst_port に bind し、
           最初の受信パケットから SENDER のアドレスを動的学習する。 */
        uint32_t bind_addr;

        if (ctx->service.dst_addr[0][0] == '\0')
        {
            bind_addr = CPLAT_IPV4_ADDR_ANY;
        }
        else
        {
            result = api_paths_resolve_ipv4(ctx->service.dst_addr[0], &bind_addr);
            if (result != POTR_OK)
            {
                return result;
            }
            ctx->dst_addr_resolved[0] = bind_addr;
        }
        ctx->sock[0] = open_socket_unicast(bind_addr, ctx->service.dst_port);
        if (ctx->sock[0] == CPLAT_INVALID_SOCKET)
        {
            return POTR_ERR_IO;
        }
        ctx->n_path = 1;
        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                   "potr_service_open: service_id=%" PRId64 " UNICAST_BIDIR 1:1 dynamic RECEIVER"
                   " bind dst_port=%u",
                   ctx->service.service_id, (unsigned)ctx->service.dst_port);
        return POTR_OK;
    }

    /* 1:1 モード: src_addr/dst_addr ペア ループ。
       SENDER は src_addr 省略時に INADDR_ANY で bind する (OS がアダプターを自動選択)。
       RECEIVER はここには src_addr がある場合のみ到達する。 */
    {
        int i;

        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            uint32_t bind_addr;

            /* dst_addr が空 → パス終端。
               RECEIVER は src_addr も必要 (src_addr なし RECEIVER は上で処理済み)。 */
            if (ctx->service.dst_addr[i][0] == '\0')
                break;
            if (role == POTR_ROLE_RECEIVER && ctx->service.src_addr[i][0] == '\0')
                break;

            if (ctx->service.src_addr[i][0] != '\0')
            {
                result = api_paths_resolve_ipv4(ctx->service.src_addr[i], &ctx->src_addr_resolved[i]);
                if (result != POTR_OK)
                {
                    return result;
                }
            }

            result = api_paths_resolve_ipv4(ctx->service.dst_addr[i], &ctx->dst_addr_resolved[i]);
            if (result != POTR_OK)
            {
                return result;
            }

            if (role == POTR_ROLE_SENDER)
            {
                /* src_addr 省略時は INADDR_ANY で bind し OS がアダプターを自動選択 */
                if (ctx->service.src_addr[i][0] != '\0')
                {
                    bind_addr = ctx->src_addr_resolved[i];
                }
                else
                {
                    bind_addr = CPLAT_IPV4_ADDR_ANY;
                }
                ctx->sock[i] = open_socket_unicast(bind_addr, ctx->service.src_port);
            }
            else
            {
                /* RECEIVER: dst_addr:dst_port で bind */
                ctx->sock[i] = open_socket_unicast(ctx->dst_addr_resolved[i], ctx->service.dst_port);
            }
            if (ctx->sock[i] == CPLAT_INVALID_SOCKET)
            {
                return POTR_ERR_IO;
            }

            ctx->n_path++;
        }

        if (ctx->n_path == 0)
        {
            return POTR_ERR_INVALID_ARGUMENT;
        }
    }

    return POTR_OK;
}

/* UNICAST_BIDIR_N1 (N:1 サーバー): dst_addr[i]:dst_port ごとにソケットを bind し、
   ピア テーブルを初期化する。dst_addr がすべて省略されている場合は INADDR_ANY で
   1 ソケットのみ作成する。 */
/* Doxygen コメントは、ヘッダーに記載 */
int api_open_paths_unicast_bidir_n1(potr_context *ctx)
{
    int i;
    int result;

    ctx->is_multi_peer = 1;

    /* dst_port は必須。 */
    if (ctx->service.dst_port == 0)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "potr_service_open: service_id=%" PRId64 " UNICAST_BIDIR_N1 requires"
                   " dst_port (non-zero)",
                   ctx->service.service_id);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (ctx->service.dst_addr[0][0] == '\0')
    {
        /* dst_addr すべて省略: INADDR_ANY で 1 ソケット */
        uint32_t any_addr;
        any_addr = CPLAT_IPV4_ADDR_ANY;
        ctx->sock[0] = open_socket_unicast(any_addr, ctx->service.dst_port);
        if (ctx->sock[0] == CPLAT_INVALID_SOCKET)
        {
            return POTR_ERR_IO;
        }
        ctx->n_path = 1;
    }
    else
    {
        /* dst_addr[i] を列挙してパスごとにソケットを作成する */
        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            uint32_t bind_addr;

            if (ctx->service.dst_addr[i][0] == '\0')
                break;

            result = api_paths_resolve_ipv4(ctx->service.dst_addr[i], &bind_addr);
            if (result != POTR_OK)
            {
                return result;
            }
            ctx->dst_addr_resolved[i] = bind_addr;
            ctx->sock[i] = open_socket_unicast(bind_addr, ctx->service.dst_port);
            if (ctx->sock[i] == CPLAT_INVALID_SOCKET)
            {
                return POTR_ERR_IO;
            }
            ctx->n_path = i + 1;
        }
    }

    /* ピア テーブル初期化 */
    ctx->max_peers = (int)ctx->service.max_peers;
    if (ctx->max_peers <= 0)
    {
        ctx->max_peers = 1024;
    }
    result = potr_internal_peer_table_init(ctx);
    if (result != POTR_OK)
    {
        return result;
    }

    POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
               "potr_service_open: service_id=%" PRId64 " UNICAST_BIDIR_N1"
               " (max_peers=%d src_port_filter=%u) bind dst_port=%u n_path=%d",
               ctx->service.service_id, ctx->max_peers, (unsigned)ctx->service.src_port,
               (unsigned)ctx->service.dst_port, ctx->n_path);

    return POTR_OK;
}
