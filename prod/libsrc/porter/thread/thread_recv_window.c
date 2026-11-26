/**
 *******************************************************************************
 *  @file           thread_recv_window.c
 *  @brief          受信ウィンドウへの投入、再送要求、順序整列を行います。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include <cplat/base/platform.h>
#include <porter/infra/potr_send_queue.h>
#include <cplat/clock/timespec.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/endpoint.h>
#include <cplat/net/socket.h>
#include <cplat/sync/sync.h>
#include <porter/infra/potr_socket_io.h>
#include <porter/infra/potr_trace.h>
#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/packet_seal.h>
#include <porter/protocol/seqnum.h>
#include <porter/protocol/window.h>
#include <porter/potr_context.h>

#include "thread_recv_fin.h"
#include "thread_recv_session.h"
#include "thread_recv_window.h"

/** NACK 重複抑制の時間窓 [ms]。この時間内の同一 ack_num の NACK は破棄する。 */
#define POTR_NACK_DEDUP_MS 200U

static_assert(POTR_NACK_DEDUP_MS > 0U && POTR_NACK_DEDUP_MS <= POTR_WAIT_MS_MAX,
              "porter: POTR_NACK_DEDUP_MS out of range");

/**
 * 受信ウィンドウの使用率の警告のしきい値を表す分数の分子と分母。
 * 使用率が NACK_STRETCH_WARN_NUM / NACK_STRETCH_WARN_DEN 以上の場合に警告する。
 */
#define NACK_STRETCH_WARN_NUM 8U
#define NACK_STRETCH_WARN_DEN 10U

static_assert(NACK_STRETCH_WARN_NUM >= 1U && NACK_STRETCH_WARN_NUM <= NACK_STRETCH_WARN_DEN,
              "porter: recv_window utilization threshold must be in (0, 1]");
static_assert((uint64_t)POTR_MAX_WINDOW_SIZE * NACK_STRETCH_WARN_DEN <= UINT32_MAX,
              "porter: recv_window utilization comparison must fit in uint32_t");

static void fill_own_session_hdr(const thread_recv_slot *slot, potr_internal_packet_session_hdr *shdr);
static void slot_send_packet(thread_recv_slot *slot, const potr_packet *packet);
static void slot_send_nack(thread_recv_slot *slot, uint32_t nack_seq);
static void slot_send_reject(thread_recv_slot *slot, uint32_t seq_num);
static int reorder_gap_ready(thread_recv_slot *slot, uint32_t nack_num);
static int slot_gap_ready(thread_recv_slot *slot, uint32_t nack_num);
static void raw_session_disconnect(thread_recv_slot *slot);
static int slot_flow_overflow_disconnect(thread_recv_slot *slot);
static int slot_drain_recv_window(thread_recv_slot *slot, int *state_changed);
static int nack_is_duplicate(thread_recv_slot *slot, uint32_t ack_num);
static void slot_retransmit_or_reject(thread_recv_slot *slot, uint32_t ack_num);

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_window_accept_outer(thread_recv_slot *slot, const potr_packet *pkt, int path_idx)
{
    potr_context *ctx = slot->ctx;
    uint32_t nack_num;
    uint32_t stretch;
    int is_raw = is_raw_type(ctx->service.type);
    int push_ret;
    int state_changed = 0;

    /* 受信ウィンドウへ投入する。 */
    push_ret = potr_internal_window_recv_push(slot->recv_window, pkt);
    if (push_ret != POTR_OK)
    {
        /*
         * RAW は再送を行わないため、ウィンドウが満杯になった時点でセッション切断として扱う。
         * 到着した通番を基点に受信ウィンドウをリセットし、このパケットを再投入する。
         */
        if (is_raw)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "recv[service_id=%" PRId64 "]: RAW recv_window full, resetting to seq=%u",
                       ctx->service.service_id, (unsigned)pkt->seq_num);
            raw_session_disconnect(slot);
            potr_internal_window_recv_reset(slot->recv_window, pkt->seq_num);

            /* リセット直後の再投入は常に成功するはずであり、失敗時は不具合としてログに記録して戻る。 */
            if (potr_internal_window_recv_push(slot->recv_window, pkt) != POTR_OK)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                           "recv[service_id=%" PRId64 "]: RAW window re-push failed seq=%u (bug)",
                           ctx->service.service_id, (unsigned)pkt->seq_num);
                return 0;
            }
        }
        else
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "recv[service_id=%" PRId64 "]: peer=%u seq=%u out of recv_window, dropping"
                       " (base_seq=%u window_size=%u)",
                       ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)pkt->seq_num,
                       (unsigned)slot->recv_window->base_seq, (unsigned)slot->recv_window->window_size);

            /*
             * 受信ウィンドウより先の通番が届いた時点で、next_seq から先が欠落している。
             * パケットを捨てたままにせず next_seq の NACK を送り、送信側に再送または REJECT 判定を促す。
             */
            if (push_ret == POTR_ERR_OUT_OF_WINDOW &&
                (int32_t)(pkt->seq_num - slot->recv_window->base_seq) >= (int32_t)slot->recv_window->window_size)
            {
                uint32_t next_seq = slot->recv_window->next_seq;

                if (slot_gap_ready(slot, next_seq) != 0)
                {
                    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                               "recv[service_id=%" PRId64 "]: peer=%u NACK seq=%u (out of window)",
                               ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)next_seq);
                    slot_send_nack(slot, next_seq);
                }
            }

            return 0;
        }
    }

    /*
     * 使用率が 80 [%] 以上なら警告する。
     * stretch は base_seq から到着した通番までの幅であり、浮動小数点演算を避けて、
     * 分母と分子をそれぞれ掛けて比較する。
     */
    stretch = (uint32_t)(pkt->seq_num - slot->recv_window->base_seq) + 1U;
    if (stretch * NACK_STRETCH_WARN_DEN >= (uint32_t)slot->recv_window->window_size * NACK_STRETCH_WARN_NUM)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                   "recv[service_id=%" PRId64 "]: peer=%u recv_window utilization high (%u/%u >= 80%%)"
                   " seq=%u base_seq=%u",
                   ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)stretch,
                   (unsigned)slot->recv_window->window_size, (unsigned)pkt->seq_num,
                   (unsigned)slot->recv_window->base_seq);
    }

    /*
     * 欠番の有無を判定し、欠番が解消していればリオーダー待機を解除する。
     * RAW は NACK を送らず、待機期限を超えた欠番を切断として扱い、到着した通番でウィンドウをリセットする。
     */
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
                return 0;
            }
        }
    }
    else if (slot_gap_ready(slot, nack_num) != 0)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: peer=%u NACK seq=%u",
                   ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)nack_num);
        slot_send_nack(slot, nack_num);
    }

    /*
     * 片方向の通信種別では、有効な DATA の受信も経路の生存を示す。
     * 双方向 UDP は PING 応答で経路を判定するため、ここでは更新しない。
     */
    if (slot->peer == NULL && (pkt->flags & POTR_FLAG_DATA) != 0 && ctx->service.type != POTR_TYPE_UNICAST_BIDIR)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: DATA seq=%u updates health on path=%d",
                   ctx->service.service_id, (unsigned)pkt->seq_num, path_idx);
        /*
         * 結果は受信状態が変化したかどうかであり、直後の thread_recv_sync_path_state が変化の有無によらず状態を
         * 反映するため、戻り値は明示的に破棄する。
         */
        (void)thread_recv_update_path_health(slot, path_idx);
        thread_recv_sync_path_state(slot);
    }

    /* 整列済みのデータを配信する。N:1 で peer が解放された場合は slot を参照せずに戻る。 */
    {
        int drain_state_changed = 0;

        if (slot_drain_recv_window(slot, &drain_state_changed) == 0)
        {
            return 0;
        }
        state_changed |= drain_state_changed;
    }

    if (state_changed != 0)
    {
        if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR || ctx->service.type == POTR_TYPE_UNICAST_BIDIR_N1)
        {
            return 1;
        }
        return 0;
    }

    /*
     * 配信によって base_seq が進むと、後続の欠番が新たな先頭になることがある。
     * 次のパケット受信を待たずに再判定し、後続の欠番に対する NACK 送信を遅らせない。
     */
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

    return 0;
}

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_window_on_reject(thread_recv_slot *slot, const potr_packet *pkt, int path_idx,
                                 const cplat_ipv4_endpoint *sender)
{
    potr_context *ctx = slot->ctx;
    uint32_t next_seq;

    /* RAW は NACK を送らないため、REJECT を受ける状況がない。 */
    if (is_raw_type(ctx->service.type))
    {
        return 0;
    }

    if (*slot->peer_session_known == 0)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                   "recv[service_id=%" PRId64 "]: peer=%u REJECT ignored seq=%u (session unknown)",
                   ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)pkt->ack_num);
        return 0;
    }

    if (thread_recv_session_adopt(slot, pkt) == 0)
    {
        return 0;
    }

    /*
     * 一対多では他の受信者向けの REJECT も届き、再送要求の反復で同じ REJECT が複数回届くこともある。
     * 自スロットの next_seq より前または受信ウィンドウ内で既に受信済みの通番は無視し、不要な切断を防ぐ。
     */
    next_seq = slot->recv_window->next_seq;
    if ((int32_t)(pkt->ack_num - next_seq) < 0 ||
        potr_internal_window_recv_has_packet(slot->recv_window, pkt->ack_num) != 0)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                   "recv[service_id=%" PRId64 "]: peer=%u REJECT ignored seq=%u (next_seq=%u)",
                   ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)pkt->ack_num, (unsigned)next_seq);
        return 0;
    }

    thread_recv_learn_sender_path(slot, path_idx, sender);

    if (slot->peer != NULL)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                   "recv[service_id=%" PRId64 "]: peer=%u REJECT received seq=%u (packet unrecoverable)",
                   ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)pkt->ack_num);
    }
    else
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                   "recv[service_id=%" PRId64 "]: REJECT received seq=%u (packet unrecoverable)",
                   ctx->service.service_id, (unsigned)pkt->ack_num);
    }

    /* 保留中の FIN は欠番が埋まらなければ目標通番へ到達しないため、直ちに FIN を発火して終了する。 */
    if (*slot->pending_fin != 0)
    {
        thread_recv_fin_fire(slot, *slot->fin_target_seq);
        return 0;
    }

    /*
     * 1 通番ずつのスキップでは大きく遅れた受信側が送信側の保持範囲へ追い付けない。
     * 全経路の切断を通知したうえで受信状態を破棄し、次に届く DATA または PING の通番で受信ウィンドウを再初期化する。
     * last_recv_ts は送信側の生存を示すため維持する。
     * 戻り値は双方向 UDP で割り込み PING を要求するかどうかを表す。
     */
    int state_changed = thread_recv_set_all_path_ping_states(slot->path_ping_state, POTR_MAX_PATH,
                                                             POTR_PING_STATE_UNDEFINED);
    memset(slot->remote_path_ping_state, 0, POTR_MAX_PATH);

    if (slot->peer != NULL)
    {
        thread_recv_disconnect_peer_all_paths(ctx, slot->peer);
    }
    else
    {
        thread_recv_disconnect_service_all_paths(ctx);
    }

    potr_internal_window_recv_reset(slot->recv_window, 0U);
    *slot->frag_buf_len = 0U;
    *slot->frag_count = 0U;
    *slot->frag_compressed = 0;
    *slot->reorder_pending = 0;
    *slot->peer_session_known = 0;

    if (state_changed != 0 && (ctx->service.type == POTR_TYPE_UNICAST_BIDIR ||
                               ctx->service.type == POTR_TYPE_UNICAST_BIDIR_N1))
    {
        return 1;
    }

    return 0;
}

/* Doxygen コメントは、ヘッダーに記載 */

void thread_recv_window_on_nack(thread_recv_slot *slot, const potr_packet *pkt)
{
    if ((pkt->flags & POTR_FLAG_NACK) == 0)
    {
        return;
    }

    if (is_raw_type(slot->ctx->service.type))
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

    /*
     * PING の seq_num は、送信側が次に割り当てる DATA の通番を示す。
     * 受信側の next_seq がそれより前にあれば、その間の DATA が届いていない。
     */
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

    if (is_raw_type(ctx->service.type))
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

    /*
     * 1:1 の通常モードでは、next_seq から PING の seq_num の手前まで未着の通番を順に走査して NACK を送る。
     * 待機期限に達していない欠番に当たった時点で走査を打ち切り、残りは期限後の判定に回す。
     * 既に next_seq が進んでいる古い PING では走査しない。
     */
    if (pkt->seq_num == next_seq)
    {
        *slot->reorder_pending = 0;
        return;
    }
    if ((int32_t)(pkt->seq_num - next_seq) < 0)
    {
        return;
    }

    {
        uint32_t scan_seq = next_seq;

        while (scan_seq != pkt->seq_num && potr_internal_seqnum_in_window(scan_seq, slot->recv_window->base_seq,
                                                                          slot->recv_window->window_size) != 0)
        {
            if (potr_internal_window_recv_has_packet(slot->recv_window, scan_seq) == 0)
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

        /* 打ち切らずに最後まで走査した場合は、待機中の欠番が残っていない。 */
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

    /*
     * RAW は待機期限を超えた欠番を切断として扱う。タイムアウト時点では新しい基点通番にする受信パケットがないため、
     * peer_session_known を 0 (未知) に戻し、次に到着したパケットからセッションを再開する。
     */
    if (is_raw_type(ctx->service.type))
    {
        raw_session_disconnect(slot);
        *slot->peer_session_known = 0;
        slot->last_recv_ts->tv_sec = 0;
        /*
         * サービスまたはピアを作る時点で同じ window_size と max_payload で確保したウィンドウの状態をリセットするだけで
         * 再確保が起きないため、戻り値は明示的に破棄する。
         */
        (void)potr_internal_window_init(slot->recv_window, 0, ctx->global.window_size, ctx->global.max_payload);
    }
    else
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: NACK seq=%u (reorder timeout)",
                   ctx->service.service_id, (unsigned)*slot->reorder_nack_num);
        slot_send_nack(slot, *slot->reorder_nack_num);
    }
}

static void fill_own_session_hdr(const thread_recv_slot *slot, potr_internal_packet_session_hdr *shdr)
{
    shdr->service_id = slot->ctx->service.service_id;
    shdr->session_id = *slot->session_id;
    session_ts_to_hdr(slot->session_ts, &shdr->session_tv_sec, &shdr->session_tv_nsec);
}

static void slot_send_packet(thread_recv_slot *slot, const potr_packet *packet)
{
    potr_context *ctx = slot->ctx;
    potr_internal_tx_session *session = &ctx->tx_session;
    int count = ctx->n_path;
    uint8_t paths = UINT8_MAX;
    int i;

    if (slot->peer != NULL)
    {
        session = &slot->peer->tx_session;
        count = (int)POTR_MAX_PATH;
    }
    if ((cplat_ntoh16(packet->flags) & (POTR_FLAG_DATA | POTR_FLAG_REJECT)) != 0U)
    {
        potr_internal_path_group_result selection;

        if (potr_internal_path_group_select(ctx, slot->peer, &selection) != POTR_OK)
        {
            return;
        }
        paths = selection.selected_paths;
    }
    for (i = 0; i < count; i++)
    {
        size_t wire_len = POTR_PACKET_HEADER_SIZE + ctx->global.max_payload;

        if ((paths & (uint8_t)(1U << i)) == 0U ||
            (slot->peer != NULL && endpoint_is_unset(&slot->dest_addr[i])))
        {
            continue;
        }
        if (potr_internal_packet_seal_for_context(ctx, session, packet, ctx->recv_buf, &wire_len) != POTR_OK)
        {
            return;
        }
        /* UDP の送出は届く保証がなく、失われた NACK、REJECT、再送 DATA は相手の再要求で回復するため、戻り値は明示的に破棄する。 */
        (void)potr_internal_udp_sendto(ctx, i, ctx->recv_buf, wire_len, &slot->dest_addr[i], NULL);
    }
}

static void slot_send_nack(thread_recv_slot *slot, uint32_t nack_seq)
{
    potr_context *ctx = slot->ctx;
    potr_packet nack_pkt;
    potr_internal_packet_session_hdr shdr;
    uint8_t wire_buf[POTR_PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
    size_t wire_len;
    int i;

    fill_own_session_hdr(slot, &shdr);

    if (potr_internal_packet_build_nack(&nack_pkt, &shdr, nack_seq) != POTR_OK)
    {
        return;
    }

    /* N:1 と POTR_TYPE_UNICAST_BIDIR は、slot->dest_addr に設定済みの送信先へ返送する。 */
    if (slot->peer != NULL || ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
    {
        slot_send_packet(slot, &nack_pkt);
        return;
    }

    /*
     * 片方向の通信種別では slot->dest_addr を使わないため、送信元アドレス (src_addr_resolved) へ NACK を返送する。
     * 返送先のポートは設定値の src_port を優先し、0 なら受信時に学習した peer_port を使う。
     * どちらも未確定の経路には返送できないためスキップする。
     */
    for (i = 0; i < ctx->n_path; i++)
    {
        cplat_ipv4_endpoint dest = {0};
        uint16_t port;

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
        if (dest.address == 0U)
        {
            dest.address = ctx->peer_address[i];
        }
        dest.port = port;
        wire_len = sizeof(wire_buf);
        if (potr_internal_packet_seal_for_context(ctx, &ctx->tx_session, &nack_pkt, wire_buf, &wire_len) != POTR_OK)
        {
            return;
        }
        /* NACK が届かない場合は、欠番が残る間の再判定で送り直すため、戻り値は明示的に破棄する。 */
        (void)potr_internal_udp_sendto(ctx, i, wire_buf, wire_len, &dest, NULL);
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

    slot_send_packet(slot, &reject_pkt);
}

static int reorder_gap_ready(thread_recv_slot *slot, uint32_t nack_num)
{
    potr_context *ctx = slot->ctx;
    cplat_timespec now_ts;
    uint32_t ms;

    if (ctx->global.reorder_timeout_ms == 0U)
    {
        return 1; /* 待機しない設定では、欠番を検出した時点で処理する */
    }

    ms = ctx->global.reorder_timeout_ms;

    /*
     * 欠番の初回検出時、または待機中と異なる欠番に変わった時点では、待機期限を設定して 0 (待機継続) を返す。
     * 期限前に欠番が埋まれば、呼び出し側が reorder_pending を解除する。
     */
    if (!*slot->reorder_pending || *slot->reorder_nack_num != nack_num)
    {
        uint32_t effective_ms;

        if (cplat_clock_get_monotonic(&now_ts) != CPLAT_OK)
        {
            cplat_atomic_store_i32(&slot->ctx->fatal_error, POTR_ERR_IO, CPLAT_MEMORY_ORDER_RELEASE);
            potr_internal_send_queue_abort(&slot->ctx->send_queue, POTR_ERR_IO);
            return 0;
        }

        effective_ms = ms;

        /*
         * マルチキャストとブロードキャストでは、複数の受信者が同じ欠番をほぼ同時に検出しやすい。
         * 送信者への NACK 集中 (NACK implosion) を抑えるため、取得済みの現在時刻のナノ秒部をジッターとして加算する。
         */
        if (ctx->service.type == POTR_TYPE_MULTICAST || ctx->service.type == POTR_TYPE_BROADCAST)
        {
            effective_ms = ms + (uint32_t)((uint64_t)now_ts.tv_nsec % ms); /* ms 以上 2 * ms 未満 */
        }

        if (cplat_timespec_add_ms(&now_ts, (uint64_t)effective_ms, slot->reorder_deadline_ts) != CPLAT_OK)
        {
            *slot->reorder_pending = 0;
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "recv: cannot calculate reorder deadline");
            return 1;
        }
        *slot->reorder_pending = 1;
        *slot->reorder_nack_num = nack_num;
        return 0;
    }

    if (cplat_clock_get_monotonic(&now_ts) != CPLAT_OK)
    {
        cplat_atomic_store_i32(&slot->ctx->fatal_error, POTR_ERR_IO, CPLAT_MEMORY_ORDER_RELEASE);
        potr_internal_send_queue_abort(&slot->ctx->send_queue, POTR_ERR_IO);
        return 0;
    }

    if (cplat_timespec_cmp(&now_ts, slot->reorder_deadline_ts) >= 0)
    {
        *slot->reorder_pending = 0;
        return 1;
    }

    return 0;
}

/*
 * N:1 は現状リオーダー待機を持たないため常に 1 (即時処理) を返す。
 * 真偽値を返す述語のため共通結果コードの適用対象外とする。
 */
static int slot_gap_ready(thread_recv_slot *slot, uint32_t nack_num)
{
    if (slot->peer != NULL)
    {
        return 1;
    }

    return reorder_gap_ready(slot, nack_num);
}

/*
 * RAW の欠番や満杯によるセッション切断時に、経路の疎通状態を未定義へ戻して切断を通知する。
 * 切断前の未完了フラグメントや保留中の FIN がリセット後のデータと混ざらないよう、あわせて破棄する。
 */
static void raw_session_disconnect(thread_recv_slot *slot)
{
    /* 結果は失敗ではなく経路の状態が変わったかを表し、この経路では割り込みの PING を送らないため、戻り値は明示的に破棄する。 */
    (void)thread_recv_set_all_path_ping_states(slot->path_ping_state, POTR_MAX_PATH, POTR_PING_STATE_UNDEFINED);
    memset(slot->remote_path_ping_state, 0, POTR_MAX_PATH);
    thread_recv_disconnect_service_all_paths(slot->ctx);
    *slot->pending_fin = 0;
    *slot->fin_target_seq = 0;
    *slot->frag_buf_len = 0;
    *slot->frag_count = 0U;
    *slot->frag_compressed = 0;
}

/*
 * 通知キューの容量を超える DATA を破棄した相手との通信を切断する。
 * 全経路の切断を通知したうえで受信状態を破棄する。登録済みの DATA は通知キューに残り、切断の通知より先に渡る。
 * 占有量が解除のしきい値を下回るまでは、thread_recv_dispatch_packet() がこの相手の DATA と PING を破棄し、
 * 新しい通信として採用しない。切断と接続の通知が繰り返されることを避けるためである。
 * last_recv_ts は送信側の生存を示すため維持する。
 */
static int slot_flow_overflow_disconnect(thread_recv_slot *slot)
{
    potr_context *ctx = slot->ctx;
    int state_changed = thread_recv_set_all_path_ping_states(slot->path_ping_state, POTR_MAX_PATH,
                                                            POTR_PING_STATE_UNDEFINED);

    memset(slot->remote_path_ping_state, 0, POTR_MAX_PATH);

    if (slot->peer != NULL)
    {
        thread_recv_disconnect_peer_all_paths(ctx, slot->peer);
    }
    else
    {
        thread_recv_disconnect_service_all_paths(ctx);
    }

    potr_internal_window_recv_reset(slot->recv_window, 0U);
    *slot->pending_fin = 0;
    *slot->fin_target_seq = 0U;
    *slot->frag_buf_len = 0U;
    *slot->frag_count = 0U;
    *slot->frag_compressed = 0;
    *slot->reorder_pending = 0;
    *slot->peer_session_known = 0;
    slot->flow_overflow = 0;

    return state_changed;
}

/* pending FIN の発火で peer が解放された場合は 0、それ以外は 1 を返す。状態変化は別に出力する。 */
static int slot_drain_recv_window(thread_recv_slot *slot, int *state_changed)
{
    potr_context *ctx = slot->ctx;
    potr_packet pop_pkt;

    *state_changed = 0;

    /* 先頭から通番が連続する受信済みパケットを順に取り出す。未着の通番に達した時点で停止する。 */
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

        /* PING は受信ウィンドウの通番を進めるだけで、利用者へ配信するペイロードを持たない。 */
        if ((pop_pkt.flags & POTR_FLAG_PING) != 0)
        {
            continue;
        }

        /* 1 つの外側パケットに格納された複数のペイロード エレメントを順に配信する。 */
        {
            size_t offset = 0;
            potr_packet elem;

            slot->packet_expanded_size = 0U;
            while (potr_internal_packet_unpack_next(&pop_pkt, &offset, &elem) == POTR_OK)
            {
                thread_recv_slot_deliver_payload_elem(slot, &elem);
            }
        }

        /* 通知キューの容量を超えた場合は、残りの受信済みパケットを配信せずに切断する。 */
        if (slot->flow_overflow != 0)
        {
            *state_changed = slot_flow_overflow_disconnect(slot);
            return 1;
        }

        /*
         * 順序どおりに受信した DATA パケットを 1 個処理するたびに、送出上限の更新と、
         * 送出上限まで送り終えた相手への通知を判定する。フラグメントの途中のパケットも対象とする。
         */
        potr_internal_flow_control_on_received(ctx, slot->peer_id, *slot->peer_session_id, slot->peer_session_ts,
                                               pop_pkt.seq_num + 1U, 1, slot->packet_expanded_size);
    }

    {
        uint32_t fin_target_seq = 0U;

        /* 保留中の FIN は、目標通番までのデータをすべて配信し終えてから発火する。 */
        if (thread_recv_fin_pending_reached(slot, &fin_target_seq) != 0)
        {
            int peer_will_be_freed = (slot->peer != NULL); /* 発火で peer が解放されるため先に判定を控える */

            thread_recv_fin_fire(slot, fin_target_seq);

            if (peer_will_be_freed != 0)
            {
                return 0;
            }
        }
    }

    return 1;
}

static int nack_is_duplicate(thread_recv_slot *slot, uint32_t ack_num)
{
    uint64_t now_ms;

    if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
    {
        cplat_atomic_store_i32(&slot->ctx->fatal_error, POTR_ERR_IO, CPLAT_MEMORY_ORDER_RELEASE);
        potr_internal_send_queue_abort(&slot->ctx->send_queue, POTR_ERR_IO);
        return 1;
    }

    int dedup_idx;

    /*
     * マルチキャストやブロードキャストでは複数の受信者から同じ ack_num の NACK が届き、
     * 同一受信者も再送完了前に NACK を繰り返すことがある。
     * POTR_NACK_DEDUP_MS 内の同一 ack_num は重複と判定する。time_ms == 0 は未使用要素を表す。
     */
    for (dedup_idx = 0; dedup_idx < (int)POTR_NACK_DEDUP_SLOTS; dedup_idx++)
    {
        const potr_internal_nack_dedup_entry *entry = &slot->nack_dedup_buf[dedup_idx];
        if (entry->time_ms != 0 && entry->ack_num == ack_num &&
            (now_ms - entry->time_ms) < (uint64_t)POTR_NACK_DEDUP_MS)
        {
            return 1;
        }
    }

    /* 重複でなければ循環バッファーへ記録し、最も古い要素から順に上書きする。 */
    slot->nack_dedup_buf[*slot->nack_dedup_next].ack_num = ack_num;
    slot->nack_dedup_buf[*slot->nack_dedup_next].time_ms = now_ms;
    *slot->nack_dedup_next = (uint8_t)((*slot->nack_dedup_next + 1U) % POTR_NACK_DEDUP_SLOTS);

    return 0;
}

static void slot_retransmit_or_reject(thread_recv_slot *slot, uint32_t ack_num)
{
    potr_context *ctx = slot->ctx;
    potr_packet resend_pkt;
    int get_result;

    /*
     * 送信ウィンドウは送信スレッドと共有する。送信スレッドを待たせず、かつ解放後の上書きも防ぐため、
     * ロック中は受信スレッド専用の復号バッファーへ平文をコピーし、ロック解放後に封緘して送信する。
     * NACK は平文ペイロードを持たないため、復号バッファーの再利用は受信内容と競合しない。
     */
    cplat_local_lock_lock(slot->send_window_mutex);
    get_result = potr_internal_window_send_get(slot->send_window, ack_num, &resend_pkt);
    if (get_result == POTR_OK)
    {
        memcpy(ctx->recv_crypto_buf, resend_pkt.payload, cplat_ntoh16(resend_pkt.payload_len));
        resend_pkt.payload = ctx->recv_crypto_buf;
    }

    cplat_local_lock_unlock(slot->send_window_mutex);

    if (get_result == POTR_OK)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: peer=%u NACK seq=%u -> retransmit",
                   ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)ack_num);
        slot_send_packet(slot, &resend_pkt);
    }
    else
    {
        /* 送信ウィンドウから外れた通番は再送できないため、REJECT で受信側に再送不能を通知する。 */
        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                   "recv[service_id=%" PRId64 "]: peer=%u NACK seq=%u not in window -> REJECT", ctx->service.service_id,
                   (unsigned)slot->peer_id, (unsigned)ack_num);
        slot_send_reject(slot, ack_num);
    }
}
