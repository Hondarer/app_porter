/**
 *******************************************************************************
 *  @file           potr_send_queue.h
 *  @brief          非同期送信キューの型と操作関数を定義します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/08
 *  @version        1.0.0
 *
 *  potr_service_send が使用するスレッド セーフな送信キューです。\n
 *  ペイロード エレメントをリング バッファーに積み、送信スレッドが順に
 *  sendto で送出します。\n
 *  同一ハンドルへの並行送信では、1 メッセージ分の断片の登録を
 *  potr_internal_send_queue_lock_push() と potr_internal_send_queue_unlock_push() で直列化し、
 *  断片が他のメッセージの断片と交互に並ばないようにします。\n
 *  登録したエントリには登録順の番号 (チケット) を付けます。送信スレッドは 1 本で、
 *  取り出した順に完了させるため、完了済み件数が番号以上になれば、その番号までの全エントリが送信済みです。\n
 *  ブロッキング送信は potr_internal_send_queue_wait_ticket() で、自分の最後の断片の番号まで待ちます。
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

#include <cplat/base/platform.h>
#include <cplat/sync/atomic.h>
#include <cplat/sync/sync.h>
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
    potr_peer_id peer_id; /**< 送信先ピア識別子 (N:1 モード用。1:1 モードでは 0)。 */
    uint16_t flags;       /**< ペイロード エレメント フラグ (MORE_FRAG, COMPRESSED など)。 */
    uint16_t payload_len; /**< ペイロード長 (バイト)。 */
    uint8_t *payload;     /**< ペイロード データへのポインター (プール スロット内を指す)。 */
} potr_internal_payload_elem;

/**
 *  @brief  非同期送信キュー。
 *
 *  リング バッファーとミューテックス・条件変数により、
 *  potr_service_send 呼び出し元スレッドと送信スレッドの間でスレッド セーフに
 *  ペイロード エレメント (メッセージのフラグメント) を受け渡します。\n
 *  - count: キュー内エントリ数\n
 *  - inflight: 送信スレッドが sendto 実行中のエントリ数\n
 *  - count + inflight <= depth が常に保証される (ペイロード プール スロット衝突を防止)\n
 *  - not_full 条件変数: count + inflight < depth になったことを通知 (push_wait が待機)\n
 *  - drained 条件変数: count == 0 かつ inflight == 0 を通知 (close の送信完了待ちが待機)\n
 *  - progress 条件変数: completed_seq の増加を通知 (ブロッキング送信が待機)
 */
typedef struct potr_internal_send_queue
{
    potr_internal_payload_elem *entries; /**< ペイロード エレメント バッファー (動的確保。depth 要素)。 */
    uint8_t *payload_pool;               /**< ペイロード プール (動的確保。depth × max_payload バイト)。 */
    int failure;                         /**< 永続する待機中断理由。mutex で保護します。 */
    int failure_padding;                 /**< 境界調整。 */
    size_t depth;                        /**< キュー容量 (エントリ数)。 */
    size_t head;                         /**< 読み出し位置 (送信スレッドが使用)。 */
    size_t tail;                         /**< 書き込み位置 (potr_service_send 呼び出し元が使用)。 */
    size_t count;                        /**< キュー内エントリ数。 */
    size_t inflight;                     /**< sendto 実行中エントリ数。 */
    uint64_t pushed_seq;                 /**< 登録済みエントリ数。登録順の番号の最大値。mutex で保護します。 */
    uint64_t completed_seq;              /**< 完了済みエントリ数。mutex で保護します。 */
    cplat_local_lock *mutex;             /**< 排他制御。 */
    cplat_local_lock *push_mutex;        /**< 1 メッセージ分の登録を直列化する。mutex より先に取得します。 */
    cplat_condvar *not_empty;            /**< count > 0 になったことを通知する条件変数。 */
    cplat_condvar *not_full;             /**< count + inflight < depth になったことを通知する条件変数。 */
    cplat_condvar *drained;              /**< count == 0 && inflight == 0 を通知する条件変数。 */
    cplat_condvar *progress;             /**< completed_seq が増えたことを通知する条件変数。 */
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
     *  @return         成功時は POTR_OK、メモリ確保失敗時は POTR_ERR_OUT_OF_MEMORY、
     *                  同期資源生成失敗時は POTR_ERR_UNKNOWN を返します。
     *  @note           失敗時は生成済み資源を回収します。成功時の資源はキューが所有します。
     */
    extern int potr_internal_send_queue_init(potr_internal_send_queue *q, size_t depth, uint16_t max_payload);

    /**
     *  @brief          送信キューのリソースを解放します。
     *  @note           使用する全スレッドの終了待ち後に呼びます。部分初期化を許可し、所有ポインターを NULL へ戻します。
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
     *  @param[out]     ticket      登録したエントリの番号の格納先。NULL の場合は格納しません。
     *  @return         成功時は POTR_OK、満杯時は POTR_ERR_FULL。
     */
    extern int potr_internal_send_queue_push(potr_internal_send_queue *q, potr_peer_id peer_id, uint16_t flags,
                                             const void *payload, uint16_t payload_len, uint64_t *ticket);

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
     *  @param[in]      running     実行フラグへのポインター。取得順序で読み、0 になると待機を中断します。
     *  @param[out]     ticket      登録したエントリの番号の格納先。NULL の場合は格納しません。
     *  @return         成功時は POTR_OK、running が 0 になった場合は POTR_ERR_CANCELED。
     */
    extern int potr_internal_send_queue_push_wait(potr_internal_send_queue *q, potr_peer_id peer_id, uint16_t flags,
                                                  const void *payload, uint16_t payload_len, cplat_atomic_i32 *running,
                                                  uint64_t *ticket);

    /**
     *  @brief          先頭エントリを取り出して inflight に移行する (ブロッキング)。
     *
     *  キューが空の場合は not_empty 条件変数を待機します。
     *
     *  @param[in,out]  q       送信キュー。
     *  @param[out]     out     取り出したエントリの書き戻し先。
     *  @param[in]      running 実行フラグへのポインター。取得順序で読み、0 になると待機を中断します。
     *  @return         成功時は POTR_OK、running が 0 になった場合は POTR_ERR_CANCELED。
     */
    extern int potr_internal_send_queue_pop(potr_internal_send_queue *q, potr_internal_payload_elem *out,
                                            cplat_atomic_i32 *running);

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
    extern int potr_internal_send_queue_peek_timed(potr_internal_send_queue *q, potr_internal_payload_elem *out,
                                                   int timeout_ms);

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
     *  completed_seq をインクリメントし、progress を broadcast します。\n
     *  push_wait で待機中のスレッドを起床させるため not_full もシグナルします。\n
     *  送信スレッドは、取り出した順に本関数を呼び出してください。
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
     *  @brief          指定した番号までのエントリがすべて完了するまで待機します。
     *
     *  completed_seq >= ticket になるまで待機します。送信スレッドの停止を理由に待機を中断しません。
     *
     *  @param[in,out]  q       送信キュー。
     *  @param[in]      ticket  待機する番号。potr_internal_send_queue_push_wait() が返した値を指定します。
     */
    extern void potr_internal_send_queue_wait_ticket(potr_internal_send_queue *q, uint64_t ticket);

    /**
     *  @brief          1 メッセージ分の登録を開始し、他の登録元を待たせます。
     *
     *  同一キューへの複数スレッドからの登録で、1 メッセージの断片が他のメッセージの断片と
     *  交互に並ばないようにします。potr_internal_send_queue_unlock_push() と対で呼び出します。
     *
     *  @param[in,out]  q   送信キュー。
     */
    extern void potr_internal_send_queue_lock_push(potr_internal_send_queue *q);

    /**
     *  @brief          1 メッセージ分の登録を終了します。
     *  @param[in,out]  q   送信キュー。
     */
    extern void potr_internal_send_queue_unlock_push(potr_internal_send_queue *q);

    /**
     *  @brief          待機スレッドをすべて起床させてキューをシャットダウンします。
     *
     *  not_empty と not_full の条件変数を broadcast し、pop や push_wait で待機中の
     *  スレッドを起床させます。実際のキュー破棄は potr_internal_send_queue_dispose() で行います。
     *
     *  @param[in,out]  q   送信キュー。
     */
    extern void potr_internal_send_queue_shutdown(potr_internal_send_queue *q);

    /** @brief 異常で全待機を解除します。@param[in,out] q キュー。@param[in] error 原因コード。 */
    void potr_internal_send_queue_abort(potr_internal_send_queue *q, int error);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_SEND_QUEUE_H */
