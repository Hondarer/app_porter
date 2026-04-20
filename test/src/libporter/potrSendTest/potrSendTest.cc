#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_com_util.h>
#include <mock_potrPeerTable.h>

#include <porter_const.h>
#include <porter.h>
#include <potrContext.h>
#include <potrSendQueue.h>

#if defined(PLATFORM_LINUX)
    #include <pthread.h>
#elif defined(PLATFORM_WINDOWS)
    #include <com_util/base/windows_sdk.h>
#endif /* PLATFORM_ */
#include <string.h>

using namespace testing;

class potrSendTest : public Test
{
  protected:
    void SetUp() override
    {
        memset(&ctx, 0, sizeof(ctx));
        memset(peers, 0, sizeof(peers));

        ctx.service.service_id      = 42;
        ctx.global.max_payload      = 1400;
        ctx.global.max_message_size = 4096;
        ctx.send_thread_running     = 1;
        ctx.max_peers               = (int)(sizeof(peers) / sizeof(peers[0]));
        ctx.peers                   = peers;

        ASSERT_EQ(POTR_SUCCESS, potr_send_queue_init(&ctx.send_queue, 8, 1400));

#if defined(PLATFORM_LINUX)
        pthread_mutex_init(&ctx.peers_mutex, NULL);
#elif defined(PLATFORM_WINDOWS)
        InitializeCriticalSection(&ctx.peers_mutex);
#endif /* PLATFORM_ */
    }

    void TearDown() override
    {
#if defined(PLATFORM_LINUX)
        pthread_mutex_destroy(&ctx.peers_mutex);
#elif defined(PLATFORM_WINDOWS)
        DeleteCriticalSection(&ctx.peers_mutex);
#endif /* PLATFORM_ */
        potr_send_queue_destroy(&ctx.send_queue);
    }

    PotrPayloadElem popQueuedElem()
    {
        PotrPayloadElem elem;
        memset(&elem, 0, sizeof(elem));
        EXPECT_EQ(POTR_SUCCESS, potr_send_queue_try_pop(&ctx.send_queue, &elem));
        return elem;
    }

    struct PotrContext_ ctx;
    PotrPeerContext     peers[2];
};

TEST_F(potrSendTest, tcp_requires_logical_connected_even_with_active_path)
{
    NiceMock<Mock_com_util>       mock_log;
    NiceMock<Mock_potrPeerTable> mock_peer_table;
    const char payload[] = "tcp-before-connected";

    ctx.service.type     = POTR_TYPE_TCP_BIDIR;
    ctx.tcp_active_paths = 1;
    ctx.health_alive     = 0;

    EXPECT_EQ(POTR_ERROR_DISCONNECTED,
              potrSend(&ctx, POTR_PEER_NA, payload, strlen(payload), 0));
    EXPECT_EQ(0U, ctx.send_queue.count);
}

TEST_F(potrSendTest, peer_all_returns_disconnected_when_no_connected_peers)
{
    NiceMock<Mock_com_util>       mock_log;
    NiceMock<Mock_potrPeerTable> mock_peer_table;
    const char payload[] = "n1-broadcast";

    ctx.service.type   = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer  = 1;
    peers[0].active    = 1;
    peers[0].peer_id   = 10;
    peers[0].health_alive = 0;

    EXPECT_EQ(POTR_ERROR_DISCONNECTED,
              potrSend(&ctx, POTR_PEER_ALL, payload, strlen(payload), 0));
    EXPECT_EQ(0U, ctx.send_queue.count);
}

TEST_F(potrSendTest, peer_all_sends_only_to_connected_peers)
{
    NiceMock<Mock_com_util>       mock_log;
    NiceMock<Mock_potrPeerTable> mock_peer_table;
    const char payload[] = "n1-connected-peer";

    ctx.service.type   = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer  = 1;
    peers[0].active    = 1;
    peers[0].peer_id   = 10;
    peers[0].health_alive = 1;
    peers[1].active    = 1;
    peers[1].peer_id   = 11;
    peers[1].health_alive = 0;

    EXPECT_EQ(POTR_SUCCESS,
              potrSend(&ctx, POTR_PEER_ALL, payload, strlen(payload), 0));
    EXPECT_EQ(1U, ctx.send_queue.count);

    {
        PotrPayloadElem elem = popQueuedElem();
        EXPECT_EQ((PotrPeerId)10, elem.peer_id);
        EXPECT_EQ(strlen(payload), (size_t)elem.payload_len);
        EXPECT_EQ(0, memcmp(elem.payload, payload, strlen(payload)));
    }
}

TEST_F(potrSendTest, unicast_sender_path_still_sends_without_connected_state)
{
    NiceMock<Mock_com_util>       mock_log;
    NiceMock<Mock_potrPeerTable> mock_peer_table;
    const char payload[] = "one-way-still-sendable";

    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.health_alive = 0;

    EXPECT_EQ(POTR_SUCCESS,
              potrSend(&ctx, POTR_PEER_NA, payload, strlen(payload), 0));
    EXPECT_EQ(1U, ctx.send_queue.count);

    {
        PotrPayloadElem elem = popQueuedElem();
        EXPECT_EQ(POTR_PEER_NA, elem.peer_id);
        EXPECT_EQ(strlen(payload), (size_t)elem.payload_len);
        EXPECT_EQ(0, memcmp(elem.payload, payload, strlen(payload)));
    }
}

TEST_F(potrSendTest, data_based_health_ping_suppression_applies_only_to_type_1_to_6)
{
    EXPECT_TRUE(potr_is_oneway_udp_type(POTR_TYPE_UNICAST_RAW));
    EXPECT_TRUE(potr_is_oneway_udp_type(POTR_TYPE_MULTICAST_RAW));
    EXPECT_TRUE(potr_is_oneway_udp_type(POTR_TYPE_BROADCAST_RAW));
    EXPECT_TRUE(potr_is_oneway_udp_type(POTR_TYPE_UNICAST));
    EXPECT_TRUE(potr_is_oneway_udp_type(POTR_TYPE_MULTICAST));
    EXPECT_TRUE(potr_is_oneway_udp_type(POTR_TYPE_BROADCAST));

    EXPECT_FALSE(potr_is_oneway_udp_type(POTR_TYPE_UNICAST_BIDIR));
    EXPECT_FALSE(potr_is_oneway_udp_type(POTR_TYPE_UNICAST_BIDIR_N1));
    EXPECT_FALSE(potr_is_oneway_udp_type(POTR_TYPE_TCP));
    EXPECT_FALSE(potr_is_oneway_udp_type(POTR_TYPE_TCP_BIDIR));
}

TEST_F(potrSendTest, immediate_health_ping_is_disabled_only_for_type_1_to_6)
{
    EXPECT_FALSE(potr_type_uses_immediate_health_ping(POTR_TYPE_UNICAST_RAW));
    EXPECT_FALSE(potr_type_uses_immediate_health_ping(POTR_TYPE_MULTICAST_RAW));
    EXPECT_FALSE(potr_type_uses_immediate_health_ping(POTR_TYPE_BROADCAST_RAW));
    EXPECT_FALSE(potr_type_uses_immediate_health_ping(POTR_TYPE_UNICAST));
    EXPECT_FALSE(potr_type_uses_immediate_health_ping(POTR_TYPE_MULTICAST));
    EXPECT_FALSE(potr_type_uses_immediate_health_ping(POTR_TYPE_BROADCAST));

    EXPECT_TRUE(potr_type_uses_immediate_health_ping(POTR_TYPE_UNICAST_BIDIR));
    EXPECT_TRUE(potr_type_uses_immediate_health_ping(POTR_TYPE_UNICAST_BIDIR_N1));
    EXPECT_TRUE(potr_type_uses_immediate_health_ping(POTR_TYPE_TCP));
    EXPECT_TRUE(potr_type_uses_immediate_health_ping(POTR_TYPE_TCP_BIDIR));
}
