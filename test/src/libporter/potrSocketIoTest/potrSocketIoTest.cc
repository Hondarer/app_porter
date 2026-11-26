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
    for (size_t i = 0U; i < POTR_MAX_PATH; i++)
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
    for (size_t i = 0U; i < POTR_MAX_PATH; i++)
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

// 一括の送信で、全データグラムを 1 回の呼び出しで送り、全要素の結果を POTR_OK にすることの確認
TEST(potrSocketIoTest, udp_sendto_multi_sends_all_in_one_call)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int watch_tok = 0;
    int cancel_tok = 0;
    init_udp_ctx(&ctx, &watch_tok, &cancel_tok); // [状態] - path 0 を登録済みの UDP ソケットとする。
    const uint8_t payload[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};
    cplat_ipv4_endpoint ep = {};
    cplat_socket_datagram datagrams[3] = {};
    int results[3] = {POTR_ERR_UNKNOWN, POTR_ERR_UNKNOWN, POTR_ERR_UNKNOWN};
    for (size_t i = 0U; i < 3U; i++)
    {
        datagrams[i].buf = payload[i];
        datagrams[i].len = sizeof(payload[i]);
        datagrams[i].endpoint = &ep;
    } // [状態] - 4 [byte] のデータグラムを 3 個用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto_multi(ctx.udp_watch[0], datagrams, 3U, _, _))
        .WillOnce(DoAll(SetArgPointee<3>(3U), Return(CPLAT_OK)));
    // [Pre-Assert確認_正常系] - cplat_socket_watch_sendto_multi() が path 0 の登録、先頭のデータグラム、3 を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - 3 個を送れたことを返す。
    EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto(_, _, _, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - cplat_socket_watch_sendto() が呼び出されないこと。
    EXPECT_CALL(mock_cplat, cplat_socket_watch_wait_writable(_, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - cplat_socket_watch_wait_writable() が呼び出されないこと。

    // Act
    int actual_ret = potr_internal_udp_sendto_multi(&ctx, 0, datagrams, 3U, results); // [手順] - 3 個を一括で送る。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - POTR_OK を返すこと。
    EXPECT_EQ(POTR_OK, results[0]); // [確認_正常系] - 1 個目の結果が POTR_OK であること。
    EXPECT_EQ(POTR_OK, results[1]); // [確認_正常系] - 2 個目の結果が POTR_OK であること。
    EXPECT_EQ(POTR_OK, results[2]); // [確認_正常系] - 3 個目の結果が POTR_OK であること。
    dispose_udp_ctx(&ctx);
}

// 一括の送信で先頭に送信の空きがない場合に、送信可能を待って、まだ送っていない先頭から送り直すことの確認
TEST(potrSocketIoTest, udp_sendto_multi_waits_and_retries_on_would_block)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int watch_tok = 0;
    int cancel_tok = 0;
    init_udp_ctx(&ctx, &watch_tok, &cancel_tok); // [状態] - path 0 を登録済みの UDP ソケットとする。
    const uint8_t payload[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};
    cplat_ipv4_endpoint ep = {};
    cplat_socket_datagram datagrams[3] = {};
    int results[3] = {POTR_ERR_UNKNOWN, POTR_ERR_UNKNOWN, POTR_ERR_UNKNOWN};
    for (size_t i = 0U; i < 3U; i++)
    {
        datagrams[i].buf = payload[i];
        datagrams[i].len = sizeof(payload[i]);
        datagrams[i].endpoint = &ep;
    } // [状態] - 4 [byte] のデータグラムを 3 個用意する。
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_))
        .WillByDefault(DoAll(SetArgPointee<0>(1000U), Return(CPLAT_OK))); // [状態] - 単調時刻を 1000 [ms] とする。

    // Pre-Assert
    {
        InSequence seq;
        EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto_multi(ctx.udp_watch[0], datagrams, 3U, _, _))
            .WillOnce(Return(CPLAT_ERR_WOULD_BLOCK));
        // [Pre-Assert確認_正常系] - cplat_socket_watch_sendto_multi() が先頭のデータグラムと 3 を指定して呼び出されること。
        // [Pre-Assert手順] - 送信の空きがないことを返す。
        EXPECT_CALL(mock_cplat, cplat_socket_watch_wait_writable(ctx.udp_watch[0], ctx.recv_cancel, 100, _))
            .WillOnce(Return(CPLAT_OK));
        // [Pre-Assert確認_正常系] - cplat_socket_watch_wait_writable() が path 0 の登録、recv_cancel、100 を指定して 1 回呼び出されること。
        // [Pre-Assert手順] - 送信可能を返す。
        EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto_multi(ctx.udp_watch[0], datagrams, 3U, _, _))
            .WillOnce(DoAll(SetArgPointee<3>(3U), Return(CPLAT_OK)));
        // [Pre-Assert確認_正常系] - 待機の後に、cplat_socket_watch_sendto_multi() が先頭のデータグラムと 3 を指定して呼び出されること。
        // [Pre-Assert手順] - 3 個を送れたことを返す。
    }
    EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto(_, _, _, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - cplat_socket_watch_sendto() が呼び出されないこと。

    // Act
    int actual_ret = potr_internal_udp_sendto_multi(&ctx, 0, datagrams, 3U, results); // [手順] - 3 個を一括で送る。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - POTR_OK を返すこと。
    EXPECT_EQ(POTR_OK, results[0]); // [確認_正常系] - 1 個目の結果が POTR_OK であること。
    EXPECT_EQ(POTR_OK, results[1]); // [確認_正常系] - 2 個目の結果が POTR_OK であること。
    EXPECT_EQ(POTR_OK, results[2]); // [確認_正常系] - 3 個目の結果が POTR_OK であること。
    dispose_udp_ctx(&ctx);
}

// 一括の送信が一部 (要求 4 個のうち 2 個) だけを送った場合に、残りを 1 個ずつ 1 回ずつ送り、その結果を格納することの確認
TEST(potrSocketIoTest, udp_sendto_multi_sends_rest_one_by_one_after_partial)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int watch_tok = 0;
    int cancel_tok = 0;
    init_udp_ctx(&ctx, &watch_tok, &cancel_tok); // [状態] - path 0 を登録済みの UDP ソケットとする。
    const uint8_t payload[4][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}, {13, 14, 15, 16}};
    cplat_ipv4_endpoint ep = {};
    cplat_socket_datagram datagrams[4] = {};
    int results[4] = {POTR_ERR_UNKNOWN, POTR_ERR_UNKNOWN, POTR_ERR_UNKNOWN, POTR_ERR_UNKNOWN};
    for (size_t i = 0U; i < 4U; i++)
    {
        datagrams[i].buf = payload[i];
        datagrams[i].len = sizeof(payload[i]);
        datagrams[i].endpoint = &ep;
    } // [状態] - 4 [byte] のデータグラムを 4 個用意する。

    // Pre-Assert
    {
        InSequence seq;
        EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto_multi(ctx.udp_watch[0], datagrams, 4U, _, _))
            .WillOnce(DoAll(SetArgPointee<3>(2U), Return(CPLAT_OK)));
        // [Pre-Assert確認_正常系] - cplat_socket_watch_sendto_multi() が先頭のデータグラムと 4 を指定して 1 回呼び出されること。
        // [Pre-Assert手順] - 先頭の 2 個だけを送れたことを返す。
        EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto(ctx.udp_watch[0], payload[2], 4U, &ep, _, _))
            .WillOnce(DoAll(SetArgPointee<4>(4U), Return(CPLAT_OK)));
        // [Pre-Assert確認_正常系] - cplat_socket_watch_sendto() が 3 個目のデータグラムを指定して 1 回呼び出されること。
        // [Pre-Assert手順] - 全量を送れたことを返す。
        EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto(ctx.udp_watch[0], payload[3], 4U, &ep, _, _))
            .WillOnce(DoAll(SetArgPointee<4>(1U), Return(CPLAT_OK)));
        // [Pre-Assert確認_正常系] - cplat_socket_watch_sendto() が 4 個目のデータグラムを指定して 1 回呼び出されること。
        // [Pre-Assert手順] - 1 [byte] だけの短い送信を返す。
    }

    // Act
    int actual_ret = potr_internal_udp_sendto_multi(&ctx, 0, datagrams, 4U, results); // [手順] - 4 個を一括で送る。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);     // [確認_正常系] - POTR_OK を返すこと。
    EXPECT_EQ(POTR_OK, results[0]);     // [確認_正常系] - 一括で送った 1 個目の結果が POTR_OK であること。
    EXPECT_EQ(POTR_OK, results[1]);     // [確認_正常系] - 一括で送った 2 個目の結果が POTR_OK であること。
    EXPECT_EQ(POTR_OK, results[2]);     // [確認_正常系] - 1 個ずつ送った 3 個目の結果が POTR_OK であること。
    EXPECT_EQ(POTR_ERR_IO, results[3]); // [確認_異常系] - 短い送信となった 4 個目の結果が POTR_ERR_IO であること。
    dispose_udp_ctx(&ctx);
}

// 一括の送信で送信可能にならないまま期限を過ぎた場合に、全要素へ POTR_ERR_TIMEOUT を格納することの確認
TEST(potrSocketIoTest, udp_sendto_multi_times_out)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int watch_tok = 0;
    int cancel_tok = 0;
    init_udp_ctx(&ctx, &watch_tok, &cancel_tok); // [状態] - path 0 を登録済みの UDP ソケットとする。
    const uint8_t payload[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
    cplat_ipv4_endpoint ep = {};
    cplat_socket_datagram datagrams[2] = {};
    int results[2] = {POTR_ERR_UNKNOWN, POTR_ERR_UNKNOWN};
    uint64_t now_ms = 5000U;
    for (size_t i = 0U; i < 2U; i++)
    {
        datagrams[i].buf = payload[i];
        datagrams[i].len = sizeof(payload[i]);
        datagrams[i].endpoint = &ep;
    } // [状態] - 4 [byte] のデータグラムを 2 個用意する。
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_))
        .WillByDefault(
            [&](uint64_t *out)
            {
                *out = now_ms;
                return CPLAT_OK;
            }); // [状態] - 単調時刻として now_ms を返す。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto_multi(ctx.udp_watch[0], datagrams, 2U, _, _))
        .WillOnce(Return(CPLAT_ERR_WOULD_BLOCK));
    // [Pre-Assert確認_異常系] - cplat_socket_watch_sendto_multi() が 1 回だけ呼び出されること。
    // [Pre-Assert手順] - 送信の空きがないことを返す。
    EXPECT_CALL(mock_cplat, cplat_socket_watch_wait_writable(ctx.udp_watch[0], ctx.recv_cancel, 100, _))
        .WillOnce(
            [&](cplat_socket_watch *, cplat_socket_wait_cancel *, int, cplat_error *)
            {
                now_ms += 100U;
                return CPLAT_ERR_TIMEOUT;
            });
    // [Pre-Assert確認_異常系] - cplat_socket_watch_wait_writable() が 100 を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - 時刻を 100 [ms] 進めて、待機の時間切れを返す。
    EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto(_, _, _, _, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - 期限を過ぎた後に cplat_socket_watch_sendto() で送り直さないこと。

    // Act
    int actual_ret = potr_internal_udp_sendto_multi(&ctx, 0, datagrams, 2U, results); // [手順] - 2 個を一括で送る。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);          // [確認_正常系] - 引数が正しいため POTR_OK を返すこと。
    EXPECT_EQ(POTR_ERR_TIMEOUT, results[0]); // [確認_異常系] - 1 個目の結果が POTR_ERR_TIMEOUT であること。
    EXPECT_EQ(POTR_ERR_TIMEOUT, results[1]); // [確認_異常系] - 2 個目の結果が POTR_ERR_TIMEOUT であること。
    dispose_udp_ctx(&ctx);
}

// 一括の送信の不正な引数で POTR_ERR_INVALID_ARGUMENT を返し、何も送らないことの確認
TEST(potrSocketIoTest, udp_sendto_multi_rejects_invalid_arguments)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int watch_tok = 0;
    int cancel_tok = 0;
    init_udp_ctx(&ctx, &watch_tok, &cancel_tok); // [状態] - path 0 を登録済みの UDP ソケットとする。
    const uint8_t payload[4] = {1, 2, 3, 4};
    cplat_ipv4_endpoint ep = {};
    cplat_socket_datagram datagrams[2] = {};
    cplat_socket_datagram null_buf[2] = {};
    cplat_socket_datagram null_endpoint[2] = {};
    int results[2] = {POTR_ERR_UNKNOWN, POTR_ERR_UNKNOWN};
    for (size_t i = 0U; i < 2U; i++)
    {
        datagrams[i].buf = payload;
        datagrams[i].len = sizeof(payload);
        datagrams[i].endpoint = &ep;
        null_buf[i] = datagrams[i];
        null_endpoint[i] = datagrams[i];
    } // [状態] - 正しいデータグラムを 2 個用意する。
    null_buf[1].buf = nullptr;           // [状態] - 2 個目の buf を NULL にした配列を用意する。
    null_endpoint[1].endpoint = nullptr; // [状態] - 2 個目の endpoint を NULL にした配列を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto_multi(_, _, _, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - cplat_socket_watch_sendto_multi() が呼び出されないこと。
    EXPECT_CALL(mock_cplat, cplat_socket_watch_sendto(_, _, _, _, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - cplat_socket_watch_sendto() が呼び出されないこと。

    // Act
    int null_ctx = potr_internal_udp_sendto_multi(nullptr, 0, datagrams, 2U, results); // [手順] - ctx を NULL にして呼び出す。
    int null_datagrams = potr_internal_udp_sendto_multi(&ctx, 0, nullptr, 2U, results); // [手順] - datagrams を NULL にして呼び出す。
    int null_results = potr_internal_udp_sendto_multi(&ctx, 0, datagrams, 2U, nullptr); // [手順] - results_out を NULL にして呼び出す。
    int negative_path = potr_internal_udp_sendto_multi(&ctx, -1, datagrams, 2U, results); // [手順] - path_idx を -1 にして呼び出す。
    int over_path = potr_internal_udp_sendto_multi(&ctx, (int)POTR_MAX_PATH, datagrams, 2U, results);
    // [手順] - path_idx を POTR_MAX_PATH にして呼び出す。
    int zero_count = potr_internal_udp_sendto_multi(&ctx, 0, datagrams, 0U, results); // [手順] - count を 0 にして呼び出す。
    int over_count = potr_internal_udp_sendto_multi(&ctx, 0, datagrams, CPLAT_SOCKET_SENDTO_MULTI_MAX + 1U, results);
    // [手順] - count を CPLAT_SOCKET_SENDTO_MULTI_MAX + 1 にして呼び出す。
    int element_buf = potr_internal_udp_sendto_multi(&ctx, 0, null_buf, 2U, results); // [手順] - 要素の buf が NULL の配列で呼び出す。
    int element_endpoint = potr_internal_udp_sendto_multi(&ctx, 0, null_endpoint, 2U, results);
    // [手順] - 要素の endpoint が NULL の配列で呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, null_ctx);         // [確認_異常系] - ctx が NULL の場合に POTR_ERR_INVALID_ARGUMENT を返すこと。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, null_datagrams);   // [確認_異常系] - datagrams が NULL の場合に POTR_ERR_INVALID_ARGUMENT を返すこと。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, null_results);     // [確認_異常系] - results_out が NULL の場合に POTR_ERR_INVALID_ARGUMENT を返すこと。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, negative_path);    // [確認_異常系] - path_idx が -1 の場合に POTR_ERR_INVALID_ARGUMENT を返すこと。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, over_path);        // [確認_異常系] - path_idx が POTR_MAX_PATH の場合に POTR_ERR_INVALID_ARGUMENT を返すこと。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, zero_count);       // [確認_異常系] - count が 0 の場合に POTR_ERR_INVALID_ARGUMENT を返すこと。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, over_count);       // [確認_異常系] - count が上限を超える場合に POTR_ERR_INVALID_ARGUMENT を返すこと。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, element_buf);      // [確認_異常系] - 要素の buf が NULL の場合に POTR_ERR_INVALID_ARGUMENT を返すこと。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, element_endpoint); // [確認_異常系] - 要素の endpoint が NULL の場合に POTR_ERR_INVALID_ARGUMENT を返すこと。
    EXPECT_EQ(POTR_ERR_UNKNOWN, results[0]);                // [確認_異常系] - results_out の 1 個目を変更しないこと。
    EXPECT_EQ(POTR_ERR_UNKNOWN, results[1]);                // [確認_異常系] - results_out の 2 個目を変更しないこと。
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
