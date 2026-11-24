/**
 *******************************************************************************
 *  @file           porterConfigCheckTest.cc
 *  @brief          設定確認用 CLI (porter-config-check) の単体テストです。
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

#include <cplat/crt/stdio.h>
#include <mock_cplat.h>
#include <mock_stdio.h>
#include <wrapmain.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace testing;

namespace
{
class TempDirGuard
{
  public:
    explicit TempDirGuard(const std::string &tag)
    {
        std::filesystem::path base = std::filesystem::temp_directory_path();
        dir_ = base / ("porter_cfg_check_test_" + tag);
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
        std::filesystem::create_directories(dir_, ec);
    }

    ~TempDirGuard()
    {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    std::string writeFile(const std::string &rel, const std::string &content) const
    {
        std::filesystem::path p = dir_ / rel;
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        FILE *fp = cplat_fopen(p.u8string().c_str(), "wb", nullptr);
        if (fp == nullptr)
        {
            return "";
        }
        size_t n = fwrite(content.data(), 1U, content.size(), fp);
        (void)fclose(fp); // テスト用一時ファイルの書き込み後クローズであり、書込バイト数で結果を検証するため戻り値は明示的に破棄する。
        return (n == content.size()) ? p.u8string() : "";
    }

    std::string subDir(const std::string &rel) const
    {
        std::filesystem::path p = dir_ / rel;
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        return p.u8string();
    }

  private:
    std::filesystem::path dir_;
};

std::string replace_backslashes(const std::string &s)
{
    std::string out;
    for (unsigned char c : s)
    {
        if (c == '\\')
        {
            out += "\\\\";
        }
        else if (c < 0x20U || c == 0x7FU)
        {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\x%02X", (unsigned int)c);
            out += buf;
        }
        else
        {
            out.push_back((char)c);
        }
    }
    return out;
}

int run_cli(std::vector<std::string> args, std::string *stdout_out, std::string *stderr_out)
{
    std::vector<std::vector<char>> bufs;
    std::vector<char *> argv;
    bufs.reserve(args.size());
    argv.reserve(args.size());
    for (const std::string &a : args)
    {
        bufs.emplace_back(a.begin(), a.end());
        bufs.back().push_back('\0');
    }
    for (auto &b : bufs)
    {
        argv.push_back(b.data());
    }

    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    int rc = __real_main(static_cast<int>(argv.size()), argv.data());
    *stdout_out = testing::internal::GetCapturedStdout();
    *stderr_out = testing::internal::GetCapturedStderr();
    return rc;
}

std::string find_sample_config()
{
    std::filesystem::path p = std::filesystem::current_path();
    while (!p.empty() && !std::filesystem::exists(p / "prod" / "sample-config" / "porter-services.jsonc"))
    {
        if (p == p.parent_path())
        {
            return "";
        }
        p = p.parent_path();
    }
    return p.empty() ? "" : (p / "prod" / "sample-config" / "porter-services.jsonc").u8string();
}
} // namespace

// -h、引数誤り (位置引数なし・未知オプション・--role 不正値)、不在ファイル (1)、sample-config (0) の終了コード確認
TEST(porterConfigCheckTest, verifiesHelpArgErrorsIoFailureAndSampleConfig)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    std::string out;
    std::string err;

    // 1) --help -> 0
    int rc_help = run_cli({"porter-config-check", "-h"}, &out, &err);
    EXPECT_EQ(0, rc_help); // [確認_正常系] - -h で終了コード 0 を返すこと。
    EXPECT_NE(std::string::npos, out.find("porter-config-check"));

    // 2) 位置引数不足 -> 1
    int rc_no_pos = run_cli({"porter-config-check"}, &out, &err);
    EXPECT_EQ(1, rc_no_pos); // [確認_異常系] - config_path 欠如で終了コード 1 を返すこと。
    EXPECT_FALSE(err.empty());

    // 3) 未知オプション -> 1
    int rc_unk_opt = run_cli({"porter-config-check", "--no-such-opt", "a.jsonc"}, &out, &err);
    EXPECT_EQ(1, rc_unk_opt); // [確認_異常系] - 未知のオプションで終了コード 1 を返すこと。

    // 4) --role 不正値 -> 1
    int rc_bad_role = run_cli({"porter-config-check", "--role", "invalid", "a.jsonc"}, &out, &err);
    EXPECT_EQ(1, rc_bad_role); // [確認_異常系] - --role 不正値で終了コード 1 を返すこと。
    EXPECT_NE(std::string::npos, err.find("--role"));

    // 5) 存在しないファイル -> 1 (STDERR に POTR_ERR_IO)
    int rc_missing = run_cli({"porter-config-check", "Z:/no/such/file/porter-missing.jsonc"}, &out, &err);
    EXPECT_EQ(1, rc_missing); // [確認_異常系] - 存在しないファイルで終了コード 1 を返すこと。
    EXPECT_NE(std::string::npos, err.find("エラー: 設定ファイルを読み取れません:"));
    EXPECT_NE(std::string::npos, err.find("POTR_ERR_IO"));
    EXPECT_TRUE(out.empty());

    // 6) リポジトリの sample-config/porter-services.jsonc -> 0
    std::string sample = find_sample_config();
    ASSERT_FALSE(sample.empty());
    int rc_sample = run_cli({"porter-config-check", sample}, &out, &err);
    EXPECT_EQ(0, rc_sample); // [確認_正常系] - sample-config が終了コード 0 になること。
    EXPECT_NE(std::string::npos, out.find(": OK ("));
    EXPECT_TRUE(err.empty());
}

// 誤り書式・全誤り種別・[expected: ...]・ルート '/'・エスケープ・サービス順行・不正 ID 時の非表示・鍵非漏洩の確認
TEST(porterConfigCheckTest, formatsIssuesServicesSummaryEscapingAndSecretMasking)
{
    // Arrange
    TempDirGuard dir("formats");
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    std::string out;
    std::string err;

    // 1) 構文誤り (ファイル:行:桁: error: syntax:)
    std::string syn_path = dir.writeFile("syn.jsonc", "{\n  \"global\":{,}\n}\n");
    int rc_syn = run_cli({"porter-config-check", syn_path}, &out, &err);
    EXPECT_EQ(2, rc_syn); // [確認_異常系] - 構文誤りで終了コード 2。
    EXPECT_NE(std::string::npos, out.find(replace_backslashes(syn_path) + ":2:"));
    EXPECT_NE(std::string::npos, out.find(": error: syntax:"));
    EXPECT_NE(std::string::npos, out.find(": 1 error(s)\n"));
    EXPECT_EQ(std::string::npos, out.find(": service "));

    // 2) 構文以外の全種別 + 制御文字/バックスラッシュ エスケープ + encrypt_key 非漏洩
    const std::string secret = "cli-secret-passphrase-abc";
    std::string multi_path = dir.writeFile(
        "multi.jsonc",
        "{\n"
        "  \"global\": {\n"
        "    \"window_size\": 1,\n"
        "    \"window_size\": 2,\n"
        "    \"unknown_global\": 1,\n"
        "    \"bad\\\\key\\u0001\": 1,\n"
        "    \"max_payload\": true\n"
        "  },\n"
        "  \"services\": {\n"
        "    \"20\": {\n"
        "      \"type\": \"tcp\",\n"
        "      \"dst_addr1\": \"127.0.0.1\",\n"
        "      \"ttl\": 1,\n"
        "      \"encrypt_key\": \"" +
            secret +
            "\"\n"
            "    },\n"
            "    \"10\": {\n"
            "      \"type\": \"unicast_bidir\",\n"
            "      \"dst_port\": \"badnum\"\n"
            "    }\n"
            "  }\n"
            "}\n");
    int rc_multi = run_cli({"porter-config-check", multi_path}, &out, &err);
    EXPECT_EQ(2, rc_multi); // [確認_異常系] - 複数種別誤りで終了コード 2。
    EXPECT_NE(std::string::npos, out.find(": error: range: /global/window_size: "));
    EXPECT_NE(std::string::npos, out.find("[expected: 2..256]"));
    EXPECT_NE(std::string::npos, out.find(": error: duplicate-key: /global/window_size: "));
    EXPECT_NE(std::string::npos, out.find(": error: unknown-key: /global/unknown_global: "));
    EXPECT_NE(std::string::npos, out.find(": error: unknown-key: /global/bad\\\\key\\x01: "));
    EXPECT_NE(std::string::npos, out.find(": error: type: /global/max_payload: "));
    EXPECT_NE(std::string::npos, out.find(": error: not-applicable: /services/20/ttl: "));
    EXPECT_NE(std::string::npos, out.find(": error: missing: /services/20/dst_port: "));
    EXPECT_NE(std::string::npos, out.find(": error: value: /services/10/dst_port: "));
    EXPECT_NE(std::string::npos, out.find(": error: constraint: /services/10/dst_addr1: "));
    // サービス順行 (20 -> 10) が保持されること
    size_t pos20 = out.find(": service 20: ");
    size_t pos10 = out.find(": service 10: ");
    EXPECT_NE(std::string::npos, pos20);
    EXPECT_NE(std::string::npos, pos10);
    EXPECT_LT(pos20, pos10); // [確認_異常系] - ファイル記載順 (20 -> 10) でサービス行が出力されること。
    EXPECT_EQ(std::string::npos, out.find(secret));
    EXPECT_EQ(std::string::npos, err.find(secret));
    // [確認_異常系] - encrypt_key の値が stdout / stderr に出ないこと。

    // 3) ルート誤り ('/') + service-id 不正 -> サービスごとの行を出さず、合計件数のみ出力
    std::string root_err_path = dir.writeFile("badroot.jsonc", "[]");
    int rc_arr = run_cli({"porter-config-check", root_err_path}, &out, &err);
    EXPECT_EQ(2, rc_arr);
    EXPECT_NE(std::string::npos, out.find(": error: type: /: "));
    EXPECT_EQ(std::string::npos, out.find(": service "));

    std::string bad_id_path = dir.writeFile("badid.jsonc", "{\"services\":{\"010\":{\"type\":\"unicast\"}}}");
    int rc_badid = run_cli({"porter-config-check", bad_id_path}, &out, &err);
    EXPECT_EQ(2, rc_badid);
    EXPECT_NE(std::string::npos, out.find(": error: service-id: /services/010: "));
    EXPECT_EQ(std::string::npos, out.find(": service 010:"));
    EXPECT_NE(std::string::npos, out.find(": 1 error(s)\n"));
}

// --role、--check-files (通常ファイル/不在/ディレクトリ/fopen失敗、未指定時非統計)、単一解析差し替え不変の確認
TEST(porterConfigCheckTest, checksRolesAuthFilesAndSingleParseGuarantee)
{
    // Arrange
    TempDirGuard dir("files");
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    std::string out;
    std::string err;

    // 1) --role フィルタリング
    std::string role_path =
        dir.writeFile("role.jsonc", "{\"services\":{\"1\":{\"type\":\"unicast_bidir\",\"dst_port\":5001}}}");
    int rc_recv = run_cli({"porter-config-check", "--role", "receiver", role_path}, &out, &err);
    EXPECT_EQ(0, rc_recv); // [確認_正常系] - --role receiver では送信側専用 constraint が出ず 0 になること。
    EXPECT_NE(std::string::npos, out.find(": service 1: OK\n"));

    int rc_send = run_cli({"porter-config-check", "--role", "sender", role_path}, &out, &err);
    EXPECT_EQ(2, rc_send); // [確認_異常系] - --role sender では 2 になること。

    // 2) --check-files 未指定時は cplat_stat を呼ばない
    std::string cert_ok = dir.writeFile("certs/c.pem", "dummy");
    std::string key_ok = dir.writeFile("certs/k.pem", "dummy");
    std::string missing_ca = dir.subDir("certs") + "/missing_ca.pem";
    std::string subdir_crl = dir.subDir("certs/dir_as_crl");
    std::string pin_unreadable = dir.writeFile("certs/pin_locked.pem", "dummy");
    std::string cf_path = dir.writeFile(
        "cf.jsonc",
        "{\n"
        "  \"services\": {\n"
        "    \"500\": {\n"
        "      \"type\": \"tcp\",\n"
        "      \"dst_addr1\": \"127.0.0.1\",\n"
        "      \"dst_port\": 5001,\n"
        "      \"auth\": \"cert\",\n"
        "      \"cert_file\": \"" +
            replace_backslashes(cert_ok) +
            "\",\n"
            "      \"key_file\": \"" +
            replace_backslashes(key_ok) +
            "\",\n"
            "      \"ca_file\": \"" +
            replace_backslashes(missing_ca) +
            "\",\n"
            "      \"crl_file\": \"" +
            replace_backslashes(subdir_crl) +
            "\",\n"
            "      \"pinned_certs\": [\"" +
            replace_backslashes(pin_unreadable) +
            "\"],\n"
            "      \"peer_names\": [\"peer\"]\n"
            "    }\n"
            "  }\n"
            "}\n");

    EXPECT_CALL(mock_cplat, cplat_stat(_, _, _)).Times(0);
    int rc_no_cf = run_cli({"porter-config-check", cf_path}, &out, &err);
    EXPECT_EQ(0, rc_no_cf); // [確認_正常系] - --check-files 未指定時はファイル確認を行わず 0 になること。

    // 3) --check-files 指定時 + 単一解析中のファイル差し替えで元のサービスのみ出力
    Mock::VerifyAndClearExpectations(&mock_cplat);
    EXPECT_CALL(mock_cplat, cplat_fopen(_, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq(pin_unreadable), StrEq("rb"), nullptr)).WillOnce(Return(nullptr));
    // 単一解析検証のため、cert_file の stat 時にファイル内容を別サービスへ差し替える
    bool swapped = false;
    ON_CALL(mock_cplat, cplat_stat(_, _, _))
        .WillByDefault(Invoke([&](cplat_file_stat_t *st, cplat_error *detail, const char *p) -> int {
            if (!swapped && p != nullptr && std::string(p) == cert_ok)
            {
                swapped = true;
                dir.writeFile("cf.jsonc",
                              "{\"services\":{\"999\":{\"type\":\"unicast\",\"src_addr1\":\"127.0.0.1\",\"dst_addr1\":"
                              "\"127.0.0.1\",\"dst_port\":5001}}}");
            }
            return delegate_real_cplat_stat(st, detail, p);
        }));

    int rc_cf = run_cli({"porter-config-check", "--check-files", cf_path}, &out, &err);
    EXPECT_EQ(2, rc_cf); // [確認_異常系] - 不在・ディレクトリ・fopen失敗を検出し終了コード 2 になること。
    EXPECT_NE(std::string::npos, out.find(": error: file: /services/500/ca_file: "));
    EXPECT_NE(std::string::npos, out.find(": error: file: /services/500/crl_file: "));
    EXPECT_NE(std::string::npos, out.find(": error: file: /services/500/pinned_certs/0: "));
    EXPECT_NE(std::string::npos, out.find(": service 500: 3 error(s)\n"));
    EXPECT_EQ(std::string::npos, out.find(": service 999:"));
    // [確認_異常系] - 差し替え後の 999 が出力されず、1 回の解析結果のみで集計されること。
}

// 記録領域 OOM (誤り報告後の含む) と stdout fflush/ferror 失敗が終了コード 2 より優先して 1 を返すことの確認
TEST(porterConfigCheckTest, prioritizesExecutionFailuresOverConfigErrorExitCode)
{
    // Arrange
    TempDirGuard dir("prio");
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    std::string out;
    std::string err;

    std::string bad_path =
        dir.writeFile("bad.jsonc", "{\"global\":{\"window_size\":1},\"services\":{\"1\":{\"type\":\"unicast\"}}}");

    // 1) stdout の fflush 失敗 -> 設定誤りがあっても 1 が優先 (stderr の fflush は通常許可)
    EXPECT_CALL(mock_stdio, fflush(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_stdio, fflush(_, _, _, stdout)).WillOnce(Return(EOF));
    int rc_flush = run_cli({"porter-config-check", bad_path}, &out, &err);
    EXPECT_EQ(1, rc_flush); // [確認_異常系] - fflush(stdout) 失敗で終了コード 1 が優先されること。
    EXPECT_NE(std::string::npos, err.find("標準出力へ書き込めませんでした"));

    // 2) on_service 内のサービス記録配列確保 (16 * sizeof(config_service_entry)) を失敗させると
    // すでに標準出力へ出た global 誤りは残し、サービス行と要約を出さずに 1 を返す
    Mock::VerifyAndClearExpectations(&mock_stdio);
    EXPECT_CALL(mock_cplat, cplat_malloc(_)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_malloc(16U * 16U)).WillOnce(Return(nullptr));
    int rc_oom = run_cli({"porter-config-check", bad_path}, &out, &err);
    EXPECT_EQ(1, rc_oom); // [確認_異常系] - サービス配列確保失敗で終了コード 1 になること。
    EXPECT_NE(std::string::npos, out.find(": error: range: /global/window_size: "));
    EXPECT_EQ(std::string::npos, out.find(": service "));
    EXPECT_EQ(std::string::npos, out.find(": 1 error(s)\n"));
    EXPECT_NE(std::string::npos, err.find("POTR_ERR_OUT_OF_MEMORY"));
}
