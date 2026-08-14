/**
 *******************************************************************************
 *  @file           potr_recv_thread.h
 *  @brief          データを受信するスレッドの内部インターフェイスを宣言します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/* NOTE: このヘッダーは多数のソース ファイルから参照されるため、            */
/*       @hideincludedbygraph によって "Included by" グラフを無効にします。 */

#ifndef POTR_RECV_THREAD_H
#define POTR_RECV_THREAD_H

#include <porter/porter_result.h>
#include <porter/potr_context.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          非 TCP 受信スレッドを起動します。
     *  @param[in,out]  ctx セッション コンテキスト。
     *  @retval         POTR_OK                    起動に成功しました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx が NULL です。
     *  @retval         POTR_ERR_UNKNOWN           スレッド生成に失敗しました。
     */
    extern int potr_internal_comm_recv_thread_start(potr_context *ctx);

    /**
     *  @brief          非 TCP 受信スレッドを停止します。
     *  @param[in,out]  ctx セッション コンテキスト。
     *  @retval         POTR_OK                    停止に成功しました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx が NULL です。
     */
    extern int potr_internal_comm_recv_thread_stop(potr_context *ctx);

    /**
     *  @brief          TCP 受信スレッドを path ごとに起動します。
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス (0 〜 n_path-1)。
     *  @retval         POTR_OK                    起動に成功しました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx または path_idx が不正です。
     *  @retval         POTR_ERR_UNKNOWN           スレッド生成に失敗しました。
     */
    extern int potr_internal_tcp_recv_thread_start(potr_context *ctx, int path_idx);

    /**
     *  @brief          TCP 受信スレッドの終了を待機します。
     *
     *  スレッドの停止はソケット クローズ (connect スレッド側) で行います。本関数は join のみを担当します。
     *
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス (0 〜 n_path-1)。
     *  @retval         POTR_OK                    終了待機に成功しました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx または path_idx が不正です。
     */
    extern int potr_internal_tcp_recv_thread_stop(potr_context *ctx, int path_idx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_RECV_THREAD_H */
