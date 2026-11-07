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
#include <atomic>
#include <chrono>
#include <future>
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

// 同じ TCP health の停止が重なっても、先行する終了待ちが完了するまで後続を待たせることの確認
TEST(potrHealthThreadTest, concurrent_tcp_stop_waits_for_single_join)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int token = 0;
    std::promise<void> join_entered;
    std::promise<void> release_join;
    std::shared_future<void> join_gate = release_join.get_future().share();
    std::promise<void> stop_waiting;
    std::atomic<bool> wait_recorded(false);
    auto join_entered_future = join_entered.get_future();
    auto stop_waiting_future = stop_waiting.get_future();
    delegate_real_cplat_local_lock_create(&ctx.health_mutex[0]);
    delegate_real_cplat_condvar_create(&ctx.health_wakeup[0]);
    ctx.health_thread[0] = reinterpret_cast<cplat_thread *>(&token);
    ON_CALL(mock_cplat, cplat_thread_join(_, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    ON_CALL(mock_cplat, cplat_condvar_wait(ctx.health_wakeup[0], ctx.health_mutex[0], CPLAT_SYNC_WAIT_FOREVER))
        .WillByDefault(
            [&](cplat_condvar *cv, cplat_local_lock *mutex, int timeout_ms)
            {
                if (!wait_recorded.exchange(true))
                {
                    stop_waiting.set_value();
                }
                return delegate_real_cplat_condvar_wait(cv, mutex, timeout_ms);
            }); // [状態] - 後続の停止要求が条件変数で待機する位置を記録し、実際の待機へ委譲する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_thread_join(ctx.health_thread[0], CPLAT_SYNC_WAIT_FOREVER))
        .WillOnce(
            [&](cplat_thread *, int)
            {
                join_entered.set_value();
                join_gate.wait();
                return CPLAT_OK;
            });
    // [Pre-Assert確認_正常系] - 並行する停止要求からの join が合計 1 回であること。
    // [Pre-Assert手順] - join の復帰をテスト本体が許可するまで待たせる。

    // Act
    auto first = std::async(std::launch::async, [&] { return potr_internal_tcp_health_thread_stop(&ctx, 0); });
    // [手順] - 先行する停止要求を開始する。
    auto entered = join_entered_future.wait_for(std::chrono::seconds(2));
    // [手順] - 先行要求が join に入るまで最大 2 秒待つ。
    auto second = std::async(std::launch::async, [&] { return potr_internal_tcp_health_thread_stop(&ctx, 0); });
    // [手順] - 後続の停止要求を開始する。
    auto waiting = stop_waiting_future.wait_for(std::chrono::seconds(2));
    // [手順] - 後続要求が先行要求の完了待ちに入るまで最大 2 秒待つ。
    auto second_status = second.wait_for(std::chrono::seconds(0));
    // [手順] - join 完了前の後続要求の復帰状態を取得する。
    release_join.set_value();
    // [手順] - 先行する join を復帰させる。
    int first_ret = first.get();
    int second_ret = second.get();
    // [手順] - 両方の停止要求の結果を取得する。

    // Assert
    EXPECT_EQ(std::future_status::ready, entered);         // [確認_正常系] - 先行要求が join に入ること。
    EXPECT_EQ(std::future_status::ready, waiting);         // [確認_正常系] - 後続要求が先行要求の完了を待つこと。
    EXPECT_EQ(std::future_status::timeout, second_status); // [確認_正常系] - 回収完了前に後続要求が復帰しないこと。
    EXPECT_EQ(POTR_OK, first_ret);                         // [確認_正常系] - 先行要求が成功すること。
    EXPECT_EQ(POTR_OK, second_ret);                        // [確認_正常系] - 後続要求も成功すること。
    EXPECT_EQ(nullptr, ctx.health_thread[0]);              // [確認_正常系] - 回収後のハンドルが NULL であること。

    // Cleanup
    delegate_real_cplat_condvar_dispose(ctx.health_wakeup[0]);
    delegate_real_cplat_local_lock_dispose(ctx.health_mutex[0]);
}

// TCP health の終了待ち失敗後も、ハンドルを保持して停止を再試行できることの確認
TEST(potrHealthThreadTest, tcp_stop_failure_retains_handle_for_retry)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int token = 0;
    cplat_thread *handle = reinterpret_cast<cplat_thread *>(&token);
    ctx.health_thread[0] = handle;
    delegate_real_cplat_local_lock_create(&ctx.health_mutex[0]);
    delegate_real_cplat_condvar_create(&ctx.health_wakeup[0]);

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_thread_join(handle, CPLAT_SYNC_WAIT_FOREVER))
        .WillOnce(Return(CPLAT_ERR_UNKNOWN))
        .WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_異常系] - 同じハンドルで join を再試行すること。
    // [Pre-Assert手順] - 最初の終了待ちを失敗させ、再試行を成功させる。

    // Act
    int first_ret = potr_internal_tcp_health_thread_stop(&ctx, 0);  // [手順] - 最初の停止を呼ぶ。
    cplat_thread *retained = ctx.health_thread[0];                  // [手順] - 失敗後の所有ハンドルを取得する。
    int second_ret = potr_internal_tcp_health_thread_stop(&ctx, 0); // [手順] - 停止を再試行する。

    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, first_ret);   // [確認_異常系] - 終了待ち失敗を返すこと。
    EXPECT_EQ(handle, retained);              // [確認_異常系] - 失敗時はハンドルを保持すること。
    EXPECT_EQ(POTR_OK, second_ret);           // [確認_正常系] - 再試行が成功すること。
    EXPECT_EQ(nullptr, ctx.health_thread[0]); // [確認_正常系] - 成功後のハンドルが NULL であること。

    // Cleanup
    delegate_real_cplat_condvar_dispose(ctx.health_wakeup[0]);
    delegate_real_cplat_local_lock_dispose(ctx.health_mutex[0]);
}

// close 開始後は TCP health を再起動しないことの確認
TEST(potrHealthThreadTest, tcp_start_rejects_closing_context)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.health_interval_ms = 10;
    cplat_atomic_store_i32(&ctx.close_requested, 1, CPLAT_MEMORY_ORDER_RELEASE);
    delegate_real_cplat_local_lock_create(&ctx.health_mutex[0]);

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_thread_create(_, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 終了処理中にスレッドを生成しないこと。

    // Act
    int ret = potr_internal_tcp_health_thread_start(&ctx, 0); // [手順] - 終了処理中に起動を要求する。

    // Assert
    EXPECT_EQ(POTR_ERR_CANCELED, ret);        // [確認_正常系] - 起動の中止を返すこと。
    EXPECT_EQ(nullptr, ctx.health_thread[0]); // [確認_正常系] - ハンドルを生成しないこと。

    // Cleanup
    delegate_real_cplat_local_lock_dispose(ctx.health_mutex[0]);
}
