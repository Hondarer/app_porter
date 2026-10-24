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

TEST(apiOpenPathsTest, dispatch_unicast)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_UNICAST; // [状態] - 通信方式を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, api_open_paths_unicast(&ctx, POTR_ROLE_SENDER)).WillOnce(Return(POTR_ERR_IO));
    // [Pre-Assert確認_正常系] - 選択した方式の準備を 1 回呼ぶこと。
    // [Pre-Assert手順] - 準備失敗を返す。
    // Act
    int actual_ret = api_open_paths_by_type(&ctx, POTR_ROLE_SENDER); // [手順] - 方式別準備を呼ぶ。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 方式別の失敗を伝達すること。
}

TEST(apiOpenPathsTest, dispatch_multicast)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_MULTICAST; // [状態] - 通信方式を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, api_open_paths_multicast(&ctx, POTR_ROLE_SENDER)).WillOnce(Return(POTR_ERR_IO));
    // [Pre-Assert確認_正常系] - 選択した方式の準備を 1 回呼ぶこと。
    // [Pre-Assert手順] - 準備失敗を返す。
    // Act
    int actual_ret = api_open_paths_by_type(&ctx, POTR_ROLE_SENDER); // [手順] - 方式別準備を呼ぶ。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 方式別の失敗を伝達すること。
}

TEST(apiOpenPathsTest, dispatch_broadcast)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_BROADCAST; // [状態] - 通信方式を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, api_open_paths_broadcast(&ctx, POTR_ROLE_SENDER)).WillOnce(Return(POTR_ERR_IO));
    // [Pre-Assert確認_正常系] - 選択した方式の準備を 1 回呼ぶこと。
    // [Pre-Assert手順] - 準備失敗を返す。
    // Act
    int actual_ret = api_open_paths_by_type(&ctx, POTR_ROLE_SENDER); // [手順] - 方式別準備を呼ぶ。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 方式別の失敗を伝達すること。
}

TEST(apiOpenPathsTest, dispatch_unicast_bidir)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR; // [状態] - 通信方式を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, api_open_paths_unicast_bidir(&ctx, POTR_ROLE_SENDER)).WillOnce(Return(POTR_ERR_IO));
    // [Pre-Assert確認_正常系] - 選択した方式の準備を 1 回呼ぶこと。
    // [Pre-Assert手順] - 準備失敗を返す。
    // Act
    int actual_ret = api_open_paths_by_type(&ctx, POTR_ROLE_SENDER); // [手順] - 方式別準備を呼ぶ。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 方式別の失敗を伝達すること。
}

TEST(apiOpenPathsTest, dispatch_unicast_bidir_n1)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1; // [状態] - 通信方式を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, api_open_paths_unicast_bidir_n1(&ctx)).WillOnce(Return(POTR_ERR_IO));
    // [Pre-Assert確認_正常系] - 選択した方式の準備を 1 回呼ぶこと。
    // [Pre-Assert手順] - 準備失敗を返す。
    // Act
    int actual_ret = api_open_paths_by_type(&ctx, POTR_ROLE_SENDER); // [手順] - 方式別準備を呼ぶ。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 方式別の失敗を伝達すること。
}

TEST(apiOpenPathsTest, dispatch_tcp)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_TCP; // [状態] - 通信方式を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, api_open_paths_tcp(&ctx, POTR_ROLE_SENDER)).WillOnce(Return(POTR_ERR_IO));
    // [Pre-Assert確認_正常系] - 選択した方式の準備を 1 回呼ぶこと。
    // [Pre-Assert手順] - 準備失敗を返す。
    // Act
    int actual_ret = api_open_paths_by_type(&ctx, POTR_ROLE_SENDER); // [手順] - 方式別準備を呼ぶ。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 方式別の失敗を伝達すること。
}

TEST(apiOpenPathsTest, dispatch_tcp_bidir)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_TCP_BIDIR; // [状態] - 通信方式を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, api_open_paths_tcp(&ctx, POTR_ROLE_SENDER)).WillOnce(Return(POTR_ERR_IO));
    // [Pre-Assert確認_正常系] - 選択した方式の準備を 1 回呼ぶこと。
    // [Pre-Assert手順] - 準備失敗を返す。
    // Act
    int actual_ret = api_open_paths_by_type(&ctx, POTR_ROLE_SENDER); // [手順] - 方式別準備を呼ぶ。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 方式別の失敗を伝達すること。
}

TEST(apiOpenPathsTest, dispatch_unicast_raw)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_UNICAST_RAW; // [状態] - 通信方式を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, api_open_paths_unicast(&ctx, POTR_ROLE_SENDER)).WillOnce(Return(POTR_ERR_IO));
    // [Pre-Assert確認_正常系] - 選択した方式の準備を 1 回呼ぶこと。
    // [Pre-Assert手順] - 準備失敗を返す。
    // Act
    int actual_ret = api_open_paths_by_type(&ctx, POTR_ROLE_SENDER); // [手順] - 方式別準備を呼ぶ。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 方式別の失敗を伝達すること。
}

TEST(apiOpenPathsTest, dispatch_multicast_raw)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_MULTICAST_RAW; // [状態] - 通信方式を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, api_open_paths_multicast(&ctx, POTR_ROLE_SENDER)).WillOnce(Return(POTR_ERR_IO));
    // [Pre-Assert確認_正常系] - 選択した方式の準備を 1 回呼ぶこと。
    // [Pre-Assert手順] - 準備失敗を返す。
    // Act
    int actual_ret = api_open_paths_by_type(&ctx, POTR_ROLE_SENDER); // [手順] - 方式別準備を呼ぶ。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 方式別の失敗を伝達すること。
}

TEST(apiOpenPathsTest, dispatch_broadcast_raw)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_BROADCAST_RAW; // [状態] - 通信方式を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, api_open_paths_broadcast(&ctx, POTR_ROLE_SENDER)).WillOnce(Return(POTR_ERR_IO));
    // [Pre-Assert確認_正常系] - 選択した方式の準備を 1 回呼ぶこと。
    // [Pre-Assert手順] - 準備失敗を返す。
    // Act
    int actual_ret = api_open_paths_by_type(&ctx, POTR_ROLE_SENDER); // [手順] - 方式別準備を呼ぶ。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 方式別の失敗を伝達すること。
}

TEST(apiOpenPathsTest, unsupported_type)
{
    // Arrange
    potr_context ctx = {};
    ctx.service.type = static_cast<potr_type>(999); // [状態] - 未対応の方式を指定する。
    // Act
    int actual_ret = api_open_paths_by_type(&ctx, POTR_ROLE_SENDER); // [手順] - 未対応方式を準備する。
    // Assert
    EXPECT_EQ(POTR_ERR_UNSUPPORTED, actual_ret); // [確認_異常系] - 未対応を返すこと。
}

TEST(apiOpenPathsTest, multicast_destination_parse_failure)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_MULTICAST;
    ON_CALL(mock_porter, api_paths_parse_ipv4(_, _)).WillByDefault(Return(POTR_ERR_IO));
    // [状態] - multicast アドレスの解析を失敗させる。
    // Act
    int actual_ret = api_setup_dest_addr(&ctx, POTR_ROLE_SENDER); // [手順] - 宛先を設定する。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 宛先不正の分類を保持すること。
}

TEST(apiOpenPathsTest, unicast_destination_uses_resolved_address)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.n_path = 1;
    ctx.dst_addr_resolved[0] = 42;
    ctx.service.dst_port = 1234; // [状態] - 解決済みアドレスとポートを指定する。
    // Act
    int actual_ret = api_setup_dest_addr(&ctx, POTR_ROLE_SENDER); // [手順] - 宛先を設定する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);           // [確認_正常系] - 準備に成功すること。
    EXPECT_EQ(42u, ctx.dest_addr[0].address); // [確認_正常系] - 解決済みアドレスを使うこと。
    EXPECT_EQ(delegate_real_cplat_hton16(1234),
              ctx.dest_addr[0].port); // [確認_正常系] - ポートをネットワーク順にすること。
}
