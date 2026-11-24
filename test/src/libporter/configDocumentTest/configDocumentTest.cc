/**
 *******************************************************************************
 *  @file           configDocumentTest.cc
 *  @brief          設定文書読み取りと JSONC 解析・位置変換・消去 (段階 1) の単体テストです。
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

#include <config_test_helper.h>
#include <mock_cjson.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <mock_stdio.h>
#include <porter/porter_result.h>
#include <porter/protocol/config_document.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace testing;

namespace
{
struct doc_issue
{
    potr_config_issue_kind kind;
    uint32_t line;
    uint32_t column;
    std::string message;
};

void on_doc_issue(const potr_config_issue *issue, void *user_data)
{
    auto *out = static_cast<std::vector<doc_issue> *>(user_data);
    doc_issue item = {};
    item.kind = issue->kind;
    item.line = issue->line;
    item.column = issue->column;
    item.message = issue->message != nullptr ? issue->message : "";
    out->push_back(item);
}

void setup_binary_stream(NiceMock<Mock_cplat> &mock_cplat, NiceMock<Mock_stdio> &mock_stdio, ConfigLineStream &stream,
                         const char *path)
{
    ON_CALL(mock_cplat, cplat_fopen(StrEq(path), StrEq("rb"), nullptr)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, setvbuf(_, _, _, ConfigLineStream::handle(), _, _, _)).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fread(_, _, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke([&](const char *, const int, const char *, void *buf, size_t size, size_t count,
                                  FILE *fp) -> size_t { return stream.readBytes(buf, size, count, fp); }));
    ON_CALL(mock_stdio, ferror(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
}
} // namespace

// NULL 引数、fopen 失敗、setvbuf 失敗、先頭と途中の ferror() で I/O エラーになり部分木を返さないことの確認
TEST(configDocumentTest, handlesArgumentAndIoErrorsSafely)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    NiceMock<Mock_cjson> mock_cjson;
    cJSON *root = reinterpret_cast<cJSON *>(0x1);
    std::vector<doc_issue> issues;
    potr_internal_config_reporter reporter = {};
    potr_internal_config_reporter_init(&reporter, "doc.jsonc", on_doc_issue, &issues, 0);

    // 1) NULL 引数
    int ret_null1 = potr_internal_config_document_load(nullptr, &reporter, &root);
    cJSON *root_after_null1 = root;
    int ret_null2 = potr_internal_config_document_load("doc.jsonc", &reporter, nullptr);

    // 2) cplat_fopen 失敗 ("rb")
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("missing.jsonc"), StrEq("rb"), nullptr)).WillOnce(Return(nullptr));
    int ret_open = potr_internal_config_document_load("missing.jsonc", &reporter, &root);
    ASSERT_TRUE(Mock::VerifyAndClearExpectations(&mock_cplat));

    // 3) setvbuf 失敗
    ConfigLineStream s1("{}");
    setup_binary_stream(mock_cplat, mock_stdio, s1, "vbuf.jsonc");
    EXPECT_CALL(mock_stdio, setvbuf(_, _, _, ConfigLineStream::handle(), _, _IONBF, 0U)).WillOnce(Return(-1));
    EXPECT_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillOnce(Return(0));
    int ret_vbuf = potr_internal_config_document_load("vbuf.jsonc", &reporter, &root);
    ASSERT_TRUE(Mock::VerifyAndClearExpectations(&mock_cplat));
    ASSERT_TRUE(Mock::VerifyAndClearExpectations(&mock_stdio));

    // 4) 先頭での ferror()
    ConfigLineStream s_eof("");
    setup_binary_stream(mock_cplat, mock_stdio, s_eof, "ferr0.jsonc");
    EXPECT_CALL(mock_stdio, ferror(_, _, _, ConfigLineStream::handle())).WillOnce(Return(1));
    int ret_ferr0 = potr_internal_config_document_load("ferr0.jsonc", &reporter, &root);
    ASSERT_TRUE(Mock::VerifyAndClearExpectations(&mock_cplat));
    ASSERT_TRUE(Mock::VerifyAndClearExpectations(&mock_stdio));

    // 5) 有効プレフィックス読み取り後の途中 ferror()
    ConfigLineStream s_mid("{\"services\":");
    setup_binary_stream(mock_cplat, mock_stdio, s_mid, "ferrmid.jsonc");
    s_mid.setChunkLimit(4U);
    EXPECT_CALL(mock_cjson, cjson_app_cJSON_ParseJSONCWithLengthOpts(_, _, _, _, _, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - 途中エラーでは解析へ進まないこと。
    EXPECT_CALL(mock_stdio, ferror(_, _, _, ConfigLineStream::handle())).WillOnce(Return(1));
    int ret_ferrmid = potr_internal_config_document_load("ferrmid.jsonc", &reporter, &root);

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, ret_null1); // [確認_異常系] - path NULL で POTR_ERR_INVALID_ARGUMENT。
    EXPECT_EQ(nullptr, root_after_null1);            // [確認_異常系] - 失敗時に *root_out が nullptr になること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, ret_null2); // [確認_異常系] - root_out NULL で POTR_ERR_INVALID_ARGUMENT。
    EXPECT_EQ(POTR_ERR_IO, ret_open);                // [確認_異常系] - open 失敗で POTR_ERR_IO。
    EXPECT_EQ(POTR_ERR_IO, ret_vbuf);                // [確認_異常系] - setvbuf 失敗で POTR_ERR_IO。
    EXPECT_EQ(POTR_ERR_IO, ret_ferr0);               // [確認_異常系] - 先頭 ferror() で POTR_ERR_IO。
    EXPECT_EQ(POTR_ERR_IO, ret_ferrmid);             // [確認_異常系] - 途中 ferror() で POTR_ERR_IO。
    EXPECT_EQ(nullptr, root);                        // [確認_異常系] - 失敗時に部分木を返さないこと。
    EXPECT_TRUE(issues.empty());                     // [確認_異常系] - I/O 失敗では構文エラーを報告しないこと。
}

// 4096 byte 超拡張時に realloc せず旧領域・作業チャンク・読込領域を容量全体ゼロ消去してから解放し、OOM を検出することの確認
TEST(configDocumentTest, growsWithoutReallocSecureZeroesAllBuffersAndHandlesOom)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    NiceMock<Mock_cjson> mock_cjson;
    std::string large = "{\"global\":{\"window_size\":16},\"services\":{";
    for (int i = 0; i < 160; i++)
    {
        large += (i == 0 ? "" : ",") + std::string("\"") + std::to_string(1000 + i) + "\":{\"type\":\"unicast\"}";
    }
    large += "}}";
    ASSERT_GT(large.size(), 4096U);

    ConfigLineStream stream(large);
    setup_binary_stream(mock_cplat, mock_stdio, stream, "large.jsonc");
    std::vector<std::pair<void *, size_t>> zeroed_calls;
    ON_CALL(mock_cplat, cplat_secure_zero(_, _))
        .WillByDefault(Invoke([&](void *ptr, size_t len) {
            zeroed_calls.emplace_back(ptr, len);
            std::memset(ptr, 0, len);
        }));
    ON_CALL(mock_cplat, cplat_realloc(_, _, _)).WillByDefault(Invoke([](void *, size_t, size_t) -> void * {
        ADD_FAILURE() << "cplat_realloc() must not be used for config buffer growth";
        return nullptr;
    }));

    cJSON *root = nullptr;
    std::vector<doc_issue> issues;
    potr_internal_config_reporter reporter = {};
    potr_internal_config_reporter_init(&reporter, "large.jsonc", on_doc_issue, &issues, 0);

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_realloc(_, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - cplat_realloc() を呼ばないこと。
    EXPECT_CALL(mock_cplat, cplat_secure_zero(_, Ge(4096U))).Times(AtLeast(2));
    // [Pre-Assert確認_正常系] - 拡張前バッファーと最終読込バッファーの容量全体をゼロ消去すること。

    // Act
    int ret_ok = potr_internal_config_document_load("large.jsonc", &reporter, &root);

    // Assert
    ASSERT_EQ(POTR_OK, ret_ok); // [確認_正常系] - 4096 byte 超の読込と解析が成功すること。
    ASSERT_NE(nullptr, root);   // [確認_正常系] - 解析ツリーが返ること。
    EXPECT_TRUE(issues.empty());
    cjson_app_cJSON_Delete(root);
    root = nullptr;

    // 確保失敗 (初期確保失敗と拡張時確保失敗)
    ConfigLineStream stream_oom1("{\"a\":1}");
    setup_binary_stream(mock_cplat, mock_stdio, stream_oom1, "oom1.jsonc");
    EXPECT_CALL(mock_cplat, cplat_malloc(_)).WillOnce(Return(nullptr));
    int ret_oom_init = potr_internal_config_document_load("oom1.jsonc", &reporter, &root);

    ConfigLineStream stream_oom2(large);
    setup_binary_stream(mock_cplat, mock_stdio, stream_oom2, "oom2.jsonc");
    EXPECT_CALL(mock_cplat, cplat_malloc(_))
        .WillOnce(Invoke(delegate_real_cplat_malloc))
        .WillOnce(Return(nullptr));
    int ret_oom_grow = potr_internal_config_document_load("oom2.jsonc", &reporter, &root);

    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, ret_oom_init); // [確認_異常系] - 初期確保失敗で POTR_ERR_OUT_OF_MEMORY。
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, ret_oom_grow); // [確認_異常系] - 拡張時確保失敗で POTR_ERR_OUT_OF_MEMORY。
    EXPECT_EQ(nullptr, root);                        // [確認_異常系] - 確保失敗時に *root_out が nullptr であること。
}

// 生 NUL と JSON 文字列内の \u0000 の位置 (CRLF / コメント / \\u0000 の区別含む) と 0x1A 読込の確認
TEST(configDocumentTest, rejectsRawNulAndStringU0000WhileAllowingCommentsAndEscapedBackslash)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    NiceMock<Mock_cjson> mock_cjson;
    cJSON *root = nullptr;
    std::vector<doc_issue> issues;
    potr_internal_config_reporter reporter = {};
    potr_internal_config_reporter_init(&reporter, "nul.jsonc", on_doc_issue, &issues, 0);

    // 1) 生の NUL バイト (2 行目 5 桁目)
    std::string raw_nul = "{\r\n  \"a";
    raw_nul.push_back('\0');
    raw_nul += "\":1}";
    ConfigLineStream s_nul(raw_nul);
    setup_binary_stream(mock_cplat, mock_stdio, s_nul, "raw_nul.jsonc");
    int ret_nul = potr_internal_config_document_load("raw_nul.jsonc", &reporter, &root);

    // 2) JSON 文字列中の \u0000 (3 行目 8 桁目)
    std::string str_u0000 = "{\n  // \\u0000 comment\n  \"a\":\"\\u0000\"\n}";
    ConfigLineStream s_u0(str_u0000);
    setup_binary_stream(mock_cplat, mock_stdio, s_u0, "u0000.jsonc");
    int ret_u0 = potr_internal_config_document_load("u0000.jsonc", &reporter, &root);

    // 3) コメント中の \u0000、エスケープされた \\u0000、文字列中の 0x1A は受理
    std::string valid_doc = "// \\u0000\r\n/* \\u0000 */\r\n{\"k\":\"\\\\u0000";
    valid_doc.push_back('\x1A');
    valid_doc += "\"}";
    ConfigLineStream s_ok(valid_doc);
    setup_binary_stream(mock_cplat, mock_stdio, s_ok, "ok.jsonc");
    int ret_ok = potr_internal_config_document_load("ok.jsonc", &reporter, &root);

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_nul); // [確認_異常系] - 生 NUL で POTR_ERR_INVALID_CONFIG。
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_u0);  // [確認_異常系] - 文字列内 \u0000 で POTR_ERR_INVALID_CONFIG。
    ASSERT_EQ(2U, issues.size());                // [確認_異常系] - NUL 系の 2 件だけ報告されること。
    EXPECT_EQ(POTR_CONFIG_ISSUE_SYNTAX, issues[0].kind);
    EXPECT_EQ(2U, issues[0].line);
    EXPECT_EQ(5U, issues[0].column);
    // [確認_異常系] - CRLF 後の生 NUL 位置が 2 行目 5 桁目であること。
    EXPECT_EQ(POTR_CONFIG_ISSUE_SYNTAX, issues[1].kind);
    EXPECT_EQ(3U, issues[1].line);
    EXPECT_EQ(8U, issues[1].column);
    // [確認_異常系] - コメントを飛ばし文字列内 \u0000 の開始 '\' (3 行目 8 桁目) を指すこと。
    ASSERT_EQ(POTR_OK, ret_ok); // [確認_正常系] - コメント・\\u0000・0x1A を含む設定を受理すること。
    ASSERT_NE(nullptr, root);
    cjson_app_cJSON_Delete(root);
}

// cJSON_ParseJSONCWithLengthOpts モックによる SYNTAX / OOM 分岐、EOF 付近行桁変換、および実 JSONC 位置確認
TEST(configDocumentTest, mapsJsoncWrapperStatusAndLineColumnPositions)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    NiceMock<Mock_cjson> mock_cjson;
    cJSON *root = nullptr;
    std::vector<doc_issue> issues;
    potr_internal_config_reporter reporter = {};
    potr_internal_config_reporter_init(&reporter, "wrap.jsonc", on_doc_issue, &issues, 0);

    // 1) ラッパーからの SYNTAX (2 行目の末尾) と CJSON_GetErrorPtr 非呼出
    ConfigLineStream s1("{\n  }");
    setup_binary_stream(mock_cplat, mock_stdio, s1, "syn.jsonc");
    EXPECT_CALL(mock_cjson, cjson_app_cJSON_GetErrorPtr(_, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - 非スレッドセーフな cJSON_GetErrorPtr() を呼ばないこと。
    EXPECT_CALL(mock_cjson, cjson_app_cJSON_ParseJSONCWithLengthOpts(_, _, _, _, _, _, _))
        .WillOnce(Invoke([](const char *, int, const char *, const char *, size_t len, cjson_app_jsonc_status *status,
                            size_t *offset) -> cJSON * {
            *status = CJSON_APP_JSONC_SYNTAX_ERROR;
            *offset = len; // EOF 位置の境界確認
            return nullptr;
        }));
    int ret_syn = potr_internal_config_document_load("syn.jsonc", &reporter, &root);

    // 2) ラッパーからの OUT_OF_MEMORY
    ConfigLineStream s2("{\"a\":1}");
    setup_binary_stream(mock_cplat, mock_stdio, s2, "oom_wrap.jsonc");
    EXPECT_CALL(mock_cjson, cjson_app_cJSON_ParseJSONCWithLengthOpts(_, _, _, _, _, _, _))
        .WillOnce(Invoke([](const char *, int, const char *, const char *, size_t, cjson_app_jsonc_status *status,
                            size_t *offset) -> cJSON * {
            *status = CJSON_APP_JSONC_OUT_OF_MEMORY;
            *offset = 0U;
            return nullptr;
        }));
    int ret_oom = potr_internal_config_document_load("oom_wrap.jsonc", &reporter, &root);

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_syn); // [確認_異常系] - SYNTAX_ERROR で POTR_ERR_INVALID_CONFIG。
    ASSERT_EQ(1U, issues.size());                // [確認_異常系] - SYNTAX 誤りが 1 件通知されること。
    EXPECT_EQ(POTR_CONFIG_ISSUE_SYNTAX, issues[0].kind);
    EXPECT_EQ(2U, issues[0].line);   // [確認_異常系] - EOF オフセットでも 2 行目になること。
    EXPECT_EQ(4U, issues[0].column); // [確認_異常系] - EOF オフセットの byte 列が 4 になること。
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, ret_oom); // [確認_異常系] - OOM で POTR_ERR_OUT_OF_MEMORY かつ追加 syntax なし。
    EXPECT_EQ(1U, issues.size());
}
