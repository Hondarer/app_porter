/**
 *******************************************************************************
 *  @file           potr_send_thread.h
 *  @brief          データを非同期送信するスレッドの内部インターフェイスを宣言します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/08
 *  @version        1.0.0
 *
 *  送信者 (POTR_ROLE_SENDER) が非ブロッキング送信を使用する場合に起動する
 *  送信スレッドの起動・停止 API。\n
 *  送信スレッドは送信キュー (potr_internal_send_queue) からペイロード エレメントを取り出して
 *  外側パケットを構築し sendto を呼び出します。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/* NOTE: このヘッダーは多数のソース ファイルから参照されるため、            */
/*       @hideincludedbygraph によって "Included by" グラフを無効にします。 */

#ifndef POTR_SEND_THREAD_H
#define POTR_SEND_THREAD_H

#include <porter/porter_result.h>
#include <porter/potr_context.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          非同期送信スレッドを起動します。
     *
     *  send_window_mutex を生成し、送信スレッドを起動します。失敗時は生成済みロックを回収します。
     *
     *  @param[in,out]  ctx セッション コンテキスト。
     *  @retval         POTR_OK                    起動に成功しました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx が NULL です。
     *  @retval         POTR_ERR_UNKNOWN           同期資源またはスレッドの生成に失敗しました。
     */
    extern int potr_internal_send_thread_start(potr_context *ctx);

    /**
     *  @brief          非同期送信スレッドを停止します。
     *
     *  send_thread_running フラグをクリアして送信キューをシャットダウンし、スレッド終了を待機します。
     *  生成済みハンドルを基準に終了待ちし、成功後にハンドルとロックを NULL へ戻します。
     *  待機失敗時はハンドルとロックを保持します。呼び出し側は残存ハンドルを確認してください。
     *
     *  @param[in,out]  ctx セッション コンテキスト。
     */
    extern void potr_internal_send_thread_stop(potr_context *ctx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_SEND_THREAD_H */
