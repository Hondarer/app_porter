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
    global->flow_control_capacity = (uint32_t)POTR_DEFAULT_FLOW_CONTROL_CAPACITY;
    global->flow_control_suppress_percent = (uint32_t)POTR_DEFAULT_FLOW_CONTROL_SUPPRESS_PERCENT;
    global->flow_control_resume_percent = (uint32_t)POTR_DEFAULT_FLOW_CONTROL_RESUME_PERCENT;
    global->flow_control_request_interval_ms = (uint32_t)POTR_DEFAULT_FLOW_CONTROL_REQUEST_INTERVAL_MS;
    global->flow_control_request_lifetime_ms = (uint32_t)POTR_DEFAULT_FLOW_CONTROL_REQUEST_LIFETIME_MS;
    global->flow_control_receiver_percent = (uint32_t)POTR_DEFAULT_FLOW_CONTROL_RECEIVER_PERCENT;
    global->flow_control_receiver_count = (uint32_t)POTR_DEFAULT_FLOW_CONTROL_RECEIVER_COUNT;
    global->flow_control_monitored_receiver_percent = (uint32_t)POTR_DEFAULT_FLOW_CONTROL_MONITORED_RECEIVER_PERCENT;
    global->flow_control_monitored_receiver_count = (uint32_t)POTR_DEFAULT_FLOW_CONTROL_MONITORED_RECEIVER_COUNT;
    global->flow_control_receive_buffer_percent = (uint32_t)POTR_DEFAULT_FLOW_CONTROL_RECEIVE_BUFFER_PERCENT;
    global->udp_receive_buffer_size = (uint32_t)POTR_DEFAULT_UDP_RECEIVE_BUFFER_SIZE;
}

/*
 * global でも -1 を「無効」を表す専用の値 (POTR_FLOW_CONTROL_DISABLED) として受け付ける設定値を解釈する。
 * 既定で有効で、0 が「既定値を使用」を表す項目 (flow_control_receive_buffer_percent、udp_receive_buffer_size)
 * で使用する。それ以外は非負整数として解釈する。
 * 不正な文字列の場合は、直前の値を維持し、cplat_parse_uint32 の戻り値を返す。
 */
static int parse_disabled_or_uint32(uint32_t *value, const char *val)
{
    if (strcmp(val, "-1") == 0)
    {
        *value = POTR_FLOW_CONTROL_DISABLED;
        return CPLAT_OK;
    }

    return cplat_parse_uint32(value, val, 10);
}

/**
 *  @brief          整数として解釈できない global の設定値を WARNING のトレースへ出力します。
 *  @param[in]      key     設定キー。
 *  @param[in]      val     解釈できなかった設定値の文字列。
 *
 *  @details        設定値は既定値または直前の値のまま維持し、呼び出し元へ失敗を通知しません。
 */
static void trace_unparsed_global_value(const char *key, const char *val)
{
    POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING, "config: global %s value \"%s\" cannot be parsed, keeping current value",
               key, val);
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

    global_object = cjson_app_cJSON_GetObjectItemCaseSensitive(root, "global");
    if (global_object != NULL && !cjson_app_cJSON_IsObject(global_object))
    {
        cjson_app_cJSON_Delete(root);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    /*
     * global オブジェクトの各キーを走査し、対応する設定項目を更新する。
     * パースに失敗した項目はデフォルト値 (または直前の値) を維持し、項目名と値を WARNING のトレースへ出力する。
     */
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
            if (cplat_parse_uint16(&global->window_size, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "max_payload") == 0)
        {
            if (cplat_parse_uint16(&global->max_payload, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "udp_health_interval_ms") == 0)
        {
            if (cplat_parse_uint32(&global->udp_health_interval_ms, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "udp_health_timeout_ms") == 0)
        {
            if (cplat_parse_uint32(&global->udp_health_timeout_ms, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "tcp_health_interval_ms") == 0)
        {
            if (cplat_parse_uint32(&global->tcp_health_interval_ms, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "tcp_health_timeout_ms") == 0)
        {
            if (cplat_parse_uint32(&global->tcp_health_timeout_ms, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "tcp_close_timeout_ms") == 0)
        {
            if (cplat_parse_uint32(&global->tcp_close_timeout_ms, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "reorder_timeout_ms") == 0)
        {
            if (cplat_parse_uint32(&global->reorder_timeout_ms, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "max_message_size") == 0)
        {
            if (cplat_parse_uint32(&global->max_message_size, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "send_queue_depth") == 0)
        {
            if (cplat_parse_uint32(&global->send_queue_depth, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "flow_control_capacity") == 0)
        {
            if (cplat_parse_uint32(&global->flow_control_capacity, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "flow_control_suppress_percent") == 0)
        {
            if (cplat_parse_uint32(&global->flow_control_suppress_percent, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "flow_control_resume_percent") == 0)
        {
            if (cplat_parse_uint32(&global->flow_control_resume_percent, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "flow_control_request_interval_ms") == 0)
        {
            if (cplat_parse_uint32(&global->flow_control_request_interval_ms, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "flow_control_request_lifetime_ms") == 0)
        {
            if (cplat_parse_uint32(&global->flow_control_request_lifetime_ms, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "flow_control_receiver_percent") == 0)
        {
            if (cplat_parse_uint32(&global->flow_control_receiver_percent, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "flow_control_receiver_count") == 0)
        {
            if (cplat_parse_uint32(&global->flow_control_receiver_count, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "flow_control_monitored_receiver_percent") == 0)
        {
            if (cplat_parse_uint32(&global->flow_control_monitored_receiver_percent, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "flow_control_monitored_receiver_count") == 0)
        {
            if (cplat_parse_uint32(&global->flow_control_monitored_receiver_count, val, 10) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "flow_control_receive_buffer_percent") == 0)
        {
            if (parse_disabled_or_uint32(&global->flow_control_receive_buffer_percent, val) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
        else if (strcmp(key, "udp_receive_buffer_size") == 0)
        {
            if (parse_disabled_or_uint32(&global->udp_receive_buffer_size, val) != CPLAT_OK)
            {
                trace_unparsed_global_value(key, val);
            }
        }
    }
    cjson_app_cJSON_Delete(root);

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
