/**
 *******************************************************************************
 *  @file           potrGetServiceType.c
 *  @brief          サービス種別を取得する potrGetServiceType 関数を提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/22
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/porter_spec.h>

#include <porter/protocol/config.h>

/* Doxygen コメントは、ヘッダーに記載 */

int potrGetServiceType(const char *config_path, int64_t service_id, PotrType *type)
{
    PotrServiceDef def;
    int result;

    if (config_path == NULL || type == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    result = config_load_service(config_path, service_id, &def);
    if (result != POTR_OK)
    {
        return result;
    }

    *type = def.type;
    return POTR_OK;
}
