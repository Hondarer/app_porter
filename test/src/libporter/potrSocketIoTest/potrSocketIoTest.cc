#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <porter/porter_result.h>
#include <porter/protocol/packet.h>
#include <atomic>
#include <cstring>
#include <future>
#include <thread>
#include <vector>

using namespace testing;

namespace
{
void init_udp_ctx(potr_context *ctx, int *watch_tok, int *cancel_tok)
{
    std::memset(ctx, 0, sizeof(*ctx));
    for (int i = 0; i < POTR_MAX_PATH; i++)
    {
        ctx->sock[i] = CPLAT_INVALID_SOCKET;
    }
    ctx->n_path = 1;
    ctx->sock[0] = static_cast<cplat_socket>(10);
    ctx->udp_watch[0] = reinterpret_cast<cplat_socket_watch *>(watch_tok);
    ctx->recv_cancel = reinterpret_cast<cplat_socket_wait_cancel *>(cancel_tok);
    delegate_real_cplat_local_lock_create(&ctx->udp_send_mutex[0]);
}

void dispose_udp_ctx(potr_context *ctx)
{
    if (ctx->udp_send_mutex[0] != nullptr)
    {
        delegate_real_cplat_local_lock_dispose(ctx->udp_send_mutex[0]);
        ctx->udp_send_mutex[0] = nullptr;
    }
}

void init_tcp_ctx(potr_context *ctx, int *watch_tok, int *cancel_tok, uint64_t generation)
{
    std::memset(ctx, 0, sizeof(*ctx));
    for (int i = 0; i < POTR_MAX_PATH; i++)
    {
        ctx->tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
    }
    ctx->n_path = 1;
    ctx->tcp_conn_fd[0] = static_cast<cplat_socket>(20);
    ctx->tcp_conn_watch[0] = reinterpret_cast<cplat_socket_watch *>(watch_tok);
    ctx->tcp_conn_cancel[0] = reinterpret_cast<cplat_socket_wait_cancel *>(cancel_tok);
    ctx->tcp_conn_generation[0] = generation;
    delegate_real_cplat_local_lock_create(&ctx->tcp_send_mutex[0]);
    delegate_real_cplat_local_lock_create(&ctx->tcp_lifecycle_mutex[0]);
    delegate_real_cplat_local_lock_create(&ctx->tcp_state_mutex);
}

void dispose_tcp_ctx(potr_context *ctx)
{
    if (ctx->tcp_send_mutex[0] != nullptr)
    {
        delegate_real_cplat_local_lock_dispose(ctx->tcp_send_mutex[0]);
        ctx->tcp_send_mutex[0] = nullptr;
    }
    if (ctx->tcp_lifecycle_mutex[0] != nullptr)
    {
        delegate_real_cplat_local_lock_dispose(ctx->tcp_lifecycle_mutex[0]);
        ctx->tcp_lifecycle_mutex[0] = nullptr;
    }
    if (ctx->tcp_state_mutex != nullptr)
    {
        delegate_real_cplat_local_lock_dispose(ctx->tcp_state_mutex);
        ctx->tcp_state_mutex = nullptr;
    }
}
} // namespace

// UDP 送信が 1 回目で全量成功することの確認
TEST(potrSocketIoTest, udp_sendto_immediate_success)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int watch_tok = 0;
    int cancel_tok = 0;
    init_udp_ctx(&ctx, &watch_tok, &cancel_tok);
    const uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    cplat_ipv4_endpoint ep = {};
    ep.address = 0x0100007FU;
    ep.port = 1234U;
    cplat_error detail = {};

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto(ctx.udp_watch[0], payload, sizeof(payload), &ep, _, _))
        .WillOnce(DoAll(SetArgPointee<4>(sizeof(payload)), Return(CPLAT_OK)));
    EXPECT_CALL(mock_cplat, cplat_socket_watch_wait_writable(_, _, _, _)).Times(0);

    // Act
    int actual_ret = potr_internal_udp_sendto(&ctx, 0, payload, sizeof(payload), &ep, &detail);

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 即時全量送信が POTR_OK であること。
    dispose_udp_ctx(&ctx);
}

// UDP 送信で WOULD_BLOCK 後に ready -> 再度 WOULD_BLOCK が返っても 100 ms 期限内で再試行し成功することの確認
TEST(potrSocketIoTest, udp_sendto_retries_would_block_within_deadline)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int watch_tok = 0;
    int cancel_tok = 0;
    init_udp_ctx(&ctx, &watch_tok, &cancel_tok);
    const uint8_t payload[4] = {1, 2, 3, 4};
    cplat_ipv4_endpoint ep = {};
    uint64_t now_ms = 1000U;
    std::vector<int> wait_timeouts;

    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_))
        .WillByDefault(
            [&](uint64_t *out)
            {
                *out = now_ms;
                return CPLAT_OK;
            });

    // Pre-Assert
    {
        InSequence seq;
        EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto(ctx.udp_watch[0], payload, sizeof(payload), &ep, _, _))
            .WillOnce(Return(CPLAT_ERR_WOULD_BLOCK));
        EXPECT_CALL(mock_cplat, cplat_socket_watch_wait_writable(ctx.udp_watch[0], ctx.recv_cancel, _, _))
            .WillOnce(
                [&](cplat_socket_watch *, cplat_socket_wait_cancel *, int timeout_ms, cplat_error *)
                {
                    wait_timeouts.push_back(timeout_ms);
                    now_ms += 40U;
                    return CPLAT_OK;
                });
        EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto(ctx.udp_watch[0], payload, sizeof(payload), &ep, _, _))
            .WillOnce(Return(CPLAT_ERR_WOULD_BLOCK));
        EXPECT_CALL(mock_cplat, cplat_socket_watch_wait_writable(ctx.udp_watch[0], ctx.recv_cancel, _, _))
            .WillOnce(
                [&](cplat_socket_watch *, cplat_socket_wait_cancel *, int timeout_ms, cplat_error *)
                {
                    wait_timeouts.push_back(timeout_ms);
                    now_ms += 30U;
                    return CPLAT_OK;
                });
        EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto(ctx.udp_watch[0], payload, sizeof(payload), &ep, _, _))
            .WillOnce(DoAll(SetArgPointee<4>(sizeof(payload)), Return(CPLAT_OK)));
    }

    // Act
    int actual_ret = potr_internal_udp_sendto(&ctx, 0, payload, sizeof(payload), &ep, nullptr);

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);                    // [確認_正常系] - 再度の WOULD_BLOCK 後も期限内なら成功すること。
    EXPECT_EQ((std::vector<int>{100, 60}), wait_timeouts); // [確認_正常系] - 100 ms の絶対期限を延長せず残り時間で待つこと。
    dispose_udp_ctx(&ctx);
}

// UDP 送信で 100 ms の絶対期限を超過した場合に POTR_ERR_TIMEOUT を返すことの確認
TEST(potrSocketIoTest, udp_sendto_deadline_timeout)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int watch_tok = 0;
    int cancel_tok = 0;
    init_udp_ctx(&ctx, &watch_tok, &cancel_tok);
    const uint8_t payload[4] = {1, 2, 3, 4};
    cplat_ipv4_endpoint ep = {};
    uint64_t now_ms = 5000U;

    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_))
        .WillByDefault(
            [&](uint64_t *out)
            {
                *out = now_ms;
                return CPLAT_OK;
            });

    // Pre-Assert
    {
        InSequence seq;
        EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto(_, _, _, _, _, _)).WillOnce(Return(CPLAT_ERR_WOULD_BLOCK));
        EXPECT_CALL(mock_cplat, cplat_socket_watch_wait_writable(_, ctx.recv_cancel, 100, _))
            .WillOnce(
                [&](cplat_socket_watch *, cplat_socket_wait_cancel *, int, cplat_error *)
                {
                    now_ms += 100U;
                    return CPLAT_ERR_TIMEOUT;
                });
    }

    // Act
    int actual_ret = potr_internal_udp_sendto(&ctx, 0, payload, sizeof(payload), &ep, nullptr);

    // Assert
    EXPECT_EQ(POTR_ERR_TIMEOUT, actual_ret); // [確認_異常系] - 期限超過で POTR_ERR_TIMEOUT を返すこと。
    dispose_udp_ctx(&ctx);
}

// UDP 送信で待機が解除された場合に POTR_ERR_CANCELED を返すことの確認
TEST(potrSocketIoTest, udp_sendto_canceled)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int watch_tok = 0;
    int cancel_tok = 0;
    init_udp_ctx(&ctx, &watch_tok, &cancel_tok);
    const uint8_t payload[4] = {1, 2, 3, 4};
    cplat_ipv4_endpoint ep = {};

    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault(DoAll(SetArgPointee<0>(1000U), Return(CPLAT_OK)));
    EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto(_, _, _, _, _, _)).WillOnce(Return(CPLAT_ERR_WOULD_BLOCK));
    EXPECT_CALL(mock_cplat, cplat_socket_watch_wait_writable(_, ctx.recv_cancel, _, _))
        .WillOnce(Return(CPLAT_ERR_CANCELED));

    // Act
    int actual_ret = potr_internal_udp_sendto(&ctx, 0, payload, sizeof(payload), &ep, nullptr);

    // Assert
    EXPECT_EQ(POTR_ERR_CANCELED, actual_ret); // [確認_異常系] - 解除通知で POTR_ERR_CANCELED を返すこと。
    dispose_udp_ctx(&ctx);
}

// UDP で短い送信が発生した場合に POTR_ERR_IO とし再送しないことの確認
TEST(potrSocketIoTest, udp_sendto_short_send_returns_io)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int watch_tok = 0;
    int cancel_tok = 0;
    init_udp_ctx(&ctx, &watch_tok, &cancel_tok);
    const uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    cplat_ipv4_endpoint ep = {};

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto(_, _, sizeof(payload), _, _, _))
        .WillOnce(DoAll(SetArgPointee<4>(4U), Return(CPLAT_OK)));

    // Act
    int actual_ret = potr_internal_udp_sendto(&ctx, 0, payload, sizeof(payload), &ep, nullptr);

    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 短い送信を POTR_ERR_IO として扱うこと。
    dispose_udp_ctx(&ctx);
}

// TCP 送信で部分送信・待機・残量送信を経て全量送信に成功することの確認
TEST(potrSocketIoTest, tcp_send_all_partial_then_complete)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int watch_tok = 0;
    int cancel_tok = 0;
    init_tcp_ctx(&ctx, &watch_tok, &cancel_tok, 1U);
    const uint8_t payload[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    size_t sent = 0U;

    // Pre-Assert
    {
        InSequence seq;
        EXPECT_CALL(mock_cplat, cplat_socket_watch_send(ctx.tcp_conn_watch[0], payload, 10U, _, _))
            .WillOnce(DoAll(SetArgPointee<3>(4U), Return(CPLAT_OK)));
        EXPECT_CALL(mock_cplat, cplat_socket_watch_send(ctx.tcp_conn_watch[0], payload + 4, 6U, _, _))
            .WillOnce(Return(CPLAT_ERR_WOULD_BLOCK));
        EXPECT_CALL(mock_cplat,
                    cplat_socket_watch_wait_writable(ctx.tcp_conn_watch[0], ctx.tcp_conn_cancel[0], 100, _))
            .WillOnce(Return(CPLAT_OK));
        EXPECT_CALL(mock_cplat, cplat_socket_watch_send(ctx.tcp_conn_watch[0], payload + 4, 6U, _, _))
            .WillOnce(DoAll(SetArgPointee<3>(6U), Return(CPLAT_OK)));
    }

    // Act
    int actual_ret = potr_internal_tcp_send_all(&ctx, 0, payload, sizeof(payload), &sent, nullptr);

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 全量送信が成功すること。
    EXPECT_EQ(10U, sent);           // [確認_正常系] - 送信済みバイト数が 10 であること。
    dispose_tcp_ctx(&ctx);
}

// TCP の送信可能待機が CPLAT_OK を返した後の再送信が切断エラーなら失敗扱いとし閉じる要求を発行することの確認
TEST(potrSocketIoTest, tcp_send_all_writable_ok_followed_by_disconnect_requests_close)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int watch_tok = 0;
    int cancel_tok = 0;
    init_tcp_ctx(&ctx, &watch_tok, &cancel_tok, 3U);
    const uint8_t payload[10] = {};
    size_t sent = 0U;
    cplat_error reset_err = {};
    reset_err.result = CPLAT_ERR_UNKNOWN;
    ON_CALL(mock_cplat, cplat_error_get_cause(_)).WillByDefault(Return(CPLAT_CAUSE_CONNECTION_RESET));

    // Pre-Assert
    {
        InSequence seq;
        EXPECT_CALL(mock_cplat, cplat_socket_watch_send(ctx.tcp_conn_watch[0], payload, 10U, _, _))
            .WillOnce(DoAll(SetArgPointee<3>(3U), Return(CPLAT_OK)));
        EXPECT_CALL(mock_cplat, cplat_socket_watch_send(ctx.tcp_conn_watch[0], payload + 3, 7U, _, _))
            .WillOnce(Return(CPLAT_ERR_WOULD_BLOCK));
        EXPECT_CALL(mock_cplat, cplat_socket_watch_wait_writable(ctx.tcp_conn_watch[0], ctx.tcp_conn_cancel[0], 100, _))
            .WillOnce(Return(CPLAT_OK));
        EXPECT_CALL(mock_cplat, cplat_socket_watch_send(ctx.tcp_conn_watch[0], payload + 3, 7U, _, _))
            .WillOnce(DoAll(SetArgPointee<4>(reset_err), Return(CPLAT_ERR_UNKNOWN)));
        EXPECT_CALL(mock_cplat, cplat_socket_wait_cancel_signal(ctx.tcp_conn_cancel[0], _)).WillOnce(Return(CPLAT_OK));
    }

    // Act
    int actual_ret = potr_internal_tcp_send_all(&ctx, 0, payload, sizeof(payload), &sent, nullptr);

    // Assert
    EXPECT_EQ(POTR_ERR_DISCONNECTED, actual_ret); // [確認_異常系] - 再送信の切断を返すこと。
    EXPECT_EQ(3U, sent);                          // [確認_異常系] - 途中の送信済みバイト数を保持すること。
    EXPECT_EQ(1, ctx.tcp_conn_closing[0]);        // [確認_異常系] - 対象世代へ閉じる要求を発行したこと。
    dispose_tcp_ctx(&ctx);
}

// TCP 送信の途中で後から close 期限が公開された場合、次の待機スライスで検出し期限超過で打ち切ることの確認
TEST(potrSocketIoTest, tcp_send_all_observes_later_published_close_deadline)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int watch_tok = 0;
    int cancel_tok = 0;
    init_tcp_ctx(&ctx, &watch_tok, &cancel_tok, 5U);
    ctx.global.tcp_close_timeout_ms = 50U;
    const uint8_t payload[8] = {};
    size_t sent = 0U;
    uint64_t now_ms = 2000U;

    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_))
        .WillByDefault(
            [&](uint64_t *out)
            {
                *out = now_ms;
                return CPLAT_OK;
            });

    // Pre-Assert
    {
        InSequence seq;
        EXPECT_CALL(mock_cplat, cplat_socket_watch_send(ctx.tcp_conn_watch[0], payload, 8U, _, _))
            .WillOnce(Return(CPLAT_ERR_WOULD_BLOCK));
        EXPECT_CALL(mock_cplat, cplat_socket_watch_wait_writable(ctx.tcp_conn_watch[0], ctx.tcp_conn_cancel[0], 100, _))
            .WillOnce(
                [&](cplat_socket_watch *, cplat_socket_wait_cancel *, int, cplat_error *)
                {
                    /* 1 回目の 100 ms 待機中に close 期限を公開し、30 ms 進める。 */
                    potr_internal_tcp_close_deadline_publish(&ctx);
                    now_ms += 30U;
                    return CPLAT_ERR_TIMEOUT;
                });
        EXPECT_CALL(mock_cplat, cplat_socket_watch_send(ctx.tcp_conn_watch[0], payload, 8U, _, _))
            .WillOnce(Return(CPLAT_ERR_WOULD_BLOCK));
        EXPECT_CALL(mock_cplat, cplat_socket_watch_wait_writable(ctx.tcp_conn_watch[0], ctx.tcp_conn_cancel[0], 20, _))
            .WillOnce(
                [&](cplat_socket_watch *, cplat_socket_wait_cancel *, int, cplat_error *)
                {
                    now_ms += 25U;
                    return CPLAT_ERR_TIMEOUT;
                });
        EXPECT_CALL(mock_cplat, cplat_socket_wait_cancel_signal(ctx.tcp_conn_cancel[0], _)).WillOnce(Return(CPLAT_OK));
    }

    // Act
    int actual_ret = potr_internal_tcp_send_all(&ctx, 0, payload, sizeof(payload), &sent, nullptr);

    // Assert
    EXPECT_EQ(POTR_ERR_TIMEOUT, actual_ret); // [確認_異常系] - 後から公開された close 期限超過で POTR_ERR_TIMEOUT を返すこと。
    EXPECT_EQ(0U, sent);                     // [確認_異常系] - 未送信のまま打ち切ること。
    dispose_tcp_ctx(&ctx);
}

// 送信用ロック保持中の送信待機に対して、閉じる要求が送信用ロックを取らず signal を出し、送信完了後にだけ release が進むことの確認
TEST(potrSocketIoTest, tcp_request_close_does_not_wait_for_send_mutex_and_release_waits)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int watch_tok = 0;
    int cancel_tok = 0;
    init_tcp_ctx(&ctx, &watch_tok, &cancel_tok, 7U);
    const uint8_t payload[8] = {};

    std::promise<void> sender_in_wait;
    std::promise<void> cancel_signaled;
    std::shared_future<void> cancel_future = cancel_signaled.get_future().share();
    std::atomic<bool> detached(false);
    std::atomic<bool> sender_done(false);

    EXPECT_CALL(mock_cplat, cplat_socket_watch_send(ctx.tcp_conn_watch[0], _, _, _, _))
        .WillOnce(Return(CPLAT_ERR_WOULD_BLOCK));
    EXPECT_CALL(mock_cplat, cplat_socket_watch_wait_writable(ctx.tcp_conn_watch[0], ctx.tcp_conn_cancel[0], _, _))
        .WillOnce(
            [&](cplat_socket_watch *, cplat_socket_wait_cancel *, int, cplat_error *)
            {
                sender_in_wait.set_value();
                cancel_future.wait();
                return CPLAT_ERR_CANCELED;
            });
    EXPECT_CALL(mock_cplat, cplat_socket_wait_cancel_signal(ctx.tcp_conn_cancel[0], _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            [&](cplat_socket_wait_cancel *, cplat_error *)
            {
                /* 送信スレッドが送信用ロックを保持したまま待っている間に呼ばれる。 */
                static std::atomic<bool> once(false);
                if (!once.exchange(true))
                {
                    cancel_signaled.set_value();
                }
                return CPLAT_OK;
            });
    EXPECT_CALL(mock_cplat, cplat_socket_watch_detach(ctx.tcp_conn_watch[0]))
        .WillOnce(
            [&](cplat_socket_watch *)
            {
                EXPECT_TRUE(sender_done.load());
                detached.store(true);
            });
    EXPECT_CALL(mock_cplat, cplat_socket_close(ctx.tcp_conn_fd[0])).WillOnce(Return());
    EXPECT_CALL(mock_cplat, cplat_socket_wait_cancel_dispose(ctx.tcp_conn_cancel[0])).WillOnce(Return());

    // Act
    int send_ret = POTR_OK;
    std::thread sender(
        [&]()
        {
            send_ret = potr_internal_tcp_send_all(&ctx, 0, payload, sizeof(payload), nullptr, nullptr);
            sender_done.store(true);
        });

    sender_in_wait.get_future().wait();
    /* 古い世代 (6) への閉じる要求は新世代 (7) を解除しないこと。 */
    potr_internal_tcp_conn_request_close(&ctx, 0, 6U);
    EXPECT_EQ(0, ctx.tcp_conn_closing[0]);

    /* 現行世代 (7) への閉じる要求は送信用ロックを待たず即座に signal を発行すること。 */
    potr_internal_tcp_conn_request_close(&ctx, 0, 7U);
    sender.join();
    potr_internal_tcp_conn_release(&ctx, 0, 7U);

    // Assert
    EXPECT_EQ(POTR_ERR_CANCELED, send_ret);              // [確認_異常系] - 送信スレッドが解除で復帰すること。
    EXPECT_TRUE(detached.load());                        // [確認_正常系] - 送信終了後にだけ detach されること。
    EXPECT_EQ(nullptr, ctx.tcp_conn_watch[0]);           // [確認_正常系] - 公開 watch が NULL になること。
    EXPECT_EQ(CPLAT_INVALID_SOCKET, ctx.tcp_conn_fd[0]); // [確認_正常系] - 公開ソケットが無効値になること。
    dispose_tcp_ctx(&ctx);
}

// TCP フレーム読み取りがヘッダー部分受信・ペイロード部分受信・WOULD_BLOCK を経て 1 フレームを復元することの確認
TEST(potrSocketIoTest, tcp_frame_reader_partial_header_and_payload)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_tcp_frame_reader reader = {};
    potr_internal_tcp_frame_reader_reset(&reader);
    uint8_t wire[POTR_PACKET_HEADER_SIZE + 4] = {};
    uint16_t net_len = delegate_real_cplat_hton16(4U);
    std::memcpy(wire + 34, &net_len, sizeof(net_len));
    wire[POTR_PACKET_HEADER_SIZE + 0] = 0xAAU;
    wire[POTR_PACKET_HEADER_SIZE + 1] = 0xBBU;
    wire[POTR_PACKET_HEADER_SIZE + 2] = 0xCCU;
    wire[POTR_PACKET_HEADER_SIZE + 3] = 0xDDU;
    uint8_t buf[POTR_PACKET_HEADER_SIZE + 4] = {};
    int complete = 0;

    // Pre-Assert
    {
        InSequence seq;
        EXPECT_CALL(mock_cplat, cplat_socket_recv(_, _, _, _, _))
            .WillOnce(
                [&](cplat_socket, void *out, size_t, size_t *recv_len, cplat_error *)
                {
                    std::memcpy(out, wire, 10U);
                    *recv_len = 10U;
                    return CPLAT_OK;
                });
        EXPECT_CALL(mock_cplat, cplat_socket_recv(_, _, _, _, _)).WillOnce(Return(CPLAT_ERR_WOULD_BLOCK));
        EXPECT_CALL(mock_cplat, cplat_socket_recv(_, _, _, _, _))
            .WillOnce(
                [&](cplat_socket, void *out, size_t, size_t *recv_len, cplat_error *)
                {
                    std::memcpy(out, wire + 10U, POTR_PACKET_HEADER_SIZE - 10U);
                    *recv_len = POTR_PACKET_HEADER_SIZE - 10U;
                    return CPLAT_OK;
                });
        EXPECT_CALL(mock_cplat, cplat_socket_recv(_, _, _, _, _))
            .WillOnce(
                [&](cplat_socket, void *out, size_t, size_t *recv_len, cplat_error *)
                {
                    std::memcpy(out, wire + POTR_PACKET_HEADER_SIZE, 2U);
                    *recv_len = 2U;
                    return CPLAT_OK;
                });
        EXPECT_CALL(mock_cplat, cplat_socket_recv(_, _, _, _, _))
            .WillOnce(
                [&](cplat_socket, void *out, size_t, size_t *recv_len, cplat_error *)
                {
                    std::memcpy(out, wire + POTR_PACKET_HEADER_SIZE + 2U, 2U);
                    *recv_len = 2U;
                    return CPLAT_OK;
                });
    }

    // Act & Assert
    EXPECT_EQ(POTR_OK, potr_internal_tcp_frame_reader_step(&reader, static_cast<cplat_socket>(1), buf, sizeof(buf),
                                                           &complete, nullptr));
    EXPECT_EQ(0, complete);
    EXPECT_EQ(POTR_OK, potr_internal_tcp_frame_reader_step(&reader, static_cast<cplat_socket>(1), buf, sizeof(buf),
                                                           &complete, nullptr));
    EXPECT_EQ(0, complete);
    EXPECT_EQ(POTR_OK, potr_internal_tcp_frame_reader_step(&reader, static_cast<cplat_socket>(1), buf, sizeof(buf),
                                                           &complete, nullptr));
    EXPECT_EQ(0, complete);
    EXPECT_EQ(POTR_OK, potr_internal_tcp_frame_reader_step(&reader, static_cast<cplat_socket>(1), buf, sizeof(buf),
                                                           &complete, nullptr));
    EXPECT_EQ(0, complete);
    EXPECT_EQ(POTR_OK, potr_internal_tcp_frame_reader_step(&reader, static_cast<cplat_socket>(1), buf, sizeof(buf),
                                                           &complete, nullptr));
    EXPECT_EQ(1, complete);
    EXPECT_EQ(0, std::memcmp(wire, buf, sizeof(wire))); // [確認_正常系] - 分割受信したフレーム全体が一致すること。
}

// TCP フレーム読み取りで上限を超えるペイロード長と EOF を検出することの確認
TEST(potrSocketIoTest, tcp_frame_reader_oversize_and_eof)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_tcp_frame_reader reader = {};
    potr_internal_tcp_frame_reader_reset(&reader);
    uint8_t buf[POTR_PACKET_HEADER_SIZE + 4] = {};
    int complete = 0;

    EXPECT_CALL(mock_cplat, cplat_socket_recv(_, _, _, _, _))
        .WillOnce(
            [](cplat_socket, void *out, size_t, size_t *recv_len, cplat_error *)
            {
                std::memset(out, 0, POTR_PACKET_HEADER_SIZE);
                uint16_t net_len = delegate_real_cplat_hton16(5U);
                std::memcpy(static_cast<uint8_t *>(out) + 34, &net_len, sizeof(net_len));
                *recv_len = POTR_PACKET_HEADER_SIZE;
                return CPLAT_OK;
            })
        .WillOnce(Return(CPLAT_ERR_EOF));

    // Act
    int ret_oversize =
        potr_internal_tcp_frame_reader_step(&reader, static_cast<cplat_socket>(1), buf, sizeof(buf), &complete, nullptr);
    potr_internal_tcp_frame_reader_reset(&reader);
    int ret_eof =
        potr_internal_tcp_frame_reader_step(&reader, static_cast<cplat_socket>(1), buf, sizeof(buf), &complete, nullptr);

    // Assert
    EXPECT_EQ(POTR_ERR_PROTOCOL, ret_oversize); // [確認_異常系] - ペイロード長超過で POTR_ERR_PROTOCOL を返すこと。
    EXPECT_EQ(POTR_ERR_EOF, ret_eof);           // [確認_異常系] - EOF で POTR_ERR_EOF を返すこと。
}
