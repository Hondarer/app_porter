/**
 *******************************************************************************
 *  @file           receiverReportTest.cc
 *  @brief          受信報告の登録、期限、上限とリプレイ記録を検証します。
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
#include <porter/protocol/receiver_report.h>
#include <cstring>
#include <cstdlib>
using namespace testing;

// 受信者ごとにグループの経路の和を取り、全受信者の到達を要求することの確認
TEST(receiverReportTest, register_update_and_require_every_receiver)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_packet packet = {};
    uint8_t states[POTR_MAX_PATH] = {};
    uint8_t wire[POTR_PACKET_HEADER_SIZE] = {};
    uint64_t now_ms = 1000;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.service.max_peers = 2;
    ctx.service.path_group[0] = 1;
    ctx.service.path_group[1] = 1;
    ctx.service.path_group[2] = 3;
    ctx.n_path = 3;
    ctx.health_timeout_ms = 100;
    packet.flags = POTR_FLAG_REPORT;
    packet.payload = states;
    packet.payload_len = POTR_MAX_PATH;
    packet.session_id = 1;
    states[0] = POTR_PING_STATE_NORMAL;
    states[2] = POTR_PING_STATE_NORMAL;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 報告の到着時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    uint8_t groups_before = 0;
    uint8_t groups_after = 0;
    // Pre-Assert
    // Act
    int actual_ret_first = potr_internal_receiver_accept(&ctx, &packet, wire);
    groups_before = potr_internal_receiver_groups_locked(&ctx, now_ms);
    packet.session_id = 2;
    states[0] = POTR_PING_STATE_ABNORMAL;
    states[1] = POTR_PING_STATE_NORMAL;
    int actual_ret_second = potr_internal_receiver_accept(&ctx, &packet, wire);
    uint8_t groups_with_distinct_paths = potr_internal_receiver_groups_locked(&ctx, now_ms);
    states[1] = POTR_PING_STATE_ABNORMAL;
    now_ms++;
    int actual_ret_update = potr_internal_receiver_accept(&ctx, &packet, wire);
    groups_after = potr_internal_receiver_groups_locked(&ctx, now_ms);
    // [手順] - 2 受信者を登録し、片方だけをグループ 1 不達へ更新する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_first); // [確認_正常系] - 1 者目を登録できること。
    EXPECT_EQ(POTR_OK, actual_ret_second); // [確認_正常系] - 別経路で同じグループに届く 2 者目を登録できること。
    EXPECT_EQ(POTR_OK, actual_ret_update); // [確認_正常系] - 既存受信者の報告を更新できること。
    EXPECT_EQ(5U, groups_before); // [確認_正常系] - グループ 1 と 3 が到達可能であること。
    EXPECT_EQ(5U, groups_with_distinct_paths);
    // [確認_正常系] - 各受信者が異なる経路で届いても同じグループを到達可能とすること。
    EXPECT_EQ(4U, groups_after); // [確認_正常系] - 1 者だけが不達になったグループを除外すること。
    EXPECT_EQ(1, ctx.report_received); // [確認_正常系] - 報告の受信済み状態を保持すること。
    // Cleanup
    while (ctx.receivers != nullptr)
    {
        auto *receiver = ctx.receivers;
        ctx.receivers = receiver->next;
        std::free(receiver);
    }
}

// 上限では新規を破棄し、離脱した最古エントリを再利用することの確認
TEST(receiverReportTest, limit_expiry_and_oldest_entry_reuse)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_packet packet = {};
    uint8_t states[POTR_MAX_PATH] = {};
    uint8_t wire[POTR_PACKET_HEADER_SIZE] = {};
    uint64_t now_ms = 1000;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.service.max_peers = 2;
    ctx.service.path_group[0] = 1;
    ctx.service.path_group[1] = 1;
    ctx.service.path_group[2] = 3;
    ctx.n_path = 3;
    ctx.health_timeout_ms = 100;
    packet.flags = POTR_FLAG_REPORT;
    packet.payload = states;
    packet.payload_len = POTR_MAX_PATH;
    packet.session_id = 1;
    states[0] = POTR_PING_STATE_NORMAL;
    states[2] = POTR_PING_STATE_NORMAL;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 報告の到着時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    potr_internal_receiver *first = nullptr;
    potr_internal_receiver *second = nullptr;
    // Pre-Assert
    // Act
    int actual_ret_first = potr_internal_receiver_accept(&ctx, &packet, wire);
    first = ctx.receivers;
    packet.session_id = 2;
    now_ms += 50;
    int actual_ret_second = potr_internal_receiver_accept(&ctx, &packet, wire);
    second = ctx.receivers;
    packet.session_id = 3;
    int actual_ret_full = potr_internal_receiver_accept(&ctx, &packet, wire);
    now_ms = 1100;
    uint8_t groups_after_expiry = potr_internal_receiver_groups_locked(&ctx, now_ms);
    int actual_ret_reuse = potr_internal_receiver_accept(&ctx, &packet, wire);
    // [手順] - 上限まで登録し、上限超過、先頭登録者の期限切れ、新規登録を順に処理する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_first); // [確認_正常系] - 1 者目を受理すること。
    EXPECT_EQ(POTR_OK, actual_ret_second); // [確認_正常系] - 2 者目を受理すること。
    EXPECT_EQ(POTR_ERR_FULL, actual_ret_full); // [確認_異常系] - 全員が報告中なら上限超過を破棄すること。
    EXPECT_EQ(5U, groups_after_expiry); // [確認_正常系] - 期限内の受信者だけで判定すること。
    EXPECT_EQ(POTR_OK, actual_ret_reuse); // [確認_正常系] - 離脱済みの領域を再利用できること。
    ASSERT_NE(nullptr, first); // [確認_正常系] - 再利用対象が存在すること。
    EXPECT_EQ(3U, first->session_id); // [確認_正常系] - 最古の離脱済みエントリを再利用すること。
    EXPECT_EQ(second, ctx.receivers); // [確認_正常系] - 期限内のエントリを維持すること。
    EXPECT_EQ(first, second->next); // [確認_正常系] - 管理件数を増やさないこと。
    // Cleanup
    while (ctx.receivers != nullptr)
    {
        auto *receiver = ctx.receivers;
        ctx.receivers = receiver->next;
        std::free(receiver);
    }
}

// 離脱者を到達判定から外し、同じセッションの再送報告を破棄することの確認
TEST(receiverReportTest, departed_receiver_excluded_but_replay_retained)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_packet packet = {};
    uint8_t states[POTR_MAX_PATH] = {};
    uint8_t wire[POTR_PACKET_HEADER_SIZE] = {};
    uint64_t now_ms = 1000;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.service.max_peers = 2;
    ctx.service.path_group[0] = 1;
    ctx.service.path_group[1] = 1;
    ctx.service.path_group[2] = 3;
    ctx.n_path = 3;
    ctx.health_timeout_ms = 100;
    packet.flags = POTR_FLAG_REPORT;
    packet.payload = states;
    packet.payload_len = POTR_MAX_PATH;
    packet.session_id = 1;
    states[0] = POTR_PING_STATE_NORMAL;
    states[2] = POTR_PING_STATE_NORMAL;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 報告の到着時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    uint16_t encrypted = cplat_hton16(POTR_FLAG_ENCRYPTED | POTR_FLAG_REPORT);
    std::memcpy(wire + 32, &encrypted, sizeof(encrypted));
    packet.tx_counter = 10;
    // Pre-Assert
    // Act
    int actual_ret_first = potr_internal_receiver_accept(&ctx, &packet, wire);
    packet.session_id = 2;
    states[0] = POTR_PING_STATE_ABNORMAL;
    now_ms += 10;
    int actual_ret_second = potr_internal_receiver_accept(&ctx, &packet, wire);
    packet.session_id = 1;
    states[0] = POTR_PING_STATE_NORMAL;
    packet.tx_counter++;
    now_ms = 1090;
    int actual_ret_refresh = potr_internal_receiver_accept(&ctx, &packet, wire);
    uint8_t groups_with_both = potr_internal_receiver_groups_locked(&ctx, now_ms);
    now_ms = 1110;
    uint8_t groups_without_departed = potr_internal_receiver_groups_locked(&ctx, now_ms);
    packet.session_id = 2;
    packet.tx_counter = 10;
    int actual_ret_replay = potr_internal_receiver_accept(&ctx, &packet, wire);
    // [手順] - 一方を更新し、他方の期限切れ後に同じ通番の報告を再受信する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_first); // [確認_正常系] - 最初の認証済み報告を受理すること。
    EXPECT_EQ(POTR_OK, actual_ret_second); // [確認_正常系] - 別受信者の同じ通番を受理すること。
    EXPECT_EQ(POTR_OK, actual_ret_refresh); // [確認_正常系] - 1 者目の更新を受理すること。
    EXPECT_EQ(4U, groups_with_both); // [確認_正常系] - 報告中の不達者を判定に含めること。
    EXPECT_EQ(5U, groups_without_departed); // [確認_正常系] - 離脱者を判定から外すこと。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_replay); // [確認_異常系] - 離脱後も保持した通番でリプレイを破棄すること。
    // Cleanup
    while (ctx.receivers != nullptr)
    {
        auto *receiver = ctx.receivers;
        ctx.receivers = receiver->next;
        std::free(receiver);
    }
}

// NACK と REPORT が同一受信者のリプレイ窓を共用することの確認
TEST(receiverReportTest, nack_and_report_share_replay_window)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_packet packet = {};
    uint8_t states[POTR_MAX_PATH] = {};
    uint8_t wire[POTR_PACKET_HEADER_SIZE] = {};
    uint64_t now_ms = 1000;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.service.max_peers = 2;
    ctx.service.path_group[0] = 1;
    ctx.service.path_group[1] = 1;
    ctx.service.path_group[2] = 3;
    ctx.n_path = 3;
    ctx.health_timeout_ms = 100;
    packet.flags = POTR_FLAG_REPORT;
    packet.payload = states;
    packet.payload_len = POTR_MAX_PATH;
    packet.session_id = 1;
    states[0] = POTR_PING_STATE_NORMAL;
    states[2] = POTR_PING_STATE_NORMAL;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 報告の到着時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    uint16_t encrypted = cplat_hton16(POTR_FLAG_ENCRYPTED);
    std::memcpy(wire + 32, &encrypted, sizeof(encrypted));
    packet.tx_counter = 5;
    // Pre-Assert
    // Act
    int actual_ret_report = potr_internal_receiver_accept(&ctx, &packet, wire);
    packet.flags = POTR_FLAG_NACK;
    packet.payload_len = 0;
    packet.payload = nullptr;
    int actual_ret_duplicate = potr_internal_receiver_accept(&ctx, &packet, wire);
    packet.tx_counter++;
    int actual_ret_nack = potr_internal_receiver_accept(&ctx, &packet, wire);
    // [手順] - REPORT と同じ送出通番の NACK、および次の通番の NACK を検査する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_report); // [確認_正常系] - REPORT を受理すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_duplicate); // [確認_異常系] - 種別が違っても同じ通番を破棄すること。
    EXPECT_EQ(POTR_OK, actual_ret_nack); // [確認_正常系] - 次の通番の NACK を受理すること。
    ASSERT_NE(nullptr, ctx.receivers); // [確認_正常系] - 管理エントリが存在すること。
    EXPECT_EQ(nullptr, ctx.receivers->next); // [確認_正常系] - 種別ごとに別エントリを作らないこと。
    // Cleanup
    while (ctx.receivers != nullptr)
    {
        auto *receiver = ctx.receivers;
        ctx.receivers = receiver->next;
        std::free(receiver);
    }
}

// 形式不正の報告ではエントリを作らないことの確認
TEST(receiverReportTest, malformed_report_does_not_register)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_packet packet = {};
    uint8_t states[POTR_MAX_PATH] = {};
    uint8_t wire[POTR_PACKET_HEADER_SIZE] = {};
    uint64_t now_ms = 1000;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.service.max_peers = 2;
    ctx.service.path_group[0] = 1;
    ctx.service.path_group[1] = 1;
    ctx.service.path_group[2] = 3;
    ctx.n_path = 3;
    ctx.health_timeout_ms = 100;
    packet.flags = POTR_FLAG_REPORT;
    packet.payload = states;
    packet.payload_len = POTR_MAX_PATH;
    packet.session_id = 1;
    states[0] = POTR_PING_STATE_NORMAL;
    states[2] = POTR_PING_STATE_NORMAL;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 報告の到着時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    // Pre-Assert
    // Act
    packet.payload_len--;
    int actual_ret_length = potr_internal_receiver_accept(&ctx, &packet, wire);
    packet.payload_len = POTR_MAX_PATH;
    states[0] = 255;
    int actual_ret_state = potr_internal_receiver_accept(&ctx, &packet, wire);
    states[0] = POTR_PING_STATE_NORMAL;
    packet.seq_num = 1;
    int actual_ret_sequence = potr_internal_receiver_accept(&ctx, &packet, wire);
    // [手順] - ペイロード長、経路状態、通番の不正を順に検査する。
    // Assert
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_length); // [確認_異常系] - 長さ不正を破棄すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_state); // [確認_異常系] - 未定義の経路状態を破棄すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_sequence); // [確認_異常系] - 通番 0 以外の報告を破棄すること。
    EXPECT_EQ(nullptr, ctx.receivers); // [確認_異常系] - 不正報告を登録しないこと。
    // Cleanup
    while (ctx.receivers != nullptr)
    {
        auto *receiver = ctx.receivers;
        ctx.receivers = receiver->next;
        std::free(receiver);
    }
}
