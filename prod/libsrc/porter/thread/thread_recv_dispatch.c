/**
 *******************************************************************************
 *  @file           thread_recv_dispatch.c
 *  @brief          構成済み受信スロットへパケット種別を振り分けます。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <cplat/clock/clock.h>
#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include <porter/protocol/packet.h>
#include <porter/potr_context.h>
#include <porter/infra/potr_trace.h>
#include <porter/thread/potr_health_thread.h>

#include "thread_recv_dispatch.h"
#include "thread_recv_fin.h"
#include "thread_recv_report.h"
#include "thread_recv_session.h"
#include "thread_recv_window.h"

static int dispatch_fin(thread_recv_slot *slot, const potr_packet *pkt);
static int dispatch_ping(thread_recv_slot *slot, const potr_packet *pkt, int path_idx);

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
    if (state_changed != 0 && (ctx->service.type == POTR_TYPE_UNICAST_BIDIR ||
                               ctx->service.type == POTR_TYPE_UNICAST_BIDIR_N1))
    {
        potr_internal_health_thread_wake(ctx);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_dispatch_packet(thread_recv_slot *slot, potr_packet *pkt, int path_idx,
                                const cplat_ipv4_endpoint *sender)
{
    potr_context *ctx = slot->ctx;
    int state_changed = 0;

    if ((pkt->flags & POTR_FLAG_FIN) != 0)
    {
        return dispatch_fin(slot, pkt);
    }

    if ((pkt->flags & POTR_FLAG_NACK) != 0)
    {
        if (slot->peer != NULL)
        {
            potr_internal_flow_control_on_peer_packet(ctx, slot->peer, pkt);
            thread_recv_window_on_nack(slot, pkt);
        }

        return 0;
    }

    if ((pkt->flags & POTR_FLAG_REJECT) != 0)
    {
        return thread_recv_window_on_reject(slot, pkt, path_idx, sender);
    }

    if ((pkt->flags & POTR_FLAG_FLOW_CONTROL) != 0)
    {
        cplat_timespec pkt_session_ts;

        /*
         * 追跡中の相手セッションからの要求と解除、および multicast と broadcast の送信者からの監視対象の一覧だけを反映する。
         * 古いセッションのパケットは破棄する。
         */
        session_ts_from_hdr(pkt->session_tv_sec, pkt->session_tv_nsec, &pkt_session_ts);
        if (*slot->peer_session_known != 0 && *slot->peer_session_id == pkt->session_id &&
            slot->peer_session_ts->tv_sec == pkt_session_ts.tv_sec &&
            slot->peer_session_ts->tv_nsec == pkt_session_ts.tv_nsec)
        {
            potr_internal_flow_control_on_peer_packet(ctx, slot->peer, pkt);
            potr_internal_flow_control_on_packet(ctx, slot->peer, pkt);
        }

        return 0;
    }

    if ((pkt->flags & (POTR_FLAG_DATA | POTR_FLAG_PING)) == 0)
    {
        return 0;
    }

    /*
     * 通知キューの容量超過で切断した相手は、占有量が解除のしきい値を下回るまで採用しない。
     * 採用すると、次の DATA で再び容量を超え、接続と切断の通知が繰り返されるためである。
     */
    {
        cplat_timespec pkt_session_ts;

        session_ts_from_hdr(pkt->session_tv_sec, pkt->session_tv_nsec, &pkt_session_ts);
        if (potr_internal_flow_control_refused(ctx, slot->peer_id, pkt->session_id, &pkt_session_ts) != 0)
        {
            return 0;
        }
    }

    {
        int was_known = *slot->peer_session_known;
        uint32_t prev_session_id = *slot->peer_session_id;
        cplat_timespec prev_session_ts = *slot->peer_session_ts;
        int adopted_new_session;

        if (thread_recv_session_adopt(slot, pkt) == 0)
        {
            return 0;
        }

        adopted_new_session =
            (slot->peer == NULL &&
             (was_known == 0 || *slot->peer_session_id != prev_session_id ||
              slot->peer_session_ts->tv_sec != prev_session_ts.tv_sec ||
              slot->peer_session_ts->tv_nsec != prev_session_ts.tv_nsec));

        potr_internal_flow_control_on_peer_packet(ctx, slot->peer, pkt);
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
                       ctx->service.service_id, (unsigned)slot->peer_id, pkt_kind_str, (unsigned)pkt->seq_num,
                       path_idx);
        }

        if ((pkt->flags & POTR_FLAG_PING) != 0)
        {
            int ping_ret;

            /*
             * 新しい通信として採用した相手へ、まだ知らせていない送出上限を、最初に送出できる時点で知らせる。
             * 送出上限の更新の判定は、DATA パケットの処理のたびに受信ウィンドウの取り出しで行う。
             */
            potr_internal_flow_control_on_received(ctx, slot->peer_id, *slot->peer_session_id, slot->peer_session_ts,
                                                   slot->recv_window->next_seq, 0, 0U);
            ping_ret = dispatch_ping(slot, pkt, path_idx);
            if (adopted_new_session != 0 && *slot->peer_session_known != 0)
            {
                int report_ret = thread_recv_report_on_adopted(ctx);

                if (report_ret != POTR_OK)
                {
                    cplat_atomic_store_i32(&ctx->fatal_error, report_ret, CPLAT_MEMORY_ORDER_RELEASE);
                    potr_internal_send_queue_abort(&ctx->send_queue, report_ret);
                }
            }

            return ping_ret;
        }

        {
            int peer_was_present = (slot->peer != NULL);

            state_changed = thread_recv_window_accept_outer(slot, pkt, path_idx);
            if (peer_was_present != 0 && slot->peer == NULL)
            {
                return state_changed;
            }
        }

        if (slot->peer == NULL && *slot->peer_session_known != 0)
        {
            if (adopted_new_session != 0)
            {
                int report_ret = thread_recv_report_on_adopted(ctx);

                if (report_ret != POTR_OK)
                {
                    cplat_atomic_store_i32(&ctx->fatal_error, report_ret, CPLAT_MEMORY_ORDER_RELEASE);
                    potr_internal_send_queue_abort(&ctx->send_queue, report_ret);
                    return 0;
                }
            }

            if (thread_recv_report_tracks_lag(ctx) != 0)
            {
                uint64_t now_ms = 0U;

                if (cplat_clock_get_monotonic_ms(&now_ms) == CPLAT_OK)
                {
                    int report_ret = thread_recv_report_on_data_accepted(ctx, now_ms);

                    if (report_ret != POTR_OK)
                    {
                        cplat_atomic_store_i32(&ctx->fatal_error, report_ret, CPLAT_MEMORY_ORDER_RELEASE);
                        potr_internal_send_queue_abort(&ctx->send_queue, report_ret);
                    }
                }
            }
        }
    }

    return state_changed;
}

/*
 * FIN パケットを受信した際のセッション採用判定、pending 判定、切断発火を行う。
 * FIN による経路状態の変化では割り込み PING を要求しないため 0 を返す。
 */
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

/*
 * PING パケットを受信した際の経路ヘルス状態更新、相手端経路状態の反映、PING 欠落スキャンを行う。
 * 双方向 UDP の PING 受信状態が変化した場合は 1、それ以外は 0 を返す。
 */
static int dispatch_ping(thread_recv_slot *slot, const potr_packet *pkt, int path_idx)
{
    potr_context *ctx = slot->ctx;
    int ping_state_changed = thread_recv_update_path_health(slot, path_idx);

    if (cplat_atomic_load_i32(&slot->ctx->fatal_error, CPLAT_MEMORY_ORDER_ACQUIRE) != POTR_OK)
    {
        return 0;
    }

    if (pkt->payload_len >= POTR_MAX_PATH && pkt->payload != NULL)
    {
        thread_recv_apply_remote_path_ping_state(slot->remote_path_ping_state, pkt->payload, POTR_MAX_PATH);
    }

    if (is_raw_type(ctx->service.type))
    {
        thread_recv_window_scan_ping_gap(slot, pkt);
        thread_recv_sync_path_state(slot);
        return 0;
    }

    thread_recv_sync_path_state(slot);
    thread_recv_window_scan_ping_gap(slot, pkt);
    if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR || ctx->service.type == POTR_TYPE_UNICAST_BIDIR_N1)
    {
        return ping_state_changed;
    }

    return 0;
}
