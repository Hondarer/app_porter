/**
 *******************************************************************************
 *  @file           potrTrace.h
 *  @brief          porter 内部ログマクロ定義ヘッダー。
 *  @author         Tetsuo Honda
 *  @date           2026/04/19
 *  @version        1.0.0
 *
 *  @details
 *  porter ライブラリ内部でのみ使用するログ出力マクロを定義します。\n
 *  ライブラリ外部には公開しません。\n
 *  公開 API は porter.h の potrGetLogger() を参照してください。
 *
 *  使用方法:
 *  @code{.c}
    POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,  "service_id=%" PRId64 " opened", service_id);
    POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR, "socket bind failed: port=%u", port);
    POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE, "PING sent: seq=%u", seq);
 *  @endcode
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef POTR_TRACE_H
#define POTR_TRACE_H

#include <com_util/trace/logger.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/**
 *******************************************************************************
 *  @brief          グローバルロガーハンドルを返します (初回呼び出し時に lazy create)。
 *  @return         com_util_logger_t ハンドル。NULL を返すことはありません。
 *
 *  @details
 *  本関数を直接呼び出さず、POTR_LOG マクロを使用してください。
 *******************************************************************************
 */
com_util_logger_t *potr_trace_get(void);

/**
 *  @def            POTR_LOG(level, ...)
 *  @brief          porter 内部ログ出力マクロ。
 *
 *  @details
 *  __FILE__ と __LINE__ を自動付加して com_util_logger_writef() を呼び出します。\n
 *  ロガーが未 start の場合は無視されます。\n
 *  level には com_util_log_level_t の値を指定してください。
 *
 *  @par            例
 *  @code{.c}
    POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,    "potrOpenService: service_id=%" PRId64 "", service_id);
    POTR_LOG(COM_UTIL_LOG_LEVEL_WARNING, "NACK received: seq=%u", seq);
    POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,   "socket() failed");
 *  @endcode
 */
#define POTR_LOG(level, ...) \
    COM_UTIL_LOGGER_WRITEF(potr_trace_get(), (level), __VA_ARGS__)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_TRACE_H */
