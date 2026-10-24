/**
 *******************************************************************************
 *  @file           api_open_paths_common.c
 *  @brief          IPv4 の解決・解析結果を porter の結果コードへ変換します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <inttypes.h>
#include <string.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/potr_peer_table.h>
#include <porter/infra/potr_trace.h>
#include <porter/infra/potr_result.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/endpoint.h>
#include <cplat/net/socket.h>

#include "api_open_paths_common.h"

/* cplat のアドレス解決/変換結果コードを porter の結果コードへ変換する。
 * cplat_ipv4_resolve() / cplat_ipv4_parse() は CPLAT_ERR_INVALID_ARGUMENT または
 * CPLAT_ERR_UNKNOWN のみを返し、失敗時の詳細エラーはソケット由来ではない (GAI ドメイン等) ため
 * potr_internal_result_from_error() の要因ベース変換は適用しない。 */
static int result_from_ipv4_result(int cplat_result)
{
    if (cplat_result == CPLAT_OK)
    {
        return POTR_OK;
    }
    if (cplat_result == CPLAT_ERR_INVALID_ARGUMENT)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    return POTR_ERR_IO;
}

/* Doxygen コメントは、ヘッダーに記載 */
int api_paths_resolve_ipv4(const char *host, uint32_t *address_out)
{
    cplat_error detail;
    int result = cplat_ipv4_resolve(host, address_out, &detail);

    if (result != CPLAT_OK)
    {
        POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail, "resolve_ipv4(%s) failed", host);
    }
    return result_from_ipv4_result(result);
}

/* Doxygen コメントは、ヘッダーに記載 */
int api_paths_parse_ipv4(const char *text, uint32_t *address_out)
{
    return result_from_ipv4_result(cplat_ipv4_parse(text, address_out));
}
