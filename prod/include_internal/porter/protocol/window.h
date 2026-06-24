/**
 *******************************************************************************
 *  @file           window.h
 *  @brief          スライディング ウィンドウを管理する内部 API を提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/* NOTE: このヘッダーは多数のソース ファイルから参照されるため、            */
/*       @hideincludedbygraph によって "Included by" グラフを無効にします。 */

#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>

#include <porter/porter_const.h>
#include <porter/porter_type.h>

/**
 *  @brief  スライディング ウィンドウ管理構造体。
 *
 *  パケット バッファ・有効フラグ・ペイロード プールは動的確保します。\n
 *  window_init() で確保し、window_dispose() で解放すること。
 */
typedef struct PotrWindow
{
    PotrPacket *packets;   /**< パケット バッファー (動的確保。window_size 要素)。 */
    uint8_t *valid;        /**< バッファー有効フラグ配列 (動的確保。window_size バイト)。 */
    uint8_t *payload_pool; /**< ペイロード プール (動的確保。window_size × max_payload バイト)。 */
    uint32_t base_seq;     /**< ウィンドウ先頭の通番。 */
    uint32_t next_seq;     /**< 送信側: 次に割り当てる通番。受信側: 次に期待する通番。 */
    uint16_t window_size;  /**< ウィンドウ サイズ (パケット数)。 */
    uint16_t max_payload;  /**< エントリごとのペイロード最大長 (バイト)。 */
    uint32_t _pad;         /**< パディング (構造体サイズを 8 バイト境界に揃える)。 */
} PotrWindow;

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          ウィンドウを初期化します。
     *  @param[in,out]  win         初期化するウィンドウ構造体へのポインター。
     *  @param[in]      initial_seq 初期通番。
     *  @param[in]      window_size ウィンドウ サイズ (パケット数)。
     *  @param[in]      max_payload エントリごとのペイロード最大長 (バイト)。
     *  @return         成功時は POTR_SUCCESS、malloc 失敗時は POTR_ERROR を返します。
     *
     *  サイズが既存と同一の場合は状態をリセットするのみで再確保は行いません。\n
     *  異なるサイズの場合は既存バッファーを解放して再確保します。
     */
    extern int window_init(PotrWindow *win, uint32_t initial_seq, uint16_t window_size, uint16_t max_payload);

    /**
     *  @brief          ウィンドウが保持する動的確保バッファーを解放します。
     *  @param[in,out]  win  解放するウィンドウ構造体へのポインター。
     */
    extern void window_dispose(PotrWindow *win);

    /**
     *  @brief          送信ウィンドウにパケットを積みます。
     *  @param[in,out]  win     送信ウィンドウ構造体へのポインター。
     *  @param[in]      packet  積むパケットへのポインター。
     *  @return         成功時は POTR_SUCCESS、ウィンドウ満杯の場合は POTR_ERROR を返します。
     *
     *  ACK なし設計のため、満杯の場合は最古エントリを evict して循環利用します。\n
     *  evict されたエントリへの NACK は REJECT で応答します。
     */
    extern int window_send_push(PotrWindow *win, const PotrPacket *packet);

    /**
     *  @brief          送信ウィンドウが満杯かどうかを返します。
     *  @param[in]      win     送信ウィンドウ構造体へのポインター。
     *  @return         満杯の場合は 1、空きがある場合は 0 を返します。
     */
    extern int window_send_full(const PotrWindow *win);

    /**
     *  @brief          送信ウィンドウから指定通番のパケットを取得します (再送用)。
     *  @param[in]      win         送信ウィンドウ構造体へのポインター。
     *  @param[in]      seq_num     取得する通番。
     *  @param[out]     packet_out  取得したパケットを格納する構造体へのポインター。
     *  @return         成功時は POTR_SUCCESS、エントリが存在しない場合は POTR_ERROR を返します。
     */
    extern int window_send_get(const PotrWindow *win, uint32_t seq_num, PotrPacket *packet_out);

    /**
     *  @brief          受信ウィンドウにパケットを格納します。
     *  @param[in,out]  win     受信ウィンドウ構造体へのポインター。
     *  @param[in]      packet  格納するパケットへのポインター。
     *  @return         成功時は POTR_SUCCESS、ウィンドウ外の場合は POTR_ERROR を返します。
     *
     *  通番が受信ウィンドウ内であればバッファリングします。\n
     *  追い越し (順序外到着) にも対応します。
     */
    extern int window_recv_push(PotrWindow *win, const PotrPacket *packet);

    /**
     *  @brief          受信ウィンドウから順序整列済みパケットを取り出します。
     *  @param[in,out]  win     受信ウィンドウ構造体へのポインター。
     *  @param[out]     packet  取り出したパケットを格納する構造体へのポインター。
     *  @return         取り出せた場合は POTR_SUCCESS、次のパケットが未着の場合は POTR_ERROR を返します。
     */
    extern int window_recv_pop(PotrWindow *win, PotrPacket *packet);

    /**
     *  @brief          受信ウィンドウで欠番が発生しているか確認し、NACK 番号を返します。
     *  @param[in]      win         受信ウィンドウ構造体へのポインター。
     *  @param[out]     nack_num    欠番の通番を格納するポインター。
     *  @return         欠番がある場合は 1、ない場合は 0 を返します。
     */
    extern int window_recv_needs_nack(const PotrWindow *win, uint32_t *nack_num);

    /**
     *  @brief          受信ウィンドウで指定通番をスキップして次の通番へ前進させます。
     *  @param[in,out]  win     受信ウィンドウ構造体へのポインター。
     *  @param[in]      seq_num スキップする通番。next_seq と一致する場合のみ動作します。
     *
     *  REJECT 受信時に欠落パケットを「配信済み」として扱い、後続パケットの配信を
     *  継続するために使用します。seq_num が next_seq と一致しない場合は何もしません。
     */
    extern void window_recv_skip(PotrWindow *win, uint32_t seq_num);

    /**
     *  @brief          受信ウィンドウを新しい基点通番でリセットします。
     *  @param[in,out]  win          受信ウィンドウ構造体へのポインター。
     *  @param[in]      new_base_seq リセット後の基点通番 (次に期待する通番)。
     *
     *  全スロットを無効化し、base_seq / next_seq を new_base_seq に設定します。\n
     *  バッファーの再確保は行いません。\n
     *  RAW モードでギャップを検出してセッションをリセットする際に使用します。
     */
    extern void window_recv_reset(PotrWindow *win, uint32_t new_base_seq);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* WINDOW_H */
