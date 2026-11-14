/**
 *******************************************************************************
 *  @file           config_load_service.c
 *  @brief          JSONC 設定のサービス定義を読み込む機能を実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/04/26
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include <cplat/base/result.h>
#include <cplat/crt/stdlib.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/porter_type.h>

#include <cplat/crypto/crypto.h>
#include <cplat/runtime/memory_lock.h>
#include <porter/infra/potr_trace.h>
#include <porter/protocol/config.h>
#include <porter/protocol/config_parse_common.h>

/** サービスの最大ピア数の既定値。 */
#define POTR_DEFAULT_MAX_PEERS 1024U

/**
 *  @brief          src を dst に終端 NUL 付きで切り詰めコピーします。
 *  @param[out]     dst      コピー先バッファーへのポインター。
 *  @param[in]      dst_size コピー先バッファーのサイズ [byte]。
 *  @param[in]      src      コピー元文字列へのポインター。NULL の場合は空文字列を設定します。
 */
static void copy_cstr_trunc(char *dst, size_t dst_size, const char *src)
{
    size_t len;

    if (dst == NULL || dst_size == 0U)
    {
        return;
    }

    if (src == NULL)
    {
        dst[0] = '\0';
        return;
    }

    len = strlen(src);
    if (len >= dst_size)
    {
        len = dst_size - 1U;
    }

    memcpy(dst, src, len);
    dst[len] = '\0';
}

/**
 *  @brief          service セクションのキーと値 1 組をサービス定義へ反映します。
 *  @param[in]      key     設定キー。
 *  @param[in]      val     設定値の文字列。
 *  @param[in,out]  current 反映先のサービス定義構造体へのポインター。
 */
static void apply_service_kv(const char *key, const char *val, potr_service_def *current)
{
    if (strcmp(key, "type") == 0)
    {
        if (strcmp(val, "unicast_raw") == 0)
        {
            current->type = POTR_TYPE_UNICAST_RAW;
        }
        else if (strcmp(val, "multicast_raw") == 0)
        {
            current->type = POTR_TYPE_MULTICAST_RAW;
        }
        else if (strcmp(val, "broadcast_raw") == 0)
        {
            current->type = POTR_TYPE_BROADCAST_RAW;
        }
        else if (strcmp(val, "unicast") == 0)
        {
            current->type = POTR_TYPE_UNICAST;
        }
        else if (strcmp(val, "multicast") == 0)
        {
            current->type = POTR_TYPE_MULTICAST;
        }
        else if (strcmp(val, "broadcast") == 0)
        {
            current->type = POTR_TYPE_BROADCAST;
        }
        else if (strcmp(val, "unicast_bidir") == 0)
        {
            current->type = POTR_TYPE_UNICAST_BIDIR;
        }
        else if (strcmp(val, "unicast_bidir_n1") == 0)
        {
            current->type = POTR_TYPE_UNICAST_BIDIR_N1;
        }
        else if (strcmp(val, "tcp") == 0)
        {
            current->type = POTR_TYPE_TCP;
        }
        else if (strcmp(val, "tcp_bidir") == 0)
        {
            current->type = POTR_TYPE_TCP_BIDIR;
        }
    }
    else if (strcmp(key, "dst_port") == 0)
    {
        (void)cplat_parse_uint16(&current->dst_port, val, 10);
    }
    else if (strcmp(key, "src_port") == 0)
    {
        (void)cplat_parse_uint16(&current->src_port, val, 10);
    }
    else if (strcmp(key, "multicast_group") == 0)
    {
        copy_cstr_trunc(current->multicast_group, sizeof(current->multicast_group), val);
    }
    else if (strcmp(key, "ttl") == 0)
    {
        (void)cplat_parse_uint8(&current->ttl, val, 10);
    }
    else if (strcmp(key, "broadcast_addr") == 0)
    {
        copy_cstr_trunc(current->broadcast_addr, sizeof(current->broadcast_addr), val);
    }
    else if (strcmp(key, "pack_wait_ms") == 0)
    {
        (void)cplat_parse_uint32(&current->pack_wait_ms, val, 10);
    }
    else if (strcmp(key, "max_peers") == 0)
    {
        uint32_t parsed;

        if ((cplat_parse_uint32(&parsed, val, 10) == CPLAT_OK) && (parsed > 0U))
        {
            current->max_peers = parsed;
        }
    }
    else if (strcmp(key, "health_interval_ms") == 0)
    {
        (void)cplat_parse_uint32(&current->health_interval_ms, val, 10);
    }
    else if (strcmp(key, "health_timeout_ms") == 0)
    {
        (void)cplat_parse_uint32(&current->health_timeout_ms, val, 10);
    }
    else if (strcmp(key, "reconnect_interval_ms") == 0)
    {
        (void)cplat_parse_uint32(&current->reconnect_interval_ms, val, 10);
    }
    else if (strcmp(key, "connect_timeout_ms") == 0)
    {
        uint32_t parsed;

        if (cplat_parse_uint32(&parsed, val, 10) == CPLAT_OK)
        {
            current->connect_timeout_ms = parsed;
        }
    }
    else if (strcmp(key, "encrypt_key") == 0)
    {
        size_t hex_len = strlen(val);
        int i;
        int is_hex = 1;

        /* 16 進数表記の鍵文字列であるか判定する。 */
        if (hex_len == POTR_CRYPTO_KEY_SIZE * 2U)
        {
            for (i = 0; i < (int)(POTR_CRYPTO_KEY_SIZE * 2U); i++)
            {
                if (!isxdigit((unsigned char)val[i]))
                {
                    is_hex = 0;
                    break;
                }
            }
        }
        else
        {
            is_hex = 0;
        }

        /*
         * 16 進数表記の場合はバイナリ鍵へ直接変換し、それ以外の非空文字列は
         * パスフレーズとして SHA-256 でハッシュ化して鍵を導出する。
         */
        if (is_hex)
        {
            for (i = 0; i < (int)POTR_CRYPTO_KEY_SIZE; i++)
            {
                char byte_str[3];
                int64_t byte_val;

                byte_str[0] = val[i * 2];
                byte_str[1] = val[i * 2 + 1];
                byte_str[2] = '\0';

                if (cplat_parse_int64(&byte_val, byte_str, 16) == CPLAT_OK)
                {
                    current->encrypt_key[i] = (uint8_t)byte_val;
                }
            }

            current->encrypt_enabled = 1;
            POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "config: encrypt_key loaded as hex key (service_id=%" PRId64 ")",
                       current->service_id);
        }
        else if (hex_len > 0U)
        {
            if (cplat_passphrase_to_key(current->encrypt_key, (const uint8_t *)val, hex_len) == CPLAT_OK)
            {
                current->encrypt_enabled = 1;
                POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                           "config: encrypt_key treated as passphrase (SHA-256, service_id=%" PRId64 ")",
                           current->service_id);
            }
            else
            {
                cplat_secure_zero(current->encrypt_key, sizeof(current->encrypt_key));
                current->encrypt_enabled = 0;
                POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                           "config: encrypt_key passphrase hashing failed (service_id=%" PRId64 ")",
                           current->service_id);
            }
        }
        else
        {
            cplat_secure_zero(current->encrypt_key, sizeof(current->encrypt_key));
            current->encrypt_enabled = 0;
            POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING, "config: encrypt_key is empty, ignored (service_id=%" PRId64 ")",
                       current->service_id);
        }
    }
}

/**
 *  @brief          経路キーの番号を検査します。
 *  @param[in]      key       設定キー。
 *  @param[out]     path_idx  経路の添字。
 *  @param[out]     prefix_len キーの接頭辞の長さ。
 *  @return         経路キーは POTR_OK、その他のキーは POTR_ERR_NOT_FOUND、番号が範囲外なら POTR_ERR_INVALID_ARGUMENT。
 */
static int parse_path_key(const char *key, size_t *path_idx, size_t *prefix_len)
{
    const char *number;
    const char *cursor;
    uint32_t parsed;

    if (strncmp(key, "src_addr", 8) == 0 || strncmp(key, "dst_addr", 8) == 0)
    {
        *prefix_len = 8U;
    }
    else if (strncmp(key, "path_group", 10) == 0)
    {
        *prefix_len = 10U;
    }
    else
    {
        return POTR_ERR_NOT_FOUND;
    }

    number = key + *prefix_len;
    if (*number == '\0')
    {
        return POTR_ERR_NOT_FOUND;
    }
    for (cursor = number; *cursor != '\0'; cursor++)
    {
        if (!isdigit((unsigned char)*cursor))
        {
            return POTR_ERR_NOT_FOUND;
        }
    }
    if (cplat_parse_uint32(&parsed, number, 10) != CPLAT_OK || parsed < 1U || parsed > POTR_MAX_PATH)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (number[0] == '0')
    {
        return POTR_ERR_NOT_FOUND;
    }
    *path_idx = (size_t)(parsed - 1U);
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_config_service_dispose(potr_internal_config_service *loaded)
{
    if (loaded == NULL)
    {
        return;
    }
    cjson_app_cJSON_Delete(loaded->document);
    cplat_free(loaded->pinned_certs);
    cplat_free(loaded->peer_names);
    cplat_secure_zero(loaded, sizeof(*loaded));
}

static int borrow_auth_string(const cJSON *item, const char **out)
{
    if (!cjson_app_cJSON_IsString(item) || item->valuestring == NULL || item->valuestring[0] == '\0')
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    *out = item->valuestring;
    return POTR_OK;
}

static int borrow_auth_array(const cJSON *item, const char ***out, size_t *count)
{
    cJSON *element;
    size_t used = 0U;
    int length;

    if (!cjson_app_cJSON_IsArray(item) || *out != NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    length = cjson_app_cJSON_GetArraySize(item);
    if (length == 0)
    {
        return POTR_OK;
    }
    *out = (const char **)cplat_calloc((size_t)length, sizeof(**out));
    if (*out == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }
    cJSON_ArrayForEach(element, item)
    {
        int ret = borrow_auth_string(element, &(*out)[used]);
        if (ret != POTR_OK)
        {
            return ret;
        }
        ++used;
    }
    *count = used;
    return POTR_OK;
}

static int apply_auth_item(const cJSON *item, potr_internal_config_service *loaded)
{
    const char *key = item->string;
    potr_service_def *def = &loaded->definition;

    if (strcmp(key, "auth") == 0)
    {
        if (!cjson_app_cJSON_IsString(item) || item->valuestring == NULL)
        {
            return POTR_ERR_INVALID_ARGUMENT;
        }
        if (strcmp(item->valuestring, "psk") == 0)
        {
            def->auth = POTR_AUTH_PSK;
        }
        else if (strcmp(item->valuestring, "cert") == 0)
        {
            def->auth = POTR_AUTH_CERT;
        }
        else
        {
            return POTR_ERR_INVALID_ARGUMENT;
        }
        return POTR_OK;
    }
    if (strcmp(key, "cert_file") == 0)
    {
        return borrow_auth_string(item, &def->cert_file);
    }
    if (strcmp(key, "key_file") == 0)
    {
        return borrow_auth_string(item, &def->key_file);
    }
    if (strcmp(key, "ca_file") == 0)
    {
        return borrow_auth_string(item, &def->ca_file);
    }
    if (strcmp(key, "crl_file") == 0)
    {
        return borrow_auth_string(item, &def->crl_file);
    }
    if (strcmp(key, "pinned_certs") == 0)
    {
        int ret = borrow_auth_array(item, &loaded->pinned_certs, &def->pinned_cert_count);
        def->pinned_certs = loaded->pinned_certs;
        return ret;
    }
    if (strcmp(key, "peer_names") == 0)
    {
        int ret = borrow_auth_array(item, &loaded->peer_names, &def->peer_name_count);
        def->peer_names = loaded->peer_names;
        return ret;
    }
    return POTR_ERR_NOT_FOUND;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_config_load_service(const char *config_path, int64_t service_id, potr_internal_config_service *loaded)
{
    cJSON *root;
    potr_service_def *def;
    cJSON *services;
    cJSON *service;
    cJSON *item;
    char id_text[32];
    int ret;
    int path_group_specified = 0;

    if (config_path == NULL || loaded == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    memset(loaded, 0, sizeof(*loaded));
    def = &loaded->definition;
    ret = config_read_jsonc(config_path, &root);
    if (ret != POTR_OK)
    {
        return ret;
    }

    loaded->document = root;
    services = cjson_app_cJSON_GetObjectItemCaseSensitive(root, "services");
    if (services != NULL && !cjson_app_cJSON_IsObject(services))
    {
        potr_internal_config_service_dispose(loaded);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    (void)snprintf(id_text, sizeof(id_text), "%" PRId64, service_id);
    service = cjson_app_cJSON_GetObjectItemCaseSensitive(services, id_text);
    if (service == NULL)
    {
        potr_internal_config_service_dispose(loaded);
        return POTR_ERR_NOT_FOUND;
    }

    if (!cjson_app_cJSON_IsObject(service))
    {
        potr_internal_config_service_dispose(loaded);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    memset(def, 0, sizeof(*def));
    memset(def->path_group, 1, sizeof(def->path_group));
    def->ttl = (uint8_t)POTR_DEFAULT_TTL;
    def->pack_wait_ms = (uint32_t)POTR_DEFAULT_PACK_WAIT_MS;
    def->max_peers = POTR_DEFAULT_MAX_PEERS;
    def->service_id = service_id;
    def->reconnect_interval_ms = (uint32_t)POTR_DEFAULT_RECONNECT_INTERVAL_MS;
    def->connect_timeout_ms = (uint32_t)POTR_DEFAULT_CONNECT_TIMEOUT_MS;

    /* サービス定義オブジェクトの各キーを走査し、設定項目へ反映する。 */
    cJSON_ArrayForEach(item, service)
    {
        int auth_ret = apply_auth_item(item, loaded);
        if (auth_ret != POTR_ERR_NOT_FOUND)
        {
            if (auth_ret != POTR_OK)
            {
                potr_internal_config_service_dispose(loaded);
                return auth_ret;
            }
            continue;
        }
        size_t path_idx = 0U;
        size_t prefix_len = 0U;
        int path_ret = parse_path_key(item->string, &path_idx, &prefix_len);
        char value_buffer[32];
        const char *val = config_value_text(item, value_buffer, sizeof(value_buffer));

        if (path_ret == POTR_ERR_INVALID_ARGUMENT)
        {
            potr_internal_config_service_dispose(loaded);
            return POTR_ERR_INVALID_ARGUMENT;
        }
        if (path_ret == POTR_OK)
        {
            if (val == NULL && prefix_len != 10U)
            {
                continue;
            }
            if (prefix_len == 10U)
            {
                uint8_t group;

                if (val == NULL || cplat_parse_uint8(&group, val, 10) != CPLAT_OK ||
                    group < 1U || group > POTR_MAX_PATH_GROUP)
                {
                    potr_internal_config_service_dispose(loaded);
                    return POTR_ERR_INVALID_ARGUMENT;
                }
                def->path_group[path_idx] = group;
                path_group_specified = 1;
            }
            else if (item->string[0] == 's')
            {
                copy_cstr_trunc(def->src_addr[path_idx], POTR_MAX_ADDR_LEN, val);
            }
            else
            {
                copy_cstr_trunc(def->dst_addr[path_idx], POTR_MAX_ADDR_LEN, val);
            }
            continue;
        }
        if (strcmp(item->string, "group_failback_ms") == 0)
        {
            if (val == NULL || cplat_parse_uint32(&def->group_failback_ms, val, 10) != CPLAT_OK)
            {
                potr_internal_config_service_dispose(loaded);
                return POTR_ERR_INVALID_ARGUMENT;
            }
            continue;
        }
        if (val != NULL)
        {
            apply_service_kv(item->string, val, def);

            /* メモリ上に平文の鍵文字列が残らないようゼロクリアする。 */
            if (strcmp(item->string, "encrypt_key") == 0 && item->valuestring != NULL)
            {
                cplat_secure_zero(item->valuestring, strlen(item->valuestring));
            }
        }
    }

    if (path_group_specified &&
        (def->type == POTR_TYPE_UNICAST_RAW || def->type == POTR_TYPE_MULTICAST_RAW ||
         def->type == POTR_TYPE_BROADCAST_RAW))
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                   "config: path_group is ignored in RAW mode (service_id=%" PRId64 ")", def->service_id);
        memset(def->path_group, 0, sizeof(def->path_group));
    }

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
               "service loaded: service_id=%" PRId64 " type=%d "
               "src_addr1=%s dst_addr1=%s dst_port=%u src_port=%u",
               def->service_id, (int)def->type, def->src_addr[0], def->dst_addr[0], (unsigned)def->dst_port,
               (unsigned)def->src_port);

    return POTR_OK;
}
