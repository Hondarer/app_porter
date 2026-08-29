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

    for (i = 0; i < depth; i++)
    {
        q->entries[i].peer_id = POTR_PEER_NA;
        q->entries[i].flags = 0;
        q->entries[i].payload_len = 0;
        q->entries[i].payload = q->payload_pool + i * (size_t)max_payload;
    }

    cplat_local_lock_create(&q->mutex);
    cplat_condvar_create(&q->not_empty);
    cplat_condvar_create(&q->not_full);
    cplat_condvar_create(&q->drained);
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_send_queue_dispose(potr_internal_send_queue *q)
{
    cplat_condvar_dispose(q->drained);
    cplat_condvar_dispose(q->not_full);
    cplat_condvar_dispose(q->not_empty);
    cplat_local_lock_dispose(q->mutex);
    cplat_free(q->entries);
    cplat_free(q->payload_pool);
    q->entries = NULL;
    q->payload_pool = NULL;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_send_queue_push(potr_internal_send_queue *q, potr_peer_id peer_id, uint16_t flags, const void *payload,
                         uint16_t payload_len)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);

    if (q->count + q->inflight >= q->depth)
    {
        cplat_local_lock_unlock(q->mutex);
        return POTR_ERR_FULL;
    }

    q->entries[q->tail].peer_id = peer_id;
    q->entries[q->tail].flags = flags;
    q->entries[q->tail].payload_len = payload_len;
    memcpy(q->entries[q->tail].payload, payload, payload_len);
    q->tail = (q->tail + 1U) % q->depth;
    q->count++;

    cplat_condvar_signal(q->not_empty);
    cplat_local_lock_unlock(q->mutex);

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_send_queue_push_wait(potr_internal_send_queue *q, potr_peer_id peer_id, uint16_t flags, const void *payload,
                              uint16_t payload_len, volatile int *running)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);

    /* count + inflight < depth が保証されるまで待機する。
       inflight エントリもプール スロットを占有するため、count だけでは不足。 */
    while (q->count + q->inflight >= q->depth)
    {
        if (!*running)
        {
            cplat_local_lock_unlock(q->mutex);
            return POTR_ERR_CANCELED;
        }
        cplat_condvar_wait(q->not_full, q->mutex, CPLAT_SYNC_WAIT_FOREVER);
    }

    q->entries[q->tail].peer_id = peer_id;
    q->entries[q->tail].flags = flags;
    q->entries[q->tail].payload_len = payload_len;
    memcpy(q->entries[q->tail].payload, payload, payload_len);
    q->tail = (q->tail + 1U) % q->depth;
    q->count++;

    cplat_condvar_signal(q->not_empty);
    cplat_local_lock_unlock(q->mutex);

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_send_queue_pop(potr_internal_send_queue *q, potr_internal_payload_elem *out, volatile int *running)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);

    while (q->count == 0)
    {
        if (!*running)
        {
            cplat_local_lock_unlock(q->mutex);
            return POTR_ERR_CANCELED;
        }
        cplat_condvar_wait(q->not_empty, q->mutex, CPLAT_SYNC_WAIT_FOREVER);
    }

    *out = q->entries[q->head];
    q->head = (q->head + 1U) % q->depth;
    q->count--;
    q->inflight++;

    /* count + inflight は変化しない (count-- と inflight++ が相殺) ため
       not_full シグナルは complete() が担う */
    cplat_local_lock_unlock(q->mutex);
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_send_queue_peek(potr_internal_send_queue *q, potr_internal_payload_elem *out)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);

    if (q->count == 0)
    {
        cplat_local_lock_unlock(q->mutex);
        return POTR_ERR_EMPTY;
    }

    *out = q->entries[q->head]; /* head は送信スレッドのみが変更するので安全 */

    cplat_local_lock_unlock(q->mutex);
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_send_queue_peek_timed(potr_internal_send_queue *q, potr_internal_payload_elem *out, int timeout_ms)
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

    *out = q->entries[q->head];

    cplat_local_lock_unlock(q->mutex);
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_send_queue_try_pop(potr_internal_send_queue *q, potr_internal_payload_elem *out)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);

    if (q->count == 0)
    {
        cplat_local_lock_unlock(q->mutex);
        return POTR_ERR_EMPTY;
    }

    *out = q->entries[q->head];
    q->head = (q->head + 1U) % q->depth;
    q->count--;
    q->inflight++;

    cplat_local_lock_unlock(q->mutex);
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_send_queue_complete(potr_internal_send_queue *q)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);

    if (q->inflight > 0U)
    {
        q->inflight--;
    }

    if (q->count == 0U && q->inflight == 0U)
    {
        cplat_condvar_broadcast(q->drained);
    }

    /* inflight 減少により count + inflight < depth となる可能性があるため
       push_wait で待機中のスレッドを起床させる */
    cplat_condvar_signal(q->not_full);

    cplat_local_lock_unlock(q->mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_send_queue_wait_drained(potr_internal_send_queue *q)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);

    while (q->count > 0U || q->inflight > 0U)
    {
        cplat_condvar_wait(q->drained, q->mutex, CPLAT_SYNC_WAIT_FOREVER);
    }

    cplat_local_lock_unlock(q->mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_send_queue_shutdown(potr_internal_send_queue *q)
{
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);
    cplat_condvar_broadcast(q->not_empty);
    cplat_condvar_broadcast(q->not_full);
    cplat_local_lock_unlock(q->mutex);
}
