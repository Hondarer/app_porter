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

#include <assert.h>

#include <porter/porter_result.h>
#include <porter/potr_context.h>

/**
 *  @brief          送信スレッドのまとめ送りで、1 回の送信呼び出しにまとめる外側パケットの上限数です。
 *
 *  TCP の 1:1 通信では、経路ごとに連結した外側パケットを 1 回の書き込みで送ります。\n
 *  UDP では、外側パケットごとのデータグラムを 1 回の一括の送信で送ります。
 *  N:1 通信では、同じピア宛ての外側パケットだけをまとめます。\n
 *  送出通番の割り当て順は経路ごとに最大この数だけずれるため、
 *  経路数の上限との積は受信側の重複検出の範囲 (直近 256 通番) より小さくします。
 */
#define POTR_SEND_BATCH_MAX_PACKETS 16U

/**
 *  @brief          まとめ送りのバッファーの上限 [byte] です。
 *
 *  TCP の連結バッファーと、UDP の封緘結果の格納先 (外側パケットごとの区画) の両方に適用します。\n
 *  封緘後の外側パケットの最大長 (crypto_buf_size) と POTR_SEND_BATCH_MAX_PACKETS の積がこの値を超える場合は、
 *  1 回にまとめる数を下げます。2 個未満になる場合は、まとめ送りを使いません。
 */
#define POTR_SEND_BATCH_MAX_BYTES 65536U

/**
 *  @brief          まとめ送りを使う、1 回にまとめる外側パケットの最小数です。
 *
 *  1 回にまとめる数 (send_batch_cap) がこの値未満の場合は、まとめ送りを使いません。
 */
#define POTR_SEND_BATCH_MIN_PACKETS 2U

static_assert(POTR_SEND_BATCH_MIN_PACKETS <= POTR_SEND_BATCH_MAX_PACKETS,
              "porter: POTR_SEND_BATCH_MIN_PACKETS must not exceed POTR_SEND_BATCH_MAX_PACKETS");

/**
 *  @brief          TCP の送信バッファー満杯の警告を抑制する回数です。
 *
 *  送信スレッドは、path を飛ばした警告を出力した後、その path を書き込み可能と判定した回数の累計が
 *  この値に達するまで、同じ path の警告を出力しません。書き込み不可の判定は数えません。
 */
#define POTR_TCP_BUF_FULL_SUPPRESS_COUNT 10

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
