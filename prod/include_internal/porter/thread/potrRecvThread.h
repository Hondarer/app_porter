/**
 *******************************************************************************
 *  @file           potrRecvThread.h
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

#include <porter/potrContext.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          非 TCP 受信スレッドを起動します。
     *  @param[in,out]  ctx セッション コンテキスト。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int comm_recv_thread_start(PotrContext *ctx);

    /**
     *  @brief          非 TCP 受信スレッドを停止します。
     *  @param[in,out]  ctx セッション コンテキスト。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int comm_recv_thread_stop(PotrContext *ctx);

    /**
     *  @brief          TCP 受信スレッドを path ごとに起動します。
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス (0 〜 n_path-1)。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int tcp_recv_thread_start(PotrContext *ctx, int path_idx);

    /**
     *  @brief          TCP 受信スレッドの終了を待機します。
     *
     *  スレッドの停止はソケット クローズ (connect スレッド側) で行います。本関数は join のみを担当します。
     *
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス (0 〜 n_path-1)。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int tcp_recv_thread_stop(PotrContext *ctx, int path_idx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_RECV_THREAD_H */
