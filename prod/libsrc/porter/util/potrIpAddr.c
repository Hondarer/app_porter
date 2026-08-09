/**
 *******************************************************************************
 *  @file           potrIpAddr.c
 *  @brief          IPv4 アドレスを変換する内部ユーティリティを提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/05
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>
#include <com_util/base/error_message.h>
#include <stddef.h>
#include <string.h>

#if defined(PLATFORM_LINUX)
    #include <netdb.h>
#endif /* PLATFORM_LINUX */

#include <porter/porter_result.h>
#include <porter/porter_const.h>

#include <porter/util/potrIpAddr.h>
#include <porter/infra/potrSocketError.h>
#include <porter/infra/potrTrace.h>

/* Doxygen コメントは、ヘッダーに記載 */

int parse_ipv4_addr(const char *ip_str, struct in_addr *out_addr)
{
    if (ip_str == NULL || out_addr == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (inet_pton(AF_INET, ip_str, out_addr) == 1)
    {
        return POTR_OK;
    }
    else
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

int resolve_ipv4_addr(const char *host, struct in_addr *out_addr)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    int ret;

    if (host == NULL || out_addr == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    ret = getaddrinfo(host, NULL, &hints, &res);
    if (ret != 0)
    {
#if defined(PLATFORM_LINUX)
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR, "resolve_ipv4_addr: getaddrinfo(%s) failed: code=%d: %s", host, ret,
                   gai_strerror(ret));
#elif defined(PLATFORM_WINDOWS)
        char message[256];
        com_util_error detail;

        /* gai_strerrorA() は静的バッファーを使用するため、接続スレッド間で共有しません。 */
        /* see: https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-gai_strerrora */
        com_util_error_capture_windows_error(&detail, (unsigned long)ret);
        (void)com_util_error_message(message, sizeof(message), &detail);
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR, "resolve_ipv4_addr: getaddrinfo(%s) failed: code=%d: %s", host, ret,
                   message);
#endif /* PLATFORM_ */
        if (res != NULL)
        {
            freeaddrinfo(res);
        }
        return POTR_ERR_IO;
    }
    if (res == NULL)
    {
        POTR_TRACE(COM_UTIL_TRACE_LEVEL_ERROR, "resolve_ipv4_addr: getaddrinfo(%s) returned no IPv4 result", host);
        return POTR_ERR_IO;
    }

    /* 複数アドレスが返された場合は先頭を採用する */
    *out_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;

    freeaddrinfo(res);
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_ipv4_to_string(const struct in_addr addr, char *buffer, const size_t buffer_size, com_util_error *detail_out)
{
    if ((buffer == NULL) || (buffer_size == 0U))
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    /* buffer_size は呼び出し側バッファー長。IPv4 文字列は 16 バイト未満で socklen_t に収まる */
    if (inet_ntop(AF_INET, &addr, buffer, (socklen_t)buffer_size) == NULL)
    {
        return potr_socket_error_report(detail_out);
    }
    potr_socket_error_clear(detail_out);
    return POTR_OK;
}
