#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
    #include <cplat/base/windows_sdk.h>
#elif defined(PLATFORM_LINUX)
    #include <netinet/in.h>
    #include <sys/socket.h>
#endif
#include <testfw.h>
#include <porter.h>
#include <porter_test_trace.h>
#include <cplat/crt/string.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/socket.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace
{
std::atomic<int> g_recv_count{0};
std::atomic<int> g_connected{0};

void noop_cb(int64_t, potr_peer_id, potr_event event, const void *, size_t)
{
    if (event == POTR_EVENT_CONNECTED)
    {
        g_connected.fetch_add(1, std::memory_order_release);
    }
    else if (event == POTR_EVENT_DATA)
    {
        g_recv_count.fetch_add(1, std::memory_order_release);
    }
}

potr_global_config make_global(uint32_t close_timeout_ms = 1000U)
{
    potr_global_config g = {};
    g.window_size = 16U;
    g.max_payload = 1400U;
    g.max_message_size = 65535U;
    g.send_queue_depth = 64U;
    g.udp_health_interval_ms = 0U;
    g.udp_health_timeout_ms = 0U;
    g.tcp_health_interval_ms = 0U;
    g.tcp_health_timeout_ms = 0U;
    g.tcp_close_timeout_ms = close_timeout_ms;
    return g;
}
} // namespace

// UDP 無通信・複数経路・双方向・N:1 の各構成が recv_cancel により 1 秒以内に安全停止することの確認
TEST(nonblockingStopIntegrationTest, udp_idle_multipath_bidir_n1_stop_quickly)
{
    potr_global_config global = make_global();
    std::vector<uint16_t> ports;
    ASSERT_TRUE(allocateTestPorts(4U, ports)); // [状態確認] - allocateTestPorts() の戻り値が true であること。
    ASSERT_TRUE(porter_test_start_error_trace()); // [状態確認] - porter_test_start_error_trace() の戻り値が true であること。
    const uint16_t p1 = ports[0];
    const uint16_t p2 = ports[1];
    const uint16_t p3 = ports[2];
    const uint16_t p4 = ports[3];

    // 1. 単一経路 UDP 受信者 (無通信)
    {
        potr_service_def svc = {};
        svc.service_id = 8101;
        svc.type = POTR_TYPE_UNICAST;
        svc.dst_port = p1;
        cplat_strcpy(svc.src_addr[0], sizeof(svc.src_addr[0]), "127.0.0.1");
        cplat_strcpy(svc.dst_addr[0], sizeof(svc.dst_addr[0]), "127.0.0.1");
        potr_context *h = nullptr;
        ASSERT_EQ(POTR_OK, potr_service_open(&global, &svc, POTR_ROLE_RECEIVER, noop_cb, &h));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto t0 = std::chrono::steady_clock::now();
        EXPECT_EQ(POTR_OK, potr_service_close(h));
        EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(),
                  1000);
    }

    // 2. 複数経路 UDP 受信者
    {
        potr_service_def svc = {};
        svc.service_id = 8102;
        svc.type = POTR_TYPE_UNICAST;
        svc.dst_port = p2;
        cplat_strcpy(svc.src_addr[0], sizeof(svc.src_addr[0]), "127.0.0.1");
        cplat_strcpy(svc.dst_addr[0], sizeof(svc.dst_addr[0]), "127.0.0.1");
        cplat_strcpy(svc.src_addr[1], sizeof(svc.src_addr[1]), "127.0.0.1");
        cplat_strcpy(svc.dst_addr[1], sizeof(svc.dst_addr[1]), "127.0.0.1");
        potr_context *h = nullptr;
        ASSERT_EQ(POTR_OK, potr_service_open(&global, &svc, POTR_ROLE_RECEIVER, noop_cb, &h));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto t0 = std::chrono::steady_clock::now();
        EXPECT_EQ(POTR_OK, potr_service_close(h));
        EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(),
                  1000);
    }

    // 3. 双方向 UDP
    {
        potr_service_def svc = {};
        svc.service_id = 8103;
        svc.type = POTR_TYPE_UNICAST_BIDIR;
        svc.dst_port = p3;
        cplat_strcpy(svc.src_addr[0], sizeof(svc.src_addr[0]), "127.0.0.1");
        cplat_strcpy(svc.dst_addr[0], sizeof(svc.dst_addr[0]), "127.0.0.1");
        potr_context *h = nullptr;
        ASSERT_EQ(POTR_OK, potr_service_open(&global, &svc, POTR_ROLE_RECEIVER, noop_cb, &h));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto t0 = std::chrono::steady_clock::now();
        EXPECT_EQ(POTR_OK, potr_service_close(h));
        EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(),
                  1000);
    }

    // 4. N:1 UDP サーバー
    {
        potr_service_def svc = {};
        svc.service_id = 8104;
        svc.type = POTR_TYPE_UNICAST_BIDIR_N1;
        svc.dst_port = p4;
        cplat_strcpy(svc.dst_addr[0], sizeof(svc.dst_addr[0]), "127.0.0.1");
        potr_context *h = nullptr;
        ASSERT_EQ(POTR_OK, potr_service_open(&global, &svc, POTR_ROLE_RECEIVER, noop_cb, &h));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto t0 = std::chrono::steady_clock::now();
        EXPECT_EQ(POTR_OK, potr_service_close(h));
        EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(),
                  1000);
    }
}

// TCP の受け付け待ちおよびヘッダー途中での停止が 1 秒以内に完了し、部分フレームを配信しないことの確認
TEST(nonblockingStopIntegrationTest, tcp_accept_wait_and_partial_header_stop_within_1s)
{
    g_recv_count.store(0, std::memory_order_release);
    potr_global_config global = make_global(500U);
    const uint16_t port = allocateTestPort();
    ASSERT_NE(0U, port); // [状態確認] - allocateTestPort() の戻り値が 0 以外であること。
    ASSERT_TRUE(porter_test_start_error_trace()); // [状態確認] - porter_test_start_error_trace() の戻り値が true であること。

    // 1. 無期限の受け付け待ちからの即時停止
    {
        potr_service_def svc = {};
        svc.service_id = 8201;
        svc.type = POTR_TYPE_TCP;
        svc.dst_port = port;
        cplat_strcpy(svc.dst_addr[0], sizeof(svc.dst_addr[0]), "127.0.0.1");
        potr_context *h = nullptr;
        ASSERT_EQ(POTR_OK, potr_service_open(&global, &svc, POTR_ROLE_RECEIVER, noop_cb, &h));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto t0 = std::chrono::steady_clock::now();
        EXPECT_EQ(POTR_OK, potr_service_close(h));
        EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(),
                  1000);
    }

    // 2. ヘッダー途中の部分データ受信中にサービスを閉じる
    {
        potr_service_def svc = {};
        svc.service_id = 8202;
        svc.type = POTR_TYPE_TCP;
        svc.dst_port = port;
        cplat_strcpy(svc.dst_addr[0], sizeof(svc.dst_addr[0]), "127.0.0.1");
        potr_context *h = nullptr;
        ASSERT_EQ(POTR_OK, potr_service_open(&global, &svc, POTR_ROLE_RECEIVER, noop_cb, &h));

        cplat_error detail = {};
        cplat_socket client = CPLAT_INVALID_SOCKET;
        ASSERT_EQ(CPLAT_OK, cplat_socket_open(CPLAT_SOCKET_TCP, &client, &detail));
        cplat_ipv4_endpoint ep = {};
        ep.address = cplat_hton32(0x7F000001U);
        ep.port = cplat_hton16(port);
        ASSERT_EQ(CPLAT_OK, cplat_socket_connect(client, &ep, &detail));
        const uint8_t partial[12] = {0, 0, 0, 0, 0, 0, 0x20, 0x0A, 1, 2, 3, 4};
        ASSERT_EQ(CPLAT_OK, cplat_socket_send_all(client, partial, sizeof(partial), &detail));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        auto t0 = std::chrono::steady_clock::now();
        EXPECT_EQ(POTR_OK, potr_service_close(h));
        EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(),
                  1000);
        EXPECT_EQ(0, g_recv_count.load(std::memory_order_acquire));
        cplat_socket_close(client);
    }
}

// 実ソケットのマルチキャスト送受信 (ttl = 1 および構造体 0 -> 実効 1) の確認
TEST(nonblockingStopIntegrationTest, multicast_send_recv_with_ttl)
{
    g_recv_count.store(0, std::memory_order_release);
    potr_global_config global = make_global();
    const uint16_t port = allocateTestPort();
    ASSERT_NE(0U, port); // [状態確認] - allocateTestPort() の戻り値が 0 以外であること。
    ASSERT_TRUE(porter_test_start_error_trace()); // [状態確認] - porter_test_start_error_trace() の戻り値が true であること。

    potr_service_def svc = {};
    svc.service_id = 8301;
    svc.type = POTR_TYPE_MULTICAST;
    svc.dst_port = port;
    svc.ttl = 0U; /* 構造体 0 -> 実効 TTL 1 */
    cplat_strcpy(svc.multicast_group, sizeof(svc.multicast_group), "239.255.86.79");
    cplat_strcpy(svc.src_addr[0], sizeof(svc.src_addr[0]), "127.0.0.1");
    cplat_strcpy(svc.dst_addr[0], sizeof(svc.dst_addr[0]), "127.0.0.1");

    potr_context *rx = nullptr;
    potr_context *tx = nullptr;
    ASSERT_EQ(POTR_OK, potr_service_open(&global, &svc, POTR_ROLE_RECEIVER, noop_cb, &rx));
    ASSERT_EQ(POTR_OK, potr_service_open(&global, &svc, POTR_ROLE_SENDER, nullptr, &tx));

    const char msg[] = "mcast-ttl";
    EXPECT_EQ(POTR_OK, potr_service_send(tx, POTR_PEER_NA, msg, sizeof(msg), POTR_SEND_BLOCKING));

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (g_recv_count.load(std::memory_order_acquire) == 0 && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_GE(g_recv_count.load(std::memory_order_acquire), 1);

    EXPECT_EQ(POTR_OK, potr_service_close(tx));
    EXPECT_EQ(POTR_OK, potr_service_close(rx));
}
