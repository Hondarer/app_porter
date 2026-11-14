/**
 *******************************************************************************
 *  @file           path_group_context.c
 *  @brief          論理経路状態をグループ判定へ接続します。
 *
 *******************************************************************************
 */
#include <string.h>
#include <inttypes.h>
#include <porter/potr_context.h>
#include <porter/porter_result.h>
#include <porter/protocol/path_group.h>
#include <porter/protocol/receiver_report.h>
#include <porter/infra/potr_trace.h>

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_path_group_select(potr_context *ctx, potr_internal_peer_context *peer,
    potr_internal_path_group_result *result)
{
    potr_internal_path_group_input input = {0};
    potr_internal_path_group_state *state;
    int i;
    potr_peer_id peer_id = POTR_PEER_NA;

    if (ctx == NULL || result == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (ctx->n_path < 0 || ctx->n_path > (int)POTR_MAX_PATH)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    state = &ctx->path_group_state;
    if (cplat_clock_get_monotonic_ms(&input.now_ms) != CPLAT_OK)
    {
        return POTR_ERR_IO;
    }
    input.started_ms = ctx->started_ms;
    input.timeout_ms = ctx->health_timeout_ms;
    input.interval_ms = ctx->health_interval_ms;
    input.failback_ms = ctx->service.group_failback_ms;
    input.type = ctx->service.type;
    memcpy(input.group, ctx->service.path_group, sizeof(input.group));

    /* TCP の fd は経路の送信 mutex の下で採取し、callback_mutex と入れ子にしません。 */
    for (i = 0; i < ctx->n_path; i++)
    {
        if (is_tcp_type(ctx->service.type))
        {
            cplat_local_lock_lock(ctx->tcp_send_mutex[i], CPLAT_SYNC_WAIT_FOREVER);
            input.available[i] = (uint8_t)(ctx->tcp_conn_fd[i] != CPLAT_INVALID_SOCKET);
            cplat_local_lock_unlock(ctx->tcp_send_mutex[i]);
        }
        else
        {
            input.available[i] = (uint8_t)(ctx->sock[i] != CPLAT_INVALID_SOCKET &&
                (peer == NULL || !endpoint_is_unset(&peer->dest_addr[i])));
        }
    }
    cplat_local_lock_lock(ctx->callback_mutex, CPLAT_SYNC_WAIT_FOREVER);
    if (peer != NULL)
    {
        state = &peer->path_group_state;
        peer_id = peer->peer_id;
    }
    for (i = 0; i < ctx->n_path; i++)
    {
        if (peer != NULL)
        {
            input.reachable[i] = (uint8_t)(peer->path_logical_alive[i] != 0);
        }
        else
        {
            input.reachable[i] = (uint8_t)(ctx->path_logical_alive[i] != 0);
        }
    }
    input.report_received = (uint8_t)ctx->report_received;
    if (is_oneway_udp_type(ctx->service.type) && !is_raw_type(ctx->service.type))
    {
        input.reported_groups = potr_internal_receiver_groups_locked(ctx, input.now_ms);
    }
    potr_internal_path_group_evaluate(&input, state, state, result);
    if (result->changed)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                   "path_group[service_id=%" PRId64 " peer=%u]: group %u -> %u",
                   ctx->service.service_id, (unsigned)peer_id,
                   (unsigned)result->previous_group, (unsigned)result->next_group);
    }
    cplat_local_lock_unlock(ctx->callback_mutex);
    return POTR_OK;
}
