#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_com_util.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/infra/potr_send_queue.h>

#include <string.h>

using namespace testing;

class potrSendQueueTest : public Test
{
  protected:
    void SetUp() override
    {
        memset(&q, 0, sizeof(q));
        ASSERT_EQ(POTR_OK, potr_internal_send_queue_init(&q, 4, 16));
    }

    void TearDown() override
    {
        potr_internal_send_queue_dispose(&q);
    }

    potr_internal_send_queue q;
};

// 空キューの参照系 API が空とタイムアウトを区別した結果コードを返すことの確認
TEST_F(potrSendQueueTest, empty_queue_returns_empty_and_timeout)
{
    // Arrange
    NiceMock<Mock_com_util> mock_com_util;
    potr_internal_payload_elem elem; // [状態] - エントリを 1 件も積んでいない送信キューを用意する。

    // Pre-Assert

    // Act
    int rtc_peek = potr_internal_send_queue_peek(&q, &elem);            // [手順] - 空キューを peek で参照する。
    int rtc_try_pop = potr_internal_send_queue_try_pop(&q, &elem);      // [手順] - 空キューから try_pop で取り出す。
    int rtc_timed = potr_internal_send_queue_peek_timed(&q, &elem, 10); // [手順] - timeout_ms=10 の peek_timed で待機する。

    // Assert
    EXPECT_EQ(POTR_ERR_EMPTY,
              rtc_peek); // [確認_異常系] - potr_internal_send_queue_peek の戻り値が POTR_ERR_EMPTY であること。
    EXPECT_EQ(POTR_ERR_EMPTY,
              rtc_try_pop); // [確認_異常系] - potr_internal_send_queue_try_pop の戻り値が POTR_ERR_EMPTY であること。
    EXPECT_EQ(POTR_ERR_TIMEOUT,
              rtc_timed); // [確認_異常系] - potr_internal_send_queue_peek_timed の戻り値が POTR_ERR_TIMEOUT であること。
}

// 満杯キューへの push が POTR_ERR_FULL を返すことの確認
TEST_F(potrSendQueueTest, push_to_full_queue_returns_full)
{
    // Arrange
    NiceMock<Mock_com_util> mock_com_util;
    const uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    size_t i;

    for (i = 0; i < 4; i++)
    {
        ASSERT_EQ(POTR_OK,
                  potr_internal_send_queue_push(&q, 0, 0, payload,
                                       sizeof(payload))); // [状態] - depth=4 のキューを 4 件の push で満杯にする。
                                                          // [状態確認] - potr_internal_send_queue_push の戻り値が POTR_OK であること。
    }

    // Pre-Assert

    // Act
    int rtc_full =
        potr_internal_send_queue_push(&q, 0, 0, payload, sizeof(payload)); // [手順] - 満杯のキューへ 5 件目を push する。

    // Assert
    EXPECT_EQ(POTR_ERR_FULL,
              rtc_full); // [確認_異常系] - 満杯時の potr_internal_send_queue_push の戻り値が POTR_ERR_FULL であること。
}

// 停止済み (running=0) の満杯キューへの push_wait が POTR_ERR_CANCELED を返すことの確認
TEST_F(potrSendQueueTest, push_wait_returns_canceled_when_stopped)
{
    // Arrange
    NiceMock<Mock_com_util> mock_com_util;
    const uint8_t payload[4] = {0x11, 0x22, 0x33, 0x44};
    volatile int running = 0; // [状態] - 実行フラグ running を 0 (停止済み) とする。
    size_t i;

    for (i = 0; i < 4; i++)
    {
        ASSERT_EQ(POTR_OK,
                  potr_internal_send_queue_push(&q, 0, 0, payload, sizeof(payload))); // [状態] - キューを満杯にしておく。
                                                                            // [状態確認] - potr_internal_send_queue_push の戻り値が POTR_OK であること。
    }

    // Pre-Assert

    // Act
    int actual_ret = potr_internal_send_queue_push_wait(&q, 0, 0, payload, sizeof(payload),
                                        &running); // [手順] - 満杯のキューへ push_wait で追加を試みる。

    // Assert
    EXPECT_EQ(POTR_ERR_CANCELED,
              actual_ret); // [確認_異常系] - potr_internal_send_queue_push_wait の戻り値が POTR_ERR_CANCELED であること。
}

// 停止済み (running=0) の空キューからの pop が POTR_ERR_CANCELED を返すことの確認
TEST_F(potrSendQueueTest, pop_returns_canceled_when_stopped)
{
    // Arrange
    NiceMock<Mock_com_util> mock_com_util;
    potr_internal_payload_elem elem;
    volatile int running = 0; // [状態] - エントリのない空キューと、値 0 (停止済み) の実行フラグ running を用意する。

    // Pre-Assert

    // Act
    int actual_ret = potr_internal_send_queue_pop(&q, &elem, &running); // [手順] - 空のキューから pop で取り出しを試みる。

    // Assert
    EXPECT_EQ(POTR_ERR_CANCELED,
              actual_ret); // [確認_異常系] - potr_internal_send_queue_pop の戻り値が POTR_ERR_CANCELED であること。
}
