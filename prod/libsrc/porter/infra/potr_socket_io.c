/**
 *******************************************************************************
 *  @file           potr_socket_io.c
 *  @brief          非ブロッキング ソケット通信と TCP 接続世代の制御ヘルパーを実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/11/24
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <assert.h>
#include <limits.h>
#include <string.h>

#include <cplat/base/error.h>
#include <cplat/base/result.h>
#include <cplat/base/wait.h>
#include <cplat/clock/clock.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/socket.h>
#include <cplat/sync/atomic.h>
#include <cplat/sync/sync.h>

#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include <porter/infra/potr_result.h>
#include <porter/infra/potr_socket_io.h>
#include <porter/protocol/packet.h>

static_assert(POTR_UDP_SEND_TIMEOUT_MS > 0U && POTR_UDP_SEND_TIMEOUT_MS <= POTR_WAIT_MS_MAX,
              "porter: POTR_UDP_SEND_TIMEOUT_MS out of range");
static_assert(POTR_TCP_SEND_WAIT_SLICE_MS > 0U && POTR_TCP_SEND_WAIT_SLICE_MS <= POTR_WAIT_MS_MAX,
              "porter: POTR_TCP_SEND_WAIT_SLICE_MS out of range");

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_tcp_close_deadline_publish(potr_context *ctx)
{
    uint64_t now_ms = 0U;
    uint64_t deadline_ms;

    if (ctx == NULL)
    {
        return;
    }

    if (cplat_atomic_load_i32(&ctx->tcp_close_deadline_active, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
    {
        return;
    }

    if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
    {
        /* 単調時刻が取得できない場合でも期限超過扱いとして待機を打ち切る。 */
        deadline_ms = 0U;
    }
    else
    {
        deadline_ms = now_ms + (uint64_t)ctx->global.tcp_close_timeout_ms;
    }

    cplat_atomic_store_u64(&ctx->tcp_close_deadline_ms, deadline_ms, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_i32(&ctx->tcp_close_deadline_active, 1, CPLAT_MEMORY_ORDER_RELEASE);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_tcp_close_remaining_ms(const potr_context *ctx, int *has_deadline_out, int *remaining_ms_out)
{
    uint64_t deadline_ms;
    uint64_t now_ms;
    uint64_t diff;

    if (has_deadline_out != NULL)
    {
        *has_deadline_out = 0;
    }
    if (remaining_ms_out != NULL)
    {
        *remaining_ms_out = CPLAT_WAIT_FOREVER;
    }

    if (ctx == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (cplat_atomic_load_i32(&ctx->tcp_close_deadline_active, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
    {
        return POTR_OK;
    }

    if (has_deadline_out != NULL)
    {
        *has_deadline_out = 1;
    }

    deadline_ms = cplat_atomic_load_u64(&ctx->tcp_close_deadline_ms, CPLAT_MEMORY_ORDER_ACQUIRE);

    if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
    {
        if (remaining_ms_out != NULL)
        {
            *remaining_ms_out = 0;
        }
        return POTR_ERR_IO;
    }

    if (now_ms >= deadline_ms)
    {
        if (remaining_ms_out != NULL)
        {
            *remaining_ms_out = 0;
        }
        return POTR_ERR_TIMEOUT;
    }

    diff = deadline_ms - now_ms;
    if (diff > (uint64_t)POTR_WAIT_MS_MAX)
    {
        diff = (uint64_t)POTR_WAIT_MS_MAX;
    }

    if (remaining_ms_out != NULL)
    {
        *remaining_ms_out = (int)diff;
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_tcp_conn_request_close(potr_context *ctx, int path_idx, uint64_t generation)
{
    if (ctx == NULL || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH)
    {
        return;
    }

    if (ctx->tcp_lifecycle_mutex[path_idx] == NULL)
    {
        return;
    }

    cplat_local_lock_lock(ctx->tcp_lifecycle_mutex[path_idx]);

    if (ctx->tcp_conn_generation[path_idx] != 0U &&
        (generation == 0U || ctx->tcp_conn_generation[path_idx] == generation))
    {
        ctx->tcp_conn_closing[path_idx] = 1;
        cplat_atomic_store_i32(&ctx->running[path_idx], 0, CPLAT_MEMORY_ORDER_RELEASE);

        if (ctx->tcp_conn_cancel[path_idx] != NULL)
        {
            /*
             * 有効な cancel オブジェクトへの通知は必ず CPLAT_OK を返し、OS 通知失敗時も
             * 待機側が最大 50 [ms] の内部間隔で解除を検出する契約のため、戻り値は明示的に破棄する。
             */
            (void)cplat_socket_wait_cancel_signal(ctx->tcp_conn_cancel[path_idx], NULL);
        }
    }

    cplat_local_lock_unlock(ctx->tcp_lifecycle_mutex[path_idx]);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_tcp_conn_request_close_all(potr_context *ctx)
{
    int i;

    if (ctx == NULL)
    {
        return;
    }

    for (i = 0; i < ctx->n_path; i++)
    {
        potr_internal_tcp_conn_request_close(ctx, i, 0U);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_tcp_conn_release(potr_context *ctx, int path_idx, uint64_t generation)
{
    cplat_socket fd = CPLAT_INVALID_SOCKET;
    cplat_socket_watch *watch = NULL;
    cplat_socket_wait_cancel *cancel = NULL;

    if (ctx == NULL || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH || generation == 0U)
    {
        return;
    }

    if (ctx->tcp_send_mutex[path_idx] == NULL || ctx->tcp_lifecycle_mutex[path_idx] == NULL)
    {
        return;
    }

    /*
     * 送信用ロックを取得して I/O の使用完了を確定し、寿命管理用ロックの下で世代を公開解除してから
     * watch の detach、socket の close、cancel の破棄を行う。
     * 使用中のソケットを別スレッドから閉じない規則に従う。
     * see: https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-closesocket
     */
    cplat_local_lock_lock(ctx->tcp_send_mutex[path_idx]);
    cplat_local_lock_lock(ctx->tcp_lifecycle_mutex[path_idx]);

    if (ctx->tcp_conn_generation[path_idx] == generation)
    {
        fd = ctx->tcp_conn_fd[path_idx];
        watch = ctx->tcp_conn_watch[path_idx];
        cancel = ctx->tcp_conn_cancel[path_idx];

        ctx->tcp_conn_fd[path_idx] = CPLAT_INVALID_SOCKET;
        ctx->tcp_conn_watch[path_idx] = NULL;
        ctx->tcp_conn_cancel[path_idx] = NULL;
        ctx->tcp_conn_generation[path_idx] = 0U;
        ctx->tcp_conn_closing[path_idx] = 0;
        ctx->tcp_ping_announced[path_idx] = 0;
    }

    cplat_local_lock_unlock(ctx->tcp_lifecycle_mutex[path_idx]);

    if (watch != NULL)
    {
        cplat_socket_watch_detach(watch);
    }
    if (fd != CPLAT_INVALID_SOCKET)
    {
        cplat_socket_close(fd);
    }
    if (cancel != NULL)
    {
        cplat_socket_wait_cancel_dispose(cancel);
    }

    cplat_local_lock_unlock(ctx->tcp_send_mutex[path_idx]);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_udp_sendto(potr_context *ctx, int path_idx, const void *buf, size_t len,
                             const cplat_ipv4_endpoint *endpoint, cplat_error *detail_out)
{
    cplat_socket_watch *watch;
    cplat_error detail;
    int has_deadline = 0;
    uint64_t deadline_ms = 0U;
    int result = POTR_OK;

    if (detail_out != NULL)
    {
        cplat_error_clear(detail_out);
    }

    if (ctx == NULL || buf == NULL || endpoint == NULL || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (ctx->udp_send_mutex[path_idx] != NULL)
    {
        cplat_local_lock_lock(ctx->udp_send_mutex[path_idx]);
    }

    watch = ctx->udp_watch[path_idx];
    if (watch == NULL || ctx->sock[path_idx] == CPLAT_INVALID_SOCKET)
    {
        result = POTR_ERR_IO;
        goto done;
    }

    for (;;)
    {
        size_t sent = 0U;
        int send_rc;

        cplat_error_clear(&detail);
        send_rc = cplat_socket_watch_sendto(watch, buf, len, endpoint, &sent, &detail);
        if (send_rc == CPLAT_OK)
        {
            if (sent != len)
            {
                /* UDP は全量が送信された場合だけ成功とし、短い送信は残りだけを別データグラムとして送らない。 */
                result = POTR_ERR_IO;
            }
            else
            {
                result = POTR_OK;
            }
            break;
        }

        if (send_rc == CPLAT_ERR_WOULD_BLOCK)
        {
            uint64_t now_ms;
            uint64_t remaining;
            int wait_rc;

            if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
            {
                result = POTR_ERR_IO;
                break;
            }

            if (!has_deadline)
            {
                deadline_ms = now_ms + (uint64_t)POTR_UDP_SEND_TIMEOUT_MS;
                has_deadline = 1;
            }

            if (now_ms >= deadline_ms)
            {
                result = POTR_ERR_TIMEOUT;
                break;
            }

            remaining = deadline_ms - now_ms;
            if (remaining > (uint64_t)POTR_WAIT_MS_MAX)
            {
                remaining = (uint64_t)POTR_WAIT_MS_MAX;
            }

            cplat_error_clear(&detail);
            wait_rc = cplat_socket_watch_wait_writable(watch, ctx->recv_cancel, (int)remaining, &detail);
            if (wait_rc == CPLAT_OK)
            {
                /* 待機成功は送信成功を意味しないため、必ず再送信して結果を判定する。 */
                continue;
            }

            if (detail_out != NULL && cplat_error_is_set(&detail))
            {
                *detail_out = detail;
            }
            result = potr_internal_result_from_socket_result(wait_rc, &detail);
            break;
        }

        if (detail_out != NULL && cplat_error_is_set(&detail))
        {
            *detail_out = detail;
        }
        result = potr_internal_result_from_socket_result(send_rc, &detail);
        break;
    }

done:
    if (ctx->udp_send_mutex[path_idx] != NULL)
    {
        cplat_local_lock_unlock(ctx->udp_send_mutex[path_idx]);
    }

    return result;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_tcp_send_all_locked(potr_context *ctx, int path_idx, const void *buf, size_t len, size_t *sent_out,
                                      cplat_error *detail_out)
{
    cplat_socket_watch *watch = NULL;
    cplat_socket_wait_cancel *cancel = NULL;
    uint64_t generation = 0U;
    size_t total_sent = 0U;
    cplat_error detail;
    int result = POTR_OK;

    if (sent_out != NULL)
    {
        *sent_out = 0U;
    }
    if (detail_out != NULL)
    {
        cplat_error_clear(detail_out);
    }

    if (ctx == NULL || (buf == NULL && len > 0U) || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (ctx->tcp_lifecycle_mutex[path_idx] == NULL)
    {
        return POTR_ERR_DISCONNECTED;
    }

    cplat_local_lock_lock(ctx->tcp_lifecycle_mutex[path_idx]);
    if (ctx->tcp_conn_fd[path_idx] != CPLAT_INVALID_SOCKET && ctx->tcp_conn_watch[path_idx] != NULL &&
        ctx->tcp_conn_generation[path_idx] != 0U && ctx->tcp_conn_closing[path_idx] == 0)
    {
        watch = ctx->tcp_conn_watch[path_idx];
        cancel = ctx->tcp_conn_cancel[path_idx];
        generation = ctx->tcp_conn_generation[path_idx];
    }
    cplat_local_lock_unlock(ctx->tcp_lifecycle_mutex[path_idx]);

    if (watch == NULL || generation == 0U)
    {
        return POTR_ERR_DISCONNECTED;
    }

    while (total_sent < len)
    {
        int has_deadline = 0;
        int remaining_ms = CPLAT_WAIT_FOREVER;
        int dl_rc;
        int closing = 0;
        size_t chunk_sent = 0U;
        int send_rc;

        cplat_local_lock_lock(ctx->tcp_lifecycle_mutex[path_idx]);
        if (ctx->tcp_conn_generation[path_idx] != generation || ctx->tcp_conn_closing[path_idx] != 0)
        {
            closing = 1;
        }
        cplat_local_lock_unlock(ctx->tcp_lifecycle_mutex[path_idx]);

        if (closing != 0)
        {
            result = POTR_ERR_CANCELED;
            break;
        }

        dl_rc = potr_internal_tcp_close_remaining_ms(ctx, &has_deadline, &remaining_ms);
        if (dl_rc != POTR_OK)
        {
            potr_internal_tcp_conn_request_close(ctx, path_idx, generation);
            result = dl_rc;
            break;
        }

        cplat_error_clear(&detail);
        send_rc = cplat_socket_watch_send(watch, (const uint8_t *)buf + total_sent, len - total_sent, &chunk_sent,
                                          &detail);
        if (send_rc == CPLAT_OK)
        {
            total_sent += chunk_sent;
            if (chunk_sent == 0U)
            {
                potr_internal_tcp_conn_request_close(ctx, path_idx, generation);
                result = POTR_ERR_IO;
                break;
            }
            continue;
        }

        if (send_rc == CPLAT_ERR_WOULD_BLOCK)
        {
            int wait_ms = (int)POTR_TCP_SEND_WAIT_SLICE_MS;
            int wait_rc;

            dl_rc = potr_internal_tcp_close_remaining_ms(ctx, &has_deadline, &remaining_ms);
            if (dl_rc != POTR_OK)
            {
                potr_internal_tcp_conn_request_close(ctx, path_idx, generation);
                result = dl_rc;
                break;
            }

            if (has_deadline != 0 && remaining_ms < wait_ms)
            {
                wait_ms = remaining_ms;
            }

            cplat_error_clear(&detail);
            wait_rc = cplat_socket_watch_wait_writable(watch, cancel, wait_ms, &detail);
            if (wait_rc == CPLAT_OK || wait_rc == CPLAT_ERR_TIMEOUT)
            {
                /*
                 * CPLAT_OK は送信可能または切断/エラーの通知であり、送信成功を意味しないため再送信へ戻る。
                 * CPLAT_ERR_TIMEOUT は最大 100 [ms] の内部待機満了であり、先頭で全体期限を再判定する。
                 */
                continue;
            }

            if (detail_out != NULL && cplat_error_is_set(&detail))
            {
                *detail_out = detail;
            }
            potr_internal_tcp_conn_request_close(ctx, path_idx, generation);
            result = potr_internal_result_from_socket_result(wait_rc, &detail);
            break;
        }

        if (detail_out != NULL && cplat_error_is_set(&detail))
        {
            *detail_out = detail;
        }
        potr_internal_tcp_conn_request_close(ctx, path_idx, generation);
        result = potr_internal_result_from_socket_result(send_rc, &detail);
        break;
    }

    if (sent_out != NULL)
    {
        *sent_out = total_sent;
    }

    return result;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_tcp_send_all(potr_context *ctx, int path_idx, const void *buf, size_t len, size_t *sent_out,
                               cplat_error *detail_out)
{
    int result;

    if (ctx == NULL || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH)
    {
        if (sent_out != NULL)
        {
            *sent_out = 0U;
        }
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (ctx->tcp_send_mutex[path_idx] == NULL)
    {
        if (sent_out != NULL)
        {
            *sent_out = 0U;
        }
        return POTR_ERR_DISCONNECTED;
    }

    cplat_local_lock_lock(ctx->tcp_send_mutex[path_idx]);
    result = potr_internal_tcp_send_all_locked(ctx, path_idx, buf, len, sent_out, detail_out);
    cplat_local_lock_unlock(ctx->tcp_send_mutex[path_idx]);

    return result;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_tcp_check_writable(potr_context *ctx, int path_idx)
{
    cplat_socket_watch *watch = NULL;
    cplat_socket_wait_cancel *cancel = NULL;
    int wait_rc;

    if (ctx == NULL || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (ctx->tcp_send_mutex[path_idx] == NULL || ctx->tcp_lifecycle_mutex[path_idx] == NULL)
    {
        return POTR_ERR_DISCONNECTED;
    }

    cplat_local_lock_lock(ctx->tcp_send_mutex[path_idx]);
    cplat_local_lock_lock(ctx->tcp_lifecycle_mutex[path_idx]);

    if (ctx->tcp_conn_fd[path_idx] != CPLAT_INVALID_SOCKET && ctx->tcp_conn_watch[path_idx] != NULL &&
        ctx->tcp_conn_generation[path_idx] != 0U && ctx->tcp_conn_closing[path_idx] == 0)
    {
        watch = ctx->tcp_conn_watch[path_idx];
        cancel = ctx->tcp_conn_cancel[path_idx];
    }

    cplat_local_lock_unlock(ctx->tcp_lifecycle_mutex[path_idx]);

    if (watch == NULL)
    {
        cplat_local_lock_unlock(ctx->tcp_send_mutex[path_idx]);
        return POTR_ERR_DISCONNECTED;
    }

    wait_rc = cplat_socket_watch_wait_writable(watch, cancel, CPLAT_NO_WAIT, NULL);
    cplat_local_lock_unlock(ctx->tcp_send_mutex[path_idx]);

    if (wait_rc == CPLAT_ERR_TIMEOUT)
    {
        return POTR_ERR_TIMEOUT;
    }
    if (wait_rc == CPLAT_ERR_CANCELED)
    {
        return POTR_ERR_CANCELED;
    }

    /* その他の結果 (CPLAT_OK または OS 失敗) は続く実際の送信で結果を判定する。 */
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_tcp_frame_reader_reset(potr_internal_tcp_frame_reader *reader)
{
    if (reader == NULL)
    {
        return;
    }

    reader->received = 0U;
    reader->expected = POTR_PACKET_HEADER_SIZE;
    reader->payload_len = 0U;
    reader->header_done = 0U;
    reader->_pad = 0U;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_tcp_frame_reader_step(potr_internal_tcp_frame_reader *reader, cplat_socket fd, uint8_t *buf,
                                        size_t max_buf, int *complete_out, cplat_error *detail_out)
{
    cplat_error detail;
    size_t n = 0U;
    int rc;

    if (detail_out != NULL)
    {
        cplat_error_clear(detail_out);
    }
    if (complete_out != NULL)
    {
        *complete_out = 0;
    }

    if (reader == NULL || buf == NULL || complete_out == NULL || max_buf < POTR_PACKET_HEADER_SIZE ||
        fd == CPLAT_INVALID_SOCKET)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (reader->expected == 0U || reader->received >= reader->expected)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    cplat_error_clear(&detail);
    rc = cplat_socket_recv(fd, buf + reader->received, reader->expected - reader->received, &n, &detail);
    if (rc == CPLAT_ERR_WOULD_BLOCK)
    {
        return POTR_OK;
    }
    if (rc != CPLAT_OK)
    {
        if (detail_out != NULL && cplat_error_is_set(&detail))
        {
            *detail_out = detail;
        }
        return potr_internal_result_from_socket_result(rc, &detail);
    }
    if (n == 0U)
    {
        return POTR_ERR_EOF;
    }

    reader->received += n;

    if (reader->header_done == 0U && reader->received == POTR_PACKET_HEADER_SIZE)
    {
        uint16_t wpl;
        uint16_t wire_payload_len;

        memcpy(&wpl, buf + POTR_PACKET_PAYLOAD_LEN_OFFSET, sizeof(wpl));
        wire_payload_len = cplat_ntoh16(wpl);

        if (POTR_PACKET_HEADER_SIZE + (size_t)wire_payload_len > max_buf)
        {
            return POTR_ERR_PROTOCOL;
        }

        reader->payload_len = wire_payload_len;
        reader->header_done = 1U;
        reader->expected = POTR_PACKET_HEADER_SIZE + (size_t)wire_payload_len;
    }

    if (reader->header_done != 0U && reader->received == reader->expected)
    {
        *complete_out = 1;
    }

    return POTR_OK;
}
