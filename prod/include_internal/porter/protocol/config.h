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
     *  @brief          読み込みが所有する文字列と配列を解放し、ポインターを NULL にします。
     *  @param[in,out]  loaded 読み込み結果。NULL は無処理。直接指定した定義には使用しません。
     *  @note           並行使用中には呼びません。失敗した読み込みは内部で解放します。
     */
    extern void potr_internal_config_service_dispose(potr_internal_config_service *loaded);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_PROTOCOL_CONFIG_H */
