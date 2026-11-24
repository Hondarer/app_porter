/**
 *******************************************************************************
 *  @file           potr_config_list_service_ids.c
 *  @brief          設定ファイルのサービス ID を列挙する公開関数を提供します。
 *******************************************************************************
 */

#include <cJSON.h>

#include <cplat/crt/stdlib.h>

#include <porter/porter_result.h>
#include <porter/porter_spec.h>
#include <porter/infra/potr_trace.h>
#include <porter/protocol/config_decode.h>
#include <porter/protocol/config_document.h>
#include <porter/protocol/config_report.h>

/* Doxygen コメントは、ヘッダーに記載 */

int POTR_API potr_config_list_service_ids(const char *config_path, int64_t **ids, size_t *count)
{
    potr_internal_config_reporter reporter;
    potr_internal_config_id_list id_list;
    cJSON *root = NULL;
    int ret;

    if (ids != NULL)
    {
        *ids = NULL;
    }
    if (count != NULL)
    {
        *count = 0U;
    }

    if (config_path == NULL || ids == NULL || count == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    id_list.ids = NULL;
    id_list.count = 0U;
    potr_internal_config_reporter_init(&reporter, config_path, NULL, NULL, 1);

    ret = potr_internal_config_document_load(config_path, &reporter, &root);
    if (ret != POTR_OK)
    {
        if (ret == POTR_ERR_IO)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_config_list_service_ids: config '%s' read failed", config_path);
        }
        else if (ret == POTR_ERR_OUT_OF_MEMORY)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_config_list_service_ids: config '%s' out of memory", config_path);
        }
        return ret;
    }

    ret = potr_internal_config_decode_root(root, &reporter, &id_list);
    cjson_app_cJSON_Delete(root);

    if (ret != POTR_OK)
    {
        if (ret == POTR_ERR_OUT_OF_MEMORY)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_config_list_service_ids: config '%s' out of memory", config_path);
        }
        cplat_free(id_list.ids);
        return ret;
    }

    *ids = id_list.ids;
    *count = id_list.count;
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void POTR_API potr_config_service_ids_free(int64_t *ids)
{
    cplat_free(ids);
}
