/**
 *******************************************************************************
 *  @file           configReportTest.cc
 *  @brief          設定検証の誤り報告と RFC 6901 JSON Pointer 構築の単体テストです。
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

#include <porter/porter_result.h>
#include <porter/protocol/config_report.h>
#include <mock_cplat.h>
#include <mock_porter.h>

#include <cstddef>
#include <string>
#include <vector>

using namespace testing;

namespace
{
struct captured_issue
{
    potr_config_issue_kind kind;
    int has_service_id;
    int64_t service_id;
    std::string key_path;
    std::string message;
    bool has_expected;
    std::string expected;
    uint32_t line;
    uint32_t column;
};

void capture_issue_cb(const potr_config_issue *issue, void *user_data)
{
    auto *out = static_cast<std::vector<captured_issue> *>(user_data);
    captured_issue item = {};
    item.kind = issue->kind;
    item.has_service_id = issue->has_service_id;
    item.service_id = issue->service_id;
    item.key_path = issue->key_path != nullptr ? issue->key_path : "";
    item.message = issue->message != nullptr ? issue->message : "";
    item.has_expected = (issue->expected != nullptr);
    item.expected = issue->expected != nullptr ? issue->expected : "";
    item.line = issue->line;
    item.column = issue->column;
    out->push_back(item);
}
} // namespace

// 報告先の初期化と、コールバック・件数加算・ERROR トレース抑止の有無が正しく動作することの確認
TEST(configReportTest, reporterInitAndIssueDispatch)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    std::vector<captured_issue> issues;
    potr_internal_config_reporter reporter = {};
    potr_internal_config_reporter null_path_reporter = {};
    // [状態] - コールバック付き・トレース無効の報告先と、パスが NULL の報告先を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_ERROR, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - trace_errors == 0 の報告では ERROR トレースが出ないこと。

    // Act
    potr_internal_config_reporter_init(nullptr, "ignored", capture_issue_cb, &issues, 0);
    potr_internal_config_reporter_init(&reporter, "cfg.jsonc", capture_issue_cb, &issues, 0);
    potr_internal_config_reporter_init(&null_path_reporter, nullptr, nullptr, nullptr, 0);
    potr_internal_config_report_issue(nullptr, POTR_CONFIG_ISSUE_SYNTAX, 0, 0, "", "msg", nullptr, 1U, 1U);
    potr_internal_config_report_issue(&reporter, POTR_CONFIG_ISSUE_SYNTAX, 0, 99, "", "syntax err", nullptr, 3U, 7U);
    potr_internal_config_report_issue(&reporter, POTR_CONFIG_ISSUE_OUT_OF_RANGE, 1, 1001, "/services/1001/dst_port",
                                      "value is out of range", "1..65535", 5U, 9U);
    potr_internal_config_report_issue(&null_path_reporter, POTR_CONFIG_ISSUE_MISSING, 0, 0, nullptr, nullptr, nullptr,
                                      0U, 0U);
    // [手順] - 初期化と報告の呼び出しを実行する。

    // Assert
    EXPECT_STREQ("(null)", null_path_reporter.config_path); // [確認_正常系] - config_path == NULL が "(null)" になること。
    EXPECT_EQ(1U, null_path_reporter.issue_count);        // [確認_異常系] - コールバックなしでも件数が増えること。
    ASSERT_EQ(2U, issues.size());                         // [確認_正常系] - 2 件の誤りがコールバックへ渡ること。
    EXPECT_EQ(2U, reporter.issue_count);                  // [確認_正常系] - reporter.issue_count が 2 になること。
    EXPECT_EQ(POTR_CONFIG_ISSUE_SYNTAX, issues[0].kind);  // [確認_正常系] - 1 件目が SYNTAX であること。
    EXPECT_EQ(0, issues[0].has_service_id);               // [確認_正常系] - has_service_id が 0 であること。
    EXPECT_EQ(0, issues[0].service_id);                   // [確認_正常系] - has_service_id == 0 の service_id が 0 へ正規化されること。
    EXPECT_EQ(3U, issues[0].line);                        // [確認_正常系] - SYNTAX の行が保持されること。
    EXPECT_EQ(7U, issues[0].column);                      // [確認_正常系] - SYNTAX の桁が保持されること。
    EXPECT_EQ(1, issues[1].has_service_id);               // [確認_正常系] - 2 件目の has_service_id が 1 であること。
    EXPECT_EQ(1001, issues[1].service_id);                // [確認_正常系] - service_id が 1001 であること。
    EXPECT_EQ(0U, issues[1].line);                        // [確認_正常系] - 非 SYNTAX の line が 0 になること。
    EXPECT_EQ(0U, issues[1].column);                      // [確認_正常系] - 非 SYNTAX の column が 0 になること。
    EXPECT_TRUE(issues[1].has_expected);                  // [確認_正常系] - expected が渡ること。
    EXPECT_EQ("1..65535", issues[1].expected);            // [確認_正常系] - expected の内容が一致すること。
}

// trace_errors != 0 で制御文字エスケープ、(root)、(expected)、(service_id)、長パス省略を含む ERROR トレースが出ることの確認
TEST(configReportTest, formatsTraceErrorsWithEscapingAndOptionalSuffixes)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_config_reporter reporter = {};
    std::string cfg_with_ctrl = "a\nb\rc\td";
    cfg_with_ctrl.push_back('\x01');
    cfg_with_ctrl.push_back('\x7F');
    std::string long_path(600U, 'x');
    potr_internal_config_reporter_init(&reporter, cfg_with_ctrl.c_str(), nullptr, nullptr, 1);

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_ERROR, nullptr,
                                                 HasSubstr("a\\nb\\rc\\td\\x01\\x7F: line 2, column 4: bad json")))
        .Times(1); // [Pre-Assert確認_異常系] - SYNTAX トレースで制御文字エスケープと行・桁が出力されること。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(
                                _, CPLAT_TRACE_LEVEL_ERROR, nullptr,
                                AllOf(HasSubstr(": (root): root err"), Not(HasSubstr("(expected:")),
                                      Not(HasSubstr("(service_id=")))))
        .Times(1); // [Pre-Assert確認_異常系] - 空パスが (root) になり、expected と service_id が省略されること。
    EXPECT_CALL(mock_cplat,
                cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_ERROR, nullptr,
                                       AllOf(HasSubstr("...: capped (expected: 1..10) (service_id=42)"))))
        .Times(1); // [Pre-Assert確認_異常系] - 512 byte 超のキーパス末尾が ... で省略され、expected と service_id が付くこと。

    // Act
    potr_internal_config_report_issue(&reporter, POTR_CONFIG_ISSUE_SYNTAX, 0, 0, "", "bad json", nullptr, 2U, 4U);
    potr_internal_config_report_issue(&reporter, POTR_CONFIG_ISSUE_TYPE_MISMATCH, 0, 0, "", "root err", nullptr, 0U,
                                      0U);
    potr_internal_config_report_issue(&reporter, POTR_CONFIG_ISSUE_OUT_OF_RANGE, 1, 42, long_path.c_str(), "capped",
                                      "1..10", 0U, 0U);
    // [手順] - 3 通りの誤りをトレース出力有効で報告する。

    // Assert
    EXPECT_EQ(3U, reporter.issue_count); // [確認_異常系] - 3 件すべて計上されること。
}

// RFC 6901 JSON Pointer の ~0/~1 エスケープ、配列添字、256 byte 超の動的確保、OOM、NULL 引数拒否の確認
TEST(configReportTest, buildsAndDisposesRfc6901JsonPointers)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_config_path_buf p1 = {};
    potr_internal_config_path_buf p2 = {};
    potr_internal_config_path_buf pidx = {};
    potr_internal_config_path_buf plong = {};
    potr_internal_config_path_buf poom = {};
    std::string big_seg(300U, 'k');

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_malloc(_)).Times(AnyNumber());

    // Act
    int ret_null1 = potr_internal_config_path_push(nullptr, "", "a");
    int ret_null2 = potr_internal_config_path_push(&p1, "", nullptr);
    int ret_null3 = potr_internal_config_path_push_index(nullptr, "/a", 0U);
    int ret_esc = potr_internal_config_path_push(&p1, "", "a/b~c");
    int ret_child = potr_internal_config_path_push(&p2, p1.ptr, "dst_port");
    int ret_idx = potr_internal_config_path_push_index(&pidx, "/services/1/pinned_certs", 12U);
    int ret_long = potr_internal_config_path_push(&plong, "/services/1", big_seg.c_str());

    EXPECT_CALL(mock_cplat, cplat_malloc(_)).WillOnce(Return(nullptr));
    // [Pre-Assert確認_異常系] - 256 byte 超の確保時に cplat_malloc() が 1 回呼ばれること。
    // [Pre-Assert手順] - nullptr を返す。
    int ret_oom = potr_internal_config_path_push(&poom, "/services/1", big_seg.c_str());
    // [手順] - 引数誤り、エスケープ、子追加、配列添字、長大パス、確保失敗を順に実行する。

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, ret_null1); // [確認_異常系] - buf NULL で引数エラー。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, ret_null2); // [確認_異常系] - segment NULL で引数エラー。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, ret_null3); // [確認_異常系] - push_index で buf NULL は引数エラー。
    ASSERT_EQ(POTR_OK, ret_esc);                     // [確認_正常系] - エスケープ付き追加が成功すること。
    EXPECT_STREQ("/a~1b~0c", p1.ptr);                // [確認_正常系] - '/' が ~1、'~' が ~0 へ変換されること。
    ASSERT_EQ(POTR_OK, ret_child);                   // [確認_正常系] - 子パス追加が成功すること。
    EXPECT_STREQ("/a~1b~0c/dst_port", p2.ptr);       // [確認_正常系] - 連結結果が一致すること。
    ASSERT_EQ(POTR_OK, ret_idx);                     // [確認_正常系] - 配列添字追加が成功すること。
    EXPECT_STREQ("/services/1/pinned_certs/12", pidx.ptr); // [確認_正常系] - 添字 12 が付与されること。
    ASSERT_EQ(POTR_OK, ret_long);                    // [確認_正常系] - 256 byte 超の動的確保が成功すること。
    EXPECT_EQ(std::string("/services/1/") + big_seg, plong.ptr); // [確認_正常系] - 長大パスが欠けずに保持されること。
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, ret_oom);      // [確認_異常系] - 確保失敗時に POTR_ERR_OUT_OF_MEMORY となること。
    EXPECT_EQ(nullptr, poom.ptr);                    // [確認_異常系] - 確保失敗時に ptr が nullptr であること。

    // Cleanup
    potr_internal_config_path_dispose(&p1);
    potr_internal_config_path_dispose(&p2);
    potr_internal_config_path_dispose(&pidx);
    potr_internal_config_path_dispose(&plong);
    potr_internal_config_path_dispose(&poom);
    potr_internal_config_path_dispose(nullptr);
}
