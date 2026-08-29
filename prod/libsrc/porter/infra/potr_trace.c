/**
 *******************************************************************************
 *  @file           potr_trace.c
 *  @brief          porter のグローバル トレーサーを管理します。
 *  @author         Tetsuo Honda
 *  @date           2026/04/19
 *  @version        1.0.0
 *
 *  porter ライブラリ全体で共有する cplat_tracer ハンドルを管理します。\n
 *  トレーサーは初回アクセス時に lazy create され、プロセス終了時に
 *  trace_registry_dispose_all_on_unload() によって自動的に解放されます。\n
 *  出力開始 (cplat_tracer_start) はライブラリ利用者が potr_tracer_get() 経由で
 *  stderr レベルを設定した後に行います。
 *
 *  @par            スレッド セーフ
 *  トレース書き込みは trace-cplat が内部で排他制御を行います。\n
 *  potr_internal_trace_get() の lazy create はプロセス起動直後の単一スレッド期間に
 *  完了することを前提とします。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/trace/tracer.h>
#include <cplat/base/error_message.h>
#include <cplat/crt/path.h>
#include <cplat/crt/stdio.h>
#include <stdarg.h>
#include <stdio.h>

#include <porter/porter_spec.h>

#include <porter/infra/potr_trace.h>

/* ── グローバル トレーサー状態 ──────────────────────────────────────────────── */

/** トレース プロバイダー ハンドル。potr_internal_trace_get() で一度だけ初期化する。 */
static cplat_tracer *s_trace = NULL;

/* ── 内部 API ─────────────────────────────────────────────────────────── */

/* Doxygen コメントは、ヘッダーに記載 */

cplat_tracer *potr_internal_trace_get(void)
{
    if (s_trace == NULL)
    {
        s_trace = cplat_tracer_create(CPLAT_TRACER_CONCURRENCY_TRACER_MANAGED);
        if (s_trace != NULL)
        {
            cplat_tracer_set_name(s_trace, "porter", 0);
            /* set_name は OS トレースの識別名のみに作用する。出力先はデフォルト設定に従い、
             * デフォルトはファイル トレースのみ (実行ファイルのディレクトリ配下の
             * log/{実行ファイル名}.log、OS と stderr は CPLAT_TRACE_LEVEL_NONE)。
             * start は potr_tracer_get() 経由で利用者が明示的に呼ぶ。 */
        }
    }
    return s_trace;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_trace_socket_failure_at(const char *file, const int line, const cplat_trace_level level,
                                  const cplat_error *detail, const char *format, ...)
{
    char context[256];
    char message[256];
    va_list args;

    if ((file == NULL) || (detail == NULL) || (format == NULL))
    {
        return;
    }
    va_start(args, format);
    (void)cplat_vsnprintf(context, sizeof(context), format, args);
    va_end(args);
    (void)cplat_error_message(message, sizeof(message), detail);

    /* POTR_TRACE は展開位置の __FILE__ と __LINE__ を埋め込むため、本関数で使うと発生位置が
       potr_trace.c で固定される。呼び出し元の位置を残すため、下位 API へ直接書式を渡す。 */
    (void)cplat_tracer_writef_at(potr_internal_trace_get(), level, NULL, "[%s:%d] %s: domain=%d code=%lu: %s",
                                  cplat_path_basename(file), line, context, (int)detail->domain, detail->code,
                                  message);
}

/* ── 公開 API ─────────────────────────────────────────────────────────── */

/* Doxygen コメントは、ヘッダーに記載 */

cplat_tracer *potr_tracer_get(void)
{
    return potr_internal_trace_get();
}
