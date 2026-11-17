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

/*
 * このヘッダーは多数のソース ファイルから参照されるため、
 * @hideincludedbygraph によって "Included by" グラフを無効にする。
 */

#ifndef POTR_SEND_THREAD_H
#define POTR_SEND_THREAD_H

#include <porter/porter_result.h>
#include <porter/potr_context.h>

/**
 *  @brief          TCP の 1:1 通信で、送信スレッドが 1 回の送信呼び出しにまとめる外側パケットの上限数です。
 *
 *  送出通番の割り当て順は経路ごとに最大この数だけずれるため、
 *  経路数の上限との積は受信側の重複検出の範囲 (直近 256 通番) より小さくします。
 */
#define POTR_TCP_BATCH_MAX_PACKETS 16U

/**
 *  @brief          まとめ送りの連結バッファーの上限 [byte] です。
 *
 *  封緘後の外側パケットの最大長 (crypto_buf_size) と POTR_TCP_BATCH_MAX_PACKETS の積がこの値を超える場合は、
 *  1 回にまとめる数を下げます。2 個未満になる場合は、まとめ送りを使いません。
 */
#define POTR_TCP_BATCH_MAX_BYTES 65536U

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
     *  生成済みハンドルを基準に終了待ちし、ハンドルとロックを NULL へ戻します。
     *
     *  @param[in,out]  ctx セッション コンテキスト。
     */
    extern void potr_internal_send_thread_stop(potr_context *ctx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_SEND_THREAD_H */
