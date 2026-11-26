#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <porter/infra/potr_trace.h>
#include <porter/porter_spec.h>
#include "potr_trace.inject.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

using namespace testing;

TEST(potrTraceTest, retries_after_failure_and_returns_the_same_handle)
{
    NiceMock<Mock_cplat> mock_cplat;
    cplat_tracer *const expected_tracer = reinterpret_cast<cplat_tracer *>(1);
    potr_trace_test_reset_state();

    EXPECT_CALL(mock_cplat, cplat_tracer_create(CPLAT_TRACER_CONCURRENCY_TRACER_MANAGED))
        .Times(2)
        .WillOnce(Return(nullptr))
        .WillOnce(Return(expected_tracer));
    EXPECT_CALL(mock_cplat, cplat_tracer_set_name(expected_tracer, StrEq("porter"), 0))
        .WillOnce(Return(CPLAT_ERR_UNKNOWN));

    EXPECT_EQ(nullptr, potr_tracer_get()); // [確認_異常系] - 生成失敗時に NULL を返すこと。
    EXPECT_EQ(expected_tracer, potr_tracer_get()); // [確認_正常系] - 次の呼び出しで生成を再試行すること。
    EXPECT_EQ(expected_tracer, potr_tracer_get()); // [確認_正常系] - 生成後は同じハンドルを返すこと。

    potr_trace_test_reset_state();
}

TEST(potrTraceTest, concurrent_initial_calls_create_and_publish_once)
{
    NiceMock<Mock_cplat> mock_cplat;
    cplat_tracer *const expected_tracer = reinterpret_cast<cplat_tracer *>(1);
    constexpr size_t caller_count = 5U;
    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool create_entered = false;
    bool allow_create_to_finish = false;
    bool all_waiters_yielded = false;
    size_t waiter_yield_count = 0U;
    cplat_tracer *published_during_set_name = expected_tracer;
    std::vector<cplat_tracer *> results(caller_count, nullptr);
    std::vector<std::thread> callers;
    potr_trace_test_reset_state();

    EXPECT_CALL(mock_cplat, cplat_tracer_create(CPLAT_TRACER_CONCURRENCY_TRACER_MANAGED))
        .Times(1)
        .WillOnce(Invoke([&](cplat_tracer_concurrency_mode)
                         {
                             std::unique_lock<std::mutex> lock(gate_mutex);
                             create_entered = true;
                             gate_cv.notify_all();
                             gate_cv.wait(lock, [&]() { return allow_create_to_finish; });
                             return expected_tracer;
                         }));
    EXPECT_CALL(mock_cplat, cplat_tracer_set_name(expected_tracer, StrEq("porter"), 0))
        .WillOnce(Invoke([&](cplat_tracer *, const char *, int64_t)
                         {
                             published_during_set_name = potr_trace_test_get_published();
                             return CPLAT_OK;
                         }));
    EXPECT_CALL(mock_cplat, cplat_thread_yield())
        .Times(AtLeast(static_cast<int>(caller_count - 1U)))
        .WillRepeatedly(Invoke([&]()
                               {
                                   std::lock_guard<std::mutex> lock(gate_mutex);
                                   ++waiter_yield_count;
                                   gate_cv.notify_all();
                               }));

    callers.emplace_back([&]() { results[0] = potr_tracer_get(); });
    bool creator_entered = false;
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        creator_entered = gate_cv.wait_for(lock, std::chrono::seconds(5), [&]() { return create_entered; });
    }

    if (creator_entered)
    {
        for (size_t index = 1U; index < caller_count; ++index)
        {
            callers.emplace_back([&, index]() { results[index] = potr_tracer_get(); });
        }
        std::unique_lock<std::mutex> lock(gate_mutex);
        all_waiters_yielded =
            gate_cv.wait_for(lock, std::chrono::seconds(5), [&]() { return waiter_yield_count >= caller_count - 1U; });
        allow_create_to_finish = true;
        gate_cv.notify_all();
    }
    else
    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        allow_create_to_finish = true;
        gate_cv.notify_all();
    }

    for (std::thread &caller : callers)
    {
        caller.join();
    }

    EXPECT_TRUE(creator_entered); // [確認_異常系] - 生成開始を障壁で確認できること。
    EXPECT_TRUE(all_waiters_yielded); // [確認_正常系] - 生成完了前に全待機側が待機へ入ること。
    EXPECT_GE(waiter_yield_count, caller_count - 1U); // [確認_正常系] - 他の初回呼び出しが生成完了を待つこと。
    EXPECT_EQ(nullptr, published_during_set_name); // [確認_正常系] - 識別名を設定するまで共有ポインターを公開しないこと。
    for (cplat_tracer *actual_tracer : results)
    {
        EXPECT_EQ(expected_tracer, actual_tracer); // [確認_正常系] - すべての呼び出しが公開済みの同一ハンドルを得ること。
    }

    potr_trace_test_reset_state();
}

TEST(potrTraceTest, waiters_return_failure_and_a_later_call_retries)
{
    NiceMock<Mock_cplat> mock_cplat;
    cplat_tracer *const expected_tracer = reinterpret_cast<cplat_tracer *>(1);
    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool create_entered = false;
    bool allow_failure = false;
    bool waiter_yielded = false;
    size_t waiter_yield_count = 0U;
    cplat_tracer *creator_result = expected_tracer;
    cplat_tracer *waiter_result = expected_tracer;
    std::thread creator;
    std::thread waiter;
    potr_trace_test_reset_state();

    EXPECT_CALL(mock_cplat, cplat_tracer_create(CPLAT_TRACER_CONCURRENCY_TRACER_MANAGED))
        .Times(2)
        .WillOnce(Invoke([&](cplat_tracer_concurrency_mode)
                         {
                             std::unique_lock<std::mutex> lock(gate_mutex);
                             create_entered = true;
                             gate_cv.notify_all();
                             gate_cv.wait(lock, [&]() { return allow_failure; });
                             return static_cast<cplat_tracer *>(nullptr);
                         }))
        .WillOnce(Return(expected_tracer));
    EXPECT_CALL(mock_cplat, cplat_tracer_set_name(expected_tracer, StrEq("porter"), 0)).WillOnce(Return(CPLAT_OK));
    EXPECT_CALL(mock_cplat, cplat_thread_yield())
        .Times(AtLeast(1))
        .WillRepeatedly(Invoke([&]()
                               {
                                   std::lock_guard<std::mutex> lock(gate_mutex);
                                   ++waiter_yield_count;
                                   gate_cv.notify_all();
                               }));

    creator = std::thread([&]() { creator_result = potr_tracer_get(); });
    bool creator_entered_ok = false;
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        creator_entered_ok = gate_cv.wait_for(lock, std::chrono::seconds(5), [&]() { return create_entered; });
    }
    if (creator_entered_ok)
    {
        waiter = std::thread([&]() { waiter_result = potr_tracer_get(); });
        std::unique_lock<std::mutex> lock(gate_mutex);
        waiter_yielded = gate_cv.wait_for(lock, std::chrono::seconds(5), [&]() { return waiter_yield_count > 0U; });
        allow_failure = true;
        gate_cv.notify_all();
    }
    else
    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        allow_failure = true;
        gate_cv.notify_all();
    }

    creator.join();
    if (waiter.joinable())
    {
        waiter.join();
    }
    EXPECT_TRUE(creator_entered_ok); // [確認_異常系] - 生成失敗を待つ状態へ進むこと。
    EXPECT_TRUE(waiter_yielded); // [確認_正常系] - 同時呼び出しが生成完了を待つこと。
    EXPECT_GT(waiter_yield_count, 0U); // [確認_正常系] - 同時の呼び出しが生成完了を待つこと。
    EXPECT_EQ(nullptr, creator_result); // [確認_異常系] - 生成を行った呼び出しが失敗を返すこと。
    EXPECT_EQ(nullptr, waiter_result); // [確認_異常系] - 待機中の呼び出しが同じ失敗を返し、自分では再試行しないこと。
    EXPECT_EQ(expected_tracer, potr_tracer_get()); // [確認_正常系] - 後続の新しい呼び出しが再試行に成功すること。

    potr_trace_test_reset_state();
}

TEST(potrTraceTest, trace_write_continues_when_tracer_creation_fails)
{
    NiceMock<Mock_cplat> mock_cplat;
    cplat_error detail = {};
    potr_trace_test_reset_state();

    EXPECT_CALL(mock_cplat, cplat_tracer_create(CPLAT_TRACER_CONCURRENCY_TRACER_MANAGED)).WillOnce(Return(nullptr));
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(nullptr, CPLAT_TRACE_LEVEL_ERROR, nullptr, _))
        .WillOnce(Return(CPLAT_ERR_UNKNOWN));

    potr_internal_trace_socket_failure_at(__FILE__, __LINE__, CPLAT_TRACE_LEVEL_ERROR, &detail, "test failure");
    // [確認_正常系] - トレーサーが NULL でも内部診断の呼び出しが処理を中断しないこと。

    potr_trace_test_reset_state();
}

TEST(potrTraceTest, registers_shutdown_callback_once_after_successful_creation)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    cplat_tracer *const expected_tracer = reinterpret_cast<cplat_tracer *>(1);
    cplat_shutdown_fn registered_callback = nullptr;
    potr_trace_test_reset_state(); // [状態] - porter のトレーサーを未生成の状態にする。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_create(CPLAT_TRACER_CONCURRENCY_TRACER_MANAGED))
        .WillOnce(Return(expected_tracer)); // [Pre-Assert確認_正常系] - cplat_tracer_create() が CPLAT_TRACER_CONCURRENCY_TRACER_MANAGED を指定して 1 回呼び出されること。
                                            // [Pre-Assert手順] - cplat_tracer_create() から expected_tracer を返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_set_name(expected_tracer, StrEq("porter"), 0))
        .WillOnce(Return(CPLAT_OK)); // [Pre-Assert確認_正常系] - cplat_tracer_set_name() が expected_tracer、"porter" と等しい文字列、0 を指定して 1 回呼び出されること。
                                     // [Pre-Assert手順] - cplat_tracer_set_name() から CPLAT_OK を返す。
    EXPECT_CALL(mock_cplat, cplat_shutdown_register(NotNull(), nullptr))
        .WillOnce(DoAll(SaveArg<0>(&registered_callback), Return(CPLAT_OK))); // [Pre-Assert確認_正常系] - cplat_shutdown_register() が nullptr 以外、nullptr を指定して 1 回呼び出されること。
                                                                              // [Pre-Assert手順] - 登録されたコールバックを registered_callback へ保存し、CPLAT_OK を返す。

    // Act
    cplat_tracer *actual_first = potr_tracer_get(); // [手順] - potr_tracer_get() で初回の取得を行う。
    cplat_tracer *actual_second = potr_tracer_get(); // [手順] - potr_tracer_get() でもう一度取得する。

    // Assert
    EXPECT_EQ(expected_tracer, actual_first); // [確認_正常系] - 初回の potr_tracer_get() の戻り値が expected_tracer と一致すること。
    EXPECT_EQ(expected_tracer, actual_second); // [確認_正常系] - 2 回目の potr_tracer_get() の戻り値が expected_tracer と一致すること。(2 回目の取得では登録を繰り返さないことの確認)
    EXPECT_NE(nullptr, registered_callback); // [確認_正常系] - registered_callback が nullptr 以外であること。

    // Cleanup
    potr_trace_test_reset_state();
}

TEST(potrTraceTest, returns_null_without_recreation_after_shutdown_callback)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    cplat_tracer *const expected_tracer = reinterpret_cast<cplat_tracer *>(1);
    cplat_shutdown_fn registered_callback = nullptr;
    const cplat_shutdown_event event = {CPLAT_SHUTDOWN_REASON_NORMAL_EXIT, CPLAT_SHUTDOWN_CODE_KIND_NONE, 0};
    potr_trace_test_reset_state(); // [状態] - porter のトレーサーを未生成の状態にする。
    ON_CALL(mock_cplat, cplat_tracer_set_name(_, _, _)).WillByDefault(Return(CPLAT_OK)); // [状態] - cplat_tracer_set_name() は CPLAT_OK を返す。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_create(CPLAT_TRACER_CONCURRENCY_TRACER_MANAGED))
        .WillOnce(Return(expected_tracer)); // [Pre-Assert確認_正常系] - cplat_tracer_create() が CPLAT_TRACER_CONCURRENCY_TRACER_MANAGED を指定して 1 回呼び出されること。(終了コールバックの後に再生成しないことの確認)
                                            // [Pre-Assert手順] - cplat_tracer_create() から expected_tracer を返す。
    EXPECT_CALL(mock_cplat, cplat_shutdown_register(NotNull(), nullptr))
        .WillOnce(DoAll(SaveArg<0>(&registered_callback), Return(CPLAT_OK))); // [Pre-Assert確認_正常系] - cplat_shutdown_register() が nullptr 以外、nullptr を指定して 1 回呼び出されること。
                                                                              // [Pre-Assert手順] - 登録されたコールバックを registered_callback へ保存し、CPLAT_OK を返す。

    // Act
    cplat_tracer *actual_before = potr_tracer_get(); // [手順] - potr_tracer_get() でトレーサーを生成する。
    ASSERT_NE(nullptr, registered_callback); // [確認_正常系] - registered_callback が nullptr 以外であること。
    registered_callback(&event, nullptr); // [手順] - cplat の終了処理として、registered_callback を通常の終了のイベントで呼ぶ。
    cplat_tracer *actual_public = potr_tracer_get(); // [手順] - 終了コールバックの後に potr_tracer_get() を呼ぶ。
    cplat_tracer *actual_internal = potr_internal_trace_get(); // [手順] - 終了コールバックの後に potr_internal_trace_get() を呼ぶ。

    // Assert
    EXPECT_EQ(expected_tracer, actual_before); // [確認_正常系] - 終了コールバックの前の potr_tracer_get() の戻り値が expected_tracer と一致すること。
    EXPECT_EQ(nullptr, actual_public); // [確認_正常系] - 終了コールバックの後の potr_tracer_get() の戻り値が nullptr であること。
    EXPECT_EQ(nullptr, actual_internal); // [確認_正常系] - 終了コールバックの後の potr_internal_trace_get() の戻り値が nullptr であること。
    EXPECT_EQ(nullptr, potr_trace_test_get_published()); // [確認_正常系] - potr_trace_test_get_published() の戻り値が nullptr であること。(共有ハンドルを取り下げたことの確認)

    // Cleanup
    potr_trace_test_reset_state();
}

TEST(potrTraceTest, trace_write_after_shutdown_callback_passes_null_handle)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    cplat_tracer *const expected_tracer = reinterpret_cast<cplat_tracer *>(1);
    cplat_shutdown_fn registered_callback = nullptr;
    const cplat_shutdown_event event = {CPLAT_SHUTDOWN_REASON_NORMAL_EXIT, CPLAT_SHUTDOWN_CODE_KIND_NONE, 0};
    potr_trace_test_reset_state(); // [状態] - porter のトレーサーを未生成の状態にする。
    ON_CALL(mock_cplat, cplat_tracer_create(_)).WillByDefault(Return(expected_tracer)); // [状態] - cplat_tracer_create() は expected_tracer を返す。
    ON_CALL(mock_cplat, cplat_tracer_set_name(_, _, _)).WillByDefault(Return(CPLAT_OK)); // [状態] - cplat_tracer_set_name() は CPLAT_OK を返す。
    ON_CALL(mock_cplat, cplat_shutdown_register(_, _))
        .WillByDefault(DoAll(SaveArg<0>(&registered_callback), Return(CPLAT_OK))); // [状態] - cplat_shutdown_register() は登録されたコールバックを registered_callback へ保存し、CPLAT_OK を返す。
    ASSERT_EQ(expected_tracer, potr_tracer_get()); // [状態確認] - potr_tracer_get() の戻り値が expected_tracer と一致すること。
    ASSERT_NE(nullptr, registered_callback); // [状態確認] - registered_callback が nullptr 以外であること。
    registered_callback(&event, nullptr); // [状態] - cplat の終了処理として、registered_callback を通常の終了のイベントで呼ぶ。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(nullptr, CPLAT_TRACE_LEVEL_ERROR, nullptr, _))
        .WillOnce(Return(CPLAT_OK)); // [Pre-Assert確認_正常系] - cplat_tracer_writef_at() が nullptr、CPLAT_TRACE_LEVEL_ERROR、nullptr を指定して 1 回呼び出されること。(終了処理の後は解放されたトレーサーを渡さないことの確認)
                                     // [Pre-Assert手順] - cplat_tracer_writef_at() から CPLAT_OK を返す。

    // Act
    POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "after shutdown: value=%d", 1); // [手順] - 終了コールバックの後に POTR_TRACE で出力する。

    // Assert
    SUCCEED(); // [確認_正常系] - SUCCEED() に到達すること。(終了コールバックの後の POTR_TRACE が異常終了しないことの確認)

    // Cleanup
    potr_trace_test_reset_state();
}

TEST(potrTraceTest, disposes_tracer_and_retries_when_shutdown_registration_fails)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    cplat_tracer *const failed_tracer = reinterpret_cast<cplat_tracer *>(1);
    cplat_tracer *const expected_tracer = reinterpret_cast<cplat_tracer *>(2);
    potr_trace_test_reset_state(); // [状態] - porter のトレーサーを未生成の状態にする。
    ON_CALL(mock_cplat, cplat_tracer_set_name(_, _, _)).WillByDefault(Return(CPLAT_OK)); // [状態] - cplat_tracer_set_name() は CPLAT_OK を返す。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_create(CPLAT_TRACER_CONCURRENCY_TRACER_MANAGED))
        .WillOnce(Return(failed_tracer))
        .WillOnce(Return(expected_tracer)); // [Pre-Assert確認_正常系] - cplat_tracer_create() が CPLAT_TRACER_CONCURRENCY_TRACER_MANAGED を指定して 2 回呼び出されること。(次の呼び出しで再試行することの確認)
                                            // [Pre-Assert手順] - cplat_tracer_create() から 1 回目は failed_tracer、2 回目は expected_tracer を返す。
    EXPECT_CALL(mock_cplat, cplat_shutdown_register(NotNull(), nullptr))
        .WillOnce(Return(CPLAT_ERR_OUT_OF_MEMORY))
        .WillOnce(Return(CPLAT_OK)); // [Pre-Assert確認_正常系] - cplat_shutdown_register() が nullptr 以外、nullptr を指定して 2 回呼び出されること。
                                     // [Pre-Assert手順] - cplat_shutdown_register() から 1 回目は CPLAT_ERR_OUT_OF_MEMORY、2 回目は CPLAT_OK を返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_dispose(Pointee(failed_tracer)))
        .WillOnce([](cplat_tracer **handle) { *handle = nullptr; }); // [Pre-Assert確認_異常系] - cplat_tracer_dispose() が指す先が failed_tracer であるポインターを指定して 1 回呼び出されること。
                                                                       // [Pre-Assert手順] - cplat_tracer_dispose() で *handle を nullptr にする。
    EXPECT_CALL(mock_cplat, cplat_tracer_dispose(Pointee(expected_tracer)))
        .Times(0); // [Pre-Assert確認_正常系] - 指す先が expected_tracer であるポインターを指定した cplat_tracer_dispose() の呼び出しがないこと。

    // Act
    cplat_tracer *actual_failed = potr_tracer_get(); // [手順] - 登録が失敗する状態で potr_tracer_get() を呼ぶ。
    cplat_tracer *actual_published = potr_trace_test_get_published(); // [手順] - 失敗の後の共有ハンドルを取得する。
    cplat_tracer *actual_retried = potr_tracer_get(); // [手順] - 次の potr_tracer_get() を呼ぶ。

    // Assert
    EXPECT_EQ(nullptr, actual_failed); // [確認_異常系] - 登録が失敗した potr_tracer_get() の戻り値が nullptr であること。
    EXPECT_EQ(nullptr, actual_published); // [確認_異常系] - actual_published が nullptr であること。(解放したトレーサーを公開しないことの確認)
    EXPECT_EQ(expected_tracer, actual_retried); // [確認_正常系] - 次の potr_tracer_get() の戻り値が expected_tracer と一致すること。

    // Cleanup
    potr_trace_test_reset_state();
}
