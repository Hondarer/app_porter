/**
 *******************************************************************************
 *  @file           potrFlowControlResolveTest.cc
 *  @brief          potr_internal_flow_control_resolve() の実効値の決定と検査を検証します。
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
#include <porter/porter_const.h>
#include <porter/potr_context.h>
#include <porter/potr_flow_control.h>
#include <cstring>
using namespace testing;

class potrFlowControlResolveTest : public Test
{
  protected:
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_service_def svc;
    potr_global_config glb;
    uint32_t fixture_padding = 0U; // クラスのサイズを 8 バイト境界にそろえるためのパディング
    potr_internal_flow_control_config cfg;

    // [サブ手順 名前=potrFlowControlResolveTest.SetUp]
    void SetUp() override
    {
        memset(&svc, 0, sizeof(svc));
        memset(&glb, 0, sizeof(glb));
        memset(&cfg, 0xAA, sizeof(cfg));
        svc.type = POTR_TYPE_UNICAST;
        svc.service_id = 1;
        glb.max_message_size = 4096U; // [状態] - 余裕の検査に使う max_message_size を 4096 にする。
    }
    // [サブ手順終了]

    int resolve(potr_role role = POTR_ROLE_SENDER, uint32_t health_interval_ms = 0U)
    {
        return potr_internal_flow_control_resolve(&svc, &glb, role, health_interval_ms, &cfg);
    }
};

// サービスとグローバルがすべて 0 の場合に、既定値が実効値になることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, all_zero_uses_defaults)
{
    // Arrange
    // Pre-Assert
    // Act
    int actual_ret = resolve(); // [手順] - サービスとグローバルを 0 のままで resolve を呼び出す。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);                                                    // [確認_正常系] - 戻り値が POTR_OK であること。
    EXPECT_EQ(0U, cfg.capacity);                                                       // [確認_正常系] - 容量が 0 であること。
    EXPECT_EQ((uint32_t)POTR_DEFAULT_FLOW_CONTROL_SUPPRESS_PERCENT, cfg.suppress_percent); // [確認_正常系] - 要求のしきい値が既定値であること。
    EXPECT_EQ((uint32_t)POTR_DEFAULT_FLOW_CONTROL_RESUME_PERCENT, cfg.resume_percent);     // [確認_正常系] - 解除のしきい値が既定値であること。
    EXPECT_EQ((uint32_t)POTR_DEFAULT_FLOW_CONTROL_REQUEST_INTERVAL_MS, cfg.request_interval_ms); // [確認_正常系] - 再送周期が既定値であること。
    EXPECT_EQ((uint32_t)POTR_DEFAULT_FLOW_CONTROL_REQUEST_LIFETIME_MS, cfg.request_lifetime_ms); // [確認_正常系] - 有効期限が既定値であること。
    EXPECT_EQ(0U, cfg.receiver_percent);                                               // [確認_正常系] - 割合が 0 であること。
    EXPECT_EQ(0U, cfg.receiver_count);                                                 // [確認_正常系] - 台数が 0 であること。
}

// サービスが 0 の場合に、グローバルの 7 項目がそのまま実効値になることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, global_values_used_when_service_is_zero)
{
    // Arrange
    glb.flow_control_capacity = 70000U;
    glb.flow_control_suppress_percent = 90U;
    glb.flow_control_resume_percent = 50U;
    glb.flow_control_request_interval_ms = 300U;
    glb.flow_control_request_lifetime_ms = 2000U;
    glb.flow_control_receiver_percent = 25U;
    glb.flow_control_receiver_count = 3U;
    // [状態] - グローバルへ 7 項目の値を設定する。サービスは 0 のままにする。
    // Pre-Assert
    // Act
    int actual_ret = resolve(); // [手順] - resolve を呼び出す。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);               // [確認_正常系] - 戻り値が POTR_OK であること。
    EXPECT_EQ(70000U, cfg.capacity);              // [確認_正常系] - 容量がグローバルの値であること。
    EXPECT_EQ(90U, cfg.suppress_percent);         // [確認_正常系] - 要求のしきい値がグローバルの値であること。
    EXPECT_EQ(50U, cfg.resume_percent);           // [確認_正常系] - 解除のしきい値がグローバルの値であること。
    EXPECT_EQ(300U, cfg.request_interval_ms);     // [確認_正常系] - 再送周期がグローバルの値であること。
    EXPECT_EQ(2000U, cfg.request_lifetime_ms);    // [確認_正常系] - 有効期限がグローバルの値であること。
    EXPECT_EQ(25U, cfg.receiver_percent);         // [確認_正常系] - 割合がグローバルの値であること。
    EXPECT_EQ(3U, cfg.receiver_count);            // [確認_正常系] - 台数がグローバルの値であること。
}

// サービスに 0 以外の値がある場合に、サービスの値が優先されることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, service_values_take_precedence)
{
    // Arrange
    glb.flow_control_capacity = 70000U;
    glb.flow_control_suppress_percent = 90U;
    glb.flow_control_resume_percent = 50U;
    glb.flow_control_request_interval_ms = 300U;
    glb.flow_control_request_lifetime_ms = 2000U;
    glb.flow_control_receiver_percent = 25U;
    glb.flow_control_receiver_count = 3U;
    svc.flow_control_capacity = 100000U;
    svc.flow_control_suppress_percent = 95U;
    svc.flow_control_resume_percent = 40U;
    svc.flow_control_request_interval_ms = 100U;
    svc.flow_control_request_lifetime_ms = 500U;
    svc.flow_control_receiver_percent = 50U;
    svc.flow_control_receiver_count = 4U;
    // [状態] - グローバルとサービスへ、異なる 7 項目の値を設定する。
    // Pre-Assert
    // Act
    int actual_ret = resolve(); // [手順] - resolve を呼び出す。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);               // [確認_正常系] - 戻り値が POTR_OK であること。
    EXPECT_EQ(100000U, cfg.capacity);             // [確認_正常系] - 容量がサービスの値であること。
    EXPECT_EQ(95U, cfg.suppress_percent);         // [確認_正常系] - 要求のしきい値がサービスの値であること。
    EXPECT_EQ(40U, cfg.resume_percent);           // [確認_正常系] - 解除のしきい値がサービスの値であること。
    EXPECT_EQ(100U, cfg.request_interval_ms);     // [確認_正常系] - 再送周期がサービスの値であること。
    EXPECT_EQ(500U, cfg.request_lifetime_ms);     // [確認_正常系] - 有効期限がサービスの値であること。
    EXPECT_EQ(50U, cfg.receiver_percent);         // [確認_正常系] - 割合がサービスの値であること。
    EXPECT_EQ(4U, cfg.receiver_count);            // [確認_正常系] - 台数がサービスの値であること。
}

// サービスの容量、割合、台数が DISABLED の場合に、グローバルに値があっても実効値が 0 になることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, service_disabled_overrides_global_for_capacity_percent_count)
{
    // Arrange
    glb.flow_control_capacity = 70000U;
    glb.flow_control_receiver_percent = 25U;
    glb.flow_control_receiver_count = 3U;
    svc.flow_control_capacity = POTR_FLOW_CONTROL_DISABLED;
    svc.flow_control_receiver_percent = POTR_FLOW_CONTROL_DISABLED;
    svc.flow_control_receiver_count = POTR_FLOW_CONTROL_DISABLED;
    // [状態] - グローバルに値を設定し、サービスの容量、割合、台数を DISABLED にする。
    // Pre-Assert
    // Act
    int actual_ret = resolve(); // [手順] - resolve を呼び出す。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);       // [確認_正常系] - 戻り値が POTR_OK であること。
    EXPECT_EQ(0U, cfg.capacity);          // [確認_正常系] - 容量が 0 であること。
    EXPECT_EQ(0U, cfg.receiver_percent);  // [確認_正常系] - 割合が 0 であること。
    EXPECT_EQ(0U, cfg.receiver_count);    // [確認_正常系] - 台数が 0 であること。
}

// サービスのしきい値、再送周期、有効期限が DISABLED の場合に、引数エラーになることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, service_disabled_is_invalid_for_non_disableable_fields)
{
    // Arrange
    uint32_t potr_service_def::*fields[] = {&potr_service_def::flow_control_suppress_percent,
                                            &potr_service_def::flow_control_resume_percent,
                                            &potr_service_def::flow_control_request_interval_ms,
                                            &potr_service_def::flow_control_request_lifetime_ms};
    int actual_rets[4] = {};
    // [状態] - 要求のしきい値、解除のしきい値、再送周期、有効期限の 4 項目を順に DISABLED にする (ループ 4 回)。
    // Pre-Assert
    // Act
    for (size_t i = 0; i < 4; i++)
    {
        memset(&svc, 0, sizeof(svc));
        svc.type = POTR_TYPE_UNICAST;
        svc.*(fields[i]) = POTR_FLOW_CONTROL_DISABLED;
        actual_rets[i] = resolve();
    }
    // [手順] - 項目ごとに DISABLED を設定して resolve を呼び出し、戻り値を記録する。
    // Assert
    for (size_t i = 0; i < 4; i++)
    {
        EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_rets[i]);
        // [確認_異常系] - 4 項目のいずれでも戻り値が POTR_ERR_INVALID_ARGUMENT であること。(確認 4 回)
    }
}

// グローバルの 7 項目のいずれかが DISABLED の場合に、引数エラーになることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, global_disabled_is_invalid)
{
    // Arrange
    uint32_t potr_global_config::*fields[] = {&potr_global_config::flow_control_capacity,
                                              &potr_global_config::flow_control_suppress_percent,
                                              &potr_global_config::flow_control_resume_percent,
                                              &potr_global_config::flow_control_request_interval_ms,
                                              &potr_global_config::flow_control_request_lifetime_ms,
                                              &potr_global_config::flow_control_receiver_percent,
                                              &potr_global_config::flow_control_receiver_count};
    int actual_rets[7] = {};
    // [状態] - グローバルの 7 項目を順に DISABLED にする (ループ 7 回)。
    // Pre-Assert
    // Act
    for (size_t i = 0; i < 7; i++)
    {
        memset(&glb, 0, sizeof(glb));
        glb.max_message_size = 4096U;
        glb.*(fields[i]) = POTR_FLOW_CONTROL_DISABLED;
        actual_rets[i] = resolve();
    }
    // [手順] - 項目ごとに DISABLED を設定して resolve を呼び出し、戻り値を記録する。
    // Assert
    for (size_t i = 0; i < 7; i++)
    {
        EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_rets[i]);
        // [確認_異常系] - 7 項目のいずれでも戻り値が POTR_ERR_INVALID_ARGUMENT であること。(確認 7 回)
    }
}

// しきい値の境界を検査することの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, threshold_validation)
{
    // Arrange
    // Pre-Assert
    // Act
    svc.flow_control_suppress_percent = 101U;
    svc.flow_control_resume_percent = 60U;
    int actual_ret_suppress_over = resolve(); // [手順] - 要求のしきい値 101、解除のしきい値 60 で resolve を呼び出す。
    svc.flow_control_suppress_percent = 80U;
    svc.flow_control_resume_percent = 80U;
    int actual_ret_equal = resolve(); // [手順] - 要求と解除をともに 80 にして resolve を呼び出す。
    svc.flow_control_suppress_percent = 60U;
    svc.flow_control_resume_percent = 80U;
    int actual_ret_inverted = resolve(); // [手順] - 要求 60、解除 80 にして resolve を呼び出す。
    svc.flow_control_suppress_percent = 100U;
    svc.flow_control_resume_percent = 99U;
    int actual_ret_max = resolve(); // [手順] - 要求 100、解除 99 にして resolve を呼び出す。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_suppress_over); // [確認_異常系] - 要求のしきい値 101 で POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_equal);         // [確認_異常系] - 解除が要求と同じ場合に POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_inverted);      // [確認_異常系] - 解除が要求より大きい場合に POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_OK, actual_ret_max);                             // [確認_正常系] - 要求 100、解除 99 で POTR_OK であること。
    EXPECT_EQ(100U, cfg.suppress_percent);                          // [確認_正常系] - 要求のしきい値が 100 であること。
    EXPECT_EQ(99U, cfg.resume_percent);                             // [確認_正常系] - 解除のしきい値が 99 であること。
}

// 有効期限が再送周期以下の場合に、引数エラーになることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, lifetime_must_exceed_interval)
{
    // Arrange
    // Pre-Assert
    // Act
    svc.flow_control_request_interval_ms = 200U;
    svc.flow_control_request_lifetime_ms = 200U;
    int actual_ret_equal = resolve(); // [手順] - 有効期限を再送周期と同じ 200 にして resolve を呼び出す。
    svc.flow_control_request_lifetime_ms = 199U;
    int actual_ret_smaller = resolve(); // [手順] - 有効期限を再送周期より小さい 199 にして resolve を呼び出す。
    svc.flow_control_request_lifetime_ms = 201U;
    int actual_ret_larger = resolve(); // [手順] - 有効期限を再送周期より 1 大きい 201 にして resolve を呼び出す。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_equal);   // [確認_異常系] - 同じ値で POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_smaller); // [確認_異常系] - 小さい値で POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_OK, actual_ret_larger);                    // [確認_正常系] - 1 大きい値で POTR_OK であること。
}

// 一対多の割合が 100 を超える場合に、引数エラーになることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, receiver_percent_upper_bound)
{
    // Arrange
    // Pre-Assert
    // Act
    svc.flow_control_receiver_percent = 101U;
    int actual_ret_over = resolve(); // [手順] - 割合 101 で resolve を呼び出す。
    svc.flow_control_receiver_percent = 100U;
    int actual_ret_max = resolve(); // [手順] - 割合 100 で resolve を呼び出す。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_over); // [確認_異常系] - 割合 101 で POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_OK, actual_ret_max);                    // [確認_正常系] - 割合 100 で POTR_OK であること。
}

// 要求のしきい値から容量までの余裕が max_message_size 未満の場合に、引数エラーになることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, margin_must_hold_max_message)
{
    // Arrange
    // [状態] - max_message_size を 4096 とし、要求のしきい値は既定の 80 [%] とする。
    // Pre-Assert
    // Act
    svc.flow_control_capacity = 20474U;
    int actual_ret_smaller = resolve();
    // [手順] - 余裕が 20474 − 16379 = 4095 (max_message_size より 1 小さい) 容量で resolve を呼び出す。
    svc.flow_control_capacity = 20480U;
    int actual_ret_equal = resolve();
    uint32_t capacity_equal = cfg.capacity;
    // [手順] - 余裕が 20480 − 16384 = 4096 (max_message_size と同じ) 容量で resolve を呼び出す。
    svc.flow_control_suppress_percent = 100U;
    svc.flow_control_resume_percent = 50U;
    svc.flow_control_capacity = 1000000U;
    int actual_ret_no_margin = resolve();
    // [手順] - 要求のしきい値を 100 [%] (余裕 0) にして、十分に大きい容量で resolve を呼び出す。
    svc.flow_control_suppress_percent = 80U;
    svc.flow_control_capacity = 0xFFFFFFFEU;
    int actual_ret_large = resolve();
    // [手順] - 容量を 0xFFFFFFFE (容量 × 要求のしきい値が 32 ビットを超える) にして resolve を呼び出す。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_smaller); // [確認_異常系] - 余裕が 1 小さい容量で POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_OK, actual_ret_equal);                     // [確認_正常系] - 余裕が同じ容量で POTR_OK であること。
    EXPECT_EQ(20480U, capacity_equal);                        // [確認_正常系] - 容量が 20480 であること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_no_margin); // [確認_異常系] - 余裕が 0 の構成で POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_OK, actual_ret_large); // [確認_正常系] - 桁あふれせずに余裕を求め、POTR_OK であること。
}

// 再送周期と有効期限が、待機時間の設定値の上限 (POTR_WAIT_MS_MAX) を超える場合に、引数エラーになることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, interval_and_lifetime_upper_bound)
{
    // Arrange
    // Pre-Assert
    // Act
    svc.flow_control_request_interval_ms = POTR_WAIT_MS_MAX - 1U;
    svc.flow_control_request_lifetime_ms = POTR_WAIT_MS_MAX;
    int actual_ret_max = resolve();
    // [手順] - 再送周期を上限 − 1、有効期限を上限ちょうどにして resolve を呼び出す。
    svc.flow_control_request_lifetime_ms = POTR_WAIT_MS_MAX + 1U;
    int actual_ret_lifetime_over = resolve();
    // [手順] - 有効期限を上限 + 1 にして resolve を呼び出す。
    svc.flow_control_request_interval_ms = POTR_WAIT_MS_MAX + 1U;
    svc.flow_control_request_lifetime_ms = POTR_WAIT_MS_MAX + 2U;
    int actual_ret_interval_over = resolve();
    // [手順] - 再送周期を上限 + 1、有効期限を上限 + 2 にして resolve を呼び出す。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_max);                             // [確認_正常系] - 上限ちょうどで POTR_OK であること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_lifetime_over); // [確認_異常系] - 有効期限の超過で POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_interval_over); // [確認_異常系] - 再送周期の超過で POTR_ERR_INVALID_ARGUMENT であること。
}

// マルチキャストとブロードキャストの受信者で、health_interval_ms が 0 の場合に引数エラーになることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, one_to_many_receiver_requires_health_interval)
{
    // Arrange
    potr_type types[] = {POTR_TYPE_MULTICAST, POTR_TYPE_BROADCAST};
    int receiver_zero[2] = {};
    int sender_zero[2] = {};
    int receiver_nonzero[2] = {};
    svc.flow_control_capacity = 70000U;
    // [状態] - 容量 70000 を設定する。通信種別ごとに 3 通りを確認する (ループ 2 回)。
    // Pre-Assert
    // Act
    for (size_t i = 0; i < 2; i++)
    {
        svc.type = types[i];
        receiver_zero[i] = resolve(POTR_ROLE_RECEIVER, 0U);
        sender_zero[i] = resolve(POTR_ROLE_SENDER, 0U);
        receiver_nonzero[i] = resolve(POTR_ROLE_RECEIVER, 1000U);
    }
    // [手順] - 受信者の 0、送信者の 0、受信者の 1000 で resolve を呼び出し、戻り値を記録する。
    // Assert
    for (size_t i = 0; i < 2; i++)
    {
        EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, receiver_zero[i]);
        // [確認_異常系] - 受信者で health_interval_ms が 0 の場合に POTR_ERR_INVALID_ARGUMENT であること。(確認 2 回)
        EXPECT_EQ(POTR_OK, sender_zero[i]);
        // [確認_正常系] - 送信者で health_interval_ms が 0 の場合に POTR_OK であること。(確認 2 回)
        EXPECT_EQ(POTR_OK, receiver_nonzero[i]);
        // [確認_正常系] - 受信者で health_interval_ms が 0 以外の場合に POTR_OK であること。(確認 2 回)
    }
}

// RAW 系の通信種別で、不正な値があっても POTR_OK になり、容量が 0 になることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, raw_types_ignore_settings)
{
    // Arrange
    potr_type types[] = {POTR_TYPE_UNICAST_RAW, POTR_TYPE_MULTICAST_RAW, POTR_TYPE_BROADCAST_RAW};
    int actual_rets[3] = {};
    uint32_t actual_capacities[3] = {};
    svc.flow_control_capacity = 70000U;
    svc.flow_control_suppress_percent = 200U;
    // [状態] - 容量 70000 と不正な要求のしきい値 200 を設定する (ループ 3 回)。
    // Pre-Assert
    // Act
    for (size_t i = 0; i < 3; i++)
    {
        svc.type = types[i];
        actual_rets[i] = resolve(POTR_ROLE_RECEIVER, 0U);
        actual_capacities[i] = cfg.capacity;
    }
    // [手順] - RAW 系の 3 種別で resolve を呼び出し、戻り値と容量を記録する。
    // Assert
    for (size_t i = 0; i < 3; i++)
    {
        EXPECT_EQ(POTR_OK, actual_rets[i]);       // [確認_正常系] - 戻り値が POTR_OK であること。(確認 3 回)
        EXPECT_EQ(0U, actual_capacities[i]);      // [確認_正常系] - 実効の容量が 0 であること。(確認 3 回)
    }
}

// service、global、config のいずれかが NULL の場合に、引数エラーになることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, null_arguments)
{
    // Arrange
    // Pre-Assert
    // Act
    int actual_ret_service = potr_internal_flow_control_resolve(nullptr, &glb, POTR_ROLE_SENDER, 0U, &cfg);
    // [手順] - service を NULL にして resolve を呼び出す。
    int actual_ret_global = potr_internal_flow_control_resolve(&svc, nullptr, POTR_ROLE_SENDER, 0U, &cfg);
    // [手順] - global を NULL にして resolve を呼び出す。
    int actual_ret_config = potr_internal_flow_control_resolve(&svc, &glb, POTR_ROLE_SENDER, 0U, nullptr);
    // [手順] - config を NULL にして resolve を呼び出す。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_service); // [確認_異常系] - service が NULL で POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_global);  // [確認_異常系] - global が NULL で POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_config);  // [確認_異常系] - config が NULL で POTR_ERR_INVALID_ARGUMENT であること。
}

// サービスとグローバルがすべて 0 の場合に、監視する受信者の割合と台数が 0、受信バッファーの幅の割合が既定値になることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, all_zero_uses_defaults_for_monitor_and_receive_buffer)
{
    // Arrange
    // [状態] - 通信種別は unicast のままとする。
    // Pre-Assert
    // Act
    int actual_ret = resolve(POTR_ROLE_RECEIVER); // [手順] - サービスとグローバルを 0 のままで、受信者として resolve を呼び出す。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);                  // [確認_正常系] - 戻り値が POTR_OK であること。
    EXPECT_EQ(0U, cfg.monitored_receiver_percent);   // [確認_正常系] - 監視する受信者の割合が 0 であること。
    EXPECT_EQ(0U, cfg.monitored_receiver_count);     // [確認_正常系] - 監視する受信者の台数が 0 であること。
    EXPECT_EQ((uint32_t)POTR_DEFAULT_FLOW_CONTROL_RECEIVE_BUFFER_PERCENT, cfg.receive_buffer_percent);
    // [確認_正常系] - 受信バッファーの幅の割合が既定値 (100) であること。
}

// 監視する受信者の割合と台数が、グローバル、サービスの順に重なり、サービスの DISABLED で 0 になることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, monitored_receiver_values_overlay)
{
    // Arrange
    glb.flow_control_monitored_receiver_percent = 10U;
    glb.flow_control_monitored_receiver_count = 4U;
    // [状態] - グローバルの監視する受信者の割合を 10、台数を 4 にする。
    // Pre-Assert
    // Act
    int actual_ret_global = resolve();
    uint32_t global_percent = cfg.monitored_receiver_percent;
    uint32_t global_count = cfg.monitored_receiver_count;
    // [手順] - サービスを 0 のままで resolve を呼び出す。
    svc.flow_control_monitored_receiver_percent = 20U;
    svc.flow_control_monitored_receiver_count = 8U;
    int actual_ret_service = resolve();
    uint32_t service_percent = cfg.monitored_receiver_percent;
    uint32_t service_count = cfg.monitored_receiver_count;
    // [手順] - サービスの割合を 20、台数を 8 にして resolve を呼び出す。
    svc.flow_control_monitored_receiver_percent = POTR_FLOW_CONTROL_DISABLED;
    svc.flow_control_monitored_receiver_count = POTR_FLOW_CONTROL_DISABLED;
    int actual_ret_disabled = resolve();
    // [手順] - サービスの割合と台数を DISABLED にして resolve を呼び出す。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_global);           // [確認_正常系] - グローバルだけの場合に POTR_OK であること。
    EXPECT_EQ(10U, global_percent);                  // [確認_正常系] - 割合がグローバルの値であること。
    EXPECT_EQ(4U, global_count);                     // [確認_正常系] - 台数がグローバルの値であること。
    EXPECT_EQ(POTR_OK, actual_ret_service);          // [確認_正常系] - サービスの値がある場合に POTR_OK であること。
    EXPECT_EQ(20U, service_percent);                 // [確認_正常系] - 割合がサービスの値であること。
    EXPECT_EQ(8U, service_count);                    // [確認_正常系] - 台数がサービスの値であること。
    EXPECT_EQ(POTR_OK, actual_ret_disabled);         // [確認_正常系] - DISABLED の場合に POTR_OK であること。
    EXPECT_EQ(0U, cfg.monitored_receiver_percent);   // [確認_正常系] - DISABLED の割合が 0 であること。
    EXPECT_EQ(0U, cfg.monitored_receiver_count);     // [確認_正常系] - DISABLED の台数が 0 であること。
}

// グローバルの監視する受信者の割合と台数が DISABLED の場合に、引数エラーになることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, global_disabled_is_invalid_for_monitored_receiver)
{
    // Arrange
    // Pre-Assert
    // Act
    glb.flow_control_monitored_receiver_percent = POTR_FLOW_CONTROL_DISABLED;
    int actual_ret_percent = resolve(); // [手順] - グローバルの割合を DISABLED にして resolve を呼び出す。
    glb.flow_control_monitored_receiver_percent = 0U;
    glb.flow_control_monitored_receiver_count = POTR_FLOW_CONTROL_DISABLED;
    int actual_ret_count = resolve(); // [手順] - グローバルの台数を DISABLED にして resolve を呼び出す。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_percent); // [確認_異常系] - 割合の DISABLED で POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_count);   // [確認_異常系] - 台数の DISABLED で POTR_ERR_INVALID_ARGUMENT であること。
}

// 監視する受信者の割合が 100、台数が 64 を超える場合に、引数エラーになることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, monitored_receiver_upper_bound)
{
    // Arrange
    // Pre-Assert
    // Act
    svc.flow_control_monitored_receiver_percent = 101U;
    int actual_ret_percent_over = resolve(); // [手順] - 割合 101 で resolve を呼び出す。
    svc.flow_control_monitored_receiver_percent = 100U;
    int actual_ret_percent_max = resolve(); // [手順] - 割合 100 で resolve を呼び出す。
    svc.flow_control_monitored_receiver_count = POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX + 1U;
    int actual_ret_count_over = resolve(); // [手順] - 台数 65 で resolve を呼び出す。
    svc.flow_control_monitored_receiver_count = POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX;
    int actual_ret_count_max = resolve(); // [手順] - 台数 64 で resolve を呼び出す。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_percent_over); // [確認_異常系] - 割合 101 で POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_OK, actual_ret_percent_max);                    // [確認_正常系] - 割合 100 で POTR_OK であること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_count_over);   // [確認_異常系] - 台数 65 で POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_OK, actual_ret_count_max);                      // [確認_正常系] - 台数 64 で POTR_OK であること。
    EXPECT_EQ(POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX, cfg.monitored_receiver_count); // [確認_正常系] - 台数が 64 であること。
}

// 受信バッファーの幅の割合が、グローバルとサービスの両方で DISABLED を受け付け、サービスの値が優先されることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, receive_buffer_percent_overlay_and_disabled)
{
    // Arrange
    // [状態] - unicast の受信者として resolve を呼び出す。
    // Pre-Assert
    // Act
    glb.flow_control_receive_buffer_percent = 50U;
    int actual_ret_global = resolve(POTR_ROLE_RECEIVER);
    uint32_t global_percent = cfg.receive_buffer_percent;
    // [手順] - グローバルを 50、サービスを 0 にして resolve を呼び出す。
    svc.flow_control_receive_buffer_percent = 30U;
    int actual_ret_service = resolve(POTR_ROLE_RECEIVER);
    uint32_t service_percent = cfg.receive_buffer_percent;
    // [手順] - サービスを 30 にして resolve を呼び出す。
    svc.flow_control_receive_buffer_percent = POTR_FLOW_CONTROL_DISABLED;
    int actual_ret_service_disabled = resolve(POTR_ROLE_RECEIVER);
    uint32_t service_disabled_percent = cfg.receive_buffer_percent;
    // [手順] - サービスを DISABLED にして resolve を呼び出す。
    svc.flow_control_receive_buffer_percent = 0U;
    glb.flow_control_receive_buffer_percent = POTR_FLOW_CONTROL_DISABLED;
    int actual_ret_global_disabled = resolve(POTR_ROLE_RECEIVER);
    uint32_t global_disabled_percent = cfg.receive_buffer_percent;
    // [手順] - サービスを 0、グローバルを DISABLED にして resolve を呼び出す。
    svc.flow_control_receive_buffer_percent = 40U;
    int actual_ret_service_over_disabled = resolve(POTR_ROLE_RECEIVER);
    // [手順] - グローバルを DISABLED のまま、サービスを 40 にして resolve を呼び出す。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_global);               // [確認_正常系] - グローバルだけの場合に POTR_OK であること。
    EXPECT_EQ(50U, global_percent);                      // [確認_正常系] - 割合がグローバルの値であること。
    EXPECT_EQ(POTR_OK, actual_ret_service);              // [確認_正常系] - サービスの値がある場合に POTR_OK であること。
    EXPECT_EQ(30U, service_percent);                     // [確認_正常系] - 割合がサービスの値であること。
    EXPECT_EQ(POTR_OK, actual_ret_service_disabled);     // [確認_正常系] - サービスの DISABLED で POTR_OK であること。
    EXPECT_EQ(0U, service_disabled_percent);             // [確認_正常系] - サービスの DISABLED で 0 (使わない) であること。
    EXPECT_EQ(POTR_OK, actual_ret_global_disabled);      // [確認_正常系] - グローバルの DISABLED で POTR_OK であること。
    EXPECT_EQ(0U, global_disabled_percent);              // [確認_正常系] - グローバルの DISABLED で 0 (使わない) であること。
    EXPECT_EQ(POTR_OK, actual_ret_service_over_disabled); // [確認_正常系] - グローバルの DISABLED をサービスで上書きして POTR_OK であること。
    EXPECT_EQ(40U, cfg.receive_buffer_percent);          // [確認_正常系] - 割合がサービスの値であること。
}

// 受信バッファーの幅の割合が 1 から 100 の範囲にない場合に、通信種別によらず引数エラーになることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, receive_buffer_percent_upper_bound)
{
    // Arrange
    // Pre-Assert
    // Act
    svc.flow_control_receive_buffer_percent = 101U;
    int actual_ret_over = resolve(POTR_ROLE_RECEIVER); // [手順] - unicast の受信者で、サービスの割合 101 で resolve を呼び出す。
    svc.flow_control_receive_buffer_percent = 0U;
    glb.flow_control_receive_buffer_percent = 101U;
    int actual_ret_global_over = resolve(POTR_ROLE_RECEIVER); // [手順] - グローバルの割合 101 で resolve を呼び出す。
    glb.flow_control_receive_buffer_percent = 0U;
    svc.type = POTR_TYPE_TCP;
    svc.flow_control_receive_buffer_percent = 101U;
    int actual_ret_tcp_over = resolve(POTR_ROLE_RECEIVER); // [手順] - 対象外の tcp で、割合 101 で resolve を呼び出す。
    svc.type = POTR_TYPE_UNICAST;
    svc.flow_control_receive_buffer_percent = 100U;
    int actual_ret_max = resolve(POTR_ROLE_RECEIVER); // [手順] - unicast の受信者で、割合 100 で resolve を呼び出す。
    svc.flow_control_receive_buffer_percent = 1U;
    int actual_ret_min = resolve(POTR_ROLE_RECEIVER); // [手順] - unicast の受信者で、割合 1 で resolve を呼び出す。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_over);        // [確認_異常系] - サービスの割合 101 で POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_global_over); // [確認_異常系] - グローバルの割合 101 で POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_tcp_over);    // [確認_異常系] - 対象外の通信種別でも割合 101 で POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_OK, actual_ret_max);                           // [確認_正常系] - 割合 100 で POTR_OK であること。
    EXPECT_EQ(POTR_OK, actual_ret_min);                           // [確認_正常系] - 割合 1 で POTR_OK であること。
    EXPECT_EQ(1U, cfg.receive_buffer_percent);                    // [確認_正常系] - 割合が 1 であること。
}

namespace
{
// 受信バッファーの幅の割合を使うかどうかを確認する、通信種別、役割、実効 health_interval_ms と期待値の組
struct receive_buffer_case
{
    potr_type type;
    potr_role role;
    uint32_t health_interval_ms;
    uint32_t expected_percent;
};
} // namespace

// OS の受信バッファーに基づく送出上限を、対象の通信種別と役割だけで使うことの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, receive_buffer_percent_applies_to_target_types)
{
    // Arrange
    const receive_buffer_case cases[] = {
        {POTR_TYPE_UNICAST, POTR_ROLE_RECEIVER, 0U, 100U},
        {POTR_TYPE_UNICAST, POTR_ROLE_SENDER, 0U, 0U},
        {POTR_TYPE_MULTICAST, POTR_ROLE_RECEIVER, 1000U, 100U},
        {POTR_TYPE_MULTICAST, POTR_ROLE_RECEIVER, 0U, 0U},
        {POTR_TYPE_MULTICAST, POTR_ROLE_SENDER, 1000U, 0U},
        {POTR_TYPE_BROADCAST, POTR_ROLE_RECEIVER, 1000U, 100U},
        {POTR_TYPE_BROADCAST, POTR_ROLE_RECEIVER, 0U, 0U},
        {POTR_TYPE_UNICAST_BIDIR, POTR_ROLE_SENDER, 0U, 100U},
        {POTR_TYPE_UNICAST_BIDIR, POTR_ROLE_RECEIVER, 0U, 100U},
        {POTR_TYPE_UNICAST_BIDIR_N1, POTR_ROLE_SENDER, 0U, 100U},
        {POTR_TYPE_UNICAST_BIDIR_N1, POTR_ROLE_RECEIVER, 0U, 100U},
        {POTR_TYPE_TCP, POTR_ROLE_RECEIVER, 0U, 0U},
        {POTR_TYPE_TCP_BIDIR, POTR_ROLE_SENDER, 0U, 0U},
        {POTR_TYPE_UNICAST_RAW, POTR_ROLE_RECEIVER, 0U, 0U},
    };
    const size_t case_count = sizeof(cases) / sizeof(cases[0]);
    int actual_rets[sizeof(cases) / sizeof(cases[0])] = {};
    uint32_t actual_percents[sizeof(cases) / sizeof(cases[0])] = {};
    // [状態] - 14 通りの通信種別、役割、実効 health_interval_ms の組を用意する (ループ 14 回)。
    // Pre-Assert
    // Act
    for (size_t i = 0; i < case_count; i++)
    {
        svc.type = cases[i].type;
        actual_rets[i] = resolve(cases[i].role, cases[i].health_interval_ms);
        actual_percents[i] = cfg.receive_buffer_percent;
    }
    // [手順] - 組ごとに resolve を呼び出し、戻り値と受信バッファーの幅の割合を記録する。
    // Assert
    for (size_t i = 0; i < case_count; i++)
    {
        EXPECT_EQ(POTR_OK, actual_rets[i]); // [確認_正常系] - 戻り値が POTR_OK であること。(確認 14 回)
        EXPECT_EQ(cases[i].expected_percent, actual_percents[i]);
        // [確認_正常系] - unicast、multicast、broadcast の受信者 (multicast と broadcast は health_interval_ms が 0 以外)、unicast_bidir と unicast_bidir_n1 の両方の役割で 100、それ以外で 0 であること。(確認 14 回)
    }
}
