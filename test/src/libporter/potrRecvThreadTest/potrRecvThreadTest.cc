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
    EXPECT_CALL(mock_cplat, cplat_thread_join(_)).Times(0);
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
    EXPECT_CALL(mock_cplat, cplat_thread_join(ctx.recv_thread[0])).WillOnce(Return());
    // [Pre-Assert確認_正常系] - 生成済み recv を 1 回だけ join すること。
    // [Pre-Assert手順] - join 成功を返す。
    // Act
    potr_internal_comm_recv_thread_stop(&ctx); // [手順] - recv の終了を待つ。
    potr_internal_comm_recv_thread_stop(&ctx); // [手順] - 停止済み recv を再度停止する。
    // Assert
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
    EXPECT_CALL(mock_cplat, cplat_thread_join(_)).Times(0);
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
    EXPECT_CALL(mock_cplat, cplat_thread_join(ctx.recv_thread[0])).WillOnce(Return());
    // [Pre-Assert確認_正常系] - 生成済み recv を 1 回だけ join すること。
    // [Pre-Assert手順] - join 成功を返す。
    // Act
    potr_internal_tcp_recv_thread_stop(&ctx, 0); // [手順] - recv の終了を待つ。
    potr_internal_tcp_recv_thread_stop(&ctx, 0); // [手順] - 停止済み recv を再度停止する。
    // Assert
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
    ON_CALL(mock_cplat, cplat_socket_timed_wait_readable_multi(_, _, _, _, _))
        .WillByDefault(
            [&](const cplat_socket *, size_t, int, unsigned char *ready, cplat_error *)
            {
                ready[0] = 0;
                cplat_atomic_store_i32(&ctx.running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_ERR_TIMEOUT;
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
    ON_CALL(mock_cplat, cplat_socket_timed_wait_readable_multi(_, _, _, _, _))
        .WillByDefault(
            [&](const cplat_socket *, size_t, int, unsigned char *ready, cplat_error *)
            {
                ready[0] = 0;
                cplat_atomic_store_i32(&ctx.running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_ERR_TIMEOUT;
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

// multicast の送信者の受信ループが、周回ごとに監視対象の一覧の送出を判定し、次の送出の時刻までに待機を抑えることの確認
TEST(potrRecvThreadTest, multicast_sender_loop_calls_monitor_tick)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_SENDER;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.n_path = 1;
    cplat_thread_fn entry = nullptr;
    void *entry_arg = nullptr;
    int waited_ms = -1;
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
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([](uint64_t *out) {
        *out = 1000U;
        return CPLAT_OK;
    });
    ON_CALL(mock_cplat, cplat_socket_timed_wait_readable_multi(_, _, _, _, _))
        .WillByDefault(
            [&](const cplat_socket *, size_t, int wait_ms, unsigned char *ready, cplat_error *)
            {
                waited_ms = wait_ms;
                ready[0] = 0;
                cplat_atomic_store_i32(&ctx.running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_ERR_TIMEOUT;
            });
    // [状態] - スレッド本体を捕捉し、時刻 1000 で 1 回の poll タイムアウト後に終了させ、待機の上限を記録する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_monitor_tick(&ctx, 1000U))
        .WillOnce([](potr_context *context, uint64_t now_ms) { context->flow_monitor_next_ms = now_ms + 30U; });
    // [Pre-Assert確認_正常系] - potr_internal_flow_control_monitor_tick() が時刻 1000 で 1 回呼び出されること。
    // [Pre-Assert手順] - 次の周期の送出の時刻を 1030 にする。
    // Act
    int actual_ret = potr_internal_comm_recv_thread_start(&ctx); // [手順] - 受信処理を準備する。
    ASSERT_NE(nullptr, entry);                                   // [状態確認] - スレッド本体を捕捉できたこと。
    entry(entry_arg);                                            // [手順] - 受信ループを同じスレッドで同期実行する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 開始準備に成功すること。
    EXPECT_EQ(30, waited_ms);       // [確認_正常系] - 待機の上限が、次の送出の時刻までの 30 [ms] であること。
}

// multicast の受信者の受信ループは、監視対象の一覧を送出しないことの確認
TEST(potrRecvThreadTest, receiver_loop_does_not_call_monitor_tick)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.n_path = 1;
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
    ON_CALL(mock_cplat, cplat_socket_timed_wait_readable_multi(_, _, _, _, _))
        .WillByDefault(
            [&](const cplat_socket *, size_t, int, unsigned char *ready, cplat_error *)
            {
                ready[0] = 0;
                cplat_atomic_store_i32(&ctx.running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_ERR_TIMEOUT;
            });
    // [状態] - スレッド本体を捕捉し、1 回の poll タイムアウト後に終了させる。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_monitor_tick(_, _))
        .Times(0); // [Pre-Assert確認_正常系] - potr_internal_flow_control_monitor_tick() が呼び出されないこと。
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

// 1:1 TCP で即時 FIN を受理して FIN_ACK を送信した後、受信ループを終了することの確認
// [サブ手順参照 名前=potrRecvThreadTcpDataTest.SetUp]
TEST_F(potrRecvThreadTcpDataTest, fin_fire_stops_tcp_recv_loop)
{
    // Arrange
    potr_packet pkt = {};
    pkt.flags = POTR_FLAG_FIN;

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_packet_parse(_, _, _))
        .WillOnce(DoAll(SetArgPointee<0>(pkt), Return(POTR_OK)));
    // [Pre-Assert確認_正常系] - 先読みした FIN パケットを 1 回解析すること。
    EXPECT_CALL(mock_porter, thread_recv_fin_on_packet(_, _, _))
        .WillOnce(DoAll(SetArgPointee<2>(7U), Return(THREAD_RECV_FIN_FIRE)));
    // [Pre-Assert確認_正常系] - FIN パケットを即時発動として判定すること。
    EXPECT_CALL(mock_porter, thread_recv_fin_fire(_, 7U)).Times(1);
    // [Pre-Assert確認_正常系] - FIN 発動処理 (FIN_ACK 送信等) を 1 回呼び出すこと。
    EXPECT_CALL(mock_cplat, cplat_socket_recv_all(_, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - FIN 発動後は次のパケット読み取りを行わず受信ループを終了すること。

    // Act
    ASSERT_EQ(POTR_OK, potr_internal_tcp_recv_thread_start(&ctx, 0)); // [手順] - 受信処理を準備する。
    // [状態確認] - potr_internal_tcp_recv_thread_start() の戻り値が POTR_OK であること。
    ASSERT_NE(nullptr, entry); // [状態確認] - スレッド本体を捕捉できたこと。
    entry(entry_arg);          // [手順] - 受信ループを同じスレッドで同期実行する。

    // Assert
    EXPECT_EQ(1, cplat_atomic_load_i32(&ctx.tcp_peer_fin_received, CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_正常系] - 相手の正常 FIN 受理フラグが 1 になること。
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.running[0], CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_正常系] - 受信スレッド動作フラグが 0 になること。
}
// [サブ手順参照 名前=potrRecvThreadTcpDataTest.TearDown]

// 1:1 TCP で保留 FIN が DATA 到達により発動した後、受信ループを終了することの確認
// [サブ手順参照 名前=potrRecvThreadTcpDataTest.SetUp]
TEST_F(potrRecvThreadTcpDataTest, pending_fin_triggered_by_data_stops_tcp_recv_loop)
{
    // Arrange
    potr_packet pkt = {};
    pkt.flags = POTR_FLAG_DATA;
    pkt.seq_num = 1U;

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_packet_parse(_, _, _))
        .WillOnce(DoAll(SetArgPointee<0>(pkt), Return(POTR_OK)));
    // [Pre-Assert確認_正常系] - 先読みした DATA パケットを 1 回解析すること。
    EXPECT_CALL(mock_porter, potr_internal_window_recv_push(&ctx.recv_window, _)).WillOnce(Return(POTR_OK));
    // [Pre-Assert確認_正常系] - 受信ウィンドウへの投入が成功すること。
    EXPECT_CALL(mock_porter, potr_internal_window_recv_pop(&ctx.recv_window, _)).WillOnce(Return(POTR_ERR_NOT_FOUND));
    // [Pre-Assert確認_正常系] - 取り出す連続 DATA がない状態で保留 FIN 判定へ進むこと。
    EXPECT_CALL(mock_porter, thread_recv_fin_pending_reached(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(2U), Return(1)));
    // [Pre-Assert確認_正常系] - 保留 FIN の target 通番に追いついたと判定すること。
    EXPECT_CALL(mock_porter, thread_recv_fin_fire(_, 2U)).Times(1);
    // [Pre-Assert確認_正常系] - 保留 FIN の発動処理を 1 回呼び出すこと。
    EXPECT_CALL(mock_cplat, cplat_socket_recv_all(_, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 保留 FIN 発動後は次のパケット読み取りを行わず受信ループを終了すること。

    // Act
    ASSERT_EQ(POTR_OK, potr_internal_tcp_recv_thread_start(&ctx, 0)); // [手順] - 受信処理を準備する。
    // [状態確認] - potr_internal_tcp_recv_thread_start() の戻り値が POTR_OK であること。
    ASSERT_NE(nullptr, entry); // [状態確認] - スレッド本体を捕捉できたこと。
    entry(entry_arg);          // [手順] - 受信ループを同じスレッドで同期実行する。

    // Assert
    EXPECT_EQ(1, cplat_atomic_load_i32(&ctx.tcp_peer_fin_received, CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_正常系] - 相手の正常 FIN 受理フラグが 1 になること。
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.running[0], CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_正常系] - 受信スレッド動作フラグが 0 になること。
}
// [サブ手順参照 名前=potrRecvThreadTcpDataTest.TearDown]

// N:1 モードで遅いピアとして拒否中のセッションからの PING と DATA を破棄し、ピアを作らないことの確認
TEST(potrRecvThreadTest, n1_drops_ping_and_data_from_rejected_slow_peer_without_creating_peer)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint8_t recv_buf[POTR_PACKET_HEADER_SIZE + 64U] = {};
    cplat_local_lock *lock = reinterpret_cast<cplat_local_lock *>(0x200);
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.service.service_id = 10;
    ctx.global.max_payload = 64U;
    ctx.n_path = 1;
    ctx.is_multi_peer = 1;
    ctx.peers_mutex = lock;
    ctx.sock[0] = (cplat_socket)1;
    ctx.recv_buf = recv_buf;
    cplat_thread_fn entry = nullptr;
    void *entry_arg = nullptr;
    int poll_calls = 0;
    potr_packet ping_pkt = {};
    ping_pkt.service_id = 10;
    ping_pkt.session_id = 301U;
    ping_pkt.session_tv_sec = 500;
    ping_pkt.session_tv_nsec = 600;
    ping_pkt.flags = POTR_FLAG_PING;
    potr_packet data_pkt = ping_pkt;
    data_pkt.flags = POTR_FLAG_DATA;
    // [状態] - N:1 サーバーで、拒否中の未知セッション (301, 500.600) から PING と DATA を順に受信する条件を用意する。
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault([&](cplat_thread **out, cplat_thread_fn fn, void *arg) {
            *out = nullptr;
            entry = fn;
            entry_arg = arg;
            return CPLAT_OK;
        });
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([](uint64_t *out) {
        *out = 1500U;
        return CPLAT_OK;
    });
    ON_CALL(mock_cplat, cplat_socket_timed_wait_readable_multi(_, _, _, _, _))
        .WillByDefault([&](const cplat_socket *, size_t, int, unsigned char *ready, cplat_error *) {
            poll_calls++;
            if (poll_calls <= 2)
            {
                ready[0] = 1U;
                return CPLAT_OK;
            }
            cplat_atomic_store_i32(&ctx.running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
            return CPLAT_ERR_TIMEOUT;
        });
    ON_CALL(mock_cplat, cplat_socket_recvfrom(_, _, _, _, _, _))
        .WillByDefault([](cplat_socket, void *, size_t, cplat_ipv4_endpoint *sender, size_t *recv_len, cplat_error *) {
            sender->address = 0x0100007FU;
            sender->port = 0x3930U;
            *recv_len = POTR_PACKET_HEADER_SIZE;
            return CPLAT_OK;
        });
    ON_CALL(mock_cplat, cplat_local_lock_lock(lock)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(lock)).WillByDefault(Return());
    ON_CALL(mock_porter, thread_recv_check_src_addr(_, _)).WillByDefault(Return(1));
    ON_CALL(mock_porter, potr_internal_certificate_receive(_, _, _, _, _)).WillByDefault(Return(POTR_ERR_NOT_FOUND));
    ON_CALL(mock_porter, thread_recv_authenticate_packet(_, _, _, _, _)).WillByDefault(Return(POTR_OK));
    ON_CALL(mock_porter, potr_internal_peer_find_by_session(_, _, _)).WillByDefault(Return(nullptr));
    ON_CALL(mock_porter, potr_internal_flow_control_refused(_, _, _, _)).WillByDefault(Return(0));
    ON_CALL(mock_porter, potr_internal_flow_control_slow_peer_rejected_locked(&ctx, 301U, _, 1500U))
        .WillByDefault(Return(1));
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_packet_parse(_, _, _))
        .WillOnce(DoAll(SetArgPointee<0>(ping_pkt), Return(POTR_OK)))
        .WillOnce(DoAll(SetArgPointee<0>(data_pkt), Return(POTR_OK)));
    // [Pre-Assert確認_正常系] - 1 回目に PING、2 回目に DATA を解析すること。
    EXPECT_CALL(mock_porter, potr_internal_flow_control_slow_peer_rejected_locked(&ctx, 301U, _, 1500U)).Times(2);
    // [Pre-Assert確認_正常系] - PING と DATA の両方で遅いピア拒否の照合が行われること。
    EXPECT_CALL(mock_porter, potr_internal_peer_create(_, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 拒否期間内は新しいピアを作成しないこと。
    EXPECT_CALL(mock_porter, thread_recv_dispatch_packet(_, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 拒否期間内はパケットをディスパッチせず破棄すること。
    // Act
    ASSERT_EQ(POTR_OK, potr_internal_comm_recv_thread_start(&ctx));
    // [状態確認] - 受信スレッド開始準備が成功すること。
    ASSERT_NE(nullptr, entry); // [状態確認] - スレッド本体を捕捉できたこと。
    entry(entry_arg);          // [手順] - 受信ループを実行し、PING と DATA を処理する。
    // Assert
    EXPECT_EQ(3, poll_calls); // [確認_正常系] - PING と DATA の 2 回の受信後に停止したこと。
}

// N:1 モードで拒否期間を過ぎたセッションからの PING で新しいピアを作成することの確認
TEST(potrRecvThreadTest, n1_creates_peer_when_slow_peer_reject_expired)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context new_peer = {};
    uint8_t recv_buf[POTR_PACKET_HEADER_SIZE + 64U] = {};
    cplat_local_lock *lock = reinterpret_cast<cplat_local_lock *>(0x200);
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.service.service_id = 10;
    ctx.global.max_payload = 64U;
    ctx.n_path = 1;
    ctx.is_multi_peer = 1;
    ctx.peers_mutex = lock;
    ctx.sock[0] = (cplat_socket)1;
    ctx.recv_buf = recv_buf;
    new_peer.peer_id = 5U;
    new_peer.active = 1;
    cplat_thread_fn entry = nullptr;
    void *entry_arg = nullptr;
    int poll_calls = 0;
    potr_packet ping_pkt = {};
    ping_pkt.service_id = 10;
    ping_pkt.session_id = 301U;
    ping_pkt.session_tv_sec = 500;
    ping_pkt.session_tv_nsec = 600;
    ping_pkt.flags = POTR_FLAG_PING;
    // [状態] - N:1 サーバーで、拒否期間を過ぎた未知セッション (301, 500.600) から PING を受信する条件を用意する。
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault([&](cplat_thread **out, cplat_thread_fn fn, void *arg) {
            *out = nullptr;
            entry = fn;
            entry_arg = arg;
            return CPLAT_OK;
        });
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([](uint64_t *out) {
        *out = 4000U;
        return CPLAT_OK;
    });
    ON_CALL(mock_cplat, cplat_socket_timed_wait_readable_multi(_, _, _, _, _))
        .WillByDefault([&](const cplat_socket *, size_t, int, unsigned char *ready, cplat_error *) {
            poll_calls++;
            if (poll_calls == 1)
            {
                ready[0] = 1U;
                return CPLAT_OK;
            }
            cplat_atomic_store_i32(&ctx.running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
            return CPLAT_ERR_TIMEOUT;
        });
    ON_CALL(mock_cplat, cplat_socket_recvfrom(_, _, _, _, _, _))
        .WillByDefault([](cplat_socket, void *, size_t, cplat_ipv4_endpoint *sender, size_t *recv_len, cplat_error *) {
            sender->address = 0x0100007FU;
            sender->port = 0x3930U;
            *recv_len = POTR_PACKET_HEADER_SIZE;
            return CPLAT_OK;
        });
    ON_CALL(mock_cplat, cplat_local_lock_lock(lock)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(lock)).WillByDefault(Return());
    ON_CALL(mock_porter, thread_recv_check_src_addr(_, _)).WillByDefault(Return(1));
    ON_CALL(mock_porter, potr_internal_certificate_receive(_, _, _, _, _)).WillByDefault(Return(POTR_ERR_NOT_FOUND));
    ON_CALL(mock_porter, thread_recv_authenticate_packet(_, _, _, _, _)).WillByDefault(Return(POTR_OK));
    ON_CALL(mock_porter, potr_internal_peer_find_by_session(_, _, _)).WillByDefault(Return(nullptr));
    ON_CALL(mock_porter, potr_internal_flow_control_refused(_, _, _, _)).WillByDefault(Return(0));
    ON_CALL(mock_porter, potr_internal_flow_control_slow_peer_rejected_locked(&ctx, 301U, _, 4000U))
        .WillByDefault(Return(0));
    ON_CALL(mock_porter, thread_recv_replay_packet(_, _, _)).WillByDefault(Return(POTR_OK));
    ON_CALL(mock_porter, thread_recv_dispatch_packet(_, _, _, _)).WillByDefault(Return(0));
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_packet_parse(_, _, _))
        .WillOnce(DoAll(SetArgPointee<0>(ping_pkt), Return(POTR_OK)));
    // [Pre-Assert確認_正常系] - PING を 1 回解析すること。
    EXPECT_CALL(mock_porter, potr_internal_peer_create(&ctx, _, 0)).WillOnce(Return(&new_peer));
    // [Pre-Assert確認_正常系] - 拒否されていないため新しいピアを 1 回作成すること。
    EXPECT_CALL(mock_porter, thread_recv_dispatch_packet(_, _, 0, _)).Times(1);
    // [Pre-Assert確認_正常系] - 作成したピアへパケットをディスパッチすること。
    // Act
    ASSERT_EQ(POTR_OK, potr_internal_comm_recv_thread_start(&ctx));
    // [状態確認] - 受信スレッド開始準備が成功すること。
    ASSERT_NE(nullptr, entry); // [状態確認] - スレッド本体を捕捉できたこと。
    entry(entry_arg);          // [手順] - 受信ループを実行し、PING を処理する。
    // Assert
    EXPECT_EQ(301U, new_peer.peer_session_id); // [確認_正常系] - 新しいピアにセッション識別子 301 が設定されること。
    EXPECT_EQ(1, new_peer.peer_session_known); // [確認_正常系] - 新しいピアのセッション既知フラグが 1 になること。
}
