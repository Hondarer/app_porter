#ifndef THREAD_RECV_FIN_PRIVATE_H
#define THREAD_RECV_FIN_PRIVATE_H

#include <stdint.h>

#include <porter/protocol/packet.h>

#include "thread_recv_slot.h"

/** FIN ではない。 */
#define THREAD_RECV_FIN_IGNORED 0
/** 目標通番へ未到達のため pending とする。 */
#define THREAD_RECV_FIN_PENDING 1
/** 即時切断対象。発火は呼び出し側が行う。 */
#define THREAD_RECV_FIN_FIRE 2

/**
 * @brief 受信した FIN を pending にするか、即時切断するかを判定します。
 * @param[in,out] slot 構成済みの受信状態ビュー。NULL は許可しません。
 * @param[in] pkt 判定するパケット。NULL は許可しません。
 * @param[out] fin_target_seq_out 即時切断または pending の目標通番。NULL は許可しません。
 * @return THREAD_RECV_FIN_IGNORED、THREAD_RECV_FIN_PENDING、THREAD_RECV_FIN_FIRE のいずれかです。
 * @note FIN の処理区分を返すため共通結果コードの適用対象外です。
 *       FIN でなければ状態を変更しません。
 *       目標付き FIN で受信ウィンドウが未到達の場合は pending とし、切断は発火しません。
 *       呼び出し側が recv_window_mutex を保持している場合は、発火の前に解放してください。
 */
int thread_recv_fin_on_packet(thread_recv_slot *slot, const potr_packet *pkt, uint32_t *fin_target_seq_out);

/**
 * @brief pending FIN が受信ウィンドウの目標通番へ到達したかどうかを判定します。
 * @param[in] slot 構成済みの受信状態ビュー。NULL は許可しません。
 * @param[out] fin_target_seq_out 到達している場合の目標通番。NULL は許可しません。
 * @return 到達している場合は 1、それ以外は 0 を返します。
 * @note 到達判定の述語のため共通結果コードの適用対象外です。
 *       到達していても切断は発火しません。
 */
int thread_recv_fin_pending_reached(const thread_recv_slot *slot, uint32_t *fin_target_seq_out);

/**
 * @brief FIN による切断を発火し、セッション状態をリセットします。
 * @param[in,out] slot 構成済みの受信状態ビュー。NULL は許可しません。
 * @param[in] fin_target_seq TCP の FIN_ACK に設定する目標通番。
 * @note 1:1 の TCP では FIN_ACK を送信した後に経路を切断します。
 *       N:1 ではピアを解放します。recv_window_mutex は保持しないでください。
 *       N:1 では peers_mutex 保護下で呼び出してください。
 */
void thread_recv_fin_fire(thread_recv_slot *slot, uint32_t fin_target_seq);

/**
 * @brief TCP の FIN_ACK 受信を close 待機処理へ通知します。
 * @param[in,out] ctx セッション コンテキスト。NULL は許可しません。
 * @param[in] ack_num 受信した FIN_ACK の通番。
 * @note tcp_close_mutex を取得します。一致する待機処理が存在しない場合は処理を行いません。
 */
void thread_recv_fin_on_ack(potr_context *ctx, uint32_t ack_num);

#endif
