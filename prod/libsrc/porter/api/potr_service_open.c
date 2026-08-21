/**
 *******************************************************************************
 *  @file           potr_service_open.c
 *  @brief          porter サービスを開始する potr_service_open 関数を提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>
#include <com_util/crt/stdlib.h>
#include <com_util/crypto/random.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/porter_spec.h>

#include <porter/protocol/packet.h>
#include <porter/protocol/window.h>
#include <porter/potr_context.h>
#include <porter/potr_path_event.h>
#include <porter/potr_peer_table.h>
#include <porter/thread/potr_recv_thread.h>
#include <porter/thread/potr_health_thread.h>
#include <porter/thread/potr_connect_thread.h>
#include <porter/infra/potr_send_queue.h>
#include <porter/thread/potr_send_thread.h>
#include <porter/infra/potr_trace.h>
#include <porter/infra/potr_result.h>
#include <com_util/net/byteorder.h>
#include <com_util/net/endpoint.h>
#include <com_util/net/socket.h>

/* com_util のアドレス解決/変換結果コードを porter の結果コードへ変換する。
 * com_util_ipv4_resolve() / com_util_ipv4_parse() は COM_UTIL_ERR_INVALID_ARGUMENT または
 * COM_UTIL_ERR_UNKNOWN のみを返し、失敗時の詳細エラーはソケット由来ではない (GAI ドメイン等) ため
 * potr_internal_result_from_error() の要因ベース変換は適用しない。 */
static int result_from_ipv4_result(int com_util_result)
{
    if (com_util_result == COM_UTIL_OK)
    {
        return POTR_OK;
    }
    if (com_util_result == COM_UTIL_ERR_INVALID_ARGUMENT)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    return POTR_ERR_IO;
}

/* ホスト名または IPv4 アドレス文字列を解決する。旧 resolve_ipv4() 相当の薄いラッパー。 */
static int resolve_ipv4(const char *host, uint32_t *address_out)
{
    com_util_error detail;
    int result = com_util_ipv4_resolve(host, address_out, &detail);

    if (result != COM_UTIL_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_ERROR, &detail, "resolve_ipv4(%s) failed", host);
    }
    return result_from_ipv4_result(result);
}

/* ドット区切りの IPv4 アドレス文字列を解析する。旧 parse_ipv4_addr() 相当の薄いラッパー。 */
static int parse_ipv4(const char *text, uint32_t *address_out)
{
    return result_from_ipv4_result(com_util_ipv4_parse(text, address_out));
}

/* ソケットを作成して bind する。成功時はソケットを返す。失敗時は COM_UTIL_INVALID_SOCKET。
   bind_addr: bind する IPv4 アドレス (ネットワーク バイト オーダー)。port: bind するポート番号 (0 = OS 自動選定)。 */
static com_util_socket open_socket_unicast(uint32_t bind_addr, uint16_t port)
{
    com_util_socket sock;
    com_util_ipv4_endpoint addr = {0};
    com_util_error detail;

    if (com_util_socket_open(COM_UTIL_SOCKET_UDP, &sock, &detail) != COM_UTIL_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_ERROR, &detail, "socket failed");
        return COM_UTIL_INVALID_SOCKET;
    }

    /* SO_REUSEADDR は互換性向上のための best-effort 設定であり、失敗しても bind を試行します。 */
    (void)com_util_socket_set_reuse_address(sock, 1, NULL);

    addr.address = bind_addr;
    addr.port = com_util_hton16(port);

    if (com_util_socket_bind(sock, &addr, &detail) != COM_UTIL_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_ERROR, &detail, "bind failed");
        com_util_socket_close(sock);
        return COM_UTIL_INVALID_SOCKET;
    }

    return sock;
}

/* セッション識別子と開始時刻を生成してコンテキストに格納する。
 * session_id は AES-256-GCM nonce の非決定要素であり、衝突または推測は
 * (key, nonce) の再利用を招く。暗号論的乱数源から取得する。 */
static int generate_session(potr_context *ctx)
{
    int ret = com_util_random_bytes(&ctx->session_id, sizeof(ctx->session_id));

    if (ret != COM_UTIL_OK)
    {
        return ret;
    }
    com_util_get_realtime(&ctx->session_ts);

    ctx->last_ping_send_ms = 0U;
    ctx->last_valid_data_send_ms = 0U;

    return COM_UTIL_OK;
}

/* マルチキャスト ソケットを作成して bind・グループ参加する。
   src_if: 使用するローカル インターフェース (INADDR_ANY = OS 自動選択)。
   is_receiver: 1 = 受信者、0 = 送信者。 */
static com_util_socket open_socket_multicast(const potr_service_def *def, uint32_t src_if, int is_receiver)
{
    com_util_socket sock;
    com_util_ipv4_endpoint addr = {0};
    uint32_t group_addr;
    com_util_error detail;
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

    if (com_util_socket_open(COM_UTIL_SOCKET_UDP, &sock, &detail) != COM_UTIL_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_ERROR, &detail, "socket failed");
        return COM_UTIL_INVALID_SOCKET;
    }

    /* SO_REUSEADDR は互換性向上のための best-effort 設定であり、失敗しても bind を試行します。 */
    (void)com_util_socket_set_reuse_address(sock, 1, NULL);

    addr.address = COM_UTIL_IPV4_ADDR_ANY;
    addr.port = com_util_hton16(bind_port);

    if (com_util_socket_bind(sock, &addr, &detail) != COM_UTIL_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_ERROR, &detail, "bind failed");
        com_util_socket_close(sock);
        return COM_UTIL_INVALID_SOCKET;
    }

    /* マルチキャスト グループへ参加 (送受信ともに参加する) */
    if (parse_ipv4(def->multicast_group, &group_addr) != POTR_OK)
    {
        com_util_socket_close(sock);
        return COM_UTIL_INVALID_SOCKET;
    }

    if (com_util_socket_join_multicast_group(sock, group_addr, src_if, &detail) != COM_UTIL_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_ERROR, &detail, "join_multicast_group failed");
        com_util_socket_close(sock);
        return COM_UTIL_INVALID_SOCKET;
    }

    /* 送信者: マルチキャスト送信インターフェースを設定する */
    if (!is_receiver)
    {
        (void)com_util_socket_set_multicast_interface(sock, src_if, NULL);
    }

    return sock;
}

/* ブロードキャスト ソケットを作成して bind する。
   src_port: 送信者の送信元 bind ポート (0 = OS 自動選定)。
   dst_port: 受信者の listen ポート / 送信者の送信先ポート (省略不可)。
   src_if: 送信者が使用するローカル インターフェース (INADDR_ANY = OS 自動選択)。
   is_receiver: 1 = 受信者 (INADDR_ANY で bind)、0 = 送信者 (src_if で bind)。 */
static com_util_socket open_socket_broadcast(uint16_t src_port, uint16_t dst_port, uint32_t src_if, int is_receiver)
{
    com_util_socket sock;
    com_util_ipv4_endpoint addr = {0};
    com_util_error detail;
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

    if (com_util_socket_open(COM_UTIL_SOCKET_UDP, &sock, &detail) != COM_UTIL_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_ERROR, &detail, "socket failed");
        return COM_UTIL_INVALID_SOCKET;
    }

    /* SO_REUSEADDR は互換性向上のための best-effort 設定であり、失敗しても bind を試行します。 */
    (void)com_util_socket_set_reuse_address(sock, 1, NULL);
    if (com_util_socket_set_broadcast(sock, 1, &detail) != COM_UTIL_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_ERROR, &detail, "set_broadcast failed");
        com_util_socket_close(sock);
        return COM_UTIL_INVALID_SOCKET;
    }

    /* 送信者: src_addr で bind してインターフェースを選択する。受信者: INADDR_ANY で bind する。 */
    if (!is_receiver)
    {
        addr.address = src_if;
    }
    else
    {
        addr.address = COM_UTIL_IPV4_ADDR_ANY;
    }
    addr.port = com_util_hton16(bind_port);

    if (com_util_socket_bind(sock, &addr, &detail) != COM_UTIL_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_ERROR, &detail, "bind failed");
        com_util_socket_close(sock);
        return COM_UTIL_INVALID_SOCKET;
    }

    return sock;
}

/* 生成済みソケットをすべてクローズする */
static void cleanup_sockets(potr_context *ctx)
{
    int i;
    for (i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        if (ctx->sock[i] != COM_UTIL_INVALID_SOCKET)
        {
            com_util_socket_close(ctx->sock[i]);
            ctx->sock[i] = COM_UTIL_INVALID_SOCKET;
        }
    }
}

/* コンテキストが保持するすべてのリソースを解放して ctx 本体を free する。
   memset(ctx, 0, ...) 後であれば、未初期化ポインター (NULL) に対しても安全に呼び出せる。 */
static void ctx_cleanup(potr_context *ctx)
{
    potr_internal_callback_mutex_dispose(ctx);
    potr_internal_window_dispose(&ctx->send_window);
    potr_internal_window_dispose(&ctx->recv_window);
    com_util_free(ctx->frag_buf);
    com_util_free(ctx->compress_buf);
    com_util_free(ctx->crypto_buf);
    com_util_free(ctx->recv_buf);
    com_util_free(ctx->send_wire_buf);
    if (ctx->is_multi_peer && ctx->peers != NULL)
    {
        potr_internal_peer_table_dispose(ctx);
    }
    /* TCP listen ソケットをクローズ (path ごと) */
    {
        int i;
        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            if (ctx->tcp_listen_sock[i] != COM_UTIL_INVALID_SOCKET)
            {
                com_util_socket_close(ctx->tcp_listen_sock[i]);
            }
        }
    }
    {
        int i;
        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            if (ctx->tcp_conn_fd[i] != COM_UTIL_INVALID_SOCKET)
            {
                com_util_socket_close(ctx->tcp_conn_fd[i]);
            }
        }
    }
    cleanup_sockets(ctx);
    com_util_free(ctx);
}

/* TCP RECEIVER: path_idx 番目の listen ソケットを作成して bind・listen する。
   dst_addr[path_idx] が指定されていれば dst_addr_resolved[path_idx] に解決する。
   src_addr[path_idx] が指定されていれば src_addr_resolved[path_idx] にも解決する (接続元フィルター用)。
   成功時は ctx->tcp_listen_sock[path_idx] に格納して POTR_OK を返す。 */
static int open_socket_tcp_receiver(potr_context *ctx, int path_idx)
{
    com_util_socket sock;
    com_util_ipv4_endpoint addr = {0};
    uint32_t bind_ip;
    int result;
    com_util_error detail;

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
        bind_ip = COM_UTIL_IPV4_ADDR_ANY;
    }

    if (ctx->service.src_addr[path_idx][0] != '\0')
    {
        result = resolve_ipv4(ctx->service.src_addr[path_idx], &ctx->src_addr_resolved[path_idx]);
        if (result != POTR_OK)
        {
            return result;
        }
    }

    result = com_util_socket_open(COM_UTIL_SOCKET_TCP, &sock, &detail);
    if (result != COM_UTIL_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_ERROR, &detail, "socket failed");
        return potr_internal_result_from_error(&detail);
    }

    /* SO_REUSEADDR は互換性向上のための best-effort 設定であり、失敗しても bind を試行します。 */
    (void)com_util_socket_set_reuse_address(sock, 1, NULL);

    addr.address = bind_ip;
    addr.port = com_util_hton16(ctx->service.dst_port);

    result = com_util_socket_bind(sock, &addr, &detail);
    if (result != COM_UTIL_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_ERROR, &detail, "bind failed");
        com_util_socket_close(sock);
        return potr_internal_result_from_error(&detail);
    }

    result = com_util_socket_listen(sock, COM_UTIL_SOCKET_BACKLOG_DEFAULT, &detail);
    if (result != COM_UTIL_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_ERROR, &detail, "listen failed");
        com_util_socket_close(sock);
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
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR, "open_socket_tcp_sender: dst_addr[%d] is empty", path_idx);
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

/* callback と role・通信種別の整合性を検証する (ctx->service 確定後に呼ぶ) */
static int open_validate_callback(const potr_context *ctx, potr_role role, potr_recv_fn callback)
{
    if (role == POTR_ROLE_SENDER && callback != NULL && ctx->service.type != POTR_TYPE_UNICAST_BIDIR &&
        ctx->service.type != POTR_TYPE_TCP_BIDIR)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                   "potr_service_open: service_id=%" PRId64 " SENDER role must not have callback"
                   " (type=%d)",
                   ctx->service.service_id, (int)ctx->service.type);
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (role == POTR_ROLE_SENDER && callback == NULL &&
        (ctx->service.type == POTR_TYPE_UNICAST_BIDIR || ctx->service.type == POTR_TYPE_TCP_BIDIR))
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                   "potr_service_open: service_id=%" PRId64 " bidirectional SENDER role requires callback"
                   " (type=%d)",
                   ctx->service.service_id, (int)ctx->service.type);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    return POTR_OK;
}

/* 設定値を検証し、実効ヘルスチェック間隔・タイムアウトを解決する */
static int open_validate_config(potr_context *ctx)
{
    if (ctx->global.max_payload < 64U || ctx->global.max_payload > POTR_MAX_PAYLOAD)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                   "potr_service_open: service_id=%" PRId64 " invalid max_payload=%u (range: 64..%u)",
                   ctx->service.service_id, (unsigned)ctx->global.max_payload, (unsigned)POTR_MAX_PAYLOAD);
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (ctx->global.window_size < 2U || ctx->global.window_size > POTR_MAX_WINDOW_SIZE)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                   "potr_service_open: service_id=%" PRId64 " invalid window_size=%u (range: 2..%u)",
                   ctx->service.service_id, (unsigned)ctx->global.window_size, (unsigned)POTR_MAX_WINDOW_SIZE);
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (ctx->global.max_message_size < (uint32_t)ctx->global.max_payload)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                   "potr_service_open: service_id=%" PRId64 " max_message_size=%u must be >= max_payload=%u",
                   ctx->service.service_id, (unsigned)ctx->global.max_message_size, (unsigned)ctx->global.max_payload);
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (ctx->global.send_queue_depth < 2U)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                   "potr_service_open: service_id=%" PRId64 " invalid send_queue_depth=%u (min: 2)",
                   ctx->service.service_id, (unsigned)ctx->global.send_queue_depth);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    /* 通信種別ごとのグローバル既定値を選び、サービス単位設定で実効値を上書きする。 */
    if (potr_is_tcp_type(ctx->service.type))
    {
        ctx->health_interval_ms = ctx->global.tcp_health_interval_ms;
        ctx->health_timeout_ms = ctx->global.tcp_health_timeout_ms;
    }
    else
    {
        ctx->health_interval_ms = ctx->global.udp_health_interval_ms;
        ctx->health_timeout_ms = ctx->global.udp_health_timeout_ms;
    }

    if (ctx->service.health_interval_ms != 0U)
    {
        ctx->health_interval_ms = ctx->service.health_interval_ms;
    }
    if (ctx->service.health_timeout_ms != 0U)
    {
        ctx->health_timeout_ms = ctx->service.health_timeout_ms;
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
        if (ctx->sock[i] == COM_UTIL_INVALID_SOCKET)
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
        if (ctx->sock[i] == COM_UTIL_INVALID_SOCKET)
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
        if (ctx->sock[i] == COM_UTIL_INVALID_SOCKET)
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
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
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
            bind_addr = COM_UTIL_IPV4_ADDR_ANY;
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
        if (ctx->sock[0] == COM_UTIL_INVALID_SOCKET)
        {
            return POTR_ERR_IO;
        }
        ctx->n_path = 1;
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
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
                    bind_addr = COM_UTIL_IPV4_ADDR_ANY;
                }
                ctx->sock[i] = open_socket_unicast(bind_addr, ctx->service.src_port);
            }
            else
            {
                /* RECEIVER: dst_addr:dst_port で bind */
                ctx->sock[i] = open_socket_unicast(ctx->dst_addr_resolved[i], ctx->service.dst_port);
            }
            if (ctx->sock[i] == COM_UTIL_INVALID_SOCKET)
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
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                   "potr_service_open: service_id=%" PRId64 " UNICAST_BIDIR_N1 requires"
                   " dst_port (non-zero)",
                   ctx->service.service_id);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (ctx->service.dst_addr[0][0] == '\0')
    {
        /* dst_addr すべて省略: INADDR_ANY で 1 ソケット */
        uint32_t any_addr;
        any_addr = COM_UTIL_IPV4_ADDR_ANY;
        ctx->sock[0] = open_socket_unicast(any_addr, ctx->service.dst_port);
        if (ctx->sock[0] == COM_UTIL_INVALID_SOCKET)
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
            if (ctx->sock[i] == COM_UTIL_INVALID_SOCKET)
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

    POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
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
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR, "potr_service_open: service_id=%" PRId64 " TCP requires dst_port",
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
                POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                           "potr_service_open: service_id=%" PRId64 " TCP listen failed"
                           " (path=%d dst_addr=%s dst_port=%u)",
                           ctx->service.service_id, i, ctx->service.dst_addr[i], (unsigned)ctx->service.dst_port);
                return result;
            }
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
                       "potr_service_open: service_id=%" PRId64 " TCP path[%d] listening"
                       " on %s:%u",
                       ctx->service.service_id, i, ctx->service.dst_addr[i], (unsigned)ctx->service.dst_port);
            ctx->n_path = i + 1;
        }
        if (ctx->n_path == 0)
        {
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
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
                POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                           "potr_service_open: service_id=%" PRId64 " TCP sender"
                           " dst_addr resolve failed (path=%d %s)",
                           ctx->service.service_id, i, ctx->service.dst_addr[i]);
                return result;
            }
            ctx->n_path = i + 1;
        }
        if (ctx->n_path == 0)
        {
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                       "potr_service_open: service_id=%" PRId64 " TCP SENDER requires"
                       " at least one dst_addr",
                       ctx->service.service_id);
            return POTR_ERR_INVALID_ARGUMENT;
        }
    }

    return POTR_OK;
}

/* 通信種別に応じてソケット・パスを準備する (RAW 型はベース型に正規化してから判定) */
static int open_paths_by_type(potr_context *ctx, potr_role role)
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

/* 送信先ソケット アドレス (dest_addr) を設定する (RAW 型はベース型に正規化してから判定)。
   UNICAST_BIDIR は両端 (SENDER / RECEIVER) ともに dest_addr を設定する。 */
static int setup_dest_addr(potr_context *ctx, potr_role role)
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
                ctx->dest_addr[i].port = com_util_hton16(ctx->service.dst_port);
            }
            else
            {
                /* RECEIVER: src_addr:src_port (SENDER の bind アドレス) へ送信 */
                ctx->dest_addr[i].address = ctx->src_addr_resolved[i];
                ctx->dest_addr[i].port = com_util_hton16(ctx->service.src_port);
            }
        }
        break;

    case POTR_TYPE_UNICAST:
        for (i = 0; i < ctx->n_path; i++)
        {
            potr_endpoint_clear(&ctx->dest_addr[i]);
            ctx->dest_addr[i].address = ctx->dst_addr_resolved[i];
            ctx->dest_addr[i].port = com_util_hton16(ctx->service.dst_port);
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
            ctx->dest_addr[i].port = com_util_hton16(ctx->service.dst_port);
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
            ctx->dest_addr[i].port = com_util_hton16(ctx->service.dst_port);
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

/* 送受信ウィンドウを初期化し、動的バッファーを確保する */
static int alloc_context_buffers(potr_context *ctx)
{
    int result;

    result = potr_internal_window_init(&ctx->send_window, 0, ctx->global.window_size, ctx->global.max_payload);
    if (result != POTR_OK)
    {
        return result;
    }
    ctx->send_has_data = 0;
    result = potr_internal_window_init(&ctx->recv_window, 0, ctx->global.window_size, ctx->global.max_payload);
    if (result != POTR_OK)
    {
        return result;
    }

    ctx->frag_buf = (uint8_t *)com_util_malloc(ctx->global.max_message_size);
    if (ctx->frag_buf == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    ctx->compress_buf_size = COM_UTIL_COMPRESS_HEADER_SIZE + (size_t)ctx->global.max_message_size + 64U;
    ctx->compress_buf = (uint8_t *)com_util_malloc(ctx->compress_buf_size);
    if (ctx->compress_buf == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    ctx->recv_buf = (uint8_t *)com_util_malloc(PACKET_HEADER_SIZE + ctx->global.max_payload);
    if (ctx->recv_buf == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    ctx->send_wire_buf = (uint8_t *)com_util_malloc(PACKET_HEADER_SIZE + ctx->global.max_payload);
    if (ctx->send_wire_buf == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    ctx->crypto_buf_size = ctx->global.max_payload + POTR_CRYPTO_TAG_SIZE;
    ctx->crypto_buf = (uint8_t *)com_util_malloc(ctx->crypto_buf_size);
    if (ctx->crypto_buf == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    return POTR_OK;
}

/* TCP 用同期プリミティブ群を破棄する (start_threads_tcp のエラー巻き戻し用) */
static void destroy_tcp_sync_primitives(potr_context *ctx)
{
    int i;

    com_util_local_lock_dispose(ctx->tcp_state_mutex);
    com_util_condvar_dispose(ctx->tcp_state_cv);
    com_util_local_lock_dispose(ctx->tcp_close_mutex);
    com_util_condvar_dispose(ctx->tcp_close_cv);
    for (i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        com_util_local_lock_dispose(ctx->tcp_send_mutex[i]);
    }
    com_util_local_lock_dispose(ctx->recv_window_mutex);
}

/* TCP: 同期プリミティブと送信キューを初期化し、接続管理スレッドを起動する。
   send/recv/health スレッドは接続確立後に connect スレッドが管理する。 */
static int start_threads_tcp(potr_context *ctx, potr_role role)
{
    int result;

    /* tcp_state_mutex / tcp_state_cv / tcp_close_mutex / tcp_close_cv /
       tcp_send_mutex[] / recv_window_mutex /
       health_mutex[] / health_wakeup[] を初期化 */
    {
        int i;
        com_util_local_lock_create(&ctx->tcp_state_mutex);
        com_util_condvar_create(&ctx->tcp_state_cv);
        com_util_local_lock_create(&ctx->tcp_close_mutex);
        com_util_condvar_create(&ctx->tcp_close_cv);
        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            com_util_local_lock_create(&ctx->tcp_send_mutex[i]);
            com_util_local_lock_create(&ctx->health_mutex[i]);
            com_util_condvar_create(&ctx->health_wakeup[i]);
        }
        com_util_local_lock_create(&ctx->recv_window_mutex);
    }

    /* SENDER または TCP_BIDIR: 送信キューを初期化 (connect スレッドが reconnect 時に dispose+init する) */
    if (role == POTR_ROLE_SENDER || ctx->service.type == POTR_TYPE_TCP_BIDIR)
    {
        result = potr_internal_send_queue_init(&ctx->send_queue, (size_t)ctx->global.send_queue_depth, ctx->global.max_payload);
        if (result != POTR_OK)
        {
            destroy_tcp_sync_primitives(ctx);
            return result;
        }
    }

    result = potr_internal_connect_thread_start(ctx);
    if (result != POTR_OK)
    {
        if (role == POTR_ROLE_SENDER || ctx->service.type == POTR_TYPE_TCP_BIDIR)
        {
            potr_internal_send_queue_dispose(&ctx->send_queue);
        }
        destroy_tcp_sync_primitives(ctx);
        return result;
    }

    return POTR_OK;
}

/* 非 TCP: 役割と通信種別に応じて受信・送信・ヘルスチェック スレッドを起動する */
static int start_threads_udp(potr_context *ctx, potr_role role)
{
    int result;

    /* 非 TCP: 受信者の場合は受信スレッドのみ起動
       ただし UNICAST_BIDIR / UNICAST_BIDIR_N1 の RECEIVER は
       下の全スレッド起動ブロックで処理する */
    if (role == POTR_ROLE_RECEIVER && ctx->service.type != POTR_TYPE_UNICAST_BIDIR &&
        ctx->service.type != POTR_TYPE_UNICAST_BIDIR_N1)
    {
        result = potr_internal_comm_recv_thread_start(ctx);
        if (result != POTR_OK)
        {
            return result;
        }
    }

    /* 非 TCP 送信者 / UNICAST_BIDIR / UNICAST_BIDIR_N1 受信者:
       送信キュー・送信スレッド・ヘルスチェック スレッド・受信スレッドを起動 */
    if (role == POTR_ROLE_SENDER || ctx->service.type == POTR_TYPE_UNICAST_BIDIR ||
        ctx->service.type == POTR_TYPE_UNICAST_BIDIR_N1)
    {
        result = potr_internal_send_queue_init(&ctx->send_queue, (size_t)ctx->global.send_queue_depth, ctx->global.max_payload);
        if (result != POTR_OK)
        {
            return result;
        }

        result = potr_internal_send_thread_start(ctx);
        if (result != POTR_OK)
        {
            potr_internal_send_queue_dispose(&ctx->send_queue);
            return result;
        }

        ctx->health_send_immediate[0] = 0;
        if (potr_type_uses_immediate_health_ping(ctx->service.type))
        {
            ctx->health_send_immediate[0] = 1;
        }
        result = potr_internal_health_thread_start(ctx);
        if (result != POTR_OK)
        {
            potr_internal_send_thread_stop(ctx);
            potr_internal_send_queue_dispose(&ctx->send_queue);
            return result;
        }

        result = potr_internal_comm_recv_thread_start(ctx);
        if (result != POTR_OK)
        {
            potr_internal_health_thread_stop(ctx);
            potr_internal_send_thread_stop(ctx);
            potr_internal_send_queue_dispose(&ctx->send_queue);
            return result;
        }
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_service_open(const potr_global_config *global, const potr_service_def *service, potr_role role,
                    potr_recv_fn callback, potr_context **handle)
{
    potr_context *ctx;
    int result;
    int start_result;

    if (global == NULL || service == NULL || handle == NULL)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR, "potr_service_open: invalid argument (global=%p service=%p handle=%p)",
                   (const void *)global, (const void *)service, (const void *)handle);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE, "potr_service_open: service_id=%" PRId64 " role=%d", service->service_id,
               (int)role);

    /* role と callback の整合性チェック (設定読み込み前に確定できる部分のみ) */
    if (role == POTR_ROLE_RECEIVER && callback == NULL)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                   "potr_service_open: service_id=%" PRId64 " RECEIVER role requires callback", service->service_id);
        return POTR_ERR_INVALID_ARGUMENT;
    }
    /* SENDER + callback の完全チェックは設定読み込み後に行う
       (POTR_TYPE_UNICAST_BIDIR の SENDER は callback が必須のため) */
    if (role != POTR_ROLE_SENDER && role != POTR_ROLE_RECEIVER)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR, "potr_service_open: service_id=%" PRId64 " unknown role=%d",
                   service->service_id, (int)role);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    ctx = (potr_context *)com_util_malloc(sizeof(potr_context));
    if (ctx == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    potr_internal_callback_mutex_init(ctx);

    /* 全ソケットを INVALID で初期化 */
    {
        int i;
        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            ctx->sock[i] = COM_UTIL_INVALID_SOCKET;
            ctx->tcp_conn_fd[i] = COM_UTIL_INVALID_SOCKET;
            ctx->tcp_listen_sock[i] = COM_UTIL_INVALID_SOCKET;
        }
    }

    /* グローバル設定とサービス定義をコンテキストにコピー */
    memcpy(&ctx->global, global, sizeof(potr_global_config));
    memcpy(&ctx->service, service, sizeof(potr_service_def));

    /* SENDER + callback の整合性チェック (型が確定した後) と設定値バリデーション */
    result = open_validate_callback(ctx, role, callback);
    if (result == POTR_OK)
    {
        result = open_validate_config(ctx);
    }
    if (result != POTR_OK)
    {
        ctx_cleanup(ctx);
        return result;
    }

    POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
               "potr_service_open: service_id=%" PRId64 " type=%d window=%u max_payload=%u"
               " max_message_size=%u send_queue_depth=%u"
               " health_interval=%ums health_timeout=%ums tcp_close_timeout=%ums",
               ctx->service.service_id, (int)ctx->service.type, (unsigned)ctx->global.window_size,
               (unsigned)ctx->global.max_payload, (unsigned)ctx->global.max_message_size,
               (unsigned)ctx->global.send_queue_depth, (unsigned)ctx->health_interval_ms,
               (unsigned)ctx->health_timeout_ms, (unsigned)ctx->global.tcp_close_timeout_ms);

    /* 通信種別に応じてソケット・パスを準備する */
    result = open_paths_by_type(ctx, role);
    if (result != POTR_OK)
    {
        ctx_cleanup(ctx);
        return result;
    }

    ctx->callback = callback;
    ctx->role = role;

    /* 送信先ソケット アドレスを設定する */
    result = setup_dest_addr(ctx, role);
    if (result != POTR_OK)
    {
        ctx_cleanup(ctx);
        return result;
    }

    /* セッション識別子を生成する */
    if (generate_session(ctx) != COM_UTIL_OK)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR, "potr_service_open: session id generation failed (service_id=%" PRId64 ")",
                   ctx->service.service_id);
        ctx_cleanup(ctx);
        return POTR_ERR_UNKNOWN;
    }

    /* 送受信ウィンドウと動的バッファーを確保する */
    result = alloc_context_buffers(ctx);
    if (result != POTR_OK)
    {
        ctx_cleanup(ctx);
        return result;
    }

    /* 通信種別に応じてスレッドを起動する */
    if (potr_is_tcp_type(ctx->service.type))
    {
        start_result = start_threads_tcp(ctx, role);
    }
    else
    {
        start_result = start_threads_udp(ctx, role);
    }
    if (start_result != POTR_OK)
    {
        ctx_cleanup(ctx);
        return start_result;
    }

    *handle = ctx;
    {
        const char *role_str;
        const char *encrypt_str;

        if (role == POTR_ROLE_SENDER)
        {
            role_str = "SENDER";
        }
        else
        {
            role_str = "RECEIVER";
        }
        if (ctx->service.encrypt_enabled)
        {
            encrypt_str = "ON";
        }
        else
        {
            encrypt_str = "OFF";
        }
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
                   "potr_service_open: service_id=%" PRId64 " role=%s encrypt=%s opened successfully",
                   ctx->service.service_id, role_str, encrypt_str);
    }
    return POTR_OK;
}
