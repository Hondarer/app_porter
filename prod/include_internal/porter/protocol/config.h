/**
 *******************************************************************************
 *  @file           config.h
 *  @brief          設定ファイル解析モジュールの内部ヘッダー。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <porter_type.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    extern int config_load_global(const char *config_path, PotrGlobalConfig *global);
    extern int config_load_service(const char *config_path, int64_t service_id,
                                   PotrServiceDef *def);
    extern int config_list_service_ids(const char *config_path,
                                       int64_t **ids_out, int *count_out);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CONFIG_H */
