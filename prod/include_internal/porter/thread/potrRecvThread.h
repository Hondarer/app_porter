/**
 *******************************************************************************
 *  @file           potrRecvThread.h
 *  @brief          受信スレッド内部ヘッダー。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef POTR_RECV_THREAD_H
#define POTR_RECV_THREAD_H

#include <porter/potrContext.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          非 TCP 受信スレッドを起動する。
     *  @param[in,out]  ctx セッションコンテキスト。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int comm_recv_thread_start(struct PotrContext_ *ctx);

    /**
     *  @brief          非 TCP 受信スレッドを停止する。
     *  @param[in,out]  ctx セッションコンテキスト。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int comm_recv_thread_stop(struct PotrContext_ *ctx);

    /**
     *  @brief          TCP 受信スレッドを path ごとに起動する。
     *  @param[in,out]  ctx      セッションコンテキスト。
     *  @param[in]      path_idx パスインデックス (0 〜 n_path-1)。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int tcp_recv_thread_start(struct PotrContext_ *ctx, int path_idx);

    /**
     *  @brief          TCP 受信スレッドの終了を待機する。
     *  @details
     *  スレッドの停止はソケットクローズ (connect スレッド側) で行う。本関数は join のみを担当する。
     *  @param[in,out]  ctx      セッションコンテキスト。
     *  @param[in]      path_idx パスインデックス (0 〜 n_path-1)。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int tcp_recv_thread_stop(struct PotrContext_ *ctx, int path_idx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_RECV_THREAD_H */
