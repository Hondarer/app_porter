/**
 *******************************************************************************
 *  @file           api_open_paths_common.h
 *  @brief          IPv4 の解決・解析結果を porter の結果コードへ変換します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef API_OPEN_PATHS_COMMON_PRIVATE_H
#define API_OPEN_PATHS_COMMON_PRIVATE_H

#include <porter/potr_context.h>

/**
 *  @brief          ホスト名または IPv4 アドレスを解決します。
 *  @param[in]      host            入力文字列。NULL は許可しません。
 *  @param[out]     address_out     ネットワーク バイト順のアドレス。NULL は許可しません。
 *  @return         成功時 POTR_OK、不正な入力は POTR_ERR_INVALID_ARGUMENT、解決失敗は POTR_ERR_IO。
 *  @par            スレッド セーフ
 *                  出力先を共有しない呼び出しは並行実行できます。
 */
int api_paths_resolve_ipv4(const char *host, uint32_t *address_out);

/**
 *  @brief          IPv4 アドレス文字列を解析します。
 *  @param[in]      text            入力文字列。NULL は許可しません。
 *  @param[out]     address_out     ネットワーク バイト順のアドレス。NULL は許可しません。
 *  @return         成功時 POTR_OK、不正な入力は POTR_ERR_INVALID_ARGUMENT、解決失敗は POTR_ERR_IO。
 *  @par            スレッド セーフ
 *                  出力先を共有しない呼び出しは並行実行できます。
 */
int api_paths_parse_ipv4(const char *text, uint32_t *address_out);

#endif
