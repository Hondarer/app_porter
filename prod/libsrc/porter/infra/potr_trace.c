/**
 *******************************************************************************
 *  @file           potr_trace.c
 *  @brief          porter のグローバル トレーサーを管理します。
 *  @author         Tetsuo Honda
 *  @date           2026/04/19
 *  @version        1.0.0
 *
 *  porter ライブラリ全体で共有する cplat_tracer ハンドルを管理します。\n
 *  トレーサーは初回アクセス時に lazy create されます。\n
 *  解放は cplat の終了処理 (cplat_exit() や通常の終了) が行います。porter は生成時に終了コールバックを登録し、
 *  cplat がトレーサーを解放する前に共有ハンドルを取り下げて、以後の取得で NULL を返します。\n
 *  出力開始 (cplat_tracer_start) はライブラリ利用者が potr_tracer_get() 経由で
 *  stderr レベルを設定した後に行います。
 *
 *  @par            スレッド セーフ
 *  トレース書き込みは trace-cplat が内部で排他制御を行います。\n
 *  potr_internal_trace_get() はアトミックな状態で生成を直列化し、失敗後の呼び出しで再試行します。
 *  複数スレッドから同時に初回アクセスしても安全です。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/trace/tracer.h>
#include <cplat/base/error_message.h>
#include <cplat/crt/path.h>
#include <cplat/crt/stdio.h>
#include <cplat/runtime/shutdown.h>
#include <cplat/sync/sync.h>
#include <cplat/sync/atomic.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#include <porter/porter_spec.h>

#include <porter/infra/potr_trace.h>

/** ソケットの失敗のトレースで、呼び出し元の文脈を書式化するバッファーの大きさ [byte]。超える分は切り詰める。 */
#define POTR_TRACE_CONTEXT_BUF_SIZE 256U

/** ソケットの失敗のトレースで、エラーの説明を格納するバッファーの大きさ [byte]。超える分は切り詰める。 */
#define POTR_TRACE_MESSAGE_BUF_SIZE 256U

static_assert(POTR_TRACE_CONTEXT_BUF_SIZE >= 2U && POTR_TRACE_MESSAGE_BUF_SIZE >= 2U,
              "porter: trace buffers must hold at least one character and NUL");

/* ── ファイル内補助関数の前方宣言 ─────────────────────────────────────────── */

static cplat_tracer *create_trace(void);
static void potr_trace_shutdown(const cplat_shutdown_event *event, void *context);

/* ── グローバル トレーサー状態 ──────────────────────────────────────────────── */

/** トレース プロバイダー ハンドル。生成に成功した後に公開する。 */
static cplat_atomic_ptr s_trace = CPLAT_ATOMIC_INIT(NULL);

/**
 *  状態は 0 が未生成、1 が生成中、2 が生成済み、3 が終了済みです。\n
 *  生成の失敗後は 0 へ戻して次の呼び出しで再試行します。3 は cplat の終了処理の開始を示し、以後は 0 へ戻しません。
 */
static cplat_atomic_i32 s_trace_state = CPLAT_ATOMIC_INIT(0);

/* ── 公開 API ─────────────────────────────────────────────────────────── */

/* Doxygen コメントは、ヘッダーに記載 */

cplat_tracer *potr_tracer_get(void)
{
    return potr_internal_trace_get();
}

/* ── 内部 API ─────────────────────────────────────────────────────────── */

/* Doxygen コメントは、ヘッダーに記載 */

cplat_tracer *potr_internal_trace_get(void)
{
    cplat_tracer *trace;
    int32_t expected_state;
    int32_t state;

    trace = (cplat_tracer *)cplat_atomic_load_ptr(&s_trace, CPLAT_MEMORY_ORDER_ACQUIRE);
    if (trace != NULL)
    {
        return trace;
    }

    expected_state = 0;
    if (cplat_atomic_compare_exchange_i32(&s_trace_state, &expected_state, 1, CPLAT_MEMORY_ORDER_ACQ_REL))
    {
        trace = create_trace();
        if (trace == NULL)
        {
            cplat_atomic_store_i32(&s_trace_state, 0, CPLAT_MEMORY_ORDER_RELEASE);
            return NULL;
        }

        /*
         * cplat の終了処理がトレーサーを解放した後に、共有ハンドルを返さないための登録である。
         * cplat_tracer_create() は、ハンドルを返す前に cplat のトレースの終了コールバックを 1 度だけ登録する。
         * 本登録は必ずその後になり、cplat_shutdown_register() は登録の逆順 (LIFO) で実行するため (shutdown.h)、
         * porter のコールバックは cplat がトレーサーを解放するより先に実行される。
         * 登録できなければ取り下げの手段がないため、生成したトレーサーを解放して生成の失敗として扱い、次の呼び出しで再試行する。
         * 解放できなかったトレーサーは、cplat の登録簿に残り、cplat の終了処理が回収する。
         */
        if (cplat_shutdown_register(potr_trace_shutdown, NULL) != CPLAT_OK)
        {
            cplat_tracer_dispose(&trace);
            cplat_atomic_store_i32(&s_trace_state, 0, CPLAT_MEMORY_ORDER_RELEASE);
            return NULL;
        }

        cplat_atomic_store_ptr(&s_trace, trace, CPLAT_MEMORY_ORDER_RELEASE);
        cplat_atomic_store_i32(&s_trace_state, 2, CPLAT_MEMORY_ORDER_RELEASE);
        return trace;
    }

    state = cplat_atomic_load_i32(&s_trace_state, CPLAT_MEMORY_ORDER_ACQUIRE);
    while (state == 1)
    {
        cplat_thread_yield();
        state = cplat_atomic_load_i32(&s_trace_state, CPLAT_MEMORY_ORDER_ACQUIRE);
    }

    if (state == 2)
    {
        return (cplat_tracer *)cplat_atomic_load_ptr(&s_trace, CPLAT_MEMORY_ORDER_ACQUIRE);
    }

    /* 0 は生成の失敗、3 は終了済み (待機の後に見た場合を含む) である。終了済みでは再生成しない。 */
    return NULL;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_trace_socket_failure_at(const char *file, const int line, const cplat_trace_level level,
                                           const cplat_error *detail, const char *format, ...)
{
    char context[POTR_TRACE_CONTEXT_BUF_SIZE];
    char message[POTR_TRACE_MESSAGE_BUF_SIZE];
    va_list args;

    if ((file == NULL) || (detail == NULL) || (format == NULL))
    {
        return;
    }

    /*
     * トレース出力の中の書式化であり、失敗を知らせる先がほかになく、失敗した項目は空文字列のまま出力するため、
     * cplat_vsnprintf と cplat_error_message の戻り値は明示的に破棄する。
     */
    va_start(args, format);
    (void)cplat_vsnprintf(context, sizeof(context), format, args);
    va_end(args);
    (void)cplat_error_message(message, sizeof(message), detail);

    /*
     * POTR_TRACE は展開位置の __FILE__ と __LINE__ を埋め込むため、本関数で使うと発生位置が
     * potr_trace.c で固定される。呼び出し元の位置を残すため、下位 API へ直接書式を渡す。
     * トレースは補助の記録であり、出力の失敗で処理を変えないため、戻り値は明示的に破棄する。
     */
    (void)cplat_tracer_writef_at(potr_internal_trace_get(), level, NULL, "[%s:%d] %s: domain=%d code=%lu: %s",
                                 cplat_path_basename(file), line, context, (int)detail->domain, detail->code, message);
}

/* ── ファイル内補助関数 ─────────────────────────────────────────────── */

/**
 *  @brief          porter のトレーサーを生成します。
 *  @return         成功時は生成したハンドル、失敗時は NULL を返します。
 *
 *  @par            スレッド セーフ
 *  本関数は生成状態を取得したスレッドからだけ呼び出します。
 */
static cplat_tracer *create_trace(void)
{
    cplat_tracer *trace;

    trace = cplat_tracer_create(CPLAT_TRACER_CONCURRENCY_TRACER_MANAGED);
    if (trace == NULL)
    {
        return NULL;
    }

    /*
     * 識別名の設定に失敗してもトレーサーは使え、失敗を出力する先もこのトレーサー自身のため、
     * 戻り値は明示的に破棄する。
     */
    (void)cplat_tracer_set_name(trace, "porter", 0);

    /*
     * set_name は OS トレースの識別名のみに作用する。出力先はデフォルト設定に従い、
     * デフォルトはすべての出力先が CPLAT_TRACE_LEVEL_NONE で、porter 自身は
     * 出力先を設定しない。出力先の詳細度の設定と start は、potr_tracer_get() 経由で
     * 利用者が明示的に呼び出す。
     */
    return trace;
}

/**
 *  @brief          cplat の終了処理で、porter の共有ハンドルを取り下げます。
 *  @param[in]      event   終了イベント情報。使用しません。
 *  @param[in]      context 登録時に渡したコンテキスト (NULL)。使用しません。
 *
 *  共有ハンドルを NULL にし、状態を 3 (終了済み) にします。以後の potr_internal_trace_get() は NULL を返し、
 *  トレーサーを再生成しません。\n
 *  トレーサーの解放は cplat の終了処理が行うため、ここでは解放しません。
 *
 *  @par            スレッド セーフ
 *  cplat の終了処理から 1 スレッドで呼び出されます。
 */
static void potr_trace_shutdown(const cplat_shutdown_event *event, void *context)
{
    (void)event;
    (void)context;

    cplat_atomic_store_ptr(&s_trace, NULL, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_i32(&s_trace_state, 3, CPLAT_MEMORY_ORDER_RELEASE);
}
