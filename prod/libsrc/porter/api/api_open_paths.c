/** @file api_open_paths.c
 *  @brief 通信種別ごとのソケットと送受信アドレスを準備します。
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

/* cplat のアドレス解決/変換結果コードを porter の結果コードへ変換する。
 * cplat_ipv4_resolve() / cplat_ipv4_parse() は CPLAT_ERR_INVALID_ARGUMENT または
 * CPLAT_ERR_UNKNOWN のみを返し、失敗時の詳細エラーはソケット由来ではない (GAI ドメイン等) ため
 * potr_internal_result_from_error() の要因ベース変換は適用しない。 */
static int result_from_ipv4_result(int cplat_result)
{
    if (cplat_result == CPLAT_OK)
    {
        return POTR_OK;
    }
    if (cplat_result == CPLAT_ERR_INVALID_ARGUMENT)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    return POTR_ERR_IO;
}

/* ホスト名または IPv4 アドレス文字列を解決する。旧 resolve_ipv4() 相当の薄いラッパー。 */
static int resolve_ipv4(const char *host, uint32_t *address_out)
{
    cplat_error detail;
    int result = cplat_ipv4_resolve(host, address_out, &detail);

    if (result != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "resolve_ipv4(%s) failed", host);
    }
    return result_from_ipv4_result(result);
}

/* ドット区切りの IPv4 アドレス文字列を解析する。旧 parse_ipv4_addr() 相当の薄いラッパー。 */
static int parse_ipv4(const char *text, uint32_t *address_out)
{
    return result_from_ipv4_result(cplat_ipv4_parse(text, address_out));
}

/* ソケットを作成して bind する。成功時はソケットを返す。失敗時は CPLAT_INVALID_SOCKET。
   bind_addr: bind する IPv4 アドレス (ネットワーク バイト オーダー)。port: bind するポート番号 (0 = OS 自動選定)。 */
static cplat_socket open_socket_unicast(uint32_t bind_addr, uint16_t port)
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
   src_if: 使用するローカル インターフェース (INADDR_ANY = OS 自動選択)。
   is_receiver: 1 = 受信者、0 = 送信者。 */
static cplat_socket open_socket_multicast(const potr_service_def *def, uint32_t src_if, int is_receiver)
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
    if (parse_ipv4(def->multicast_group, &group_addr) != POTR_OK)
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

    /* 送信者: マルチキャスト送信インターフェースを設定する */
    if (!is_receiver)
    {
        (void)cplat_socket_set_multicast_interface(sock, src_if, NULL);
    }

    return sock;
}

/* ブロードキャスト ソケットを作成して bind する。
   src_port: 送信者の送信元 bind ポート (0 = OS 自動選定)。
   dst_port: 受信者の listen ポート / 送信者の送信先ポート (省略不可)。
   src_if: 送信者が使用するローカル インターフェース (INADDR_ANY = OS 自動選択)。
   is_receiver: 1 = 受信者 (INADDR_ANY で bind)、0 = 送信者 (src_if で bind)。 */
static cplat_socket open_socket_broadcast(uint16_t src_port, uint16_t dst_port, uint32_t src_if, int is_receiver)
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

    /* 送信者: src_addr で bind してインターフェースを選択する。受信者: INADDR_ANY で bind する。 */
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

/* TCP RECEIVER: path_idx 番目の listen ソケットを作成して bind・listen する。
   dst_addr[path_idx] が指定されていれば dst_addr_resolved[path_idx] に解決する。
   src_addr[path_idx] が指定されていれば src_addr_resolved[path_idx] にも解決する (接続元フィルター用)。
   成功時は ctx->tcp_listen_sock[path_idx] に格納して POTR_OK を返す。 */
static int open_socket_tcp_receiver(potr_context *ctx, int path_idx)
{
    cplat_socket sock;
    cplat_ipv4_endpoint addr = {0};
    uint32_t bind_ip;
    int result;
    cplat_error detail;

    if (ctx->service.dst_addr[path_idx][0] != '\0')
    {
        result = resolve_ipv4(ctx->service.dst_addr[path_idx], &bind_ip);
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
        result = resolve_ipv4(ctx->service.src_addr[path_idx], &ctx->src_addr_resolved[path_idx]);
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
static int open_socket_tcp_sender(potr_context *ctx, int path_idx)
{
    int result;

    if (ctx->service.dst_addr[path_idx][0] == '\0')
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "open_socket_tcp_sender: dst_addr[%d] is empty", path_idx);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    result = resolve_ipv4(ctx->service.dst_addr[path_idx], &ctx->dst_addr_resolved[path_idx]);
    if (result != POTR_OK)
    {
        return result;
    }

    if (ctx->service.src_addr[path_idx][0] != '\0')
    {
        result = resolve_ipv4(ctx->service.src_addr[path_idx], &ctx->src_addr_resolved[path_idx]);
        if (result != POTR_OK)
        {
            return result;
        }
    }

    return POTR_OK;
}

/* UNICAST: src_addr/dst_addr ペアごとにソケットを作成する */
static int open_paths_unicast(potr_context *ctx, potr_role role)
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

        result = resolve_ipv4(ctx->service.src_addr[i], &ctx->src_addr_resolved[i]);
        if (result != POTR_OK)
        {
            return result;
        }
        result = resolve_ipv4(ctx->service.dst_addr[i], &ctx->dst_addr_resolved[i]);
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
static int open_paths_multicast(potr_context *ctx, potr_role role)
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

        result = resolve_ipv4(ctx->service.src_addr[i], &ctx->src_addr_resolved[i]);
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
static int open_paths_broadcast(potr_context *ctx, potr_role role)
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

        result = resolve_ipv4(ctx->service.src_addr[i], &ctx->src_addr_resolved[i]);
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
static int open_paths_unicast_bidir(potr_context *ctx, potr_role role)
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
            result = resolve_ipv4(ctx->service.dst_addr[0], &bind_addr);
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
                result = resolve_ipv4(ctx->service.src_addr[i], &ctx->src_addr_resolved[i]);
                if (result != POTR_OK)
                {
                    return result;
                }
            }

            result = resolve_ipv4(ctx->service.dst_addr[i], &ctx->dst_addr_resolved[i]);
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
static int open_paths_unicast_bidir_n1(potr_context *ctx)
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

            result = resolve_ipv4(ctx->service.dst_addr[i], &bind_addr);
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

/* TCP / TCP_BIDIR: RECEIVER は listen ソケット作成、SENDER はアドレス解決のみ行う */
static int open_paths_tcp(potr_context *ctx, potr_role role)
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

/* Doxygen コメントは、ヘッダーに記載 */

int api_open_paths_by_type(potr_context *ctx, potr_role role)
{
    switch (potr_raw_base_type(ctx->service.type))
    {
    case POTR_TYPE_UNICAST:
        return open_paths_unicast(ctx, role);
    case POTR_TYPE_MULTICAST:
        return open_paths_multicast(ctx, role);
    case POTR_TYPE_BROADCAST:
        return open_paths_broadcast(ctx, role);
    case POTR_TYPE_UNICAST_BIDIR:
        return open_paths_unicast_bidir(ctx, role);
    case POTR_TYPE_UNICAST_BIDIR_N1:
        return open_paths_unicast_bidir_n1(ctx);
    case POTR_TYPE_TCP:
    case POTR_TYPE_TCP_BIDIR:
        return open_paths_tcp(ctx, role);
    case POTR_TYPE_UNICAST_RAW:
    case POTR_TYPE_MULTICAST_RAW:
    case POTR_TYPE_BROADCAST_RAW:
        /* potr_raw_base_type() は RAW 型をベース型に変換するため、ここには到達しない */
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

    switch (potr_raw_base_type(ctx->service.type))
    {
    case POTR_TYPE_UNICAST_BIDIR:
    case POTR_TYPE_UNICAST_BIDIR_N1:
        for (i = 0; i < ctx->n_path; i++)
        {
            potr_endpoint_clear(&ctx->dest_addr[i]);
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
            potr_endpoint_clear(&ctx->dest_addr[i]);
            ctx->dest_addr[i].address = ctx->dst_addr_resolved[i];
            ctx->dest_addr[i].port = cplat_hton16(ctx->service.dst_port);
        }
        break;

    case POTR_TYPE_MULTICAST:
    {
        uint32_t mcast_ip;
        if (parse_ipv4(ctx->service.multicast_group, &mcast_ip) != POTR_OK)
        {
            return POTR_ERR_INVALID_ARGUMENT;
        }
        for (i = 0; i < ctx->n_path; i++)
        {
            potr_endpoint_clear(&ctx->dest_addr[i]);
            ctx->dest_addr[i].address = mcast_ip;
            ctx->dest_addr[i].port = cplat_hton16(ctx->service.dst_port);
        }
        break;
    }

    case POTR_TYPE_BROADCAST:
    {
        uint32_t bcast_ip;
        if (parse_ipv4(ctx->service.broadcast_addr, &bcast_ip) != POTR_OK)
        {
            return POTR_ERR_INVALID_ARGUMENT;
        }
        for (i = 0; i < ctx->n_path; i++)
        {
            potr_endpoint_clear(&ctx->dest_addr[i]);
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
        /* potr_raw_base_type() は RAW 型をベース型に変換するため、ここには到達しない */
        /* fall through */
    default:
        break;
    }

    return POTR_OK;
}
