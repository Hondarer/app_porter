/**
 *******************************************************************************
 *  @file           potrSendThread.h
 *  @brief          データを非同期送信するスレッドの内部インターフェイスを宣言します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/08
 *  @version        1.0.0
 *
 *  送信者 (POTR_ROLE_SENDER) が非ブロッキング送信を使用する場合に起動する
 *  送信スレッドの起動・停止 API。\n
 *  送信スレッドは送信キュー (PotrSendQueue) からペイロード エレメントを取り出して
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

#include <porter/potrContext.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          非同期送信スレッドを起動します。
     *
     *  send_window_mutex を生成し、送信スレッドを起動します。
     *
     *  @param[in,out]  ctx セッション コンテキスト。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int potr_send_thread_start(PotrContext *ctx);

    /**
     *  @brief          非同期送信スレッドを停止します。
     *
     *  send_thread_running フラグをクリアして送信キューをシャットダウンし、スレッド終了を待機します。
     *
     *  @param[in,out]  ctx セッション コンテキスト。
     */
    extern void potr_send_thread_stop(PotrContext *ctx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_SEND_THREAD_H */
