/** @file thread_recv_slot.c
 * @brief 受信状態を参照し、メッセージを結合・展開して配信します。
 */

#include <string.h>
#include <inttypes.h>
#include <porter/porter_const.h>
#include <porter/protocol/packet.h>
#include <porter/potr_path_event.h>
#include <porter/infra/potr_trace.h>
#include <cplat/compress/compress.h>

#include "thread_recv_slot.h"

/* Doxygen コメントは、ヘッダーに記載 */

void thread_recv_slot_init_ctx(thread_recv_slot *slot, potr_context *ctx)
{
    slot->ctx = ctx;
    slot->peer = NULL;
    slot->peer_id = POTR_PEER_NA;
    slot->pad = 0;
    slot->recv_window = &ctx->recv_window;
    slot->peer_session_id = &ctx->peer_session_id;
    slot->peer_session_ts = &ctx->peer_session_ts;
    slot->peer_session_known = &ctx->peer_session_known;
    slot->reorder_pending = &ctx->reorder_pending;
    slot->pending_fin = &ctx->pending_fin;
    slot->fin_target_seq = &ctx->fin_target_seq;
    slot->frag_buf = ctx->frag_buf;
    slot->frag_buf_len = &ctx->frag_buf_len;
    slot->frag_compressed = &ctx->frag_compressed;
    slot->health_alive = &ctx->health_alive;
    slot->last_recv_ts = &ctx->last_recv_ts;
    slot->path_last_recv_ts = ctx->path_last_recv_ts;
    slot->path_ping_state = ctx->path_ping_state;
}

/* Doxygen コメントは、ヘッダーに記載 */

void thread_recv_slot_init_peer(thread_recv_slot *slot, potr_context *ctx, potr_internal_peer_context *peer)
{
    slot->ctx = ctx;
    slot->peer = peer;
    slot->peer_id = peer->peer_id;
    slot->pad = 0;
    slot->recv_window = &peer->recv_window;
    slot->peer_session_id = &peer->peer_session_id;
    slot->peer_session_ts = &peer->peer_session_ts;
    slot->peer_session_known = &peer->peer_session_known;
    slot->reorder_pending = &peer->reorder_pending;
    slot->pending_fin = &peer->pending_fin;
    slot->fin_target_seq = &peer->fin_target_seq;
    slot->frag_buf = peer->frag_buf;
    slot->frag_buf_len = &peer->frag_buf_len;
    slot->frag_compressed = &peer->frag_compressed;
    slot->health_alive = &peer->health_alive;
    slot->last_recv_ts = &peer->last_recv_ts;
    slot->path_last_recv_ts = peer->path_last_recv_ts;
    slot->path_ping_state = peer->path_ping_state;
}

/* 受信データを展開してコールバックに渡す */
static void slot_recv_deliver(thread_recv_slot *slot, const uint8_t *payload, size_t payload_len, int compressed)
{
    potr_context *ctx = slot->ctx;

    if (compressed)
    {
        size_t dec_len = ctx->compress_buf_size;

        if (cplat_decompress(ctx->compress_buf, &dec_len, payload, payload_len) == CPLAT_OK)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: decompress %zu -> %zu bytes",
                       ctx->service.service_id, payload_len, dec_len);
            potr_internal_callback_emit(ctx, slot->peer_id, POTR_EVENT_DATA, ctx->compress_buf, dec_len);
        }
        else
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "recv[service_id=%" PRId64 "]: decompress failed (src_len=%zu)",
                       ctx->service.service_id, payload_len);
        }
    }
    else
    {
        potr_internal_callback_emit(ctx, slot->peer_id, POTR_EVENT_DATA, payload, payload_len);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void thread_recv_slot_deliver_payload_elem(thread_recv_slot *slot, const potr_packet *elem)
{
    potr_context *ctx = slot->ctx;

    /* 未接続状態では DATA を破棄する。接続確立前/DISCONNECTED 後の DATA が
       アプリに届かないようにする。CONNECTED 発火は health_alive=1 への遷移で行う。 */
    if (!*slot->health_alive)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: drop DATA elem while health_alive=0",
                   ctx->service.service_id);
        return;
    }

    if (elem->flags & POTR_FLAG_MORE_FRAG)
    {
        /* 中間フラグメント: バッファーに追記 */
        if (*slot->frag_buf_len + elem->payload_len <= ctx->global.max_message_size)
        {
            if (*slot->frag_buf_len == 0)
            {
                if (elem->flags & POTR_FLAG_COMPRESSED)
                {
                    *slot->frag_compressed = 1;
                }
                else
                {
                    *slot->frag_compressed = 0;
                }
            }
            memcpy(slot->frag_buf + *slot->frag_buf_len, elem->payload, elem->payload_len);
            *slot->frag_buf_len += elem->payload_len;
        }
        else
        {
            *slot->frag_buf_len = 0;
            *slot->frag_compressed = 0;
        }
    }
    else if (*slot->frag_buf_len > 0)
    {
        /* 最終フラグメント: バッファーに追記してコールバック */
        if (*slot->frag_buf_len + elem->payload_len <= ctx->global.max_message_size)
        {
            memcpy(slot->frag_buf + *slot->frag_buf_len, elem->payload, elem->payload_len);
            *slot->frag_buf_len += elem->payload_len;

            if (ctx->callback != NULL)
            {
                slot_recv_deliver(slot, slot->frag_buf, *slot->frag_buf_len, *slot->frag_compressed);
            }
        }
        *slot->frag_buf_len = 0;
        *slot->frag_compressed = 0;
    }
    else
    {
        /* フラグメントなし: 直接コールバック */
        if (ctx->callback != NULL)
        {
            int is_compressed;
            if (elem->flags & POTR_FLAG_COMPRESSED)
            {
                is_compressed = 1;
            }
            else
            {
                is_compressed = 0;
            }
            slot_recv_deliver(slot, elem->payload, (size_t)elem->payload_len, is_compressed);
        }
    }
}
