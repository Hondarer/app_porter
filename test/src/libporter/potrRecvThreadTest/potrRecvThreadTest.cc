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

class potrRecvThreadTcpDataTest : public Test
{
  protected:
    // [サブ手順 名前=potrRecvThreadTcpDataTest.SetUp]
    void SetUp() override
    {
        memset(&ctx, 0, sizeof(ctx));
        recv_buf.assign(POTR_PACKET_HEADER_SIZE + 1400U, 0U);
        first_pkt_buf.assign(POTR_PACKET_HEADER_SIZE, 0U);
        ctx.role = POTR_ROLE_SENDER;            // [状態] - 受理セッションの照合を行わない送信側とする。
        ctx.service.type = POTR_TYPE_TCP_BIDIR; // [状態] - TCP_BIDIR とする。
        ctx.n_path = 1;                         // [状態] - path 数を 1 とする。
        ctx.tcp_conn_fd[0] = (cplat_socket)7;   // [状態] - path 0 を接続済みとする。
        ctx.tcp_recv_buf[0] = recv_buf.data();  // [状態] - 受信バッファーを設定する。
        ctx.tcp_first_pkt_buf[0] = first_pkt_buf.data();
        ctx.tcp_first_pkt_len[0] = POTR_PACKET_HEADER_SIZE; // [状態] - ペイロードのない 1 パケットを先読み済みとする。
        ctx.recv_window.base_seq = 100U;                    // [状態] - 受信ウィンドウ先頭の通番を 100 とする。
        delegate_real_cplat_local_lock_create(&ctx.tcp_recv_mutex);
        delegate_real_cplat_local_lock_create(&ctx.recv_window_mutex);
        ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
            .WillByDefault(
                [this](cplat_thread **out, cplat_thread_fn fn, void *arg)
                {
                    *out = nullptr;
                    entry = fn;
                    entry_arg = arg;
                    return CPLAT_OK;
                }); // [状態] - スレッド本体を捕捉し、同じスレッドで実行できるようにする。
        ON_CALL(mock_porter, thread_recv_slot_init_ctx(_, _))
            .WillByDefault([](thread_recv_slot *slot, potr_context *context) { slot->ctx = context; });
        ON_CALL(mock_porter, thread_recv_session_adopt(_, _)).WillByDefault(Return(1));
        ON_CALL(mock_porter, potr_internal_window_recv_push(&ctx.recv_window, _))
            .WillByDefault(Return(POTR_ERR_OUT_OF_WINDOW)); // [状態] - DATA が受信ウィンドウの範囲外となるものとする。
        ON_CALL(mock_cplat, cplat_socket_recv_all(_, _, _, _))
            .WillByDefault(Return(CPLAT_ERR_UNKNOWN)); // [状態] - 2 パケット目の読み取りは失敗するものとする。
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrRecvThreadTcpDataTest.TearDown]
    void TearDown() override
    {
        delegate_real_cplat_local_lock_dispose(ctx.recv_window_mutex);
        delegate_real_cplat_local_lock_dispose(ctx.tcp_recv_mutex);
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrRecvThreadTcpDataTest.receiveData]
    void receiveData(uint32_t seq_num)
    {
        potr_packet pkt = {};
        pkt.flags = POTR_FLAG_DATA;
        pkt.seq_num = seq_num;
        EXPECT_CALL(mock_porter, potr_internal_packet_parse(_, _, _))
            .WillOnce(DoAll(SetArgPointee<0>(pkt), Return(POTR_OK)));
        // [Pre-Assert確認_正常系] - 先読みしたパケットを 1 回解析すること。
        // [Pre-Assert手順] - 指定の通番の DATA を返す。
        ASSERT_EQ(POTR_OK, potr_internal_tcp_recv_thread_start(&ctx, 0)); // [手順] - 受信処理を準備する。
        // [状態確認] - potr_internal_tcp_recv_thread_start() の戻り値が POTR_OK であること。
        ASSERT_NE(nullptr, entry); // [状態確認] - スレッド本体を捕捉できたこと。
        entry(entry_arg);          // [手順] - 受信ループを同じスレッドで同期実行する。
    }
    // [サブ手順終了]

    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx;
    std::vector<uint8_t> recv_buf;
    std::vector<uint8_t> first_pkt_buf;
    cplat_thread_fn entry = nullptr;
    void *entry_arg = nullptr;
};

// TCP で受信ウィンドウより先の DATA を受信した場合、欠番として以降の読み取りをやめて切断することの確認
// [サブ手順参照 名前=potrRecvThreadTcpDataTest.SetUp]
TEST_F(potrRecvThreadTcpDataTest, data_beyond_window_disconnects_stream)
{
    // Arrange

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_recv_all(_, _, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - 範囲外の DATA の後は、次のパケットを読み取らないこと。
    EXPECT_CALL(mock_porter, potr_internal_window_recv_pop(_, _)).Times(0);
    // [Pre-Assert確認_異常系] - 受信ウィンドウから配信しないこと。

    // Act
    // [サブ手順参照 名前=potrRecvThreadTcpDataTest.receiveData]
    receiveData(100U + 64U); // [手順] - ウィンドウ先頭より 64 先の通番の DATA を受信する。

    // Assert
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.running[0], CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_異常系] - 受信ループが終了し、実行フラグが 0 であること。
}
// [サブ手順参照 名前=potrRecvThreadTcpDataTest.TearDown]

// TCP で他の path から受信済みの DATA を受信した場合、重複として破棄して読み取りを続けることの確認
// [サブ手順参照 名前=potrRecvThreadTcpDataTest.SetUp]
TEST_F(potrRecvThreadTcpDataTest, duplicate_data_is_skipped_and_reading_continues)
{
    // Arrange

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_recv_all(_, _, _, _)).Times(1);
    // [Pre-Assert確認_正常系] - 重複の DATA の後も、次のパケットの読み取りを 1 回行うこと。
    EXPECT_CALL(mock_porter, potr_internal_window_recv_pop(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - 重複の DATA を配信しないこと。

    // Act
    // [サブ手順参照 名前=potrRecvThreadTcpDataTest.receiveData]
    receiveData(100U - 1U); // [手順] - ウィンドウ先頭より 1 前の通番の DATA を受信する。

    // Assert
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.running[0], CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_正常系] - 読み取り失敗で受信ループが終了し、実行フラグが 0 であること。
}
// [サブ手順参照 名前=potrRecvThreadTcpDataTest.TearDown]
