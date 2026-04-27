/**
 *******************************************************************************
 *  @file           potrOpenService.c
 *  @brief          potrOpenService 関数の実装。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#include <porter_const.h>
#include <porter.h>

#include "../protocol/packet.h"
#include "../protocol/window.h"
#include "../potrContext.h"
#include "../potrPathEvent.h"
#include "../potrPeerTable.h"
#include "../thread/potrRecvThread.h"
#include "../thread/potrHealthThread.h"
#include "../thread/potrConnectThread.h"
#include "../infra/potrSendQueue.h"
#include "../thread/potrSendThread.h"
#include "../util/potrIpAddr.h"
#include "../infra/potrTrace.h"

/* ソケットを作成して bind する。成功時は PotrSocket を返す。失敗時は POTR_INVALID_SOCKET。
   bind_addr: bind する IPv4 アドレス。port: bind するポート番号 (0 = OS 自動選定)。 */
static PotrSocket open_socket_unicast(struct in_addr bind_addr, uint16_t port)
{
    PotrSocket         sock;
    struct sockaddr_in addr;
    int                reuse = 1;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == POTR_INVALID_SOCKET)
    {
        return POTR_INVALID_SOCKET;
    }

    potr_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr        = bind_addr;
    addr.sin_port        = htons(port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        potr_close_socket(sock);
        return POTR_INVALID_SOCKET;
    }

    return sock;
}

/* セッション識別子と開始時刻を生成してコンテキストに格納する。 */
static void generate_session(struct PotrContext_ *ctx)
{
    srand((unsigned int)com_util_get_monotonic_ms());
    ctx->session_id = (uint32_t)rand();
    com_util_get_realtime(&ctx->session_tv_sec, &ctx->session_tv_nsec);

    ctx->last_ping_send_ms       = 0U;
    ctx->last_valid_data_send_ms = 0U;
}

/* マルチキャストソケットを作成して bind・グループ参加する。
   src_if: 使用するローカルインターフェース (INADDR_ANY = OS 自動選択)。
   is_receiver: 1 = 受信者、0 = 送信者。 */
static PotrSocket open_socket_multicast(const PotrServiceDef *def,
                                        struct in_addr        src_if,
                                        int                   is_receiver)
{
    PotrSocket         sock;
    struct sockaddr_in addr;
    struct ip_mreq     mreq;
    int                reuse = 1;
    /* 受信者: dst_port で bind する。送信者: src_port で bind する (送信元ポート)。 */
    uint16_t           bind_port;
    if (is_receiver)
    {
        bind_port = def->dst_port;
    }
    else
    {
        bind_port = def->src_port;
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == POTR_INVALID_SOCKET)
    {
        return POTR_INVALID_SOCKET;
    }

    potr_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(bind_port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        potr_close_socket(sock);
        return POTR_INVALID_SOCKET;
    }

    /* マルチキャストグループへ参加 (送受信ともに参加する) */
    memset(&mreq, 0, sizeof(mreq));
    if (parse_ipv4_addr(def->multicast_group, &mreq.imr_multiaddr) != POTR_SUCCESS)
    {
        potr_close_socket(sock);
        return POTR_INVALID_SOCKET;
    }
    mreq.imr_interface = src_if;

    if (potr_setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                        &mreq, sizeof(mreq)) < 0)
    {
        potr_close_socket(sock);
        return POTR_INVALID_SOCKET;
    }

    /* 送信者: マルチキャスト送信インターフェースを設定する */
    if (!is_receiver)
    {
        potr_setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF,
                        &src_if, sizeof(src_if));
    }

    return sock;
}

/* ブロードキャストソケットを作成して bind する。
   src_port: 送信者の送信元 bind ポート (0 = OS 自動選定)。
   dst_port: 受信者の listen ポート / 送信者の送信先ポート (省略不可)。
   src_if: 送信者が使用するローカルインターフェース (INADDR_ANY = OS 自動選択)。
   is_receiver: 1 = 受信者 (INADDR_ANY で bind)、0 = 送信者 (src_if で bind)。 */
static PotrSocket open_socket_broadcast(uint16_t       src_port,
                                        uint16_t       dst_port,
                                        struct in_addr src_if,
                                        int            is_receiver)
{
    PotrSocket         sock;
    struct sockaddr_in addr;
    int                reuse      = 1;
    int                bcast      = 1;
    /* 受信者: dst_port で bind する。送信者: src_port で bind する (送信元ポート)。 */
    uint16_t           bind_port;
    if (is_receiver)
    {
        bind_port = dst_port;
    }
    else
    {
        bind_port = src_port;
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == POTR_INVALID_SOCKET)
    {
        return POTR_INVALID_SOCKET;
    }

    potr_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    potr_setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    /* 送信者: src_addr で bind してインターフェースを選択する。受信者: INADDR_ANY で bind する。 */
    if (!is_receiver)
    {
        addr.sin_addr.s_addr = src_if.s_addr;
    }
    else
    {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    addr.sin_port        = htons(bind_port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        potr_close_socket(sock);
        return POTR_INVALID_SOCKET;
    }

    return sock;
}

/* 生成済みソケットをすべてクローズする */
static void cleanup_sockets(struct PotrContext_ *ctx)
{
    int i;
    for (i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        if (ctx->sock[i] != POTR_INVALID_SOCKET)
        {
            potr_close_socket(ctx->sock[i]);
            ctx->sock[i] = POTR_INVALID_SOCKET;
        }
    }
}

/* コンテキストが保持するすべてのリソースを解放して ctx 本体を free する。
   memset(ctx, 0, ...) 後であれば、未初期化ポインタ (NULL) に対しても安全に呼び出せる。 */
static void ctx_cleanup(struct PotrContext_ *ctx)
{
    potr_callback_mutex_destroy(ctx);
    window_destroy(&ctx->send_window);
    window_destroy(&ctx->recv_window);
    free(ctx->frag_buf);
    free(ctx->compress_buf);
    free(ctx->crypto_buf);
    free(ctx->recv_buf);
    free(ctx->send_wire_buf);
    if (ctx->is_multi_peer && ctx->peers != NULL)
    {
        peer_table_destroy(ctx);
    }
    /* TCP listen ソケットをクローズ (path ごと) */
    {
        int i;
        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            if (ctx->tcp_listen_sock[i] != POTR_INVALID_SOCKET)
            {
                potr_close_socket(ctx->tcp_listen_sock[i]);
            }
        }
    }
    {
        int i;
        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            if (ctx->tcp_conn_fd[i] != POTR_INVALID_SOCKET)
            {
                potr_close_socket(ctx->tcp_conn_fd[i]);
            }
        }
    }
    cleanup_sockets(ctx);
    free(ctx);
}

/* TCP RECEIVER: path_idx 番目の listen ソケットを作成して bind・listen する。
   dst_addr[path_idx] が指定されていれば dst_addr_resolved[path_idx] に解決する。
   src_addr[path_idx] が指定されていれば src_addr_resolved[path_idx] にも解決する（接続元フィルタ用）。
   成功時は ctx->tcp_listen_sock[path_idx] に格納して POTR_SUCCESS を返す。 */
static int open_socket_tcp_receiver(struct PotrContext_ *ctx, int path_idx)
{
    PotrSocket         sock;
    struct sockaddr_in addr;
    int                reuse = 1;
    struct in_addr     bind_ip;

    if (ctx->service.dst_addr[path_idx][0] != '\0')
    {
        if (resolve_ipv4_addr(ctx->service.dst_addr[path_idx], &bind_ip) != POTR_SUCCESS)
        {
            return POTR_ERROR;
        }
        ctx->dst_addr_resolved[path_idx] = bind_ip;
    }
    else
    {
        bind_ip.s_addr = htonl(INADDR_ANY);
    }

    if (ctx->service.src_addr[path_idx][0] != '\0')
    {
        if (resolve_ipv4_addr(ctx->service.src_addr[path_idx],
                               &ctx->src_addr_resolved[path_idx]) != POTR_SUCCESS)
        {
            return POTR_ERROR;
        }
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == POTR_INVALID_SOCKET)
    {
        return POTR_ERROR;
    }

    potr_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr        = bind_ip;
    addr.sin_port        = htons(ctx->service.dst_port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        potr_close_socket(sock);
        return POTR_ERROR;
    }

    if (listen(sock, SOMAXCONN) < 0)
    {
        potr_close_socket(sock);
        return POTR_ERROR;
    }

    ctx->tcp_listen_sock[path_idx] = sock;
    return POTR_SUCCESS;
}

/* TCP SENDER: path_idx 番目の接続先 dst_addr を解決して dst_addr_resolved[path_idx] に格納する。
   src_addr[path_idx] が指定されていれば src_addr_resolved[path_idx] にも解決する。
   実際の TCP 接続は connect スレッドが行う。 */
static int open_socket_tcp_sender(struct PotrContext_ *ctx, int path_idx)
{
    if (ctx->service.dst_addr[path_idx][0] == '\0')
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "open_socket_tcp_sender: dst_addr[%d] is empty", path_idx);
        return POTR_ERROR;
    }

    if (resolve_ipv4_addr(ctx->service.dst_addr[path_idx],
                           &ctx->dst_addr_resolved[path_idx]) != POTR_SUCCESS)
    {
        return POTR_ERROR;
    }

    if (ctx->service.src_addr[path_idx][0] != '\0')
    {
        if (resolve_ipv4_addr(ctx->service.src_addr[path_idx],
                               &ctx->src_addr_resolved[path_idx]) != POTR_SUCCESS)
        {
            return POTR_ERROR;
        }
    }

    return POTR_SUCCESS;
}

/* doxygen コメントは、ヘッダに記載 */
POTR_EXPORT int POTR_API potrOpenService(const PotrGlobalConfig *global,
                                     const PotrServiceDef   *service,
                                     PotrRole                role,
                                     PotrRecvCallback        callback,
                                     PotrHandle             *handle)
{
    struct PotrContext_ *ctx;

    if (global == NULL || service == NULL || handle == NULL)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "potrOpenService: invalid argument (global=%p service=%p handle=%p)",
                 (const void *)global, (const void *)service, (const void *)handle);
        return POTR_ERROR;
    }

    POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
             "potrOpenService: service_id=%" PRId64 " role=%d",
             service->service_id, (int)role);

    /* role と callback の整合性チェック (設定読み込み前に確定できる部分のみ) */
    if (role == POTR_ROLE_RECEIVER && callback == NULL)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "potrOpenService: service_id=%" PRId64 " RECEIVER role requires callback",
                 service->service_id);
        return POTR_ERROR;
    }
    /* SENDER + callback の完全チェックは設定読み込み後に行う
       (POTR_TYPE_UNICAST_BIDIR の SENDER は callback が必須のため) */
    if (role != POTR_ROLE_SENDER && role != POTR_ROLE_RECEIVER)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "potrOpenService: service_id=%" PRId64 " unknown role=%d",
                 service->service_id, (int)role);
        return POTR_ERROR;
    }

    if (potr_socket_lib_init() != 0)
    {
        return POTR_ERROR;
    }

    ctx = (struct PotrContext_ *)malloc(sizeof(struct PotrContext_));
    if (ctx == NULL)
    {
        return POTR_ERROR;
    }
    memset(ctx, 0, sizeof(*ctx));
    potr_callback_mutex_init(ctx);

    /* 全ソケットを INVALID で初期化 */
    {
        int i;
        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            ctx->sock[i]              = POTR_INVALID_SOCKET;
            ctx->tcp_conn_fd[i]       = POTR_INVALID_SOCKET;
            ctx->tcp_listen_sock[i]   = POTR_INVALID_SOCKET;
        }
    }

    /* グローバル設定とサービス定義をコンテキストにコピー */
    memcpy(&ctx->global, global, sizeof(PotrGlobalConfig));
    memcpy(&ctx->service, service, sizeof(PotrServiceDef));

    /* SENDER + callback の整合性チェック (型が確定した後) */
    if (role == POTR_ROLE_SENDER && callback != NULL
        && ctx->service.type != POTR_TYPE_UNICAST_BIDIR
        && ctx->service.type != POTR_TYPE_TCP_BIDIR)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "potrOpenService: service_id=%" PRId64 " SENDER role must not have callback"
                 " (type=%d)",
                 ctx->service.service_id, (int)ctx->service.type);
        ctx_cleanup(ctx);
        return POTR_ERROR;
    }
    if (role == POTR_ROLE_SENDER && callback == NULL
        && (ctx->service.type == POTR_TYPE_UNICAST_BIDIR
            || ctx->service.type == POTR_TYPE_TCP_BIDIR))
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "potrOpenService: service_id=%" PRId64 " bidirectional SENDER role requires callback"
                 " (type=%d)",
                 ctx->service.service_id, (int)ctx->service.type);
        ctx_cleanup(ctx);
        return POTR_ERROR;
    }

    /* 設定値バリデーション */
    if (ctx->global.max_payload < 64U || ctx->global.max_payload > POTR_MAX_PAYLOAD)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "potrOpenService: service_id=%" PRId64 " invalid max_payload=%u (range: 64..%u)",
                 ctx->service.service_id, (unsigned)ctx->global.max_payload, (unsigned)POTR_MAX_PAYLOAD);
        ctx_cleanup(ctx);
        return POTR_ERROR;
    }
    if (ctx->global.window_size < 2U || ctx->global.window_size > POTR_MAX_WINDOW_SIZE)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "potrOpenService: service_id=%" PRId64 " invalid window_size=%u (range: 2..%u)",
                 ctx->service.service_id, (unsigned)ctx->global.window_size, (unsigned)POTR_MAX_WINDOW_SIZE);
        ctx_cleanup(ctx);
        return POTR_ERROR;
    }
    if (ctx->global.max_message_size < (uint32_t)ctx->global.max_payload)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "potrOpenService: service_id=%" PRId64 " max_message_size=%u must be >= max_payload=%u",
                 ctx->service.service_id, (unsigned)ctx->global.max_message_size,
                 (unsigned)ctx->global.max_payload);
        ctx_cleanup(ctx);
        return POTR_ERROR;
    }
    if (ctx->global.send_queue_depth < 2U)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "potrOpenService: service_id=%" PRId64 " invalid send_queue_depth=%u (min: 2)",
                 ctx->service.service_id, (unsigned)ctx->global.send_queue_depth);
        ctx_cleanup(ctx);
        return POTR_ERROR;
    }

    /* 通信種別ごとのグローバル既定値を選び、サービス単位設定で実効値を上書きする。 */
    if (potr_is_tcp_type(ctx->service.type))
    {
        ctx->health_interval_ms = ctx->global.tcp_health_interval_ms;
        ctx->health_timeout_ms  = ctx->global.tcp_health_timeout_ms;
    }
    else
    {
        ctx->health_interval_ms = ctx->global.udp_health_interval_ms;
        ctx->health_timeout_ms  = ctx->global.udp_health_timeout_ms;
    }

    if (ctx->service.health_interval_ms != 0U)
    {
        ctx->health_interval_ms = ctx->service.health_interval_ms;
    }
    if (ctx->service.health_timeout_ms != 0U)
    {
        ctx->health_timeout_ms = ctx->service.health_timeout_ms;
    }

    POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
             "potrOpenService: service_id=%" PRId64 " type=%d window=%u max_payload=%u"
             " max_message_size=%u send_queue_depth=%u"
             " health_interval=%ums health_timeout=%ums tcp_close_timeout=%ums",
             ctx->service.service_id, (int)ctx->service.type,
             (unsigned)ctx->global.window_size, (unsigned)ctx->global.max_payload,
             (unsigned)ctx->global.max_message_size, (unsigned)ctx->global.send_queue_depth,
             (unsigned)ctx->health_interval_ms,
             (unsigned)ctx->health_timeout_ms,
             (unsigned)ctx->global.tcp_close_timeout_ms);

    /* 通信種別に応じてソケットを作成 (RAW 型はベース型に正規化してから判定) */
    switch (potr_raw_base_type(ctx->service.type))
    {
        case POTR_TYPE_UNICAST:
        {
            int i;

            if (ctx->service.dst_port == 0)
            {
                ctx_cleanup(ctx);
                return POTR_ERROR;
            }

            for (i = 0; i < (int)POTR_MAX_PATH; i++)
            {
                struct in_addr bind_addr;
                uint16_t       bind_port;

                if (ctx->service.src_addr[i][0] == '\0' ||
                    ctx->service.dst_addr[i][0] == '\0')
                {
                    break;
                }

                if (resolve_ipv4_addr(ctx->service.src_addr[i],
                                      &ctx->src_addr_resolved[i]) != POTR_SUCCESS ||
                    resolve_ipv4_addr(ctx->service.dst_addr[i],
                                      &ctx->dst_addr_resolved[i]) != POTR_SUCCESS)
                {
                    ctx_cleanup(ctx);
                    return POTR_ERROR;
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
                if (ctx->sock[i] == POTR_INVALID_SOCKET)
                {
                    ctx_cleanup(ctx);
                    return POTR_ERROR;
                }

                ctx->n_path++;
            }

            if (ctx->n_path == 0)
            {
                ctx_cleanup(ctx);
                return POTR_ERROR;
            }
            break;
        }

        case POTR_TYPE_MULTICAST:
        {
            int i;

            if (ctx->service.dst_port == 0 ||
                ctx->service.multicast_group[0] == '\0')
            {
                ctx_cleanup(ctx);
                return POTR_ERROR;
            }

            for (i = 0; i < (int)POTR_MAX_PATH; i++)
            {
                if (ctx->service.src_addr[i][0] == '\0') break;

                if (resolve_ipv4_addr(ctx->service.src_addr[i],
                                      &ctx->src_addr_resolved[i]) != POTR_SUCCESS)
                {
                    ctx_cleanup(ctx);
                    return POTR_ERROR;
                }

                ctx->sock[i] = open_socket_multicast(&ctx->service,
                                                     ctx->src_addr_resolved[i],
                                                     role == POTR_ROLE_RECEIVER);
                if (ctx->sock[i] == POTR_INVALID_SOCKET)
                {
                    ctx_cleanup(ctx);
                    return POTR_ERROR;
                }

                ctx->n_path++;
            }

            if (ctx->n_path == 0)
            {
                ctx_cleanup(ctx);
                return POTR_ERROR;
            }
            break;
        }

        case POTR_TYPE_BROADCAST:
        {
            int i;

            if (ctx->service.dst_port == 0)
            {
                ctx_cleanup(ctx);
                return POTR_ERROR;
            }

            /* broadcast_addr 省略時は限定ブロードキャスト (255.255.255.255) を使用する */
            if (ctx->service.broadcast_addr[0] == '\0')
            {
                const char *dflt = "255.255.255.255";
                size_t      len  = strlen(dflt);
                memcpy(ctx->service.broadcast_addr, dflt, len + 1);
            }

            for (i = 0; i < (int)POTR_MAX_PATH; i++)
            {
                if (ctx->service.src_addr[i][0] == '\0') break;

                if (resolve_ipv4_addr(ctx->service.src_addr[i],
                                      &ctx->src_addr_resolved[i]) != POTR_SUCCESS)
                {
                    ctx_cleanup(ctx);
                    return POTR_ERROR;
                }

                ctx->sock[i] = open_socket_broadcast(ctx->service.src_port,
                                                     ctx->service.dst_port,
                                                     ctx->src_addr_resolved[i],
                                                     role == POTR_ROLE_RECEIVER);
                if (ctx->sock[i] == POTR_INVALID_SOCKET)
                {
                    ctx_cleanup(ctx);
                    return POTR_ERROR;
                }

                ctx->n_path++;
            }

            if (ctx->n_path == 0)
            {
                ctx_cleanup(ctx);
                return POTR_ERROR;
            }
            break;
        }

        case POTR_TYPE_UNICAST_BIDIR:
        {
            ctx->is_multi_peer = 0;

            /* dst_port は必須。 */
            if (ctx->service.dst_port == 0)
            {
                POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                         "potrOpenService: service_id=%" PRId64 " UNICAST_BIDIR requires"
                         " dst_port (non-zero)",
                         ctx->service.service_id);
                ctx_cleanup(ctx);
                return POTR_ERROR;
            }

            if (role == POTR_ROLE_RECEIVER && ctx->service.src_addr[0][0] == '\0')
            {
                /* 動的 1:1 RECEIVER: src_addr 省略 → dst_addr:dst_port に bind し、
                   最初の受信パケットから SENDER のアドレスを動的学習する。 */
                struct in_addr bind_addr;

                if (ctx->service.dst_addr[0][0] == '\0')
                {
                    bind_addr.s_addr = htonl(INADDR_ANY);
                }
                else
                {
                    if (resolve_ipv4_addr(ctx->service.dst_addr[0],
                                         &bind_addr) != POTR_SUCCESS)
                    {
                        ctx_cleanup(ctx);
                        return POTR_ERROR;
                    }
                    ctx->dst_addr_resolved[0] = bind_addr;
                }
                ctx->sock[0] = open_socket_unicast(bind_addr, ctx->service.dst_port);
                if (ctx->sock[0] == POTR_INVALID_SOCKET)
                {
                    ctx_cleanup(ctx);
                    return POTR_ERROR;
                }
                ctx->n_path = 1;
                POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,
                         "potrOpenService: service_id=%" PRId64 " UNICAST_BIDIR 1:1 dynamic RECEIVER"
                         " bind dst_port=%u",
                         ctx->service.service_id, (unsigned)ctx->service.dst_port);
            }
            else
            {
                /* 1:1 モード: src_addr/dst_addr ペアループ。
                   SENDER は src_addr 省略時に INADDR_ANY で bind する (OS がアダプタを自動選択)。
                   RECEIVER はここには src_addr がある場合のみ到達する。 */
                int i;

                for (i = 0; i < (int)POTR_MAX_PATH; i++)
                {
                    struct in_addr bind_addr;

                    /* dst_addr が空 → パス終端。
                       RECEIVER は src_addr も必要 (src_addr なし RECEIVER は上で処理済み)。 */
                    if (ctx->service.dst_addr[i][0] == '\0') break;
                    if (role == POTR_ROLE_RECEIVER
                        && ctx->service.src_addr[i][0] == '\0') break;

                    if (ctx->service.src_addr[i][0] != '\0')
                    {
                        if (resolve_ipv4_addr(ctx->service.src_addr[i],
                                              &ctx->src_addr_resolved[i]) != POTR_SUCCESS)
                        {
                            ctx_cleanup(ctx);
                            return POTR_ERROR;
                        }
                    }

                    if (resolve_ipv4_addr(ctx->service.dst_addr[i],
                                          &ctx->dst_addr_resolved[i]) != POTR_SUCCESS)
                    {
                        ctx_cleanup(ctx);
                        return POTR_ERROR;
                    }

                    if (role == POTR_ROLE_SENDER)
                    {
                        /* src_addr 省略時は INADDR_ANY で bind し OS がアダプタを自動選択 */
                        if (ctx->service.src_addr[i][0] != '\0')
                            bind_addr = ctx->src_addr_resolved[i];
                        else
                            bind_addr.s_addr = htonl(INADDR_ANY);
                        ctx->sock[i] = open_socket_unicast(bind_addr, ctx->service.src_port);
                    }
                    else
                    {
                        /* RECEIVER: dst_addr:dst_port で bind */
                        ctx->sock[i] = open_socket_unicast(ctx->dst_addr_resolved[i],
                                                           ctx->service.dst_port);
                    }
                    if (ctx->sock[i] == POTR_INVALID_SOCKET)
                    {
                        ctx_cleanup(ctx);
                        return POTR_ERROR;
                    }

                    ctx->n_path++;
                }

                if (ctx->n_path == 0)
                {
                    ctx_cleanup(ctx);
                    return POTR_ERROR;
                }
            }
            break;
        }

        case POTR_TYPE_UNICAST_BIDIR_N1:
        {
            /* N:1 サーバ: dst_addr[i]:dst_port ごとにソケットを bind する。
               dst_addr が全て省略されている場合は INADDR_ANY で 1 ソケットのみ作成する。 */
            int i;

            ctx->is_multi_peer = 1;

            /* dst_port は必須。 */
            if (ctx->service.dst_port == 0)
            {
                POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                         "potrOpenService: service_id=%" PRId64 " UNICAST_BIDIR_N1 requires"
                         " dst_port (non-zero)",
                         ctx->service.service_id);
                ctx_cleanup(ctx);
                return POTR_ERROR;
            }

            if (ctx->service.dst_addr[0][0] == '\0')
            {
                /* dst_addr 全て省略: INADDR_ANY で 1 ソケット */
                struct in_addr any_addr;
                any_addr.s_addr = htonl(INADDR_ANY);
                ctx->sock[0] = open_socket_unicast(any_addr, ctx->service.dst_port);
                if (ctx->sock[0] == POTR_INVALID_SOCKET)
                {
                    ctx_cleanup(ctx);
                    return POTR_ERROR;
                }
                ctx->n_path = 1;
            }
            else
            {
                /* dst_addr[i] を列挙してパスごとにソケットを作成する */
                for (i = 0; i < (int)POTR_MAX_PATH; i++)
                {
                    struct in_addr bind_addr;

                    if (ctx->service.dst_addr[i][0] == '\0') break;

                    if (resolve_ipv4_addr(ctx->service.dst_addr[i],
                                          &bind_addr) != POTR_SUCCESS)
                    {
                        ctx_cleanup(ctx);
                        return POTR_ERROR;
                    }
                    ctx->dst_addr_resolved[i] = bind_addr;
                    ctx->sock[i] = open_socket_unicast(bind_addr, ctx->service.dst_port);
                    if (ctx->sock[i] == POTR_INVALID_SOCKET)
                    {
                        ctx_cleanup(ctx);
                        return POTR_ERROR;
                    }
                    ctx->n_path = i + 1;
                }
            }

            /* ピアテーブル初期化 */
            ctx->max_peers = (int)ctx->service.max_peers;
            if (ctx->max_peers <= 0)
            {
                ctx->max_peers = 1024;
            }
            if (peer_table_init(ctx) != POTR_SUCCESS)
            {
                ctx_cleanup(ctx);
                return POTR_ERROR;
            }

            POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,
                     "potrOpenService: service_id=%" PRId64 " UNICAST_BIDIR_N1"
                     " (max_peers=%d src_port_filter=%u) bind dst_port=%u n_path=%d",
                     ctx->service.service_id, ctx->max_peers,
                     (unsigned)ctx->service.src_port,
                     (unsigned)ctx->service.dst_port,
                     ctx->n_path);
            break;
        }

        case POTR_TYPE_TCP:
        case POTR_TYPE_TCP_BIDIR:
        {
            if (ctx->service.dst_port == 0)
            {
                POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                         "potrOpenService: service_id=%" PRId64 " TCP requires dst_port",
                         ctx->service.service_id);
                ctx_cleanup(ctx);
                return POTR_ERROR;
            }

            if (role == POTR_ROLE_RECEIVER)
            {
                /* 非空の dst_addr[i] ごとに listen ソケットを作成する */
                int i;
                for (i = 0; i < (int)POTR_MAX_PATH; i++)
                {
                    if (ctx->service.dst_addr[i][0] == '\0') break;

                    if (open_socket_tcp_receiver(ctx, i) != POTR_SUCCESS)
                    {
                        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                                 "potrOpenService: service_id=%" PRId64 " TCP listen failed"
                                 " (path=%d dst_addr=%s dst_port=%u)",
                                 ctx->service.service_id, i,
                                 ctx->service.dst_addr[i][0] ? ctx->service.dst_addr[i] : "*",
                                 (unsigned)ctx->service.dst_port);
                        ctx_cleanup(ctx);
                        return POTR_ERROR;
                    }
                    POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,
                             "potrOpenService: service_id=%" PRId64 " TCP path[%d] listening"
                             " on %s:%u",
                             ctx->service.service_id, i,
                             ctx->service.dst_addr[i][0] ? ctx->service.dst_addr[i] : "*",
                             (unsigned)ctx->service.dst_port);
                    ctx->n_path = i + 1;
                }
                if (ctx->n_path == 0)
                {
                    POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                             "potrOpenService: service_id=%" PRId64 " TCP RECEIVER requires"
                             " at least one dst_addr",
                             ctx->service.service_id);
                    ctx_cleanup(ctx);
                    return POTR_ERROR;
                }
            }
            else
            {
                /* 非空の dst_addr[i] ごとにアドレスを解決する */
                int i;
                for (i = 0; i < (int)POTR_MAX_PATH; i++)
                {
                    if (ctx->service.dst_addr[i][0] == '\0') break;

                    if (open_socket_tcp_sender(ctx, i) != POTR_SUCCESS)
                    {
                        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                                 "potrOpenService: service_id=%" PRId64 " TCP sender"
                                 " dst_addr resolve failed (path=%d %s)",
                                 ctx->service.service_id, i, ctx->service.dst_addr[i]);
                        ctx_cleanup(ctx);
                        return POTR_ERROR;
                    }
                    ctx->n_path = i + 1;
                }
                if (ctx->n_path == 0)
                {
                    POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                             "potrOpenService: service_id=%" PRId64 " TCP SENDER requires"
                             " at least one dst_addr",
                             ctx->service.service_id);
                    ctx_cleanup(ctx);
                    return POTR_ERROR;
                }
            }
            break;
        }

        case POTR_TYPE_UNICAST_RAW:
        case POTR_TYPE_MULTICAST_RAW:
        case POTR_TYPE_BROADCAST_RAW:
            /* potr_raw_base_type() は RAW 型をベース型に変換するため、ここには到達しない */
            /* fall through */
        default:
            ctx_cleanup(ctx);
            return POTR_ERROR;
    }

    ctx->callback = callback;
    ctx->role     = role;

    /* 送信先ソケットアドレスを設定 (RAW 型はベース型に正規化してから判定)
       UNICAST_BIDIR は両端 (SENDER / RECEIVER) ともに dest_addr を設定する */
    if (role == POTR_ROLE_SENDER
        || ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
    {
        int i;

        switch (potr_raw_base_type(ctx->service.type))
        {
            case POTR_TYPE_UNICAST_BIDIR:
            case POTR_TYPE_UNICAST_BIDIR_N1:
                for (i = 0; i < ctx->n_path; i++)
                {
                    memset(&ctx->dest_addr[i], 0, sizeof(ctx->dest_addr[i]));
                    ctx->dest_addr[i].sin_family = AF_INET;
                    if (role == POTR_ROLE_SENDER)
                    {
                        /* SENDER: dst_addr:dst_port (RECEIVER の bind アドレス) へ送信 */
                        ctx->dest_addr[i].sin_addr = ctx->dst_addr_resolved[i];
                        ctx->dest_addr[i].sin_port = htons(ctx->service.dst_port);
                    }
                    else
                    {
                        /* RECEIVER: src_addr:src_port (SENDER の bind アドレス) へ送信 */
                        ctx->dest_addr[i].sin_addr = ctx->src_addr_resolved[i];
                        ctx->dest_addr[i].sin_port = htons(ctx->service.src_port);
                    }
                }
                break;

            case POTR_TYPE_UNICAST:
                for (i = 0; i < ctx->n_path; i++)
                {
                    memset(&ctx->dest_addr[i], 0, sizeof(ctx->dest_addr[i]));
                    ctx->dest_addr[i].sin_family = AF_INET;
                    ctx->dest_addr[i].sin_addr   = ctx->dst_addr_resolved[i];
                    ctx->dest_addr[i].sin_port   = htons(ctx->service.dst_port);
                }
                break;

            case POTR_TYPE_MULTICAST:
            {
                struct in_addr mcast_ip;
                if (parse_ipv4_addr(ctx->service.multicast_group, &mcast_ip) != POTR_SUCCESS)
                {
                    ctx_cleanup(ctx);
                    return POTR_ERROR;
                }
                for (i = 0; i < ctx->n_path; i++)
                {
                    memset(&ctx->dest_addr[i], 0, sizeof(ctx->dest_addr[i]));
                    ctx->dest_addr[i].sin_family = AF_INET;
                    ctx->dest_addr[i].sin_addr   = mcast_ip;
                    ctx->dest_addr[i].sin_port   = htons(ctx->service.dst_port);
                }
                break;
            }

            case POTR_TYPE_BROADCAST:
            {
                struct in_addr bcast_ip;
                if (parse_ipv4_addr(ctx->service.broadcast_addr, &bcast_ip) != POTR_SUCCESS)
                {
                    ctx_cleanup(ctx);
                    return POTR_ERROR;
                }
                for (i = 0; i < ctx->n_path; i++)
                {
                    memset(&ctx->dest_addr[i], 0, sizeof(ctx->dest_addr[i]));
                    ctx->dest_addr[i].sin_family = AF_INET;
                    ctx->dest_addr[i].sin_addr   = bcast_ip;
                    ctx->dest_addr[i].sin_port   = htons(ctx->service.dst_port);
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
    }

    /* セッション識別子を生成する */
    generate_session(ctx);

    /* 送受信ウィンドウを初期化 */
    if (window_init(&ctx->send_window, 0,
                    ctx->global.window_size, ctx->global.max_payload) != POTR_SUCCESS)
    {
        ctx_cleanup(ctx);
        return POTR_ERROR;
    }
    ctx->send_has_data = 0;
    if (window_init(&ctx->recv_window, 0,
                    ctx->global.window_size, ctx->global.max_payload) != POTR_SUCCESS)
    {
        ctx_cleanup(ctx);
        return POTR_ERROR;
    }

    /* 動的バッファを確保 */
    ctx->frag_buf = (uint8_t *)malloc(ctx->global.max_message_size);
    if (ctx->frag_buf == NULL)
    {
        ctx_cleanup(ctx);
        return POTR_ERROR;
    }

    ctx->compress_buf_size = COM_UTIL_COMPRESS_HEADER_SIZE
                             + (size_t)ctx->global.max_message_size + 64U;
    ctx->compress_buf = (uint8_t *)malloc(ctx->compress_buf_size);
    if (ctx->compress_buf == NULL)
    {
        ctx_cleanup(ctx);
        return POTR_ERROR;
    }

    ctx->recv_buf = (uint8_t *)malloc(PACKET_HEADER_SIZE + ctx->global.max_payload);
    if (ctx->recv_buf == NULL)
    {
        ctx_cleanup(ctx);
        return POTR_ERROR;
    }

    ctx->send_wire_buf = (uint8_t *)malloc(PACKET_HEADER_SIZE + ctx->global.max_payload);
    if (ctx->send_wire_buf == NULL)
    {
        ctx_cleanup(ctx);
        return POTR_ERROR;
    }

    ctx->crypto_buf_size = ctx->global.max_payload + POTR_CRYPTO_TAG_SIZE;
    ctx->crypto_buf      = (uint8_t *)malloc(ctx->crypto_buf_size);
    if (ctx->crypto_buf == NULL)
    {
        ctx_cleanup(ctx);
        return POTR_ERROR;
    }

    /* TCP: 接続管理スレッドを起動する。
       send/recv/health スレッドは接続確立後に connect スレッドが管理する。 */
    if (potr_is_tcp_type(ctx->service.type))
    {
        /* tcp_state_mutex / tcp_state_cv / tcp_close_mutex / tcp_close_cv /
           tcp_send_mutex[] / recv_window_mutex /
           health_mutex[] / health_wakeup[] を初期化 */
        {
            int i;
            COM_UTIL_MUTEX_INIT(&ctx->tcp_state_mutex);
            COM_UTIL_COND_INIT(&ctx->tcp_state_cv);
            COM_UTIL_MUTEX_INIT(&ctx->tcp_close_mutex);
            COM_UTIL_COND_INIT(&ctx->tcp_close_cv);
            for (i = 0; i < (int)POTR_MAX_PATH; i++)
            {
                COM_UTIL_MUTEX_INIT(&ctx->tcp_send_mutex[i]);
                COM_UTIL_MUTEX_INIT(&ctx->health_mutex[i]);
                COM_UTIL_COND_INIT(&ctx->health_wakeup[i]);
            }
            COM_UTIL_MUTEX_INIT(&ctx->recv_window_mutex);
        }

        /* SENDER または TCP_BIDIR: 送信キューを初期化 (connect スレッドが reconnect 時に destroy+init する) */
        if (role == POTR_ROLE_SENDER
            || ctx->service.type == POTR_TYPE_TCP_BIDIR)
        {
            if (potr_send_queue_init(&ctx->send_queue,
                                     (size_t)ctx->global.send_queue_depth,
                                     ctx->global.max_payload) != POTR_SUCCESS)
            {
                {
                    int i;
                    COM_UTIL_MUTEX_DESTROY(&ctx->tcp_state_mutex);
                    COM_UTIL_COND_DESTROY(&ctx->tcp_state_cv);
                    COM_UTIL_MUTEX_DESTROY(&ctx->tcp_close_mutex);
                    COM_UTIL_COND_DESTROY(&ctx->tcp_close_cv);
                    for (i = 0; i < (int)POTR_MAX_PATH; i++)
                        COM_UTIL_MUTEX_DESTROY(&ctx->tcp_send_mutex[i]);
                    COM_UTIL_MUTEX_DESTROY(&ctx->recv_window_mutex);
                }
                ctx_cleanup(ctx);
                return POTR_ERROR;
            }
        }

        if (potr_connect_thread_start(ctx) != POTR_SUCCESS)
        {
            if (role == POTR_ROLE_SENDER
                || ctx->service.type == POTR_TYPE_TCP_BIDIR)
            {
                potr_send_queue_destroy(&ctx->send_queue);
            }
            {
                int i;
                COM_UTIL_MUTEX_DESTROY(&ctx->tcp_state_mutex);
                COM_UTIL_COND_DESTROY(&ctx->tcp_state_cv);
                COM_UTIL_MUTEX_DESTROY(&ctx->tcp_close_mutex);
                COM_UTIL_COND_DESTROY(&ctx->tcp_close_cv);
                for (i = 0; i < (int)POTR_MAX_PATH; i++)
                    COM_UTIL_MUTEX_DESTROY(&ctx->tcp_send_mutex[i]);
                COM_UTIL_MUTEX_DESTROY(&ctx->recv_window_mutex);
            }
            ctx_cleanup(ctx);
            return POTR_ERROR;
        }
    }
    else
    {
        /* 非 TCP: 受信者の場合は受信スレッドのみ起動
           ただし UNICAST_BIDIR / UNICAST_BIDIR_N1 の RECEIVER は
           下の全スレッド起動ブロックで処理する */
        if (role == POTR_ROLE_RECEIVER
            && ctx->service.type != POTR_TYPE_UNICAST_BIDIR
            && ctx->service.type != POTR_TYPE_UNICAST_BIDIR_N1)
        {
            if (comm_recv_thread_start(ctx) != POTR_SUCCESS)
            {
                ctx_cleanup(ctx);
                return POTR_ERROR;
            }
        }

        /* 非 TCP 送信者 / UNICAST_BIDIR / UNICAST_BIDIR_N1 受信者:
           送信キュー・送信スレッド・ヘルスチェックスレッド・受信スレッドを起動 */
        if (role == POTR_ROLE_SENDER
            || ctx->service.type == POTR_TYPE_UNICAST_BIDIR
            || ctx->service.type == POTR_TYPE_UNICAST_BIDIR_N1)
        {
            if (potr_send_queue_init(&ctx->send_queue,
                                     (size_t)ctx->global.send_queue_depth,
                                     ctx->global.max_payload) != POTR_SUCCESS)
            {
                ctx_cleanup(ctx);
                return POTR_ERROR;
            }

            if (potr_send_thread_start(ctx) != POTR_SUCCESS)
            {
                potr_send_queue_destroy(&ctx->send_queue);
                ctx_cleanup(ctx);
                return POTR_ERROR;
            }

            ctx->health_send_immediate[0] =
                potr_type_uses_immediate_health_ping(ctx->service.type) ? 1 : 0;
            if (potr_health_thread_start(ctx) != POTR_SUCCESS)
            {
                potr_send_thread_stop(ctx);
                potr_send_queue_destroy(&ctx->send_queue);
                ctx_cleanup(ctx);
                return POTR_ERROR;
            }

            if (comm_recv_thread_start(ctx) != POTR_SUCCESS)
            {
                potr_health_thread_stop(ctx);
                potr_send_thread_stop(ctx);
                potr_send_queue_destroy(&ctx->send_queue);
                ctx_cleanup(ctx);
                return POTR_ERROR;
            }
        }
    }

    *handle = ctx;
    const char *role_str;
    if (role == POTR_ROLE_SENDER)
    {
        role_str = "SENDER";
    }
    else
    {
        role_str = "RECEIVER";
    }
    POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,
             "potrOpenService: service_id=%" PRId64 " role=%s encrypt=%s opened successfully",
             ctx->service.service_id,
             role_str,
             ctx->service.encrypt_enabled ? "ON" : "OFF");
    return POTR_SUCCESS;
}
