/**
 *******************************************************************************
 *  @file           configFileLoadTest.cc
 *  @brief          実ファイルを用いる設定検証・列挙・種別取得 API の統合テストです。
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

#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include <porter/porter_spec.h>
#include <porter_test_helper.h>

#include <cplat/crt/path.h>
#include <cplat/crt/stdio.h>
#include <cplat/trace/tracer.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace
{
struct validate_capture
{
    std::vector<int64_t> services;
    std::vector<potr_config_issue_kind> kinds;
    std::vector<int> has_service_id;
    std::vector<int64_t> issue_service_ids;
    std::vector<std::string> key_paths;
    std::vector<std::string> auth_paths;
};

void capture_service(int64_t service_id, void *user_data)
{
    validate_capture *cap = static_cast<validate_capture *>(user_data);
    cap->services.push_back(service_id);
}

void capture_issue(const potr_config_issue *issue, void *user_data)
{
    validate_capture *cap = static_cast<validate_capture *>(user_data);
    cap->kinds.push_back(issue->kind);
    cap->has_service_id.push_back(issue->has_service_id);
    cap->issue_service_ids.push_back(issue->service_id);
    cap->key_paths.emplace_back(issue->key_path != nullptr ? issue->key_path : "");
}

std::string write_temp_file(const std::string &name_hint, const std::string &bytes)
{
    std::filesystem::path dir = std::filesystem::temp_directory_path();
    std::filesystem::path p = dir / ("porter_cfg_integ_" + name_hint + ".jsonc");
    FILE *fp = cplat_fopen(p.u8string().c_str(), "wb", nullptr);
    if (fp == nullptr)
    {
        return "";
    }
    size_t written = fwrite(bytes.data(), 1U, bytes.size(), fp);
    (void)fclose(fp); // テスト用一時ファイルの書き込み後クローズであり、written 比較で書き込み完了を検証するため戻り値は明示的に破棄する。
    if (written != bytes.size())
    {
        std::error_code ec;
        std::filesystem::remove(p, ec);
        return "";
    }
    return p.u8string();
}

void remove_temp_file(const std::string &path)
{
    if (!path.empty())
    {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::u8path(path), ec);
    }
}
} // namespace

// 実ファイルに対して検証、サービス ID 列挙、種別取得が一貫して動作することの確認
// potr-req: id=POTR-CONFIG-FUNC-007; uuid=a1ee646d-15a7-466f-904e-d1c2d752f69f
TEST(configFileLoadTest, validatesListsAndGetsServiceTypesFromRealFile)
{
    // Arrange
    PorterConfigBuilder builder;
    int64_t *ids = nullptr;
    size_t count = 0U;
    size_t issue_count = 99U;
    validate_capture cap;
    potr_config_validate_options opts = {};
    opts.roles = POTR_CONFIG_ROLE_ALL;
    opts.on_service = capture_service;
    opts.on_issue = capture_issue;
    opts.user_data = &cap;
    potr_type actual_type = (potr_type)0;

    std::string config_path = builder.setUdpHealthIntervalMs(111U)
                                  .setUdpHealthTimeoutMs(222U)
                                  .setTcpHealthIntervalMs(333U)
                                  .setTcpHealthTimeoutMs(444U)
                                  .setTcpCloseTimeoutMs(555U)
                                  .addUnicastService(1001, 5001, "127.0.0.1")
                                  .addUnicastService(INT64_MAX, 5003, "127.0.0.1")
                                  .addTcpBidirService(2002, 6002, "127.0.0.1")
                                  .build();

    ASSERT_FALSE(config_path.empty()); // [状態確認] - 実ファイルへ 3 サービスを書き出せること。

    // Pre-Assert

    // Act
    int ret_validate = potr_config_validate_file(config_path.c_str(), &opts, &issue_count);
    int ret_list = potr_config_list_service_ids(config_path.c_str(), &ids, &count);
    int ret_type = potr_service_get_type(config_path.c_str(), 2002, &actual_type);
    // [手順] - potr_config_validate_file()、potr_config_list_service_ids()、potr_service_get_type() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_OK, ret_validate);         // [確認_正常系] - 検証が POTR_OK であること。
    EXPECT_EQ(0U, issue_count);               // [確認_正常系] - 誤り件数が 0 であること。
    ASSERT_EQ(3U, cap.services.size());       // [確認_正常系] - on_service で 3 件通知されること。
    EXPECT_EQ(1001, cap.services[0]);         // [確認_正常系] - 1 件目が 1001 であること。
    EXPECT_EQ(INT64_MAX, cap.services[1]);    // [確認_正常系] - 2 件目が INT64_MAX であること。
    EXPECT_EQ(2002, cap.services[2]);         // [確認_正常系] - 3 件目が 2002 であること。
    ASSERT_EQ(POTR_OK, ret_list);             // [確認_正常系] - 列挙が POTR_OK であること。
    ASSERT_NE(nullptr, ids);                  // [確認_正常系] - ids が nullptr 以外であること。
    ASSERT_EQ(3U, count);                     // [確認_正常系] - count が 3 であること。
    EXPECT_EQ(1001, ids[0]);                  // [確認_正常系] - 列挙 1 件目が 1001 であること。
    EXPECT_EQ(INT64_MAX, ids[1]);             // [確認_正常系] - 列挙 2 件目が INT64_MAX であること。
    EXPECT_EQ(2002, ids[2]);                  // [確認_正常系] - 列挙 3 件目が 2002 であること。
    EXPECT_EQ(POTR_OK, ret_type);             // [確認_正常系] - 種別取得が POTR_OK であること。
    EXPECT_EQ(POTR_TYPE_TCP_BIDIR, actual_type); // [確認_正常系] - 種別が POTR_TYPE_TCP_BIDIR であること。

    // Cleanup
    potr_config_service_ids_free(ids);
}

// 他サービス本文の誤りが別のサービスの potr_service_get_type() を妨げず、失敗時に *type を書き換えないことの確認
TEST(configFileLoadTest, isolatesOtherServiceBodyErrorsAndPreservesTypeOnError)
{
    // Arrange
    const std::string json = "{\n"
                             "  \"global\": { \"window_size\": 16 },\n"
                             "  \"services\": {\n"
                             "    \"1001\": { \"type\": \"unicast\", \"src_addr1\": \"127.0.0.1\", "
                             "\"dst_addr1\": \"127.0.0.1\", \"dst_port\": 5001 },\n"
                             "    \"1002\": { \"type\": \"tcp\", \"dst_addr1\": \"127.0.0.1\", \"dst_port\": 99999 }\n"
                             "  }\n"
                             "}\n";
    std::string path = write_temp_file("isolation", json);
    ASSERT_FALSE(path.empty()); // [状態確認] - 一時設定ファイルを生成できること。
    validate_capture cap;
    size_t issue_count = 0U;
    potr_config_validate_options opts = {};
    opts.roles = POTR_CONFIG_ROLE_ALL;
    opts.on_service = capture_service;
    opts.on_issue = capture_issue;
    opts.user_data = &cap;
    potr_type target_ok = POTR_TYPE_BROADCAST;
    potr_type target_missing = POTR_TYPE_MULTICAST;
    potr_type target_invalid = POTR_TYPE_TCP_BIDIR;

    // Pre-Assert

    // Act
    int ret_valid_svc = potr_service_get_type(path.c_str(), 1001, &target_ok);
    int ret_not_found = potr_service_get_type(path.c_str(), 9999, &target_missing);
    int ret_invalid_svc = potr_service_get_type(path.c_str(), 1002, &target_invalid);
    int ret_validate = potr_config_validate_file(path.c_str(), &opts, &issue_count);
    // [手順] - 有効な 1001、未定義 9999、値域外 1002 に対する get_type() と、ファイル全体の検証を呼び出す。

    // Assert
    EXPECT_EQ(POTR_OK, ret_valid_svc);              // [確認_正常系] - 1001 の種別取得は他サービスの誤りに影響されず POTR_OK であること。
    EXPECT_EQ(POTR_TYPE_UNICAST, target_ok);        // [確認_正常系] - 1001 の種別が POTR_TYPE_UNICAST であること。
    EXPECT_EQ(POTR_ERR_NOT_FOUND, ret_not_found);   // [確認_異常系] - 未定義 ID 9999 は POTR_ERR_NOT_FOUND であること。
    EXPECT_EQ(POTR_TYPE_MULTICAST, target_missing); // [確認_異常系] - NOT_FOUND 時に *type が維持されること。
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_invalid_svc); // [確認_異常系] - 1002 は POTR_ERR_INVALID_CONFIG であること。
    EXPECT_EQ(POTR_TYPE_TCP_BIDIR, target_invalid); // [確認_異常系] - 設定エラー時に *type が維持されること。
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_validate); // [確認_異常系] - 全体検証は POTR_ERR_INVALID_CONFIG であること。
    EXPECT_EQ(1U, issue_count);                     // [確認_異常系] - 誤り件数が 1 件であること。
    ASSERT_EQ(2U, cap.services.size());             // [確認_異常系] - on_service は 1001 と 1002 の 2 件を通知すること。

    // Cleanup
    remove_temp_file(path);
}

// バイナリ モード ("rb") により、Windows でも JSONC コメント内の 0x1A で打ち切らず最後まで読めることの確認
TEST(configFileLoadTest, readsPastCtrlZInsideJsoncCommentOnWindows)
{
    // Arrange
    std::string content = "// comment with ";
    content.push_back('\x1A');
    content += " ctrl-z\n"
               "{\n"
               "  \"services\": {\n"
               "    \"1001\": {\n"
               "      \"type\": \"unicast\",\n"
               "      \"src_addr1\": \"127.0.0.1\",\n"
               "      \"dst_addr1\": \"127.0.0.1\",\n"
               "      \"dst_port\": 5001\n"
               "    }\n"
               "  }\n"
               "}\n";
    std::string path = write_temp_file("ctrlz", content);
    ASSERT_FALSE(path.empty()); // [状態確認] - 0x1A を含む一時設定ファイルを生成できること。
    size_t issue_count = 99U;
    potr_config_validate_options opts = {};
    opts.roles = POTR_CONFIG_ROLE_ALL;
    int64_t *ids = nullptr;
    size_t count = 0U;

    // Pre-Assert

    // Act
    int ret_validate = potr_config_validate_file(path.c_str(), &opts, &issue_count);
    int ret_list = potr_config_list_service_ids(path.c_str(), &ids, &count);
    // [手順] - 0x1A を含むファイルを検証および列挙する。

    // Assert
    EXPECT_EQ(POTR_OK, ret_validate); // [確認_正常系] - 検証が POTR_OK であること。
    EXPECT_EQ(0U, issue_count);       // [確認_正常系] - 誤り件数が 0 であること。
    ASSERT_EQ(POTR_OK, ret_list);     // [確認_正常系] - 列挙が POTR_OK であること。
    ASSERT_EQ(1U, count);             // [確認_正常系] - 0x1A 以降に定義された 1001 を取得できること。
    EXPECT_EQ(1001, ids[0]);          // [確認_正常系] - 取得した ID が 1001 であること。

    // Cleanup
    potr_config_service_ids_free(ids);
    remove_temp_file(path);
}

// リポジトリの sample-config/porter-services.jsonc が potr_config_validate_file() と列挙で正常と判定されることの確認
TEST(configFileLoadTest, validatesSampleConfigSuccessfully)
{
    // Arrange
    std::filesystem::path sample = std::filesystem::current_path();
    while (!sample.empty() && !std::filesystem::exists(sample / "prod" / "sample-config" / "porter-services.jsonc"))
    {
        if (sample == sample.parent_path())
        {
            sample.clear();
            break;
        }
        sample = sample.parent_path();
    }
    ASSERT_FALSE(sample.empty()) << "sample-config/porter-services.jsonc not found from cwd";
    std::string sample_path = (sample / "prod" / "sample-config" / "porter-services.jsonc").u8string();

    size_t issue_count = 99U;
    validate_capture cap;
    potr_config_validate_options opts = {};
    opts.roles = POTR_CONFIG_ROLE_ALL;
    opts.on_service = capture_service;
    opts.on_issue = capture_issue;
    opts.user_data = &cap;
    int64_t *ids = nullptr;
    size_t id_count = 0U;

    // Pre-Assert

    // Act
    int ret_validate = potr_config_validate_file(sample_path.c_str(), &opts, &issue_count);
    int ret_list = potr_config_list_service_ids(sample_path.c_str(), &ids, &id_count);
    // [手順] - sample-config/porter-services.jsonc を検証および列挙する。

    // Assert
    EXPECT_EQ(POTR_OK, ret_validate);                    // [確認_正常系] - サンプル設定が POTR_OK であること。
    EXPECT_EQ(0U, issue_count);                          // [確認_正常系] - 誤り件数が 0 であること。
    EXPECT_EQ(POTR_OK, ret_list);                        // [確認_正常系] - 列挙が POTR_OK であること。
    EXPECT_GT(id_count, 0U);                             // [確認_正常系] - 1 件以上のサービス ID を返すこと。
    EXPECT_EQ(cap.services.size(), id_count);            // [確認_正常系] - 検証と列挙で同じ件数であること。

    // Cleanup
    potr_config_service_ids_free(ids);
}

namespace
{
std::vector<std::string> *g_trace_lines = nullptr;

void config_trace_hook(cplat_tracer_hook_entry *prev, cplat_tracer *handle, cplat_trace_level level,
                       const cplat_timespec *timestamp, const char *message, void *user_data)
{
    cplat_tracer_call_next_hook(prev, handle, level, timestamp, message);
    (void)user_data;
    if (level == CPLAT_TRACE_LEVEL_ERROR && g_trace_lines != nullptr && message != nullptr)
    {
        g_trace_lines->emplace_back(message);
    }
}
} // namespace

// 検証関数と open_from_config の設定誤り判定の一致および複数スレッド並列検証・列挙の整合性確認
TEST(configFileLoadTest, parityWithOpenAndConcurrentValidateAndListIds)
{
    // Arrange
    const std::string json =
        "{\n"
        "  \"global\": { \"window_size\": 16 },\n"
        "  \"services\": {\n"
        "    \"1001\": { \"type\": \"unicast\", \"src_addr1\": \"127.0.0.1\", \"dst_addr1\": \"127.0.0.1\", \"dst_port\": 5001 },\n"
        "    \"1002\": { \"type\": \"tcp\", \"dst_addr1\": \"127.0.0.1\", \"dst_port\": 70000 }\n"
        "  }\n"
        "}\n";
    std::string path = write_temp_file("parity_conc", json);
    ASSERT_FALSE(path.empty()); // [状態確認] - 正常・異常サービスを含む一時ファイルを作成できること。

    // 1) open_from_config との誤り一致確認 (サービス 1002 の誤り)
    validate_capture cap;
    size_t issue_count = 0U;
    potr_config_validate_options opts = {POTR_CONFIG_ROLE_ALL, 0U, capture_issue, nullptr, capture_service, &cap};
    int ret_val = potr_config_validate_file(path.c_str(), &opts, &issue_count);
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_val);
    EXPECT_EQ(1U, issue_count);

    std::vector<std::string> trace_lines;
    g_trace_lines = &trace_lines;
    cplat_tracer *tracer = potr_tracer_get();
    cplat_tracer_hook_entry *hook = nullptr;
    if (tracer != nullptr)
    {
        hook = cplat_tracer_set_hook(tracer, config_trace_hook, nullptr);
        /* 検証用の一時フックを有効にするため開始し、解放前に停止する。戻り値は後の確認に影響しないため破棄する。 */
        (void)cplat_tracer_start(tracer);
    }
    potr_context *handle = nullptr;
    int ret_open = potr_service_open_from_config(path.c_str(), 1002, POTR_ROLE_SENDER, nullptr, &handle);
    if (tracer != nullptr && hook != nullptr)
    {
        (void)cplat_tracer_stop(tracer);
        cplat_tracer_remove_hook(tracer, hook);
    }
    g_trace_lines = nullptr;
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_open);
    EXPECT_EQ(nullptr, handle);
    bool found_matching_trace = false;
    for (const auto &line : trace_lines)
    {
        if (line.find("/services/1002/dst_port") != std::string::npos &&
            line.find("(service_id=1002)") != std::string::npos)
        {
            found_matching_trace = true;
            break;
        }
    }
    EXPECT_TRUE(found_matching_trace);
    // [確認_異常系] - validate_file の報告箇所と同じエラーが open_from_config の ERROR トレースへ記録されること。

    // 2) potr_config_validate_file() と potr_config_list_service_ids() の複数スレッド同時実行
    constexpr size_t kThreads = 4U;
    std::vector<int> val_rets(kThreads, -1);
    std::vector<size_t> val_counts(kThreads, 0U);
    std::vector<int> list_rets(kThreads, -1);
    std::vector<size_t> list_counts(kThreads, 0U);
    std::vector<std::thread> threads;
    for (size_t t = 0U; t < kThreads; t++)
    {
        threads.emplace_back([&, t]() {
            potr_config_validate_options t_opts = {};
            t_opts.roles = POTR_CONFIG_ROLE_ALL;
            val_rets[t] = potr_config_validate_file(path.c_str(), &t_opts, &val_counts[t]);

            int64_t *t_ids = nullptr;
            list_rets[t] = potr_config_list_service_ids(path.c_str(), &t_ids, &list_counts[t]);
            potr_config_service_ids_free(t_ids);
        });
    }
    for (auto &th : threads)
    {
        th.join();
    }
    for (size_t t = 0U; t < kThreads; t++)
    {
        EXPECT_EQ(POTR_ERR_INVALID_CONFIG, val_rets[t]);
        EXPECT_EQ(1U, val_counts[t]);
        EXPECT_EQ(POTR_OK, list_rets[t]);
        EXPECT_EQ(2U, list_counts[t]);
        // [確認_正常系 回数=4] - 並列実行した全スレッドで単独実行と同一の結果を得ること。
    }

    // Cleanup
    remove_temp_file(path);
}
