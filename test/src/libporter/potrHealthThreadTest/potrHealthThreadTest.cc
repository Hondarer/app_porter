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

// UDP health の lock 生成失敗で生成済み資源を回収することの確認
TEST(potrHealthThreadTest, udp_lock_failure_releases_sync)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.health_interval_ms = 10;
    ON_CALL(mock_cplat, cplat_local_lock_create(_))
        .WillByDefault(Return(CPLAT_ERR_UNKNOWN)); // [状態] - 指定の生成処理を失敗させる。
    // Act
    int actual_ret = potr_internal_health_thread_start(&ctx); // [手順] - UDP health を開始する。
    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret);  // [確認_異常系] - 生成失敗を UNKNOWN に変換すること。
    EXPECT_EQ(nullptr, ctx.health_mutex[0]);  // [確認_異常系] - ロックを残さないこと。
    EXPECT_EQ(nullptr, ctx.health_wakeup[0]); // [確認_異常系] - 条件変数を残さないこと。
    EXPECT_EQ(nullptr, ctx.health_thread[0]); // [確認_異常系] - 未生成のスレッドを所有しないこと。
}

// UDP health の condvar 生成失敗で生成済み資源を回収することの確認
TEST(potrHealthThreadTest, udp_condvar_failure_releases_sync)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.health_interval_ms = 10;
    ON_CALL(mock_cplat, cplat_condvar_create(_))
        .WillByDefault(Return(CPLAT_ERR_UNKNOWN)); // [状態] - 指定の生成処理を失敗させる。
    // Act
    int actual_ret = potr_internal_health_thread_start(&ctx); // [手順] - UDP health を開始する。
    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret);  // [確認_異常系] - 生成失敗を UNKNOWN に変換すること。
    EXPECT_EQ(nullptr, ctx.health_mutex[0]);  // [確認_異常系] - ロックを残さないこと。
    EXPECT_EQ(nullptr, ctx.health_wakeup[0]); // [確認_異常系] - 条件変数を残さないこと。
    EXPECT_EQ(nullptr, ctx.health_thread[0]); // [確認_異常系] - 未生成のスレッドを所有しないこと。
}

// UDP health の thread 生成失敗で生成済み資源を回収することの確認
TEST(potrHealthThreadTest, udp_thread_failure_releases_sync)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.health_interval_ms = 10;
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(Return(CPLAT_ERR_UNKNOWN)); // [状態] - 指定の生成処理を失敗させる。
    // Act
    int actual_ret = potr_internal_health_thread_start(&ctx); // [手順] - UDP health を開始する。
    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret);  // [確認_異常系] - 生成失敗を UNKNOWN に変換すること。
    EXPECT_EQ(nullptr, ctx.health_mutex[0]);  // [確認_異常系] - ロックを残さないこと。
    EXPECT_EQ(nullptr, ctx.health_wakeup[0]); // [確認_異常系] - 条件変数を残さないこと。
    EXPECT_EQ(nullptr, ctx.health_thread[0]); // [確認_異常系] - 未生成のスレッドを所有しないこと。
}

// UDP health の終了待ち後にハンドルを無効化し、二重 join を防ぐことの確認
TEST(potrHealthThreadTest, udp_stop_joins_once)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int token = 0;
    ctx.health_interval_ms = 10;

    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn, void *)
            {
                *out = reinterpret_cast<cplat_thread *>(&token);
                return CPLAT_OK;
            }); // [状態] - スレッド関数を実行せず生成済みハンドルを返す。
    (void)potr_internal_health_thread_start(&ctx);
    cplat_atomic_store_i32(&ctx.health_running[0], 0, CPLAT_MEMORY_ORDER_RELAXED);
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_thread_join(ctx.health_thread[0], CPLAT_SYNC_WAIT_FOREVER))
        .WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - 稼働終了後のハンドルを 1 回だけ join すること。
    // [Pre-Assert手順] - join 成功を返す。
    // Act
    int actual_ret = potr_internal_health_thread_stop(&ctx);       // [手順] - 稼働終了した health の終了を待つ。
    int actual_ret_again = potr_internal_health_thread_stop(&ctx); // [手順] - 二度目の停止を呼ぶ。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);           // [確認_正常系] - 停止に成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_again);     // [確認_正常系] - 未生成状態の停止が成功すること。
    EXPECT_EQ(nullptr, ctx.health_thread[0]); // [確認_正常系] - join 後のハンドルを NULL にすること。
}

// TCP health の終了待ち後にハンドルを無効化し、二重 join を防ぐことの確認
TEST(potrHealthThreadTest, tcp_stop_joins_once)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int token = 0;
    ctx.health_interval_ms = 10;
    delegate_real_cplat_local_lock_create(&ctx.health_mutex[1]);
    delegate_real_cplat_condvar_create(&ctx.health_wakeup[1]);
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn, void *)
            {
                *out = reinterpret_cast<cplat_thread *>(&token);
                return CPLAT_OK;
            }); // [状態] - スレッド関数を実行せず生成済みハンドルを返す。
    (void)potr_internal_tcp_health_thread_start(&ctx, 1);
    cplat_atomic_store_i32(&ctx.health_running[1], 0, CPLAT_MEMORY_ORDER_RELAXED);
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_thread_join(ctx.health_thread[1], CPLAT_SYNC_WAIT_FOREVER))
        .WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - 稼働終了後のハンドルを 1 回だけ join すること。
    // [Pre-Assert手順] - join 成功を返す。
    // Act
    int actual_ret = potr_internal_tcp_health_thread_stop(&ctx, 1);       // [手順] - 稼働終了した health の終了を待つ。
    int actual_ret_again = potr_internal_tcp_health_thread_stop(&ctx, 1); // [手順] - 二度目の停止を呼ぶ。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);           // [確認_正常系] - 停止に成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_again);     // [確認_正常系] - 未生成状態の停止が成功すること。
    EXPECT_EQ(nullptr, ctx.health_thread[1]); // [確認_正常系] - join 後のハンドルを NULL にすること。
    delegate_real_cplat_condvar_dispose(ctx.health_wakeup[1]);
    delegate_real_cplat_local_lock_dispose(ctx.health_mutex[1]);
}

// health 無効時に同期資源やスレッドを生成しないことの確認
TEST(potrHealthThreadTest, disabled_health_does_not_allocate)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_local_lock_create(_)).Times(0);
    // [Pre-Assert確認_正常系] - 無効時にロックを生成しないこと。
    EXPECT_CALL(mock_cplat, cplat_thread_create(_, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 無効時にスレッドを生成しないこと。
    // Act
    int actual_ret = potr_internal_health_thread_start(&ctx);     // [手順] - 監視無効の UDP health を開始する。
    int actual_ret_stop = potr_internal_health_thread_stop(&ctx); // [手順] - 未起動の health を停止する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);      // [確認_正常系] - 無効時の開始が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_stop); // [確認_正常系] - 未起動の停止が成功すること。
}
