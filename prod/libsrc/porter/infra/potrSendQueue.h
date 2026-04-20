/**
 *******************************************************************************
 *  @file           potrSendQueue.h
 *  @brief          非同期送信キューの型定義と操作関数。
 *  @author         Tetsuo Honda
 *  @date           2026/03/08
 *  @version        1.0.0
 *
 *  @details
 *  ノンブロッキング送信 (potrSend の flags に POTR_SEND_BLOCKING なし) で使用される
 *  スレッドセーフな送信キューです。\n
 *  ペイロードエレメントをリングバッファに積み、送信スレッドが順に
 *  sendto で送出します。\n
 *  ブロッキング送信は potr_send_queue_wait_drained() で先行キューの
 *  sendto 完了を待ってから直接送信します。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef POTR_SEND_QUEUE_H
#define POTR_SEND_QUEUE_H

#include <stddef.h>

#include <com_util/base/platform.h>
#include <porter_const.h>
#include <porter_type.h>

#include "potrPlatform.h"

/**
 *  @brief  送信キューの 1 エントリ。ペイロードエレメント 1 個分のデータを保持する。
 *
 *  @details
 *  外側パケットの構築 (seq_num 付与・window_send_push・sendto) は送信スレッドが行う。\n
 *  本エントリはペイロードエレメントのデータのみを保持し、再送バッファには登録しない。\n
 *  payload はキュー初期化時に確保したプールスロットを指す。\n
 *  N:1 モードでは peer_id が送信先ピアを示す。1:1 モードでは 0。
 */
typedef struct
{
    PotrPeerId peer_id;     /**< 送信先ピア識別子 (N:1 モード用。1:1 モードでは 0)。 */
    uint16_t  flags;        /**< ペイロードエレメントフラグ (MORE_FRAG, COMPRESSED など)。 */
    uint16_t  payload_len;  /**< ペイロード長 (バイト)。 */
    uint8_t  *payload;      /**< ペイロードデータへのポインタ (プールスロット内を指す)。 */
} PotrPayloadElem;

/**
 *  @brief  非同期送信キュー。
 *
 *  @details
 *  リングバッファとミューテックス・条件変数により、
 *  potrSend 呼び出し元スレッドと送信スレッドの間でスレッドセーフに
 *  ペイロードエレメント (メッセージのフラグメント) を受け渡します。\n
 *  - count: キュー内エントリ数\n
 *  - inflight: 送信スレッドが sendto 実行中のエントリ数\n
 *  - count + inflight <= depth が常に保証される (ペイロードプールスロット衝突を防止)\n
 *  - not_full 条件変数: count + inflight < depth になったことを通知 (push_wait が待機)\n
 *  - drained 条件変数: count == 0 かつ inflight == 0 を通知 (ブロッキング送信が待機)
 */
typedef struct
{
    PotrPayloadElem *entries;      /**< ペイロードエレメントバッファ (動的確保。depth 要素)。 */
    uint8_t         *payload_pool; /**< ペイロードプール (動的確保。depth × max_payload バイト)。 */
    size_t           depth;        /**< キュー容量 (エントリ数)。 */
    size_t           head;         /**< 読み出し位置 (送信スレッドが使用)。 */
    size_t           tail;         /**< 書き込み位置 (potrSend 呼び出し元が使用)。 */
    size_t           count;        /**< キュー内エントリ数。 */
    size_t           inflight;     /**< sendto 実行中エントリ数。 */
    com_util_mutex_t        mutex;        /**< 排他制御。 */
    com_util_condvar_t      not_empty;    /**< count > 0 になったことを通知する条件変数。 */
    com_util_condvar_t      not_full;     /**< count + inflight < depth になったことを通知する条件変数。 */
    com_util_condvar_t      drained;      /**< count == 0 && inflight == 0 を通知する条件変数。 */
} PotrSendQueue;

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    extern int  potr_send_queue_init(PotrSendQueue *q, size_t depth, uint16_t max_payload);
    extern void potr_send_queue_destroy(PotrSendQueue *q);
    extern int  potr_send_queue_push(PotrSendQueue *q, PotrPeerId peer_id,
                                     uint16_t flags,
                                     const void *payload, uint16_t payload_len);      /* 満杯時は即時 POTR_ERROR */
    extern int  potr_send_queue_push_wait(PotrSendQueue *q, PotrPeerId peer_id,
                                          uint16_t flags,
                                          const void *payload, uint16_t payload_len,
                                          volatile int *running);                      /* 満杯時は空きを待機 */
    extern int  potr_send_queue_pop(PotrSendQueue *q, PotrPayloadElem *out,
                                    volatile int *running);
    extern int  potr_send_queue_peek(PotrSendQueue *q, PotrPayloadElem *out);
    extern int  potr_send_queue_peek_timed(PotrSendQueue *q, PotrPayloadElem *out,
                                           uint32_t timeout_ms);
    extern int  potr_send_queue_try_pop(PotrSendQueue *q, PotrPayloadElem *out);
    extern void potr_send_queue_complete(PotrSendQueue *q);
    extern void potr_send_queue_wait_drained(PotrSendQueue *q);
    extern void potr_send_queue_shutdown(PotrSendQueue *q);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_SEND_QUEUE_H */
