/**
 *******************************************************************************
 *  @file           potr_handle.c
 *  @brief          公開ハンドルの登録と使用中参照を管理します。
 *******************************************************************************
 */
#include <stdint.h>
#include <cplat/crt/stdlib.h>
#include <cplat/sync/sync.h>
#include <porter/porter_result.h>
#include <porter/potr_context.h>
#include <porter/potr_handle.h>
#include <porter/potr_path_event.h>

/*
 * 登録表は負荷率 1 以下で拡張し、CLOSED の制御オブジェクトも保持する。
 * 通信資源を解放しても公開値を再利用しないため、古いハンドルが別接続を指さない。
 */
struct potr_internal_handle
{
    struct potr_internal_handle *next;
    potr_context *ctx;
    cplat_local_lock *mutex;
    cplat_condvar *idle;
    size_t refs;
    int state; /* 0: OPEN, 1: CLOSING, 2: CLOSE_FAILED, 3: CLOSED */
    int reserved;
};

static cplat_once_flag s_once;
static cplat_local_lock *s_mutex;
static struct potr_internal_handle **s_buckets;
static size_t s_capacity;
static size_t s_count;

/* 初期化失敗は s_mutex が NULL のままであることを呼び出し元が検出する。 */
static void registry_init(void)
{
    (void)cplat_local_lock_create(&s_mutex);
}

/* 登録表の容量は 2 の累乗であり、ポインター値を混ぜてから下位ビットで索引を求める。 */
static size_t bucket_for(const void *handle, size_t capacity)
{
    uintptr_t value = (uintptr_t)handle;
    value ^= value >> 17U;
    value ^= value >> 9U;

    return (size_t)value & (capacity - 1U);
}

/* 登録表のロックは検索中だけ保持する。登録済みオブジェクト自体は常に生存する。 */
static struct potr_internal_handle *lookup(potr_context *handle)
{
    struct potr_internal_handle *entry = NULL;
    cplat_call_once(&s_once, registry_init);

    if (s_mutex == NULL)
    {
        return NULL;
    }

    cplat_local_lock_lock(s_mutex, CPLAT_SYNC_WAIT_FOREVER);

    if (s_capacity != 0U)
    {
        entry = s_buckets[bucket_for(handle, s_capacity)];
        while (entry != NULL && (void *)entry != (void *)handle)
        {
            entry = entry->next;
        }
    }

    cplat_local_lock_unlock(s_mutex);

    return entry;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_handle_prepare(potr_context *ctx)
{
    struct potr_internal_handle *entry;

    if (ctx == NULL || ctx->handle_owner != NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    cplat_call_once(&s_once, registry_init);

    if (s_mutex == NULL)
    {
        return POTR_ERR_UNKNOWN; /* 同期資源生成 API は詳細原因を提供しない */
    }

    entry = (struct potr_internal_handle *)cplat_calloc(1U, sizeof(*entry));
    if (entry == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    if (cplat_local_lock_create(&entry->mutex) != CPLAT_OK || cplat_condvar_create(&entry->idle) != CPLAT_OK)
    {
        if (entry->mutex != NULL)
        {
            cplat_local_lock_dispose(entry->mutex);
        }

        cplat_free(entry);
        return POTR_ERR_UNKNOWN;
    }

    cplat_local_lock_lock(s_mutex, CPLAT_SYNC_WAIT_FOREVER);

    if (s_count == s_capacity)
    {
        size_t capacity = 64U;
        struct potr_internal_handle **buckets;
        size_t i;

        if (s_capacity != 0U)
        {
            capacity = s_capacity * 2U;
        }

        buckets = NULL;
        if (capacity > s_capacity && capacity <= SIZE_MAX / sizeof(*buckets))
        {
            buckets = (struct potr_internal_handle **)cplat_calloc(capacity, sizeof(*buckets));
        }

        if (buckets == NULL)
        {
            cplat_local_lock_unlock(s_mutex);
            cplat_condvar_dispose(entry->idle);
            cplat_local_lock_dispose(entry->mutex);
            cplat_free(entry);
            return POTR_ERR_OUT_OF_MEMORY;
        }

        for (i = 0U; i < s_capacity; ++i)
        {
            struct potr_internal_handle *item = s_buckets[i];
            while (item != NULL)
            {
                struct potr_internal_handle *next = item->next;
                size_t index = bucket_for(item, capacity);
                item->next = buckets[index];
                buckets[index] = item;
                item = next;
            }
        }

        cplat_free(s_buckets);
        s_buckets = buckets;
        s_capacity = capacity;
    }

    entry->ctx = ctx;

    {
        size_t index = bucket_for(entry, s_capacity);
        entry->next = s_buckets[index];
        s_buckets[index] = entry;
    }

    ++s_count;
    ctx->handle_owner = entry;
    cplat_local_lock_unlock(s_mutex);

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

potr_context *potr_internal_handle_public(const potr_context *ctx)
{
    if (ctx == NULL)
    {
        return NULL;
    }

    return (potr_context *)(void *)ctx->handle_owner;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_handle_discard(potr_context *ctx)
{
    struct potr_internal_handle *entry;
    struct potr_internal_handle **link;

    if (ctx == NULL || ctx->handle_owner == NULL)
    {
        return;
    }

    entry = ctx->handle_owner;
    cplat_local_lock_lock(s_mutex, CPLAT_SYNC_WAIT_FOREVER);
    link = &s_buckets[bucket_for(entry, s_capacity)];
    while (*link != entry)
    {
        link = &(*link)->next;
    }

    *link = entry->next;
    --s_count;
    cplat_local_lock_unlock(s_mutex);
    cplat_condvar_dispose(entry->idle);
    cplat_local_lock_dispose(entry->mutex);
    cplat_free(entry);
    ctx->handle_owner = NULL;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_handle_acquire(potr_context *handle, potr_context **ctx)
{
    struct potr_internal_handle *entry;
    int ret = POTR_OK;

    if (ctx == NULL || handle == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    entry = lookup(handle);
    if (entry == NULL)
    {
        return POTR_ERR_INVALID_HANDLE;
    }

    cplat_local_lock_lock(entry->mutex, CPLAT_SYNC_WAIT_FOREVER);

    if (entry->ctx == NULL)
    {
        ret = POTR_ERR_INVALID_HANDLE;
    }
    else if (cplat_atomic_load_i32(&entry->ctx->fatal_error, CPLAT_MEMORY_ORDER_ACQUIRE) != POTR_OK)
    {
        ret = POTR_ERR_OUT_OF_MEMORY;
    }
    else if (entry->state != 0)
    {
        ret = POTR_ERR_CANCELED;
    }
    else
    {
        ++entry->refs;
        *ctx = entry->ctx;
    }

    cplat_local_lock_unlock(entry->mutex);

    return ret;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_handle_release(potr_context *ctx)
{
    struct potr_internal_handle *entry;

    if (ctx == NULL || ctx->handle_owner == NULL)
    {
        return;
    }

    entry = ctx->handle_owner;
    cplat_local_lock_lock(entry->mutex, CPLAT_SYNC_WAIT_FOREVER);
    --entry->refs;

    if (entry->refs == 0U)
    {
        cplat_condvar_broadcast(entry->idle);
    }

    cplat_local_lock_unlock(entry->mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_handle_begin_close(potr_context *handle, potr_context **ctx)
{
    struct potr_internal_handle *entry;
    int ret = POTR_OK;

    if (ctx == NULL || handle == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    entry = lookup(handle);
    if (entry == NULL)
    {
        return POTR_ERR_INVALID_HANDLE;
    }

    cplat_local_lock_lock(entry->mutex, CPLAT_SYNC_WAIT_FOREVER);

    if (entry->ctx == NULL)
    {
        ret = POTR_ERR_INVALID_HANDLE;
    }
    else if (potr_internal_in_own_callback(entry->ctx))
    {
        ret = POTR_ERR_UNSUPPORTED;
    }
    else if (entry->state == 1)
    {
        ret = POTR_ERR_CANCELED;
    }
    else
    {
        /* 新規参照を拒否してから先行 API の返却を待ち、通信資源を回収できる状態にする。 */
        entry->state = 1;

        while (entry->refs != 0U)
        {
            cplat_condvar_wait(entry->idle, entry->mutex, CPLAT_SYNC_WAIT_FOREVER);
        }

        *ctx = entry->ctx;
        cplat_atomic_store_i32(&(*ctx)->close_requested, 1, CPLAT_MEMORY_ORDER_RELEASE);
    }

    cplat_local_lock_unlock(entry->mutex);

    return ret;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_handle_end_close(potr_context *ctx, int closed)
{
    struct potr_internal_handle *entry;

    if (ctx == NULL || ctx->handle_owner == NULL)
    {
        return;
    }

    entry = ctx->handle_owner;
    cplat_local_lock_lock(entry->mutex, CPLAT_SYNC_WAIT_FOREVER);

    if (closed != 0)
    {
        entry->ctx = NULL;
        entry->state = 3;
    }
    else
    {
        entry->state = 2;
    }

    cplat_local_lock_unlock(entry->mutex);
}
