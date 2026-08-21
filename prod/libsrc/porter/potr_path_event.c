/**
 *******************************************************************************
 *  @file           potr_path_event.c
 *  @brief          PATH イベントを処理する内部 helper を実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/04/19
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <string.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>

#include <com_util/sync/sync.h>
#include <porter/potr_path_event.h>

/**
 *  @brief          PING 受信状態が NORMAL かどうかを判定します。
 *  @param[in]      state   判定する PING 受信状態 (POTR_PING_STATE_*)。
 *  @return         POTR_PING_STATE_NORMAL の場合は 1、それ以外は 0 を返します。
 */
static int path_state_is_normal(uint8_t state)
{
    if (state == POTR_PING_STATE_NORMAL)
    {
        return 1;
    }

    return 0;
}

/**
 *  @brief          論理接続中の path が 1 本以上存在するかを判定します。
 *  @param[in]      states  POTR_MAX_PATH 要素の path 論理接続状態配列。
 *  @return         1 本以上が接続中の場合は 1、すべて切断の場合は 0 を返します。
 */
static int any_path_alive(const int *states)
{
    int k;

    for (k = 0; k < (int)POTR_MAX_PATH; k++)
    {
        if (states[k] != 0)
        {
            return 1;
        }
    }
    return 0;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_callback_mutex_init(potr_context *ctx)
{
    com_util_local_lock_create(&ctx->callback_mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_callback_mutex_dispose(potr_context *ctx)
{
    com_util_local_lock_dispose(ctx->callback_mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_callback_emit_locked(potr_context *ctx, potr_peer_id peer_id, potr_event event, const void *data, size_t len)
{
    if (ctx != NULL && ctx->callback != NULL)
    {
        ctx->callback(ctx->service.service_id, peer_id, event, data, len);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_callback_emit(potr_context *ctx, potr_peer_id peer_id, potr_event event, const void *data, size_t len)
{
    if (ctx == NULL || ctx->callback == NULL)
    {
        return;
    }

    com_util_local_lock_lock(ctx->callback_mutex, COM_UTIL_SYNC_WAIT_FOREVER);
    potr_internal_callback_emit_locked(ctx, peer_id, event, data, len);
    com_util_local_lock_unlock(ctx->callback_mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_zero_path_states(int *states)
{
    memset(states, 0, sizeof(int) * POTR_MAX_PATH);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_copy_oneway_path_states(const potr_context *ctx, int *states)
{
    int k;

    for (k = 0; k < (int)POTR_MAX_PATH; k++)
    {
        states[k] = path_state_is_normal(ctx->path_ping_state[k]);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_copy_bidir_udp_path_states(const potr_context *ctx, int *states)
{
    int k;

    for (k = 0; k < (int)POTR_MAX_PATH; k++)
    {
        states[k] =
            path_state_is_normal(ctx->path_ping_state[k]) && path_state_is_normal(ctx->remote_path_ping_state[k]);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_copy_bidir_n1_path_states(const potr_internal_peer_context *peer, int *states)
{
    int k;

    for (k = 0; k < (int)POTR_MAX_PATH; k++)
    {
        states[k] =
            path_state_is_normal(peer->path_ping_state[k]) && path_state_is_normal(peer->remote_path_ping_state[k]);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_copy_tcp_path_states(const potr_context *ctx, int *states)
{
    int k;

    for (k = 0; k < (int)POTR_MAX_PATH; k++)
    {
        states[k] = (ctx->tcp_conn_fd[k] != COM_UTIL_INVALID_SOCKET) && path_state_is_normal(ctx->path_ping_state[k]) &&
                    path_state_is_normal(ctx->remote_path_ping_state[k]);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_sync_service_path_state_locked(potr_context *ctx, const int *next_states, potr_internal_prepared_path_events *prepared)
{
    int k;
    int old_alive;
    int new_alive;

    memset(prepared, 0, sizeof(*prepared));
    memcpy(prepared->final_states, next_states, sizeof(prepared->final_states));

    if (ctx->health_alive)
    {
        old_alive = 1;
    }
    else
    {
        old_alive = 0;
    }
    new_alive = any_path_alive(next_states);

    for (k = 0; k < (int)POTR_MAX_PATH; k++)
    {
        if (ctx->path_logical_alive[k] == next_states[k])
        {
            continue;
        }

        prepared->changed_paths[prepared->changed_count] = k;
        if (next_states[k] != 0)
        {
            prepared->changed_events[prepared->changed_count] = POTR_EVENT_PATH_CONNECTED;
        }
        else
        {
            prepared->changed_events[prepared->changed_count] = POTR_EVENT_PATH_DISCONNECTED;
        }
        prepared->changed_count++;
    }

    memcpy(ctx->path_logical_alive, next_states, sizeof(ctx->path_logical_alive));
    ctx->health_alive = new_alive;

    if (old_alive != new_alive)
    {
        if (new_alive != 0)
        {
            prepared->session_event = POTR_EVENT_CONNECTED;
        }
        else
        {
            prepared->session_event = POTR_EVENT_DISCONNECTED;
        }
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_sync_peer_path_state_locked(potr_internal_peer_context *peer, const int *next_states, potr_internal_prepared_path_events *prepared)
{
    int k;
    int old_alive;
    int new_alive;

    memset(prepared, 0, sizeof(*prepared));
    memcpy(prepared->final_states, next_states, sizeof(prepared->final_states));

    if (peer->health_alive)
    {
        old_alive = 1;
    }
    else
    {
        old_alive = 0;
    }
    new_alive = any_path_alive(next_states);

    for (k = 0; k < (int)POTR_MAX_PATH; k++)
    {
        if (peer->path_logical_alive[k] == next_states[k])
        {
            continue;
        }

        prepared->changed_paths[prepared->changed_count] = k;
        if (next_states[k] != 0)
        {
            prepared->changed_events[prepared->changed_count] = POTR_EVENT_PATH_CONNECTED;
        }
        else
        {
            prepared->changed_events[prepared->changed_count] = POTR_EVENT_PATH_DISCONNECTED;
        }
        prepared->changed_count++;
    }

    memcpy(peer->path_logical_alive, next_states, sizeof(peer->path_logical_alive));
    peer->health_alive = new_alive;

    if (old_alive != new_alive)
    {
        if (new_alive != 0)
        {
            prepared->session_event = POTR_EVENT_CONNECTED;
        }
        else
        {
            prepared->session_event = POTR_EVENT_DISCONNECTED;
        }
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_emit_service_path_events_locked(potr_context *ctx, const potr_internal_prepared_path_events *prepared)
{
    int k;

    for (k = 0; k < prepared->changed_count; k++)
    {
        potr_internal_callback_emit_locked(ctx, POTR_PEER_NA, prepared->changed_events[k], prepared->final_states,
                                  (size_t)prepared->changed_paths[k]);
    }

    if (prepared->session_event != 0)
    {
        potr_internal_callback_emit_locked(ctx, POTR_PEER_NA, prepared->session_event, NULL, 0);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_emit_peer_path_events_locked(potr_context *ctx, const potr_internal_peer_context *peer,
                                       const potr_internal_prepared_path_events *prepared)
{
    int k;

    for (k = 0; k < prepared->changed_count; k++)
    {
        potr_internal_callback_emit_locked(ctx, peer->peer_id, prepared->changed_events[k], prepared->final_states,
                                  (size_t)prepared->changed_paths[k]);
    }

    if (prepared->session_event != 0)
    {
        potr_internal_callback_emit_locked(ctx, peer->peer_id, prepared->session_event, NULL, 0);
    }
}
