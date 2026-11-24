#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <porter/porter_result.h>
#include <porter/potr_context.h>
#include <porter/potr_handle.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <future>

using namespace testing;

// 受付済み参照を保持し、close の受付停止後の操作と重複 close を決定的に検証する。
TEST(potrHandleTest, close_waits_for_reference_and_rejects_late_entries)
{
    // Arrange
    potr_context ctx = {};
    ASSERT_EQ(POTR_OK, potr_internal_handle_prepare(&ctx));
    // [状態確認] - potr_internal_handle_prepare() が成功すること。
    potr_context *handle = potr_internal_handle_public(&ctx);
    potr_context *active = nullptr;
    ASSERT_EQ(POTR_OK, potr_internal_handle_acquire(handle, &active));
    // [状態確認] - 先行 API の使用中参照を確保できること。
    std::atomic<int> close_ret(POTR_ERR_UNKNOWN);
    std::atomic<bool> finished(false);
    int late_ret = POTR_OK;
    potr_context *later = nullptr;
    // Pre-Assert
    // Act
    std::thread closer(
        [&]
        {
            potr_context *closing = nullptr;
            close_ret.store(potr_internal_handle_begin_close(handle, &closing));
            finished.store(true);
        }); // [手順] - 別スレッドから close の受付を停止し、参照返却を待つ。
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do
    {
        late_ret = potr_internal_handle_acquire(handle, &later);
        if (late_ret == POTR_OK)
        {
            potr_internal_handle_release(later);
        }
        std::this_thread::yield();
    } while (late_ret == POTR_OK && std::chrono::steady_clock::now() < deadline);
    bool waited = !finished.load();
    int duplicate_ret = POTR_ERR_UNKNOWN;
    if (late_ret == POTR_ERR_CANCELED)
    {
        duplicate_ret = potr_internal_handle_begin_close(handle, &later);
    }
    potr_internal_handle_release(active); // [手順] - 先行 API の参照を返して close を進める。
    closer.join();
    potr_internal_handle_end_close(&ctx); // [手順] - 資源回収済みとしてハンドルを無効化する。
    int stale_ret = potr_internal_handle_acquire(handle, &later);
    // [手順] - close 完了後の古いハンドルを使用する。
    // Assert
    EXPECT_EQ(POTR_ERR_CANCELED, late_ret);        // [確認_異常系] - 受付停止後の API を拒否すること。
    EXPECT_TRUE(waited);                           // [確認_正常系] - 先行参照を返す前に close が完了しないこと。
    EXPECT_EQ(POTR_ERR_CANCELED, duplicate_ret);   // [確認_異常系] - 進行中の重複 close を拒否すること。
    EXPECT_EQ(POTR_OK, close_ret.load());          // [確認_正常系] - 参照返却後に close が進むこと。
    EXPECT_EQ(POTR_ERR_INVALID_HANDLE, stale_ret); // [確認_異常系] - 古いハンドルを識別すること。
    // Cleanup
    potr_internal_handle_discard(&ctx);
}

// 終了済みの公開値を再利用せず、終了後の close を無効ハンドルとして拒否することの確認
TEST(potrHandleTest, closed_value_is_not_reused)
{
    // Arrange
    potr_context first = {};
    potr_context second = {};
    ASSERT_EQ(POTR_OK, potr_internal_handle_prepare(&first));
    // [状態確認] - 最初のハンドルを準備できること。
    potr_context *old_handle = potr_internal_handle_public(&first);
    potr_context *resolved = nullptr;
    // Pre-Assert
    // Act
    int initial = potr_internal_handle_begin_close(old_handle, &resolved);
    // [手順] - 最初の終了処理を開始する。
    potr_internal_handle_end_close(&first);
    // [手順] - 資源回収済みとして終了を記録する。
    int closed_again = potr_internal_handle_begin_close(old_handle, &resolved);
    // [手順] - 終了後に close を再度要求する。
    int prepared = potr_internal_handle_prepare(&second);
    int stale = potr_internal_handle_acquire(old_handle, &resolved);
    // [手順] - 新しいハンドルを作成した後に古い値を使用する。
    // Assert
    EXPECT_EQ(POTR_OK, initial); // [確認_正常系] - 最初の終了処理を受け付けること。
    EXPECT_EQ(POTR_ERR_INVALID_HANDLE,
              closed_again);      // [確認_異常系] - 終了後の close を無効ハンドルとして拒否すること。
    EXPECT_EQ(POTR_OK, prepared); // [確認_正常系] - 新しいハンドルを準備できること。
    EXPECT_NE(old_handle, potr_internal_handle_public(&second)); // [確認_正常系] - 終了済みの公開値を再利用しないこと。
    EXPECT_EQ(POTR_ERR_INVALID_HANDLE, stale); // [確認_異常系] - 古い値から新しいコンテキストへ到達しないこと。
    // Cleanup
    potr_internal_handle_discard(&second);
    potr_internal_handle_discard(&first);
}

// 通知継続不能時に通常操作を拒否し、close だけを受け付けることの確認
TEST(potrHandleTest, fatal_error_rejects_normal_operation_but_allows_close)
{
    // Arrange
    potr_context ctx = {};
    ASSERT_EQ(POTR_OK, potr_internal_handle_prepare(&ctx));
    // [状態確認] - ハンドルを準備できること。
    potr_context *handle = potr_internal_handle_public(&ctx);
    potr_context *resolved = nullptr;
    cplat_atomic_store_i32(&ctx.fatal_error, POTR_ERR_OUT_OF_MEMORY, CPLAT_MEMORY_ORDER_RELEASE);
    // Pre-Assert
    // Act
    int actual_ret_normal = potr_internal_handle_acquire(handle, &resolved);
    int actual_ret_close = potr_internal_handle_begin_close(handle, &resolved);
    // [手順] - 異常状態で通常操作と close を要求する。
    // Assert
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, actual_ret_normal);
    // [確認_異常系] - 通常操作を通知継続不能の原因で拒否すること。
    EXPECT_EQ(POTR_OK, actual_ret_close);
    // [確認_正常系] - close による資源回収を受け付けること。
    // Cleanup
    potr_internal_handle_end_close(&ctx);
    potr_internal_handle_discard(&ctx);
}

// TCP で受付済み API が参照を保持したまま close 期限を超過した場合に、キュー中断と全経路の閉じる要求を発行してから参照回収を待つことの確認
TEST(potrHandleTest, tcp_close_deadline_timeout_aborts_queue_and_requests_close_all)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_TCP;
    ctx.global.tcp_close_timeout_ms = 50U;
    ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&ctx.send_queue.mutex));
    ASSERT_EQ(CPLAT_OK, cplat_condvar_create(&ctx.send_queue.not_empty));
    ASSERT_EQ(CPLAT_OK, cplat_condvar_create(&ctx.send_queue.not_full));
    ASSERT_EQ(CPLAT_OK, cplat_condvar_create(&ctx.send_queue.progress));
    ASSERT_EQ(CPLAT_OK, cplat_condvar_create(&ctx.send_queue.drained));
    ASSERT_EQ(POTR_OK, potr_internal_handle_prepare(&ctx));
    potr_context *handle = potr_internal_handle_public(&ctx);
    potr_context *active = nullptr;
    ASSERT_EQ(POTR_OK, potr_internal_handle_acquire(handle, &active));

    std::promise<void> abort_called;
    ON_CALL(mock_porter, potr_internal_tcp_close_deadline_publish(&ctx))
        .WillByDefault([](potr_context *c) {
            cplat_atomic_store_u64(&c->tcp_close_deadline_ms, 1050U, CPLAT_MEMORY_ORDER_RELEASE);
            cplat_atomic_store_i32(&c->tcp_close_deadline_active, 1, CPLAT_MEMORY_ORDER_RELEASE);
        });
    ON_CALL(mock_porter, potr_internal_tcp_close_remaining_ms(&ctx, _, _))
        .WillByDefault([](const potr_context *, int *has_out, int *rem_out) {
            if (has_out != nullptr)
            {
                *has_out = 1;
            }
            if (rem_out != nullptr)
            {
                *rem_out = 0;
            }
            return POTR_ERR_TIMEOUT;
        });

    // Pre-Assert
    {
        InSequence seq;
        EXPECT_CALL(mock_porter, potr_internal_tcp_close_deadline_publish(&ctx)).Times(1);
        EXPECT_CALL(mock_porter, potr_internal_tcp_conn_request_close_all(&ctx))
            .WillOnce([&](potr_context *) { abort_called.set_value(); });
    }

    // Act
    int close_ret = POTR_ERR_UNKNOWN;
    std::thread closer([&] {
        potr_context *closing = nullptr;
        close_ret = potr_internal_handle_begin_close(handle, &closing);
    });
    ASSERT_EQ(std::future_status::ready, abort_called.get_future().wait_for(std::chrono::seconds(5)));
    potr_internal_handle_release(active);
    closer.join();

    // Assert
    EXPECT_EQ(POTR_OK, close_ret);
    EXPECT_EQ(POTR_ERR_TIMEOUT, ctx.send_queue.failure);
    EXPECT_EQ(1, cplat_atomic_load_i32(&ctx.tcp_close_timed_out, CPLAT_MEMORY_ORDER_ACQUIRE));
    EXPECT_EQ(1, cplat_atomic_load_i32(&ctx.tcp_publish_stopped, CPLAT_MEMORY_ORDER_ACQUIRE));

    // Cleanup
    potr_internal_handle_end_close(&ctx);
    potr_internal_handle_discard(&ctx);
    cplat_condvar_dispose(ctx.send_queue.drained);
    cplat_condvar_dispose(ctx.send_queue.progress);
    cplat_condvar_dispose(ctx.send_queue.not_full);
    cplat_condvar_dispose(ctx.send_queue.not_empty);
    cplat_local_lock_dispose(ctx.send_queue.mutex);
}
