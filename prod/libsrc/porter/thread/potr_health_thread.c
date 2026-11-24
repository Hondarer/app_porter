/**
 *******************************************************************************
 *  @file           potr_health_thread.c
 *  @brief          ヘルス チェックを行うスレッドを実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/08
 *  @version        1.0.0
 *
 *  非 TCP 通信種別で動作する定周期 PING 送信スレッドです。\n
 *  health_interval_ms 周期で PING パケットを送信します。\n
 *  一方向通信では送信のみ、双方向通信では request/reply 形式の PING 送信を担います。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/base/platform.h>
#include <porter/infra/potr_send_queue.h>
#include <string.h>
#include <inttypes.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/porter_spec.h>

#include <porter/protocol/packet.h>
#include <porter/protocol/packet_seal.h>
#include <porter/protocol/window.h>
#include <porter/potr_context.h>
#include <porter/potr_peer_table.h>
#include <porter/thread/potr_health_thread.h>
#include <porter/infra/potr_trace.h>
#include <porter/infra/potr_result.h>
#include <porter/infra/potr_socket_io.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/socket.h>

#include "thread_recv_session.h"

/*
 * PING 送信時刻と有効データ送信時刻のうち、新しい方の時刻を取得する。
 * 戻り値は最終送信時刻 [ms]。
 */
static uint64_t get_last_health_signal_send_ms(const potr_context *ctx, int path_idx)
{
    uint64_t last_ping = cplat_atomic_load_u64(&ctx->last_ping_send_ms[path_idx], CPLAT_MEMORY_ORDER_RELAXED);
    uint64_t last_data = cplat_atomic_load_u64(&ctx->last_valid_data_send_ms[path_idx], CPLAT_MEMORY_ORDER_RELAXED);

    if (last_ping > last_data)
    {
        return last_ping;
    }

    return last_data;
}

/* 指定パスのヘルス スレッドへ送信要求を記録し、起床シグナルを送信する。 */
static void signal_health_thread(potr_context *ctx, int path_idx)
{
    if (ctx == NULL || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH || ctx->health_mutex[path_idx] == NULL ||
        ctx->health_wakeup[path_idx] == NULL)
    {
        return;
    }

    cplat_local_lock_lock(ctx->health_mutex[path_idx]);

    if (cplat_atomic_load_i32(&ctx->health_running[path_idx], CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
    {
        ctx->health_send_pending[path_idx] = 1;
        cplat_condvar_signal(ctx->health_wakeup[path_idx]);
    }

    cplat_local_lock_unlock(ctx->health_mutex[path_idx]);
}

/*
 * 次の PING 送信まで待機する (path_idx 版)。
 * 同じロックの下で停止、保留要求 (health_send_pending)、絶対期限を繰り返し確認する。
 * 停止は保留要求より優先する。要求がある場合は 0 へ消費してからロックを解放し、1 を返す。
 * 周期期限切れでも同じロックの下で保留要求を確認・消費してから 1 を返す。
 * 見せかけの起床では絶対期限を延長しない。
 * 戻り値: 1 = 送信へ進む、0 = 停止または致命的失敗。
 */
static int health_wait_next_send(potr_context *ctx, int path_idx, uint32_t interval_ms)
{
    int has_deadline = 0;
    uint64_t deadline_ms = 0U;

    cplat_local_lock_lock(ctx->health_mutex[path_idx]);

    for (;;)
    {
        uint64_t now_ms;
        uint64_t remaining;
        int wait_ms;
        int wait_result;

        if (cplat_atomic_load_i32(&ctx->health_running[path_idx], CPLAT_MEMORY_ORDER_ACQUIRE) == 0 ||
            cplat_atomic_load_i32(&ctx->fatal_error, CPLAT_MEMORY_ORDER_ACQUIRE) != POTR_OK)
        {
            cplat_local_lock_unlock(ctx->health_mutex[path_idx]);
            return 0;
        }

        if (ctx->health_send_pending[path_idx] != 0)
        {
            ctx->health_send_pending[path_idx] = 0;
            cplat_local_lock_unlock(ctx->health_mutex[path_idx]);
            return 1;
        }

        if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
        {
            cplat_local_lock_unlock(ctx->health_mutex[path_idx]);
            cplat_atomic_store_i32(&ctx->fatal_error, POTR_ERR_IO, CPLAT_MEMORY_ORDER_RELEASE);
            potr_internal_send_queue_abort(&ctx->send_queue, POTR_ERR_IO);
            return 0;
        }

        if (!has_deadline)
        {
            deadline_ms = now_ms + (uint64_t)interval_ms;
            has_deadline = 1;
        }

        if (now_ms >= deadline_ms)
        {
            /* 周期の期限切れ。同じロックの下で保留要求が 0 であることを確認済みのため送信へ進む。 */
            cplat_local_lock_unlock(ctx->health_mutex[path_idx]);
            return 1;
        }

        remaining = deadline_ms - now_ms;
        if (remaining > (uint64_t)POTR_WAIT_MS_MAX)
        {
            remaining = (uint64_t)POTR_WAIT_MS_MAX;
        }
        wait_ms = (int)remaining;

        wait_result = cplat_condvar_timed_wait(ctx->health_wakeup[path_idx], ctx->health_mutex[path_idx], wait_ms);
        if (wait_result != CPLAT_OK && wait_result != CPLAT_ERR_TIMEOUT)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "health[service_id=%" PRId64 " path=%d]: wait failed (result=%d)",
                       ctx->service.service_id, path_idx, wait_result);
        }
    }
}

/*
 * 次回 PING 送信時刻まで待機する。PING を送信すべきなら 1、health_running 停止なら 0 を返す。
 * 待機結果の判定 (真偽値) を返す述語のため共通結果コードの適用対象外である。
 */
static int wait_oneway_udp_ping_due(potr_context *ctx, uint64_t initial_ping_due_ms, uint64_t *last_logged_data_ms)
{
    while (cplat_atomic_load_i32(&ctx->health_running[0], CPLAT_MEMORY_ORDER_ACQUIRE) != 0 &&
           cplat_atomic_load_i32(&ctx->fatal_error, CPLAT_MEMORY_ORDER_ACQUIRE) == POTR_OK)
    {
        uint64_t now;
        uint64_t due_ms = UINT64_MAX;
        int i;

        if (cplat_clock_get_monotonic_ms(&now) != CPLAT_OK)
        {
            cplat_atomic_store_i32(&ctx->fatal_error, POTR_ERR_IO, CPLAT_MEMORY_ORDER_RELEASE);
            potr_internal_send_queue_abort(&ctx->send_queue, POTR_ERR_IO);
            return 0;
        }
        for (i = 0; i < ctx->n_path; i++)
        {
            uint64_t last_signal = get_last_health_signal_send_ms(ctx, i);
            uint64_t path_due = initial_ping_due_ms;

            if (last_signal != 0U)
            {
                path_due = last_signal + ctx->health_interval_ms;
            }
            if (path_due < due_ms)
            {
                due_ms = path_due;
            }
        }
        if (now >= due_ms)
        {
            return 1;
        }
        for (i = 0; i < ctx->n_path; i++)
        {
            uint64_t last_ping = cplat_atomic_load_u64(&ctx->last_ping_send_ms[i], CPLAT_MEMORY_ORDER_RELAXED);
            uint64_t last_data = cplat_atomic_load_u64(&ctx->last_valid_data_send_ms[i], CPLAT_MEMORY_ORDER_RELAXED);

            if (last_data != 0U && last_data >= last_ping && last_data != last_logged_data_ms[i])
            {
                uint64_t path_due = last_data + ctx->health_interval_ms;

                if (path_due > now)
                {
                    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                               "health[service_id=%" PRId64 "]: suppress PING due to recent DATA"
                               " (remaining=%" PRIu64 "ms)",
                               ctx->service.service_id, path_due - now);
                }
                last_logged_data_ms[i] = last_data;
            }
        }
        if (!health_wait_next_send(ctx, 0, (uint32_t)(due_ms - now)))
        {
            return 0;
        }
    }
    return 0;
}

/*
 * TCP 接続の指定パスへ、現在の経路状態を載せた PING パケットを書き込む。
 * 呼び出し元は send_window_mutex と tcp_send_mutex[path_idx] をこの順に保持する。
 * 経路状態の採取と書き込みを同じクリティカル セクションで行い、同一 path で
 * 古い経路状態を後から届ける順序逆転を防ぐ。順序が崩れると、相手端の remote_path_ping_state が
 * NORMAL から UNDEFINED へ後退し、瞬間的に DISCONNECTED と判定される。
 * force が 0 の場合は、この接続で通知済みの経路状態が現在と同じであれば書き込まない。
 * 書き込みに成功した経路状態は通知済みとして記録する。
 * 成功時は POTR_OK、失敗時はエラー コードを返す。
 */
static int tcp_write_ping_locked(potr_context *ctx, int path_idx, int force)
{
    potr_packet ping_pkt;
    potr_internal_packet_session_hdr shdr;
    uint8_t health_states[POTR_MAX_PATH];
    uint8_t wire_buf[POTR_PACKET_HEADER_SIZE + POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE];
    size_t wire_len;
    cplat_error detail;
    int build_result;
    int send_result;

    if (ctx->tcp_conn_fd[path_idx] == CPLAT_INVALID_SOCKET)
    {
        return POTR_ERR_DISCONNECTED;
    }

    copy_path_ping_state(health_states, ctx->path_ping_state, POTR_MAX_PATH);

    if (!force && ctx->tcp_ping_announced[path_idx] != 0 &&
        memcmp(health_states, ctx->tcp_announced_ping_state[path_idx], POTR_MAX_PATH) == 0)
    {
        return POTR_OK;
    }

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = ctx->session_id;
    session_ts_to_hdr(&ctx->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);
    build_result = potr_internal_packet_build_ping(&ping_pkt, &shdr, ctx->send_window.next_seq, health_states,
                                                   (uint16_t)POTR_MAX_PATH);
    if (build_result != POTR_OK)
    {
        return build_result;
    }

    wire_len = sizeof(wire_buf);
    build_result = potr_internal_packet_seal_for_context(ctx, &ctx->tx_session, &ping_pkt, wire_buf, &wire_len);
    if (build_result != POTR_OK)
    {
        return build_result;
    }

    send_result = potr_internal_tcp_send_all_locked(ctx, path_idx, wire_buf, wire_len, NULL, &detail);
    if (send_result != POTR_OK)
    {
        return send_result;
    }

    memcpy(ctx->tcp_announced_ping_state[path_idx], health_states, POTR_MAX_PATH);
    ctx->tcp_ping_announced[path_idx] = 1;

    return POTR_OK;
}

/*
 * TCP 接続の指定パスへ PING パケットを送信する。
 * 成功時は POTR_OK、失敗時はエラー コードを返す。
 */
static int tcp_send_ping_packet(potr_context *ctx, int path_idx)
{
    int send_result;

    if (ctx == NULL || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (cplat_atomic_load_i32(&ctx->close_requested, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
    {
        return POTR_ERR_CANCELED;
    }

    if (cplat_atomic_load_i32(&ctx->tcp_active_paths, CPLAT_MEMORY_ORDER_ACQUIRE) == 0 ||
        ctx->tcp_conn_fd[path_idx] == CPLAT_INVALID_SOCKET)
    {
        return POTR_ERR_DISCONNECTED;
    }

    cplat_local_lock_lock(ctx->send_window_mutex);
    cplat_local_lock_lock(ctx->tcp_send_mutex[path_idx]);
    send_result = tcp_write_ping_locked(ctx, path_idx, 1);
    cplat_local_lock_unlock(ctx->tcp_send_mutex[path_idx]);
    cplat_local_lock_unlock(ctx->send_window_mutex);

    return send_result;
}

/*
 * ====================================================================
 * 非 TCP (UDP/マルチキャスト) 用ヘルス チェック スレッド本体
 * ====================================================================
 */
static void health_thread_func(void *arg)
{
    potr_context *ctx = (potr_context *)arg;
    potr_internal_packet_session_hdr shdr;
    int is_oneway_udp = is_oneway_udp_type(ctx->service.type);
    uint64_t initial_ping_due_ms;
    uint64_t last_logged_data_ms[POTR_MAX_PATH] = {0};

    if (cplat_clock_get_monotonic_ms(&initial_ping_due_ms) != CPLAT_OK)
    {
        cplat_atomic_store_i32(&ctx->fatal_error, POTR_ERR_IO, CPLAT_MEMORY_ORDER_RELEASE);
        potr_internal_send_queue_abort(&ctx->send_queue, POTR_ERR_IO);
        cplat_atomic_store_i32(&ctx->health_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
        return;
    }

    initial_ping_due_ms += (uint64_t)ctx->health_interval_ms;

    shdr.service_id = ctx->service.service_id;

    /* 1:1 モード: session はコンテキストから取得する (N:1 は後述のループで各ピアから取得)。 */
    if (!ctx->is_multi_peer)
    {
        shdr.session_id = ctx->session_id;
        session_ts_to_hdr(&ctx->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);
    }

    while (cplat_atomic_load_i32(&ctx->health_running[0], CPLAT_MEMORY_ORDER_ACQUIRE) != 0 &&
           cplat_atomic_load_i32(&ctx->fatal_error, CPLAT_MEMORY_ORDER_ACQUIRE) == POTR_OK)
    {
        if (is_oneway_udp)
        {
            if (!wait_oneway_udp_ping_due(ctx, initial_ping_due_ms, last_logged_data_ms))
            {
                break;
            }
        }
        else
        {
            if (!health_wait_next_send(ctx, 0, ctx->health_interval_ms))
            {
                break;
            }
        }

        /* PING を送信する。 */
        if (ctx->is_multi_peer)
        {
            /* N:1 モード: アクティブ全ピアへ PING を送信する。 */
            int i;

            cplat_local_lock_lock(ctx->peers_mutex);

            for (i = 0;
                 i < ctx->max_peers && cplat_atomic_load_i32(&ctx->health_running[0], CPLAT_MEMORY_ORDER_ACQUIRE) != 0;
                 i++)
            {
                potr_packet ping_pkt;
                potr_internal_packet_session_hdr peer_shdr;
                uint32_t seq;
                size_t wire_len;
                uint8_t health_states[POTR_MAX_PATH];
                int k;

                if (!ctx->peers[i].active)
                {
                    continue;
                }

                peer_shdr.service_id = ctx->service.service_id;
                peer_shdr.session_id = ctx->peers[i].session_id;
                session_ts_to_hdr(&ctx->peers[i].session_ts, &peer_shdr.session_tv_sec, &peer_shdr.session_tv_nsec);

                cplat_local_lock_lock(ctx->peers[i].send_window_mutex);
                seq = ctx->peers[i].send_window.next_seq;

                /* N:1 (UNICAST_BIDIR_N1) は双方向 PING。ピアごとの自端パス受信状態をペイロードに設定する。 */
                copy_path_ping_state(health_states, ctx->peers[i].path_ping_state, POTR_MAX_PATH);
                /*
                 * パケットとヘッダーはどちらもローカル変数のアドレスで、NULL の場合だけ失敗する契約のため、
                 * 戻り値は明示的に破棄する。
                 */
                (void)potr_internal_packet_build_ping(&ping_pkt, &peer_shdr, seq, health_states,
                                                      (uint16_t)POTR_MAX_PATH);
                cplat_local_lock_unlock(ctx->peers[i].send_window_mutex);

                for (k = 0; k < (int)POTR_MAX_PATH; k++)
                {
                    uint8_t wire_buf[POTR_PACKET_HEADER_SIZE + POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE];

                    if (endpoint_is_unset(&ctx->peers[i].dest_addr[k]))
                    {
                        continue;
                    }
                    wire_len = sizeof(wire_buf);
                    if (potr_internal_packet_seal_for_context(ctx, &ctx->peers[i].tx_session, &ping_pkt,
                                                               wire_buf, &wire_len) != POTR_OK)
                    {
                        break;
                    }
                    /*
                     * PING は次の周期に送り直し、届かない状態が続く経路は相手のヘルス チェックのタイムアウトで検出するため、
                     * 戻り値は明示的に破棄する。
                     */
                    (void)potr_internal_udp_sendto(ctx, k, wire_buf, wire_len, &ctx->peers[i].dest_addr[k], NULL);
                }

                POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "health[service_id=%" PRId64 "]: PING peer=%u seq=%u",
                           ctx->service.service_id, (unsigned)ctx->peers[i].peer_id, (unsigned)seq);
                if (cplat_atomic_load_i32(&ctx->peers[i].tx_session.exhausted, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
                {
                    thread_recv_disconnect_peer_all_paths(ctx, &ctx->peers[i]);
                    potr_internal_peer_free(ctx, &ctx->peers[i]);
                }
            }

            cplat_local_lock_unlock(ctx->peers_mutex);
        }
        else
        {
            /* 1:1 UDP/マルチキャスト: 単一宛先へ PING を送信する。 */
            potr_packet ping_pkt;
            uint32_t seq;
            size_t wire_len;
            int build_result;
            int sent_any = 0;
            uint8_t health_states[POTR_MAX_PATH];
            int k;

            /*
             * 双方向 PING (UNICAST_BIDIR) のみ自端のパス受信状態を設定する。
             * それ以外 (UNICAST/MULTICAST/BROADCAST/RAW 系) は PING を受信しないため UNDEFINED を設定する。
             */
            if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
            {
                copy_path_ping_state(health_states, ctx->path_ping_state, POTR_MAX_PATH);
            }
            else
            {
                memset(health_states, POTR_PING_STATE_UNDEFINED, POTR_MAX_PATH);
            }

            cplat_local_lock_lock(ctx->send_window_mutex);
            seq = ctx->send_window.next_seq;
            build_result =
                potr_internal_packet_build_ping(&ping_pkt, &shdr, seq, health_states, (uint16_t)POTR_MAX_PATH);
            cplat_local_lock_unlock(ctx->send_window_mutex);

            if (build_result != POTR_OK)
            {
                continue;
            }

            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "health[service_id=%" PRId64 "]: PING seq=%u",
                       ctx->service.service_id, (unsigned)seq);

            for (k = 0; k < ctx->n_path; k++)
            {
                uint8_t wire_buf[POTR_PACKET_HEADER_SIZE + POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE];
                int send_ret;

                uint64_t ping_time = 0U;

                if (is_oneway_udp)
                {
                    uint64_t last_signal = get_last_health_signal_send_ms(ctx, k);

                    if (cplat_clock_get_monotonic_ms(&ping_time) != CPLAT_OK)
                    {
                        cplat_atomic_store_i32(&ctx->fatal_error, POTR_ERR_IO, CPLAT_MEMORY_ORDER_RELEASE);
                        potr_internal_send_queue_abort(&ctx->send_queue, POTR_ERR_IO);
                        break;
                    }
                    if ((last_signal == 0U && ping_time < initial_ping_due_ms) ||
                        (last_signal != 0U && ping_time - last_signal < ctx->health_interval_ms))
                    {
                        continue;
                    }
                }
                wire_len = sizeof(wire_buf);
                if (potr_internal_packet_seal_for_context(ctx, &ctx->tx_session, &ping_pkt, wire_buf, &wire_len) != POTR_OK)
                {
                    break;
                }
                send_ret = potr_internal_udp_sendto(ctx, k, wire_buf, wire_len, &ctx->dest_addr[k], NULL);
                if (send_ret == POTR_OK)
                {
                    sent_any = 1;
                    if (is_oneway_udp)
                    {
                        cplat_atomic_store_u64(&ctx->last_ping_send_ms[k], ping_time, CPLAT_MEMORY_ORDER_RELAXED);
                        last_logged_data_ms[k] = 0U;
                    }
                }
            }

            if (is_oneway_udp && !sent_any)
            {
                /* 送出失敗による待機なしの再試行を避けます。 */
                if (!health_wait_next_send(ctx, 0, ctx->health_interval_ms))
                {
                    break;
                }
            }
        }
    }

    cplat_atomic_store_i32(&ctx->health_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);

    return;
}

/*
 * ====================================================================
 * TCP 用ヘルス チェック スレッド本体 (path ごと)
 * ====================================================================
 */
static void tcp_health_thread_func(void *arg)
{
    potr_internal_path_thread_arg *harg = (potr_internal_path_thread_arg *)arg;
    potr_context *ctx = harg->ctx;
    int path_idx = harg->path_idx;
    int send_result;

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "tcp_health[service_id=%" PRId64 " path=%d]: starting",
               ctx->service.service_id, path_idx);

    while (cplat_atomic_load_i32(&ctx->health_running[path_idx], CPLAT_MEMORY_ORDER_ACQUIRE) != 0 &&
           cplat_atomic_load_i32(&ctx->fatal_error, CPLAT_MEMORY_ORDER_ACQUIRE) == POTR_OK)
    {
        /* 固定間隔または保留要求まで待機する。 */
        if (!health_wait_next_send(ctx, path_idx, ctx->health_interval_ms))
        {
            break;
        }

        /* 終了の処理中 (POTR_ERR_CANCELED) と未接続 (POTR_ERR_DISCONNECTED) は通常の状態のため、トレースへ出さない。 */
        send_result = tcp_send_ping_packet(ctx, path_idx);
        if (send_result != POTR_OK && send_result != POTR_ERR_CANCELED && send_result != POTR_ERR_DISCONNECTED)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                       "tcp_health[service_id=%" PRId64 " path=%d]: PING send failed (result=%d)",
                       ctx->service.service_id, path_idx, send_result);
        }
    }

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "tcp_health[service_id=%" PRId64 " path=%d]: exited", ctx->service.service_id,
               path_idx);

    return;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_tcp_send_ping_now(potr_context *ctx, int path_idx)
{
    return tcp_send_ping_packet(ctx, path_idx);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_tcp_announce_ping_state_locked(potr_context *ctx, int path_idx)
{
    return tcp_write_ping_locked(ctx, path_idx, 0);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_health_thread_start(potr_context *ctx)
{
    if (ctx == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (ctx->health_interval_ms == 0)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "health_thread[service_id=%" PRId64 "]: disabled (health_interval_ms=0)",
                   ctx->service.service_id);
        return POTR_OK;
    }

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "health_thread[service_id=%" PRId64 "]: starting (interval=%ums)",
               ctx->service.service_id, (unsigned)ctx->health_interval_ms);

    /*
     * 同期資源はサービスが事前に生成して所有する。
     * 単体テスト等で直接呼ばれた場合のみ未生成分を補い、開始失敗時は自らが生成した分だけ回収する。
     */
    {
        int created_mutex = 0;
        int created_wakeup = 0;

        if (ctx->health_mutex[0] == NULL)
        {
            if (cplat_local_lock_create(&ctx->health_mutex[0]) != CPLAT_OK)
            {
                /* 同期資源生成 API は詳細原因を提供しない */
                return POTR_ERR_UNKNOWN;
            }
            created_mutex = 1;
        }

        if (ctx->health_wakeup[0] == NULL)
        {
            if (cplat_condvar_create(&ctx->health_wakeup[0]) != CPLAT_OK)
            {
                if (created_mutex)
                {
                    cplat_local_lock_dispose(ctx->health_mutex[0]);
                    ctx->health_mutex[0] = NULL;
                }
                return POTR_ERR_UNKNOWN;
            }
            created_wakeup = 1;
        }

        cplat_local_lock_lock(ctx->health_mutex[0]);
        /*
         * 片方向 UDP 以外は開始時の即時 PING を保留要求として記録する。
         * 片方向 UDP は開始直後の即時送信を行わない。
         */
        ctx->health_send_pending[0] = type_uses_immediate_health_ping(ctx->service.type) ? 1 : 0;
        cplat_atomic_store_i32(&ctx->health_running[0], 1, CPLAT_MEMORY_ORDER_RELEASE);
        cplat_local_lock_unlock(ctx->health_mutex[0]);

        if (cplat_thread_create(&ctx->health_thread[0], health_thread_func, ctx) != CPLAT_OK)
        {
            cplat_local_lock_lock(ctx->health_mutex[0]);
            cplat_atomic_store_i32(&ctx->health_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
            ctx->health_send_pending[0] = 0;
            cplat_local_lock_unlock(ctx->health_mutex[0]);
            if (created_wakeup)
            {
                cplat_condvar_dispose(ctx->health_wakeup[0]);
                ctx->health_wakeup[0] = NULL;
            }
            if (created_mutex)
            {
                cplat_local_lock_dispose(ctx->health_mutex[0]);
                ctx->health_mutex[0] = NULL;
            }
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "health_thread[service_id=%" PRId64 "]: thread create failed",
                       ctx->service.service_id);

            /* cplat のスレッド生成失敗には、porter の分類へ変換できる詳細コードがない。 */
            return POTR_ERR_UNKNOWN;
        }
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_health_thread_stop(potr_context *ctx)
{
    if (ctx == NULL || ctx->health_thread[0] == NULL)
    {
        return;
    }

    cplat_local_lock_lock(ctx->health_mutex[0]);
    cplat_atomic_store_i32(&ctx->health_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_condvar_broadcast(ctx->health_wakeup[0]);
    cplat_local_lock_unlock(ctx->health_mutex[0]);

    cplat_thread_join(ctx->health_thread[0]);

    ctx->health_thread[0] = NULL;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_health_thread_wake(potr_context *ctx)
{
    signal_health_thread(ctx, 0);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_tcp_health_thread_start(potr_context *ctx, int path_idx)
{
    if (ctx == NULL || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (ctx->health_interval_ms == 0)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "tcp_health_thread[service_id=%" PRId64 " path=%d]: disabled",
                   ctx->service.service_id, path_idx);
        return POTR_OK;
    }

    cplat_local_lock_lock(ctx->health_mutex[path_idx]);

    /* 前のスレッドの回収が終わるまで、同じ path のハンドルを再利用しない。 */
    while (ctx->health_stopping[path_idx] != 0)
    {
        cplat_condvar_wait(ctx->health_wakeup[path_idx], ctx->health_mutex[path_idx]);
    }

    if (cplat_atomic_load_i32(&ctx->close_requested, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
    {
        cplat_local_lock_unlock(ctx->health_mutex[path_idx]);
        return POTR_ERR_CANCELED;
    }

    if (ctx->health_thread[path_idx] != NULL)
    {
        cplat_local_lock_unlock(ctx->health_mutex[path_idx]);
        return POTR_OK;
    }

    ctx->health_args[path_idx].ctx = ctx;
    ctx->health_args[path_idx].path_idx = path_idx;
    if (ctx->tcp_lifecycle_mutex[path_idx] != NULL)
    {
        cplat_local_lock_lock(ctx->tcp_lifecycle_mutex[path_idx]);
        ctx->health_args[path_idx].generation = ctx->tcp_conn_generation[path_idx];
        ctx->health_args[path_idx].fd = ctx->tcp_conn_fd[path_idx];
        ctx->health_args[path_idx].watch = ctx->tcp_conn_watch[path_idx];
        ctx->health_args[path_idx].cancel = ctx->tcp_conn_cancel[path_idx];
        cplat_local_lock_unlock(ctx->tcp_lifecycle_mutex[path_idx]);
    }
    ctx->health_send_pending[path_idx] = 0;
    cplat_atomic_store_i32(&ctx->health_running[path_idx], 1, CPLAT_MEMORY_ORDER_RELEASE);

    if (cplat_thread_create(&ctx->health_thread[path_idx], tcp_health_thread_func, &ctx->health_args[path_idx]) !=
        CPLAT_OK)
    {
        cplat_atomic_store_i32(&ctx->health_running[path_idx], 0, CPLAT_MEMORY_ORDER_RELEASE);
        cplat_local_lock_unlock(ctx->health_mutex[path_idx]);
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "tcp_health_thread[service_id=%" PRId64 " path=%d]: thread create failed",
                   ctx->service.service_id, path_idx);

        /* cplat のスレッド生成失敗には、porter の分類へ変換できる詳細コードがない。 */
        return POTR_ERR_UNKNOWN;
    }

    cplat_local_lock_unlock(ctx->health_mutex[path_idx]);

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_tcp_health_thread_stop(potr_context *ctx, int path_idx)
{
    if (ctx == NULL || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH)
    {
        return;
    }

    /* 起動前の失敗で同期資源が未生成なら、回収するスレッドもない。 */
    if (ctx->health_mutex[path_idx] == NULL)
    {
        return;
    }

    cplat_local_lock_lock(ctx->health_mutex[path_idx]);

    /* close と接続管理の停止要求が重なっても、join を実行する呼び出しは 1 つにする。 */
    while (ctx->health_stopping[path_idx] != 0)
    {
        cplat_condvar_wait(ctx->health_wakeup[path_idx], ctx->health_mutex[path_idx]);
    }

    if (ctx->health_thread[path_idx] == NULL)
    {
        cplat_local_lock_unlock(ctx->health_mutex[path_idx]);
        return;
    }

    ctx->health_stopping[path_idx] = 1;
    cplat_atomic_store_i32(&ctx->health_running[path_idx], 0, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_condvar_broadcast(ctx->health_wakeup[path_idx]);
    cplat_local_lock_unlock(ctx->health_mutex[path_idx]);

    /* health 自身も health_mutex を取得するため、ロックを保持したまま終了を待たない。 */
    cplat_thread_join(ctx->health_thread[path_idx]);

    cplat_local_lock_lock(ctx->health_mutex[path_idx]);

    ctx->health_thread[path_idx] = NULL;
    ctx->health_stopping[path_idx] = 0;
    cplat_condvar_broadcast(ctx->health_wakeup[path_idx]);
    cplat_local_lock_unlock(ctx->health_mutex[path_idx]);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_tcp_health_thread_wake(potr_context *ctx, int path_idx)
{
    signal_health_thread(ctx, path_idx);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_tcp_health_thread_wake_all(potr_context *ctx)
{
    int i;

    if (ctx == NULL)
    {
        return;
    }

    for (i = 0; i < ctx->n_path; i++)
    {
        signal_health_thread(ctx, i);
    }
}
