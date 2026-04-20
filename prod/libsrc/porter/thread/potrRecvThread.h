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

#include "../potrContext.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /* 非 TCP 用受信スレッド (単一スレッド) */
    extern int comm_recv_thread_start(struct PotrContext_ *ctx);
    extern int comm_recv_thread_stop(struct PotrContext_ *ctx);

    /* TCP 用受信スレッド (path ごと) */
    extern int tcp_recv_thread_start(struct PotrContext_ *ctx, int path_idx);
    extern int tcp_recv_thread_stop(struct PotrContext_ *ctx, int path_idx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_RECV_THREAD_H */
