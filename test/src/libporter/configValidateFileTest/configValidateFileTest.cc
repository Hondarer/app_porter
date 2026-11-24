/**
 *******************************************************************************
 *  @file           configValidateFileTest.cc
 *  @brief          設定ファイル検証 API (potr_config_validate_file) の単体テストです。
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

#include <mock_cplat.h>
#include <mock_porter.h>
#include <porter/porter_result.h>
#include <porter/porter_spec.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace testing;

namespace
{
struct recorded_issue
{
    potr_config_issue_kind kind;
    int has_service_id;
    int64_t service_id;
    std::string key_path;
    std::string message;
    std::string expected;
};

struct recorded_validation
{
    std::vector<recorded_issue> issues;
    std::vector<int64_t> services;
    std::vector<std::string> auth_key_paths;
    std::vector<std::string> auth_paths;
};

void on_issue_rec(const potr_config_issue *issue, void *user_data)
{
    auto *rec = static_cast<recorded_validation *>(user_data);
    recorded_issue it = {};
    it.kind = issue->kind;
    it.has_service_id = issue->has_service_id;
    it.service_id = issue->service_id;
    it.key_path = issue->key_path != nullptr ? issue->key_path : "";
    it.message = issue->message != nullptr ? issue->message : "";
    it.expected = issue->expected != nullptr ? issue->expected : "";
    rec->issues.push_back(it);
}

void on_service_rec(int64_t service_id, void *user_data)
{
    auto *rec = static_cast<recorded_validation *>(user_data);
    rec->services.push_back(service_id);
}

void on_auth_rec(const potr_config_file_ref *ref, void *user_data)
{
    auto *rec = static_cast<recorded_validation *>(user_data);
    rec->auth_key_paths.emplace_back(ref->key_path != nullptr ? ref->key_path : "");
    rec->auth_paths.emplace_back(ref->path != nullptr ? ref->path : "");
}

class TempJsoncFile
{
  public:
    TempJsoncFile(const std::string &tag, const std::string &content)
    {
        static int seq = 0;
        ++seq;
        path_ = "tmp_cvf_" + tag + "_" + std::to_string(seq) + ".jsonc";
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

bool has_issue(const recorded_validation &rec, potr_config_issue_kind kind, const std::string &path)
{
    for (const auto &it : rec.issues)
    {
        if (it.kind == kind && it.key_path == path)
        {
            return true;
        }
    }
    return false;
}
} // namespace

// NULL パス、未定義役割ビット、予約値非 0 で POTR_ERR_INVALID_ARGUMENT と件数 0、options NULL で件数返却の確認
TEST(configValidateFileTest, checksArgumentsAndAllowsNullOptions)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    size_t count = 99U;
    potr_config_validate_options bad_roles = {};
    bad_roles.roles = 0x4U;
    potr_config_validate_options bad_reserved = {};
    bad_reserved.reserved = 1U;

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_ERROR, _, _)).Times(0);

    // Act
    int ret_null = potr_config_validate_file(nullptr, nullptr, &count);
    size_t c_null = count;
    count = 99U;
    int ret_bad_roles = potr_config_validate_file("a.jsonc", &bad_roles, &count);
    size_t c_roles = count;
    count = 99U;
    int ret_bad_res = potr_config_validate_file("a.jsonc", &bad_reserved, &count);
    size_t c_res = count;

    TempJsoncFile s_err("opts_null", "{\"global\":{\"window_size\":1},\"services\":{\"1\":{\"dst_port\":5001}}}");
    setup_stream(mock_cplat, s_err, "opts_null.jsonc");
    int ret_opts_null = potr_config_validate_file("opts_null.jsonc", nullptr, &count);

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, ret_null);
    EXPECT_EQ(0U, c_null);
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, ret_bad_roles);
    EXPECT_EQ(0U, c_roles);
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, ret_bad_res);
    EXPECT_EQ(0U, c_res);
    // [確認_異常系] - 引数不正はいずれも POTR_ERR_INVALID_ARGUMENT と件数 0 を返すこと。
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_opts_null);
    EXPECT_EQ(2U, count);
    // [確認_異常系] - options が NULL でも誤り件数を返し ERROR トレースを出さないこと。
}

// 構文誤り・I/O 失敗・途中 OOM での打ち切りと、それまでの報告保持・未通知の確認
TEST(configValidateFileTest, abortsOnSyntaxIoAndMidValidationOom)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    recorded_validation rec_syn;
    potr_config_validate_options opts = {};
    opts.roles = POTR_CONFIG_ROLE_ALL;
    opts.on_issue = on_issue_rec;
    opts.on_service = on_service_rec;
    size_t count = 99U;

    // 1) 構文誤り
    TempJsoncFile s_syn("syn", "{,}");
    setup_stream(mock_cplat, s_syn, "syn.jsonc");
    opts.user_data = &rec_syn;
    int ret_syn = potr_config_validate_file("syn.jsonc", &opts, &count);
    size_t syn_count = count;

    // 2) IO 失敗
    recorded_validation rec_io;
    opts.user_data = &rec_io;
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("nofile.jsonc"), StrEq("rb"), nullptr)).WillOnce(Return(nullptr));
    int ret_io = potr_config_validate_file("nofile.jsonc", &opts, &count);
    size_t io_count = count;
    ASSERT_TRUE(Mock::VerifyAndClearExpectations(&mock_cplat));

    // 3) global 報告後のパス構築での OOM (途中のメモリ不足)
    recorded_validation rec_oom;
    opts.user_data = &rec_oom;
    std::string long_unknown(300U, 'u');
    std::string oom_json = "{\"global\":{\"window_size\":1,\"send_queue_depth\":1},\"services\":{\"1\":{\"" +
                           long_unknown + "\":1}}}";
    TempJsoncFile s_oom("oom", oom_json);
    setup_stream(mock_cplat, s_oom, "oom.jsonc");
    // chunk/read buf の確保を許し、path_push の 256 byte 超確保で失敗させる
    EXPECT_CALL(mock_cplat, cplat_malloc(_)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_malloc(Ge(300U)))
        .WillOnce(Invoke(delegate_real_cplat_malloc))
        .WillOnce(Return(nullptr));
    int ret_oom = potr_config_validate_file("oom.jsonc", &opts, &count);

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_syn);
    EXPECT_EQ(1U, syn_count);
    EXPECT_TRUE(rec_syn.services.empty());
    // [確認_異常系] - 構文誤りで 1 件報告、サービス未通知、POTR_ERR_INVALID_CONFIG を返すこと。
    EXPECT_EQ(POTR_ERR_IO, ret_io);
    EXPECT_EQ(0U, io_count);
    EXPECT_TRUE(rec_io.issues.empty());
    // [確認_異常系] - IO 失敗で報告 0 件、POTR_ERR_IO を返すこと。
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, ret_oom);
    EXPECT_GE(rec_oom.issues.size(), 2U);
    // [確認_異常系] - 途中 OOM で POTR_ERR_OUT_OF_MEMORY が返り、直前の報告が残ること。
}

// 複数サービス・役割フィルタ・認証ファイル通知・ID 通知順・鍵非漏洩・トレース非出力の網羅確認
TEST(configValidateFileTest, reportsAllIssuesFiltersByRoleNotifiesAuthFilesAndServicesInOrder)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const std::string secret = "secret-key-should-not-appear";
    const std::string json =
        "{"
        "\"global\":{\"window_size\":1},"
        "\"services\":{"
        "\"-9223372036854775808\":{"
        "  \"type\":\"unicast_bidir\","
        "  \"dst_port\":5001"
        "},"
        "\"1001\":{"
        "  \"type\":\"unicast\","
        "  \"src_addr1\":\"127.0.0.1\","
        "  \"dst_addr1\":\"127.0.0.1\","
        "  \"dst_port\":99999,"
        "  \"encrypt_key\":\"" +
        secret +
        "\""
        "},"
        "\"9223372036854775807\":{"
        "  \"type\":\"tcp\","
        "  \"dst_addr1\":\"127.0.0.1\","
        "  \"dst_port\":6001,"
        "  \"auth\":\"cert\","
        "  \"cert_file\":\"server.crt\","
        "  \"key_file\":\"server.key\","
        "  \"ca_file\":\"ca.crt\","
        "  \"crl_file\":\"ca.crl\","
        "  \"pinned_certs\":[\"pin1.crt\", \"\"],"
        "  \"peer_names\":[\"client.local\"]"
        "}"
        "}"
        "}";

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - potr_config_validate_file() がトレースを出力しないこと。
    ON_CALL(mock_cplat, cplat_passphrase_to_key(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));

    // 1) POTR_CONFIG_ROLE_RECEIVER
    TempJsoncFile s_all("all", json);
    setup_stream(mock_cplat, s_all, "all.jsonc");
    recorded_validation rec_recv;
    potr_config_validate_options o_recv = {POTR_CONFIG_ROLE_RECEIVER, 0U, on_issue_rec, on_auth_rec, on_service_rec,
                                           &rec_recv};
    size_t cnt_recv = 0U;
    int ret_recv = potr_config_validate_file("all.jsonc", &o_recv, &cnt_recv);

    // 2) POTR_CONFIG_ROLE_SENDER
    recorded_validation rec_send;
    potr_config_validate_options o_send = {POTR_CONFIG_ROLE_SENDER, 0U, on_issue_rec, on_auth_rec, on_service_rec,
                                           &rec_send};
    size_t cnt_send = 0U;
    int ret_send = potr_config_validate_file("all.jsonc", &o_send, &cnt_send);

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_recv);
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_send);
    EXPECT_EQ(rec_recv.issues.size(), cnt_recv);
    EXPECT_EQ(rec_send.issues.size(), cnt_send);
    EXPECT_FALSE(has_issue(rec_recv, POTR_CONFIG_ISSUE_CONSTRAINT, "/services/-9223372036854775808/dst_addr1"));
    EXPECT_TRUE(has_issue(rec_send, POTR_CONFIG_ISSUE_CONSTRAINT, "/services/-9223372036854775808/dst_addr1"));
    // [確認_異常系] - 役割別の条件が RECEIVER では出ず SENDER で報告されること。
    EXPECT_TRUE(has_issue(rec_send, POTR_CONFIG_ISSUE_OUT_OF_RANGE, "/global/window_size"));
    EXPECT_TRUE(has_issue(rec_send, POTR_CONFIG_ISSUE_OUT_OF_RANGE, "/services/1001/dst_port"));
    EXPECT_TRUE(has_issue(rec_send, POTR_CONFIG_ISSUE_KEY_DERIVATION, "/services/1001/encrypt_key"));
    EXPECT_TRUE(has_issue(rec_send, POTR_CONFIG_ISSUE_INVALID_VALUE, "/services/9223372036854775807/pinned_certs/1"));
    ASSERT_EQ(3U, rec_send.services.size());
    EXPECT_EQ(INT64_MIN, rec_send.services[0]);
    EXPECT_EQ(1001, rec_send.services[1]);
    EXPECT_EQ(INT64_MAX, rec_send.services[2]);
    // [確認_正常系] - on_service が定義順 (INT64_MIN, 1001, INT64_MAX) で呼ばれること。
    ASSERT_EQ(5U, rec_send.auth_paths.size());
    EXPECT_EQ("server.crt", rec_send.auth_paths[0]);
    EXPECT_EQ("server.key", rec_send.auth_paths[1]);
    EXPECT_EQ("ca.crt", rec_send.auth_paths[2]);
    EXPECT_EQ("ca.crl", rec_send.auth_paths[3]);
    EXPECT_EQ("pin1.crt", rec_send.auth_paths[4]);
    // [確認_正常系] - 5 種の有効認証ファイルだけが通知され、不正要素 "" と PSK サービスは渡らないこと。
    for (const auto &it : rec_send.issues)
    {
        EXPECT_EQ(std::string::npos, it.message.find(secret));
        EXPECT_EQ(std::string::npos, it.expected.find(secret));
        // [確認_異常系] - 報告文字列に encrypt_key の値が一切含まれないこと。
    }
}
