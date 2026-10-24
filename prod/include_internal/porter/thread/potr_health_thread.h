/**
 *******************************************************************************
 *  @file           potr_health_thread.h
 *  @brief          ヘルス チェックを行うスレッドの内部インターフェイスを宣言します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/08
 *  @version        1.0.0
 *
 *  送信者が定周期で PING パケットを送信するスレッドの起動・停止 API。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/* NOTE: このヘッダーは多数のソース ファイルから参照されるため、            */
/*       @hideincludedbygraph によって "Included by" グラフを無効にします。 */

#ifndef POTR_HEALTH_THREAD_H
#define POTR_HEALTH_THREAD_H

#include <porter/porter_result.h>
#include <porter/potr_context.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          非 TCP ヘルス チェック スレッドを起動します。
     *  @note           UDP 同期資源を所有し、途中失敗時は生成済み分を回収します。
     *
     *  ctx->health_interval_ms が 0 の場合は起動しない (POTR_OK を返す)。
     *
     *  @param[in,out]  ctx セッション コンテキスト。
     *  @retval         POTR_OK                    起動に成功したか、起動が不要です。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx が NULL です。
     *  @retval         POTR_ERR_UNKNOWN           同期資源またはスレッドの生成に失敗しました。
     */
    extern int potr_internal_health_thread_start(potr_context *ctx);

    /**
     *  @brief          非 TCP ヘルス チェック スレッドを停止します。
     *  @note           生成済みハンドルの終了待ち後、ハンドルと UDP 同期資源を NULL へ戻します。
     *                  待機失敗時は資源を保持します。
     *  @retval         POTR_ERR_UNKNOWN           終了待ちに失敗しました。
     *  @param[in,out]  ctx セッション コンテキスト。
     *  @retval         POTR_OK                    停止に成功したか、停止済みです。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx が NULL です。
     */
    extern int potr_internal_health_thread_stop(potr_context *ctx);

    /**
     *  @brief          非 TCP ヘルス チェック スレッドを即時起床させます。
     *  @param[in,out]  ctx セッション コンテキスト。
     */
    extern void potr_internal_health_thread_wake(potr_context *ctx);

    /**
     *  @brief          TCP PING パケットを即時送信します。
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス。
     *  @retval         POTR_OK                    送信に成功しました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx または path_idx が不正です。
     *  @retval         POTR_ERR_DISCONNECTED      送信可能なパスがありません。
     *  @retval         POTR_ERR_CANCELED          終了処理中です。
     *  @retval         POTR_ERR_IO                送信に失敗しました。
     *  @retval         POTR_ERR_UNKNOWN           暗号化に失敗しました。
     */
    extern int potr_internal_tcp_send_ping_now(potr_context *ctx, int path_idx);

    /**
     *  @brief          TCP ヘルス チェック スレッドを path ごとに起動します。
     *
     *  ctx->health_interval_ms が 0 の場合は起動しない (POTR_OK を返す)。
     *
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス (0 〜 n_path-1)。
     *  @retval         POTR_OK                    起動に成功したか、起動が不要です。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx または path_idx が不正です。
     *  @retval         POTR_ERR_UNKNOWN           同期資源またはスレッドの生成に失敗しました。
     */
    extern int potr_internal_tcp_health_thread_start(potr_context *ctx, int path_idx);

    /**
     *  @brief          TCP ヘルス チェック スレッドを停止します。
     *  @note           終了待ち成功後にハンドルを NULL へ戻します。同期資源の所有権はサービスに残ります。
     *  @retval         POTR_ERR_UNKNOWN           終了待ちに失敗しました。
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス (0 〜 n_path-1)。
     *  @retval         POTR_OK                    停止に成功したか、停止済みです。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx または path_idx が不正です。
     */
    extern int potr_internal_tcp_health_thread_stop(potr_context *ctx, int path_idx);

    /**
     *  @brief          指定パスの TCP ヘルス チェック スレッドを即時起床させます。
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス。
     */
    extern void potr_internal_tcp_health_thread_wake(potr_context *ctx, int path_idx);

    /**
     *  @brief          全パスの TCP ヘルス チェック スレッドを即時起床させます。
     *  @param[in,out]  ctx セッション コンテキスト。
     */
    extern void potr_internal_tcp_health_thread_wake_all(potr_context *ctx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_HEALTH_THREAD_H */
