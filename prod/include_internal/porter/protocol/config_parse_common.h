/**
 *******************************************************************************
 *  @file           config_parse_common.h
 *  @brief          設定項目を解析する共通内部 API を提供します。
 *
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/* NOTE: このヘッダーは多数のソース ファイルから参照されるため、            */
/*       @hideincludedbygraph によって "Included by" グラフを無効にします。 */

#ifndef PORTER_PROTOCOL_CONFIG_PARSE_COMMON_H
#define PORTER_PROTOCOL_CONFIG_PARSE_COMMON_H

#include <cJSON.h>
#include <cJSON_JSONC.h>
#include <cJSON_Integer.h>
#include <cplat/crt/stdio.h>
#include <cplat/crt/stdlib.h>
#include <cplat/runtime/memory_lock.h>
#include <porter/porter_result.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

/* JSONC ファイルを読み込む。呼び出し元が cJSON_Delete で解放する。 */
static int config_read_jsonc(const char *path, cJSON **root_out)
{
    FILE *fp;
    char chunk[1024];
    char *text;
    size_t length;
    size_t capacity;
    cJSON *root;

    fp = cplat_fopen(path, "r", NULL);
    if (fp == NULL)
    {
        return POTR_ERR_IO;
    }
    capacity = sizeof(chunk);
    text = (char *)cplat_malloc(capacity);
    if (text == NULL)
    {
        fclose(fp);
        return POTR_ERR_OUT_OF_MEMORY;
    }
    length = 0U;
    while (fgets(chunk, (int)sizeof(chunk), fp) != NULL)
    {
        size_t size = strlen(chunk);
        if (size > SIZE_MAX - length - 1U)
        {
            cplat_secure_zero(text, length);
            cplat_free(text);
            fclose(fp);
            return POTR_ERR_OUT_OF_MEMORY;
        }
        if (length + size + 1U > capacity)
        {
            size_t new_capacity = capacity;
            char *expanded;
            while (new_capacity < length + size + 1U)
            {
                if (new_capacity > SIZE_MAX / 2U)
                {
                    cplat_secure_zero(text, length);
                    cplat_free(text);
                    fclose(fp);
                    return POTR_ERR_OUT_OF_MEMORY;
                }
                new_capacity *= 2U;
            }
            expanded = (char *)cplat_realloc(text, new_capacity, 1U);
            if (expanded == NULL)
            {
                cplat_secure_zero(text, length);
                cplat_free(text);
                fclose(fp);
                return POTR_ERR_OUT_OF_MEMORY;
            }
            text = expanded;
            capacity = new_capacity;
        }
        memcpy(text + length, chunk, size);
        length += size;
    }
    cplat_secure_zero(chunk, sizeof(chunk));
    fclose(fp);
    text[length] = '\0';
    root = cJSON_ParseJSONCWithLength(text, length);
    cplat_secure_zero(text, length);
    cplat_free(text);
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        return POTR_ERR_INVALID_ARGUMENT;
    }
    *root_out = root;
    return POTR_OK;
}

/* 既存の値域検査を使うため、JSON 整数を十進文字列として返す。 */
static inline const char *config_value_text(const cJSON *item, char *buffer, size_t buffer_size)
{
    int64_t parsed;
    if (cJSON_IsString(item))
    {
        return cJSON_GetStringValue(item);
    }
    if (cJSON_HasExactInteger(item) && cJSON_GetInt64Value(item, &parsed) && buffer_size >= 32U)
    {
        (void)snprintf(buffer, buffer_size, "%" PRId64, parsed);
        return buffer;
    }
    return NULL;
}

#endif /* PORTER_PROTOCOL_CONFIG_PARSE_COMMON_H */
