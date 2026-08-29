/**
 *******************************************************************************
 *  @file           potr_result.h
 *  @brief          cplat の詳細エラーを porter の結果コードへ変換する API を宣言します。
 *  @author         Tetsuo Honda
 *  @date           2026/08/12
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

#ifndef POTR_INFRA_RESULT_H
#define POTR_INFRA_RESULT_H

#include <cplat/base/error.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          cplat の詳細エラーを porter の結果コードへ変換します。
     *  @param[in]      detail  変換元の詳細エラー。NULL 可。
     *  @return         対応する POTR_OK または POTR_ERR_* を返します。
     *
     *  cplat/net の API はプラットフォーム共通の要因 (cplat_error_cause) を
     *  detail_out へ格納するため、要因を porter の結果コードへ写像します。\n
     *  対応する要因が見当たらない場合は POTR_ERR_IO を返します。
     */
    extern int potr_internal_result_from_error(const cplat_error *detail);

    /**
     *  @brief          cplat の結果コードと詳細エラーを porter の結果コードへ変換します。
     *  @param[in]      cplat_result cplat API の戻り値 (CPLAT_OK または CPLAT_ERR_*)。
     *  @param[in]      detail          対応する詳細エラー。NULL 可。
     *  @return         対応する POTR_OK または POTR_ERR_* を返します。
     *
     *  `cplat_socket_recv_all` のように、OS エラーを伴わない結果コード
     *  (`CPLAT_ERR_EOF` など) を返す API では、@p detail に要因が
     *  格納されないため @ref potr_internal_result_from_error だけでは判定できません。\n
     *  本関数は、そのような結果コードを先に判定してから要因ベースの変換へ委譲します。
     */
    extern int potr_internal_result_from_socket_result(int cplat_result, const cplat_error *detail);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_INFRA_RESULT_H */
