/**
 *******************************************************************************
 *  @file           potrHealthThread.h
 *  @brief          ヘルスチェックを行うスレッドの内部インターフェイスを宣言します。
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
#include <porter/potrContext.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          非 TCP ヘルスチェック スレッドを起動します。
     *
     *  ctx->health_interval_ms が 0 の場合は起動しない (POTR_OK を返す)。
     *
     *  @param[in,out]  ctx セッション コンテキスト。
     *  @retval         POTR_OK                    起動に成功したか、起動が不要です。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx が NULL です。
     *  @retval         POTR_ERR_UNKNOWN           スレッド生成に失敗しました。
     */
    extern int potr_health_thread_start(PotrContext *ctx);

    /**
     *  @brief          非 TCP ヘルスチェック スレッドを停止します。
     *  @param[in,out]  ctx セッション コンテキスト。
     *  @retval         POTR_OK                    停止に成功したか、停止済みです。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx が NULL です。
     */
    extern int potr_health_thread_stop(PotrContext *ctx);

    /**
     *  @brief          非 TCP ヘルスチェック スレッドを即時起床させます。
     *  @param[in,out]  ctx セッション コンテキスト。
     */
    extern void potr_health_thread_wake(PotrContext *ctx);

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
    extern int potr_tcp_send_ping_now(PotrContext *ctx, int path_idx);

    /**
     *  @brief          TCP ヘルスチェック スレッドを path ごとに起動します。
     *
     *  ctx->health_interval_ms が 0 の場合は起動しない (POTR_OK を返す)。
     *
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス (0 〜 n_path-1)。
     *  @retval         POTR_OK                    起動に成功したか、起動が不要です。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx または path_idx が不正です。
     *  @retval         POTR_ERR_UNKNOWN           スレッド生成に失敗しました。
     */
    extern int potr_tcp_health_thread_start(PotrContext *ctx, int path_idx);

    /**
     *  @brief          TCP ヘルスチェック スレッドを停止します。
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス (0 〜 n_path-1)。
     *  @retval         POTR_OK                    停止に成功したか、停止済みです。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx または path_idx が不正です。
     */
    extern int potr_tcp_health_thread_stop(PotrContext *ctx, int path_idx);

    /**
     *  @brief          指定パスの TCP ヘルスチェック スレッドを即時起床させます。
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス。
     */
    extern void potr_tcp_health_thread_wake(PotrContext *ctx, int path_idx);

    /**
     *  @brief          全パスの TCP ヘルスチェック スレッドを即時起床させます。
     *  @param[in,out]  ctx セッション コンテキスト。
     */
    extern void potr_tcp_health_thread_wake_all(PotrContext *ctx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_HEALTH_THREAD_H */
