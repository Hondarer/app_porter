#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <mock_string.h>
#include <porter/porter_result.h>
#include <algorithm>
#include <cstring>
#include <vector>

using namespace testing;

// send の lock 生成失敗時にロックと稼働状態を戻すことの確認
TEST(potrSendThreadTest, lock_failure_rolls_back)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ON_CALL(mock_cplat, cplat_local_lock_create(_))
        .WillByDefault(Return(CPLAT_ERR_UNKNOWN)); // [状態] - 生成処理を失敗させる。
    // Act
    int actual_ret = potr_internal_send_thread_start(&ctx); // [手順] - send を開始する。
    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret);   // [確認_異常系] - 生成失敗を返すこと。
    EXPECT_EQ(nullptr, ctx.send_window_mutex); // [確認_異常系] - ロックを残さないこと。
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.send_thread_running, CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_異常系] - 稼働状態を 0 に戻すこと。
}

// send の thread 生成失敗時にロックと稼働状態を戻すことの確認
TEST(potrSendThreadTest, thread_failure_rolls_back)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(Return(CPLAT_ERR_UNKNOWN)); // [状態] - 生成処理を失敗させる。
    // Act
    int actual_ret = potr_internal_send_thread_start(&ctx); // [手順] - send を開始する。
    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret);   // [確認_異常系] - 生成失敗を返すこと。
    EXPECT_EQ(nullptr, ctx.send_window_mutex); // [確認_異常系] - ロックを残さないこと。
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.send_thread_running, CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_異常系] - 稼働状態を 0 に戻すこと。
}

// send の稼働終了後も一度だけ join してロックを破棄することの確認
TEST(potrSendThreadTest, stop_joins_finished_thread_once)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int token = 0;
    ctx.send_thread = reinterpret_cast<cplat_thread *>(&token);
    delegate_real_cplat_local_lock_create(&ctx.send_window_mutex);
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_thread_join(ctx.send_thread, CPLAT_SYNC_WAIT_FOREVER)).WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - 生成済み send を 1 回だけ join すること。
    // [Pre-Assert手順] - join 成功を返す。
    // Act
    potr_internal_send_thread_stop(&ctx); // [手順] - send を停止する。
    potr_internal_send_thread_stop(&ctx); // [手順] - 停止済み send を再度停止する。
    // Assert
    EXPECT_EQ(nullptr, ctx.send_thread);       // [確認_正常系] - ハンドルを NULL にすること。
    EXPECT_EQ(nullptr, ctx.send_window_mutex); // [確認_正常系] - ロックを NULL にすること。
}
