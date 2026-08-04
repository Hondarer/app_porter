/**
 *******************************************************************************
 *  @file           potrPlatform_windows.c
 *  @brief          プラットフォームの差異を抽象化する Windows 向け機能を実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/04/18
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)

    #include <porter/porter_result.h>
    #include <porter/porter_const.h>

    #include <porter/infra/potrPlatform.h>
    #include <porter/infra/potrSocketError.h>

int potr_socket_open(const int type, PotrSocket *sock_out, com_util_error *detail_out)
{
    PotrSocket sock;
    if (sock_out == NULL)
    {
        return potr_socket_error_report_invalid_argument(detail_out);
    }
    *sock_out = POTR_INVALID_SOCKET;
    sock = socket(AF_INET, type, 0);
    if (sock == POTR_INVALID_SOCKET)
    {
        return potr_socket_error_report(detail_out);
    }
    *sock_out = sock;
    potr_socket_error_clear(detail_out);
    return POTR_OK;
}

int potr_bind(const PotrSocket sock, const struct sockaddr_in *addr, com_util_error *detail_out)
{
    if (addr == NULL)
    {
        return potr_socket_error_report_invalid_argument(detail_out);
    }
    if (bind(sock, (const struct sockaddr *)addr, (int)sizeof(*addr)) == SOCKET_ERROR)
    {
        return potr_socket_error_report(detail_out);
    }
    potr_socket_error_clear(detail_out);
    return POTR_OK;
}

int potr_listen(const PotrSocket sock, const int backlog, com_util_error *detail_out)
{
    if (listen(sock, backlog) == SOCKET_ERROR)
    {
        return potr_socket_error_report(detail_out);
    }
    potr_socket_error_clear(detail_out);
    return POTR_OK;
}

int potr_accept(const PotrSocket sock, struct sockaddr_in *addr, PotrSocket *sock_out, com_util_error *detail_out)
{
    int length = (int)sizeof(*addr);
    PotrSocket accepted;
    if ((addr == NULL) || (sock_out == NULL))
    {
        return potr_socket_error_report_invalid_argument(detail_out);
    }
    accepted = accept(sock, (struct sockaddr *)addr, &length);
    if (accepted == POTR_INVALID_SOCKET)
    {
        return potr_socket_error_report(detail_out);
    }
    *sock_out = accepted;
    potr_socket_error_clear(detail_out);
    return POTR_OK;
}

int potr_connect(const PotrSocket sock, const struct sockaddr_in *addr, com_util_error *detail_out)
{
    if (addr == NULL)
    {
        return potr_socket_error_report_invalid_argument(detail_out);
    }
    if (connect(sock, (const struct sockaddr *)addr, (int)sizeof(*addr)) == SOCKET_ERROR)
    {
        return potr_socket_error_report(detail_out);
    }
    potr_socket_error_clear(detail_out);
    return POTR_OK;
}

int potr_setsockopt(const PotrSocket sock, const int level, const int optname, const void *optval, const int optlen,
                    com_util_error *detail_out)
{
    if ((optval == NULL) || (optlen <= 0))
    {
        return potr_socket_error_report_invalid_argument(detail_out);
    }
    if (setsockopt(sock, level, optname, (const char *)optval, optlen) == SOCKET_ERROR)
    {
        return potr_socket_error_report(detail_out);
    }
    potr_socket_error_clear(detail_out);
    return POTR_OK;
}

int potr_socket_get_pending_error(const PotrSocket sock, com_util_error *detail_out)
{
    int pending_error = 0;
    int length = (int)sizeof(pending_error);
    /* SO_ERROR はソケット単位の保留エラーを返し、その値をリセットします。 */
    /* see: https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-getsockopt */
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&pending_error, &length) == SOCKET_ERROR)
        return potr_socket_error_report(detail_out);
    if (pending_error != 0)
        return potr_socket_error_report_code(detail_out, (unsigned long)pending_error);
    potr_socket_error_clear(detail_out);
    return POTR_OK;
}

int potr_poll_readable_multi(const PotrSocket *fds, const size_t count, const int timeout_ms, unsigned char *ready_out,
                             com_util_error *detail_out)
{
    WSAPOLLFD poll_fds[POTR_MAX_PATH];
    size_t index;
    ULONG valid_count = 0;
    int poll_result;
    if ((fds == NULL) || (ready_out == NULL) || (count == 0U) || (count > POTR_MAX_PATH))
    {
        return potr_socket_error_report_invalid_argument(detail_out);
    }
    for (index = 0U; index < count; ++index)
    {
        ready_out[index] = 0U;
        if (fds[index] != POTR_INVALID_SOCKET)
        {
            poll_fds[valid_count].fd = fds[index];
            poll_fds[valid_count].events = POLLIN;
            poll_fds[valid_count].revents = 0;
            ++valid_count;
        }
    }
    if (valid_count == 0U)
    {
        potr_socket_error_clear(detail_out);
        return POTR_OK;
    }
    poll_result = WSAPoll(poll_fds, valid_count, timeout_ms);
    if (poll_result == SOCKET_ERROR)
    {
        com_util_error detail;
        const int result = potr_socket_error_report(&detail);
        if (potr_socket_error_is(&detail, POTR_SOCKET_CAUSE_INTERRUPTED))
        {
            potr_socket_error_clear(detail_out);
            return POTR_OK;
        }
        if (detail_out != NULL)
        {
            *detail_out = detail;
        }
        return result;
    }
    valid_count = 0;
    for (index = 0U; index < count; ++index)
    {
        if (fds[index] != POTR_INVALID_SOCKET)
        {
            ready_out[index] =
                (unsigned char)((poll_fds[valid_count].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0);
            ++valid_count;
        }
    }
    potr_socket_error_clear(detail_out);
    return POTR_OK;
}

int potr_shutdown_receive(PotrSocket *sock_inout, com_util_error *detail_out)
{
    int result = POTR_OK;
    if (sock_inout == NULL)
    {
        return potr_socket_error_report_invalid_argument(detail_out);
    }
    if (closesocket(*sock_inout) == SOCKET_ERROR)
    {
        result = potr_socket_error_report(detail_out);
    }
    *sock_inout = POTR_INVALID_SOCKET;
    if (result == POTR_OK)
    {
        potr_socket_error_clear(detail_out);
    }
    return result;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_sendto(PotrSocket sock, const uint8_t *buf, size_t len, int flags, const struct sockaddr *dest, int dest_len,
                com_util_error *detail_out)
{
    const int result = sendto(sock, (const char *)buf, (int)len, flags, dest, dest_len);
    if (result == SOCKET_ERROR)
    {
        (void)potr_socket_error_report(detail_out);
    }
    else
    {
        potr_socket_error_clear(detail_out);
    }
    return result;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_recvfrom(PotrSocket sock, uint8_t *buf, size_t len, int flags, struct sockaddr *src, int *src_len,
                  com_util_error *detail_out)
{
    if (src_len == NULL)
    {
        (void)potr_socket_error_report_invalid_argument(detail_out);
        return SOCKET_ERROR;
    }
    const int result = recvfrom(sock, (char *)buf, (int)len, flags, src, src_len);
    if (result == SOCKET_ERROR)
    {
        (void)potr_socket_error_report(detail_out);
    }
    else
    {
        potr_socket_error_clear(detail_out);
    }
    return result;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_poll_writable(PotrSocket fd, int timeout_ms, com_util_error *detail_out)
{
    WSAPOLLFD pfd;
    int r;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    r = WSAPoll(&pfd, 1, timeout_ms);
    if (r > 0 && (pfd.revents & POLLOUT))
    {
        potr_socket_error_clear(detail_out);
        return 1;
    }
    if (r == 0)
    {
        potr_socket_error_clear(detail_out);
        return 0;
    }
    com_util_error detail;
    (void)potr_socket_error_report(&detail);
    if (potr_socket_error_is(&detail, POTR_SOCKET_CAUSE_INTERRUPTED))
    {
        potr_socket_error_clear(detail_out);
        return 0;
    }
    if (detail_out != NULL)
    {
        *detail_out = detail;
    }
    return -1;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_poll_readable(PotrSocket fd, int timeout_ms, com_util_error *detail_out)
{
    WSAPOLLFD pfd;
    int r;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    r = WSAPoll(&pfd, 1, timeout_ms);
    if (r > 0 && (pfd.revents & POLLIN))
    {
        potr_socket_error_clear(detail_out);
        return 1;
    }
    if (r == 0)
    {
        potr_socket_error_clear(detail_out);
        return 0;
    }
    com_util_error detail;
    (void)potr_socket_error_report(&detail);
    if (potr_socket_error_is(&detail, POTR_SOCKET_CAUSE_INTERRUPTED))
    {
        potr_socket_error_clear(detail_out);
        return 0;
    }
    if (detail_out != NULL)
    {
        *detail_out = detail;
    }
    return -1;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_tcp_send(PotrSocket fd, const uint8_t *buf, size_t len, com_util_error *detail_out)
{
    size_t sent = 0;
    while (sent < len)
    {
        int n = send(fd, (const char *)(buf + sent), (int)(len - sent), 0);
        if (n == SOCKET_ERROR)
        {
            return potr_socket_error_report(detail_out);
        }
        if (n == 0)
        {
            potr_socket_error_clear(detail_out);
            return POTR_ERR_IO;
        }
        sent += (size_t)n;
    }
    potr_socket_error_clear(detail_out);
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_tcp_recv_all(PotrSocket fd, uint8_t *buf, size_t n, com_util_error *detail_out)
{
    size_t received = 0;
    while (received < n)
    {
        int r = recv(fd, (char *)(buf + received), (int)(n - received), 0);
        if (r == SOCKET_ERROR)
        {
            return potr_socket_error_report(detail_out);
        }
        if (r == 0)
        {
            potr_socket_error_clear(detail_out);
            return POTR_ERR_EOF;
        }
        received += (size_t)r;
    }
    potr_socket_error_clear(detail_out);
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_socket_lib_init(com_util_error *detail_out)
{
    WSADATA wsa;
    const int error_code = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (error_code == 0)
    {
        potr_socket_error_clear(detail_out);
        return POTR_OK;
    }
    /* WSAStartup は失敗コードを直接返すため、WSAGetLastError は使用しません。 */
    /* see: https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-wsastartup */
    return potr_socket_error_report_code(detail_out, (unsigned long)error_code);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_socket_lib_cleanup(void)
{
    WSACleanup();
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_set_nonblocking(PotrSocket fd, com_util_error *detail_out)
{
    u_long mode = 1;
    if (ioctlsocket(fd, FIONBIO, &mode) == 0)
    {
        potr_socket_error_clear(detail_out);
        return POTR_OK;
    }

    return potr_socket_error_report(detail_out);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_set_blocking(PotrSocket fd, com_util_error *detail_out)
{
    u_long mode = 0;
    if (ioctlsocket(fd, FIONBIO, &mode) == 0)
    {
        potr_socket_error_clear(detail_out);
        return POTR_OK;
    }

    return potr_socket_error_report(detail_out);
}

#endif /* PLATFORM_WINDOWS */
