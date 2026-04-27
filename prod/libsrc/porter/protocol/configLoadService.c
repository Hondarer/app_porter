/**
 *******************************************************************************
 *  @file           configLoadService.c
 *  @brief          service.id セクション読み込みの実装。
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
#include <stdlib.h>
#include <string.h>

#include <porter_const.h>
#include <porter_type.h>

#include <com_util/crypto/crypto.h>
#include "../infra/potrTrace.h"
#include "config.h"
#include "configParseCommon.h"
#include "configParseKvCommon.h"

/* src を dst に切り詰めコピーする。 */
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

/* service セクションの 1 エントリ分を current に読み込む共通処理。 */
static void apply_service_kv(const char *key, const char *val,
                             PotrServiceDef *current)
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
        current->dst_port = (uint16_t)atoi(val);
    }
    else if (strcmp(key, "src_port") == 0)
    {
        current->src_port = (uint16_t)atoi(val);
    }
    else if (strcmp(key, "multicast_group") == 0)
    {
        copy_cstr_trunc(current->multicast_group,
                        sizeof(current->multicast_group),
                        val);
    }
    else if (strcmp(key, "ttl") == 0)
    {
        current->ttl = (uint8_t)atoi(val);
    }
    else if (strcmp(key, "broadcast_addr") == 0)
    {
        copy_cstr_trunc(current->broadcast_addr,
                        sizeof(current->broadcast_addr),
                        val);
    }
    else if (strncmp(key, "src_addr", 8) == 0)
    {
        int idx;

        if (key[8] < '1' || key[8] > '4' || key[9] != '\0')
        {
            return;
        }

        idx = key[8] - '1';
        copy_cstr_trunc(current->src_addr[idx], POTR_MAX_ADDR_LEN, val);
    }
    else if (strncmp(key, "dst_addr", 8) == 0)
    {
        int idx;

        if (key[8] < '1' || key[8] > '4' || key[9] != '\0')
        {
            return;
        }

        idx = key[8] - '1';
        copy_cstr_trunc(current->dst_addr[idx], POTR_MAX_ADDR_LEN, val);
    }
    else if (strcmp(key, "pack_wait_ms") == 0)
    {
        current->pack_wait_ms = (uint32_t)atoi(val);
    }
    else if (strcmp(key, "max_peers") == 0)
    {
        int parsed = atoi(val);

        if (parsed > 0)
        {
            current->max_peers = (uint32_t)parsed;
        }
    }
    else if (strcmp(key, "health_interval_ms") == 0)
    {
        current->health_interval_ms = (uint32_t)atoi(val);
    }
    else if (strcmp(key, "health_timeout_ms") == 0)
    {
        current->health_timeout_ms = (uint32_t)atoi(val);
    }
    else if (strcmp(key, "reconnect_interval_ms") == 0)
    {
        current->reconnect_interval_ms = (uint32_t)atoi(val);
    }
    else if (strcmp(key, "connect_timeout_ms") == 0)
    {
        int parsed = atoi(val);

        if (parsed >= 0)
        {
            current->connect_timeout_ms = (uint32_t)parsed;
        }
    }
    else if (strcmp(key, "encrypt_key") == 0)
    {
        size_t hex_len = strlen(val);
        int    i;
        int    is_hex = 1;

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

        if (is_hex)
        {
            for (i = 0; i < (int)POTR_CRYPTO_KEY_SIZE; i++)
            {
                char          byte_str[3];
                char         *endp;
                unsigned long byte_val;

                byte_str[0] = val[i * 2];
                byte_str[1] = val[i * 2 + 1];
                byte_str[2] = '\0';

                byte_val = strtoul(byte_str, &endp, 16);
                current->encrypt_key[i] = (uint8_t)byte_val;
            }
            current->encrypt_enabled = 1;
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
                     "config: encrypt_key loaded as hex key (service_id=%" PRId64 ")",
                     current->service_id);
        }
        else if (hex_len > 0U)
        {
            if (com_util_passphrase_to_key(current->encrypt_key,
                                           (const uint8_t *)val, hex_len) == 0)
            {
                current->encrypt_enabled = 1;
                POTR_TRACE(COM_UTIL_TRACE_LEVEL_INFO,
                         "config: encrypt_key treated as passphrase (SHA-256, service_id=%" PRId64 ")",
                         current->service_id);
            }
            else
            {
                memset(current->encrypt_key, 0, sizeof(current->encrypt_key));
                current->encrypt_enabled = 0;
                POTR_TRACE(COM_UTIL_TRACE_LEVEL_WARNING,
                         "config: encrypt_key passphrase hashing failed (service_id=%" PRId64 ")",
                         current->service_id);
            }
        }
        else
        {
            memset(current->encrypt_key, 0, sizeof(current->encrypt_key));
            current->encrypt_enabled = 0;
            POTR_TRACE(COM_UTIL_TRACE_LEVEL_WARNING,
                     "config: encrypt_key is empty, ignored (service_id=%" PRId64 ")",
                     current->service_id);
        }
    }
}

/**
 *******************************************************************************
 *  @brief          設定ファイルから指定サービスの定義を読み込みます。
 *  @param[in]      config_path 設定ファイルのパス。
 *  @param[in]      service_id  読み込むサービスの ID。
 *  @param[out]     def         読み込み結果を格納する構造体へのポインタ。
 *  @return         成功時は POTR_SUCCESS、サービスが見つからない場合は POTR_ERROR を返します。
 *
 *  @details
 *  [service.id] 形式のセクション名から id 部分を取得し、service_id と照合します。
 *  サービスの識別子はセクション名の id であり、ポート番号とは無関係です。
 *******************************************************************************
 */
int config_load_service(const char *config_path, int64_t service_id,
                        PotrServiceDef *def)
{
    FILE *fp;
    char  line[CONFIG_LINE_MAX];
    char  key[CONFIG_KEY_MAX];
    char  val[CONFIG_VAL_MAX];
    int   in_target;
    int   found;

    if (config_path == NULL || def == NULL)
    {
        return POTR_ERROR;
    }

    fp = config_open_file_read(config_path);
    if (fp == NULL)
    {
        return POTR_ERROR;
    }

    in_target = 0;
    found     = 0;

    while (com_util_fgets(line, (int)sizeof(line), fp) != NULL)
    {
        char trimmed[CONFIG_LINE_MAX];
        config_trim(line, trimmed, sizeof(trimmed));

        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';')
        {
            continue;
        }

        if (trimmed[0] == '[')
        {
            char section[CONFIG_SECTION_MAX];

            if (in_target)
            {
                break;
            }

            if (config_parse_section_name(trimmed, section, sizeof(section)) &&
                strncmp(section, "service.", 8) == 0 &&
                strtoll(section + 8, NULL, 10) == service_id)
            {
                memset(def, 0, sizeof(*def));
                def->ttl                   = (uint8_t)POTR_DEFAULT_TTL;
                def->pack_wait_ms          = (uint32_t)POTR_DEFAULT_PACK_WAIT_MS;
                def->max_peers             = 1024U;
                def->service_id            = service_id;
                def->health_interval_ms    = 0U;
                def->health_timeout_ms     = 0U;
                def->reconnect_interval_ms = (uint32_t)POTR_DEFAULT_RECONNECT_INTERVAL_MS;
                def->connect_timeout_ms    = (uint32_t)POTR_DEFAULT_CONNECT_TIMEOUT_MS;
                in_target = 1;
                found     = 1;
            }
            continue;
        }

        if (!in_target)
        {
            continue;
        }

        if (!config_parse_kv(trimmed, key, sizeof(key), val, sizeof(val)))
        {
            continue;
        }

        apply_service_kv(key, val, def);
    }

    com_util_fclose(fp);
    if (!found)
    {
        return POTR_ERROR;
    }

    POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
             "service loaded: service_id=%" PRId64 " type=%d "
             "src_addr1=%s dst_addr1=%s dst_port=%u src_port=%u",
             def->service_id, (int)def->type,
             def->src_addr[0], def->dst_addr[0],
             (unsigned)def->dst_port, (unsigned)def->src_port);
    return POTR_SUCCESS;
}
