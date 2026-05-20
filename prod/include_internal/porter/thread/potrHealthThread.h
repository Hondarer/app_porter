/**
 *******************************************************************************
 *  @file           potrHealthThread.h
 *  @brief          ヘルスチェックスレッド内部ヘッダー。
 *  @author         Tetsuo Honda
 *  @date           2026/03/08
 *  @version        1.0.0
 *
 *  @details
 *  送信者が定周期で PING パケットを送信するスレッドの起動・停止 API。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef POTR_HEALTH_THREAD_H
#define POTR_HEALTH_THREAD_H

#include <porter/potrContext.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          非 TCP ヘルスチェックスレッドを起動する。
     *  @details
     *  ctx->health_interval_ms が 0 の場合は起動しない (POTR_SUCCESS を返す)。
     *  @param[in,out]  ctx セッションコンテキスト。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int potr_health_thread_start(struct PotrContext_ *ctx);

    /**
     *  @brief          非 TCP ヘルスチェックスレッドを停止する。
     *  @param[in,out]  ctx セッションコンテキスト。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int potr_health_thread_stop(struct PotrContext_ *ctx);

    /**
     *  @brief          非 TCP ヘルスチェックスレッドを即時起床させる。
     *  @param[in,out]  ctx セッションコンテキスト。
     */
    extern void potr_health_thread_wake(struct PotrContext_ *ctx);

    /**
     *  @brief          TCP PING パケットを即時送信する。
     *  @param[in,out]  ctx      セッションコンテキスト。
     *  @param[in]      path_idx パスインデックス。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int potr_tcp_send_ping_now(struct PotrContext_ *ctx, int path_idx);

    /**
     *  @brief          TCP ヘルスチェックスレッドを path ごとに起動する。
     *  @details
     *  ctx->health_interval_ms が 0 の場合は起動しない (POTR_SUCCESS を返す)。
     *  @param[in,out]  ctx      セッションコンテキスト。
     *  @param[in]      path_idx パスインデックス (0 〜 n_path-1)。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int potr_tcp_health_thread_start(struct PotrContext_ *ctx, int path_idx);

    /**
     *  @brief          TCP ヘルスチェックスレッドを停止する。
     *  @param[in,out]  ctx      セッションコンテキスト。
     *  @param[in]      path_idx パスインデックス (0 〜 n_path-1)。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int potr_tcp_health_thread_stop(struct PotrContext_ *ctx, int path_idx);

    /**
     *  @brief          指定パスの TCP ヘルスチェックスレッドを即時起床させる。
     *  @param[in,out]  ctx      セッションコンテキスト。
     *  @param[in]      path_idx パスインデックス。
     */
    extern void potr_tcp_health_thread_wake(struct PotrContext_ *ctx, int path_idx);

    /**
     *  @brief          全パスの TCP ヘルスチェックスレッドを即時起床させる。
     *  @param[in,out]  ctx セッションコンテキスト。
     */
    extern void potr_tcp_health_thread_wake_all(struct PotrContext_ *ctx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_HEALTH_THREAD_H */
