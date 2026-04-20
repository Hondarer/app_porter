/**
 *******************************************************************************
 *  @file           potrSend.c
 *  @brief          potrSend 関数の実装。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>
#include <stdlib.h>
#include <inttypes.h>

#include <porter_const.h>
#include <porter.h>

#include "../potrContext.h"
#include "../potrPeerTable.h"
#include "../infra/potrSendQueue.h"
#include <com_util/compress/compress.h>
#include "../infra/potrTrace.h"


/* N:1 モードで 1 ピアへ send を行う内部実装 (peers_mutex 取得不要・呼び出し元で検索済み) */
static int send_to_peer(struct PotrContext_ *ctx, PotrPeerId peer_id,
                        const uint8_t *ptr, size_t len, int flags,
                        uint16_t base_flags)
{
    size_t   remaining  = len;
    size_t   max_payload;

    max_payload = ctx->global.max_payload - POTR_PAYLOAD_ELEM_HDR_SIZE;
    if (ctx->service.encrypt_enabled)
    {
        max_payload -= POTR_CRYPTO_TAG_SIZE;
    }

    if ((flags & POTR_SEND_BLOCKING) != 0)
    {
        potr_send_queue_wait_drained(&ctx->send_queue);
    }

    while (remaining > 0)
    {
        size_t   chunk     = (remaining > max_payload) ? max_payload : remaining;
        int      more_frag = (remaining > chunk);
        uint16_t elem_flags = base_flags;

        if (more_frag)
        {
            elem_flags |= POTR_FLAG_MORE_FRAG;
        }

        if (potr_send_queue_push_wait(&ctx->send_queue, peer_id,
                                      elem_flags, ptr, (uint16_t)chunk,
                                      &ctx->send_thread_running) != POTR_SUCCESS)
        {
            return POTR_ERROR;
        }

        ptr       += chunk;
        remaining -= chunk;
    }

    if ((flags & POTR_SEND_BLOCKING) != 0)
    {
        potr_send_queue_wait_drained(&ctx->send_queue);
    }

    return POTR_SUCCESS;
}

/* doxygen コメントは、ヘッダに記載 */
POTR_EXPORT int POTR_API potrSend(PotrHandle handle, PotrPeerId peer_id,
                              const void *data, size_t len, int flags)
{
    struct PotrContext_ *ctx       = (struct PotrContext_ *)handle;
    const uint8_t       *ptr      = (const uint8_t *)data;
    uint16_t             base_flags = 0;

    if (ctx == NULL || data == NULL || len == 0
        || len > (size_t)ctx->global.max_message_size)
    {
        POTR_LOG(TRACE_LEVEL_ERROR,
                 "potrSend: invalid argument (handle=%p data=%p len=%zu max=%u)",
                 (const void *)handle, data, len,
                 (ctx != NULL) ? (unsigned)ctx->global.max_message_size : 0U);
        return POTR_ERROR;
    }

    POTR_LOG(TRACE_LEVEL_VERBOSE,
             "potrSend: service_id=%" PRId64 " peer_id=%u len=%zu flags=0x%x",
             ctx->service.service_id, (unsigned)peer_id, len, (unsigned)flags);

    if (ctx->close_requested)
    {
        POTR_LOG(TRACE_LEVEL_VERBOSE,
                 "potrSend: service_id=%" PRId64 " rejected because close is in progress",
                 ctx->service.service_id);
        return POTR_ERROR;
    }

    /* TCP: 物理 path 未接続、または PING 交換による論理 CONNECTED 前は
       POTR_ERROR_DISCONNECTED を返す */
    if (potr_is_tcp_type(ctx->service.type)
        && (ctx->tcp_active_paths == 0 || !ctx->health_alive))
    {
        POTR_LOG(TRACE_LEVEL_VERBOSE,
                 "potrSend: service_id=%" PRId64 " TCP not connected"
                 " (active_paths=%d health_alive=%d)",
                 ctx->service.service_id,
                 (int)ctx->tcp_active_paths, (int)ctx->health_alive);
        return POTR_ERROR_DISCONNECTED;
    }

    /* UDP 1:1 双方向: PING 交換による接続確立前は POTR_ERROR_DISCONNECTED を返す */
    if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR && !ctx->health_alive)
    {
        POTR_LOG(TRACE_LEVEL_VERBOSE,
                 "potrSend: service_id=%" PRId64 " UDP bidir not connected",
                 ctx->service.service_id);
        return POTR_ERROR_DISCONNECTED;
    }

    /* RAW モードは常にブロッキング送信 */
    if (potr_is_raw_type(ctx->service.type))
    {
        flags |= POTR_SEND_BLOCKING;
    }

    /* 圧縮が要求された場合はペイロード全体を圧縮する */
    if ((flags & POTR_SEND_COMPRESS) != 0)
    {
        size_t cmp_len = ctx->compress_buf_size;

        if (com_util_compress(ctx->compress_buf, &cmp_len,
                          (const uint8_t *)data, len) != 0)
        {
            POTR_LOG(TRACE_LEVEL_ERROR,
                     "potrSend: service_id=%" PRId64 " compression failed (len=%zu)",
                     ctx->service.service_id, len);
            return POTR_ERROR;
        }

        if (cmp_len < len)
        {
            POTR_LOG(TRACE_LEVEL_VERBOSE,
                     "potrSend: service_id=%" PRId64 " compress %zu -> %zu bytes",
                     ctx->service.service_id, len, cmp_len);
            ptr        = ctx->compress_buf;
            len        = cmp_len;
            base_flags = POTR_FLAG_COMPRESSED;
        }
        else
        {
            POTR_LOG(TRACE_LEVEL_VERBOSE,
                     "potrSend: service_id=%" PRId64 " compression skipped"
                     " (compressed %zu >= original %zu bytes), sending uncompressed",
                     ctx->service.service_id, cmp_len, len);
            /* 圧縮効果なし: 非圧縮のまま送信 (ptr, len, base_flags は初期値を維持) */
        }
    }

    /* N:1 モード: peer_id に基づくルーティング */
    if (ctx->is_multi_peer)
    {
        if (peer_id == POTR_PEER_NA)
        {
            POTR_LOG(TRACE_LEVEL_ERROR,
                     "potrSend: service_id=%" PRId64 " N:1 mode requires valid peer_id (got POTR_PEER_NA)",
                     ctx->service.service_id);
            return POTR_ERROR;
        }

        if (peer_id == POTR_PEER_ALL)
        {
            /* 全アクティブピアへ送信: peers_mutex を保持しない状態で送信するため
             * まず peer_id リストを収集してからキューに積む */
            PotrPeerId *ids;
            int         n_ids = 0;
            int         i;
            int         result = POTR_SUCCESS;

            ids = (PotrPeerId *)malloc((size_t)ctx->max_peers * sizeof(PotrPeerId));
            if (ids == NULL)
            {
                POTR_LOG(TRACE_LEVEL_ERROR,
                         "potrSend: service_id=%" PRId64 " PEER_ALL malloc failed",
                         ctx->service.service_id);
                return POTR_ERROR;
            }

            COM_UTIL_MUTEX_LOCK(&ctx->peers_mutex);
            for (i = 0; i < ctx->max_peers; i++)
            {
                if (ctx->peers[i].active && ctx->peers[i].health_alive)
                {
                    ids[n_ids++] = ctx->peers[i].peer_id;
                }
            }
            COM_UTIL_MUTEX_UNLOCK(&ctx->peers_mutex);

            if (n_ids == 0)
            {
                free(ids);
                POTR_LOG(TRACE_LEVEL_VERBOSE,
                         "potrSend: service_id=%" PRId64 " PEER_ALL not connected",
                         ctx->service.service_id);
                return POTR_ERROR_DISCONNECTED;
            }

            for (i = 0; i < n_ids; i++)
            {
                if (send_to_peer(ctx, ids[i], ptr, len, flags, base_flags) != POTR_SUCCESS)
                {
                    result = POTR_ERROR;
                }
            }
            free(ids);
            return result;
        }
        else
        {
            /* 指定ピアへ送信: 存在確認・接続確認だけ mutex で保護し、送信は mutex 外で行う */
            int peer_alive;
            COM_UTIL_MUTEX_LOCK(&ctx->peers_mutex);
            {
                PotrPeerContext *peer = peer_find_by_id(ctx, peer_id);
                if (peer == NULL)
                {
                    COM_UTIL_MUTEX_UNLOCK(&ctx->peers_mutex);
                    POTR_LOG(TRACE_LEVEL_ERROR,
                             "potrSend: service_id=%" PRId64 " peer_id=%u not found",
                             ctx->service.service_id, (unsigned)peer_id);
                    return POTR_ERROR;
                }
                peer_alive = peer->health_alive;
            }
            COM_UTIL_MUTEX_UNLOCK(&ctx->peers_mutex);

            if (!peer_alive)
            {
                POTR_LOG(TRACE_LEVEL_VERBOSE,
                         "potrSend: service_id=%" PRId64 " peer_id=%u N:1 not connected",
                         ctx->service.service_id, (unsigned)peer_id);
                return POTR_ERROR_DISCONNECTED;
            }

            return send_to_peer(ctx, peer_id, ptr, len, flags, base_flags);
        }
    }

    /* 1:1 モード: peer_id は無視 */
    return send_to_peer(ctx, POTR_PEER_NA, ptr, len, flags, base_flags);
}
