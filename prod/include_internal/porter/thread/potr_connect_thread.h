/**
 *******************************************************************************
 *  @file           potr_connect_thread.h
 *  @brief          TCP 接続を管理するスレッドの内部インターフェイスを宣言します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/23
 *  @version        1.0.0
 *
 *  SENDER: TCP connect / 自動再接続ループを管理するスレッドです。\n
 *  RECEIVER: TCP accept ループを管理するスレッドです。\n
 *  接続確立後、送受信・ヘルス チェック スレッドを起動します。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/* NOTE: このヘッダーは多数のソース ファイルから参照されるため、            */
/*       @hideincludedbygraph によって "Included by" グラフを無効にします。 */

#ifndef POTR_CONNECT_THREAD_H
#define POTR_CONNECT_THREAD_H

#include <porter/porter_result.h>
#include <porter/potr_context.h>

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
     *  @param[in,out]  ctx セッション コンテキスト。
     *  @retval         POTR_OK                    起動に成功しました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx が NULL です。
     *  @retval         POTR_ERR_OUT_OF_MEMORY     先読みバッファーを確保できません。
     *  @retval         POTR_ERR_UNKNOWN           同期資源またはスレッドの生成に失敗しました。
     *  @note           途中失敗時は起動成功分を停止・終了待ちし、受信者用準備資源を回収します。
     *                  終了待ちに失敗した場合は使用中の資源とハンドルを保持します。
     */
    extern int potr_internal_connect_thread_start(potr_context *ctx);

    /**
     *  @brief          TCP 接続管理スレッドを停止します。
     *
     *  connect_thread_running フラグをクリアし、tcp_state_cv を broadcast して\n
     *  reconnect sleep を中断します。全 path のソケットをクローズしてスレッド終了を待機します。
     *  稼働フラグが 0 でも生成済みハンドルを終了待ちし、成功後に NULL へ戻します。
     *  起動数が 0 の場合も受信者用準備資源を回収します。待機失敗時は共有資源を保持します。
     *
     *  @param[in,out]  ctx セッション コンテキスト。
     */
    extern void potr_internal_connect_thread_stop(potr_context *ctx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_CONNECT_THREAD_H */
