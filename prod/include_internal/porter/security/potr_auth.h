/**
 *******************************************************************************
 *  @file           potr_auth.h
 *  @brief          メモリ入出力による相互認証と方向別の鍵導出を提供します。
 *******************************************************************************
 */
#ifndef POTR_AUTH_H
#define POTR_AUTH_H

#include <stddef.h>
#include <stdint.h>
#include <porter/porter_const.h>
#include <porter/porter_result.h>

/** @brief 証明書、秘密鍵、信頼情報を所有する不透明型です。生成後は変更しません。 */
typedef struct potr_internal_auth_credentials potr_internal_auth_credentials;
/** @brief 1 回のハンドシェイクを保持する不透明型です。 */
typedef struct potr_internal_auth_session potr_internal_auth_session;

/** @brief 認証に使うトランスポートです。 */
typedef enum potr_internal_auth_transport
{
    POTR_INTERNAL_AUTH_TRANSPORT_DATAGRAM = 1, /**< DTLS 1.2 を使用します。 */
    POTR_INTERNAL_AUTH_TRANSPORT_STREAM = 2    /**< TLS 1.3 を使用します。 */
} potr_internal_auth_transport;

/** @brief ハンドシェイクの役割です。 */
typedef enum potr_internal_auth_role
{
    POTR_INTERNAL_AUTH_ROLE_CLIENT = 1, /**< 接続を開始します。 */
    POTR_INTERNAL_AUTH_ROLE_SERVER = 2  /**< 接続を受け付けます。 */
} potr_internal_auth_role;

/** @brief 相互認証の状態です。 */
typedef enum potr_internal_auth_state
{
    POTR_INTERNAL_AUTH_STATE_IN_PROGRESS = 1, /**< 相互認証を進行中です。 */
    POTR_INTERNAL_AUTH_STATE_ESTABLISHED = 2, /**< 相互認証を完了しています。 */
    POTR_INTERNAL_AUTH_STATE_FAILED = 3       /**< 相互認証に失敗しています。 */
} potr_internal_auth_state;

/** @brief 生成時に読み込む PEM ファイルと照合する名前です。文字列と配列は生成中だけ参照します。 */
typedef struct potr_internal_auth_config
{
    const char *cert_file;           /**< 自端の証明書と中間証明書チェーン。必須です。 */
    const char *key_file;            /**< 自端の暗号化されていない秘密鍵。必須です。 */
    const char *ca_file;             /**< 信頼する CA。NULL を許可します。 */
    const char *crl_file;            /**< 失効リスト。NULL を許可します。 */
    const char *const *pinned_certs; /**< 直接登録する相手の証明書ファイルの配列です。 */
    size_t pinned_cert_count;        /**< 直接登録する証明書数です。 */
    const char *const *peer_names;   /**< SAN の DNS 名または URI と完全一致で照合する名前の配列です。 */
    size_t peer_name_count;          /**< 相手の名前の数です。 */
} potr_internal_auth_config;

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/**
 *  @brief          証明書、秘密鍵、信頼情報を 1 回だけ読み込みます。
 *  @param[in]      config 設定。NULL は不可です。CA または直接登録が必要です。CA には名前も必要です。
 *  @param[in]      transport DTLS 1.2 または TLS 1.3 の選択です。
 *  @param[out]     credentials_out 所有権を返す先。NULL は不可です。失敗時は NULL を返します。
 *  @return         POTR_OK、POTR_ERR_INVALID_ARGUMENT、POTR_ERR_IO、POTR_ERR_OUT_OF_MEMORY、
 *                  または OpenSSL の初期化失敗を表す POTR_ERR_UNKNOWN です。
 *  @par            スレッド セーフ
 *  生成後の credentials は複数の session から同時に参照できます。
 */
int potr_internal_auth_credentials_create(const potr_internal_auth_config *config,
    const potr_internal_auth_transport transport, potr_internal_auth_credentials **credentials_out);

/**
 *  @brief          認証情報を解放します。
 *  @param[in]      credentials 所有する認証情報。NULL は何もしません。全 session の解放後に呼びます。
 *  @par            スレッド セーフ
 *  同じ credentials の参照中に呼び出してはなりません。
 */
void potr_internal_auth_credentials_dispose(potr_internal_auth_credentials *credentials);

/**
 *  @brief          認証情報を借用して session を生成します。
 *  @param[in]      credentials 認証情報。NULL は不可です。session の解放まで保持してください。
 *  @param[in]      role クライアントまたはサーバーです。
 *  @param[in]      max_record_size 出力上限 [byte]。1 以上、INT_MAX 以下です。DTLS では OpenSSL が受理する MTU が必要です。
 *  @param[out]     session_out 所有権を返す先。NULL は不可です。失敗時は NULL を返します。
 *  @return         POTR_OK、POTR_ERR_INVALID_ARGUMENT、POTR_ERR_OUT_OF_MEMORY、POTR_ERR_UNKNOWN です。
 *  @par            スレッド セーフ
 *  各 session は 1 スレッドから操作してください。異なる session は同時に使用できます。
 */
int potr_internal_auth_session_create(const potr_internal_auth_credentials *credentials,
    const potr_internal_auth_role role, const size_t max_record_size, potr_internal_auth_session **session_out);

/**
 *  @brief          session と所有する入出力を解放します。
 *  @param[in]      session 解放する session。NULL は何もしません。同時操作は不可です。
 */
void potr_internal_auth_session_dispose(potr_internal_auth_session *session);

/**
 *  @brief          受信バイト列を入力して相互認証を進めます。
 *  @param[in]      session 操作する session。NULL は不可です。同時操作は不可です。
 *  @param[in]      in 受信データ。in_len が 0 の場合は NULL を許可します。開始時は NULL を渡します。
 *  @param[in]      in_len 入力長 [byte]。INT_MAX 以下。DTLS では 1 データグラム分です。
 *  @param[out]     state_out 認証状態。NULL は不可です。証明書の検証失敗も FAILED で返します。
 *  @return         POTR_OK、POTR_ERR_INVALID_ARGUMENT、または入出力失敗の POTR_ERR_IO です。
 */
int potr_internal_auth_session_receive(potr_internal_auth_session *session, const uint8_t *in,
    const size_t in_len, potr_internal_auth_state *state_out);

/**
 *  @brief          送信バイト列を 1 件取り出します。
 *  @param[in]      session 操作する session。NULL は不可です。同時操作は不可です。
 *  @param[out]     out 出力先。NULL は不可です。DTLS はデータグラム全体を格納できる容量が必要です。
 *  @param[in]      out_capacity 容量 [byte]。不足時は出力を消費しません。
 *  @param[out]     out_len_out 出力長。出力がなければ 0。NULL は不可です。
 *  @return         POTR_OK、POTR_ERR_INVALID_ARGUMENT、POTR_ERR_PROTOCOL、POTR_ERR_IO です。
 */
int potr_internal_auth_session_take_output(potr_internal_auth_session *session, uint8_t *out,
    const size_t out_capacity, size_t *out_len_out);

/**
 *  @brief          DTLS の実時間による次の再送期限を取得します。
 *  @param[in]      session 操作する session。NULL は不可です。同時操作は不可です。
 *  @param[out]     has_timeout_out 期限があれば 1、なければ 0。ストリームでは常に 0 です。NULL は不可です。
 *  @param[out]     timeout_ms_out 期限までの時間 [ms]。端数は切り上げ、UINT32_MAX で飽和します。NULL は不可です。
 *  @return         POTR_OK、POTR_ERR_INVALID_ARGUMENT、POTR_ERR_UNKNOWN です。
 */
int potr_internal_auth_session_get_timeout_ms(potr_internal_auth_session *session,
    int *has_timeout_out, uint32_t *timeout_ms_out);

/**
 *  @brief          DTLS の期限到来時の再送を処理します。
 *  @param[in]      session 操作する session。NULL は不可です。同時操作は不可です。
 *  @param[out]     state_out 処理後の状態。NULL は不可です。ストリームでは状態を変更しません。
 *  @return         POTR_OK または POTR_ERR_INVALID_ARGUMENT です。再送失敗は FAILED にします。
 */
int potr_internal_auth_session_handle_timeout(potr_internal_auth_session *session, potr_internal_auth_state *state_out);

/**
 *  @brief          相互認証の完了後に EXPORTER-porter-v2 で方向別の鍵を導出します。
 *  @param[in]      session 操作する session。NULL は不可です。同時操作は不可です。
 *  @param[in]      context 導出文脈。context_len が 0 の場合は NULL を許可します。
 *                  porter ではクライアントの 16 バイトのセッション識別子の後に、サーバーの
 *                  16 バイトを連結します。各識別子は session_tv_sec (8 バイト)、session_id
 *                  (4 バイト)、session_tv_nsec (4 バイト) の順の NBO 表現で、合計 32 バイトです。
 *  @param[in]      context_len 文脈長 [byte]。両端で同じ値を渡します。
 *  @param[out]     client_to_server_key クライアントからサーバー方向の鍵。NULL は不可です。
 *  @param[out]     server_to_client_key サーバーからクライアント方向の鍵。NULL は不可です。出力先は重ねません。
 *  @return         POTR_OK、POTR_ERR_INVALID_ARGUMENT、未確立時の POTR_ERR_UNSUPPORTED、
 *                  または導出失敗の POTR_ERR_UNKNOWN です。失敗時は出力を書き換えません。
 */
int potr_internal_auth_session_export_keys(potr_internal_auth_session *session,
    const uint8_t *context, const size_t context_len, uint8_t client_to_server_key[POTR_CRYPTO_KEY_SIZE],
    uint8_t server_to_client_key[POTR_CRYPTO_KEY_SIZE]);

/**
 *  @brief          FAILED の理由を取得します。
 *  @param[in]      session 参照する session。同時操作は不可です。NULL を許可します。
 *  @return         静的文字列を借用して返します。FAILED 以外と NULL には空文字列を返します。
 *                  結果コードの対象外です。ログ用の値を返すためです。
 */
const char *potr_internal_auth_session_failure_reason(const potr_internal_auth_session *session);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* POTR_AUTH_H */
