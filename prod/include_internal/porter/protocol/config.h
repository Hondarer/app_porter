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

/*
 * このヘッダーは多数のソース ファイルから参照されるため、
 * @hideincludedbygraph によって "Included by" グラフを無効にする。
 */

#ifndef POTR_PROTOCOL_CONFIG_H
#define POTR_PROTOCOL_CONFIG_H

#include <porter/porter_result.h>
#include <cplat/crt/stdlib.h>
#include <porter/porter_type.h>

/** 設定の読み込みが所有するサービス定義。直接 open へ渡した定義には使用しません。 */
typedef struct potr_internal_config_service
{
    potr_service_def definition; /**< document から借用する文字列と、所有する配列を含む読み込み結果。 */
    struct cJSON *document; /**< 文字列を所有する JSON 文書。 */
    const char **pinned_certs; /**< 所有する直接登録のパス配列。 */
    const char **peer_names; /**< 所有する相手の名前の配列。 */
} potr_internal_config_service;

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          JSONC 設定ファイルから global オブジェクトを読み込みます。
     *  @param[in]      config_path 設定ファイルのパス。
     *  @param[out]     global      読み込み結果を格納する構造体へのポインター。
     *  @return         成功時は POTR_OK、引数が NULL または構文が不正な場合は
     *                  POTR_ERR_INVALID_ARGUMENT、設定ファイルを開けない場合は POTR_ERR_IO を返します。
     *
     *  global オブジェクトが存在しない場合はデフォルト値を設定します。
     */
    extern int potr_internal_config_load_global(const char *config_path, potr_global_config *global);

    /**
     *  @brief          読み込みが所有する文字列と配列を解放し、ポインターを NULL にします。
     *  @param[in,out]  loaded 読み込み結果。NULL は無処理。直接指定した定義には使用しません。
     *  @note           並行使用中には呼びません。失敗した読み込みは内部で解放します。
     */
    void potr_internal_config_service_dispose(potr_internal_config_service *loaded);

    /**
     *  @brief          設定ファイルから指定サービスの定義を読み込みます。
     *  @param[in]      config_path 設定ファイルのパス。
     *  @param[in]      service_id  読み込むサービスの ID。
     *  @param[out]     loaded      読み込み結果を所有する構造体。再使用前に解放します。
     *  @return         成功時は POTR_OK、引数が NULL または構文が不正な場合は POTR_ERR_INVALID_ARGUMENT、
     *                  指定した service_id が存在しない場合は POTR_ERR_NOT_FOUND、
     *                  設定ファイルを開けない場合は POTR_ERR_IO を返します。
     *
     *  services オブジェクトの十進 ID キーを service_id と照合します。\n
     *  サービスの識別子はキーの ID であり、ポート番号とは無関係です。
     *  成功時の文字列と配列は読み込み側が所有し、使用後に potr_internal_config_service_dispose() で解放します。
     *  確保失敗時は POTR_ERR_OUT_OF_MEMORY を返します。
     */
    extern int potr_internal_config_load_service(const char *config_path, int64_t service_id, potr_internal_config_service *loaded);

    /**
     *  @brief          設定ファイルに登録されているすべてのサービス ID を列挙します。
     *  @param[in]      config_path 設定ファイルのパス。
     *  @param[out]     ids_out     サービス ID 配列へのポインターを格納する変数。
     *                              呼び出し元が cplat_free(*ids_out) の責務を持ちます。
     *  @param[out]     count_out   列挙したサービス ID 数。
     *  @return         成功時は POTR_OK、引数が NULL または構文が不正な場合は POTR_ERR_INVALID_ARGUMENT、
     *                  メモリを確保できない場合は POTR_ERR_OUT_OF_MEMORY、
     *                  設定ファイルを開けない場合は POTR_ERR_IO を返します。
     *
     *  初期容量 POTR_MAX_SERVICES で配列を確保し、超過時は realloc で 2 倍に拡張します。
     */
    extern int potr_internal_config_list_service_ids(const char *config_path, int64_t **ids_out, int *count_out);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_PROTOCOL_CONFIG_H */
