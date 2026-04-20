#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>

#include <porter_const.h>
#include <potrContext.h>
#include <potrConnectedThreads.h>

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

static int fake_send_start(struct PotrContext_ *ctx)
{
    g_calls.send_start_calls++;
    ctx->send_thread_running = 1;
    return POTR_SUCCESS;
}

static void fake_send_stop(struct PotrContext_ *ctx)
{
    g_calls.send_stop_calls++;
    ctx->send_thread_running = 0;
}

static int fake_recv_start(struct PotrContext_ *ctx, int path_idx)
{
    g_calls.recv_start_calls++;
    if (g_calls.recv_start_result == POTR_SUCCESS)
    {
        ctx->running[path_idx] = 1;
    }
    return g_calls.recv_start_result;
}

extern "C" int potr_tcp_send_ping_now(struct PotrContext_ *ctx, int path_idx)
{
    (void)ctx;
    g_calls.tcp_send_ping_calls++;
    g_calls.last_tcp_send_ping_path = path_idx;
    return g_calls.tcp_send_ping_result;
}

static int fake_health_start(struct PotrContext_ *ctx, int path_idx)
{
    (void)ctx;
    (void)path_idx;
    g_calls.health_start_calls++;
    return g_calls.health_start_result;
}

static void fake_close_conn(struct PotrContext_ *ctx, int path_idx)
{
    g_calls.close_conn_calls++;
    ctx->tcp_conn_fd[path_idx] = POTR_INVALID_SOCKET;
}

static void fake_join_recv(struct PotrContext_ *ctx, int path_idx)
{
    (void)ctx;
    (void)path_idx;
    g_calls.join_recv_calls++;
}

static void fake_set_path_ping_state(struct PotrContext_ *ctx,
                                     int                  path_idx,
                                     uint8_t              next_state)
{
    g_calls.set_ping_state_calls++;
    g_calls.last_set_ping_path  = path_idx;
    g_calls.last_set_ping_state = (int)next_state;
    ctx->path_ping_state[path_idx] = next_state;
}

class potrConnectedThreadsTest : public Test
{
  protected:
    void SetUp() override
    {
        memset(&ctx, 0, sizeof(ctx));
        memset(&g_calls, 0, sizeof(g_calls));

        ctx.service.service_id = 42;
        ctx.service.type       = POTR_TYPE_TCP_BIDIR;
        ctx.role               = POTR_ROLE_SENDER;
        ctx.tcp_conn_fd[0]     = 123;
        ctx.tcp_conn_fd[1]     = 456;

        g_calls.recv_start_result   = POTR_SUCCESS;
        g_calls.tcp_send_ping_result = POTR_SUCCESS;
        g_calls.health_start_result = POTR_SUCCESS;
    }

    PotrConnectedThreadsOps make_ops()
    {
        PotrConnectedThreadsOps ops =
        {
            fake_send_start,
            fake_send_stop,
            fake_recv_start,
            fake_health_start,
            fake_close_conn,
            fake_join_recv,
            fake_set_path_ping_state
        };
        return ops;
    }

    struct PotrContext_ ctx;
};

TEST_F(potrConnectedThreadsTest, recv_failure_stops_send_started_by_this_call)
{
    PotrConnectedThreadsOps ops = make_ops();
    g_calls.recv_start_result = POTR_ERROR;

    int rtc = potr_start_connected_threads(&ctx, 0, &ops);

    EXPECT_EQ(POTR_ERROR, rtc);
    EXPECT_EQ(1, g_calls.send_start_calls);
    EXPECT_EQ(1, g_calls.recv_start_calls);
    EXPECT_EQ(1, g_calls.send_stop_calls);
    EXPECT_EQ(1, g_calls.close_conn_calls);
    EXPECT_EQ(0, g_calls.join_recv_calls);
    EXPECT_EQ(0, g_calls.tcp_send_ping_calls);
    EXPECT_EQ(0, g_calls.health_start_calls);
    EXPECT_EQ(POTR_INVALID_SOCKET, ctx.tcp_conn_fd[0]);
}

TEST_F(potrConnectedThreadsTest, recv_failure_keeps_preexisting_send_thread_running)
{
    PotrConnectedThreadsOps ops = make_ops();
    ctx.send_thread_running = 1;
    g_calls.recv_start_result = POTR_ERROR;

    int rtc = potr_start_connected_threads(&ctx, 0, &ops);

    EXPECT_EQ(POTR_ERROR, rtc);
    EXPECT_EQ(0, g_calls.send_start_calls);
    EXPECT_EQ(1, g_calls.recv_start_calls);
    EXPECT_EQ(0, g_calls.send_stop_calls);
    EXPECT_EQ(1, g_calls.close_conn_calls);
    EXPECT_EQ(0, g_calls.tcp_send_ping_calls);
}

TEST_F(potrConnectedThreadsTest, bootstrap_ping_failure_rolls_back_recv_and_new_send_thread)
{
    PotrConnectedThreadsOps ops = make_ops();
    g_calls.tcp_send_ping_result = POTR_ERROR;

    int rtc = potr_start_connected_threads(&ctx, 0, &ops);

    EXPECT_EQ(POTR_ERROR, rtc);
    EXPECT_EQ(1, g_calls.send_start_calls);
    EXPECT_EQ(1, g_calls.recv_start_calls);
    EXPECT_EQ(1, g_calls.tcp_send_ping_calls);
    EXPECT_EQ(0, g_calls.health_start_calls);
    EXPECT_EQ(1, g_calls.close_conn_calls);
    EXPECT_EQ(1, g_calls.join_recv_calls);
    EXPECT_EQ(1, g_calls.send_stop_calls);
    EXPECT_EQ(0, ctx.running[0]);
    EXPECT_EQ(POTR_INVALID_SOCKET, ctx.tcp_conn_fd[0]);
}

TEST_F(potrConnectedThreadsTest, health_failure_rolls_back_recv_and_new_send_thread)
{
    PotrConnectedThreadsOps ops = make_ops();
    g_calls.health_start_result = POTR_ERROR;

    int rtc = potr_start_connected_threads(&ctx, 0, &ops);

    EXPECT_EQ(POTR_ERROR, rtc);
    EXPECT_EQ(1, g_calls.send_start_calls);
    EXPECT_EQ(1, g_calls.recv_start_calls);
    EXPECT_EQ(1, g_calls.tcp_send_ping_calls);
    EXPECT_EQ(1, g_calls.health_start_calls);
    EXPECT_EQ(1, g_calls.close_conn_calls);
    EXPECT_EQ(1, g_calls.join_recv_calls);
    EXPECT_EQ(1, g_calls.send_stop_calls);
    EXPECT_EQ(0, ctx.running[0]);
    EXPECT_EQ(POTR_INVALID_SOCKET, ctx.tcp_conn_fd[0]);
}

TEST_F(potrConnectedThreadsTest, health_failure_keeps_preexisting_send_thread_running)
{
    PotrConnectedThreadsOps ops = make_ops();
    ctx.send_thread_running = 1;
    g_calls.health_start_result = POTR_ERROR;

    int rtc = potr_start_connected_threads(&ctx, 0, &ops);

    EXPECT_EQ(POTR_ERROR, rtc);
    EXPECT_EQ(0, g_calls.send_start_calls);
    EXPECT_EQ(1, g_calls.recv_start_calls);
    EXPECT_EQ(1, g_calls.tcp_send_ping_calls);
    EXPECT_EQ(1, g_calls.health_start_calls);
    EXPECT_EQ(1, g_calls.close_conn_calls);
    EXPECT_EQ(1, g_calls.join_recv_calls);
    EXPECT_EQ(0, g_calls.send_stop_calls);
}

TEST_F(potrConnectedThreadsTest, non_primary_path_does_not_touch_send_thread)
{
    PotrConnectedThreadsOps ops = make_ops();
    g_calls.recv_start_result = POTR_ERROR;

    int rtc = potr_start_connected_threads(&ctx, 1, &ops);

    EXPECT_EQ(POTR_ERROR, rtc);
    EXPECT_EQ(0, g_calls.send_start_calls);
    EXPECT_EQ(1, g_calls.recv_start_calls);
    EXPECT_EQ(0, g_calls.send_stop_calls);
    EXPECT_EQ(1, g_calls.close_conn_calls);
    EXPECT_EQ(0, g_calls.tcp_send_ping_calls);
    EXPECT_EQ(POTR_INVALID_SOCKET, ctx.tcp_conn_fd[1]);
}

TEST_F(potrConnectedThreadsTest, success_sets_ping_state_without_rollback)
{
    PotrConnectedThreadsOps ops = make_ops();

    int rtc = potr_start_connected_threads(&ctx, 0, &ops);

    EXPECT_EQ(POTR_SUCCESS, rtc);
    EXPECT_EQ(1, g_calls.send_start_calls);
    EXPECT_EQ(1, g_calls.recv_start_calls);
    EXPECT_EQ(1, g_calls.tcp_send_ping_calls);
    EXPECT_EQ(0, g_calls.last_tcp_send_ping_path);
    EXPECT_EQ(1, g_calls.health_start_calls);
    EXPECT_EQ(0, g_calls.close_conn_calls);
    EXPECT_EQ(0, g_calls.join_recv_calls);
    EXPECT_EQ(0, g_calls.send_stop_calls);
    EXPECT_EQ(1, g_calls.set_ping_state_calls);
    EXPECT_EQ(0, g_calls.last_set_ping_path);
    EXPECT_EQ((int)POTR_PING_STATE_UNDEFINED, g_calls.last_set_ping_state);
    EXPECT_EQ(POTR_PING_STATE_UNDEFINED, ctx.path_ping_state[0]);
}
