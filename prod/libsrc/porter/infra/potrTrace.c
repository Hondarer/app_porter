/**
 *******************************************************************************
 *  @file           potrTrace.c
 *  @brief          porter グローバルトレーサー管理。
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

#include <porter/porter_spec.h>

#include <porter/infra/potrTrace.h>

/* ── グローバルトレーサー状態 ──────────────────────────────────────────────── */

/** トレースプロバイダハンドル。potr_trace_get() で一度だけ初期化する。 */
static com_util_tracer *s_trace = NULL;

/* ── 内部 API ─────────────────────────────────────────────────────────── */

/* doxygen コメントは potrTrace.h に記載 */
com_util_tracer *potr_trace_get(void)
{
    if (s_trace == NULL)
    {
        s_trace = com_util_tracer_create();
        if (s_trace != NULL)
        {
            com_util_tracer_set_name(s_trace, "porter", 0);
            /* OS レベルは COM_UTIL_TRACE_LEVEL_INFO (デフォルト値)、stderr は COM_UTIL_TRACE_LEVEL_NONE (デフォルト値)。
             * start は potrGetTracer() 経由で利用者が明示的に呼ぶ。 */
        }
    }
    return s_trace;
}

/* ── 公開 API ─────────────────────────────────────────────────────────── */

/* doxygen コメントは porter.h に記載 */
POTR_EXPORT com_util_tracer *POTR_API potrGetTracer(void)
{
    return potr_trace_get();
}
