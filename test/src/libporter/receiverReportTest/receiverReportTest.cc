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
    uint8_t state = POTR_FLOW_CONTROL_STATE_SUPPRESS;
    uint8_t wire[POTR_PACKET_HEADER_SIZE] = {};
    uint64_t now_ms = 1000;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.flow_control_config.request_lifetime_ms = 300;
    packet.flags = POTR_FLAG_FLOW_CONTROL;
    packet.payload = &state;
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
    // Arrange_2
    expire_after_suppress = ctx.receivers->flow_request_expire_ms;
    state = POTR_FLOW_CONTROL_STATE_RESUME;
    now_ms = 1100;
    // Act_2
    int actual_ret_resume = potr_internal_receiver_accept(&ctx, &packet, wire);
    // [手順] - 同じ受信者から、解除を受理する。
    // Assert_2
    EXPECT_EQ(1300U, expire_after_suppress); // [確認_正常系] - 解除の前の期限が記録されていること。
    EXPECT_EQ(POTR_OK, actual_ret_resume); // [確認_正常系] - 解除を受理すること。
    EXPECT_EQ(0U, ctx.receivers->flow_request_expire_ms); // [確認_正常系] - 期限が 0 (要求なし) へ戻ること。
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
    uint8_t state = POTR_FLOW_CONTROL_STATE_SUPPRESS;
    uint8_t wire[POTR_PACKET_HEADER_SIZE] = {};
    uint64_t now_ms = 1000;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.flow_control_config.request_lifetime_ms = 300;
    packet.flags = POTR_FLAG_FLOW_CONTROL;
    packet.payload = &state;
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
    packet.payload_len = 2;
    int actual_ret_length = potr_internal_receiver_accept(&ctx, &packet, wire);
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
    // [手順] - ペイロード長、状態 0、未定義の状態、通番、確認応答番号、ペイロードの不正を順に検査する。
    // Assert
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_length); // [確認_異常系] - ペイロード長が 1 でないパケットを破棄すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_state_zero); // [確認_異常系] - 状態 0 を破棄すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_state_undefined); // [確認_異常系] - 未定義の状態 3 を破棄すること。
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
    int actual_valid = potr_internal_receiver_flow_suppressed(&ctx, now_ms, &requesting, &until_ms);
    // [手順] - 期限が現在時刻より後の要求が 1 つある状態で判定する。
    // Assert
    EXPECT_EQ(1, actual_valid); // [確認_正常系] - 有効な要求が 1 つあれば抑制すること。
    EXPECT_EQ(1U, requesting); // [確認_正常系] - 要求中の受信者数が 1 であること。
    EXPECT_EQ(1300U, until_ms); // [確認_正常系] - 最も早い期限が要求の期限であること。
    // Arrange_2
    receiver->flow_request_expire_ms = now_ms;
    // Act_2
    int actual_expired = potr_internal_receiver_flow_suppressed(&ctx, now_ms, &requesting_expired, &until_expired);
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
    int actual_one = potr_internal_receiver_flow_suppressed(&ctx, now_ms, &requesting_one, &until_one);
    receivers[1]->flow_request_expire_ms = 1300;
    int actual_two = potr_internal_receiver_flow_suppressed(&ctx, now_ms, &requesting_two, &until_two);
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
    int actual_all = potr_internal_receiver_flow_suppressed(&ctx, now_ms, &requesting, &until_ms);
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
    int actual_partial = potr_internal_receiver_flow_suppressed(&ctx, now_ms, &requesting_partial, &until_partial);
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
    int actual_two = potr_internal_receiver_flow_suppressed(&ctx, now_ms, &requesting_two, &until_two);
    receivers[2]->flow_request_expire_ms = 1300;
    int actual_three = potr_internal_receiver_flow_suppressed(&ctx, now_ms, &requesting_three, &until_three);
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
    int actual_four = potr_internal_receiver_flow_suppressed(&ctx, now_ms, &requesting_four, &until_four);
    receivers[4]->flow_request_expire_ms = 1300;
    int actual_five = potr_internal_receiver_flow_suppressed(&ctx, now_ms, &requesting_five, &until_five);
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
    int actual_ret = potr_internal_receiver_flow_suppressed(&ctx, now_ms, &requesting, &until_ms);
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
    int actual_ret = potr_internal_receiver_flow_suppressed(&ctx, now_ms, &requesting, &until_ms);
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
    int actual_ret = potr_internal_receiver_flow_suppressed(&ctx, now_ms, &requesting, &until_ms);
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
    int actual_ctx = potr_internal_receiver_flow_suppressed(nullptr, now_ms, &requesting, &until_ms);
    int actual_requesting = potr_internal_receiver_flow_suppressed(&ctx, now_ms, nullptr, &until_ms);
    int actual_until = potr_internal_receiver_flow_suppressed(&ctx, now_ms, &requesting, nullptr);
    // [手順] - ctx、requesting、until_ms を順に NULL にして判定する。
    // Assert
    EXPECT_EQ(0, actual_ctx); // [確認_異常系] - ctx が NULL なら 0 を返すこと。
    EXPECT_EQ(0, actual_requesting); // [確認_異常系] - requesting が NULL なら 0 を返すこと。
    EXPECT_EQ(0, actual_until); // [確認_異常系] - until_ms が NULL なら 0 を返すこと。
    // Cleanup
    FreeReceivers(&ctx);
}
