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

#include "potrSendQueue.h"
#include "potrPlatform.h"

/* doxygen コメントは、ヘッダに記載 */
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

    COM_UTIL_MUTEX_INIT(&q->mutex);
    COM_UTIL_COND_INIT(&q->not_empty);
    COM_UTIL_COND_INIT(&q->not_full);
    COM_UTIL_COND_INIT(&q->drained);
    return POTR_SUCCESS;
}

/* doxygen コメントは、ヘッダに記載 */
void potr_send_queue_destroy(PotrSendQueue *q)
{
    COM_UTIL_COND_DESTROY(&q->drained);
    COM_UTIL_COND_DESTROY(&q->not_full);
    COM_UTIL_COND_DESTROY(&q->not_empty);
    COM_UTIL_MUTEX_DESTROY(&q->mutex);
    free(q->entries);
    free(q->payload_pool);
    q->entries      = NULL;
    q->payload_pool = NULL;
}

/* doxygen コメントは、ヘッダに記載 */
int potr_send_queue_push(PotrSendQueue *q, PotrPeerId peer_id,
                         uint16_t flags,
                         const void *payload, uint16_t payload_len)
{
    COM_UTIL_MUTEX_LOCK(&q->mutex);

    if (q->count + q->inflight >= q->depth)
    {
        COM_UTIL_MUTEX_UNLOCK(&q->mutex);
        return POTR_ERROR;
    }

    q->entries[q->tail].peer_id     = peer_id;
    q->entries[q->tail].flags       = flags;
    q->entries[q->tail].payload_len = payload_len;
    memcpy(q->entries[q->tail].payload, payload, payload_len);
    q->tail = (q->tail + 1U) % q->depth;
    q->count++;

    COM_UTIL_COND_SIGNAL(&q->not_empty);
    COM_UTIL_MUTEX_UNLOCK(&q->mutex);

    return POTR_SUCCESS;
}

/* doxygen コメントは、ヘッダに記載 */
int potr_send_queue_push_wait(PotrSendQueue *q, PotrPeerId peer_id,
                              uint16_t flags,
                              const void *payload, uint16_t payload_len,
                              volatile int *running)
{
    COM_UTIL_MUTEX_LOCK(&q->mutex);

    /* count + inflight < depth が保証されるまで待機する。
       inflight エントリもプールスロットを占有するため、count だけでは不足。 */
    while (q->count + q->inflight >= q->depth)
    {
        if (!*running)
        {
            COM_UTIL_MUTEX_UNLOCK(&q->mutex);
            return POTR_ERROR;
        }
        COM_UTIL_COND_WAIT(&q->not_full, &q->mutex);
    }

    q->entries[q->tail].peer_id     = peer_id;
    q->entries[q->tail].flags       = flags;
    q->entries[q->tail].payload_len = payload_len;
    memcpy(q->entries[q->tail].payload, payload, payload_len);
    q->tail = (q->tail + 1U) % q->depth;
    q->count++;

    COM_UTIL_COND_SIGNAL(&q->not_empty);
    COM_UTIL_MUTEX_UNLOCK(&q->mutex);

    return POTR_SUCCESS;
}

/* doxygen コメントは、ヘッダに記載 */
int potr_send_queue_pop(PotrSendQueue *q, PotrPayloadElem *out, volatile int *running)
{
    COM_UTIL_MUTEX_LOCK(&q->mutex);

    while (q->count == 0)
    {
        if (!*running)
        {
            COM_UTIL_MUTEX_UNLOCK(&q->mutex);
            return POTR_ERROR;
        }
        COM_UTIL_COND_WAIT(&q->not_empty, &q->mutex);
    }

    *out    = q->entries[q->head];
    q->head = (q->head + 1U) % q->depth;
    q->count--;
    q->inflight++;

    /* count + inflight は変化しない (count-- と inflight++ が相殺) ため
       not_full シグナルは complete() が担う */
    COM_UTIL_MUTEX_UNLOCK(&q->mutex);
    return POTR_SUCCESS;
}

/* doxygen コメントは、ヘッダに記載 */
int potr_send_queue_peek(PotrSendQueue *q, PotrPayloadElem *out)
{
    COM_UTIL_MUTEX_LOCK(&q->mutex);

    if (q->count == 0)
    {
        COM_UTIL_MUTEX_UNLOCK(&q->mutex);
        return POTR_ERROR;
    }

    *out = q->entries[q->head]; /* head は送信スレッドのみが変更するので安全 */

    COM_UTIL_MUTEX_UNLOCK(&q->mutex);
    return POTR_SUCCESS;
}

/* doxygen コメントは、ヘッダに記載 */
int potr_send_queue_peek_timed(PotrSendQueue *q, PotrPayloadElem *out,
                               uint32_t timeout_ms)
{
    COM_UTIL_MUTEX_LOCK(&q->mutex);

    if (q->count == 0)
    {
        com_util_condvar_timedwait(&q->not_empty, &q->mutex, timeout_ms);
    }

    if (q->count == 0)
    {
        COM_UTIL_MUTEX_UNLOCK(&q->mutex);
        return POTR_ERROR;
    }

    *out = q->entries[q->head];

    COM_UTIL_MUTEX_UNLOCK(&q->mutex);
    return POTR_SUCCESS;
}

/* doxygen コメントは、ヘッダに記載 */
int potr_send_queue_try_pop(PotrSendQueue *q, PotrPayloadElem *out)
{
    COM_UTIL_MUTEX_LOCK(&q->mutex);

    if (q->count == 0)
    {
        COM_UTIL_MUTEX_UNLOCK(&q->mutex);
        return POTR_ERROR;
    }

    *out    = q->entries[q->head];
    q->head = (q->head + 1U) % q->depth;
    q->count--;
    q->inflight++;

    COM_UTIL_MUTEX_UNLOCK(&q->mutex);
    return POTR_SUCCESS;
}

/* doxygen コメントは、ヘッダに記載 */
void potr_send_queue_complete(PotrSendQueue *q)
{
    COM_UTIL_MUTEX_LOCK(&q->mutex);

    if (q->inflight > 0U)
    {
        q->inflight--;
    }

    if (q->count == 0U && q->inflight == 0U)
    {
        COM_UTIL_COND_BROADCAST(&q->drained);
    }

    /* inflight 減少により count + inflight < depth となる可能性があるため
       push_wait で待機中のスレッドを起床させる */
    COM_UTIL_COND_SIGNAL(&q->not_full);

    COM_UTIL_MUTEX_UNLOCK(&q->mutex);
}

/* doxygen コメントは、ヘッダに記載 */
void potr_send_queue_wait_drained(PotrSendQueue *q)
{
    COM_UTIL_MUTEX_LOCK(&q->mutex);

    while (q->count > 0U || q->inflight > 0U)
    {
        COM_UTIL_COND_WAIT(&q->drained, &q->mutex);
    }

    COM_UTIL_MUTEX_UNLOCK(&q->mutex);
}

/* doxygen コメントは、ヘッダに記載 */
void potr_send_queue_shutdown(PotrSendQueue *q)
{
    COM_UTIL_MUTEX_LOCK(&q->mutex);
    COM_UTIL_COND_BROADCAST(&q->not_empty);
    COM_UTIL_COND_BROADCAST(&q->not_full);
    COM_UTIL_MUTEX_UNLOCK(&q->mutex);
}
