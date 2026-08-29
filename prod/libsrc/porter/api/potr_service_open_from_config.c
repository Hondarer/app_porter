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
#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/porter_spec.h>

#include <porter/protocol/config.h>
#include <porter/infra/potr_trace.h>

#include <cplat/runtime/memory_lock.h>

/* Doxygen コメントは、ヘッダーに記載 */

int potr_service_open_from_config(const char *config_path, int64_t service_id, potr_role role, potr_recv_fn callback,
                              potr_context **handle)
{
    potr_global_config global;
    potr_service_def service;
    const char *config_label;
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

    if (config_path == NULL || handle == NULL)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "potr_service_open_from_config: invalid argument"
                   " (config_path=%p handle=%p)",
                   (const void *)config_path, (const void *)handle);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    result = potr_internal_config_load_global(config_path, &global);
    if (result != POTR_OK)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "potr_service_open_from_config: service_id=%" PRId64 " failed to load global config from '%s'",
                   service_id, config_path);
        return result;
    }

    result = potr_internal_config_load_service(config_path, service_id, &service);
    if (result != POTR_OK)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_service_open_from_config: service_id=%" PRId64 " not found in '%s'",
                   service_id, config_path);
        return result;
    }

    result = potr_service_open(&global, &service, role, callback, handle);

    /* service は AES 鍵を平文で保持するため、復帰前に消去する */
    cplat_secure_zero(&service, sizeof(service));

    return result;
}
