/**
 *******************************************************************************
 *  @file           potr_internal_config_load_global.c
 *  @brief          設定の global セクションを読み込む機能を実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/04/26
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <stdint.h>
#include <string.h>

#include <com_util/base/result.h>
#include <com_util/crt/stdlib.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/porter_type.h>

#include <porter/infra/potr_trace.h>
#include <porter/protocol/config.h>
#include <porter/protocol/config_parse_common.h>
#include <porter/protocol/config_parse_kv_common.h>

static int parse_u32_field(const char *text, uint32_t *value_out)
{
    int64_t parsed;
    int ret;

    ret = com_util_parse_int64(&parsed, text, 10);
    if (ret != COM_UTIL_OK)
    {
        return ret;
    }
    if ((parsed < 0) || (parsed > (int64_t)UINT32_MAX))
    {
        return COM_UTIL_ERR_OUT_OF_RANGE;
    }
    *value_out = (uint32_t)parsed;
    return COM_UTIL_OK;
}

static int parse_u16_field(const char *text, uint16_t *value_out)
{
    uint32_t parsed;
    int ret;

    ret = parse_u32_field(text, &parsed);
    if (ret != COM_UTIL_OK)
    {
        return ret;
    }
    if (parsed > (uint32_t)UINT16_MAX)
    {
        return COM_UTIL_ERR_OUT_OF_RANGE;
    }
    *value_out = (uint16_t)parsed;
    return COM_UTIL_OK;
}

/**
 *  @brief          global 設定構造体へデフォルト値を設定します。
 *  @param[out]     global  デフォルト値を設定する構造体へのポインター。
 */
static void config_set_global_defaults(potr_global_config *global)
{
    global->window_size = (uint16_t)POTR_DEFAULT_WINDOW_SIZE;
    global->max_payload = (uint16_t)POTR_DEFAULT_MAX_PAYLOAD;
    global->reorder_timeout_ms = 0U;
    global->max_message_size = (uint32_t)POTR_MAX_MESSAGE_SIZE;
    global->send_queue_depth = (uint32_t)POTR_SEND_QUEUE_DEPTH;
    global->udp_health_interval_ms = (uint32_t)POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS;
    global->udp_health_timeout_ms = (uint32_t)POTR_DEFAULT_UDP_HEALTH_TIMEOUT_MS;
    global->tcp_health_interval_ms = (uint32_t)POTR_DEFAULT_TCP_HEALTH_INTERVAL_MS;
    global->tcp_health_timeout_ms = (uint32_t)POTR_DEFAULT_TCP_HEALTH_TIMEOUT_MS;
    global->tcp_close_timeout_ms = (uint32_t)POTR_DEFAULT_TCP_CLOSE_TIMEOUT_MS;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_config_load_global(const char *config_path, potr_global_config *global)
{
    FILE *fp;
    char line[CONFIG_LINE_MAX];
    char section[CONFIG_SECTION_MAX];
    char key[CONFIG_KEY_MAX];
    char val[CONFIG_VAL_MAX];
    int in_global;

    if (config_path == NULL || global == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    config_set_global_defaults(global);

    fp = config_open_file_read(config_path);
    if (fp == NULL)
    {
        return POTR_ERR_IO;
    }

    section[0] = '\0';
    in_global = 0;

    while (fgets(line, (int)sizeof(line), fp) != NULL)
    {
        char trimmed[CONFIG_LINE_MAX];
        config_trim(line, trimmed, sizeof(trimmed));

        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';')
        {
            continue;
        }

        if (config_parse_section_name(trimmed, section, sizeof(section)))
        {
            if (strcmp(section, "global") == 0)
            {
                in_global = 1;
            }
            else
            {
                in_global = 0;
            }
            continue;
        }

        if (!in_global)
        {
            continue;
        }

        if (!config_parse_kv(trimmed, key, sizeof(key), val, sizeof(val)))
        {
            continue;
        }

        if (strcmp(key, "window_size") == 0)
        {
            (void)parse_u16_field(val, &global->window_size);
        }
        else if (strcmp(key, "max_payload") == 0)
        {
            (void)parse_u16_field(val, &global->max_payload);
        }
        else if (strcmp(key, "udp_health_interval_ms") == 0)
        {
            (void)parse_u32_field(val, &global->udp_health_interval_ms);
        }
        else if (strcmp(key, "udp_health_timeout_ms") == 0)
        {
            (void)parse_u32_field(val, &global->udp_health_timeout_ms);
        }
        else if (strcmp(key, "tcp_health_interval_ms") == 0)
        {
            (void)parse_u32_field(val, &global->tcp_health_interval_ms);
        }
        else if (strcmp(key, "tcp_health_timeout_ms") == 0)
        {
            (void)parse_u32_field(val, &global->tcp_health_timeout_ms);
        }
        else if (strcmp(key, "tcp_close_timeout_ms") == 0)
        {
            (void)parse_u32_field(val, &global->tcp_close_timeout_ms);
        }
        else if (strcmp(key, "reorder_timeout_ms") == 0)
        {
            (void)parse_u32_field(val, &global->reorder_timeout_ms);
        }
        else if (strcmp(key, "max_message_size") == 0)
        {
            (void)parse_u32_field(val, &global->max_message_size);
        }
        else if (strcmp(key, "send_queue_depth") == 0)
        {
            (void)parse_u32_field(val, &global->send_queue_depth);
        }
    }

    POTR_TRACE(COM_UTIL_TRACE_LEVEL_VERBOSE,
               "config loaded: window_size=%u max_payload=%u "
               "max_message_size=%u send_queue_depth=%u "
               "udp_health=%u/%u tcp_health=%u/%u tcp_close_timeout_ms=%u reorder_timeout_ms=%u",
               (unsigned)global->window_size, (unsigned)global->max_payload, (unsigned)global->max_message_size,
               (unsigned)global->send_queue_depth, (unsigned)global->udp_health_interval_ms,
               (unsigned)global->udp_health_timeout_ms, (unsigned)global->tcp_health_interval_ms,
               (unsigned)global->tcp_health_timeout_ms, (unsigned)global->tcp_close_timeout_ms,
               (unsigned)global->reorder_timeout_ms);

    fclose(fp);
    return POTR_OK;
}
