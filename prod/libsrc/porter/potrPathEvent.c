/**
 *******************************************************************************
 *  @file           potrPathEvent.c
 *  @brief          PATH イベント内部 helper の実装。
 *  @author         Tetsuo Honda
 *  @date           2026/04/19
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <string.h>

#include <porter_const.h>

#include "potrPathEvent.h"
#include "infra/potrPlatform.h"

static int path_state_is_normal(uint8_t state)
{
    return state == POTR_PING_STATE_NORMAL ? 1 : 0;
}

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

void potr_callback_mutex_init(struct PotrContext_ *ctx)
{
    COM_UTIL_MUTEX_INIT(&ctx->callback_mutex);
}

void potr_callback_mutex_destroy(struct PotrContext_ *ctx)
{
    COM_UTIL_MUTEX_DESTROY(&ctx->callback_mutex);
}

void potr_callback_emit_locked(struct PotrContext_ *ctx, PotrPeerId peer_id,
                               PotrEvent event, const void *data, size_t len)
{
    if (ctx != NULL && ctx->callback != NULL)
    {
        ctx->callback(ctx->service.service_id, peer_id, event, data, len);
    }
}

void potr_callback_emit(struct PotrContext_ *ctx, PotrPeerId peer_id,
                        PotrEvent event, const void *data, size_t len)
{
    if (ctx == NULL || ctx->callback == NULL)
    {
        return;
    }

    COM_UTIL_MUTEX_LOCK(&ctx->callback_mutex);
    potr_callback_emit_locked(ctx, peer_id, event, data, len);
    COM_UTIL_MUTEX_UNLOCK(&ctx->callback_mutex);
}

void potr_zero_path_states(int *states)
{
    memset(states, 0, sizeof(int) * POTR_MAX_PATH);
}

void potr_copy_oneway_path_states(const struct PotrContext_ *ctx, int *states)
{
    int k;

    for (k = 0; k < (int)POTR_MAX_PATH; k++)
    {
        states[k] = path_state_is_normal(ctx->path_ping_state[k]);
    }
}

void potr_copy_bidir_udp_path_states(const struct PotrContext_ *ctx, int *states)
{
    int k;

    for (k = 0; k < (int)POTR_MAX_PATH; k++)
    {
        states[k] = path_state_is_normal(ctx->path_ping_state[k])
                 && path_state_is_normal(ctx->remote_path_ping_state[k]);
    }
}

void potr_copy_bidir_n1_path_states(const PotrPeerContext *peer, int *states)
{
    int k;

    for (k = 0; k < (int)POTR_MAX_PATH; k++)
    {
        states[k] = path_state_is_normal(peer->path_ping_state[k])
                 && path_state_is_normal(peer->remote_path_ping_state[k]);
    }
}

void potr_copy_tcp_path_states(const struct PotrContext_ *ctx, int *states)
{
    int k;

    for (k = 0; k < (int)POTR_MAX_PATH; k++)
    {
        states[k] = (ctx->tcp_conn_fd[k] != POTR_INVALID_SOCKET)
                 && path_state_is_normal(ctx->path_ping_state[k])
                 && path_state_is_normal(ctx->remote_path_ping_state[k]);
    }
}

void potr_sync_service_path_state_locked(struct PotrContext_   *ctx,
                                         const int             *next_states,
                                         PotrPreparedPathEvents *prepared)
{
    int k;
    int old_alive;
    int new_alive;

    memset(prepared, 0, sizeof(*prepared));
    memcpy(prepared->final_states, next_states, sizeof(prepared->final_states));

    old_alive = ctx->health_alive ? 1 : 0;
    new_alive = any_path_alive(next_states);

    for (k = 0; k < (int)POTR_MAX_PATH; k++)
    {
        if (ctx->path_logical_alive[k] == next_states[k])
        {
            continue;
        }

        prepared->changed_paths[prepared->changed_count] = k;
        prepared->changed_events[prepared->changed_count] =
            next_states[k] ? POTR_EVENT_PATH_CONNECTED : POTR_EVENT_PATH_DISCONNECTED;
        prepared->changed_count++;
    }

    memcpy(ctx->path_logical_alive, next_states, sizeof(ctx->path_logical_alive));
    ctx->health_alive = new_alive;

    if (old_alive != new_alive)
    {
        prepared->session_event = new_alive ? POTR_EVENT_CONNECTED
                                            : POTR_EVENT_DISCONNECTED;
    }
}

void potr_sync_peer_path_state_locked(PotrPeerContext         *peer,
                                      const int               *next_states,
                                      PotrPreparedPathEvents  *prepared)
{
    int k;
    int old_alive;
    int new_alive;

    memset(prepared, 0, sizeof(*prepared));
    memcpy(prepared->final_states, next_states, sizeof(prepared->final_states));

    old_alive = peer->health_alive ? 1 : 0;
    new_alive = any_path_alive(next_states);

    for (k = 0; k < (int)POTR_MAX_PATH; k++)
    {
        if (peer->path_logical_alive[k] == next_states[k])
        {
            continue;
        }

        prepared->changed_paths[prepared->changed_count] = k;
        prepared->changed_events[prepared->changed_count] =
            next_states[k] ? POTR_EVENT_PATH_CONNECTED : POTR_EVENT_PATH_DISCONNECTED;
        prepared->changed_count++;
    }

    memcpy(peer->path_logical_alive, next_states, sizeof(peer->path_logical_alive));
    peer->health_alive = new_alive;

    if (old_alive != new_alive)
    {
        prepared->session_event = new_alive ? POTR_EVENT_CONNECTED
                                            : POTR_EVENT_DISCONNECTED;
    }
}

void potr_emit_service_path_events_locked(struct PotrContext_          *ctx,
                                          const PotrPreparedPathEvents *prepared)
{
    int k;

    for (k = 0; k < prepared->changed_count; k++)
    {
        potr_callback_emit_locked(ctx, POTR_PEER_NA,
                                  prepared->changed_events[k],
                                  prepared->final_states,
                                  (size_t)prepared->changed_paths[k]);
    }

    if (prepared->session_event != 0)
    {
        potr_callback_emit_locked(ctx, POTR_PEER_NA,
                                  prepared->session_event, NULL, 0);
    }
}

void potr_emit_peer_path_events_locked(struct PotrContext_          *ctx,
                                       PotrPeerContext              *peer,
                                       const PotrPreparedPathEvents *prepared)
{
    int k;

    for (k = 0; k < prepared->changed_count; k++)
    {
        potr_callback_emit_locked(ctx, peer->peer_id,
                                  prepared->changed_events[k],
                                  prepared->final_states,
                                  (size_t)prepared->changed_paths[k]);
    }

    if (prepared->session_event != 0)
    {
        potr_callback_emit_locked(ctx, peer->peer_id,
                                  prepared->session_event, NULL, 0);
    }
}
