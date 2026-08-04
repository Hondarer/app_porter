/**
 *******************************************************************************
 *  @file           potrSocketError.h
 *  @brief          ソケット エラーの捕捉、分類、結果コード変換を提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/08/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *  @hideincludedbygraph
 *******************************************************************************
 */

#ifndef POTR_SOCKET_ERROR_H
#define POTR_SOCKET_ERROR_H

#include <com_util/base/error.h>

typedef enum potr_socket_cause_t
{
    POTR_SOCKET_CAUSE_NONE = 0,              /**< エラーなし。 */
    POTR_SOCKET_CAUSE_OTHER,                 /**< 分類できないエラー。 */
    POTR_SOCKET_CAUSE_WOULD_BLOCK,           /**< 処理を直ちに完了できない。 */
    POTR_SOCKET_CAUSE_IN_PROGRESS,           /**< 非同期処理中。 */
    POTR_SOCKET_CAUSE_INTERRUPTED,           /**< 割り込みによる中断。 */
    POTR_SOCKET_CAUSE_CONNECTION_REFUSED,    /**< 接続拒否。 */
    POTR_SOCKET_CAUSE_CONNECTION_RESET,      /**< 接続のリセット。 */
    POTR_SOCKET_CAUSE_CONNECTION_ABORTED,    /**< 接続の中断。 */
    POTR_SOCKET_CAUSE_NOT_CONNECTED,         /**< 未接続。 */
    POTR_SOCKET_CAUSE_TIMED_OUT,             /**< タイムアウト。 */
    POTR_SOCKET_CAUSE_HOST_UNREACHABLE,      /**< ホストへ到達不能。 */
    POTR_SOCKET_CAUSE_NET_UNREACHABLE,       /**< ネットワークへ到達不能。 */
    POTR_SOCKET_CAUSE_NET_DOWN,              /**< ネットワーク停止。 */
    POTR_SOCKET_CAUSE_ADDRESS_IN_USE,        /**< アドレス使用中。 */
    POTR_SOCKET_CAUSE_ADDRESS_NOT_AVAILABLE, /**< アドレスを利用できない。 */
    POTR_SOCKET_CAUSE_MESSAGE_SIZE,          /**< メッセージ サイズ不正。 */
    POTR_SOCKET_CAUSE_ACCESS_DENIED,         /**< アクセス拒否。 */
    POTR_SOCKET_CAUSE_OUT_OF_MEMORY,         /**< メモリー不足。 */
    POTR_SOCKET_CAUSE_TOO_MANY_OPEN_FILES,   /**< オープン ファイル数超過。 */
    POTR_SOCKET_CAUSE_INVALID_ARGUMENT,      /**< 不正引数。 */
    POTR_SOCKET_CAUSE_UNSUPPORTED,           /**< 未対応機能。 */
    POTR_SOCKET_CAUSE_NOT_INITIALIZED,       /**< ソケット ライブラリ未初期化。 */
    POTR_SOCKET_CAUSE_SHUTDOWN               /**< ソケット停止済み。 */
} potr_socket_cause_t;

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          OS が保持する直前のソケット エラーを報告します。
     *  @param[out]     detail_out 詳細情報。NULL 可。
     *  @return         ネイティブ エラーに対応する POTR_ERR_* を返します。
     */
    extern int potr_socket_error_report(com_util_error *detail_out);
    /**
     *  @brief          指定した OS 固有コードをソケット エラーとして報告します。
     *  @param[out]     detail_out 詳細情報。NULL 可。
     *  @param[in]      code OS 固有エラー コード。
     *  @return         ネイティブ エラーに対応する POTR_ERR_* を返します。
     */
    extern int potr_socket_error_report_code(com_util_error *detail_out, unsigned long code);
    /**
     *  @brief          OS 固有の不正引数エラーを報告します。
     *  @param[out]     detail_out EINVAL または WSAEINVAL の詳細情報。NULL 可。
     *  @return         POTR_ERR_INVALID_ARGUMENT を返します。
     */
    extern int potr_socket_error_report_invalid_argument(com_util_error *detail_out);
    /**
     *  @brief          詳細情報と現在のスレッドの直前エラーをクリアします。
     *  @param[out]     detail_out クリアする詳細情報。NULL 可。
     */
    extern void potr_socket_error_clear(com_util_error *detail_out);
    /**
     *  @brief          詳細情報を OS 非依存のソケット エラー要因へ分類します。
     *  @param[in]      error 分類する詳細情報。NULL 可。
     *  @return         対応する POTR_SOCKET_CAUSE_* を返します。
     */
    extern potr_socket_cause_t potr_socket_error_get_cause(const com_util_error *error);
    /**
     *  @brief          詳細情報が指定したソケット エラー要因かを判定します。
     *  @param[in]      error 判定する詳細情報。NULL 可。
     *  @param[in]      cause 比較する要因。
     *  @return         一致する場合は 1、一致しない場合は 0 を返します。
     */
    extern int potr_socket_error_is(const com_util_error *error, potr_socket_cause_t cause);
    /**
     *  @brief          詳細情報が非同期接続処理中を示すかを判定します。
     *  @param[in]      error 判定する詳細情報。NULL 可。
     *  @return         IN_PROGRESS または WOULD_BLOCK の場合は 1、それ以外は 0 を返します。
     */
    extern int potr_socket_error_is_connect_in_progress(const com_util_error *error);
    /**
     *  @brief          詳細情報を porter 結果コードへ変換します。
     *  @param[in]      error 変換する詳細情報。NULL 可。
     *  @return         エラーなしの場合は POTR_OK、それ以外は対応する POTR_ERR_* を返します。
     */
    extern int potr_result_from_socket_error(const com_util_error *error);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_SOCKET_ERROR_H */
