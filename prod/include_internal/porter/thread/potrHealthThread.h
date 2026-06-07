/**
 *******************************************************************************
 *  @file           potrHealthThread.h
 *  @brief          ヘルスチェック スレッド内部ヘッダー。
 *  @author         Tetsuo Honda
 *  @date           2026/03/08
 *  @version        1.0.0
 *
 *  送信者が定周期で PING パケットを送信するスレッドの起動・停止 API。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/* NOTE: このヘッダーは多数のソース ファイルから参照されるため、            */
/*       @hideincludedbygraph によって "Included by" グラフを無効にします。 */

#ifndef POTR_HEALTH_THREAD_H
#define POTR_HEALTH_THREAD_H

#include <porter/potrContext.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          非 TCP ヘルスチェック スレッドを起動する。
     *
     *  ctx->health_interval_ms が 0 の場合は起動しない (POTR_SUCCESS を返す)。
     *
     *  @param[in,out]  ctx セッション コンテキスト。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int potr_health_thread_start(PotrContext *ctx);

    /**
     *  @brief          非 TCP ヘルスチェック スレッドを停止する。
     *  @param[in,out]  ctx セッション コンテキスト。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int potr_health_thread_stop(PotrContext *ctx);

    /**
     *  @brief          非 TCP ヘルスチェック スレッドを即時起床させる。
     *  @param[in,out]  ctx セッション コンテキスト。
     */
    extern void potr_health_thread_wake(PotrContext *ctx);

    /**
     *  @brief          TCP PING パケットを即時送信する。
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int potr_tcp_send_ping_now(PotrContext *ctx, int path_idx);

    /**
     *  @brief          TCP ヘルスチェック スレッドを path ごとに起動する。
     *
     *  ctx->health_interval_ms が 0 の場合は起動しない (POTR_SUCCESS を返す)。
     *
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス (0 〜 n_path-1)。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int potr_tcp_health_thread_start(PotrContext *ctx, int path_idx);

    /**
     *  @brief          TCP ヘルスチェック スレッドを停止する。
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス (0 〜 n_path-1)。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int potr_tcp_health_thread_stop(PotrContext *ctx, int path_idx);

    /**
     *  @brief          指定パスの TCP ヘルスチェック スレッドを即時起床させる。
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス。
     */
    extern void potr_tcp_health_thread_wake(PotrContext *ctx, int path_idx);

    /**
     *  @brief          全パスの TCP ヘルスチェック スレッドを即時起床させる。
     *  @param[in,out]  ctx セッション コンテキスト。
     */
    extern void potr_tcp_health_thread_wake_all(PotrContext *ctx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_HEALTH_THREAD_H */
