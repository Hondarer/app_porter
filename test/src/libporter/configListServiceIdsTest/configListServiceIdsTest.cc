/**
 *******************************************************************************
 *  @file           configListServiceIdsTest.cc
 *  @brief          サービス ID 列挙 API の単体テストを定義します。
 *  @author         Tetsuo Honda
 *  @date           2026/11/24
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

#include <porter/porter_spec.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace testing;

namespace
{
class TempJsoncFile
{
  public:
    TempJsoncFile(const std::string &tag, const std::string &content)
    {
        static int seq = 0;
        ++seq;
        path_ = "tmp_lsi_" + tag + "_" + std::to_string(seq) + ".jsonc";
        FILE *fp = delegate_real_cplat_fopen(path_.c_str(), "wb", nullptr);
        if (fp != nullptr)
        {
            if (!content.empty())
            {
                std::fwrite(content.data(), 1U, content.size(), fp);
            }
            std::fclose(fp);
        }
    }
    ~TempJsoncFile()
    {
        std::remove(path_.c_str());
    }
    const std::string &path() const
    {
        return path_;
    }

  private:
    std::string path_;
};

void setup_stream(NiceMock<Mock_cplat> &mock_cplat, const TempJsoncFile &file, const char *path)
{
    std::string real_path = file.path();
    ON_CALL(mock_cplat, cplat_fopen(StrEq(path), StrEq("rb"), nullptr))
        .WillByDefault(Invoke([real_path](const char *, const char *mode, cplat_error *err) -> FILE * {
            return delegate_real_cplat_fopen(real_path.c_str(), mode, err);
        }));
}
} // namespace

// 必須引数が NULL の場合に POTR_ERR_INVALID_ARGUMENT を返し、出力を初期化することの確認
// potr-req: id=POTR-CONFIG-FUNC-007; uuid=a1ee646d-15a7-466f-904e-d1c2d752f69f
TEST(configListServiceIdsTest, returnsInvalidArgumentWhenParameterIsNull)
{
    // Arrange
    int64_t dummy = 1;
    int64_t *ids = &dummy; // [状態] - 出力用 ids と count を非初期値にしておき、失敗時に初期化されることも確認する。
    size_t count = 99U;

    // Pre-Assert

    // Act
    int actual_ret_null_path = potr_config_list_service_ids(
        nullptr, &ids, &count); // [手順] - config_path に nullptr を指定して potr_config_list_service_ids() を呼び出す。
    size_t count_after_null_path = count;
    int64_t *ids_after_null_path = ids;
    ids = &dummy;
    count = 99U;
    int actual_ret_null_ids = potr_config_list_service_ids(
        "config.jsonc", nullptr, &count); // [手順] - ids に nullptr を指定して potr_config_list_service_ids() を呼び出す。
    size_t count_after_null_ids = count;
    int actual_ret_null_count = potr_config_list_service_ids(
        "config.jsonc", &ids, nullptr); // [手順] - count に nullptr を指定して potr_config_list_service_ids() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_null_path);
    // [確認_異常系] - config_path が nullptr の場合に POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(nullptr, ids_after_null_path); // [確認_異常系] - 失敗時に *ids が nullptr になること。
    EXPECT_EQ(0U, count_after_null_path);    // [確認_異常系] - 失敗時に *count が 0 になること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_null_ids);
    // [確認_異常系] - ids が nullptr の場合に POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(0U, count_after_null_ids); // [確認_異常系] - ids が nullptr の場合に *count が 0 になること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_null_count);
    // [確認_異常系] - count が nullptr の場合に POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(nullptr, ids); // [確認_異常系] - count が nullptr の場合に *ids が nullptr になること。
}

// 設定ファイルの open に失敗した場合に POTR_ERR_IO を返すことの確認
// potr-req: id=POTR-CONFIG-FUNC-007; uuid=a1ee646d-15a7-466f-904e-d1c2d752f69f
TEST(configListServiceIdsTest, returnsIoErrorWhenFileCannotBeOpened)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    int64_t dummy = 1;
    int64_t *ids = &dummy;
    size_t count = 5U;

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("missing.jsonc"), StrEq("rb"), nullptr))
        .WillOnce(Return(nullptr)); // [Pre-Assert確認_異常系] - cplat_fopen() がバイナリ モード "rb" で呼ばれること。
                                    // [Pre-Assert手順] - nullptr を返す。

    // Act
    int actual_ret = potr_config_list_service_ids("missing.jsonc", &ids, &count);
    // [手順] - 開けないパスを指定して potr_config_list_service_ids() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 戻り値が POTR_ERR_IO であること。
    EXPECT_EQ(nullptr, ids);            // [確認_異常系] - 失敗時に *ids が nullptr であること。
    EXPECT_EQ(0U, count);               // [確認_異常系] - 失敗時に *count が 0 であること。
}

// 70 件のサービスで初期容量を超えて拡張し、書かれた順で全 ID を取得して解放できることの確認
// potr-req: id=POTR-CONFIG-FUNC-007; uuid=a1ee646d-15a7-466f-904e-d1c2d752f69f
TEST(configListServiceIdsTest, listsOnlyServiceSectionsAndExpandsBeyondDefaultCapacity)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    std::string content = "{\"global\":{\"window_size\":16},\"services\":{";
    for (int i = 0; i < 70; i++)
    {
        content += (i == 0 ? "" : ",") + std::string("\"") + std::to_string(1000 + i) + "\":{\"dst_port\":5001}";
    }
    content += "}}";
    TempJsoncFile stream("list70", content);
    setup_stream(mock_cplat, stream, "config.jsonc");
    int64_t *ids = nullptr;
    size_t count = 0U;
    // [状態] - 70 個の service 定義を含む JSONC を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("config.jsonc"), StrEq("rb"), nullptr)).Times(1);
    // [Pre-Assert確認_正常系] - cplat_fopen() が "rb" で 1 回呼ばれること。

    // Act
    int actual_ret = potr_config_list_service_ids("config.jsonc", &ids, &count);
    // [手順] - potr_config_list_service_ids() で 70 件を列挙する。

    // Assert
    ASSERT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 戻り値が POTR_OK であること。
    ASSERT_NE(nullptr, ids);        // [確認_正常系] - ids が nullptr 以外であること。
    ASSERT_EQ(70U, count);          // [確認_正常系] - 70 件が返ること。
    EXPECT_EQ(1000, ids[0]);        // [確認_正常系] - 先頭が 1000 であること。
    EXPECT_EQ(1069, ids[69]);       // [確認_正常系] - 末尾が 1069 であること。

    // Cleanup
    potr_config_service_ids_free(ids);
    potr_config_service_ids_free(nullptr);
}

// サービス 0 件 (services なし、空オブジェクト) で POTR_OK、*ids が NULL、*count が 0 になることの確認
// potr-req: id=POTR-CONFIG-FUNC-007; uuid=a1ee646d-15a7-466f-904e-d1c2d752f69f
TEST(configListServiceIdsTest, returnsNullAndZeroForEmptyOrMissingServices)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    TempJsoncFile stream_missing("missing_s", "{\"global\":{\"window_size\":16}}");
    TempJsoncFile stream_empty("empty_s", "{\"services\":{}}");
    int64_t dummy = 1;
    int64_t *ids1 = &dummy;
    size_t count1 = 9U;
    int64_t *ids2 = &dummy;
    size_t count2 = 9U;

    // Pre-Assert
    setup_stream(mock_cplat, stream_missing, "no_services.jsonc");
    int ret1 = potr_config_list_service_ids("no_services.jsonc", &ids1, &count1);
    setup_stream(mock_cplat, stream_empty, "empty_services.jsonc");
    int ret2 = potr_config_list_service_ids("empty_services.jsonc", &ids2, &count2);
    // [手順] - services なしと空オブジェクトの 2 通りの設定を列挙する。

    // Assert
    EXPECT_EQ(POTR_OK, ret1);   // [確認_正常系] - services なしでも POTR_OK であること。
    EXPECT_EQ(nullptr, ids1);   // [確認_正常系] - services なしで *ids が nullptr であること。
    EXPECT_EQ(0U, count1);      // [確認_正常系] - services なしで *count が 0 であること。
    EXPECT_EQ(POTR_OK, ret2);   // [確認_正常系] - 空 services でも POTR_OK であること。
    EXPECT_EQ(nullptr, ids2);   // [確認_正常系] - 空 services で *ids が nullptr であること。
    EXPECT_EQ(0U, count2);      // [確認_正常系] - 空 services で *count が 0 であること。
}

// 正規でない ID や重複した ID で POTR_ERR_INVALID_CONFIG を返し、失敗時に出力を初期化することの確認
// potr-req: id=POTR-CONFIG-FUNC-007; uuid=a1ee646d-15a7-466f-904e-d1c2d752f69f
TEST(configListServiceIdsTest, rejectsNonCanonicalAndDuplicateServiceIds)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    TempJsoncFile stream_bad_id("bad_id", "{\"services\":{\"010\":{\"type\":\"unicast\"}}}");
    TempJsoncFile stream_dup_id("dup_id", "{\"services\":{\"1001\":{\"type\":\"unicast\"},\"1001\":{\"type\":\"tcp\"}}}");
    int64_t *ids = nullptr;
    size_t count = 3U;

    // Pre-Assert

    // Act
    setup_stream(mock_cplat, stream_bad_id, "bad_id.jsonc");
    int ret_bad = potr_config_list_service_ids("bad_id.jsonc", &ids, &count);
    size_t count_bad = count;
    int64_t *ids_bad = ids;
    count = 3U;
    setup_stream(mock_cplat, stream_dup_id, "dup_id.jsonc");
    int ret_dup = potr_config_list_service_ids("dup_id.jsonc", &ids, &count);
    // [手順] - 非正規 ID と重複 ID を含む設定をそれぞれ列挙する。

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_bad); // [確認_異常系] - 先頭ゼロ付き ID で POTR_ERR_INVALID_CONFIG であること。
    EXPECT_EQ(nullptr, ids_bad);                 // [確認_異常系] - 非正規 ID 失敗時に *ids が nullptr であること。
    EXPECT_EQ(0U, count_bad);                    // [確認_異常系] - 非正規 ID 失敗時に *count が 0 であること。
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_dup); // [確認_異常系] - 重複 ID で POTR_ERR_INVALID_CONFIG であること。
    EXPECT_EQ(nullptr, ids);                     // [確認_異常系] - 重複 ID 失敗時に *ids が nullptr であること。
    EXPECT_EQ(0U, count);                        // [確認_異常系] - 重複 ID 失敗時に *count が 0 であること。
}

// 配列確保の失敗時に POTR_ERR_OUT_OF_MEMORY を返すことの確認
// potr-req: id=POTR-CONFIG-FUNC-007; uuid=a1ee646d-15a7-466f-904e-d1c2d752f69f
TEST(configListServiceIdsTest, returnsOutOfMemoryWhenResultArrayAllocationFails)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    TempJsoncFile stream("oom", "{\"services\":{\"1001\":{\"type\":\"unicast\"},\"1002\":{\"type\":\"tcp\"}}}");
    setup_stream(mock_cplat, stream, "oom.jsonc");
    int64_t *ids = nullptr;
    size_t count = 7U;
    // [状態] - 2 件のサービスを含む設定ファイルと、読込確保 (4096 B) および ID 配列確保 (POTR_MAX_SERVICES * sizeof(int64_t)) の失敗を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_malloc(_)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_malloc((size_t)POTR_MAX_SERVICES * sizeof(int64_t)))
        .WillOnce(Return(nullptr))
        .RetiresOnSaturation();
    // [Pre-Assert確認_異常系] - 結果配列の cplat_malloc() が POTR_MAX_SERVICES 件分で呼ばれること。
    // [Pre-Assert手順] - nullptr を返す。

    // Act
    int actual_ret = potr_config_list_service_ids("oom.jsonc", &ids, &count);
    int64_t *ids_doc_oom = ids;
    size_t count_doc_oom = 7U;
    EXPECT_CALL(mock_cplat, cplat_malloc(4096U)).WillOnce(Return(nullptr));
    int ret_doc_oom = potr_config_list_service_ids("oom.jsonc", &ids_doc_oom, &count_doc_oom);
    // [手順] - 結果配列確保失敗と文書読込バッファー確保失敗の 2 通りで potr_config_list_service_ids() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, actual_ret); // [確認_異常系] - 配列確保失敗で POTR_ERR_OUT_OF_MEMORY であること。
    EXPECT_EQ(nullptr, ids);                       // [確認_異常系] - 失敗時に *ids が nullptr であること。
    EXPECT_EQ(0U, count);                          // [確認_異常系] - 失敗時に *count が 0 であること。
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, ret_doc_oom); // [確認_異常系] - 文書読込 OOM でも POTR_ERR_OUT_OF_MEMORY であること。
    EXPECT_EQ(nullptr, ids_doc_oom);                // [確認_異常系] - 文書読込 OOM 時に *ids が nullptr であること。
    EXPECT_EQ(0U, count_doc_oom);                   // [確認_異常系] - 文書読込 OOM 時に *count が 0 であること。
}

// type のないサービス、非オブジェクトのサービス、本文の重複キー・未知キー・値の誤りを含む設定でも ID を列挙できることの確認
// potr-req: id=POTR-CONFIG-FUNC-007; uuid=a1ee646d-15a7-466f-904e-d1c2d752f69f
TEST(configListServiceIdsTest, ignoresServiceBodyErrorsAndNonObjectEntries)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    TempJsoncFile stream("body_errors",
        "{\"global\":{\"window_size\":\"invalid\",\"unknown_g\":1},"
        "\"services\":{"
        "\"1001\":{\"unknown_k\":1,\"dst_port\":5001,\"dst_port\":5002},"
        "\"-5\":123,"
        "\"9223372036854775807\":[]"
        "}}");
    setup_stream(mock_cplat, stream, "body_errors.jsonc");
    int64_t *ids = nullptr;
    size_t count = 0U;
    // [状態] - ルート構造は正しいが、global や各サービス本文に誤りや非オブジェクトを含む JSONC を用意する。

    // Pre-Assert

    // Act
    int actual_ret = potr_config_list_service_ids("body_errors.jsonc", &ids, &count);
    // [手順] - 本文の誤りを含む設定で potr_config_list_service_ids() を呼び出す。

    // Assert
    ASSERT_EQ(POTR_OK, actual_ret);          // [確認_正常系] - 戻り値が POTR_OK であること。
    ASSERT_NE(nullptr, ids);                 // [確認_正常系] - ids が nullptr 以外であること。
    ASSERT_EQ(3U, count);                    // [確認_正常系] - 3 件すべて列挙されること。
    EXPECT_EQ(1001, ids[0]);                 // [確認_正常系] - 1 件目が 1001 であること。
    EXPECT_EQ(-5, ids[1]);                   // [確認_正常系] - 2 件目が -5 であること。
    EXPECT_EQ(INT64_MAX, ids[2]);            // [確認_正常系] - 3 件目が INT64_MAX であること。

    // Cleanup
    potr_config_service_ids_free(ids);
}
