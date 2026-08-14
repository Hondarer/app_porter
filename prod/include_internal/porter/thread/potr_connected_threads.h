/**
 *******************************************************************************
 *  @file           potr_connected_threads.h
 *  @brief          接続確立後のスレッドを起動する内部 helper を宣言します。
 *  @author         Tetsuo Honda
 *  @date           2026/04/17
 *  @version        1.0.0
 *
 *  TCP 接続確立後に send / recv / health スレッドを起動し、
 *  途中失敗時の rollback を統一的に扱う内部 helper です。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/* NOTE: このヘッダーは多数のソース ファイルから参照されるため、            */
/*       @hideincludedbygraph によって "Included by" グラフを無効にします。 */

#ifndef POTR_CONNECTED_THREADS_H
#define POTR_CONNECTED_THREADS_H

#include <porter/porter_result.h>
#include <porter/potr_context.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    typedef struct potr_internal_connected_threads_ops
    {
        int (*send_start)(potr_context *ctx);
        void (*send_stop)(potr_context *ctx);
        int (*recv_start)(potr_context *ctx, int path_idx);
        int (*health_start)(potr_context *ctx, int path_idx);
        void (*close_conn)(potr_context *ctx, int path_idx);
        void (*join_recv)(potr_context *ctx, int path_idx);
        void (*set_path_ping_state)(potr_context *ctx, int path_idx, uint8_t next_state);
    } potr_internal_connected_threads_ops;

    /**
     *  @brief          接続確立後に send / recv / health スレッドを起動します。
     *
     *  起動に失敗した場合は起動済みのスレッドを停止してロールバックします。
     *
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx 接続確立済みパスのインデックス。
     *  @param[in]      ops      スレッド操作コールバック テーブル。
     *  @retval         POTR_OK                    起動に成功しました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx または ops が NULL です。
     *  @return         依存する起動処理または PING 送信に失敗した場合は、その結果コードを返します。
     */
    extern int potr_internal_start_connected_threads(potr_context *ctx, int path_idx, const potr_internal_connected_threads_ops *ops);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_CONNECTED_THREADS_H */
