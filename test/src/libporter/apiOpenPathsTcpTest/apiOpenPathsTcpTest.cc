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

TEST(apiOpenPathsTcpTest, sender)
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
    std::memcpy(ctx.service.dst_addr[0], "host", sizeof("host"));
    ctx.tcp_listen_sock[0] = CPLAT_INVALID_SOCKET;
    ON_CALL(mock_porter, potr_internal_result_from_error(_)).WillByDefault(Return(POTR_ERR_IO));
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(7);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_listen(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - TCP ソケットと下位操作の結果を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(7))).Times(0);
    // [Pre-Assert確認_正常系] - 未登録ソケットを 閉じないこと。
    EXPECT_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .Times(0); // [Pre-Assert確認_正常系] - 送信側ではソケットを生成しないこと。
    // Act
    int actual_ret = api_open_paths_tcp(&ctx, POTR_ROLE_SENDER); // [手順] - TCP 経路を準備する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 操作結果を伝達すること。
    EXPECT_EQ(1, ctx.n_path);       // [確認_正常系] - 完了した経路だけを数えること。
    EXPECT_EQ(CPLAT_INVALID_SOCKET,
              ctx.tcp_listen_sock[0]); // [確認_正常系] - 完了時だけ listen ソケットを登録すること。
}

TEST(apiOpenPathsTcpTest, listen)
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
    std::memcpy(ctx.service.dst_addr[0], "host", sizeof("host"));
    ctx.tcp_listen_sock[0] = CPLAT_INVALID_SOCKET;
    ON_CALL(mock_porter, potr_internal_result_from_error(_)).WillByDefault(Return(POTR_ERR_IO));
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(7);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_listen(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - TCP ソケットと下位操作の結果を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(7))).Times(0);
    // [Pre-Assert確認_正常系] - 未登録ソケットを 閉じないこと。

    // Act
    int actual_ret = api_open_paths_tcp(&ctx, POTR_ROLE_RECEIVER); // [手順] - TCP 経路を準備する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 操作結果を伝達すること。
    EXPECT_EQ(1, ctx.n_path);       // [確認_正常系] - 完了した経路だけを数えること。
    EXPECT_EQ(static_cast<cplat_socket>(7),
              ctx.tcp_listen_sock[0]); // [確認_正常系] - 完了時だけ listen ソケットを登録すること。
}

TEST(apiOpenPathsTcpTest, bind_failure)
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
    std::memcpy(ctx.service.dst_addr[0], "host", sizeof("host"));
    ctx.tcp_listen_sock[0] = CPLAT_INVALID_SOCKET;
    ON_CALL(mock_porter, potr_internal_result_from_error(_)).WillByDefault(Return(POTR_ERR_IO));
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(7);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    ON_CALL(mock_cplat, cplat_socket_listen(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - TCP ソケットと下位操作の結果を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(7))).Times(1);
    // [Pre-Assert確認_異常系] - 未登録ソケットを 1 回閉じること。

    // Act
    int actual_ret = api_open_paths_tcp(&ctx, POTR_ROLE_RECEIVER); // [手順] - TCP 経路を準備する。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 操作結果を伝達すること。
    EXPECT_EQ(0, ctx.n_path);           // [確認_異常系] - 完了した経路だけを数えること。
    EXPECT_EQ(CPLAT_INVALID_SOCKET,
              ctx.tcp_listen_sock[0]); // [確認_異常系] - 完了時だけ listen ソケットを登録すること。
}

TEST(apiOpenPathsTcpTest, listen_failure)
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
    std::memcpy(ctx.service.dst_addr[0], "host", sizeof("host"));
    ctx.tcp_listen_sock[0] = CPLAT_INVALID_SOCKET;
    ON_CALL(mock_porter, potr_internal_result_from_error(_)).WillByDefault(Return(POTR_ERR_IO));
    ON_CALL(mock_cplat, cplat_socket_open(_, _, _))
        .WillByDefault(
            [](cplat_socket_kind, cplat_socket *out, cplat_error *)
            {
                *out = static_cast<cplat_socket>(7);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_bind(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_listen(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    // [状態] - TCP ソケットと下位操作の結果を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(7))).Times(1);
    // [Pre-Assert確認_異常系] - 未登録ソケットを 1 回閉じること。

    // Act
    int actual_ret = api_open_paths_tcp(&ctx, POTR_ROLE_RECEIVER); // [手順] - TCP 経路を準備する。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 操作結果を伝達すること。
    EXPECT_EQ(0, ctx.n_path);           // [確認_異常系] - 完了した経路だけを数えること。
    EXPECT_EQ(CPLAT_INVALID_SOCKET,
              ctx.tcp_listen_sock[0]); // [確認_異常系] - 完了時だけ listen ソケットを登録すること。
}

TEST(apiOpenPathsTcpTest, resolution_failure_precedes_socket_creation)
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
    int actual_ret = api_open_paths_tcp(&ctx, POTR_ROLE_RECEIVER); // [手順] - 経路を準備する。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - アドレスの結果分類を保持すること。
    EXPECT_EQ(0, ctx.n_path);                         // [確認_異常系] - 未完成の経路を数えないこと。
}
