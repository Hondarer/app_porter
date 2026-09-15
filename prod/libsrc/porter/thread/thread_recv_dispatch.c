/** @file thread_recv_dispatch.c
 * @brief 構成済み受信スロットへパケット種別を振り分けます。
 */

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <porter/porter_const.h>
#include <porter/protocol/packet.h>
#include <porter/potr_context.h>
#include <porter/infra/potr_trace.h>
#include <porter/thread/potr_health_thread.h>

#include "thread_recv_dispatch.h"
#include "thread_recv_fin.h"
#include "thread_recv_session.h"
#include "thread_recv_window.h"

/* Doxygen コメントは、ヘッダーに記載 */

void thread_recv_apply_remote_path_ping_state(uint8_t *dst, const uint8_t *src, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        if (src[i] != POTR_PING_STATE_UNDEFINED)
        {
            dst[i] = src[i];
        }
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void thread_recv_wake_udp_interrupt_ping(potr_context *ctx, int state_changed)
{
    if (state_changed != 0 && ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
    {
        potr_internal_health_thread_wake(ctx);
    }
}

static int dispatch_fin(thread_recv_slot *slot, const potr_packet *pkt)
{
    potr_context *ctx = slot->ctx;
    uint32_t fin_target_seq = 0U;
    int fin_action;

    if (slot->peer == NULL)
    {
        if (thread_recv_session_adopt(slot, pkt) == 0)
        {
            return 0;
        }
    }

    POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
               "recv[service_id=%" PRId64 "]: peer=%u FIN received (fin_target_seq=%u recv_next=%u)",
               ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)pkt->ack_num,
               (unsigned)slot->recv_window->next_seq);

    fin_action = thread_recv_fin_on_packet(slot, pkt, &fin_target_seq);
    if (fin_action == THREAD_RECV_FIN_PENDING)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "recv[service_id=%" PRId64 "]: peer=%u FIN pending (waiting for seq=%u)",
                   ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)pkt->ack_num);
        return 0;
    }

    if (fin_action == THREAD_RECV_FIN_FIRE)
    {
        thread_recv_fin_fire(slot, fin_target_seq);
    }

    return 0;
}

static int dispatch_ping(thread_recv_slot *slot, const potr_packet *pkt, int path_idx)
{
    potr_context *ctx = slot->ctx;
    int ping_state_changed = thread_recv_update_path_health(slot, path_idx);

    if (pkt->payload_len >= POTR_MAX_PATH && pkt->payload != NULL)
    {
        thread_recv_apply_remote_path_ping_state(slot->remote_path_ping_state, pkt->payload, POTR_MAX_PATH);
    }

    if (potr_is_raw_type(ctx->service.type))
    {
        thread_recv_window_scan_ping_gap(slot, pkt);
        thread_recv_sync_path_state(slot);
        return 0;
    }

    if (slot->peer == NULL)
    {
        thread_recv_wake_udp_interrupt_ping(ctx, ping_state_changed);
    }
    thread_recv_sync_path_state(slot);
    thread_recv_window_scan_ping_gap(slot, pkt);

    if (slot->peer != NULL)
    {
        return ping_state_changed;
    }

    return 0;
}

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_dispatch_packet(thread_recv_slot *slot, potr_packet *pkt, int path_idx,
                                const cplat_ipv4_endpoint *sender)
{
    potr_context *ctx = slot->ctx;

    if ((pkt->flags & POTR_FLAG_FIN) != 0)
    {
        return dispatch_fin(slot, pkt);
    }

    if ((pkt->flags & POTR_FLAG_NACK) != 0)
    {
        if (slot->peer != NULL)
        {
            thread_recv_window_on_nack(slot, pkt);
        }
        return 0;
    }

    if ((pkt->flags & POTR_FLAG_REJECT) != 0)
    {
        thread_recv_window_on_reject(slot, pkt, path_idx, sender);
        return 0;
    }

    if ((pkt->flags & (POTR_FLAG_DATA | POTR_FLAG_PING)) == 0)
    {
        return 0;
    }

    if (thread_recv_session_adopt(slot, pkt) == 0)
    {
        return 0;
    }

    thread_recv_learn_sender_path(slot, path_idx, sender);

    {
        const char *pkt_kind_str;
        if ((pkt->flags & POTR_FLAG_PING) != 0)
        {
            pkt_kind_str = "PING";
        }
        else
        {
            pkt_kind_str = "DATA";
        }
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: peer=%u %s seq=%u path=%d",
                   ctx->service.service_id, (unsigned)slot->peer_id, pkt_kind_str, (unsigned)pkt->seq_num, path_idx);
    }

    if ((pkt->flags & POTR_FLAG_PING) != 0)
    {
        return dispatch_ping(slot, pkt, path_idx);
    }

    thread_recv_window_accept_outer(slot, pkt, path_idx);
    return 0;
}
