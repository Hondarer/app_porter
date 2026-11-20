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
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
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
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
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
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
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
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
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
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    // Pre-Assert
    // Act
    packet.payload_len--;
    int actual_ret_length = potr_internal_receiver_accept(&ctx, &packet, wire);
    packet.payload_len = POTR_MAX_PATH;
    states[0] = 255;
    int actual_ret_state = potr_internal_receiver_accept(&ctx, &packet, wire);
    states[0] = POTR_PING_STATE_NORMAL;
    packet.seq_num = 2;
    int actual_ret_sequence = potr_internal_receiver_accept(&ctx, &packet, wire);
    packet.seq_num = 0;
    packet.ack_num = 1;
    int actual_ret_ack_without_limit = potr_internal_receiver_accept(&ctx, &packet, wire);
    // [手順] - ペイロード長、経路状態、送出上限の有無 (seq_num が 2)、送出上限がないのに ack_num が 0 でない報告を順に検査する。
    // Assert
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_length); // [確認_異常系] - 長さ不正を破棄すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_state); // [確認_異常系] - 未定義の経路状態を破棄すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_sequence); // [確認_異常系] - seq_num が 0 と 1 以外の報告を破棄すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_ack_without_limit);
    // [確認_異常系] - seq_num が 0 (送出上限なし) で ack_num が 0 でない報告を破棄すること。
    EXPECT_EQ(nullptr, ctx.receivers); // [確認_異常系] - 不正報告を登録しないこと。
    // Cleanup
    while (ctx.receivers != nullptr)
    {
        auto *receiver = ctx.receivers;
        ctx.receivers = receiver->next;
        std::free(receiver);
    }
}

namespace
{
    // 受信者の一覧へ、指定した状態の受信者を 1 件追加する。
    potr_internal_receiver *AddReceiver(potr_context *ctx, int active, uint64_t now_ms, uint64_t expire_ms)
    {
        auto *receiver = static_cast<potr_internal_receiver *>(std::calloc(1, sizeof(potr_internal_receiver)));
        receiver->active = active;
        receiver->last_report_ms = active ? now_ms : 0U;
        receiver->flow_request_expire_ms = expire_ms;
        receiver->next = ctx->receivers;
        ctx->receivers = receiver;
        return receiver;
    }

    // 受信者に送出上限を保持させる。
    void HoldSendLimit(potr_internal_receiver *receiver, uint32_t send_limit, uint64_t received_ms)
    {
        receiver->flow_send_limit = POTR_FLOW_SEND_LIMIT_HELD | (uint64_t)send_limit;
        receiver->flow_limit_received_ms = received_ms;
    }

    // 受信者の一覧を解放する。
    void FreeReceivers(potr_context *ctx)
    {
        while (ctx->receivers != nullptr)
        {
            auto *receiver = ctx->receivers;
            ctx->receivers = receiver->next;
            std::free(receiver);
        }
    }
} // namespace

// 抑制の要求で期限を記録し、解除で要求なしへ戻すことの確認
TEST(receiverReportTest, flow_control_suppress_records_expiry_and_resume_clears)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_packet packet = {};
    uint8_t payload[POTR_FLOW_CONTROL_PAYLOAD_SIZE] = {(uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS, 0U, 0U, 0x01U, 0x00U};
    uint8_t &state = payload[0]; // 先頭 1 バイトが状態、続く 4 バイトが送出上限 (256)
    uint8_t wire[POTR_PACKET_HEADER_SIZE] = {};
    uint64_t now_ms = 1000;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.flow_control_config.request_lifetime_ms = 300;
    packet.flags = POTR_FLAG_FLOW_CONTROL;
    packet.payload = payload;
    packet.payload_len = POTR_FLOW_CONTROL_PAYLOAD_SIZE;
    packet.session_id = 1;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 要求の到着時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    uint64_t expire_after_suppress = 0;
    // Pre-Assert
    // Act
    int actual_ret_suppress = potr_internal_receiver_accept(&ctx, &packet, wire);
    // [手順] - 受信報告を受けていない受信者から、抑制の要求を受理する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_suppress); // [確認_正常系] - 抑制の要求を受理すること。
    ASSERT_NE(nullptr, ctx.receivers); // [確認_正常系] - 受信報告を受けていない受信者の記録が作られること。
    EXPECT_EQ(1300U, ctx.receivers->flow_request_expire_ms);
    // [確認_正常系] - 期限が現在時刻に有効期限を加えた値になること。
    EXPECT_EQ(0, ctx.receivers->active); // [確認_正常系] - 要求だけでは活動中にならないこと。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 256U, ctx.receivers->flow_send_limit);
    // [確認_正常系] - 抑制の要求に載った送出上限 256 を保持すること。
    EXPECT_EQ(1000U, ctx.receivers->flow_limit_received_ms); // [確認_正常系] - 受理した時刻を記録すること。
    // Arrange_2
    expire_after_suppress = ctx.receivers->flow_request_expire_ms;
    state = POTR_FLOW_CONTROL_STATE_RESUME;
    payload[3] = 0x02U; // [状態] - 解除に載せる送出上限を 512 にする。
    now_ms = 1100;
    // Act_2
    int actual_ret_resume = potr_internal_receiver_accept(&ctx, &packet, wire);
    // [手順] - 同じ受信者から、解除を受理する。
    // Assert_2
    EXPECT_EQ(1300U, expire_after_suppress); // [確認_正常系] - 解除の前の期限が記録されていること。
    EXPECT_EQ(POTR_OK, actual_ret_resume); // [確認_正常系] - 解除を受理すること。
    EXPECT_EQ(0U, ctx.receivers->flow_request_expire_ms); // [確認_正常系] - 期限が 0 (要求なし) へ戻ること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 512U, ctx.receivers->flow_send_limit);
    // [確認_正常系] - 解除でも、送出上限を 512 へ上書きすること。
    EXPECT_EQ(1100U, ctx.receivers->flow_limit_received_ms); // [確認_正常系] - 受理した時刻を更新すること。
    EXPECT_EQ(nullptr, ctx.receivers->next); // [確認_正常系] - 同じ受信者の記録を更新し、増やさないこと。
    // Cleanup
    FreeReceivers(&ctx);
}

// 形式不正のフロー制御パケットを破棄することの確認
TEST(receiverReportTest, malformed_flow_control_is_rejected)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_packet packet = {};
    uint8_t payload[POTR_FLOW_CONTROL_PAYLOAD_SIZE] = {(uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS, 0U, 0U, 0x01U, 0x00U};
    uint8_t &state = payload[0]; // 先頭 1 バイトが状態、続く 4 バイトが送出上限 (256)
    uint8_t wire[POTR_PACKET_HEADER_SIZE] = {};
    uint64_t now_ms = 1000;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.flow_control_config.request_lifetime_ms = 300;
    packet.flags = POTR_FLAG_FLOW_CONTROL;
    packet.payload = payload;
    packet.payload_len = POTR_FLOW_CONTROL_PAYLOAD_SIZE;
    packet.session_id = 1;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 要求の到着時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    // Pre-Assert
    // Act
    packet.payload_len = 1;
    int actual_ret_length = potr_internal_receiver_accept(&ctx, &packet, wire);
    packet.payload_len = 6;
    int actual_ret_length_long = potr_internal_receiver_accept(&ctx, &packet, wire);
    packet.payload_len = POTR_FLOW_CONTROL_PAYLOAD_SIZE;
    state = 0;
    int actual_ret_state_zero = potr_internal_receiver_accept(&ctx, &packet, wire);
    state = 3;
    int actual_ret_state_undefined = potr_internal_receiver_accept(&ctx, &packet, wire);
    state = POTR_FLOW_CONTROL_STATE_SUPPRESS;
    packet.seq_num = 1;
    int actual_ret_sequence = potr_internal_receiver_accept(&ctx, &packet, wire);
    packet.seq_num = 0;
    packet.ack_num = 1;
    int actual_ret_ack = potr_internal_receiver_accept(&ctx, &packet, wire);
    packet.ack_num = 0;
    packet.payload = nullptr;
    int actual_ret_null_payload = potr_internal_receiver_accept(&ctx, &packet, wire);
    // [手順] - ペイロード長 (短い、長い)、状態 0、未定義の状態、通番、確認応答番号、ペイロードの不正を順に検査する。
    // Assert
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_length); // [確認_異常系] - ペイロード長が 1 (5 未満) のパケットを破棄すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_length_long); // [確認_異常系] - ペイロード長が 6 (5 超) のパケットを破棄すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_state_zero); // [確認_異常系] - 状態 0 を破棄すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_state_undefined); // [確認_異常系] - 送信者が受けた状態 3 (監視対象の一覧) を破棄すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_sequence); // [確認_異常系] - 通番 0 以外のパケットを破棄すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_ack); // [確認_異常系] - 確認応答番号 0 以外のパケットを破棄すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_null_payload); // [確認_異常系] - ペイロードが NULL のパケットを破棄すること。
    EXPECT_EQ(nullptr, ctx.receivers); // [確認_異常系] - 不正なパケットで受信者を登録しないこと。
    // Cleanup
    FreeReceivers(&ctx);
}

// unicast は有効な要求が 1 つあれば抑制し、期限切れでは抑制しないことの確認
TEST(receiverReportTest, flow_suppressed_unicast_valid_and_expired)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000;
    size_t requesting = 99;
    uint64_t until_ms = 99;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.health_timeout_ms = 0;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    potr_internal_receiver *receiver = AddReceiver(&ctx, 1, now_ms, 1300);
    size_t requesting_expired = 99;
    uint64_t until_expired = 99;
    // Pre-Assert
    // Act
    int actual_valid = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, &requesting, &until_ms);
    // [手順] - 期限が現在時刻より後の要求が 1 つある状態で判定する。
    // Assert
    EXPECT_EQ(1, actual_valid); // [確認_正常系] - 有効な要求が 1 つあれば抑制すること。
    EXPECT_EQ(1U, requesting); // [確認_正常系] - 要求中の受信者数が 1 であること。
    EXPECT_EQ(1300U, until_ms); // [確認_正常系] - 最も早い期限が要求の期限であること。
    // Arrange_2
    receiver->flow_request_expire_ms = now_ms;
    // Act_2
    int actual_expired = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, &requesting_expired, &until_expired);
    // [手順] - 要求の期限が現在時刻と等しい状態で判定する。
    // Assert_2
    EXPECT_EQ(0, actual_expired); // [確認_正常系] - 期限切れの要求では抑制しないこと。
    EXPECT_EQ(0U, requesting_expired); // [確認_正常系] - 要求中の受信者数が 0 であること。
    EXPECT_EQ(0U, until_expired); // [確認_正常系] - 期限が 0 であること。
    // Cleanup
    FreeReceivers(&ctx);
}

// multicast の台数の条件 (活動中 10 台、台数 2) の確認
TEST(receiverReportTest, flow_suppressed_multicast_count_threshold)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000;
    size_t requesting_one = 99;
    uint64_t until_one = 99;
    size_t requesting_two = 99;
    uint64_t until_two = 99;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.health_timeout_ms = 0;
    ctx.flow_control_config.receiver_count = 2;
    ctx.flow_control_config.receiver_percent = 0;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    potr_internal_receiver *receivers[10] = {};
    for (int i = 0; i < 10; i++)
    {
        receivers[i] = AddReceiver(&ctx, 1, now_ms, 0);
    }
    receivers[0]->flow_request_expire_ms = 1300;
    // Pre-Assert
    // Act
    int actual_one = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, &requesting_one, &until_one);
    receivers[1]->flow_request_expire_ms = 1300;
    int actual_two = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, &requesting_two, &until_two);
    // [手順] - 活動中 10 台のうち 1 台、続いて 2 台が要求中の状態で判定する。
    // Assert
    EXPECT_EQ(0, actual_one); // [確認_正常系] - 要求中 1 台では台数 2 に満たず抑制しないこと。
    EXPECT_EQ(1U, requesting_one); // [確認_正常系] - 要求中の受信者数が 1 であること。
    EXPECT_EQ(1, actual_two); // [確認_正常系] - 要求中 2 台で台数 2 を満たし抑制すること。
    EXPECT_EQ(2U, requesting_two); // [確認_正常系] - 要求中の受信者数が 2 であること。
    // Cleanup
    FreeReceivers(&ctx);
}

// 活動中の受信者数が台数を下回る場合は活動中の全員の要求で抑制することの確認
TEST(receiverReportTest, flow_suppressed_multicast_active_below_count)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000;
    size_t requesting = 99;
    uint64_t until_ms = 99;
    size_t requesting_partial = 99;
    uint64_t until_partial = 99;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.health_timeout_ms = 0;
    ctx.flow_control_config.receiver_count = 2;
    ctx.flow_control_config.receiver_percent = 0;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    potr_internal_receiver *receiver = AddReceiver(&ctx, 1, now_ms, 1300);
    // Pre-Assert
    // Act
    int actual_all = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, &requesting, &until_ms);
    // [手順] - 活動中 1 台の受信者が要求中の状態で、台数 2 の設定を判定する。
    // Assert
    EXPECT_EQ(1, actual_all); // [確認_正常系] - 活動中の受信者数をしきい値として使い抑制すること。
    EXPECT_EQ(1U, requesting); // [確認_正常系] - 要求中の受信者数が 1 であること。
    EXPECT_EQ(1300U, until_ms); // [確認_正常系] - 最も早い期限が要求の期限であること。
    // Arrange_2
    receiver->flow_request_expire_ms = 0;
    AddReceiver(&ctx, 1, now_ms, 0);
    AddReceiver(&ctx, 1, now_ms, 0);
    ctx.receivers->flow_request_expire_ms = 1300;
    // Act_2
    int actual_partial = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, &requesting_partial, &until_partial);
    // [手順] - 活動中 3 台のうち 1 台だけが要求中の状態で、同じ設定を判定する。
    // Assert_2
    EXPECT_EQ(0, actual_partial); // [確認_正常系] - 活動中の受信者数が台数以上なら 1 台の要求では抑制しないこと。
    EXPECT_EQ(1U, requesting_partial); // [確認_正常系] - 要求中の受信者数が 1 であること。
    // Cleanup
    FreeReceivers(&ctx);
}

// 活動中の受信者数が台数を下回る場合 (台数 5、活動中 3 台) の確認
TEST(receiverReportTest, flow_suppressed_multicast_count_exceeds_active)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000;
    size_t requesting_two = 99;
    uint64_t until_two = 99;
    size_t requesting_three = 99;
    uint64_t until_three = 99;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.health_timeout_ms = 0;
    ctx.flow_control_config.receiver_count = 5;
    ctx.flow_control_config.receiver_percent = 0;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    potr_internal_receiver *receivers[3] = {};
    for (int i = 0; i < 3; i++)
    {
        receivers[i] = AddReceiver(&ctx, 1, now_ms, 0);
    }
    receivers[0]->flow_request_expire_ms = 1300;
    receivers[1]->flow_request_expire_ms = 1300;
    // Pre-Assert
    // Act
    int actual_two = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, &requesting_two, &until_two);
    receivers[2]->flow_request_expire_ms = 1300;
    int actual_three = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, &requesting_three, &until_three);
    // [手順] - 活動中 3 台のうち 2 台、続いて 3 台が要求中の状態で、台数 5 の設定を判定する。
    // Assert
    EXPECT_EQ(0, actual_two); // [確認_正常系] - 要求中 2 台では活動中 3 台に満たず抑制しないこと。
    EXPECT_EQ(2U, requesting_two); // [確認_正常系] - 要求中の受信者数が 2 であること。
    EXPECT_EQ(1, actual_three); // [確認_正常系] - 活動中の全員 (3 台) の要求で抑制すること。
    EXPECT_EQ(3U, requesting_three); // [確認_正常系] - 要求中の受信者数が 3 であること。
    // Cleanup
    FreeReceivers(&ctx);
}

// multicast の割合の条件 (活動中 10 台、割合 50) の確認
TEST(receiverReportTest, flow_suppressed_multicast_percent_threshold)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000;
    size_t requesting_four = 99;
    uint64_t until_four = 99;
    size_t requesting_five = 99;
    uint64_t until_five = 99;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.health_timeout_ms = 0;
    ctx.flow_control_config.receiver_count = 0;
    ctx.flow_control_config.receiver_percent = 50;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    potr_internal_receiver *receivers[10] = {};
    for (int i = 0; i < 10; i++)
    {
        receivers[i] = AddReceiver(&ctx, 1, now_ms, 0);
    }
    for (int i = 0; i < 4; i++)
    {
        receivers[i]->flow_request_expire_ms = 1300;
    }
    // Pre-Assert
    // Act
    int actual_four = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, &requesting_four, &until_four);
    receivers[4]->flow_request_expire_ms = 1300;
    int actual_five = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, &requesting_five, &until_five);
    // [手順] - 活動中 10 台のうち 4 台、続いて 5 台が要求中の状態で、割合 50 の設定を判定する。
    // Assert
    EXPECT_EQ(0, actual_four); // [確認_正常系] - 要求中 4 台 (40%) では抑制しないこと。
    EXPECT_EQ(4U, requesting_four); // [確認_正常系] - 要求中の受信者数が 4 であること。
    EXPECT_EQ(1, actual_five); // [確認_正常系] - 要求中 5 台 (50%) で抑制すること。
    EXPECT_EQ(5U, requesting_five); // [確認_正常系] - 要求中の受信者数が 5 であること。
    // Cleanup
    FreeReceivers(&ctx);
}

// 割合と台数がともに 0 の broadcast は抑制せず、要求中の数は返すことの確認
TEST(receiverReportTest, flow_suppressed_broadcast_without_condition)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000;
    size_t requesting = 99;
    uint64_t until_ms = 99;
    ctx.service.type = POTR_TYPE_BROADCAST;
    ctx.health_timeout_ms = 0;
    ctx.flow_control_config.receiver_count = 0;
    ctx.flow_control_config.receiver_percent = 0;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    AddReceiver(&ctx, 1, now_ms, 1300);
    AddReceiver(&ctx, 1, now_ms, 1400);
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, &requesting, &until_ms);
    // [手順] - 条件が無効の broadcast で、要求中の受信者が 2 台いる状態を判定する。
    // Assert
    EXPECT_EQ(0, actual_ret); // [確認_正常系] - 条件が無効なら要求があっても抑制しないこと。
    EXPECT_EQ(2U, requesting); // [確認_正常系] - 要求中の受信者数を返すこと。
    // Cleanup
    FreeReceivers(&ctx);
}

// 受信報告を受けていない受信者も、有効な要求があれば活動中として数えることの確認
TEST(receiverReportTest, flow_suppressed_counts_requesting_inactive_receivers)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000;
    size_t requesting = 99;
    uint64_t until_ms = 99;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.health_timeout_ms = 0;
    ctx.flow_control_config.receiver_count = 2;
    ctx.flow_control_config.receiver_percent = 0;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    AddReceiver(&ctx, 0, now_ms, 1300);
    AddReceiver(&ctx, 0, now_ms, 1300);
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, &requesting, &until_ms);
    // [手順] - 受信報告を受けていない 2 台が有効な要求を送ってきている状態を判定する。
    // Assert
    EXPECT_EQ(1, actual_ret); // [確認_正常系] - 要求中の 2 台を活動中として数え、台数 2 を満たし抑制すること。
    EXPECT_EQ(2U, requesting); // [確認_正常系] - 要求中の受信者数が 2 であること。
    // Cleanup
    FreeReceivers(&ctx);
}

// until_ms が有効な要求のうち最も早い期限であることの確認
TEST(receiverReportTest, flow_suppressed_until_is_earliest_expiry)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000;
    size_t requesting = 99;
    uint64_t until_ms = 99;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.health_timeout_ms = 0;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    AddReceiver(&ctx, 1, now_ms, 1500);
    AddReceiver(&ctx, 1, now_ms, 1200);
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, &requesting, &until_ms);
    // [手順] - 期限が 1500 と 1200 の 2 台が要求中の状態を判定する。
    // Assert
    EXPECT_EQ(1, actual_ret); // [確認_正常系] - 有効な要求があるため抑制すること。
    EXPECT_EQ(2U, requesting); // [確認_正常系] - 要求中の受信者数が 2 であること。
    EXPECT_EQ(1200U, until_ms); // [確認_正常系] - 早い方の期限 1200 を返すこと。
    // Cleanup
    FreeReceivers(&ctx);
}

// 引数が NULL の場合は 0 を返すことの確認
TEST(receiverReportTest, flow_suppressed_null_arguments_return_zero)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000;
    size_t requesting = 99;
    uint64_t until_ms = 99;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.health_timeout_ms = 0;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    AddReceiver(&ctx, 1, now_ms, 1300);
    // Pre-Assert
    // Act
    int actual_ctx = potr_internal_receiver_flow_suppressed(nullptr, now_ms, 0U, &requesting, &until_ms);
    int actual_requesting = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, nullptr, &until_ms);
    int actual_until = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, &requesting, nullptr);
    // [手順] - ctx、tracked (要求または送出上限を保持している受信者数の格納先)、until_ms を順に NULL にして判定する。
    // Assert
    EXPECT_EQ(0, actual_ctx); // [確認_異常系] - ctx が NULL なら 0 を返すこと。
    EXPECT_EQ(0, actual_requesting); // [確認_異常系] - tracked が NULL なら 0 を返すこと。
    EXPECT_EQ(0, actual_until); // [確認_異常系] - until_ms が NULL なら 0 を返すこと。
    // Cleanup
    FreeReceivers(&ctx);
}

// unicast で、送出上限に達していない間は抑制せず、達したら抑制し、止まった時点を記録することの確認
TEST(receiverReportTest, flow_suppressed_unicast_send_limit_reached)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000;
    size_t tracked_below = 99;
    uint64_t until_below = 99;
    size_t tracked_reached = 99;
    uint64_t until_reached = 99;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.health_timeout_ms = 0;
    ctx.flow_control_config.request_lifetime_ms = 300;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    potr_internal_receiver *receiver = AddReceiver(&ctx, 1, now_ms, 0);
    HoldSendLimit(receiver, 100U, 900U); // [状態] - 要求なしで、送出上限 100 (時刻 900 に受理) を保持する。
    // Pre-Assert
    // Act
    int actual_below = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 99U, &tracked_below, &until_below);
    // [手順] - 次の通番が 99 (送出上限の 1 つ手前) の状態で判定する。
    int actual_reached = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 100U, &tracked_reached, &until_reached);
    // [手順] - 次の通番が 100 (送出上限ちょうど) の状態で判定する。
    // Assert
    EXPECT_EQ(0, actual_below);       // [確認_正常系] - 送出上限の手前では抑制しないこと。
    EXPECT_EQ(1U, tracked_below);     // [確認_正常系] - 送出上限を保持している受信者を 1 として返すこと。
    EXPECT_EQ(0U, until_below);       // [確認_正常系] - 「止める状態」の受信者がないため 0 であること。
    EXPECT_EQ(1, actual_reached);     // [確認_正常系] - 送出上限に達したら抑制すること。
    EXPECT_EQ(1U, tracked_reached);   // [確認_正常系] - 送出上限を保持している受信者を 1 として返すこと。
    EXPECT_EQ(1300U, until_reached);  // [確認_正常系] - 止まった時点 1000 に有効期限 300 を加えた時刻を返すこと。
    EXPECT_EQ(1000U, receiver->flow_limit_stalled_ms); // [確認_正常系] - 止まった時点を記録すること。
    // Cleanup
    FreeReceivers(&ctx);
}

// 送出上限で止まった時点から有効期限が経過すると送出上限を破棄し、その前に届いたパケットで期限が延びることの確認
TEST(receiverReportTest, flow_suppressed_send_limit_expires_after_lifetime)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    size_t tracked = 99;
    uint64_t until_ms = 99;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.health_timeout_ms = 0;
    ctx.flow_control_config.request_lifetime_ms = 300;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    potr_internal_receiver *receiver = AddReceiver(&ctx, 1, 1000U, 0);
    HoldSendLimit(receiver, 100U, 900U); // [状態] - 送出上限 100 (時刻 900 に受理) を保持する。
    int actual_stall = potr_internal_receiver_flow_suppressed(&ctx, 1000U, 100U, &tracked, &until_ms);
    ASSERT_EQ(1, actual_stall);
    // [状態] - 時刻 1000 に送出上限で止まる。
    // [状態確認] - 判定の戻り値が 1 (抑制) であること。
    // Pre-Assert
    // Act
    int actual_before = potr_internal_receiver_flow_suppressed(&ctx, 1299U, 100U, &tracked, &until_ms);
    // [手順] - 止まった時点から 299 [ms] 後に判定する。
    receiver->flow_limit_received_ms = 1200U;
    int actual_extended = potr_internal_receiver_flow_suppressed(&ctx, 1300U, 100U, &tracked, &until_ms);
    uint64_t until_extended = until_ms;
    // [手順] - 時刻 1200 に同じ送出上限のフロー制御パケットを受理した後、時刻 1300 に判定する。
    int actual_expired = potr_internal_receiver_flow_suppressed(&ctx, 1500U, 100U, &tracked, &until_ms);
    size_t tracked_expired = tracked;
    // [手順] - 受理した時刻 1200 から有効期限 300 [ms] が経過した時刻 1500 に判定する。
    // Assert
    EXPECT_EQ(1, actual_before);      // [確認_正常系] - 有効期限の経過前は抑制を続けること。
    EXPECT_EQ(1, actual_extended);    // [確認_正常系] - 止まった後に受理したパケットで抑制を続けること。
    EXPECT_EQ(1500U, until_extended); // [確認_正常系] - 破棄の時刻が受理した時刻 1200 + 300 になること。
    EXPECT_EQ(0, actual_expired);     // [確認_正常系] - 有効期限の経過後は抑制しないこと。
    EXPECT_EQ(0U, receiver->flow_send_limit); // [確認_正常系] - 送出上限を破棄すること。
    EXPECT_EQ(0U, tracked_expired);   // [確認_正常系] - 破棄の後は、保持している受信者を数えないこと。
    // Cleanup
    FreeReceivers(&ctx);
}

// 送出上限の比較が、通番の循環をまたいでも正しいことの確認
TEST(receiverReportTest, flow_suppressed_send_limit_wraps_sequence)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000;
    size_t tracked = 99;
    uint64_t until_ms = 99;
    uint32_t next_seqs[4] = {0xFFFFFFFEU, 0xFFFFFFFFU, 4U, 5U};
    int expected[4] = {0, 0, 0, 1};
    int actual[4] = {};
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.health_timeout_ms = 0;
    ctx.flow_control_config.request_lifetime_ms = 300;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    potr_internal_receiver *receiver = AddReceiver(&ctx, 1, now_ms, 0);
    HoldSendLimit(receiver, 5U, now_ms); // [状態] - 循環した後の通番 5 を送出上限として保持する (ループ 4 回)。
    // Pre-Assert
    // Act
    for (size_t i = 0; i < 4; i++)
    {
        receiver->flow_limit_stalled_ms = 0U;
        actual[i] = potr_internal_receiver_flow_suppressed(&ctx, now_ms, next_seqs[i], &tracked, &until_ms);
    }
    // [手順] - 次の通番を 0xFFFFFFFE、0xFFFFFFFF、4、5 として判定する。
    // Assert
    for (size_t i = 0; i < 4; i++)
    {
        EXPECT_EQ(expected[i], actual[i]);
        // [確認_正常系] - 循環の前と送出上限の手前では抑制せず、送出上限ちょうどで抑制すること。(確認 4 回)
    }
    // Cleanup
    FreeReceivers(&ctx);
}

// multicast の台数の判定で、送出上限に達した受信者を「止める状態」として数えることの確認
TEST(receiverReportTest, flow_suppressed_multicast_counts_send_limit_reached)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000;
    size_t tracked_one = 99;
    uint64_t until_one = 99;
    size_t tracked_two = 99;
    uint64_t until_two = 99;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.health_timeout_ms = 0;
    ctx.flow_control_config.receiver_count = 2;
    ctx.flow_control_config.receiver_percent = 0;
    ctx.flow_control_config.request_lifetime_ms = 300;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    AddReceiver(&ctx, 1, now_ms, 1500);                              // 要求中の受信者
    potr_internal_receiver *limited = AddReceiver(&ctx, 1, now_ms, 0); // 送出上限を保持する受信者
    AddReceiver(&ctx, 1, now_ms, 0);                                  // 要求も送出上限もない受信者
    HoldSendLimit(limited, 50U, now_ms);
    // [状態] - 活動中 3 台のうち 1 台が要求中、1 台が送出上限 50 を保持する。
    // Pre-Assert
    // Act
    int actual_one = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 49U, &tracked_one, &until_one);
    // [手順] - 次の通番 49 (送出上限の手前) で判定する。
    int actual_two = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 50U, &tracked_two, &until_two);
    // [手順] - 次の通番 50 (送出上限ちょうど) で判定する。
    // Assert
    EXPECT_EQ(0, actual_one);   // [確認_正常系] - 「止める状態」が 1 台では台数 2 に満たず抑制しないこと。
    EXPECT_EQ(2U, tracked_one); // [確認_正常系] - 要求中と送出上限の保持の 2 台を返すこと。
    EXPECT_EQ(1, actual_two);   // [確認_正常系] - 送出上限に達した受信者を含めて 2 台となり抑制すること。
    EXPECT_EQ(1300U, until_two); // [確認_正常系] - 送出上限の破棄の時刻 1300 が、要求の期限 1500 より早いため返すこと。
    // Cleanup
    FreeReceivers(&ctx);
}

namespace
{
// 監視する数の計算の入力と期待値の組
struct monitor_count_case
{
    size_t active;
    size_t candidates;
    uint32_t receiver_percent;
    uint32_t receiver_count;
    uint32_t monitored_percent;
    uint32_t monitored_count;
    size_t max_payload; // 構造体に詰め物が入らないよう size_t で持ち、呼び出しで uint32_t へ変換する
    size_t expected;
};

// multicast の送信者の構成にする。監視する数は台数 monitored_count、1 対 N の台数の条件は receiver_count とする。
void SetupMonitoringSender(potr_context *ctx, uint32_t receiver_count, uint32_t monitored_count)
{
    ctx->service.type = POTR_TYPE_MULTICAST;
    ctx->role = POTR_ROLE_SENDER;
    ctx->global.max_payload = 1400U;
    ctx->health_timeout_ms = 1000U;
    ctx->flow_control_config.receiver_count = receiver_count;
    ctx->flow_control_config.monitored_receiver_count = monitored_count;
    ctx->flow_control_config.request_lifetime_ms = 300U;
}

// 受信報告を 1 件受理させる。seq_num が 1 の場合は ack_num を送出上限として載せる。
int AcceptReport(potr_context *ctx, uint32_t session_id, uint32_t seq_num, uint32_t ack_num)
{
    uint8_t states[POTR_MAX_PATH] = {};
    uint8_t wire[POTR_PACKET_HEADER_SIZE] = {};
    potr_packet packet = {};

    packet.flags = POTR_FLAG_REPORT;
    packet.payload = states;
    packet.payload_len = POTR_MAX_PATH;
    packet.session_id = session_id;
    packet.seq_num = seq_num;
    packet.ack_num = ack_num;
    return potr_internal_receiver_accept(ctx, &packet, wire);
}

// フロー制御パケット (状態と送出上限) を 1 件受理させる。
int AcceptFlowControl(potr_context *ctx, uint32_t session_id, uint8_t state, uint32_t send_limit)
{
    uint8_t payload[POTR_FLOW_CONTROL_PAYLOAD_SIZE] = {state, (uint8_t)(send_limit >> 24), (uint8_t)(send_limit >> 16),
                                                       (uint8_t)(send_limit >> 8), (uint8_t)send_limit};
    uint8_t wire[POTR_PACKET_HEADER_SIZE] = {};
    potr_packet packet = {};

    packet.flags = POTR_FLAG_FLOW_CONTROL;
    packet.payload = payload;
    packet.payload_len = POTR_FLOW_CONTROL_PAYLOAD_SIZE;
    packet.session_id = session_id;
    return potr_internal_receiver_accept(ctx, &packet, wire);
}

// セッション識別子が一致する受信者を返す。
potr_internal_receiver *FindReceiver(potr_context *ctx, uint32_t session_id)
{
    for (potr_internal_receiver *receiver = ctx->receivers; receiver != nullptr; receiver = receiver->next)
    {
        if (receiver->session_id == session_id)
        {
            return receiver;
        }
    }
    return nullptr;
}
} // namespace

// 監視する数を、台数、割合、しきい値による下限、上限、候補の数、しきい値が 0 の場合のそれぞれで求めることの確認
TEST(receiverReportTest, monitor_count_cases)
{
    // Arrange
    const monitor_count_case cases[] = {
        {3U, 3U, 0U, 2U, 0U, 4U, 1400U, 3U},       // 台数 4 より受信者が少ない場合は全員
        {100U, 100U, 0U, 2U, 0U, 4U, 1400U, 4U},   // 既定の構成で受信者が多い場合は台数 4
        {100U, 100U, 0U, 2U, 0U, 1U, 1400U, 2U},   // しきい値 2 より少ない台数は、しきい値の台数
        {100U, 100U, 0U, 10U, 0U, 20U, 1400U, 20U}, // しきい値 10、台数 20
        {100U, 100U, 0U, 2U, 10U, 4U, 1400U, 10U}, // 割合 10% (10 台) が台数 4 より大きい
        {15U, 15U, 0U, 1U, 10U, 0U, 1400U, 2U},    // 割合 10% の 1.5 台は切り上げて 2 台
        {10U, 10U, 50U, 0U, 0U, 1U, 1400U, 5U},    // 割合の条件 50% のしきい値は 5 台
        {10U, 10U, 50U, 2U, 0U, 0U, 1400U, 2U},    // 両方の条件があるしきい値は小さいほうの 2 台
        {10U, 10U, 0U, 0U, 0U, 4U, 1400U, 0U},     // 両方の条件が無効の場合は 0
        {0U, 0U, 0U, 2U, 0U, 4U, 1400U, 0U},       // 活動中の受信者がいない場合は 0
        {100U, 100U, 0U, 2U, 100U, 0U, 1400U, 64U}, // 割合 100% でも上限 64
        {100U, 100U, 0U, 2U, 0U, 10U, 100U, 6U},   // max_payload 100 の上限 (100 − 2) ÷ 16 = 6
        {10U, 3U, 0U, 2U, 0U, 4U, 1400U, 3U},      // 候補が 3 台の場合は 3 台
    };
    const size_t case_count = sizeof(cases) / sizeof(cases[0]);
    size_t actual[sizeof(cases) / sizeof(cases[0])] = {};
    // [状態] - 13 通りの入力を用意する (ループ 13 回)。
    // Pre-Assert
    // Act
    for (size_t i = 0; i < case_count; i++)
    {
        actual[i] = flow_monitor_count(cases[i].active, cases[i].candidates, cases[i].receiver_percent,
                                       cases[i].receiver_count, cases[i].monitored_percent, cases[i].monitored_count,
                                       (uint32_t)cases[i].max_payload);
    }
    // [手順] - 入力ごとに flow_monitor_count() を呼び出し、監視する数を記録する。
    // Assert
    for (size_t i = 0; i < case_count; i++)
    {
        EXPECT_EQ(cases[i].expected, actual[i]); // [確認_正常系] - 監視する数が期待値であること。(確認 13 回)
    }
}

// 受信報告の送出上限から余裕を記録し、余裕が小さいほうから監視対象に選ぶことの確認
TEST(receiverReportTest, report_records_margin_and_selects_slowest)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_packet_session_hdr list[POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX] = {};
    uint64_t now_ms = 1000;
    int changed_first = -1;
    int changed_second = -1;
    SetupMonitoringSender(&ctx, 2U, 2U);
    ctx.send_window.next_seq = 100U;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 受理の時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    // [状態] - multicast の送信者で、台数の条件 2、監視する台数 2、次に割り当てる通番 100 とする。
    // Pre-Assert
    // Act
    int actual_ret_a = AcceptReport(&ctx, 1U, 1U, 150U); // [手順] - 受信者 1 から送出上限 150 (余裕 50) の受信報告を受理する。
    int actual_ret_b = AcceptReport(&ctx, 2U, 1U, 120U); // [手順] - 受信者 2 から送出上限 120 (余裕 20) の受信報告を受理する。
    int actual_ret_c = AcceptReport(&ctx, 3U, 1U, 180U); // [手順] - 受信者 3 から送出上限 180 (余裕 80) の受信報告を受理する。
    size_t count_first = potr_internal_receiver_monitor_list(&ctx, now_ms, list, POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX,
                                                             &changed_first);
    // [手順] - 監視対象の一覧を取り出す。
    size_t count_second = potr_internal_receiver_monitor_list(&ctx, now_ms, list,
                                                              POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX, &changed_second);
    // [手順] - 変化のないまま、監視対象の一覧をもう一度取り出す。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_a); // [確認_正常系] - 受信者 1 の受信報告を受理すること。
    EXPECT_EQ(POTR_OK, actual_ret_b); // [確認_正常系] - 受信者 2 の受信報告を受理すること。
    EXPECT_EQ(POTR_OK, actual_ret_c); // [確認_正常系] - 受信者 3 の受信報告を受理すること。
    ASSERT_NE(nullptr, FindReceiver(&ctx, 1U)); // [確認_正常系] - 受信者 1 の記録があること。
    ASSERT_NE(nullptr, FindReceiver(&ctx, 2U)); // [確認_正常系] - 受信者 2 の記録があること。
    ASSERT_NE(nullptr, FindReceiver(&ctx, 3U)); // [確認_正常系] - 受信者 3 の記録があること。
    EXPECT_EQ(50, FindReceiver(&ctx, 1U)->flow_margin); // [確認_正常系] - 受信者 1 の余裕が 50 であること。
    EXPECT_EQ(20, FindReceiver(&ctx, 2U)->flow_margin); // [確認_正常系] - 受信者 2 の余裕が 20 であること。
    EXPECT_EQ(1, FindReceiver(&ctx, 1U)->flow_monitored); // [確認_正常系] - 余裕が 2 番目に小さい受信者 1 が監視対象であること。
    EXPECT_EQ(1, FindReceiver(&ctx, 2U)->flow_monitored); // [確認_正常系] - 余裕が最も小さい受信者 2 が監視対象であること。
    EXPECT_EQ(0, FindReceiver(&ctx, 3U)->flow_monitored); // [確認_正常系] - 余裕が最も大きい受信者 3 は監視対象でないこと。
    EXPECT_EQ(2U, count_first);  // [確認_正常系] - 一覧が 2 台であること。
    EXPECT_EQ(1, changed_first); // [確認_正常系] - 監視対象が変わったことを返すこと。
    EXPECT_EQ(2U, count_second); // [確認_正常系] - 変化がなくても一覧が 2 台であること。
    EXPECT_EQ(0, changed_second); // [確認_正常系] - 2 回目は監視対象が変わっていないことを返すこと。
    // Cleanup
    FreeReceivers(&ctx);
}

// 余裕を循環を考慮した差で記録することの確認
TEST(receiverReportTest, report_margin_wraps_sequence)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000;
    SetupMonitoringSender(&ctx, 2U, 4U);
    ctx.send_window.next_seq = 0xFFFFFFF0U;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 受理の時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    // [状態] - 次に割り当てる通番を 0xFFFFFFF0 (循環の直前) とする。
    // Pre-Assert
    // Act
    int actual_ret = AcceptReport(&ctx, 1U, 1U, 0x10U); // [手順] - 送出上限 0x10 (循環した後) の受信報告を受理する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 受信報告を受理すること。
    ASSERT_NE(nullptr, ctx.receivers); // [確認_正常系] - 受信者の記録があること。
    EXPECT_EQ(1, ctx.receivers->flow_margin_known); // [確認_正常系] - 余裕の記録があること。
    EXPECT_EQ(32, ctx.receivers->flow_margin); // [確認_正常系] - 余裕が循環をまたいだ差の 32 であること。
    // Cleanup
    FreeReceivers(&ctx);
}

// 送出上限がない受信報告で余裕の記録を消し、監視対象から外れた受信者の送出上限を破棄することの確認
TEST(receiverReportTest, report_without_limit_erases_margin_and_discards_limit)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_packet_session_hdr list[POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX] = {};
    uint64_t now_ms = 1000;
    int changed = -1;
    SetupMonitoringSender(&ctx, 1U, 1U);
    ctx.send_window.next_seq = 100U;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 受理の時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    AcceptReport(&ctx, 1U, 1U, 110U); // [状態] - 受信者 1 から余裕 10 の受信報告を受理し、監視対象にする。
    AcceptReport(&ctx, 2U, 1U, 140U); // [状態] - 受信者 2 から余裕 40 の受信報告を受理する。
    AcceptFlowControl(&ctx, 1U, (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME, 130U);
    // [状態] - 監視対象の受信者 1 から送出上限 130 を受理する。
    // 変化の記録を消す目的だけで一覧を取り出し、台数はこの時点では確認しないため、戻り値は明示的に破棄する。
    (void)potr_internal_receiver_monitor_list(&ctx, now_ms, list, POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX, &changed);
    // [状態] - 変化の記録を消すため、一覧を 1 回取り出す。
    uint64_t limit_before = FindReceiver(&ctx, 1U)->flow_send_limit;
    // Pre-Assert
    // Act
    int actual_ret = AcceptReport(&ctx, 1U, 0U, 0U); // [手順] - 受信者 1 から送出上限がない受信報告を受理する。
    size_t count = potr_internal_receiver_monitor_list(&ctx, now_ms, list, POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX,
                                                       &changed);
    // [手順] - 監視対象の一覧を取り出す。
    // Assert
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 130U, limit_before); // [確認_正常系] - 外れる前は受信者 1 の送出上限を保持していること。
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 送出上限がない受信報告を受理すること。
    EXPECT_EQ(0, FindReceiver(&ctx, 1U)->flow_margin_known); // [確認_正常系] - 受信者 1 の余裕の記録が消えること。
    EXPECT_EQ(0, FindReceiver(&ctx, 1U)->flow_monitored); // [確認_正常系] - 受信者 1 が監視対象から外れること。
    EXPECT_EQ(0U, FindReceiver(&ctx, 1U)->flow_send_limit); // [確認_正常系] - 外れた受信者 1 の送出上限を破棄すること。
    EXPECT_EQ(1, FindReceiver(&ctx, 2U)->flow_monitored); // [確認_正常系] - 受信者 2 が監視対象になること。
    EXPECT_EQ(1U, count); // [確認_正常系] - 一覧が 1 台であること。
    EXPECT_EQ(2U, list[0].session_id); // [確認_正常系] - 一覧の受信者が受信者 2 であること。
    EXPECT_EQ(1, changed); // [確認_正常系] - 監視対象が変わったことを返すこと。
    // Cleanup
    FreeReceivers(&ctx);
}

// 監視対象でない受信者の抑制の要求で余裕 0 の候補になり、その要求の送出上限は使わないことの確認
TEST(receiverReportTest, suppress_from_non_monitored_receiver_becomes_candidate)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000;
    SetupMonitoringSender(&ctx, 1U, 1U);
    ctx.send_window.next_seq = 100U;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 受理の時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    AcceptReport(&ctx, 1U, 1U, 150U); // [状態] - 受信者 1 から余裕 50 の受信報告を受理し、監視対象にする。
    AcceptReport(&ctx, 2U, 1U, 180U); // [状態] - 受信者 2 から余裕 80 の受信報告を受理する (監視対象でない)。
    // Pre-Assert
    // Act
    int actual_ret_suppress = AcceptFlowControl(&ctx, 2U, (uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS, 105U);
    // [手順] - 次の受信報告を待たずに、監視対象でない受信者 2 から、送出上限 105 の抑制の要求を受理する。
    potr_internal_receiver *requester = FindReceiver(&ctx, 2U);
    uint64_t limit_after_suppress = requester != nullptr ? requester->flow_send_limit : 99U;
    int actual_ret_resume = AcceptFlowControl(&ctx, 2U, (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME, 160U);
    // [手順] - 監視対象になった受信者 2 から、送出上限 160 の解除を受理する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_suppress); // [確認_正常系] - 抑制の要求を受理すること。
    ASSERT_NE(nullptr, requester); // [確認_正常系] - 受信者 2 の記録があること。
    EXPECT_EQ(0U, limit_after_suppress); // [確認_正常系] - 監視対象でない間に受けた送出上限を保持しないこと。
    EXPECT_EQ(1, requester->flow_margin_known); // [確認_正常系] - 受信者 2 の余裕の記録があること。
    EXPECT_EQ(0, requester->flow_margin); // [確認_正常系] - 受信者 2 の余裕が 0 であること。
    EXPECT_EQ(1, requester->flow_monitored); // [確認_正常系] - 余裕 0 の受信者 2 が監視対象になること。
    EXPECT_EQ(0, FindReceiver(&ctx, 1U)->flow_monitored); // [確認_正常系] - 受信者 1 が監視対象から外れること。
    EXPECT_EQ(POTR_OK, actual_ret_resume); // [確認_正常系] - 解除を受理すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 160U, requester->flow_send_limit);
    // [確認_正常系] - 監視対象になった後に受けた送出上限 160 を保持すること。
    // Cleanup
    FreeReceivers(&ctx);
}

// 監視対象でない受信者のフロー制御パケットの送出上限を使わず、抑制の要求と解除は受理することの確認
TEST(receiverReportTest, send_limit_of_non_monitored_receiver_is_ignored)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000;
    SetupMonitoringSender(&ctx, 1U, 1U);
    ctx.send_window.next_seq = 100U;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 受理の時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    AcceptReport(&ctx, 1U, 1U, 110U); // [状態] - 受信者 1 から余裕 10 の受信報告を受理し、監視対象にする。
    AcceptReport(&ctx, 2U, 1U, 150U); // [状態] - 受信者 2 から余裕 50 の受信報告を受理する (監視対象でない)。
    // Pre-Assert
    // Act
    int actual_ret_monitored = AcceptFlowControl(&ctx, 1U, (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME, 140U);
    // [手順] - 監視対象の受信者 1 から送出上限 140 の解除を受理する。
    int actual_ret_other = AcceptFlowControl(&ctx, 2U, (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME, 120U);
    // [手順] - 監視対象でない受信者 2 から送出上限 120 の解除を受理する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_monitored); // [確認_正常系] - 監視対象の受信者の解除を受理すること。
    EXPECT_EQ(POTR_OK, actual_ret_other); // [確認_正常系] - 監視対象でない受信者の解除を受理すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 140U, FindReceiver(&ctx, 1U)->flow_send_limit);
    // [確認_正常系] - 監視対象の受信者の送出上限 140 を保持すること。
    EXPECT_EQ(0U, FindReceiver(&ctx, 2U)->flow_send_limit); // [確認_正常系] - 監視対象でない受信者の送出上限を保持しないこと。
    EXPECT_EQ(0, FindReceiver(&ctx, 2U)->flow_monitored); // [確認_正常系] - 解除では監視対象を変えないこと。
    // Cleanup
    FreeReceivers(&ctx);
}

// 1 対 N の判定の条件が両方とも無効の場合に、監視対象を選ばないことの確認
TEST(receiverReportTest, monitor_list_without_condition_selects_none)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_packet_session_hdr list[POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX] = {};
    uint64_t now_ms = 1000;
    int changed = -1;
    SetupMonitoringSender(&ctx, 0U, 4U);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 受理の時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    AcceptReport(&ctx, 1U, 1U, 110U); // [状態] - 台数と割合の条件が両方とも無効の送信者で、受信者 1 の受信報告を受理する。
    // Pre-Assert
    // Act
    size_t count = potr_internal_receiver_monitor_list(&ctx, now_ms, list, POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX,
                                                       &changed);
    // [手順] - 監視対象の一覧を取り出す。
    // Assert
    EXPECT_EQ(0U, count); // [確認_正常系] - 監視対象が 0 台であること。
    EXPECT_EQ(0, changed); // [確認_正常系] - 監視対象が変わっていないことを返すこと。
    EXPECT_EQ(0, ctx.receivers->flow_monitored); // [確認_正常系] - 受信者が監視対象でないこと。
    // Cleanup
    FreeReceivers(&ctx);
}

// 受信報告が途絶えた監視対象を、一覧の取り出しで外すことの確認
TEST(receiverReportTest, monitor_list_reselects_when_receiver_goes_inactive)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_packet_session_hdr list[POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX] = {};
    uint64_t now_ms = 1000;
    int changed_before = -1;
    int changed_after = -1;
    SetupMonitoringSender(&ctx, 1U, 1U);
    ctx.send_window.next_seq = 100U;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 受理の時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    AcceptReport(&ctx, 1U, 1U, 110U); // [状態] - 時刻 1000 に受信者 1 から余裕 10 の受信報告を受理する。
    now_ms = 1500;
    AcceptReport(&ctx, 2U, 1U, 150U); // [状態] - 時刻 1500 に受信者 2 から余裕 50 の受信報告を受理する。
    size_t count_before = potr_internal_receiver_monitor_list(&ctx, now_ms, list,
                                                              POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX, &changed_before);
    uint32_t session_before = list[0].session_id;
    // [状態] - 一覧を取り出し、受信者 1 が監視対象であることを記録する。
    // Pre-Assert
    // Act
    now_ms = 2000;
    size_t count_after = potr_internal_receiver_monitor_list(&ctx, now_ms, list,
                                                             POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX, &changed_after);
    // [手順] - 受信者 1 の受信報告が health_timeout_ms (1000) 途絶えた時刻 2000 に、一覧を取り出す。
    // Assert
    EXPECT_EQ(1U, count_before);    // [確認_正常系] - 途絶える前の一覧が 1 台であること。
    EXPECT_EQ(1U, session_before);  // [確認_正常系] - 途絶える前の監視対象が受信者 1 であること。
    EXPECT_EQ(1U, count_after);     // [確認_正常系] - 途絶えた後の一覧が 1 台であること。
    EXPECT_EQ(2U, list[0].session_id); // [確認_正常系] - 途絶えた後の監視対象が受信者 2 であること。
    EXPECT_EQ(1, changed_after);    // [確認_正常系] - 監視対象が変わったことを返すこと。
    // Cleanup
    FreeReceivers(&ctx);
}

// multicast と broadcast の送信者以外と、引数が NULL の場合に、一覧が 0 台になることの確認
TEST(receiverReportTest, monitor_list_other_types_and_null_arguments)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_packet_session_hdr list[1] = {};
    int changed = -1;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.role = POTR_ROLE_SENDER;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - unicast の送信者とする。
    // Pre-Assert
    // Act
    size_t count_unicast = potr_internal_receiver_monitor_list(&ctx, 1000U, list, 1U, &changed);
    // [手順] - unicast の送信者で一覧を取り出す。
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.role = POTR_ROLE_RECEIVER;
    size_t count_receiver = potr_internal_receiver_monitor_list(&ctx, 1000U, list, 1U, &changed);
    // [手順] - multicast の受信者で一覧を取り出す。
    size_t count_null_ctx = potr_internal_receiver_monitor_list(nullptr, 1000U, list, 1U, &changed);
    size_t count_null_list = potr_internal_receiver_monitor_list(&ctx, 1000U, nullptr, 1U, &changed);
    size_t count_null_changed = potr_internal_receiver_monitor_list(&ctx, 1000U, list, 1U, nullptr);
    // [手順] - ctx、list、changed を順に NULL にして一覧を取り出す。
    // Assert
    EXPECT_EQ(0U, count_unicast);      // [確認_正常系] - unicast の送信者で 0 台であること。
    EXPECT_EQ(0U, count_receiver);     // [確認_正常系] - multicast の受信者で 0 台であること。
    EXPECT_EQ(0, changed);             // [確認_正常系] - 監視対象が変わっていないことを返すこと。
    EXPECT_EQ(0U, count_null_ctx);     // [確認_異常系] - ctx が NULL で 0 台であること。
    EXPECT_EQ(0U, count_null_list);    // [確認_異常系] - list が NULL で 0 台であること。
    EXPECT_EQ(0U, count_null_changed); // [確認_異常系] - changed が NULL で 0 台であること。
}

// 受信報告を受けていない受信者の抑制の要求で、受信報告を待たずに監視対象の候補になることの確認
TEST(receiverReportTest, suppress_without_report_becomes_candidate)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000;
    SetupMonitoringSender(&ctx, 1U, 1U);
    ctx.send_window.next_seq = 100U;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 受理の時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    // Pre-Assert
    // Act
    int actual_ret = AcceptFlowControl(&ctx, 2U, (uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS, 105U);
    // [手順] - 受信報告を受けていない受信者 2 から、抑制の要求を受理する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 抑制の要求を受理すること。
    ASSERT_NE(nullptr, ctx.receivers); // [確認_正常系] - 受信者 2 の記録があること。
    EXPECT_EQ(0, ctx.receivers->active); // [確認_正常系] - 受信報告を受けていないため、受信報告による活動中でないこと。
    EXPECT_EQ(1, ctx.receivers->flow_margin_known); // [確認_正常系] - 余裕の記録があること。
    EXPECT_EQ(0, ctx.receivers->flow_margin); // [確認_正常系] - 余裕が 0 であること。
    EXPECT_EQ(1, ctx.receivers->flow_monitored); // [確認_正常系] - 有効な要求で活動中として数え、監視対象になること。
    EXPECT_EQ(0U, ctx.receivers->flow_send_limit); // [確認_正常系] - 監視対象になる前に受けた送出上限は保持しないこと。
    // Cleanup
    FreeReceivers(&ctx);
}
