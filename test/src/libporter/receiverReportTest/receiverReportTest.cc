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
    receiver->flow_send_limit = POTR_FLOW_SEND_LIMIT_UNLIMITED;
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

// multicast で 1 台でも要求中であれば抑制することの確認
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
    EXPECT_EQ(1, actual_one); // [確認_正常系] - 要求中 1 台で抑制すること。
    EXPECT_EQ(1U, requesting_one); // [確認_正常系] - 要求中の受信者数が 1 であること。
    EXPECT_EQ(1, actual_two); // [確認_正常系] - 要求中 2 台でも抑制すること。
    EXPECT_EQ(2U, requesting_two); // [確認_正常系] - 要求中の受信者数が 2 であること。
    // Cleanup
    FreeReceivers(&ctx);
}

// multicast で活動中 1 台でも 3 台のうち 1 台でも、要求中があれば抑制することの確認
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
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    potr_internal_receiver *receiver = AddReceiver(&ctx, 1, now_ms, 1300);
    // Pre-Assert
    // Act
    int actual_all = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, &requesting, &until_ms);
    // [手順] - 活動中 1 台の受信者が要求中の状態で判定する。
    // Assert
    EXPECT_EQ(1, actual_all); // [確認_正常系] - 要求中 1 台で抑制すること。
    EXPECT_EQ(1U, requesting); // [確認_正常系] - 要求中の受信者数が 1 であること。
    EXPECT_EQ(1300U, until_ms); // [確認_正常系] - 最も早い期限が要求の期限であること。
    // Arrange_2
    receiver->flow_request_expire_ms = 0;
    AddReceiver(&ctx, 1, now_ms, 0);
    AddReceiver(&ctx, 1, now_ms, 0);
    ctx.receivers->flow_request_expire_ms = 1300;
    // Act_2
    int actual_partial = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, &requesting_partial, &until_partial);
    // [手順] - 活動中 3 台のうち 1 台だけが要求中の状態で判定する。
    // Assert_2
    EXPECT_EQ(1, actual_partial); // [確認_正常系] - 活動中 3 台のうち 1 台の要求でも抑制すること。
    EXPECT_EQ(1U, requesting_partial); // [確認_正常系] - 要求中の受信者数が 1 であること。
    // Cleanup
    FreeReceivers(&ctx);
}

// multicast で活動中 3 台のうち 2 台または 3 台が要求中の場合に抑制することの確認
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
    // [手順] - 活動中 3 台のうち 2 台、続いて 3 台が要求中の状態で判定する。
    // Assert
    EXPECT_EQ(1, actual_two); // [確認_正常系] - 要求中 2 台で抑制すること。
    EXPECT_EQ(2U, requesting_two); // [確認_正常系] - 要求中の受信者数が 2 であること。
    EXPECT_EQ(1, actual_three); // [確認_正常系] - 活動中の全員 (3 台) の要求で抑制すること。
    EXPECT_EQ(3U, requesting_three); // [確認_正常系] - 要求中の受信者数が 3 であること。
    // Cleanup
    FreeReceivers(&ctx);
}

// multicast で活動中 10 台のうち 4 台または 5 台が要求中の場合に抑制することの確認
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
    // [手順] - 活動中 10 台のうち 4 台、続いて 5 台が要求中の状態で判定する。
    // Assert
    EXPECT_EQ(1, actual_four); // [確認_正常系] - 要求中 4 台で抑制すること。
    EXPECT_EQ(4U, requesting_four); // [確認_正常系] - 要求中の受信者数が 4 であること。
    EXPECT_EQ(1, actual_five); // [確認_正常系] - 要求中 5 台でも抑制すること。
    EXPECT_EQ(5U, requesting_five); // [確認_正常系] - 要求中の受信者数が 5 であること。
    // Cleanup
    FreeReceivers(&ctx);
}

// broadcast でも要求中の受信者がいれば抑制し、要求中の数を返すことの確認
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
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    AddReceiver(&ctx, 1, now_ms, 1300);
    AddReceiver(&ctx, 1, now_ms, 1400);
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 0U, &requesting, &until_ms);
    // [手順] - broadcast で、要求中の受信者が 2 台いる状態を判定する。
    // Assert
    EXPECT_EQ(1, actual_ret); // [確認_正常系] - 要求中の受信者がいれば抑制すること。
    EXPECT_EQ(2U, requesting); // [確認_正常系] - 要求中の受信者数を返すこと。
    // Cleanup
    FreeReceivers(&ctx);
}

// 受信報告を受けていない受信者でも、有効な要求があれば抑制することの確認
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
    EXPECT_EQ(1, actual_ret); // [確認_正常系] - 要求中の受信者がいれば抑制すること。
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
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_NO_PEER, receiver->flow_send_limit); // [確認_正常系] - 有効期限経過後に「相手なし」へ遷移すること。
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

// multicast の判定で、送出上限に達した受信者を「止める状態」として扱い、最も早い期限を返すことの確認
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
    EXPECT_EQ(1, actual_one);   // [確認_正常系] - 要求中の 1 台がいるため抑制すること。
    EXPECT_EQ(2U, tracked_one); // [確認_正常系] - 要求中と送出上限の保持の 2 台を返すこと。
    EXPECT_EQ(1500U, until_one); // [確認_正常系] - 送出上限の手前では要求の期限 1500 を返すこと。
    EXPECT_EQ(1, actual_two);   // [確認_正常系] - 送出上限に達した受信者を含めて抑制すること。
    EXPECT_EQ(1300U, until_two); // [確認_正常系] - 送出上限の破棄の時刻 1300 が、要求の期限 1500 より早いため返すこと。
    // Cleanup
    FreeReceivers(&ctx);
}

namespace
{
// 監視する数の計算の入力と期待値の組
struct monitor_count_case
{
    size_t candidates;
    size_t monitored_count; // 構造体に詰め物が入らないよう size_t で持ち、呼び出しで uint32_t へ変換する
    size_t max_payload;
    size_t expected;
};

// multicast の送信者の構成にする。監視する数の下限は monitored_count とする。
void SetupMonitoringSender(potr_context *ctx, uint32_t monitored_count)
{
    ctx->service.type = POTR_TYPE_MULTICAST;
    ctx->role = POTR_ROLE_SENDER;
    ctx->global.max_payload = 1400U;
    ctx->health_timeout_ms = 1000U;
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

// 監視する数を、下限、上限 (64 と max_payload による制限)、候補の数のそれぞれで求めることの確認
TEST(receiverReportTest, monitor_count_cases)
{
    // Arrange
    const monitor_count_case cases[] = {
        {3U, 4U, 1400U, 3U},   // 下限 4 より候補が少ない場合は候補の数 3
        {100U, 1U, 1400U, 1U}, // 既定の下限 1 で候補が多い場合は 1
        {100U, 4U, 1400U, 4U}, // 下限 4 で候補が多い場合は 4
        {100U, 20U, 1400U, 20U}, // 下限 20 で候補が多い場合は 20
        {10U, 0U, 1400U, 0U},  // 下限 0 (下限なし) の場合は 0
        {0U, 4U, 1400U, 0U},   // 候補がいない場合は 0
        {100U, 100U, 1400U, 64U}, // 下限が 64 を超える場合は上限 64
        {100U, 10U, 100U, 6U}, // max_payload 100 の上限 (100 − 2) ÷ 16 = 6
        {3U, 10U, 100U, 3U},   // max_payload 上限 6 より候補 3 が小さい場合は 3
    };
    const size_t case_count = sizeof(cases) / sizeof(cases[0]);
    size_t actual[sizeof(cases) / sizeof(cases[0])] = {};
    // [状態] - 9 通りの入力を用意する (ループ 9 回)。
    // Pre-Assert
    // Act
    for (size_t i = 0; i < case_count; i++)
    {
        actual[i] = flow_monitor_count(cases[i].candidates, (uint32_t)cases[i].monitored_count,
                                       (uint32_t)cases[i].max_payload);
    }
    // [手順] - 入力ごとに flow_monitor_count() を呼び出し、監視する数を記録する。
    // Assert
    for (size_t i = 0; i < case_count; i++)
    {
        EXPECT_EQ(cases[i].expected, actual[i]); // [確認_正常系] - 監視する数が期待値であること。(確認 9 回)
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
    SetupMonitoringSender(&ctx, 2U);
    ctx.send_window.next_seq = 100U;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 受理の時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    // [状態] - multicast の送信者で、監視する台数の下限 2、次に割り当てる通番 100 とする。
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
    SetupMonitoringSender(&ctx, 4U);
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
    SetupMonitoringSender(&ctx, 1U);
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
    SetupMonitoringSender(&ctx, 1U);
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
    SetupMonitoringSender(&ctx, 1U);
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

// 監視する数の下限が 0 (下限なし) の場合に、監視対象を選ばないことの確認
TEST(receiverReportTest, monitor_list_without_condition_selects_none)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_packet_session_hdr list[POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX] = {};
    uint64_t now_ms = 1000;
    int changed = -1;
    SetupMonitoringSender(&ctx, 0U);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 受理の時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    AcceptReport(&ctx, 1U, 1U, 110U); // [状態] - 監視する数の下限が 0 の送信者で、受信者 1 の受信報告を受理する。
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
    SetupMonitoringSender(&ctx, 1U);
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
    SetupMonitoringSender(&ctx, 1U);
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

// 片方向 UDP の送信者が、問い合わせを破棄し、「上限なし」で送出上限を解除することの確認
TEST(receiverReportTest, flow_control_query_rejected_and_unlimited_clears_limit)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000;
    uint8_t query_payload[POTR_FLOW_CONTROL_QUERY_PAYLOAD_SIZE] = {(uint8_t)POTR_FLOW_CONTROL_STATE_QUERY};
    uint8_t wire[POTR_PACKET_HEADER_WIRE_SIZE] = {};
    potr_packet query = {};
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.role = POTR_ROLE_SENDER;
    ctx.flow_control_config.request_lifetime_ms = 1000U;
    query.flags = POTR_FLAG_FLOW_CONTROL;
    query.session_id = 1U;
    query.payload = query_payload;
    query.payload_len = POTR_FLOW_CONTROL_QUERY_PAYLOAD_SIZE;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 受理の時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    ASSERT_EQ(POTR_OK, AcceptFlowControl(&ctx, 1U, (uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS, 50U));
    // [状態] - 受信者 1 の抑制の要求と送出上限 50 を登録する。
    // Pre-Assert
    // Act
    int query_ret = potr_internal_receiver_accept(&ctx, &query, wire);
    // [手順] - 問い合わせパケットを potr_internal_receiver_accept() に渡す。
    int unlimited_ret = AcceptFlowControl(&ctx, 1U, (uint8_t)POTR_FLOW_CONTROL_STATE_UNLIMITED, 0U);
    // [手順] - 状態「上限なし」(送出上限 0) を potr_internal_receiver_accept() に渡す。
    // Assert
    EXPECT_EQ(POTR_ERR_PROTOCOL, query_ret); // [確認_異常系] - 送信者宛の問い合わせを POTR_ERR_PROTOCOL で破棄すること。
    EXPECT_EQ(POTR_OK, unlimited_ret);       // [確認_正常系] - 状態「上限なし」を受理すること。
    ASSERT_NE(nullptr, ctx.receivers);       // [確認_正常系] - 受信者 1 の記録があること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNLIMITED, ctx.receivers->flow_send_limit); // [確認_正常系] - 状態が「上限なし (通知済み)」になること。
    EXPECT_EQ(0U, ctx.receivers->flow_request_expire_ms); // [確認_正常系] - 抑制の要求の期限が 0 に戻ること。
    // Cleanup
    FreeReceivers(&ctx);
}

// 片方向 unicast で、受信報告の受理中は破棄せず、無応答で相手なしへ遷移し、再到着で未通知へ戻して問い合わせを送出することの確認
TEST(receiverReportTest, unicast_last_packet_keeps_limit_and_no_peer_recovers_with_query)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000U;
    uint8_t report_payload[POTR_MAX_PATH] = {};
    uint8_t wire[POTR_PACKET_HEADER_WIRE_SIZE] = {};
    potr_packet report = {};
    size_t tracked = 0U;
    uint64_t until_ms = 0U;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.role = POTR_ROLE_SENDER;
    ctx.flow_control_config.request_lifetime_ms = 500U;
    ctx.send_window.next_seq = 10U;
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 10U, CPLAT_MEMORY_ORDER_RELAXED);
    report.flags = POTR_FLAG_REPORT;
    report.session_id = 1U;
    report.payload = report_payload;
    report.payload_len = POTR_MAX_PATH;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 受理の時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_send_query(&ctx, nullptr)).Times(1);
    // [Pre-Assert確認_正常系] - 相手なしからの復帰時に問い合わせを 1 回送出すること。
    // Act
    ASSERT_EQ(POTR_OK, potr_internal_receiver_accept(&ctx, &report, wire));
    // [手順] - 時刻 1000、次の通番 10 で初回 REPORT を受理する (未通知: 基準 10、上限 14)。
    uint64_t state_initial = ctx.receivers->flow_send_limit;
    int stalled = potr_internal_receiver_flow_suppressed(&ctx, 1000U, 14U, &tracked, &until_ms);
    now_ms = 1400U;
    ASSERT_EQ(POTR_OK, potr_internal_receiver_accept(&ctx, &report, wire));
    int kept = potr_internal_receiver_flow_suppressed(&ctx, 1700U, 14U, &tracked, &until_ms);
    // [手順] - 時刻 1400 に REPORT を受理し、元の期限 (1500) 後の時刻 1700 で判定する。
    int expired = potr_internal_receiver_flow_suppressed(&ctx, 1900U, 14U, &tracked, &until_ms);
    uint64_t state_no_peer = ctx.receivers->flow_send_limit;
    // [手順] - 1400 から有効期限 500 [ms] 後の時刻 1900 で相手なしへ遷移させる。
    ctx.send_window.next_seq = 40U;
    now_ms = 2100U;
    ASSERT_EQ(POTR_OK, potr_internal_receiver_accept(&ctx, &report, wire));
    // [手順] - 相手なし状態で次の通番 40 のときに REPORT を再受理する。
    // Assert
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 10U, state_initial); // [確認_正常系] - 初回 REPORT で基準 10 の未通知になること。
    EXPECT_EQ(1, stalled); // [確認_正常系] - 初期の幅 4 に達した通番 14 で止まること。
    EXPECT_EQ(1, kept);    // [確認_正常系] - REPORT 到着中は破棄しないこと。
    EXPECT_EQ(0, expired); // [確認_正常系] - 無応答で有効期限経過後に破棄すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_NO_PEER, state_no_peer); // [確認_正常系] - 「相手なし」へ遷移すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 40U, ctx.receivers->flow_send_limit);
    // [確認_正常系] - 再到着でその時点の通番 40 を基準とする未通知へ戻ること。
    // Cleanup
    FreeReceivers(&ctx);
}

// 片方向 unicast で、最後の DATA 送出から有効期限以上途切れた場合に保持中の送出上限を初期の幅へ短縮することの確認
TEST(receiverReportTest, unicast_idle_send_clamps_limit_at_lifetime)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    size_t tracked = 0U;
    uint64_t until_ms = 0U;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.role = POTR_ROLE_SENDER;
    ctx.flow_control_config.request_lifetime_ms = 500U;
    ctx.flow_last_data_send_ms = 1000U;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    potr_internal_receiver *receiver = AddReceiver(&ctx, 1, 1000U, 0);
    HoldSendLimit(receiver, 100U, 1000U);
    // [状態] - 受信者 1 が送出上限 100、最終 DATA 送出 1000、有効期限 500 [ms] の状態にする。
    // Pre-Assert
    // Act
    int before = potr_internal_receiver_flow_suppressed(&ctx, 1499U, 10U, &tracked, &until_ms);
    uint64_t limit_before = receiver->flow_send_limit;
    int at_boundary = potr_internal_receiver_flow_suppressed(&ctx, 1500U, 10U, &tracked, &until_ms);
    uint64_t limit_after = receiver->flow_send_limit;
    // [手順] - 有効期限未満 (1499) とちょうど (1500) に、次の通番 10 で判定する。
    // Assert
    EXPECT_EQ(0, before);                                      // [確認_正常系] - 1499 では抑制しないこと。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 100U, limit_before); // [確認_正常系] - 有効期限未満では短縮しないこと。
    EXPECT_EQ(0, at_boundary);                                 // [確認_正常系] - 1500 では初期の幅 4 の手前のため抑制しないこと。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 14U, limit_after);   // [確認_正常系] - 有効期限ちょうどで 10 + 4 = 14 へ短縮すること。
    // Cleanup
    FreeReceivers(&ctx);
}

// unicast で受信報告の送出上限が未通知の初回通知として機能し、保持中は先へ進んだ場合だけ更新されることの確認
TEST(receiverReportTest, unicast_report_recovers_send_limit_only_when_advancing)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000U;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.role = POTR_ROLE_SENDER;
    ctx.flow_control_config.request_lifetime_ms = 500U;
    ctx.send_window.next_seq = 10U;
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 10U, CPLAT_MEMORY_ORDER_RELAXED);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 受理の時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 単一スレッドのテストとして排他操作を置換する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_send_query(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - 送出上限付きの受信報告では問い合わせを送出しないこと。
    // Act
    int ret_first = AcceptReport(&ctx, 1U, 1U, 50U);
    uint64_t limit_first = ctx.receivers != nullptr ? ctx.receivers->flow_send_limit : 0U;
    uint64_t received_first = ctx.receivers != nullptr ? ctx.receivers->flow_limit_received_ms : 0U;
    int pending_first = cplat_atomic_load_i32(&ctx.flow_request_pending, CPLAT_MEMORY_ORDER_RELAXED);
    // [手順] - 未通知の初回受信報告で送出上限 50 を受理する。
    now_ms = 1100U;
    int ret_older = AcceptReport(&ctx, 1U, 1U, 40U);
    int ret_same = AcceptReport(&ctx, 1U, 1U, 50U);
    uint64_t limit_after_stale = ctx.receivers != nullptr ? ctx.receivers->flow_send_limit : 0U;
    uint64_t received_after_stale = ctx.receivers != nullptr ? ctx.receivers->flow_limit_received_ms : 0U;
    // [手順] - 保持中の 50 より古い 40 と同じ 50 の受信報告を受理する。
    now_ms = 1200U;
    int ret_advance = AcceptReport(&ctx, 1U, 1U, 80U);
    uint64_t limit_after_advance = ctx.receivers != nullptr ? ctx.receivers->flow_send_limit : 0U;
    uint64_t received_after_advance = ctx.receivers != nullptr ? ctx.receivers->flow_limit_received_ms : 0U;
    // [手順] - 先へ進んだ 80 の受信報告を受理する。
    ctx.receivers->flow_send_limit = POTR_FLOW_SEND_LIMIT_HELD | 0xFFFFFFF0U;
    now_ms = 1300U;
    int ret_wrap_advance = AcceptReport(&ctx, 1U, 1U, 0x10U);
    uint64_t limit_after_wrap = ctx.receivers != nullptr ? ctx.receivers->flow_send_limit : 0U;
    now_ms = 1400U;
    int ret_wrap_older = AcceptReport(&ctx, 1U, 1U, 0xFFFFFFF8U);
    uint64_t limit_after_wrap_older = ctx.receivers != nullptr ? ctx.receivers->flow_send_limit : 0U;
    // [手順] - 通番の循環をまたぐ前進 (0xFFFFFFF0 -> 0x10) と後退 (0x10 -> 0xFFFFFFF8) を受理する。
    ctx.receivers->flow_send_limit = POTR_FLOW_SEND_LIMIT_UNLIMITED;
    int ret_unlimited = AcceptReport(&ctx, 1U, 1U, 100U);
    uint64_t limit_after_unlimited = ctx.receivers != nullptr ? ctx.receivers->flow_send_limit : 0U;
    // [手順] - 「上限なし」の状態で送出上限付きの受信報告を受理する。
    int ret_no_limit_report = AcceptReport(&ctx, 2U, 0U, 0U);
    uint64_t limit_after_no_limit_report = FindReceiver(&ctx, 2U)->flow_send_limit;
    int ret_existing_unnotified = AcceptReport(&ctx, 2U, 1U, 60U);
    uint64_t limit_after_existing_unnotified = FindReceiver(&ctx, 2U)->flow_send_limit;
    FindReceiver(&ctx, 2U)->flow_send_limit = POTR_FLOW_SEND_LIMIT_NO_PEER;
    int ret_from_no_peer_with_limit = AcceptReport(&ctx, 2U, 1U, 70U);
    uint64_t limit_after_no_peer_with_limit = FindReceiver(&ctx, 2U)->flow_send_limit;
    // [手順] - 受信者 2 で送出上限なし受信報告、未通知からの送出上限付き受信報告、相手なしからの送出上限付き受信報告を受理する。
    // Assert
    EXPECT_EQ(POTR_OK, ret_first);                                     // [確認_正常系] - 初回の受信報告を受理すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 50U, limit_first);           // [確認_正常系] - 未通知から最初の通知として 50 を保持すること。
    EXPECT_EQ(1000U, received_first);                                  // [確認_正常系] - 受理時刻 1000 を記録すること。
    EXPECT_EQ(1, pending_first);                                       // [確認_正常系] - flow_request_pending を 1 にすること。
    EXPECT_EQ(POTR_OK, ret_older);                                     // [確認_正常系] - 古い送出上限の受信報告も受理すること。
    EXPECT_EQ(POTR_OK, ret_same);                                      // [確認_正常系] - 同じ送出上限の受信報告も受理すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 50U, limit_after_stale);     // [確認_正常系] - 古い値や同じ値で送出上限を上書きしないこと。
    EXPECT_EQ(1000U, received_after_stale);                            // [確認_正常系] - 更新しない場合は受理時刻を変えないこと。
    EXPECT_EQ(POTR_OK, ret_advance);                                   // [確認_正常系] - 先へ進んだ受信報告を受理すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 80U, limit_after_advance);   // [確認_正常系] - 先へ進んだ 80 へ更新すること。
    EXPECT_EQ(1200U, received_after_advance);                          // [確認_正常系] - 更新した時刻 1200 を記録すること。
    EXPECT_EQ(POTR_OK, ret_wrap_advance);                              // [確認_正常系] - 循環をまたぐ前進を受理すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 0x10U, limit_after_wrap);    // [確認_正常系] - 循環をまたぐ前進で 0x10 へ更新すること。
    EXPECT_EQ(POTR_OK, ret_wrap_older);                                // [確認_正常系] - 循環をまたぐ古い値も受理すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 0x10U, limit_after_wrap_older); // [確認_正常系] - 循環をまたぐ古い値で戻さないこと。
    EXPECT_EQ(POTR_OK, ret_unlimited);                                 // [確認_正常系] - 「上限なし」でも受信報告を受理すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNLIMITED, limit_after_unlimited);  // [確認_正常系] - 「上限なし」を上書きしないこと。
    EXPECT_EQ(POTR_OK, ret_no_limit_report);                           // [確認_正常系] - 送出上限なしの受信報告を受理すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 10U, limit_after_no_limit_report);
    // [確認_正常系] - 送出上限なしの受信報告を「上限なし」として扱わず未通知のままとすること。
    EXPECT_EQ(POTR_OK, ret_existing_unnotified);                       // [確認_正常系] - 未通知の既存受信者への送出上限付き受信報告を受理すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 60U, limit_after_existing_unnotified);
    // [確認_正常系] - 未通知の既存受信者が最初の通知として 60 を保持すること。
    EXPECT_EQ(POTR_OK, ret_from_no_peer_with_limit);                   // [確認_正常系] - 相手なしからの送出上限付き受信報告を受理すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 70U, limit_after_no_peer_with_limit);
    // [確認_正常系] - 相手なしから送出上限 70 を保持すること。
    // Cleanup
    FreeReceivers(&ctx);
}

// multicast の監視対象で、保持中の送出上限より先へ進んだ受信報告だけで更新し、非監視対象と未保持では更新しないことの確認
TEST(receiverReportTest, multicast_monitored_report_recovers_send_limit_only_when_advancing)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000U;
    SetupMonitoringSender(&ctx, 1U);
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
    int ret_init_1 = AcceptReport(&ctx, 1U, 1U, 110U);
    int ret_init_2 = AcceptReport(&ctx, 2U, 1U, 180U);
    uint64_t limit_before_fc = FindReceiver(&ctx, 1U)->flow_send_limit;
    // [手順] - 受信者 1 (監視対象) と受信者 2 (非監視対象) の初回受信報告を受理する。
    int ret_fc = AcceptFlowControl(&ctx, 1U, (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME, 120U);
    // [手順] - 監視対象の受信者 1 からフロー制御パケットで送出上限 120 を受理する。
    cplat_atomic_store_i32(&ctx.flow_request_pending, 0, CPLAT_MEMORY_ORDER_RELAXED);
    now_ms = 1100U;
    int ret_older = AcceptReport(&ctx, 1U, 1U, 115U);
    int ret_same = AcceptReport(&ctx, 1U, 1U, 120U);
    uint64_t limit_after_stale = FindReceiver(&ctx, 1U)->flow_send_limit;
    uint64_t received_after_stale = FindReceiver(&ctx, 1U)->flow_limit_received_ms;
    // [手順] - 監視対象の受信者 1 から古い 115 と同じ 120 の受信報告を受理する。
    now_ms = 1200U;
    int ret_advance = AcceptReport(&ctx, 1U, 1U, 135U);
    uint64_t limit_after_advance = FindReceiver(&ctx, 1U)->flow_send_limit;
    uint64_t received_after_advance = FindReceiver(&ctx, 1U)->flow_limit_received_ms;
    int pending_after_advance = cplat_atomic_load_i32(&ctx.flow_request_pending, CPLAT_MEMORY_ORDER_RELAXED);
    // [手順] - 監視対象の受信者 1 から先へ進んだ 135 の受信報告を受理する。
    int ret_non_monitored = AcceptReport(&ctx, 2U, 1U, 200U);
    uint64_t limit_non_monitored = FindReceiver(&ctx, 2U)->flow_send_limit;
    // [手順] - 非監視対象の受信者 2 から送出上限 200 の受信報告を受理する。
    // Assert
    EXPECT_EQ(POTR_OK, ret_init_1);                                    // [確認_正常系] - 受信者 1 の初回受信報告を受理すること。
    EXPECT_EQ(POTR_OK, ret_init_2);                                    // [確認_正常系] - 受信者 2 の初回受信報告を受理すること。
    EXPECT_EQ(0U, limit_before_fc);                                    // [確認_正常系] - 送出上限未保持の監視対象では受信報告で保持を始めないこと。
    EXPECT_EQ(POTR_OK, ret_fc);                                        // [確認_正常系] - フロー制御パケットを受理すること。
    EXPECT_EQ(POTR_OK, ret_older);                                     // [確認_正常系] - 古い送出上限の受信報告を受理すること。
    EXPECT_EQ(POTR_OK, ret_same);                                      // [確認_正常系] - 同じ送出上限の受信報告を受理すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 120U, limit_after_stale);    // [確認_正常系] - 古い値や同じ値で送出上限を上書きしないこと。
    EXPECT_EQ(1000U, received_after_stale);                            // [確認_正常系] - 更新しない場合は受理時刻を変えないこと。
    EXPECT_EQ(POTR_OK, ret_advance);                                   // [確認_正常系] - 先へ進んだ受信報告を受理すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 135U, limit_after_advance);  // [確認_正常系] - 監視対象の送出上限が 135 へ更新されること。
    EXPECT_EQ(1200U, received_after_advance);                          // [確認_正常系] - 更新時刻 1200 を記録すること。
    EXPECT_EQ(1, pending_after_advance);                               // [確認_正常系] - flow_request_pending を 1 にすること。
    EXPECT_EQ(POTR_OK, ret_non_monitored);                             // [確認_正常系] - 非監視対象の受信報告を受理すること。
    EXPECT_EQ(0U, limit_non_monitored);                                // [確認_正常系] - 非監視対象の送出上限は保持しないこと。
    // Cleanup
    FreeReceivers(&ctx);
}

// unicast で新しい受信者を作る際、ハンドル側が未通知なら基準通番を引き継ぎ、相手なしなら現在の通番を基準にすることの確認
TEST(receiverReportTest, unicast_new_receiver_inherits_handle_initial_base_seq)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000U;
    size_t tracked = 0U;
    uint64_t until_ms = 0U;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.role = POTR_ROLE_SENDER;
    ctx.flow_control_config.request_lifetime_ms = 500U;
    ctx.send_window.next_seq = 14U;
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 10U, CPLAT_MEMORY_ORDER_RELAXED);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 受理の時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - ハンドル側が未通知 (基準 10) で、すでに初期の幅 4 個を送出して next_seq が 14 に進んだ状態とする。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_send_query(&ctx, nullptr)).Times(1);
    // [Pre-Assert確認_正常系] - ハンドル側が相手なしから復帰するときだけ問い合わせを 1 回送出すること。
    // Act
    int ret_report = AcceptReport(&ctx, 1U, 0U, 0U);
    uint64_t limit_from_unnotified = FindReceiver(&ctx, 1U)->flow_send_limit;
    int stalled_at_14 = potr_internal_receiver_flow_suppressed(&ctx, now_ms, 14U, &tracked, &until_ms);
    // [手順] - 送出上限のない受信報告を受理し、基準 10 が引き継がれて通番 14 で止まることを確認する。
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_NO_PEER, CPLAT_MEMORY_ORDER_RELAXED);
    ctx.send_window.next_seq = 30U;
    int ret_from_no_peer = AcceptReport(&ctx, 2U, 0U, 0U);
    uint64_t limit_from_no_peer = FindReceiver(&ctx, 2U)->flow_send_limit;
    // [手順] - ハンドル側が相手なし、next_seq が 30 の状態で新しい受信者 2 の受信報告を受理する。
    ctx.send_window.next_seq = 50U;
    potr_packet nack = {};
    uint8_t wire[POTR_PACKET_HEADER_WIRE_SIZE] = {};
    nack.flags = POTR_FLAG_NACK;
    nack.session_id = 3U;
    int ret_after_unlimited = potr_internal_receiver_accept(&ctx, &nack, wire);
    uint64_t limit_after_unlimited = FindReceiver(&ctx, 3U)->flow_send_limit;
    // [手順] - ハンドル側が上限なし、next_seq が 50 の状態で新しい受信者 3 の NACK を受理する。
    // Assert
    EXPECT_EQ(POTR_OK, ret_report);                                      // [確認_正常系] - 受信者 1 の受信報告を受理すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 10U, limit_from_unnotified);
    // [確認_正常系] - ハンドル側の未通知の基準 10 を引き継ぎ、14 へ進めないこと。
    EXPECT_EQ(1, stalled_at_14);                                         // [確認_正常系] - 通番 14 で初期の幅 (10 + 4) に達して止まること。
    EXPECT_EQ(POTR_OK, ret_from_no_peer);                                // [確認_正常系] - 受信者 2 の受信報告を受理すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 30U, limit_from_no_peer);
    // [確認_正常系] - ハンドル側が相手なしだった場合は、その時点の通番 30 を基準にすること。
    EXPECT_EQ(POTR_OK, ret_after_unlimited);                             // [確認_正常系] - 受信者 3 の NACK を受理すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 50U, limit_after_unlimited);
    // [確認_正常系] - ハンドル側が上限なしだった場合は、その時点の通番 50 を基準にすること。
    // Cleanup
    FreeReceivers(&ctx);
}

namespace
{
// 除外する数の上限の計算の入力と期待値の組
struct excluded_limit_case
{
    size_t active;
    size_t excluded_count;   // 構造体に詰め物が入らないよう size_t で持ち、呼び出しで uint32_t へ変換する
    size_t excluded_percent;
    size_t expected;
};

// multicast の送信者で、除外と猶予時間を設定する。
void SetupExclusionSender(potr_context *ctx, uint32_t excluded_count, uint32_t excluded_percent, uint32_t grace_ms,
                          uint32_t monitored_count)
{
    SetupMonitoringSender(ctx, monitored_count);
    ctx->flow_control_config.excluded_receiver_count = excluded_count;
    ctx->flow_control_config.excluded_receiver_percent = excluded_percent;
    ctx->flow_control_config.grace_ms = grace_ms;
}
} // namespace

// 除外する数の上限の表 (既定値、両方無効、片方無効、活動中 − 1 の上限) を確認する
TEST(receiverReportTest, excluded_limit_cases)
{
    // Arrange
    const excluded_limit_case cases[] = {
        {0U, 1U, 10U, 0U},    // 活動中 0 (既定) → 0
        {1U, 1U, 10U, 0U},    // 活動中 1 (既定) → 0
        {3U, 1U, 10U, 1U},    // 活動中 3 (既定) → 1
        {10U, 1U, 10U, 1U},   // 活動中 10 (既定) → 1
        {50U, 1U, 10U, 5U},   // 活動中 50 (既定) → 5
        {100U, 1U, 10U, 10U}, // 活動中 100 (既定) → 10
        {10U, 0U, 0U, 0U},    // 両方が無効 → 0
        {3U, 0U, 10U, 0U},    // 台数が無効、割合 10% で活動中 3 → 切り捨て 0
        {10U, 0U, 10U, 1U},   // 台数が無効、割合 10% で活動中 10 → 1
        {50U, 0U, 10U, 5U},   // 台数が無効、割合 10% で活動中 50 → 5
        {1U, 3U, 0U, 0U},     // 割合が無効、台数 3 で活動中 1 → 0
        {2U, 3U, 0U, 1U},     // 割合が無効、台数 3 で活動中 2 → 活動中 − 1 = 1
        {3U, 3U, 0U, 2U},     // 割合が無効、台数 3 で活動中 3 → 活動中 − 1 = 2
        {5U, 3U, 0U, 3U},     // 割合が無効、台数 3 で活動中 5 → 3
        {100U, 3U, 0U, 3U},   // 割合が無効、台数 3 で活動中 100 → 3
    };
    const size_t case_count = sizeof(cases) / sizeof(cases[0]);
    size_t actual[sizeof(cases) / sizeof(cases[0])] = {};
    // [状態] - 15 通りの入力を用意する (ループ 15 回)。
    // Pre-Assert
    // Act
    for (size_t i = 0; i < case_count; i++)
    {
        actual[i] = flow_excluded_limit(cases[i].active, (uint32_t)cases[i].excluded_count,
                                        (uint32_t)cases[i].excluded_percent);
    }
    // [手順] - 入力ごとに flow_excluded_limit() を呼び出し、上限を記録する。
    // Assert
    for (size_t i = 0; i < case_count; i++)
    {
        EXPECT_EQ(cases[i].expected, actual[i]); // [確認_正常系] - 除外する数の上限が期待値であること。(確認 15 回)
    }
}

// 猶予時間の直前では除外されず、達した時点で除外されて監視対象から外れ、送信者が再開することの確認
TEST(receiverReportTest, flow_suppressed_excludes_at_grace_boundary_and_resumes)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000U;
    size_t tracked = 0U;
    uint64_t until_ms = 0U;
    SetupExclusionSender(&ctx, 1U, 10U, 200U, 1U);
    ctx.send_window.next_seq = 100U;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 受理の時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 活動中 3 台 (上限 1)、猶予時間 200 [ms]、監視対象 1 台の構成にする。
    ASSERT_EQ(POTR_OK, AcceptReport(&ctx, 1U, 1U, 110U));
    ASSERT_EQ(POTR_OK, AcceptReport(&ctx, 2U, 1U, 130U));
    ASSERT_EQ(POTR_OK, AcceptReport(&ctx, 3U, 1U, 150U));
    ASSERT_EQ(POTR_OK, AcceptFlowControl(&ctx, 1U, (uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS, 100U));
    ctx.flow_monitor_changed = 0;
    // [状態] - 受信者 1 が監視対象かつ止める状態 (抑制要求 + 送出上限到達) である。
    // Pre-Assert
    // Act
    int sup_0 = potr_internal_receiver_flow_suppressed(&ctx, 1000U, 100U, &tracked, &until_ms);
    uint64_t until_0 = until_ms;
    int sup_50 = potr_internal_receiver_flow_suppressed(&ctx, 1050U, 100U, &tracked, &until_ms);
    int sup_100 = potr_internal_receiver_flow_suppressed(&ctx, 1100U, 100U, &tracked, &until_ms);
    int sup_150 = potr_internal_receiver_flow_suppressed(&ctx, 1150U, 100U, &tracked, &until_ms);
    int sup_199 = potr_internal_receiver_flow_suppressed(&ctx, 1199U, 100U, &tracked, &until_ms);
    uint64_t until_199 = until_ms;
    int excluded_at_199 = FindReceiver(&ctx, 1U)->flow_excluded;
    // [手順] - 50 [ms] 刻みで 199 [ms] まで進める。
    int sup_200 = potr_internal_receiver_flow_suppressed(&ctx, 1200U, 100U, &tracked, &until_ms);
    size_t tracked_200 = tracked;
    uint64_t until_200 = until_ms;
    // [手順] - 猶予時間ちょうど (200 [ms]) で判定する。
    now_ms = 1210U;
    int ret_sup_after = AcceptFlowControl(&ctx, 1U, (uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS, 100U);
    int sup_after = potr_internal_receiver_flow_suppressed(&ctx, 1210U, 100U, &tracked, &until_ms);
    // [手順] - 除外後の受信者 1 からさらに抑制の要求を受けて判定する。
    // Assert
    EXPECT_EQ(1, sup_0);                                         // [確認_正常系] - 1 台が止める状態で抑制すること。
    EXPECT_EQ(1200U, until_0);                                   // [確認_正常系] - 猶予時間に達する時刻 1200 を返すこと。
    EXPECT_EQ(1, sup_50);                                        // [確認_正常系] - 50 [ms] 経過時は抑制を続けること。
    EXPECT_EQ(1, sup_100);                                       // [確認_正常系] - 100 [ms] 経過時は抑制を続けること。
    EXPECT_EQ(1, sup_150);                                       // [確認_正常系] - 150 [ms] 経過時は抑制を続けること。
    EXPECT_EQ(1, sup_199);                                       // [確認_正常系] - 猶予時間の直前 (199 [ms]) は抑制を続けること。
    EXPECT_EQ(1200U, until_199);                                 // [確認_正常系] - 直前の until_ms が 1200 であること。
    EXPECT_EQ(0, excluded_at_199);                               // [確認_正常系] - 猶予時間の直前では除外されないこと。
    EXPECT_EQ(0, sup_200);                                       // [確認_正常系] - 猶予時間に達して除外されると送信者が再開すること。
    EXPECT_EQ(1U, tracked_200);                                  // [確認_正常系] - 除外中の受信者を tracked に数えること。
    EXPECT_EQ(0U, until_200);                                    // [確認_正常系] - 再開時は until_ms が 0 であること。
    EXPECT_EQ(1, FindReceiver(&ctx, 1U)->flow_excluded);         // [確認_正常系] - 受信者 1 が除外されること。
    EXPECT_EQ(0, FindReceiver(&ctx, 1U)->flow_monitored);        // [確認_正常系] - 除外された受信者 1 が監視対象から外れること。
    EXPECT_EQ(0U, FindReceiver(&ctx, 1U)->flow_send_limit);      // [確認_正常系] - 監視対象から外れた受信者 1 の送出上限を破棄すること。
    EXPECT_EQ(1, FindReceiver(&ctx, 2U)->flow_monitored);        // [確認_正常系] - 次に余裕が小さい受信者 2 が監視対象に選ばれること。
    EXPECT_EQ(1, ctx.flow_monitor_changed);                      // [確認_正常系] - 監視対象の変更フラグが立つこと。
    EXPECT_EQ(POTR_OK, ret_sup_after);                           // [確認_正常系] - 除外後の抑制要求パケット自体は受理すること。
    EXPECT_EQ(0, sup_after);                                     // [確認_正常系] - 除外中の受信者の抑制要求は止める判定に使わないこと。
    EXPECT_EQ(0, FindReceiver(&ctx, 1U)->flow_monitored);        // [確認_正常系] - 除外中の受信者は監視対象に選ばれないこと。
    // Cleanup
    FreeReceivers(&ctx);
}

// 積算が止めていた間に増え、止めていない間に経過時間 ÷ 9 で減って 0 を下回らず、判定間隔が空きすぎた場合も減ることの確認
TEST(receiverReportTest, flow_suppressed_accum_increases_decays_and_handles_idle_gap)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    size_t tracked = 0U;
    uint64_t until_ms = 0U;
    SetupExclusionSender(&ctx, 1U, 10U, 200U, 1U);
    ctx.health_timeout_ms = 0U;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 活動中 3 台、猶予時間 200 [ms] で受信者 1 が抑制要求中とする。
    potr_internal_receiver *r1 = AddReceiver(&ctx, 1, 1000U, 5000U);
    AddReceiver(&ctx, 1, 1000U, 0U);
    AddReceiver(&ctx, 1, 1000U, 0U);
    // Pre-Assert
    // Act
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1000U, 0U, &tracked, &until_ms);
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1050U, 0U, &tracked, &until_ms);
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1090U, 0U, &tracked, &until_ms);
    uint64_t accum_after_90 = r1->flow_stall_accum_units;
    // [手順] - 90 [ms] の間、止める状態で判定する。
    r1->flow_request_expire_ms = 0U;
    int sup_released = potr_internal_receiver_flow_suppressed(&ctx, 1180U, 0U, &tracked, &until_ms);
    uint64_t accum_after_decay_90 = r1->flow_stall_accum_units;
    size_t tracked_with_accum = tracked;
    // [手順] - 止める状態を解き、90 [ms] 進めて判定する (90 ÷ 9 = 10 [ms] 減少)。
    for (uint64_t t = 1181U; t <= 1189U; t++)
    {
        (void)potr_internal_receiver_flow_suppressed(&ctx, t, 0U, &tracked, &until_ms);
    }
    uint64_t accum_after_9x1ms = r1->flow_stall_accum_units;
    // [手順] - 1 [ms] 刻みで 9 回進める (端数で減らなくならないことの確認)。
    (void)potr_internal_receiver_flow_suppressed(&ctx, 11189U, 0U, &tracked, &until_ms);
    uint64_t accum_at_floor = r1->flow_stall_accum_units;
    size_t tracked_at_zero = tracked;
    // [手順] - 十分に時間を進めて積算が 0 で止まることを確認する。
    r1->flow_request_expire_ms = 20000U;
    (void)potr_internal_receiver_flow_suppressed(&ctx, 12000U, 0U, &tracked, &until_ms);
    (void)potr_internal_receiver_flow_suppressed(&ctx, 12090U, 0U, &tracked, &until_ms);
    uint64_t accum_before_gap = r1->flow_stall_accum_units;
    (void)potr_internal_receiver_flow_suppressed(&ctx, 12090U + POTR_INTERNAL_FLOW_STALL_GAP_LIMIT_MS, 0U, &tracked,
                                                 &until_ms);
    uint64_t accum_after_gap = r1->flow_stall_accum_units;
    // [手順] - 止める状態で 90 [ms] 積算した後、許容上限 (POTR_INTERNAL_FLOW_STALL_GAP_LIMIT_MS) だけ間隔を空けて判定する。
    // Assert
    EXPECT_EQ(90U * POTR_FLOW_ACCUM_DECAY_DIVISOR, accum_after_90);
    // [確認_正常系] - 90 [ms] 止めた後の積算が 90 [ms] 相当 (810 単位) であること。
    EXPECT_EQ(0, sup_released);                                      // [確認_正常系] - 解除後は抑制しないこと。
    EXPECT_EQ(80U * POTR_FLOW_ACCUM_DECAY_DIVISOR, accum_after_decay_90);
    // [確認_正常系] - 90 [ms] 止めていない間に 10 [ms] 減り、80 [ms] 相当 (720 単位) になること。
    EXPECT_EQ(1U, tracked_with_accum);                               // [確認_正常系] - 積算が 0 でない間は tracked に数えること。
    EXPECT_EQ(79U * POTR_FLOW_ACCUM_DECAY_DIVISOR, accum_after_9x1ms);
    // [確認_正常系] - 1 [ms] 刻みで 9 回進めるとちょうど 1 [ms] 減ること。
    EXPECT_EQ(0U, accum_at_floor);                                   // [確認_正常系] - 積算が 0 を下回らないこと。
    EXPECT_EQ(0U, tracked_at_zero);                                  // [確認_正常系] - 積算が 0 に戻ると tracked が 0 になること。
    EXPECT_EQ(90U * POTR_FLOW_ACCUM_DECAY_DIVISOR, accum_before_gap); // [確認_正常系] - 間隔を空ける前の積算が 90 [ms] 相当であること。
    EXPECT_EQ(90U * POTR_FLOW_ACCUM_DECAY_DIVISOR - POTR_INTERNAL_FLOW_STALL_GAP_LIMIT_MS, accum_after_gap);
    // [確認_正常系] - 判定間隔が空きすぎた場合は止めていない間として減算すること。
    // Cleanup
    FreeReceivers(&ctx);
}

// 除外する数の上限を超えて除外せず積算を猶予時間で保持することと、上限 0 (1 台または両方無効) では除外しないことの確認
TEST(receiverReportTest, flow_suppressed_respects_excluded_limit_and_zero_limit)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    size_t tracked = 0U;
    uint64_t until_ms = 0U;
    SetupExclusionSender(&ctx, 1U, 10U, 200U, 1U);
    ctx.health_timeout_ms = 0U;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 活動中 3 台 (上限 1) で、2 台が止める状態とする。
    potr_internal_receiver *r1 = AddReceiver(&ctx, 1, 1000U, 5000U);
    potr_internal_receiver *r2 = AddReceiver(&ctx, 1, 1000U, 5000U);
    AddReceiver(&ctx, 1, 1000U, 0U);
    // Pre-Assert
    // Act
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1000U, 0U, &tracked, &until_ms);
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1100U, 0U, &tracked, &until_ms);
    int sup_at_grace = potr_internal_receiver_flow_suppressed(&ctx, 1200U, 0U, &tracked, &until_ms);
    int sup_after_grace = potr_internal_receiver_flow_suppressed(&ctx, 1250U, 0U, &tracked, &until_ms);
    int excluded_sum = r1->flow_excluded + r2->flow_excluded;
    potr_internal_receiver *kept = r1->flow_excluded ? r2 : r1;
    uint64_t kept_accum = kept->flow_stall_accum_units;
    // [手順] - 猶予時間 200 [ms] とさらに 50 [ms] 進める。
    FreeReceivers(&ctx);

    // Arrange_2
    ctx = {};
    SetupExclusionSender(&ctx, 1U, 10U, 200U, 1U);
    ctx.health_timeout_ms = 0U;
    potr_internal_receiver *solo = AddReceiver(&ctx, 1, 1000U, 5000U);
    // Act_2
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1000U, 0U, &tracked, &until_ms);
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1100U, 0U, &tracked, &until_ms);
    int sup_solo = potr_internal_receiver_flow_suppressed(&ctx, 1200U, 0U, &tracked, &until_ms);
    int solo_excluded = solo->flow_excluded;
    // [手順] - 活動中 1 台 (上限 0) で猶予時間まで進める。
    FreeReceivers(&ctx);

    // Arrange_3
    ctx = {};
    SetupExclusionSender(&ctx, 0U, 0U, 200U, 1U);
    ctx.health_timeout_ms = 0U;
    potr_internal_receiver *dis = AddReceiver(&ctx, 1, 1000U, 5000U);
    AddReceiver(&ctx, 1, 1000U, 0U);
    AddReceiver(&ctx, 1, 1000U, 0U);
    // Act_3
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1000U, 0U, &tracked, &until_ms);
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1100U, 0U, &tracked, &until_ms);
    int sup_disabled = potr_internal_receiver_flow_suppressed(&ctx, 1200U, 0U, &tracked, &until_ms);
    int disabled_excluded = dis->flow_excluded;
    // [手順] - 活動中 3 台、除外設定の両方が無効 (上限 0) で猶予時間まで進める。
    // Assert
    EXPECT_EQ(1, sup_at_grace);                                      // [確認_正常系] - 上限 1 に達しても残る 1 台で抑制を続けること。
    EXPECT_EQ(1, sup_after_grace);                                   // [確認_正常系] - さらに時間が経過しても抑制を続けること。
    EXPECT_EQ(1, excluded_sum);                                      // [確認_正常系] - 除外する数が上限の 1 台を超えないこと。
    EXPECT_EQ(200U * POTR_FLOW_ACCUM_DECAY_DIVISOR, kept_accum);     // [確認_正常系] - 除外されなかった受信者の積算が猶予時間で保持されること。
    EXPECT_EQ(1, sup_solo);                                          // [確認_正常系] - 活動中 1 台 (上限 0) では抑制を続けること。
    EXPECT_EQ(0, solo_excluded);                                     // [確認_正常系] - 活動中 1 台では除外しないこと。
    EXPECT_EQ(1, sup_disabled);                                      // [確認_正常系] - 両方が無効 (上限 0) では抑制を続けること。
    EXPECT_EQ(0, disabled_excluded);                                 // [確認_正常系] - 両方が無効では除外しないこと。
    // Cleanup
    FreeReceivers(&ctx);
}

// 同時に猶予時間に達した場合に、積算が大きい順、同じなら管理表の順で上限まで除外することの確認
TEST(receiverReportTest, flow_suppressed_exclusion_tie_break_order)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    size_t tracked = 0U;
    uint64_t until_ms = 0U;
    SetupExclusionSender(&ctx, 1U, 0U, 100U, 1U);
    ctx.health_timeout_ms = 0U;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 管理表の先頭が r_first、次が r_second になるように追加する (AddReceiver は先頭挿入)。
    AddReceiver(&ctx, 1, 1000U, 0U);
    AddReceiver(&ctx, 1, 1000U, 0U);
    potr_internal_receiver *r_second = AddReceiver(&ctx, 1, 1000U, 5000U); // 先に止めるが管理表では後ろ
    potr_internal_receiver *r_first = AddReceiver(&ctx, 1, 1000U, 0U);     // 管理表では先頭
    // Pre-Assert
    // Act
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1000U, 0U, &tracked, &until_ms);
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1020U, 0U, &tracked, &until_ms);
    r_first->flow_request_expire_ms = 5000U;
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1090U, 0U, &tracked, &until_ms);
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1130U, 0U, &tracked, &until_ms);
    int first_ex_by_accum = r_first->flow_excluded;
    int second_ex_by_accum = r_second->flow_excluded;
    // [手順] - 同じ判定 (t=1130) で r_second (130 ms) と r_first (110 ms) の両方が 100 [ms] を超える。
    FreeReceivers(&ctx);

    // Arrange_2
    ctx = {};
    SetupExclusionSender(&ctx, 1U, 0U, 100U, 1U);
    ctx.health_timeout_ms = 0U;
    AddReceiver(&ctx, 1, 1000U, 0U);
    AddReceiver(&ctx, 1, 1000U, 0U);
    potr_internal_receiver *r_tail = AddReceiver(&ctx, 1, 1000U, 5000U);
    potr_internal_receiver *r_head = AddReceiver(&ctx, 1, 1000U, 5000U);
    // Act_2
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1000U, 0U, &tracked, &until_ms);
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1050U, 0U, &tracked, &until_ms);
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1100U, 0U, &tracked, &until_ms);
    int head_ex_by_list = r_head->flow_excluded;
    int tail_ex_by_list = r_tail->flow_excluded;
    // [手順] - 同じ積算 (100 [ms]) で同時に達した場合に、管理表の先頭側が選ばれることを確認する。
    // Assert
    EXPECT_EQ(0, first_ex_by_accum);  // [確認_正常系] - 積算が小さい r_first は除外されないこと。
    EXPECT_EQ(1, second_ex_by_accum); // [確認_正常系] - 積算が大きい (先に達した) r_second が除外されること。
    EXPECT_EQ(1, head_ex_by_list);    // [確認_正常系] - 積算が同じ場合は管理表の先頭側が除外されること。
    EXPECT_EQ(0, tail_ex_by_list);    // [確認_正常系] - 積算が同じ場合は管理表の後ろ側が除外されないこと。
    // Cleanup
    FreeReceivers(&ctx);
}

// 除外を解く条件 (猶予時間の 10 倍の境界、途中での数え直し、期限切れでの数え直し、通番循環、解いた時点の積算 0) の確認
TEST(receiverReportTest, flow_suppressed_unexclude_boundary_reset_and_wrap)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000U;
    size_t tracked = 0U;
    uint64_t until_ms = 0U;
    SetupExclusionSender(&ctx, 1U, 10U, 100U, 1U);
    ctx.health_timeout_ms = 2000U;
    ctx.send_window.next_seq = 100U;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 受理の時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    // [状態] - 活動中 3 台、猶予時間 100 [ms] (解除条件は 1000 [ms]) で受信者 1 を除外する。
    ASSERT_EQ(POTR_OK, AcceptReport(&ctx, 1U, 1U, 100U));
    ASSERT_EQ(POTR_OK, AcceptReport(&ctx, 2U, 1U, 150U));
    ASSERT_EQ(POTR_OK, AcceptReport(&ctx, 3U, 1U, 160U));
    ASSERT_EQ(POTR_OK, AcceptFlowControl(&ctx, 1U, (uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS, 100U));
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1000U, 100U, &tracked, &until_ms);
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1100U, 100U, &tracked, &until_ms);
    potr_internal_receiver *r1 = FindReceiver(&ctx, 1U);
    ASSERT_NE(nullptr, r1);
    ASSERT_EQ(1, r1->flow_excluded);
    // [状態確認] - 受信者 1 が除外されていること。
    // Pre-Assert
    // Act
    now_ms = 1500U;
    ASSERT_EQ(POTR_OK, AcceptReport(&ctx, 1U, 1U, 200U));
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1500U, 100U, &tracked, &until_ms);
    int pending_started = r1->flow_unexclude_pending;
    // [手順] - 受信報告の送出上限 200 (> next_seq 100) で数え始めさせる。
    (void)potr_internal_receiver_flow_suppressed(&ctx, 2000U, 200U, &tracked, &until_ms);
    int pending_reset_seq = r1->flow_unexclude_pending;
    // [手順] - 途中で next_seq が 200 (送出上限以下) になり、数え直すことを確認する。
    now_ms = 2100U;
    ASSERT_EQ(POTR_OK, AcceptReport(&ctx, 1U, 1U, 300U));
    (void)potr_internal_receiver_flow_suppressed(&ctx, 2100U, 200U, &tracked, &until_ms);
    // [手順] - 送出上限 300 (> 200) で時刻 2100 から再び数え始める。
    (void)potr_internal_receiver_flow_suppressed(&ctx, 3099U, 200U, &tracked, &until_ms);
    int excluded_at_999 = r1->flow_excluded;
    // [手順] - 999 [ms] 経過 (10 倍の 1 [ms] 手前) で判定する。
    (void)potr_internal_receiver_flow_suppressed(&ctx, 3100U, 200U, &tracked, &until_ms);
    int excluded_at_1000 = r1->flow_excluded;
    uint64_t accum_after_unexclude = r1->flow_stall_accum_units;
    // [手順] - ちょうど 1000 [ms] (猶予時間の 10 倍) で判定する。

    // Arrange_2
    // 期限切れでの数え直しと、通番循環をまたぐ解除を確認する。
    r1->flow_excluded = 1;
    r1->flow_stall_accum_units = 100U * POTR_FLOW_ACCUM_DECAY_DIVISOR;
    r1->flow_unexclude_pending = 0;
    now_ms = 4000U;
    ctx.send_window.next_seq = 0xFFFFFFF0U;
    ASSERT_EQ(POTR_OK, AcceptReport(&ctx, 1U, 1U, 0x10U));
    // Act_2
    (void)potr_internal_receiver_flow_suppressed(&ctx, 4000U, 0xFFFFFFF0U, &tracked, &until_ms);
    int pending_wrap = r1->flow_unexclude_pending;
    (void)potr_internal_receiver_flow_suppressed(&ctx, 6000U, 0xFFFFFFF0U, &tracked, &until_ms);
    int excluded_after_inactive = r1->flow_excluded;
    int pending_after_inactive = r1->flow_unexclude_pending;
    // [手順] - 受信報告の有効期限 (2000 [ms]) が切れた時刻 6000 で判定し、活動中でないため数え直すことを確認する。
    now_ms = 6100U;
    ASSERT_EQ(POTR_OK, AcceptReport(&ctx, 1U, 1U, 0x10U));
    (void)potr_internal_receiver_flow_suppressed(&ctx, 6100U, 0xFFFFFFF0U, &tracked, &until_ms);
    (void)potr_internal_receiver_flow_suppressed(&ctx, 7100U, 0xFFFFFFF0U, &tracked, &until_ms);
    int excluded_after_wrap_unexclude = r1->flow_excluded;
    // [手順] - 再び活動中にしてから 1000 [ms] 維持し、循環をまたいだ比較で除外が解けることを確認する。
    // Assert
    EXPECT_EQ(1, pending_started);              // [確認_正常系] - 送出上限が次の通番より先にあれば数え始めること。
    EXPECT_EQ(0, pending_reset_seq);            // [確認_正常系] - 途中で送出上限が次の通番以下になったら数え直すこと。
    EXPECT_EQ(1, excluded_at_999);              // [確認_正常系] - 猶予時間の 10 倍の直前 (999 [ms]) では解かないこと。
    EXPECT_EQ(0, excluded_at_1000);             // [確認_正常系] - 猶予時間の 10 倍 (1000 [ms]) で除外を解くこと。
    EXPECT_EQ(0U, accum_after_unexclude);       // [確認_正常系] - 除外を解いた時点で積算が 0 になること。
    EXPECT_EQ(1, pending_wrap);                 // [確認_正常系] - 通番循環をまたいでも先にあると判定すること。
    EXPECT_EQ(1, excluded_after_inactive);      // [確認_正常系] - 受信報告の有効期限切れでは除外を解かないこと。
    EXPECT_EQ(0, pending_after_inactive);       // [確認_正常系] - 活動中でなくなったら数え直すこと。
    EXPECT_EQ(0, excluded_after_wrap_unexclude); // [確認_正常系] - 循環をまたぐ比較で 10 倍の継続後に除外を解くこと。
    // Cleanup
    FreeReceivers(&ctx);
}

// セッションが変わった受信者が新しい受信者として扱われる (積算 0、除外なし) ことの確認
TEST(receiverReportTest, session_change_starts_with_zero_accum_and_not_excluded)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint64_t now_ms = 1000U;
    size_t tracked = 0U;
    uint64_t until_ms = 0U;
    SetupExclusionSender(&ctx, 1U, 10U, 100U, 1U);
    ctx.send_window.next_seq = 100U;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([&](uint64_t *out) {
        *out = now_ms;
        return CPLAT_OK;
    }); // [状態] - 受理の時刻を制御する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    ASSERT_EQ(POTR_OK, AcceptReport(&ctx, 1U, 1U, 100U));
    ASSERT_EQ(POTR_OK, AcceptReport(&ctx, 2U, 1U, 150U));
    ASSERT_EQ(POTR_OK, AcceptReport(&ctx, 3U, 1U, 160U));
    ASSERT_EQ(POTR_OK, AcceptFlowControl(&ctx, 1U, (uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS, 100U));
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1000U, 100U, &tracked, &until_ms);
    (void)potr_internal_receiver_flow_suppressed(&ctx, 1100U, 100U, &tracked, &until_ms);
    ASSERT_EQ(1, FindReceiver(&ctx, 1U)->flow_excluded);
    // [状態] - セッション 1 が除外された状態にする。
    // [状態確認] - セッション 1 が除外されていること。
    // Pre-Assert
    // Act
    now_ms = 1200U;
    int ret_new = AcceptReport(&ctx, 4U, 1U, 120U);
    potr_internal_receiver *r_new = FindReceiver(&ctx, 4U);
    // [手順] - セッションが変わった受信者 (session_id = 4) の受信報告を受理する。
    // Assert
    EXPECT_EQ(POTR_OK, ret_new);                      // [確認_正常系] - 新しいセッションの受信報告を受理すること。
    ASSERT_NE(nullptr, r_new);                        // [確認_正常系] - 新しいセッションのエントリが作られること。
    EXPECT_EQ(0U, r_new->flow_stall_accum_units);     // [確認_正常系] - 新しいセッションの積算が 0 であること。
    EXPECT_EQ(0, r_new->flow_excluded);               // [確認_正常系] - 新しいセッションが除外されていないこと。
    EXPECT_EQ(0, r_new->flow_unexclude_pending);      // [確認_正常系] - 解除待ち時刻が未設定であること。
    // Cleanup
    FreeReceivers(&ctx);
}

// 1 台の連続停止と 2 台の交互停止のいずれでも有限の時間で除外され送信者が再開することの確認
TEST(receiverReportTest, flow_suppressed_resolves_in_finite_time_continuous_and_alternating)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    size_t tracked = 0U;
    uint64_t until_ms = 0U;
    SetupExclusionSender(&ctx, 1U, 10U, 200U, 1U);
    ctx.health_timeout_ms = 0U;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    potr_internal_receiver *slow = AddReceiver(&ctx, 1, 1000U, 100000U);
    AddReceiver(&ctx, 1, 1000U, 0U);
    AddReceiver(&ctx, 1, 1000U, 0U);
    // [状態] - 活動中 3 台、猶予時間 200 [ms] で 1 台が止める状態を続ける。
    // Pre-Assert
    // Act
    uint64_t t = 1000U;
    int sup = 1;
    int steps = 0;
    while (steps < 20)
    {
        sup = potr_internal_receiver_flow_suppressed(&ctx, t, 0U, &tracked, &until_ms);
        if (sup == 0)
        {
            break;
        }
        uint64_t step = (until_ms > t && until_ms - t < (uint64_t)POTR_INTERNAL_FLOW_RECHECK_MS)
                            ? (until_ms - t)
                            : (uint64_t)POTR_INTERNAL_FLOW_RECHECK_MS;
        t += step;
        steps++;
    }
    int continuous_excluded = slow->flow_excluded;
    int continuous_sup = sup;
    uint64_t continuous_end_ms = t;
    // [手順] - until_ms と再判定周期に従って時刻を進め、有限回で除外・再開することを確認する。
    FreeReceivers(&ctx);

    // Arrange_2
    ctx = {};
    SetupExclusionSender(&ctx, 1U, 10U, 200U, 1U);
    ctx.health_timeout_ms = 0U;
    potr_internal_receiver *r1 = AddReceiver(&ctx, 1, 1000U, 0U);
    potr_internal_receiver *r2 = AddReceiver(&ctx, 1, 1000U, 0U);
    AddReceiver(&ctx, 1, 1000U, 0U);
    // [状態] - 活動中 3 台で、r1 (40 [ms] 停止) と r2 (10 [ms] 停止) が交互に止める状態になる。
    // Act_2
    t = 1000U;
    int alt_excluded = 0;
    for (int cycle = 0; cycle < 20 && !alt_excluded; cycle++)
    {
        r1->flow_request_expire_ms = t + 1000U;
        r2->flow_request_expire_ms = 0U;
        (void)potr_internal_receiver_flow_suppressed(&ctx, t, 0U, &tracked, &until_ms);
        t += 40U;
        (void)potr_internal_receiver_flow_suppressed(&ctx, t, 0U, &tracked, &until_ms);
        if (r1->flow_excluded)
        {
            alt_excluded = 1;
            break;
        }
        r1->flow_request_expire_ms = 0U;
        r2->flow_request_expire_ms = t + 1000U;
        (void)potr_internal_receiver_flow_suppressed(&ctx, t, 0U, &tracked, &until_ms);
        t += 10U;
        (void)potr_internal_receiver_flow_suppressed(&ctx, t, 0U, &tracked, &until_ms);
    }
    int r2_excluded = r2->flow_excluded;
    // [手順] - r1 と r2 が交互に止める状態を繰り返し、積算が猶予時間に達する r1 が除外されることを確認する。
    // Assert
    EXPECT_EQ(0, continuous_sup);       // [確認_正常系] - 連続停止の受信者が除外され送信者が再開すること。
    EXPECT_EQ(1, continuous_excluded);  // [確認_正常系] - 連続停止の受信者が除外されること。
    EXPECT_EQ(1200U, continuous_end_ms); // [確認_正常系] - ちょうど猶予時間 200 [ms] 後の時刻 1200 で再開すること。
    EXPECT_EQ(1, alt_excluded);         // [確認_正常系] - 交互停止でも積算が猶予時間に達する r1 が除外されること。
    EXPECT_EQ(0, r2_excluded);          // [確認_正常系] - 停止時間が短い r2 は除外されないこと。
    // Cleanup
    FreeReceivers(&ctx);
}
