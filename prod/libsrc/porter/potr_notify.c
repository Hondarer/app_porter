/**
 *******************************************************************************
 *  @file           potr_notify.c
 *  @brief          動的通知キューと通知専用スレッドを管理します。
 *******************************************************************************
 */
#include <string.h>
#include <stdint.h>
#include <cplat/crt/stdlib.h>
#include <cplat/runtime/process.h>
#include <cplat/runtime/memory_lock.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/potr_context.h>
#include <porter/potr_notify.h>

typedef struct potr_internal_notice
{
    struct potr_internal_notice *next;
    size_t len;
    size_t capacity;
    size_t bytes;
    potr_peer_id peer_id;
    potr_event event;
} potr_internal_notice;

struct potr_internal_notifications
{
    cplat_local_lock *mutex;
    cplat_condvar *ready;
    cplat_thread *thread;
    potr_internal_notice *head;
    potr_internal_notice *tail;
    potr_internal_notice *free_nodes;
    potr_internal_notice error_notice;
    int error_code;
    int sealed;
};

/* 登録済みの連続した通知をまとめて取り出す。コールバック中はロックを保持しない。
 * ノードの返却もまとめ、通知 1 件ごとのロック取得と起床の追加コストを抑える。 */
static void notify_thread(void *arg)
{
    potr_context *ctx = (potr_context *)arg;
    struct potr_internal_notifications *q = ctx->notifications;
    cplat_atomic_store_u32(&ctx->callback_tid, cplat_process_get_tid(), CPLAT_MEMORY_ORDER_RELAXED);
    for (;;)
    {
        potr_internal_notice *node;
        potr_internal_notice *returned = NULL;
        potr_internal_notice *returned_tail = NULL;
        cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);
        while (q->head == NULL && q->sealed == 0)
        {
            cplat_condvar_wait(q->ready, q->mutex, CPLAT_SYNC_WAIT_FOREVER);
        }
        node = q->head;
        if (node == NULL)
        {
            cplat_local_lock_unlock(q->mutex);
            break;
        }
        q->head = NULL;
        q->tail = NULL;
        cplat_local_lock_unlock(q->mutex);
        while (node != NULL)
        {
            potr_internal_notice *next = node->next;
            const void *data = NULL;
            if (node == &q->error_notice)
            {
                data = &q->error_code;
            }
            else if (node->bytes != 0U)
            {
                data = node + 1;
            }
            cplat_atomic_store_i32(&ctx->callback_active, 1, CPLAT_MEMORY_ORDER_RELEASE);
            ctx->callback(ctx->service.service_id, node->peer_id, node->event, data, node->len);
            cplat_atomic_store_i32(&ctx->callback_active, 0, CPLAT_MEMORY_ORDER_RELEASE);
            if (node != &q->error_notice)
            {
                cplat_secure_zero(node + 1, node->bytes);
                node->next = returned;
                returned = node;
                if (returned_tail == NULL)
                {
                    returned_tail = node;
                }
            }
            node = next;
        }
        if (returned_tail != NULL)
        {
            cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);
            returned_tail->next = q->free_nodes;
            q->free_nodes = returned;
            cplat_local_lock_unlock(q->mutex);
        }
    }
}
/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_notify_start(potr_context *ctx)
{
    struct potr_internal_notifications *q;
    if (ctx == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (ctx->notifications != NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (ctx->callback == NULL)
    {
        return POTR_OK;
    }
    q = (struct potr_internal_notifications *)cplat_calloc(1U, sizeof(*q));
    if (q == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }
    if (cplat_local_lock_create(&q->mutex) != CPLAT_OK || cplat_condvar_create(&q->ready) != CPLAT_OK)
    {
        if (q->mutex != NULL)
        {
            cplat_local_lock_dispose(q->mutex);
        }
        cplat_free(q);
        return POTR_ERR_UNKNOWN; /* 同期資源生成 API は詳細原因を提供しない。 */
    }
    ctx->notifications = q;
    if (cplat_thread_create(&q->thread, notify_thread, ctx) != CPLAT_OK)
    {
        cplat_condvar_dispose(q->ready);
        cplat_local_lock_dispose(q->mutex);
        cplat_free(q);
        ctx->notifications = NULL;
        return POTR_ERR_UNKNOWN;
    }
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_notify_stop(potr_context *ctx)
{
    struct potr_internal_notifications *q;
    potr_internal_notice *node;
    if (ctx == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    q = ctx->notifications;
    if (q == NULL)
    {
        return POTR_OK;
    }
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);
    q->sealed = 1;
    cplat_condvar_broadcast(q->ready);
    cplat_local_lock_unlock(q->mutex);
    if (cplat_thread_join(q->thread, CPLAT_SYNC_WAIT_FOREVER) != CPLAT_OK)
    {
        return POTR_ERR_UNKNOWN;
    }
    node = q->free_nodes;
    while (node != NULL)
    {
        potr_internal_notice *next = node->next;
        cplat_free(node);
        node = next;
    }
    cplat_condvar_dispose(q->ready);
    cplat_local_lock_dispose(q->mutex);
    cplat_free(q);
    ctx->notifications = NULL;
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_notify_enqueue(potr_context *ctx, potr_peer_id peer_id, potr_event event, const void *data,
                                  size_t len)
{
    struct potr_internal_notifications *q;
    potr_internal_notice *node;
    potr_internal_notice **link;
    size_t bytes = 0U;
    int failed = 0;
    int wake = 0;
    if (ctx == NULL || ctx->notifications == NULL)
    {
        return;
    }
    q = ctx->notifications;
    if (event == POTR_EVENT_DATA)
    {
        bytes = len;
    }
    else if (event == POTR_EVENT_PATH_CONNECTED || event == POTR_EVENT_PATH_DISCONNECTED)
    {
        if (len >= POTR_MAX_PATH)
        {
            return;
        }
        bytes = sizeof(int) * POTR_MAX_PATH;
    }
    else if (event != POTR_EVENT_CONNECTED && event != POTR_EVENT_DISCONNECTED)
    {
        return;
    }
    if (bytes != 0U && data == NULL)
    {
        return;
    }
    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);
    if (q->sealed != 0 || q->error_code != POTR_OK)
    {
        cplat_local_lock_unlock(q->mutex);
        return;
    }
    link = &q->free_nodes;
    while (*link != NULL && (*link)->capacity < bytes)
    {
        link = &(*link)->next;
    }
    node = *link;
    if (node != NULL)
    {
        *link = node->next;
    }
    else if (bytes <= SIZE_MAX - sizeof(*node))
    {
        node = (potr_internal_notice *)cplat_malloc(sizeof(*node) + bytes);
        if (node != NULL)
        {
            node->capacity = bytes;
        }
    }
    if (node == NULL)
    {
        /* 予約済みノードは通常の free list に入れない。追加確保なしで末尾へ登録する。 */
        node = &q->error_notice;
        q->error_code = POTR_ERR_OUT_OF_MEMORY;
        node->event = POTR_EVENT_ERROR;
        node->peer_id = POTR_PEER_NA;
        node->len = sizeof(q->error_code);
        node->bytes = 0U;
        cplat_atomic_store_i32(&ctx->fatal_error, POTR_ERR_OUT_OF_MEMORY, CPLAT_MEMORY_ORDER_RELEASE);
        failed = 1;
    }
    else
    {
        node->event = event;
        node->peer_id = peer_id;
        node->len = len;
        node->bytes = bytes;
        if (bytes != 0U)
        {
            memcpy(node + 1, data, bytes);
        }
    }
    node->next = NULL;
    if (q->tail != NULL)
    {
        q->tail->next = node;
    }
    else
    {
        q->head = node;
        wake = 1;
    }
    q->tail = node;
    if (wake != 0)
    {
        cplat_condvar_signal(q->ready);
    }
    cplat_local_lock_unlock(q->mutex);
    if (failed != 0 && ctx->send_queue.mutex != NULL)
    {
        potr_internal_send_queue_abort(&ctx->send_queue, POTR_ERR_OUT_OF_MEMORY);
    }
}
