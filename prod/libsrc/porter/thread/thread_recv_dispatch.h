#ifndef THREAD_RECV_DISPATCH_PRIVATE_H
#define THREAD_RECV_DISPATCH_PRIVATE_H

#include <stddef.h>
#include <stdint.h>

#include <cplat/net/endpoint.h>
#include <porter/protocol/packet.h>

#include "thread_recv_slot.h"

/**
 * @brief 構成済みスロットへ FIN / NACK / REJECT / DATA / PING を振り分けます。
 * @param[in,out] slot 構成済みの受信状態ビュー。NULL は許可しません。
 * @param[in,out] pkt 認証済みパケット。NULL は許可しません。
 * @param[in] path_idx 受信した経路番号。
 * @param[in] sender 受信した送信元アドレス。NULL は許可しません。
 * @return N:1 で PING 受信状態が変化した場合は 1、それ以外は 0 を返します。
 * @note 変化の有無を返す述語のため共通結果コードの適用対象外です。
 *       1:1 の FIN はセッション採用後に処理します。N:1 の FIN は採用せずに処理します。
 *       NACK 再送は N:1 のみ行います。1:1 の NACK は呼び出し側の送信者処理が担当します。
 *       戻り値 1 のときは、peers_mutex を解放してからヘルスチェック スレッドを起床してください。
 *       所有権は移動しません。N:1 では peers_mutex 保護下で呼び出してください。
 */
int thread_recv_dispatch_packet(thread_recv_slot *slot, potr_packet *pkt, int path_idx,
                                const cplat_ipv4_endpoint *sender);

/**
 * @brief PING ペイロードの経路受信状態を取り込みます。
 * @param[in,out] dst 更新する状態配列。NULL は許可しません。
 * @param[in] src 受信した状態配列。NULL は許可しません。
 * @param[in] count 要素数。
 * @note UNDEFINED の入力は既存値を後退させないため更新しません。所有権は移動しません。
 */
void thread_recv_apply_remote_path_ping_state(uint8_t *dst, const uint8_t *src, size_t count);

/**
 * @brief UDP 双方向で、経路受信状態の変化に応じてヘルスチェック スレッドを起床します。
 * @param[in,out] ctx セッション コンテキスト。NULL は許可しません。
 * @param[in] state_changed 受信状態が変化した場合は 0 以外。
 * @note unicast_bidir 以外では何もしません。所有権は移動しません。
 */
void thread_recv_wake_udp_interrupt_ping(potr_context *ctx, int state_changed);

#endif
