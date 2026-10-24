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

// UDP recv の生成失敗で状態を戻すことの確認
TEST(potrRecvThreadTest, udp_create_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_thread_join(_, _)).Times(0);
    // [Pre-Assert確認_異常系] - 未生成のスレッドを join しないこと。

    // Act
    int actual_ret = potr_internal_comm_recv_thread_start(&ctx); // [手順] - recv の開始を失敗させる。

    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret); // [確認_異常系] - 生成失敗を返すこと。
    EXPECT_EQ(nullptr, ctx.recv_thread[0]);  // [確認_異常系] - ハンドルが NULL であること。
}

// UDP recv の自然終了したスレッドを一度だけ回収することの確認
TEST(potrRecvThreadTest, udp_stop_joins_finished_thread_once)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int token = 0;
    ctx.recv_thread[0] = reinterpret_cast<cplat_thread *>(&token);
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_thread_join(ctx.recv_thread[0], CPLAT_SYNC_WAIT_FOREVER)).WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - 生成済み recv を 1 回だけ join すること。
    // [Pre-Assert手順] - join 成功を返す。
    // Act
    int actual_ret = potr_internal_comm_recv_thread_stop(&ctx); // [手順] - recv の終了を待つ。
    (void)potr_internal_comm_recv_thread_stop(&ctx);            // [手順] - 停止済み recv を再度停止する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);         // [確認_正常系] - 停止成功を返すこと。
    EXPECT_EQ(nullptr, ctx.recv_thread[0]); // [確認_正常系] - ハンドルが NULL であること。
}

// TCP recv の生成失敗で状態を戻すことの確認
TEST(potrRecvThreadTest, tcp_create_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_thread_join(_, _)).Times(0);
    // [Pre-Assert確認_異常系] - 未生成のスレッドを join しないこと。

    // Act
    int actual_ret = potr_internal_tcp_recv_thread_start(&ctx, 0); // [手順] - recv の開始を失敗させる。

    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret); // [確認_異常系] - 生成失敗を返すこと。
    EXPECT_EQ(nullptr, ctx.recv_thread[0]);  // [確認_異常系] - ハンドルが NULL であること。
}

// TCP recv の自然終了したスレッドを一度だけ回収することの確認
TEST(potrRecvThreadTest, tcp_stop_joins_finished_thread_once)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int token = 0;
    ctx.recv_thread[0] = reinterpret_cast<cplat_thread *>(&token);
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_thread_join(ctx.recv_thread[0], CPLAT_SYNC_WAIT_FOREVER)).WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - 生成済み recv を 1 回だけ join すること。
    // [Pre-Assert手順] - join 成功を返す。
    // Act
    int actual_ret = potr_internal_tcp_recv_thread_stop(&ctx, 0); // [手順] - recv の終了を待つ。
    (void)potr_internal_tcp_recv_thread_stop(&ctx, 0);            // [手順] - 停止済み recv を再度停止する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);         // [確認_正常系] - 停止成功を返すこと。
    EXPECT_EQ(nullptr, ctx.recv_thread[0]); // [確認_正常系] - ハンドルが NULL であること。
}

TEST(potrRecvThreadTest, poll_timeout_calls_service_monitor)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.n_path = 1;
    ctx.is_multi_peer = 0;
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
            });
    ON_CALL(mock_porter, thread_recv_slot_init_ctx(_, _))
        .WillByDefault([](thread_recv_slot *slot, potr_context *context) { slot->ctx = context; });
    ON_CALL(mock_cplat, cplat_socket_wait_readable_multi(_, _, _, _, _))
        .WillByDefault(
            [&](const cplat_socket *, size_t, int, unsigned char *ready, cplat_error *)
            {
                ready[0] = 0;
                cplat_atomic_store_i32(&ctx.running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_OK;
            });
    // [状態] - スレッド本体を捕捉し、1 回の poll タイムアウト後に終了させる。
    // Pre-Assert
    EXPECT_CALL(mock_porter, thread_recv_health_check_peers(&ctx))
        .Times(0); // [Pre-Assert確認_正常系] - N:1 ではピア監視を 1 回呼び、それ以外では呼ばないこと。
    EXPECT_CALL(mock_porter, thread_recv_health_check_service(_))
        .Times(1); // [Pre-Assert確認_正常系] - 1:1 ではサービス監視を 1 回呼び、N:1 では呼ばないこと。
    // Act
    int actual_ret = potr_internal_comm_recv_thread_start(&ctx); // [手順] - 受信処理を準備する。
    ASSERT_NE(nullptr, entry);                                   // [状態確認] - スレッド本体を捕捉できたこと。
    entry(entry_arg);                                            // [手順] - 受信ループを同じスレッドで同期実行する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 開始準備に成功すること。
}

TEST(potrRecvThreadTest, poll_timeout_calls_peer_monitor)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.n_path = 1;
    ctx.is_multi_peer = 1;
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
            });
    ON_CALL(mock_porter, thread_recv_slot_init_ctx(_, _))
        .WillByDefault([](thread_recv_slot *slot, potr_context *context) { slot->ctx = context; });
    ON_CALL(mock_cplat, cplat_socket_wait_readable_multi(_, _, _, _, _))
        .WillByDefault(
            [&](const cplat_socket *, size_t, int, unsigned char *ready, cplat_error *)
            {
                ready[0] = 0;
                cplat_atomic_store_i32(&ctx.running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_OK;
            });
    // [状態] - スレッド本体を捕捉し、1 回の poll タイムアウト後に終了させる。
    // Pre-Assert
    EXPECT_CALL(mock_porter, thread_recv_health_check_peers(&ctx))
        .Times(1); // [Pre-Assert確認_正常系] - N:1 ではピア監視を 1 回呼び、それ以外では呼ばないこと。
    EXPECT_CALL(mock_porter, thread_recv_health_check_service(_))
        .Times(0); // [Pre-Assert確認_正常系] - 1:1 ではサービス監視を 1 回呼び、N:1 では呼ばないこと。
    // Act
    int actual_ret = potr_internal_comm_recv_thread_start(&ctx); // [手順] - 受信処理を準備する。
    ASSERT_NE(nullptr, entry);                                   // [状態確認] - スレッド本体を捕捉できたこと。
    entry(entry_arg);                                            // [手順] - 受信ループを同じスレッドで同期実行する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 開始準備に成功すること。
}
