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

// NACK 重複抑制リング バッファーが POTR_NACK_DEDUP_SLOTS 件の ack_num を保持することの確認
TEST(threadRecvWindowTest, nack_dedup_retains_slot_count_of_potr_nack_dedup_slots)
{
    // Arrange
    NiceMock<Mock_cplat> mock;
    potr_context ctx = {};
    thread_recv_slot slot = {};
    slot.ctx = &ctx;
    slot.nack_dedup_buf = ctx.nack_dedup_buf;
    slot.nack_dedup_next = &ctx.nack_dedup_next;
    ON_CALL(mock, cplat_clock_get_monotonic_ms(_)).WillByDefault(DoAll(SetArgPointee<0>(1000), Return(CPLAT_OK)));
    // [状態] - 単調時刻が常に同じ値を返すようにする。
    for (uint32_t ack_num = 1U; ack_num <= POTR_NACK_DEDUP_SLOTS; ack_num++)
    {
        ASSERT_EQ(0, test_nack_is_duplicate(&slot, ack_num));
        // [状態確認] - 異なる ack_num を 1 から POTR_NACK_DEDUP_SLOTS まで登録し、重複と判定しないこと。
    }

    // Act & Assert
    for (uint32_t ack_num = 1U; ack_num <= POTR_NACK_DEDUP_SLOTS; ack_num++)
    {
        EXPECT_EQ(1, test_nack_is_duplicate(&slot, ack_num)) << "ack_num=" << ack_num;
        // [手順] - 登録済みの ack_num を先頭から末尾まで再入力する。
        // [確認_正常系] - POTR_NACK_DEDUP_SLOTS 件すべてを重複として抑制すること。
    }
}

// POTR_NACK_DEDUP_SLOTS 件を超えて登録すると最も古い ack_num だけが押し出されることの確認
TEST(threadRecvWindowTest, nack_dedup_evicts_oldest_after_potr_nack_dedup_slots)
{
    // Arrange
    NiceMock<Mock_cplat> mock;
    potr_context ctx = {};
    thread_recv_slot slot = {};
    slot.ctx = &ctx;
    slot.nack_dedup_buf = ctx.nack_dedup_buf;
    slot.nack_dedup_next = &ctx.nack_dedup_next;
    ON_CALL(mock, cplat_clock_get_monotonic_ms(_)).WillByDefault(DoAll(SetArgPointee<0>(1000), Return(CPLAT_OK)));
    // [状態] - 単調時刻が常に同じ値を返すようにする。
    for (uint32_t ack_num = 1U; ack_num <= POTR_NACK_DEDUP_SLOTS + 1U; ack_num++)
    {
        ASSERT_EQ(0, test_nack_is_duplicate(&slot, ack_num));
        // [状態確認] - 異なる ack_num を 1 から POTR_NACK_DEDUP_SLOTS + 1 まで登録し、重複と判定しないこと。
    }

    // Act
    // 重複と判定しない入力はリング バッファーへ記録されるため、2 番目に古い ack_num を先に確かめる。
    int actual_second = test_nack_is_duplicate(&slot, 2U); // [手順] - 2 番目に古い ack_num を再入力する。
    int actual_oldest = test_nack_is_duplicate(&slot, 1U); // [手順] - 最も古い ack_num を再入力する。

    // Assert
    EXPECT_EQ(0, actual_oldest); // [確認_正常系] - 最も古い ack_num は押し出され、重複と判定しないこと。
    EXPECT_EQ(1, actual_second); // [確認_正常系] - 2 番目に古い ack_num は、重複として抑制すること。
}
