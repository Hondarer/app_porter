/**
 *******************************************************************************
 *  @file           potr_connect_thread.c
 *  @brief          TCP 接続を管理するスレッドを実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/23
 *  @version        1.0.0
 *
 *  SENDER: TCP connect/reconnect ループを管理するスレッドです。\n
 *  RECEIVER: TCP accept ループを管理するスレッドです。\n
 *  接続確立後、送受信・ヘルスチェック スレッドを起動し、\n
 *  recv スレッドが切断を検知して終了するまで待機してから再接続サイクルへ移行します。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>
#include <com_util/crt/stdlib.h>
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
#include <com_util/net/byteorder.h>
#include <com_util/net/endpoint.h>
#include <com_util/net/socket.h>

static void sync_tcp_service_path_state_locked(potr_context *ctx)
{
    int next_states[POTR_MAX_PATH];
    potr_internal_prepared_path_events prepared;

    potr_internal_copy_tcp_path_states(ctx, next_states);
    com_util_local_lock_lock(ctx->callback_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
    potr_internal_sync_service_path_state_locked(ctx, next_states, &prepared);
    potr_internal_emit_service_path_events_locked(ctx, &prepared);
    com_util_local_lock_unlock(ctx->callback_mutex);
}

static void set_tcp_path_ping_state(potr_context *ctx, int path_idx, uint8_t next_state)
{
    if (ctx->path_ping_state[path_idx] == next_state)
    {
        return;
    }

    ctx->path_ping_state[path_idx] = next_state;
    potr_internal_tcp_health_thread_wake_all(ctx);
}

/* TCP 接続ソケット [path_idx] をシャットダウン・クローズして INVALID にする */
static void close_tcp_conn(potr_context *ctx, int path_idx)
{
    if (ctx->tcp_conn_fd[path_idx] != COM_UTIL_INVALID_SOCKET)
    {
        com_util_socket_shutdown(ctx->tcp_conn_fd[path_idx]);
        com_util_socket_close(ctx->tcp_conn_fd[path_idx]);
        ctx->tcp_conn_fd[path_idx] = COM_UTIL_INVALID_SOCKET;
    }
}

/* 再接続待機: reconnect_interval_ms 経過または停止シグナルまでスリープする */
static void reconnect_wait(potr_context *ctx, int path_idx, int wait_ms)
{
    com_util_local_lock_lock(ctx->tcp_state_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
    if (ctx->connect_thread_running[path_idx])
    {
        com_util_condvar_wait(ctx->tcp_state_cv, ctx->tcp_state_mutex, wait_ms);
    }
    com_util_local_lock_unlock(ctx->tcp_state_mutex);
}

/* ================================================================
 * TCP セッション識別ヘルパー (accept スレッド専用)
 * ================================================================ */

/* accept 直後の TCP ソケットから 1 パケット分を buf に読み取る。
 * buf は PACKET_HEADER_SIZE + max_payload バイト以上確保されていること。
 * 戻り値: 成功時 (*len_out にバイト数を格納) は POTR_OK、タイムアウト時は POTR_ERR_TIMEOUT、
 * EOF 時は POTR_ERR_EOF、I/O エラー時は POTR_ERR_IO、不正時は POTR_ERR_PROTOCOL。 */
static int tcp_read_first_packet(com_util_socket fd, uint8_t *buf, size_t max_buf, size_t *len_out, int timeout_ms)
{
    int ready = 0;
    uint16_t wire_payload_len;
    com_util_error detail;

    /* タイムアウト付き待機 */
    if (com_util_socket_wait_readable(fd, timeout_ms, &ready, &detail) != COM_UTIL_OK)
        return potr_internal_result_from_error(&detail);
    if (!ready)
        return POTR_ERR_TIMEOUT;

    /* 固定長ヘッダー読み取り */
    {
        int recv_result = com_util_socket_recv_all(fd, buf, PACKET_HEADER_SIZE, &detail);
        if (recv_result != COM_UTIL_OK)
            return potr_internal_result_from_socket_result(recv_result, &detail);
    }

    /* payload_len は固定長ヘッダー末尾の offset 34 に格納される */
    {
        uint16_t wpl;
        memcpy(&wpl, buf + 34, sizeof(wpl));
        wire_payload_len = com_util_ntoh16(wpl);
    }

    /* ペイロード長バリデーション */
    if (PACKET_HEADER_SIZE + (size_t)wire_payload_len > max_buf)
        return POTR_ERR_PROTOCOL;

    /* ペイロード読み取り */
    if (wire_payload_len > 0)
    {
        int recv_result = com_util_socket_recv_all(fd, buf + PACKET_HEADER_SIZE, (size_t)wire_payload_len, &detail);
        if (recv_result != COM_UTIL_OK)
            return potr_internal_result_from_socket_result(recv_result, &detail);
    }

    *len_out = PACKET_HEADER_SIZE + (size_t)wire_payload_len;
    return POTR_OK;
}

/* session triplet 比較の戻り値。成否ではなく 3 状態の比較結果を表すため
   共通結果コードの適用対象外。 */
#define TCP_SESSION_NEW  (1)  /* 新セッション (または初回接続) */
#define TCP_SESSION_SAME (0)  /* 同一セッション                 */
#define TCP_SESSION_OLD  (-1) /* 旧セッション (破棄すべき)      */

/* ctx に記録されている相手セッションと pkt のセッション triplet を比較する。
 * peer_session_known == 0 の場合は TCP_SESSION_NEW を返す。
 * 呼び出し前提: session_establish_mutex を取得済みであること。 */
static int tcp_session_compare(const potr_context *ctx, const potr_packet *pkt)
{
    com_util_timespec pkt_session_ts;
    int ts_cmp;

    if (!ctx->peer_session_known)
        return TCP_SESSION_NEW;

    potr_session_ts_from_hdr(pkt->session_tv_sec, pkt->session_tv_nsec, &pkt_session_ts);
    ts_cmp = com_util_timespec_cmp(&pkt_session_ts, &ctx->peer_session_ts);

    if (ts_cmp > 0)
        return TCP_SESSION_NEW;
    else if (ts_cmp < 0)
        return TCP_SESSION_OLD;
    else if (pkt->session_id > ctx->peer_session_id)
        return TCP_SESSION_NEW;
    else if (pkt->session_id < ctx->peer_session_id)
        return TCP_SESSION_OLD;
    return TCP_SESSION_SAME;
}

/* recv スレッド [path_idx] の自然終了を待機してハンドルを解放する。
   TCP 接続断後 recv スレッドが自然終了する設計のため、ソケットはクローズしない。 */
static void join_recv_thread(potr_context *ctx, int path_idx)
{
    com_util_thread_join(ctx->recv_thread[path_idx], COM_UTIL_SYNC_WAIT_FOREVER);
}

/* 接続確立前のコンテキスト状態をリセットする。
   フラグメント バッファーや peer_session 状態をクリアする。
   TCP v2 マルチパスでは send_window 通番と health_alive は保持する
   (部分切断・再接続時のセッション継続のため)。
   呼び出しタイミング: start_connected_threads 前 (他スレッド未起動)。 */
static void reset_connection_state(potr_context *ctx)
{
    ctx->peer_session_known = 0;
    ctx->frag_buf_len = 0;
}

/* 全 path 切断時のリセット: send_window 通番・peer_session 状態・health_alive をクリアし
   DISCONNECTED イベントを発火する。
   呼び出しタイミング: tcp_active_paths が 0 になった直後 (tcp_state_mutex 非保護)。 */
static void reset_all_paths_disconnected(potr_context *ctx)
{
    ctx->peer_session_known = 0;
    ctx->frag_buf_len = 0;
    ctx->send_window.next_seq = 0U;
    ctx->send_window.base_seq = 0U;
    ctx->send_has_data = 0;
    if (ctx->send_window.valid != NULL)
    {
        memset(ctx->send_window.valid, 0, (size_t)ctx->send_window.window_size * sizeof(uint8_t));
    }
    memset((void *)ctx->remote_path_ping_state, 0, sizeof(ctx->remote_path_ping_state));
    memset((void *)ctx->path_logical_alive, 0, sizeof(ctx->path_logical_alive));
    ctx->health_alive = 0;
}

/* 送信キューを再初期化する (reconnect 時に shutdown 済みのキューをリセットする)。
   depth と max_payload はキュー構造体から取得する。 */
static void reset_send_queue(potr_context *ctx)
{
    size_t depth = ctx->send_queue.depth;
    uint16_t max_payload = (uint16_t)ctx->global.max_payload;
    potr_internal_send_queue_dispose(&ctx->send_queue);
    (void)potr_internal_send_queue_init(&ctx->send_queue, depth, max_payload);
}

/* 接続確立後に依存スレッドを起動する (path ごと)。
   SENDER および TCP_BIDIR RECEIVER: path_idx==0 の初回のみ send スレッドを起動する。
   各 path で recv スレッドと health スレッドを起動 (全ロール共通)。
   失敗時は起動済みスレッドをすべて停止して、失敗した処理の結果コードを返す。 */
static int start_connected_threads(potr_context *ctx, int path_idx)
{
    const potr_internal_connected_threads_ops ops = {potr_internal_send_thread_start,       potr_internal_send_thread_stop, potr_internal_tcp_recv_thread_start,
                                         potr_internal_tcp_health_thread_start, close_tcp_conn,        join_recv_thread,
                                         set_tcp_path_ping_state};

    return potr_internal_start_connected_threads(ctx, path_idx, &ops);
}

/* 接続断後に依存スレッドを停止する (path ごと)。
   呼び出し前提: join_recv_thread(path_idx) 完了済み (recv スレッドは終了している)。
   注意: 送信スレッドは共有のため、potr_internal_connect_thread_stop で全 path join 後に停止する。 */
static void stop_connected_threads(potr_context *ctx, int path_idx)
{
    /* health スレッドを先に停止 (PING 送信が tcp_conn_fd を参照するため) */
    potr_internal_tcp_health_thread_stop(ctx, path_idx);

    /* 接続ソケットをクローズ */
    close_tcp_conn(ctx, path_idx);
}

/* SENDER: path_idx 番目の宛先へ TCP 接続を試みる。
   connect_timeout_ms が 0 の場合は OS デフォルト (ブロッキング) で接続する。
   成功時はソケットを返す。失敗時は COM_UTIL_INVALID_SOCKET を返す。 */
static com_util_socket tcp_connect_with_timeout(potr_context *ctx, int path_idx)
{
    com_util_socket sock;
    com_util_ipv4_endpoint addr = {0};
    uint32_t timeout_ms = ctx->service.connect_timeout_ms;
    com_util_error detail;

    if (com_util_socket_open(COM_UTIL_SOCKET_TCP, &sock, &detail) != COM_UTIL_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_ERROR, &detail,
                                  "connect_thread[service_id=%" PRId64 " path=%d]: socket() failed",
                                  ctx->service.service_id, path_idx);
        return COM_UTIL_INVALID_SOCKET;
    }

    (void)com_util_socket_set_reuse_address(sock, 1, NULL);

    if (ctx->service.src_addr[path_idx][0] != '\0' || ctx->service.src_port != 0)
    {
        com_util_ipv4_endpoint bind_addr = {0};

        if (ctx->service.src_addr[path_idx][0] != '\0')
        {
            bind_addr.address = ctx->src_addr_resolved[path_idx];
        }
        else
        {
            bind_addr.address = COM_UTIL_IPV4_ADDR_ANY;
        }
        bind_addr.port = com_util_hton16(ctx->service.src_port); /* 0 = エフェメラル */

        if (com_util_socket_bind(sock, &bind_addr, &detail) != COM_UTIL_OK)
        {
            POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_ERROR, &detail,
                                      "connect_thread[service_id=%" PRId64 " path=%d]: bind() failed",
                                      ctx->service.service_id, path_idx);
            com_util_socket_close(sock);
            return COM_UTIL_INVALID_SOCKET;
        }
    }

    addr.address = ctx->dst_addr_resolved[path_idx];
    addr.port = com_util_hton16(ctx->service.dst_port);

    if (timeout_ms == 0U)
    {
        /* ブロッキング接続 */
        if (com_util_socket_connect(sock, &addr, &detail) != COM_UTIL_OK)
        {
            POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_VERBOSE, &detail,
                                      "connect_thread[service_id=%" PRId64 " path=%d]: connect() failed (blocking)",
                                      ctx->service.service_id, path_idx);
            com_util_socket_close(sock);
            return COM_UTIL_INVALID_SOCKET;
        }
        return sock;
    }

    /* タイムアウト付き接続: 非ブロッキング モードで writable ポーリングを使う。
       停止シグナルに素早く応答するため 100ms 単位でポーリングする。 */
    (void)com_util_socket_set_nonblocking(sock, 1, NULL);
    {
        const int connect_result = com_util_socket_connect(sock, &addr, &detail);
        if (connect_result == COM_UTIL_OK)
        {
            /* 即座に接続成功 */
            (void)com_util_socket_set_nonblocking(sock, 0, NULL);
            return sock;
        }
        if (connect_result != COM_UTIL_ERR_IN_PROGRESS)
        {
            POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_VERBOSE, &detail,
                                      "connect_thread[service_id=%" PRId64 " path=%d]: connect() failed",
                                      ctx->service.service_id, path_idx);
            com_util_socket_close(sock);
            return COM_UTIL_INVALID_SOCKET;
        }
    }

    /* writable ポーリング ループ (共通) */
    {
        uint32_t elapsed_ms = 0U;
        int ready = 0;

        while (elapsed_ms < timeout_ms && ctx->connect_thread_running[path_idx])
        {
            uint32_t poll_ms = timeout_ms - elapsed_ms;
            int wait_ready = 0;
            int wait_result;
            if (poll_ms > 100U)
                poll_ms = 100U;
            wait_result = com_util_socket_wait_writable(sock, (int)poll_ms, &wait_ready, NULL);
            if (wait_result != COM_UTIL_OK)
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
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE, "connect_thread[service_id=%" PRId64 "]: connect() timed out",
                       ctx->service.service_id);
            com_util_socket_close(sock);
            return COM_UTIL_INVALID_SOCKET;
        }
    }

    if (com_util_socket_get_pending_error(sock, &detail) != COM_UTIL_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_VERBOSE, &detail,
                                  "connect_thread[service_id=%" PRId64 " path=%d]: SO_ERROR", ctx->service.service_id,
                                  path_idx);
        com_util_socket_close(sock);
        return COM_UTIL_INVALID_SOCKET;
    }

    (void)com_util_socket_set_nonblocking(sock, 0, NULL);
    return sock;
}

/* SENDER 用接続ループ (path ごと) */
static void sender_connect_loop(potr_context *ctx, int path_idx)
{
    int is_reconnect = 0; /* 初回接続フラグ */

    while (ctx->connect_thread_running[path_idx])
    {
        com_util_socket sock;
        int active_count;

        POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO, "connect_thread[service_id=%" PRId64 " path=%d]: connecting to %s:%u ...",
                   ctx->service.service_id, path_idx, ctx->service.dst_addr[path_idx], (unsigned)ctx->service.dst_port);

        sock = tcp_connect_with_timeout(ctx, path_idx);

        if (sock == COM_UTIL_INVALID_SOCKET)
        {
            if (!ctx->connect_thread_running[path_idx])
                break;

            if (ctx->service.reconnect_interval_ms == 0U)
            {
                POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
                           "connect_thread[service_id=%" PRId64 " path=%d]: connect failed, "
                           "no reconnect (reconnect_interval_ms=0)",
                           ctx->service.service_id, path_idx);
                break;
            }

            POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
                       "connect_thread[service_id=%" PRId64 " path=%d]: connect failed, "
                       "retrying in %ums",
                       ctx->service.service_id, path_idx, (unsigned)ctx->service.reconnect_interval_ms);
            /* reconnect_interval_ms は再接続間隔の設定値。実用範囲は INT_MAX 以下 */
            reconnect_wait(ctx, path_idx, (int)ctx->service.reconnect_interval_ms);
            continue;
        }

        POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO, "connect_thread[service_id=%" PRId64 " path=%d]: TCP connected",
                   ctx->service.service_id, path_idx);

        ctx->tcp_conn_fd[path_idx] = sock;
        ctx->tcp_last_ping_recv_ms[path_idx] = com_util_get_monotonic_ms();

        /* tcp_active_paths カウンターをインクリメント (tcp_state_mutex 保護) */
        com_util_local_lock_lock(ctx->tcp_state_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
        active_count = ++ctx->tcp_active_paths;
        com_util_local_lock_unlock(ctx->tcp_state_mutex);
        (void)active_count; /* CONNECTED イベントは recv スレッドが最初のパケット受信時に発火 */

        reset_connection_state(ctx);

        /* 再接続時 (path[0] のみ): 全 path 切断後の再起動ではキューをリセット */
        if (is_reconnect && path_idx == 0)
        {
            reset_send_queue(ctx);
        }

        if (start_connected_threads(ctx, path_idx) != POTR_OK)
        {
            /* スレッド起動失敗: カウンターを戻す */
            com_util_local_lock_lock(ctx->tcp_state_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
            active_count = --ctx->tcp_active_paths;
            com_util_local_lock_unlock(ctx->tcp_state_mutex);
            if (active_count == 0)
            {
                reset_all_paths_disconnected(ctx);
            }

            if (!ctx->connect_thread_running[path_idx])
                break;
            if (ctx->service.reconnect_interval_ms == 0U)
                break;

            /* reconnect_interval_ms は再接続間隔の設定値。実用範囲は INT_MAX 以下 */
            reconnect_wait(ctx, path_idx, (int)ctx->service.reconnect_interval_ms);
            is_reconnect = 1;
            continue;
        }

        /* recv スレッドが接続断を検知して自然終了するまで待機する */
        join_recv_thread(ctx, path_idx);

        POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO, "connect_thread[service_id=%" PRId64 " path=%d]: TCP disconnected",
                   ctx->service.service_id, path_idx);

        stop_connected_threads(ctx, path_idx);

        com_util_local_lock_lock(ctx->tcp_state_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
        set_tcp_path_ping_state(ctx, path_idx, POTR_PING_STATE_UNDEFINED);
        sync_tcp_service_path_state_locked(ctx);
        com_util_local_lock_unlock(ctx->tcp_state_mutex);

        /* tcp_active_paths カウンターをデクリメント (tcp_state_mutex 保護) */
        com_util_local_lock_lock(ctx->tcp_state_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
        active_count = --ctx->tcp_active_paths;
        com_util_local_lock_unlock(ctx->tcp_state_mutex);

        if (active_count == 0)
        {
            /* 全 path 切断: send_window 通番と session をリセット */
            reset_all_paths_disconnected(ctx);
        }

        if (!ctx->connect_thread_running[path_idx])
            break;
        if (ctx->service.reconnect_interval_ms == 0U)
        {
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
                       "connect_thread[service_id=%" PRId64 " path=%d]: no reconnect "
                       "(reconnect_interval_ms=0)",
                       ctx->service.service_id, path_idx);
            break;
        }

        POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
                   "connect_thread[service_id=%" PRId64 " path=%d]: waiting %ums before reconnect",
                   ctx->service.service_id, path_idx, (unsigned)ctx->service.reconnect_interval_ms);
        /* reconnect_interval_ms は再接続間隔の設定値。実用範囲は INT_MAX 以下 */
        reconnect_wait(ctx, path_idx, (int)ctx->service.reconnect_interval_ms);
        is_reconnect = 1;
    }
}

/* RECEIVER 用 accept ループ (path ごと)
 *
 * [セッション層対称化]
 * accept() 直後に最初の 1 パケットを先読みし session_id を取得する。
 * session_establish_mutex 下で ctx の既知セッションと比較し、以下の 3 ケースを判別する。
 *   TCP_SESSION_NEW  : 新セッション (初回 or SENDER 再起動)
 *  → 他 path の既存接続に切断シグナルを送ってから新規セッションを開始する。
 *   TCP_SESSION_SAME : 同一セッションの追加パス (マルチパス)
 *  → reset_connection_state() を呼ばずにパスを追加する。
 *   TCP_SESSION_OLD  : 旧セッション (再送や遅延パケット等)
 *  → コネクションを閉じてループ先頭へ戻る。
 * 先読みパケットは tcp_first_pkt_buf/len に保存し、recv スレッドが起動直後に処理する。 */
static void receiver_accept_loop(potr_context *ctx, int path_idx)
{
    int is_bidir = (ctx->service.type == POTR_TYPE_TCP_BIDIR);
    int is_reconnect = 0;

    /* 先読みタイムアウト: TCP ヘルスチェック タイムアウトの POTR_TCP_FIRST_PKT_TIMEOUT_SCALE 倍、未設定時は既定値 */
    uint32_t first_pkt_timeout_ms = POTR_DEFAULT_TCP_FIRST_PKT_TIMEOUT_MS;
    if (ctx->global.tcp_health_timeout_ms > 0U)
    {
        first_pkt_timeout_ms = ctx->global.tcp_health_timeout_ms * POTR_TCP_FIRST_PKT_TIMEOUT_SCALE;
    }

    while (ctx->connect_thread_running[path_idx])
    {
        com_util_socket conn;
        com_util_ipv4_endpoint peer_addr = {0};
        int active_count;
        char peer_addr_str[COM_UTIL_IPV4_ADDR_STRLEN];
        com_util_error detail;
        int session_result;

        if (com_util_socket_accept(ctx->tcp_listen_sock[path_idx], &peer_addr, &conn, &detail) != COM_UTIL_OK)
        {
            if (!ctx->connect_thread_running[path_idx])
            {
                break;
            }
            /* 一時的なエラー: ループ継続 */
            POTR_TRACE_SOCKET_FAILURE(COM_UTIL_TRACE_LEVEL_VERBOSE, &detail,
                                      "connect_thread[service_id=%" PRId64 " path=%d]: accept() error, retrying",
                                      ctx->service.service_id, path_idx);
            continue;
        }

        (void)com_util_ipv4_to_string(peer_addr.address, peer_addr_str, sizeof(peer_addr_str), NULL);

        /* 接続元フィルター: src_addr[path_idx] / src_port が指定されていれば一致確認 */
        {
            int filtered = 0;
            if (ctx->service.src_addr[path_idx][0] != '\0')
            {
                if (peer_addr.address != ctx->src_addr_resolved[path_idx])
                {
                    filtered = 1;
                }
            }
            if (!filtered && ctx->service.src_port != 0)
            {
                if (com_util_ntoh16(peer_addr.port) != ctx->service.src_port)
                {
                    filtered = 1;
                }
            }
            if (filtered)
            {
                POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
                           "connect_thread[service_id=%" PRId64 " path=%d]: rejected connection"
                           " from %s:%u (src filter)",
                           ctx->service.service_id, path_idx, peer_addr_str, (unsigned)com_util_ntoh16(peer_addr.port));
                com_util_socket_close(conn);
                continue;
            }
        }

        POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO, "connect_thread[service_id=%" PRId64 " path=%d]: TCP accepted from %s:%u",
                   ctx->service.service_id, path_idx, peer_addr_str, (unsigned)com_util_ntoh16(peer_addr.port));

        /* ── セッション判定: 最初の 1 パケットを先読みして session_id を取得する ── */
        {
            potr_packet pkt;
            size_t pkt_len = 0;
            int r;

            /* first_pkt_timeout_ms は先読み待機の ms。実用範囲は INT_MAX 以下 */
            r = tcp_read_first_packet(conn, ctx->tcp_first_pkt_buf[path_idx],
                                      PACKET_HEADER_SIZE + ctx->global.max_payload, &pkt_len,
                                      (int)first_pkt_timeout_ms);
            if (r != POTR_OK)
            {
                /* タイムアウトまたは EOF/エラー */
                POTR_TRACE(COM_UTIL_TRACE_LEVEL_WARNING,
                           "connect_thread[service_id=%" PRId64 " path=%d]: "
                           "first packet read failed (r=%d), closing",
                           ctx->service.service_id, path_idx, r);
                com_util_socket_close(conn);
                continue;
            }

            if (potr_internal_packet_parse(&pkt, ctx->tcp_first_pkt_buf[path_idx], pkt_len) != POTR_OK)
            {
                POTR_TRACE(COM_UTIL_TRACE_LEVEL_WARNING,
                           "connect_thread[service_id=%" PRId64 " path=%d]: "
                           "first packet parse failed, closing",
                           ctx->service.service_id, path_idx);
                com_util_socket_close(conn);
                continue;
            }

            /* session_establish_mutex 下でセッション判定と状態更新を行う */
            com_util_local_lock_lock(ctx->session_establish_mutex, COM_UTIL_SYNC_WAIT_FOREVER);

            session_result = tcp_session_compare(ctx, &pkt);

            if (session_result == TCP_SESSION_OLD)
            {
                /* 旧セッション: 拒否 */
                com_util_local_lock_unlock(ctx->session_establish_mutex);
                POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
                           "connect_thread[service_id=%" PRId64 " path=%d]: "
                           "old session rejected (known_id=%u pkt_id=%u)",
                           ctx->service.service_id, path_idx, ctx->peer_session_id, pkt.session_id);
                com_util_socket_close(conn);
                continue;
            }

            if (session_result == TCP_SESSION_NEW)
            {
                /* 新セッション: 他 path の既存接続に切断シグナルを送る。
                 * cleanup は各 path の accept スレッドが自然に行う。 */
                int k;
                for (k = 0; k < ctx->n_path; k++)
                {
                    if (k == path_idx)
                        continue;
                    if (ctx->tcp_conn_fd[k] != COM_UTIL_INVALID_SOCKET)
                    {
                        ctx->running[k] = 0;
                        close_tcp_conn(ctx, k); /* recv ブロックを解除 */
                    }
                }
                reset_connection_state(ctx); /* peer_session_known = 0, frag_buf_len = 0 */
            }
            /* TCP_SESSION_SAME の場合は reset 不要 (セッション継続) */

            ctx->tcp_conn_fd[path_idx] = conn;
            ctx->tcp_last_ping_recv_ms[path_idx] = com_util_get_monotonic_ms();
            ctx->tcp_first_pkt_len[path_idx] = pkt_len; /* 先読みバッファー有効化 */

            com_util_local_lock_unlock(ctx->session_establish_mutex);
        }
        /* ── セッション判定ここまで ── */

        /* tcp_active_paths カウンターをインクリメント (tcp_state_mutex 保護) */
        com_util_local_lock_lock(ctx->tcp_state_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
        active_count = ++ctx->tcp_active_paths;
        com_util_local_lock_unlock(ctx->tcp_state_mutex);
        (void)active_count; /* CONNECTED イベントは recv スレッドが最初のパケット受信時に発火 */

        /* TCP_BIDIR 新セッション再接続時 (path[0] のみ): shutdown 済みのキューをリセット */
        if (is_bidir && session_result == TCP_SESSION_NEW && is_reconnect && path_idx == 0)
        {
            reset_send_queue(ctx);
        }

        if (start_connected_threads(ctx, path_idx) != POTR_OK)
        {
            com_util_local_lock_lock(ctx->tcp_state_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
            active_count = --ctx->tcp_active_paths;
            com_util_local_lock_unlock(ctx->tcp_state_mutex);
            if (active_count == 0)
            {
                reset_all_paths_disconnected(ctx);
            }
            ctx->tcp_first_pkt_len[path_idx] = 0; /* 先読みバッファーを無効化 */
            close_tcp_conn(ctx, path_idx);
            is_reconnect = 1;
            continue;
        }

        /* recv スレッドが接続断を検知して自然終了するまで待機する */
        join_recv_thread(ctx, path_idx);

        POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO, "connect_thread[service_id=%" PRId64 " path=%d]: TCP connection closed",
                   ctx->service.service_id, path_idx);

        stop_connected_threads(ctx, path_idx);

        com_util_local_lock_lock(ctx->tcp_state_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
        set_tcp_path_ping_state(ctx, path_idx, POTR_PING_STATE_UNDEFINED);
        sync_tcp_service_path_state_locked(ctx);
        com_util_local_lock_unlock(ctx->tcp_state_mutex);

        /* 先読みバッファーをクリア (recv スレッドが未処理のまま終了した場合の安全策) */
        ctx->tcp_first_pkt_len[path_idx] = 0;

        /* tcp_active_paths カウンターをデクリメント (tcp_state_mutex 保護) */
        com_util_local_lock_lock(ctx->tcp_state_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
        active_count = --ctx->tcp_active_paths;
        com_util_local_lock_unlock(ctx->tcp_state_mutex);

        if (active_count == 0)
        {
            reset_all_paths_disconnected(ctx);
        }

        is_reconnect = 1;
        /* ループ継続: 次の accept へ */
    }
}

/* 接続管理スレッド本体 (potr_internal_path_thread_arg* を受け取り、path ごとに動作) */
static void connect_thread_func(void *arg)
{
    potr_internal_path_thread_arg *carg = (potr_internal_path_thread_arg *)arg;
    potr_context *ctx = carg->ctx;
    int path_idx = carg->path_idx;
    const char *role_str;
    const char *type_str;

    if (ctx->role == POTR_ROLE_SENDER)
    {
        role_str = "SENDER";
    }
    else
    {
        role_str = "RECEIVER";
    }

    if (ctx->service.type == POTR_TYPE_TCP_BIDIR)
    {
        type_str = "TCP_BIDIR";
    }
    else
    {
        type_str = "TCP";
    }

    POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
               "connect_thread[service_id=%" PRId64 " path=%d]: started (role=%s type=%s)", ctx->service.service_id,
               path_idx, role_str, type_str);

    if (ctx->role == POTR_ROLE_SENDER)
    {
        sender_connect_loop(ctx, path_idx);
    }
    else
    {
        receiver_accept_loop(ctx, path_idx);
    }

    ctx->connect_thread_running[path_idx] = 0;

    POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE, "connect_thread[service_id=%" PRId64 " path=%d]: exited",
               ctx->service.service_id, path_idx);

    return;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_connect_thread_start(potr_context *ctx)
{
    int i;

    if (ctx == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE, "connect_thread[service_id=%" PRId64 "]: starting %d path(s)",
               ctx->service.service_id, ctx->n_path);

    /* RECEIVER: session_establish_mutex と先読みバッファーを初期化する */
    if (ctx->role == POTR_ROLE_RECEIVER)
    {
        com_util_local_lock_create(&ctx->session_establish_mutex);

        for (i = 0; i < ctx->n_path; i++)
        {
            ctx->tcp_first_pkt_len[i] = 0;
            ctx->tcp_first_pkt_buf[i] = (uint8_t *)com_util_malloc(PACKET_HEADER_SIZE + ctx->global.max_payload);
            if (ctx->tcp_first_pkt_buf[i] == NULL)
            {
                int j;
                POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                           "connect_thread[service_id=%" PRId64 "]: "
                           "tcp_first_pkt_buf[%d] malloc failed",
                           ctx->service.service_id, i);
                /* 確保済み分を解放 */
                for (j = 0; j < i; j++)
                {
                    com_util_free(ctx->tcp_first_pkt_buf[j]);
                    ctx->tcp_first_pkt_buf[j] = NULL;
                }
                com_util_local_lock_destroy(ctx->session_establish_mutex);
                return POTR_ERR_OUT_OF_MEMORY;
            }
        }
    }

    for (i = 0; i < ctx->n_path; i++)
    {
        ctx->connect_thread_running[i] = 1;
        ctx->connect_args[i].ctx = ctx;
        ctx->connect_args[i].path_idx = i;

        if (com_util_thread_create(&ctx->connect_thread[i], connect_thread_func, &ctx->connect_args[i]) != COM_UTIL_OK)
        {
            ctx->connect_thread_running[i] = 0;
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                       "connect_thread[service_id=%" PRId64 " path=%d]: thread create failed", ctx->service.service_id,
                       i);
            /* com_util のスレッド生成失敗には、porter の分類へ変換できる詳細コードがありません。 */
            return POTR_ERR_UNKNOWN;
        }
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_connect_thread_stop(potr_context *ctx)
{
    int i;
    int any_running = 0;

    if (ctx == NULL)
    {
        return;
    }

    for (i = 0; i < ctx->n_path; i++)
    {
        if (ctx->connect_thread_running[i])
        {
            any_running = 1;
            break;
        }
    }
    if (!any_running)
    {
        return;
    }

    /* 1. 全 path の停止フラグをクリア */
    for (i = 0; i < ctx->n_path; i++)
    {
        ctx->connect_thread_running[i] = 0;
    }

    /* 2. reconnect_wait 中の全スレッドを起床させる */
    com_util_local_lock_lock(ctx->tcp_state_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
    com_util_condvar_broadcast(ctx->tcp_state_cv);
    com_util_local_lock_unlock(ctx->tcp_state_mutex);

    /* 3. RECEIVER: 全 path の listen ソケットをクローズして accept のブロックを解除 */
    if (ctx->role == POTR_ROLE_RECEIVER)
    {
        for (i = 0; i < ctx->n_path; i++)
        {
            if (ctx->tcp_listen_sock[i] == COM_UTIL_INVALID_SOCKET)
                continue;
            com_util_socket_shutdown(ctx->tcp_listen_sock[i]);
            com_util_socket_close(ctx->tcp_listen_sock[i]);
            ctx->tcp_listen_sock[i] = COM_UTIL_INVALID_SOCKET;
        }
    }

    /* 4. 全 path の接続ソケットをクローズして recv ループのブロックを解除 */
    for (i = 0; i < ctx->n_path; i++)
    {
        if (ctx->tcp_conn_fd[i] == COM_UTIL_INVALID_SOCKET)
            continue;
        com_util_socket_shutdown(ctx->tcp_conn_fd[i]);
        com_util_socket_close(ctx->tcp_conn_fd[i]);
        ctx->tcp_conn_fd[i] = COM_UTIL_INVALID_SOCKET;
    }

    /* 5. 全 connect スレッドの終了を待機する */
    for (i = 0; i < ctx->n_path; i++)
    {
        com_util_thread_join(ctx->connect_thread[i], COM_UTIL_SYNC_WAIT_FOREVER);
    }

    /* 6. 送信スレッドを停止する (全 path join 後) */
    potr_internal_send_thread_stop(ctx);

    /* 7. RECEIVER: session_establish_mutex と先読みバッファーを破棄する */
    if (ctx->role == POTR_ROLE_RECEIVER)
    {
        for (i = 0; i < ctx->n_path; i++)
        {
            ctx->tcp_first_pkt_len[i] = 0;
            if (ctx->tcp_first_pkt_buf[i] != NULL)
            {
                com_util_free(ctx->tcp_first_pkt_buf[i]);
                ctx->tcp_first_pkt_buf[i] = NULL;
            }
        }
        com_util_local_lock_destroy(ctx->session_establish_mutex);
    }

    POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE, "connect_thread[service_id=%" PRId64 "]: all paths stopped",
               ctx->service.service_id);
}
