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
