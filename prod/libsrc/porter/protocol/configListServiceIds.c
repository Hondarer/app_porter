/**
 *******************************************************************************
 *  @file           configListServiceIds.c
 *  @brief          設定内の service ID を列挙する機能を実装します。
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

#include <porter/porter_const.h>

#include <porter/protocol/config.h>
#include <porter/protocol/configParseCommon.h>

/* Doxygen コメントは、ヘッダーに記載 */

int config_list_service_ids(const char *config_path, int64_t **ids_out, int *count_out)
{
    FILE *fp;
    char line[CONFIG_LINE_MAX];
    int64_t *ids;
    int capacity;
    int count;

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
    count = 0;
    ids = (int64_t *)malloc((size_t)capacity * sizeof(int64_t));
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
            int new_capacity = capacity * 2;
            int64_t *new_ids;

            new_ids = (int64_t *)realloc(ids, (size_t)new_capacity * sizeof(int64_t));
            if (new_ids == NULL)
            {
                free(ids);
                com_util_fclose(fp);
                return POTR_ERROR;
            }
            ids = new_ids;
            capacity = new_capacity;
        }

        ids[count++] = strtoll(section + 8, NULL, 10);
    }

    com_util_fclose(fp);
    *ids_out = ids;
    *count_out = count;
    return POTR_SUCCESS;
}
