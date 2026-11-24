/**
 *******************************************************************************
 *  @file           configSchemaTest.cc
 *  @brief          設定スキーマ表・経路キー解析・種別変換・既定値初期化の単体テストです。
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
#include <porter/protocol/config_schema.h>

#include <cstddef>

using namespace testing;

// global 既知キー表 (22 項目) と service 固定キー表 (33 項目) の検索網羅性の確認
TEST(configSchemaTest, findsAllGlobalAndServiceSchemaEntries)
{
    // Arrange
    size_t gcount = 0U;
    size_t scount = 0U;
    const potr_internal_config_field_spec *gtab = potr_internal_config_schema_global_table(&gcount);
    const potr_internal_config_field_spec *stab = potr_internal_config_schema_service_table(&scount);

    // Pre-Assert

    // Act
    const potr_internal_config_field_spec *g_unknown = potr_internal_config_schema_find_global("unknown_g");
    const potr_internal_config_field_spec *g_null = potr_internal_config_schema_find_global(nullptr);
    const potr_internal_config_field_spec *s_path = potr_internal_config_schema_find_service("src_addr1");
    const potr_internal_config_field_spec *s_null = potr_internal_config_schema_find_service(nullptr);
    // [手順] - 両スキーマ表の取得と、未知キー・経路キー・NULL 検索を行う。

    // Assert
    ASSERT_NE(nullptr, gtab);                             // [確認_正常系] - global 表が非 NULL であること。
    ASSERT_EQ((size_t)POTR_CONFIG_GFIELD_COUNT, gcount);  // [確認_正常系] - global が 22 項目であること。
    ASSERT_NE(nullptr, stab);                             // [確認_正常系] - service 表が非 NULL であること。
    ASSERT_EQ((size_t)POTR_CONFIG_SFIELD_COUNT, scount);  // [確認_正常系] - service が 33 項目であること。
    for (size_t i = 0U; i < gcount; i++)
    {
        EXPECT_EQ(i, gtab[i].id);
        EXPECT_EQ(&gtab[i], potr_internal_config_schema_find_global(gtab[i].name));
        // [確認_正常系 回数=22] - 全 global 項目が表の位置と一致し名前検索で見つかること。
    }
    for (size_t i = 0U; i < scount; i++)
    {
        EXPECT_EQ(i, stab[i].id);
        EXPECT_EQ(&stab[i], potr_internal_config_schema_find_service(stab[i].name));
        // [確認_正常系 回数=33] - 全 service 固定項目が表の位置と一致し名前検索で見つかること。
    }
    EXPECT_EQ(nullptr, g_unknown); // [確認_異常系] - global の未知キーは NULL であること。
    EXPECT_EQ(nullptr, g_null);    // [確認_異常系] - global の NULL 検索は NULL であること。
    EXPECT_EQ(nullptr, s_path);    // [確認_異常系] - 経路キーは固定キー検索で NULL であること。
    EXPECT_EQ(nullptr, s_null);    // [確認_異常系] - service の NULL 検索は NULL であること。
}

// 経路キー名 (src_addrN / dst_addrN / path_groupN) が 1..8 厳格判定と非数字・先頭ゼロ拒否を満たすことの確認
TEST(configSchemaTest, parsesPathKeysStrictly)
{
    // Arrange
    potr_internal_config_path_key_kind kind = (potr_internal_config_path_key_kind)0;
    size_t idx = 99U;

    // Pre-Assert

    // Act & Assert
    for (size_t n = 1U; n <= 8U; n++)
    {
        std::string s_key = "src_addr" + std::to_string(n);
        std::string d_key = "dst_addr" + std::to_string(n);
        std::string p_key = "path_group" + std::to_string(n);
        EXPECT_EQ(POTR_OK, potr_internal_config_schema_parse_path_key(s_key.c_str(), &kind, &idx));
        EXPECT_EQ(POTR_CONFIG_PATH_SRC_ADDR, kind);
        EXPECT_EQ(n - 1U, idx);
        // [確認_正常系 回数=8] - src_addr1..8 が 0..7 へ変換されること。
        EXPECT_EQ(POTR_OK, potr_internal_config_schema_parse_path_key(d_key.c_str(), &kind, &idx));
        EXPECT_EQ(POTR_CONFIG_PATH_DST_ADDR, kind);
        EXPECT_EQ(n - 1U, idx);
        // [確認_正常系 回数=8] - dst_addr1..8 が 0..7 へ変換されること。
        EXPECT_EQ(POTR_OK, potr_internal_config_schema_parse_path_key(p_key.c_str(), &kind, &idx));
        EXPECT_EQ(POTR_CONFIG_PATH_GROUP, kind);
        EXPECT_EQ(n - 1U, idx);
        // [確認_正常系 回数=8] - path_group1..8 が 0..7 へ変換されること。
    }

    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, potr_internal_config_schema_parse_path_key("src_addr0", &kind, &idx));
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, potr_internal_config_schema_parse_path_key("src_addr9", &kind, &idx));
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, potr_internal_config_schema_parse_path_key("dst_addr10", &kind, &idx));
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, potr_internal_config_schema_parse_path_key("path_group0", &kind, &idx));
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, potr_internal_config_schema_parse_path_key("path_group9", &kind, &idx));
    // [確認_異常系] - 数字のみで 1..8 範囲外が POTR_ERR_INVALID_CONFIG であること。

    EXPECT_EQ(POTR_ERR_NOT_FOUND, potr_internal_config_schema_parse_path_key("src_addr01", &kind, &idx));
    EXPECT_EQ(POTR_ERR_NOT_FOUND, potr_internal_config_schema_parse_path_key("src_addr", &kind, &idx));
    EXPECT_EQ(POTR_ERR_NOT_FOUND, potr_internal_config_schema_parse_path_key("src_addr1a", &kind, &idx));
    EXPECT_EQ(POTR_ERR_NOT_FOUND, potr_internal_config_schema_parse_path_key("src_addr-1", &kind, &idx));
    EXPECT_EQ(POTR_ERR_NOT_FOUND, potr_internal_config_schema_parse_path_key("unknown_key", &kind, &idx));
    EXPECT_EQ(POTR_ERR_NOT_FOUND, potr_internal_config_schema_parse_path_key("", &kind, &idx));
    // [確認_異常系] - 先頭ゼロ・空接尾辞・非数字・未知キーが POTR_ERR_NOT_FOUND であること。

    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, potr_internal_config_schema_parse_path_key(nullptr, &kind, &idx));
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, potr_internal_config_schema_parse_path_key("src_addr1", nullptr, &idx));
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, potr_internal_config_schema_parse_path_key("src_addr1", &kind, nullptr));
    // [確認_異常系] - name / kind_out / index_out の NULL が POTR_ERR_INVALID_ARGUMENT であること。
}

// 通信種別変換・種別ごとの項目/経路適用マトリクス・既定値初期化の整合性確認
TEST(configSchemaTest, checksTypeConversionApplicabilityAndDefaults)
{
    // Arrange
    potr_global_config global;
    potr_service_def service;

    // Pre-Assert

    // Act
    potr_internal_config_global_init_defaults(nullptr);
    potr_internal_config_service_init_defaults(nullptr, 1);
    potr_internal_config_global_init_defaults(&global);
    potr_internal_config_service_init_defaults(&service, 42);
    // [手順] - NULL 許容と既定値初期化、通信種別変換・適用判定を呼び出す。

    // Assert
    EXPECT_EQ(POTR_TYPE_UNICAST, potr_internal_config_type_from_string("unicast"));
    EXPECT_EQ(POTR_TYPE_TCP_BIDIR, potr_internal_config_type_from_string("tcp_bidir"));
    EXPECT_EQ((potr_type)0, potr_internal_config_type_from_string("UNICAST"));
    EXPECT_EQ((potr_type)0, potr_internal_config_type_from_string(nullptr));
    // [確認_正常系] - 通信種別の文字列変換と大文字小文字区別が正しいこと。

    EXPECT_EQ(0, potr_internal_config_path_key_is_applicable(POTR_CONFIG_PATH_SRC_ADDR, POTR_TYPE_UNICAST_BIDIR_N1));
    EXPECT_EQ(1, potr_internal_config_path_key_is_applicable(POTR_CONFIG_PATH_DST_ADDR, POTR_TYPE_UNICAST_BIDIR_N1));
    EXPECT_EQ(0, potr_internal_config_path_key_is_applicable(POTR_CONFIG_PATH_DST_ADDR, POTR_TYPE_BROADCAST));
    EXPECT_EQ(0, potr_internal_config_path_key_is_applicable(POTR_CONFIG_PATH_GROUP, POTR_TYPE_UNICAST_RAW));
    EXPECT_EQ(0, potr_internal_config_path_key_is_applicable(POTR_CONFIG_PATH_GROUP, POTR_TYPE_MULTICAST_RAW));
    EXPECT_EQ(0, potr_internal_config_path_key_is_applicable(POTR_CONFIG_PATH_GROUP, POTR_TYPE_BROADCAST_RAW));
    EXPECT_EQ(1, potr_internal_config_path_key_is_applicable(POTR_CONFIG_PATH_GROUP, POTR_TYPE_TCP_BIDIR));
    EXPECT_EQ(0, potr_internal_config_type_is_applicable((potr_type)0, UINT32_MAX));
    int invalid_type = 99;
    EXPECT_EQ(0, potr_internal_config_type_is_applicable(static_cast<potr_type>(invalid_type), UINT32_MAX));
    // [確認_正常系] - 経路キー適用可否および範囲外通信種別が設計仕様の表と一致すること。

    EXPECT_EQ(64U, global.window_size);
    EXPECT_EQ(1400U, global.max_payload);
    EXPECT_EQ(65535U, global.max_message_size);
    EXPECT_EQ(1024U, global.send_queue_depth);
    EXPECT_EQ(4194304U, global.udp_receive_buffer_size);
    EXPECT_EQ(42, service.service_id);
    EXPECT_EQ(1U, service.ttl);
    EXPECT_EQ(1024U, service.max_peers);
    EXPECT_EQ(5000U, service.reconnect_interval_ms);
    EXPECT_EQ(10000U, service.connect_timeout_ms);
    EXPECT_EQ(POTR_AUTH_PSK, service.auth);
    // [確認_正常系] - global と service の既定値初期化が設計仕様どおりであること。
}
