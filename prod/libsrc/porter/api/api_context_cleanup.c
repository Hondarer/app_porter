/**
 *******************************************************************************
 *  @file           api_context_cleanup.c
 *  @brief          停止済みコンテキストの資源を解放します。
 *  @author         Tetsuo Honda
 *  @date           2026/10/24
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include "api_context_cleanup.h"
#include <cplat/crt/stdlib.h>
#include <cplat/runtime/memory_lock.h>
#include <porter/potr_path_event.h>
#include <porter/potr_peer_table.h>
#include <porter/protocol/packet.h>

static void dispose_secret_buffer(void *buffer, size_t size)
{
    if (buffer != NULL)
    {
        cplat_secure_zero(buffer, size);
        cplat_free(buffer);
    }
}

static void dispose_lock(cplat_local_lock **lock)
{
    if (*lock != NULL)
    {
        cplat_local_lock_dispose(*lock);
        *lock = NULL;
    }
}

static void dispose_condvar(cplat_condvar **cv)
{
    if (*cv != NULL)
    {
        cplat_condvar_dispose(*cv);
        *cv = NULL;
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

int api_context_has_threads(const potr_context *ctx)
{
    int i;

    if (ctx->send_thread != NULL)
    {
        return 1;
    }
    for (i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        if (ctx->connect_thread[i] != NULL || ctx->recv_thread[i] != NULL || ctx->health_thread[i] != NULL)
        {
            return 1;
        }
    }
    return 0;
}

/* Doxygen コメントは、ヘッダーに記載 */

void api_context_dispose_tcp_sync(potr_context *ctx)
{
    int i;

    dispose_lock(&ctx->tcp_state_mutex);
    dispose_condvar(&ctx->tcp_state_cv);
    dispose_lock(&ctx->tcp_close_mutex);
    dispose_condvar(&ctx->tcp_close_cv);
    for (i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        dispose_lock(&ctx->tcp_send_mutex[i]);
        dispose_lock(&ctx->health_mutex[i]);
        dispose_condvar(&ctx->health_wakeup[i]);
    }
    dispose_lock(&ctx->recv_window_mutex);
    dispose_lock(&ctx->tcp_recv_mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */

void api_context_dispose_resources(potr_context *ctx)
{
    int i;

    potr_internal_window_dispose(&ctx->send_window);
    potr_internal_window_dispose(&ctx->recv_window);
    if (ctx->peers != NULL)
    {
        potr_internal_peer_table_dispose(ctx);
    }
    dispose_secret_buffer(ctx->frag_buf, ctx->global.max_message_size);
    ctx->frag_buf = NULL;
    dispose_secret_buffer(ctx->compress_buf, ctx->compress_buf_size);
    ctx->compress_buf = NULL;
    dispose_secret_buffer(ctx->crypto_buf, ctx->crypto_buf_size);
    ctx->crypto_buf = NULL;
    dispose_secret_buffer(ctx->recv_buf, POTR_PACKET_HEADER_SIZE + ctx->global.max_payload);
    ctx->recv_buf = NULL;
    dispose_secret_buffer(ctx->recv_compress_buf, ctx->compress_buf_size);
    ctx->recv_compress_buf = NULL;
    dispose_secret_buffer(ctx->recv_crypto_buf, ctx->crypto_buf_size);
    ctx->recv_crypto_buf = NULL;
    for (i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        dispose_secret_buffer(ctx->tcp_recv_buf[i], POTR_PACKET_HEADER_SIZE + ctx->global.max_payload);
        ctx->tcp_recv_buf[i] = NULL;
        if (ctx->tcp_listen_sock[i] != CPLAT_INVALID_SOCKET)
        {
            cplat_socket_close(ctx->tcp_listen_sock[i]);
            ctx->tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
        }
        if (ctx->tcp_conn_fd[i] != CPLAT_INVALID_SOCKET)
        {
            cplat_socket_close(ctx->tcp_conn_fd[i]);
            ctx->tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
        }
        if (ctx->sock[i] != CPLAT_INVALID_SOCKET)
        {
            cplat_socket_close(ctx->sock[i]);
            ctx->sock[i] = CPLAT_INVALID_SOCKET;
        }
    }
    cplat_free(ctx->send_wire_buf);
    ctx->send_wire_buf = NULL;
    cplat_secure_zero(ctx->service.encrypt_key, sizeof(ctx->service.encrypt_key));
    potr_internal_callback_mutex_dispose(ctx);
}
