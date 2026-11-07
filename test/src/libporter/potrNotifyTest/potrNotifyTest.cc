#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/potr_context.h>
#include <porter/potr_notify.h>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <chrono>
#include <atomic>
#include <thread>
using namespace testing;
namespace
{
std::mutex s_mutex;
std::condition_variable s_cv;
bool s_entered;
bool s_release;
std::vector<potr_event> s_events;
std::vector<int> s_values;
void capture(int64_t, potr_peer_id, potr_event event, const void *data, size_t)
{
    std::unique_lock<std::mutex> lock(s_mutex);
    if (!s_entered)
    {
        s_entered = true;
        s_cv.notify_all();
        s_cv.wait(lock, [] { return s_release; });
    }
    s_events.push_back(event);
    if (event == POTR_EVENT_ERROR)
        s_values.push_back(*static_cast<const int *>(data));
    else
        s_values.push_back(*static_cast<const unsigned char *>(data));
}
} // namespace
class potrNotifyTest : public Test
{
  protected:
    potr_context ctx = {};
    // [サブ手順 名前=potrNotifyTest.SetUp]
    void SetUp() override
    {
        s_entered = false;
        s_release = false;
        s_events.clear();
        s_values.clear();
        ctx.callback = capture;
        ASSERT_EQ(POTR_OK, potr_internal_send_queue_init(&ctx.send_queue, 2U, 64U));
        // [状態確認] - potr_internal_send_queue_init() が成功すること。
        ASSERT_EQ(POTR_OK, potr_internal_notify_start(&ctx));
        // [状態確認] - potr_internal_notify_start() が成功すること。
    }
    // [サブ手順終了]
    // [サブ手順 名前=potrNotifyTest.TearDown]
    void TearDown() override
    {
        release();
        EXPECT_EQ(POTR_OK, potr_internal_notify_stop(&ctx));
        // [状態確認] - potr_internal_notify_stop() で資源を回収できること。
        potr_internal_send_queue_dispose(&ctx.send_queue);
    }
    // [サブ手順終了]
    void release()
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_release = true;
        s_cv.notify_all();
    }
    bool wait_entered()
    {
        std::unique_lock<std::mutex> lock(s_mutex);
        return s_cv.wait_for(lock, std::chrono::seconds(5), [] { return s_entered; });
    }
};

// 通知が元データから独立し、停止時に登録順で完了することの確認
// [サブ手順参照 名前=potrNotifyTest.SetUp]
TEST_F(potrNotifyTest, owns_data_and_drains_in_order)
{
    // Arrange
    unsigned char first = 17;
    unsigned char second = 23;
    // Pre-Assert
    // Act
    potr_internal_notify_enqueue(&ctx, 1U, POTR_EVENT_DATA, &first, 1U);
    // [手順] - 先行通知を登録する。
    bool entered = wait_entered(); // [手順] - 通知スレッドがコールバックへ入るまで待つ。
    potr_internal_notify_enqueue(&ctx, 1U, POTR_EVENT_DATA, &second, 1U);
    // [手順] - コールバックを実行中に後続通知を登録する。
    first = 99;
    second = 99;
    release();
    int actual_ret = potr_internal_notify_stop(&ctx); // [手順] - 登録済み通知を完了させる。
    // Assert
    EXPECT_TRUE(entered);           // [確認_正常系] - 通知スレッドが先行通知を開始したこと。
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 通知をすべて処理して停止したこと。
    EXPECT_EQ((std::vector<int>{17, 23}),
              s_values); // [確認_正常系] - 元領域の変更から独立した内容と順序を保持すること。
}
// [サブ手順参照 名前=potrNotifyTest.TearDown]

// 通知の確保失敗時に予約領域から異常を通知し、送信待機を解除することの確認
// [サブ手順参照 名前=potrNotifyTest.SetUp]
TEST_F(potrNotifyTest, allocation_failure_uses_reserved_error_and_wakes_senders)
{
    // Arrange
    unsigned char data = 31;
    uint64_t ticket = 0U;
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_push(&ctx.send_queue, 1U, 0U, &data, 1U, &ticket));
    // [状態確認] - 未完了の送信チケットを発行できること。
    potr_internal_notify_enqueue(&ctx, 1U, POTR_EVENT_DATA, &data, 1U);
    bool entered = wait_entered();
    std::atomic<bool> waiter_entered(false);
    std::atomic<bool> waiter_done(false);
    NiceMock<Mock_cplat> mock_cplat;
    ON_CALL(mock_cplat, cplat_malloc(_)).WillByDefault(Return(nullptr));
    // [状態] - 通知領域の追加確保を失敗させる。
    ON_CALL(mock_cplat, cplat_condvar_wait(ctx.send_queue.progress, ctx.send_queue.mutex, _))
        .WillByDefault(
            [&](cplat_condvar *cv, cplat_local_lock *mutex, int timeout)
            {
                waiter_entered.store(true);
                return delegate_real_cplat_condvar_wait(cv, mutex, timeout);
            });
    // [状態] - チケット待機へ入った時点を記録する。
    // Pre-Assert
    // Act
    std::thread waiter(
        [&]
        {
            potr_internal_send_queue_wait_ticket(&ctx.send_queue, ticket);
            waiter_done.store(true);
        }); // [手順] - 未完了の送信チケットを待つ。
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!waiter_entered.load() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    {
        potr_internal_notify_enqueue(&ctx, 1U, POTR_EVENT_DATA, &data, 1U);
        // [手順] - 通知領域を確保できない通知を登録する。
        potr_internal_notify_enqueue(&ctx, 1U, POTR_EVENT_DATA, &data, 1U);
        // [手順] - 異常確定後も通知を試みる。
    }
    waiter.join(); // [手順] - 異常通知で送信待機が解除されるまで待つ。
    release();
    int actual_ret = potr_internal_notify_stop(&ctx); // [手順] - 先行通知と異常通知を処理する。
    // Assert
    EXPECT_TRUE(waiter_entered.load()); // [確認_正常系] - 送信チケットの待機中に確保を失敗させたこと。
    EXPECT_TRUE(waiter_done.load());    // [確認_異常系] - 異常理由によって送信待機が解除されること。
    EXPECT_TRUE(entered);               // [確認_正常系] - 先行コールバックの実行を確認したこと。
    EXPECT_EQ(POTR_OK, actual_ret);     // [確認_正常系] - 異常通知後に回収できること。
    EXPECT_EQ((std::vector<potr_event>{POTR_EVENT_DATA, POTR_EVENT_ERROR}), s_events);
    // [確認_異常系] - 先行通知の後に異常通知だけを一度実行すること。
    EXPECT_EQ((std::vector<int>{31, POTR_ERR_OUT_OF_MEMORY}), s_values);
    // [確認_異常系] - 予約領域から原因コードを渡すこと。
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, ctx.send_queue.failure);
    // [確認_異常系] - 送信待機にメモリ不足を設定すること。
}
// [サブ手順参照 名前=potrNotifyTest.TearDown]
