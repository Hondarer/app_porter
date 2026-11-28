/**
 *******************************************************************************
 *  @file           potr_service_open_from_config.c
 *  @brief          設定ファイルから porter サービスを開始する potr_service_open_from_config 関数を提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/28
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <inttypes.h>

#include <cJSON.h>
#include <cJSON_PatchCheck.h>

#include <cplat/runtime/memory_lock.h>

#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include <porter/porter_spec.h>
#include <porter/infra/potr_trace.h>
#include <porter/protocol/config.h>
#include <porter/protocol/config_decode.h>
#include <porter/protocol/config_document.h>
#include <porter/protocol/config_report.h>
#include "api_open_internal.h"

/* Doxygen コメントは、ヘッダーに記載 */

int potr_service_open_from_config(const char *config_path, int64_t service_id, potr_role role, potr_recv_fn callback,
                                  potr_context **handle)
{
    potr_global_config global;
    potr_internal_config_service owned = {0};
    potr_internal_config_reporter reporter;
    cJSON *root = NULL;
    const char *config_label;
    uint32_t role_mask;
    int result;

    if (config_path != NULL)
    {
        config_label = config_path;
    }
    else
    {
        config_label = "(null)";
    }

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "potr_service_open_from_config: service_id=%" PRId64 " config=%s", service_id,
               config_label);

    if (config_path == NULL || handle == NULL || (role != POTR_ROLE_SENDER && role != POTR_ROLE_RECEIVER))
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "potr_service_open_from_config: invalid argument"
                   " (config_path=%p handle=%p role=%d)",
                   (const void *)config_path, (const void *)handle, (int)role);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    role_mask = (role == POTR_ROLE_SENDER) ? POTR_CONFIG_ROLE_SENDER : POTR_CONFIG_ROLE_RECEIVER;
    potr_internal_config_reporter_init(&reporter, config_path, NULL, NULL, 1);

    result = potr_internal_config_document_load(config_path, &reporter, &root);
    if (result != POTR_OK)
    {
        if (result == POTR_ERR_IO)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_service_open_from_config: service_id=%" PRId64 " config '%s' read failed",
                       service_id, config_path);
        }
        else if (result == POTR_ERR_OUT_OF_MEMORY)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "potr_service_open_from_config: service_id=%" PRId64 " config '%s' out of memory", service_id,
                       config_path);
        }
        else
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "potr_service_open_from_config: service_id=%" PRId64 " config '%s' has %zu error(s)", service_id,
                       config_path, reporter.issue_count);
        }
        return result;
    }

    result = potr_internal_config_decode_target_service(root, service_id, role_mask, &global, &owned, &reporter);
    if (result != POTR_OK)
    {
        cjson_app_cJSON_Delete(root);
        if (result == POTR_ERR_NOT_FOUND)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "potr_service_open_from_config: service_id=%" PRId64 " not found in '%s'", service_id,
                       config_path);
        }
        else if (result == POTR_ERR_OUT_OF_MEMORY)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "potr_service_open_from_config: service_id=%" PRId64 " config '%s' out of memory", service_id,
                       config_path);
        }
        else
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "potr_service_open_from_config: service_id=%" PRId64 " config '%s' has %zu error(s)", service_id,
                       config_path, reporter.issue_count);
        }
        return result;
    }

    owned.document = root;
    result = api_service_open_validated(&global, &owned.definition, role, callback, handle);
    potr_internal_config_service_dispose(&owned);

    cplat_secure_zero(&owned.definition, sizeof(owned.definition)); /* service は AES 鍵を平文で保持するため、復帰前に消去する */

    return result;
}
