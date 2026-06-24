/**
 *******************************************************************************
 *  @file           config.h
 *  @brief          設定ファイルを解析する内部 API を提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/* NOTE: このヘッダーは多数のソース ファイルから参照されるため、            */
/*       @hideincludedbygraph によって "Included by" グラフを無効にします。 */

#ifndef CONFIG_H
#define CONFIG_H

#include <porter/porter_type.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          設定ファイルから global セクションを読み込みます。
     *  @param[in]      config_path 設定ファイルのパス。
     *  @param[out]     global      読み込み結果を格納する構造体へのポインター。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
     *
     *  global セクションが存在しない場合はデフォルト値を設定します。
     */
    extern int config_load_global(const char *config_path, PotrGlobalConfig *global);

    /**
     *  @brief          設定ファイルから指定サービスの定義を読み込みます。
     *  @param[in]      config_path 設定ファイルのパス。
     *  @param[in]      service_id  読み込むサービスの ID。
     *  @param[out]     def         読み込み結果を格納する構造体へのポインター。
     *  @return         成功時は POTR_SUCCESS、サービスが見つからない場合は POTR_ERROR を返します。
     *
     *  [service.id] 形式のセクション名から id 部分を取得し、service_id と照合します。\n
     *  サービスの識別子はセクション名の id であり、ポート番号とは無関係です。
     */
    extern int config_load_service(const char *config_path, int64_t service_id, PotrServiceDef *def);

    /**
     *  @brief          設定ファイルに登録されているすべてのサービス ID を列挙します。
     *  @param[in]      config_path 設定ファイルのパス。
     *  @param[out]     ids_out     サービス ID 配列へのポインターを格納する変数。
     *                              呼び出し元が free(*ids_out) の責務を持ちます。
     *  @param[out]     count_out   列挙したサービス ID 数。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
     *
     *  初期容量 POTR_MAX_SERVICES で配列を確保し、超過時は realloc で 2 倍に拡張します。
     */
    extern int config_list_service_ids(const char *config_path, int64_t **ids_out, int *count_out);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CONFIG_H */
