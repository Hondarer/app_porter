/**
 *******************************************************************************
 *  @file           potrTrace.c
 *  @brief          porter のグローバル トレーサーを管理します。
 *  @author         Tetsuo Honda
 *  @date           2026/04/19
 *  @version        1.0.0
 *
 *  porter ライブラリ全体で共有する com_util_tracer ハンドルを管理します。\n
 *  トレーサーは初回アクセス時に lazy create され、プロセス終了時に
 *  trace_registry_dispose_all_on_unload() によって自動的に解放されます。\n
 *  出力開始 (com_util_tracer_start) はライブラリ利用者が potrGetTracer() 経由で
 *  stderr レベルを設定した後に行います。
 *
 *  @par            スレッド セーフ
 *  トレース書き込みは trace-com_util が内部で排他制御を行います。\n
 *  potr_trace_get() の lazy create はプロセス起動直後の単一スレッド期間に
 *  完了することを前提とします。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/trace/tracer.h>
#include <com_util/base/error_message.h>
#include <com_util/crt/path.h>
#include <stdarg.h>
#include <stdio.h>

#include <porter/porter_spec.h>

#include <porter/infra/potrTrace.h>

/* ── グローバル トレーサー状態 ──────────────────────────────────────────────── */

/** トレース プロバイダー ハンドル。potr_trace_get() で一度だけ初期化する。 */
static com_util_tracer *s_trace = NULL;

/* ── 内部 API ─────────────────────────────────────────────────────────── */

/* Doxygen コメントは、ヘッダーに記載 */

com_util_tracer *potr_trace_get(void)
{
    if (s_trace == NULL)
    {
        s_trace = com_util_tracer_create(COM_UTIL_TRACER_CONCURRENCY_TRACER_MANAGED);
        if (s_trace != NULL)
        {
            com_util_tracer_set_name(s_trace, "porter", 0);
            /* set_name は OS トレースの識別名のみに作用する。出力先はデフォルト設定に従い、
             * デフォルトはファイル トレースのみ (実行ファイルのディレクトリ配下の
             * log/{実行ファイル名}.log、OS と stderr は COM_UTIL_TRACE_LEVEL_NONE)。
             * start は potrGetTracer() 経由で利用者が明示的に呼ぶ。 */
        }
    }
    return s_trace;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_trace_socket_failure_at(const char *file, const int line, const com_util_trace_level level,
                                  const com_util_error *detail, const char *format, ...)
{
    char context[256];
    char message[256];
    va_list args;

    if ((file == NULL) || (detail == NULL) || (format == NULL))
    {
        return;
    }
    va_start(args, format);
    (void)vsnprintf(context, sizeof(context), format, args);
    va_end(args);
    (void)com_util_error_message(message, sizeof(message), detail);

    /* POTR_TRACE は展開位置の __FILE__ と __LINE__ を埋め込むため、本関数で使うと発生位置が
       potrTrace.c で固定される。呼び出し元の位置を残すため、下位 API へ直接書式を渡す。 */
    (void)_com_util_tracer_writef(potr_trace_get(), level, NULL, "[%s:%d] %s: domain=%d code=%lu: %s",
                                  com_util_path_basename(file), line, context, (int)detail->domain, detail->code,
                                  message);
}

/* ── 公開 API ─────────────────────────────────────────────────────────── */

/* Doxygen コメントは、ヘッダーに記載 */

com_util_tracer *potrGetTracer(void)
{
    return potr_trace_get();
}
