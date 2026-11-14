/**
 *******************************************************************************
 *  @file           receiver_report.h
 *  @brief          受信報告と返送元ごとのリプレイ記録を定義します。
 *
 *******************************************************************************
 */
#ifndef POTR_PROTOCOL_RECEIVER_REPORT_H
#define POTR_PROTOCOL_RECEIVER_REPORT_H
#include <porter/protocol/packet_seal.h>
#include <porter/porter_const.h>
#include <porter/security/certificate.h>

/** 片方向 UDP の返送元セッション。離脱後も再利用までリプレイ記録を保持します。 */
typedef struct potr_internal_receiver
{
    potr_internal_certificate_state certificate; /**< 認証前から受信者ごとに保持する相互認証。 */
    potr_internal_replay_window replay_window; /**< NACK と REPORT に共通の受信記録。 */
    struct potr_internal_receiver *next; /**< 次の管理エントリ。 */
    uint64_t last_report_ms; /**< 最後に受理した REPORT の到着時刻 [ms]。 */
    uint64_t retired_ms; /**< 離脱時刻 [ms]。NACK だけのエントリは登録時刻。再利用順の基準。 */
    int64_t session_tv_sec; /**< 受信者のセッション開始秒。 */
    int32_t session_tv_nsec; /**< 受信者のセッション開始ナノ秒部。 */
    uint32_t session_id; /**< 受信者のセッション識別子。 */
    uint8_t path_state[POTR_MAX_PATH]; /**< 受信者が報告した経路受信状態。 */
    int active; /**< 期限内に REPORT を受信したエントリ。 */
    int padding; /**< 8 [byte] 境界のパディング。 */
} potr_internal_receiver;
#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/**
 *  @brief          認証後の REPORT または NACK を返送元ごとの記録で検査します。
 *  @param[in,out]  ctx 送信者。NULL は許可しません。
 *  @param[in]      packet 開封済みパケット。NULL は許可しません。
 *  @param[in]      wire_header 受信ヘッダー。NULL は許可しません。
 *  @return         受理時 POTR_OK、上限時 POTR_ERR_FULL、形式・リプレイ不正時 POTR_ERR_PROTOCOL、
 *                  確保失敗時 POTR_ERR_OUT_OF_MEMORY、時計取得失敗時 POTR_ERR_IO、NULL 時 POTR_ERR_INVALID_ARGUMENT。
 *  @note           callback_mutex を内部で取得します。所有権は移動しません。
 */
int potr_internal_receiver_accept(potr_context *ctx, const potr_packet *packet, const uint8_t *wire_header);
/**
 *  @brief          期限切れを除いた全受信者に届くグループの集合を返します。
 *  @param[in,out]  ctx 送信者。NULL は許可しません。
 *  @param[in]      now_ms 単調増加時刻 [ms]。
 *  @return         ビット 0 がグループ 1 を表すマスク。報告中の受信者がなければ 0。
 *  @note           NULL は 0 を返します。集合を返す値関数のため結果コードの対象外です。
 *                  呼び出し側が callback_mutex を保持します。所有権は移動しません。
 */
uint8_t potr_internal_receiver_groups_locked(potr_context *ctx, uint64_t now_ms);
#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* POTR_PROTOCOL_RECEIVER_REPORT_H */
