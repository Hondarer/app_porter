#ifndef THREAD_RECV_WINDOW_PRIVATE_H
#define THREAD_RECV_WINDOW_PRIVATE_H

#include <cplat/net/endpoint.h>
#include <porter/protocol/packet.h>

#include "thread_recv_slot.h"

/**
 * @brief 外側パケットを受信ウィンドウへ投入し、欠番なら再送要求し、整列済みデータを配信します。
 * @param[in,out] slot 構成済みの受信状態ビュー。NULL は許可しません。
 * @param[in] pkt 投入する DATA または PING の外側パケット。NULL は許可しません。
 * @param[in] path_idx 受信した経路番号。片方向 DATA のヘルス更新に使います。
 * @note 再送と順序整列の入口です。NACK 送出、RAW リセット、ウィンドウ取り出しを内部で完結します。
 *       所有権は移動しません。呼び出し側は同一スロットへの並行更新を避けてください。
 *       N:1 では peers_mutex 保護下で呼び出してください。
 */
void thread_recv_window_accept_outer(thread_recv_slot *slot, const potr_packet *pkt, int path_idx);

/**
 * @brief REJECT を処理し、欠番をスキップして後続データを配信します。
 * @param[in,out] slot 構成済みの受信状態ビュー。NULL は許可しません。
 * @param[in] pkt 受信した REJECT。NULL は許可しません。
 * @param[in] path_idx 受信した経路番号。
 * @param[in] sender 受信した送信元アドレス。NULL は許可しません。
 * @note RAW では何もしません。旧セッションは破棄します。
 *       経路切断とウィンドウ前進を内部で完結します。所有権は移動しません。
 *       N:1 では peers_mutex 保護下で呼び出してください。
 */
void thread_recv_window_on_reject(thread_recv_slot *slot, const potr_packet *pkt, int path_idx,
                                  const cplat_ipv4_endpoint *sender);

/**
 * @brief 受信した NACK に対してデータ再送または REJECT を返します。
 * @param[in,out] slot 構成済みの受信状態ビュー。NULL は許可しません。
 * @param[in] pkt 受信した NACK。NULL は許可しません。
 * @note NACK 以外では何もしません。短時間の同一通番は重複として破棄します。
 *       send_window_mutex を取得します。所有権は移動しません。
 *       N:1 では peers_mutex 保護下で呼び出してください。
 */
void thread_recv_window_on_nack(thread_recv_slot *slot, const potr_packet *pkt);

/**
 * @brief PING が示す前方通番までの欠番を走査し、NACK または RAW リセットを行います。
 * @param[in,out] slot 構成済みの受信状態ビュー。NULL は許可しません。
 * @param[in] pkt 受信した PING。NULL は許可しません。
 * @note ヘルス更新や経路イベント発行は呼び出し側が行います。
 *       N:1 は先頭欠番だけを即時 NACK します。所有権は移動しません。
 */
void thread_recv_window_scan_ping_gap(thread_recv_slot *slot, const potr_packet *pkt);

/**
 * @brief poll タイムアウト時に、リオーダー待機の期限切れを処理します。
 * @param[in,out] slot 構成済みの受信状態ビュー。NULL は許可しません。
 * @note 待機中でなければ何もしません。N:1 はリオーダー待機を開始しないため通常は到達しません。
 *       所有権は移動しません。
 */
void thread_recv_window_check_reorder_timeout(thread_recv_slot *slot);

#endif
