#include <cplat/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_cplat.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/infra/potr_send_queue.h>

#include <atomic>
#include <string.h>
#include <thread>

using namespace testing;

class potrSendQueueTest : public Test
{
  protected:
    // [サブ手順 名前=potrSendQueueTest.SetUp]
    void SetUp() override
    {
        memset(&q, 0, sizeof(q));
        ASSERT_EQ(POTR_OK, potr_internal_send_queue_init(&q, 4, 16));
        // [状態確認] - potr_internal_send_queue_init() の戻り値が POTR_OK であること。
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrSendQueueTest.TearDown]
    void TearDown() override
    {
        potr_internal_send_queue_dispose(&q);
    }
    // [サブ手順終了]

    potr_internal_send_queue q;
};

// 空キューの参照系 API が空とタイムアウトを区別した結果コードを返すことの確認
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, empty_queue_returns_empty_and_timeout)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_payload_elem elem; // [状態] - エントリを 1 件も積んでいない送信キューを用意する。

    // Pre-Assert

    // Act
    int actual_ret_peek = potr_internal_send_queue_peek(
        &q, &elem); // [手順] - 空キューを指定して potr_internal_send_queue_peek() を呼び出す。
    int actual_ret_try_pop = potr_internal_send_queue_try_pop(
        &q, &elem); // [手順] - 空キューを指定して potr_internal_send_queue_try_pop() を呼び出す。
    int actual_ret_timed = potr_internal_send_queue_peek_timed(
        &q, &elem,
        10); // [手順] - 空キューと timeout_ms=10 を指定して potr_internal_send_queue_peek_timed() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_EMPTY,
        actual_ret_peek); // [確認_異常系] - potr_internal_send_queue_peek() の戻り値が POTR_ERR_EMPTY であること。
    EXPECT_EQ(
        POTR_ERR_EMPTY,
        actual_ret_try_pop); // [確認_異常系] - potr_internal_send_queue_try_pop() の戻り値が POTR_ERR_EMPTY であること。
    EXPECT_EQ(
        POTR_ERR_TIMEOUT,
        actual_ret_timed); // [確認_異常系] - potr_internal_send_queue_peek_timed() の戻り値が POTR_ERR_TIMEOUT であること。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]

// 満杯キューへの push が POTR_ERR_FULL を返すことの確認
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, push_to_full_queue_returns_full)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    size_t i;

    for (i = 0; i < 4; i++)
    {
        ASSERT_EQ(POTR_OK, potr_internal_send_queue_push(
                               &q, 0, 0, payload,
                               sizeof(payload), nullptr)); // [状態] - depth=4 のキューに 4 件を積んで満杯にする。
        // [状態確認] - potr_internal_send_queue_push() の戻り値が POTR_OK であること。
    }

    // Pre-Assert

    // Act
    int actual_ret_full = potr_internal_send_queue_push(
        &q, 0, 0, payload,
        sizeof(
            payload),
        nullptr); // [手順] - 満杯のキューに 5 件目のペイロードを指定して potr_internal_send_queue_push() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_FULL,
        actual_ret_full); // [確認_異常系] - 満杯時の potr_internal_send_queue_push() の戻り値が POTR_ERR_FULL であること。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]

// 停止済み (running=0) の満杯キューへの push_wait が POTR_ERR_CANCELED を返すことの確認
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, push_wait_returns_canceled_when_stopped)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const uint8_t payload[4] = {0x11, 0x22, 0x33, 0x44};
    cplat_atomic_i32 running = CPLAT_ATOMIC_INIT(0); // [状態] - 実行フラグ running を 0 (停止済み) とする。
    size_t i;

    for (i = 0; i < 4; i++)
    {
        ASSERT_EQ(POTR_OK, potr_internal_send_queue_push(&q, 0, 0, payload,
                                                         sizeof(payload), nullptr)); // [状態] - キューを満杯にしておく。
        // [状態確認] - potr_internal_send_queue_push() の戻り値が POTR_OK であること。
    }

    // Pre-Assert

    // Act
    int actual_ret = potr_internal_send_queue_push_wait(
        &q, 0, 0, payload, sizeof(payload),
        &running, nullptr); // [手順] - 満杯のキューと停止済みの実行フラグを指定して potr_internal_send_queue_push_wait() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_CANCELED,
        actual_ret); // [確認_異常系] - potr_internal_send_queue_push_wait() の戻り値が POTR_ERR_CANCELED であること。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]

// 登録順に 1 から始まる番号を返し、登録用ロックを初期化で生成して破棄で解放することの確認
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, push_returns_sequential_tickets)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    cplat_atomic_i32 running = CPLAT_ATOMIC_INIT(1); // [状態] - 実行フラグ running を 1 (実行中) とする。
    uint64_t ticket1 = 0U;
    uint64_t ticket2 = 0U;
    uint64_t ticket3 = 0U;

    // Pre-Assert

    // Act
    int actual_ret1 = potr_internal_send_queue_push(&q, 0, 0, payload, sizeof(payload),
                                                    &ticket1); // [手順] - 1 件目を potr_internal_send_queue_push() で登録する。
    int actual_ret2 = potr_internal_send_queue_push_wait(
        &q, 0, 0, payload, sizeof(payload), &running,
        &ticket2); // [手順] - 2 件目を potr_internal_send_queue_push_wait() で登録する。
    int actual_ret3 = potr_internal_send_queue_push(&q, 0, 0, payload, sizeof(payload),
                                                    &ticket3); // [手順] - 3 件目を potr_internal_send_queue_push() で登録する。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret1); // [確認_正常系] - 1 件目の登録が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret2); // [確認_正常系] - 2 件目の登録が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret3); // [確認_正常系] - 3 件目の登録が成功すること。
    EXPECT_EQ(1U, ticket1);          // [確認_正常系] - 1 件目の番号が 1 であること。
    EXPECT_EQ(2U, ticket2);          // [確認_正常系] - 2 件目の番号が 2 であること。
    EXPECT_EQ(3U, ticket3);          // [確認_正常系] - 3 件目の番号が 3 であること。
    EXPECT_NE(nullptr, q.push_mutex); // [確認_正常系] - 登録用ロックが生成されていること。
    EXPECT_NE(nullptr, q.progress);   // [確認_正常系] - 完了通知の条件変数が生成されていること。

    // Act_2
    potr_internal_send_queue_lock_push(&q);   // [手順] - 登録用ロックを取得する。
    potr_internal_send_queue_unlock_push(&q); // [手順] - 登録用ロックを解放する。
    potr_internal_send_queue_dispose(&q);     // [手順] - キューを破棄する。

    // Assert_2
    EXPECT_EQ(nullptr, q.push_mutex); // [確認_正常系] - 破棄後に登録用ロックが NULL であること。
    EXPECT_EQ(nullptr, q.progress);   // [確認_正常系] - 破棄後に完了通知の条件変数が NULL であること。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]

// 番号待ちが、その番号までのエントリの完了で解除されることの確認
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, wait_ticket_returns_after_entries_complete)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    cplat_atomic_i32 running = CPLAT_ATOMIC_INIT(1);
    potr_internal_payload_elem elem;
    uint64_t ticket = 0U;
    std::atomic<int> waiter_done(0);
    size_t i;

    for (i = 0; i < 2; i++)
    {
        ASSERT_EQ(POTR_OK, potr_internal_send_queue_push(&q, 0, 0, payload, sizeof(payload),
                                                         &ticket)); // [状態] - 2 件を登録し、2 件目の番号を保持する。
        // [状態確認] - potr_internal_send_queue_push() の戻り値が POTR_OK であること。
    }
    std::thread waiter(
        [&]()
        {
            potr_internal_send_queue_wait_ticket(&q, ticket);
            waiter_done.store(1);
        }); // [状態] - 別スレッドで 2 件目の番号の完了を待つ。

    // Pre-Assert

    // Act
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_pop(&q, &elem, &running));
    // [手順] - 1 件目を取り出す。
    potr_internal_send_queue_complete(&q); // [手順] - 1 件目を完了させる。
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int done_after_first = waiter_done.load();
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_pop(&q, &elem, &running));
    // [手順] - 2 件目を取り出す。
    potr_internal_send_queue_complete(&q); // [手順] - 2 件目を完了させる。
    waiter.join();

    // Assert
    EXPECT_EQ(0, done_after_first);   // [確認_正常系] - 1 件目の完了だけでは待ちが解除されないこと。
    EXPECT_EQ(1, waiter_done.load()); // [確認_正常系] - 2 件目の完了で待ちが解除されること。
    EXPECT_EQ(2U, q.completed_seq);   // [確認_正常系] - 完了済み件数が 2 であること。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]

// 送信失敗として完了したエントリが、その番号以前から始まるメッセージの結果に反映されることの確認
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, complete_failed_reports_failure_to_covering_tickets)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    cplat_atomic_i32 running = CPLAT_ATOMIC_INIT(1);
    potr_internal_payload_elem elem;
    uint64_t tickets[3] = {0U, 0U, 0U};
    size_t i;

    for (i = 0; i < 3; i++)
    {
        ASSERT_EQ(POTR_OK, potr_internal_send_queue_push(&q, 0, 0, payload, sizeof(payload),
                                                         &tickets[i])); // [状態] - 3 件を登録し、各番号を保持する。
        // [状態確認] - potr_internal_send_queue_push() の戻り値が POTR_OK であること。
    }

    // Pre-Assert

    // Act
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_pop(&q, &elem, &running));
    // [手順] - 1 件目を取り出す。
    potr_internal_send_queue_complete(&q); // [手順] - 1 件目を成功として完了させる。
    int result_before_failure = potr_internal_send_queue_ticket_result(&q, tickets[0]);
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_pop(&q, &elem, &running));
    // [手順] - 2 件目を取り出す。
    potr_internal_send_queue_complete_failed(&q, POTR_ERR_DISCONNECTED); // [手順] - 2 件目を送信失敗として完了させる。

    // Assert
    EXPECT_EQ(POTR_OK, result_before_failure); // [確認_正常系] - 失敗の記録前は 1 件目からの結果が POTR_OK であること。
    EXPECT_EQ(POTR_ERR_DISCONNECTED, potr_internal_send_queue_ticket_result(&q, tickets[1]));
    // [確認_異常系] - 失敗したエントリから始まるメッセージの結果が POTR_ERR_DISCONNECTED であること。
    EXPECT_EQ(POTR_ERR_DISCONNECTED, potr_internal_send_queue_ticket_result(&q, tickets[0]));
    // [確認_異常系] - 失敗したエントリを含むメッセージの結果が POTR_ERR_DISCONNECTED であること。
    EXPECT_EQ(POTR_OK, potr_internal_send_queue_ticket_result(&q, tickets[2]));
    // [確認_正常系] - 失敗より後から始まるメッセージの結果が POTR_OK であること。
    EXPECT_EQ(2U, q.completed_seq); // [確認_正常系] - 送信失敗も完了済み件数に数えること。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]

// 停止済み (running=0) の空キューからの pop が POTR_ERR_CANCELED を返すことの確認
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, pop_returns_canceled_when_stopped)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_payload_elem elem;
    cplat_atomic_i32 running =
        CPLAT_ATOMIC_INIT(0); // [状態] - エントリのない空キューと、値 0 (停止済み) の実行フラグ running を用意する。

    // Pre-Assert

    // Act
    int actual_ret = potr_internal_send_queue_pop(
        &q, &elem,
        &running); // [手順] - 空のキューと停止済みの実行フラグを指定して potr_internal_send_queue_pop() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERR_CANCELED,
              actual_ret); // [確認_異常系] - potr_internal_send_queue_pop() の戻り値が POTR_ERR_CANCELED であること。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]
