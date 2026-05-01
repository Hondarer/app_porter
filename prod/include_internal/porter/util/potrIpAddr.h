/**
 *******************************************************************************
 *  @file           potrIpAddr.h
 *  @brief          IPv4 アドレス変換ユーティリティ (内部用)。
 *  @author         Tetsuo Honda
 *  @date           2026/03/05
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef POTR_IP_ADDR_H
#define POTR_IP_ADDR_H

#include <com_util/base/platform.h>

#if defined(PLATFORM_LINUX)
    #include <arpa/inet.h>
    #include <netinet/in.h>
#elif defined(PLATFORM_WINDOWS)
    #include <com_util/base/windows_sdk.h>
#endif /* PLATFORM_ */

int parse_ipv4_addr(const char *ip_str, struct in_addr *out_addr);

/**
 *  @brief  ホスト名または IPv4 アドレス文字列を struct in_addr に解決する。
 *  @details
 *  getaddrinfo() を使用して AF_INET で名前解決する。
 *  複数のアドレスが返された場合は先頭のアドレスを採用する。
 */
int resolve_ipv4_addr(const char *host, struct in_addr *out_addr);

#endif /* POTR_IP_ADDR_H */
