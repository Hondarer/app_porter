/**
 *******************************************************************************
 *  @file           potrTrace.h
 *  @brief          porter の内部トレースに使用するマクロを定義します。
 *  @author         Tetsuo Honda
 *  @date           2026/04/19
 *  @version        1.0.0
 *
 *  porter ライブラリ内部でのみ使用するログ出力マクロを定義します。\n
 *  ライブラリ外部には公開しません。\n
 *  公開 API は porter.h の potrGetTracer() を参照してください。
 *
 *  使用方法:
    @code{.c}
    POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,  "service_id=%" PRId64 " opened", service_id);
    POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR, "socket bind failed: port=%u", port);
    POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE, "PING sent: seq=%u", seq);
    @endcode
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/* NOTE: このヘッダーは多数のソース ファイルから参照されるため、            */
/*       @hideincludedbygraph によって "Included by" グラフを無効にします。 */

#ifndef POTR_TRACE_H
#define POTR_TRACE_H

#include <com_util/trace/tracer.h>
#include <com_util/base/error.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          グローバル トレーサー ハンドルを返します (初回呼び出し時に lazy create)。
     *  @return         com_util_tracer ハンドル。NULL を返すことはありません。
     *
     *  本関数を直接呼び出さず、POTR_TRACE マクロを使用してください。
     */
    com_util_tracer *potr_trace_get(void);

    /**
     *******************************************************************************
     *  @brief          ソケット失敗の文脈と詳細情報を共通形式で出力します。
     *  @param[in]      level  トレース レベル。
     *  @param[in]      detail ソケット エラーの詳細情報。NULL 不可。
     *  @param[in]      format 操作名やサービス ID を組み立てる printf 形式文字列。NULL 不可。
     *  @param[in]      ...    @p format に対応する引数。
     *******************************************************************************
     */
    void potr_trace_socket_failure(com_util_trace_level_t level, const com_util_error *detail, const char *format, ...);

/**
 *  @brief          porter 内部ログ出力マクロ。
 *
 *  __FILE__ と __LINE__ を自動付加する com_util_tracer_writef() マクロを呼び出します。\n
 *  トレーサーが未 start の場合は無視されます。\n
 *  level には com_util_trace_level_t の値を指定してください。
 *
 *  @par            例
    @code{.c}
    POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,    "potrOpenService: service_id=%" PRId64 "", service_id);
    POTR_TRACE(COM_UTIL_TRACE_LEVEL_WARNING, "NACK received: seq=%u", seq);
    POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR,   "socket() failed");
    @endcode
 */
#define POTR_TRACE(level, ...) com_util_tracer_writef(potr_trace_get(), (level), NULL, __VA_ARGS__)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_TRACE_H */
