/**
 *******************************************************************************
 *  @file           potrPlatform_linux.c
 *  @brief          プラットフォームの差異を抽象化する Linux 向け機能を実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/04/18
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>

#if defined(PLATFORM_LINUX)

    #include <fcntl.h>

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
    if (bind(sock, (const struct sockaddr *)addr, sizeof(*addr)) < 0)
    {
        return potr_socket_error_report(detail_out);
    }
    potr_socket_error_clear(detail_out);
    return POTR_OK;
}

int potr_listen(const PotrSocket sock, const int backlog, com_util_error *detail_out)
{
    if (listen(sock, backlog) < 0)
    {
        return potr_socket_error_report(detail_out);
    }
    potr_socket_error_clear(detail_out);
    return POTR_OK;
}

int potr_accept(const PotrSocket sock, struct sockaddr_in *addr, PotrSocket *sock_out, com_util_error *detail_out)
{
    socklen_t addr_len = sizeof(*addr);
    PotrSocket accepted;

    if ((addr == NULL) || (sock_out == NULL))
    {
        return potr_socket_error_report_invalid_argument(detail_out);
    }
    accepted = accept(sock, (struct sockaddr *)addr, &addr_len);
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
    if (connect(sock, (const struct sockaddr *)addr, sizeof(*addr)) < 0)
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
    if (setsockopt(sock, level, optname, optval, (socklen_t)optlen) < 0)
    {
        return potr_socket_error_report(detail_out);
    }
    potr_socket_error_clear(detail_out);
    return POTR_OK;
}

int potr_socket_get_pending_error(const PotrSocket sock, com_util_error *detail_out)
{
    int pending_error = 0;
    socklen_t length = sizeof(pending_error);

    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &pending_error, &length) < 0)
    {
        return potr_socket_error_report(detail_out);
    }
    if (pending_error != 0)
    {
        return potr_socket_error_report_code(detail_out, (unsigned long)pending_error);
    }
    potr_socket_error_clear(detail_out);
    return POTR_OK;
}

int potr_poll_readable_multi(const PotrSocket *fds, const size_t count, const int timeout_ms, unsigned char *ready_out,
                             com_util_error *detail_out)
{
    struct pollfd poll_fds[POTR_MAX_PATH];
    size_t index;
    nfds_t valid_count = 0;
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
    poll_result = poll(poll_fds, valid_count, timeout_ms);
    if (poll_result < 0)
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
    valid_count = 0U;
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
    if (sock_inout == NULL)
    {
        return potr_socket_error_report_invalid_argument(detail_out);
    }
    if (shutdown(*sock_inout, SHUT_RD) < 0)
    {
        return potr_socket_error_report(detail_out);
    }
    potr_socket_error_clear(detail_out);
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_sendto(PotrSocket sock, const uint8_t *buf, size_t len, int flags, const struct sockaddr *dest, int dest_len,
                com_util_error *detail_out)
{
    const int result = (int)sendto(sock, buf, len, flags, dest, (socklen_t)dest_len);
    if (result < 0)
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
        return -1;
    }
    socklen_t sl = (socklen_t)*src_len;
    int n = (int)recvfrom(sock, buf, len, flags, src, &sl);
    *src_len = (int)sl;
    if (n < 0)
    {
        (void)potr_socket_error_report(detail_out);
    }
    else
    {
        potr_socket_error_clear(detail_out);
    }
    return n;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_poll_writable(PotrSocket fd, int timeout_ms, com_util_error *detail_out)
{
    struct pollfd pfd;
    int r;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    r = poll(&pfd, 1, timeout_ms);
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
    struct pollfd pfd;
    int r;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    r = poll(&pfd, 1, timeout_ms);
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
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0)
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
        ssize_t r = recv(fd, buf + received, n - received, 0);
        if (r < 0)
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
    potr_socket_error_clear(detail_out);
    return POTR_OK;
}
void potr_socket_lib_cleanup(void) {}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_set_nonblocking(PotrSocket fd, com_util_error *detail_out)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return potr_socket_error_report(detail_out);
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        return potr_socket_error_report(detail_out);
    }

    potr_socket_error_clear(detail_out);
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_set_blocking(PotrSocket fd, com_util_error *detail_out)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return potr_socket_error_report(detail_out);
    if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0)
    {
        return potr_socket_error_report(detail_out);
    }

    potr_socket_error_clear(detail_out);
    return POTR_OK;
}

#elif defined(PLATFORM_WINDOWS) && defined(COMPILER_MSVC)
    #pragma warning(disable : 4206)
#endif
