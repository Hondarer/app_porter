/**
 *******************************************************************************
 *  @file           packet_seal.h
 *  @brief          送出の封緘、受信の開封とリプレイ窓を定義します。
 *
 *******************************************************************************
 */
#ifndef POTR_PROTOCOL_PACKET_SEAL_H
#define POTR_PROTOCOL_PACKET_SEAL_H

#include <stdint.h>
#include <stddef.h>
#include <cplat/sync/atomic.h>
#include <porter/porter_type.h>

/** 送信側セッションの通番と単調増加時計の起点です。 */
typedef struct potr_internal_tx_session
{
    cplat_atomic_u64 next_counter; /**< 次の送出通番。UINT64_MAX は送出禁止を表します。 */
    uint64_t start_mono_ns; /**< セッション開始時の単調増加時計 [ns]。 */
    int start_mono_valid; /**< 時計の起点を取得できたか。 */
    cplat_atomic_i32 exhausted; /**< 枯渇検出後の N:1 ピア切断要求。 */
} potr_internal_tx_session;

/** 送信元セッションに属する直近 256 通番の認証済み受信記録です。 */
typedef struct potr_internal_replay_window
{
    uint64_t bitmap[4]; /**< 最大通番からの距離をビット位置とする受信記録。 */
    uint64_t highest; /**< 受信済み最大通番。 */
    int64_t session_tv_sec; /**< 送信元セッション開始時刻の秒部。 */
    int32_t session_tv_nsec; /**< 送信元セッション開始時刻のナノ秒部。 */
    uint32_t session_id; /**< 送信元セッション識別子。 */
    int known; /**< 受信記録が有効か。 */
    int padding; /**< 8 [byte] 境界のパディング。 */
} potr_internal_replay_window;

#ifdef __cplusplus
extern "C"
{
#endif

/**
 *  @brief          自セッションの送出状態を初期化します。
 *  @param[out]     session 初期化先。NULL の場合は無処理で戻ります。
 *  @note           自セッション生成時に、送出スレッドの起動前に呼び出します。
 *                  時計の取得失敗は起点無効として記録します。所有権は移動しません。
 */
void potr_internal_tx_session_init(potr_internal_tx_session *session);

/**
 *  @brief          NBO パケットを送出ごとに採番してワイヤーへ封緘します。
 *  @param[in,out]  session 自セッションの送出状態。NULL は許可しません。
 *  @param[in]      packet NBO のヘッダーと平文ペイロード。NULL は許可しません。
 *  @param[in]      key 暗号鍵。NULL の場合は平文です。
 *  @param[out]     wire 封緘先。ペイロードと重複させません。NULL は許可しません。
 *  @param[in,out]  wire_len 入力は容量、成功時の出力はワイヤー長。NULL は許可しません。
 *  @return         成功時は POTR_OK、通番枯渇時は POTR_ERR_DISCONNECTED、引数・容量不正時は
 *                  POTR_ERR_INVALID_ARGUMENT、暗号化失敗時は POTR_ERR_UNKNOWN です。
 *  @par            スレッド セーフ
 *  通番はアトミックに採番します。起点は初期化後に変更しません。
 *  パケットと出力バッファーの並行更新は呼び出し側で排他します。所有権は移動しません。
 */
int potr_internal_packet_seal(potr_internal_tx_session *session, const potr_packet *packet,
                              const uint8_t *key, uint8_t *wire, size_t *wire_len);

/**
 *  @brief          封緘し、通番枯渇時にはセッション停止を要求します。
 *  @param[in,out]  ctx サービス。NULL は許可しません。
 *  @param[in,out]  session 自セッション。NULL は許可しません。
 *  @param[in]      packet 平文の NBO パケット。NULL は許可しません。
 *  @param[out]     wire 封緘先。NULL は許可しません。
 *  @param[in,out]  wire_len 容量と成功時のワイヤー長。NULL は許可しません。
 *  @return         potr_internal_packet_seal() の結果を返します。
 *                  証明書方式で鍵が未確立の場合は POTR_ERR_DISCONNECTED を返します。
 *  @note           事前共有鍵方式では encrypt_key、証明書方式では送信先セッションの送信鍵を使います。
 *                  一対多の DATA、PING と再送にはグループ鍵を使います。HANDSHAKE は平文です。
 *  @note           バッファーとパケットは呼び出し側で排他します。所有権は移動しません。
 *                  1:1 は通知継続不能として送信を停止します。N:1 の自セッションは exhausted を設定し、
 *                  呼び出し側が peers_mutex の下で処理完了後に論理切断して解放します。
 */
int potr_internal_packet_seal_for_context(potr_context *ctx, potr_internal_tx_session *session,
                                          const potr_packet *packet, uint8_t *wire, size_t *wire_len);

/**
 *  @brief          解析済みパケットをヘッダー全体の認証後に開封します。
 *  @param[in,out]  packet ホスト バイト オーダーのパケット。NULL は許可しません。
 *  @param[in]      wire_header 受信ヘッダー 72 [byte]。NULL は許可しません。
 *  @param[in]      key 暗号鍵。NULL は平文のみ受理します。
 *  @param[out]     plaintext 復号先。暗号化時は NULL を許可しません。
 *  @param[in]      capacity 復号先の容量 [byte]。
 *  @return         成功時は POTR_OK、形式・認証不正は POTR_ERR_PROTOCOL、メモリ不足は
 *                  POTR_ERR_OUT_OF_MEMORY、ローカル契約不正は POTR_ERR_INVALID_ARGUMENT、
 *                  その他は POTR_ERR_UNKNOWN です。
 *  @note           成功時だけ packet のペイロードを復号先へ付け替えます。
 *                  リプレイ検査は認証成功後に呼び出し側で行います。所有権は移動しません。
 *  @par            スレッド セーフ
 *  パケットと復号先の並行更新は呼び出し側で排他します。
 */
int potr_internal_packet_open(potr_packet *packet, const uint8_t *wire_header, const uint8_t *key,
                              uint8_t *plaintext, size_t capacity);

/**
 *  @brief          相手セッションの追跡開始・更新に合わせてリプレイ窓を準備します。
 *  @param[in,out]  window 受信記録。NULL の場合は無処理で戻ります。
 *  @param[in]      packet 追跡する認証済みセッション。NULL の場合は無処理で戻ります。
 *  @note           異なるセッションの記録を初期化します。同じセッションを認証直後に採用する場合は、
 *                  記録済みの通番を保持して二重受理を防ぎます。所有権は移動しません。
 *  @par            スレッド セーフ
 *  受信状態と同じ排他を呼び出し側で行います。
 */
void potr_internal_replay_window_prepare(potr_internal_replay_window *window, const potr_packet *packet);

/**
 *  @brief          認証済み通番を検査し、新セッションでは受信記録を初期化します。
 *  @param[in,out]  window 送信元ごとの受信記録。ゼロ初期化して使用します。NULL は許可しません。
 *  @param[in]      packet 認証済みパケット。NULL は許可しません。
 *  @return         受理時は POTR_OK、重複・窓外・旧セッションは POTR_ERR_PROTOCOL、
 *                  NULL 引数は POTR_ERR_INVALID_ARGUMENT です。
 *  @note           セッションは開始時刻と識別子の辞書順で比較します。
 *                  平文には呼び出しません。所有権は移動しません。
 *  @par            スレッド セーフ
 *  UDP は単一受信スレッド、N:1 は peers_mutex、TCP は tcp_recv_mutex で排他します。
 */
int potr_internal_replay_window_accept(potr_internal_replay_window *window, const potr_packet *packet);

#ifdef __cplusplus
}
#endif
#endif /* POTR_PROTOCOL_PACKET_SEAL_H */
