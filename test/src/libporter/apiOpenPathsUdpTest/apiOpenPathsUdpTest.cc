#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <porter/porter_result.h>
#include <cstring>
using namespace testing;

TEST(apiOpenPathsUdpTest, unicast_requires_destination_port)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    potr_context ctx = {}; // [状態] - 必須ポートを未設定にする。
    // Act
    int actual_ret = api_open_paths_unicast(&ctx, POTR_ROLE_SENDER); // [手順] - ソケット準備を試す。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, actual_ret); // [確認_異常系] - 必須ポートの欠落を設定エラーとして拒否すること。
}

TEST(apiOpenPathsUdpTest, multicast_requires_destination_port)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    potr_context ctx = {}; // [状態] - 必須ポートを未設定にする。
    // Act
    int actual_ret = api_open_paths_multicast(&ctx, POTR_ROLE_RECEIVER); // [手順] - ソケット準備を試す。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, actual_ret); // [確認_異常系] - 必須ポートの欠落を設定エラーとして拒否すること。
}

TEST(apiOpenPathsUdpTest, broadcast_requires_destination_port)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    potr_context ctx = {}; // [状態] - 必須ポートを未設定にする。
    // Act
    int actual_ret = api_open_paths_broadcast(&ctx, POTR_ROLE_SENDER); // [手順] - ソケット準備を試す。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, actual_ret); // [確認_異常系] - 必須ポートの欠落を設定エラーとして拒否すること。
}

TEST(apiOpenPathsUdpTest, bidir_requires_destination_port)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    potr_context ctx = {}; // [状態] - 必須ポートを未設定にする。
    // Act
    int actual_ret = api_open_paths_unicast_bidir(&ctx, POTR_ROLE_SENDER); // [手順] - ソケット準備を試す。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, actual_ret); // [確認_異常系] - 必須ポートの欠落を設定エラーとして拒否すること。
}

TEST(apiOpenPathsUdpTest, n1_requires_destination_port)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    potr_context ctx = {}; // [状態] - 必須ポートを未設定にする。
    // Act
    int actual_ret = api_open_paths_unicast_bidir_n1(&ctx); // [手順] - ソケット準備を試す。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, actual_ret); // [確認_異常系] - 必須ポートの欠落を設定エラーとして拒否すること。
}

TEST(apiOpenPathsUdpTest, unicast_success)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    potr_context ctx = {};
    ctx.service.dst_port = 1234;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.dst_addr[0], "dst", sizeof("dst"));

    int opens = 0;
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [&](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(++opens);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _))
        .WillByDefault([&](cplat_socket, const cplat_ipv4_endpoint *, cplat_error *) { return CPLAT_OK; });
    // [状態] - ソケット生成と bind の結果を制御する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(2))).Times(0);
    // [Pre-Assert確認_正常系] - 未登録ソケットを 閉じないこと。
    // Act
    int actual_ret = api_open_paths_unicast(&ctx, POTR_ROLE_SENDER); // [手順] - unicast を準備する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);                       // [確認_正常系] - 準備結果を返すこと。
    EXPECT_EQ(1, ctx.n_path);                             // [確認_正常系] - 成功したパスを保持すること。
    EXPECT_EQ(static_cast<cplat_socket>(1), ctx.sock[0]); // [確認_正常系] - 登録済みソケットの所有権を保持すること。
}

TEST(apiOpenPathsUdpTest, unicast_second_bind_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    potr_context ctx = {};
    ctx.service.dst_port = 1234;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.dst_addr[0], "dst", sizeof("dst"));
    std::memcpy(ctx.service.src_addr[1], "src2", sizeof("src2"));
    std::memcpy(ctx.service.dst_addr[1], "dst2", sizeof("dst2"));
    int opens = 0;
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [&](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(++opens);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _))
        .WillByDefault([&](cplat_socket, const cplat_ipv4_endpoint *, cplat_error *)
                       { return opens == 2 ? CPLAT_ERR_UNKNOWN : CPLAT_OK; });
    // [状態] - ソケット生成と bind の結果を制御する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(2))).Times(1);
    // [Pre-Assert確認_異常系] - 未登録ソケットを 1 回閉じること。
    // Act
    int actual_ret = api_open_paths_unicast(&ctx, POTR_ROLE_SENDER); // [手順] - unicast を準備する。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret);                   // [確認_異常系] - 準備結果を返すこと。
    EXPECT_EQ(1, ctx.n_path);                             // [確認_正常系] - 成功したパスを保持すること。
    EXPECT_EQ(static_cast<cplat_socket>(1), ctx.sock[0]); // [確認_正常系] - 登録済みソケットの所有権を保持すること。
}

TEST(apiOpenPathsUdpTest, multicast_success)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    potr_context ctx = {};
    ctx.service.dst_port = 1234;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.multicast_group, "239.1.2.3", sizeof("239.1.2.3"));
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(9);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_set_broadcast(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 方式に必要な設定と bind の結果を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(9))).Times(0);
    // [Pre-Assert確認_正常系] - 未登録ソケットを 閉じないこと。
    // Act
    int actual_ret = api_open_paths_multicast(&ctx, POTR_ROLE_RECEIVER); // [手順] - 方式別の経路を準備する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 準備結果を保持すること。
    EXPECT_EQ(1, ctx.n_path);       // [確認_正常系] - 成功したパスだけを数えること。
}

TEST(apiOpenPathsUdpTest, multicast_operation_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    potr_context ctx = {};
    ctx.service.dst_port = 1234;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.multicast_group, "239.1.2.3", sizeof("239.1.2.3"));
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(9);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    ON_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_set_broadcast(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 方式に必要な設定と bind の結果を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(9))).Times(1);
    // [Pre-Assert確認_異常系] - 未登録ソケットを 1 回閉じること。
    // Act
    int actual_ret = api_open_paths_multicast(&ctx, POTR_ROLE_RECEIVER); // [手順] - 方式別の経路を準備する。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 準備結果を保持すること。
    EXPECT_EQ(0, ctx.n_path);           // [確認_異常系] - 成功したパスだけを数えること。
}

TEST(apiOpenPathsUdpTest, broadcast_success)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    potr_context ctx = {};
    ctx.service.dst_port = 1234;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.multicast_group, "239.1.2.3", sizeof("239.1.2.3"));
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(9);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_set_broadcast(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 方式に必要な設定と bind の結果を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(9))).Times(0);
    // [Pre-Assert確認_正常系] - 未登録ソケットを 閉じないこと。
    // Act
    int actual_ret = api_open_paths_broadcast(&ctx, POTR_ROLE_RECEIVER); // [手順] - 方式別の経路を準備する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 準備結果を保持すること。
    EXPECT_EQ(1, ctx.n_path);       // [確認_正常系] - 成功したパスだけを数えること。
}

TEST(apiOpenPathsUdpTest, broadcast_operation_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    potr_context ctx = {};
    ctx.service.dst_port = 1234;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.multicast_group, "239.1.2.3", sizeof("239.1.2.3"));
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(9);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    ON_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_set_broadcast(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 方式に必要な設定と bind の結果を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(9))).Times(1);
    // [Pre-Assert確認_異常系] - 未登録ソケットを 1 回閉じること。
    // Act
    int actual_ret = api_open_paths_broadcast(&ctx, POTR_ROLE_RECEIVER); // [手順] - 方式別の経路を準備する。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 準備結果を保持すること。
    EXPECT_EQ(0, ctx.n_path);           // [確認_異常系] - 成功したパスだけを数えること。
}

TEST(apiOpenPathsUdpTest, dynamic_bidir_success)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    potr_context ctx = {};
    ctx.service.dst_port = 1234;

    std::memcpy(ctx.service.multicast_group, "239.1.2.3", sizeof("239.1.2.3"));
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(9);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_set_broadcast(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 方式に必要な設定と bind の結果を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(9))).Times(0);
    // [Pre-Assert確認_正常系] - 未登録ソケットを 閉じないこと。
    // Act
    int actual_ret = api_open_paths_unicast_bidir(&ctx, POTR_ROLE_RECEIVER); // [手順] - 方式別の経路を準備する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 準備結果を保持すること。
    EXPECT_EQ(1, ctx.n_path);       // [確認_正常系] - 成功したパスだけを数えること。
}

TEST(apiOpenPathsUdpTest, dynamic_bidir_operation_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    potr_context ctx = {};
    ctx.service.dst_port = 1234;

    std::memcpy(ctx.service.multicast_group, "239.1.2.3", sizeof("239.1.2.3"));
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(9);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    ON_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_set_broadcast(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 方式に必要な設定と bind の結果を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(9))).Times(1);
    // [Pre-Assert確認_異常系] - 未登録ソケットを 1 回閉じること。
    // Act
    int actual_ret = api_open_paths_unicast_bidir(&ctx, POTR_ROLE_RECEIVER); // [手順] - 方式別の経路を準備する。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 準備結果を保持すること。
    EXPECT_EQ(0, ctx.n_path);           // [確認_異常系] - 成功したパスだけを数えること。
}

TEST(apiOpenPathsUdpTest, n1_success)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    potr_context ctx = {};
    ctx.service.dst_port = 1234;

    std::memcpy(ctx.service.multicast_group, "239.1.2.3", sizeof("239.1.2.3"));
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(9);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_set_broadcast(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 方式に必要な設定と bind の結果を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(9))).Times(0);
    // [Pre-Assert確認_正常系] - 未登録ソケットを 閉じないこと。
    // Act
    int actual_ret = api_open_paths_unicast_bidir_n1(&ctx); // [手順] - 方式別の経路を準備する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 準備結果を保持すること。
    EXPECT_EQ(1, ctx.n_path);       // [確認_正常系] - 成功したパスだけを数えること。
}

TEST(apiOpenPathsUdpTest, n1_operation_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    potr_context ctx = {};
    ctx.service.dst_port = 1234;

    std::memcpy(ctx.service.multicast_group, "239.1.2.3", sizeof("239.1.2.3"));
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(9);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    ON_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_set_broadcast(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 方式に必要な設定と bind の結果を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(9))).Times(1);
    // [Pre-Assert確認_異常系] - 未登録ソケットを 1 回閉じること。
    // Act
    int actual_ret = api_open_paths_unicast_bidir_n1(&ctx); // [手順] - 方式別の経路を準備する。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 準備結果を保持すること。
    EXPECT_EQ(0, ctx.n_path);           // [確認_異常系] - 成功したパスだけを数えること。
}

TEST(apiOpenPathsUdpTest, n1_table_failure_preserves_socket_for_cleanup)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    potr_context ctx = {};
    ctx.service.dst_port = 1234;
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(5);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_porter, potr_internal_peer_table_init(_)).WillByDefault(Return(POTR_ERR_OUT_OF_MEMORY));
    // [状態] - ソケット生成後のピア テーブル初期化を失敗させる。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(_))
        .Times(0); // [Pre-Assert確認_異常系] - 登録済みソケットをその場で閉じないこと。
    // Act
    int actual_ret = api_open_paths_unicast_bidir_n1(&ctx); // [手順] - N:1 を準備する。
    // Assert
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, actual_ret);        // [確認_異常系] - テーブルの失敗を返すこと。
    EXPECT_EQ(1, ctx.n_path);                             // [確認_異常系] - 上位の解放対象パスを保持すること。
    EXPECT_EQ(static_cast<cplat_socket>(5), ctx.sock[0]); // [確認_異常系] - 上位が回収するソケットを保持すること。
}

TEST(apiOpenPathsUdpTest, resolution_failure_precedes_socket_creation)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42;
                return POTR_OK;
            });
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ctx.service.dst_port = 1234;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.dst_addr[0], "dst", sizeof("dst"));
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _)).WillByDefault(Return(POTR_ERR_INVALID_ARGUMENT));
    // [状態] - アドレス解決を失敗させる。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .Times(0); // [Pre-Assert確認_異常系] - 解決失敗後にソケットを生成しないこと。
    // Act
    int actual_ret = api_open_paths_unicast(&ctx, POTR_ROLE_RECEIVER); // [手順] - 経路を準備する。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - アドレスの結果分類を保持すること。
    EXPECT_EQ(0, ctx.n_path);                         // [確認_異常系] - 未完成の経路を数えないこと。
}

// 受信側が dst_addr1 の解決値でグループに参加することの確認
TEST(apiOpenPathsUdpTest, multicast_receiver_joins_with_dst_addr)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *host, uint32_t *out)
            {
                if (std::strcmp(host, "dst") == 0)
                {
                    *out = 77U;
                }
                else
                {
                    *out = 42U;
                }
                return POTR_OK;
            }); // [状態] - src は 42U、dst は 77U に解決する。
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42U;
                return POTR_OK;
            }); // [状態] - グループ アドレスの解析を成功させる。
    potr_context ctx = {};
    ctx.service.dst_port = 1234;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.dst_addr[0], "dst", sizeof("dst"));
    std::memcpy(ctx.service.multicast_group, "239.1.2.3", sizeof("239.1.2.3"));
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(9);
                return CPLAT_OK;
            }); // [状態] - ソケット生成を成功させる。
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - bind を成功させる。
    ON_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - グループ参加を成功させる。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, 77U, _)).Times(1);
    // [Pre-Assert確認_正常系] - cplat_socket_join_multicast_group() が 77U を指定して 1 回呼び出されること。
    EXPECT_CALL(mock_cplat, cplat_socket_close(_)).Times(0);
    // [Pre-Assert確認_正常系] - 未登録ソケットを閉じないこと。
    // Act
    int actual_ret = api_open_paths_multicast(&ctx, POTR_ROLE_RECEIVER); // [手順] - 受信側の経路を準備する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);                 // [確認_正常系] - api_open_paths_multicast() の戻り値が POTR_OK であること。
    EXPECT_EQ(1, ctx.n_path);                       // [確認_正常系] - ctx.n_path が 1 であること。
    EXPECT_EQ(77U, ctx.multicast_if_resolved[0]);   // [確認_正常系] - ctx.multicast_if_resolved[0] が 77U であること。(dst_addr1 の解決値を参加 NIC として保持したことの確認)
    EXPECT_EQ(42U, ctx.src_addr_resolved[0]);       // [確認_正常系] - ctx.src_addr_resolved[0] が 42U であること。(送信元の照合値を変えていないことの確認)
}

// 受信側が dst_addr1 を省略したとき src_addr1 の解決値でグループに参加することの確認
TEST(apiOpenPathsUdpTest, multicast_receiver_joins_with_src_addr_when_dst_omitted)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42U;
                return POTR_OK;
            }); // [状態] - アドレス解決を 42U で成功させる。
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42U;
                return POTR_OK;
            }); // [状態] - グループ アドレスの解析を成功させる。
    potr_context ctx = {};
    ctx.service.dst_port = 1234;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.multicast_group, "239.1.2.3", sizeof("239.1.2.3"));
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(9);
                return CPLAT_OK;
            }); // [状態] - ソケット生成を成功させる。
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - bind を成功させる。
    ON_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - グループ参加を成功させる。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, 42U, _)).Times(1);
    // [Pre-Assert確認_正常系] - cplat_socket_join_multicast_group() が 42U を指定して 1 回呼び出されること。
    // Act
    int actual_ret = api_open_paths_multicast(&ctx, POTR_ROLE_RECEIVER); // [手順] - 受信側の経路を準備する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);               // [確認_正常系] - api_open_paths_multicast() の戻り値が POTR_OK であること。
    EXPECT_EQ(1, ctx.n_path);                     // [確認_正常系] - ctx.n_path が 1 であること。
    EXPECT_EQ(42U, ctx.multicast_if_resolved[0]); // [確認_正常系] - ctx.multicast_if_resolved[0] が 42U であること。(省略時に src_addr1 の解決値を参加 NIC としたことの確認)
}

// 送信側は dst_addr1 があっても src_addr1 の解決値でグループに参加することの確認
TEST(apiOpenPathsUdpTest, multicast_sender_joins_with_src_addr)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *host, uint32_t *out)
            {
                if (std::strcmp(host, "dst") == 0)
                {
                    *out = 77U;
                }
                else
                {
                    *out = 42U;
                }
                return POTR_OK;
            }); // [状態] - src は 42U、dst は 77U に解決する。
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42U;
                return POTR_OK;
            }); // [状態] - グループ アドレスの解析を成功させる。
    potr_context ctx = {};
    ctx.service.dst_port = 1234;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.dst_addr[0], "dst", sizeof("dst"));
    std::memcpy(ctx.service.multicast_group, "239.1.2.3", sizeof("239.1.2.3"));
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(9);
                return CPLAT_OK;
            }); // [状態] - ソケット生成を成功させる。
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - bind を成功させる。
    ON_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - グループ参加を成功させる。
    ON_CALL(mock_cplat, cplat_socket_set_multicast_interface(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 送信インターフェイスの設定を成功させる。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, 42U, _)).Times(1);
    // [Pre-Assert確認_正常系] - cplat_socket_join_multicast_group() が 42U を指定して 1 回呼び出されること。
    EXPECT_CALL(mock_porter, api_paths_resolve_ipv4(_, _)).Times(1);
    // [Pre-Assert確認_正常系] - api_paths_resolve_ipv4() が 1 回呼び出されること。(dst_addr1 を解決しないことの確認)
    // Act
    int actual_ret = api_open_paths_multicast(&ctx, POTR_ROLE_SENDER); // [手順] - 送信側の経路を準備する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);               // [確認_正常系] - api_open_paths_multicast() の戻り値が POTR_OK であること。
    EXPECT_EQ(1, ctx.n_path);                     // [確認_正常系] - ctx.n_path が 1 であること。
    EXPECT_EQ(42U, ctx.multicast_if_resolved[0]); // [確認_正常系] - ctx.multicast_if_resolved[0] が 42U であること。(送信側が src_addr1 の解決値を参加 NIC としたことの確認)
}

// 送信側で送信インターフェイスの設定に失敗した場合に、ソケットを閉じて POTR_ERR_IO を返すことの確認
TEST(apiOpenPathsUdpTest, multicast_sender_set_interface_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42U;
                return POTR_OK;
            }); // [状態] - src を 42U に解決する。
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42U;
                return POTR_OK;
            }); // [状態] - グループ アドレスの解析を成功させる。
    potr_context ctx = {};
    ctx.service.dst_port = 1234;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.multicast_group, "239.1.2.3", sizeof("239.1.2.3"));
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(9);
                return CPLAT_OK;
            }); // [状態] - ソケット生成を成功させる。
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - bind を成功させる。
    ON_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - グループ参加を成功させる。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_set_multicast_interface(static_cast<cplat_socket>(9), 42U, _))
        .WillOnce(Return(CPLAT_ERR_UNKNOWN));
    // [Pre-Assert確認_異常系] - cplat_socket_set_multicast_interface() が 9、42U を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - cplat_socket_set_multicast_interface() から CPLAT_ERR_UNKNOWN を返す。
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(9))).Times(1);
    // [Pre-Assert確認_異常系] - cplat_socket_close() が 9 を指定して 1 回呼び出されること。
    // Act
    int actual_ret = api_open_paths_multicast(&ctx, POTR_ROLE_SENDER); // [手順] - 送信側の経路を準備する。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - api_open_paths_multicast() の戻り値が POTR_ERR_IO であること。
}

// dst_addr2 だけの経路を列挙しないことの確認
TEST(apiOpenPathsUdpTest, multicast_ignores_dst_only_path)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *host, uint32_t *out)
            {
                if (std::strcmp(host, "dst2") == 0)
                {
                    *out = 77U;
                }
                else
                {
                    *out = 42U;
                }
                return POTR_OK;
            }); // [状態] - src は 42U、dst2 は 77U に解決する。
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42U;
                return POTR_OK;
            }); // [状態] - グループ アドレスの解析を成功させる。
    potr_context ctx = {};
    ctx.service.dst_port = 1234;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.dst_addr[1], "dst2", sizeof("dst2"));
    std::memcpy(ctx.service.multicast_group, "239.1.2.3", sizeof("239.1.2.3"));
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(9);
                return CPLAT_OK;
            }); // [状態] - ソケット生成を成功させる。
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - bind を成功させる。
    ON_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - グループ参加を成功させる。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, 42U, _)).Times(1);
    // [Pre-Assert確認_正常系] - cplat_socket_join_multicast_group() が 42U を指定して 1 回呼び出されること。
    EXPECT_CALL(mock_porter, api_paths_resolve_ipv4(_, _)).Times(1);
    // [Pre-Assert確認_正常系] - api_paths_resolve_ipv4() が 1 回呼び出されること。(dst_addr2 だけの経路を解決しないことの確認)
    // Act
    int actual_ret = api_open_paths_multicast(&ctx, POTR_ROLE_RECEIVER); // [手順] - 受信側の経路を準備する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - api_open_paths_multicast() の戻り値が POTR_OK であること。
    EXPECT_EQ(1, ctx.n_path);       // [確認_正常系] - ctx.n_path が 1 であること。(dst_addr2 だけの経路を数えないことの確認)
}

// 受信側で dst_addr1 の解決に失敗したとき open が失敗することの確認
TEST(apiOpenPathsUdpTest, multicast_receiver_dst_resolution_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *host, uint32_t *out)
            {
                if (std::strcmp(host, "dst") == 0)
                {
                    return POTR_ERR_INVALID_ARGUMENT;
                }
                *out = 42U;
                return POTR_OK;
            }); // [状態] - src の解決は成功させ、dst の解決は失敗させる。
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42U;
                return POTR_OK;
            }); // [状態] - グループ アドレスの解析を成功させる。
    potr_context ctx = {};
    ctx.service.dst_port = 1234;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.dst_addr[0], "dst", sizeof("dst"));
    std::memcpy(ctx.service.multicast_group, "239.1.2.3", sizeof("239.1.2.3"));
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_open(_, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - 解決失敗後にソケットを生成しないこと。
    // Act
    int actual_ret = api_open_paths_multicast(&ctx, POTR_ROLE_RECEIVER); // [手順] - 受信側の経路を準備する。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - api_open_paths_multicast() の戻り値が POTR_ERR_INVALID_ARGUMENT であること。(解決関数の結果を返したことの確認)
    EXPECT_EQ(0, ctx.n_path);                         // [確認_異常系] - ctx.n_path が 0 であること。
}

// udp_receive_buffer_size の値の決まり方 (既定値、global、service 優先、DISABLED の扱い) の確認
TEST(apiOpenPathsUdpTest, udp_receive_buffer_size_resolution)
{
    // Arrange
    potr_service_def svc = {};
    potr_global_config glb = {};
    // [状態] - サービス定義とグローバル設定を 0 初期化する。

    // Pre-Assert

    // Act
    uint32_t default_val = resolve_udp_receive_buffer_size(&svc, &glb);
    // [手順] - 両方 0 で解決する。
    glb.udp_receive_buffer_size = 1048576U;
    uint32_t global_val = resolve_udp_receive_buffer_size(&svc, &glb);
    // [手順] - global に 1048576U、service に 0 で解決する。
    svc.udp_receive_buffer_size = 524288U;
    uint32_t service_val = resolve_udp_receive_buffer_size(&svc, &glb);
    // [手順] - service に 524288U を指定して解決する。
    svc.udp_receive_buffer_size = POTR_FLOW_CONTROL_DISABLED;
    uint32_t service_disabled_val = resolve_udp_receive_buffer_size(&svc, &glb);
    // [手順] - service に POTR_FLOW_CONTROL_DISABLED を指定して解決する。
    svc.udp_receive_buffer_size = 0U;
    glb.udp_receive_buffer_size = POTR_FLOW_CONTROL_DISABLED;
    uint32_t global_disabled_val = resolve_udp_receive_buffer_size(&svc, &glb);
    // [手順] - global に POTR_FLOW_CONTROL_DISABLED、service に 0 を指定して解決する。
    svc.udp_receive_buffer_size = 262144U;
    uint32_t service_over_global_disabled_val = resolve_udp_receive_buffer_size(&svc, &glb);
    // [手順] - global が POTR_FLOW_CONTROL_DISABLED のまま、service に 262144U を指定して解決する。

    // Assert
    EXPECT_EQ(POTR_DEFAULT_UDP_RECEIVE_BUFFER_SIZE, default_val); // [確認_正常系] - 両方 0 のとき既定値 (4194304U) であること。
    EXPECT_EQ(1048576U, global_val); // [確認_正常系] - service が 0 のとき global の値であること。
    EXPECT_EQ(524288U, service_val); // [確認_正常系] - service が非 0 のとき service の値が優先されること。
    EXPECT_EQ(0U, service_disabled_val); // [確認_正常系] - service が DISABLED のとき 0U (要求しない) であること。
    EXPECT_EQ(0U, global_disabled_val); // [確認_正常系] - global が DISABLED かつ service が 0 のとき 0U (要求しない) であること。
    EXPECT_EQ(262144U, service_over_global_disabled_val); // [確認_正常系] - global の DISABLED を service の値で上書きできること。
}

// UDP の全通信種別 (送信者・受信者、片方向・双方向・N 対 1) で cplat_socket_set_receive_buffer_size() を呼ぶことの確認
TEST(apiOpenPathsUdpTest, udp_receive_buffer_size_requested_on_all_udp_paths)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42U;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42U;
                return POTR_OK;
            });
    ON_CALL(mock_porter, potr_internal_peer_table_init(_)).WillByDefault(Return(POTR_OK));
    int next_sock = 10;
    ON_CALL(mock_cplat, cplat_socket_open(CPLAT_SOCKET_UDP, _, _))
        .WillByDefault(
            [&](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(++next_sock);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_set_multicast_interface(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_set_broadcast(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_get_receive_buffer_size(_, _, _))
        .WillByDefault(
            [](cplat_socket, size_t *size_out, cplat_error *)
            {
                *size_out = POTR_DEFAULT_UDP_RECEIVE_BUFFER_SIZE;
                return CPLAT_OK;
            });
    // [状態] - UDP ソケットの作成・設定をすべて成功させ、取得値は要求値と同じにする。

    // Pre-Assert
    EXPECT_CALL(mock_cplat,
                cplat_socket_set_receive_buffer_size(_, static_cast<size_t>(POTR_DEFAULT_UDP_RECEIVE_BUFFER_SIZE), _))
        .Times(11)
        .WillRepeatedly(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - UDP の全 11 経路で既定値 (4 MiB) の要求が 1 回ずつ呼ばれること。

    // Act
    potr_context ctx = {};
    ctx.service.dst_port = 5000;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.dst_addr[0], "dst", sizeof("dst"));
    int ret_unicast_sender = api_open_paths_unicast(&ctx, POTR_ROLE_SENDER);
    ctx.n_path = 0;
    int ret_unicast_receiver = api_open_paths_unicast(&ctx, POTR_ROLE_RECEIVER);
    // [手順] - unicast の送信者と受信者を開く。

    ctx = {};
    ctx.service.dst_port = 6000;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.multicast_group, "239.0.0.1", sizeof("239.0.0.1"));
    int ret_mcast_sender = api_open_paths_multicast(&ctx, POTR_ROLE_SENDER);
    ctx.n_path = 0;
    int ret_mcast_receiver = api_open_paths_multicast(&ctx, POTR_ROLE_RECEIVER);
    // [手順] - multicast の送信者と受信者を開く。

    ctx = {};
    ctx.service.dst_port = 7000;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    int ret_bcast_sender = api_open_paths_broadcast(&ctx, POTR_ROLE_SENDER);
    ctx.n_path = 0;
    int ret_bcast_receiver = api_open_paths_broadcast(&ctx, POTR_ROLE_RECEIVER);
    // [手順] - broadcast の送信者と受信者を開く。

    ctx = {};
    ctx.service.dst_port = 8000;
    int ret_bidir_dyn_receiver = api_open_paths_unicast_bidir(&ctx, POTR_ROLE_RECEIVER);
    ctx = {};
    ctx.service.dst_port = 8000;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.dst_addr[0], "dst", sizeof("dst"));
    int ret_bidir_sender = api_open_paths_unicast_bidir(&ctx, POTR_ROLE_SENDER);
    ctx.n_path = 0;
    int ret_bidir_receiver = api_open_paths_unicast_bidir(&ctx, POTR_ROLE_RECEIVER);
    // [手順] - unicast_bidir の動的受信者、1:1 送信者、1:1 受信者を開く。

    ctx = {};
    ctx.service.dst_port = 9000;
    int ret_n1_any = api_open_paths_unicast_bidir_n1(&ctx);
    ctx = {};
    ctx.service.dst_port = 9000;
    std::memcpy(ctx.service.dst_addr[0], "dst", sizeof("dst"));
    int ret_n1_addr = api_open_paths_unicast_bidir_n1(&ctx);
    // [手順] - unicast_bidir_n1 の dst_addr 省略と dst_addr 指定を開く。

    // Assert
    EXPECT_EQ(POTR_OK, ret_unicast_sender);     // [確認_正常系] - unicast 送信者が成功すること。
    EXPECT_EQ(POTR_OK, ret_unicast_receiver);   // [確認_正常系] - unicast 受信者が成功すること。
    EXPECT_EQ(POTR_OK, ret_mcast_sender);       // [確認_正常系] - multicast 送信者が成功すること。
    EXPECT_EQ(POTR_OK, ret_mcast_receiver);     // [確認_正常系] - multicast 受信者が成功すること。
    EXPECT_EQ(POTR_OK, ret_bcast_sender);       // [確認_正常系] - broadcast 送信者が成功すること。
    EXPECT_EQ(POTR_OK, ret_bcast_receiver);     // [確認_正常系] - broadcast 受信者が成功すること。
    EXPECT_EQ(POTR_OK, ret_bidir_dyn_receiver); // [確認_正常系] - unicast_bidir 動的受信者が成功すること。
    EXPECT_EQ(POTR_OK, ret_bidir_sender);       // [確認_正常系] - unicast_bidir 送信者が成功すること。
    EXPECT_EQ(POTR_OK, ret_bidir_receiver);     // [確認_正常系] - unicast_bidir 受信者が成功すること。
    EXPECT_EQ(POTR_OK, ret_n1_any);             // [確認_正常系] - unicast_bidir_n1 (INADDR_ANY) が成功すること。
    EXPECT_EQ(POTR_OK, ret_n1_addr);            // [確認_正常系] - unicast_bidir_n1 (dst_addr 指定) が成功すること。
}

// udp_receive_buffer_size が「要求しない」(POTR_FLOW_CONTROL_DISABLED) のとき、要求を呼ばないことの確認
TEST(apiOpenPathsUdpTest, udp_receive_buffer_size_disabled_skips_request)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42U;
                return POTR_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(10);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    potr_context ctx = {};
    ctx.service.dst_port = 5000;
    ctx.service.udp_receive_buffer_size = POTR_FLOW_CONTROL_DISABLED;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.dst_addr[0], "dst", sizeof("dst"));
    // [状態] - サービスの udp_receive_buffer_size に POTR_FLOW_CONTROL_DISABLED を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_set_receive_buffer_size(_, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - cplat_socket_set_receive_buffer_size() が呼ばれないこと。
    EXPECT_CALL(mock_cplat, cplat_socket_get_receive_buffer_size(_, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - cplat_socket_get_receive_buffer_size() が呼ばれないこと。

    // Act
    int actual_ret = api_open_paths_unicast(&ctx, POTR_ROLE_RECEIVER); // [手順] - unicast 経路を開く。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 戻り値が POTR_OK であること。
}

// 要求が失敗してもエラーにせず続行し、INFO に要求値・実際の値・OS 設定の見直しをサービスごとに 1 回記録することの確認
TEST(apiOpenPathsUdpTest, udp_receive_buffer_size_failure_continues_and_logs_info_once)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42U;
                return POTR_OK;
            });
    int opens = 0;
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [&](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(++opens);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_set_receive_buffer_size(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    ON_CALL(mock_cplat, cplat_socket_get_receive_buffer_size(_, _, _))
        .WillByDefault(
            [](cplat_socket, size_t *size_out, cplat_error *)
            {
                *size_out = 212992U;
                return CPLAT_OK;
            });
    potr_context ctx = {};
    ctx.service.service_id = 1001;
    ctx.service.dst_port = 5000;
    ctx.global.udp_receive_buffer_size = 4194304U;
    std::memcpy(ctx.service.src_addr[0], "src1", sizeof("src1"));
    std::memcpy(ctx.service.dst_addr[0], "dst1", sizeof("dst1"));
    std::memcpy(ctx.service.src_addr[1], "src2", sizeof("src2"));
    std::memcpy(ctx.service.dst_addr[1], "dst2", sizeof("dst2"));
    // [状態] - 2 経路の unicast で、要求を失敗させ、取得値を 212992 にする。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat,
                cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_INFO, nullptr,
                                       AllOf(HasSubstr("requested=4194304"), HasSubstr("actual=212992"),
                                             HasSubstr("OS receive buffer settings"))))
        .Times(1);
    // [Pre-Assert確認_正常系] - 2 経路あっても INFO に要求値、実際の値、OS 設定の見直しが 1 回だけ記録されること。

    // Act
    int actual_ret = api_open_paths_unicast(&ctx, POTR_ROLE_RECEIVER); // [手順] - 2 経路の unicast を開く。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 要求が失敗してもエラーにせず POTR_OK を返すこと。
    EXPECT_EQ(2, ctx.n_path);       // [確認_正常系] - 2 経路すべてが開かれること。
}

// 取得値が要求値より小さい場合に INFO をサービスごとに 1 回記録し、要求値以上の場合は記録しないことの確認
TEST(apiOpenPathsUdpTest, udp_receive_buffer_size_compares_actual_and_requested_for_info_log)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42U;
                return POTR_OK;
            });
    int opens = 0;
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [&](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(++opens);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_set_receive_buffer_size(_, _, _)).WillByDefault(Return(CPLAT_OK));
    size_t mock_actual_size = 425984U;
    ON_CALL(mock_cplat, cplat_socket_get_receive_buffer_size(_, _, _))
        .WillByDefault(
            [&](cplat_socket, size_t *size_out, cplat_error *)
            {
                *size_out = mock_actual_size;
                return CPLAT_OK;
            });
    potr_context ctx_small = {};
    ctx_small.service.service_id = 1002;
    ctx_small.service.dst_port = 5001;
    ctx_small.service.udp_receive_buffer_size = 4194304U;
    std::memcpy(ctx_small.service.src_addr[0], "src1", sizeof("src1"));
    std::memcpy(ctx_small.service.dst_addr[0], "dst1", sizeof("dst1"));
    std::memcpy(ctx_small.service.src_addr[1], "src2", sizeof("src2"));
    std::memcpy(ctx_small.service.dst_addr[1], "dst2", sizeof("dst2"));
    potr_context ctx_exact = ctx_small;
    ctx_exact.service.service_id = 1003;
    potr_context ctx_doubled = ctx_small;
    ctx_doubled.service.service_id = 1004;
    // [状態] - 要求成功とし、取得値が要求値未満・同値・2 倍の 3 ケースを用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat,
                cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_INFO, nullptr,
                                       AllOf(HasSubstr("service_id=1002"), HasSubstr("requested=4194304"),
                                             HasSubstr("actual=425984"), HasSubstr("OS receive buffer settings"))))
        .Times(1);
    // [Pre-Assert確認_正常系] - 取得値が要求値より小さいサービス 1002 は 2 経路でも INFO が 1 回だけ記録されること。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_INFO, nullptr, HasSubstr("service_id=1003")))
        .Times(0);
    // [Pre-Assert確認_正常系] - 取得値が要求値と同じサービス 1003 は INFO を記録しないこと。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_INFO, nullptr, HasSubstr("service_id=1004")))
        .Times(0);
    // [Pre-Assert確認_正常系] - 取得値が要求値の 2 倍のサービス 1004 は INFO を記録しないこと。

    // Act
    mock_actual_size = 425984U;
    int ret_small = api_open_paths_unicast(&ctx_small, POTR_ROLE_RECEIVER);
    // [手順] - 取得値 425984 (< 4194304) でサービス 1002 を開く。
    mock_actual_size = 4194304U;
    int ret_exact = api_open_paths_unicast(&ctx_exact, POTR_ROLE_RECEIVER);
    // [手順] - 取得値 4194304 (== 4194304) でサービス 1003 を開く。
    mock_actual_size = 8388608U;
    int ret_doubled = api_open_paths_unicast(&ctx_doubled, POTR_ROLE_RECEIVER);
    // [手順] - 取得値 8388608 (2 倍) でサービス 1004 を開く。

    // Assert
    EXPECT_EQ(POTR_OK, ret_small);   // [確認_正常系] - サービス 1002 が POTR_OK であること。
    EXPECT_EQ(POTR_OK, ret_exact);   // [確認_正常系] - サービス 1003 が POTR_OK であること。
    EXPECT_EQ(POTR_OK, ret_doubled); // [確認_正常系] - サービス 1004 が POTR_OK であること。
}

// unicast / unicast_bidir の送信者が port=0 (OS 自動割り当て) で bind するとき cplat_socket_set_reuse_address() を呼ばないことの確認
TEST(apiOpenPathsUdpTest, unicast_ephemeral_port_skips_reuse_address)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42U;
                return POTR_OK;
            });
    int opens = 0;
    ON_CALL(mock_cplat, cplat_socket_open(CPLAT_SOCKET_UDP, _, _))
        .WillByDefault(
            [&](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(++opens);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    potr_context ctx_unicast = {};
    ctx_unicast.service.src_port = 0;
    ctx_unicast.service.dst_port = 5000;
    std::memcpy(ctx_unicast.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx_unicast.service.dst_addr[0], "dst", sizeof("dst"));
    potr_context ctx_bidir = {};
    ctx_bidir.service.src_port = 0;
    ctx_bidir.service.dst_port = 5001;
    std::memcpy(ctx_bidir.service.dst_addr[0], "dst", sizeof("dst"));
    // [状態] - unicast と unicast_bidir の送信者に src_port=0 を設定し、ソケット作成と bind を成功させる。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_set_reuse_address(_, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - cplat_socket_set_reuse_address() が呼び出されないこと。

    // Act
    int ret_unicast = api_open_paths_unicast(&ctx_unicast, POTR_ROLE_SENDER);
    // [手順] - src_port=0 で unicast 送信者の経路を開く。
    int ret_bidir = api_open_paths_unicast_bidir(&ctx_bidir, POTR_ROLE_SENDER);
    // [手順] - src_port=0 で unicast_bidir 送信者の経路を開く。

    // Assert
    EXPECT_EQ(POTR_OK, ret_unicast); // [確認_正常系] - unicast 送信者の戻り値が POTR_OK であること。
    EXPECT_EQ(POTR_OK, ret_bidir);   // [確認_正常系] - unicast_bidir 送信者の戻り値が POTR_OK であること。
}

// unicast / unicast_bidir / unicast_bidir_n1 が固定ポート (非 0) で bind するとき cplat_socket_set_reuse_address() を呼ぶことの確認
TEST(apiOpenPathsUdpTest, unicast_fixed_port_sets_reuse_address)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42U;
                return POTR_OK;
            });
    ON_CALL(mock_porter, potr_internal_peer_table_init(_)).WillByDefault(Return(POTR_OK));
    int opens = 0;
    ON_CALL(mock_cplat, cplat_socket_open(CPLAT_SOCKET_UDP, _, _))
        .WillByDefault(
            [&](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(++opens);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 固定ポートでのソケット作成と bind を成功させる。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_set_reuse_address(_, 1, _)).Times(5).WillRepeatedly(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - 固定ポートを使う 5 経路それぞれで cplat_socket_set_reuse_address() が 1 を指定して計 5 回呼び出されること。

    // Act
    potr_context ctx = {};
    ctx.service.src_port = 40000;
    ctx.service.dst_port = 5000;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.dst_addr[0], "dst", sizeof("dst"));
    int ret_unicast_sender = api_open_paths_unicast(&ctx, POTR_ROLE_SENDER);
    ctx.n_path = 0;
    int ret_unicast_receiver = api_open_paths_unicast(&ctx, POTR_ROLE_RECEIVER);
    // [手順] - 固定ポートで unicast の送信者と受信者を開く。

    ctx = {};
    ctx.service.src_port = 40001;
    ctx.service.dst_port = 5001;
    std::memcpy(ctx.service.dst_addr[0], "dst", sizeof("dst"));
    int ret_bidir_sender = api_open_paths_unicast_bidir(&ctx, POTR_ROLE_SENDER);
    ctx = {};
    ctx.service.dst_port = 5001;
    int ret_bidir_receiver = api_open_paths_unicast_bidir(&ctx, POTR_ROLE_RECEIVER);
    // [手順] - 固定ポートで unicast_bidir の送信者と受信者を開く。

    ctx = {};
    ctx.service.dst_port = 5002;
    int ret_n1 = api_open_paths_unicast_bidir_n1(&ctx);
    // [手順] - 固定ポートで unicast_bidir_n1 サーバーを開く。

    // Assert
    EXPECT_EQ(POTR_OK, ret_unicast_sender);   // [確認_正常系] - unicast 送信者が POTR_OK であること。
    EXPECT_EQ(POTR_OK, ret_unicast_receiver); // [確認_正常系] - unicast 受信者が POTR_OK であること。
    EXPECT_EQ(POTR_OK, ret_bidir_sender);     // [確認_正常系] - unicast_bidir 送信者が POTR_OK であること。
    EXPECT_EQ(POTR_OK, ret_bidir_receiver);   // [確認_正常系] - unicast_bidir 受信者が POTR_OK であること。
    EXPECT_EQ(POTR_OK, ret_n1);               // [確認_正常系] - unicast_bidir_n1 が POTR_OK であること。
}

// multicast / broadcast は固定ポート (受信者・固定ポート送信者) で再利用を有効にし、送信者 port=0 では有効にしないことの確認
TEST(apiOpenPathsUdpTest, multicast_and_broadcast_reuse_address_follows_bind_port)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ON_CALL(mock_porter, api_paths_resolve_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42U;
                return POTR_OK;
            });
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _))
        .WillByDefault(
            [](const char *, uint32_t *out)
            {
                *out = 42U;
                return POTR_OK;
            });
    int opens = 0;
    ON_CALL(mock_cplat, cplat_socket_open(CPLAT_SOCKET_UDP, _, _))
        .WillByDefault(
            [&](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(++opens);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_join_multicast_group(_, _, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_set_multicast_interface(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_set_broadcast(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - multicast と broadcast のソケット操作をすべて成功させる。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_set_reuse_address(static_cast<cplat_socket>(1), _, _)).Times(0);
    // [Pre-Assert確認_正常系] - multicast 送信者 (src_port=0) のソケット 1 では cplat_socket_set_reuse_address() が呼び出されないこと。
    EXPECT_CALL(mock_cplat, cplat_socket_set_reuse_address(static_cast<cplat_socket>(2), 1, _))
        .Times(1)
        .WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - multicast 送信者 (固定 src_port) のソケット 2 では cplat_socket_set_reuse_address() が 1 回呼び出されること。
    EXPECT_CALL(mock_cplat, cplat_socket_set_reuse_address(static_cast<cplat_socket>(3), 1, _))
        .Times(1)
        .WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - multicast 受信者 (dst_port) のソケット 3 では cplat_socket_set_reuse_address() が 1 回呼び出されること。
    EXPECT_CALL(mock_cplat, cplat_socket_set_reuse_address(static_cast<cplat_socket>(4), _, _)).Times(0);
    // [Pre-Assert確認_正常系] - broadcast 送信者 (src_port=0) のソケット 4 では cplat_socket_set_reuse_address() が呼び出されないこと。
    EXPECT_CALL(mock_cplat, cplat_socket_set_reuse_address(static_cast<cplat_socket>(5), 1, _))
        .Times(1)
        .WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - broadcast 送信者 (固定 src_port) のソケット 5 では cplat_socket_set_reuse_address() が 1 回呼び出されること。
    EXPECT_CALL(mock_cplat, cplat_socket_set_reuse_address(static_cast<cplat_socket>(6), 1, _))
        .Times(1)
        .WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - broadcast 受信者 (dst_port) のソケット 6 では cplat_socket_set_reuse_address() が 1 回呼び出されること。

    // Act
    potr_context ctx = {};
    ctx.service.src_port = 0;
    ctx.service.dst_port = 6000;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    std::memcpy(ctx.service.multicast_group, "239.0.0.1", sizeof("239.0.0.1"));
    int ret_mcast_sender_auto = api_open_paths_multicast(&ctx, POTR_ROLE_SENDER);
    ctx.n_path = 0;
    ctx.service.src_port = 40010;
    int ret_mcast_sender_fixed = api_open_paths_multicast(&ctx, POTR_ROLE_SENDER);
    ctx.n_path = 0;
    ctx.service.src_port = 0;
    int ret_mcast_receiver = api_open_paths_multicast(&ctx, POTR_ROLE_RECEIVER);
    // [手順] - multicast の送信者 (port=0、固定) と受信者を開く。

    ctx = {};
    ctx.service.src_port = 0;
    ctx.service.dst_port = 7000;
    std::memcpy(ctx.service.src_addr[0], "src", sizeof("src"));
    int ret_bcast_sender_auto = api_open_paths_broadcast(&ctx, POTR_ROLE_SENDER);
    ctx.n_path = 0;
    ctx.service.src_port = 40020;
    int ret_bcast_sender_fixed = api_open_paths_broadcast(&ctx, POTR_ROLE_SENDER);
    ctx.n_path = 0;
    ctx.service.src_port = 0;
    int ret_bcast_receiver = api_open_paths_broadcast(&ctx, POTR_ROLE_RECEIVER);
    // [手順] - broadcast の送信者 (port=0、固定) と受信者を開く。

    // Assert
    EXPECT_EQ(POTR_OK, ret_mcast_sender_auto);  // [確認_正常系] - multicast 送信者 (port=0) が POTR_OK であること。
    EXPECT_EQ(POTR_OK, ret_mcast_sender_fixed); // [確認_正常系] - multicast 送信者 (固定) が POTR_OK であること。
    EXPECT_EQ(POTR_OK, ret_mcast_receiver);     // [確認_正常系] - multicast 受信者が POTR_OK であること。
    EXPECT_EQ(POTR_OK, ret_bcast_sender_auto);  // [確認_正常系] - broadcast 送信者 (port=0) が POTR_OK であること。
    EXPECT_EQ(POTR_OK, ret_bcast_sender_fixed); // [確認_正常系] - broadcast 送信者 (固定) が POTR_OK であること。
    EXPECT_EQ(POTR_OK, ret_bcast_receiver);     // [確認_正常系] - broadcast 受信者が POTR_OK であること。
}
