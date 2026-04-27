/**
 *******************************************************************************
 *  @file           potrConnectedThreads.c
 *  @brief          接続確立後スレッド起動 helper の実装。
 *  @author         Tetsuo Honda
 *  @date           2026/04/17
 *  @version        1.0.0
 *
 *  @details
 *  send / recv / health スレッドの起動順序、bootstrap PING 送信、
 *  途中失敗時の rollback を所有権ベースで制御します。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <inttypes.h>

#include <porter_const.h>

#include "../infra/potrTrace.h"
#include "potrHealthThread.h"
#include "potrConnectedThreads.h"

int potr_start_connected_threads(struct PotrContext_           *ctx,
                                 int                            path_idx,
                                 const PotrConnectedThreadsOps *ops)
{
    int is_bidir;
    int is_sender;
    int started_send_thread = 0;

    if (ctx == NULL || ops == NULL)
    {
        return POTR_ERROR;
    }

    is_bidir  = (ctx->service.type == POTR_TYPE_TCP_BIDIR);
    is_sender = (ctx->role == POTR_ROLE_SENDER);

    if ((is_sender || is_bidir) && path_idx == 0 && !ctx->send_thread_running)
    {
        if (ops->send_start(ctx) != POTR_SUCCESS)
        {
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                     "connect_thread[service_id=%" PRId64 "]: send_thread_start failed",
                     ctx->service.service_id);
            return POTR_ERROR;
        }
        started_send_thread = 1;
    }

    if (ops->recv_start(ctx, path_idx) != POTR_SUCCESS)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                 "connect_thread[service_id=%" PRId64 "]: tcp_recv_thread_start failed"
                 " (path=%d)",
                 ctx->service.service_id, path_idx);
        ops->close_conn(ctx, path_idx);
        if (started_send_thread)
        {
            ops->send_stop(ctx);
        }
        return POTR_ERROR;
    }

    ops->set_path_ping_state(ctx, path_idx, POTR_PING_STATE_UNDEFINED);

    if (potr_tcp_send_ping_now(ctx, path_idx) != POTR_SUCCESS)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                 "connect_thread[service_id=%" PRId64 "]: bootstrap TCP PING failed"
                 " (path=%d)",
                 ctx->service.service_id, path_idx);
        ctx->running[path_idx] = 0;
        ops->close_conn(ctx, path_idx);
        ops->join_recv(ctx, path_idx);
        if (started_send_thread)
        {
            ops->send_stop(ctx);
        }
        return POTR_ERROR;
    }

    if (ops->health_start(ctx, path_idx) != POTR_SUCCESS)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                 "connect_thread[service_id=%" PRId64 "]: tcp_health_thread_start failed"
                 " (path=%d)",
                 ctx->service.service_id, path_idx);
        ctx->running[path_idx] = 0;
        ops->close_conn(ctx, path_idx);
        ops->join_recv(ctx, path_idx);
        if (started_send_thread)
        {
            ops->send_stop(ctx);
        }
        return POTR_ERROR;
    }

    return POTR_SUCCESS;
}
