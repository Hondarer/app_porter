/**
 *******************************************************************************
 *  @file           receiver_report.c
 *  @brief          受信報告の期限と返送元の上限を管理します。
 *
 *******************************************************************************
 */
#include <string.h>
#include <inttypes.h>
#include <cplat/crt/stdlib.h>
#include <cplat/net/byteorder.h>
#include <porter/potr_context.h>
#include <porter/porter_result.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/receiver_report.h>
#include <porter/infra/potr_trace.h>

/* Doxygen コメントは、ヘッダーに記載 */

uint8_t potr_internal_receiver_groups_locked(potr_context *ctx, uint64_t now_ms)
{
    potr_internal_receiver *receiver;
    uint8_t groups = (uint8_t)((1U << POTR_MAX_PATH_GROUP) - 1U);
    int active = 0;

    if (ctx == NULL)
    {
        return 0U;
    }
    for (receiver = ctx->receivers; receiver != NULL; receiver = receiver->next)
    {
        uint8_t receiver_groups = 0U;
        int i;

        if (receiver->active && ctx->health_timeout_ms != 0U &&
            now_ms - receiver->last_report_ms >= ctx->health_timeout_ms)
        {
            receiver->active = 0;
            receiver->retired_ms = receiver->last_report_ms + ctx->health_timeout_ms;
        }
        if (!receiver->active)
        {
            continue;
        }
        active = 1;
        for (i = 0; i < ctx->n_path; i++)
        {
            uint8_t group = ctx->service.path_group[i];

            if (receiver->path_state[i] == POTR_PING_STATE_NORMAL && group >= 1U && group <= POTR_MAX_PATH_GROUP)
            {
                receiver_groups |= (uint8_t)(1U << (group - 1U));
            }
        }
        groups &= receiver_groups;
    }
    if (!active)
    {
        return 0U;
    }
    return groups;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_receiver_accept(potr_context *ctx, const potr_packet *packet, const uint8_t *wire_header)
{
    potr_internal_receiver *receiver;
    potr_internal_receiver *oldest = NULL;
    uint64_t now_ms;
    uint16_t wire_flags;
    int report;
    int flow;
    int ret = POTR_OK;
    uint8_t flow_state = 0U;
    uint32_t flow_send_limit = 0U;
    size_t count = 0U;
    size_t limit;
    size_t i;

    if (ctx == NULL || packet == NULL || wire_header == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    report = packet->flags == POTR_FLAG_REPORT;
    flow = packet->flags == POTR_FLAG_FLOW_CONTROL;
    limit = ctx->service.max_peers;
    if (limit == 0U)
    {
        limit = 1024U;
    }
    if (!report && !flow && packet->flags != POTR_FLAG_NACK)
    {
        return POTR_ERR_PROTOCOL;
    }
    /* ペイロードが 5 [byte] でないフロー制御パケットと、状態が不正なフロー制御パケットは破棄する。 */
    if (flow && flow_control_payload_decode(packet, &flow_state, &flow_send_limit) != POTR_OK)
    {
        return POTR_ERR_PROTOCOL;
    }
    if (report)
    {
        if (packet->payload == NULL || packet->payload_len != POTR_MAX_PATH ||
            packet->seq_num != 0U || packet->ack_num != 0U)
        {
            return POTR_ERR_PROTOCOL;
        }
        for (i = 0U; i < POTR_MAX_PATH; i++)
        {
            if (packet->payload[i] != POTR_PING_STATE_UNDEFINED && packet->payload[i] != POTR_PING_STATE_NORMAL &&
                packet->payload[i] != POTR_PING_STATE_ABNORMAL)
            {
                return POTR_ERR_PROTOCOL;
            }
        }
    }
    if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
    {
        return POTR_ERR_IO;
    }
    cplat_local_lock_lock(ctx->callback_mutex);
    /* 期限切れの受信者を無効にする副作用だけを使い、到達できるグループの集合はここでは使わないため、戻り値は明示的に破棄する。 */
    (void)potr_internal_receiver_groups_locked(ctx, now_ms);
    for (receiver = ctx->receivers; receiver != NULL; receiver = receiver->next)
    {
        if (receiver->session_id == packet->session_id && receiver->session_tv_sec == packet->session_tv_sec &&
            receiver->session_tv_nsec == packet->session_tv_nsec)
        {
            break;
        }
        if (!receiver->active && (oldest == NULL || receiver->retired_ms < oldest->retired_ms))
        {
            oldest = receiver;
        }
        count++;
    }
    if (ctx->service.auth == POTR_AUTH_CERT && (receiver == NULL || !receiver->certificate.established))
    {
        cplat_local_lock_unlock(ctx->callback_mutex);
        return POTR_ERR_PROTOCOL;
    }
    if (receiver == NULL)
    {
        if (count < limit)
        {
            receiver = (potr_internal_receiver *)cplat_calloc(1U, sizeof(*receiver));
            if (receiver != NULL)
            {
                receiver->next = ctx->receivers;
                ctx->receivers = receiver;
            }
        }
        else if (oldest != NULL)
        {
            potr_internal_receiver *next = oldest->next;

            receiver = oldest;
            memset(receiver, 0, sizeof(*receiver));
            receiver->next = next;
        }
        else
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                       "report[service_id=%" PRId64 "]: max_peers=%zu reached, dropping receiver session=%u",
                       ctx->service.service_id, limit, (unsigned)packet->session_id);
            cplat_local_lock_unlock(ctx->callback_mutex);
            return POTR_ERR_FULL;
        }
        if (receiver == NULL)
        {
            cplat_local_lock_unlock(ctx->callback_mutex);
            return POTR_ERR_OUT_OF_MEMORY;
        }
        receiver->retired_ms = now_ms;
        receiver->session_id = packet->session_id;
        receiver->session_tv_sec = packet->session_tv_sec;
        receiver->session_tv_nsec = packet->session_tv_nsec;
    }
    memcpy(&wire_flags, wire_header + 32U, sizeof(wire_flags));
    if ((cplat_ntoh16(wire_flags) & POTR_FLAG_ENCRYPTED) != 0U)
    {
        ret = potr_internal_replay_window_accept(&receiver->replay_window, packet);
    }
    if (ret == POTR_OK)
    {
        if (report)
        {
            memcpy(receiver->path_state, packet->payload, POTR_MAX_PATH);
            receiver->last_report_ms = now_ms;
            receiver->active = 1;
            receiver->retired_ms = 0U;
            ctx->report_received = 1;
        }
        else if (flow)
        {
            if (flow_state == POTR_FLOW_CONTROL_STATE_SUPPRESS)
            {
                receiver->flow_request_expire_ms = now_ms + ctx->flow_control_config.request_lifetime_ms;
            }
            else
            {
                receiver->flow_request_expire_ms = 0U;
            }

            /* 送出上限は、状態の値によらず上書きする。通番で表すため、重複して届いても判定は変わらない。 */
            receiver->flow_send_limit = POTR_FLOW_SEND_LIMIT_HELD | (uint64_t)flow_send_limit;
            receiver->flow_limit_received_ms = now_ms;
        }
    }
    cplat_local_lock_unlock(ctx->callback_mutex);
    return ret;
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_receiver_flow_suppressed(potr_context *ctx, uint64_t now_ms, uint32_t next_seq, size_t *tracked,
                                           uint64_t *until_ms)
{
    potr_internal_receiver *receiver;
    size_t active_count = 0U;
    size_t stopping_count = 0U;
    size_t tracked_count = 0U;
    uint64_t earliest_ms = 0U;
    int suppressed = 0;

    if (ctx == NULL || tracked == NULL || until_ms == NULL)
    {
        return 0;
    }

    cplat_local_lock_lock(ctx->callback_mutex);

    /*
     * 受信報告が途絶えた受信者を活動中の数から先に外す副作用だけを使い、到達できるグループの集合はここでは
     * 使わないため、戻り値は明示的に破棄する。
     */
    (void)potr_internal_receiver_groups_locked(ctx, now_ms);

    for (receiver = ctx->receivers; receiver != NULL; receiver = receiver->next)
    {
        uint64_t limit_until_ms = 0U;
        uint32_t allowance = 0U;
        int valid = receiver->flow_request_expire_ms > now_ms;
        potr_internal_send_limit_state limit_state = flow_send_limit_judge(
            receiver->flow_send_limit, receiver->flow_limit_received_ms, &receiver->flow_limit_stalled_ms, next_seq,
            now_ms, ctx->flow_control_config.request_lifetime_ms, &limit_until_ms, &allowance);
        int stopping;

        /* 送出上限に達した時点から有効期限が経過しても次のフロー制御パケットが届かない場合は、送出上限を破棄する。 */
        if (limit_state == POTR_SEND_LIMIT_EXPIRED)
        {
            receiver->flow_send_limit = 0U;
            receiver->flow_limit_stalled_ms = 0U;
        }

        stopping = valid || limit_state == POTR_SEND_LIMIT_REACHED;

        /*
         * 「止める状態」の受信者は、受信報告の有無にかかわらず活動中として数える。
         * この扱いはフロー制御の判定だけに使い、経路の優先グループの判定と死活監視には使わない。
         */
        if (receiver->active || stopping)
        {
            active_count++;
        }

        /* 要求か送出上限が残っている間は、送出のたびに判定し直す必要がある。 */
        if (valid || receiver->flow_send_limit != 0U)
        {
            tracked_count++;
        }

        if (stopping)
        {
            stopping_count++;

            if (valid && (earliest_ms == 0U || receiver->flow_request_expire_ms < earliest_ms))
            {
                earliest_ms = receiver->flow_request_expire_ms;
            }

            if (limit_state == POTR_SEND_LIMIT_REACHED && (earliest_ms == 0U || limit_until_ms < earliest_ms))
            {
                earliest_ms = limit_until_ms;
            }
        }
    }

    cplat_local_lock_unlock(ctx->callback_mutex);

    if (stopping_count != 0U)
    {
        if (ctx->service.type == POTR_TYPE_MULTICAST || ctx->service.type == POTR_TYPE_BROADCAST)
        {
            size_t count_threshold = (size_t)ctx->flow_control_config.receiver_count;
            size_t percent = (size_t)ctx->flow_control_config.receiver_percent;

            /* 活動中の受信者数が台数の設定値を下回る場合は、活動中の全員が「止める状態」で条件を満たす。 */
            if (count_threshold > active_count)
            {
                count_threshold = active_count;
            }

            if (count_threshold != 0U && stopping_count >= count_threshold)
            {
                suppressed = 1;
            }

            /* 受信者数は max_peers 以下、percent は 100 以下のため、乗算は桁あふれしない。 */
            if (percent != 0U && stopping_count * 100U >= percent * active_count)
            {
                suppressed = 1;
            }
        }
        else
        {
            suppressed = 1;
        }
    }

    *tracked = tracked_count;
    *until_ms = earliest_ms;

    return suppressed;
}
