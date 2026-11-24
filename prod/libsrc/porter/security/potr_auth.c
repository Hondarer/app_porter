/**
 *******************************************************************************
 *  @file           potr_auth.c
 *  @brief          OpenSSL の証明書検証、メモリ入出力、鍵導出を集約します。
 *******************************************************************************
 */
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <cplat/crt/stdio.h>
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

/** OpenSSL の失敗の理由を付けた失敗の理由を格納するバッファーの大きさ [byte]。 */
#define POTR_AUTH_FAILURE_TEXT_MAX 128U

static_assert(POTR_AUTH_FAILURE_TEXT_MAX >= 2U, "porter: POTR_AUTH_FAILURE_TEXT_MAX too small");

/*
 * X509_cmp_current_time() の戻り値の分類。日時が現在時刻より前の場合は X509_CMP_TIME_BEFORE、
 * 後の場合は X509_CMP_TIME_AFTER を返す (0 は日時の形式エラー)。
 */
#define X509_CMP_TIME_BEFORE (-1)
#define X509_CMP_TIME_AFTER  1

/* timeval から [ms] への換算に使う係数。 */
#define POTR_AUTH_USEC_PER_SEC  1000000L
#define POTR_AUTH_MSEC_PER_SEC  1000U
#define POTR_AUTH_USEC_PER_MSEC 1000U
#define POTR_AUTH_USEC_ROUND    (POTR_AUTH_USEC_PER_MSEC - 1U)

static_assert(POTR_AUTH_USEC_PER_SEC == (long)POTR_AUTH_MSEC_PER_SEC * (long)POTR_AUTH_USEC_PER_MSEC,
              "porter: time conversion factors mismatch");

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
    /** OpenSSL の失敗の理由を付けた失敗の理由。failure_reason が指す場合があります。 */
    char failure_text[POTR_AUTH_FAILURE_TEXT_MAX];
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
    if (config == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (!valid_string(config->cert_file) || !valid_string(config->key_file)
        || (config->ca_file == NULL && config->pinned_cert_count == 0U)
        || (config->ca_file != NULL && (!valid_string(config->ca_file) || config->peer_name_count == 0U))
        || (config->crl_file != NULL && !valid_string(config->crl_file))
        || (config->pinned_cert_count != 0U && config->pinned_certs == NULL)
        || (config->peer_name_count != 0U && config->peer_names == NULL)
        || config->pinned_cert_count > SIZE_MAX / sizeof(X509 *)
        || config->peer_name_count > SIZE_MAX / sizeof(char *))
    {
        return POTR_ERR_INVALID_CONFIG;
    }
    for (size_t i = 0; i < config->pinned_cert_count; ++i)
    {
        if (!valid_string(config->pinned_certs[i]))
        {
            return POTR_ERR_INVALID_CONFIG;
        }
    }
    for (size_t i = 0; i < config->peer_name_count; ++i)
    {
        if (!valid_string(config->peer_names[i]))
        {
            return POTR_ERR_INVALID_CONFIG;
        }
    }
    return POTR_OK;
}

/** 認証ファイルを読み取るチャンクの大きさ [byte]。 */
#define POTR_AUTH_FILE_CHUNK_SIZE 4096U

/** PEM ブロック検査で行頭を保持する上限の大きさ [byte]。 */
#define POTR_AUTH_PEM_LINE_CAP 128U

/** PEM ブロックのラベル名の最大長 [byte]。 */
#define POTR_AUTH_PEM_LABEL_MAX 64U

static_assert(POTR_AUTH_FILE_CHUNK_SIZE > 0U && POTR_AUTH_FILE_CHUNK_SIZE <= (size_t)INT_MAX,
              "porter: POTR_AUTH_FILE_CHUNK_SIZE must fit in int for BIO_write");

static const char *const kCertPemLabels[] = {"CERTIFICATE", "X509 CERTIFICATE"};
static const char *const kKeyPemLabels[] = {"PRIVATE KEY",     "RSA PRIVATE KEY", "EC PRIVATE KEY",
                                            "DSA PRIVATE KEY", "SM2 PRIVATE KEY", "ANY PRIVATE KEY",
                                            "ENCRYPTED PRIVATE KEY"};
static const char *const kCrlPemLabels[] = {"X509 CRL"};

typedef struct potr_pem_scanner
{
    const char *const *allowed_labels;
    size_t allowed_label_count;
    size_t line_len;
    size_t current_label_len;
    size_t block_count;
    size_t total_bytes;
    int in_block;
    int has_body_char;
    int line_has_non_ws;
    int line_truncated;
    int invalid;
    char current_label[POTR_AUTH_PEM_LABEL_MAX];
    char line_buf[POTR_AUTH_PEM_LINE_CAP];
} potr_pem_scanner;

static int is_internal_openssl_error(unsigned long error, int lib, int reason)
{
    if (ERR_SYSTEM_ERROR(error) || lib == ERR_LIB_SYS)
    {
        return 1;
    }
    return reason == ERR_R_SYS_LIB || reason == ERR_R_BIO_LIB || reason == ERR_R_BUF_LIB
           || reason == ERR_R_CRYPTO_LIB || reason == ERR_R_INTERNAL_ERROR
           || reason == ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED || reason == ERR_R_PASSED_NULL_PARAMETER
           || reason == ERR_R_PASSED_INVALID_ARGUMENT || reason == ERR_R_INIT_FAIL || reason == ERR_R_DISABLED
           || reason == ERR_R_OPERATION_FAIL || reason == ERR_R_INVALID_PROVIDER_FUNCTIONS
           || reason == ERR_R_UNABLE_TO_GET_READ_LOCK || reason == ERR_R_UNABLE_TO_GET_WRITE_LOCK
           || reason == ERR_R_FATAL;
}

static int is_config_openssl_lib(int lib)
{
    return lib == ERR_LIB_PEM || lib == ERR_LIB_ASN1 || lib == ERR_LIB_OSSL_DECODER || lib == ERR_LIB_EVP
           || lib == ERR_LIB_EC || lib == ERR_LIB_RSA || lib == ERR_LIB_DSA || lib == ERR_LIB_X509
           || lib == ERR_LIB_X509V3 || lib == ERR_LIB_PKCS7 || lib == ERR_LIB_PKCS12 || lib == ERR_LIB_SSL
           || lib == ERR_LIB_PROV || lib == ERR_LIB_CMP || lib == ERR_LIB_OBJ;
}

/*
 * OpenSSL は下位層の失敗 (例: CRYPTO_malloc の ERR_R_MALLOC_FAILURE) の後に上位層が別エラーを積むため、
 * 末尾の 1 件だけではメモリ不足を見落とす。キュー全体を走査して ERR_R_MALLOC_FAILURE を最優先にする。
 * see: https://docs.openssl.org/3.5/man3/ERR_get_error/
 */
static int classify_openssl_error_queue(void)
{
    unsigned long error;
    int has_malloc_failure = 0;
    int has_config_error = 0;

    while ((error = ERR_get_error()) != 0UL)
    {
        int lib = ERR_GET_LIB(error);
        int reason = ERR_GET_REASON(error);
        if (reason == ERR_R_MALLOC_FAILURE)
        {
            has_malloc_failure = 1;
        }
        else if (is_internal_openssl_error(error, lib, reason) == 0 && is_config_openssl_lib(lib) != 0)
        {
            has_config_error = 1;
        }
    }
    if (has_malloc_failure != 0)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }
    if (has_config_error != 0)
    {
        return POTR_ERR_INVALID_CONFIG;
    }
    return POTR_ERR_UNKNOWN;
}

static int classify_openssl_alloc_error_queue(void)
{
    unsigned long error;
    while ((error = ERR_get_error()) != 0UL)
    {
        if (ERR_GET_REASON(error) == ERR_R_MALLOC_FAILURE)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }
    return POTR_ERR_OUT_OF_MEMORY;
}

static int classify_openssl_bio_write_error_queue(void)
{
    unsigned long error;
    int has_malloc_failure = 0;
    while ((error = ERR_get_error()) != 0UL)
    {
        if (ERR_GET_REASON(error) == ERR_R_MALLOC_FAILURE)
        {
            has_malloc_failure = 1;
        }
    }
    return has_malloc_failure != 0 ? POTR_ERR_OUT_OF_MEMORY : POTR_ERR_UNKNOWN;
}

static int classify_pem_loop_eof(size_t loaded, size_t expected_blocks)
{
    unsigned long error;
    int has_malloc_failure = 0;
    int has_no_start_line = 0;
    int has_config_error = 0;
    int has_other_error = 0;

    while ((error = ERR_get_error()) != 0UL)
    {
        int lib = ERR_GET_LIB(error);
        int reason = ERR_GET_REASON(error);
        if (reason == ERR_R_MALLOC_FAILURE)
        {
            has_malloc_failure = 1;
        }
        else if (lib == ERR_LIB_PEM && reason == PEM_R_NO_START_LINE)
        {
            has_no_start_line = 1;
        }
        else if (is_internal_openssl_error(error, lib, reason) != 0)
        {
            has_other_error = 1;
        }
        else if (is_config_openssl_lib(lib) != 0)
        {
            has_config_error = 1;
        }
        else
        {
            has_other_error = 1;
        }
    }
    if (has_malloc_failure != 0)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }
    if (has_no_start_line != 0 && has_config_error == 0 && has_other_error == 0
        && loaded > 0U && loaded == expected_blocks)
    {
        return POTR_OK;
    }
    if (has_config_error != 0 || (has_no_start_line != 0 && loaded != expected_blocks))
    {
        return POTR_ERR_INVALID_CONFIG;
    }
    return POTR_ERR_UNKNOWN;
}

static void pem_scanner_init(potr_pem_scanner *scanner, const char *const *allowed_labels,
                             size_t allowed_label_count)
{
    cplat_secure_zero(scanner, sizeof(*scanner));
    scanner->allowed_labels = allowed_labels;
    scanner->allowed_label_count = allowed_label_count;
}

static int match_allowed_pem_label(const char *label, size_t label_len, const char *const *allowed_labels,
                                   size_t allowed_label_count)
{
    for (size_t i = 0U; i < allowed_label_count; ++i)
    {
        size_t expected_len = strlen(allowed_labels[i]);
        if (label_len == expected_len && memcmp(label, allowed_labels[i], expected_len) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static void pem_scanner_finish_line(potr_pem_scanner *scanner)
{
    size_t len = scanner->line_len;
    while (len > 0U
           && (scanner->line_buf[len - 1U] == '\r' || scanner->line_buf[len - 1U] == ' '
               || scanner->line_buf[len - 1U] == '\t'))
    {
        --len;
    }
    if (len >= 5U && memcmp(scanner->line_buf, "-----", 5U) == 0)
    {
        if (scanner->line_truncated != 0)
        {
            scanner->invalid = 1;
        }
        else if (len >= 16U && memcmp(scanner->line_buf, "-----BEGIN ", 11U) == 0
                 && memcmp(scanner->line_buf + len - 5U, "-----", 5U) == 0)
        {
            size_t label_len = len - 16U;
            if (scanner->in_block != 0 || label_len == 0U || label_len > POTR_AUTH_PEM_LABEL_MAX
                || match_allowed_pem_label(scanner->line_buf + 11U, label_len, scanner->allowed_labels,
                                           scanner->allowed_label_count)
                       == 0)
            {
                scanner->invalid = 1;
            }
            else
            {
                memcpy(scanner->current_label, scanner->line_buf + 11U, label_len);
                scanner->current_label_len = label_len;
                scanner->in_block = 1;
                scanner->has_body_char = 0;
            }
        }
        else if (len >= 14U && memcmp(scanner->line_buf, "-----END ", 9U) == 0
                 && memcmp(scanner->line_buf + len - 5U, "-----", 5U) == 0)
        {
            size_t label_len = len - 14U;
            if (scanner->in_block == 0 || scanner->has_body_char == 0
                || label_len != scanner->current_label_len
                || memcmp(scanner->line_buf + 9U, scanner->current_label, label_len) != 0)
            {
                scanner->invalid = 1;
            }
            else
            {
                scanner->in_block = 0;
                scanner->current_label_len = 0U;
                ++scanner->block_count;
            }
        }
        else
        {
            scanner->invalid = 1;
        }
    }
    else if (scanner->in_block != 0 && scanner->line_has_non_ws != 0)
    {
        scanner->has_body_char = 1;
    }
    scanner->line_len = 0U;
    scanner->line_has_non_ws = 0;
    scanner->line_truncated = 0;
}

static void pem_scanner_feed_chunk(potr_pem_scanner *scanner, const unsigned char *chunk, size_t length)
{
    scanner->total_bytes += length;
    for (size_t i = 0U; i < length; ++i)
    {
        unsigned char ch = chunk[i];
        if (ch == '\0')
        {
            scanner->invalid = 1;
            continue;
        }
        if (ch == '\n')
        {
            pem_scanner_finish_line(scanner);
            continue;
        }
        if (ch != ' ' && ch != '\t' && ch != '\r')
        {
            scanner->line_has_non_ws = 1;
        }
        if (scanner->line_len < sizeof(scanner->line_buf))
        {
            scanner->line_buf[scanner->line_len++] = (char)ch;
        }
        else
        {
            scanner->line_truncated = 1;
        }
    }
}

/*
 * パスを OpenSSL へ渡して開き直さず、cplat_fopen() で開いた単一ハンドルから EOF まで読み切って
 * メモリ BIO (BIO_s_secmem) へ蓄積してから解析する。これにより、オープン後のパス差し替えを防ぎ、
 * Windows での UTF-8 非 ASCII パスでも cplat と一貫した結果を得る。
 * 秘密鍵を含む全チャンクは使用後および全終了経路で容量全体を消去する。
 * see: https://docs.openssl.org/3.5/man3/BIO_s_mem/
 */
static int load_file_to_secmem_bio(const char *path, const char *const *allowed_labels,
                                   size_t allowed_label_count, size_t min_blocks, size_t max_blocks,
                                   BIO **bio_out, size_t *block_count_out)
{
    FILE *fp = NULL;
    BIO *bio = NULL;
    const BIO_METHOD *method;
    unsigned char chunk[POTR_AUTH_FILE_CHUNK_SIZE];
    potr_pem_scanner scanner;
    int ret = POTR_OK;

    *bio_out = NULL;
    if (block_count_out != NULL)
    {
        *block_count_out = 0U;
    }

    pem_scanner_init(&scanner, allowed_labels, allowed_label_count);

    fp = cplat_fopen(path, "rb", NULL);
    if (fp == NULL)
    {
        cplat_secure_zero(&scanner, sizeof(scanner));
        return POTR_ERR_IO;
    }
    if (setvbuf(fp, NULL, _IONBF, 0U) != 0)
    {
        /* 読み取り前のバッファリング設定失敗の後始末であり、すでに POTR_ERR_IO を返すため戻り値は破棄する。 */
        (void)fclose(fp);
        cplat_secure_zero(&scanner, sizeof(scanner));
        return POTR_ERR_IO;
    }

    ERR_clear_error();
    method = BIO_s_secmem();
    if (method == NULL)
    {
        /* BIO メソッド取得に失敗した後のクリーンアップであり、すでに POTR_ERR_UNKNOWN を返すため戻り値は破棄する。 */
        (void)fclose(fp);
        cplat_secure_zero(&scanner, sizeof(scanner));
        return POTR_ERR_UNKNOWN;
    }
    bio = BIO_new(method);
    if (bio == NULL)
    {
        ret = classify_openssl_alloc_error_queue();
        /* メモリ BIO 確保失敗の後始末であり、すでに POTR_ERR_OUT_OF_MEMORY を返すため戻り値は破棄する。 */
        (void)fclose(fp);
        cplat_secure_zero(&scanner, sizeof(scanner));
        return ret;
    }

    for (;;)
    {
        size_t read_bytes = fread(chunk, 1U, sizeof(chunk), fp);
        if (ferror(fp) != 0)
        {
            ret = POTR_ERR_IO;
            break;
        }
        if (read_bytes == 0U)
        {
            break;
        }
        pem_scanner_feed_chunk(&scanner, chunk, read_bytes);
        ERR_clear_error();
        int written = BIO_write(bio, chunk, (int)read_bytes);
        cplat_secure_zero(chunk, sizeof(chunk));
        if (written != (int)read_bytes)
        {
            ret = classify_openssl_bio_write_error_queue();
            break;
        }
    }

    cplat_secure_zero(chunk, sizeof(chunk));
    /* 読み取り専用で開いたファイルであり、全量の読み取り後または失敗経路での閉鎖のため戻り値は破棄する。 */
    (void)fclose(fp);

    if (ret == POTR_OK)
    {
        if (scanner.line_len > 0U || scanner.line_truncated != 0)
        {
            pem_scanner_finish_line(&scanner);
        }
        if (scanner.total_bytes == 0U || scanner.invalid != 0 || scanner.in_block != 0
            || scanner.block_count < min_blocks || scanner.block_count > max_blocks)
        {
            ret = POTR_ERR_INVALID_CONFIG;
        }
    }

    size_t block_count = scanner.block_count;
    cplat_secure_zero(&scanner, sizeof(scanner));

    if (ret == POTR_OK)
    {
        ERR_clear_error();
        if (BIO_set_mem_eof_return(bio, 0) <= 0)
        {
            ret = classify_openssl_bio_write_error_queue();
        }
    }

    if (ret != POTR_OK)
    {
        /*
         * 読み取り失敗または形式不正時に未譲渡のメモリ BIO を解放するため、戻り値は破棄する。
         * see: https://docs.openssl.org/3.5/man3/BIO_new/
         */
        (void)BIO_free(bio);
        return ret;
    }

    *bio_out = bio;
    if (block_count_out != NULL)
    {
        *block_count_out = block_count;
    }
    return POTR_OK;
}

static int load_pinned_certificate(const char *path, X509 **certificate_out)
{
    BIO *bio = NULL;
    size_t expected_blocks = 0U;
    X509 *certificate;
    int ret;

    *certificate_out = NULL;
    ret = load_file_to_secmem_bio(path, kCertPemLabels, sizeof(kCertPemLabels) / sizeof(kCertPemLabels[0]), 1U, 1U,
                                  &bio, &expected_blocks);
    if (ret != POTR_OK)
    {
        return ret;
    }
    (void)expected_blocks;

    ERR_clear_error();
    certificate = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    /*
     * 読み取り専用のメモリ BIO を読み終えた後の解放であり、閉じる際に失う書き込みがなく、
     * 証明書の読み込みの結果は変わらないため、戻り値は明示的に破棄する。
     * see: https://docs.openssl.org/3.5/man3/BIO_new/
     */
    (void)BIO_free(bio);
    if (certificate == NULL)
    {
        return classify_openssl_error_queue();
    }
    ERR_clear_error();
    *certificate_out = certificate;
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
            int ret = load_pinned_certificate(config->pinned_certs[i], &credentials->pinned_certs[i]);
            if (ret != POTR_OK)
            {
                return ret;
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

static int load_cert_chain(SSL_CTX *context, const char *path)
{
    BIO *bio = NULL;
    size_t expected_blocks = 0U;
    size_t loaded = 0U;
    int ret = load_file_to_secmem_bio(path, kCertPemLabels, sizeof(kCertPemLabels) / sizeof(kCertPemLabels[0]), 1U,
                                      SIZE_MAX, &bio, &expected_blocks);
    if (ret != POTR_OK)
    {
        return ret;
    }
    for (;;)
    {
        X509 *certificate;
        ERR_clear_error();
        certificate = PEM_read_bio_X509(bio, NULL, NULL, NULL);
        if (certificate == NULL)
        {
            ret = classify_pem_loop_eof(loaded, expected_blocks);
            /*
             * 読み取り専用のメモリ BIO を読み終えた後の解放であり、閉じる際に失う書き込みがなく、
             * 証明書の読み込みの結果は変わらないため、戻り値は明示的に破棄する。
             * see: https://docs.openssl.org/3.5/man3/BIO_new/
             */
            (void)BIO_free(bio);
            return ret;
        }
        ERR_clear_error();
        if (loaded == 0U)
        {
            /* SSL_CTX_use_certificate は参照数を増やすため、成否を問わずローカル参照を解放する。 */
            ret = SSL_CTX_use_certificate(context, certificate);
            X509_free(certificate);
        }
        else
        {
            /*
             * SSL_CTX_add_extra_chain_cert は成功時だけ certificate の所有権を受け取り、
             * 失敗時は呼び出し側が解放する。
             * see: https://docs.openssl.org/3.5/man3/SSL_CTX_add_extra_chain_cert/
             */
            ret = (int)SSL_CTX_add_extra_chain_cert(context, certificate);
            if (ret != 1)
            {
                X509_free(certificate);
            }
        }
        if (ret != 1)
        {
            /* エラー キューから返却コードを確定するため、未譲渡のメモリ BIO を解放して戻り値は破棄する。 */
            (void)BIO_free(bio);
            return classify_openssl_error_queue();
        }
        ++loaded;
    }
}

/*
 * 秘密鍵は secmem メモリ BIO へ読み込んだ上で reject_password を明示指定して復号し、
 * 対話でのパスフレーズ入力を行わない。
 * see: https://docs.openssl.org/3.5/man3/PEM_read_bio_PrivateKey/
 */
static int load_private_key(SSL_CTX *context, const char *path)
{
    BIO *bio = NULL;
    size_t expected_blocks = 0U;
    EVP_PKEY *key;
    int ret = load_file_to_secmem_bio(path, kKeyPemLabels, sizeof(kKeyPemLabels) / sizeof(kKeyPemLabels[0]), 1U, 1U,
                                      &bio, &expected_blocks);
    if (ret != POTR_OK)
    {
        return ret;
    }
    (void)expected_blocks;

    ERR_clear_error();
    key = PEM_read_bio_PrivateKey(bio, NULL, reject_password, NULL);
    /*
     * 読み取り専用のメモリ BIO を読み終えた後の解放であり、閉じる際に失う書き込みがなく、
     * 秘密鍵の読み込みの結果は変わらないため、戻り値は明示的に破棄する。
     * see: https://docs.openssl.org/3.5/man3/BIO_new/
     */
    (void)BIO_free(bio);
    if (key == NULL)
    {
        return classify_openssl_error_queue();
    }

    ERR_clear_error();
    ret = SSL_CTX_use_PrivateKey(context, key);
    /* SSL_CTX_use_PrivateKey は参照数を増やすため、ローカル参照は常にここで解放する。 */
    EVP_PKEY_free(key);
    if (ret != 1)
    {
        return classify_openssl_error_queue();
    }
    return POTR_OK;
}

static int load_ca_certificates(SSL_CTX *context, const char *path)
{
    BIO *bio = NULL;
    size_t expected_blocks = 0U;
    size_t loaded = 0U;
    X509_STORE *store;
    int ret = load_file_to_secmem_bio(path, kCertPemLabels, sizeof(kCertPemLabels) / sizeof(kCertPemLabels[0]), 1U,
                                      SIZE_MAX, &bio, &expected_blocks);
    if (ret != POTR_OK)
    {
        return ret;
    }
    store = SSL_CTX_get_cert_store(context);
    for (;;)
    {
        X509 *certificate;
        ERR_clear_error();
        certificate = PEM_read_bio_X509(bio, NULL, NULL, NULL);
        if (certificate == NULL)
        {
            ret = classify_pem_loop_eof(loaded, expected_blocks);
            /*
             * 読み取り専用のメモリ BIO を読み終えた後の解放であり、閉じる際に失う書き込みがなく、
             * 信頼証明書の読み込みの結果は変わらないため、戻り値は明示的に破棄する。
             * see: https://docs.openssl.org/3.5/man3/BIO_new/
             */
            (void)BIO_free(bio);
            return ret;
        }
        /* store は参照数を増やすため、読み込んだ証明書のローカル参照はここで解放する。
         * see: https://docs.openssl.org/3.5/man3/X509_STORE_add_cert/ */
        ERR_clear_error();
        ret = X509_STORE_add_cert(store, certificate);
        X509_free(certificate);
        if (ret != 1)
        {
            /* 失敗分類後に終了するため、読み取り専用のメモリ BIO の解放戻り値は明示的に破棄する。 */
            (void)BIO_free(bio);
            return classify_openssl_error_queue();
        }
        ++loaded;
    }
}

static int load_revocations(SSL_CTX *context, const char *path)
{
    BIO *bio = NULL;
    size_t expected_blocks = 0U;
    X509_CRL *revocations;
    X509_STORE *store;
    size_t loaded = 0U;
    int ret = load_file_to_secmem_bio(path, kCrlPemLabels, sizeof(kCrlPemLabels) / sizeof(kCrlPemLabels[0]), 1U,
                                      SIZE_MAX, &bio, &expected_blocks);
    if (ret != POTR_OK)
    {
        return ret;
    }
    store = SSL_CTX_get_cert_store(context);
    for (;;)
    {
        ERR_clear_error();
        revocations = PEM_read_bio_X509_CRL(bio, NULL, NULL, NULL);
        if (revocations == NULL)
        {
            ret = classify_pem_loop_eof(loaded, expected_blocks);
            /*
             * 読み取り専用のメモリ BIO を読み終えた後の解放であり、閉じる際に失う書き込みがなく、
             * 失効リストの読み込みの結果は変わらないため、戻り値は明示的に破棄する。
             * see: https://docs.openssl.org/3.5/man3/BIO_new/
             */
            (void)BIO_free(bio);
            if (ret != POTR_OK)
            {
                return ret;
            }
            break;
        }
        /* store は参照数を増やすため、読み込んだ CRL の所有権をここで解放する。
         * see: https://docs.openssl.org/3.5/man3/X509_STORE_add_cert/ */
        ERR_clear_error();
        ret = X509_STORE_add_crl(store, revocations);
        X509_CRL_free(revocations);
        if (ret != 1)
        {
            /* 失敗分類後に終了するため、読み取り専用のメモリ BIO を解放して戻り値は明示的に破棄する。 */
            (void)BIO_free(bio);
            return classify_openssl_error_queue();
        }
        ++loaded;
    }
    ERR_clear_error();
    if (X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL) != 1)
    {
        return classify_openssl_error_queue();
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
    else if (X509_cmp_current_time(X509_get0_notBefore(certificate)) != X509_CMP_TIME_BEFORE ||
             X509_cmp_current_time(X509_get0_notAfter(certificate)) != X509_CMP_TIME_AFTER)
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
     * SSL_CTX_set_options は設定後のオプションのビット集合、SSL_CTX_set_session_cache_mode は変更前のキャッシュ モードを返し、
     * どちらも失敗を表さないため、この 2 つの戻り値は明示的に破棄する。
     * see: https://docs.openssl.org/3.5/man3/SSL_CTX_set_options/
     * see: https://docs.openssl.org/3.5/man3/SSL_CTX_set_session_cache_mode/
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
    {
        int ret = load_cert_chain(context, config->cert_file);
        if (ret != POTR_OK)
        {
            return ret;
        }
        ret = load_private_key(context, config->key_file);
        if (ret != POTR_OK)
        {
            return ret;
        }
    }
    ERR_clear_error();
    if (SSL_CTX_check_private_key(context) != 1)
    {
        return classify_openssl_error_queue();
    }
    if (config->ca_file != NULL)
    {
        int ret = load_ca_certificates(context, config->ca_file);
        if (ret != POTR_OK)
        {
            return ret;
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
        /*
         * メモリ BIO の確保の失敗の後始末であり、すでに POTR_ERR_OUT_OF_MEMORY を返す経路のため (NULL を渡した場合も 0 を返す)、
         * この 2 つの BIO_free の戻り値は明示的に破棄する。
         * see: https://docs.openssl.org/3.5/man3/BIO_new/
         */
        (void)BIO_free(session->input);
        (void)BIO_free(session->output);
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
         * SSL_set_options の戻り値は設定後のオプションのビット集合であり、失敗を表さないため、戻り値は明示的に破棄する。
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

/*
 * 汎用の失敗の理由に、OpenSSL のエラー キューの最後のエラーの理由を付けて失敗を記録します。
 * ERR_reason_error_string は OpenSSL が持つ静的な理由の文字列を返し、未登録のコードでは NULL を返します。
 * 理由の文字列は鍵や証明書の内容を含みません。付けられない場合は汎用の理由だけを記録します。
 * see: https://docs.openssl.org/3.5/man3/ERR_error_string/
 */
static void fail_session_with_detail(potr_internal_auth_session *session, const char *reason, const unsigned long detail)
{
    const char *detail_reason;
    int ret = CPLAT_ERR_UNKNOWN;

    if (session->failure_reason[0] == '\0' && detail != 0UL)
    {
        detail_reason = ERR_reason_error_string(detail);
        if (detail_reason != NULL)
        {
            ret = cplat_snprintf(session->failure_text, sizeof(session->failure_text), "%s (OpenSSL: %s)", reason,
                detail_reason);
        }
        else
        {
            ret = cplat_snprintf(session->failure_text, sizeof(session->failure_text), "%s (OpenSSL error 0x%08lx)",
                reason, detail);
        }
    }
    if (ret == CPLAT_OK)
    {
        reason = session->failure_text;
    }
    fail_session(session, reason);
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
            fail_session_with_detail(session, "handshake failed or peer rejected certificate", detail);
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
    if (timeout.tv_sec < 0 || timeout.tv_usec < 0 || timeout.tv_usec >= POTR_AUTH_USEC_PER_SEC)
    {
        return POTR_ERR_UNKNOWN;
    }
    *has_timeout_out = 1;
    if ((uint64_t)timeout.tv_sec > UINT32_MAX / POTR_AUTH_MSEC_PER_SEC)
    {
        *timeout_ms_out = UINT32_MAX;
        return POTR_OK;
    }
    milliseconds = (uint64_t)timeout.tv_sec * POTR_AUTH_MSEC_PER_SEC +
                   ((uint64_t)timeout.tv_usec + POTR_AUTH_USEC_ROUND) / POTR_AUTH_USEC_PER_MSEC;
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
