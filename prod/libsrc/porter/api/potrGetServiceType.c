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

#include <porter/porter_const.h>
#include <porter/porter_spec.h>

#include <porter/protocol/config.h>

/* Doxygen コメントは、ヘッダーに記載 */

int potrGetServiceType(const char *config_path, int64_t service_id, PotrType *type)
{
    PotrServiceDef def;

    if (config_path == NULL || type == NULL)
    {
        return POTR_ERROR;
    }

    if (config_load_service(config_path, service_id, &def) != POTR_SUCCESS)
    {
        return POTR_ERROR;
    }

    *type = def.type;
    return POTR_SUCCESS;
}
