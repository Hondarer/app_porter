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
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 必須ポートの欠落を拒否すること。
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
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 必須ポートの欠落を拒否すること。
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
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 必須ポートの欠落を拒否すること。
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
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 必須ポートの欠落を拒否すること。
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
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 必須ポートの欠落を拒否すること。
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
