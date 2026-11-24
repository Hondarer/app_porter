#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
    #include <winsock2.h>
    #include <mock_winsock.h>
#elif defined(PLATFORM_LINUX)
    #include <errno.h>
    #include <mock_poll.h>
    #include <mock_unistd.h>
    #include <sys/mock_socket.h>
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/socket.h>
#include <porter/porter_result.h>
#include <porter/potr_context.h>
#include <porter/thread/potr_connect_thread.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <thread>

using namespace testing;

// OS の起床通知 (Windows: WSASetEvent / Linux: eventfd write) が失敗しても、
// 無期限の受け付け待機が 50 ms の内部確認により 1 秒以内に回収され、detach -> close -> cancel dispose の順で終わることの確認
TEST(porterCancelFailureTest, indefinite_accept_wait_recovers_within_1s_when_os_signal_fails)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
#if defined(PLATFORM_WINDOWS)
    NiceMock<Mock_winsock> mock_winsock;
#elif defined(PLATFORM_LINUX)
    NiceMock<Mock_unistd> mock_unistd;
    NiceMock<Mock_poll> mock_poll;
#endif

    cplat_error detail = {};
    cplat_socket listen_fd = CPLAT_INVALID_SOCKET;
    ASSERT_EQ(CPLAT_OK, cplat_socket_open(CPLAT_SOCKET_TCP, &listen_fd, &detail));
    cplat_ipv4_endpoint bind_ep = {};
    bind_ep.address = cplat_hton32(0x7F000001U);
    bind_ep.port = 0U;
    ASSERT_EQ(CPLAT_OK, cplat_socket_bind(listen_fd, &bind_ep, &detail));
    ASSERT_EQ(CPLAT_OK, cplat_socket_listen(listen_fd, 4, &detail));

    cplat_socket_watch *listen_watch = nullptr;
    ASSERT_EQ(CPLAT_OK, cplat_socket_watch_attach(listen_fd, &listen_watch, &detail));
    cplat_socket_wait_cancel *connect_cancel = nullptr;
    ASSERT_EQ(CPLAT_OK, cplat_socket_wait_cancel_create(&connect_cancel, &detail));

    potr_context ctx = {};
    for (int i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        ctx.sock[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
    }
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.service.type = POTR_TYPE_TCP;
    ctx.service.service_id = 9001;
    ctx.n_path = 1;
    ctx.global.max_payload = 64U;
    ctx.tcp_listen_sock[0] = listen_fd;
    ctx.tcp_listen_watch[0] = listen_watch;
    ctx.tcp_connect_cancel[0] = connect_cancel;
    ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&ctx.tcp_state_mutex));
    ASSERT_EQ(CPLAT_OK, cplat_condvar_create(&ctx.tcp_state_cv));
    ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&ctx.tcp_lifecycle_mutex[0]));
    ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&ctx.tcp_send_mutex[0]));

    std::promise<void> wait_entered;
    std::atomic<bool> wait_signaled(false);

#if defined(PLATFORM_WINDOWS)
    ON_CALL(mock_winsock, WSAWaitForMultipleEvents(_, _, _, _, _, _, _, _))
        .WillByDefault([&](const char *file, int line, const char *func, DWORD cEvents, const WSAEVENT *lphEvents,
                           BOOL fWaitAll, DWORD dwTimeout, BOOL fAlertable) {
            if (!wait_signaled.exchange(true))
            {
                wait_entered.set_value();
            }
            return delegate_real_WSAWaitForMultipleEvents(file, line, func, cEvents, lphEvents, fWaitAll, dwTimeout,
                                                          fAlertable);
        });
    EXPECT_CALL(mock_winsock, WSASetEvent(_, _, _, _)).Times(AtLeast(1)).WillRepeatedly(Return(FALSE));
    ON_CALL(mock_winsock, WSAGetLastError).WillByDefault(Return(WSAEINVAL));
#elif defined(PLATFORM_LINUX)
    ON_CALL(mock_poll, poll(_, _, _, _, _, _))
        .WillByDefault([&](const char *file, int line, const char *func, struct pollfd *fds, nfds_t nfds, int timeout) {
            if (!wait_signaled.exchange(true))
            {
                wait_entered.set_value();
            }
            return delegate_real_poll(file, line, func, fds, nfds, timeout);
        });
    ON_CALL(mock_unistd, write(_, _, _, _, _, _))
        .WillByDefault([](const char *file, int line, const char *func, int fd, const void *buf, size_t count) {
            if (count == sizeof(uint64_t))
            {
                errno = EIO;
                return (ssize_t)-1;
            }
            return delegate_real_write(file, line, func, fd, buf, count);
        });
#endif

    // Act
    ASSERT_EQ(POTR_OK, potr_internal_connect_thread_start(&ctx));
    ASSERT_EQ(std::future_status::ready, wait_entered.get_future().wait_for(std::chrono::seconds(2)));

    const auto start = std::chrono::steady_clock::now();
    const int sig_ret = cplat_socket_wait_cancel_signal(connect_cancel, &detail);
    potr_internal_connect_thread_stop(&ctx);
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    // Assert
    EXPECT_EQ(CPLAT_OK, sig_ret); // [確認_異常系] - OS 通知失敗でも cancel_signal は CPLAT_OK を返すこと。
    EXPECT_LT(elapsed_ms, 1000);  // [確認_異常系] - 50 ms の内部確認により 1 秒以内に回収されること。
    EXPECT_EQ(nullptr, ctx.connect_thread[0]);

    // Cleanup (detach -> close -> cancel dispose の順序)
    cplat_socket_watch_detach(listen_watch);
    cplat_socket_close(listen_fd);
    cplat_socket_wait_cancel_dispose(connect_cancel);
    cplat_local_lock_dispose(ctx.tcp_send_mutex[0]);
    cplat_local_lock_dispose(ctx.tcp_lifecycle_mutex[0]);
    cplat_condvar_dispose(ctx.tcp_state_cv);
    cplat_local_lock_dispose(ctx.tcp_state_mutex);
}

// OS の起床通知が失敗しても、無期限の connect 待機 (connect_timeout_ms == 0) が
// 50 ms の内部確認により 1 秒以内に回収されることの確認
TEST(porterCancelFailureTest, indefinite_connect_wait_recovers_within_1s_when_os_signal_fails)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
#if defined(PLATFORM_WINDOWS)
    NiceMock<Mock_winsock> mock_winsock;
#elif defined(PLATFORM_LINUX)
    NiceMock<Mock_unistd> mock_unistd;
    NiceMock<Mock_poll> mock_poll;
    NiceMock<Mock_socket> mock_socket;
#endif

    cplat_error detail = {};
    cplat_socket_wait_cancel *connect_cancel = nullptr;
    ASSERT_EQ(CPLAT_OK, cplat_socket_wait_cancel_create(&connect_cancel, &detail));

    potr_context ctx = {};
    for (int i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        ctx.sock[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
    }
    ctx.role = POTR_ROLE_SENDER;
    ctx.service.type = POTR_TYPE_TCP;
    ctx.service.service_id = 9002;
    ctx.service.dst_port = 19999U;
    ctx.service.connect_timeout_ms = 0U; /* 無期限の connect 待機 */
    ctx.service.reconnect_interval_ms = 1000U;
    ctx.global.max_payload = 64U;
    ctx.n_path = 1;
    ctx.dst_addr_resolved[0] = cplat_hton32(0x7F000001U);
    ctx.tcp_connect_cancel[0] = connect_cancel;
    ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&ctx.tcp_state_mutex));
    ASSERT_EQ(CPLAT_OK, cplat_condvar_create(&ctx.tcp_state_cv));
    ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&ctx.tcp_lifecycle_mutex[0]));
    ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&ctx.tcp_send_mutex[0]));

    std::promise<void> wait_entered;
    std::atomic<bool> wait_signaled(false);

#if defined(PLATFORM_WINDOWS)
    ON_CALL(mock_winsock, connect(_, _, _, _, _, _))
        .WillByDefault([](const char *, int, const char *, SOCKET, const struct sockaddr *, int) {
            WSASetLastError(WSAEWOULDBLOCK);
            return SOCKET_ERROR;
        });
    ON_CALL(mock_winsock, WSAWaitForMultipleEvents(_, _, _, _, _, _, _, _))
        .WillByDefault([&](const char *file, int line, const char *func, DWORD cEvents, const WSAEVENT *lphEvents,
                           BOOL fWaitAll, DWORD dwTimeout, BOOL fAlertable) {
            if (!wait_signaled.exchange(true))
            {
                wait_entered.set_value();
            }
            return delegate_real_WSAWaitForMultipleEvents(file, line, func, cEvents, lphEvents, fWaitAll, dwTimeout,
                                                          fAlertable);
        });
    EXPECT_CALL(mock_winsock, WSASetEvent(_, _, _, _)).Times(AtLeast(1)).WillRepeatedly(Return(FALSE));
#elif defined(PLATFORM_LINUX)
    ON_CALL(mock_socket, connect(_, _, _, _, _, _))
        .WillByDefault([](const char *, int, const char *, int, const struct sockaddr *, socklen_t) {
            errno = EINPROGRESS;
            return -1;
        });
    ON_CALL(mock_poll, poll(_, _, _, _, _, _))
        .WillByDefault([&](const char *file, int line, const char *func, struct pollfd *fds, nfds_t nfds, int timeout) {
            if (!wait_signaled.exchange(true))
            {
                wait_entered.set_value();
            }
            return delegate_real_poll(file, line, func, fds, nfds, timeout);
        });
    ON_CALL(mock_unistd, write(_, _, _, _, _, _))
        .WillByDefault([](const char *file, int line, const char *func, int fd, const void *buf, size_t count) {
            if (count == sizeof(uint64_t))
            {
                errno = EIO;
                return (ssize_t)-1;
            }
            return delegate_real_write(file, line, func, fd, buf, count);
        });
#endif

    // Act
    ASSERT_EQ(POTR_OK, potr_internal_connect_thread_start(&ctx));
    ASSERT_EQ(std::future_status::ready, wait_entered.get_future().wait_for(std::chrono::seconds(2)));

    const auto start = std::chrono::steady_clock::now();
    const int sig_ret = cplat_socket_wait_cancel_signal(connect_cancel, &detail);
    potr_internal_connect_thread_stop(&ctx);
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    // Assert
    EXPECT_EQ(CPLAT_OK, sig_ret); // [確認_異常系] - OS 通知失敗でも cancel_signal は CPLAT_OK を返すこと。
    EXPECT_LT(elapsed_ms, 1000);  // [確認_異常系] - 無期限 connect も 1 秒以内に回収されること。
    EXPECT_EQ(nullptr, ctx.connect_thread[0]);

    // Cleanup
    cplat_socket_wait_cancel_dispose(connect_cancel);
    cplat_local_lock_dispose(ctx.tcp_send_mutex[0]);
    cplat_local_lock_dispose(ctx.tcp_lifecycle_mutex[0]);
    cplat_condvar_dispose(ctx.tcp_state_cv);
    cplat_local_lock_dispose(ctx.tcp_state_mutex);
}

// 通知前の待機開始直前・通知後の待機開始でも OS 通知失敗時に即時解除されることの確認
TEST(porterCancelFailureTest, pre_signaled_cancel_with_os_signal_failure_returns_canceled_immediately)
{
    // Arrange
#if defined(PLATFORM_WINDOWS)
    NiceMock<Mock_winsock> mock_winsock;
    EXPECT_CALL(mock_winsock, WSASetEvent(_, _, _, _)).Times(AtLeast(1)).WillRepeatedly(Return(FALSE));
    ON_CALL(mock_winsock, WSAGetLastError).WillByDefault(Return(WSAEINVAL));
#elif defined(PLATFORM_LINUX)
    NiceMock<Mock_unistd> mock_unistd;
    EXPECT_CALL(mock_unistd, write(_, _, _, _, _, _))
        .WillOnce([](const char *, int, const char *, int, const void *, size_t) {
            errno = EIO;
            return (ssize_t)-1;
        });
#endif

    cplat_error detail = {};
    cplat_socket sock = CPLAT_INVALID_SOCKET;
    ASSERT_EQ(CPLAT_OK, cplat_socket_open(CPLAT_SOCKET_TCP, &sock, &detail));
    cplat_socket_watch *watch = nullptr;
    ASSERT_EQ(CPLAT_OK, cplat_socket_watch_attach(sock, &watch, &detail));
    cplat_socket_wait_cancel *cancel = nullptr;
    ASSERT_EQ(CPLAT_OK, cplat_socket_wait_cancel_create(&cancel, &detail));

    // Act
    const int sig_ret = cplat_socket_wait_cancel_signal(cancel, &detail);
    cplat_socket_watch *watches[1] = {watch};
    unsigned char ready[1] = {1U};
    const auto start = std::chrono::steady_clock::now();
    const int wait_ret =
        cplat_socket_watch_wait_readable_multi(watches, 1U, cancel, CPLAT_WAIT_FOREVER, ready, &detail);
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    // Assert
    EXPECT_EQ(CPLAT_OK, sig_ret);
    EXPECT_EQ(CPLAT_ERR_CANCELED, wait_ret);
    EXPECT_EQ(0U, ready[0]);
    EXPECT_LT(elapsed_ms, 500);

    // Cleanup
    cplat_socket_watch_detach(watch);
    cplat_socket_close(sock);
    cplat_socket_wait_cancel_dispose(cancel);
}
