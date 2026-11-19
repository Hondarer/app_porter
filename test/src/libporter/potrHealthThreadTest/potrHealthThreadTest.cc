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
    EXPECT_CALL(mock_cplat, cplat_thread_join(ctx.health_thread[0])).WillOnce(Return());
    // [Pre-Assert確認_正常系] - 稼働終了後のハンドルを 1 回だけ join すること。
    // [Pre-Assert手順] - join 成功を返す。
    // Act
    potr_internal_health_thread_stop(&ctx); // [手順] - 稼働終了した health の終了を待つ。
    potr_internal_health_thread_stop(&ctx); // [手順] - 二度目の停止を呼ぶ。
    // Assert
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
    EXPECT_CALL(mock_cplat, cplat_thread_join(ctx.health_thread[1])).WillOnce(Return());
    // [Pre-Assert確認_正常系] - 稼働終了後のハンドルを 1 回だけ join すること。
    // [Pre-Assert手順] - join 成功を返す。
    // Act
    potr_internal_tcp_health_thread_stop(&ctx, 1); // [手順] - 稼働終了した health の終了を待つ。
    potr_internal_tcp_health_thread_stop(&ctx, 1); // [手順] - 二度目の停止を呼ぶ。
    // Assert
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
    EXPECT_CALL(mock_cplat, cplat_thread_join(_)).Times(0);
    // [Pre-Assert確認_正常系] - 未起動の停止で join しないこと。
    // Act
    int actual_ret = potr_internal_health_thread_start(&ctx); // [手順] - 監視無効の UDP health を開始する。
    potr_internal_health_thread_stop(&ctx);                   // [手順] - 未起動の health を停止する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);           // [確認_正常系] - 無効時の開始が成功すること。
    EXPECT_EQ(nullptr, ctx.health_thread[0]); // [確認_正常系] - 未起動の状態のままであること。
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
    ON_CALL(mock_cplat, cplat_thread_join(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_condvar_wait(ctx.health_wakeup[0], ctx.health_mutex[0]))
        .WillByDefault(
            [&](cplat_condvar *cv, cplat_local_lock *mutex)
            {
                if (!wait_recorded.exchange(true))
                {
                    stop_waiting.set_value();
                }
                delegate_real_cplat_condvar_wait(cv, mutex);
            }); // [状態] - 後続の停止要求が条件変数で待機する位置を記録し、実際の待機へ委譲する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_thread_join(ctx.health_thread[0]))
        .WillOnce(
            [&](cplat_thread *)
            {
                join_entered.set_value();
                join_gate.wait();
            });
    // [Pre-Assert確認_正常系] - 並行する停止要求からの join が合計 1 回であること。
    // [Pre-Assert手順] - join の復帰をテスト本体が許可するまで待たせる。

    // Act
    auto first = std::async(std::launch::async, [&] { potr_internal_tcp_health_thread_stop(&ctx, 0); });
    // [手順] - 先行する停止要求を開始する。
    auto entered = join_entered_future.wait_for(std::chrono::seconds(2));
    // [手順] - 先行要求が join に入るまで最大 2 秒待つ。
    auto second = std::async(std::launch::async, [&] { potr_internal_tcp_health_thread_stop(&ctx, 0); });
    // [手順] - 後続の停止要求を開始する。
    auto waiting = stop_waiting_future.wait_for(std::chrono::seconds(2));
    // [手順] - 後続要求が先行要求の完了待ちに入るまで最大 2 秒待つ。
    auto second_status = second.wait_for(std::chrono::seconds(0));
    // [手順] - join 完了前の後続要求の復帰状態を取得する。
    release_join.set_value();
    // [手順] - 先行する join を復帰させる。
    first.get();
    second.get();
    // [手順] - 両方の停止要求の復帰を待つ。

    // Assert
    EXPECT_EQ(std::future_status::ready, entered);         // [確認_正常系] - 先行要求が join に入ること。
    EXPECT_EQ(std::future_status::ready, waiting);         // [確認_正常系] - 後続要求が先行要求の完了を待つこと。
    EXPECT_EQ(std::future_status::timeout, second_status); // [確認_正常系] - 回収完了前に後続要求が復帰しないこと。
    EXPECT_EQ(nullptr, ctx.health_thread[0]);              // [確認_正常系] - 回収後のハンドルが NULL であること。

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

// TCP の定周期の PING の送信に失敗した場合に、WARNING を出してスレッドを続けることの確認
TEST(potrHealthThreadTest, tcp_ping_send_failure_traces_warning)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_TCP;
    ctx.service.service_id = 11;
    ctx.health_interval_ms = 10;
    ctx.tcp_conn_fd[0] = static_cast<cplat_socket>(7);
    cplat_atomic_store_i32(&ctx.tcp_active_paths, 1, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_thread_fn entry = nullptr;
    void *entry_arg = nullptr;
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn fn, void *arg)
            {
                *out = nullptr;
                entry = fn;
                entry_arg = arg;
                return CPLAT_OK;
            }); // [状態] - cplat_thread_create() から、スレッド本体を捕捉して CPLAT_OK を返すように既定動作を設定する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_))
        .WillByDefault(Return()); // [状態] - cplat_local_lock_lock() が何もしないように既定動作を設定する。
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_))
        .WillByDefault(Return()); // [状態] - cplat_local_lock_unlock() が何もしないように既定動作を設定する。
    ON_CALL(mock_cplat, cplat_condvar_timed_wait(_, _, _))
        .WillByDefault(Return(CPLAT_ERR_TIMEOUT)); // [状態] - cplat_condvar_timed_wait() から CPLAT_ERR_TIMEOUT を返すように既定動作を設定する。
    ON_CALL(mock_porter, potr_internal_result_from_error(_))
        .WillByDefault(Return(POTR_ERR_IO)); // [状態] - potr_internal_result_from_error() から POTR_ERR_IO を返すように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_send_all(static_cast<cplat_socket>(7), _, _, _))
        .WillOnce(
            [&](cplat_socket, const void *, size_t, cplat_error *)
            {
                cplat_atomic_store_i32(&ctx.health_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_ERR_UNKNOWN;
            }); // [Pre-Assert確認_異常系] - cplat_socket_send_all() が 7 を指定して 1 回呼び出されること。
                // [Pre-Assert手順] - ctx.health_running[0] を 0 にし、cplat_socket_send_all() から CPLAT_ERR_UNKNOWN を返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _))
        .Times(AnyNumber()); // [Pre-Assert確認_正常系] - cplat_tracer_writef_at() が任意の回数呼び出されること。
    EXPECT_CALL(mock_cplat,
                cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_WARNING, nullptr, HasSubstr("PING send failed (result=")))
        .Times(1); // [Pre-Assert確認_異常系] - cplat_tracer_writef_at() が CPLAT_TRACE_LEVEL_WARNING、nullptr、"PING send failed (result=" を含む文字列を指定して 1 回呼び出されること。

    // Act
    int actual_ret = potr_internal_tcp_health_thread_start(&ctx, 0); // [手順] - potr_internal_tcp_health_thread_start() を呼び出す。
    ASSERT_NE(nullptr, entry);                                        // [状態確認] - スレッド本体を捕捉できたこと。
    entry(entry_arg);                                                 // [手順] - スレッド本体を同期で実行する。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - potr_internal_tcp_health_thread_start() の戻り値が POTR_OK であること。
}

// 終了の処理中 (POTR_ERR_CANCELED) の PING の送信では WARNING を出さないことの確認
TEST(potrHealthThreadTest, tcp_ping_canceled_does_not_trace_warning)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_TCP;
    ctx.service.service_id = 11;
    ctx.health_interval_ms = 10;
    ctx.tcp_conn_fd[0] = static_cast<cplat_socket>(7);
    cplat_atomic_store_i32(&ctx.tcp_active_paths, 1, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_thread_fn entry = nullptr;
    void *entry_arg = nullptr;
    int waits = 0;
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn fn, void *arg)
            {
                *out = nullptr;
                entry = fn;
                entry_arg = arg;
                return CPLAT_OK;
            }); // [状態] - cplat_thread_create() から、スレッド本体を捕捉して CPLAT_OK を返すように既定動作を設定する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_))
        .WillByDefault(Return()); // [状態] - cplat_local_lock_lock() が何もしないように既定動作を設定する。
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_))
        .WillByDefault(Return()); // [状態] - cplat_local_lock_unlock() が何もしないように既定動作を設定する。
    ON_CALL(mock_cplat, cplat_condvar_timed_wait(_, _, _))
        .WillByDefault(
            [&](cplat_condvar *, cplat_local_lock *, int)
            {
                if (++waits == 2)
                {
                    cplat_atomic_store_i32(&ctx.health_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                }
                return CPLAT_ERR_TIMEOUT;
            }); // [状態] - cplat_condvar_timed_wait() から CPLAT_ERR_TIMEOUT を返し、2 回目の待機で ctx.health_running[0] を 0 にするように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_send_all(_, _, _, _))
        .Times(0); // [Pre-Assert確認_正常系] - cplat_socket_send_all() が呼び出されないこと。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _))
        .Times(AnyNumber()); // [Pre-Assert確認_正常系] - cplat_tracer_writef_at() が任意の回数呼び出されること。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_WARNING, _, _))
        .Times(0); // [Pre-Assert確認_正常系] - CPLAT_TRACE_LEVEL_WARNING を指定した cplat_tracer_writef_at() の呼び出しがないこと。

    // Act
    int actual_ret = potr_internal_tcp_health_thread_start(&ctx, 0); // [手順] - potr_internal_tcp_health_thread_start() を呼び出す。
    ASSERT_NE(nullptr, entry);                                        // [状態確認] - スレッド本体を捕捉できたこと。
    cplat_atomic_store_i32(&ctx.close_requested, 1, CPLAT_MEMORY_ORDER_RELEASE); // [手順] - 終了の処理中の状態にする。
    entry(entry_arg);                                                            // [手順] - スレッド本体を同期で実行する。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - potr_internal_tcp_health_thread_start() の戻り値が POTR_OK であること。
    EXPECT_EQ(2, waits);            // [確認_正常系] - 2 回待機したこと。(PING の送信を 1 回試みた後に終えたことの確認)
}

// TCP の経路で待機に失敗した場合に、ERROR を出してスレッドを続けることの確認
TEST(potrHealthThreadTest, tcp_wait_failure_traces_error)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_TCP;
    ctx.service.service_id = 11;
    ctx.health_interval_ms = 10;
    ctx.tcp_conn_fd[0] = static_cast<cplat_socket>(7);
    cplat_atomic_store_i32(&ctx.tcp_active_paths, 1, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_thread_fn entry = nullptr;
    void *entry_arg = nullptr;
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn fn, void *arg)
            {
                *out = nullptr;
                entry = fn;
                entry_arg = arg;
                return CPLAT_OK;
            }); // [状態] - cplat_thread_create() から、スレッド本体を捕捉して CPLAT_OK を返すように既定動作を設定する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_))
        .WillByDefault(Return()); // [状態] - cplat_local_lock_lock() が何もしないように既定動作を設定する。
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_))
        .WillByDefault(Return()); // [状態] - cplat_local_lock_unlock() が何もしないように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_condvar_timed_wait(_, _, 10))
        .WillOnce(
            [&](cplat_condvar *, cplat_local_lock *, int)
            {
                cplat_atomic_store_i32(&ctx.health_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_ERR_UNKNOWN;
            }); // [Pre-Assert確認_異常系] - cplat_condvar_timed_wait() が 10 を指定して 1 回呼び出されること。
                // [Pre-Assert手順] - ctx.health_running[0] を 0 にし、cplat_condvar_timed_wait() から CPLAT_ERR_UNKNOWN を返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _))
        .Times(AnyNumber()); // [Pre-Assert確認_正常系] - cplat_tracer_writef_at() が任意の回数呼び出されること。
    EXPECT_CALL(mock_cplat,
                cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_ERROR, nullptr, HasSubstr("wait failed (result=")))
        .Times(1); // [Pre-Assert確認_異常系] - cplat_tracer_writef_at() が CPLAT_TRACE_LEVEL_ERROR、nullptr、"wait failed (result=" を含む文字列を指定して 1 回呼び出されること。

    // Act
    int actual_ret = potr_internal_tcp_health_thread_start(&ctx, 0); // [手順] - potr_internal_tcp_health_thread_start() を呼び出す。
    ASSERT_NE(nullptr, entry);                                        // [状態確認] - スレッド本体を捕捉できたこと。
    entry(entry_arg);                                                 // [手順] - スレッド本体を同期で実行する。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - potr_internal_tcp_health_thread_start() の戻り値が POTR_OK であること。
}

// 片方向 UDP の PING の期限の待機に失敗した場合に、ERROR を出してスレッドを続けることの確認
TEST(potrHealthThreadTest, oneway_udp_wait_failure_traces_error)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.service.service_id = 11;
    ctx.role = POTR_ROLE_SENDER;
    ctx.n_path = 1;
    ctx.health_interval_ms = 1000;
    cplat_thread_fn entry = nullptr;
    void *entry_arg = nullptr;
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn fn, void *arg)
            {
                *out = nullptr;
                entry = fn;
                entry_arg = arg;
                return CPLAT_OK;
            }); // [状態] - cplat_thread_create() から、スレッド本体を捕捉して CPLAT_OK を返すように既定動作を設定する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_))
        .WillByDefault(Return()); // [状態] - cplat_local_lock_lock() が何もしないように既定動作を設定する。
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_))
        .WillByDefault(Return()); // [状態] - cplat_local_lock_unlock() が何もしないように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_condvar_timed_wait(_, _, _))
        .WillOnce(
            [&](cplat_condvar *, cplat_local_lock *, int)
            {
                cplat_atomic_store_i32(&ctx.health_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_ERR_UNKNOWN;
            }); // [Pre-Assert確認_異常系] - cplat_condvar_timed_wait() が 1 回呼び出されること。
                // [Pre-Assert手順] - ctx.health_running[0] を 0 にし、cplat_condvar_timed_wait() から CPLAT_ERR_UNKNOWN を返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _))
        .Times(AnyNumber()); // [Pre-Assert確認_正常系] - cplat_tracer_writef_at() が任意の回数呼び出されること。
    EXPECT_CALL(mock_cplat,
                cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_ERROR, nullptr, HasSubstr("wait failed (result=")))
        .Times(1); // [Pre-Assert確認_異常系] - cplat_tracer_writef_at() が CPLAT_TRACE_LEVEL_ERROR、nullptr、"wait failed (result=" を含む文字列を指定して 1 回呼び出されること。
    EXPECT_CALL(mock_cplat, cplat_socket_sendto(_, _, _, _, _, _))
        .Times(0); // [Pre-Assert確認_正常系] - cplat_socket_sendto() が呼び出されないこと。(期限の前に PING を送らないことの確認)

    // Act
    int actual_ret = potr_internal_health_thread_start(&ctx); // [手順] - potr_internal_health_thread_start() を呼び出す。
    ASSERT_NE(nullptr, entry);                                 // [状態確認] - スレッド本体を捕捉できたこと。
    entry(entry_arg);                                          // [手順] - スレッド本体を同期で実行する。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - potr_internal_health_thread_start() の戻り値が POTR_OK であること。

    // Cleanup
    ON_CALL(mock_cplat, cplat_thread_join(_))
        .WillByDefault(Return()); // [状態] - cplat_thread_join() が何もしないように既定動作を設定する。
    ctx.health_thread[0] = reinterpret_cast<cplat_thread *>(&entry); // [状態] - 生成済みのスレッドとして停止できるようにハンドルを設定する。
    potr_internal_health_thread_stop(&ctx);
}