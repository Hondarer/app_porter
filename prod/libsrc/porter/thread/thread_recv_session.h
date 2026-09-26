#ifndef THREAD_RECV_SESSION_PRIVATE_H
#define THREAD_RECV_SESSION_PRIVATE_H

#include <stddef.h>
#include <stdint.h>

#include <cplat/net/endpoint.h>
#include <cplat/sync/atomic.h>
#include <porter/protocol/packet.h>
#include <porter/potr_peer_table.h>

#include "thread_recv_slot.h"

/**
 * @brief セッションの採用判定を行い、必要なら受信状態を初期化します。
 * @param[in,out] slot 構成済みの受信状態ビュー。NULL は許可しません。
 * @param[in] pkt 照合するパケット。NULL は許可しません。
 * @return 採用する場合は 1、旧セッションとして破棄する場合は 0 を返します。
 * @note 採用可否を返す述語のため共通結果コードの適用対象外です。
 *       未知セッションと新セッションでは pending FIN をクリアし、
 *       受信ウィンドウを pkt の通番で初期化します。
 *       同一セッションでは状態を変更しません。
 *       呼び出し側は同一スロットへの並行更新を避けてください。
 */
int thread_recv_session_adopt(thread_recv_slot *slot, const potr_packet *pkt);

/**
 * @brief 1 経路の PING 受信状態を更新します。
 * @param[in,out] state 更新する状態。NULL は許可しません。アトミックに読み書きします。
 * @param[in] next_state 設定する状態。
 * @return 値が変更された場合は 1、変更がない場合は 0 を返します。
 * @note 変化の有無を返す述語のため共通結果コードの適用対象外です。
 */
int thread_recv_set_path_ping_state(cplat_atomic_u8 *state, uint8_t next_state);

/**
 * @brief 複数経路の PING 受信状態を同じ値へ更新します。
 * @param[in,out] states 更新する状態配列。NULL は許可しません。アトミックに読み書きします。
 * @param[in] count 要素数。
 * @param[in] next_state 設定する状態。
 * @return 1 要素でも値が変更された場合は 1、いずれの要素も変更がない場合は 0 を返します。
 * @note 変化の有無を返す述語のため共通結果コードの適用対象外です。
 */
int thread_recv_set_all_path_ping_states(cplat_atomic_u8 *states, size_t count, uint8_t next_state);

/**
 * @brief サービスの全経路を切断し、経路イベントを発行します。
 * @param[in,out] ctx セッション コンテキスト。NULL は許可しません。
 * @note コールバック mutex を取得します。所有権は移動しません。
 */
void thread_recv_disconnect_service_all_paths(potr_context *ctx);

/**
 * @brief ピアの全経路を切断し、経路イベントを発行します。
 * @param[in,out] ctx 所属コンテキスト。NULL は許可しません。
 * @param[in,out] peer 対象ピア。NULL は許可しません。
 * @note コールバック mutex を取得します。所有権は移動しません。
 */
void thread_recv_disconnect_peer_all_paths(potr_context *ctx, potr_internal_peer_context *peer);

/**
 * @brief パスごとの最終受信時刻と PING 受信状態を更新します。
 * @param[in,out] slot 構成済みの受信状態ビュー。NULL は許可しません。
 * @param[in] path_idx 更新する経路番号。
 * @return 受信状態が変化した場合は 1、変化がない場合は 0 を返します。
 * @note 変化の有無を返す述語のため共通結果コードの適用対象外です。
 *       所有権は移動しません。
 */
int thread_recv_update_path_health(thread_recv_slot *slot, int path_idx);

/**
 * @brief 現在の経路受信状態を経路イベントへ反映します。
 * @param[in,out] slot 構成済みの受信状態ビュー。NULL は許可しません。
 * @note コールバック mutex を取得します。所有権は移動しません。
 */
void thread_recv_sync_path_state(thread_recv_slot *slot);

/**
 * @brief 受信した送信元アドレスを経路の送信先として記録します。
 * @param[in,out] slot 構成済みの受信状態ビュー。NULL は許可しません。
 * @param[in] path_idx 更新する経路番号。
 * @param[in] sender 受信した送信元アドレス。NULL は許可しません。
 * @note N:1 はピアの送信先を学習します。1:1 は peer_port と、必要なら dest_addr を更新します。
 *       所有権は移動しません。
 */
void thread_recv_learn_sender_path(thread_recv_slot *slot, int path_idx, const cplat_ipv4_endpoint *sender);

#endif
