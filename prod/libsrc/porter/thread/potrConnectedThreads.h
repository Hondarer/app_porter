/**
 *******************************************************************************
 *  @file           potrConnectedThreads.h
 *  @brief          接続確立後スレッド起動 helper の内部ヘッダー。
 *  @author         Tetsuo Honda
 *  @date           2026/04/17
 *  @version        1.0.0
 *
 *  @details
 *  TCP 接続確立後に send / recv / health スレッドを起動し、
 *  途中失敗時の rollback を統一的に扱う内部 helper です。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef POTR_CONNECTED_THREADS_H
#define POTR_CONNECTED_THREADS_H

#include "../potrContext.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

typedef struct PotrConnectedThreadsOps_
{
    int  (*send_start)(struct PotrContext_ *ctx);
    void (*send_stop)(struct PotrContext_ *ctx);
    int  (*recv_start)(struct PotrContext_ *ctx, int path_idx);
    int  (*health_start)(struct PotrContext_ *ctx, int path_idx);
    void (*close_conn)(struct PotrContext_ *ctx, int path_idx);
    void (*join_recv)(struct PotrContext_ *ctx, int path_idx);
    void (*set_path_ping_state)(struct PotrContext_ *ctx,
                                int                  path_idx,
                                uint8_t              next_state);
} PotrConnectedThreadsOps;

extern int potr_start_connected_threads(struct PotrContext_           *ctx,
                                        int                            path_idx,
                                        const PotrConnectedThreadsOps *ops);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_CONNECTED_THREADS_H */
