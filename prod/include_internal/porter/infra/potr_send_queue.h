/**
 *******************************************************************************
 *  @file           potr_send_queue.h
 *  @brief          非同期送信キューの型と操作関数を定義します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/08
 *  @version        1.0.0
 *
 *  非ブロッキング送信 (potr_send の flags に POTR_SEND_BLOCKING なし) で使用される
 *  スレッド セーフな送信キューです。\n
 *  ペイロード エレメントをリング バッファーに積み、送信スレッドが順に
 *  sendto で送出します。\n
 *  ブロッキング送信は potr_internal_send_queue_wait_drained() で先行キューの
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
#include <com_util/sync/sync.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/porter_type.h>

/**
 *  @brief  送信キューの 1 エントリ。ペイロード エレメント 1 個分のデータを保持します。
 *
 *  外側パケットの構築 (seq_num 付与・potr_internal_window_send_push・sendto) は送信スレッドが行います。\n
 *  本エントリはペイロード エレメントのデータのみを保持し、再送バッファーには登録しない。\n
 *  payload はキュー初期化時に確保したプール スロットを指す。\n
 *  N:1 モードでは peer_id が送信先ピアを示します。1:1 モードでは 0。
 */
typedef struct potr_internal_payload_elem
{
    potr_peer_id peer_id;   /**< 送信先ピア識別子 (N:1 モード用。1:1 モードでは 0)。 */
    uint16_t flags;       /**< ペイロード エレメント フラグ (MORE_FRAG, COMPRESSED など)。 */
    uint16_t payload_len; /**< ペイロード長 (バイト)。 */
    uint8_t *payload;     /**< ペイロード データへのポインター (プール スロット内を指す)。 */
} potr_internal_payload_elem;

/**
 *  @brief  非同期送信キュー。
 *
 *  リング バッファーとミューテックス・条件変数により、
 *  potr_send 呼び出し元スレッドと送信スレッドの間でスレッド セーフに
 *  ペイロード エレメント (メッセージのフラグメント) を受け渡します。\n
 *  - count: キュー内エントリ数\n
 *  - inflight: 送信スレッドが sendto 実行中のエントリ数\n
 *  - count + inflight <= depth が常に保証される (ペイロード プール スロット衝突を防止)\n
 *  - not_full 条件変数: count + inflight < depth になったことを通知 (push_wait が待機)\n
 *  - drained 条件変数: count == 0 かつ inflight == 0 を通知 (ブロッキング送信が待機)
 */
typedef struct potr_internal_send_queue
{
    potr_internal_payload_elem *entries;    /**< ペイロード エレメント バッファー (動的確保。depth 要素)。 */
    uint8_t *payload_pool;       /**< ペイロード プール (動的確保。depth × max_payload バイト)。 */
    size_t depth;                /**< キュー容量 (エントリ数)。 */
    size_t head;                 /**< 読み出し位置 (送信スレッドが使用)。 */
    size_t tail;                 /**< 書き込み位置 (potr_send 呼び出し元が使用)。 */
    size_t count;                /**< キュー内エントリ数。 */
    size_t inflight;             /**< sendto 実行中エントリ数。 */
    com_util_local_lock *mutex;  /**< 排他制御。 */
    com_util_condvar *not_empty; /**< count > 0 になったことを通知する条件変数。 */
    com_util_condvar *not_full;  /**< count + inflight < depth になったことを通知する条件変数。 */
    com_util_condvar *drained;   /**< count == 0 && inflight == 0 を通知する条件変数。 */
} potr_internal_send_queue;

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          送信キューを初期化します。
     *  @param[in,out]  q           送信キュー。entries・payload_pool を動的確保して設定します。
     *  @param[in]      depth       キュー容量 (エントリ数)。
     *  @param[in]      max_payload ペイロード最大長 (バイト)。
     *  @return         成功時は POTR_OK、メモリ確保に失敗した場合は POTR_ERR_OUT_OF_MEMORY。
     */
    extern int potr_internal_send_queue_init(potr_internal_send_queue *q, size_t depth, uint16_t max_payload);

    /**
     *  @brief          送信キューのリソースを解放します。
     *  @param[in,out]  q   解放する送信キュー。
     */
    extern void potr_internal_send_queue_dispose(potr_internal_send_queue *q);

    /**
     *  @brief          ペイロード エレメントをキューに追加します。
     *
     *  キューが満杯の場合は待機せず即時 POTR_ERR_FULL を返します。
     *
     *  @param[in,out]  q           送信キュー。
     *  @param[in]      peer_id     送信先ピア識別子 (1:1 モードでは POTR_PEER_NA を指定)。
     *  @param[in]      flags       ペイロード エレメント フラグ。
     *  @param[in]      payload     送信ペイロード データへのポインター。
     *  @param[in]      payload_len 送信ペイロード長 (バイト)。
     *  @return         成功時は POTR_OK、満杯時は POTR_ERR_FULL。
     */
    extern int potr_internal_send_queue_push(potr_internal_send_queue *q, potr_peer_id peer_id, uint16_t flags, const void *payload,
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
     *  @return         成功時は POTR_OK、running が 0 になった場合は POTR_ERR_CANCELED。
     */
    extern int potr_internal_send_queue_push_wait(potr_internal_send_queue *q, potr_peer_id peer_id, uint16_t flags, const void *payload,
                                         uint16_t payload_len, volatile int *running);

    /**
     *  @brief          先頭エントリを取り出して inflight に移行する (ブロッキング)。
     *
     *  キューが空の場合は not_empty 条件変数を待機します。
     *
     *  @param[in,out]  q       送信キュー。
     *  @param[out]     out     取り出したエントリの書き戻し先。
     *  @param[in]      running 実行フラグへのポインター。0 になると待機を中断します。
     *  @return         成功時は POTR_OK、running が 0 になった場合は POTR_ERR_CANCELED。
     */
    extern int potr_internal_send_queue_pop(potr_internal_send_queue *q, potr_internal_payload_elem *out, volatile int *running);

    /**
     *  @brief          先頭エントリを参照する (inflight へは移行しない)。
     *
     *  キューが空の場合は即時 POTR_ERR_EMPTY を返します。
     *
     *  @param[in,out]  q   送信キュー (mutex ロック・ロック解除を行う)。
     *  @param[out]     out 先頭エントリの書き戻し先。
     *  @return         成功時は POTR_OK、空の場合は POTR_ERR_EMPTY。
     */
    extern int potr_internal_send_queue_peek(potr_internal_send_queue *q, potr_internal_payload_elem *out);

    /**
     *  @brief          先頭エントリを参照する (タイムアウトあり)。
     *
     *  キューが空の場合は timeout_ms ミリ秒まで待機します。
     *
     *  @param[in,out]  q           送信キュー。
     *  @param[out]     out         先頭エントリの書き戻し先。
     *  @param[in]      timeout_ms  待機タイムアウト (ミリ秒)。
     *  @return         成功時は POTR_OK、timeout_ms 以内にエントリが到着しなかった場合は POTR_ERR_TIMEOUT。
     */
    extern int potr_internal_send_queue_peek_timed(potr_internal_send_queue *q, potr_internal_payload_elem *out, int timeout_ms);

    /**
     *  @brief          先頭エントリを取り出して inflight に移行する (非ブロッキング)。
     *
     *  キューが空の場合は即時 POTR_ERR_EMPTY を返します。
     *
     *  @param[in,out]  q   送信キュー。
     *  @param[out]     out 取り出したエントリの書き戻し先。
     *  @return         成功時は POTR_OK、空の場合は POTR_ERR_EMPTY。
     */
    extern int potr_internal_send_queue_try_pop(potr_internal_send_queue *q, potr_internal_payload_elem *out);

    /**
     *  @brief          inflight エントリを 1 つ完了としてマークします。
     *
     *  inflight をデクリメントし、count == 0 かつ inflight == 0 なら drained を broadcast します。\n
     *  push_wait で待機中のスレッドを起床させるため not_full もシグナルします。
     *
     *  @param[in,out]  q   送信キュー。
     */
    extern void potr_internal_send_queue_complete(potr_internal_send_queue *q);

    /**
     *  @brief          count と inflight が共に 0 になるまで待機します。
     *  @param[in,out]  q   送信キュー。
     */
    extern void potr_internal_send_queue_wait_drained(potr_internal_send_queue *q);

    /**
     *  @brief          待機スレッドをすべて起床させてキューをシャットダウンします。
     *
     *  not_empty と not_full の条件変数を broadcast し、pop や push_wait で待機中の
     *  スレッドを起床させます。実際のキュー破棄は potr_internal_send_queue_dispose() で行います。
     *
     *  @param[in,out]  q   送信キュー。
     */
    extern void potr_internal_send_queue_shutdown(potr_internal_send_queue *q);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_SEND_QUEUE_H */
