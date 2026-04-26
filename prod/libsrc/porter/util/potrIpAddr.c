/**
 *******************************************************************************
 *  @file           potrIpAddr.c
 *  @brief          IPv4 アドレス変換ユーティリティ (内部用)。
 *  @author         Tetsuo Honda
 *  @date           2026/03/05
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>
#include <stddef.h>
#include <string.h>

#if defined(PLATFORM_LINUX)
    #include <netdb.h>
#endif /* PLATFORM_LINUX */

#include <porter_const.h>

#include "potrIpAddr.h"

/* IPv4 文字列をネットワークバイトオーダーへ変換する。 */
int parse_ipv4_addr(const char *ip_str, struct in_addr *out_addr)
{
    if (ip_str == NULL || out_addr == NULL)
    {
        return POTR_ERROR;
    }

    if (inet_pton(AF_INET, ip_str, out_addr) == 1)
    {
        return POTR_SUCCESS;
    }
    else
    {
        return POTR_ERROR;
    }
}

/* ホスト名または IPv4 アドレス文字列を struct in_addr に解決する。 */
int resolve_ipv4_addr(const char *host, struct in_addr *out_addr)
{
    struct addrinfo  hints;
    struct addrinfo *res = NULL;
    int              ret;

    if (host == NULL || out_addr == NULL)
    {
        return POTR_ERROR;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    ret = getaddrinfo(host, NULL, &hints, &res);
    if (ret != 0 || res == NULL)
    {
        return POTR_ERROR;
    }

    /* 複数アドレスが返された場合は先頭を採用する */
    *out_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;

    freeaddrinfo(res);
    return POTR_SUCCESS;
}
