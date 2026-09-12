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

#include <cplat/base/platform.h>
#include <cplat/crt/stdlib.h>
#include <cplat/crypto/random.h>
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
#include "api_open_paths.h"

#include <cplat/net/byteorder.h>
#include <cplat/net/endpoint.h>
#include <cplat/net/socket.h>

/* セッション識別子と開始時刻を生成してコンテキストに格納する。
 * session_id は AES-256-GCM nonce の非決定要素であり、衝突または推測は
 * (key, nonce) の再利用を招く。暗号論的乱数源から取得する。 */
static int generate_session(potr_context *ctx)
{
    int ret = cplat_random_bytes(&ctx->session_id, sizeof(ctx->session_id));

    if (ret != CPLAT_OK)
    {
        return ret;
    }
    cplat_get_realtime(&ctx->session_ts);

    ctx->last_ping_send_ms = 0U;
    ctx->last_valid_data_send_ms = 0U;

    return CPLAT_OK;
}

/* 生成済みソケットをすべてクローズする */
static void cleanup_sockets(potr_context *ctx)
{
    int i;
    for (i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        if (ctx->sock[i] != CPLAT_INVALID_SOCKET)
        {
            cplat_socket_close(ctx->sock[i]);
            ctx->sock[i] = CPLAT_INVALID_SOCKET;
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
    cplat_free(ctx->frag_buf);
    cplat_free(ctx->compress_buf);
    cplat_free(ctx->crypto_buf);
    cplat_free(ctx->recv_buf);
    cplat_free(ctx->send_wire_buf);
    if (ctx->is_multi_peer && ctx->peers != NULL)
    {
        potr_internal_peer_table_dispose(ctx);
    }
    /* TCP listen ソケットをクローズ (path ごと) */
    {
        int i;
        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            if (ctx->tcp_listen_sock[i] != CPLAT_INVALID_SOCKET)
            {
                cplat_socket_close(ctx->tcp_listen_sock[i]);
            }
        }
    }
    {
        int i;
        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            if (ctx->tcp_conn_fd[i] != CPLAT_INVALID_SOCKET)
            {
                cplat_socket_close(ctx->tcp_conn_fd[i]);
            }
        }
    }
    cleanup_sockets(ctx);
    cplat_free(ctx);
}

/* callback と role・通信種別の整合性を検証する (ctx->service 確定後に呼び出す) */
static int open_validate_callback(const potr_context *ctx, potr_role role, potr_recv_fn callback)
{
    if (role == POTR_ROLE_SENDER && callback != NULL && ctx->service.type != POTR_TYPE_UNICAST_BIDIR &&
        ctx->service.type != POTR_TYPE_TCP_BIDIR)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "potr_service_open: service_id=%" PRId64 " SENDER role must not have callback"
                   " (type=%d)",
                   ctx->service.service_id, (int)ctx->service.type);
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (role == POTR_ROLE_SENDER && callback == NULL &&
        (ctx->service.type == POTR_TYPE_UNICAST_BIDIR || ctx->service.type == POTR_TYPE_TCP_BIDIR))
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
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
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "potr_service_open: service_id=%" PRId64 " invalid max_payload=%u (range: 64..%u)",
                   ctx->service.service_id, (unsigned)ctx->global.max_payload, (unsigned)POTR_MAX_PAYLOAD);
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (ctx->global.window_size < 2U || ctx->global.window_size > POTR_MAX_WINDOW_SIZE)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "potr_service_open: service_id=%" PRId64 " invalid window_size=%u (range: 2..%u)",
                   ctx->service.service_id, (unsigned)ctx->global.window_size, (unsigned)POTR_MAX_WINDOW_SIZE);
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (ctx->global.max_message_size < (uint32_t)ctx->global.max_payload)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "potr_service_open: service_id=%" PRId64 " max_message_size=%u must be >= max_payload=%u",
                   ctx->service.service_id, (unsigned)ctx->global.max_message_size, (unsigned)ctx->global.max_payload);
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (ctx->global.send_queue_depth < 2U)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
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

    ctx->frag_buf = (uint8_t *)cplat_malloc(ctx->global.max_message_size);
    if (ctx->frag_buf == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    ctx->compress_buf_size = CPLAT_COMPRESS_HEADER_SIZE + (size_t)ctx->global.max_message_size + 64U;
    ctx->compress_buf = (uint8_t *)cplat_malloc(ctx->compress_buf_size);
    if (ctx->compress_buf == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    ctx->recv_buf = (uint8_t *)cplat_malloc(PACKET_HEADER_SIZE + ctx->global.max_payload);
    if (ctx->recv_buf == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    ctx->send_wire_buf = (uint8_t *)cplat_malloc(PACKET_HEADER_SIZE + ctx->global.max_payload);
    if (ctx->send_wire_buf == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    ctx->crypto_buf_size = ctx->global.max_payload + POTR_CRYPTO_TAG_SIZE;
    ctx->crypto_buf = (uint8_t *)cplat_malloc(ctx->crypto_buf_size);
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

    cplat_local_lock_dispose(ctx->tcp_state_mutex);
    cplat_condvar_dispose(ctx->tcp_state_cv);
    cplat_local_lock_dispose(ctx->tcp_close_mutex);
    cplat_condvar_dispose(ctx->tcp_close_cv);
    for (i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        cplat_local_lock_dispose(ctx->tcp_send_mutex[i]);
    }
    cplat_local_lock_dispose(ctx->recv_window_mutex);
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
        cplat_local_lock_create(&ctx->tcp_state_mutex);
        cplat_condvar_create(&ctx->tcp_state_cv);
        cplat_local_lock_create(&ctx->tcp_close_mutex);
        cplat_condvar_create(&ctx->tcp_close_cv);
        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            cplat_local_lock_create(&ctx->tcp_send_mutex[i]);
            cplat_local_lock_create(&ctx->health_mutex[i]);
            cplat_condvar_create(&ctx->health_wakeup[i]);
        }
        cplat_local_lock_create(&ctx->recv_window_mutex);
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
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_service_open: invalid argument (global=%p service=%p handle=%p)",
                   (const void *)global, (const void *)service, (const void *)handle);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "potr_service_open: service_id=%" PRId64 " role=%d", service->service_id,
               (int)role);

    /* role と callback の整合性チェック (設定読み込み前に確定できる部分のみ) */
    if (role == POTR_ROLE_RECEIVER && callback == NULL)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "potr_service_open: service_id=%" PRId64 " RECEIVER role requires callback", service->service_id);
        return POTR_ERR_INVALID_ARGUMENT;
    }
    /* SENDER + callback の完全チェックは設定読み込み後に行う
       (POTR_TYPE_UNICAST_BIDIR の SENDER は callback が必須のため) */
    if (role != POTR_ROLE_SENDER && role != POTR_ROLE_RECEIVER)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_service_open: service_id=%" PRId64 " unknown role=%d",
                   service->service_id, (int)role);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    ctx = (potr_context *)cplat_malloc(sizeof(potr_context));
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
            ctx->sock[i] = CPLAT_INVALID_SOCKET;
            ctx->tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
            ctx->tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
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

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
               "potr_service_open: service_id=%" PRId64 " type=%d window=%u max_payload=%u"
               " max_message_size=%u send_queue_depth=%u"
               " health_interval=%ums health_timeout=%ums tcp_close_timeout=%ums",
               ctx->service.service_id, (int)ctx->service.type, (unsigned)ctx->global.window_size,
               (unsigned)ctx->global.max_payload, (unsigned)ctx->global.max_message_size,
               (unsigned)ctx->global.send_queue_depth, (unsigned)ctx->health_interval_ms,
               (unsigned)ctx->health_timeout_ms, (unsigned)ctx->global.tcp_close_timeout_ms);

    /* 通信種別に応じてソケット・パスを準備する */
    result = api_open_paths_by_type(ctx, role);
    if (result != POTR_OK)
    {
        ctx_cleanup(ctx);
        return result;
    }

    ctx->callback = callback;
    ctx->role = role;

    /* 送信先ソケット アドレスを設定する */
    result = api_setup_dest_addr(ctx, role);
    if (result != POTR_OK)
    {
        ctx_cleanup(ctx);
        return result;
    }

    /* セッション識別子を生成する */
    if (generate_session(ctx) != CPLAT_OK)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_service_open: session id generation failed (service_id=%" PRId64 ")",
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
        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                   "potr_service_open: service_id=%" PRId64 " role=%s encrypt=%s opened successfully",
                   ctx->service.service_id, role_str, encrypt_str);
    }
    return POTR_OK;
}
