#include <cplat/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/potr_context.h>
#include <porter/thread/potr_connected_threads.h>
#include <porter/thread/potr_health_thread.h>

#include <string.h>
#include <vector>

using namespace testing;

struct ConnectedThreadsCallState
{
    int send_start_calls;
    int send_stop_calls;
    int recv_start_calls;
    int tcp_send_ping_calls;
    int health_start_calls;
    int request_close_calls;
    int join_recv_calls;
    int stop_health_calls;
    int release_conn_calls;
    int set_ping_state_calls;
    int last_set_ping_path;
    int last_set_ping_state;
    int last_tcp_send_ping_path;
    int send_start_result;
    int recv_start_result;
    int ping_state_at_recv_start;
    int tcp_send_ping_result;
    int health_start_result;
    uint64_t last_request_close_gen;
    uint64_t last_release_conn_gen;
};

static ConnectedThreadsCallState s_calls;
static std::vector<const char *> s_order;

static int fake_send_start(potr_context *ctx)
{
    s_calls.send_start_calls++;
    s_order.push_back("send_start");
    if (s_calls.send_start_result == POTR_OK)
    {
        cplat_atomic_store_i32(&ctx->send_thread_running, 1, CPLAT_MEMORY_ORDER_RELAXED);
    }
    return s_calls.send_start_result;
}

static void fake_send_stop(potr_context *ctx)
{
    s_calls.send_stop_calls++;
    s_order.push_back("send_stop");
    cplat_atomic_store_i32(&ctx->send_thread_running, 0, CPLAT_MEMORY_ORDER_RELAXED);
}

static int fake_recv_start(potr_context *ctx, int path_idx)
{
    s_calls.recv_start_calls++;
    s_order.push_back("recv_start");
    s_calls.ping_state_at_recv_start =
        (int)cplat_atomic_load_u8(&ctx->path_ping_state[path_idx], CPLAT_MEMORY_ORDER_RELAXED);
    if (s_calls.recv_start_result == POTR_OK)
    {
        cplat_atomic_store_i32(&ctx->running[path_idx], 1, CPLAT_MEMORY_ORDER_RELAXED);
    }
    return s_calls.recv_start_result;
}

int potr_internal_tcp_send_ping_now(potr_context *ctx, int path_idx)
{
    (void)ctx;
    s_calls.tcp_send_ping_calls++;
    s_order.push_back("tcp_send_ping");
    s_calls.last_tcp_send_ping_path = path_idx;
    return s_calls.tcp_send_ping_result;
}

static int fake_health_start(potr_context *ctx, int path_idx)
{
    (void)ctx;
    (void)path_idx;
    s_calls.health_start_calls++;
    s_order.push_back("health_start");
    return s_calls.health_start_result;
}

static void fake_request_close(potr_context *ctx, int path_idx, uint64_t generation)
{
    s_calls.request_close_calls++;
    s_calls.last_request_close_gen = generation;
    s_order.push_back("request_close");
    cplat_atomic_store_i32(&ctx->running[path_idx], 0, CPLAT_MEMORY_ORDER_RELAXED);
}

static void fake_join_recv(potr_context *ctx, int path_idx)
{
    (void)ctx;
    (void)path_idx;
    s_calls.join_recv_calls++;
    s_order.push_back("join_recv");
}

static void fake_stop_health(potr_context *ctx, int path_idx)
{
    (void)ctx;
    (void)path_idx;
    s_calls.stop_health_calls++;
    s_order.push_back("stop_health");
}

static void fake_release_conn(potr_context *ctx, int path_idx, uint64_t generation)
{
    s_calls.release_conn_calls++;
    s_calls.last_release_conn_gen = generation;
    s_order.push_back("release_conn");
    ctx->tcp_conn_fd[path_idx] = CPLAT_INVALID_SOCKET;
}

static void fake_set_path_ping_state(potr_context *ctx, int path_idx, uint8_t next_state)
{
    s_calls.set_ping_state_calls++;
    s_calls.last_set_ping_path = path_idx;
    s_calls.last_set_ping_state = (int)next_state;
    cplat_atomic_store_u8(&ctx->path_ping_state[path_idx], next_state, CPLAT_MEMORY_ORDER_RELAXED);
}

class potrConnectedThreadsTest : public Test
{
  protected:
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;

    void SetUp() override
    {
        ON_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _)).WillByDefault(Return(0));
        memset(&ctx, 0, sizeof(ctx));
        memset(&s_calls, 0, sizeof(s_calls));
        s_order.clear();

        ctx.service.service_id = 42;
        ctx.service.type = POTR_TYPE_TCP_BIDIR;
        ctx.role = POTR_ROLE_SENDER;
        ctx.tcp_conn_fd[0] = 123;
        ctx.tcp_conn_fd[1] = 456;

        s_calls.send_start_result = POTR_OK;
        s_calls.recv_start_result = POTR_OK;
        s_calls.tcp_send_ping_result = POTR_OK;
        s_calls.health_start_result = POTR_OK;
    }

    potr_internal_connected_threads_ops make_ops()
    {
        potr_internal_connected_threads_ops ops = {fake_send_start,   fake_send_stop,   fake_recv_start,
                                                   fake_health_start, fake_request_close, fake_join_recv,
                                                   fake_stop_health,  fake_release_conn, fake_set_path_ping_state};
        return ops;
    }

    potr_context ctx;
};

// send 開始失敗時に、未起動の recv/health を回収せず対象世代を解放することの確認
TEST_F(potrConnectedThreadsTest, send_failure_releases_conn_without_stopping_unstarted_threads)
{
    potr_internal_connected_threads_ops ops = make_ops();
    s_calls.send_start_result = POTR_ERR_OUT_OF_MEMORY;

    int actual_ret = potr_internal_start_connected_threads(&ctx, 0, 11U, &ops);

    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, actual_ret);
    EXPECT_EQ(1, s_calls.send_start_calls);
    EXPECT_EQ(0, s_calls.send_stop_calls);
    EXPECT_EQ(0, s_calls.recv_start_calls);
    EXPECT_EQ(1, s_calls.request_close_calls);
    EXPECT_EQ(0, s_calls.join_recv_calls);
    EXPECT_EQ(0, s_calls.stop_health_calls);
    EXPECT_EQ(1, s_calls.release_conn_calls);
    EXPECT_EQ(11U, s_calls.last_request_close_gen);
    EXPECT_EQ(11U, s_calls.last_release_conn_gen);
    EXPECT_EQ(CPLAT_INVALID_SOCKET, ctx.tcp_conn_fd[0]);
}

// recv 開始失敗時に、この呼び出しで開始した send スレッドを停止し、対象世代を解放することの確認
TEST_F(potrConnectedThreadsTest, recv_failure_stops_send_started_by_this_call)
{
    potr_internal_connected_threads_ops ops = make_ops();
    s_calls.recv_start_result = POTR_ERR_IO;

    int actual_ret = potr_internal_start_connected_threads(&ctx, 0, 12U, &ops);

    EXPECT_EQ(POTR_ERR_IO, actual_ret);
    EXPECT_EQ(1, s_calls.send_start_calls);
    EXPECT_EQ(1, s_calls.recv_start_calls);
    EXPECT_EQ(1, s_calls.send_stop_calls);
    EXPECT_EQ(1, s_calls.request_close_calls);
    EXPECT_EQ(0, s_calls.join_recv_calls);
    EXPECT_EQ(0, s_calls.stop_health_calls);
    EXPECT_EQ(1, s_calls.release_conn_calls);
    EXPECT_EQ(CPLAT_INVALID_SOCKET, ctx.tcp_conn_fd[0]);
}

// recv 開始失敗時に、既存の send スレッドが停止されないことの確認
TEST_F(potrConnectedThreadsTest, recv_failure_keeps_preexisting_send_thread_running)
{
    potr_internal_connected_threads_ops ops = make_ops();
    cplat_atomic_store_i32(&ctx.send_thread_running, 1, CPLAT_MEMORY_ORDER_RELAXED);
    s_calls.recv_start_result = POTR_ERR_IO;

    int actual_ret = potr_internal_start_connected_threads(&ctx, 0, 13U, &ops);

    EXPECT_EQ(POTR_ERR_IO, actual_ret);
    EXPECT_EQ(0, s_calls.send_start_calls);
    EXPECT_EQ(1, s_calls.recv_start_calls);
    EXPECT_EQ(0, s_calls.send_stop_calls);
    EXPECT_EQ(1, s_calls.request_close_calls);
    EXPECT_EQ(1, s_calls.release_conn_calls);
}

// bootstrap ping 失敗時に request_close -> join_recv -> send_stop -> release_conn の順で回収することの確認
TEST_F(potrConnectedThreadsTest, bootstrap_ping_failure_rolls_back_in_order)
{
    potr_internal_connected_threads_ops ops = make_ops();
    s_calls.tcp_send_ping_result = POTR_ERR_DISCONNECTED;

    int actual_ret = potr_internal_start_connected_threads(&ctx, 0, 14U, &ops);

    EXPECT_EQ(POTR_ERR_DISCONNECTED, actual_ret);
    EXPECT_EQ(1, s_calls.request_close_calls);
    EXPECT_EQ(1, s_calls.join_recv_calls);
    EXPECT_EQ(0, s_calls.stop_health_calls);
    EXPECT_EQ(1, s_calls.send_stop_calls);
    EXPECT_EQ(1, s_calls.release_conn_calls);
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.running[0], CPLAT_MEMORY_ORDER_RELAXED));
    EXPECT_EQ(CPLAT_INVALID_SOCKET, ctx.tcp_conn_fd[0]);
}

// health スレッド開始失敗時に recv と新規 send スレッドがロールバックされることの確認
TEST_F(potrConnectedThreadsTest, health_failure_rolls_back_recv_and_new_send_thread)
{
    potr_internal_connected_threads_ops ops = make_ops();
    s_calls.health_start_result = POTR_ERR_OUT_OF_MEMORY;

    int actual_ret = potr_internal_start_connected_threads(&ctx, 0, 15U, &ops);

    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, actual_ret);
    EXPECT_EQ(1, s_calls.send_start_calls);
    EXPECT_EQ(1, s_calls.recv_start_calls);
    EXPECT_EQ(1, s_calls.tcp_send_ping_calls);
    EXPECT_EQ(1, s_calls.health_start_calls);
    EXPECT_EQ(1, s_calls.request_close_calls);
    EXPECT_EQ(1, s_calls.join_recv_calls);
    EXPECT_EQ(0, s_calls.stop_health_calls);
    EXPECT_EQ(1, s_calls.send_stop_calls);
    EXPECT_EQ(1, s_calls.release_conn_calls);
    EXPECT_EQ(CPLAT_INVALID_SOCKET, ctx.tcp_conn_fd[0]);
}

// health スレッド開始失敗時に、既存の send スレッドが停止されないことの確認
TEST_F(potrConnectedThreadsTest, health_failure_keeps_preexisting_send_thread_running)
{
    potr_internal_connected_threads_ops ops = make_ops();
    cplat_atomic_store_i32(&ctx.send_thread_running, 1, CPLAT_MEMORY_ORDER_RELAXED);
    s_calls.health_start_result = POTR_ERR_OUT_OF_MEMORY;

    int actual_ret = potr_internal_start_connected_threads(&ctx, 0, 16U, &ops);

    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, actual_ret);
    EXPECT_EQ(0, s_calls.send_start_calls);
    EXPECT_EQ(0, s_calls.send_stop_calls);
    EXPECT_EQ(1, s_calls.request_close_calls);
    EXPECT_EQ(1, s_calls.join_recv_calls);
    EXPECT_EQ(1, s_calls.release_conn_calls);
}

// 非 primary path では send スレッドが操作されないことの確認
TEST_F(potrConnectedThreadsTest, non_primary_path_does_not_touch_send_thread)
{
    potr_internal_connected_threads_ops ops = make_ops();
    s_calls.recv_start_result = POTR_ERR_IO;

    int actual_ret = potr_internal_start_connected_threads(&ctx, 1, 17U, &ops);

    EXPECT_EQ(POTR_ERR_IO, actual_ret);
    EXPECT_EQ(0, s_calls.send_start_calls);
    EXPECT_EQ(0, s_calls.send_stop_calls);
    EXPECT_EQ(1, s_calls.request_close_calls);
    EXPECT_EQ(1, s_calls.release_conn_calls);
    EXPECT_EQ(CPLAT_INVALID_SOCKET, ctx.tcp_conn_fd[1]);
}

// 全段成功時に ping 状態が設定され、ロールバックが発生しないことの確認
TEST_F(potrConnectedThreadsTest, success_sets_ping_state_without_rollback)
{
    potr_internal_connected_threads_ops ops = make_ops();

    int actual_ret = potr_internal_start_connected_threads(&ctx, 0, 18U, &ops);

    EXPECT_EQ(POTR_OK, actual_ret);
    EXPECT_EQ(1, s_calls.send_start_calls);
    EXPECT_EQ(1, s_calls.recv_start_calls);
    EXPECT_EQ(1, s_calls.tcp_send_ping_calls);
    EXPECT_EQ(1, s_calls.health_start_calls);
    EXPECT_EQ(0, s_calls.request_close_calls);
    EXPECT_EQ(0, s_calls.join_recv_calls);
    EXPECT_EQ(0, s_calls.release_conn_calls);
    EXPECT_EQ(0, s_calls.send_stop_calls);
    EXPECT_EQ((int)POTR_PING_STATE_UNDEFINED, s_calls.last_set_ping_state);
}

// recv スレッド起動前に ping 状態が初期化され、起動後の受信結果を上書きしないことの確認
TEST_F(potrConnectedThreadsTest, ping_state_is_reset_before_recv_start)
{
    potr_internal_connected_threads_ops ops = make_ops();
    cplat_atomic_store_u8(&ctx.path_ping_state[1], POTR_PING_STATE_NORMAL, CPLAT_MEMORY_ORDER_RELAXED);
    s_calls.ping_state_at_recv_start = -1;

    int actual_ret = potr_internal_start_connected_threads(&ctx, 1, 19U, &ops);

    EXPECT_EQ(POTR_OK, actual_ret);
    EXPECT_EQ(1, s_calls.set_ping_state_calls);
    EXPECT_EQ((int)POTR_PING_STATE_UNDEFINED, s_calls.ping_state_at_recv_start);
}
