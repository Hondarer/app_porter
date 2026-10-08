/**
 *******************************************************************************
 *  @file           configListServiceIdsTest.cc
 *  @brief          サービス ID 列挙処理の単体テストを定義します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>

#include <porter/protocol/config.h>
#include <config_test_helper.h>
#include <mock_cplat.h>
#include <mock_stdio.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>

#include <cstdlib>
#include <string>
#include <vector>

using namespace testing;

// 必須引数が NULL の場合に POTR_ERR_INVALID_ARGUMENT を返すことの確認
TEST(configListServiceIdsTest, returnsInvalidArgumentWhenParameterIsNull)
{
    // Arrange
    int64_t *ids = nullptr; // [状態] - 出力用 ids ポインターを nullptr で初期化する。
    int count = 0;          // [状態] - 出力用 count を 0 で初期化する。

    // Pre-Assert

    // Act
    int actual_ret_null_path =
        potr_internal_config_list_service_ids(nullptr, &ids, &count); // [手順] - config_path を NULL にして呼び出す。
    int actual_ret_null_ids = potr_internal_config_list_service_ids(
        "config.conf", nullptr, &count); // [手順] - ids_out を NULL にして呼び出す。
    int actual_ret_null_count = potr_internal_config_list_service_ids(
        "config.conf", &ids, nullptr); // [手順] - count_out を NULL にして呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_INVALID_ARGUMENT,
        actual_ret_null_path); // [確認_異常系] - config_path が NULL の場合に potr_internal_config_list_service_ids の戻り値が POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(
        POTR_ERR_INVALID_ARGUMENT,
        actual_ret_null_ids); // [確認_異常系] - ids_out が NULL の場合に potr_internal_config_list_service_ids の戻り値が POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(
        POTR_ERR_INVALID_ARGUMENT,
        actual_ret_null_count); // [確認_異常系] - count_out が NULL の場合に potr_internal_config_list_service_ids の戻り値が POTR_ERR_INVALID_ARGUMENT であること。
}

// 設定ファイルの open に失敗した場合に POTR_ERR_IO を返すことの確認
TEST(configListServiceIdsTest, returnsIoErrorWhenFileCannotBeOpened)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat; // [状態] - cplat モックを用意する。
    int64_t *ids = nullptr;          // [状態] - 出力用 ids ポインターを nullptr で初期化する。
    int count = 0;                   // [状態] - 出力用 count を 0 で初期化する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("missing.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(nullptr)); // [Pre-Assert確認_異常系] - 存在しない設定ファイルの open が 1 回試行されること。

    // Act
    int actual_ret_open_fail =
        potr_internal_config_list_service_ids("missing.conf", &ids,
                                              &count); // [手順] - open に失敗する設定ファイルを指定して呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_IO,
        actual_ret_open_fail); // [確認_異常系] - open に失敗する設定ファイルを指定した場合に potr_internal_config_list_service_ids の戻り値が POTR_ERR_IO であること。
}

// 設定ファイルから services のサービス ID のみを列挙し、既定容量 (64 件) を超える場合も動的に領域を拡張して全 ID を取得できることの確認
TEST(configListServiceIdsTest, listsOnlyServiceSectionsAndExpandsBeyondDefaultCapacity)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    std::vector<std::string> config_lines;
    int64_t *ids = nullptr;
    int count = 0;

    config_lines.emplace_back("{\"global\":{\"window_size\":16},\"services\":{");
    for (int i = 0; i < 70; i++)
    {
        config_lines.emplace_back((i == 0 ? "" : ",") + std::string("\"") + std::to_string(1000 + i) +
                                  "\":{\"dst_port\":5001}");
    }
    config_lines.emplace_back("}}");

    ConfigLineStream lines(config_lines);
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, const int, const char *, char *buf, int size, FILE *stream) -> char *
            {
                return lines.read(buf, size, stream);
            })); // [状態] - fgets が呼び出された際に 70 個の service 定義を含む JSONC を返すようにモックを設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("config.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(
            ConfigLineStream::handle())); // [Pre-Assert確認_正常系] - 設定ファイル open が 1 回呼び出されること。
    EXPECT_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle()))
        .WillOnce(Return(0)); // [Pre-Assert確認_正常系] - 読み込み完了時に fclose が呼び出されること。

    // Act
    int actual_ret =
        potr_internal_config_list_service_ids("config.conf", &ids,
                                              &count); // [手順] - 複数 service 定義を含む設定から ID を列挙する。

    // Assert
    ASSERT_EQ(
        POTR_OK,
        actual_ret); // [確認_正常系] - potr_internal_config_list_service_ids の戻り値から、列挙に成功したと判断できること。
    ASSERT_NE(nullptr, ids);  // [確認_正常系] - service ID 配列が確保されること。
    EXPECT_EQ(70, count);     // [確認_正常系] - 70 件の service ID が列挙されること。
    EXPECT_EQ(1000, ids[0]);  // [確認_正常系] - 先頭 service ID を保持すること。
    EXPECT_EQ(1069, ids[69]); // [確認_正常系] - 64 件超でも末尾 service ID を保持すること。

    // Cleanup
    free(ids);
}
