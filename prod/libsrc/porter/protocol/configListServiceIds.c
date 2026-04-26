/**
 *******************************************************************************
 *  @file           configListServiceIds.c
 *  @brief          service ID 列挙の実装。
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

#include "config.h"
#include "configParseCommon.h"

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
    FILE    *fp;
    char     line[CONFIG_LINE_MAX];
    int64_t *ids;
    int      capacity;
    int      count;

    if (config_path == NULL || ids_out == NULL || count_out == NULL)
    {
        return POTR_ERROR;
    }

    fp = config_open_file_read(config_path);
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

    while (com_util_fgets(line, (int)sizeof(line), fp) != NULL)
    {
        char trimmed[CONFIG_LINE_MAX];
        char section[CONFIG_SECTION_MAX];

        config_trim(line, trimmed, sizeof(trimmed));
        if (!config_parse_section_name(trimmed, section, sizeof(section)))
        {
            continue;
        }

        if (strncmp(section, "service.", 8) != 0)
        {
            continue;
        }

        if (count >= capacity)
        {
            int      new_capacity = capacity * 2;
            int64_t *new_ids;

            new_ids = (int64_t *)realloc(ids, (size_t)new_capacity * sizeof(int64_t));
            if (new_ids == NULL)
            {
                free(ids);
                com_util_fclose(fp);
                return POTR_ERROR;
            }
            ids      = new_ids;
            capacity = new_capacity;
        }

        ids[count++] = strtoll(section + 8, NULL, 10);
    }

    com_util_fclose(fp);
    *ids_out   = ids;
    *count_out = count;
    return POTR_SUCCESS;
}
