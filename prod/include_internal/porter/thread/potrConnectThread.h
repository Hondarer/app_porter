/**
 *******************************************************************************
 *  @file           potrConnectThread.h
 *  @brief          TCP 接続管理スレッドの内部ヘッダー。
 *  @author         Tetsuo Honda
 *  @date           2026/03/23
 *  @version        1.0.0
 *
 *  SENDER: TCP connect / 自動再接続ループを管理するスレッドです。\n
 *  RECEIVER: TCP accept ループを管理するスレッドです。\n
 *  接続確立後、送受信・ヘルスチェックスレッドを起動します。
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

#ifndef POTR_CONNECT_THREAD_H
#define POTR_CONNECT_THREAD_H

#include <porter/potrContext.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          TCP 接続管理スレッドを起動します。
     *
     *  呼び出し前提:\n
     *  - SENDER: dst_addr_resolved[i] および dst_port が設定済みであること (n_path 分)。\n
     *  - RECEIVER: tcp_listen_sock[i] が listen 状態であること (n_path 分)。\n
     *  - tcp_state_mutex / tcp_state_cv / tcp_send_mutex[] が初期化済みであること。
     *
     *  @param[in,out]  ctx セッションコンテキスト。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int potr_connect_thread_start(PotrContext *ctx);

    /**
     *  @brief          TCP 接続管理スレッドを停止します。
     *
     *  connect_thread_running フラグをクリアし、tcp_state_cv を broadcast して\n
     *  reconnect sleep を中断する。全 path のソケットをクローズしてスレッド終了を待機する。
     *
     *  @param[in,out]  ctx セッションコンテキスト。
     */
    extern void potr_connect_thread_stop(PotrContext *ctx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_CONNECT_THREAD_H */
