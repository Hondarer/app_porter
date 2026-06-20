/**
 *******************************************************************************
 *  @file           potrSendQueue.h
 *  @brief          非同期送信キューの型定義と操作関数。
 *  @author         Tetsuo Honda
 *  @date           2026/03/08
 *  @version        1.0.0
 *
 *  ノンブロッキング送信 (potrSend の flags に POTR_SEND_BLOCKING なし) で使用される
 *  スレッド セーフな送信キューです。\n
 *  ペイロード エレメントをリング バッファーに積み、送信スレッドが順に
 *  sendto で送出します。\n
 *  ブロッキング送信は potr_send_queue_wait_drained() で先行キューの
 *  sendto 完了を待ってから直接送信します。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/* NOTE: このヘッダーは多数のソース ファイルから参照されるため、            */
/*       @hideincludedbygraph によって "Included by" グラフを無効にします。 */

#ifndef POTR_SEND_QUEUE_H
#define POTR_SEND_QUEUE_H

#include <stddef.h>

#include <com_util/base/platform.h>
#include <porter/porter_const.h>
#include <porter/porter_type.h>

#include <porter/infra/potrPlatform.h>

/**
 *  @brief  送信キューの 1 エントリ。ペイロード エレメント 1 個分のデータを保持します。
 *
 *  外側パケットの構築 (seq_num 付与・window_send_push・sendto) は送信スレッドが行います。\n
 *  本エントリはペイロード エレメントのデータのみを保持し、再送バッファーには登録しない。\n
 *  payload はキュー初期化時に確保したプール スロットを指す。\n
 *  N:1 モードでは peer_id が送信先ピアを示します。1:1 モードでは 0。
 */
typedef struct PotrPayloadElem
{
    PotrPeerId peer_id;   /**< 送信先ピア識別子 (N:1 モード用。1:1 モードでは 0)。 */
    uint16_t flags;       /**< ペイロード エレメント フラグ (MORE_FRAG, COMPRESSED など)。 */
    uint16_t payload_len; /**< ペイロード長 (バイト)。 */
    uint8_t *payload;     /**< ペイロード データへのポインター (プール スロット内を指す)。 */
} PotrPayloadElem;

/**
 *  @brief  非同期送信キュー。
 *
 *  リング バッファーとミューテックス・条件変数により、
 *  potrSend 呼び出し元スレッドと送信スレッドの間でスレッド セーフに
 *  ペイロード エレメント (メッセージのフラグメント) を受け渡します。\n
 *  - count: キュー内エントリ数\n
 *  - inflight: 送信スレッドが sendto 実行中のエントリ数\n
 *  - count + inflight <= depth が常に保証される (ペイロード プール スロット衝突を防止)\n
 *  - not_full 条件変数: count + inflight < depth になったことを通知 (push_wait が待機)\n
 *  - drained 条件変数: count == 0 かつ inflight == 0 を通知 (ブロッキング送信が待機)
 */
typedef struct PotrSendQueue
{
    PotrPayloadElem *entries;    /**< ペイロード エレメント バッファー (動的確保。depth 要素)。 */
    uint8_t *payload_pool;       /**< ペイロード プール (動的確保。depth × max_payload バイト)。 */
    size_t depth;                /**< キュー容量 (エントリ数)。 */
    size_t head;                 /**< 読み出し位置 (送信スレッドが使用)。 */
    size_t tail;                 /**< 書き込み位置 (potrSend 呼び出し元が使用)。 */
    size_t count;                /**< キュー内エントリ数。 */
    size_t inflight;             /**< sendto 実行中エントリ数。 */
    com_util_local_lock *mutex;  /**< 排他制御。 */
    com_util_condvar *not_empty; /**< count > 0 になったことを通知する条件変数。 */
    com_util_condvar *not_full;  /**< count + inflight < depth になったことを通知する条件変数。 */
    com_util_condvar *drained;   /**< count == 0 && inflight == 0 を通知する条件変数。 */
} PotrSendQueue;

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          送信キューを初期化します。
     *  @param[in,out]  q           送信キュー。entries・payload_pool を動的確保して設定します。
     *  @param[in]      depth       キュー容量 (エントリ数)。
     *  @param[in]      max_payload ペイロード最大長 (バイト)。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int potr_send_queue_init(PotrSendQueue *q, size_t depth, uint16_t max_payload);

    /**
     *  @brief          送信キューのリソースを解放します。
     *  @param[in,out]  q   解放する送信キュー。
     */
    extern void potr_send_queue_dispose(PotrSendQueue *q);

    /**
     *  @brief          ペイロード エレメントをキューに追加します。
     *
     *  キューが満杯の場合は待機せず即時 POTR_ERROR を返します。
     *
     *  @param[in,out]  q           送信キュー。
     *  @param[in]      peer_id     送信先ピア識別子 (1:1 モードでは POTR_PEER_NA を指定)。
     *  @param[in]      flags       ペイロード エレメント フラグ。
     *  @param[in]      payload     送信ペイロード データへのポインター。
     *  @param[in]      payload_len 送信ペイロード長 (バイト)。
     *  @return         成功時は POTR_SUCCESS、満杯時は POTR_ERROR。
     */
    extern int potr_send_queue_push(PotrSendQueue *q, PotrPeerId peer_id, uint16_t flags, const void *payload,
                                    uint16_t payload_len);

    /**
     *  @brief          ペイロード エレメントをキューに追加する (空き待機あり)。
     *
     *  キューが満杯の場合は空きが生じるまで待機します。
     *
     *  @param[in,out]  q           送信キュー。
     *  @param[in]      peer_id     送信先ピア識別子 (1:1 モードでは POTR_PEER_NA を指定)。
     *  @param[in]      flags       ペイロード エレメント フラグ。
     *  @param[in]      payload     送信ペイロード データへのポインター。
     *  @param[in]      payload_len 送信ペイロード長 (バイト)。
     *  @param[in]      running     実行フラグへのポインター。0 になると待機を中断します。
     *  @return         成功時は POTR_SUCCESS、running が 0 になった場合は POTR_ERROR。
     */
    extern int potr_send_queue_push_wait(PotrSendQueue *q, PotrPeerId peer_id, uint16_t flags, const void *payload,
                                         uint16_t payload_len, volatile int *running);

    /**
     *  @brief          先頭エントリを取り出して inflight に移行する (ブロッキング)。
     *
     *  キューが空の場合は not_empty 条件変数を待機します。
     *
     *  @param[in,out]  q       送信キュー。
     *  @param[out]     out     取り出したエントリの書き戻し先。
     *  @param[in]      running 実行フラグへのポインター。0 になると待機を中断します。
     *  @return         成功時は POTR_SUCCESS、running が 0 になった場合は POTR_ERROR。
     */
    extern int potr_send_queue_pop(PotrSendQueue *q, PotrPayloadElem *out, volatile int *running);

    /**
     *  @brief          先頭エントリを参照する (inflight へは移行しない)。
     *
     *  キューが空の場合は即時 POTR_ERROR を返します。
     *
     *  @param[in,out]  q   送信キュー (mutex ロック・ロック解除を行う)。
     *  @param[out]     out 先頭エントリの書き戻し先。
     *  @return         成功時は POTR_SUCCESS、空の場合は POTR_ERROR。
     */
    extern int potr_send_queue_peek(PotrSendQueue *q, PotrPayloadElem *out);

    /**
     *  @brief          先頭エントリを参照する (タイムアウトあり)。
     *
     *  キューが空の場合は timeout_ms ミリ秒まで待機します。
     *
     *  @param[in,out]  q           送信キュー。
     *  @param[out]     out         先頭エントリの書き戻し先。
     *  @param[in]      timeout_ms  待機タイムアウト (ミリ秒)。
     *  @return         成功時は POTR_SUCCESS、タイムアウトまたは空の場合は POTR_ERROR。
     */
    extern int potr_send_queue_peek_timed(PotrSendQueue *q, PotrPayloadElem *out, int timeout_ms);

    /**
     *  @brief          先頭エントリを取り出して inflight に移行する (ノンブロッキング)。
     *
     *  キューが空の場合は即時 POTR_ERROR を返します。
     *
     *  @param[in,out]  q   送信キュー。
     *  @param[out]     out 取り出したエントリの書き戻し先。
     *  @return         成功時は POTR_SUCCESS、空の場合は POTR_ERROR。
     */
    extern int potr_send_queue_try_pop(PotrSendQueue *q, PotrPayloadElem *out);

    /**
     *  @brief          inflight エントリを 1 つ完了としてマークします。
     *
     *  inflight をデクリメントし、count == 0 かつ inflight == 0 なら drained を broadcast します。\n
     *  push_wait で待機中のスレッドを起床させるため not_full もシグナルします。
     *
     *  @param[in,out]  q   送信キュー。
     */
    extern void potr_send_queue_complete(PotrSendQueue *q);

    /**
     *  @brief          count と inflight が共に 0 になるまで待機します。
     *  @param[in,out]  q   送信キュー。
     */
    extern void potr_send_queue_wait_drained(PotrSendQueue *q);

    /**
     *  @brief          待機スレッドをすべて起床させてキューをシャットダウンします。
     *
     *  not_empty と not_full の条件変数を broadcast し、pop や push_wait で待機中の
     *  スレッドを起床させます。実際のキュー破棄は potr_send_queue_dispose() で行います。
     *
     *  @param[in,out]  q   送信キュー。
     */
    extern void potr_send_queue_shutdown(PotrSendQueue *q);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_SEND_QUEUE_H */
