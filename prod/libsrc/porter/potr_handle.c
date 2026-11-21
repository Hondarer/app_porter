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

/** 登録表のエントリの状態。 */
typedef enum handle_entry_state
{
    HANDLE_STATE_OPEN = 0,    /**< 利用できる。確保直後のゼロ初期化でこの状態になる。 */
    HANDLE_STATE_CLOSING = 1, /**< close を開始し、新しい利用を受け付けない。 */
    HANDLE_STATE_CLOSED = 2   /**< close を完了した。公開値を再利用しないため、エントリは残す。 */
} handle_entry_state;

/*
 * 登録表の索引を求める際に、ポインター値の上位のビットを下位のビットへ混ぜるシフト量。
 * uintptr_t のビット幅未満とする。値を変えると、索引の偏りが変わる。
 */
#define HANDLE_HASH_SHIFT_HIGH 17U
#define HANDLE_HASH_SHIFT_LOW  9U

static_assert(HANDLE_HASH_SHIFT_HIGH < sizeof(uintptr_t) * CHAR_BIT &&
                  HANDLE_HASH_SHIFT_LOW < sizeof(uintptr_t) * CHAR_BIT,
              "porter: handle hash shift must be less than the width of uintptr_t");

/** 登録表の初期の容量 (バケットの数)。索引を下位ビットで求めるため、2 の累乗とする。 */
#define HANDLE_TABLE_INIT_CAPACITY 64U

/** 登録表の拡張倍率。拡張後も容量を 2 の累乗に保つため、2 の累乗とする。 */
#define HANDLE_TABLE_GROWTH_FACTOR 2U

static_assert(HANDLE_TABLE_INIT_CAPACITY >= 2U &&
                  (HANDLE_TABLE_INIT_CAPACITY & (HANDLE_TABLE_INIT_CAPACITY - 1U)) == 0U,
              "porter: HANDLE_TABLE_INIT_CAPACITY must be a power of two");
static_assert(HANDLE_TABLE_GROWTH_FACTOR >= 2U &&
                  (HANDLE_TABLE_GROWTH_FACTOR & (HANDLE_TABLE_GROWTH_FACTOR - 1U)) == 0U,
              "porter: HANDLE_TABLE_GROWTH_FACTOR must be a power of two");

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
    handle_entry_state state;
    int reserved;
};

static cplat_once_flag s_once;
static cplat_local_lock *s_mutex;
static struct potr_internal_handle **s_buckets;
static size_t s_capacity;
static size_t s_count;

static void registry_init(void)
{
    /* 初期化失敗は s_mutex が NULL のままであることを呼び出し元が検出するため、戻り値は明示的に破棄する。 */
    (void)cplat_local_lock_create(&s_mutex);
}

/* 登録表の容量は 2 の累乗であり、ポインター値を混ぜてから下位ビットで索引を求める。 */
static size_t bucket_for(const void *handle, size_t capacity)
{
    uintptr_t value = (uintptr_t)handle;
    value ^= value >> HANDLE_HASH_SHIFT_HIGH;
    value ^= value >> HANDLE_HASH_SHIFT_LOW;

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

    cplat_local_lock_lock(s_mutex);

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

    cplat_local_lock_lock(s_mutex);

    if (s_count == s_capacity)
    {
        size_t capacity = HANDLE_TABLE_INIT_CAPACITY;
        struct potr_internal_handle **buckets;
        size_t i;

        if (s_capacity != 0U)
        {
            capacity = s_capacity * HANDLE_TABLE_GROWTH_FACTOR;
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
    cplat_local_lock_lock(s_mutex);
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

    cplat_local_lock_lock(entry->mutex);

    if (entry->ctx == NULL)
    {
        ret = POTR_ERR_INVALID_HANDLE;
    }
    else if (cplat_atomic_load_i32(&entry->ctx->fatal_error, CPLAT_MEMORY_ORDER_ACQUIRE) != POTR_OK)
    {
        ret = POTR_ERR_OUT_OF_MEMORY;
    }
    else if (entry->state != HANDLE_STATE_OPEN)
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
    cplat_local_lock_lock(entry->mutex);
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

    cplat_local_lock_lock(entry->mutex);

    if (entry->ctx == NULL)
    {
        ret = POTR_ERR_INVALID_HANDLE;
    }
    else if (potr_internal_in_own_callback(entry->ctx))
    {
        ret = POTR_ERR_UNSUPPORTED;
    }
    else if (entry->state == HANDLE_STATE_CLOSING)
    {
        ret = POTR_ERR_CANCELED;
    }
    else
    {
        /* 新規参照を拒否してから先行 API の返却を待ち、通信資源を回収できる状態にする。 */
        entry->state = HANDLE_STATE_CLOSING;

        while (entry->refs != 0U)
        {
            cplat_condvar_wait(entry->idle, entry->mutex);
        }

        *ctx = entry->ctx;
        cplat_atomic_store_i32(&(*ctx)->close_requested, 1, CPLAT_MEMORY_ORDER_RELEASE);
    }

    cplat_local_lock_unlock(entry->mutex);

    return ret;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_handle_end_close(potr_context *ctx)
{
    struct potr_internal_handle *entry;

    if (ctx == NULL || ctx->handle_owner == NULL)
    {
        return;
    }

    entry = ctx->handle_owner;
    cplat_local_lock_lock(entry->mutex);
    entry->ctx = NULL;
    entry->state = HANDLE_STATE_CLOSED;
    cplat_local_lock_unlock(entry->mutex);
}
