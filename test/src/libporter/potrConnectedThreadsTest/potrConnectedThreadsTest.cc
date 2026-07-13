#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_com_util.h>
#include <mock_porter.h>

#include <porter/porter_const.h>
#include <porter/potrContext.h>
#include <porter/thread/potrConnectedThreads.h>
#include <porter/thread/potrHealthThread.h>

#include <string.h>

using namespace testing;

struct ConnectedThreadsCallState
{
    int send_start_calls;
    int send_stop_calls;
    int recv_start_calls;
    int tcp_send_ping_calls;
    int health_start_calls;
    int close_conn_calls;
    int join_recv_calls;
    int set_ping_state_calls;
    int last_set_ping_path;
    int last_set_ping_state;
    int last_tcp_send_ping_path;
    int recv_start_result;
    int tcp_send_ping_result;
    int health_start_result;
};

static ConnectedThreadsCallState g_calls;

static int fake_send_start(PotrContext *ctx)
{
    g_calls.send_start_calls++;
    ctx->send_thread_running = 1;
    return POTR_SUCCESS;
}

static void fake_send_stop(PotrContext *ctx)
{
    g_calls.send_stop_calls++;
    ctx->send_thread_running = 0;
}

static int fake_recv_start(PotrContext *ctx, int path_idx)
{
    g_calls.recv_start_calls++;
    if (g_calls.recv_start_result == POTR_SUCCESS)
    {
        ctx->running[path_idx] = 1;
    }
    return g_calls.recv_start_result;
}

int potr_tcp_send_ping_now(PotrContext *ctx, int path_idx)
{
    (void)ctx;
    g_calls.tcp_send_ping_calls++;
    g_calls.last_tcp_send_ping_path = path_idx;
    return g_calls.tcp_send_ping_result;
}

static int fake_health_start(PotrContext *ctx, int path_idx)
{
    (void)ctx;
    (void)path_idx;
    g_calls.health_start_calls++;
    return g_calls.health_start_result;
}

static void fake_close_conn(PotrContext *ctx, int path_idx)
{
    g_calls.close_conn_calls++;
    ctx->tcp_conn_fd[path_idx] = POTR_INVALID_SOCKET;
}

static void fake_join_recv(PotrContext *ctx, int path_idx)
{
    (void)ctx;
    (void)path_idx;
    g_calls.join_recv_calls++;
}

static void fake_set_path_ping_state(PotrContext *ctx, int path_idx, uint8_t next_state)
{
    g_calls.set_ping_state_calls++;
    g_calls.last_set_ping_path = path_idx;
    g_calls.last_set_ping_state = (int)next_state;
    ctx->path_ping_state[path_idx] = next_state;
}

class potrConnectedThreadsTest : public Test
{
  protected:
    NiceMock<Mock_com_util> mock_com_util;
    NiceMock<Mock_porter> mock_porter;

    void SetUp() override
    {
        ON_CALL(mock_com_util, _com_util_tracer_writef(_, _, _, _)).WillByDefault(Return(0));

        memset(&ctx, 0, sizeof(ctx));
        memset(&g_calls, 0, sizeof(g_calls));

        ctx.service.service_id = 42;
        ctx.service.type = POTR_TYPE_TCP_BIDIR;
        ctx.role = POTR_ROLE_SENDER;
        ctx.tcp_conn_fd[0] = 123;
        ctx.tcp_conn_fd[1] = 456;

        g_calls.recv_start_result = POTR_SUCCESS;
        g_calls.tcp_send_ping_result = POTR_SUCCESS;
        g_calls.health_start_result = POTR_SUCCESS;
    }

    PotrConnectedThreadsOps make_ops()
    {
        PotrConnectedThreadsOps ops = {fake_send_start, fake_send_stop, fake_recv_start,         fake_health_start,
                                       fake_close_conn, fake_join_recv, fake_set_path_ping_state};
        return ops;
    }

    PotrContext ctx;
};

// recv 開始失敗時に、この呼び出しで開始した send スレッドが停止されることの確認
TEST_F(potrConnectedThreadsTest, recv_failure_stops_send_started_by_this_call)
{
    // Arrange
    PotrConnectedThreadsOps ops = make_ops(); // [状態] - fake ops 一式を用意する (send スレッドは未起動)。

    // Pre-Assert
    g_calls.recv_start_result = POTR_ERROR; // [Pre-Assert手順] - recv 開始 fake から POTR_ERROR を返却する。

    // Act
    int rtc = potr_start_connected_threads(&ctx, 0, &ops); // [手順] - primary path (0) で接続時スレッド群を開始する。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc);                // [確認_異常系] - POTR_ERROR が返ること。
    EXPECT_EQ(1, g_calls.send_start_calls);    // [確認_異常系] - send 開始が 1 回呼ばれること。
    EXPECT_EQ(1, g_calls.recv_start_calls);    // [確認_異常系] - recv 開始が 1 回呼ばれること。
    EXPECT_EQ(1, g_calls.send_stop_calls);     // [確認_異常系] - この呼び出しで開始した send が停止されること。
    EXPECT_EQ(1, g_calls.close_conn_calls);    // [確認_異常系] - 接続が close されること。
    EXPECT_EQ(0, g_calls.join_recv_calls);     // [確認_異常系] - 未起動の recv は join されないこと。
    EXPECT_EQ(0, g_calls.tcp_send_ping_calls); // [確認_異常系] - bootstrap ping まで進まないこと。
    EXPECT_EQ(0, g_calls.health_start_calls);  // [確認_異常系] - health 開始まで進まないこと。
    EXPECT_EQ(POTR_INVALID_SOCKET, ctx.tcp_conn_fd[0]); // [確認_異常系] - path 0 のソケットが無効化されること。
}

// recv 開始失敗時に、既存の send スレッドが停止されないことの確認
TEST_F(potrConnectedThreadsTest, recv_failure_keeps_preexisting_send_thread_running)
{
    // Arrange
    PotrConnectedThreadsOps ops = make_ops();
    ctx.send_thread_running = 1; // [状態] - send スレッドが既に起動済みの状態とする。

    // Pre-Assert
    g_calls.recv_start_result = POTR_ERROR; // [Pre-Assert手順] - recv 開始 fake から POTR_ERROR を返却する。

    // Act
    int rtc = potr_start_connected_threads(&ctx, 0, &ops); // [手順] - primary path (0) で接続時スレッド群を開始する。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc);                // [確認_異常系] - POTR_ERROR が返ること。
    EXPECT_EQ(0, g_calls.send_start_calls);    // [確認_異常系] - 既存 send があるため send 開始が呼ばれないこと。
    EXPECT_EQ(1, g_calls.recv_start_calls);    // [確認_異常系] - recv 開始が 1 回呼ばれること。
    EXPECT_EQ(0, g_calls.send_stop_calls);     // [確認_異常系] - 既存の send スレッドが停止されないこと。
    EXPECT_EQ(1, g_calls.close_conn_calls);    // [確認_異常系] - 接続が close されること。
    EXPECT_EQ(0, g_calls.tcp_send_ping_calls); // [確認_異常系] - bootstrap ping まで進まないこと。
}

// bootstrap ping 失敗時に recv と新規 send スレッドがロールバックされることの確認
TEST_F(potrConnectedThreadsTest, bootstrap_ping_failure_rolls_back_recv_and_new_send_thread)
{
    // Arrange
    PotrConnectedThreadsOps ops = make_ops(); // [状態] - fake ops 一式を用意する (send スレッドは未起動)。

    // Pre-Assert
    g_calls.tcp_send_ping_result =
        POTR_ERROR; // [Pre-Assert手順] - bootstrap ping (potr_tcp_send_ping_now) から POTR_ERROR を返却する。

    // Act
    int rtc = potr_start_connected_threads(&ctx, 0, &ops); // [手順] - primary path (0) で接続時スレッド群を開始する。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc);                         // [確認_異常系] - POTR_ERROR が返ること。
    EXPECT_EQ(1, g_calls.send_start_calls);             // [確認_異常系] - send 開始が 1 回呼ばれること。
    EXPECT_EQ(1, g_calls.recv_start_calls);             // [確認_異常系] - recv 開始が 1 回呼ばれること。
    EXPECT_EQ(1, g_calls.tcp_send_ping_calls);          // [確認_異常系] - bootstrap ping が 1 回呼ばれること。
    EXPECT_EQ(0, g_calls.health_start_calls);           // [確認_異常系] - health 開始まで進まないこと。
    EXPECT_EQ(1, g_calls.close_conn_calls);             // [確認_異常系] - 接続が close されること。
    EXPECT_EQ(1, g_calls.join_recv_calls);              // [確認_異常系] - 起動済みの recv が join されること。
    EXPECT_EQ(1, g_calls.send_stop_calls);              // [確認_異常系] - 新規に開始した send が停止されること。
    EXPECT_EQ(0, ctx.running[0]);                       // [確認_異常系] - path 0 の running フラグが下がること。
    EXPECT_EQ(POTR_INVALID_SOCKET, ctx.tcp_conn_fd[0]); // [確認_異常系] - path 0 のソケットが無効化されること。
}

// health スレッド開始失敗時に recv と新規 send スレッドがロールバックされることの確認
TEST_F(potrConnectedThreadsTest, health_failure_rolls_back_recv_and_new_send_thread)
{
    // Arrange
    PotrConnectedThreadsOps ops = make_ops(); // [状態] - fake ops 一式を用意する (send スレッドは未起動)。

    // Pre-Assert
    g_calls.health_start_result = POTR_ERROR; // [Pre-Assert手順] - health 開始 fake から POTR_ERROR を返却する。

    // Act
    int rtc = potr_start_connected_threads(&ctx, 0, &ops); // [手順] - primary path (0) で接続時スレッド群を開始する。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc);                         // [確認_異常系] - POTR_ERROR が返ること。
    EXPECT_EQ(1, g_calls.send_start_calls);             // [確認_異常系] - send 開始が 1 回呼ばれること。
    EXPECT_EQ(1, g_calls.recv_start_calls);             // [確認_異常系] - recv 開始が 1 回呼ばれること。
    EXPECT_EQ(1, g_calls.tcp_send_ping_calls);          // [確認_異常系] - bootstrap ping が 1 回呼ばれること。
    EXPECT_EQ(1, g_calls.health_start_calls);           // [確認_異常系] - health 開始が 1 回呼ばれること。
    EXPECT_EQ(1, g_calls.close_conn_calls);             // [確認_異常系] - 接続が close されること。
    EXPECT_EQ(1, g_calls.join_recv_calls);              // [確認_異常系] - 起動済みの recv が join されること。
    EXPECT_EQ(1, g_calls.send_stop_calls);              // [確認_異常系] - 新規に開始した send が停止されること。
    EXPECT_EQ(0, ctx.running[0]);                       // [確認_異常系] - path 0 の running フラグが下がること。
    EXPECT_EQ(POTR_INVALID_SOCKET, ctx.tcp_conn_fd[0]); // [確認_異常系] - path 0 のソケットが無効化されること。
}

// health スレッド開始失敗時に、既存の send スレッドが停止されないことの確認
TEST_F(potrConnectedThreadsTest, health_failure_keeps_preexisting_send_thread_running)
{
    // Arrange
    PotrConnectedThreadsOps ops = make_ops();
    ctx.send_thread_running = 1; // [状態] - send スレッドが既に起動済みの状態とする。

    // Pre-Assert
    g_calls.health_start_result = POTR_ERROR; // [Pre-Assert手順] - health 開始 fake から POTR_ERROR を返却する。

    // Act
    int rtc = potr_start_connected_threads(&ctx, 0, &ops); // [手順] - primary path (0) で接続時スレッド群を開始する。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc);                // [確認_異常系] - POTR_ERROR が返ること。
    EXPECT_EQ(0, g_calls.send_start_calls);    // [確認_異常系] - 既存 send があるため send 開始が呼ばれないこと。
    EXPECT_EQ(1, g_calls.recv_start_calls);    // [確認_異常系] - recv 開始が 1 回呼ばれること。
    EXPECT_EQ(1, g_calls.tcp_send_ping_calls); // [確認_異常系] - bootstrap ping が 1 回呼ばれること。
    EXPECT_EQ(1, g_calls.health_start_calls);  // [確認_異常系] - health 開始が 1 回呼ばれること。
    EXPECT_EQ(1, g_calls.close_conn_calls);    // [確認_異常系] - 接続が close されること。
    EXPECT_EQ(1, g_calls.join_recv_calls);     // [確認_異常系] - 起動済みの recv が join されること。
    EXPECT_EQ(0, g_calls.send_stop_calls);     // [確認_異常系] - 既存の send スレッドが停止されないこと。
}

// 非 primary path では send スレッドが操作されないことの確認
TEST_F(potrConnectedThreadsTest, non_primary_path_does_not_touch_send_thread)
{
    // Arrange
    PotrConnectedThreadsOps ops = make_ops(); // [状態] - fake ops 一式を用意する。

    // Pre-Assert
    g_calls.recv_start_result = POTR_ERROR; // [Pre-Assert手順] - recv 開始 fake から POTR_ERROR を返却する。

    // Act
    int rtc =
        potr_start_connected_threads(&ctx, 1, &ops); // [手順] - 非 primary path (1) で接続時スレッド群を開始する。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc);                         // [確認_異常系] - POTR_ERROR が返ること。
    EXPECT_EQ(0, g_calls.send_start_calls);             // [確認_異常系] - send 開始が呼ばれないこと。
    EXPECT_EQ(1, g_calls.recv_start_calls);             // [確認_異常系] - recv 開始が 1 回呼ばれること。
    EXPECT_EQ(0, g_calls.send_stop_calls);              // [確認_異常系] - send 停止が呼ばれないこと。
    EXPECT_EQ(1, g_calls.close_conn_calls);             // [確認_異常系] - 接続が close されること。
    EXPECT_EQ(0, g_calls.tcp_send_ping_calls);          // [確認_異常系] - bootstrap ping まで進まないこと。
    EXPECT_EQ(POTR_INVALID_SOCKET, ctx.tcp_conn_fd[1]); // [確認_異常系] - path 1 のソケットが無効化されること。
}

// 全段成功時に ping 状態が設定され、ロールバックが発生しないことの確認
TEST_F(potrConnectedThreadsTest, success_sets_ping_state_without_rollback)
{
    // Arrange
    PotrConnectedThreadsOps ops = make_ops(); // [状態] - fake ops 一式を用意する (すべて成功を返す既定)。

    // Pre-Assert

    // Act
    int rtc = potr_start_connected_threads(&ctx, 0, &ops); // [手順] - primary path (0) で接続時スレッド群を開始する。

    // Assert
    EXPECT_EQ(POTR_SUCCESS, rtc);                  // [確認_正常系] - POTR_SUCCESS が返ること。
    EXPECT_EQ(1, g_calls.send_start_calls);        // [確認_正常系] - send 開始が 1 回呼ばれること。
    EXPECT_EQ(1, g_calls.recv_start_calls);        // [確認_正常系] - recv 開始が 1 回呼ばれること。
    EXPECT_EQ(1, g_calls.tcp_send_ping_calls);     // [確認_正常系] - bootstrap ping が 1 回呼ばれること。
    EXPECT_EQ(0, g_calls.last_tcp_send_ping_path); // [確認_正常系] - bootstrap ping の対象が path 0 であること。
    EXPECT_EQ(1, g_calls.health_start_calls);      // [確認_正常系] - health 開始が 1 回呼ばれること。
    EXPECT_EQ(0, g_calls.close_conn_calls);        // [確認_正常系] - close が呼ばれないこと。
    EXPECT_EQ(0, g_calls.join_recv_calls);         // [確認_正常系] - join が呼ばれないこと。
    EXPECT_EQ(0, g_calls.send_stop_calls);         // [確認_正常系] - send 停止が呼ばれないこと。
    EXPECT_EQ(1, g_calls.set_ping_state_calls);    // [確認_正常系] - ping 状態設定が 1 回呼ばれること。
    EXPECT_EQ(0, g_calls.last_set_ping_path);      // [確認_正常系] - 設定対象が path 0 であること。
    EXPECT_EQ((int)POTR_PING_STATE_UNDEFINED,
              g_calls.last_set_ping_state); // [確認_正常系] - 設定値が POTR_PING_STATE_UNDEFINED であること。
    EXPECT_EQ(POTR_PING_STATE_UNDEFINED,
              ctx.path_ping_state[0]); // [確認_正常系] - path 0 の ping 状態が UNDEFINED になること。
}
