#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include "thread_recv_window.inject.h"

// 期限を計算できない場合に待機せず欠番処理へ進むことの確認
TEST(threadRecvWindowTest, arithmetic_failure_clears_pending_and_handles_gap_immediately)
{
    // Arrange
    NiceMock<Mock_cplat> mock;
    potr_context ctx = {};
    ctx.global.reorder_timeout_ms = 100;
    thread_recv_slot slot = {};
    slot.ctx = &ctx;
    int pending = 1;
    uint32_t nack = 10;
    cplat_timespec deadline = {42, 43};
    slot.reorder_pending = &pending;
    slot.reorder_nack_num = &nack;
    slot.reorder_deadline_ts = &deadline; // [状態] - 別の欠番の待機状態を用意する。

    // Pre-Assert
    EXPECT_CALL(mock, cplat_timespec_add_ms(_, _, _)).WillOnce(Return(CPLAT_ERR_OUT_OF_RANGE));
    // [Pre-Assert確認_異常系] - 新しい欠番の期限を 1 回計算すること。
    // [Pre-Assert手順] - 表現範囲外を返す。

    // Act
    int actual = test_reorder_gap_ready(&slot, 11); // [手順] - 欠番が変わった際の期限計算失敗を処理する。

    // Assert
    EXPECT_EQ(1, actual);  // [確認_異常系] - 即時処理を指示すること。
    EXPECT_EQ(0, pending); // [確認_異常系] - 待機を解除すること。
    EXPECT_EQ(42, deadline.tv_sec);
    EXPECT_EQ(43, deadline.tv_nsec);
}
