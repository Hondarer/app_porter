/**
 *******************************************************************************
 *  @file           potrPathEvent.h
 *  @brief          PATH イベント内部 helper の宣言。
 *  @author         Tetsuo Honda
 *  @date           2026/04/19
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef POTR_PATH_EVENT_H
#define POTR_PATH_EVENT_H

#include <stddef.h>

#include <porter/porter_type.h>

#include <porter/potrContext.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    typedef struct PotrPreparedPathEvents
    {
        int final_states[POTR_MAX_PATH];
        int changed_paths[POTR_MAX_PATH];
        PotrEvent changed_events[POTR_MAX_PATH];
        int changed_count;
        PotrEvent session_event;
    } PotrPreparedPathEvents;

    extern void potr_callback_mutex_init(PotrContext *ctx);
    extern void potr_callback_mutex_dispose(PotrContext *ctx);
    extern void potr_callback_emit(PotrContext *ctx, PotrPeerId peer_id, PotrEvent event, const void *data, size_t len);
    extern void potr_callback_emit_locked(PotrContext *ctx, PotrPeerId peer_id, PotrEvent event, const void *data,
                                          size_t len);

    extern void potr_zero_path_states(int *states);
    extern void potr_copy_oneway_path_states(const PotrContext *ctx, int *states);
    extern void potr_copy_bidir_udp_path_states(const PotrContext *ctx, int *states);
    extern void potr_copy_bidir_n1_path_states(const PotrPeerContext *peer, int *states);
    extern void potr_copy_tcp_path_states(const PotrContext *ctx, int *states);

    extern void potr_sync_service_path_state_locked(PotrContext *ctx, const int *next_states,
                                                    PotrPreparedPathEvents *prepared);
    extern void potr_sync_peer_path_state_locked(PotrPeerContext *peer, const int *next_states,
                                                 PotrPreparedPathEvents *prepared);
    extern void potr_emit_service_path_events_locked(PotrContext *ctx, const PotrPreparedPathEvents *prepared);
    extern void potr_emit_peer_path_events_locked(PotrContext *ctx, const PotrPeerContext *peer,
                                                  const PotrPreparedPathEvents *prepared);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_PATH_EVENT_H */
