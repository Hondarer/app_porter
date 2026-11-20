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
#include <chrono>
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
        ASSERT_EQ(POTR_OK,
                  potr_internal_send_queue_push(&q, 0, 0, payload, sizeof(payload),
                                                nullptr)); // [状態] - depth=4 のキューに 4 件を積んで満杯にする。
        // [状態確認] - potr_internal_send_queue_push() の戻り値が POTR_OK であること。
    }

    // Pre-Assert

    // Act
    int actual_ret_full = potr_internal_send_queue_push(
        &q, 0, 0, payload, sizeof(payload),
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
        ASSERT_EQ(POTR_OK, potr_internal_send_queue_push(&q, 0, 0, payload, sizeof(payload),
                                                         nullptr)); // [状態] - キューを満杯にしておく。
        // [状態確認] - potr_internal_send_queue_push() の戻り値が POTR_OK であること。
    }

    // Pre-Assert

    // Act
    int actual_ret = potr_internal_send_queue_push_wait(
        &q, 0, 0, payload, sizeof(payload), &running,
        nullptr); // [手順] - 満杯のキューと停止済みの実行フラグを指定して potr_internal_send_queue_push_wait() を呼び出す。

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
    int actual_ret1 =
        potr_internal_send_queue_push(&q, 0, 0, payload, sizeof(payload),
                                      &ticket1); // [手順] - 1 件目を potr_internal_send_queue_push() で登録する。
    int actual_ret2 = potr_internal_send_queue_push_wait(
        &q, 0, 0, payload, sizeof(payload), &running,
        &ticket2); // [手順] - 2 件目を potr_internal_send_queue_push_wait() で登録する。
    int actual_ret3 =
        potr_internal_send_queue_push(&q, 0, 0, payload, sizeof(payload),
                                      &ticket3); // [手順] - 3 件目を potr_internal_send_queue_push() で登録する。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret1);  // [確認_正常系] - 1 件目の登録が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret2);  // [確認_正常系] - 2 件目の登録が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret3);  // [確認_正常系] - 3 件目の登録が成功すること。
    EXPECT_EQ(1U, ticket1);           // [確認_正常系] - 1 件目の番号が 1 であること。
    EXPECT_EQ(2U, ticket2);           // [確認_正常系] - 2 件目の番号が 2 であること。
    EXPECT_EQ(3U, ticket3);           // [確認_正常系] - 3 件目の番号が 3 であること。
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

// 空きが要求数以上なら check_space が POTR_OK を返すことの確認
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, check_space_returns_ok_when_space_is_enough)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};

    // Pre-Assert

    // Act
    int actual_ret_empty = potr_internal_send_queue_check_space(
        &q, 4); // [手順] - エントリを積んでいない depth=4 のキューへ、容量と同じ 4 件を指定して potr_internal_send_queue_check_space() を呼び出す。
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_push(&q, 0, 0, payload, sizeof(payload),
                                                     nullptr)); // [手順] - 1 件を登録する。
    int actual_ret_one = potr_internal_send_queue_check_space(
        &q, 3); // [手順] - 1 件登録済みのキューへ、容量 - 1 の 3 件を指定して potr_internal_send_queue_check_space() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_empty); // [確認_正常系] - 空のキューで容量と同じ件数の確認が POTR_OK であること。
    EXPECT_EQ(POTR_OK, actual_ret_one); // [確認_正常系] - 1 件登録済みで容量 - 1 の件数の確認が POTR_OK であること。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]

// 空きが不足する場合に check_space が POTR_ERR_FULL を返し、キューの件数を変えないことの確認
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, check_space_returns_full_when_space_is_short)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_push(&q, 0, 0, payload, sizeof(payload),
                                                     nullptr)); // [状態] - 1 件を登録済みにする。
    // [状態確認] - potr_internal_send_queue_push() の戻り値が POTR_OK であること。
    size_t count_before = q.count;

    // Pre-Assert

    // Act
    int actual_ret = potr_internal_send_queue_check_space(
        &q, 4); // [手順] - 1 件登録済みの depth=4 のキューへ、容量と同じ 4 件を指定して potr_internal_send_queue_check_space() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERR_FULL, actual_ret); // [確認_異常系] - potr_internal_send_queue_check_space() の戻り値が POTR_ERR_FULL であること。
    EXPECT_EQ(count_before, q.count);     // [確認_正常系] - 確認の前後でキューの件数が変わらないこと。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]

// 送出中 (inflight) のエントリも check_space の空きを減らし、完了後に戻ることの確認
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, check_space_counts_inflight_entries)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    potr_internal_payload_elem elem;
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_push(&q, 0, 0, payload, sizeof(payload),
                                                     nullptr)); // [状態] - 1 件を登録する。
    // [状態確認] - potr_internal_send_queue_push() の戻り値が POTR_OK であること。
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_try_pop(&q, &elem)); // [状態] - 登録した 1 件を取り出して送出中にする。
    // [状態確認] - potr_internal_send_queue_try_pop() の戻り値が POTR_OK であること。

    // Pre-Assert

    // Act
    int actual_ret_inflight = potr_internal_send_queue_check_space(
        &q, 4); // [手順] - 送出中の 1 件がある状態で、容量と同じ 4 件を指定して potr_internal_send_queue_check_space() を呼び出す。
    potr_internal_send_queue_complete(&q); // [手順] - 送出中の 1 件を完了させる。
    int actual_ret_completed = potr_internal_send_queue_check_space(
        &q, 4); // [手順] - 完了後に、容量と同じ 4 件を指定して potr_internal_send_queue_check_space() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERR_FULL, actual_ret_inflight); // [確認_異常系] - 送出中の 1 件が空きを減らし、戻り値が POTR_ERR_FULL であること。
    EXPECT_EQ(POTR_OK, actual_ret_completed);      // [確認_正常系] - 完了後は空きが戻り、戻り値が POTR_OK であること。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]

// 待機中断理由が設定されている場合に check_space がその原因コードを返すことの確認
// (テストのビルド定義に potr_send_queue_abort.c を取り込んでいないため、failure を直接設定する)
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, check_space_returns_failure_when_aborted)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    q.failure = POTR_ERR_IO; // [状態] - 待機中断理由 failure に POTR_ERR_IO を設定する。

    // Pre-Assert

    // Act
    int actual_ret = potr_internal_send_queue_check_space(
        &q, 1); // [手順] - 待機中断理由が設定されたキューへ 1 件を指定して potr_internal_send_queue_check_space() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_ret); // [確認_異常系] - 戻り値が待機中断理由の原因コード POTR_ERR_IO であること。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]

// defer が完了済み件数だけを進め、complete_deferred が送出中の件数だけを減らすことの確認
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, defer_advances_completed_seq_and_complete_deferred_releases_inflight)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    potr_internal_payload_elem elem;
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_push(&q, 0, 0, payload, sizeof(payload),
                                                     nullptr)); // [状態] - 1 件を登録する。
    // [状態確認] - potr_internal_send_queue_push() の戻り値が POTR_OK であること。
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_try_pop(&q, &elem)); // [状態] - 登録した 1 件を取り出して送出中にする。
    // [状態確認] - potr_internal_send_queue_try_pop() の戻り値が POTR_OK であること。

    // Pre-Assert

    // Act
    potr_internal_send_queue_defer(&q); // [手順] - 取り出した 1 件を potr_internal_send_queue_defer() で保留する。

    // Assert
    EXPECT_EQ(1U, q.completed_seq); // [確認_正常系] - 保留により完了済み件数が 1 増えること。
    EXPECT_EQ(1U, q.inflight);      // [確認_正常系] - 保留では送出中の件数が減らないこと。

    // Act_2
    potr_internal_send_queue_complete_deferred(&q); // [手順] - potr_internal_send_queue_complete_deferred() を呼び出す。

    // Assert_2
    EXPECT_EQ(0U, q.inflight);      // [確認_正常系] - 送出中の件数が 1 減って 0 になること。
    EXPECT_EQ(1U, q.completed_seq); // [確認_正常系] - 完了済み件数が変わらないこと。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]

// 保留したエントリが、後続エントリの番号の完了待ちを止めないことの確認
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, deferred_entry_does_not_block_wait_ticket_of_next_entry)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
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
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_try_pop(&q, &elem)); // [手順] - 1 件目を取り出す。
    potr_internal_send_queue_defer(&q);                               // [手順] - 1 件目を保留する。
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int done_after_defer = waiter_done.load();
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_try_pop(&q, &elem)); // [手順] - 2 件目を取り出す。
    potr_internal_send_queue_complete(&q);                            // [手順] - 2 件目を完了させる。
    waiter.join();

    // Assert
    EXPECT_EQ(0, done_after_defer);   // [確認_正常系] - 1 件目の保留だけでは 2 件目の番号の待ちが解除されないこと。
    EXPECT_EQ(1, waiter_done.load()); // [確認_正常系] - 保留した 1 件目の完了を待たず、2 件目の完了で待ちが解除されること。
    EXPECT_EQ(2U, q.completed_seq);   // [確認_正常系] - 完了済み件数が 2 であること。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]

// 保留したエントリが残る間は排出済みにならず、complete_deferred 後に wait_drained が戻ることの確認
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, deferred_entry_blocks_drained_until_complete_deferred)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    potr_internal_payload_elem elem;
    std::atomic<int> waiter_done(0);
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_push(&q, 0, 0, payload, sizeof(payload),
                                                     nullptr)); // [状態] - 1 件を登録する。
    // [状態確認] - potr_internal_send_queue_push() の戻り値が POTR_OK であること。
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_try_pop(&q, &elem)); // [状態] - 登録した 1 件を取り出す。
    // [状態確認] - potr_internal_send_queue_try_pop() の戻り値が POTR_OK であること。
    potr_internal_send_queue_defer(&q); // [状態] - 取り出した 1 件を保留する。
    std::thread waiter(
        [&]()
        {
            potr_internal_send_queue_wait_drained(&q);
            waiter_done.store(1);
        }); // [状態] - 別スレッドで排出済みになるまで待つ。

    // Pre-Assert

    // Act
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int done_while_deferred = waiter_done.load();
    size_t count_while_deferred = q.count;
    size_t inflight_while_deferred = q.inflight;
    potr_internal_send_queue_complete_deferred(&q); // [手順] - 保留したエントリを potr_internal_send_queue_complete_deferred() で完了させる。
    waiter.join();

    // Assert
    EXPECT_EQ(0U, count_while_deferred);    // [確認_正常系] - 保留中もキュー内のエントリ数が 0 であること。
    EXPECT_EQ(1U, inflight_while_deferred); // [確認_正常系] - 保留中は送出中の件数が 1 のままであること。
    EXPECT_EQ(0, done_while_deferred);      // [確認_正常系] - 保留中は potr_internal_send_queue_wait_drained() が戻らないこと。
    EXPECT_EQ(1, waiter_done.load());       // [確認_正常系] - 完了後に potr_internal_send_queue_wait_drained() が戻ること。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]

// 保留したエントリが容量を占有し続けることの確認
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, deferred_entry_keeps_occupying_capacity)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    potr_internal_payload_elem elem;
    potr_internal_send_queue small_q;
    memset(&small_q, 0, sizeof(small_q));
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_init(&small_q, 2, 16)); // [状態] - 容量 2 の送信キューを別に用意する。
    // [状態確認] - potr_internal_send_queue_init() の戻り値が POTR_OK であること。
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_push(&small_q, 0, 0, payload, sizeof(payload),
                                                     nullptr)); // [状態] - 1 件を登録する。
    // [状態確認] - potr_internal_send_queue_push() の戻り値が POTR_OK であること。
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_try_pop(&small_q, &elem)); // [状態] - 登録した 1 件を取り出す。
    // [状態確認] - potr_internal_send_queue_try_pop() の戻り値が POTR_OK であること。
    potr_internal_send_queue_defer(&small_q); // [状態] - 取り出した 1 件を保留する。

    // Pre-Assert

    // Act
    int actual_ret_second = potr_internal_send_queue_push(
        &small_q, 0, 0, payload, sizeof(payload), nullptr); // [手順] - 保留中に 2 件目を potr_internal_send_queue_push() で登録する。
    int actual_ret_third = potr_internal_send_queue_push(
        &small_q, 0, 0, payload, sizeof(payload), nullptr); // [手順] - 保留中に 3 件目を potr_internal_send_queue_push() で登録する。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_second);   // [確認_正常系] - 保留中でも残りの 1 件は登録できること。
    EXPECT_EQ(POTR_ERR_FULL, actual_ret_third); // [確認_異常系] - 保留したエントリが容量を占有するため、3 件目は POTR_ERR_FULL であること。

    // 後始末
    potr_internal_send_queue_dispose(&small_q);
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]

// 通知回数が呼び出し元の値と異なる場合に、wait_flow が待たずに戻り、通知回数を更新することの確認
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, wait_flow_returns_immediately_when_generation_differs)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    uint64_t generation = 0U;
    potr_internal_send_queue_wake_flow(&q); // [状態] - potr_internal_send_queue_wake_flow() を 1 回呼び、通知回数を 1 にする。

    // Pre-Assert

    // Act
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    potr_internal_send_queue_wait_flow(
        &q, &generation, 5000); // [手順] - generation=0 と timeout_ms=5000 を指定して potr_internal_send_queue_wait_flow() を呼び出す。
    long long elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    // Assert
    EXPECT_LT(elapsed_ms, 1000); // [確認_正常系] - timeout_ms を待たずに戻ること。
    EXPECT_EQ(1U, generation);   // [確認_正常系] - generation が現在の通知回数 1 へ更新されること。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]

// 通知回数が同じ場合に、wait_flow が timeout まで待って戻ることの確認
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, wait_flow_waits_until_timeout_when_generation_is_same)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    uint64_t generation = 0U; // [状態] - 通知を受けていないキューと、現在値と同じ generation=0 を用意する。

    // Pre-Assert

    // Act
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    potr_internal_send_queue_wait_flow(
        &q, &generation, 50); // [手順] - generation=0 と timeout_ms=50 を指定して potr_internal_send_queue_wait_flow() を呼び出す。
    long long elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    // Assert
    EXPECT_GE(elapsed_ms, 30); // [確認_正常系] - 30 ms 以上待ってから戻ること。
    EXPECT_EQ(0U, generation); // [確認_正常系] - 通知がないため generation が 0 のままであること。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]

// 待機中の wake_flow で、wait_flow が timeout を待たずに戻ることの確認
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, wait_flow_returns_when_wake_flow_is_called)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    uint64_t generation = 0U;
    long long elapsed_ms = -1;
    std::thread waiter(
        [&]()
        {
            std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            potr_internal_send_queue_wait_flow(&q, &generation, 5000);
            elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        }); // [状態] - 別スレッドで generation=0、timeout_ms=5000 の potr_internal_send_queue_wait_flow() を呼び出して待たせる。

    // Pre-Assert

    // Act
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    potr_internal_send_queue_wake_flow(&q); // [手順] - 待機中に potr_internal_send_queue_wake_flow() を呼び出す。
    waiter.join();

    // Assert
    EXPECT_GE(elapsed_ms, 0);    // [確認_正常系] - 待機スレッドが戻っていること。
    EXPECT_LT(elapsed_ms, 3000); // [確認_正常系] - timeout_ms を待たずに戻ること。
    EXPECT_EQ(1U, generation);   // [確認_正常系] - generation が 1 増えていること。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]

// 初期化していないキューに対する wake_flow が何もせずに戻ることの確認
TEST(potrSendQueueUninitializedTest, wake_flow_does_nothing_for_uninitialized_queue)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_send_queue uninit_q;
    memset(&uninit_q, 0, sizeof(uninit_q)); // [状態] - memset で 0 にしただけで mutex が NULL の未初期化のキューを用意する。

    // Pre-Assert

    // Act
    potr_internal_send_queue_wake_flow(&uninit_q); // [手順] - 未初期化のキューを指定して potr_internal_send_queue_wake_flow() を呼び出す。

    // Assert
    EXPECT_EQ(0U, uninit_q.flow_generation); // [確認_正常系] - クラッシュせず、通知回数が 0 のままであること。
}

// 満杯のキューで待機中断理由が設定されている場合に、push が空き不足より先に原因コードを返すことの確認
// (送信 API は、断片が 1 つのメッセージで check_space を省き push だけを呼ぶため、check_space と同じ優先順位であることを確かめる)
// [サブ手順参照 名前=potrSendQueueTest.SetUp]
TEST_F(potrSendQueueTest, push_returns_failure_before_full)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    size_t i;

    for (i = 0; i < 4; i++)
    {
        ASSERT_EQ(POTR_OK,
                  potr_internal_send_queue_push(&q, 0, 0, payload, sizeof(payload),
                                                nullptr)); // [状態] - depth=4 のキューに 4 件を積んで満杯にする。
        // [状態確認] - potr_internal_send_queue_push() の戻り値が POTR_OK であること。
    }
    q.failure = POTR_ERR_IO; // [状態] - 待機中断理由 failure に POTR_ERR_IO を設定する。

    // Pre-Assert

    // Act
    int actual_push = potr_internal_send_queue_push(
        &q, 0, 0, payload, sizeof(payload),
        nullptr); // [手順] - 満杯で待機中断理由が設定されたキューへ potr_internal_send_queue_push() を呼び出す。
    int actual_check = potr_internal_send_queue_check_space(
        &q, 1); // [手順] - 同じキューへ 1 件を指定して potr_internal_send_queue_check_space() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERR_IO, actual_push);  // [確認_異常系] - push の戻り値が待機中断理由の原因コード POTR_ERR_IO であること。
    EXPECT_EQ(POTR_ERR_IO, actual_check); // [確認_異常系] - check_space の戻り値も POTR_ERR_IO であり、同じ優先順位であること。
    EXPECT_EQ(4U, q.count);               // [確認_異常系] - キューの件数が 4 のままであること。
}
// [サブ手順参照 名前=potrSendQueueTest.TearDown]
