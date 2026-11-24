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
#include <porter/protocol/config_check.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/endpoint.h>
#include <cplat/net/socket.h>

#include "api_open_paths_udp.h"
#include "api_open_paths_common.h"

/* 途中失敗時に作成済みの UDP 登録とソケットを逆順で回収する。 */
static void rollback_udp_paths(potr_context *ctx)
{
    int i;

    for (i = 0; i < ctx->n_path && i < (int)POTR_MAX_PATH; i++)
    {
        if (ctx->udp_watch[i] != NULL)
        {
            cplat_socket_watch_detach(ctx->udp_watch[i]);
            ctx->udp_watch[i] = NULL;
        }
        if (ctx->sock[i] != CPLAT_INVALID_SOCKET)
        {
            cplat_socket_close(ctx->sock[i]);
            ctx->sock[i] = CPLAT_INVALID_SOCKET;
        }
    }

    ctx->n_path = 0;
}

/* 作成済み UDP ソケットを watch へ登録する。失敗時はソケットを閉じてエラーを返す。 */
static int attach_udp_watch(cplat_socket sock, cplat_socket_watch **watch_out)
{
    cplat_error detail;
    int rc;

    *watch_out = NULL;
    cplat_error_clear(&detail);
    rc = cplat_socket_watch_attach(sock, watch_out, &detail);
    if (rc != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "socket watch attach failed");
        cplat_socket_close(sock);
        return potr_internal_result_from_socket_result(rc, &detail);
    }

    return POTR_OK;
}

/*
 * UDP ソケットの OS 受信バッファーの拡大を要求し、結果を確認して必要なら INFO にサービスごとに 1 回記録する。
 * 要求しない設定 (実効値 0) の場合は何もしない。失敗してもエラーにはせず続行する。
 */
static void configure_udp_receive_buffer(const cplat_socket sock, const potr_context *ctx, int *logged)
{
    uint32_t requested_size = resolve_udp_receive_buffer_size(&ctx->service, &ctx->global);
    cplat_error detail;
    size_t actual_size = 0U;
    int set_ret;
    int get_ret;

    if (requested_size == 0U)
    {
        return;
    }

    /*
     * OS の受信バッファーの拡大を要求する。
     * Linux は設定値を 2 倍にして保持し net.core.rmem_max で切り詰めるため、
     * 取得値そのものが要求値未満かで不足を判定する。
     * see: https://man7.org/linux/man-pages/man7/socket.7.html
     */
    set_ret = cplat_socket_set_receive_buffer_size(sock, (size_t)requested_size, &detail);
    get_ret = cplat_socket_get_receive_buffer_size(sock, &actual_size, NULL);

    if (*logged != 0)
    {
        return;
    }

    if (set_ret != CPLAT_OK || get_ret != CPLAT_OK || actual_size < (size_t)requested_size)
    {
        if (get_ret == CPLAT_OK)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                       "udp_receive_buffer_size[service_id=%" PRId64 "]: requested=%" PRIu32
                       " actual=%zu; check OS receive buffer settings (e.g. net.core.rmem_max on Linux)",
                       ctx->service.service_id, requested_size, actual_size);
        }
        else
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                       "udp_receive_buffer_size[service_id=%" PRId64 "]: requested=%" PRIu32
                       " actual=unknown; check OS receive buffer settings (e.g. net.core.rmem_max on Linux)",
                       ctx->service.service_id, requested_size);
        }
        *logged = 1;
    }
}

/*
 * ソケットを作成して bind・watch 登録する。成功時は POTR_OK を返す。
 * bind_addr: bind する IPv4 アドレス (ネットワーク バイト オーダー)。port: bind するポート番号 (0 = OS 自動選定)。
 */
static int open_socket_unicast(const potr_context *ctx, const uint32_t bind_addr, const uint16_t port,
                               int *rcvbuf_logged, cplat_socket *sock_out, cplat_socket_watch **watch_out)
{
    cplat_socket sock;
    cplat_ipv4_endpoint addr = {0};
    cplat_error detail;
    int attach_ret;

    *sock_out = CPLAT_INVALID_SOCKET;
    *watch_out = NULL;

    if (cplat_socket_open(CPLAT_SOCKET_UDP, &sock, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "socket failed");
        return POTR_ERR_IO;
    }

    /*
     * 自動割り当て (port == 0) で SO_REUSEADDR を有効にすると、同一ホストの複数ソケットへ
     * 同じローカル ポートが割り当てられ、返信や PING が片方にしか届かなくなる。
     * 固定ポート (port != 0) の場合のみ、再起動時の互換性向上のための best-effort 設定として有効化する。
     * 失敗しても bind を試行するため、戻り値は明示的に破棄する。
     * see: https://man7.org/linux/man-pages/man7/socket.7.html
     * see: https://learn.microsoft.com/en-us/windows/win32/winsock/using-so-reuseaddr-and-so-exclusiveaddruse
     */
    if (port != 0)
    {
        (void)cplat_socket_set_reuse_address(sock, 1, NULL);
    }

    addr.address = bind_addr;
    addr.port = cplat_hton16(port);

    if (cplat_socket_bind(sock, &addr, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "bind failed");
        cplat_socket_close(sock);
        return POTR_ERR_IO;
    }

    configure_udp_receive_buffer(sock, ctx, rcvbuf_logged);

    attach_ret = attach_udp_watch(sock, watch_out);
    if (attach_ret != POTR_OK)
    {
        return attach_ret;
    }

    *sock_out = sock;
    return POTR_OK;
}

/*
 * マルチキャスト ソケットを作成して bind・グループ参加・送信設定 (IF / TTL)・watch 登録する。
 * join_if: グループ参加と、送信者の送信インターフェイスに使うアドレス (INADDR_ANY = OS 自動選択)。
 * is_receiver: 1 = 受信者、0 = 送信者。
 */
static int open_socket_multicast(const potr_context *ctx, int path_idx, const uint32_t join_if, const int is_receiver,
                                 int *rcvbuf_logged, cplat_socket *sock_out, cplat_socket_watch **watch_out)
{
    const potr_service_def *def = &ctx->service;
    cplat_socket sock;
    cplat_ipv4_endpoint addr = {0};
    uint32_t group_addr;
    cplat_error detail;
    int attach_ret;

    /* 受信者: dst_port で bind する。送信者: src_port で bind する (送信元ポート)。 */
    uint16_t bind_port;

    *sock_out = CPLAT_INVALID_SOCKET;
    *watch_out = NULL;

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
        return POTR_ERR_IO;
    }

    /*
     * 受信者は常に固定ポート (dst_port != 0) で bind するため、同一ポートへの複数参加を許可する。
     * 送信者が自動割り当て (src_port == 0) で bind する場合は、同一ホストの複数ソケット間で
     * ローカル ポートが重複するのを防ぐため有効化しない。
     * 失敗しても bind を試行するため、戻り値は明示的に破棄する。
     * see: https://man7.org/linux/man-pages/man7/socket.7.html
     * see: https://learn.microsoft.com/en-us/windows/win32/winsock/using-so-reuseaddr-and-so-exclusiveaddruse
     */
    if (bind_port != 0)
    {
        (void)cplat_socket_set_reuse_address(sock, 1, NULL);
    }

    addr.address = CPLAT_IPV4_ADDR_ANY;
    addr.port = cplat_hton16(bind_port);

    if (cplat_socket_bind(sock, &addr, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "bind failed");
        cplat_socket_close(sock);
        return POTR_ERR_IO;
    }

    /* マルチキャスト グループへ参加する (送受信ともに参加する)。 */
    if (api_paths_parse_ipv4(def->multicast_group, &group_addr) != POTR_OK)
    {
        cplat_socket_close(sock);
        return POTR_ERR_IO;
    }

    if (cplat_socket_join_multicast_group(sock, group_addr, join_if, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "join_multicast_group failed");
        cplat_socket_close(sock);
        return POTR_ERR_IO;
    }

    /* 送信者: マルチキャスト送信インターフェイスと実効 TTL を設定する。設定できない場合は失敗とする。 */
    if (!is_receiver)
    {
        unsigned int effective_ttl;

        if (cplat_socket_set_multicast_interface(sock, join_if, &detail) != CPLAT_OK)
        {
            POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "set_multicast_interface failed");
            cplat_socket_close(sock);
            return POTR_ERR_IO;
        }

        effective_ttl = potr_internal_config_effective_ttl(def->ttl);
        if (cplat_socket_set_multicast_ttl(sock, effective_ttl, &detail) != CPLAT_OK)
        {
            POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail,
                                      "set_multicast_ttl failed (service_id=%" PRId64 " path=%d ttl=%u)",
                                      def->service_id, path_idx, effective_ttl);
            cplat_socket_close(sock);
            return POTR_ERR_IO;
        }
    }

    configure_udp_receive_buffer(sock, ctx, rcvbuf_logged);

    attach_ret = attach_udp_watch(sock, watch_out);
    if (attach_ret != POTR_OK)
    {
        return attach_ret;
    }

    *sock_out = sock;
    return POTR_OK;
}

/*
 * ブロードキャスト ソケットを作成して bind・watch 登録する。
 * src_port: 送信者の送信元 bind ポート (0 = OS 自動選定)。
 * dst_port: 受信者の listen ポート / 送信者の送信先ポート (省略不可)。
 * src_if: 送信者が使用するローカル インターフェイス (INADDR_ANY = OS 自動選択)。
 * is_receiver: 1 = 受信者 (INADDR_ANY で bind)、0 = 送信者 (src_if で bind)。
 */
static int open_socket_broadcast(const potr_context *ctx, uint16_t src_port, uint16_t dst_port, const uint32_t src_if,
                                 const int is_receiver, int *rcvbuf_logged, cplat_socket *sock_out,
                                 cplat_socket_watch **watch_out)
{
    cplat_socket sock;
    cplat_ipv4_endpoint addr = {0};
    cplat_error detail;
    int attach_ret;

    /* 受信者: dst_port で bind する。送信者: src_port で bind する (送信元ポート)。 */
    uint16_t bind_port;

    *sock_out = CPLAT_INVALID_SOCKET;
    *watch_out = NULL;

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
        return POTR_ERR_IO;
    }

    /*
     * 受信者は常に固定ポート (dst_port != 0) で bind するため、同一ポートへの複数参加を許可する。
     * 送信者が自動割り当て (src_port == 0) で bind する場合は、同一ホストの複数ソケット間で
     * ローカル ポートが重複するのを防ぐため有効化しない。
     * 失敗しても bind を試行するため、戻り値は明示的に破棄する。
     * see: https://man7.org/linux/man-pages/man7/socket.7.html
     * see: https://learn.microsoft.com/en-us/windows/win32/winsock/using-so-reuseaddr-and-so-exclusiveaddruse
     */
    if (bind_port != 0)
    {
        (void)cplat_socket_set_reuse_address(sock, 1, NULL);
    }

    if (cplat_socket_set_broadcast(sock, 1, &detail) != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "set_broadcast failed");
        cplat_socket_close(sock);
        return POTR_ERR_IO;
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
        return POTR_ERR_IO;
    }

    configure_udp_receive_buffer(sock, ctx, rcvbuf_logged);

    attach_ret = attach_udp_watch(sock, watch_out);
    if (attach_ret != POTR_OK)
    {
        return attach_ret;
    }

    *sock_out = sock;
    return POTR_OK;
}

/* UNICAST: src_addr/dst_addr ペアごとにソケットを作成する。 */
/* Doxygen コメントは、ヘッダーに記載 */
int api_open_paths_unicast(potr_context *ctx, const potr_role role)
{
    int i;
    int result;
    int rcvbuf_logged = 0;

    if (ctx->service.dst_port == 0)
    {
        return POTR_ERR_INVALID_CONFIG;
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
            rollback_udp_paths(ctx);
            return result;
        }

        result = api_paths_resolve_ipv4(ctx->service.dst_addr[i], &ctx->dst_addr_resolved[i]);
        if (result != POTR_OK)
        {
            rollback_udp_paths(ctx);
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

        result = open_socket_unicast(ctx, bind_addr, bind_port, &rcvbuf_logged, &ctx->sock[i], &ctx->udp_watch[i]);
        if (result != POTR_OK)
        {
            rollback_udp_paths(ctx);
            return result;
        }

        ctx->n_path++;
    }

    if (ctx->n_path == 0)
    {
        return POTR_ERR_INVALID_CONFIG;
    }

    return POTR_OK;
}

/* MULTICAST: 経路ごとに参加 NIC を決め、グループ参加済みソケットを作成する。 */
/* Doxygen コメントは、ヘッダーに記載 */
int api_open_paths_multicast(potr_context *ctx, const potr_role role)
{
    int i;
    int result;
    int rcvbuf_logged = 0;

    if (ctx->service.dst_port == 0 || ctx->service.multicast_group[0] == '\0')
    {
        return POTR_ERR_INVALID_CONFIG;
    }

    /*
     * 経路は src_addr が空の位置で列挙を終える。dst_addr だけの経路は使わない。
     * 受信側の参加 NIC は、dst_addr が空でなければその解決値、空なら src_addr の解決値とする。
     * 送信側は src_addr の解決値を参加 NIC とし、dst_addr は解決しない。
     */
    for (i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        uint32_t join_if;

        if (ctx->service.src_addr[i][0] == '\0')
        {
            break;
        }

        result = api_paths_resolve_ipv4(ctx->service.src_addr[i], &ctx->src_addr_resolved[i]);
        if (result != POTR_OK)
        {
            rollback_udp_paths(ctx);
            return result;
        }

        if (role == POTR_ROLE_RECEIVER && ctx->service.dst_addr[i][0] != '\0')
        {
            result = api_paths_resolve_ipv4(ctx->service.dst_addr[i], &join_if);
            if (result != POTR_OK)
            {
                rollback_udp_paths(ctx);
                return result;
            }
        }
        else
        {
            join_if = ctx->src_addr_resolved[i];
        }

        ctx->multicast_if_resolved[i] = join_if;
        result = open_socket_multicast(ctx, i, join_if, role == POTR_ROLE_RECEIVER, &rcvbuf_logged, &ctx->sock[i],
                                       &ctx->udp_watch[i]);
        if (result != POTR_OK)
        {
            rollback_udp_paths(ctx);
            return result;
        }

        ctx->n_path++;
    }

    if (ctx->n_path == 0)
    {
        return POTR_ERR_INVALID_CONFIG;
    }

    return POTR_OK;
}

/* BROADCAST: src_addr ごとにブロードキャスト許可ソケットを作成する。 */
/* Doxygen コメントは、ヘッダーに記載 */
int api_open_paths_broadcast(potr_context *ctx, const potr_role role)
{
    int i;
    int result;
    int rcvbuf_logged = 0;

    if (ctx->service.dst_port == 0)
    {
        return POTR_ERR_INVALID_CONFIG;
    }

    /* broadcast_addr 省略時は限定ブロードキャスト (255.255.255.255) を使用する。 */
    if (ctx->service.broadcast_addr[0] == '\0')
    {
        const char *dflt = "255.255.255.255";
        size_t len = strlen(dflt);
        memcpy(ctx->service.broadcast_addr, dflt, len + 1);
    }

    for (i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        if (ctx->service.src_addr[i][0] == '\0')
        {
            break;
        }

        result = api_paths_resolve_ipv4(ctx->service.src_addr[i], &ctx->src_addr_resolved[i]);
        if (result != POTR_OK)
        {
            rollback_udp_paths(ctx);
            return result;
        }

        result = open_socket_broadcast(ctx, ctx->service.src_port, ctx->service.dst_port, ctx->src_addr_resolved[i],
                                       role == POTR_ROLE_RECEIVER, &rcvbuf_logged, &ctx->sock[i], &ctx->udp_watch[i]);
        if (result != POTR_OK)
        {
            rollback_udp_paths(ctx);
            return result;
        }

        ctx->n_path++;
    }

    if (ctx->n_path == 0)
    {
        return POTR_ERR_INVALID_CONFIG;
    }

    return POTR_OK;
}

/* UNICAST_BIDIR (1:1): 動的 RECEIVER または src/dst ペア ループでソケットを作成する。 */
/* Doxygen コメントは、ヘッダーに記載 */
int api_open_paths_unicast_bidir(potr_context *ctx, const potr_role role)
{
    int result;
    int rcvbuf_logged = 0;

    ctx->is_multi_peer = 0;

    /* dst_port は必須。 */
    if (ctx->service.dst_port == 0)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "potr_service_open: service_id=%" PRId64 " UNICAST_BIDIR requires"
                   " dst_port (non-zero)",
                   ctx->service.service_id);
        return POTR_ERR_INVALID_CONFIG;
    }

    if (role == POTR_ROLE_RECEIVER && ctx->service.src_addr[0][0] == '\0')
    {
        /*
         * 動的 1:1 RECEIVER: src_addr 省略 → dst_addr:dst_port に bind し、
         * 最初の受信パケットから SENDER のアドレスを動的学習する。
         */
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

        result =
            open_socket_unicast(ctx, bind_addr, ctx->service.dst_port, &rcvbuf_logged, &ctx->sock[0], &ctx->udp_watch[0]);
        if (result != POTR_OK)
        {
            rollback_udp_paths(ctx);
            return result;
        }

        ctx->n_path = 1;
        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                   "potr_service_open: service_id=%" PRId64 " UNICAST_BIDIR 1:1 dynamic RECEIVER"
                   " bind dst_port=%u",
                   ctx->service.service_id, (unsigned)ctx->service.dst_port);
        return POTR_OK;
    }

    /*
     * 1:1 モード: src_addr/dst_addr ペア ループ。
     * SENDER は src_addr 省略時に INADDR_ANY で bind する (OS がアダプターを自動選択)。
     * RECEIVER はここには src_addr がある場合のみ到達する。
     */
    {
        int i;

        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            uint32_t bind_addr;

            /*
             * dst_addr が空 → パス終端。
             * RECEIVER は src_addr も必要 (src_addr なし RECEIVER は上で処理済み)。
             */
            if (ctx->service.dst_addr[i][0] == '\0')
            {
                break;
            }
            if (role == POTR_ROLE_RECEIVER && ctx->service.src_addr[i][0] == '\0')
            {
                break;
            }

            if (ctx->service.src_addr[i][0] != '\0')
            {
                result = api_paths_resolve_ipv4(ctx->service.src_addr[i], &ctx->src_addr_resolved[i]);
                if (result != POTR_OK)
                {
                    rollback_udp_paths(ctx);
                    return result;
                }
            }

            result = api_paths_resolve_ipv4(ctx->service.dst_addr[i], &ctx->dst_addr_resolved[i]);
            if (result != POTR_OK)
            {
                rollback_udp_paths(ctx);
                return result;
            }

            if (role == POTR_ROLE_SENDER)
            {
                /* src_addr 省略時は INADDR_ANY で bind し、OS がアダプターを自動選択する。 */
                if (ctx->service.src_addr[i][0] != '\0')
                {
                    bind_addr = ctx->src_addr_resolved[i];
                }
                else
                {
                    bind_addr = CPLAT_IPV4_ADDR_ANY;
                }

                result = open_socket_unicast(ctx, bind_addr, ctx->service.src_port, &rcvbuf_logged, &ctx->sock[i],
                                             &ctx->udp_watch[i]);
            }
            else
            {
                /* RECEIVER: dst_addr:dst_port で bind する。 */
                result = open_socket_unicast(ctx, ctx->dst_addr_resolved[i], ctx->service.dst_port, &rcvbuf_logged,
                                             &ctx->sock[i], &ctx->udp_watch[i]);
            }

            if (result != POTR_OK)
            {
                rollback_udp_paths(ctx);
                return result;
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

/*
 * UNICAST_BIDIR_N1 (N:1 サーバー): dst_addr[i]:dst_port ごとにソケットを bind し、
 * ピア テーブルを初期化する。dst_addr がすべて省略されている場合は INADDR_ANY で
 * 1 ソケットのみ作成する。
 */
/* Doxygen コメントは、ヘッダーに記載 */
int api_open_paths_unicast_bidir_n1(potr_context *ctx)
{
    int i;
    int result;
    int rcvbuf_logged = 0;

    ctx->is_multi_peer = 1;

    /* dst_port は必須。 */
    if (ctx->service.dst_port == 0)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "potr_service_open: service_id=%" PRId64 " UNICAST_BIDIR_N1 requires"
                   " dst_port (non-zero)",
                   ctx->service.service_id);
        return POTR_ERR_INVALID_CONFIG;
    }

    if (ctx->service.dst_addr[0][0] == '\0')
    {
        /* dst_addr すべて省略: INADDR_ANY で 1 ソケットを作成する。 */
        uint32_t any_addr;
        any_addr = CPLAT_IPV4_ADDR_ANY;
        result =
            open_socket_unicast(ctx, any_addr, ctx->service.dst_port, &rcvbuf_logged, &ctx->sock[0], &ctx->udp_watch[0]);
        if (result != POTR_OK)
        {
            rollback_udp_paths(ctx);
            return result;
        }

        ctx->n_path = 1;
    }
    else
    {
        /* dst_addr[i] を列挙してパスごとにソケットを作成する。 */
        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            uint32_t bind_addr;

            if (ctx->service.dst_addr[i][0] == '\0')
            {
                break;
            }

            result = api_paths_resolve_ipv4(ctx->service.dst_addr[i], &bind_addr);
            if (result != POTR_OK)
            {
                rollback_udp_paths(ctx);
                return result;
            }

            ctx->dst_addr_resolved[i] = bind_addr;
            result = open_socket_unicast(ctx, bind_addr, ctx->service.dst_port, &rcvbuf_logged, &ctx->sock[i],
                                         &ctx->udp_watch[i]);
            if (result != POTR_OK)
            {
                rollback_udp_paths(ctx);
                return result;
            }

            ctx->n_path = i + 1;
        }
    }

    /* ピア テーブルを初期化する。 */
    ctx->max_peers = (int)ctx->service.max_peers;
    if (ctx->max_peers <= 0)
    {
        ctx->max_peers = (int)POTR_DEFAULT_MAX_PEERS;
    }

    result = potr_internal_peer_table_init(ctx);
    if (result != POTR_OK)
    {
        rollback_udp_paths(ctx);
        return result;
    }

    POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
               "potr_service_open: service_id=%" PRId64 " UNICAST_BIDIR_N1"
               " (max_peers=%d src_port_filter=%u) bind dst_port=%u n_path=%d",
               ctx->service.service_id, ctx->max_peers, (unsigned)ctx->service.src_port,
               (unsigned)ctx->service.dst_port, ctx->n_path);

    return POTR_OK;
}
