/**
 *******************************************************************************
 *  @file           configLoadGlobal.c
 *  @brief          global セクション読み込みの実装。
 *  @author         Tetsuo Honda
 *  @date           2026/04/26
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <stdlib.h>
#include <string.h>

#include <porter_const.h>
#include <porter_type.h>

#include "../infra/potrTrace.h"
#include "config.h"
#include "configParseCommon.h"
#include "configParseKvCommon.h"

/* global 既定値を構造体へ設定する。 */
static void config_set_global_defaults(PotrGlobalConfig *global)
{
    global->window_size            = (uint16_t)POTR_DEFAULT_WINDOW_SIZE;
    global->max_payload            = (uint16_t)POTR_DEFAULT_MAX_PAYLOAD;
    global->reorder_timeout_ms     = 0U;
    global->max_message_size       = (uint32_t)POTR_MAX_MESSAGE_SIZE;
    global->send_queue_depth       = (uint32_t)POTR_SEND_QUEUE_DEPTH;
    global->udp_health_interval_ms = (uint32_t)POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS;
    global->udp_health_timeout_ms  = (uint32_t)POTR_DEFAULT_UDP_HEALTH_TIMEOUT_MS;
    global->tcp_health_interval_ms = (uint32_t)POTR_DEFAULT_TCP_HEALTH_INTERVAL_MS;
    global->tcp_health_timeout_ms  = (uint32_t)POTR_DEFAULT_TCP_HEALTH_TIMEOUT_MS;
    global->tcp_close_timeout_ms   = (uint32_t)POTR_DEFAULT_TCP_CLOSE_TIMEOUT_MS;
}

/**
 *******************************************************************************
 *  @brief          設定ファイルから global セクションを読み込みます。
 *  @param[in]      config_path 設定ファイルのパス。
 *  @param[out]     global      読み込み結果を格納する構造体へのポインタ。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *
 *  @details
 *  global セクションが存在しない場合はデフォルト値を設定します。
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

    config_set_global_defaults(global);

    fp = config_open_file_read(config_path);
    if (fp == NULL)
    {
        return POTR_ERROR;
    }

    section[0] = '\0';
    in_global  = 0;

    while (com_util_fgets(line, (int)sizeof(line), fp) != NULL)
    {
        char trimmed[CONFIG_LINE_MAX];
        config_trim(line, trimmed, sizeof(trimmed));

        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';')
        {
            continue;
        }

        if (config_parse_section_name(trimmed, section, sizeof(section)))
        {
            in_global = (strcmp(section, "global") == 0) ? 1 : 0;
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

    POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
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
