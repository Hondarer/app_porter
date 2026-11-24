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

uint16_t pick_free_port(cplat_socket_kind kind)
{
    cplat_error detail = {};
    cplat_socket sock = CPLAT_INVALID_SOCKET;
    if (cplat_socket_open(kind, &sock, &detail) != CPLAT_OK)
    {
        return 0U;
    }
    cplat_ipv4_endpoint ep = {};
    ep.address = cplat_hton32(0x7F000001U);
    ep.port = 0U;
    uint16_t port = 0U;
    if (cplat_socket_bind(sock, &ep, &detail) == CPLAT_OK)
    {
        struct sockaddr_in sin = {};
#if defined(PLATFORM_WINDOWS)
        int sin_len = (int)sizeof(sin);
        if (getsockname((SOCKET)sock, reinterpret_cast<struct sockaddr *>(&sin), &sin_len) == 0)
#else
        socklen_t sin_len = (socklen_t)sizeof(sin);
        if (getsockname((int)sock, reinterpret_cast<struct sockaddr *>(&sin), &sin_len) == 0)
#endif
        {
            port = cplat_ntoh16(sin.sin_port);
        }
    }
    cplat_socket_close(sock);
    return port;
}
} // namespace

// UDP 無通信・複数経路・双方向・N:1 の各構成が recv_cancel により 1 秒以内に安全停止することの確認
TEST(nonblockingStopIntegrationTest, udp_idle_multipath_bidir_n1_stop_quickly)
{
    potr_global_config global = make_global();
    const uint16_t p1 = pick_free_port(CPLAT_SOCKET_UDP);
    const uint16_t p2 = pick_free_port(CPLAT_SOCKET_UDP);
    const uint16_t p3 = pick_free_port(CPLAT_SOCKET_UDP);
    const uint16_t p4 = pick_free_port(CPLAT_SOCKET_UDP);
    ASSERT_NE(0U, p1);
    ASSERT_NE(0U, p2);
    ASSERT_NE(0U, p3);
    ASSERT_NE(0U, p4);

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
    const uint16_t port = pick_free_port(CPLAT_SOCKET_TCP);
    ASSERT_NE(0U, port);

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
    const uint16_t port = pick_free_port(CPLAT_SOCKET_UDP);
    ASSERT_NE(0U, port);

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
