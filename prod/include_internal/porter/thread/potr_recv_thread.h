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

/*
 * このヘッダーは多数のソース ファイルから参照されるため、
 * @hideincludedbygraph によって "Included by" グラフを無効にする。
 */

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
     *  @note           生成済みハンドルを終了待ちし、NULL へ戻します。
     *                  終了待ちは失敗しないため、戻った時点でスレッドは終了しています。
     *                  ctx が NULL の場合は何もしません。
     *  @param[in,out]  ctx セッション コンテキスト。
     */
    extern void potr_internal_comm_recv_thread_stop(potr_context *ctx);

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
     *  @note           生成済みハンドルだけを終了待ちし、NULL へ戻します。
     *
     *  スレッドの停止はソケット クローズ (connect スレッド側) で行います。本関数は join のみを担当します。
     *  終了待ちは失敗しないため、戻った時点でスレッドは終了しています。
     *  ctx が NULL または path_idx が範囲外の場合は何もしません。
     *
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス (0 〜 n_path-1)。
     */
    extern void potr_internal_tcp_recv_thread_stop(potr_context *ctx, int path_idx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_RECV_THREAD_H */
