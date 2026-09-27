/**
 *******************************************************************************
 *  @file           config_load_global.c
 *  @brief          JSONC 設定の global オブジェクトを読み込む機能を実装します。
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

#include <cplat/base/result.h>
#include <cplat/crt/stdlib.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/porter_type.h>

#include <porter/infra/potr_trace.h>
#include <porter/protocol/config.h>
#include <porter/protocol/config_parse_common.h>

static int parse_u32_field(const char *text, uint32_t *value_out)
{
    int64_t parsed;
    int ret;

    ret = cplat_parse_int64(&parsed, text, 10);
    if (ret != CPLAT_OK)
    {
        return ret;
    }
    if ((parsed < 0) || (parsed > (int64_t)UINT32_MAX))
    {
        return CPLAT_ERR_OUT_OF_RANGE;
    }
    *value_out = (uint32_t)parsed;
    return CPLAT_OK;
}

static int parse_u16_field(const char *text, uint16_t *value_out)
{
    uint32_t parsed;
    int ret;

    ret = parse_u32_field(text, &parsed);
    if (ret != CPLAT_OK)
    {
        return ret;
    }
    if (parsed > (uint32_t)UINT16_MAX)
    {
        return CPLAT_ERR_OUT_OF_RANGE;
    }
    *value_out = (uint16_t)parsed;
    return CPLAT_OK;
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
    cJSON *root;
    const cJSON *global_object;
    const cJSON *item;
    int ret;

    if (config_path == NULL || global == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    config_set_global_defaults(global);
    ret = config_read_jsonc(config_path, &root);
    if (ret != POTR_OK)
    {
        return ret;
    }
    global_object = cJSON_GetObjectItemCaseSensitive(root, "global");
    if (global_object != NULL && !cJSON_IsObject(global_object))
    {
        cJSON_Delete(root);
        return POTR_ERR_INVALID_ARGUMENT;
    }
    cJSON_ArrayForEach(item, global_object)
    {
        char value_buffer[32];
        const char *val = config_value_text(item, value_buffer, sizeof(value_buffer));
        const char *key = item->string;
        if (val == NULL)
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
    cJSON_Delete(root);

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
               "config loaded: window_size=%u max_payload=%u "
               "max_message_size=%u send_queue_depth=%u "
               "udp_health=%u/%u tcp_health=%u/%u tcp_close_timeout_ms=%u reorder_timeout_ms=%u",
               (unsigned)global->window_size, (unsigned)global->max_payload, (unsigned)global->max_message_size,
               (unsigned)global->send_queue_depth, (unsigned)global->udp_health_interval_ms,
               (unsigned)global->udp_health_timeout_ms, (unsigned)global->tcp_health_interval_ms,
               (unsigned)global->tcp_health_timeout_ms, (unsigned)global->tcp_close_timeout_ms,
               (unsigned)global->reorder_timeout_ms);

    return POTR_OK;
}
