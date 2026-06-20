/**
 *******************************************************************************
 *  @file           potrConnectedThreads.h
 *  @brief          接続確立後スレッド起動 helper の内部ヘッダー。
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

#include <porter/potrContext.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    typedef struct PotrConnectedThreadsOps
    {
        int (*send_start)(PotrContext *ctx);
        void (*send_stop)(PotrContext *ctx);
        int (*recv_start)(PotrContext *ctx, int path_idx);
        int (*health_start)(PotrContext *ctx, int path_idx);
        void (*close_conn)(PotrContext *ctx, int path_idx);
        void (*join_recv)(PotrContext *ctx, int path_idx);
        void (*set_path_ping_state)(PotrContext *ctx, int path_idx, uint8_t next_state);
    } PotrConnectedThreadsOps;

    /**
     *  @brief          接続確立後に send / recv / health スレッドを起動します。
     *
     *  起動に失敗した場合は起動済みのスレッドを停止してロールバックします。
     *
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx 接続確立済みパスのインデックス。
     *  @param[in]      ops      スレッド操作コールバック テーブル。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int potr_start_connected_threads(PotrContext *ctx, int path_idx, const PotrConnectedThreadsOps *ops);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_CONNECTED_THREADS_H */
