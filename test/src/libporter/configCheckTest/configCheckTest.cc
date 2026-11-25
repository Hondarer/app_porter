/**
 *******************************************************************************
 *  @file           configCheckTest.cc
 *  @brief          設定構造体の検証 (段階 4) と potr_internal_config_effective_ttl() の単体テストです。
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
#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include <porter/protocol/config_check.h>
#include <porter/protocol/config_schema.h>

#include <cstring>
#include <string>
#include <vector>

using namespace testing;

namespace
{
struct check_issue
{
    potr_config_issue_kind kind;
    int has_service_id;
    int64_t service_id;
    std::string key_path;
    std::string message;
};

void collect_issue(const potr_config_issue *issue, void *user_data)
{
    auto *out = static_cast<std::vector<check_issue> *>(user_data);
    check_issue item = {};
    item.kind = issue->kind;
    item.has_service_id = issue->has_service_id;
    item.service_id = issue->service_id;
    item.key_path = issue->key_path != nullptr ? issue->key_path : "";
    item.message = issue->message != nullptr ? issue->message : "";
    out->push_back(item);
}

void init_valid_unicast(potr_global_config *g, potr_service_def *s)
{
    potr_internal_config_global_init_defaults(g);
    potr_internal_config_service_init_defaults(s, 1001);
    s->type = POTR_TYPE_UNICAST;
    s->dst_port = 5001U;
    std::memcpy(s->src_addr[0], "127.0.0.1", sizeof("127.0.0.1"));
    std::memcpy(s->dst_addr[0], "127.0.0.1", sizeof("127.0.0.1"));
}

bool has_issue_path(const std::vector<check_issue> &issues, potr_config_issue_kind kind, const std::string &path)
{
    for (const auto &it : issues)
    {
        if (it.kind == kind && it.key_path == path)
        {
            return true;
        }
    }
    return false;
}
} // namespace

// potr_internal_config_effective_ttl() が 0 を 1 に、1..255 をそのまま返すことの確認
TEST(configCheckTest, effectiveTtlConvertsZeroToDefaultOne)
{
    // Arrange & Act & Assert
    EXPECT_EQ(1U, potr_internal_config_effective_ttl(0U));   // [確認_正常系] - 0 が 1 になること。
    EXPECT_EQ(1U, potr_internal_config_effective_ttl(1U));   // [確認_正常系] - 1 が 1 になること。
    EXPECT_EQ(64U, potr_internal_config_effective_ttl(64U)); // [確認_正常系] - 64 がそのまま返ること。
    EXPECT_EQ(255U, potr_internal_config_effective_ttl(255U)); // [確認_正常系] - 255 がそのまま返ること。
}

// 構造体の固定長文字配列に NUL 終端がない場合に安全に値の誤りを報告することの確認
TEST(configCheckTest, detectsMissingNulTerminatorInCharArrays)
{
    // Arrange
    potr_global_config g;
    potr_service_def s;
    init_valid_unicast(&g, &s);
    std::memset(s.src_addr[0], 'a', POTR_MAX_ADDR_LEN);
    std::memset(s.dst_addr[0], 'b', POTR_MAX_ADDR_LEN);
    std::memset(s.multicast_group, 'c', POTR_MAX_ADDR_LEN);
    std::memset(s.broadcast_addr, 'd', POTR_MAX_ADDR_LEN);
    std::vector<check_issue> issues;
    potr_internal_config_reporter reporter = {};
    potr_internal_config_reporter_init(&reporter, "struct", collect_issue, &issues, 0);

    // Pre-Assert

    // Act
    int ret = potr_internal_config_check_service(&g, &s, POTR_CONFIG_ROLE_ALL, 0, 0, nullptr, "", 1, 1001, &reporter);
    // [手順] - NUL 終端を欠く全固定長配列を持つサービス構造体を検証する。

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret); // [確認_異常系] - 戻り値が POTR_ERR_INVALID_CONFIG であること。
    EXPECT_TRUE(has_issue_path(issues, POTR_CONFIG_ISSUE_INVALID_VALUE, "/src_addr1"));
    // [確認_異常系] - src_addr1 の非 NUL 終端が報告されること。
    EXPECT_TRUE(has_issue_path(issues, POTR_CONFIG_ISSUE_INVALID_VALUE, "/dst_addr1"));
    // [確認_異常系] - dst_addr1 の非 NUL 終端が報告されること。
    EXPECT_TRUE(has_issue_path(issues, POTR_CONFIG_ISSUE_INVALID_VALUE, "/multicast_group"));
    // [確認_異常系] - multicast_group の非 NUL 終端が報告されること。
    EXPECT_TRUE(has_issue_path(issues, POTR_CONFIG_ISSUE_INVALID_VALUE, "/broadcast_addr"));
    // [確認_異常系] - broadcast_addr の非 NUL 終端が報告されること。
}

// global・フロー制御の値域・制約・役割依存条件と、複数誤りの一括報告の確認 (potrFlowControlResolveTest からの移行項目を含む)
TEST(configCheckTest, validatesAllFlowControlRangesConstraintsAndReportsAllErrors)
{
    // Arrange
    potr_global_config g;
    potr_service_def s;
    init_valid_unicast(&g, &s);
    g.window_size = 1U;
    g.max_payload = 1000U;
    g.max_message_size = 500U;
    g.flow_control_capacity = POTR_FLOW_CONTROL_DISABLED;
    g.flow_control_monitored_receiver_count = POTR_FLOW_CONTROL_DISABLED;
    g.flow_control_feedback_percent = 101U;
    g.flow_control_grace_ms = POTR_WAIT_MS_MAX + 1U;

    s.type = POTR_TYPE_MULTICAST;
    std::memcpy(s.multicast_group, "239.0.0.1", sizeof("239.0.0.1"));
    s.flow_control_suppress_percent = 50U;
    s.flow_control_resume_percent = 50U;
    s.flow_control_request_interval_ms = 500U;
    s.flow_control_request_lifetime_ms = 500U;
    s.flow_control_excluded_receiver_percent = 101U;
    s.flow_control_capacity = 20474U;
    s.health_interval_ms = 0U;
    g.udp_health_interval_ms = 0U;
    std::vector<check_issue> issues;
    potr_internal_config_reporter reporter = {};
    potr_internal_config_reporter_init(&reporter, "multi", collect_issue, &issues, 0);

    // Pre-Assert

    // Act
    int ret = potr_internal_config_check_all(&g, &s, POTR_ROLE_RECEIVER, &reporter);
    // [手順] - global と service の複数の誤り (値域・DISABLED 不可・しきい値・周期/期限・余裕・1:N 受信間隔 0) を一括検証する。

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret); // [確認_異常系] - 戻り値が POTR_ERR_INVALID_CONFIG であること。
    EXPECT_TRUE(has_issue_path(issues, POTR_CONFIG_ISSUE_OUT_OF_RANGE, "/global/window_size"));
    EXPECT_TRUE(has_issue_path(issues, POTR_CONFIG_ISSUE_CONSTRAINT, "/global/max_message_size"));
    EXPECT_TRUE(has_issue_path(issues, POTR_CONFIG_ISSUE_OUT_OF_RANGE, "/global/flow_control_capacity"));
    EXPECT_TRUE(has_issue_path(issues, POTR_CONFIG_ISSUE_OUT_OF_RANGE, "/global/flow_control_monitored_receiver_count"));
    EXPECT_TRUE(has_issue_path(issues, POTR_CONFIG_ISSUE_OUT_OF_RANGE, "/global/flow_control_feedback_percent"));
    EXPECT_TRUE(has_issue_path(issues, POTR_CONFIG_ISSUE_OUT_OF_RANGE, "/global/flow_control_grace_ms"));
    EXPECT_TRUE(has_issue_path(issues, POTR_CONFIG_ISSUE_OUT_OF_RANGE, "/flow_control_excluded_receiver_percent"));
    EXPECT_TRUE(has_issue_path(issues, POTR_CONFIG_ISSUE_CONSTRAINT, "/flow_control_resume_percent"));
    EXPECT_TRUE(has_issue_path(issues, POTR_CONFIG_ISSUE_CONSTRAINT, "/flow_control_request_lifetime_ms"));
    EXPECT_TRUE(has_issue_path(issues, POTR_CONFIG_ISSUE_CONSTRAINT, "/health_interval_ms"));
    // [確認_異常系] - 打ち切らずにすべての独立した値域・制約・役割依存誤りが一括報告されること。
}

// 待機時間の 5 項目の値域の上限が POTR_WAIT_MS_MAX であることの確認
TEST(configCheckTest, limitsWaitTimeItemsToWaitMsMax)
{
    // Arrange
    struct wait_item
    {
        const char *path;
        void (*set)(potr_global_config *, potr_service_def *, uint32_t);
    };
    const wait_item items[] = {
        {"/global/udp_health_timeout_ms",
         [](potr_global_config *g, potr_service_def *, uint32_t v) { g->udp_health_timeout_ms = v; }},
        {"/global/reorder_timeout_ms",
         [](potr_global_config *g, potr_service_def *, uint32_t v) { g->reorder_timeout_ms = v; }},
        {"/pack_wait_ms", [](potr_global_config *, potr_service_def *s, uint32_t v) { s->pack_wait_ms = v; }},
        {"/health_timeout_ms", [](potr_global_config *, potr_service_def *s, uint32_t v) { s->health_timeout_ms = v; }},
        {"/connect_timeout_ms",
         [](potr_global_config *, potr_service_def *s, uint32_t v) { s->connect_timeout_ms = v; }},
    };

    for (const auto &item : items)
    {
        for (int over = 0; over < 2; ++over)
        {
            potr_global_config g;
            potr_service_def s;
            init_valid_unicast(&g, &s);
            s.type = POTR_TYPE_TCP;
            item.set(&g, &s, over == 0 ? POTR_WAIT_MS_MAX : POTR_WAIT_MS_MAX + 1U);
            std::vector<check_issue> issues;
            potr_internal_config_reporter reporter = {};
            potr_internal_config_reporter_init(&reporter, "wait", collect_issue, &issues, 0);

            // Pre-Assert

            // Act
            potr_internal_config_check_all(&g, &s, POTR_ROLE_SENDER, &reporter);
            // [手順] - 待機時間の項目へ POTR_WAIT_MS_MAX または POTR_WAIT_MS_MAX + 1 を設定して検証する。

            // Assert
            EXPECT_EQ(over != 0, has_issue_path(issues, POTR_CONFIG_ISSUE_OUT_OF_RANGE, item.path))
                << item.path << " over=" << over;
            // [確認_正常系] - POTR_WAIT_MS_MAX が受理されること。
            // [確認_異常系] - POTR_WAIT_MS_MAX + 1 が値域の誤りになること。
        }
    }
}

// 段階 3 で誤りが記録された項目を参照する段階 4 の制約 (余裕・しきい値など) が省略されることの確認
TEST(configCheckTest, skipsDerivedConstraintsWhenPrerequisiteFieldHadStage3Error)
{
    // Arrange
    potr_global_config g;
    potr_service_def s;
    init_valid_unicast(&g, &s);
    g.max_payload = 1000U;
    g.max_message_size = 500U;
    s.flow_control_capacity = 100U;
    s.flow_control_suppress_percent = 80U;
    s.flow_control_resume_percent = 90U;
    potr_internal_config_field_errors errs = {};
    errs.global_mask |= (1U << POTR_CONFIG_GFIELD_MAX_MESSAGE_SIZE);
    errs.service_mask |= (1ULL << POTR_CONFIG_SFIELD_FC_SUPPRESS_PERCENT);
    std::vector<check_issue> issues;
    potr_internal_config_reporter reporter = {};
    potr_internal_config_reporter_init(&reporter, "json", collect_issue, &issues, 0);

    // Pre-Assert

    // Act
    int ret_g = potr_internal_config_check_global(&g, &errs, "/global", &reporter);
    int ret_s = potr_internal_config_check_service(&g, &s, POTR_CONFIG_ROLE_ALL, 1, 0, &errs, "/services/1001", 1,
                                                   1001, &reporter);
    // [手順] - 段階 3 誤りビットを設定した状態で段階 4 検証を実行する。

    // Assert
    EXPECT_EQ(POTR_OK, ret_g); // [確認_正常系] - 前提崩れの max_message_size >= max_payload 制約を重複報告しないこと。
    EXPECT_EQ(POTR_OK, ret_s); // [確認_正常系] - suppress_percent 誤り時に resume < suppress と余裕制約を重複報告しないこと。
    EXPECT_TRUE(issues.empty()); // [確認_正常系] - 追加誤りが 0 件であること。
}

// 全 8 経路の番号連続性・静的/動的 unicast_bidir・証明書片方向送信 src_port・RAW path_group >= 2 の検査確認
TEST(configCheckTest, validatesAll8PathsRolesCertOneWaySrcPortAndRawPathGroup)
{
    // Arrange
    potr_global_config g;
    potr_service_def s;

    // 1) 経路 3 のみに src_addr/dst_addr を入れた場合の番号飛び検査 (全 8 経路走査)
    init_valid_unicast(&g, &s);
    std::memcpy(s.src_addr[2], "127.0.0.2", sizeof("127.0.0.2"));
    std::memcpy(s.dst_addr[2], "127.0.0.2", sizeof("127.0.0.2"));
    std::vector<check_issue> gap_issues;
    potr_internal_config_reporter r_gap = {};
    potr_internal_config_reporter_init(&r_gap, "gap", collect_issue, &gap_issues, 0);
    int ret_gap = potr_internal_config_check_service(&g, &s, POTR_CONFIG_ROLE_ALL, 0, 0, nullptr, "", 1, 1, &r_gap);

    // 2) unicast_bidir: 送信側 dst_addr1 必須、動的受信側で path2 以降拒否
    init_valid_unicast(&g, &s);
    s.type = POTR_TYPE_UNICAST_BIDIR;
    s.src_addr[0][0] = '\0';
    s.dst_addr[0][0] = '\0';
    std::vector<check_issue> bidir_send_issues;
    potr_internal_config_reporter r_bs = {};
    potr_internal_config_reporter_init(&r_bs, "bs", collect_issue, &bidir_send_issues, 0);
    int ret_bs_sender =
        potr_internal_config_check_service(&g, &s, POTR_CONFIG_ROLE_SENDER, 0, 0, nullptr, "", 1, 2, &r_bs);
    std::vector<check_issue> bidir_recv_issues;
    potr_internal_config_reporter r_br = {};
    potr_internal_config_reporter_init(&r_br, "br", collect_issue, &bidir_recv_issues, 0);
    std::memcpy(s.dst_addr[1], "127.0.0.2", sizeof("127.0.0.2"));
    int ret_br_dyn_p2 =
        potr_internal_config_check_service(&g, &s, POTR_CONFIG_ROLE_RECEIVER, 0, 0, nullptr, "", 1, 2, &r_br);

    // 3) 証明書方式の片方向 UDP 送信側は src_port > 0 必須
    init_valid_unicast(&g, &s);
    s.auth = POTR_AUTH_CERT;
    s.cert_file = "c.pem";
    s.key_file = "k.pem";
    s.pinned_certs = reinterpret_cast<const char *const *>(&s.cert_file);
    s.pinned_cert_count = 1U;
    s.src_port = 0U;
    std::vector<check_issue> cert_issues;
    potr_internal_config_reporter r_cert = {};
    potr_internal_config_reporter_init(&r_cert, "cert", collect_issue, &cert_issues, 0);
    int ret_cert_sender =
        potr_internal_config_check_service(&g, &s, POTR_CONFIG_ROLE_SENDER, 0, 0, nullptr, "", 1, 3, &r_cert);
    int ret_cert_recv =
        potr_internal_config_check_service(&g, &s, POTR_CONFIG_ROLE_RECEIVER, 0, 0, nullptr, "", 1, 3, nullptr);

    // 4) RAW path_group >= 2 -> NOT_APPLICABLE
    init_valid_unicast(&g, &s);
    s.type = POTR_TYPE_UNICAST_RAW;
    s.path_group[0] = 2U;
    std::vector<check_issue> raw_issues;
    potr_internal_config_reporter r_raw = {};
    potr_internal_config_reporter_init(&r_raw, "raw", collect_issue, &raw_issues, 0);
    int ret_raw = potr_internal_config_check_service(&g, &s, POTR_CONFIG_ROLE_SENDER, 0, 0, nullptr, "", 1, 4, &r_raw);

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_gap);
    EXPECT_TRUE(has_issue_path(gap_issues, POTR_CONFIG_ISSUE_INVALID_VALUE, "/src_addr3"));
    EXPECT_TRUE(has_issue_path(gap_issues, POTR_CONFIG_ISSUE_INVALID_VALUE, "/dst_addr3"));
    // [確認_異常系] - 全 8 経路の番号飛びが src_addr3 と dst_addr3 の両方で検出されること。
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_bs_sender);
    EXPECT_TRUE(has_issue_path(bidir_send_issues, POTR_CONFIG_ISSUE_CONSTRAINT, "/dst_addr1"));
    // [確認_異常系] - unicast_bidir 送信側の dst_addr1 欠落が CONSTRAINT として検出されること。
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_br_dyn_p2);
    EXPECT_TRUE(has_issue_path(bidir_recv_issues, POTR_CONFIG_ISSUE_CONSTRAINT, "/dst_addr2"));
    // [確認_異常系] - 動的 unicast_bidir 受信側の経路 2 以降が CONSTRAINT として拒否されること。
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_cert_sender);
    EXPECT_TRUE(has_issue_path(cert_issues, POTR_CONFIG_ISSUE_CONSTRAINT, "/src_port"));
    // [確認_異常系] - 証明書方式の片方向送信側で src_port == 0 が拒否されること。
    EXPECT_EQ(POTR_OK, ret_cert_recv);
    // [確認_正常系] - 同じ設定でも受信側のみの役割検査なら src_port == 0 が許可されること。
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_raw);
    EXPECT_TRUE(has_issue_path(raw_issues, POTR_CONFIG_ISSUE_NOT_APPLICABLE, "/path_group1"));
    // [確認_異常系] - RAW 構造体の path_group >= 2 が NOT_APPLICABLE として拒否されること。
}

// NULL 引数および不正役割に対して POTR_ERR_INVALID_ARGUMENT を返すことの確認
TEST(configCheckTest, returnsInvalidArgumentForNullOrBadRole)
{
    // Arrange
    potr_global_config g;
    potr_service_def s;
    init_valid_unicast(&g, &s);

    // Pre-Assert

    // Act & Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, potr_internal_config_check_global(nullptr, nullptr, "/global", nullptr));
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT,
              potr_internal_config_check_service(nullptr, &s, POTR_CONFIG_ROLE_ALL, 0, 0, nullptr, "", 0, 0, nullptr));
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT,
              potr_internal_config_check_service(&g, nullptr, POTR_CONFIG_ROLE_ALL, 0, 0, nullptr, "", 0, 0, nullptr));
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, potr_internal_config_check_all(nullptr, &s, POTR_ROLE_SENDER, nullptr));
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, potr_internal_config_check_all(&g, nullptr, POTR_ROLE_SENDER, nullptr));
    int invalid_role = 99;
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT,
              potr_internal_config_check_all(&g, &s, static_cast<potr_role>(invalid_role), nullptr));
    // [確認_異常系] - NULL 引数および未定義の役割で POTR_ERR_INVALID_ARGUMENT となること。
}
