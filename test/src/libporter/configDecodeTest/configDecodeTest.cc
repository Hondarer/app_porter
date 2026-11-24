/**
 *******************************************************************************
 *  @file           configDecodeTest.cc
 *  @brief          設定 JSON の段階 2 (ルート・ID・重複キー) と段階 3 (全キー項目・種別/認証対応) の単体テストです。
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

#include <cJSON_JSONC.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <porter/porter_result.h>
#include <porter/protocol/config_decode.h>
#include <porter/protocol/config_schema.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace testing;

namespace
{
struct decode_issue
{
    potr_config_issue_kind kind;
    int has_service_id;
    int64_t service_id;
    std::string key_path;
    std::string message;
    std::string expected;
};

struct decode_capture
{
    std::vector<decode_issue> issues;
    std::vector<int64_t> services;
    std::vector<std::string> auth_key_paths;
    std::vector<std::string> auth_paths;
};

void on_decode_issue(const potr_config_issue *issue, void *user_data)
{
    auto *cap = static_cast<decode_capture *>(user_data);
    decode_issue item = {};
    item.kind = issue->kind;
    item.has_service_id = issue->has_service_id;
    item.service_id = issue->service_id;
    item.key_path = issue->key_path != nullptr ? issue->key_path : "";
    item.message = issue->message != nullptr ? issue->message : "";
    item.expected = issue->expected != nullptr ? issue->expected : "";
    cap->issues.push_back(item);
}

void on_decode_service(int64_t service_id, void *user_data)
{
    auto *cap = static_cast<decode_capture *>(user_data);
    cap->services.push_back(service_id);
}

void on_decode_auth(const potr_config_file_ref *ref, void *user_data)
{
    auto *cap = static_cast<decode_capture *>(user_data);
    cap->auth_key_paths.emplace_back(ref->key_path != nullptr ? ref->key_path : "");
    cap->auth_paths.emplace_back(ref->path != nullptr ? ref->path : "");
}

cJSON *parse_jsonc(const std::string &text)
{
    cjson_app_jsonc_status status = CJSON_APP_JSONC_OK;
    size_t err_off = 0U;
    cJSON *root = cjson_app_cJSON_ParseJSONCWithLengthOpts(text.data(), text.size(), &status, &err_off);
    EXPECT_EQ(CJSON_APP_JSONC_OK, status);
    return root;
}

bool has_issue(const decode_capture &cap, potr_config_issue_kind kind, const std::string &path)
{
    for (const auto &it : cap.issues)
    {
        if (it.kind == kind && it.key_path == path)
        {
            return true;
        }
    }
    return false;
}

const decode_issue *find_issue(const decode_capture &cap, potr_config_issue_kind kind, const std::string &path)
{
    for (const auto &it : cap.issues)
    {
        if (it.kind == kind && it.key_path == path)
        {
            return &it;
        }
    }
    return nullptr;
}
} // namespace

// ルート非オブジェクト、未知/重複キー、global/services 型誤り、不正 ID、重複 ID 時のサービス本文スキップ確認
TEST(configDecodeTest, validatesStage2RootStructureCanonicalIdsAndDuplicates)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;

    // 1) ルート非オブジェクト
    cJSON *arr_root = parse_jsonc("[]");
    ASSERT_NE(nullptr, arr_root);
    decode_capture cap1;
    potr_internal_config_reporter r1 = {};
    potr_internal_config_reporter_init(&r1, "root.jsonc", on_decode_issue, &cap1, 0);
    potr_internal_config_id_list ids1 = {};
    int ret1 = potr_internal_config_decode_root(arr_root, &r1, &ids1);
    cjson_app_cJSON_Delete(arr_root);

    // 2) ルートの未知キー、重複キー、不正 ID、非オブジェクト サービス、重複 ID
    const std::string bad_root_json =
        "{"
        "\"unknown_root\":1,"
        "\"global\":{},"
        "\"global\":{},"
        "\"services\":{"
        "  \"010\":{\"type\":\"unicast\"},"
        "  \"+1\":{\"type\":\"unicast\"},"
        "  \" 1\":{\"type\":\"unicast\"},"
        "  \"-0\":{\"type\":\"unicast\"},"
        "  \"9223372036854775808\":{\"type\":\"unicast\"},"
        "  \"1001\":123,"
        "  \"2002\":{\"bad_dup\":1,\"bad_dup\":2},"
        "  \"2002\":{\"unknown_inner\":1}"
        "}"
        "}";
    cJSON *bad_root = parse_jsonc(bad_root_json);
    ASSERT_NE(nullptr, bad_root);
    decode_capture cap2;
    potr_internal_config_reporter r2 = {};
    potr_internal_config_reporter_init(&r2, "badroot.jsonc", on_decode_issue, &cap2, 0);
    int ret2 = potr_internal_config_decode_validate_all(bad_root, POTR_CONFIG_ROLE_ALL, on_decode_service,
                                                        on_decode_auth, &cap2, &r2);
    cjson_app_cJSON_Delete(bad_root);

    // 3) 正規の境界 ID (-9223372036854775808, 0, 9223372036854775807)
    const std::string min_max_json =
        "{\"services\":{"
        "\"-9223372036854775808\":{\"type\":\"unicast_bidir_n1\",\"dst_port\":5001},"
        "\"0\":{\"type\":\"unicast_bidir_n1\",\"dst_port\":5002},"
        "\"9223372036854775807\":{\"type\":\"unicast_bidir_n1\",\"dst_port\":5003}"
        "}}";
    cJSON *ok_root = parse_jsonc(min_max_json);
    ASSERT_NE(nullptr, ok_root);
    decode_capture cap3;
    potr_internal_config_reporter r3 = {};
    potr_internal_config_reporter_init(&r3, "ok.jsonc", on_decode_issue, &cap3, 0);
    potr_internal_config_id_list ids3 = {};
    int ret3_root = potr_internal_config_decode_root(ok_root, &r3, &ids3);
    int ret3_all = potr_internal_config_decode_validate_all(ok_root, POTR_CONFIG_ROLE_ALL, on_decode_service,
                                                            on_decode_auth, &cap3, &r3);
    cjson_app_cJSON_Delete(ok_root);

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret1); // [確認_異常系] - ルート配列が拒否されること。
    EXPECT_TRUE(has_issue(cap1, POTR_CONFIG_ISSUE_TYPE_MISMATCH, ""));

    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret2);
    EXPECT_TRUE(cap2.services.empty()); // [確認_異常系] - ルート誤り時に on_service が呼ばれないこと。
    EXPECT_TRUE(has_issue(cap2, POTR_CONFIG_ISSUE_UNKNOWN_KEY, "/unknown_root"));
    EXPECT_TRUE(has_issue(cap2, POTR_CONFIG_ISSUE_DUPLICATE_KEY, "/global"));
    EXPECT_TRUE(has_issue(cap2, POTR_CONFIG_ISSUE_INVALID_SERVICE_ID, "/services/010"));
    EXPECT_TRUE(has_issue(cap2, POTR_CONFIG_ISSUE_INVALID_SERVICE_ID, "/services/+1"));
    EXPECT_TRUE(has_issue(cap2, POTR_CONFIG_ISSUE_INVALID_SERVICE_ID, "/services/ 1"));
    EXPECT_TRUE(has_issue(cap2, POTR_CONFIG_ISSUE_INVALID_SERVICE_ID, "/services/-0"));
    EXPECT_TRUE(has_issue(cap2, POTR_CONFIG_ISSUE_INVALID_SERVICE_ID, "/services/9223372036854775808"));
    const decode_issue *non_obj = find_issue(cap2, POTR_CONFIG_ISSUE_TYPE_MISMATCH, "/services/1001");
    ASSERT_NE(nullptr, non_obj);
    EXPECT_EQ(1, non_obj->has_service_id);
    EXPECT_EQ(1001, non_obj->service_id);
    // [確認_異常系] - 正規 ID を持つ非オブジェクト サービスが has_service_id=1 で報告されること。
    const decode_issue *dup_id = find_issue(cap2, POTR_CONFIG_ISSUE_DUPLICATE_KEY, "/services/2002");
    ASSERT_NE(nullptr, dup_id);
    EXPECT_EQ(1, dup_id->has_service_id);
    EXPECT_EQ(2002, dup_id->service_id);
    EXPECT_FALSE(has_issue(cap2, POTR_CONFIG_ISSUE_UNKNOWN_KEY, "/services/2002/unknown_inner"));
    // [確認_異常系] - 重複 ID 2002 の本文が検査されないこと。

    EXPECT_EQ(POTR_OK, ret3_root);
    EXPECT_EQ(POTR_OK, ret3_all);
    ASSERT_EQ(3U, ids3.count);
    EXPECT_EQ(INT64_MIN, ids3.ids[0]);
    EXPECT_EQ(0, ids3.ids[1]);
    EXPECT_EQ(INT64_MAX, ids3.ids[2]);
    EXPECT_EQ(cap3.services.size(), 3U);
    // [確認_正常系] - 境界値 INT64_MIN, 0, INT64_MAX が正規 ID として受理されファイル順で返ること。
    cplat_free(ids3.ids);
}

// すべての global 22 キーに対して型不一致・不正文字列・値域外・"-1"・重複・未知キー・-0 数値・配列を検査することの確認
TEST(configDecodeTest, validatesAllGlobalKeysAndNumericStringEdgeCases)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    size_t gcount = 0U;
    const potr_internal_config_field_spec *gtab = potr_internal_config_schema_global_table(&gcount);
    ASSERT_NE(nullptr, gtab);
    ASSERT_EQ(22U, gcount);

    // 1) 全 22 global キーに対して、無効文字列・型不一致・4294967295 (許容/拒否) を順に確認
    for (size_t i = 0U; i < gcount; i++)
    {
        const potr_internal_config_field_spec *spec = &gtab[i];
        std::string key_path = std::string("/global/") + spec->name;
        std::string js = std::string("{\"global\":{\"") + spec->name + "\":";

        cJSON *root_bool = parse_jsonc(js + "true}}");
        decode_capture cap_bool;
        potr_internal_config_reporter r_bool = {};
        potr_internal_config_reporter_init(&r_bool, "g.jsonc", on_decode_issue, &cap_bool, 0);
        int ret_bool = potr_internal_config_decode_validate_all(root_bool, POTR_CONFIG_ROLE_ALL, nullptr, nullptr,
                                                                &cap_bool, &r_bool);
        cjson_app_cJSON_Delete(root_bool);
        EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_bool);
        EXPECT_TRUE(has_issue(cap_bool, POTR_CONFIG_ISSUE_TYPE_MISMATCH, key_path));
        // [確認_異常系 回数=22] - 真偽値で TYPE_MISMATCH が返ること。

        cJSON *root_str = parse_jsonc(js + "\"-0\"}}");
        decode_capture cap_str;
        potr_internal_config_reporter r_str = {};
        potr_internal_config_reporter_init(&r_str, "g.jsonc", on_decode_issue, &cap_str, 0);
        int ret_str = potr_internal_config_decode_validate_all(root_str, POTR_CONFIG_ROLE_ALL, nullptr, nullptr,
                                                               &cap_str, &r_str);
        cjson_app_cJSON_Delete(root_str);
        EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_str);
        EXPECT_TRUE(has_issue(cap_str, POTR_CONFIG_ISSUE_INVALID_VALUE, key_path));
        // [確認_異常系 回数=22] - 文字列 "-0" が INVALID_VALUE として拒否されること。

        cJSON *root_dis = parse_jsonc(js + "4294967295}}");
        decode_capture cap_dis;
        potr_internal_config_reporter r_dis = {};
        potr_internal_config_reporter_init(&r_dis, "g.jsonc", on_decode_issue, &cap_dis, 0);
        int ret_dis = potr_internal_config_decode_validate_all(root_dis, POTR_CONFIG_ROLE_ALL, nullptr, nullptr,
                                                               &cap_dis, &r_dis);
        cjson_app_cJSON_Delete(root_dis);
        EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_dis);
        EXPECT_TRUE(has_issue(cap_dis, POTR_CONFIG_ISSUE_OUT_OF_RANGE, key_path));
        // [確認_異常系 回数=22] - 専用値表現 4294967295 が global で OUT_OF_RANGE になること。
    }

    // 2) JSON 数値の -0 と整数値の指数表記は受理、-1 文字列は許可された 4 項目のみ受け入れ、小数/非整数指数は型誤り、旧キー・重複は誤り
    const std::string edge_json =
        "{"
        "\"global\":{"
        "  \"udp_health_interval_ms\": -0,"
        "  \"reorder_timeout_ms\": 1e2,"
        "  \"tcp_health_interval_ms\": 1.5e2,"
        "  \"flow_control_excluded_receiver_count\": \"-1\","
        "  \"flow_control_excluded_receiver_percent\": -1,"
        "  \"flow_control_receive_buffer_percent\": \"-1\","
        "  \"udp_receive_buffer_size\": -1,"
        "  \"window_size\": \"64\","
        "  \"max_payload\": \"+1400\""
        "}"
        "}";
    cJSON *ok_edge = parse_jsonc(edge_json);
    decode_capture cap_edge;
    potr_internal_config_reporter r_edge = {};
    potr_internal_config_reporter_init(&r_edge, "edge.jsonc", on_decode_issue, &cap_edge, 0);
    int ret_edge =
        potr_internal_config_decode_validate_all(ok_edge, POTR_CONFIG_ROLE_ALL, nullptr, nullptr, &cap_edge, &r_edge);
    cjson_app_cJSON_Delete(ok_edge);
    EXPECT_EQ(POTR_OK, ret_edge); // [確認_正常系] - 数値 -0、整数となる指数表記、許可された "-1"、数値文字列が受理されること。

    const std::string bad_edge_json =
        "{"
        "\"global\":{"
        "  \"window_size\": 1.5,"
        "  \"max_payload\": 1e-1,"
        "  \"send_queue_depth\": 1.5e-2,"
        "  \"udp_health_timeout_ms\": \"-x\","
        "  \"flow_control_capacity\": \"-1\","
        "  \"flow_control_monitored_receiver_count\": -1,"
        "  \"flow_control_legacy\": 10,"
        "  \"reorder_timeout_ms\": 10,"
        "  \"reorder_timeout_ms\": true"
        "}"
        "}";
    cJSON *bad_edge = parse_jsonc(bad_edge_json);
    decode_capture cap_bad;
    potr_internal_config_reporter r_bad = {};
    potr_internal_config_reporter_init(&r_bad, "badedge.jsonc", on_decode_issue, &cap_bad, 0);
    int ret_bad = potr_internal_config_decode_validate_all(bad_edge, POTR_CONFIG_ROLE_ALL, nullptr, nullptr, &cap_bad,
                                                           &r_bad);
    cjson_app_cJSON_Delete(bad_edge);
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret_bad);
    EXPECT_TRUE(has_issue(cap_bad, POTR_CONFIG_ISSUE_TYPE_MISMATCH, "/global/window_size"));
    EXPECT_TRUE(has_issue(cap_bad, POTR_CONFIG_ISSUE_TYPE_MISMATCH, "/global/max_payload"));
    EXPECT_TRUE(has_issue(cap_bad, POTR_CONFIG_ISSUE_TYPE_MISMATCH, "/global/send_queue_depth"));
    EXPECT_TRUE(has_issue(cap_bad, POTR_CONFIG_ISSUE_INVALID_VALUE, "/global/udp_health_timeout_ms"));
    EXPECT_TRUE(has_issue(cap_bad, POTR_CONFIG_ISSUE_OUT_OF_RANGE, "/global/flow_control_capacity"));
    EXPECT_TRUE(has_issue(cap_bad, POTR_CONFIG_ISSUE_OUT_OF_RANGE, "/global/flow_control_monitored_receiver_count"));
    EXPECT_TRUE(has_issue(cap_bad, POTR_CONFIG_ISSUE_UNKNOWN_KEY, "/global/flow_control_legacy"));
    EXPECT_TRUE(has_issue(cap_bad, POTR_CONFIG_ISSUE_DUPLICATE_KEY, "/global/reorder_timeout_ms"));
    // [確認_異常系] - 2 回目以降の重複キーは型検査せず DUPLICATE_KEY のみを報告すること。
    EXPECT_FALSE(has_issue(cap_bad, POTR_CONFIG_ISSUE_TYPE_MISMATCH, "/global/reorder_timeout_ms"));
}

// サービス 33 固定キーと経路キー、通信種別×項目適用表、認証方式、encrypt_key 秘匿、文字列長 63/64 境界の確認
TEST(configDecodeTest, validatesServiceKeysTypeMatrixAuthAndKeySecrecy)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    std::string addr63(63U, '1'); // 表示長さ境界用 (IP 表記としては 63 byte)
    std::string str63(63U, 'a');
    std::string str64(64U, 'b');

    // 1) tcp_bidir に不適合なキー・64 byte 超過・RAW の path_group・経路キー誤り・空文字・秘密鍵非漏洩
    const std::string secret_phrase = "super-secret-passphrase-never-leak";
    const std::string svc_err_json =
        "{"
        "\"services\":{"
        "\"1001\":{"
        "  \"type\":\"tcp_bidir\","
        "  \"dst_addr1\":\"127.0.0.1\","
        "  \"dst_port\":5001,"
        "  \"ttl\":1,"
        "  \"broadcast_addr\":\"255.255.255.255\","
        "  \"max_peers\":10,"
        "  \"udp_receive_buffer_size\":4096,"
        "  \"flow_control_excluded_receiver_count\":1,"
        "  \"flow_control_feedback_percent\":5,"
        "  \"src_addr1\":\"" +
        str64 +
        "\","
        "  \"src_addr01\":\"127.0.0.1\","
        "  \"src_addr9\":\"127.0.0.1\","
        "  \"path_group2\":2,"
        "  \"encrypt_key\":\"" +
        secret_phrase +
        "\","
        "  \"cert_file\":\"c.pem\","
        "  \"unknown_svc\":1"
        "},"
        "\"1002\":{"
        "  \"type\":\"unicast_raw\","
        "  \"src_addr1\":\"127.0.0.1\","
        "  \"dst_addr1\":\"127.0.0.1\","
        "  \"dst_port\":5002,"
        "  \"path_group1\":1,"
        "  \"group_failback_ms\":100,"
        "  \"flow_control_capacity\":1024,"
        "  \"encrypt_key\":\"\""
        "},"
        "\"1003\":{"
        "  \"type\":\"unicast\","
        "  \"src_addr1\":\"127.0.0.1\","
        "  \"dst_addr1\":\"127.0.0.1\","
        "  \"dst_port\":5003,"
        "  \"auth\":\"cert\","
        "  \"encrypt_key\":\"abc\","
        "  \"crl_file\":\"crl.pem\","
        "  \"peer_names\":[\"peer1\"],"
        "  \"pinned_certs\":[\"pin.pem\"]"
        "}"
        "}"
        "}";
    cJSON *root = parse_jsonc(svc_err_json);
    ASSERT_NE(nullptr, root);

    EXPECT_CALL(mock_cplat, cplat_passphrase_to_key(_, _, _)).WillOnce(Return(CPLAT_ERR_UNKNOWN));
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, HasSubstr(secret_phrase))).Times(0);
    // [Pre-Assert確認_異常系] - トレース出力にパスフレーズの値が含まれないこと。

    decode_capture cap;
    potr_internal_config_reporter r = {};
    potr_internal_config_reporter_init(&r, "svc.jsonc", on_decode_issue, &cap, 1);
    int ret = potr_internal_config_decode_validate_all(root, POTR_CONFIG_ROLE_ALL, on_decode_service, on_decode_auth,
                                                       &cap, &r);
    cjson_app_cJSON_Delete(root);

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret);
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_NOT_APPLICABLE, "/services/1001/ttl"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_NOT_APPLICABLE, "/services/1001/broadcast_addr"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_NOT_APPLICABLE, "/services/1001/max_peers"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_NOT_APPLICABLE, "/services/1001/udp_receive_buffer_size"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_NOT_APPLICABLE, "/services/1001/flow_control_excluded_receiver_count"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_NOT_APPLICABLE, "/services/1001/flow_control_feedback_percent"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_NOT_APPLICABLE, "/services/1001/cert_file"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_OUT_OF_RANGE, "/services/1001/src_addr1"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_UNKNOWN_KEY, "/services/1001/src_addr01"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_OUT_OF_RANGE, "/services/1001/src_addr9"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_INVALID_VALUE, "/services/1001/path_group2"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_KEY_DERIVATION, "/services/1001/encrypt_key"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_UNKNOWN_KEY, "/services/1001/unknown_svc"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_NOT_APPLICABLE, "/services/1002/path_group1"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_NOT_APPLICABLE, "/services/1002/group_failback_ms"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_NOT_APPLICABLE, "/services/1002/flow_control_capacity"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_INVALID_VALUE, "/services/1002/encrypt_key"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_NOT_APPLICABLE, "/services/1003/encrypt_key"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_NOT_APPLICABLE, "/services/1003/crl_file"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_NOT_APPLICABLE, "/services/1003/peer_names"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_MISSING, "/services/1003/cert_file"));
    EXPECT_TRUE(has_issue(cap, POTR_CONFIG_ISSUE_MISSING, "/services/1003/key_file"));
    for (const auto &it : cap.issues)
    {
        EXPECT_EQ(std::string::npos, it.message.find(secret_phrase));
        EXPECT_EQ(std::string::npos, it.expected.find(secret_phrase));
        // [確認_異常系] - コールバックの文字列に encrypt_key の値が含まれないこと。
    }
    ASSERT_EQ(1U, cap.auth_paths.size());
    EXPECT_EQ("pin.pem", cap.auth_paths[0]);
    EXPECT_EQ("/services/1003/pinned_certs/0", cap.auth_key_paths[0]);
    // [確認_正常系] - 誤りのある項目は除外し、有効な pinned_certs/0 のみが on_auth_file へ渡ること。

    // 2) 全 33 service 固定キーに対する型誤り・63 byte 長さ受理の確認
    size_t scount = 0U;
    const potr_internal_config_field_spec *stab = potr_internal_config_schema_service_table(&scount);
    ASSERT_EQ(33U, scount);
    for (size_t i = 0U; i < scount; i++)
    {
        const potr_internal_config_field_spec *spec = &stab[i];
        if (spec->kind == POTR_CONFIG_VAL_U8 || spec->kind == POTR_CONFIG_VAL_U16 || spec->kind == POTR_CONFIG_VAL_U32)
        {
            std::string js = std::string("{\"services\":{\"1\":{\"") + spec->name + "\":true}}}";
            cJSON *r_type = parse_jsonc(js);
            decode_capture cap_t;
            potr_internal_config_reporter rep_t = {};
            potr_internal_config_reporter_init(&rep_t, "s.jsonc", on_decode_issue, &cap_t, 0);
            EXPECT_EQ(POTR_ERR_INVALID_CONFIG,
                      potr_internal_config_decode_validate_all(r_type, POTR_CONFIG_ROLE_ALL, nullptr, nullptr, &cap_t,
                                                               &rep_t));
            EXPECT_TRUE(has_issue(cap_t, POTR_CONFIG_ISSUE_TYPE_MISMATCH, std::string("/services/1/") + spec->name));
            cjson_app_cJSON_Delete(r_type);
        }
    }

    const std::string len63_json =
        "{\"services\":{\"1\":{\"type\":\"unicast\",\"src_addr1\":\"" + str63 + "\",\"dst_addr1\":\"" + str63 +
        "\",\"dst_port\":5001}}}";
    cJSON *r63 = parse_jsonc(len63_json);
    potr_global_config g63 = {};
    potr_internal_config_service s63 = {};
    decode_capture cap63;
    potr_internal_config_reporter rep63 = {};
    potr_internal_config_reporter_init(&rep63, "len63.jsonc", on_decode_issue, &cap63, 0);
    int ret63 = potr_internal_config_decode_target_service(r63, 1, POTR_CONFIG_ROLE_ALL, &g63, &s63, &rep63);
    potr_internal_config_service_dispose(&s63);
    cjson_app_cJSON_Delete(r63);
    EXPECT_EQ(POTR_OK, ret63); // [確認_正常系] - 63 byte のアドレス文字列は境界値として受理されること。
}
