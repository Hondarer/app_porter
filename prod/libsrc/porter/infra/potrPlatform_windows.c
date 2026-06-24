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

    #include <porter/infra/potrPlatform.h>

/* Doxygen コメントは、ヘッダーに記載 */

int potr_sendto(PotrSocket sock, const uint8_t *buf, size_t len, int flags, const struct sockaddr *dest, int dest_len)
{
    return sendto(sock, (const char *)buf, (int)len, flags, dest, dest_len);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_recvfrom(PotrSocket sock, uint8_t *buf, size_t len, int flags, struct sockaddr *src, int *src_len)
{
    return recvfrom(sock, (char *)buf, (int)len, flags, src, src_len);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_poll_writable(PotrSocket fd, int timeout_ms)
{
    WSAPOLLFD pfd;
    int r;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    r = WSAPoll(&pfd, 1, timeout_ms);
    if (r > 0 && (pfd.revents & POLLOUT))
        return 1;
    if (r == 0)
        return 0;
    return -1;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_poll_readable(PotrSocket fd, int timeout_ms)
{
    WSAPOLLFD pfd;
    int r;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    r = WSAPoll(&pfd, 1, timeout_ms);
    if (r > 0 && (pfd.revents & POLLIN))
        return 1;
    if (r == 0)
        return 0;
    return -1;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_tcp_send(PotrSocket fd, const uint8_t *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        int n = send(fd, (const char *)(buf + sent), (int)(len - sent), 0);
        if (n <= 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_tcp_recv_all(PotrSocket fd, uint8_t *buf, size_t n)
{
    size_t received = 0;
    while (received < n)
    {
        int r = recv(fd, (char *)(buf + received), (int)(n - received), 0);
        if (r == SOCKET_ERROR)
            return -1;
        if (r == 0)
            return 0;
        received += (size_t)r;
    }
    return 1;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_socket_lib_init(void)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0)
    {
        return 0;
    }

    return -1;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_socket_lib_cleanup(void)
{
    WSACleanup();
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_set_nonblocking(PotrSocket fd)
{
    u_long mode = 1;
    if (ioctlsocket(fd, FIONBIO, &mode) == 0)
    {
        return 0;
    }

    return -1;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_set_blocking(PotrSocket fd)
{
    u_long mode = 0;
    if (ioctlsocket(fd, FIONBIO, &mode) == 0)
    {
        return 0;
    }

    return -1;
}

#endif /* PLATFORM_WINDOWS */
