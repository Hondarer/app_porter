/**
 *******************************************************************************
 *  @file           potr_send_queue.c
 *  @brief          非同期送信キューを管理する機能を実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/08
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/base/platform.h>
#include <cplat/crt/stdlib.h>
#include <stdlib.h>
#include <string.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>

#include <porter/infra/potr_send_queue.h>

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_send_queue_init(potr_internal_send_queue *q, size_t depth, uint16_t max_payload)
{
    size_t i;

    memset(q, 0, sizeof(*q));

    q->entries = (potr_internal_payload_elem *)cplat_calloc(depth, sizeof(potr_internal_payload_elem));
    q->payload_pool = (uint8_t *)cplat_calloc(depth, (size_t)max_payload);

    if (q->entries == NULL || q->payload_pool == NULL)
    {
        cplat_free(q->entries);
        cplat_free(q->payload_pool);
        q->entries = NULL;
        q->payload_pool = NULL;
        return POTR_ERR_OUT_OF_MEMORY;
    }

    q->depth = depth;

    /* 各エントリを初期化し、ペイロード領域を割り当てる。 */
    for (i = 0; i < depth; i++)
    {
        q->entries[i].peer_id = POTR_PEER_NA;
        q->entries[i].flags = 0;
        q->entries[i].payload_len = 0;
        q->entries[i].payload = q->payload_pool + i * (size_t)max_payload;
    }

    /* 同期資源生成 API に詳細原因がないため UNKNOWN として回収する。 */
    if (cplat_local_lock_create(&q->mutex) != CPLAT_OK || cplat_local_lock_create(&q->push_mutex) != CPLAT_OK ||
        cplat_condvar_create(&q->not_empty) != CPLAT_OK || cplat_condvar_create(&q->not_full) != CPLAT_OK ||
        cplat_condvar_create(&q->drained) != CPLAT_OK || cplat_condvar_create(&q->progress) != CPLAT_OK)
    {
        potr_internal_send_queue_dispose(q);
        return POTR_ERR_UNKNOWN;
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_send_queue_dispose(potr_internal_send_queue *q)
{
    if (q->progress != NULL)
    {
        cplat_condvar_dispose(q->progress);
        q->progress = NULL;
    }

    if (q->drained != NULL)
    {
        cplat_condvar_dispose(q->drained);
        q->drained = NULL;
    }

    if (q->not_full != NULL)
    {
        cplat_condvar_dispose(q->not_full);
        q->not_full = NULL;
    }

    if (q->not_empty != NULL)
    {
        cplat_condvar_dispose(q->not_empty);
        q->not_empty = NULL;
    }

    if (q->push_mutex != NULL)
    {
        cplat_local_lock_dispose(q->push_mutex);
        q->push_mutex = NULL;
    }

    if (q->mutex != NULL)
    {
        cplat_local_lock_dispose(q->mutex);
        q->mutex = NULL;
    }

    cplat_free(q->entries);
    cplat_free(q->payload_pool);
    q->entries = NULL;
    q->payload_pool = NULL;
}

/* Doxygen コメントは、ヘッダーに記載 */

/* 空きがあることを確認済みのキューへエントリを書き込む。呼び出し元は q->mutex を保持する。 */
static void push_locked(potr_internal_send_queue *q, potr_peer_id peer_id, uint16_t flags, const void *payload,
                        uint16_t payload_len, uint64_t *ticket)
{
    q->entries[q->tail].peer_id = peer_id;
    q->entries[q->tail].flags = flags;
    q->entries[q->tail].payload_len = payload_len;
    memcpy(q->entries[q->tail].payload, payload, payload_len);
    q->tail = (q->tail + 1U) % q->depth;
    q->count++;
    q->pushed_seq++;

    if (ticket != NULL)
    {
        *ticket = q->pushed_seq;
    }

    cplat_condvar_signal(q->not_empty);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_send_queue_push(potr_internal_send_queue *q, potr_peer_id peer_id, uint16_t flags,
                                  const void *payload, uint16_t payload_len, uint64_t *ticket)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);

    if (q->count + q->inflight >= q->depth)
    {
        cplat_local_lock_unlock(q->mutex);
        return POTR_ERR_FULL;
    }

    if (q->failure != POTR_OK)
    {
        int ret = q->failure;
        cplat_local_lock_unlock(q->mutex);
        return ret;
    }

    push_locked(q, peer_id, flags, payload, payload_len, ticket);
    cplat_local_lock_unlock(q->mutex);

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_send_queue_push_wait(potr_internal_send_queue *q, potr_peer_id peer_id, uint16_t flags,
                                       const void *payload, uint16_t payload_len, cplat_atomic_i32 *running,
                                       uint64_t *ticket)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);

    /*
     * count + inflight < depth が保証されるまで待機する。
     * inflight エントリもプール スロットを占有するため、count だけでは不足。
     */
    while (q->failure == POTR_OK && q->count + q->inflight >= q->depth)
    {
        if (cplat_atomic_load_i32(running, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
        {
            cplat_local_lock_unlock(q->mutex);
            return POTR_ERR_CANCELED;
        }

        cplat_condvar_wait(q->not_full, q->mutex, CPLAT_SYNC_WAIT_FOREVER);
    }

    if (q->failure != POTR_OK)
    {
        int ret = q->failure;
        cplat_local_lock_unlock(q->mutex);
        return ret;
    }

    push_locked(q, peer_id, flags, payload, payload_len, ticket);
    cplat_local_lock_unlock(q->mutex);

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_send_queue_pop(potr_internal_send_queue *q, potr_internal_payload_elem *item_out,
                                 cplat_atomic_i32 *running)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);

    while (q->failure == POTR_OK && q->count == 0)
    {
        if (cplat_atomic_load_i32(running, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
        {
            cplat_local_lock_unlock(q->mutex);
            return POTR_ERR_CANCELED;
        }

        cplat_condvar_wait(q->not_empty, q->mutex, CPLAT_SYNC_WAIT_FOREVER);
    }

    if (q->failure != POTR_OK)
    {
        int ret = q->failure;
        cplat_local_lock_unlock(q->mutex);
        return ret;
    }

    *item_out = q->entries[q->head];
    q->head = (q->head + 1U) % q->depth;
    q->count--;
    q->inflight++;

    /*
     * count + inflight は変化しない (count-- と inflight++ が相殺する) ため、
     * not_full シグナルは complete() が担う。
     */
    cplat_local_lock_unlock(q->mutex);

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_send_queue_peek(potr_internal_send_queue *q, potr_internal_payload_elem *item_out)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);

    if (q->count == 0)
    {
        cplat_local_lock_unlock(q->mutex);
        return POTR_ERR_EMPTY;
    }

    if (q->failure != POTR_OK)
    {
        int ret = q->failure;
        cplat_local_lock_unlock(q->mutex);
        return ret;
    }

    *item_out = q->entries[q->head]; /* head は送信スレッドのみが変更するので安全 */

    cplat_local_lock_unlock(q->mutex);

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_send_queue_peek_timed(potr_internal_send_queue *q, potr_internal_payload_elem *item_out,
                                        int timeout_ms)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);

    if (q->count == 0)
    {
        cplat_condvar_wait(q->not_empty, q->mutex, timeout_ms);
    }

    if (q->count == 0)
    {
        cplat_local_lock_unlock(q->mutex);
        return POTR_ERR_TIMEOUT;
    }

    if (q->failure != POTR_OK)
    {
        int ret = q->failure;
        cplat_local_lock_unlock(q->mutex);
        return ret;
    }

    *item_out = q->entries[q->head];

    cplat_local_lock_unlock(q->mutex);

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_send_queue_try_pop(potr_internal_send_queue *q, potr_internal_payload_elem *item_out)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);

    if (q->count == 0)
    {
        cplat_local_lock_unlock(q->mutex);
        return POTR_ERR_EMPTY;
    }

    if (q->failure != POTR_OK)
    {
        int ret = q->failure;
        cplat_local_lock_unlock(q->mutex);
        return ret;
    }

    *item_out = q->entries[q->head];
    q->head = (q->head + 1U) % q->depth;
    q->count--;
    q->inflight++;

    cplat_local_lock_unlock(q->mutex);

    return POTR_OK;
}

/* inflight エントリを 1 つ完了とし、待機中のスレッドを起床させる。呼び出し元は mutex を保持する。 */
static void complete_locked(potr_internal_send_queue *q)
{
    if (q->inflight > 0U)
    {
        q->inflight--;
    }

    /* 送信スレッドは取り出した順に完了させるため、completed_seq 以下の番号はすべて処理済みである。 */
    q->completed_seq++;
    cplat_condvar_broadcast(q->progress);

    if (q->count == 0U && q->inflight == 0U)
    {
        cplat_condvar_broadcast(q->drained);
    }

    /*
     * inflight の減少により count + inflight < depth となる可能性があるため、
     * push_wait で待機中のスレッドを起床させる。
     */
    cplat_condvar_signal(q->not_full);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_send_queue_complete(potr_internal_send_queue *q)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);
    complete_locked(q);
    cplat_local_lock_unlock(q->mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_send_queue_complete_failed(potr_internal_send_queue *q, int error)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);

    /* 完了数に 1 を足した番号が、これから完了するエントリの番号である。 */
    q->failed_seq = q->completed_seq + 1U;
    q->failed_result = error;
    complete_locked(q);
    cplat_local_lock_unlock(q->mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_send_queue_wait_drained(potr_internal_send_queue *q)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);

    while (q->failure == POTR_OK && (q->count > 0U || q->inflight > 0U))
    {
        cplat_condvar_wait(q->drained, q->mutex, CPLAT_SYNC_WAIT_FOREVER);
    }

    cplat_local_lock_unlock(q->mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_send_queue_wait_ticket(potr_internal_send_queue *q, uint64_t ticket)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);

    while (q->failure == POTR_OK && q->completed_seq < ticket)
    {
        cplat_condvar_wait(q->progress, q->mutex, CPLAT_SYNC_WAIT_FOREVER);
    }

    cplat_local_lock_unlock(q->mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_send_queue_ticket_result(potr_internal_send_queue *q, uint64_t first_ticket)
{
    int result = POTR_OK;

    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);

    if (q->failed_seq != 0U && q->failed_seq >= first_ticket)
    {
        result = q->failed_result;
    }

    cplat_local_lock_unlock(q->mutex);

    return result;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_send_queue_lock_push(potr_internal_send_queue *q)
{
    cplat_local_lock_lock(q->push_mutex, CPLAT_SYNC_WAIT_FOREVER);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_send_queue_unlock_push(potr_internal_send_queue *q)
{
    cplat_local_lock_unlock(q->push_mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_send_queue_shutdown(potr_internal_send_queue *q)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);
    cplat_condvar_broadcast(q->not_empty);
    cplat_condvar_broadcast(q->not_full);
    cplat_local_lock_unlock(q->mutex);
}
