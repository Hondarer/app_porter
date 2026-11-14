/**
 *******************************************************************************
 *  @file           pathGroupContextTest.cc
 *  @brief          既存経路状態とピア別グループ選択の接続を検証します。
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
using namespace testing;

// N:1 の選択状態と切り替えがピアごとに独立していることの確認
TEST(pathGroupContextTest, n1_peers_select_independently)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context peers[2] = {};
    potr_internal_path_group_result first = {};
    potr_internal_path_group_result second = {};
    potr_internal_path_group_result switched = {};
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.service.path_group[0] = 1;
    ctx.service.path_group[1] = 3;
    ctx.health_interval_ms = 10;
    ctx.health_timeout_ms = 100;
    ctx.n_path = 2;
    ctx.sock[0] = static_cast<cplat_socket>(7);
    ctx.sock[1] = static_cast<cplat_socket>(8);
    peers[0].peer_id = 1;
    peers[1].peer_id = 2;
    for (size_t i = 0; i < 2; i++)
    {
        peers[i].dest_addr[0].address = peers[i].dest_addr[1].address = 1;
        peers[i].dest_addr[0].port = peers[i].dest_addr[1].port = 1;
    }
    peers[0].path_logical_alive[0] = 1;
    peers[1].path_logical_alive[1] = 1;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_))
        .WillByDefault(DoAll(SetArgPointee<0>(1000), Return(CPLAT_OK)));
    ON_CALL(mock_cplat, cplat_local_lock_lock(_, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 2 ピアの異なる論理接続状態を用意し、単一スレッドで判定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_callback_emit(_, _, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - グループ選択で公開イベントを通知しないこと。
    // Act
    int actual_ret_first = potr_internal_path_group_select(&ctx, &peers[0], &first);
    int actual_ret_second = potr_internal_path_group_select(&ctx, &peers[1], &second);
    peers[0].path_logical_alive[0] = 0;
    peers[0].path_logical_alive[1] = 1;
    int actual_ret_switch = potr_internal_path_group_select(&ctx, &peers[0], &switched);
    // [手順] - 各ピアを選択し、1 者目だけを下位グループへ切り替える。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_first); // [確認_正常系] - 1 者目を選択できること。
    EXPECT_EQ(POTR_OK, actual_ret_second); // [確認_正常系] - 2 者目を選択できること。
    EXPECT_EQ(POTR_OK, actual_ret_switch); // [確認_正常系] - 1 者目を切り替えられること。
    EXPECT_EQ(1U, first.data_paths); // [確認_正常系] - 1 者目は経路 0 を使うこと。
    EXPECT_EQ(2U, second.data_paths); // [確認_正常系] - 2 者目は経路 1 を使うこと。
    EXPECT_EQ(1U, switched.changed); // [確認_正常系] - 1 者目の変更を記録すること。
    EXPECT_EQ(3U, peers[1].path_group_state.selected); // [確認_正常系] - 2 者目の選択状態を維持すること。
    EXPECT_EQ(0U, ctx.path_group_state.selected); // [確認_正常系] - サービス共通の選択状態を使わないこと。
}
