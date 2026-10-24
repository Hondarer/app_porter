#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
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
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ctx.tcp_accepted_session_known = 0;
    ctx.tcp_accepted_session_ts.tv_sec = 10;
    ctx.tcp_accepted_session_ts.tv_nsec = 1;
    ctx.tcp_accepted_session_id = 5;
    potr_packet pkt = {};
    pkt.session_tv_sec = 1;
    pkt.session_tv_nsec = 0;
    pkt.session_id = 0; // [状態] - 採用済み triplet と受信 triplet を指定する。
    // Act
    int actual_ret = thread_connect_session_compare(&ctx, &pkt); // [手順] - 他スレッドのない状態で比較する。
    // Assert
    EXPECT_EQ(TCP_SESSION_NEW, actual_ret);     // [確認_正常系] - triplet の新旧を分類すること。
    EXPECT_EQ(5u, ctx.tcp_accepted_session_id); // [確認_正常系] - 比較では採用済み状態を変更しないこと。
}

TEST(threadConnectPrepareTest, session_new_second)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ctx.tcp_accepted_session_known = 1;
    ctx.tcp_accepted_session_ts.tv_sec = 10;
    ctx.tcp_accepted_session_ts.tv_nsec = 1;
    ctx.tcp_accepted_session_id = 5;
    potr_packet pkt = {};
    pkt.session_tv_sec = 11;
    pkt.session_tv_nsec = 0;
    pkt.session_id = 1; // [状態] - 採用済み triplet と受信 triplet を指定する。
    // Act
    int actual_ret = thread_connect_session_compare(&ctx, &pkt); // [手順] - 他スレッドのない状態で比較する。
    // Assert
    EXPECT_EQ(TCP_SESSION_NEW, actual_ret);     // [確認_正常系] - triplet の新旧を分類すること。
    EXPECT_EQ(5u, ctx.tcp_accepted_session_id); // [確認_正常系] - 比較では採用済み状態を変更しないこと。
}

TEST(threadConnectPrepareTest, session_old_second)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ctx.tcp_accepted_session_known = 1;
    ctx.tcp_accepted_session_ts.tv_sec = 10;
    ctx.tcp_accepted_session_ts.tv_nsec = 1;
    ctx.tcp_accepted_session_id = 5;
    potr_packet pkt = {};
    pkt.session_tv_sec = 9;
    pkt.session_tv_nsec = 0;
    pkt.session_id = 99; // [状態] - 採用済み triplet と受信 triplet を指定する。
    // Act
    int actual_ret = thread_connect_session_compare(&ctx, &pkt); // [手順] - 他スレッドのない状態で比較する。
    // Assert
    EXPECT_EQ(TCP_SESSION_OLD, actual_ret);     // [確認_正常系] - triplet の新旧を分類すること。
    EXPECT_EQ(5u, ctx.tcp_accepted_session_id); // [確認_正常系] - 比較では採用済み状態を変更しないこと。
}

TEST(threadConnectPrepareTest, session_new_nanosecond)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ctx.tcp_accepted_session_known = 1;
    ctx.tcp_accepted_session_ts.tv_sec = 10;
    ctx.tcp_accepted_session_ts.tv_nsec = 1;
    ctx.tcp_accepted_session_id = 5;
    potr_packet pkt = {};
    pkt.session_tv_sec = 10;
    pkt.session_tv_nsec = 2;
    pkt.session_id = 1; // [状態] - 採用済み triplet と受信 triplet を指定する。
    // Act
    int actual_ret = thread_connect_session_compare(&ctx, &pkt); // [手順] - 他スレッドのない状態で比較する。
    // Assert
    EXPECT_EQ(TCP_SESSION_NEW, actual_ret);     // [確認_正常系] - triplet の新旧を分類すること。
    EXPECT_EQ(5u, ctx.tcp_accepted_session_id); // [確認_正常系] - 比較では採用済み状態を変更しないこと。
}

TEST(threadConnectPrepareTest, session_old_nanosecond)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ctx.tcp_accepted_session_known = 1;
    ctx.tcp_accepted_session_ts.tv_sec = 10;
    ctx.tcp_accepted_session_ts.tv_nsec = 1;
    ctx.tcp_accepted_session_id = 5;
    potr_packet pkt = {};
    pkt.session_tv_sec = 10;
    pkt.session_tv_nsec = 0;
    pkt.session_id = 99; // [状態] - 採用済み triplet と受信 triplet を指定する。
    // Act
    int actual_ret = thread_connect_session_compare(&ctx, &pkt); // [手順] - 他スレッドのない状態で比較する。
    // Assert
    EXPECT_EQ(TCP_SESSION_OLD, actual_ret);     // [確認_正常系] - triplet の新旧を分類すること。
    EXPECT_EQ(5u, ctx.tcp_accepted_session_id); // [確認_正常系] - 比較では採用済み状態を変更しないこと。
}

TEST(threadConnectPrepareTest, session_new_id)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ctx.tcp_accepted_session_known = 1;
    ctx.tcp_accepted_session_ts.tv_sec = 10;
    ctx.tcp_accepted_session_ts.tv_nsec = 1;
    ctx.tcp_accepted_session_id = 5;
    potr_packet pkt = {};
    pkt.session_tv_sec = 10;
    pkt.session_tv_nsec = 1;
    pkt.session_id = 6; // [状態] - 採用済み triplet と受信 triplet を指定する。
    // Act
    int actual_ret = thread_connect_session_compare(&ctx, &pkt); // [手順] - 他スレッドのない状態で比較する。
    // Assert
    EXPECT_EQ(TCP_SESSION_NEW, actual_ret);     // [確認_正常系] - triplet の新旧を分類すること。
    EXPECT_EQ(5u, ctx.tcp_accepted_session_id); // [確認_正常系] - 比較では採用済み状態を変更しないこと。
}

TEST(threadConnectPrepareTest, session_old_id)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ctx.tcp_accepted_session_known = 1;
    ctx.tcp_accepted_session_ts.tv_sec = 10;
    ctx.tcp_accepted_session_ts.tv_nsec = 1;
    ctx.tcp_accepted_session_id = 5;
    potr_packet pkt = {};
    pkt.session_tv_sec = 10;
    pkt.session_tv_nsec = 1;
    pkt.session_id = 4; // [状態] - 採用済み triplet と受信 triplet を指定する。
    // Act
    int actual_ret = thread_connect_session_compare(&ctx, &pkt); // [手順] - 他スレッドのない状態で比較する。
    // Assert
    EXPECT_EQ(TCP_SESSION_OLD, actual_ret);     // [確認_正常系] - triplet の新旧を分類すること。
    EXPECT_EQ(5u, ctx.tcp_accepted_session_id); // [確認_正常系] - 比較では採用済み状態を変更しないこと。
}

TEST(threadConnectPrepareTest, session_same)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ctx.tcp_accepted_session_known = 1;
    ctx.tcp_accepted_session_ts.tv_sec = 10;
    ctx.tcp_accepted_session_ts.tv_nsec = 1;
    ctx.tcp_accepted_session_id = 5;
    potr_packet pkt = {};
    pkt.session_tv_sec = 10;
    pkt.session_tv_nsec = 1;
    pkt.session_id = 5; // [状態] - 採用済み triplet と受信 triplet を指定する。
    // Act
    int actual_ret = thread_connect_session_compare(&ctx, &pkt); // [手順] - 他スレッドのない状態で比較する。
    // Assert
    EXPECT_EQ(TCP_SESSION_SAME, actual_ret);    // [確認_正常系] - triplet の新旧を分類すること。
    EXPECT_EQ(5u, ctx.tcp_accepted_session_id); // [確認_正常系] - 比較では採用済み状態を変更しないこと。
}

TEST(threadConnectPrepareTest, first_packet_empty)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    uint8_t buf[POTR_PACKET_HEADER_SIZE + 4] = {};
    size_t len = 999;
    int reads = 0;
    ON_CALL(mock_cplat, cplat_socket_wait_readable(_, _, _, _))
        .WillByDefault(
            [](cplat_socket, int, int *out, cplat_error *)
            {
                *out = 1;
                return CPLAT_OK;
            });
    ON_CALL(mock_porter, potr_internal_result_from_error(_)).WillByDefault(Return(POTR_ERR_IO));
    ON_CALL(mock_porter, potr_internal_result_from_socket_result(_, _)).WillByDefault(Return(POTR_ERR_IO));
    ON_CALL(mock_cplat, cplat_socket_recv_all(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, void *out, size_t count, cplat_error *)
            {
                ++reads;
                if (reads == -1)
                    return CPLAT_ERR_UNKNOWN;
                std::memset(out, 0, count);
                if (reads == 1)
                {
                    uint16_t wire = delegate_real_cplat_hton16(0);
                    std::memcpy(static_cast<uint8_t *>(out) + 34, &wire, sizeof(wire));
                }
                return CPLAT_OK;
            }); // [状態] - 待機、ヘッダー長と読み取り失敗を指定する。
    // Act
    int actual_ret = thread_connect_read_first_packet(static_cast<cplat_socket>(7), buf, sizeof(buf), &len,
                                                      100); // [手順] - 最初のパケットを読む。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 受信結果を分類すること。
    EXPECT_EQ(static_cast<size_t>(POTR_PACKET_HEADER_SIZE + 0),
              len);      // [確認_正常系] - 成功時だけ受信長を設定すること。
    EXPECT_EQ(1, reads); // [確認_正常系] - 必要な読み取りだけを実行すること。
}

TEST(threadConnectPrepareTest, first_packet_payload)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    uint8_t buf[POTR_PACKET_HEADER_SIZE + 4] = {};
    size_t len = 999;
    int reads = 0;
    ON_CALL(mock_cplat, cplat_socket_wait_readable(_, _, _, _))
        .WillByDefault(
            [](cplat_socket, int, int *out, cplat_error *)
            {
                *out = 1;
                return CPLAT_OK;
            });
    ON_CALL(mock_porter, potr_internal_result_from_error(_)).WillByDefault(Return(POTR_ERR_IO));
    ON_CALL(mock_porter, potr_internal_result_from_socket_result(_, _)).WillByDefault(Return(POTR_ERR_IO));
    ON_CALL(mock_cplat, cplat_socket_recv_all(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, void *out, size_t count, cplat_error *)
            {
                ++reads;
                if (reads == -1)
                    return CPLAT_ERR_UNKNOWN;
                std::memset(out, 0, count);
                if (reads == 1)
                {
                    uint16_t wire = delegate_real_cplat_hton16(4);
                    std::memcpy(static_cast<uint8_t *>(out) + 34, &wire, sizeof(wire));
                }
                return CPLAT_OK;
            }); // [状態] - 待機、ヘッダー長と読み取り失敗を指定する。
    // Act
    int actual_ret = thread_connect_read_first_packet(static_cast<cplat_socket>(7), buf, sizeof(buf), &len,
                                                      100); // [手順] - 最初のパケットを読む。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 受信結果を分類すること。
    EXPECT_EQ(static_cast<size_t>(POTR_PACKET_HEADER_SIZE + 4),
              len);      // [確認_正常系] - 成功時だけ受信長を設定すること。
    EXPECT_EQ(2, reads); // [確認_正常系] - 必要な読み取りだけを実行すること。
}

TEST(threadConnectPrepareTest, first_packet_timeout)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    uint8_t buf[POTR_PACKET_HEADER_SIZE + 4] = {};
    size_t len = 999;
    int reads = 0;
    ON_CALL(mock_cplat, cplat_socket_wait_readable(_, _, _, _))
        .WillByDefault(
            [](cplat_socket, int, int *out, cplat_error *)
            {
                *out = 0;
                return CPLAT_OK;
            });
    ON_CALL(mock_porter, potr_internal_result_from_error(_)).WillByDefault(Return(POTR_ERR_IO));
    ON_CALL(mock_porter, potr_internal_result_from_socket_result(_, _)).WillByDefault(Return(POTR_ERR_IO));
    ON_CALL(mock_cplat, cplat_socket_recv_all(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, void *out, size_t count, cplat_error *)
            {
                ++reads;
                if (reads == -1)
                    return CPLAT_ERR_UNKNOWN;
                std::memset(out, 0, count);
                if (reads == 1)
                {
                    uint16_t wire = delegate_real_cplat_hton16(0);
                    std::memcpy(static_cast<uint8_t *>(out) + 34, &wire, sizeof(wire));
                }
                return CPLAT_OK;
            }); // [状態] - 待機、ヘッダー長と読み取り失敗を指定する。
    // Act
    int actual_ret = thread_connect_read_first_packet(static_cast<cplat_socket>(7), buf, sizeof(buf), &len,
                                                      100); // [手順] - 最初のパケットを読む。
    // Assert
    EXPECT_EQ(POTR_ERR_TIMEOUT, actual_ret); // [確認_異常系] - 受信結果を分類すること。
    EXPECT_EQ(999u, len);                    // [確認_異常系] - 成功時だけ受信長を設定すること。
    EXPECT_EQ(0, reads);                     // [確認_正常系] - 必要な読み取りだけを実行すること。
}

TEST(threadConnectPrepareTest, first_packet_wait_error)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    uint8_t buf[POTR_PACKET_HEADER_SIZE + 4] = {};
    size_t len = 999;
    int reads = 0;
    ON_CALL(mock_cplat, cplat_socket_wait_readable(_, _, _, _))
        .WillByDefault(
            [](cplat_socket, int, int *out, cplat_error *)
            {
                *out = 0;
                return CPLAT_ERR_UNKNOWN;
            });
    ON_CALL(mock_porter, potr_internal_result_from_error(_)).WillByDefault(Return(POTR_ERR_IO));
    ON_CALL(mock_porter, potr_internal_result_from_socket_result(_, _)).WillByDefault(Return(POTR_ERR_IO));
    ON_CALL(mock_cplat, cplat_socket_recv_all(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, void *out, size_t count, cplat_error *)
            {
                ++reads;
                if (reads == -1)
                    return CPLAT_ERR_UNKNOWN;
                std::memset(out, 0, count);
                if (reads == 1)
                {
                    uint16_t wire = delegate_real_cplat_hton16(0);
                    std::memcpy(static_cast<uint8_t *>(out) + 34, &wire, sizeof(wire));
                }
                return CPLAT_OK;
            }); // [状態] - 待機、ヘッダー長と読み取り失敗を指定する。
    // Act
    int actual_ret = thread_connect_read_first_packet(static_cast<cplat_socket>(7), buf, sizeof(buf), &len,
                                                      100); // [手順] - 最初のパケットを読む。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 受信結果を分類すること。
    EXPECT_EQ(999u, len);               // [確認_異常系] - 成功時だけ受信長を設定すること。
    EXPECT_EQ(0, reads);                // [確認_正常系] - 必要な読み取りだけを実行すること。
}

TEST(threadConnectPrepareTest, first_packet_header_eof)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    uint8_t buf[POTR_PACKET_HEADER_SIZE + 4] = {};
    size_t len = 999;
    int reads = 0;
    ON_CALL(mock_cplat, cplat_socket_wait_readable(_, _, _, _))
        .WillByDefault(
            [](cplat_socket, int, int *out, cplat_error *)
            {
                *out = 1;
                return CPLAT_OK;
            });
    ON_CALL(mock_porter, potr_internal_result_from_error(_)).WillByDefault(Return(POTR_ERR_IO));
    ON_CALL(mock_porter, potr_internal_result_from_socket_result(_, _)).WillByDefault(Return(POTR_ERR_EOF));
    ON_CALL(mock_cplat, cplat_socket_recv_all(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, void *out, size_t count, cplat_error *)
            {
                ++reads;
                if (reads == 1)
                    return CPLAT_ERR_UNKNOWN;
                std::memset(out, 0, count);
                if (reads == 1)
                {
                    uint16_t wire = delegate_real_cplat_hton16(0);
                    std::memcpy(static_cast<uint8_t *>(out) + 34, &wire, sizeof(wire));
                }
                return CPLAT_OK;
            }); // [状態] - 待機、ヘッダー長と読み取り失敗を指定する。
    // Act
    int actual_ret = thread_connect_read_first_packet(static_cast<cplat_socket>(7), buf, sizeof(buf), &len,
                                                      100); // [手順] - 最初のパケットを読む。
    // Assert
    EXPECT_EQ(POTR_ERR_EOF, actual_ret); // [確認_異常系] - 受信結果を分類すること。
    EXPECT_EQ(999u, len);                // [確認_異常系] - 成功時だけ受信長を設定すること。
    EXPECT_EQ(1, reads);                 // [確認_正常系] - 必要な読み取りだけを実行すること。
}

TEST(threadConnectPrepareTest, first_packet_payload_error)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    uint8_t buf[POTR_PACKET_HEADER_SIZE + 4] = {};
    size_t len = 999;
    int reads = 0;
    ON_CALL(mock_cplat, cplat_socket_wait_readable(_, _, _, _))
        .WillByDefault(
            [](cplat_socket, int, int *out, cplat_error *)
            {
                *out = 1;
                return CPLAT_OK;
            });
    ON_CALL(mock_porter, potr_internal_result_from_error(_)).WillByDefault(Return(POTR_ERR_IO));
    ON_CALL(mock_porter, potr_internal_result_from_socket_result(_, _)).WillByDefault(Return(POTR_ERR_IO));
    ON_CALL(mock_cplat, cplat_socket_recv_all(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, void *out, size_t count, cplat_error *)
            {
                ++reads;
                if (reads == 2)
                    return CPLAT_ERR_UNKNOWN;
                std::memset(out, 0, count);
                if (reads == 1)
                {
                    uint16_t wire = delegate_real_cplat_hton16(4);
                    std::memcpy(static_cast<uint8_t *>(out) + 34, &wire, sizeof(wire));
                }
                return CPLAT_OK;
            }); // [状態] - 待機、ヘッダー長と読み取り失敗を指定する。
    // Act
    int actual_ret = thread_connect_read_first_packet(static_cast<cplat_socket>(7), buf, sizeof(buf), &len,
                                                      100); // [手順] - 最初のパケットを読む。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 受信結果を分類すること。
    EXPECT_EQ(999u, len);               // [確認_異常系] - 成功時だけ受信長を設定すること。
    EXPECT_EQ(2, reads);                // [確認_正常系] - 必要な読み取りだけを実行すること。
}

TEST(threadConnectPrepareTest, first_packet_oversize)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    uint8_t buf[POTR_PACKET_HEADER_SIZE + 4] = {};
    size_t len = 999;
    int reads = 0;
    ON_CALL(mock_cplat, cplat_socket_wait_readable(_, _, _, _))
        .WillByDefault(
            [](cplat_socket, int, int *out, cplat_error *)
            {
                *out = 1;
                return CPLAT_OK;
            });
    ON_CALL(mock_porter, potr_internal_result_from_error(_)).WillByDefault(Return(POTR_ERR_IO));
    ON_CALL(mock_porter, potr_internal_result_from_socket_result(_, _)).WillByDefault(Return(POTR_ERR_IO));
    ON_CALL(mock_cplat, cplat_socket_recv_all(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, void *out, size_t count, cplat_error *)
            {
                ++reads;
                if (reads == -1)
                    return CPLAT_ERR_UNKNOWN;
                std::memset(out, 0, count);
                if (reads == 1)
                {
                    uint16_t wire = delegate_real_cplat_hton16(5);
                    std::memcpy(static_cast<uint8_t *>(out) + 34, &wire, sizeof(wire));
                }
                return CPLAT_OK;
            }); // [状態] - 待機、ヘッダー長と読み取り失敗を指定する。
    // Act
    int actual_ret = thread_connect_read_first_packet(static_cast<cplat_socket>(7), buf, sizeof(buf), &len,
                                                      100); // [手順] - 最初のパケットを読む。
    // Assert
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret); // [確認_異常系] - 受信結果を分類すること。
    EXPECT_EQ(999u, len);                     // [確認_異常系] - 成功時だけ受信長を設定すること。
    EXPECT_EQ(1, reads);                      // [確認_正常系] - 必要な読み取りだけを実行すること。
}

TEST(threadConnectPrepareTest, connect_open_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.connect_timeout_ms = 250;
    ctx.service.src_port = 0;
    cplat_atomic_store_i32(&ctx.connect_thread_running[0], 1, CPLAT_MEMORY_ORDER_RELEASE);
    std::vector<int> waits;
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(7);
                return CPLAT_ERR_UNKNOWN;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_connect(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    ON_CALL(mock_cplat, cplat_socket_wait_writable(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, int timeout, int *out, cplat_error *)
            {
                waits.push_back(timeout);
                *out = 1;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_get_pending_error(_, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 接続の段階と停止フラグを指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(7)))
        .Times(0); // [Pre-Assert確認_異常系] - 生成済み失敗ソケットだけを閉じること。
    // Act
    cplat_socket actual_ret = thread_connect_with_timeout(&ctx, 0); // [手順] - 接続を試す。
    // Assert
    EXPECT_EQ(CPLAT_INVALID_SOCKET, actual_ret); // [確認_異常系] - 成功時だけ所有権を返すこと。
}

TEST(threadConnectPrepareTest, connect_bind_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.connect_timeout_ms = 250;
    ctx.service.src_port = 123;
    cplat_atomic_store_i32(&ctx.connect_thread_running[0], 1, CPLAT_MEMORY_ORDER_RELEASE);
    std::vector<int> waits;
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(7);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    ON_CALL(mock_cplat, cplat_socket_connect(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    ON_CALL(mock_cplat, cplat_socket_wait_writable(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, int timeout, int *out, cplat_error *)
            {
                waits.push_back(timeout);
                *out = 1;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_get_pending_error(_, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 接続の段階と停止フラグを指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(7)))
        .Times(1); // [Pre-Assert確認_異常系] - 生成済み失敗ソケットだけを閉じること。
    // Act
    cplat_socket actual_ret = thread_connect_with_timeout(&ctx, 0); // [手順] - 接続を試す。
    // Assert
    EXPECT_EQ(CPLAT_INVALID_SOCKET, actual_ret); // [確認_異常系] - 成功時だけ所有権を返すこと。
}

TEST(threadConnectPrepareTest, connect_blocking_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.connect_timeout_ms = 0;
    ctx.service.src_port = 0;
    cplat_atomic_store_i32(&ctx.connect_thread_running[0], 1, CPLAT_MEMORY_ORDER_RELEASE);
    std::vector<int> waits;
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(7);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_connect(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    ON_CALL(mock_cplat, cplat_socket_wait_writable(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, int timeout, int *out, cplat_error *)
            {
                waits.push_back(timeout);
                *out = 1;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_get_pending_error(_, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 接続の段階と停止フラグを指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(7)))
        .Times(1); // [Pre-Assert確認_異常系] - 生成済み失敗ソケットだけを閉じること。
    // Act
    cplat_socket actual_ret = thread_connect_with_timeout(&ctx, 0); // [手順] - 接続を試す。
    // Assert
    EXPECT_EQ(CPLAT_INVALID_SOCKET, actual_ret); // [確認_異常系] - 成功時だけ所有権を返すこと。
}

TEST(threadConnectPrepareTest, connect_blocking_success)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.connect_timeout_ms = 0;
    ctx.service.src_port = 0;
    cplat_atomic_store_i32(&ctx.connect_thread_running[0], 1, CPLAT_MEMORY_ORDER_RELEASE);
    std::vector<int> waits;
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(7);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_connect(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_wait_writable(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, int timeout, int *out, cplat_error *)
            {
                waits.push_back(timeout);
                *out = 1;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_get_pending_error(_, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 接続の段階と停止フラグを指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(7)))
        .Times(0); // [Pre-Assert確認_正常系] - 生成済み失敗ソケットだけを閉じること。
    // Act
    cplat_socket actual_ret = thread_connect_with_timeout(&ctx, 0); // [手順] - 接続を試す。
    // Assert
    EXPECT_EQ(static_cast<cplat_socket>(7), actual_ret); // [確認_正常系] - 成功時だけ所有権を返すこと。
}

TEST(threadConnectPrepareTest, connect_immediate_success)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.connect_timeout_ms = 250;
    ctx.service.src_port = 0;
    cplat_atomic_store_i32(&ctx.connect_thread_running[0], 1, CPLAT_MEMORY_ORDER_RELEASE);
    std::vector<int> waits;
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(7);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_connect(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_wait_writable(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, int timeout, int *out, cplat_error *)
            {
                waits.push_back(timeout);
                *out = 1;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_get_pending_error(_, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 接続の段階と停止フラグを指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(7)))
        .Times(0); // [Pre-Assert確認_正常系] - 生成済み失敗ソケットだけを閉じること。
    // Act
    cplat_socket actual_ret = thread_connect_with_timeout(&ctx, 0); // [手順] - 接続を試す。
    // Assert
    EXPECT_EQ(static_cast<cplat_socket>(7), actual_ret); // [確認_正常系] - 成功時だけ所有権を返すこと。
}

TEST(threadConnectPrepareTest, connect_connect_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.connect_timeout_ms = 250;
    ctx.service.src_port = 0;
    cplat_atomic_store_i32(&ctx.connect_thread_running[0], 1, CPLAT_MEMORY_ORDER_RELEASE);
    std::vector<int> waits;
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(7);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_connect(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    ON_CALL(mock_cplat, cplat_socket_wait_writable(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, int timeout, int *out, cplat_error *)
            {
                waits.push_back(timeout);
                *out = 1;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_get_pending_error(_, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 接続の段階と停止フラグを指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(7)))
        .Times(1); // [Pre-Assert確認_異常系] - 生成済み失敗ソケットだけを閉じること。
    // Act
    cplat_socket actual_ret = thread_connect_with_timeout(&ctx, 0); // [手順] - 接続を試す。
    // Assert
    EXPECT_EQ(CPLAT_INVALID_SOCKET, actual_ret); // [確認_異常系] - 成功時だけ所有権を返すこと。
}

TEST(threadConnectPrepareTest, connect_deadline)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.connect_timeout_ms = 250;
    ctx.service.src_port = 0;
    cplat_atomic_store_i32(&ctx.connect_thread_running[0], 1, CPLAT_MEMORY_ORDER_RELEASE);
    std::vector<int> waits;
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(7);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_connect(_, _, _)).WillByDefault(Return(CPLAT_ERR_IN_PROGRESS));
    ON_CALL(mock_cplat, cplat_socket_wait_writable(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, int timeout, int *out, cplat_error *)
            {
                waits.push_back(timeout);
                *out = 0;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_get_pending_error(_, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 接続の段階と停止フラグを指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(7)))
        .Times(1); // [Pre-Assert確認_異常系] - 生成済み失敗ソケットだけを閉じること。
    // Act
    cplat_socket actual_ret = thread_connect_with_timeout(&ctx, 0); // [手順] - 接続を試す。
    // Assert
    EXPECT_EQ(CPLAT_INVALID_SOCKET, actual_ret);        // [確認_異常系] - 成功時だけ所有権を返すこと。
    EXPECT_EQ((std::vector<int>{100, 100, 50}), waits); // [確認_異常系] - 期限まで 100、100、50 ms の順に待つこと。
}

TEST(threadConnectPrepareTest, connect_stopped)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.connect_timeout_ms = 250;
    ctx.service.src_port = 0;
    cplat_atomic_store_i32(&ctx.connect_thread_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
    std::vector<int> waits;
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(7);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_connect(_, _, _)).WillByDefault(Return(CPLAT_ERR_IN_PROGRESS));
    ON_CALL(mock_cplat, cplat_socket_wait_writable(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, int timeout, int *out, cplat_error *)
            {
                waits.push_back(timeout);
                *out = 1;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_get_pending_error(_, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 接続の段階と停止フラグを指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(7)))
        .Times(1); // [Pre-Assert確認_異常系] - 生成済み失敗ソケットだけを閉じること。
    // Act
    cplat_socket actual_ret = thread_connect_with_timeout(&ctx, 0); // [手順] - 接続を試す。
    // Assert
    EXPECT_EQ(CPLAT_INVALID_SOCKET, actual_ret); // [確認_異常系] - 成功時だけ所有権を返すこと。
    EXPECT_TRUE(waits.empty());                  // [確認_異常系] - 停止要求時は待機しないこと。
}

TEST(threadConnectPrepareTest, connect_wait_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.connect_timeout_ms = 250;
    ctx.service.src_port = 0;
    cplat_atomic_store_i32(&ctx.connect_thread_running[0], 1, CPLAT_MEMORY_ORDER_RELEASE);
    std::vector<int> waits;
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(7);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_connect(_, _, _)).WillByDefault(Return(CPLAT_ERR_IN_PROGRESS));
    ON_CALL(mock_cplat, cplat_socket_wait_writable(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, int timeout, int *out, cplat_error *)
            {
                waits.push_back(timeout);
                *out = 0;
                return CPLAT_ERR_UNKNOWN;
            });
    ON_CALL(mock_cplat, cplat_socket_get_pending_error(_, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 接続の段階と停止フラグを指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(7)))
        .Times(1); // [Pre-Assert確認_異常系] - 生成済み失敗ソケットだけを閉じること。
    // Act
    cplat_socket actual_ret = thread_connect_with_timeout(&ctx, 0); // [手順] - 接続を試す。
    // Assert
    EXPECT_EQ(CPLAT_INVALID_SOCKET, actual_ret); // [確認_異常系] - 成功時だけ所有権を返すこと。
}

TEST(threadConnectPrepareTest, connect_pending_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.connect_timeout_ms = 250;
    ctx.service.src_port = 0;
    cplat_atomic_store_i32(&ctx.connect_thread_running[0], 1, CPLAT_MEMORY_ORDER_RELEASE);
    std::vector<int> waits;
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(7);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_connect(_, _, _)).WillByDefault(Return(CPLAT_ERR_IN_PROGRESS));
    ON_CALL(mock_cplat, cplat_socket_wait_writable(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, int timeout, int *out, cplat_error *)
            {
                waits.push_back(timeout);
                *out = 1;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_get_pending_error(_, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    // [状態] - 接続の段階と停止フラグを指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(7)))
        .Times(1); // [Pre-Assert確認_異常系] - 生成済み失敗ソケットだけを閉じること。
    // Act
    cplat_socket actual_ret = thread_connect_with_timeout(&ctx, 0); // [手順] - 接続を試す。
    // Assert
    EXPECT_EQ(CPLAT_INVALID_SOCKET, actual_ret); // [確認_異常系] - 成功時だけ所有権を返すこと。
}

TEST(threadConnectPrepareTest, connect_poll_success)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.connect_timeout_ms = 250;
    ctx.service.src_port = 0;
    cplat_atomic_store_i32(&ctx.connect_thread_running[0], 1, CPLAT_MEMORY_ORDER_RELEASE);
    std::vector<int> waits;
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(7);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_connect(_, _, _)).WillByDefault(Return(CPLAT_ERR_IN_PROGRESS));
    ON_CALL(mock_cplat, cplat_socket_wait_writable(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, int timeout, int *out, cplat_error *)
            {
                waits.push_back(timeout);
                *out = 1;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_get_pending_error(_, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 接続の段階と停止フラグを指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(7)))
        .Times(0); // [Pre-Assert確認_正常系] - 生成済み失敗ソケットだけを閉じること。
    // Act
    cplat_socket actual_ret = thread_connect_with_timeout(&ctx, 0); // [手順] - 接続を試す。
    // Assert
    EXPECT_EQ(static_cast<cplat_socket>(7), actual_ret); // [確認_正常系] - 成功時だけ所有権を返すこと。
}

TEST(threadConnectPrepareTest, source_unrestricted)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ctx.service.src_addr[0][0] = 0;
    ctx.src_addr_resolved[0] = 42;
    ctx.service.src_port = 0;
    cplat_ipv4_endpoint peer = {};
    peer.address = 2;
    peer.port = delegate_real_cplat_hton16(200); // [状態] - 接続元と照合条件を指定する。
    // Act
    int actual_ret = thread_connect_peer_allowed(&ctx, 0, &peer); // [手順] - 接続元を照合する。
    // Assert
    EXPECT_EQ(1, actual_ret); // [確認_正常系] - 設定された条件で採用可否を返すこと。
}

TEST(threadConnectPrepareTest, source_match)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ctx.service.src_addr[0][0] = 1;
    ctx.src_addr_resolved[0] = 42;
    ctx.service.src_port = 123;
    cplat_ipv4_endpoint peer = {};
    peer.address = 42;
    peer.port = delegate_real_cplat_hton16(123); // [状態] - 接続元と照合条件を指定する。
    // Act
    int actual_ret = thread_connect_peer_allowed(&ctx, 0, &peer); // [手順] - 接続元を照合する。
    // Assert
    EXPECT_EQ(1, actual_ret); // [確認_正常系] - 設定された条件で採用可否を返すこと。
}

TEST(threadConnectPrepareTest, source_address_mismatch)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ctx.service.src_addr[0][0] = 1;
    ctx.src_addr_resolved[0] = 42;
    ctx.service.src_port = 123;
    cplat_ipv4_endpoint peer = {};
    peer.address = 9;
    peer.port = delegate_real_cplat_hton16(123); // [状態] - 接続元と照合条件を指定する。
    // Act
    int actual_ret = thread_connect_peer_allowed(&ctx, 0, &peer); // [手順] - 接続元を照合する。
    // Assert
    EXPECT_EQ(0, actual_ret); // [確認_正常系] - 設定された条件で採用可否を返すこと。
}

TEST(threadConnectPrepareTest, source_port_mismatch)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ctx.service.src_addr[0][0] = 1;
    ctx.src_addr_resolved[0] = 42;
    ctx.service.src_port = 123;
    cplat_ipv4_endpoint peer = {};
    peer.address = 42;
    peer.port = delegate_real_cplat_hton16(200); // [状態] - 接続元と照合条件を指定する。
    // Act
    int actual_ret = thread_connect_peer_allowed(&ctx, 0, &peer); // [手順] - 接続元を照合する。
    // Assert
    EXPECT_EQ(0, actual_ret); // [確認_正常系] - 設定された条件で採用可否を返すこと。
}
