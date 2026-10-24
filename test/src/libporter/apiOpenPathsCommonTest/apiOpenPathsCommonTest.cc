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

TEST(apiOpenPathsCommonTest, resolve_success)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    uint32_t address = 0;
    ON_CALL(mock_cplat, cplat_ipv4_resolve(_, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 下位層の結果を指定する。
    // Act
    int actual_ret = api_paths_resolve_ipv4("host", &address); // [手順] - アドレスを変換する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 結果の分類を保持すること。
}

TEST(apiOpenPathsCommonTest, resolve_invalid)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    uint32_t address = 0;
    ON_CALL(mock_cplat, cplat_ipv4_resolve(_, _, _)).WillByDefault(Return(CPLAT_ERR_INVALID_ARGUMENT));
    // [状態] - 下位層の結果を指定する。
    // Act
    int actual_ret = api_paths_resolve_ipv4("host", &address); // [手順] - アドレスを変換する。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 結果の分類を保持すること。
}

TEST(apiOpenPathsCommonTest, resolve_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    uint32_t address = 0;
    ON_CALL(mock_cplat, cplat_ipv4_resolve(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    // [状態] - 下位層の結果を指定する。
    // Act
    int actual_ret = api_paths_resolve_ipv4("host", &address); // [手順] - アドレスを変換する。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 結果の分類を保持すること。
}

TEST(apiOpenPathsCommonTest, parse_success)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    uint32_t address = 0;
    ON_CALL(mock_cplat, cplat_ipv4_parse(_, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 下位層の結果を指定する。
    // Act
    int actual_ret = api_paths_parse_ipv4("host", &address); // [手順] - アドレスを変換する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 結果の分類を保持すること。
}

TEST(apiOpenPathsCommonTest, parse_invalid)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    uint32_t address = 0;
    ON_CALL(mock_cplat, cplat_ipv4_parse(_, _)).WillByDefault(Return(CPLAT_ERR_INVALID_ARGUMENT));
    // [状態] - 下位層の結果を指定する。
    // Act
    int actual_ret = api_paths_parse_ipv4("host", &address); // [手順] - アドレスを変換する。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 結果の分類を保持すること。
}

TEST(apiOpenPathsCommonTest, parse_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    uint32_t address = 0;
    ON_CALL(mock_cplat, cplat_ipv4_parse(_, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    // [状態] - 下位層の結果を指定する。
    // Act
    int actual_ret = api_paths_parse_ipv4("host", &address); // [手順] - アドレスを変換する。
    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 結果の分類を保持すること。
}
