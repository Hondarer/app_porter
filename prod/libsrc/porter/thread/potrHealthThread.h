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

#include "../potrContext.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /* 非 TCP 用ヘルスチェックスレッド (単一スレッド) */
    extern int potr_health_thread_start(struct PotrContext_ *ctx);
    extern int potr_health_thread_stop(struct PotrContext_ *ctx);
    extern void potr_health_thread_wake(struct PotrContext_ *ctx);

    /* TCP 用ヘルスチェックスレッド / PING 送信 helper (path ごと) */
    extern int potr_tcp_send_ping_now(struct PotrContext_ *ctx, int path_idx);
    extern int potr_tcp_health_thread_start(struct PotrContext_ *ctx, int path_idx);
    extern int potr_tcp_health_thread_stop(struct PotrContext_ *ctx, int path_idx);
    extern void potr_tcp_health_thread_wake(struct PotrContext_ *ctx, int path_idx);
    extern void potr_tcp_health_thread_wake_all(struct PotrContext_ *ctx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_HEALTH_THREAD_H */
