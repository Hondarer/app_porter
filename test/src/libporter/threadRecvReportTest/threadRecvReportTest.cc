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
