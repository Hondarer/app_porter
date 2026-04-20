/**
 *******************************************************************************
 *  @file           potrConnectThread.h
 *  @brief          TCP 接続管理スレッドの内部ヘッダー。
 *  @author         Tetsuo Honda
 *  @date           2026/03/23
 *  @version        1.0.0
 *
 *  @details
 *  SENDER: TCP connect / 自動再接続ループを管理するスレッドです。\n
 *  RECEIVER: TCP accept ループを管理するスレッドです。\n
 *  接続確立後、送受信・ヘルスチェックスレッドを起動します。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef POTR_CONNECT_THREAD_H
#define POTR_CONNECT_THREAD_H

#include "../potrContext.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief  TCP 接続管理スレッドを起動します。
     *  @return  成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
     */
    extern int  potr_connect_thread_start(struct PotrContext_ *ctx);

    /**
     *  @brief  TCP 接続管理スレッドを停止します。
     */
    extern void potr_connect_thread_stop(struct PotrContext_ *ctx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_CONNECT_THREAD_H */
