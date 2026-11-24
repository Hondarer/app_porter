#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    // C++17 の std::byte と Windows SDK の byte が衝突するため、std::byte を定義しない。
    // see: https://github.com/microsoft/STL/blob/main/stl/inc/yvals_core.h
    #define _HAS_STD_BYTE 0
    #include <winsock2.h>
#elif defined(PLATFORM_LINUX)
    #include <errno.h>
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <porter/porter_result.h>
#include <cstring>
#include <vector>

using namespace testing;

TEST(threadConnectPrepareTest, session_unknown)
{
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ctx.tcp_accepted_session_known = 0;
    ctx.tcp_accepted_session_ts.tv_sec = 10;
    ctx.tcp_accepted_session_ts.tv_nsec = 1;
    ctx.tcp_accepted_session_id = 5;
    potr_packet pkt = {};
    pkt.session_tv_sec = 1;
    pkt.session_tv_nsec = 0;
    pkt.session_id = 0;
    EXPECT_EQ(TCP_SESSION_NEW, thread_connect_session_compare(&ctx, &pkt));
    EXPECT_EQ(5u, ctx.tcp_accepted_session_id);
}

TEST(threadConnectPrepareTest, session_new_and_old_and_same)
{
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ctx.tcp_accepted_session_known = 1;
    ctx.tcp_accepted_session_ts.tv_sec = 10;
    ctx.tcp_accepted_session_ts.tv_nsec = 1;
    ctx.tcp_accepted_session_id = 5;

    potr_packet pkt = {};
    pkt.session_tv_sec = 11;
    EXPECT_EQ(TCP_SESSION_NEW, thread_connect_session_compare(&ctx, &pkt));

    pkt.session_tv_sec = 9;
    EXPECT_EQ(TCP_SESSION_OLD, thread_connect_session_compare(&ctx, &pkt));

    pkt.session_tv_sec = 10;
    pkt.session_tv_nsec = 2;
    EXPECT_EQ(TCP_SESSION_NEW, thread_connect_session_compare(&ctx, &pkt));

    pkt.session_tv_nsec = 0;
    EXPECT_EQ(TCP_SESSION_OLD, thread_connect_session_compare(&ctx, &pkt));

    pkt.session_tv_nsec = 1;
    pkt.session_id = 6;
    EXPECT_EQ(TCP_SESSION_NEW, thread_connect_session_compare(&ctx, &pkt));

    pkt.session_id = 4;
    EXPECT_EQ(TCP_SESSION_OLD, thread_connect_session_compare(&ctx, &pkt));

    pkt.session_id = 5;
    EXPECT_EQ(TCP_SESSION_SAME, thread_connect_session_compare(&ctx, &pkt));
}

// 先読みで単一の絶対期限がヘッダーとペイロードを通じて延長されないことの確認
TEST(threadConnectPrepareTest, first_packet_single_deadline_across_header_and_payload)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    int watch_tok = 0;
    int cancel_tok = 0;
    cplat_socket_watch *watch = reinterpret_cast<cplat_socket_watch *>(&watch_tok);
    cplat_socket_wait_cancel *cancel = reinterpret_cast<cplat_socket_wait_cancel *>(&cancel_tok);
    uint8_t buf[POTR_PACKET_HEADER_SIZE + 4] = {};
    size_t len = 999U;
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
        EXPECT_CALL(mock_cplat, cplat_socket_watch_wait_readable_multi(_, 1U, cancel, _, _, _))
            .WillOnce(
                [&](cplat_socket_watch *const *, size_t, cplat_socket_wait_cancel *, int timeout_ms,
                    unsigned char *ready, cplat_error *)
                {
                    wait_timeouts.push_back(timeout_ms);
                    now_ms += 40U;
                    ready[0] = 1U;
                    return CPLAT_OK;
                });
        EXPECT_CALL(mock_cplat, cplat_socket_recv(static_cast<cplat_socket>(7), _, _, _, _))
            .WillOnce(
                [](cplat_socket, void *out, size_t, size_t *recv_len, cplat_error *)
                {
                    std::memset(out, 0, POTR_PACKET_HEADER_SIZE);
                    uint16_t wire = delegate_real_cplat_hton16(4U);
                    std::memcpy(static_cast<uint8_t *>(out) + 34, &wire, sizeof(wire));
                    *recv_len = POTR_PACKET_HEADER_SIZE;
                    return CPLAT_OK;
                });
        EXPECT_CALL(mock_cplat, cplat_socket_watch_wait_readable_multi(_, 1U, cancel, _, _, _))
            .WillOnce(
                [&](cplat_socket_watch *const *, size_t, cplat_socket_wait_cancel *, int timeout_ms,
                    unsigned char *ready, cplat_error *)
                {
                    wait_timeouts.push_back(timeout_ms);
                    now_ms += 30U;
                    ready[0] = 1U;
                    return CPLAT_OK;
                });
        EXPECT_CALL(mock_cplat, cplat_socket_recv(static_cast<cplat_socket>(7), _, _, _, _))
            .WillOnce(
                [](cplat_socket, void *out, size_t, size_t *recv_len, cplat_error *)
                {
                    std::memset(out, 0x5A, 4U);
                    *recv_len = 4U;
                    return CPLAT_OK;
                });
    }

    // Act
    int actual_ret = thread_connect_read_first_packet(static_cast<cplat_socket>(7), watch, cancel, buf, sizeof(buf),
                                                      &len, 100);

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);                        // [確認_正常系] - 先読みに成功すること。
    EXPECT_EQ(static_cast<size_t>(POTR_PACKET_HEADER_SIZE + 4), len); // [確認_正常系] - 受信長が一致すること。
    EXPECT_EQ((std::vector<int>{100, 60}), wait_timeouts); // [確認_正常系] - ヘッダー受信後も期限を延長せず 60 ms で待つこと。
}

// 先読みの期限超過、解除、EOF、ペイロード長超過の確認
TEST(threadConnectPrepareTest, first_packet_timeout_cancel_eof_oversize)
{
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    int watch_tok = 0;
    int cancel_tok = 0;
    cplat_socket_watch *watch = reinterpret_cast<cplat_socket_watch *>(&watch_tok);
    cplat_socket_wait_cancel *cancel = reinterpret_cast<cplat_socket_wait_cancel *>(&cancel_tok);
    uint8_t buf[POTR_PACKET_HEADER_SIZE + 4] = {};
    size_t len = 999U;

    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault(DoAll(SetArgPointee<0>(1000U), Return(CPLAT_OK)));

    EXPECT_CALL(mock_cplat, cplat_socket_watch_wait_readable_multi(_, 1U, cancel, 100, _, _))
        .WillOnce(Return(CPLAT_ERR_TIMEOUT))
        .WillOnce(Return(CPLAT_ERR_CANCELED))
        .WillOnce(
            [](cplat_socket_watch *const *, size_t, cplat_socket_wait_cancel *, int, unsigned char *ready,
               cplat_error *)
            {
                ready[0] = 1U;
                return CPLAT_OK;
            })
        .WillOnce(
            [](cplat_socket_watch *const *, size_t, cplat_socket_wait_cancel *, int, unsigned char *ready,
               cplat_error *)
            {
                ready[0] = 1U;
                return CPLAT_OK;
            });

    EXPECT_CALL(mock_cplat, cplat_socket_recv(_, _, _, _, _))
        .WillOnce(Return(CPLAT_ERR_EOF))
        .WillOnce(
            [](cplat_socket, void *out, size_t, size_t *recv_len, cplat_error *)
            {
                std::memset(out, 0, POTR_PACKET_HEADER_SIZE);
                uint16_t wire = delegate_real_cplat_hton16(5U);
                std::memcpy(static_cast<uint8_t *>(out) + 34, &wire, sizeof(wire));
                *recv_len = POTR_PACKET_HEADER_SIZE;
                return CPLAT_OK;
            });

    EXPECT_EQ(POTR_ERR_TIMEOUT,
              thread_connect_read_first_packet(static_cast<cplat_socket>(7), watch, cancel, buf, sizeof(buf), &len, 100));
    EXPECT_EQ(POTR_ERR_CANCELED,
              thread_connect_read_first_packet(static_cast<cplat_socket>(7), watch, cancel, buf, sizeof(buf), &len, 100));
    EXPECT_EQ(POTR_ERR_EOF,
              thread_connect_read_first_packet(static_cast<cplat_socket>(7), watch, cancel, buf, sizeof(buf), &len, 100));
    EXPECT_EQ(POTR_ERR_PROTOCOL,
              thread_connect_read_first_packet(static_cast<cplat_socket>(7), watch, cancel, buf, sizeof(buf), &len, 100));
    EXPECT_EQ(999U, len);
}

// connect_timeout_ms == 0 が CPLAT_WAIT_FOREVER として cplat_socket_watch_connect() へ渡されることの確認
TEST(threadConnectPrepareTest, connect_zero_timeout_uses_wait_forever)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.connect_timeout_ms = 0U;
    int watch_tok = 0;
    int cancel_tok = 0;
    cplat_socket_watch *expected_watch = reinterpret_cast<cplat_socket_watch *>(&watch_tok);
    cplat_socket_wait_cancel *cancel = reinterpret_cast<cplat_socket_wait_cancel *>(&cancel_tok);
    cplat_socket_watch *watch_out = nullptr;

    ON_CALL(mock_cplat, cplat_socket_open(CPLAT_SOCKET_TCP, _, _))
        .WillByDefault(DoAll(SetArgPointee<1>(static_cast<cplat_socket>(7)), Return(CPLAT_OK)));
    ON_CALL(mock_cplat, cplat_socket_set_nodelay(_, 1, _)).WillByDefault(Return(CPLAT_OK));

    // Pre-Assert
    {
        InSequence seq;
        EXPECT_CALL(mock_cplat, cplat_socket_watch_attach(static_cast<cplat_socket>(7), _, _))
            .WillOnce(DoAll(SetArgPointee<1>(expected_watch), Return(CPLAT_OK)));
        EXPECT_CALL(mock_cplat, cplat_socket_watch_connect(expected_watch, _, cancel, CPLAT_WAIT_FOREVER, _))
            .WillOnce(Return(CPLAT_OK));
    }
    EXPECT_CALL(mock_cplat, cplat_socket_watch_detach(_)).Times(0);
    EXPECT_CALL(mock_cplat, cplat_socket_close(_)).Times(0);

    // Act
    cplat_socket fd = thread_connect_with_timeout(&ctx, 0, cancel, &watch_out);

    // Assert
    EXPECT_EQ(static_cast<cplat_socket>(7), fd); // [確認_正常系] - 接続済みソケットを返すこと。
    EXPECT_EQ(expected_watch, watch_out);        // [確認_正常系] - 登録済み watch を返すこと。
}

// watch_connect の期限超過・解除・拒否で watch_detach -> socket_close の順に回収することの確認
TEST(threadConnectPrepareTest, connect_timeout_cancel_refused_detach_then_close)
{
    const int results[3] = {CPLAT_ERR_TIMEOUT, CPLAT_ERR_CANCELED, CPLAT_ERR_UNKNOWN};

    for (int i = 0; i < 3; i++)
    {
        NiceMock<Mock_cplat> mock_cplat;
        NiceMock<Mock_porter> mock_porter;
        potr_context ctx = {};
        ctx.service.connect_timeout_ms = 250U;
        int watch_tok = 0;
        int cancel_tok = 0;
        cplat_socket_watch *expected_watch = reinterpret_cast<cplat_socket_watch *>(&watch_tok);
        cplat_socket_wait_cancel *cancel = reinterpret_cast<cplat_socket_wait_cancel *>(&cancel_tok);
        cplat_socket_watch *watch_out = expected_watch;

        ON_CALL(mock_cplat, cplat_socket_open(CPLAT_SOCKET_TCP, _, _))
            .WillByDefault(DoAll(SetArgPointee<1>(static_cast<cplat_socket>(7)), Return(CPLAT_OK)));
        ON_CALL(mock_cplat, cplat_socket_set_nodelay(_, 1, _)).WillByDefault(Return(CPLAT_OK));

        {
            InSequence seq;
            EXPECT_CALL(mock_cplat, cplat_socket_watch_attach(static_cast<cplat_socket>(7), _, _))
                .WillOnce(DoAll(SetArgPointee<1>(expected_watch), Return(CPLAT_OK)));
            EXPECT_CALL(mock_cplat, cplat_socket_watch_connect(expected_watch, _, cancel, 250, _))
                .WillOnce(Return(results[i]));
            EXPECT_CALL(mock_cplat, cplat_socket_watch_detach(expected_watch)).WillOnce(Return());
            EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(7))).WillOnce(Return());
        }

        cplat_socket fd = thread_connect_with_timeout(&ctx, 0, cancel, &watch_out);
        EXPECT_EQ(CPLAT_INVALID_SOCKET, fd);
        EXPECT_EQ(nullptr, watch_out);
    }
}

// TCP_NODELAY 失敗後も接続を継続することの確認
TEST(threadConnectPrepareTest, connect_nodelay_failure_continues)
{
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.connect_timeout_ms = 0U;
    ctx.service.service_id = 11;
    int watch_tok = 0;
    int cancel_tok = 0;
    cplat_socket_watch *expected_watch = reinterpret_cast<cplat_socket_watch *>(&watch_tok);
    cplat_socket_wait_cancel *cancel = reinterpret_cast<cplat_socket_wait_cancel *>(&cancel_tok);
    cplat_socket_watch *watch_out = nullptr;

    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<1>(static_cast<cplat_socket>(7)), Return(CPLAT_OK)));

    cplat_error nodelay_failure = {};
    nodelay_failure.result = CPLAT_ERR_UNKNOWN;
#if defined(PLATFORM_LINUX)
    nodelay_failure.domain = CPLAT_ERROR_DOMAIN_SOCKET_ERRNO;
    nodelay_failure.code = (unsigned long)ENOPROTOOPT;
#elif defined(PLATFORM_WINDOWS)
    nodelay_failure.domain = CPLAT_ERROR_DOMAIN_WINSOCK;
    nodelay_failure.code = (unsigned long)WSAENOPROTOOPT;
#endif
    {
        InSequence order;
        EXPECT_CALL(mock_cplat, cplat_socket_set_nodelay(static_cast<cplat_socket>(7), 1, _))
            .WillOnce(DoAll(SetArgPointee<2>(nodelay_failure), Return(CPLAT_ERR_UNKNOWN)));
        EXPECT_CALL(mock_cplat, cplat_socket_watch_attach(static_cast<cplat_socket>(7), _, _))
            .WillOnce(DoAll(SetArgPointee<1>(expected_watch), Return(CPLAT_OK)));
        EXPECT_CALL(mock_cplat, cplat_socket_watch_connect(expected_watch, _, cancel, CPLAT_WAIT_FOREVER, _))
            .WillOnce(Return(CPLAT_OK));
    }
    EXPECT_CALL(mock_cplat, cplat_socket_close(_)).Times(0);

    cplat_socket actual_ret = thread_connect_with_timeout(&ctx, 0, cancel, &watch_out);
    EXPECT_EQ(static_cast<cplat_socket>(7), actual_ret);
    EXPECT_EQ(expected_watch, watch_out);
}

// src_port=0 では reuse_address を呼ばず、固定 src_port では呼ぶことの確認
TEST(threadConnectPrepareTest, connect_reuse_address_follows_src_port)
{
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    int watch_tok = 0;
    int cancel_tok = 0;
    cplat_socket_watch *expected_watch = reinterpret_cast<cplat_socket_watch *>(&watch_tok);
    cplat_socket_wait_cancel *cancel = reinterpret_cast<cplat_socket_wait_cancel *>(&cancel_tok);
    cplat_socket_watch *watch_out = nullptr;

    ON_CALL(mock_cplat, cplat_socket_open(CPLAT_SOCKET_TCP, _, _))
        .WillByDefault(DoAll(SetArgPointee<1>(static_cast<cplat_socket>(7)), Return(CPLAT_OK)));
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_set_nodelay(_, 1, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_watch_attach(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<1>(expected_watch), Return(CPLAT_OK)));
    ON_CALL(mock_cplat, cplat_socket_watch_connect(_, _, _, _, _)).WillByDefault(Return(CPLAT_OK));

    potr_context ctx_ephemeral = {};
    ctx_ephemeral.service.src_port = 0U;
    potr_context ctx_fixed = {};
    ctx_fixed.service.src_port = 40100U;

    EXPECT_CALL(mock_cplat, cplat_socket_set_reuse_address(static_cast<cplat_socket>(7), 1, _))
        .Times(1)
        .WillOnce(Return(CPLAT_OK));

    EXPECT_EQ(static_cast<cplat_socket>(7), thread_connect_with_timeout(&ctx_ephemeral, 0, cancel, &watch_out));
    EXPECT_EQ(static_cast<cplat_socket>(7), thread_connect_with_timeout(&ctx_fixed, 0, cancel, &watch_out));
}

TEST(threadConnectPrepareTest, source_filter_cases)
{
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ctx.src_addr_resolved[0] = 42;
    cplat_ipv4_endpoint peer = {};
    peer.address = 2;
    peer.port = delegate_real_cplat_hton16(200);
    EXPECT_EQ(1, thread_connect_peer_allowed(&ctx, 0, &peer));

    ctx.service.src_addr[0][0] = 1;
    ctx.service.src_port = 123;
    peer.address = 42;
    peer.port = delegate_real_cplat_hton16(123);
    EXPECT_EQ(1, thread_connect_peer_allowed(&ctx, 0, &peer));

    peer.address = 9;
    EXPECT_EQ(0, thread_connect_peer_allowed(&ctx, 0, &peer));

    peer.address = 42;
    peer.port = delegate_real_cplat_hton16(200);
    EXPECT_EQ(0, thread_connect_peer_allowed(&ctx, 0, &peer));
}
