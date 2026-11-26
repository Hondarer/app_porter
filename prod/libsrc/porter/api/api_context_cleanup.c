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
#include <cplat/net/endpoint.h>
#include <cplat/net/socket.h>
#include <cplat/runtime/memory_lock.h>
#include <porter/infra/potr_send_queue.h>
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
        dispose_lock(&ctx->tcp_lifecycle_mutex[i]);
        dispose_lock(&ctx->health_mutex[i]);
        dispose_condvar(&ctx->health_wakeup[i]);
        if (ctx->tcp_connect_cancel[i] != NULL)
        {
            cplat_socket_wait_cancel_dispose(ctx->tcp_connect_cancel[i]);
            ctx->tcp_connect_cancel[i] = NULL;
        }
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
    dispose_secret_buffer(ctx->crypto_buf, ctx->crypto_buf_size);
    ctx->crypto_buf = NULL;
    dispose_secret_buffer(ctx->send_batch_plain_buf, ctx->send_batch_cap * ctx->global.max_payload);
    ctx->send_batch_plain_buf = NULL;
    dispose_secret_buffer(ctx->send_batch_wire_buf, ctx->send_batch_cap * ctx->crypto_buf_size);
    ctx->send_batch_wire_buf = NULL;
    ctx->send_batch_cap = 0U;
    dispose_secret_buffer(ctx->recv_buf, POTR_PACKET_HEADER_SIZE + ctx->global.max_payload);
    ctx->recv_buf = NULL;
    while (ctx->receivers != NULL)
    {
        potr_internal_receiver *source = ctx->receivers;
        ctx->receivers = source->next;
        potr_internal_certificate_state_dispose(&source->certificate);
        cplat_secure_zero(source, sizeof(*source));
        cplat_free(source);
    }

    dispose_secret_buffer(ctx->recv_compress_buf, ctx->compress_buf_size);
    ctx->recv_compress_buf = NULL;
    dispose_secret_buffer(ctx->recv_crypto_buf, ctx->crypto_buf_size);
    ctx->recv_crypto_buf = NULL;

    for (i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        dispose_secret_buffer(ctx->tcp_recv_buf[i], POTR_PACKET_HEADER_SIZE + ctx->global.max_payload);
        ctx->tcp_recv_buf[i] = NULL;

        if (ctx->tcp_listen_watch[i] != NULL)
        {
            cplat_socket_watch_detach(ctx->tcp_listen_watch[i]);
            ctx->tcp_listen_watch[i] = NULL;
        }
        if (ctx->tcp_listen_sock[i] != CPLAT_INVALID_SOCKET)
        {
            cplat_socket_close(ctx->tcp_listen_sock[i]);
            ctx->tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
        }

        if (ctx->tcp_conn_watch[i] != NULL)
        {
            cplat_socket_watch_detach(ctx->tcp_conn_watch[i]);
            ctx->tcp_conn_watch[i] = NULL;
        }
        if (ctx->tcp_conn_fd[i] != CPLAT_INVALID_SOCKET)
        {
            cplat_socket_close(ctx->tcp_conn_fd[i]);
            ctx->tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
        }
        if (ctx->tcp_conn_cancel[i] != NULL)
        {
            cplat_socket_wait_cancel_dispose(ctx->tcp_conn_cancel[i]);
            ctx->tcp_conn_cancel[i] = NULL;
        }
        ctx->tcp_conn_generation[i] = 0U;
        ctx->tcp_conn_closing[i] = 0;

        if (ctx->sock[i] != CPLAT_INVALID_SOCKET && raw_base_type(ctx->service.type) == POTR_TYPE_MULTICAST)
        {
            uint32_t group_addr;

            /*
             * 離脱には参加時と同じ NIC を渡す。
             * 直後にソケットを閉じ、閉じる時に OS がグループから離脱させるため、戻り値は明示的に破棄する。
             * see: https://man7.org/linux/man-pages/man7/ip.7.html
             * see: https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ip-socket-options
             */
            if (cplat_ipv4_parse(ctx->service.multicast_group, &group_addr) == CPLAT_OK)
            {
                (void)cplat_socket_leave_multicast_group(ctx->sock[i], group_addr, ctx->multicast_if_resolved[i], NULL);
            }
        }
        if (ctx->udp_watch[i] != NULL)
        {
            cplat_socket_watch_detach(ctx->udp_watch[i]);
            ctx->udp_watch[i] = NULL;
        }
        if (ctx->sock[i] != CPLAT_INVALID_SOCKET)
        {
            cplat_socket_close(ctx->sock[i]);
            ctx->sock[i] = CPLAT_INVALID_SOCKET;
        }

        dispose_lock(&ctx->udp_send_mutex[i]);
    }

    if (ctx->recv_cancel != NULL)
    {
        cplat_socket_wait_cancel_dispose(ctx->recv_cancel);
        ctx->recv_cancel = NULL;
    }

    /* 全利用者スレッドと通知スレッドの終了後に、TCP・health 同期資源と送信キューを破棄する。 */
    api_context_dispose_tcp_sync(ctx);
    if (ctx->send_queue.mutex != NULL || ctx->send_queue.entries != NULL || ctx->send_queue.payload_pool != NULL)
    {
        potr_internal_send_queue_dispose(&ctx->send_queue);
    }

    cplat_free(ctx->send_wire_buf);
    ctx->send_wire_buf = NULL;
    cplat_secure_zero(ctx->service.encrypt_key, sizeof(ctx->service.encrypt_key));
    potr_internal_certificate_dispose(ctx);
    potr_internal_callback_mutex_dispose(ctx);
}
