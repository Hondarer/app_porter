/**
 *******************************************************************************
 *  @file           pathGroupTest.cc
 *  @brief          優先グループの選択と状態遷移を検証します。
 *
 *******************************************************************************
 */
#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <porter/protocol/path_group.h>
#include <cstring>

// 経路構成に応じて最上位を選び、グループ内の全断で切り替えることの確認
TEST(pathGroupTest, two_paths_four_groups)
{
    // Arrange
    potr_internal_path_group_input input = {};
    potr_internal_path_group_state state = {};
    potr_internal_path_group_result selection = {};
    input.type = POTR_TYPE_UNICAST_BIDIR;
    input.interval_ms = 10;
    input.timeout_ms = 100;
    input.failback_ms = 50;
    input.now_ms = 1000;
    const uint8_t groups[POTR_MAX_PATH] = {1, 1, 2, 2, 3, 3, 4, 4};
    std::memcpy(input.group, groups, sizeof(groups));
    std::memset(input.available, 1, sizeof(input.available));
    std::memset(input.reachable, 1, sizeof(input.reachable));
    potr_internal_path_group_result initial = {};
    potr_internal_path_group_result partial = {};
    // Pre-Assert
    // Act
    potr_internal_path_group_evaluate(&input, &state, &state, &initial);
    // [手順] - 全経路到達の状態で最上位グループを選ぶ。
    input.reachable[0] = 0;
    potr_internal_path_group_evaluate(&input, &state, &state, &partial);
    // [手順] - 同じグループの 1 経路だけを不達にする。
    for (size_t i = 0; i < POTR_MAX_PATH; i++)
    {
        if (groups[i] == 1U)
        {
            input.reachable[i] = 0;
        }
    }
    potr_internal_path_group_evaluate(&input, &state, &state, &selection);
    // [手順] - 最上位グループの全経路を不達にする。
    // Assert
    EXPECT_EQ(3U, initial.data_paths); // [確認_正常系] - 最上位グループの全経路を選ぶこと。
    EXPECT_EQ(3U, partial.data_paths); // [確認_正常系] - グループ内の 1 経路の断では選択を維持すること。
    EXPECT_EQ(12U, selection.data_paths); // [確認_正常系] - 到達できる次のグループの全経路を選ぶこと。
    EXPECT_EQ(0, selection.disconnected); // [確認_正常系] - 到達可能なグループがなければ論理切断になること。
}

// 経路構成に応じて最上位を選び、グループ内の全断で切り替えることの確認
TEST(pathGroupTest, eight_paths_one_group)
{
    // Arrange
    potr_internal_path_group_input input = {};
    potr_internal_path_group_state state = {};
    potr_internal_path_group_result selection = {};
    input.type = POTR_TYPE_UNICAST_BIDIR;
    input.interval_ms = 10;
    input.timeout_ms = 100;
    input.failback_ms = 50;
    input.now_ms = 1000;
    const uint8_t groups[POTR_MAX_PATH] = {1, 1, 1, 1, 1, 1, 1, 1};
    std::memcpy(input.group, groups, sizeof(groups));
    std::memset(input.available, 1, sizeof(input.available));
    std::memset(input.reachable, 1, sizeof(input.reachable));
    potr_internal_path_group_result initial = {};
    potr_internal_path_group_result partial = {};
    // Pre-Assert
    // Act
    potr_internal_path_group_evaluate(&input, &state, &state, &initial);
    // [手順] - 全経路到達の状態で最上位グループを選ぶ。
    input.reachable[0] = 0;
    potr_internal_path_group_evaluate(&input, &state, &state, &partial);
    // [手順] - 同じグループの 1 経路だけを不達にする。
    for (size_t i = 0; i < POTR_MAX_PATH; i++)
    {
        if (groups[i] == 1U)
        {
            input.reachable[i] = 0;
        }
    }
    potr_internal_path_group_evaluate(&input, &state, &state, &selection);
    // [手順] - 最上位グループの全経路を不達にする。
    // Assert
    EXPECT_EQ(255U, initial.data_paths); // [確認_正常系] - 最上位グループの全経路を選ぶこと。
    EXPECT_EQ(255U, partial.data_paths); // [確認_正常系] - グループ内の 1 経路の断では選択を維持すること。
    EXPECT_EQ(0U, selection.data_paths); // [確認_正常系] - 到達できる次のグループの全経路を選ぶこと。
    EXPECT_EQ(1, selection.disconnected); // [確認_正常系] - 到達可能なグループがなければ論理切断になること。
}

// 経路構成に応じて最上位を選び、グループ内の全断で切り替えることの確認
TEST(pathGroupTest, three_and_five_paths)
{
    // Arrange
    potr_internal_path_group_input input = {};
    potr_internal_path_group_state state = {};
    potr_internal_path_group_result selection = {};
    input.type = POTR_TYPE_UNICAST_BIDIR;
    input.interval_ms = 10;
    input.timeout_ms = 100;
    input.failback_ms = 50;
    input.now_ms = 1000;
    const uint8_t groups[POTR_MAX_PATH] = {1, 1, 1, 2, 2, 2, 2, 2};
    std::memcpy(input.group, groups, sizeof(groups));
    std::memset(input.available, 1, sizeof(input.available));
    std::memset(input.reachable, 1, sizeof(input.reachable));
    potr_internal_path_group_result initial = {};
    potr_internal_path_group_result partial = {};
    // Pre-Assert
    // Act
    potr_internal_path_group_evaluate(&input, &state, &state, &initial);
    // [手順] - 全経路到達の状態で最上位グループを選ぶ。
    input.reachable[0] = 0;
    potr_internal_path_group_evaluate(&input, &state, &state, &partial);
    // [手順] - 同じグループの 1 経路だけを不達にする。
    for (size_t i = 0; i < POTR_MAX_PATH; i++)
    {
        if (groups[i] == 1U)
        {
            input.reachable[i] = 0;
        }
    }
    potr_internal_path_group_evaluate(&input, &state, &state, &selection);
    // [手順] - 最上位グループの全経路を不達にする。
    // Assert
    EXPECT_EQ(7U, initial.data_paths); // [確認_正常系] - 最上位グループの全経路を選ぶこと。
    EXPECT_EQ(7U, partial.data_paths); // [確認_正常系] - グループ内の 1 経路の断では選択を維持すること。
    EXPECT_EQ(248U, selection.data_paths); // [確認_正常系] - 到達できる次のグループの全経路を選ぶこと。
    EXPECT_EQ(0, selection.disconnected); // [確認_正常系] - 到達可能なグループがなければ論理切断になること。
}

// 経路構成に応じて最上位を選び、グループ内の全断で切り替えることの確認
TEST(pathGroupTest, groups_one_and_three)
{
    // Arrange
    potr_internal_path_group_input input = {};
    potr_internal_path_group_state state = {};
    potr_internal_path_group_result selection = {};
    input.type = POTR_TYPE_UNICAST_BIDIR;
    input.interval_ms = 10;
    input.timeout_ms = 100;
    input.failback_ms = 50;
    input.now_ms = 1000;
    const uint8_t groups[POTR_MAX_PATH] = {1, 1, 3, 3, 3, 3, 3, 3};
    std::memcpy(input.group, groups, sizeof(groups));
    std::memset(input.available, 1, sizeof(input.available));
    std::memset(input.reachable, 1, sizeof(input.reachable));
    potr_internal_path_group_result initial = {};
    potr_internal_path_group_result partial = {};
    // Pre-Assert
    // Act
    potr_internal_path_group_evaluate(&input, &state, &state, &initial);
    // [手順] - 全経路到達の状態で最上位グループを選ぶ。
    input.reachable[0] = 0;
    potr_internal_path_group_evaluate(&input, &state, &state, &partial);
    // [手順] - 同じグループの 1 経路だけを不達にする。
    for (size_t i = 0; i < POTR_MAX_PATH; i++)
    {
        if (groups[i] == 1U)
        {
            input.reachable[i] = 0;
        }
    }
    potr_internal_path_group_evaluate(&input, &state, &state, &selection);
    // [手順] - 最上位グループの全経路を不達にする。
    // Assert
    EXPECT_EQ(3U, initial.data_paths); // [確認_正常系] - 最上位グループの全経路を選ぶこと。
    EXPECT_EQ(3U, partial.data_paths); // [確認_正常系] - グループ内の 1 経路の断では選択を維持すること。
    EXPECT_EQ(252U, selection.data_paths); // [確認_正常系] - 到達できる次のグループの全経路を選ぶこと。
    EXPECT_EQ(0, selection.disconnected); // [確認_正常系] - 到達可能なグループがなければ論理切断になること。
}

// 切り戻し待機中は 2 グループへ送り、期限後に上位へ戻ることの確認
TEST(pathGroupTest, failback_parallel_then_upper_only)
{
    // Arrange
    potr_internal_path_group_input input = {};
    potr_internal_path_group_state state = {};
    potr_internal_path_group_result selection = {};
    input.type = POTR_TYPE_UNICAST_BIDIR;
    input.interval_ms = 10;
    input.timeout_ms = 100;
    input.failback_ms = 50;
    input.now_ms = 1000;
    input.group[0] = 1;
    input.group[1] = 3;
    input.available[0] = input.available[1] = 1;
    input.reachable[0] = input.reachable[1] = 1;
    state.selected = 3;
    state.confirmed = 1;
    potr_internal_path_group_result waiting = {};
    // Pre-Assert
    // Act
    potr_internal_path_group_evaluate(&input, &state, &state, &waiting);
    input.now_ms += 50;
    potr_internal_path_group_evaluate(&input, &state, &state, &selection);
    // [手順] - 上位回復時と待機期限に判定する。
    // Assert
    EXPECT_EQ(3U, waiting.data_paths); // [確認_正常系] - 待機中の DATA は 2 グループに送ること。
    EXPECT_EQ(2U, waiting.selected_paths); // [確認_正常系] - 再送と REJECT は選択中だけに送ること。
    EXPECT_EQ(1U, selection.data_paths); // [確認_正常系] - 期限後は上位だけに送ること。
    EXPECT_EQ(1U, selection.changed); // [確認_正常系] - 切り戻しを記録すること。
    EXPECT_EQ(3U, selection.previous_group); // [確認_正常系] - 元グループを出力すること。
    EXPECT_EQ(1U, selection.next_group); // [確認_正常系] - 戻り先を出力すること。
    EXPECT_EQ(0U, selection.tcp_resend); // [確認_正常系] - 切り戻しでは送り直しを要求しないこと。
}

// 回復後の再不達で待機を取り消し、再回復から待ち直すことの確認
TEST(pathGroupTest, unstable_recovery_cancels_wait)
{
    // Arrange
    potr_internal_path_group_input input = {};
    potr_internal_path_group_state state = {};
    potr_internal_path_group_result selection = {};
    input.type = POTR_TYPE_UNICAST_BIDIR;
    input.interval_ms = 10;
    input.timeout_ms = 100;
    input.failback_ms = 50;
    input.now_ms = 1000;
    input.group[0] = 1;
    input.group[1] = 2;
    input.available[0] = input.available[1] = 1;
    input.reachable[0] = input.reachable[1] = 1;
    state.selected = 2;
    state.confirmed = 1;
    potr_internal_path_group_result canceled = {};
    // Pre-Assert
    // Act
    potr_internal_path_group_evaluate(&input, &state, &state, &selection);
    input.now_ms += 40;
    input.reachable[0] = 0;
    potr_internal_path_group_evaluate(&input, &state, &state, &canceled);
    input.now_ms += 40;
    input.reachable[0] = 1;
    potr_internal_path_group_evaluate(&input, &state, &state, &selection);
    // [手順] - 上位グループの回復、不達、再回復を順に判定する。
    // Assert
    EXPECT_EQ(2U, canceled.data_paths); // [確認_正常系] - 再不達では選択中だけへ送ること。
    EXPECT_EQ(3U, selection.data_paths); // [確認_正常系] - 再回復直後は並行送信すること。
    EXPECT_EQ(2U, state.selected); // [確認_正常系] - 前回の待機時間を引き継がないこと。
    EXPECT_EQ(input.now_ms, state.candidate_since_ms); // [確認_正常系] - 再回復した時刻から待ち直すこと。
}

// 複数の上位回復では最上位を切り戻し候補にすることの確認
TEST(pathGroupTest, highest_recovery_replaces_candidate)
{
    // Arrange
    potr_internal_path_group_input input = {};
    potr_internal_path_group_state state = {};
    potr_internal_path_group_result selection = {};
    input.type = POTR_TYPE_UNICAST_BIDIR;
    input.interval_ms = 10;
    input.timeout_ms = 100;
    input.failback_ms = 50;
    input.now_ms = 1000;
    input.group[0] = 1;
    input.group[1] = 2;
    input.group[2] = 4;
    input.available[0] = input.available[1] = input.available[2] = 1;
    input.reachable[1] = input.reachable[2] = 1;
    state.selected = 4;
    state.confirmed = 1;
    // Pre-Assert
    // Act
    potr_internal_path_group_evaluate(&input, &state, &state, &selection);
    input.now_ms += 40;
    input.reachable[0] = 1;
    potr_internal_path_group_evaluate(&input, &state, &state, &selection);
    // [手順] - グループ 2 の回復後にグループ 1 を回復させる。
    // Assert
    EXPECT_EQ(1U, state.candidate); // [確認_正常系] - 最上位を候補にすること。
    EXPECT_EQ(5U, selection.data_paths); // [確認_正常系] - 候補と現在の 2 グループだけを選ぶこと。
    EXPECT_EQ(input.now_ms, state.candidate_since_ms); // [確認_正常系] - 候補の変更時から待つこと。
}

// 切り戻し時間 0 では実効タイムアウトの間待つことの確認
TEST(pathGroupTest, zero_failback_uses_effective_timeout)
{
    // Arrange
    potr_internal_path_group_input input = {};
    potr_internal_path_group_state state = {};
    potr_internal_path_group_result selection = {};
    input.type = POTR_TYPE_UNICAST_BIDIR;
    input.interval_ms = 10;
    input.timeout_ms = 100;
    input.failback_ms = 50;
    input.now_ms = 1000;
    input.group[0] = 1;
    input.group[1] = 2;
    input.available[0] = input.available[1] = 1;
    input.reachable[0] = input.reachable[1] = 1;
    input.failback_ms = 0;
    state.selected = 2;
    state.confirmed = 1;
    potr_internal_path_group_result before_deadline = {};
    // Pre-Assert
    // Act
    potr_internal_path_group_evaluate(&input, &state, &state, &selection);
    input.now_ms += 99;
    potr_internal_path_group_evaluate(&input, &state, &state, &before_deadline);
    input.now_ms++;
    potr_internal_path_group_evaluate(&input, &state, &state, &selection);
    // [手順] - 実効タイムアウトの直前と期限で判定する。
    // Assert
    EXPECT_EQ(3U, before_deadline.data_paths); // [確認_正常系] - 期限前は 2 グループへ送ること。
    EXPECT_EQ(1U, selection.data_paths); // [確認_正常系] - 実効タイムアウト経過後に切り戻すこと。
}

// TCP の不達切り替えだけで送り直しを要求することの確認
TEST(pathGroupTest, tcp_switch_requires_replay_but_failback_does_not)
{
    // Arrange
    potr_internal_path_group_input input = {};
    potr_internal_path_group_state state = {};
    potr_internal_path_group_result selection = {};
    input.type = POTR_TYPE_UNICAST_BIDIR;
    input.interval_ms = 10;
    input.timeout_ms = 100;
    input.failback_ms = 50;
    input.now_ms = 1000;
    input.type = POTR_TYPE_TCP;
    input.group[0] = 1;
    input.group[1] = 2;
    input.available[0] = input.available[1] = 1;
    input.reachable[1] = 1;
    state.selected = 1;
    state.confirmed = 1;
    potr_internal_path_group_result switched = {};
    // Pre-Assert
    // Act
    potr_internal_path_group_evaluate(&input, &state, &state, &switched);
    state.resend_group = 0;
    input.reachable[0] = 1;
    potr_internal_path_group_evaluate(&input, &state, &state, &selection);
    // [手順] - 不達による切り替えを判定し、送り直し完了後に上位を回復させる。
    // Assert
    EXPECT_EQ(1U, switched.tcp_resend); // [確認_正常系] - 不達切り替えでは送り直しを要求すること。
    EXPECT_EQ(2U, switched.next_group); // [確認_正常系] - 到達可能な下位へ切り替えること。
    EXPECT_EQ(0U, selection.tcp_resend); // [確認_正常系] - 切り戻し待機では送り直さないこと。
    EXPECT_EQ(3U, selection.data_paths); // [確認_正常系] - 切り戻し待機では 2 グループへ送ること。
}

// 片方向では起動直後に最優先、報告なしの期限後に全経路へ送ることの確認
TEST(pathGroupTest, oneway_startup_then_no_reports)
{
    // Arrange
    potr_internal_path_group_input input = {};
    potr_internal_path_group_state state = {};
    potr_internal_path_group_result selection = {};
    input.type = POTR_TYPE_UNICAST_BIDIR;
    input.interval_ms = 10;
    input.timeout_ms = 100;
    input.failback_ms = 50;
    input.now_ms = 1000;
    input.type = POTR_TYPE_UNICAST;
    input.started_ms = 1000;
    input.group[0] = 2;
    input.group[1] = 4;
    input.available[0] = input.available[1] = 1;
    potr_internal_path_group_result initial = {};
    // Pre-Assert
    // Act
    potr_internal_path_group_evaluate(&input, &state, &state, &initial);
    input.now_ms += 100;
    potr_internal_path_group_evaluate(&input, &state, &state, &selection);
    // [手順] - 報告を受け取らず起動直後とタイムアウト経過時を判定する。
    // Assert
    EXPECT_EQ(1U, initial.data_paths); // [確認_正常系] - 経路を持つ最小番号のグループだけへ送ること。
    EXPECT_EQ(3U, selection.data_paths); // [確認_正常系] - 到達確認手段がなければ全経路へ送ること。
    EXPECT_EQ(0U, selection.disconnected); // [確認_正常系] - 片方向の送信を失敗させないこと。
}

// 全断時の送信経路が通信種別の契約に従うことの確認
TEST(pathGroupTest, all_groups_unreachable)
{
    // Arrange
    const potr_type types[] = {POTR_TYPE_UNICAST_BIDIR, POTR_TYPE_UNICAST_BIDIR_N1,
        POTR_TYPE_TCP, POTR_TYPE_TCP_BIDIR, POTR_TYPE_UNICAST, POTR_TYPE_MULTICAST, POTR_TYPE_BROADCAST};
    potr_internal_path_group_result results[7] = {};
    // Pre-Assert
    // Act
    for (size_t i = 0; i < 7; i++)
    {
        potr_internal_path_group_input input = {};
        potr_internal_path_group_state state = {};
        input.type = types[i];
        input.interval_ms = 10;
        input.timeout_ms = 100;
        input.report_received = 1;
        input.group[0] = 1;
        input.group[1] = 3;
        input.available[0] = input.available[1] = 1;
        potr_internal_path_group_evaluate(&input, &state, &state, &results[i]);
    }
    // [手順] - 双方向 2 種、TCP 2 種、片方向 3 種で全断を判定する。
    // Assert
    for (size_t i = 0; i < 7; i++)
    {
        unsigned expected_disconnected = 0U;
        unsigned expected_paths = 3U;
        if (i < 4)
        {
            expected_disconnected = 1U;
            expected_paths = 0U;
        }
        EXPECT_EQ(expected_disconnected, results[i].disconnected);
        // [確認_正常系 回数=7] - 双方向と TCP だけを論理切断にすること。
        EXPECT_EQ(expected_paths, results[i].data_paths);
        // [確認_正常系 回数=7] - 片方向と一対多は全経路へ送ること。
    }
}

// RAW と監視無効では経路の優先グループを無視することの確認
TEST(pathGroupTest, raw_and_disabled_health_use_all_available_paths)
{
    // Arrange
    potr_internal_path_group_input input = {};
    potr_internal_path_group_state state = {};
    potr_internal_path_group_result results[5] = {};
    input.group[0] = 1;
    input.group[2] = 4;
    input.available[0] = input.available[2] = 1;
    input.interval_ms = 10;
    input.timeout_ms = 100;
    // Pre-Assert
    // Act
    input.type = POTR_TYPE_UNICAST_RAW;
    potr_internal_path_group_evaluate(&input, &state, &state, &results[0]);
    input.type = POTR_TYPE_MULTICAST_RAW;
    potr_internal_path_group_evaluate(&input, &state, &state, &results[1]);
    input.type = POTR_TYPE_BROADCAST_RAW;
    potr_internal_path_group_evaluate(&input, &state, &state, &results[2]);
    input.type = POTR_TYPE_TCP;
    input.interval_ms = 0;
    potr_internal_path_group_evaluate(&input, &state, &state, &results[3]);
    input.interval_ms = 10;
    input.timeout_ms = 0;
    potr_internal_path_group_evaluate(&input, &state, &state, &results[4]);
    // [手順] - RAW 3 種と監視間隔 0、タイムアウト 0 を判定する。
    // Assert
    for (size_t i = 0; i < 5; i++)
    {
        EXPECT_EQ(5U, results[i].data_paths); // [確認_正常系 回数=5] - 有効な全経路へ送ること。
        EXPECT_EQ(0U, results[i].disconnected); // [確認_正常系 回数=5] - 監視から論理切断を作らないこと。
    }
}

// 上位グループに設定だけがあっても経路がなければ候補にしないことの確認
TEST(pathGroupTest, unavailable_upper_group_is_not_selected)
{
    // Arrange
    potr_internal_path_group_input input = {};
    potr_internal_path_group_state state = {};
    potr_internal_path_group_result selection = {};
    input.type = POTR_TYPE_TCP;
    input.interval_ms = 10;
    input.timeout_ms = 100;
    input.group[0] = 1;
    input.group[1] = 3;
    input.reachable[0] = input.reachable[1] = 1;
    input.available[1] = 1;
    // Pre-Assert
    // Act
    potr_internal_path_group_evaluate(&input, &state, &state, &selection);
    // [手順] - 上位の論理状態が残っていても接続がない入力を判定する。
    // Assert
    EXPECT_EQ(3U, state.selected); // [確認_正常系] - 接続を持つグループを選ぶこと。
    EXPECT_EQ(2U, selection.data_paths); // [確認_正常系] - 利用できる経路だけへ送ること。
    EXPECT_EQ(0U, state.candidate); // [確認_正常系] - 経路のない上位を切り戻し候補にしないこと。
}
