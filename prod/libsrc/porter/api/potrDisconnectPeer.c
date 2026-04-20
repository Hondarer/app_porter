/**
 *******************************************************************************
 *  @file           potrDisconnectPeer.c
 *  @brief          potrDisconnectPeer 関数の実装。
 *  @author         Tetsuo Honda
 *  @date           2026/03/23
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>
#include <inttypes.h>
#include <porter_const.h>
#include <porter.h>

#include "../potrContext.h"
#include "../potrPathEvent.h"
#include "../potrPeerTable.h"
#include "../infra/potrTrace.h"


/* doxygen コメントは、ヘッダに記載 */
POTR_EXPORT int POTR_API potrDisconnectPeer(PotrHandle handle, PotrPeerId peer_id)
{
    struct PotrContext_ *ctx = (struct PotrContext_ *)handle;

    if (ctx == NULL)
    {
        POTR_LOG(TRACE_LEVEL_ERROR, "potrDisconnectPeer: handle is NULL");
        return POTR_ERROR;
    }

    if (peer_id == POTR_PEER_NA || peer_id == POTR_PEER_ALL)
    {
        POTR_LOG(TRACE_LEVEL_ERROR,
                 "potrDisconnectPeer: service_id=%" PRId64 " invalid peer_id=%u"
                 " (POTR_PEER_NA or POTR_PEER_ALL not allowed)",
                 ctx != NULL ? ctx->service.service_id : 0, (unsigned)peer_id);
        return POTR_ERROR;
    }

    if (!ctx->is_multi_peer)
    {
        POTR_LOG(TRACE_LEVEL_ERROR,
                 "potrDisconnectPeer: service_id=%" PRId64 " not in N:1 mode",
                 ctx->service.service_id);
        return POTR_ERROR;
    }

    COM_UTIL_MUTEX_LOCK(&ctx->peers_mutex);

    {
        PotrPeerContext *peer = peer_find_by_id(ctx, peer_id);

        if (peer == NULL)
        {
            COM_UTIL_MUTEX_UNLOCK(&ctx->peers_mutex);
            POTR_LOG(TRACE_LEVEL_ERROR,
                     "potrDisconnectPeer: service_id=%" PRId64 " peer_id=%u not found",
                     ctx->service.service_id, (unsigned)peer_id);
            return POTR_ERROR;
        }

        POTR_LOG(TRACE_LEVEL_INFO,
                 "potrDisconnectPeer: service_id=%" PRId64 " peer_id=%u disconnecting",
                 ctx->service.service_id, (unsigned)peer_id);

        /* FIN を送信 */
        peer_send_fin(ctx, peer);

        /* 論理接続 path をすべて落としてから DISCONNECTED を発火する */
        {
            int                    next_states[POTR_MAX_PATH];
            PotrPreparedPathEvents prepared;

            potr_zero_path_states(next_states);
            COM_UTIL_MUTEX_LOCK(&ctx->callback_mutex);
            potr_sync_peer_path_state_locked(peer, next_states, &prepared);
            potr_emit_peer_path_events_locked(ctx, peer, &prepared);
            COM_UTIL_MUTEX_UNLOCK(&ctx->callback_mutex);
        }

        /* ピアリソースを解放 */
        peer_free(ctx, peer);
    }

    COM_UTIL_MUTEX_UNLOCK(&ctx->peers_mutex);
    return POTR_SUCCESS;
}
