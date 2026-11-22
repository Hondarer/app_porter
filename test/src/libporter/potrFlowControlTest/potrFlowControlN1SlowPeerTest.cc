/**
 *******************************************************************************
 *  @file           potrFlowControlN1SlowPeerTest.cc
 *  @brief          N 対 1 サーバーで全体を妨げている遅いピアの記録・積算・切断対象判定を検証します。
 *
 *  potr_internal_flow_control_peer_mark_blocking()、potr_internal_flow_control_note_queue_full()、
 *  および potr_internal_flow_control_collect_slow_peers_locked() の動作を、単調増加時刻 now_ms を
 *  制御して検証します。
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
#include <vector>
using namespace testing;

namespace
{
const int kMaxPeers = 8;
} // namespace

class potrFlowControlN1SlowPeerTest : public Test
{
  protected:
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx;
    potr_internal_peer_context peer_slots[kMaxPeers];
    uint64_t now_ms = 1000U;

    // [サブ手順 名前=potrFlowControlN1SlowPeerTest.SetUp]
    void SetUp() override
    {
        memset(&ctx, 0, sizeof(ctx));
        memset(peer_slots, 0, sizeof(peer_slots));
        ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
        ctx.service.service_id = 1;
        ctx.role = POTR_ROLE_RECEIVER;
        ctx.is_multi_peer = 1;
        ctx.peers = peer_slots;
        ctx.max_peers = kMaxPeers;
        ctx.flow_control_config.grace_ms = 200U;
        ctx.flow_control_config.excluded_receiver_count = 1U;
        ctx.flow_control_config.excluded_receiver_percent = 10U;
        ctx.flow_control_config.request_interval_ms = 200U;
        ctx.flow_control_config.request_lifetime_ms = 10000U;
        cplat_local_lock_create(&ctx.peers_mutex);
        // [状態] - N:1 サーバー、猶予時間 200 [ms]、除外上限 1 台 / 10%、有効期限 10000 [ms] とする。

        ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([this](uint64_t *out) {
            *out = now_ms;
            return CPLAT_OK;
        }); // [状態] - 現在時刻を now_ms で制御する。

        ON_CALL(mock_porter, potr_internal_peer_find_by_id(_, _))
            .WillByDefault([this](const potr_context *, potr_peer_id id) -> potr_internal_peer_context * {
                for (int i = 0; i < kMaxPeers; i++)
                {
                    if (peer_slots[i].active && peer_slots[i].peer_id == id)
                    {
                        return &peer_slots[i];
                    }
                }
                return nullptr;
            }); // [状態] - peer_slots からアクティブなピアを検索する。
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrFlowControlN1SlowPeerTest.TearDown]
    void TearDown() override
    {
        for (int i = 0; i < kMaxPeers; i++)
        {
            if (peer_slots[i].send_window_mutex != nullptr)
            {
                cplat_local_lock_dispose(peer_slots[i].send_window_mutex);
                peer_slots[i].send_window_mutex = nullptr;
            }
        }
        if (ctx.peers_mutex != nullptr)
        {
            cplat_local_lock_dispose(ctx.peers_mutex);
            ctx.peers_mutex = nullptr;
        }
    }
    // [サブ手順終了]

    potr_internal_peer_context *add_peer(int slot_idx, potr_peer_id peer_id)
    {
        potr_internal_peer_context *p = &peer_slots[slot_idx];
        memset(p, 0, sizeof(*p));
        p->peer_id = peer_id;
        p->active = 1;
        cplat_atomic_store_i32(&p->health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
        p->flow_send_limit = POTR_FLOW_SEND_LIMIT_UNLIMITED;
        cplat_local_lock_create(&p->send_window_mutex);
        ctx.n_peers++;
        return p;
    }

    void set_peer_stopping(potr_internal_peer_context *p, bool stopping)
    {
        cplat_local_lock_lock(ctx.peers_mutex);
        p->flow_request_expire_ms = stopping ? (now_ms + 5000U) : 0U;
        potr_internal_flow_control_peer_sync(&ctx, p);
        cplat_local_lock_unlock(ctx.peers_mutex);
    }

    size_t collect_slow(std::vector<potr_peer_id> &out_peers, std::vector<uint64_t> &out_accum, uint64_t *next_check_ms)
    {
        potr_peer_id buf[POTR_FLOW_SLOW_PEER_BATCH_MAX] = {};
        uint64_t accum[POTR_FLOW_SLOW_PEER_BATCH_MAX] = {};
        cplat_local_lock_lock(ctx.peers_mutex);
        size_t n = potr_internal_flow_control_collect_slow_peers_locked(&ctx, now_ms, buf, accum,
                                                                        POTR_FLOW_SLOW_PEER_BATCH_MAX, next_check_ms);
        cplat_local_lock_unlock(ctx.peers_mutex);
        out_peers.assign(buf, buf + n);
        out_accum.assign(accum, accum + n);
        return n;
    }
};

// 妨げの記録がないピアは、止める状態が猶予時間を超えて続いても切断対象にならないことの確認
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.SetUp]
TEST_F(potrFlowControlN1SlowPeerTest, unblocked_slow_peer_is_never_collected)
{
    // Arrange
    potr_internal_peer_context *p1 = add_peer(0, 10U);
    add_peer(1, 20U);
    set_peer_stopping(p1, true);
    // [状態] - 接続中 2 ピアのうちピア 10 を止める状態にする。妨げは記録しない。
    std::vector<potr_peer_id> slow_peers;
    std::vector<uint64_t> slow_accum;
    uint64_t next_ms = 999U;
    // Pre-Assert
    // Act
    now_ms = 2000U;
    size_t count = collect_slow(slow_peers, slow_accum, &next_ms);
    // [手順] - 猶予時間 (200 [ms]) より長い 1000 [ms] 経過後に収集を呼ぶ。
    // Assert
    EXPECT_EQ(0U, count);   // [確認_正常系] - 妨げの記録がないため切断対象が 0 件であること。
    EXPECT_EQ(0U, next_ms); // [確認_正常系] - 次回確認時刻が 0 (予定なし) であること。
    EXPECT_EQ(0, p1->flow_blocking); // [確認_正常系] - ピア 10 の妨げフラグが 0 のままであること。
}
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.TearDown]

// 妨げが記録されたピアが止める状態を続けたとき、猶予時間未満では切断せず、達した時点で切断対象になることの確認
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.SetUp]
TEST_F(potrFlowControlN1SlowPeerTest, accum_reaches_grace_and_collects_slow_peer)
{
    // Arrange
    potr_internal_peer_context *p1 = add_peer(0, 10U);
    add_peer(1, 20U);
    set_peer_stopping(p1, true);
    potr_internal_flow_control_peer_mark_blocking(&ctx, 10U, now_ms);
    // [状態] - 接続中 2 ピアのうちピア 10 を止める状態にし、時刻 1000 で妨げを記録する。
    std::vector<potr_peer_id> slow_peers;
    std::vector<uint64_t> slow_accum;
    uint64_t next_ms = 0U;
    // Pre-Assert
    // Act
    now_ms = 1199U;
    size_t count_before = collect_slow(slow_peers, slow_accum, &next_ms);
    // [手順] - 199 [ms] 経過後 (猶予時間 200 [ms] 未満) に収集を呼ぶ。
    uint64_t next_before = next_ms;
    now_ms = 1200U;
    size_t count_at_grace = collect_slow(slow_peers, slow_accum, &next_ms);
    // [手順] - 200 [ms] 経過後 (猶予時間ちょうど) に収集を呼ぶ。
    // Assert
    EXPECT_EQ(0U, count_before);      // [確認_正常系] - 猶予時間未満では切断対象が 0 件であること。
    EXPECT_EQ(1200U, next_before);    // [確認_正常系] - 次回確認時刻が猶予時間到達時刻 1200 であること。
    ASSERT_EQ(1U, count_at_grace);    // [確認_正常系] - 猶予時間到達で切断対象が 1 件になること。
    EXPECT_EQ(10U, slow_peers[0]);    // [確認_正常系] - 切断対象がピア 10 であること。
    EXPECT_EQ(200U, slow_accum[0]);   // [確認_正常系] - 報告される積算時間が 200 [ms] であること。
}
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.TearDown]

// 止める状態のピア数が除外上限を超えている間は切断せず猶予時間で上限保持し、上限以下に戻ると切断することの確認
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.SetUp]
TEST_F(potrFlowControlN1SlowPeerTest, stopping_peers_exceeding_limit_suppresses_disconnect_and_clamps_accum)
{
    // Arrange
    potr_internal_peer_context *p1 = add_peer(0, 10U);
    potr_internal_peer_context *p2 = add_peer(1, 20U);
    add_peer(2, 30U);
    add_peer(3, 40U);
    // 接続 4 台、上限 1 台 (count=1, percent=10 -> max(1, 0) = 1)。
    set_peer_stopping(p1, true);
    set_peer_stopping(p2, true);
    potr_internal_flow_control_peer_mark_blocking(&ctx, 10U, now_ms);
    potr_internal_flow_control_peer_mark_blocking(&ctx, 20U, now_ms);
    // [状態] - 接続中 4 ピア (除外上限 1) でピア 10 と 20 の 2 台を止める状態にし、時刻 1000 で妨げを記録する。
    std::vector<potr_peer_id> slow_peers;
    std::vector<uint64_t> slow_accum;
    uint64_t next_ms = 0U;
    // Pre-Assert
    // Act
    now_ms = 1500U;
    size_t count_over_limit = collect_slow(slow_peers, slow_accum, &next_ms);
    // [手順] - 500 [ms] 経過後 (同時停止 2 > 上限 1) に収集を呼ぶ。
    uint64_t clamped_units = p1->flow_stall_accum_units;
    set_peer_stopping(p2, false);
    now_ms = 1500U;
    size_t count_within_limit = collect_slow(slow_peers, slow_accum, &next_ms);
    // [手順] - ピア 20 の止める状態を解き (同時停止 1 <= 上限 1)、同時刻で収集を呼ぶ。
    now_ms = 1700U;
    size_t count_after_grace = collect_slow(slow_peers, slow_accum, &next_ms);
    // [手順] - 全体停止が終わった後の単独停止を 200 [ms] 積算する。
    // Assert
    EXPECT_EQ(0U, count_over_limit);
    // [確認_正常系] - 同時停止数が上限を超えている間は切断対象が 0 件であること。
    EXPECT_EQ(0U, clamped_units);
    // [確認_正常系] - 全体が遅い間の積算が 0 のままであること。
    EXPECT_EQ(0U, count_within_limit);
    // [確認_正常系] - 同時停止数が上限以下に戻った直後はまだ切断しないこと。
    ASSERT_EQ(1U, count_after_grace); // [確認_正常系] - 単独停止の猶予時間で切断対象が 1 件になること。
    EXPECT_EQ(10U, slow_peers[0]); // [確認_正常系] - 残った止める状態のピア 10 が切断対象になること。
}
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.TearDown]

// 止める状態が解けると積算が経過時間 ÷ 9 で減り、0 に戻った時点で妨げの記録が消え、再度の妨げで記録し直されることの確認
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.SetUp]
TEST_F(potrFlowControlN1SlowPeerTest, recovery_decays_accum_and_clears_blocking_at_zero)
{
    // Arrange
    potr_internal_peer_context *p1 = add_peer(0, 10U);
    add_peer(1, 20U);
    set_peer_stopping(p1, true);
    potr_internal_flow_control_peer_mark_blocking(&ctx, 10U, now_ms);
    // [状態] - 接続中 2 ピアでピア 10 を止める状態にし、時刻 1000 で妨げを記録する。
    std::vector<potr_peer_id> slow_peers;
    std::vector<uint64_t> slow_accum;
    uint64_t next_ms = 0U;
    // Pre-Assert
    // Act
    now_ms = 1100U;
    (void)collect_slow(slow_peers, slow_accum, &next_ms); // 100 [ms] 積算 (900 単位)
    set_peer_stopping(p1, false);
    (void)collect_slow(slow_peers, slow_accum, &next_ms); // [手順] - 時刻 1100 で解除を判定し、次の区間から減算する。
    now_ms = 1550U;
    (void)collect_slow(slow_peers, slow_accum, &next_ms); // 450 [ms] 減算 -> 450 単位 (50 [ms] 相当)
    uint64_t mid_units = p1->flow_stall_accum_units;
    int mid_blocking = p1->flow_blocking;
    now_ms = 2000U;
    (void)collect_slow(slow_peers, slow_accum, &next_ms); // さらに 450 [ms] 減算 -> 0 単位で妨げ記録消去
    uint64_t cleared_units = p1->flow_stall_accum_units;
    int cleared_blocking = p1->flow_blocking;
    int32_t blocking_peers_after_clear = cplat_atomic_load_i32(&ctx.flow_blocking_peers, CPLAT_MEMORY_ORDER_ACQUIRE);

    // 妨げ記録が消えた後に再び止める状態になっても、妨げが再記録されるまでは積算しない。
    set_peer_stopping(p1, true);
    now_ms = 2500U;
    size_t count_without_remark = collect_slow(slow_peers, slow_accum, &next_ms);

    // 再度妨げを記録すると、その時点 (2500) から新しく積算が始まり、200 [ms] 後に切断対象になる。
    potr_internal_flow_control_peer_mark_blocking(&ctx, 10U, now_ms);
    now_ms = 2700U;
    size_t count_after_remark = collect_slow(slow_peers, slow_accum, &next_ms);

    // Assert
    EXPECT_EQ(450U, mid_units);       // [確認_正常系] - 450 [ms] 回復後に積算が 900 - 450 = 450 単位であること。
    EXPECT_EQ(1, mid_blocking);       // [確認_正常系] - 積算が残っている間は妨げの記録が維持されること。
    EXPECT_EQ(0U, cleared_units);     // [確認_正常系] - 合計 900 [ms] 回復後に積算が 0 に戻ること。
    EXPECT_EQ(0, cleared_blocking);   // [確認_正常系] - 積算が 0 に戻った時点で妨げの記録が消えること。
    EXPECT_EQ(0, blocking_peers_after_clear); // [確認_正常系] - flow_blocking_peers が 0 に戻ること。
    EXPECT_EQ(0U, count_without_remark);
    // [確認_正常系] - 記録消去後に再停止しても、再度の妨げ記録までは切断対象にならないこと。
    ASSERT_EQ(1U, count_after_remark);
    EXPECT_EQ(10U, slow_peers[0]); // [確認_正常系] - 妨げを再記録すると、そこから猶予時間後に切断対象になること。
}
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.TearDown]

// 送信キュー満杯の記録により、保留エントリを持つ止める状態のピアだけに妨げが記録され、猶予時間後に切断対象になることの確認
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.SetUp]
TEST_F(potrFlowControlN1SlowPeerTest, queue_full_marks_deferred_stopping_peers_only)
{
    // Arrange
    potr_internal_peer_context *p1 = add_peer(0, 10U);
    potr_internal_peer_context *p2 = add_peer(1, 20U);
    potr_internal_peer_context *p3 = add_peer(2, 30U);
    ctx.flow_control_config.excluded_receiver_count = 2U;
    set_peer_stopping(p1, true);
    p1->flow_deferred_entries = 2;
    p1->flow_deferred_since_ms = 1000U;
    set_peer_stopping(p2, true);
    p2->flow_deferred_entries = 0; // 止める状態だが保留なし
    set_peer_stopping(p3, false);
    p3->flow_deferred_entries = 1; // 保留ありだが止める状態でない
    p3->flow_deferred_since_ms = 1000U;
    // [状態] - ピア 10 (停止・保留あり)、ピア 20 (停止・保留なし)、ピア 30 (非停止・保留あり) を用意する。
    std::vector<potr_peer_id> slow_peers;
    std::vector<uint64_t> slow_accum;
    uint64_t next_ms = 0U;
    // Pre-Assert
    // Act
    potr_internal_flow_control_note_queue_full(&ctx, 1050U);
    // [手順] - 時刻 1050 で送信キュー満杯を記録する。
    now_ms = 1150U;
    size_t count_mid = collect_slow(slow_peers, slow_accum, &next_ms);
    // [手順] - 時刻 1150 (満杯から 100 [ms] 経過) に収集を呼ぶ。
    now_ms = 1250U;
    size_t count_grace = collect_slow(slow_peers, slow_accum, &next_ms);
    // [手順] - 時刻 1250 (満杯から 200 [ms] 経過) に収集を呼ぶ。
    // Assert
    EXPECT_EQ(0U, count_mid);        // [確認_正常系] - 100 [ms] 経過時点ではまだ切断対象でないこと。
    EXPECT_EQ(1, p1->flow_blocking); // [確認_正常系] - 停止かつ保留ありのピア 10 に妨げが記録されること。
    EXPECT_EQ(0, p2->flow_blocking); // [確認_正常系] - 保留なしのピア 20 には妨げが記録されないこと。
    EXPECT_EQ(0, p3->flow_blocking); // [確認_正常系] - 非停止のピア 30 には妨げが記録されないこと。
    ASSERT_EQ(1U, count_grace);      // [確認_正常系] - 満杯時刻 1050 から 200 [ms] 後の 1250 で切断対象になること。
    EXPECT_EQ(10U, slow_peers[0]);   // [確認_正常系] - 切断対象がピア 10 であること。
}
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.TearDown]

// 除外上限が無効 (count=0, percent=0) の場合と接続ピアが 1 台だけの場合は切断対象にならないことの確認
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.SetUp]
TEST_F(potrFlowControlN1SlowPeerTest, excluded_limit_disabled_or_single_peer_never_disconnects)
{
    // Arrange
    potr_internal_peer_context *p1 = add_peer(0, 10U);
    set_peer_stopping(p1, true);
    potr_internal_flow_control_peer_mark_blocking(&ctx, 10U, now_ms);
    // [状態] - 接続中 1 ピアだけでピア 10 を止める状態にし、妨げを記録する。
    std::vector<potr_peer_id> slow_peers;
    std::vector<uint64_t> slow_accum;
    uint64_t next_ms = 0U;
    // Pre-Assert
    // Act
    now_ms = 1500U;
    size_t count_single = collect_slow(slow_peers, slow_accum, &next_ms);
    // [手順] - 接続 1 台で 500 [ms] 経過後に収集を呼ぶ。
    add_peer(1, 20U);
    ctx.flow_control_config.excluded_receiver_count = 0U;
    ctx.flow_control_config.excluded_receiver_percent = 0U;
    now_ms = 2000U;
    size_t count_disabled = collect_slow(slow_peers, slow_accum, &next_ms);
    // [手順] - ピア 20 を追加して接続 2 台にしたうえで除外上限を両方 0 (無効) にして収集を呼ぶ。
    // Assert
    EXPECT_EQ(0U, count_single);   // [確認_正常系] - 接続 1 台では上限 0 のため切断対象にならないこと。
    EXPECT_EQ(0U, count_disabled); // [確認_正常系] - 除外上限が無効の場合は切断対象にならないこと。
}
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.TearDown]

// N:1 ピアで、解除で起きた判定でも直前に止めていた区間を加算し、全体が遅い間は既存の積算を据え置くことの確認
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.SetUp]
TEST_F(potrFlowControlN1SlowPeerTest, short_stops_accumulate_on_release_and_all_slow_freezes_accum)
{
    // Arrange
    potr_internal_peer_context *p1 = add_peer(0, 10U);
    potr_internal_peer_context *p2 = add_peer(1, 20U);
    add_peer(2, 30U);
    // 接続 3 台、除外上限 1 台。
    set_peer_stopping(p1, true);
    potr_internal_flow_control_peer_mark_blocking(&ctx, 10U, now_ms);
    std::vector<potr_peer_id> slow_peers;
    std::vector<uint64_t> slow_accum;
    uint64_t next_ms = 0U;
    uint64_t cycle_accum[5] = {};
    // [状態] - 接続 3 ピア (上限 1) でピア 10 に妨げを記録し、40 [ms] 停止・20 [ms] 送出を繰り返す。
    // Pre-Assert
    // Act
    for (uint64_t cycle = 0U; cycle < 5U; cycle++)
    {
        uint64_t start_ms = 1000U + cycle * 60U;
        if (cycle > 0U)
        {
            set_peer_stopping(p1, true);
            now_ms = start_ms;
            (void)collect_slow(slow_peers, slow_accum, &next_ms);
        }
        set_peer_stopping(p1, false);
        now_ms = start_ms + 40U;
        (void)collect_slow(slow_peers, slow_accum, &next_ms);
        now_ms = start_ms + 60U;
        (void)collect_slow(slow_peers, slow_accum, &next_ms);
        cycle_accum[cycle] = p1->flow_stall_accum_units;
    }
    // 全体が遅い間 (同時停止 2 > 上限 1) は既存の積算 (1700 単位) を加算も減算もせず据え置く。
    set_peer_stopping(p1, true);
    set_peer_stopping(p2, true);
    now_ms = 1300U;
    (void)collect_slow(slow_peers, slow_accum, &next_ms);
    now_ms = 1600U;
    size_t count_all_slow = collect_slow(slow_peers, slow_accum, &next_ms);
    uint64_t accum_all_slow = p1->flow_stall_accum_units;
    // ピア 20 が回復し単独停止に戻った後、残り 100 単位 (12 [ms]) で猶予時間 (1800 単位) に達する。
    set_peer_stopping(p2, false);
    now_ms = 1600U;
    (void)collect_slow(slow_peers, slow_accum, &next_ms);
    now_ms = 1612U;
    size_t count_at_grace = collect_slow(slow_peers, slow_accum, &next_ms);
    // [手順] - 解除後の判定での停止区間加算を 5 周期確認し、全体停止による据え置き後に単独停止で猶予時間へ到達させる。
    // Assert
    for (size_t cycle = 0U; cycle < 5U; cycle++)
    {
        EXPECT_EQ((cycle + 1U) * (40U * 9U - 20U), cycle_accum[cycle]);
        // [確認_正常系] - 解除で起きた判定でも直前の 40 [ms] を加算し、1 周期で 340 単位ずつ増えること。(確認 5 回)
    }
    EXPECT_EQ(0U, count_all_slow);     // [確認_正常系] - 全体が遅い間は切断対象にならないこと。
    EXPECT_EQ(1700U, accum_all_slow);  // [確認_正常系] - 全体が遅い間は既存の積算 1700 単位を据え置くこと。
    ASSERT_EQ(1U, count_at_grace);     // [確認_正常系] - 単独停止へ戻った後に残りの積算で切断対象になること。
    EXPECT_EQ(10U, slow_peers[0]);     // [確認_正常系] - 切断対象がピア 10 であること。
}
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.TearDown]

// 遅いピアとして切断したセッションは猶予時間の 10 倍の間拒否され (INFO は初回 1 回だけ)、期間経過後に受け入れられることの確認
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.SetUp]
TEST_F(potrFlowControlN1SlowPeerTest, slow_peer_reject_within_duration_and_accept_after_expiry)
{
    // Arrange
    const uint32_t session_id = 101U;
    const cplat_timespec session_ts = {1000, 500};
    const uint64_t duration_ms = (uint64_t)ctx.flow_control_config.grace_ms *
                                 (uint64_t)POTR_FLOW_UNEXCLUDE_GRACE_MULTIPLIER;
    // [状態] - 猶予時間 200 [ms] (拒否期間 2000 [ms]) の N:1 サーバーとセッション (101, 1000.500) を用意する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat,
                cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_INFO, nullptr,
                                       HasSubstr("ignoring packets from disconnected slow peer")))
        .Times(1);
    // [Pre-Assert確認_正常系] - 期間内のパケット破棄の INFO 出力がセッションごとに最初の 1 回だけであること。
    // Act
    cplat_local_lock_lock(ctx.peers_mutex);
    potr_internal_flow_control_slow_peer_record_locked(&ctx, session_id, &session_ts, 1000U);
    // [手順] - 時刻 1000 で遅いピアのセッションを記録する。
    int rejected_at_start = potr_internal_flow_control_slow_peer_rejected_locked(&ctx, session_id, &session_ts, 1000U);
    int rejected_before_end =
        potr_internal_flow_control_slow_peer_rejected_locked(&ctx, session_id, &session_ts, 1000U + duration_ms - 1U);
    // [手順] - 記録直後と期間終了 1 [ms] 前に照合する。
    int rejected_at_expiry =
        potr_internal_flow_control_slow_peer_rejected_locked(&ctx, session_id, &session_ts, 1000U + duration_ms);
    size_t count_after_expiry = ctx.flow_slow_rejected_count;
    cplat_local_lock_unlock(ctx.peers_mutex);
    // [手順] - 猶予時間の 10 倍 (2000 [ms]) 経過後に照合する。
    // Assert
    EXPECT_EQ(1, rejected_at_start);     // [確認_正常系] - 記録直後は拒否 (1) されること。
    EXPECT_EQ(1, rejected_before_end);   // [確認_正常系] - 期間終了 1 [ms] 前も拒否 (1) されること。
    EXPECT_EQ(0, rejected_at_expiry);    // [確認_正常系] - 期間経過後は受け入れ (0) になること。
    EXPECT_EQ(0U, count_after_expiry);   // [確認_正常系] - 期間が過ぎた記録が照合時に消去されること。
}
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.TearDown]

// 別のセッション (セッション識別子または開始時刻が異なる) は拒否期間内でも受け入れられることの確認
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.SetUp]
TEST_F(potrFlowControlN1SlowPeerTest, slow_peer_reject_allows_different_session_id_or_timestamp)
{
    // Arrange
    const uint32_t session_id = 101U;
    const cplat_timespec session_ts = {1000, 500};
    const cplat_timespec diff_sec_ts = {1001, 500};
    const cplat_timespec diff_nsec_ts = {1000, 501};
    // [状態] - 切断したセッション (101, 1000.500) と、識別子または開始時刻が異なるセッションを用意する。
    // Pre-Assert
    // Act
    cplat_local_lock_lock(ctx.peers_mutex);
    potr_internal_flow_control_slow_peer_record_locked(&ctx, session_id, &session_ts, 1000U);
    int diff_id = potr_internal_flow_control_slow_peer_rejected_locked(&ctx, session_id + 1U, &session_ts, 1500U);
    int diff_sec = potr_internal_flow_control_slow_peer_rejected_locked(&ctx, session_id, &diff_sec_ts, 1500U);
    int diff_nsec = potr_internal_flow_control_slow_peer_rejected_locked(&ctx, session_id, &diff_nsec_ts, 1500U);
    int same_session = potr_internal_flow_control_slow_peer_rejected_locked(&ctx, session_id, &session_ts, 1500U);
    cplat_local_lock_unlock(ctx.peers_mutex);
    // [手順] - 拒否期間内 (時刻 1500) に、別セッション 3 種と元のセッションを照合する。
    // Assert
    EXPECT_EQ(0, diff_id);       // [確認_正常系] - セッション識別子が異なる場合は受け入れ (0) になること。
    EXPECT_EQ(0, diff_sec);      // [確認_正常系] - 開始時刻の秒が異なる場合は受け入れ (0) になること。
    EXPECT_EQ(0, diff_nsec);     // [確認_正常系] - 開始時刻のナノ秒が異なる場合は受け入れ (0) になること。
    EXPECT_EQ(1, same_session);  // [確認_正常系] - 元のセッション自体は引き続き拒否 (1) されること。
}
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.TearDown]

// 受け入れ拒否記録の表が満杯の場合に、最も古い記録が上書きされることの確認
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.SetUp]
TEST_F(potrFlowControlN1SlowPeerTest, slow_peer_reject_table_full_overwrites_oldest)
{
    // Arrange
    const cplat_timespec base_ts = {2000, 0};
    std::vector<potr_internal_peer_context> connected(POTR_FLOW_SLOW_PEER_REJECT_MAX + 2U);
    for (auto &peer : connected)
    {
        peer.active = 1;
        cplat_atomic_store_i32(&peer.health_alive, 1, CPLAT_MEMORY_ORDER_RELAXED);
    }
    ctx.peers = connected.data();
    ctx.max_peers = (int)connected.size();
    ctx.flow_control_config.excluded_receiver_count = POTR_FLOW_SLOW_PEER_REJECT_MAX + 1U;
    // [状態] - 除外上限を物理上限より大きくし、128 件の物理上限による上書きを確認する。
    // [状態] - 拒否期間 (2000 [ms]) 内に POTR_FLOW_SLOW_PEER_REJECT_MAX + 1 件のセッションを記録する条件を用意する。
    // Pre-Assert
    // Act
    cplat_local_lock_lock(ctx.peers_mutex);
    for (uint32_t i = 0U; i < POTR_FLOW_SLOW_PEER_REJECT_MAX; i++)
    {
        potr_internal_flow_control_slow_peer_record_locked(&ctx, 1000U + i, &base_ts, 1000U + i);
    }
    // [手順] - 時刻 1000 から 1 [ms] 刻みで表の上限までセッションを記録する。
    const uint32_t newest_id = 1000U + POTR_FLOW_SLOW_PEER_REJECT_MAX;
    const uint64_t check_ms = 1000U + POTR_FLOW_SLOW_PEER_REJECT_MAX;
    potr_internal_flow_control_slow_peer_record_locked(&ctx, newest_id, &base_ts, check_ms);
    // [手順] - 満杯の状態でさらに 1 件記録し、最も古い記録 (1000) を上書きさせる。
    size_t count = ctx.flow_slow_rejected_count;
    int oldest_rejected = potr_internal_flow_control_slow_peer_rejected_locked(&ctx, 1000U, &base_ts, check_ms);
    int second_rejected = potr_internal_flow_control_slow_peer_rejected_locked(&ctx, 1001U, &base_ts, check_ms);
    int newest_rejected = potr_internal_flow_control_slow_peer_rejected_locked(&ctx, newest_id, &base_ts, check_ms);
    cplat_local_lock_unlock(ctx.peers_mutex);
    // Assert
    EXPECT_EQ((size_t)POTR_FLOW_SLOW_PEER_REJECT_MAX, count);
    // [確認_正常系] - 有効な記録数が表の上限のままであること。
    EXPECT_EQ(0, oldest_rejected);  // [確認_正常系] - 最も古い記録 (1000) が上書きされて受け入れ (0) になること。
    EXPECT_EQ(1, second_rejected);  // [確認_正常系] - 2 番目に古い記録 (1001) は保持されて拒否 (1) されること。
    EXPECT_EQ(1, newest_rejected);  // [確認_正常系] - 新しく追加した記録が保持されて拒否 (1) されること。
}
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.TearDown]

// 期間経過後に受け入れたピアが再び遅い場合に、再び切断対象となり、再度記録されて拒否されることの確認
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.SetUp]
TEST_F(potrFlowControlN1SlowPeerTest, slow_peer_reaccepted_and_slow_again_is_disconnected_and_rerecorded)
{
    // Arrange
    const uint32_t session_id = 555U;
    const cplat_timespec session_ts = {3000, 123};
    const uint64_t duration_ms = (uint64_t)ctx.flow_control_config.grace_ms *
                                 (uint64_t)POTR_FLOW_UNEXCLUDE_GRACE_MULTIPLIER;
    potr_internal_peer_context *p1 = add_peer(0, 10U);
    add_peer(1, 20U);
    p1->peer_session_id = session_id;
    p1->peer_session_ts = session_ts;
    p1->peer_session_known = 1;
    set_peer_stopping(p1, true);
    potr_internal_flow_control_peer_mark_blocking(&ctx, 10U, now_ms);
    std::vector<potr_peer_id> slow_peers;
    std::vector<uint64_t> slow_accum;
    uint64_t next_ms = 0U;
    // [状態] - 接続中 2 ピアのうちピア 10 (セッション 555) を止める状態にし、時刻 1000 で妨げを記録する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat,
                cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_INFO, nullptr,
                                       HasSubstr("ignoring packets from disconnected slow peer")))
        .Times(2);
    // [Pre-Assert確認_正常系] - 1 回目と 2 回目の拒否期間で、破棄の INFO 出力がそれぞれ 1 回ずつ出ること。
    // Act
    now_ms = 1200U;
    size_t first_count = collect_slow(slow_peers, slow_accum, &next_ms);
    cplat_local_lock_lock(ctx.peers_mutex);
    potr_internal_flow_control_slow_peer_record_locked(&ctx, p1->peer_session_id, &p1->peer_session_ts, now_ms);
    potr_internal_flow_control_peer_clear_blocking(&ctx, p1);
    potr_internal_flow_control_peer_untrack(&ctx, p1);
    p1->active = 0;
    ctx.n_peers--;
    int first_rejected = potr_internal_flow_control_slow_peer_rejected_locked(&ctx, session_id, &session_ts, 1500U);
    int after_first_expiry =
        potr_internal_flow_control_slow_peer_rejected_locked(&ctx, session_id, &session_ts, 1200U + duration_ms);
    cplat_local_lock_unlock(ctx.peers_mutex);
    // [手順] - 1 回目の切断と記録の後、期間内の拒否と期間経過後の受け入れを確認する。

    now_ms = 1200U + duration_ms;
    cplat_local_lock_dispose(p1->send_window_mutex);
    p1->send_window_mutex = nullptr;
    potr_internal_peer_context *p1_reconnected = add_peer(0, 11U);
    p1_reconnected->peer_session_id = session_id;
    p1_reconnected->peer_session_ts = session_ts;
    p1_reconnected->peer_session_known = 1;
    set_peer_stopping(p1_reconnected, true);
    potr_internal_flow_control_peer_mark_blocking(&ctx, 11U, now_ms);
    now_ms += 200U;
    size_t second_count = collect_slow(slow_peers, slow_accum, &next_ms);
    potr_peer_id second_peer = (second_count > 0U) ? slow_peers[0] : POTR_PEER_NA;
    cplat_local_lock_lock(ctx.peers_mutex);
    potr_internal_flow_control_slow_peer_record_locked(&ctx, p1_reconnected->peer_session_id,
                                                       &p1_reconnected->peer_session_ts, now_ms);
    int second_rejected =
        potr_internal_flow_control_slow_peer_rejected_locked(&ctx, session_id, &session_ts, now_ms + 100U);
    int after_second_expiry =
        potr_internal_flow_control_slow_peer_rejected_locked(&ctx, session_id, &session_ts, now_ms + duration_ms);
    cplat_local_lock_unlock(ctx.peers_mutex);
    // [手順] - 同じセッションを受け入れ直した後に再び遅くし、再度の切断・記録・期間内拒否・期間経過後受け入れを確認する。
    // Assert
    EXPECT_EQ(1U, first_count);          // [確認_正常系] - 1 回目の猶予時間到達で切断対象になること。
    EXPECT_EQ(1, first_rejected);        // [確認_正常系] - 1 回目の切断後の拒否期間内は拒否されること。
    EXPECT_EQ(0, after_first_expiry);    // [確認_正常系] - 1 回目の拒否期間経過後は受け入れられること。
    EXPECT_EQ(1U, second_count);         // [確認_正常系] - 再受け入れ後も遅い場合に再び切断対象になること。
    EXPECT_EQ(11U, second_peer);         // [確認_正常系] - 2 回目の切断対象が再接続したピア 11 であること。
    EXPECT_EQ(1, second_rejected);       // [確認_正常系] - 2 回目の切断後の拒否期間内は再び拒否されること。
    EXPECT_EQ(0, after_second_expiry);   // [確認_正常系] - 2 回目の拒否期間経過後は再び受け入れられること。
}
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.TearDown]

// 除外上限 1 件を超える拒否記録を追加すると、最古の記録だけを消すことの確認
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.SetUp]
TEST_F(potrFlowControlN1SlowPeerTest, slow_peer_reject_retains_excluded_limit_1)
{
    // Arrange
    const cplat_timespec ts = {2000, 0};
    ctx.flow_control_config.excluded_receiver_count = 1U;
    for (int i = 0; i < 2; i++)
    {
        add_peer(i, (potr_peer_id)(i + 1));
    }
    // [状態] - 接続中のピアを 2 台、除外する数の上限を 1 件にする。
    int retained[2] = {};
    // Pre-Assert
    // Act
    cplat_local_lock_lock(ctx.peers_mutex);
    for (uint32_t i = 0U; i < 2U; i++)
    {
        potr_internal_flow_control_slow_peer_record_locked(&ctx, i + 1U, &ts, 1000U);
    }
    for (uint32_t i = 0U; i < 2U; i++)
    {
        retained[i] = potr_internal_flow_control_slow_peer_rejected_locked(&ctx, i + 1U, &ts, 1000U);
    }
    cplat_local_lock_unlock(ctx.peers_mutex);
    // [手順] - 同じ時刻に 2 件を順に記録し、各セッションを照合する。
    // Assert
    EXPECT_EQ(1U, ctx.flow_slow_rejected_count); // [確認_正常系] - 保持数が除外上限と一致すること。
    EXPECT_EQ(0, retained[0]); // [確認_正常系] - 最初の記録を消すこと。
    for (size_t i = 1U; i < 2U; i++)
    {
        EXPECT_EQ(1, retained[i]); // [確認_正常系 回数=1] - 新しい記録を保持すること。
    }
}
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.TearDown]

// 除外上限 3 件を超える拒否記録を追加すると、最古の記録だけを消すことの確認
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.SetUp]
TEST_F(potrFlowControlN1SlowPeerTest, slow_peer_reject_retains_excluded_limit_3)
{
    // Arrange
    const cplat_timespec ts = {2000, 0};
    ctx.flow_control_config.excluded_receiver_count = 3U;
    for (int i = 0; i < 4; i++)
    {
        add_peer(i, (potr_peer_id)(i + 1));
    }
    // [状態] - 接続中のピアを 4 台、除外する数の上限を 3 件にする。
    int retained[4] = {};
    // Pre-Assert
    // Act
    cplat_local_lock_lock(ctx.peers_mutex);
    for (uint32_t i = 0U; i < 4U; i++)
    {
        potr_internal_flow_control_slow_peer_record_locked(&ctx, i + 1U, &ts, 1000U);
    }
    for (uint32_t i = 0U; i < 4U; i++)
    {
        retained[i] = potr_internal_flow_control_slow_peer_rejected_locked(&ctx, i + 1U, &ts, 1000U);
    }
    cplat_local_lock_unlock(ctx.peers_mutex);
    // [手順] - 同じ時刻に 4 件を順に記録し、各セッションを照合する。
    // Assert
    EXPECT_EQ(3U, ctx.flow_slow_rejected_count); // [確認_正常系] - 保持数が除外上限と一致すること。
    EXPECT_EQ(0, retained[0]); // [確認_正常系] - 最初の記録を消すこと。
    for (size_t i = 1U; i < 4U; i++)
    {
        EXPECT_EQ(1, retained[i]); // [確認_正常系 回数=3] - 新しい記録を保持すること。
    }
}
// [サブ手順参照 名前=potrFlowControlN1SlowPeerTest.TearDown]
