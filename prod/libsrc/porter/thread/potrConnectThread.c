/**
 *******************************************************************************
 *  @file           potrConnectThread.c
 *  @brief          TCP 接続管理スレッドモジュール。
 *  @author         Tetsuo Honda
 *  @date           2026/03/23
 *  @version        1.0.0
 *
 *  @details
 *  SENDER: TCP connect/reconnect ループを管理するスレッドです。\n
 *  RECEIVER: TCP accept ループを管理するスレッドです。\n
 *  接続確立後、送受信・ヘルスチェックスレッドを起動し、\n
 *  recv スレッドが切断を検知して終了するまで待機してから再接続サイクルへ移行します。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#if defined(PLATFORM_LINUX)
    #include <arpa/inet.h>
    #include <errno.h>
#endif /* PLATFORM_LINUX */

#include <porter_const.h>

#include "../potrContext.h"
#include "../potrPathEvent.h"
#include "../protocol/packet.h"
#include "../infra/potrSendQueue.h"
#include "../protocol/window.h"
#include "../infra/potrTrace.h"
#include "../infra/potrPlatform.h"
#include "potrConnectThread.h"
#include "potrConnectedThreads.h"
#include "potrRecvThread.h"
#include "potrSendThread.h"
#include "potrHealthThread.h"

/* connect/accept スレッドに渡す引数 */
typedef struct
{
    struct PotrContext_ *ctx;
    int                  path_idx;
    int                  _pad;
} ConnectArg;

/* 静的に確保した引数バッファ (path 数分) */
static ConnectArg s_connect_args[POTR_MAX_PATH];

static void sync_tcp_service_path_state_locked(struct PotrContext_ *ctx)
{
    int                    next_states[POTR_MAX_PATH];
    PotrPreparedPathEvents prepared;

    potr_copy_tcp_path_states(ctx, next_states);
    COM_UTIL_MUTEX_LOCK(&ctx->callback_mutex);
    potr_sync_service_path_state_locked(ctx, next_states, &prepared);
    potr_emit_service_path_events_locked(ctx, &prepared);
    COM_UTIL_MUTEX_UNLOCK(&ctx->callback_mutex);
}

static void set_tcp_path_ping_state(struct PotrContext_ *ctx,
                                    int                  path_idx,
                                    uint8_t              next_state)
{
    if (ctx->path_ping_state[path_idx] == next_state)
    {
        return;
    }

    ctx->path_ping_state[path_idx] = next_state;
    potr_tcp_health_thread_wake_all(ctx);
}

/* TCP 接続ソケット [path_idx] をシャットダウン・クローズして INVALID にする */
static void close_tcp_conn(struct PotrContext_ *ctx, int path_idx)
{
    if (ctx->tcp_conn_fd[path_idx] != POTR_INVALID_SOCKET)
    {
        potr_shutdown_socket(ctx->tcp_conn_fd[path_idx]);
        potr_close_socket(ctx->tcp_conn_fd[path_idx]);
        ctx->tcp_conn_fd[path_idx] = POTR_INVALID_SOCKET;
    }
}

/* 再接続待機: reconnect_interval_ms 経過または停止シグナルまでスリープする */
static void reconnect_wait(struct PotrContext_ *ctx, int path_idx, uint32_t wait_ms)
{
    COM_UTIL_MUTEX_LOCK(&ctx->tcp_state_mutex);
    if (ctx->connect_thread_running[path_idx])
    {
        com_util_condvar_timedwait(&ctx->tcp_state_cv, &ctx->tcp_state_mutex, wait_ms);
    }
    COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_state_mutex);
}

/* ================================================================
 * TCP セッション識別ヘルパー (accept スレッド専用)
 * ================================================================ */


/* accept 直後の TCP ソケットから 1 パケット分を buf に読み取る。
 * buf は PACKET_HEADER_SIZE + max_payload バイト以上確保されていること。
 * 戻り値: 1 = 成功 (*out_len にバイト数を格納)、0 = タイムアウト、-1 = EOF/エラー/不正。 */
static int tcp_read_first_packet(PotrSocket fd, uint8_t *buf, size_t max_buf,
                                  size_t *out_len, uint32_t timeout_ms)
{
    int      ready;
    uint16_t wire_payload_len;
    int      r;

    /* タイムアウト付き待機 */
    ready = potr_poll_readable(fd, (int)timeout_ms);
    if (ready == 0) return 0;  /* タイムアウト */
    if (ready < 0)  return -1; /* エラー */

    /* 固定長ヘッダー読み取り */
    r = potr_tcp_recv_all(fd, buf, PACKET_HEADER_SIZE);
    if (r <= 0) return -1;

    /* payload_len は固定長ヘッダー末尾の offset 34 に格納される */
    {
        uint16_t wpl;
        memcpy(&wpl, buf + 34, sizeof(wpl));
        wire_payload_len = ntohs(wpl);
    }

    /* ペイロード長バリデーション */
    if (PACKET_HEADER_SIZE + (size_t)wire_payload_len > max_buf) return -1;

    /* ペイロード読み取り */
    if (wire_payload_len > 0)
    {
        r = potr_tcp_recv_all(fd, buf + PACKET_HEADER_SIZE, (size_t)wire_payload_len);
        if (r <= 0) return -1;
    }

    *out_len = PACKET_HEADER_SIZE + (size_t)wire_payload_len;
    return 1;
}

/* session triplet 比較の戻り値 */
#define TCP_SESSION_NEW   ( 1)  /* 新セッション (または初回接続) */
#define TCP_SESSION_SAME  ( 0)  /* 同一セッション                 */
#define TCP_SESSION_OLD   (-1)  /* 旧セッション (破棄すべき)      */

/* ctx に記録されている相手セッションと pkt のセッション triplet を比較する。
 * peer_session_known == 0 の場合は TCP_SESSION_NEW を返す。
 * 呼び出し前提: session_establish_mutex を取得済みであること。 */
static int tcp_session_compare(const struct PotrContext_ *ctx,
                                const PotrPacket          *pkt)
{
    if (!ctx->peer_session_known) return TCP_SESSION_NEW;

    if      (pkt->session_tv_sec  > ctx->peer_session_tv_sec)  return TCP_SESSION_NEW;
    else if (pkt->session_tv_sec  < ctx->peer_session_tv_sec)  return TCP_SESSION_OLD;
    else if (pkt->session_tv_nsec > ctx->peer_session_tv_nsec) return TCP_SESSION_NEW;
    else if (pkt->session_tv_nsec < ctx->peer_session_tv_nsec) return TCP_SESSION_OLD;
    else if (pkt->session_id      > ctx->peer_session_id)      return TCP_SESSION_NEW;
    else if (pkt->session_id      < ctx->peer_session_id)      return TCP_SESSION_OLD;
    return TCP_SESSION_SAME;
}

/* recv スレッド [path_idx] の自然終了を待機してハンドルを解放する。
   TCP 接続断後 recv スレッドが自然終了する設計のため、ソケットはクローズしない。 */
static void join_recv_thread(struct PotrContext_ *ctx, int path_idx)
{
    com_util_thread_join(&ctx->recv_thread[path_idx]);
}

/* 接続確立前のコンテキスト状態をリセットする。
   フラグメントバッファや peer_session 状態をクリアする。
   TCP v2 マルチパスでは send_window 通番と health_alive は保持する
   (部分切断・再接続時のセッション継続のため)。
   呼び出しタイミング: start_connected_threads 前 (他スレッド未起動)。 */
static void reset_connection_state(struct PotrContext_ *ctx)
{
    ctx->peer_session_known = 0;
    ctx->frag_buf_len       = 0;
}

/* 全 path 切断時のリセット: send_window 通番・peer_session 状態・health_alive をクリアし
   DISCONNECTED イベントを発火する。
   呼び出しタイミング: tcp_active_paths が 0 になった直後 (tcp_state_mutex 非保護)。 */
static void reset_all_paths_disconnected(struct PotrContext_ *ctx)
{
    ctx->peer_session_known   = 0;
    ctx->frag_buf_len         = 0;
    ctx->send_window.next_seq = 0U;
    ctx->send_window.base_seq = 0U;
    ctx->send_has_data        = 0;
    if (ctx->send_window.valid != NULL)
    {
        memset(ctx->send_window.valid, 0,
               (size_t)ctx->send_window.window_size * sizeof(uint8_t));
    }
    memset((void *)ctx->remote_path_ping_state, 0, sizeof(ctx->remote_path_ping_state));
    memset((void *)ctx->path_logical_alive, 0, sizeof(ctx->path_logical_alive));
    ctx->health_alive = 0;
}

/* 送信キューを再初期化する (reconnect 時に shutdown 済みのキューをリセットする)。
   depth と max_payload はキュー構造体から取得する。 */
static void reset_send_queue(struct PotrContext_ *ctx)
{
    size_t   depth       = ctx->send_queue.depth;
    uint16_t max_payload = (uint16_t)ctx->global.max_payload;
    potr_send_queue_destroy(&ctx->send_queue);
    (void)potr_send_queue_init(&ctx->send_queue, depth, max_payload);
}

/* 接続確立後に依存スレッドを起動する (path ごと)。
   SENDER および TCP_BIDIR RECEIVER: path_idx==0 の初回のみ send スレッドを起動する。
   各 path で recv スレッドと health スレッドを起動 (全ロール共通)。
   失敗時は起動済みスレッドをすべて停止してから POTR_ERROR を返す。 */
static int start_connected_threads(struct PotrContext_ *ctx, int path_idx)
{
    const PotrConnectedThreadsOps ops =
    {
        potr_send_thread_start,
        potr_send_thread_stop,
        tcp_recv_thread_start,
        potr_tcp_health_thread_start,
        close_tcp_conn,
        join_recv_thread,
        set_tcp_path_ping_state
    };

    return potr_start_connected_threads(ctx, path_idx, &ops);
}

/* 接続断後に依存スレッドを停止する (path ごと)。
   呼び出し前提: join_recv_thread(path_idx) 完了済み (recv スレッドは終了している)。
   注意: 送信スレッドは共有のため、potr_connect_thread_stop で全 path join 後に停止する。 */
static void stop_connected_threads(struct PotrContext_ *ctx, int path_idx)
{
    /* health スレッドを先に停止 (PING 送信が tcp_conn_fd を参照するため) */
    potr_tcp_health_thread_stop(ctx, path_idx);

    /* 接続ソケットをクローズ */
    close_tcp_conn(ctx, path_idx);
}

/* SENDER: path_idx 番目の宛先へ TCP 接続を試みる。
   connect_timeout_ms が 0 の場合は OS デフォルト (ブロッキング) で接続する。
   成功時はソケットを返す。失敗時は POTR_INVALID_SOCKET を返す。 */
static PotrSocket tcp_connect_with_timeout(struct PotrContext_ *ctx, int path_idx)
{
    PotrSocket         sock;
    struct sockaddr_in addr;
    uint32_t           timeout_ms = ctx->service.connect_timeout_ms;
    int                reuse = 1;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == POTR_INVALID_SOCKET)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                 "connect_thread[service_id=%" PRId64 "]: socket() failed",
                 ctx->service.service_id);
        return POTR_INVALID_SOCKET;
    }

    potr_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (ctx->service.src_addr[path_idx][0] != '\0' || ctx->service.src_port != 0)
    {
        struct sockaddr_in bind_addr;
        memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_family = AF_INET;
        if (ctx->service.src_addr[path_idx][0] != '\0')
        {
            bind_addr.sin_addr = ctx->src_addr_resolved[path_idx];
        }
        else
        {
            bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        }
        bind_addr.sin_port = htons(ctx->service.src_port); /* 0 = エフェメラル */

        if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0)
        {
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                     "connect_thread[service_id=%" PRId64 "]: bind() failed",
                     ctx->service.service_id);
            potr_close_socket(sock);
            return POTR_INVALID_SOCKET;
        }
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr        = ctx->dst_addr_resolved[path_idx];
    addr.sin_port        = htons(ctx->service.dst_port);

    if (timeout_ms == 0U)
    {
        /* ブロッキング接続 */
        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
                     "connect_thread[service_id=%" PRId64 "]: connect() failed (blocking)",
                     ctx->service.service_id);
            potr_close_socket(sock);
            return POTR_INVALID_SOCKET;
        }
        return sock;
    }

    /* タイムアウト付き接続: ノンブロッキングモードで writable ポーリングを使う。
       停止シグナルに素早く応答するため 100ms 単位でポーリングする。 */
    potr_set_nonblocking(sock);
    {
        int ret_conn = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
        if (ret_conn == 0)
        {
            /* 即座に接続成功 */
            potr_set_blocking(sock);
            return sock;
        }
        /* EINPROGRESS / WSAEWOULDBLOCK 以外はエラー */
#if defined(PLATFORM_LINUX)
        if (errno != EINPROGRESS)
#elif defined(PLATFORM_WINDOWS)
        if (WSAGetLastError() != WSAEWOULDBLOCK)
#endif /* PLATFORM_ */
        {
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
                     "connect_thread[service_id=%" PRId64 "]: connect() failed",
                     ctx->service.service_id);
            potr_close_socket(sock);
            return POTR_INVALID_SOCKET;
        }
    }

    /* writable ポーリングループ (共通) */
    {
        uint32_t elapsed_ms = 0U;
        int      ready      = 0;

        while (elapsed_ms < timeout_ms && ctx->connect_thread_running[path_idx])
        {
            uint32_t poll_ms = timeout_ms - elapsed_ms;
            int      pr;
            if (poll_ms > 100U) poll_ms = 100U;
            pr = potr_poll_writable(sock, (int)poll_ms);
            if (pr < 0) break;
            if (pr > 0) { ready = 1; break; }
            elapsed_ms += poll_ms;
        }

        if (!ready)
        {
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
                     "connect_thread[service_id=%" PRId64 "]: connect() timed out",
                     ctx->service.service_id);
            potr_close_socket(sock);
            return POTR_INVALID_SOCKET;
        }
    }

    /* SO_ERROR で接続成否を確認 (errlen の型差分のみ #ifdef) */
    {
        int error = 0;
#if defined(PLATFORM_LINUX)
        socklen_t errlen = (socklen_t)sizeof(error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &errlen);
#elif defined(PLATFORM_WINDOWS)
        int errlen = (int)sizeof(error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&error, &errlen);
#endif /* PLATFORM_ */
        if (error != 0)
        {
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
                     "connect_thread[service_id=%" PRId64 "]: connect() SO_ERROR=%d",
                     ctx->service.service_id, error);
            potr_close_socket(sock);
            return POTR_INVALID_SOCKET;
        }
    }

    potr_set_blocking(sock);
    return sock;
}

/* SENDER 用接続ループ (path ごと) */
static void sender_connect_loop(struct PotrContext_ *ctx, int path_idx)
{
    int is_reconnect = 0; /* 初回接続フラグ */

    while (ctx->connect_thread_running[path_idx])
    {
        PotrSocket sock;
        int        active_count;

        POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
                 "connect_thread[service_id=%" PRId64 " path=%d]: connecting to %s:%u ...",
                 ctx->service.service_id, path_idx,
                 ctx->service.dst_addr[path_idx],
                 (unsigned)ctx->service.dst_port);

        sock = tcp_connect_with_timeout(ctx, path_idx);

        if (sock == POTR_INVALID_SOCKET)
        {
            if (!ctx->connect_thread_running[path_idx]) break;

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
                     ctx->service.service_id, path_idx,
                     (unsigned)ctx->service.reconnect_interval_ms);
            reconnect_wait(ctx, path_idx, ctx->service.reconnect_interval_ms);
            continue;
        }

        POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
                 "connect_thread[service_id=%" PRId64 " path=%d]: TCP connected",
                 ctx->service.service_id, path_idx);

        ctx->tcp_conn_fd[path_idx]           = sock;
        ctx->tcp_last_ping_recv_ms[path_idx] = com_util_get_monotonic_ms();

        /* tcp_active_paths カウンタをインクリメント (tcp_state_mutex 保護) */
        COM_UTIL_MUTEX_LOCK(&ctx->tcp_state_mutex);
        active_count = ++ctx->tcp_active_paths;
        COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_state_mutex);
        (void)active_count; /* CONNECTED イベントは recv スレッドが最初のパケット受信時に発火 */

        reset_connection_state(ctx);

        /* 再接続時 (path[0] のみ): 全 path 切断後の再起動ではキューをリセット */
        if (is_reconnect && path_idx == 0)
        {
            reset_send_queue(ctx);
        }

        if (start_connected_threads(ctx, path_idx) != POTR_SUCCESS)
        {
            /* スレッド起動失敗: カウンタを戻す */
            COM_UTIL_MUTEX_LOCK(&ctx->tcp_state_mutex);
            active_count = --ctx->tcp_active_paths;
            COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_state_mutex);
            if (active_count == 0)
            {
                reset_all_paths_disconnected(ctx);
            }

            if (!ctx->connect_thread_running[path_idx]) break;
            if (ctx->service.reconnect_interval_ms == 0U) break;

            reconnect_wait(ctx, path_idx, ctx->service.reconnect_interval_ms);
            is_reconnect = 1;
            continue;
        }

        /* recv スレッドが接続断を検知して自然終了するまで待機する */
        join_recv_thread(ctx, path_idx);

        POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
                 "connect_thread[service_id=%" PRId64 " path=%d]: TCP disconnected",
                 ctx->service.service_id, path_idx);

        stop_connected_threads(ctx, path_idx);

        COM_UTIL_MUTEX_LOCK(&ctx->tcp_state_mutex);
        set_tcp_path_ping_state(ctx, path_idx, POTR_PING_STATE_UNDEFINED);
        sync_tcp_service_path_state_locked(ctx);
        COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_state_mutex);

        /* tcp_active_paths カウンタをデクリメント (tcp_state_mutex 保護) */
        COM_UTIL_MUTEX_LOCK(&ctx->tcp_state_mutex);
        active_count = --ctx->tcp_active_paths;
        COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_state_mutex);

        if (active_count == 0)
        {
            /* 全 path 切断: send_window 通番と session をリセット */
            reset_all_paths_disconnected(ctx);
        }

        if (!ctx->connect_thread_running[path_idx]) break;
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
                 ctx->service.service_id, path_idx,
                 (unsigned)ctx->service.reconnect_interval_ms);
        reconnect_wait(ctx, path_idx, ctx->service.reconnect_interval_ms);
        is_reconnect = 1;
    }
}

/* RECEIVER 用 accept ループ (path ごと)
 *
 * [セッション層対称化]
 * accept() 直後に最初の 1 パケットを先読みし session_id を取得する。
 * session_establish_mutex 下で ctx の既知セッションと比較し、以下の 3 ケースを判別する。
 *   TCP_SESSION_NEW  : 新セッション (初回 or SENDER 再起動)
 *                      → 他 path の既存接続に切断シグナルを送ってから新規セッションを開始する。
 *   TCP_SESSION_SAME : 同一セッションの追加パス (マルチパス)
 *                      → reset_connection_state() を呼ばずにパスを追加する。
 *   TCP_SESSION_OLD  : 旧セッション (再送や遅延パケット等)
 *                      → コネクションを閉じてループ先頭へ戻る。
 * 先読みパケットは tcp_first_pkt_buf/len に保存し、recv スレッドが起動直後に処理する。 */
static void receiver_accept_loop(struct PotrContext_ *ctx, int path_idx)
{
    int is_bidir     = (ctx->service.type == POTR_TYPE_TCP_BIDIR);
    int is_reconnect = 0;

    /* 先読みタイムアウト: TCP ヘルスタイムアウトの 3 倍、未設定時は 30 秒 */
    uint32_t first_pkt_timeout_ms = (ctx->global.tcp_health_timeout_ms > 0U)
                                    ? ctx->global.tcp_health_timeout_ms * 3U
                                    : 30000U;

    while (ctx->connect_thread_running[path_idx])
    {
        PotrSocket         conn;
        struct sockaddr_in peer_addr;
        socklen_t          peer_len = (socklen_t)sizeof(peer_addr);
        int                active_count;
        char               peer_addr_str[INET_ADDRSTRLEN];
        int                session_result;

        conn = accept(ctx->tcp_listen_sock[path_idx],
                      (struct sockaddr *)&peer_addr, &peer_len);

        if (conn == POTR_INVALID_SOCKET)
        {
            if (!ctx->connect_thread_running[path_idx]) break;
            /* 一時的なエラー: ループ継続 */
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
                     "connect_thread[service_id=%" PRId64 " path=%d]: accept() error, retrying",
                     ctx->service.service_id, path_idx);
            continue;
        }

        inet_ntop(AF_INET, &peer_addr.sin_addr, peer_addr_str, sizeof(peer_addr_str));

        /* 接続元フィルタ: src_addr[path_idx] / src_port が指定されていれば一致確認 */
        {
            int filtered = 0;
            if (ctx->service.src_addr[path_idx][0] != '\0')
            {
                if (peer_addr.sin_addr.s_addr !=
                    ctx->src_addr_resolved[path_idx].s_addr)
                {
                    filtered = 1;
                }
            }
            if (!filtered && ctx->service.src_port != 0)
            {
                if (ntohs(peer_addr.sin_port) != ctx->service.src_port)
                {
                    filtered = 1;
                }
            }
            if (filtered)
            {
                POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
                         "connect_thread[service_id=%" PRId64 " path=%d]: rejected connection"
                         " from %s:%u (src filter)",
                         ctx->service.service_id, path_idx,
                         peer_addr_str,
                         (unsigned)ntohs(peer_addr.sin_port));
                potr_close_socket(conn);
                continue;
            }
        }

        POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
                 "connect_thread[service_id=%" PRId64 " path=%d]: TCP accepted from %s:%u",
                 ctx->service.service_id, path_idx,
                 peer_addr_str,
                 (unsigned)ntohs(peer_addr.sin_port));

        /* ── セッション判定: 最初の 1 パケットを先読みして session_id を取得する ── */
        {
            PotrPacket pkt;
            size_t     pkt_len = 0;
            int        r;

            r = tcp_read_first_packet(conn,
                                      ctx->tcp_first_pkt_buf[path_idx],
                                      PACKET_HEADER_SIZE + ctx->global.max_payload,
                                      &pkt_len,
                                      first_pkt_timeout_ms);
            if (r <= 0)
            {
                /* タイムアウトまたは EOF/エラー */
                POTR_TRACE(COM_UTIL_TRACE_LEVEL_WARNING,
                         "connect_thread[service_id=%" PRId64 " path=%d]: "
                         "first packet read failed (r=%d), closing",
                         ctx->service.service_id, path_idx, r);
                potr_close_socket(conn);
                continue;
            }

            if (packet_parse(&pkt, ctx->tcp_first_pkt_buf[path_idx], pkt_len)
                != POTR_SUCCESS)
            {
                POTR_TRACE(COM_UTIL_TRACE_LEVEL_WARNING,
                         "connect_thread[service_id=%" PRId64 " path=%d]: "
                         "first packet parse failed, closing",
                         ctx->service.service_id, path_idx);
                potr_close_socket(conn);
                continue;
            }

            /* session_establish_mutex 下でセッション判定と状態更新を行う */
            COM_UTIL_MUTEX_LOCK(&ctx->session_establish_mutex);

            session_result = tcp_session_compare(ctx, &pkt);

            if (session_result == TCP_SESSION_OLD)
            {
                /* 旧セッション: 拒否 */
                COM_UTIL_MUTEX_UNLOCK(&ctx->session_establish_mutex);
                POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
                         "connect_thread[service_id=%" PRId64 " path=%d]: "
                         "old session rejected (known_id=%u pkt_id=%u)",
                         ctx->service.service_id, path_idx,
                         ctx->peer_session_id, pkt.session_id);
                potr_close_socket(conn);
                continue;
            }

            if (session_result == TCP_SESSION_NEW)
            {
                /* 新セッション: 他 path の既存接続に切断シグナルを送る。
                 * cleanup は各 path の accept スレッドが自然に行う。 */
                int k;
                for (k = 0; k < ctx->n_path; k++)
                {
                    if (k == path_idx) continue;
                    if (ctx->tcp_conn_fd[k] != POTR_INVALID_SOCKET)
                    {
                        ctx->running[k] = 0;
                        close_tcp_conn(ctx, k); /* recv ブロックを解除 */
                    }
                }
                reset_connection_state(ctx); /* peer_session_known = 0, frag_buf_len = 0 */
            }
            /* TCP_SESSION_SAME の場合は reset 不要 (セッション継続) */

            ctx->tcp_conn_fd[path_idx]           = conn;
            ctx->tcp_last_ping_recv_ms[path_idx] = com_util_get_monotonic_ms();
            ctx->tcp_first_pkt_len[path_idx]         = pkt_len; /* 先読みバッファ有効化 */

            COM_UTIL_MUTEX_UNLOCK(&ctx->session_establish_mutex);
        }
        /* ── セッション判定ここまで ── */

        /* tcp_active_paths カウンタをインクリメント (tcp_state_mutex 保護) */
        COM_UTIL_MUTEX_LOCK(&ctx->tcp_state_mutex);
        active_count = ++ctx->tcp_active_paths;
        COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_state_mutex);
        (void)active_count; /* CONNECTED イベントは recv スレッドが最初のパケット受信時に発火 */

        /* TCP_BIDIR 新セッション再接続時 (path[0] のみ): shutdown 済みのキューをリセット */
        if (is_bidir && session_result == TCP_SESSION_NEW && is_reconnect && path_idx == 0)
        {
            reset_send_queue(ctx);
        }

        if (start_connected_threads(ctx, path_idx) != POTR_SUCCESS)
        {
            COM_UTIL_MUTEX_LOCK(&ctx->tcp_state_mutex);
            active_count = --ctx->tcp_active_paths;
            COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_state_mutex);
            if (active_count == 0)
            {
                reset_all_paths_disconnected(ctx);
            }
            ctx->tcp_first_pkt_len[path_idx] = 0; /* 先読みバッファを無効化 */
            close_tcp_conn(ctx, path_idx);
            is_reconnect = 1;
            continue;
        }

        /* recv スレッドが接続断を検知して自然終了するまで待機する */
        join_recv_thread(ctx, path_idx);

        POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
                 "connect_thread[service_id=%" PRId64 " path=%d]: TCP connection closed",
                 ctx->service.service_id, path_idx);

        stop_connected_threads(ctx, path_idx);

        COM_UTIL_MUTEX_LOCK(&ctx->tcp_state_mutex);
        set_tcp_path_ping_state(ctx, path_idx, POTR_PING_STATE_UNDEFINED);
        sync_tcp_service_path_state_locked(ctx);
        COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_state_mutex);

        /* 先読みバッファをクリア (recv スレッドが未処理のまま終了した場合の安全策) */
        ctx->tcp_first_pkt_len[path_idx] = 0;

        /* tcp_active_paths カウンタをデクリメント (tcp_state_mutex 保護) */
        COM_UTIL_MUTEX_LOCK(&ctx->tcp_state_mutex);
        active_count = --ctx->tcp_active_paths;
        COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_state_mutex);

        if (active_count == 0)
        {
            reset_all_paths_disconnected(ctx);
        }

        is_reconnect = 1;
        /* ループ継続: 次の accept へ */
    }
}

/* 接続管理スレッド本体 (ConnectArg* を受け取り、path ごとに動作) */
COM_UTIL_THREAD_FUNC(connect_thread_func)
{
    ConnectArg          *carg     = (ConnectArg *)arg;
    struct PotrContext_ *ctx      = carg->ctx;
    int                  path_idx = carg->path_idx;

    POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
             "connect_thread[service_id=%" PRId64 " path=%d]: started (role=%s type=%s)",
             ctx->service.service_id, path_idx,
             (ctx->role == POTR_ROLE_SENDER) ? "SENDER" : "RECEIVER",
             (ctx->service.type == POTR_TYPE_TCP_BIDIR) ? "TCP_BIDIR" : "TCP");

    if (ctx->role == POTR_ROLE_SENDER)
    {
        sender_connect_loop(ctx, path_idx);
    }
    else
    {
        receiver_accept_loop(ctx, path_idx);
    }

    ctx->connect_thread_running[path_idx] = 0;

    POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
             "connect_thread[service_id=%" PRId64 " path=%d]: exited",
             ctx->service.service_id, path_idx);

    COM_UTIL_THREAD_RETURN;
}

/**
 *******************************************************************************
 *  @brief          TCP 接続管理スレッドを起動します (path 数分)。
 *  @param[in,out]  ctx  セッションコンテキストへのポインタ。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *
 *  @details
 *  呼び出し前提:
 *  - SENDER: dst_addr_resolved[i] および dst_port が設定済みであること (n_path 分)。
 *  - RECEIVER: tcp_listen_sock[i] が listen 状態であること (n_path 分)。
 *  - tcp_state_mutex / tcp_state_cv / tcp_send_mutex[] が初期化済みであること。
 *******************************************************************************
 */
int potr_connect_thread_start(struct PotrContext_ *ctx)
{
    int i;

    if (ctx == NULL)
    {
        return POTR_ERROR;
    }

    POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
             "connect_thread[service_id=%" PRId64 "]: starting %d path(s)",
             ctx->service.service_id, ctx->n_path);

    /* RECEIVER: session_establish_mutex と先読みバッファを初期化する */
    if (ctx->role == POTR_ROLE_RECEIVER)
    {
        COM_UTIL_MUTEX_INIT(&ctx->session_establish_mutex);

        for (i = 0; i < ctx->n_path; i++)
        {
            ctx->tcp_first_pkt_len[i] = 0;
            ctx->tcp_first_pkt_buf[i] = (uint8_t *)malloc(
                PACKET_HEADER_SIZE + ctx->global.max_payload);
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
                    free(ctx->tcp_first_pkt_buf[j]);
                    ctx->tcp_first_pkt_buf[j] = NULL;
                }
                COM_UTIL_MUTEX_DESTROY(&ctx->session_establish_mutex);
                return POTR_ERROR;
            }
        }
    }

    for (i = 0; i < ctx->n_path; i++)
    {
        ctx->connect_thread_running[i] = 1;
        s_connect_args[i].ctx      = ctx;
        s_connect_args[i].path_idx = i;

        if (com_util_thread_create(&ctx->connect_thread[i],
                               connect_thread_func, &s_connect_args[i]) != 0)
        {
            ctx->connect_thread_running[i] = 0;
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                     "connect_thread[service_id=%" PRId64 " path=%d]: thread create failed",
                     ctx->service.service_id, i);
            return POTR_ERROR;
        }
    }

    return POTR_SUCCESS;
}

/**
 *******************************************************************************
 *  @brief          TCP 接続管理スレッドを停止します。
 *  @param[in,out]  ctx  セッションコンテキストへのポインタ。
 *
 *  @details
 *  1. connect_thread_running[i] フラグを全 path クリアして停止を通知する。
 *  2. tcp_state_cv をブロードキャストして reconnect sleep を中断する。
 *  3. tcp_listen_sock[i] (RECEIVER) を全 path クローズして accept ブロックを解除する。
 *  4. tcp_conn_fd[i] を全 path クローズして recv ループをブロック解除する。
 *  5. connect_thread[i] の終了を全 path 待機する。
 *  6. 送信スレッドを停止する (全 path join 後)。
 *  依存スレッド (recv/health) のクリーンアップは各 connect_thread 内で行われる。
 *******************************************************************************
 */
void potr_connect_thread_stop(struct PotrContext_ *ctx)
{
    int i;
    int any_running = 0;

    if (ctx == NULL) { return; }

    for (i = 0; i < ctx->n_path; i++)
    {
        if (ctx->connect_thread_running[i]) { any_running = 1; break; }
    }
    if (!any_running) { return; }

    /* 1. 全 path の停止フラグをクリア */
    for (i = 0; i < ctx->n_path; i++)
    {
        ctx->connect_thread_running[i] = 0;
    }

    /* 2. reconnect_wait 中の全スレッドを起床させる */
    COM_UTIL_MUTEX_LOCK(&ctx->tcp_state_mutex);
    COM_UTIL_COND_BROADCAST(&ctx->tcp_state_cv);
    COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_state_mutex);

    /* 3. RECEIVER: 全 path の listen ソケットをクローズして accept をアンブロック */
    if (ctx->role == POTR_ROLE_RECEIVER)
    {
        for (i = 0; i < ctx->n_path; i++)
        {
            if (ctx->tcp_listen_sock[i] == POTR_INVALID_SOCKET) continue;
            potr_shutdown_socket(ctx->tcp_listen_sock[i]);
            potr_close_socket(ctx->tcp_listen_sock[i]);
            ctx->tcp_listen_sock[i] = POTR_INVALID_SOCKET;
        }
    }

    /* 4. 全 path の接続ソケットをクローズして recv ループをアンブロック */
    for (i = 0; i < ctx->n_path; i++)
    {
        if (ctx->tcp_conn_fd[i] == POTR_INVALID_SOCKET) continue;
        potr_shutdown_socket(ctx->tcp_conn_fd[i]);
        potr_close_socket(ctx->tcp_conn_fd[i]);
        ctx->tcp_conn_fd[i] = POTR_INVALID_SOCKET;
    }

    /* 5. 全 connect スレッドの終了を待機する */
    for (i = 0; i < ctx->n_path; i++)
    {
        com_util_thread_join(&ctx->connect_thread[i]);
    }

    /* 6. 送信スレッドを停止する (全 path join 後) */
    potr_send_thread_stop(ctx);

    /* 7. RECEIVER: session_establish_mutex と先読みバッファを破棄する */
    if (ctx->role == POTR_ROLE_RECEIVER)
    {
        for (i = 0; i < ctx->n_path; i++)
        {
            ctx->tcp_first_pkt_len[i] = 0;
            if (ctx->tcp_first_pkt_buf[i] != NULL)
            {
                free(ctx->tcp_first_pkt_buf[i]);
                ctx->tcp_first_pkt_buf[i] = NULL;
            }
        }
        COM_UTIL_MUTEX_DESTROY(&ctx->session_establish_mutex);
    }

    POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
             "connect_thread[service_id=%" PRId64 "]: all paths stopped",
             ctx->service.service_id);
}
