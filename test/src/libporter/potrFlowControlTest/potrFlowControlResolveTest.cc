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
        glb.max_message_size = 65535U;
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
    svc.flow_control_capacity = 80000U;
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
    EXPECT_EQ(80000U, cfg.capacity);              // [確認_正常系] - 容量がサービスの値であること。
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
        glb.max_message_size = 65535U;
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

// 容量が max_message_size 未満の場合に、引数エラーになることの確認
// [サブ手順参照 名前=potrFlowControlResolveTest.SetUp]
TEST_F(potrFlowControlResolveTest, capacity_must_hold_max_message)
{
    // Arrange
    // Pre-Assert
    // Act
    svc.flow_control_capacity = 65534U;
    int actual_ret_smaller = resolve(); // [手順] - max_message_size (65535) より 1 小さい容量で resolve を呼び出す。
    svc.flow_control_capacity = 65535U;
    int actual_ret_equal = resolve(); // [手順] - max_message_size と同じ容量で resolve を呼び出す。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_smaller); // [確認_異常系] - 小さい容量で POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_OK, actual_ret_equal);                     // [確認_正常系] - 同じ容量で POTR_OK であること。
    EXPECT_EQ(65535U, cfg.capacity);                          // [確認_正常系] - 容量が 65535 であること。
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
