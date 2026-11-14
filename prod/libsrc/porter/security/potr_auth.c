/**
 *******************************************************************************
 *  @file           potr_auth.c
 *  @brief          OpenSSL の証明書検証、メモリ入出力、鍵導出を集約します。
 *******************************************************************************
 */
#include <limits.h>
#include <string.h>
#include <cplat/crt/stdlib.h>
#include <cplat/crt/string.h>
#include <cplat/runtime/memory_lock.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <porter/porter_result.h>
#include <porter/security/potr_auth.h>

struct potr_internal_auth_credentials
{
    SSL_CTX *context;
    X509 **pinned_certs;
    char **peer_names;
    size_t pinned_cert_count;
    size_t peer_name_count;
    potr_internal_auth_transport transport;
    int has_ca;
};

struct potr_internal_auth_session
{
    SSL *connection;
    BIO *input;
    BIO *output;
    const potr_internal_auth_credentials *credentials;
    const char *failure_reason;
    size_t max_record_size;
    potr_internal_auth_state state;
    int padding;
};

/* パスフレーズの契約がないため、OpenSSL の既定の端末入力を起こさず失敗させる。 */
static int reject_password(char *buffer, int size, int writing, void *userdata)
{
    (void)buffer;
    (void)size;
    (void)writing;
    (void)userdata;
    return -1;
}

static int valid_string(const char *text)
{
    return text != NULL && text[0] != '\0';
}

static int validate_config(const potr_internal_auth_config *config)
{
    if (config == NULL || !valid_string(config->cert_file) || !valid_string(config->key_file)
        || (config->ca_file == NULL && config->pinned_cert_count == 0U)
        || (config->ca_file != NULL && (!valid_string(config->ca_file) || config->peer_name_count == 0U))
        || (config->crl_file != NULL && !valid_string(config->crl_file))
        || (config->pinned_cert_count != 0U && config->pinned_certs == NULL)
        || (config->peer_name_count != 0U && config->peer_names == NULL)
        || config->pinned_cert_count > SIZE_MAX / sizeof(X509 *)
        || config->peer_name_count > SIZE_MAX / sizeof(char *))
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < config->pinned_cert_count; ++i)
    {
        if (!valid_string(config->pinned_certs[i]))
        {
            return POTR_ERR_INVALID_ARGUMENT;
        }
    }
    for (size_t i = 0; i < config->peer_name_count; ++i)
    {
        if (!valid_string(config->peer_names[i]))
        {
            return POTR_ERR_INVALID_ARGUMENT;
        }
    }
    return POTR_OK;
}

static int copy_trust_config(potr_internal_auth_credentials *credentials, const potr_internal_auth_config *config)
{
    if (config->pinned_cert_count != 0U)
    {
        credentials->pinned_certs = (X509 **)cplat_calloc(config->pinned_cert_count, sizeof(*credentials->pinned_certs));
        if (credentials->pinned_certs == NULL)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
        for (size_t i = 0; i < config->pinned_cert_count; ++i)
        {
            BIO *file = BIO_new_file(config->pinned_certs[i], "rb");
            if (file == NULL)
            {
                return POTR_ERR_IO;
            }
            credentials->pinned_certs[i] = PEM_read_bio_X509(file, NULL, NULL, NULL);
            BIO_free(file);
            if (credentials->pinned_certs[i] == NULL)
            {
                return POTR_ERR_IO;
            }
            ++credentials->pinned_cert_count;
        }
    }
    if (config->peer_name_count != 0U)
    {
        credentials->peer_names = (char **)cplat_calloc(config->peer_name_count, sizeof(*credentials->peer_names));
        if (credentials->peer_names == NULL)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
        for (size_t i = 0; i < config->peer_name_count; ++i)
        {
            credentials->peer_names[i] = cplat_strdup(config->peer_names[i]);
            if (credentials->peer_names[i] == NULL)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
            ++credentials->peer_name_count;
        }
    }
    return POTR_OK;
}

static int load_revocations(SSL_CTX *context, const char *path)
{
    BIO *file = BIO_new_file(path, "rb");
    X509_CRL *revocations;
    int ret;
    if (file == NULL)
    {
        return POTR_ERR_IO;
    }
    size_t loaded = 0U;
    for (;;)
    {
        ERR_clear_error();
        revocations = PEM_read_bio_X509_CRL(file, NULL, NULL, NULL);
        if (revocations == NULL)
        {
            unsigned long error = ERR_peek_last_error();
            BIO_free(file);
            if (loaded == 0U || (error != 0UL &&
                (ERR_GET_LIB(error) != ERR_LIB_PEM || ERR_GET_REASON(error) != PEM_R_NO_START_LINE)))
            {
                return POTR_ERR_IO;
            }
            ERR_clear_error();
            break;
        }
        /* store は参照数を増やすため、読み込んだ CRL の所有権をここで解放する。
         * see: https://docs.openssl.org/3.5/man3/X509_STORE_add_cert/ */
        ret = X509_STORE_add_crl(SSL_CTX_get_cert_store(context), revocations);
        X509_CRL_free(revocations);
        if (ret != 1)
        {
            BIO_free(file);
            return POTR_ERR_UNKNOWN;
        }
        ++loaded;
    }
    if (X509_STORE_set_flags(SSL_CTX_get_cert_store(context), X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL) != 1)
    {
        return POTR_ERR_UNKNOWN;
    }
    return POTR_OK;
}

static int matches_peer_name(const potr_internal_auth_credentials *credentials, X509 *certificate)
{
    GENERAL_NAMES *names = (GENERAL_NAMES *)X509_get_ext_d2i(certificate, NID_subject_alt_name, NULL, NULL);
    int matched = 0;
    if (names == NULL)
    {
        return 0;
    }
    /* CN へのフォールバック、ワイルドカード、文字の正規化を避け、SAN の長さとバイト列を照合する。
     * see: https://docs.openssl.org/3.5/man3/X509V3_get_d2i/
     * see: https://docs.openssl.org/3.5/man3/ASN1_STRING_length/ */
    for (int i = 0; i < sk_GENERAL_NAME_num(names) && !matched; ++i)
    {
        const GENERAL_NAME *name = sk_GENERAL_NAME_value(names, i);
        const ASN1_IA5STRING *value = NULL;
        if (name->type == GEN_DNS)
        {
            value = name->d.dNSName;
        }
        else if (name->type == GEN_URI)
        {
            value = name->d.uniformResourceIdentifier;
        }
        if (value != NULL)
        {
            int length = ASN1_STRING_length(value);
            const unsigned char *data = ASN1_STRING_get0_data(value);
            for (size_t j = 0; j < credentials->peer_name_count; ++j)
            {
                size_t expected_length = strlen(credentials->peer_names[j]);
                if (length > 0 && (size_t)length == expected_length
                    && memcmp(data, credentials->peer_names[j], expected_length) == 0)
                {
                    matched = 1;
                    break;
                }
            }
        }
    }
    GENERAL_NAMES_free(names);
    return matched;
}

static const char *verification_reason(const int error)
{
    switch (error)
    {
    case X509_V_ERR_CERT_HAS_EXPIRED:
    case X509_V_ERR_CERT_NOT_YET_VALID:
    case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
    case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
        return "certificate expired or not yet valid";
    case X509_V_ERR_CERT_REVOKED:
        return "certificate revoked";
    case X509_V_ERR_UNABLE_TO_GET_CRL:
    case X509_V_ERR_CRL_HAS_EXPIRED:
    case X509_V_ERR_CRL_NOT_YET_VALID:
        return "revocation list unavailable or invalid";
    default:
        return "untrusted CA or invalid certificate chain";
    }
}

/* 検証全体を差し替え、直接登録を CA 検証と独立した受理条件として扱う。
 * 成功時も store のエラーを明示する。session の app data は他のモジュールには公開しない。
 * see: https://docs.openssl.org/3.5/man3/SSL_CTX_set_cert_verify_callback/
 * see: https://docs.openssl.org/3.5/man3/SSL_CTX_set_verify/ */
static int verify_certificate(X509_STORE_CTX *store, void *userdata)
{
    const potr_internal_auth_credentials *credentials = (const potr_internal_auth_credentials *)userdata;
    SSL *connection = (SSL *)X509_STORE_CTX_get_ex_data(store, SSL_get_ex_data_X509_STORE_CTX_idx());
    potr_internal_auth_session *session = (potr_internal_auth_session *)SSL_get_app_data(connection);
    X509 *certificate = X509_STORE_CTX_get0_cert(store);
    int error = X509_V_ERR_APPLICATION_VERIFICATION;
    if (certificate == NULL)
    {
        session->failure_reason = "peer certificate missing";
    }
    /* 比較関数の 0 は日時の形式エラーであり、同時刻ではない。
     * see: https://docs.openssl.org/3.5/man3/X509_cmp_time/ */
    else if (X509_cmp_current_time(X509_get0_notBefore(certificate)) != -1
        || X509_cmp_current_time(X509_get0_notAfter(certificate)) != 1)
    {
        error = X509_V_ERR_CERT_HAS_EXPIRED;
        session->failure_reason = verification_reason(error);
    }
    else
    {
        for (size_t i = 0; i < credentials->pinned_cert_count; ++i)
        {
            /* 公開鍵や subject だけでなく証明書全体を照合する。
             * see: https://docs.openssl.org/3.5/man3/X509_cmp/ */
            if (X509_cmp(certificate, credentials->pinned_certs[i]) == 0)
            {
                X509_STORE_CTX_set_error(store, X509_V_OK);
                return 1;
            }
        }
        if (credentials->has_ca)
        {
            if (X509_verify_cert(store) == 1)
            {
                if (matches_peer_name(credentials, certificate))
                {
                    X509_STORE_CTX_set_error(store, X509_V_OK);
                    return 1;
                }
                error = X509_V_ERR_HOSTNAME_MISMATCH;
                session->failure_reason = "peer name mismatch";
            }
            else
            {
                error = X509_STORE_CTX_get_error(store);
                session->failure_reason = verification_reason(error);
            }
        }
        else
        {
            session->failure_reason = "peer certificate not registered";
        }
    }
    X509_STORE_CTX_set_error(store, error);
    return 0;
}

static int configure_context(potr_internal_auth_credentials *credentials, const potr_internal_auth_config *config)
{
    SSL_CTX *context = credentials->context;
    int version = TLS1_3_VERSION;
    if (credentials->transport == POTR_INTERNAL_AUTH_TRANSPORT_DATAGRAM)
    {
        version = DTLS1_2_VERSION;
    }
    /* チケットを無効にするだけでは TLS 1.3 の stateful ticket は止まらないため枚数も 0 にする。
     * EC グループだけを許可し、TLS 1.3 の既定に含まれるハイブリッド鍵交換も除外する。
     * see: https://docs.openssl.org/3.5/man3/SSL_CTX_set_options/
     * see: https://docs.openssl.org/3.5/man3/SSL_CTX_set_num_tickets/
     * see: https://docs.openssl.org/3.5/man3/SSL_CTX_set1_curves/ */
    (void)SSL_CTX_set_options(context, SSL_OP_NO_TICKET | SSL_OP_NO_RENEGOTIATION);
    (void)SSL_CTX_set_session_cache_mode(context, SSL_SESS_CACHE_OFF);
    if (SSL_CTX_set_min_proto_version(context, version) != 1
        || SSL_CTX_set_max_proto_version(context, version) != 1
        || SSL_CTX_set_cipher_list(context, "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384") != 1
        || SSL_CTX_set_ciphersuites(context, "TLS_AES_256_GCM_SHA384") != 1
        || SSL_CTX_set1_groups_list(context, "P-256:P-384:P-521:X25519:X448") != 1
        || SSL_CTX_set_num_tickets(context, 0) != 1)
    {
        return POTR_ERR_UNKNOWN;
    }
    SSL_CTX_set_verify(context, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
    SSL_CTX_set_cert_verify_callback(context, verify_certificate, credentials);
    SSL_CTX_set_default_passwd_cb(context, reject_password);
    /* OpenSSL が秘密鍵を所有し、参照数が 0 になった際に消去して解放する。
     * porter は秘密鍵の平文コピーを保持しない。
     * see: https://docs.openssl.org/3.5/man3/SSL_CTX_use_certificate/
     * see: https://docs.openssl.org/3.5/man3/EVP_PKEY_new/
     * see: https://github.com/openssl/openssl/blob/openssl-3.5.9/crypto/ec/ec_key.c */
    if (SSL_CTX_use_certificate_chain_file(context, config->cert_file) != 1
        || SSL_CTX_use_PrivateKey_file(context, config->key_file, SSL_FILETYPE_PEM) != 1)
    {
        return POTR_ERR_IO;
    }
    if (SSL_CTX_check_private_key(context) != 1)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (config->ca_file != NULL)
    {
        if (SSL_CTX_load_verify_locations(context, config->ca_file, NULL) != 1)
        {
            return POTR_ERR_IO;
        }
        credentials->has_ca = 1;
    }
    if (config->crl_file != NULL)
    {
        return load_revocations(context, config->crl_file);
    }
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_auth_credentials_create(const potr_internal_auth_config *config,
    const potr_internal_auth_transport transport, potr_internal_auth_credentials **credentials_out)
{
    potr_internal_auth_credentials *credentials;
    int ret;
    if (credentials_out == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    *credentials_out = NULL;
    if (transport != POTR_INTERNAL_AUTH_TRANSPORT_DATAGRAM && transport != POTR_INTERNAL_AUTH_TRANSPORT_STREAM)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    ret = validate_config(config);
    if (ret != POTR_OK)
    {
        return ret;
    }
    credentials = (potr_internal_auth_credentials *)cplat_calloc(1U, sizeof(*credentials));
    if (credentials == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }
    credentials->transport = transport;
    ERR_clear_error();
    if (transport == POTR_INTERNAL_AUTH_TRANSPORT_DATAGRAM)
    {
        credentials->context = SSL_CTX_new(DTLS_method());
    }
    else
    {
        credentials->context = SSL_CTX_new(TLS_method());
    }
    if (credentials->context == NULL)
    {
        potr_internal_auth_credentials_dispose(credentials);
        return POTR_ERR_OUT_OF_MEMORY;
    }
    ret = configure_context(credentials, config);
    if (ret == POTR_OK)
    {
        ret = copy_trust_config(credentials, config);
    }
    if (ret != POTR_OK)
    {
        potr_internal_auth_credentials_dispose(credentials);
        return ret;
    }
    *credentials_out = credentials;
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_auth_credentials_dispose(potr_internal_auth_credentials *credentials)
{
    if (credentials == NULL)
    {
        return;
    }
    for (size_t i = 0; i < credentials->pinned_cert_count; ++i)
    {
        X509_free(credentials->pinned_certs[i]);
    }
    for (size_t i = 0; i < credentials->peer_name_count; ++i)
    {
        cplat_free(credentials->peer_names[i]);
    }
    cplat_free(credentials->pinned_certs);
    cplat_free(credentials->peer_names);
    SSL_CTX_free(credentials->context);
    cplat_secure_zero(credentials, sizeof(*credentials));
    cplat_free(credentials);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_auth_session_create(const potr_internal_auth_credentials *credentials,
    const potr_internal_auth_role role, const size_t max_record_size, potr_internal_auth_session **session_out)
{
    potr_internal_auth_session *session;
    const BIO_METHOD *method;
    if (session_out == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    *session_out = NULL;
    if (credentials == NULL || (role != POTR_INTERNAL_AUTH_ROLE_CLIENT && role != POTR_INTERNAL_AUTH_ROLE_SERVER)
        || max_record_size == 0U || max_record_size > (size_t)INT_MAX)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    session = (potr_internal_auth_session *)cplat_calloc(1U, sizeof(*session));
    if (session == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }
    session->credentials = credentials;
    session->max_record_size = max_record_size;
    session->state = POTR_INTERNAL_AUTH_STATE_IN_PROGRESS;
    session->failure_reason = "";
    session->connection = SSL_new(credentials->context);
    if (session->connection == NULL)
    {
        potr_internal_auth_session_dispose(session);
        return POTR_ERR_OUT_OF_MEMORY;
    }
    method = BIO_s_mem();
    if (credentials->transport == POTR_INTERNAL_AUTH_TRANSPORT_DATAGRAM)
    {
        method = BIO_s_dgram_mem();
    }
    session->input = BIO_new(method);
    session->output = BIO_new(method);
    if (session->input == NULL || session->output == NULL)
    {
        BIO_free(session->input);
        BIO_free(session->output);
        SSL_free(session->connection);
        cplat_free(session);
        return POTR_ERR_OUT_OF_MEMORY;
    }
    /* 異なる 2 つの BIO の所有権を SSL に移す。session 内のポインターは以後借用する。
     * see: https://docs.openssl.org/3.5/man3/SSL_set_bio/ */
    SSL_set_bio(session->connection, session->input, session->output);
    if (SSL_set_app_data(session->connection, session) != 1)
    {
        potr_internal_auth_session_dispose(session);
        return POTR_ERR_UNKNOWN;
    }
    if (credentials->transport == POTR_INTERNAL_AUTH_TRANSPORT_DATAGRAM)
    {
        /* SSL_set_mtu は IP/UDP ヘッダーを含まない DTLS の MTU を指定する。
         * OS の MTU 探索を禁止し、メモリ BIO が書き込みごとのデータグラム境界を保持する。
         * see: https://docs.openssl.org/3.5/man3/SSL_CTX_set_options/
         * see: https://docs.openssl.org/3.5/man3/BIO_s_mem/ */
        (void)SSL_set_options(session->connection, SSL_OP_NO_QUERY_MTU);
        if (SSL_set_mtu(session->connection, (long)max_record_size) <= 0)
        {
            potr_internal_auth_session_dispose(session);
            return POTR_ERR_INVALID_ARGUMENT;
        }
    }
    if (role == POTR_INTERNAL_AUTH_ROLE_CLIENT)
    {
        SSL_set_connect_state(session->connection);
    }
    else
    {
        SSL_set_accept_state(session->connection);
    }
    *session_out = session;
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_auth_session_dispose(potr_internal_auth_session *session)
{
    if (session == NULL)
    {
        return;
    }
    SSL_free(session->connection);
    cplat_secure_zero(session, sizeof(*session));
    cplat_free(session);
}

static void fail_session(potr_internal_auth_session *session, const char *reason)
{
    session->state = POTR_INTERNAL_AUTH_STATE_FAILED;
    if (session->failure_reason[0] == '\0')
    {
        session->failure_reason = reason;
    }
}

static void advance_handshake(potr_internal_auth_session *session, const int established_input)
{
    int ret;
    ERR_clear_error();
    if (established_input)
    {
        uint8_t unexpected_data;
        size_t read_length = 0;
        /* 確立後も DTLS の最終 flight の再送要求を処理する。アプリケーション データは受理しない。
         * see: https://docs.openssl.org/3.5/man3/SSL_read/ */
        ret = SSL_read_ex(session->connection, &unexpected_data, sizeof(unexpected_data), &read_length);
        if (ret == 1)
        {
            fail_session(session, "unexpected application data");
            return;
        }
    }
    else
    {
        ret = SSL_do_handshake(session->connection);
        if (ret == 1)
        {
            if (SSL_get0_peer_certificate(session->connection) == NULL)
            {
                fail_session(session, "peer certificate missing");
            }
            else
            {
                session->state = POTR_INTERNAL_AUTH_STATE_ESTABLISHED;
            }
            return;
        }
    }
    /* SSL_get_error は同じスレッドで、別の OpenSSL 呼び出しを挟まずに判定する。
     * see: https://docs.openssl.org/3.5/man3/SSL_get_error/ */
    int error = SSL_get_error(session->connection, ret);
    if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE)
    {
        unsigned long detail = ERR_peek_last_error();
        if (ERR_GET_LIB(detail) == ERR_LIB_SSL && ERR_GET_REASON(detail) == SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE)
        {
            fail_session(session, "peer certificate missing");
        }
        else
        {
            fail_session(session, "handshake failed or peer rejected certificate");
        }
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_auth_session_receive(potr_internal_auth_session *session, const uint8_t *in,
    const size_t in_len, potr_internal_auth_state *state_out)
{
    if (session == NULL || state_out == NULL || (in == NULL && in_len != 0U) || in_len > (size_t)INT_MAX)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    *state_out = session->state;
    if (session->state == POTR_INTERNAL_AUTH_STATE_FAILED)
    {
        return POTR_OK;
    }
    if (in_len != 0U && BIO_write(session->input, in, (int)in_len) != (int)in_len)
    {
        fail_session(session, "handshake input failed");
        *state_out = session->state;
        return POTR_ERR_IO;
    }
    if (session->state != POTR_INTERNAL_AUTH_STATE_ESTABLISHED || in_len != 0U)
    {
        advance_handshake(session, session->state == POTR_INTERNAL_AUTH_STATE_ESTABLISHED);
    }
    *state_out = session->state;
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_auth_session_take_output(potr_internal_auth_session *session, uint8_t *out,
    const size_t out_capacity, size_t *out_len_out)
{
    size_t length;
    int ret;
    if (out_len_out == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    *out_len_out = 0;
    if (session == NULL || out == NULL || out_capacity == 0U)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    length = BIO_ctrl_pending(session->output);
    if (length == 0U)
    {
        return POTR_OK;
    }
    if (session->credentials->transport == POTR_INTERNAL_AUTH_TRANSPORT_DATAGRAM)
    {
        /* datagram BIO の pending は先頭 1 データグラムの長さを返す。
         * 小さい容量で読み出すと残りが捨てられるため、先に全長を検査する。
         * see: https://docs.openssl.org/3.5/man3/BIO_s_mem/
         * see: https://docs.openssl.org/3.5/man3/BIO_s_dgram_pair/ */
        if (length > session->max_record_size)
        {
            fail_session(session, "datagram exceeds maximum record size");
            return POTR_ERR_PROTOCOL;
        }
        if (length > out_capacity)
        {
            return POTR_ERR_INVALID_ARGUMENT;
        }
    }
    else
    {
        if (length > session->max_record_size)
        {
            length = session->max_record_size;
        }
        if (length > out_capacity)
        {
            length = out_capacity;
        }
    }
    ret = BIO_read(session->output, out, (int)length);
    if (ret <= 0)
    {
        fail_session(session, "handshake output failed");
        return POTR_ERR_IO;
    }
    *out_len_out = (size_t)ret;
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_auth_session_get_timeout_ms(potr_internal_auth_session *session,
    int *has_timeout_out, uint32_t *timeout_ms_out)
{
    struct timeval timeout = {0};
    int infinite;
    uint64_t milliseconds;
    if (session == NULL || has_timeout_out == NULL || timeout_ms_out == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    *has_timeout_out = 0;
    *timeout_ms_out = 0;
    if (session->credentials->transport == POTR_INTERNAL_AUTH_TRANSPORT_STREAM
        || session->state == POTR_INTERNAL_AUTH_STATE_FAILED)
    {
        return POTR_OK;
    }
    /* timeval の定義は ssl.h が e_ostime.h 経由で提供する。実時間の既定時計を変更しない。
     * see: https://docs.openssl.org/3.5/man3/SSL_get_event_timeout/ */
    if (SSL_get_event_timeout(session->connection, &timeout, &infinite) != 1)
    {
        return POTR_ERR_UNKNOWN;
    }
    if (infinite)
    {
        return POTR_OK;
    }
    if (timeout.tv_sec < 0 || timeout.tv_usec < 0 || timeout.tv_usec >= 1000000L)
    {
        return POTR_ERR_UNKNOWN;
    }
    *has_timeout_out = 1;
    if ((uint64_t)timeout.tv_sec > UINT32_MAX / 1000U)
    {
        *timeout_ms_out = UINT32_MAX;
        return POTR_OK;
    }
    milliseconds = (uint64_t)timeout.tv_sec * 1000U + ((uint64_t)timeout.tv_usec + 999U) / 1000U;
    if (milliseconds > UINT32_MAX)
    {
        milliseconds = UINT32_MAX;
    }
    *timeout_ms_out = (uint32_t)milliseconds;
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_auth_session_handle_timeout(potr_internal_auth_session *session, potr_internal_auth_state *state_out)
{
    if (session == NULL || state_out == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    /* DTLSv1_handle_timeout は再送なしが 0、再送ありが正値、失敗が負値。
     * see: https://docs.openssl.org/3.5/man3/DTLSv1_handle_timeout/ */
    if (session->credentials->transport == POTR_INTERNAL_AUTH_TRANSPORT_DATAGRAM
        && session->state != POTR_INTERNAL_AUTH_STATE_FAILED && DTLSv1_handle_timeout(session->connection) < 0)
    {
        fail_session(session, "DTLS retransmission failed");
    }
    *state_out = session->state;
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_auth_session_export_keys(potr_internal_auth_session *session,
    const uint8_t *context, const size_t context_len, uint8_t client_to_server_key[POTR_CRYPTO_KEY_SIZE],
    uint8_t server_to_client_key[POTR_CRYPTO_KEY_SIZE])
{
    static const char label[] = "EXPORTER-porter-v2";
    uint8_t material[POTR_CRYPTO_KEY_SIZE * 2U] = {0};
    int ret;
    if (session == NULL || (context == NULL && context_len != 0U)
        || client_to_server_key == NULL || server_to_client_key == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (session->state != POTR_INTERNAL_AUTH_STATE_ESTABLISHED)
    {
        return POTR_ERR_UNSUPPORTED;
    }
    /* 空の文脈にも use_context=1 を指定し、DTLS 1.2 の「文脈なし」と区別する。
     * see: https://docs.openssl.org/3.5/man3/SSL_export_keying_material/ */
    ret = SSL_export_keying_material(session->connection, material, sizeof(material), label, sizeof(label) - 1U,
        context, context_len, 1);
    if (ret != 1)
    {
        cplat_secure_zero(material, sizeof(material));
        return POTR_ERR_UNKNOWN;
    }
    memcpy(client_to_server_key, material, POTR_CRYPTO_KEY_SIZE);
    memcpy(server_to_client_key, material + POTR_CRYPTO_KEY_SIZE, POTR_CRYPTO_KEY_SIZE);
    cplat_secure_zero(material, sizeof(material));
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

const char *potr_internal_auth_session_failure_reason(const potr_internal_auth_session *session)
{
    if (session == NULL || session->state != POTR_INTERNAL_AUTH_STATE_FAILED)
    {
        return "";
    }
    return session->failure_reason;
}
