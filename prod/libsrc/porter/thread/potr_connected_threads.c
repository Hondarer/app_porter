/**
 *******************************************************************************
 *  @file           potr_connected_threads.c
 *  @brief          接続確立後のスレッドを起動する helper を実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/04/17
 *  @version        1.0.0
 *
 *  send / recv / health スレッドの起動順序、bootstrap PING 送信、
 *  途中失敗時の rollback を所有権ベースで制御します。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <inttypes.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>

#include <porter/infra/potr_trace.h>
#include <porter/thread/potr_health_thread.h>
#include <porter/thread/potr_connected_threads.h>

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_start_connected_threads(potr_context *ctx, int path_idx, const potr_internal_connected_threads_ops *ops)
{
    int is_bidir;
    int is_sender;
    int started_send_thread = 0;
    int result;

    if (ctx == NULL || ops == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    is_bidir = (ctx->service.type == POTR_TYPE_TCP_BIDIR);
    is_sender = (ctx->role == POTR_ROLE_SENDER);

    if ((is_sender || is_bidir) && path_idx == 0 && !ctx->send_thread_running)
    {
        result = ops->send_start(ctx);
        if (result != POTR_OK)
        {
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR, "connect_thread[service_id=%" PRId64 "]: send_thread_start failed",
                       ctx->service.service_id);
            return result;
        }
        started_send_thread = 1;
    }

    result = ops->recv_start(ctx, path_idx);
    if (result != POTR_OK)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,
                   "connect_thread[service_id=%" PRId64 "]: potr_internal_tcp_recv_thread_start failed"
                   " (path=%d)",
                   ctx->service.service_id, path_idx);
        ops->close_conn(ctx, path_idx);
        if (started_send_thread)
        {
            ops->send_stop(ctx);
        }
        return result;
    }

    ops->set_path_ping_state(ctx, path_idx, POTR_PING_STATE_UNDEFINED);

    result = potr_internal_tcp_send_ping_now(ctx, path_idx);
    if (result != POTR_OK)
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
        return result;
    }

    result = ops->health_start(ctx, path_idx);
    if (result != POTR_OK)
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
        return result;
    }

    return POTR_OK;
}
