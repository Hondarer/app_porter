/**
 *******************************************************************************
 *  @file           thread_recv_health.c
 *  @brief          受信タイムアウトの監視と PING の起床判断を実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/base/platform.h>
#include <string.h>
#include <inttypes.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>

#include <porter/protocol/packet.h>
#include <porter/protocol/window.h>
#include <porter/potr_context.h>
#include <porter/potr_peer_table.h>
#include <porter/thread/potr_health_thread.h>
#include <porter/thread/potr_recv_thread.h>
#include <porter/infra/potr_trace.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/endpoint.h>
#include <cplat/net/socket.h>

#include "thread_recv_validate.h"
#include "thread_recv_slot.h"
#include "thread_recv_session.h"
#include "thread_recv_fin.h"
#include "thread_recv_window.h"
#include "thread_recv_dispatch.h"

#include "thread_recv_health.h"

/* N:1: poll タイムアウト時にヘルス チェック タイムアウトを確認する */
/* Doxygen コメントは、ヘッダーに記載 */
void thread_recv_health_check_peers(potr_context *ctx)
{
    cplat_timespec now_ts;
    int i;

    cplat_clock_get_monotonic(&now_ts);
    int k;
    int should_wake_health = 0;

    if (ctx->health_timeout_ms == 0)
        return;

    cplat_local_lock_lock(ctx->peers_mutex, CPLAT_SYNC_WAIT_FOREVER);

    for (i = 0; i < ctx->max_peers; i++)
    {
        int64_t elapsed_ms;
        int path_state_changed = 0;

        if (!ctx->peers[i].active)
            continue;
        if (cplat_atomic_load_i32(&ctx->peers[i].health_alive, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
            continue;

        /* パス単位のタイムアウト: 不通パスを dest_addr から削除する */
        for (k = 0; k < (int)POTR_MAX_PATH; k++)
        {
            int64_t path_elapsed;

            if (endpoint_is_unset(&ctx->peers[i].dest_addr[k]))
                continue; /* 未使用 */
            if (ctx->peers[i].path_last_recv_ts[k].tv_sec == 0)
                continue; /* 初回受信前 */

            path_elapsed = cplat_timespec_diff_ms(&now_ts, &ctx->peers[i].path_last_recv_ts[k]);

            if (path_elapsed >= (int64_t)ctx->health_timeout_ms)
            {
                int state_changed =
                    thread_recv_set_path_ping_state(&ctx->peers[i].path_ping_state[k], POTR_PING_STATE_ABNORMAL);
                should_wake_health |= state_changed;
                path_state_changed |= state_changed;
                potr_internal_peer_path_clear(ctx, &ctx->peers[i], k);
            }
        }

        if (path_state_changed)
        {
            thread_recv_slot peer_slot;

            thread_recv_slot_init_peer(&peer_slot, ctx, &ctx->peers[i]);
            thread_recv_sync_path_state(&peer_slot);
        }

        /* ピア単位のタイムアウト: 全パス消滅、または最終受信から切断判定 */
        if (ctx->peers[i].last_recv_ts.tv_sec == 0)
            continue;

        elapsed_ms = cplat_timespec_diff_ms(&now_ts, &ctx->peers[i].last_recv_ts);

        if (elapsed_ms >= (int64_t)ctx->health_timeout_ms)
        {
            potr_peer_id dead_id = ctx->peers[i].peer_id;

            POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING, "recv[service_id=%" PRId64 "]: peer=%u DISCONNECTED (timeout %lldms)",
                       ctx->service.service_id, (unsigned)dead_id, (long long)elapsed_ms);

            memset((void *)ctx->peers[i].remote_path_ping_state, 0, sizeof(ctx->peers[i].remote_path_ping_state));
            thread_recv_disconnect_peer_all_paths(ctx, &ctx->peers[i]);
            potr_internal_peer_free(ctx, &ctx->peers[i]);
        }
    }

    cplat_local_lock_unlock(ctx->peers_mutex);

    if (should_wake_health)
    {
        potr_internal_health_thread_wake(ctx);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */
void thread_recv_health_wake_tcp(potr_context *ctx, const int path_idx, const int state_changed)
{
    if (state_changed && is_tcp_type(ctx->service.type))
    {
        if (ctx->health_interval_ms > 0 &&
            cplat_atomic_load_i32(&ctx->health_running[path_idx], CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
        {
            /*
             * TCP は path ごとに health スレッドを持つが、PING ペイロードは全 path の
             * 受信状態ベクトルを運ぶ。変化した path 自身が送れない場合でも別 path から
             * 更新済みベクトルを伝播できるよう、全 health スレッドを即時起床させる。
             */
            potr_internal_tcp_health_thread_wake_all(ctx);
        }
        else
        {
            (void)potr_internal_tcp_send_ping_now(ctx, path_idx);
        }
    }
}

/* タイムアウト時に経過時間を確認し、必要なら peer_port クリアと DISCONNECTED イベントを発火する */
/* Doxygen コメントは、ヘッダーに記載 */
void thread_recv_health_check_service(thread_recv_slot *slot)
{
    potr_context *ctx = slot->ctx;
    cplat_timespec now_ts;
    int i;
    int should_wake_health = 0;
    int path_state_changed = 0;

    if (ctx->health_timeout_ms == 0)
        return;

    cplat_clock_get_monotonic(&now_ts);

    /* パスごとのタイムアウト: peer_port をクリア */
    for (i = 0; i < ctx->n_path; i++)
    {
        int64_t elapsed_ms;

        if (ctx->path_last_recv_ts[i].tv_sec == 0)
            continue;

        elapsed_ms = cplat_timespec_diff_ms(&now_ts, &ctx->path_last_recv_ts[i]);

        if (elapsed_ms >= (int64_t)ctx->health_timeout_ms)
        {
            int state_changed = thread_recv_set_path_ping_state(&ctx->path_ping_state[i], POTR_PING_STATE_ABNORMAL);
            should_wake_health |= state_changed;
            path_state_changed |= state_changed;
            ctx->peer_port[i] = 0;
            ctx->path_last_recv_ts[i].tv_sec = 0;
            /* unicast_bidir で動的学習したアドレス・ポートをリセットする (再接続を許可) */
            if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
            {
                if (ctx->service.src_addr[0][0] == '\0')
                {
                    /* 動的 1:1 RECEIVER: 学習した送信元 IP をリセット */
                    ctx->dest_addr[i].address = 0;
                }
                if (ctx->service.src_port == 0)
                {
                    /* エフェメラル ポート動的学習: ポートをリセット */
                    ctx->dest_addr[i].port = 0;
                }
            }
        }
    }

    if (path_state_changed)
    {
        thread_recv_sync_path_state(slot);
    }

    /* 全体の health_alive 判定 */
    if (cplat_atomic_load_i32(&ctx->health_alive, CPLAT_MEMORY_ORDER_ACQUIRE) == 0 || ctx->last_recv_ts.tv_sec == 0)
        return;

    {
        int64_t elapsed_ms;
        elapsed_ms = cplat_timespec_diff_ms(&now_ts, &ctx->last_recv_ts);

        if (elapsed_ms >= (int64_t)ctx->health_timeout_ms)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING, "recv[service_id=%" PRId64 "]: DISCONNECTED (timeout %lldms >= %ums)",
                       ctx->service.service_id, (long long)elapsed_ms, (unsigned)ctx->health_timeout_ms);
            /* FIN と同様にセッション状態をリセットして次の接続を受け入れ可能にする。
               peer_session_known をクリアすることで、送信者が同一セッションのまま
               復帰した場合でも potr_internal_window_init を経由して受信ウィンドウを初期化し、
               前セッションの next_seq が gap スキャンに影響しないようにする。 */
            ctx->pending_fin = 0;
            ctx->fin_target_seq = 0;
            ctx->peer_session_known = 0;
            ctx->reorder_pending = 0;
            ctx->last_recv_ts.tv_sec = 0;
            /* 次の接続到来まで受信状態を不定に戻す */
            should_wake_health |=
                thread_recv_set_all_path_ping_states(ctx->path_ping_state, POTR_MAX_PATH, POTR_PING_STATE_UNDEFINED);
            memset((void *)ctx->remote_path_ping_state, 0, sizeof(ctx->remote_path_ping_state));
            thread_recv_disconnect_service_all_paths(ctx);
            potr_internal_window_init(&ctx->recv_window, 0, ctx->global.window_size, ctx->global.max_payload);
        }
    }

    thread_recv_wake_udp_interrupt_ping(ctx, should_wake_health);
}
