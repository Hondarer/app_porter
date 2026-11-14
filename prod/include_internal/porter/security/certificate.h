/**
 *******************************************************************************
 *  @file           certificate.h
 *  @brief          証明書方式の通信状態とパケット保護を定義します。
 *
 *******************************************************************************
 */
#ifndef POTR_SECURITY_CERTIFICATE_H
#define POTR_SECURITY_CERTIFICATE_H
#include <porter/security/potr_auth.h>
#include <porter/protocol/packet_seal.h>
#include <cplat/net/socket.h>
#include <cplat/clock/timespec.h>
#include <cplat/net/endpoint.h>

/** ハンドシェイク専用の状態。通信の経路状態と受信ウィンドウから独立しています。 */
typedef struct potr_internal_certificate_state
{
    struct potr_internal_certificate_flight *flight; /**< 他の経路への複製に使う直近の DTLS 出力。 */
    potr_internal_auth_session *session; /**< 所有する認証セッション。 */
    potr_internal_tx_session transmit; /**< 認証前の N:1 自セッションの送出状態。ピア生成時に引き継ぎます。 */
    cplat_timespec local_timestamp; /**< 導出文脈に使う自セッションの開始時刻。 */
    uint32_t local_identifier; /**< 導出文脈に使う自セッション識別子。 */
    int identity_padding; /**< 8 バイト境界のパディング。 */
    uint8_t send_key[POTR_CRYPTO_KEY_SIZE]; /**< 自端から相手への鍵。 */
    uint8_t receive_key[POTR_CRYPTO_KEY_SIZE]; /**< 相手から自端への鍵。 */
    uint8_t peer_identity[16]; /**< 相手のセッション識別子の NBO 表現。 */
    cplat_ipv4_endpoint destination[POTR_MAX_PATH]; /**< ハンドシェイク専用の返送先。 */
    uint64_t retry_ms; /**< ハンドシェイクを開始した単調増加時刻 [ms]。 */
    int established; /**< 鍵の導出が完了したか。 */
    int peer_known; /**< 導出文脈の相手識別子を取得したか。 */
    int client; /**< 自端がクライアントか。 */
    int failed; /**< 次の周期まで再試行を待つか。 */
    int retransmitted; /**< DTLS の再送期限を処理済みか。 */
    int retransmit_padding; /**< 8 バイト境界のパディング。 */
} potr_internal_certificate_state;

#ifdef __cplusplus
extern "C" {
#endif
/**
 *  @brief          open 時に認証情報と同期資源を生成し、設定ポインターを保持しません。
 *  @param[in,out]  ctx 初期化するサービス。NULL は不可です。
 *  @param[in]      service 呼び出し中だけ借用する定義。NULL は不可です。
 *  @return         成功は POTR_OK。設定不正は POTR_ERR_INVALID_ARGUMENT、読み込み失敗は認証 API の結果。
 *  @note           スレッドの起動前に呼びます。失敗時の途中資源は dispose で解放します。
 */
int potr_internal_certificate_initialize(potr_context *ctx, const potr_service_def *service);
/**
 *  @brief          認証セッションと方向別の鍵を消去します。
 *  @param[in,out]  state 所有する状態。NULL は無処理です。
 *  @note           並行使用を停止するか、ctx の certificate_mutex を保持して呼びます。
 */
void potr_internal_certificate_state_dispose(potr_internal_certificate_state *state);
/**
 *  @brief          サービスが所有する認証資源を解放します。
 *  @param[in,out]  ctx サービス。NULL は無処理です。全スレッドの停止後に呼びます。
 */
void potr_internal_certificate_dispose(potr_context *ctx);
/**
 *  @brief          UDP の HANDSHAKE と KEY を消費します。
 *  @param[in,out]  ctx サービス。NULL は不可です。
 *  @param[in,out]  packet 解析済み受信パケット。NULL は不可です。
 *  @param[in]      wire_header 受信した NBO ヘッダー。NULL は不可です。
 *  @param[in]      sender 返送元。NULL は不可です。
 *  @param[in]      path_idx 受信経路。0 以上 ctx の経路数未満。
 *  @return         制御パケットを消費した場合は POTR_OK、通常パケットは POTR_ERR_NOT_FOUND。不正な引数は POTR_ERR_INVALID_ARGUMENT。
 *  @note           UDP 受信スレッドから呼びます。通信の経路状態と受信ウィンドウは更新しません。
 */
int potr_internal_certificate_receive(potr_context *ctx, potr_packet *packet, const uint8_t *wire_header,
    const cplat_ipv4_endpoint *sender, int path_idx);
/**
 *  @brief          UDP の開始、DTLS の再送と周期的な再試行を処理します。
 *  @param[in,out]  ctx サービス。NULL は無処理です。
 *  @param[in]      now_ms 単調増加時刻 [ms]。
 *  @note           UDP 受信スレッドから呼びます。各認証セッションの操作は内部で直列化します。
 */
void potr_internal_certificate_tick(potr_context *ctx, uint64_t now_ms);
/**
 *  @brief          パケットに対応する方向別の鍵をコピーします。
 *  @param[in,out]  ctx サービス。NULL は不可です。
 *  @param[in]      session 送信時の自セッション。受信時は NULL。
 *  @param[in]      packet 送信時は NBO、受信時はホスト形式。NULL は不可です。
 *  @param[in]      sending 送信方向なら非 0。
 *  @param[out]     key 呼び出し側が使用後に消去する鍵。NULL は不可です。
 *  @return         鍵が確立済みなら POTR_OK、未確立または相手不一致なら POTR_ERR_DISCONNECTED。引数不正は POTR_ERR_INVALID_ARGUMENT。
 *  @note           内部で certificate_mutex を取得します。ピアの参照中は呼び出し側が peers_mutex を保持します。
 */
int potr_internal_certificate_key(potr_context *ctx, const potr_internal_tx_session *session,
    const potr_packet *packet, int sending, uint8_t key[POTR_CRYPTO_KEY_SIZE]);
/**
 *  @brief          TCP の最初の経路で TLS を完了させます。後続経路は確立済みの鍵を共用します。
 *  @param[in,out]  ctx サービス。NULL は不可です。
 *  @param[in]      socket 接続済みソケット。
 *  @param[in]      first 先読みした HANDSHAKE。クライアントは NULL。
 *  @return         成功は POTR_OK、認証・形式不正は POTR_ERR_PROTOCOL、通信失敗は POTR_ERR_IO。NULL のサービスは POTR_ERR_INVALID_ARGUMENT。
 *  @note           接続を公開する前に tcp_recv_mutex の下で呼びます。
 *                  サーバーは session_establish_mutex も保持します。内部で認証操作を直列化します。
 */
int potr_internal_certificate_connect(potr_context *ctx, cplat_socket socket, const potr_packet *first);
#ifdef __cplusplus
}
#endif
#endif
