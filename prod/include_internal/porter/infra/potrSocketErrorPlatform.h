/**
 *******************************************************************************
 *  @file           potrSocketErrorPlatform.h
 *  @brief          OS 固有のソケット エラー取得および分類 API を提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/08/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *  @hideincludedbygraph
 *******************************************************************************
 */

#ifndef POTR_SOCKET_ERROR_PLATFORM_H
#define POTR_SOCKET_ERROR_PLATFORM_H

#include <com_util/base/error.h>
#include <porter/infra/potrSocketError.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          OS が保持する直前のソケット エラーを取得します。
     *  @param[out]     error 格納先。NULL 可。
     */
    extern void potr_socket_error_capture(com_util_error *error);
    /**
     *  @brief          OS 固有コードをソケット エラー詳細へ変換します。
     *  @param[out]     error 格納先。NULL 可。
     *  @param[in]      code OS 固有エラー コード。
     */
    extern void potr_socket_error_capture_code(com_util_error *error, unsigned long code);
    /**
     *  @brief          不正引数を表す OS 固有エラーを取得します。
     *  @param[out]     error 格納先。NULL 可。
     */
    extern void potr_socket_error_capture_invalid_argument(com_util_error *error);

#if defined(PLATFORM_LINUX)
    /**
     *  @brief          errno 値を OS 非依存のソケット エラー要因へ変換します。
     *  @param[in]      errno_value errno 値。
     *  @return         対応する POTR_SOCKET_CAUSE_* を返します。
     */
    extern potr_socket_cause_t potr_socket_cause_from_errno(int errno_value);
#elif defined(PLATFORM_WINDOWS)
/**
     *  @brief          Winsock エラー値を OS 非依存のソケット エラー要因へ変換します。
     *  @param[in]      error_code Winsock エラー値。
     *  @return         対応する POTR_SOCKET_CAUSE_* を返します。
     */
extern potr_socket_cause_t potr_socket_cause_from_windows_error(unsigned long error_code);
#endif /* PLATFORM_ */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_SOCKET_ERROR_PLATFORM_H */
