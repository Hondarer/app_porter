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
 *  接続確立後、送受信・ヘルス チェック スレッドを起動し、\n
 *  recv スレッドが切断を検知して終了するまで待機してから再接続サイクルへ移行します。
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

static void sync_tcp_service_path_state_locked(potr_context *ctx)
{
    int next_states[POTR_MAX_PATH];
    potr_internal_prepared_path_events prepared;

    potr_internal_copy_tcp_path_states(ctx, next_states);
    cplat_local_lock_lock(ctx->callback_mutex, CPLAT_SYNC_WAIT_FOREVER);
    potr_internal_sync_service_path_state_locked(ctx, next_states, &prepared);
    potr_internal_emit_service_path_events_locked(ctx, &prepared);
    cplat_local_lock_unlock(ctx->callback_mutex);
}

static void set_tcp_path_ping_state(potr_context *ctx, int path_idx, uint8_t next_state)
{
    if (cplat_atomic_load_u8(&ctx->path_ping_state[path_idx], CPLAT_MEMORY_ORDER_RELAXED) == next_state)
    {
        return;
    }

    cplat_atomic_store_u8(&ctx->path_ping_state[path_idx], next_state, CPLAT_MEMORY_ORDER_RELAXED);
    potr_internal_tcp_health_thread_wake_all(ctx);
}

/* TCP 接続ソケット [path_idx] をシャットダウン・クローズして INVALID にする */
static void close_tcp_conn(potr_context *ctx, int path_idx)
{
    cplat_socket closing_fd = ctx->tcp_conn_fd[path_idx];

    if (closing_fd == CPLAT_INVALID_SOCKET)
    {
        return;
    }

    /* 送信完了待ちを先に解除し、その後で close と fd 無効化を送信処理と直列化する。 */
    cplat_socket_shutdown(closing_fd);
    cplat_local_lock_lock(ctx->tcp_send_mutex[path_idx], CPLAT_SYNC_WAIT_FOREVER);
    if (ctx->tcp_conn_fd[path_idx] == closing_fd)
    {
        cplat_socket_close(closing_fd);
        ctx->tcp_conn_fd[path_idx] = CPLAT_INVALID_SOCKET;
    }
    cplat_local_lock_unlock(ctx->tcp_send_mutex[path_idx]);
}

/* 再接続待機: reconnect_interval_ms 経過または停止シグナルまでスリープする */
static void reconnect_wait(potr_context *ctx, int path_idx, int wait_ms)
{
    cplat_local_lock_lock(ctx->tcp_state_mutex, CPLAT_SYNC_WAIT_FOREVER);
    if (cplat_atomic_load_i32(&ctx->connect_thread_running[path_idx], CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
    {
        cplat_condvar_wait(ctx->tcp_state_cv, ctx->tcp_state_mutex, wait_ms);
    }
    cplat_local_lock_unlock(ctx->tcp_state_mutex);
}

/* recv スレッド [path_idx] の自然終了を待機してハンドルを解放する。
   TCP 接続断後 recv スレッドが自然終了する設計のため、ソケットはクローズしない。 */
static void join_recv_thread(potr_context *ctx, int path_idx)
{
    if (ctx->recv_thread[path_idx] != NULL &&
        cplat_thread_join(ctx->recv_thread[path_idx], CPLAT_SYNC_WAIT_FOREVER) == CPLAT_OK)
    {
        ctx->recv_thread[path_idx] = NULL;
    }
}

/* 接続確立前のコンテキスト状態をリセットする。
   フラグメント バッファーや peer_session 状態をクリアする。
   TCP v2 マルチパスでは send_window 通番と health_alive は保持する
   (部分切断・再接続時のセッション継続のため)。
   呼び出し前提: tcp_recv_mutex を取得済みであること。 */
static void reset_connection_state(potr_context *ctx)
{
    ctx->peer_session_known = 0;
    ctx->frag_buf_len = 0;
    ctx->frag_compressed = 0;
    ctx->pending_fin = 0;
}

/* 全 path 切断時のリセット: send_window 通番・peer_session 状態・health_alive をクリアする。
   ロック順は tcp_recv_mutex -> tcp_state_mutex -> send_window_mutex とする。
   共有送信スレッドと送信キューは再接続に備えて維持する。接続がない間に送信スレッドが
   取り出したデータは、既存の動作どおり送信せずに破棄される。
   呼び出しタイミング: tcp_active_paths が 0 になった直後 (tcp_state_mutex 非保護)。 */
static void reset_all_paths_disconnected(potr_context *ctx)
{
    cplat_local_lock_lock(ctx->tcp_recv_mutex, CPLAT_SYNC_WAIT_FOREVER);
    cplat_local_lock_lock(ctx->tcp_state_mutex, CPLAT_SYNC_WAIT_FOREVER);
    if (cplat_atomic_load_i32(&ctx->tcp_active_paths, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
    {
        cplat_local_lock_unlock(ctx->tcp_state_mutex);
        cplat_local_lock_unlock(ctx->tcp_recv_mutex);
        return;
    }

    /* send_window_mutex は send_thread と送信状態を共有する場合だけ存在する。
     * 未起動または起動失敗後は並行更新元がないため、NULL のままリセットできる。 */
    if (ctx->send_window_mutex != NULL)
    {
        cplat_local_lock_lock(ctx->send_window_mutex, CPLAT_SYNC_WAIT_FOREVER);
    }
    ctx->peer_session_known = 0;
    ctx->frag_buf_len = 0;
    ctx->frag_compressed = 0;
    ctx->pending_fin = 0;
    ctx->send_window.next_seq = 0U;
    ctx->send_window.base_seq = 0U;
    ctx->send_window.base_index = 0U;
    ctx->send_has_data = 0;
    if (ctx->send_window.valid != NULL)
    {
        memset(ctx->send_window.valid, 0, (size_t)ctx->send_window.window_size * sizeof(uint8_t));
    }
    if (ctx->send_window_mutex != NULL)
    {
        cplat_local_lock_unlock(ctx->send_window_mutex);
    }
    memset((void *)ctx->remote_path_ping_state, 0, sizeof(ctx->remote_path_ping_state));
    memset((void *)ctx->path_logical_alive, 0, sizeof(ctx->path_logical_alive));
    cplat_atomic_store_i32(&ctx->health_alive, 0, CPLAT_MEMORY_ORDER_RELEASE);
    ctx->tcp_accepted_session_known = 0;
    cplat_local_lock_unlock(ctx->tcp_state_mutex);
    cplat_local_lock_unlock(ctx->tcp_recv_mutex);
}

/* 接続確立後に依存スレッドを起動する (path ごと)。
   SENDER および TCP_BIDIR RECEIVER: path_idx==0 の初回のみ send スレッドを起動する。
   各 path で recv スレッドと health スレッドを起動 (全ロール共通)。
   失敗時は起動済みスレッドをすべて停止して、失敗した処理の結果コードを返す。 */
static int start_connected_threads(potr_context *ctx, int path_idx)
{
    /* キューの shutdown は待機解除だけであり、再初期化は不要。
     * 共有送信スレッドと API が参照するキューを再接続時も維持する。 */
    const potr_internal_connected_threads_ops ops = {potr_internal_send_thread_start,
                                                     potr_internal_send_thread_stop,
                                                     potr_internal_tcp_recv_thread_start,
                                                     potr_internal_tcp_health_thread_start,
                                                     close_tcp_conn,
                                                     join_recv_thread,
                                                     set_tcp_path_ping_state};

    return potr_internal_start_connected_threads(ctx, path_idx, &ops);
}

/* 接続断後に依存スレッドを停止する (path ごと)。
   呼び出し前提: join_recv_thread(path_idx) 完了済み (recv スレッドは終了している)。
   注意: 送信スレッドは共有のため、potr_internal_connect_thread_stop で全 path join 後に停止する。 */
static void stop_connected_threads(potr_context *ctx, int path_idx)
{
    /* shutdown で DATA/PING 送信待ちを解除してから health スレッドを停止する。 */
    close_tcp_conn(ctx, path_idx);
    potr_internal_tcp_health_thread_stop(ctx, path_idx);
}

/* SENDER 用接続ループ (path ごと) */
static void sender_connect_loop(potr_context *ctx, int path_idx)
{
    while (cplat_atomic_load_i32(&ctx->connect_thread_running[path_idx], CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
    {
        cplat_socket sock;
        int active_count;

        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "connect_thread[service_id=%" PRId64 " path=%d]: connecting to %s:%u ...",
                   ctx->service.service_id, path_idx, ctx->service.dst_addr[path_idx], (unsigned)ctx->service.dst_port);

        sock = thread_connect_with_timeout(ctx, path_idx);

        if (sock == CPLAT_INVALID_SOCKET)
        {
            if (cplat_atomic_load_i32(&ctx->connect_thread_running[path_idx], CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
                break;

            if (ctx->service.reconnect_interval_ms == 0U)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                           "connect_thread[service_id=%" PRId64 " path=%d]: connect failed, "
                           "no reconnect (reconnect_interval_ms=0)",
                           ctx->service.service_id, path_idx);
                break;
            }

            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                       "connect_thread[service_id=%" PRId64 " path=%d]: connect failed, "
                       "retrying in %ums",
                       ctx->service.service_id, path_idx, (unsigned)ctx->service.reconnect_interval_ms);
            /* reconnect_interval_ms は再接続間隔の設定値。実用範囲は INT_MAX 以下 */
            reconnect_wait(ctx, path_idx, (int)ctx->service.reconnect_interval_ms);
            continue;
        }

        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "connect_thread[service_id=%" PRId64 " path=%d]: TCP connected",
                   ctx->service.service_id, path_idx);

        /* 0→1 の初期化を完了してから接続を公開する。 */
        cplat_local_lock_lock(ctx->tcp_recv_mutex, CPLAT_SYNC_WAIT_FOREVER);
        cplat_local_lock_lock(ctx->tcp_state_mutex, CPLAT_SYNC_WAIT_FOREVER);
        active_count = cplat_atomic_fetch_add_i32(&ctx->tcp_active_paths, 1, CPLAT_MEMORY_ORDER_ACQ_REL) + 1;
        if (active_count == 1)
        {
            reset_connection_state(ctx);
        }

        ctx->tcp_conn_fd[path_idx] = sock;
        cplat_atomic_store_u64(&ctx->tcp_last_ping_recv_ms[path_idx], cplat_clock_get_monotonic_ms(),
                               CPLAT_MEMORY_ORDER_RELAXED);
        cplat_local_lock_unlock(ctx->tcp_state_mutex);
        cplat_local_lock_unlock(ctx->tcp_recv_mutex);

        if (start_connected_threads(ctx, path_idx) != POTR_OK)
        {
            /* スレッド起動失敗: カウンターを戻す */
            cplat_local_lock_lock(ctx->tcp_state_mutex, CPLAT_SYNC_WAIT_FOREVER);
            active_count = cplat_atomic_fetch_sub_i32(&ctx->tcp_active_paths, 1, CPLAT_MEMORY_ORDER_ACQ_REL) - 1;
            cplat_local_lock_unlock(ctx->tcp_state_mutex);
            if (active_count == 0)
            {
                reset_all_paths_disconnected(ctx);
            }

            if (cplat_atomic_load_i32(&ctx->connect_thread_running[path_idx], CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
                break;
            if (ctx->service.reconnect_interval_ms == 0U)
                break;

            /* reconnect_interval_ms は再接続間隔の設定値。実用範囲は INT_MAX 以下 */
            reconnect_wait(ctx, path_idx, (int)ctx->service.reconnect_interval_ms);
            continue;
        }

        /* recv スレッドが接続断を検知して自然終了するまで待機する */
        join_recv_thread(ctx, path_idx);

        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "connect_thread[service_id=%" PRId64 " path=%d]: TCP disconnected",
                   ctx->service.service_id, path_idx);

        stop_connected_threads(ctx, path_idx);

        cplat_local_lock_lock(ctx->tcp_state_mutex, CPLAT_SYNC_WAIT_FOREVER);
        set_tcp_path_ping_state(ctx, path_idx, POTR_PING_STATE_UNDEFINED);
        sync_tcp_service_path_state_locked(ctx);
        cplat_local_lock_unlock(ctx->tcp_state_mutex);

        /* tcp_active_paths カウンターをデクリメント (tcp_state_mutex 保護) */
        cplat_local_lock_lock(ctx->tcp_state_mutex, CPLAT_SYNC_WAIT_FOREVER);
        active_count = cplat_atomic_fetch_sub_i32(&ctx->tcp_active_paths, 1, CPLAT_MEMORY_ORDER_ACQ_REL) - 1;
        cplat_local_lock_unlock(ctx->tcp_state_mutex);

        if (active_count == 0)
        {
            /* 全 path 切断: send_window 通番と session をリセット */
            reset_all_paths_disconnected(ctx);
        }

        if (cplat_atomic_load_i32(&ctx->connect_thread_running[path_idx], CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
            break;
        if (ctx->service.reconnect_interval_ms == 0U)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                       "connect_thread[service_id=%" PRId64 " path=%d]: no reconnect "
                       "(reconnect_interval_ms=0)",
                       ctx->service.service_id, path_idx);
            break;
        }

        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                   "connect_thread[service_id=%" PRId64 " path=%d]: waiting %ums before reconnect",
                   ctx->service.service_id, path_idx, (unsigned)ctx->service.reconnect_interval_ms);
        /* reconnect_interval_ms は再接続間隔の設定値。実用範囲は INT_MAX 以下 */
        reconnect_wait(ctx, path_idx, (int)ctx->service.reconnect_interval_ms);
    }
}

/* RECEIVER 用 accept ループ (path ごと)
 *
 * [セッション層対称化]
 * accept() 直後に最初の 1 パケットを先読みし session_id を取得する。
 * session_establish_mutex 下で ctx の既知セッションと比較し、以下の 3 ケースを判別する。
 *   TCP_SESSION_NEW  : 新セッション (初回 or SENDER 再起動)
 *  → 他 path の既存接続に切断シグナルを送信してから新規セッションを開始する。
 *   TCP_SESSION_SAME : 同一セッションの追加パス (マルチパス)
 *  → reset_connection_state() を呼び出さずにパスを追加する。
 *   TCP_SESSION_OLD  : 旧セッション (再送や遅延パケット等)
 *  → コネクションを閉じてループ先頭へ戻る。
 * 先読みパケットは tcp_first_pkt_buf/len に保存し、recv スレッドが起動直後に処理する。 */
static void receiver_accept_loop(potr_context *ctx, int path_idx)
{
    /* 先読みタイムアウト: TCP ヘルス チェック タイムアウトの POTR_TCP_FIRST_PKT_TIMEOUT_SCALE 倍、未設定時は既定値 */
    uint32_t first_pkt_timeout_ms = POTR_DEFAULT_TCP_FIRST_PKT_TIMEOUT_MS;
    if (ctx->global.tcp_health_timeout_ms > 0U)
    {
        first_pkt_timeout_ms = ctx->global.tcp_health_timeout_ms * POTR_TCP_FIRST_PKT_TIMEOUT_SCALE;
    }

    while (cplat_atomic_load_i32(&ctx->connect_thread_running[path_idx], CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
    {
        cplat_socket conn;
        cplat_ipv4_endpoint peer_addr = {0};
        int active_count;
        char peer_addr_str[CPLAT_IPV4_ADDR_STRLEN];
        cplat_error detail;
        int session_result;

        if (cplat_socket_accept(ctx->tcp_listen_sock[path_idx], &peer_addr, &conn, &detail) != CPLAT_OK)
        {
            if (cplat_atomic_load_i32(&ctx->connect_thread_running[path_idx], CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
            {
                break;
            }
            /* 一時的なエラー: ループ継続 */
            POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_VERBOSE, &detail,
                                      "connect_thread[service_id=%" PRId64 " path=%d]: accept() error, retrying",
                                      ctx->service.service_id, path_idx);
            continue;
        }

        (void)cplat_ipv4_to_string(peer_addr.address, peer_addr_str, sizeof(peer_addr_str), NULL);

        /* 接続元フィルター: src_addr[path_idx] / src_port が指定されていれば一致確認 */
        {
            int filtered = !thread_connect_peer_allowed(ctx, path_idx, &peer_addr);
            if (filtered)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                           "connect_thread[service_id=%" PRId64 " path=%d]: rejected connection"
                           " from %s:%u (src filter)",
                           ctx->service.service_id, path_idx, peer_addr_str, (unsigned)cplat_ntoh16(peer_addr.port));
                cplat_socket_close(conn);
                continue;
            }
        }

        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "connect_thread[service_id=%" PRId64 " path=%d]: TCP accepted from %s:%u",
                   ctx->service.service_id, path_idx, peer_addr_str, (unsigned)cplat_ntoh16(peer_addr.port));

        /* ── セッション判定: 最初の 1 パケットを先読みして session_id を取得する ── */
        {
            potr_packet pkt;
            size_t pkt_len = 0;
            int r;

            /* first_pkt_timeout_ms は先読み待機の ms。実用範囲は INT_MAX 以下 */
            r = thread_connect_read_first_packet(conn, ctx->tcp_first_pkt_buf[path_idx],
                                                 POTR_PACKET_HEADER_SIZE + ctx->global.max_payload, &pkt_len,
                                                 (int)first_pkt_timeout_ms);
            if (r != POTR_OK)
            {
                /* タイムアウトまたは EOF/エラー */
                POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                           "connect_thread[service_id=%" PRId64 " path=%d]: "
                           "first packet read failed (r=%d), closing",
                           ctx->service.service_id, path_idx, r);
                cplat_socket_close(conn);
                continue;
            }

            if (potr_internal_packet_parse(&pkt, ctx->tcp_first_pkt_buf[path_idx], pkt_len) != POTR_OK)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                           "connect_thread[service_id=%" PRId64 " path=%d]: "
                           "first packet parse failed, closing",
                           ctx->service.service_id, path_idx);
                cplat_socket_close(conn);
                continue;
            }

            /* session_establish_mutex 下でセッション判定と状態更新を行う */
            cplat_local_lock_lock(ctx->session_establish_mutex, CPLAT_SYNC_WAIT_FOREVER);

            cplat_local_lock_lock(ctx->tcp_recv_mutex, CPLAT_SYNC_WAIT_FOREVER);
            if (pkt.service_id != ctx->service.service_id ||
                thread_recv_authenticate_packet(ctx, &pkt, ctx->tcp_first_pkt_buf[path_idx], "tcp_accept", path_idx) !=
                    POTR_OK)
            {
                cplat_local_lock_unlock(ctx->tcp_recv_mutex);
                cplat_local_lock_unlock(ctx->session_establish_mutex);
                cplat_socket_close(conn);
                continue;
            }
            session_result = thread_connect_session_compare(ctx, &pkt);

            if (session_result == TCP_SESSION_OLD)
            {
                /* 旧セッション: 拒否 */
                POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                           "connect_thread[service_id=%" PRId64 " path=%d]: "
                           "old session rejected (known_id=%u pkt_id=%u)",
                           ctx->service.service_id, path_idx, ctx->tcp_accepted_session_id, pkt.session_id);
                cplat_local_lock_unlock(ctx->tcp_recv_mutex);
                cplat_local_lock_unlock(ctx->session_establish_mutex);
                cplat_socket_close(conn);
                continue;
            }

            if (session_result == TCP_SESSION_NEW)
            {
                /* 新セッション: 他 path の既存接続に切断シグナルを送信する。
                 * cleanup は各 path の accept スレッドが自然に行う。 */
                int k;
                for (k = 0; k < ctx->n_path; k++)
                {
                    if (k == path_idx)
                        continue;
                    if (ctx->tcp_conn_fd[k] != CPLAT_INVALID_SOCKET)
                    {
                        cplat_atomic_store_i32(&ctx->running[k], 0, CPLAT_MEMORY_ORDER_RELEASE);
                        close_tcp_conn(ctx, k); /* recv ブロックを解除 */
                    }
                }
                reset_connection_state(ctx); /* peer_session_known = 0, frag_buf_len = 0 */
                ctx->tcp_accepted_session_id = pkt.session_id;
                session_ts_from_hdr(pkt.session_tv_sec, pkt.session_tv_nsec, &ctx->tcp_accepted_session_ts);
                ctx->tcp_accepted_session_known = 1;
            }
            /* TCP_SESSION_SAME の場合は reset 不要 (セッション継続) */

            cplat_local_lock_lock(ctx->tcp_state_mutex, CPLAT_SYNC_WAIT_FOREVER);
            (void)cplat_atomic_fetch_add_i32(&ctx->tcp_active_paths, 1, CPLAT_MEMORY_ORDER_ACQ_REL);
            ctx->tcp_conn_fd[path_idx] = conn;
            cplat_atomic_store_u64(&ctx->tcp_last_ping_recv_ms[path_idx], cplat_clock_get_monotonic_ms(),
                                   CPLAT_MEMORY_ORDER_RELAXED);
            ctx->tcp_first_pkt_len[path_idx] = pkt_len; /* 先読みバッファー有効化 */

            cplat_local_lock_unlock(ctx->tcp_state_mutex);
            cplat_local_lock_unlock(ctx->tcp_recv_mutex);
        }
        /* ── セッション判定ここまで ── */

        if (start_connected_threads(ctx, path_idx) != POTR_OK)
        {
            cplat_local_lock_lock(ctx->tcp_state_mutex, CPLAT_SYNC_WAIT_FOREVER);
            active_count = cplat_atomic_fetch_sub_i32(&ctx->tcp_active_paths, 1, CPLAT_MEMORY_ORDER_ACQ_REL) - 1;
            cplat_local_lock_unlock(ctx->tcp_state_mutex);
            if (active_count == 0)
            {
                reset_all_paths_disconnected(ctx);
            }
            ctx->tcp_first_pkt_len[path_idx] = 0; /* 先読みバッファーを無効化 */
            close_tcp_conn(ctx, path_idx);
            cplat_local_lock_unlock(ctx->session_establish_mutex);
            continue;
        }
        cplat_local_lock_unlock(ctx->session_establish_mutex);

        /* recv スレッドが接続断を検知して自然終了するまで待機する */
        join_recv_thread(ctx, path_idx);

        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "connect_thread[service_id=%" PRId64 " path=%d]: TCP connection closed",
                   ctx->service.service_id, path_idx);

        stop_connected_threads(ctx, path_idx);

        cplat_local_lock_lock(ctx->tcp_state_mutex, CPLAT_SYNC_WAIT_FOREVER);
        set_tcp_path_ping_state(ctx, path_idx, POTR_PING_STATE_UNDEFINED);
        sync_tcp_service_path_state_locked(ctx);
        cplat_local_lock_unlock(ctx->tcp_state_mutex);

        /* 先読みバッファーをクリア (recv スレッドが未処理のまま終了した場合の安全策) */
        ctx->tcp_first_pkt_len[path_idx] = 0;

        /* tcp_active_paths カウンターをデクリメント (tcp_state_mutex 保護) */
        cplat_local_lock_lock(ctx->tcp_state_mutex, CPLAT_SYNC_WAIT_FOREVER);
        active_count = cplat_atomic_fetch_sub_i32(&ctx->tcp_active_paths, 1, CPLAT_MEMORY_ORDER_ACQ_REL) - 1;
        cplat_local_lock_unlock(ctx->tcp_state_mutex);

        if (active_count == 0)
        {
            reset_all_paths_disconnected(ctx);
        }

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

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "connect_thread[service_id=%" PRId64 " path=%d]: started (role=%s type=%s)",
               ctx->service.service_id, path_idx, role_str, type_str);

    if (ctx->role == POTR_ROLE_SENDER)
    {
        sender_connect_loop(ctx, path_idx);
    }
    else
    {
        receiver_accept_loop(ctx, path_idx);
    }

    cplat_atomic_store_i32(&ctx->connect_thread_running[path_idx], 0, CPLAT_MEMORY_ORDER_RELEASE);

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "connect_thread[service_id=%" PRId64 " path=%d]: exited",
               ctx->service.service_id, path_idx);

    return;
}

/* 全 connect スレッドが使用を終えた受信者用準備資源を回収する。 */
static void dispose_connect_resources(potr_context *ctx)
{
    int i;

    for (i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        ctx->tcp_first_pkt_len[i] = 0;
        if (ctx->tcp_first_pkt_buf[i] != NULL)
        {
            cplat_secure_zero(ctx->tcp_first_pkt_buf[i], POTR_PACKET_HEADER_SIZE + ctx->global.max_payload);
            cplat_free(ctx->tcp_first_pkt_buf[i]);
            ctx->tcp_first_pkt_buf[i] = NULL;
        }
    }
    if (ctx->session_establish_mutex != NULL)
    {
        cplat_local_lock_dispose(ctx->session_establish_mutex);
        ctx->session_establish_mutex = NULL;
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_connect_thread_start(potr_context *ctx)
{
    int i;

    if (ctx == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (ctx->role == POTR_ROLE_RECEIVER)
    {
        if (cplat_local_lock_create(&ctx->session_establish_mutex) != CPLAT_OK)
        {
            /* 同期資源生成 API は詳細原因を提供しない。 */
            return POTR_ERR_UNKNOWN;
        }
        for (i = 0; i < ctx->n_path; i++)
        {
            ctx->tcp_first_pkt_len[i] = 0;
            ctx->tcp_first_pkt_buf[i] = (uint8_t *)cplat_malloc(POTR_PACKET_HEADER_SIZE + ctx->global.max_payload);
            if (ctx->tcp_first_pkt_buf[i] == NULL)
            {
                dispose_connect_resources(ctx);
                return POTR_ERR_OUT_OF_MEMORY;
            }
        }
    }
    for (i = 0; i < ctx->n_path; i++)
    {
        cplat_atomic_store_i32(&ctx->connect_thread_running[i], 1, CPLAT_MEMORY_ORDER_RELEASE);
        ctx->connect_args[i].ctx = ctx;
        ctx->connect_args[i].path_idx = i;
        if (cplat_thread_create(&ctx->connect_thread[i], connect_thread_func, &ctx->connect_args[i]) != CPLAT_OK)
        {
            cplat_atomic_store_i32(&ctx->connect_thread_running[i], 0, CPLAT_MEMORY_ORDER_RELEASE);
            /* FIN 手順を実行せず、起動成功分の終了待ち後に準備資源を回収する。 */
            potr_internal_connect_thread_stop(ctx);
            /* スレッド生成 API は詳細原因を提供しない。 */
            return POTR_ERR_UNKNOWN;
        }
    }
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_connect_thread_stop(potr_context *ctx)
{
    int i;
    int has_threads = 0;

    if (ctx == NULL)
    {
        return;
    }
    for (i = 0; i < ctx->n_path; i++)
    {
        if (ctx->connect_thread[i] != NULL)
        {
            has_threads = 1;
        }
        cplat_atomic_store_i32(&ctx->connect_thread_running[i], 0, CPLAT_MEMORY_ORDER_RELEASE);
    }
    if (has_threads)
    {
        cplat_local_lock_lock(ctx->tcp_state_mutex, CPLAT_SYNC_WAIT_FOREVER);
        cplat_condvar_broadcast(ctx->tcp_state_cv);
        cplat_local_lock_unlock(ctx->tcp_state_mutex);

        if (ctx->role == POTR_ROLE_RECEIVER)
        {
            for (i = 0; i < ctx->n_path; i++)
            {
                if (ctx->tcp_listen_sock[i] != CPLAT_INVALID_SOCKET)
                {
                    cplat_socket_shutdown(ctx->tcp_listen_sock[i]);
                    cplat_socket_close(ctx->tcp_listen_sock[i]);
                    ctx->tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
                }
            }
        }
        for (i = 0; i < ctx->n_path; i++)
        {
            close_tcp_conn(ctx, i);
        }
        for (i = 0; i < ctx->n_path; i++)
        {
            if (ctx->connect_thread[i] != NULL)
            {
                if (cplat_thread_join(ctx->connect_thread[i], CPLAT_SYNC_WAIT_FOREVER) != CPLAT_OK)
                {
                    return;
                }
                ctx->connect_thread[i] = NULL;
            }
        }
    }
    /* connect が生成した従属スレッドも、所有ハンドルが残っていれば回収する。 */
    for (i = 0; i < ctx->n_path; i++)
    {
        if (potr_internal_tcp_recv_thread_stop(ctx, i) != POTR_OK ||
            potr_internal_tcp_health_thread_stop(ctx, i) != POTR_OK)
        {
            return;
        }
    }
    potr_internal_send_thread_stop(ctx);
    if (ctx->send_thread != NULL)
    {
        return;
    }
    dispose_connect_resources(ctx);
}
