/** @file thread_recv_fin.c
 * @brief pending FIN の判定と FIN による切断を行います。
 */

#include <inttypes.h>
#include <string.h>

#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/window.h>
#include <porter/infra/potr_tcp_control.h>
#include <porter/infra/potr_trace.h>
#include <cplat/sync/sync.h>

#include "thread_recv_fin.h"
#include "thread_recv_session.h"

static int recv_window_reached_fin_target(uint32_t next_seq, uint32_t fin_target_seq)
{
    return next_seq == fin_target_seq;
}

static int fin_packet_has_target(const potr_packet *pkt)
{
    return (pkt->flags & POTR_FLAG_FIN_TARGET_VALID) != 0;
}

static int send_tcp_fin_ack(potr_context *ctx, uint32_t fin_target_seq)
{
    potr_packet fin_ack_pkt;
    potr_internal_packet_session_hdr shdr;
    int result;

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = ctx->session_id;
    potr_session_ts_to_hdr(&ctx->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);

    result = potr_internal_packet_build_fin_ack(&fin_ack_pkt, &shdr, fin_target_seq);
    if (result != POTR_OK)
    {
        return result;
    }

    return potr_internal_tcp_send_control_packet(ctx, &fin_ack_pkt, fin_target_seq);
}

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_fin_on_packet(thread_recv_slot *slot, const potr_packet *pkt, uint32_t *fin_target_seq_out)
{
    if ((pkt->flags & POTR_FLAG_FIN) == 0)
    {
        return THREAD_RECV_FIN_IGNORED;
    }

    *fin_target_seq_out = pkt->ack_num;

    if (fin_packet_has_target(pkt) && !recv_window_reached_fin_target(slot->recv_window->next_seq, pkt->ack_num))
    {
        *slot->pending_fin = 1;
        *slot->fin_target_seq = pkt->ack_num;
        return THREAD_RECV_FIN_PENDING;
    }

    return THREAD_RECV_FIN_FIRE;
}

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_fin_pending_reached(const thread_recv_slot *slot, uint32_t *fin_target_seq_out)
{
    if (*slot->pending_fin && recv_window_reached_fin_target(slot->recv_window->next_seq, *slot->fin_target_seq))
    {
        *fin_target_seq_out = *slot->fin_target_seq;
        return 1;
    }

    return 0;
}

/* Doxygen コメントは、ヘッダーに記載 */

void thread_recv_fin_fire(thread_recv_slot *slot, uint32_t fin_target_seq)
{
    potr_context *ctx = slot->ctx;

    if (slot->peer != NULL)
    {
        thread_recv_disconnect_peer_all_paths(ctx, slot->peer);
        *slot->pending_fin = 0;
        *slot->fin_target_seq = 0;
        potr_internal_peer_free(ctx, slot->peer);
        return;
    }

    if (ctx->service.type == POTR_TYPE_TCP || ctx->service.type == POTR_TYPE_TCP_BIDIR)
    {
        if (send_tcp_fin_ack(ctx, fin_target_seq) == POTR_OK)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "tcp_recv[service_id=%" PRId64 "]: FIN_ACK sent ack=%u",
                       ctx->service.service_id, (unsigned)fin_target_seq);
        }
        else
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING, "tcp_recv[service_id=%" PRId64 "]: FIN_ACK send failed ack=%u",
                       ctx->service.service_id, (unsigned)fin_target_seq);
        }
    }

    if (potr_is_tcp_type(ctx->service.type))
    {
        cplat_local_lock_lock(ctx->tcp_state_mutex, CPLAT_SYNC_WAIT_FOREVER);
        thread_recv_set_all_path_ping_states(ctx->path_ping_state, POTR_MAX_PATH, POTR_PING_STATE_UNDEFINED);
        memset((void *)ctx->remote_path_ping_state, 0, sizeof(ctx->remote_path_ping_state));
        thread_recv_disconnect_service_all_paths(ctx);
        cplat_local_lock_unlock(ctx->tcp_state_mutex);
    }
    else
    {
        thread_recv_set_all_path_ping_states(ctx->path_ping_state, POTR_MAX_PATH, POTR_PING_STATE_UNDEFINED);
        memset((void *)ctx->remote_path_ping_state, 0, sizeof(ctx->remote_path_ping_state));
        thread_recv_disconnect_service_all_paths(ctx);
    }
    *slot->pending_fin = 0;
    *slot->fin_target_seq = 0;
    ctx->peer_session_known = 0;
    ctx->reorder_pending = 0;
    ctx->last_recv_ts.tv_sec = 0;
    cplat_local_lock_lock(ctx->recv_window_mutex, CPLAT_SYNC_WAIT_FOREVER);
    potr_internal_window_init(&ctx->recv_window, 0, ctx->global.window_size, ctx->global.max_payload);
    cplat_local_lock_unlock(ctx->recv_window_mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */

void thread_recv_fin_on_ack(potr_context *ctx, uint32_t ack_num)
{
    cplat_local_lock_lock(ctx->tcp_close_mutex, CPLAT_SYNC_WAIT_FOREVER);
    if (ctx->tcp_close_waiting_ack && ctx->tcp_close_wait_target_seq == ack_num && !ctx->tcp_close_ack_received)
    {
        ctx->tcp_close_ack_received = 1;
        ctx->tcp_close_ack_seq = ack_num;
        cplat_condvar_signal(ctx->tcp_close_cv);
    }
    cplat_local_lock_unlock(ctx->tcp_close_mutex);
}
