/**
 *******************************************************************************
 *  @file           potr_peer_disconnect.c
 *  @brief          ピアとの接続を切断する potr_peer_disconnect 関数を提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/23
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/base/platform.h>
#include <inttypes.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/porter_spec.h>

#include <porter/potr_context.h>
#include <porter/potr_path_event.h>
#include <porter/potr_peer_table.h>
#include <porter/infra/potr_trace.h>

/* Doxygen コメントは、ヘッダーに記載 */

int potr_peer_disconnect(potr_context *handle, potr_peer_id peer_id)
{
    potr_context *ctx = (potr_context *)handle;
    int64_t service_id;

    if (ctx == NULL)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_peer_disconnect: handle is NULL");
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (ctx != NULL)
    {
        service_id = ctx->service.service_id;
    }
    else
    {
        service_id = 0;
    }

    if (peer_id == POTR_PEER_NA || peer_id == POTR_PEER_ALL)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "potr_peer_disconnect: service_id=%" PRId64 " invalid peer_id=%u"
                   " (POTR_PEER_NA or POTR_PEER_ALL not allowed)",
                   service_id, (unsigned)peer_id);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (!ctx->is_multi_peer)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_peer_disconnect: service_id=%" PRId64 " not in N:1 mode",
                   ctx->service.service_id);
        return POTR_ERR_UNSUPPORTED;
    }

    cplat_local_lock_lock(ctx->peers_mutex, CPLAT_SYNC_WAIT_FOREVER);

    {
        potr_internal_peer_context *peer = potr_internal_peer_find_by_id(ctx, peer_id);

        if (peer == NULL)
        {
            cplat_local_lock_unlock(ctx->peers_mutex);
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_peer_disconnect: service_id=%" PRId64 " peer_id=%u not found",
                       ctx->service.service_id, (unsigned)peer_id);
            return POTR_ERR_NOT_FOUND;
        }

        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "potr_peer_disconnect: service_id=%" PRId64 " peer_id=%u disconnecting",
                   ctx->service.service_id, (unsigned)peer_id);

        /* FIN を送信 */
        potr_internal_peer_send_fin(ctx, peer);

        /* 論理接続 path をすべて落としてから DISCONNECTED を発火する */
        {
            int next_states[POTR_MAX_PATH];
            potr_internal_prepared_path_events prepared;

            potr_internal_zero_path_states(next_states);
            cplat_local_lock_lock(ctx->callback_mutex, CPLAT_SYNC_WAIT_FOREVER);
            potr_internal_sync_peer_path_state_locked(peer, next_states, &prepared);
            potr_internal_emit_peer_path_events_locked(ctx, peer, &prepared);
            cplat_local_lock_unlock(ctx->callback_mutex);
        }

        /* ピア リソースを解放 */
        potr_internal_peer_free(ctx, peer);
    }

    cplat_local_lock_unlock(ctx->peers_mutex);
    return POTR_OK;
}
