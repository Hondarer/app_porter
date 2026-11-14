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
    int ret = POTR_OK;
    size_t count = 0U;
    size_t limit;
    size_t i;

    if (ctx == NULL || packet == NULL || wire_header == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    report = packet->flags == POTR_FLAG_REPORT;
    limit = ctx->service.max_peers;
    if (limit == 0U)
    {
        limit = 1024U;
    }
    if (!report && packet->flags != POTR_FLAG_NACK)
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
    cplat_local_lock_lock(ctx->callback_mutex, CPLAT_SYNC_WAIT_FOREVER);
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
    }
    cplat_local_lock_unlock(ctx->callback_mutex);
    return ret;
}
