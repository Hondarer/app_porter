#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>

#include <config.h>
#include <config_test_helper.h>
#include <mock_com_util.h>
#include <porter_const.h>

#include <cstdlib>
#include <string>
#include <vector>

using namespace testing;

TEST(configListServiceIdsTest, returnsErrorWhenArgumentIsInvalidOrFileCannotBeOpened)
{
    NiceMock<Mock_com_util> mock_com_util;
    int64_t                *ids   = nullptr;
    int                     count = 0;

    // Arrange

    // Pre-Assert
    EXPECT_CALL(mock_com_util, com_util_fopen(StrEq("missing.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(nullptr)); // [Pre-Assert確認_異常系] - 存在しない設定ファイルの open が 1 回試行されること。

    // Act
    int rtc_null_path  = config_list_service_ids(nullptr, &ids, &count);           // [手順] - config_path を NULL にして呼び出す。
    int rtc_null_ids   = config_list_service_ids("config.conf", nullptr, &count);   // [手順] - ids_out を NULL にして呼び出す。
    int rtc_null_count = config_list_service_ids("config.conf", &ids, nullptr);     // [手順] - count_out を NULL にして呼び出す。
    int rtc_open_fail  = config_list_service_ids("missing.conf", &ids, &count);     // [手順] - open に失敗する設定ファイルを指定して呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc_null_path);   // [確認_異常系] - config_path が NULL の場合に POTR_ERROR を返すこと。
    EXPECT_EQ(POTR_ERROR, rtc_null_ids);    // [確認_異常系] - ids_out が NULL の場合に POTR_ERROR を返すこと。
    EXPECT_EQ(POTR_ERROR, rtc_null_count);  // [確認_異常系] - count_out が NULL の場合に POTR_ERROR を返すこと。
    EXPECT_EQ(POTR_ERROR, rtc_open_fail);   // [確認_異常系] - open 失敗時に POTR_ERROR を返すこと。
}

TEST(configListServiceIdsTest, listsOnlyServiceSectionsAndExpandsBeyondDefaultCapacity)
{
    NiceMock<Mock_com_util> mock_com_util;
    std::vector<std::string> config_lines;
    int64_t                *ids   = nullptr;
    int                     count = 0;

    config_lines.emplace_back("[global]\n");
    config_lines.emplace_back("window_size = 16\n");
    config_lines.emplace_back("[misc]\n");
    config_lines.emplace_back("name = ignored\n");
    for (int i = 0; i < 70; i++)
    {
        config_lines.emplace_back("[service." + std::to_string(1000 + i) + "]\n");
        config_lines.emplace_back("dst_port = 5001\n");
    }

    ConfigLineStream lines(config_lines);

    // Arrange

    // Pre-Assert
    EXPECT_CALL(mock_com_util, com_util_fopen(StrEq("config.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(ConfigLineStream::handle())); // [Pre-Assert確認_正常系] - 設定ファイル open が 1 回呼び出されること。
    ON_CALL(mock_com_util, com_util_fgets(_, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke([&](char *buf, int size, FILE *stream) -> char * {
            return lines.read(buf, size, stream);
        })); // [Pre-Assert手順] - 70 個の service section を含む行列を順に返す。
    EXPECT_CALL(mock_com_util, com_util_fclose(ConfigLineStream::handle()))
        .WillOnce(Return(0)); // [Pre-Assert確認_正常系] - 読み込み完了時に fclose が呼び出されること。

    // Act
    int rtc = config_list_service_ids("config.conf", &ids, &count); // [手順] - 複数 service section を含む設定から ID を列挙する。

    // Assert
    ASSERT_EQ(POTR_SUCCESS, rtc);               // [確認_正常系] - 列挙に成功すること。
    ASSERT_NE(nullptr, ids);                    // [確認_正常系] - service ID 配列が確保されること。
    EXPECT_EQ(70, count);                       // [確認_正常系] - 非 service section を除いた 70 件が列挙されること。
    EXPECT_EQ(1000, ids[0]);                   // [確認_正常系] - 先頭 service ID を保持すること。
    EXPECT_EQ(1069, ids[69]);                  // [確認_正常系] - 64 件超でも末尾 service ID を保持すること。

    free(ids);
}
