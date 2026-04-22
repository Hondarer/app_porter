/**
 *******************************************************************************
 *  @file           config.c
 *  @brief          設定ファイル (INI 形式) 解析モジュール。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>
#include <com_util/crt/stdio.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <porter_const.h>
#include <porter_type.h>

#include <com_util/crypto/crypto.h>
#include "../infra/potrTrace.h"
#include "config.h"

/** 設定ファイル 1 行の最大長。 */
#define CONFIG_LINE_MAX 256

/** セクション名の最大長。 */
#define CONFIG_SECTION_MAX 64

/** キー名の最大長。 */
#define CONFIG_KEY_MAX 64

/** 値文字列の最大長。 */
#define CONFIG_VAL_MAX 128

/* 読み取り専用で設定ファイルを開く。失敗時は NULL を返す。 */
static FILE *open_config_file_read(const char *path)
{
    FILE *fp = NULL;

    if (path == NULL)
    {
        return NULL;
    }

    fp = com_util_fopen(path, "r", NULL);

    return fp;
}

/* src を dst に切り詰めコピーする。 */
static void copy_cstr_trunc(char *dst, size_t dst_size, const char *src)
{
    size_t len;

    if (dst == NULL || dst_size == 0)
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
        len = dst_size - 1;
    }

    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* 文字列の先頭・末尾の空白を除去して buf に格納する */
static void trim(const char *src, char *buf, size_t buf_size)
{
    const char *start;
    const char *end;
    size_t      len;

    if (src == NULL || buf == NULL || buf_size == 0)
    {
        return;
    }

    start = src;
    while (*start != '\0' && isspace((unsigned char)*start))
    {
        start++;
    }

    end = src + strlen(src) - 1;
    while (end >= start && isspace((unsigned char)*end))
    {
        end--;
    }

    len = (size_t)(end - start + 1);
    if (len >= buf_size)
    {
        len = buf_size - 1;
    }

    memcpy(buf, start, len);
    buf[len] = '\0';
}

/* "key = value" 行を解析して key_out, val_out に格納する。成功時は 1 を返す。 */
static int parse_kv(const char *line, char *key_out, size_t key_size,
                    char *val_out, size_t val_size)
{
    const char *eq;
    char        key_raw[CONFIG_KEY_MAX];
    size_t      key_len;

    eq = strchr(line, '=');
    if (eq == NULL)
    {
        return 0;
    }

    key_len = (size_t)(eq - line);
    if (key_len >= CONFIG_KEY_MAX)
    {
        return 0;
    }
    memcpy(key_raw, line, key_len);
    key_raw[key_len] = '\0';

    trim(key_raw, key_out, key_size);
    trim(eq + 1,  val_out, val_size);

    return 1;
}

/**
 *******************************************************************************
 *  @brief          設定ファイルから [global] セクションを読み込みます。
 *  @param[in]      config_path 設定ファイルのパス。
 *  @param[out]     global      読み込み結果を格納する構造体へのポインタ。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *
 *  @details
 *  [global] セクションが存在しない場合はデフォルト値を設定します。
 *******************************************************************************
 */
int config_load_global(const char *config_path, PotrGlobalConfig *global)
{
    FILE *fp;
    char  line[CONFIG_LINE_MAX];
    char  section[CONFIG_SECTION_MAX];
    char  key[CONFIG_KEY_MAX];
    char  val[CONFIG_VAL_MAX];
    int   in_global;

    if (config_path == NULL || global == NULL)
    {
        return POTR_ERROR;
    }

    /* デフォルト値で初期化 */
    global->window_size           = (uint16_t)POTR_DEFAULT_WINDOW_SIZE;
    global->max_payload           = (uint16_t)POTR_DEFAULT_MAX_PAYLOAD;
    global->reorder_timeout_ms    = 0U;
    global->max_message_size      = (uint32_t)POTR_MAX_MESSAGE_SIZE;
    global->send_queue_depth      = (uint32_t)POTR_SEND_QUEUE_DEPTH;
    global->udp_health_interval_ms = (uint32_t)POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS;
    global->udp_health_timeout_ms  = (uint32_t)POTR_DEFAULT_UDP_HEALTH_TIMEOUT_MS;
    global->tcp_health_interval_ms = (uint32_t)POTR_DEFAULT_TCP_HEALTH_INTERVAL_MS;
    global->tcp_health_timeout_ms  = (uint32_t)POTR_DEFAULT_TCP_HEALTH_TIMEOUT_MS;
    global->tcp_close_timeout_ms   = (uint32_t)POTR_DEFAULT_TCP_CLOSE_TIMEOUT_MS;

    fp = open_config_file_read(config_path);
    if (fp == NULL)
    {
        return POTR_ERROR;
    }

    section[0] = '\0';
    in_global  = 0;

    while (com_util_fgets(line, sizeof(line), fp) != NULL)
    {
        char trimmed[CONFIG_LINE_MAX];
        trim(line, trimmed, sizeof(trimmed));

        /* 空行・コメント行をスキップ */
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';')
        {
            continue;
        }

        /* セクションヘッダー */
        if (trimmed[0] == '[')
        {
            char *close = strchr(trimmed, ']');
            if (close != NULL)
            {
                size_t sec_len = (size_t)(close - trimmed - 1);
                if (sec_len >= CONFIG_SECTION_MAX)
                {
                    sec_len = CONFIG_SECTION_MAX - 1;
                }
                memcpy(section, trimmed + 1, sec_len);
                section[sec_len] = '\0';
                if (strcmp(section, "global") == 0)
                {
                    in_global = 1;
                }
                else
                {
                    in_global = 0;
                }
            }
            continue;
        }

        if (!in_global)
        {
            continue;
        }

        if (!parse_kv(trimmed, key, sizeof(key), val, sizeof(val)))
        {
            continue;
        }

        if (strcmp(key, "window_size") == 0)
        {
            global->window_size = (uint16_t)atoi(val);
        }
        else if (strcmp(key, "max_payload") == 0)
        {
            global->max_payload = (uint16_t)atoi(val);
        }
        else if (strcmp(key, "udp_health_interval_ms") == 0)
        {
            global->udp_health_interval_ms = (uint32_t)atoi(val);
        }
        else if (strcmp(key, "udp_health_timeout_ms") == 0)
        {
            global->udp_health_timeout_ms = (uint32_t)atoi(val);
        }
        else if (strcmp(key, "tcp_health_interval_ms") == 0)
        {
            global->tcp_health_interval_ms = (uint32_t)atoi(val);
        }
        else if (strcmp(key, "tcp_health_timeout_ms") == 0)
        {
            global->tcp_health_timeout_ms = (uint32_t)atoi(val);
        }
        else if (strcmp(key, "tcp_close_timeout_ms") == 0)
        {
            global->tcp_close_timeout_ms = (uint32_t)atoi(val);
        }
        else if (strcmp(key, "reorder_timeout_ms") == 0)
        {
            global->reorder_timeout_ms = (uint32_t)atoi(val);
        }
        else if (strcmp(key, "max_message_size") == 0)
        {
            global->max_message_size = (uint32_t)atoi(val);
        }
        else if (strcmp(key, "send_queue_depth") == 0)
        {
            global->send_queue_depth = (uint32_t)atoi(val);
        }
    }

    POTR_LOG(TRACE_LEVEL_VERBOSE,
             "config loaded: window_size=%u max_payload=%u "
             "max_message_size=%u send_queue_depth=%u "
             "udp_health=%u/%u tcp_health=%u/%u tcp_close_timeout_ms=%u reorder_timeout_ms=%u",
             (unsigned)global->window_size, (unsigned)global->max_payload,
             (unsigned)global->max_message_size, (unsigned)global->send_queue_depth,
             (unsigned)global->udp_health_interval_ms, (unsigned)global->udp_health_timeout_ms,
             (unsigned)global->tcp_health_interval_ms, (unsigned)global->tcp_health_timeout_ms,
             (unsigned)global->tcp_close_timeout_ms,
             (unsigned)global->reorder_timeout_ms);

    com_util_fclose(fp);
    return POTR_SUCCESS;
}

/* service セクションの 1 エントリ分を current に読み込む共通処理 */
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
        /* "src_addr1"〜"src_addr4" のみ有効。それ以外は無視。 */
        if (key[8] < '1' || key[8] > '4' || key[9] != '\0')
        {
            return;
        }
        idx = key[8] - '1'; /* 0〜3 */
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
        int v = atoi(val);
        if (v > 0)
        {
            current->max_peers = (uint32_t)v;
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
        int v = atoi(val);
        if (v >= 0)
        {
            current->connect_timeout_ms = (uint32_t)v;
        }
    }
    else if (strcmp(key, "encrypt_key") == 0)
    {
        size_t hex_len = strlen(val);
        int    i;
        int    is_hex  = 1;

        /* 64 文字かつ全桁 hex 文字かを検査する */
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
            /* 64 文字 hex 文字列 → 32 バイトバイナリキーに変換 */
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
            POTR_LOG(TRACE_LEVEL_INFO,
                     "config: encrypt_key loaded as hex key (service_id=%" PRId64 ")",
                     current->service_id);
        }
        else if (hex_len > 0)
        {
            /* パスフレーズ → SHA-256 で 32 バイトキーを導出する */
            if (com_util_passphrase_to_key(current->encrypt_key,
                                       (const uint8_t *)val, hex_len) == 0)
            {
                current->encrypt_enabled = 1;
                POTR_LOG(TRACE_LEVEL_INFO,
                         "config: encrypt_key treated as passphrase (SHA-256, service_id=%" PRId64 ")",
                         current->service_id);
            }
            else
            {
                memset(current->encrypt_key, 0, sizeof(current->encrypt_key));
                current->encrypt_enabled = 0;
                POTR_LOG(TRACE_LEVEL_WARNING,
                         "config: encrypt_key passphrase hashing failed (service_id=%" PRId64 ")",
                         current->service_id);
            }
        }
        else
        {
            /* 空文字列は無効 */
            memset(current->encrypt_key, 0, sizeof(current->encrypt_key));
            current->encrypt_enabled = 0;
            POTR_LOG(TRACE_LEVEL_WARNING,
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
 *  [service.\<id\>] 形式のセクション名から \<id\> 部分を取得し、service_id と照合します。\n
 *  サービスの識別子はセクション名の \<id\> であり、ポート番号とは無関係です。
 *******************************************************************************
 */
int config_load_service(const char *config_path, int64_t service_id,
                        PotrServiceDef *def)
{
    FILE *fp;
    char  line[CONFIG_LINE_MAX];
    char  key[CONFIG_KEY_MAX];
    char  val[CONFIG_VAL_MAX];
    int   in_target; /* 目的のサービスセクション内かどうか */
    int   found;

    if (config_path == NULL || def == NULL)
    {
        return POTR_ERROR;
    }

    fp = open_config_file_read(config_path);
    if (fp == NULL)
    {
        return POTR_ERROR;
    }

    in_target = 0;
    found     = 0;

    while (com_util_fgets(line, sizeof(line), fp) != NULL)
    {
        char trimmed[CONFIG_LINE_MAX];
        trim(line, trimmed, sizeof(trimmed));

        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';')
        {
            continue;
        }

        if (trimmed[0] == '[')
        {
            char   section[CONFIG_SECTION_MAX];
            char  *close;
            size_t sec_len;

            /* 目的セクションを読み込み済みなら終了 */
            if (in_target)
            {
                break;
            }

            close = strchr(trimmed, ']');
            if (close != NULL)
            {
                sec_len = (size_t)(close - trimmed - 1);
                if (sec_len >= CONFIG_SECTION_MAX)
                {
                    sec_len = CONFIG_SECTION_MAX - 1;
                }
                memcpy(section, trimmed + 1, sec_len);
                section[sec_len] = '\0';

                /* [service.<id>] の <id> が service_id と一致するか判定 */
                if (strncmp(section, "service.", 8) == 0 &&
                    strtoll(section + 8, NULL, 10) == service_id)
                {
                    memset(def, 0, sizeof(*def));
                    def->ttl                  = (uint8_t)POTR_DEFAULT_TTL;
                    def->pack_wait_ms         = (uint32_t)POTR_DEFAULT_PACK_WAIT_MS;
                    def->max_peers            = 1024U;  /* N:1 モードのデフォルト最大ピア数 */
                    def->service_id           = service_id;
                    def->health_interval_ms   = 0U;     /* 0 = グローバル値を使用 */
                    def->health_timeout_ms    = 0U;
                    def->reconnect_interval_ms = (uint32_t)POTR_DEFAULT_RECONNECT_INTERVAL_MS;
                    def->connect_timeout_ms    = (uint32_t)POTR_DEFAULT_CONNECT_TIMEOUT_MS;
                    in_target       = 1;
                    found           = 1;
                }
            }
            continue;
        }

        if (!in_target)
        {
            continue;
        }

        if (!parse_kv(trimmed, key, sizeof(key), val, sizeof(val)))
        {
            continue;
        }

        apply_service_kv(key, val, def);
    }

    com_util_fclose(fp);
    if (found)
    {
        POTR_LOG(TRACE_LEVEL_VERBOSE,
                 "service loaded: service_id=%" PRId64 " type=%d "
                 "src_addr1=%s dst_addr1=%s dst_port=%u src_port=%u",
                 def->service_id, (int)def->type,
                 def->src_addr[0], def->dst_addr[0],
                 (unsigned)def->dst_port, (unsigned)def->src_port);
        return POTR_SUCCESS;
    }
    else
    {
        return POTR_ERROR;
    }
}

/**
 *******************************************************************************
 *  @brief          設定ファイルに登録されているすべてのサービス ID を列挙します。
 *  @param[in]      config_path 設定ファイルのパス。
 *  @param[out]     ids_out     サービス ID 配列へのポインタを格納する変数。
 *                              呼び出し元が free(*ids_out) の責務を持つ。
 *  @param[out]     count_out   列挙したサービス ID 数。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *
 *  @details
 *  初期容量 POTR_MAX_SERVICES で配列を確保し、超過時は realloc で 2 倍に拡張します。
 *******************************************************************************
 */
int config_list_service_ids(const char *config_path, int64_t **ids_out, int *count_out)
{
    FILE *fp;
    char  line[CONFIG_LINE_MAX];
    int64_t *ids;
    int   capacity;
    int   count;

    if (config_path == NULL || ids_out == NULL || count_out == NULL)
    {
        return POTR_ERROR;
    }

    fp = open_config_file_read(config_path);
    if (fp == NULL)
    {
        return POTR_ERROR;
    }

    capacity = (int)POTR_MAX_SERVICES;
    count    = 0;
    ids      = (int64_t *)malloc((size_t)capacity * sizeof(int64_t));
    if (ids == NULL)
    {
        com_util_fclose(fp);
        return POTR_ERROR;
    }

    while (com_util_fgets(line, sizeof(line), fp) != NULL)
    {
        char trimmed[CONFIG_LINE_MAX];
        trim(line, trimmed, sizeof(trimmed));

        if (trimmed[0] != '[')
        {
            continue;
        }

        {
            char  *close = strchr(trimmed, ']');
            char   section[CONFIG_SECTION_MAX];
            size_t sec_len;

            if (close == NULL)
            {
                continue;
            }
            sec_len = (size_t)(close - trimmed - 1);
            if (sec_len >= CONFIG_SECTION_MAX)
            {
                sec_len = CONFIG_SECTION_MAX - 1;
            }
            memcpy(section, trimmed + 1, sec_len);
            section[sec_len] = '\0';

            if (strncmp(section, "service.", 8) != 0)
            {
                continue;
            }

            if (count >= capacity)
            {
                int  new_cap = capacity * 2;
                int64_t *new_ids = (int64_t *)realloc(ids, (size_t)new_cap * sizeof(int64_t));
                if (new_ids == NULL)
                {
                    free(ids);
                    com_util_fclose(fp);
                    return POTR_ERROR;
                }
                ids      = new_ids;
                capacity = new_cap;
            }

            ids[count++] = strtoll(section + 8, NULL, 10);
        }
    }

    com_util_fclose(fp);
    *ids_out   = ids;
    *count_out = count;
    return POTR_SUCCESS;
}
