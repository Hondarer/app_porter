/**
 *******************************************************************************
 *  @file           config_list_service_ids.c
 *  @brief          JSONC 設定内の service ID を列挙します。
 *******************************************************************************
 */
#include <stdint.h>
#include <string.h>
#include <cplat/crt/stdlib.h>
#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include <porter/protocol/config.h>
#include <porter/protocol/config_parse_common.h>

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_config_list_service_ids(const char *config_path, int64_t **ids_out, int *count_out)
{
    cJSON *root;
    const cJSON *services;
    const cJSON *service;
    int64_t *ids;
    int capacity;
    int count;
    int ret;

    if (config_path == NULL || ids_out == NULL || count_out == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    ret = config_read_jsonc(config_path, &root);
    if (ret != POTR_OK)
    {
        return ret;
    }
    services = cJSON_GetObjectItemCaseSensitive(root, "services");
    if (services != NULL && !cJSON_IsObject(services))
    {
        cJSON_Delete(root);
        return POTR_ERR_INVALID_ARGUMENT;
    }
    capacity = (int)POTR_MAX_SERVICES;
    count = 0;
    ids = (int64_t *)cplat_calloc((size_t)capacity, sizeof(*ids));
    if (ids == NULL)
    {
        cJSON_Delete(root);
        return POTR_ERR_OUT_OF_MEMORY;
    }
    cJSON_ArrayForEach(service, services)
    {
        int64_t parsed_id;
        if (service->string == NULL || cplat_parse_int64(&parsed_id, service->string, 10) != CPLAT_OK ||
            !cJSON_IsObject(service))
        {
            continue;
        }
        if (count >= capacity)
        {
            int new_capacity = capacity * 2;
            int64_t *new_ids = (int64_t *)cplat_realloc(ids, (size_t)new_capacity, sizeof(*ids));
            if (new_ids == NULL)
            {
                cplat_free(ids);
                cJSON_Delete(root);
                return POTR_ERR_OUT_OF_MEMORY;
            }
            ids = new_ids;
            capacity = new_capacity;
        }
        ids[count++] = parsed_id;
    }
    cJSON_Delete(root);
    *ids_out = ids;
    *count_out = count;
    return POTR_OK;
}
