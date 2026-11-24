/**
 *******************************************************************************
 *  @file           threadRecvReportTest.cc
 *  @brief          受信報告の周期、ばらつきと暗号化を検証します。
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
#include "thread_recv_report.h"
#include <cstring>
#include <vector>
using namespace testing;

// 一対多の報告間隔が 100%〜150% の境界内に収まることの確認
TEST(threadRecvReportTest, randomized_intervals_include_bounds)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    uint64_t interval[4] = {};
    int actual_ret[4] = {};
    uint32_t random_value = 0;
    ctx.health_interval_ms = 100;
    ON_CALL(mock_cplat, cplat_random_bytes(_, _)).WillByDefault([&](void *out, size_t size) {
        std::memcpy(out, &random_value, size);
        return CPLAT_OK;
    }); // [状態] - 乱数の下限と上限を指定できるようにする。
    // Pre-Assert
    // Act
    ctx.service.type = POTR_TYPE_MULTICAST;
    actual_ret[0] = thread_recv_report_interval(&ctx, &interval[0]);
    random_value = 50;
    actual_ret[1] = thread_recv_report_interval(&ctx, &interval[1]);
    ctx.service.type = POTR_TYPE_BROADCAST;
    random_value = 0;
    actual_ret[2] = thread_recv_report_interval(&ctx, &interval[2]);
    random_value = 50;
    actual_ret[3] = thread_recv_report_interval(&ctx, &interval[3]);
    // [手順] - multicast と broadcast の下限と上限の間隔を取得する。
    // Assert
    for (size_t i = 0; i < 4; i++)
    {
        EXPECT_EQ(POTR_OK, actual_ret[i]); // [確認_正常系 回数=4] - 間隔の決定が成功すること。
        EXPECT_GE(interval[i], 100U); // [確認_正常系 回数=4] - 100% 以上であること。
        EXPECT_LE(interval[i], 150U); // [確認_正常系 回数=4] - 150% 以下であること。
    }
    EXPECT_EQ(100U, interval[0]); // [確認_正常系] - 下限 100% を含むこと。
    EXPECT_EQ(150U, interval[1]); // [確認_正常系] - 上限 150% を含むこと。
}

// RAW、監視無効、送信者では受信報告を送らないことの確認
TEST(threadRecvReportTest, disabled_and_raw_do_not_send)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.health_interval_ms = 100;
    int actual_ret[5] = {};
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_udp_sendto(_, _, _, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 対象外の構成で受信報告を送出しないこと。
    // Act
    ctx.service.type = POTR_TYPE_UNICAST_RAW;
    actual_ret[0] = thread_recv_report_tick(&ctx, 1000);
    ctx.service.type = POTR_TYPE_MULTICAST_RAW;
    actual_ret[1] = thread_recv_report_tick(&ctx, 1000);
    ctx.service.type = POTR_TYPE_BROADCAST_RAW;
    actual_ret[2] = thread_recv_report_tick(&ctx, 1000);
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.health_interval_ms = 0;
    actual_ret[3] = thread_recv_report_tick(&ctx, 1000);
    ctx.health_interval_ms = 100;
    ctx.role = POTR_ROLE_SENDER;
    actual_ret[4] = thread_recv_report_tick(&ctx, 1000);
    // [手順] - RAW 3 種、監視無効、送信者に対して周期処理を行う。
    // Assert
    for (size_t i = 0; i < 5; i++)
    {
        EXPECT_EQ(POTR_OK, actual_ret[i]); // [確認_正常系 回数=5] - 対象外の周期処理は成功として戻ること。
    }
    EXPECT_EQ(0U, ctx.next_report_ms); // [確認_正常系] - 報告の予定を作らないこと。
}

// 受信者の識別子と全経路状態を暗号化して予定時刻だけ送ることの確認
TEST(threadRecvReportTest, encrypted_report_at_each_deadline_on_all_paths)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    std::vector<std::vector<uint8_t>> emitted;
    std::vector<cplat_socket> paths;
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.service.encrypt_enabled = 1;
    ctx.service.src_port = 4000;
    ctx.service.service_id = 9;
    ctx.session_id = 123;
    ctx.session_ts.tv_sec = 10;
    ctx.session_ts.tv_nsec = 20;
    ctx.health_interval_ms = 100;
    ctx.n_path = 2;
    ctx.sock[0] = static_cast<cplat_socket>(7);
    ctx.sock[1] = static_cast<cplat_socket>(8);
    ctx.src_addr_resolved[0] = ctx.src_addr_resolved[1] = cplat_hton32(0x7f000001U);
    cplat_atomic_store_u8(&ctx.path_ping_state[0], POTR_PING_STATE_NORMAL, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_u8(&ctx.path_ping_state[1], POTR_PING_STATE_ABNORMAL, CPLAT_MEMORY_ORDER_RELAXED);
    potr_internal_tx_session_init(&ctx.tx_session);
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_udp_sendto(_, _, _, _, _, _)).Times(4)
        .WillRepeatedly([&](potr_context *c, int path_idx, const void *wire, size_t len,
                            const cplat_ipv4_endpoint *destination, cplat_error *) {
            const auto *bytes = static_cast<const uint8_t *>(wire);
            emitted.emplace_back(bytes, bytes + len);
            paths.push_back(c->sock[path_idx]);
            EXPECT_EQ(cplat_hton16(4000), destination->port);
            // [Pre-Assert確認_正常系 回数=4] - NACK と同じ送信元ポートを宛先とすること。
            return POTR_OK;
        });
    // [Pre-Assert確認_正常系] - 2 周期にそれぞれ 2 経路から計 4 回送ること。
    // [Pre-Assert手順] - 封緘したワイヤーと送出経路を保存する。
    // Act
    int actual_ret_start = thread_recv_report_tick(&ctx, 1000);
    int actual_ret_early = thread_recv_report_tick(&ctx, 1099);
    int actual_ret_first = thread_recv_report_tick(&ctx, 1100);
    int actual_ret_second = thread_recv_report_tick(&ctx, 1200);
    // [手順] - 起動時、期限直前、1 周期目と 2 周期目で報告処理を行う。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_start); // [確認_正常系] - 初回は予定時刻を設定すること。
    EXPECT_EQ(POTR_OK, actual_ret_early); // [確認_正常系] - 期限前の処理が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_first); // [確認_正常系] - 1 周期目に送出できること。
    EXPECT_EQ(POTR_OK, actual_ret_second); // [確認_正常系] - 2 周期目に送出できること。
    ASSERT_EQ(4U, emitted.size()); // [確認_正常系] - 全送出を捕捉できること。
    EXPECT_EQ((std::vector<cplat_socket>{7, 8, 7, 8}), paths); // [確認_正常系] - 毎回全経路へ送ること。
    EXPECT_EQ(1300U, ctx.next_report_ms); // [確認_正常系] - 次回は実効間隔後となること。
    for (size_t i = 0; i < 4; i++)
    {
        potr_packet packet = {};
        uint8_t plaintext[POTR_MAX_PATH] = {};
        ASSERT_EQ(POTR_OK, potr_internal_packet_parse(&packet, emitted[i].data(), emitted[i].size()));
        // [確認_正常系 回数=4] - REPORT のワイヤーを解析できること。
        EXPECT_EQ(POTR_FLAG_REPORT | POTR_FLAG_ENCRYPTED, packet.flags);
        // [確認_正常系 回数=4] - REPORT を暗号化していること。
        ASSERT_EQ(POTR_OK, potr_internal_packet_open(&packet, emitted[i].data(), ctx.service.encrypt_key,
                                                   plaintext, sizeof(plaintext)));
        // [確認_正常系 回数=4] - REPORT のペイロードを開封できること。
        EXPECT_EQ(123U, packet.session_id); // [確認_正常系 回数=4] - 受信者自身のセッション識別子を使うこと。
        EXPECT_EQ(0U, packet.seq_num); // [確認_正常系 回数=4] - 通番を 0 とすること。
        EXPECT_EQ(0U, packet.ack_num); // [確認_正常系 回数=4] - 確認通番を 0 とすること。
        EXPECT_EQ(POTR_MAX_PATH, packet.payload_len); // [確認_正常系 回数=4] - 全経路の状態を格納すること。
        EXPECT_EQ(POTR_PING_STATE_NORMAL, plaintext[0]); // [確認_正常系 回数=4] - 正常な経路を報告すること。
        EXPECT_EQ(POTR_PING_STATE_ABNORMAL, plaintext[1]); // [確認_正常系 回数=4] - 不達の経路を報告すること。
        EXPECT_EQ(i, packet.tx_counter); // [確認_正常系 回数=4] - 各周期と経路で送出通番を更新すること。
    }
}

namespace
{
// ワイヤーの位置 offset から、ネットワーク バイト オーダーの 32 ビット値を読む。
uint32_t ReadWire32(const std::vector<uint8_t> &wire, size_t offset)
{
    return ((uint32_t)wire[offset] << 24) | ((uint32_t)wire[offset + 1U] << 16) | ((uint32_t)wire[offset + 2U] << 8) |
           (uint32_t)wire[offset + 3U];
}
} // namespace

// multicast の受信者が、送出上限がある場合は seq_num を許可数の上限、ack_num を送出上限とし、ない場合は 0 とすることの確認
TEST(threadRecvReportTest, multicast_report_carries_send_limit)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    std::vector<std::vector<uint8_t>> emitted;
    int limit_held = 1;
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.service.src_port = 4000;
    ctx.service.service_id = 9;
    ctx.session_id = 123;
    ctx.health_interval_ms = 100;
    ctx.n_path = 1;
    ctx.sock[0] = static_cast<cplat_socket>(7);
    ctx.src_addr_resolved[0] = cplat_hton32(0x7f000001U);
    potr_internal_tx_session_init(&ctx.tx_session);
    // [状態] - 暗号化しない multicast の受信者で、経路 1 本とする。
    ON_CALL(mock_cplat, cplat_random_bytes(_, _)).WillByDefault([](void *out, size_t size) {
        std::memset(out, 0, size);
        return CPLAT_OK;
    }); // [状態] - 報告間隔のばらつきを 0 にする。
    ON_CALL(mock_porter, potr_internal_flow_control_report_limit(_, _, _))
        .WillByDefault([&](potr_context *, uint32_t *send_limit, uint32_t *max_allowance) {
            *send_limit = limit_held != 0 ? 0x12345678U : 0U;
            *max_allowance = limit_held != 0 ? 42U : 0U;
            return limit_held;
        }); // [状態] - limit_held が 1 の間は送出上限 0x12345678 と許可数の上限 42 を返し、0 の間は送出上限がないことを返す。
    ON_CALL(mock_porter, potr_internal_udp_sendto(_, _, _, _, _, _))
        .WillByDefault([&](potr_context *, int, const void *wire, size_t len, const cplat_ipv4_endpoint *,
                           cplat_error *) {
            const auto *bytes = static_cast<const uint8_t *>(wire);
            emitted.emplace_back(bytes, bytes + len);
            return POTR_OK;
        }); // [状態] - 送出したワイヤーを保存する。
    // Pre-Assert
    // Act
    int actual_ret_start = thread_recv_report_tick(&ctx, 1000); // [手順] - 起動時の周期処理で、報告の予定を作る。
    int actual_ret_held = thread_recv_report_tick(&ctx, 1100);  // [手順] - 送出上限がある状態で、1 周期目の報告を送る。
    limit_held = 0;
    int actual_ret_none = thread_recv_report_tick(&ctx, 1200);  // [手順] - 送出上限がない状態で、2 周期目の報告を送る。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_start); // [確認_正常系] - 起動時の周期処理が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_held);  // [確認_正常系] - 送出上限がある報告を送れること。
    EXPECT_EQ(POTR_OK, actual_ret_none);  // [確認_正常系] - 送出上限がない報告を送れること。
    ASSERT_EQ(2U, emitted.size());        // [確認_正常系] - 2 回送出すること。
    EXPECT_EQ(42U, ReadWire32(emitted[0], 24U));          // [確認_正常系] - 1 回目の seq_num が許可数の上限 (42) であること。
    EXPECT_EQ(0x12345678U, ReadWire32(emitted[0], 28U));  // [確認_正常系] - 1 回目の ack_num が送出上限であること。
    EXPECT_EQ(0U, ReadWire32(emitted[1], 24U));           // [確認_正常系] - 2 回目の seq_num が 0 (送出上限なし) であること。
    EXPECT_EQ(0U, ReadWire32(emitted[1], 28U));           // [確認_正常系] - 2 回目の ack_num が 0 であること。
    EXPECT_EQ(emitted[0].size(), emitted[1].size());      // [確認_正常系] - 報告の大きさが送出上限の有無で変わらないこと。
}

// unicast の受信者も、送出上限がある場合は seq_num を許可数の上限、ack_num を送出上限とし、ない場合は 0 とすることの確認
TEST(threadRecvReportTest, unicast_report_carries_send_limit)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    std::vector<std::vector<uint8_t>> emitted;
    int limit_held = 1;
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.service.src_port = 4000;
    ctx.service.service_id = 9;
    ctx.health_interval_ms = 100;
    ctx.n_path = 1;
    ctx.sock[0] = static_cast<cplat_socket>(7);
    ctx.src_addr_resolved[0] = cplat_hton32(0x7f000001U);
    potr_internal_tx_session_init(&ctx.tx_session);
    // [状態] - 暗号化しない unicast の受信者で、経路 1 本とする。
    ON_CALL(mock_porter, potr_internal_flow_control_report_limit(_, _, _))
        .WillByDefault([&](potr_context *, uint32_t *send_limit, uint32_t *max_allowance) {
            *send_limit = limit_held != 0 ? 0x12345678U : 0U;
            *max_allowance = limit_held != 0 ? 208U : 0U;
            return limit_held;
        }); // [状態] - limit_held が 1 の間は送出上限 0x12345678 と許可数の上限 208 を返し、0 の間は送出上限がないことを返す。
    ON_CALL(mock_porter, potr_internal_udp_sendto(_, _, _, _, _, _))
        .WillByDefault([&](potr_context *, int, const void *wire, size_t len, const cplat_ipv4_endpoint *,
                           cplat_error *) {
            const auto *bytes = static_cast<const uint8_t *>(wire);
            emitted.emplace_back(bytes, bytes + len);
            return POTR_OK;
        }); // [状態] - 送出したワイヤーを保存する。
    // Pre-Assert
    // Act
    int actual_ret_start = thread_recv_report_tick(&ctx, 1000); // [手順] - 起動時の周期処理で、報告の予定を作る。
    int actual_ret_held = thread_recv_report_tick(&ctx, 1100);  // [手順] - 送出上限がある状態で、1 周期目の報告を送る。
    limit_held = 0;
    int actual_ret_none = thread_recv_report_tick(&ctx, 1200);  // [手順] - 送出上限がない状態で、2 周期目の報告を送る。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_start); // [確認_正常系] - 起動時の周期処理が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_held);  // [確認_正常系] - 送出上限がある報告を送れること。
    EXPECT_EQ(POTR_OK, actual_ret_none);  // [確認_正常系] - 送出上限がない報告を送れること。
    ASSERT_EQ(2U, emitted.size());        // [確認_正常系] - 2 回送出すること。
    EXPECT_EQ(208U, ReadWire32(emitted[0], 24U));         // [確認_正常系] - 1 回目の seq_num が許可数の上限 (208) であること。
    EXPECT_EQ(0x12345678U, ReadWire32(emitted[0], 28U));  // [確認_正常系] - 1 回目の ack_num が送出上限であること。
    EXPECT_EQ(0U, ReadWire32(emitted[1], 24U));           // [確認_正常系] - 2 回目の seq_num が 0 (送出上限なし) であること。
    EXPECT_EQ(0U, ReadWire32(emitted[1], 28U));           // [確認_正常系] - 2 回目の ack_num が 0 であること。
    EXPECT_EQ(emitted[0].size(), emitted[1].size());      // [確認_正常系] - 報告の大きさが送出上限の有無で変わらないこと。
}

// 連続 DATA が「幅 ÷ 2」個に達したときだけ検出され、ソケットが空になると 0 に戻ることの確認
TEST(threadRecvReportTest, ad_hoc_report_lag_threshold_and_reset_on_empty)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int sendto_count = 0;
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.service.src_port = 4000;
    ctx.service.service_id = 9;
    ctx.health_interval_ms = 1000;
    ctx.flow_control_config.request_interval_ms = 200;
    ctx.flow_receive_buffer_width = 10U;
    ctx.n_path = 1;
    ctx.sock[0] = static_cast<cplat_socket>(7);
    ctx.src_addr_resolved[0] = cplat_hton32(0x7f000001U);
    potr_internal_tx_session_init(&ctx.tx_session);
    // [状態] - multicast 受信者で受信バッファーの幅を 10 (しきい値 5) とする。
    ON_CALL(mock_porter, potr_internal_udp_sendto(_, _, _, _, _, _))
        .WillByDefault([&](potr_context *, int, const void *, size_t, const cplat_ipv4_endpoint *,
                           cplat_error *) {
            sendto_count++;
            return POTR_OK;
        }); // [状態] - 送出回数を数える。
    // Pre-Assert
    // Act
    int actual_ret = POTR_OK;
    for (int i = 0; i < 4; i++)
    {
        actual_ret = thread_recv_report_on_data_accepted(&ctx, 1000U);
    }
    uint32_t consecutive_before_reset = ctx.recv_report_consecutive_data;
    uint64_t detected_before_reset = ctx.recv_report_lag_detected;
    thread_recv_report_reset_consecutive(&ctx);
    uint32_t consecutive_after_reset = ctx.recv_report_consecutive_data;
    for (int i = 0; i < 4; i++)
    {
        actual_ret = thread_recv_report_on_data_accepted(&ctx, 1000U);
    }
    uint64_t detected_at_four = ctx.recv_report_lag_detected;
    actual_ret = thread_recv_report_on_data_accepted(&ctx, 1000U);
    // [手順] - 4 回連続で処理した後に空として 0 に戻し、さらに 5 回連続で処理する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);          // [確認_正常系] - 判定処理が成功すること。
    EXPECT_EQ(4U, consecutive_before_reset); // [確認_正常系] - リセット前の連続数が 4 であること。
    EXPECT_EQ(0U, detected_before_reset);    // [確認_正常系] - しきい値未満では検出しないこと。
    EXPECT_EQ(0U, consecutive_after_reset);  // [確認_正常系] - ソケットが空になると連続数が 0 に戻ること。
    EXPECT_EQ(0U, detected_at_four);         // [確認_正常系] - リセット後 4 回ではまだ検出しないこと。
    EXPECT_EQ(1U, ctx.recv_report_lag_detected); // [確認_正常系] - 5 回目で 1 回検出すること。
    EXPECT_EQ(1U, ctx.recv_report_lag_sent);     // [確認_正常系] - 臨時の受信報告を 1 回送出すること。
    EXPECT_EQ(1, sendto_count);                  // [確認_正常系] - 実際に 1 回送出すること。
    EXPECT_EQ(0U, ctx.recv_report_consecutive_data); // [確認_正常系] - 検出後に連続数が 0 に戻ること。
    EXPECT_EQ(9U, ctx.recv_report_accepted_data);    // [確認_正常系] - 処理した DATA の総数が 9 であること。
}

// 待機の所要時間がしきい値未満かどうかを秒の繰り上がりを含めて判定することの確認
TEST(threadRecvReportTest, ad_hoc_report_wait_was_immediate_boundary)
{
    // Arrange
    cplat_timespec start = {10, 0};
    cplat_timespec below = {10, POTR_RECV_POLL_IMMEDIATE_THRESHOLD_NS - 1LL};
    cplat_timespec exact = {10, POTR_RECV_POLL_IMMEDIATE_THRESHOLD_NS};
    cplat_timespec wrap_start = {10, POTR_NSEC_PER_SEC - 10000LL};
    cplat_timespec wrap_below = {11, 10000LL};
    cplat_timespec wrap_above = {11, POTR_RECV_POLL_IMMEDIATE_THRESHOLD_NS};
    cplat_timespec far_after = {13, 0};
    // Pre-Assert
    // Act
    int actual_zero = thread_recv_report_wait_was_immediate(&start, &start);
    int actual_below = thread_recv_report_wait_was_immediate(&start, &below);
    int actual_exact = thread_recv_report_wait_was_immediate(&start, &exact);
    int actual_wrap_below = thread_recv_report_wait_was_immediate(&wrap_start, &wrap_below);
    int actual_wrap_above = thread_recv_report_wait_was_immediate(&wrap_start, &wrap_above);
    int actual_far = thread_recv_report_wait_was_immediate(&start, &far_after);
    int actual_null = thread_recv_report_wait_was_immediate(nullptr, &start);
    // [手順] - しきい値未満、しきい値ちょうど、秒の繰り上がり前後、複数秒差、NULL で判定する。
    // Assert
    EXPECT_EQ(1, actual_zero);       // [確認_正常系] - 差 0 は待たずに返ったと判定すること。
    EXPECT_EQ(1, actual_below);      // [確認_正常系] - しきい値未満は待たずに返ったと判定すること。
    EXPECT_EQ(0, actual_exact);      // [確認_正常系] - しきい値ちょうどは待ったと判定すること。
    EXPECT_EQ(1, actual_wrap_below); // [確認_正常系] - 秒をまたいでもしきい値未満なら待たずに返ったと判定すること。
    EXPECT_EQ(0, actual_wrap_above); // [確認_正常系] - 秒をまたいでしきい値以上なら待ったと判定すること。
    EXPECT_EQ(0, actual_far);        // [確認_正常系] - 2 秒以上の差は待ったと判定すること。
    EXPECT_EQ(0, actual_null);       // [確認_異常系] - NULL は 0 を返すこと。
}

// 幅がない構成、監視対象の受信者、対象外の通信種別では臨時の受信報告を送出しないことの確認
TEST(threadRecvReportTest, ad_hoc_report_skipped_without_width_or_when_monitored)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.service.src_port = 4000;
    ctx.service.service_id = 9;
    ctx.health_interval_ms = 1000;
    ctx.flow_control_config.request_interval_ms = 200;
    ctx.n_path = 1;
    ctx.sock[0] = static_cast<cplat_socket>(7);
    ctx.src_addr_resolved[0] = cplat_hton32(0x7f000001U);
    potr_internal_tx_session_init(&ctx.tx_session);
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_udp_sendto(_, _, _, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 幅がない構成、監視対象、unicast、報告無効では一切送出しないこと。
    // Act
    ctx.flow_receive_buffer_width = 0U;
    for (int i = 0; i < 10; i++)
    {
        EXPECT_EQ(POTR_OK, thread_recv_report_on_data_accepted(&ctx, 1000U));
        // [確認_正常系 回数=10] - 幅がない構成での処理が成功すること。
    }
    ctx.flow_receive_buffer_width = 6U;
    ctx.flow_monitored = 1;
    EXPECT_EQ(POTR_OK, thread_recv_report_on_adopted(&ctx)); // [確認_正常系] - 監視対象の採用時処理が成功すること。
    for (int i = 0; i < 10; i++)
    {
        EXPECT_EQ(POTR_OK, thread_recv_report_on_data_accepted(&ctx, 2000U));
        // [確認_正常系 回数=10] - 監視対象での処理が成功すること。
    }
    ctx.flow_monitored = 0;
    ctx.service.type = POTR_TYPE_UNICAST;
    EXPECT_EQ(POTR_OK, thread_recv_report_on_adopted(&ctx)); // [確認_正常系] - unicast の採用時処理が成功すること。
    for (int i = 0; i < 6; i++)
    {
        EXPECT_EQ(POTR_OK, thread_recv_report_on_data_accepted(&ctx, 3000U));
        // [確認_正常系 回数=6] - unicast での処理が成功すること。
    }
    ctx.service.type = POTR_TYPE_BROADCAST;
    ctx.health_interval_ms = 0U;
    EXPECT_EQ(POTR_OK, thread_recv_report_on_adopted(&ctx)); // [確認_正常系] - 報告無効の採用時処理が成功すること。
    for (int i = 0; i < 6; i++)
    {
        EXPECT_EQ(POTR_OK, thread_recv_report_on_data_accepted(&ctx, 4000U));
        // [確認_正常系 回数=6] - 報告無効での処理が成功すること。
    }
    // [手順] - 幅 0、監視対象、unicast、受信報告無効の各構成で採用と連続 DATA を処理する。
    // Assert
    EXPECT_EQ(0U, ctx.recv_report_lag_detected); // [確認_正常系] - 検出回数が 0 であること。
    EXPECT_EQ(0U, ctx.recv_report_lag_sent);     // [確認_正常系] - 送出回数が 0 であること。
}

// 間隔の制限で送出が抑えられて検出回数だけが増えることと、採用時の送出が制限の対象外であることの確認
TEST(threadRecvReportTest, ad_hoc_report_interval_limit_and_adoption_exempt)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int sendto_count = 0;
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.service.type = POTR_TYPE_BROADCAST;
    ctx.service.src_port = 4000;
    ctx.service.service_id = 9;
    ctx.health_interval_ms = 1000;
    ctx.flow_control_config.request_interval_ms = 200;
    ctx.flow_receive_buffer_width = 4U;
    ctx.n_path = 1;
    ctx.sock[0] = static_cast<cplat_socket>(7);
    ctx.src_addr_resolved[0] = cplat_hton32(0x7f000001U);
    potr_internal_tx_session_init(&ctx.tx_session);
    // [状態] - broadcast 受信者で幅 4 (しきい値 2)、制限間隔 200 [ms] とする。
    ON_CALL(mock_porter, potr_internal_udp_sendto(_, _, _, _, _, _))
        .WillByDefault([&](potr_context *, int, const void *, size_t, const cplat_ipv4_endpoint *,
                           cplat_error *) {
            sendto_count++;
            return POTR_OK;
        }); // [状態] - 送出回数を数える。
    // Pre-Assert
    // Act
    int actual_ret_adopt1 = thread_recv_report_on_adopted(&ctx);
    int sent_after_adopt1 = sendto_count;
    // [手順] - 最初の送信者採用で臨時の受信報告を送出する。
    int actual_ret_lag1 = thread_recv_report_on_data_accepted(&ctx, 1000U);
    actual_ret_lag1 = thread_recv_report_on_data_accepted(&ctx, 1000U);
    int sent_after_lag1 = sendto_count;
    // [手順] - 採用直後の時刻 1000 でしきい値に達する DATA を処理する。
    int actual_ret_lag2 = thread_recv_report_on_data_accepted(&ctx, 1199U);
    actual_ret_lag2 = thread_recv_report_on_data_accepted(&ctx, 1199U);
    int sent_after_lag2 = sendto_count;
    // [手順] - 制限間隔未満の時刻 1199 で再びしきい値に達する DATA を処理する。
    int actual_ret_adopt2 = thread_recv_report_on_adopted(&ctx);
    int sent_after_adopt2 = sendto_count;
    // [手順] - 制限間隔内に新しい送信者を採用する。
    int actual_ret_lag3 = thread_recv_report_on_data_accepted(&ctx, 1200U);
    actual_ret_lag3 = thread_recv_report_on_data_accepted(&ctx, 1200U);
    int sent_after_lag3 = sendto_count;
    // [手順] - 前回の遅れ送出からちょうど 200 [ms] 後の時刻 1200 でしきい値に達する DATA を処理する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_adopt1); // [確認_正常系] - 1 回目の採用時送出が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_lag1);   // [確認_正常系] - 1 回目の遅れ検出が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_lag2);   // [確認_正常系] - 制限間隔内の遅れ検出が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_adopt2); // [確認_正常系] - 制限間隔内の採用時送出が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_lag3);   // [確認_正常系] - 制限間隔経過後の遅れ検出が成功すること。
    EXPECT_EQ(1, sent_after_adopt1);       // [確認_正常系] - 採用時に 1 回送出すること。
    EXPECT_EQ(2, sent_after_lag1);         // [確認_正常系] - 採用時の送出は遅れ送出の間隔制限に影響しないこと。
    EXPECT_EQ(2, sent_after_lag2);         // [確認_正常系] - 間隔未満では送出しないこと。
    EXPECT_EQ(3, sent_after_adopt2);       // [確認_正常系] - 制限間隔内でも採用時は制限の対象外として送出すること。
    EXPECT_EQ(4, sent_after_lag3);         // [確認_正常系] - 制限間隔ちょうどで再び遅れ送出すること。
    EXPECT_EQ(3U, ctx.recv_report_lag_detected); // [確認_正常系] - 検出回数が 3 であること。
    EXPECT_EQ(2U, ctx.recv_report_lag_sent);     // [確認_正常系] - 遅れによる送出回数が 2 であること。
}

// 臨時の受信報告が周期の受信報告と同じ形式であり、周期の予定時刻を変更しないことの確認
TEST(threadRecvReportTest, ad_hoc_report_matches_periodic_format_and_preserves_next_report_ms)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    std::vector<std::vector<uint8_t>> emitted;
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.service.src_port = 4000;
    ctx.service.service_id = 9;
    ctx.session_id = 321;
    ctx.session_ts.tv_sec = 50;
    ctx.session_ts.tv_nsec = 60;
    ctx.health_interval_ms = 500;
    ctx.flow_control_config.request_interval_ms = 200;
    ctx.flow_receive_buffer_width = 4U;
    ctx.n_path = 1;
    ctx.sock[0] = static_cast<cplat_socket>(7);
    ctx.src_addr_resolved[0] = cplat_hton32(0x7f000001U);
    cplat_atomic_store_u8(&ctx.path_ping_state[0], POTR_PING_STATE_NORMAL, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_u8(&ctx.path_ping_state[1], POTR_PING_STATE_ABNORMAL, CPLAT_MEMORY_ORDER_RELAXED);
    potr_internal_tx_session_init(&ctx.tx_session);
    // [状態] - multicast 受信者で送出上限と経路状態を設定する。
    ON_CALL(mock_cplat, cplat_random_bytes(_, _)).WillByDefault([](void *out, size_t size) {
        std::memset(out, 0, size);
        return CPLAT_OK;
    }); // [状態] - 周期報告のばらつきを 0 にする。
    ON_CALL(mock_porter, potr_internal_flow_control_report_limit(_, _, _))
        .WillByDefault([](potr_context *, uint32_t *send_limit, uint32_t *max_allowance) {
            *send_limit = 0x11223344U;
            *max_allowance = 84U;
            return 1;
        }); // [状態] - 送出上限 0x11223344 と許可数の上限 84 を返す。
    ON_CALL(mock_porter, potr_internal_udp_sendto(_, _, _, _, _, _))
        .WillByDefault([&](potr_context *, int, const void *wire, size_t len, const cplat_ipv4_endpoint *,
                           cplat_error *) {
            const auto *bytes = static_cast<const uint8_t *>(wire);
            emitted.emplace_back(bytes, bytes + len);
            return POTR_OK;
        }); // [状態] - 送出したワイヤーを保存する。
    // Pre-Assert
    // Act
    int actual_ret_init = thread_recv_report_tick(&ctx, 1000U);
    uint64_t next_after_init = ctx.next_report_ms;
    int actual_ret_adopt = thread_recv_report_on_adopted(&ctx);
    uint64_t next_after_adopt = ctx.next_report_ms;
    int actual_ret_lag = thread_recv_report_on_data_accepted(&ctx, 1100U);
    actual_ret_lag = thread_recv_report_on_data_accepted(&ctx, 1100U);
    uint64_t next_after_lag = ctx.next_report_ms;
    int actual_ret_periodic = thread_recv_report_tick(&ctx, 1500U);
    // [手順] - 周期予定の確立後、採用時・遅れ検出時の臨時送出と周期送出を行う。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_init);     // [確認_正常系] - 予定確立が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_adopt);    // [確認_正常系] - 採用時送出が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_lag);      // [確認_正常系] - 遅れ送出が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_periodic); // [確認_正常系] - 周期送出が成功すること。
    EXPECT_EQ(1500U, next_after_init);       // [確認_正常系] - 初期予定時刻が 1500 であること。
    EXPECT_EQ(1500U, next_after_adopt);      // [確認_正常系] - 採用時送出で予定時刻が変わらないこと。
    EXPECT_EQ(1500U, next_after_lag);        // [確認_正常系] - 遅れ送出で予定時刻が変わらないこと。
    ASSERT_EQ(3U, emitted.size());           // [確認_正常系] - 臨時 2 回と周期 1 回の計 3 回送出すること。
    for (size_t i = 0; i < 3U; i++)
    {
        potr_packet packet = {};
        ASSERT_EQ(POTR_OK, potr_internal_packet_parse(&packet, emitted[i].data(), emitted[i].size()));
        // [確認_正常系 回数=3] - すべての受信報告を解析できること。
        EXPECT_EQ(POTR_FLAG_REPORT, packet.flags);     // [確認_正常系 回数=3] - 種別が REPORT であること。
        EXPECT_EQ(321U, packet.session_id);            // [確認_正常系 回数=3] - セッション識別子が一致すること。
        EXPECT_EQ(84U, packet.seq_num);                // [確認_正常系 回数=3] - 許可数の上限 (84) が一致すること。
        EXPECT_EQ(0x11223344U, packet.ack_num);        // [確認_正常系 回数=3] - 送出上限の値が一致すること。
        EXPECT_EQ(POTR_MAX_PATH, packet.payload_len);  // [確認_正常系 回数=3] - ペイロード長が一致すること。
        EXPECT_EQ(POTR_PING_STATE_NORMAL, packet.payload[0]);   // [確認_正常系 回数=3] - 経路 0 の状態が一致すること。
        EXPECT_EQ(POTR_PING_STATE_ABNORMAL, packet.payload[1]); // [確認_正常系 回数=3] - 経路 1 の状態が一致すること。
        EXPECT_EQ(emitted[2].size(), emitted[i].size());        // [確認_正常系 回数=3] - 周期報告とワイヤー長が一致すること。
    }
}
