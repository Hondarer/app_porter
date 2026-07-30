/**
 *******************************************************************************
 *  @file           potrIpAddr.h
 *  @brief          IPv4 アドレスを変換する内部ユーティリティを提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/05
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

#ifndef POTR_IP_ADDR_H
#define POTR_IP_ADDR_H

#include <com_util/base/platform.h>
#include <porter/porter_result.h>

#if defined(PLATFORM_LINUX)
    #include <arpa/inet.h>
    #include <netinet/in.h>
#elif defined(PLATFORM_WINDOWS)
    #include <com_util/base/windows_sdk.h>
#endif /* PLATFORM_ */

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief      IPv4 アドレス文字列を struct in_addr に変換します。
     *  @param[in]  ip_str   変換する IPv4 アドレス文字列 (例: "192.168.0.1")。
     *  @param[out] out_addr 変換結果の書き戻し先。
     *  @retval     POTR_OK                    変換に成功しました。
     *  @retval     POTR_ERR_INVALID_ARGUMENT  引数または IPv4 アドレス文字列が不正です。
     */
    extern int parse_ipv4_addr(const char *ip_str, struct in_addr *out_addr);

    /**
     *  @brief      ホスト名または IPv4 アドレス文字列を struct in_addr に解決します。
     *
     *  getaddrinfo() を使用して AF_INET で名前解決します。\n
     *  複数のアドレスが返された場合は先頭のアドレスを採用します。
     *
     *  @param[in]  host     解決するホスト名または IPv4 アドレス文字列。
     *  @param[out] out_addr 解決結果の書き戻し先。
     *  @retval     POTR_OK                    名前解決に成功しました。
     *  @retval     POTR_ERR_INVALID_ARGUMENT  引数が不正です。
     *  @retval     POTR_ERR_IO                名前解決に失敗しました。
     */
    extern int resolve_ipv4_addr(const char *host, struct in_addr *out_addr);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_IP_ADDR_H */
