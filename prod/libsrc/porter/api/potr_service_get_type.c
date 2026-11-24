/**
 *******************************************************************************
 *  @file           potr_service_get_type.c
 *  @brief          サービス種別を取得する potr_service_get_type 関数を提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/22
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <inttypes.h>

#include <cJSON.h>

#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include <porter/porter_spec.h>
#include <porter/infra/potr_trace.h>
#include <porter/protocol/config.h>
#include <porter/protocol/config_decode.h>
#include <porter/protocol/config_document.h>
#include <porter/protocol/config_report.h>

/* Doxygen コメントは、ヘッダーに記載 */

int potr_service_get_type(const char *config_path, int64_t service_id, potr_type *type)
{
    potr_internal_config_service owned = {0};
    potr_internal_config_reporter reporter;
    cJSON *root = NULL;
    int result;

    if (config_path == NULL || type == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    potr_internal_config_reporter_init(&reporter, config_path, NULL, NULL, 1);

    result = potr_internal_config_document_load(config_path, &reporter, &root);
    if (result != POTR_OK)
    {
        if (result == POTR_ERR_IO)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_service_get_type: service_id=%" PRId64 " config '%s' read failed",
                       service_id, config_path);
        }
        else if (result == POTR_ERR_OUT_OF_MEMORY)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "potr_service_get_type: service_id=%" PRId64 " config '%s' out of memory", service_id,
                       config_path);
        }
        return result;
    }

    result = potr_internal_config_decode_target_service(root, service_id, 0U, NULL, &owned, &reporter);
    if (result != POTR_OK)
    {
        cjson_app_cJSON_Delete(root);
        if (result == POTR_ERR_OUT_OF_MEMORY)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "potr_service_get_type: service_id=%" PRId64 " config '%s' out of memory", service_id,
                       config_path);
        }
        return result;
    }

    *type = owned.definition.type;
    owned.document = root;
    potr_internal_config_service_dispose(&owned);

    return POTR_OK;
}
