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
    potr_context ctx = {};
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.health_interval_ms = 100;
    int actual_ret[5] = {};
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_sendto(_, _, _, _, _, _)).Times(0);
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
    EXPECT_CALL(mock_cplat, cplat_socket_sendto(_, _, _, _, _, _)).Times(4)
        .WillRepeatedly([&](cplat_socket fd, const void *wire, size_t len,
                            const cplat_ipv4_endpoint *destination, size_t *sent, cplat_error *) {
            const auto *bytes = static_cast<const uint8_t *>(wire);
            emitted.emplace_back(bytes, bytes + len);
            paths.push_back(fd);
            EXPECT_EQ(cplat_hton16(4000), destination->port);
            // [Pre-Assert確認_正常系 回数=4] - NACK と同じ送信元ポートを宛先とすること。
            *sent = len;
            return CPLAT_OK;
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

// multicast の受信者が、送出上限がある場合は seq_num を 1、ack_num を送出上限とし、ない場合は 0 とすることの確認
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
    ON_CALL(mock_porter, potr_internal_flow_control_report_limit(_, _))
        .WillByDefault([&](potr_context *, uint32_t *send_limit) {
            *send_limit = limit_held != 0 ? 0x12345678U : 0U;
            return limit_held;
        }); // [状態] - limit_held が 1 の間は送出上限 0x12345678 を返し、0 の間は送出上限がないことを返す。
    ON_CALL(mock_cplat, cplat_socket_sendto(_, _, _, _, _, _))
        .WillByDefault([&](cplat_socket, const void *wire, size_t len, const cplat_ipv4_endpoint *, size_t *sent,
                           cplat_error *) {
            const auto *bytes = static_cast<const uint8_t *>(wire);
            emitted.emplace_back(bytes, bytes + len);
            *sent = len;
            return CPLAT_OK;
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
    EXPECT_EQ(1U, ReadWire32(emitted[0], 24U));           // [確認_正常系] - 1 回目の seq_num が 1 (送出上限あり) であること。
    EXPECT_EQ(0x12345678U, ReadWire32(emitted[0], 28U));  // [確認_正常系] - 1 回目の ack_num が送出上限であること。
    EXPECT_EQ(0U, ReadWire32(emitted[1], 24U));           // [確認_正常系] - 2 回目の seq_num が 0 (送出上限なし) であること。
    EXPECT_EQ(0U, ReadWire32(emitted[1], 28U));           // [確認_正常系] - 2 回目の ack_num が 0 であること。
    EXPECT_EQ(emitted[0].size(), emitted[1].size());      // [確認_正常系] - 報告の大きさが送出上限の有無で変わらないこと。
}

// unicast の受信者は、受信報告に送出上限を載せないことの確認
TEST(threadRecvReportTest, unicast_report_does_not_carry_send_limit)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    std::vector<std::vector<uint8_t>> emitted;
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
    ON_CALL(mock_cplat, cplat_socket_sendto(_, _, _, _, _, _))
        .WillByDefault([&](cplat_socket, const void *wire, size_t len, const cplat_ipv4_endpoint *, size_t *sent,
                           cplat_error *) {
            const auto *bytes = static_cast<const uint8_t *>(wire);
            emitted.emplace_back(bytes, bytes + len);
            *sent = len;
            return CPLAT_OK;
        }); // [状態] - 送出したワイヤーを保存する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_report_limit(_, _))
        .Times(0); // [Pre-Assert確認_正常系] - potr_internal_flow_control_report_limit() が呼び出されないこと。
    // Act
    int actual_ret_start = thread_recv_report_tick(&ctx, 1000); // [手順] - 起動時の周期処理で、報告の予定を作る。
    int actual_ret_first = thread_recv_report_tick(&ctx, 1100); // [手順] - 1 周期目の報告を送る。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_start); // [確認_正常系] - 起動時の周期処理が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_first); // [確認_正常系] - 報告を送れること。
    ASSERT_EQ(1U, emitted.size());        // [確認_正常系] - 1 回送出すること。
    EXPECT_EQ(0U, ReadWire32(emitted[0], 24U)); // [確認_正常系] - seq_num が 0 であること。
    EXPECT_EQ(0U, ReadWire32(emitted[0], 28U)); // [確認_正常系] - ack_num が 0 であること。
}
