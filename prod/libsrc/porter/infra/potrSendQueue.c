/**
 *******************************************************************************
 *  @file           potrSendQueue.c
 *  @brief          非同期送信キューの実装。
 *  @author         Tetsuo Honda
 *  @date           2026/03/08
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>
#include <stdlib.h>
#include <string.h>

#include <porter_const.h>

#include <porter/infra/potrSendQueue.h>
#include <porter/infra/potrPlatform.h>

/* doxygen コメントは、ヘッダーに記載 */
int potr_send_queue_init(PotrSendQueue *q, size_t depth, uint16_t max_payload)
{
    size_t i;

    memset(q, 0, sizeof(*q));

    q->entries      = (PotrPayloadElem *)malloc(depth * sizeof(PotrPayloadElem));
    q->payload_pool = (uint8_t *)malloc(depth * (size_t)max_payload);

    if (q->entries == NULL || q->payload_pool == NULL)
    {
        free(q->entries);
        free(q->payload_pool);
        q->entries      = NULL;
        q->payload_pool = NULL;
        return POTR_ERROR;
    }

    q->depth = depth;

    for (i = 0; i < depth; i++)
    {
        q->entries[i].peer_id     = POTR_PEER_NA;
        q->entries[i].flags       = 0;
        q->entries[i].payload_len = 0;
        q->entries[i].payload     = q->payload_pool + i * (size_t)max_payload;
    }

    com_util_local_lock_create(&q->mutex);
    com_util_condvar_create(&q->not_empty);
    com_util_condvar_create(&q->not_full);
    com_util_condvar_create(&q->drained);
    return POTR_SUCCESS;
}

/* doxygen コメントは、ヘッダーに記載 */
void potr_send_queue_dispose(PotrSendQueue *q)
{
    com_util_condvar_destroy(q->drained);
    com_util_condvar_destroy(q->not_full);
    com_util_condvar_destroy(q->not_empty);
    com_util_local_lock_destroy(q->mutex);
    free(q->entries);
    free(q->payload_pool);
    q->entries      = NULL;
    q->payload_pool = NULL;
}

/* doxygen コメントは、ヘッダーに記載 */
int potr_send_queue_push(PotrSendQueue *q, PotrPeerId peer_id,
                         uint16_t flags,
                         const void *payload, uint16_t payload_len)
{
    com_util_local_lock_lock(q->mutex, COM_UTIL_SYNC_WAIT_FOREVER);

    if (q->count + q->inflight >= q->depth)
    {
        com_util_local_lock_unlock(q->mutex);
        return POTR_ERROR;
    }

    q->entries[q->tail].peer_id     = peer_id;
    q->entries[q->tail].flags       = flags;
    q->entries[q->tail].payload_len = payload_len;
    memcpy(q->entries[q->tail].payload, payload, payload_len);
    q->tail = (q->tail + 1U) % q->depth;
    q->count++;

    com_util_condvar_signal(q->not_empty);
    com_util_local_lock_unlock(q->mutex);

    return POTR_SUCCESS;
}

/* doxygen コメントは、ヘッダーに記載 */
int potr_send_queue_push_wait(PotrSendQueue *q, PotrPeerId peer_id,
                              uint16_t flags,
                              const void *payload, uint16_t payload_len,
                              volatile int *running)
{
    com_util_local_lock_lock(q->mutex, COM_UTIL_SYNC_WAIT_FOREVER);

    /* count + inflight < depth が保証されるまで待機する。
       inflight エントリもプールスロットを占有するため、count だけでは不足。 */
    while (q->count + q->inflight >= q->depth)
    {
        if (!*running)
        {
            com_util_local_lock_unlock(q->mutex);
            return POTR_ERROR;
        }
        com_util_condvar_wait(q->not_full, q->mutex, COM_UTIL_SYNC_WAIT_FOREVER);
    }

    q->entries[q->tail].peer_id     = peer_id;
    q->entries[q->tail].flags       = flags;
    q->entries[q->tail].payload_len = payload_len;
    memcpy(q->entries[q->tail].payload, payload, payload_len);
    q->tail = (q->tail + 1U) % q->depth;
    q->count++;

    com_util_condvar_signal(q->not_empty);
    com_util_local_lock_unlock(q->mutex);

    return POTR_SUCCESS;
}

/* doxygen コメントは、ヘッダーに記載 */
int potr_send_queue_pop(PotrSendQueue *q, PotrPayloadElem *out, volatile int *running)
{
    com_util_local_lock_lock(q->mutex, COM_UTIL_SYNC_WAIT_FOREVER);

    while (q->count == 0)
    {
        if (!*running)
        {
            com_util_local_lock_unlock(q->mutex);
            return POTR_ERROR;
        }
        com_util_condvar_wait(q->not_empty, q->mutex, COM_UTIL_SYNC_WAIT_FOREVER);
    }

    *out    = q->entries[q->head];
    q->head = (q->head + 1U) % q->depth;
    q->count--;
    q->inflight++;

    /* count + inflight は変化しない (count-- と inflight++ が相殺) ため
       not_full シグナルは complete() が担う */
    com_util_local_lock_unlock(q->mutex);
    return POTR_SUCCESS;
}

/* doxygen コメントは、ヘッダーに記載 */
int potr_send_queue_peek(PotrSendQueue *q, PotrPayloadElem *out)
{
    com_util_local_lock_lock(q->mutex, COM_UTIL_SYNC_WAIT_FOREVER);

    if (q->count == 0)
    {
        com_util_local_lock_unlock(q->mutex);
        return POTR_ERROR;
    }

    *out = q->entries[q->head]; /* head は送信スレッドのみが変更するので安全 */

    com_util_local_lock_unlock(q->mutex);
    return POTR_SUCCESS;
}

/* doxygen コメントは、ヘッダーに記載 */
int potr_send_queue_peek_timed(PotrSendQueue *q, PotrPayloadElem *out,
                               int timeout_ms)
{
    com_util_local_lock_lock(q->mutex, COM_UTIL_SYNC_WAIT_FOREVER);

    if (q->count == 0)
    {
        com_util_condvar_wait(q->not_empty, q->mutex, timeout_ms);
    }

    if (q->count == 0)
    {
        com_util_local_lock_unlock(q->mutex);
        return POTR_ERROR;
    }

    *out = q->entries[q->head];

    com_util_local_lock_unlock(q->mutex);
    return POTR_SUCCESS;
}

/* doxygen コメントは、ヘッダーに記載 */
int potr_send_queue_try_pop(PotrSendQueue *q, PotrPayloadElem *out)
{
    com_util_local_lock_lock(q->mutex, COM_UTIL_SYNC_WAIT_FOREVER);

    if (q->count == 0)
    {
        com_util_local_lock_unlock(q->mutex);
        return POTR_ERROR;
    }

    *out    = q->entries[q->head];
    q->head = (q->head + 1U) % q->depth;
    q->count--;
    q->inflight++;

    com_util_local_lock_unlock(q->mutex);
    return POTR_SUCCESS;
}

/* doxygen コメントは、ヘッダーに記載 */
void potr_send_queue_complete(PotrSendQueue *q)
{
    com_util_local_lock_lock(q->mutex, COM_UTIL_SYNC_WAIT_FOREVER);

    if (q->inflight > 0U)
    {
        q->inflight--;
    }

    if (q->count == 0U && q->inflight == 0U)
    {
        com_util_condvar_broadcast(q->drained);
    }

    /* inflight 減少により count + inflight < depth となる可能性があるため
       push_wait で待機中のスレッドを起床させる */
    com_util_condvar_signal(q->not_full);

    com_util_local_lock_unlock(q->mutex);
}

/* doxygen コメントは、ヘッダーに記載 */
void potr_send_queue_wait_drained(PotrSendQueue *q)
{
    com_util_local_lock_lock(q->mutex, COM_UTIL_SYNC_WAIT_FOREVER);

    while (q->count > 0U || q->inflight > 0U)
    {
        com_util_condvar_wait(q->drained, q->mutex, COM_UTIL_SYNC_WAIT_FOREVER);
    }

    com_util_local_lock_unlock(q->mutex);
}

/* doxygen コメントは、ヘッダーに記載 */
void potr_send_queue_shutdown(PotrSendQueue *q)
{
    com_util_local_lock_lock(q->mutex, COM_UTIL_SYNC_WAIT_FOREVER);
    com_util_condvar_broadcast(q->not_empty);
    com_util_condvar_broadcast(q->not_full);
    com_util_local_lock_unlock(q->mutex);
}
