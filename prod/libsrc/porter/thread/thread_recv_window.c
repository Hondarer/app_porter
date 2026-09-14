/** @file thread_recv_window.c
 * @brief 受信ウィンドウへの投入、再送要求、順序整列を行います。
 */

#include <inttypes.h>
#include <string.h>

#include <cplat/base/platform.h>
#include <cplat/clock/timespec.h>
#include <cplat/crypto/crypto.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/endpoint.h>
#include <cplat/net/socket.h>
#include <cplat/sync/sync.h>
#include <porter/infra/potr_trace.h>
#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/seqnum.h>
#include <porter/protocol/window.h>
#include <porter/potr_context.h>

#include "thread_recv_fin.h"
#include "thread_recv_session.h"
#include "thread_recv_window.h"

/** NACK 重複抑制の時間窓 (ミリ秒)。この時間内の同一 ack_num の NACK は破棄する。 */
#define POTR_NACK_DEDUP_MS 200U

static int build_ctrl_pkt_wire(const potr_context *ctx, potr_packet *pkt, uint8_t *wire_buf, size_t *wire_len)
{
    if (ctx->service.encrypt_enabled)
    {
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t enc_out = POTR_CRYPTO_TAG_SIZE;

        pkt->flags |= cplat_hton16(POTR_FLAG_ENCRYPTED);
        pkt->payload_len = cplat_hton16((uint16_t)POTR_CRYPTO_TAG_SIZE);

        memcpy(nonce, &pkt->session_id, 4);
        memcpy(nonce + 4, &pkt->flags, 2);
        memcpy(nonce + 6, &pkt->ack_num, 4);
        memset(nonce + 10, 0, 2);

        memcpy(wire_buf, pkt, PACKET_HEADER_SIZE);
        if (cplat_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out, NULL, 0, ctx->service.encrypt_key, nonce, wire_buf,
                          PACKET_HEADER_SIZE) != CPLAT_OK)
        {
            /* cplat の暗号化失敗には、porter の分類へ変換できる詳細コードがありません。 */
            return POTR_ERR_UNKNOWN;
        }
        *wire_len = PACKET_HEADER_SIZE + enc_out;
        return POTR_OK;
    }

    memcpy(wire_buf, pkt, PACKET_HEADER_SIZE);
    *wire_len = potr_internal_packet_wire_size(pkt);
    return POTR_OK;
}

static void fill_own_session_hdr(const thread_recv_slot *slot, potr_internal_packet_session_hdr *shdr)
{
    shdr->service_id = slot->ctx->service.service_id;
    shdr->session_id = *slot->session_id;
    potr_session_ts_to_hdr(slot->session_ts, &shdr->session_tv_sec, &shdr->session_tv_nsec);
}

static void slot_send_wire_to_dests(thread_recv_slot *slot, const uint8_t *wire_buf, size_t wire_len)
{
    potr_context *ctx = slot->ctx;
    int i;

    if (slot->peer != NULL)
    {
        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            size_t sent = 0;

            if (potr_endpoint_is_unset(&slot->dest_addr[i]))
            {
                continue;
            }
            (void)cplat_socket_sendto(ctx->sock[i], wire_buf, wire_len, &slot->dest_addr[i], &sent, NULL);
        }
        return;
    }

    for (i = 0; i < ctx->n_path; i++)
    {
        size_t sent = 0;

        (void)cplat_socket_sendto(ctx->sock[i], wire_buf, wire_len, &slot->dest_addr[i], &sent, NULL);
    }
}

static void slot_send_ctrl(thread_recv_slot *slot, potr_packet *pkt)
{
    uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
    size_t wire_len;

    if (build_ctrl_pkt_wire(slot->ctx, pkt, wire_buf, &wire_len) != POTR_OK)
    {
        return;
    }

    slot_send_wire_to_dests(slot, wire_buf, wire_len);
}

static void slot_send_nack(thread_recv_slot *slot, uint32_t nack_seq)
{
    potr_context *ctx = slot->ctx;
    potr_packet nack_pkt;
    potr_internal_packet_session_hdr shdr;
    uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
    size_t wire_len;
    int i;

    fill_own_session_hdr(slot, &shdr);
    if (potr_internal_packet_build_nack(&nack_pkt, &shdr, nack_seq) != POTR_OK)
    {
        return;
    }

    if (slot->peer != NULL || ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
    {
        slot_send_ctrl(slot, &nack_pkt);
        return;
    }

    if (build_ctrl_pkt_wire(ctx, &nack_pkt, wire_buf, &wire_len) != POTR_OK)
    {
        return;
    }

    for (i = 0; i < ctx->n_path; i++)
    {
        cplat_ipv4_endpoint dest = {0};
        uint16_t port;
        size_t sent = 0;

        if (ctx->service.src_port != 0)
        {
            port = cplat_hton16(ctx->service.src_port);
        }
        else
        {
            port = ctx->peer_port[i];
        }

        if (port == 0)
        {
            continue;
        }

        dest.address = ctx->src_addr_resolved[i];
        dest.port = port;
        (void)cplat_socket_sendto(ctx->sock[i], wire_buf, wire_len, &dest, &sent, NULL);
    }
}

static void slot_send_reject(thread_recv_slot *slot, uint32_t seq_num)
{
    potr_packet reject_pkt;
    potr_internal_packet_session_hdr shdr;

    fill_own_session_hdr(slot, &shdr);
    if (potr_internal_packet_build_reject(&reject_pkt, &shdr, seq_num) != POTR_OK)
    {
        return;
    }

    slot_send_ctrl(slot, &reject_pkt);
}

static int reorder_gap_ready(thread_recv_slot *slot, uint32_t nack_num)
{
    potr_context *ctx = slot->ctx;
    cplat_timespec now_ts;
    uint32_t ms;

    if (ctx->global.reorder_timeout_ms == 0U)
    {
        return 1;
    }

    ms = ctx->global.reorder_timeout_ms;

    if (!*slot->reorder_pending || *slot->reorder_nack_num != nack_num)
    {
        uint32_t effective_ms;

        cplat_get_monotonic(&now_ts);
        effective_ms = ms;
        if (ctx->service.type == POTR_TYPE_MULTICAST || ctx->service.type == POTR_TYPE_BROADCAST)
        {
            effective_ms = ms + (uint32_t)((uint64_t)now_ts.tv_nsec % ms);
        }

        *slot->reorder_pending = 1;
        *slot->reorder_nack_num = nack_num;
        cplat_timespec_add_ms(&now_ts, (uint64_t)effective_ms, slot->reorder_deadline_ts);
        return 0;
    }

    cplat_get_monotonic(&now_ts);
    if (cplat_timespec_cmp(&now_ts, slot->reorder_deadline_ts) >= 0)
    {
        *slot->reorder_pending = 0;
        return 1;
    }

    return 0;
}

/* N:1 は現状リオーダー待機を持たないため常に 1 (即時処理) を返す。
   真偽値を返す述語のため共通結果コードの適用対象外。 */
static int slot_gap_ready(thread_recv_slot *slot, uint32_t nack_num)
{
    if (slot->peer != NULL)
    {
        return 1;
    }

    return reorder_gap_ready(slot, nack_num);
}

static void raw_session_disconnect(thread_recv_slot *slot)
{
    thread_recv_set_all_path_ping_states(slot->path_ping_state, POTR_MAX_PATH, POTR_PING_STATE_UNDEFINED);
    memset(slot->remote_path_ping_state, 0, POTR_MAX_PATH);
    thread_recv_disconnect_service_all_paths(slot->ctx);
    *slot->pending_fin = 0;
    *slot->fin_target_seq = 0;
    *slot->frag_buf_len = 0;
    *slot->frag_compressed = 0;
}

static void slot_drain_recv_window(thread_recv_slot *slot)
{
    potr_context *ctx = slot->ctx;
    potr_packet pop_pkt;

    while (potr_internal_window_recv_pop(slot->recv_window, &pop_pkt) == POTR_OK)
    {
        const char *pkt_type_str;
        if ((pop_pkt.flags & POTR_FLAG_PING) != 0)
        {
            pkt_type_str = "PING";
        }
        else
        {
            pkt_type_str = "DATA";
        }
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: pop seq=%u %s", ctx->service.service_id,
                   (unsigned)pop_pkt.seq_num, pkt_type_str);

        if ((pop_pkt.flags & POTR_FLAG_PING) != 0)
        {
            continue;
        }

        {
            size_t offset = 0;
            potr_packet elem;

            while (potr_internal_packet_unpack_next(&pop_pkt, &offset, &elem) == POTR_OK)
            {
                thread_recv_slot_deliver_payload_elem(slot, &elem);
            }
        }
    }

    {
        uint32_t fin_target_seq = 0U;

        if (thread_recv_fin_pending_reached(slot, &fin_target_seq) != 0)
        {
            thread_recv_fin_fire(slot, fin_target_seq);
        }
    }
}

static int nack_is_duplicate(thread_recv_slot *slot, uint32_t ack_num)
{
    uint64_t now_ms = cplat_get_monotonic_ms();
    int dedup_idx;

    for (dedup_idx = 0; dedup_idx < (int)POTR_NACK_DEDUP_SLOTS; dedup_idx++)
    {
        const potr_internal_nack_dedup_entry *entry = &slot->nack_dedup_buf[dedup_idx];
        if (entry->time_ms != 0 && entry->ack_num == ack_num &&
            (now_ms - entry->time_ms) < (uint64_t)POTR_NACK_DEDUP_MS)
        {
            return 1;
        }
    }

    slot->nack_dedup_buf[*slot->nack_dedup_next].ack_num = ack_num;
    slot->nack_dedup_buf[*slot->nack_dedup_next].time_ms = now_ms;
    *slot->nack_dedup_next = (uint8_t)((*slot->nack_dedup_next + 1U) % POTR_NACK_DEDUP_SLOTS);
    return 0;
}

static void slot_retransmit_or_reject(thread_recv_slot *slot, uint32_t ack_num)
{
    potr_context *ctx = slot->ctx;
    potr_packet resend_pkt;
    size_t wire_len = 0;
    int get_result;

    cplat_local_lock_lock(slot->send_window_mutex, CPLAT_SYNC_WAIT_FOREVER);
    get_result = potr_internal_window_send_get(slot->send_window, ack_num, &resend_pkt);
    if (get_result == POTR_OK)
    {
        wire_len = potr_internal_packet_wire_size(&resend_pkt);
        memcpy(ctx->recv_buf, &resend_pkt, PACKET_HEADER_SIZE);
        memcpy(ctx->recv_buf + PACKET_HEADER_SIZE, resend_pkt.payload, wire_len - PACKET_HEADER_SIZE);
    }
    cplat_local_lock_unlock(slot->send_window_mutex);

    if (get_result == POTR_OK)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: peer=%u NACK seq=%u -> retransmit",
                   ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)ack_num);
        slot_send_wire_to_dests(slot, ctx->recv_buf, wire_len);
    }
    else
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                   "recv[service_id=%" PRId64 "]: peer=%u NACK seq=%u not in window -> REJECT", ctx->service.service_id,
                   (unsigned)slot->peer_id, (unsigned)ack_num);
        slot_send_reject(slot, ack_num);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void thread_recv_window_accept_outer(thread_recv_slot *slot, const potr_packet *pkt, int path_idx)
{
    potr_context *ctx = slot->ctx;
    uint32_t nack_num;
    uint32_t stretch;
    int is_raw = potr_is_raw_type(ctx->service.type);

    if (potr_internal_window_recv_push(slot->recv_window, pkt) != POTR_OK)
    {
        if (is_raw)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "recv[service_id=%" PRId64 "]: RAW recv_window full, resetting to seq=%u",
                       ctx->service.service_id, (unsigned)pkt->seq_num);
            raw_session_disconnect(slot);
            potr_internal_window_recv_reset(slot->recv_window, pkt->seq_num);
            if (potr_internal_window_recv_push(slot->recv_window, pkt) != POTR_OK)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                           "recv[service_id=%" PRId64 "]: RAW window re-push failed seq=%u (bug)",
                           ctx->service.service_id, (unsigned)pkt->seq_num);
                return;
            }
        }
        else
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "recv[service_id=%" PRId64 "]: peer=%u recv_window full (100%%), dropping seq=%u"
                       " (base_seq=%u window_size=%u)",
                       ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)pkt->seq_num,
                       (unsigned)slot->recv_window->base_seq, (unsigned)slot->recv_window->window_size);
            return;
        }
    }

    stretch = (uint32_t)(pkt->seq_num - slot->recv_window->base_seq) + 1U;
    if (stretch * 10U >= (uint32_t)slot->recv_window->window_size * 8U)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                   "recv[service_id=%" PRId64 "]: peer=%u recv_window utilization high (%u/%u >= 80%%)"
                   " seq=%u base_seq=%u",
                   ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)stretch,
                   (unsigned)slot->recv_window->window_size, (unsigned)pkt->seq_num,
                   (unsigned)slot->recv_window->base_seq);
    }

    if (potr_internal_window_recv_needs_nack(slot->recv_window, &nack_num) == 0)
    {
        *slot->reorder_pending = 0;
    }
    else if (is_raw)
    {
        if (reorder_gap_ready(slot, nack_num) != 0)
        {
            raw_session_disconnect(slot);
            potr_internal_window_recv_reset(slot->recv_window, pkt->seq_num);
            if (potr_internal_window_recv_push(slot->recv_window, pkt) != POTR_OK)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "recv[service_id=%" PRId64 "]: RAW gap re-push failed seq=%u (bug)",
                           ctx->service.service_id, (unsigned)pkt->seq_num);
                return;
            }
        }
    }
    else if (slot_gap_ready(slot, nack_num) != 0)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: peer=%u NACK seq=%u",
                   ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)nack_num);
        slot_send_nack(slot, nack_num);
    }

    if (slot->peer == NULL && (pkt->flags & POTR_FLAG_DATA) != 0 && ctx->service.type != POTR_TYPE_UNICAST_BIDIR)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: DATA seq=%u updates health on path=%d",
                   ctx->service.service_id, (unsigned)pkt->seq_num, path_idx);
        (void)thread_recv_update_path_health(slot, path_idx);
        thread_recv_sync_path_state(slot);
    }

    slot_drain_recv_window(slot);

    if (potr_internal_window_recv_needs_nack(slot->recv_window, &nack_num) == 0)
    {
        *slot->reorder_pending = 0;
    }
    else if (!is_raw && slot_gap_ready(slot, nack_num) != 0)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: peer=%u NACK seq=%u (post-drain)",
                   ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)nack_num);
        slot_send_nack(slot, nack_num);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void thread_recv_window_on_reject(thread_recv_slot *slot, const potr_packet *pkt, int path_idx,
                                  const cplat_ipv4_endpoint *sender)
{
    potr_context *ctx = slot->ctx;

    if (potr_is_raw_type(ctx->service.type))
    {
        return;
    }

    if (thread_recv_session_adopt(slot, pkt) == 0)
    {
        return;
    }

    thread_recv_learn_sender_path(slot, path_idx, sender);

    if (slot->peer == NULL)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                   "recv[service_id=%" PRId64 "]: REJECT received seq=%u"
                   " (packet unrecoverable)",
                   ctx->service.service_id, (unsigned)pkt->ack_num);
    }

    thread_recv_set_all_path_ping_states(slot->path_ping_state, POTR_MAX_PATH, POTR_PING_STATE_UNDEFINED);
    memset(slot->remote_path_ping_state, 0, POTR_MAX_PATH);
    if (slot->peer != NULL)
    {
        thread_recv_disconnect_peer_all_paths(ctx, slot->peer);
    }
    else
    {
        thread_recv_disconnect_service_all_paths(ctx);
    }

    *slot->reorder_pending = 0;
    potr_internal_window_recv_skip(slot->recv_window, pkt->ack_num);
    slot_drain_recv_window(slot);
}

/* Doxygen コメントは、ヘッダーに記載 */

void thread_recv_window_on_nack(thread_recv_slot *slot, const potr_packet *pkt)
{
    if ((pkt->flags & POTR_FLAG_NACK) == 0)
    {
        return;
    }

    if (potr_is_raw_type(slot->ctx->service.type))
    {
        return;
    }

    if (nack_is_duplicate(slot, pkt->ack_num) != 0)
    {
        return;
    }

    slot_retransmit_or_reject(slot, pkt->ack_num);
}

/* Doxygen コメントは、ヘッダーに記載 */

void thread_recv_window_scan_ping_gap(thread_recv_slot *slot, const potr_packet *pkt)
{
    potr_context *ctx = slot->ctx;
    uint32_t next_seq = slot->recv_window->next_seq;

    if (slot->peer != NULL)
    {
        if (pkt->seq_num != next_seq &&
            potr_internal_seqnum_in_window(pkt->seq_num, next_seq + 1U, slot->recv_window->window_size) != 0)
        {
            if (slot_gap_ready(slot, next_seq) != 0)
            {
                slot_send_nack(slot, next_seq);
            }
        }
        return;
    }

    if (potr_is_raw_type(ctx->service.type))
    {
        if (pkt->seq_num != next_seq &&
            potr_internal_seqnum_in_window(pkt->seq_num, next_seq + 1U, slot->recv_window->window_size) != 0)
        {
            if (reorder_gap_ready(slot, next_seq) != 0)
            {
                raw_session_disconnect(slot);
                potr_internal_window_recv_reset(slot->recv_window, pkt->seq_num);
            }
        }
        else
        {
            *slot->reorder_pending = 0;
        }
        return;
    }

    {
        uint32_t scan_seq = next_seq;

        while (scan_seq != pkt->seq_num && potr_internal_seqnum_in_window(scan_seq, slot->recv_window->base_seq,
                                                                          slot->recv_window->window_size) != 0)
        {
            uint16_t idx = (uint16_t)((scan_seq - slot->recv_window->base_seq) % slot->recv_window->window_size);
            if (slot->recv_window->valid[idx] == 0)
            {
                if (reorder_gap_ready(slot, scan_seq) != 0)
                {
                    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: NACK seq=%u (PING gap scan)",
                               ctx->service.service_id, (unsigned)scan_seq);
                    slot_send_nack(slot, scan_seq);
                }
                else
                {
                    break;
                }
            }
            scan_seq++;
        }
        if (scan_seq == pkt->seq_num)
        {
            *slot->reorder_pending = 0;
        }
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void thread_recv_window_check_reorder_timeout(thread_recv_slot *slot)
{
    potr_context *ctx = slot->ctx;

    if (*slot->reorder_pending == 0)
    {
        return;
    }
    if (reorder_gap_ready(slot, *slot->reorder_nack_num) == 0)
    {
        return;
    }

    if (potr_is_raw_type(ctx->service.type))
    {
        raw_session_disconnect(slot);
        *slot->peer_session_known = 0;
        slot->last_recv_ts->tv_sec = 0;
        potr_internal_window_init(slot->recv_window, 0, ctx->global.window_size, ctx->global.max_payload);
    }
    else
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: NACK seq=%u (reorder timeout)",
                   ctx->service.service_id, (unsigned)*slot->reorder_nack_num);
        slot_send_nack(slot, *slot->reorder_nack_num);
    }
}
