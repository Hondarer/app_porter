#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_com_util.h>
#include <mock_porter.h>

#include <porter/porter_const.h>
#include <porter/porter_spec.h>
#include <porter/potrContext.h>
#include <porter/infra/potrSendQueue.h>

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

        ctx.service.service_id = 42;
        ctx.global.max_payload = 1400;
        ctx.global.max_message_size = 4096;
        ctx.send_thread_running = 1;
        ctx.max_peers = (int)(sizeof(peers) / sizeof(peers[0]));
        ctx.peers = peers;

        ASSERT_EQ(POTR_SUCCESS, potr_send_queue_init(&ctx.send_queue, 8, 1400));
        com_util_local_lock_create(&ctx.peers_mutex);
    }

    void TearDown() override
    {
        com_util_local_lock_destroy(ctx.peers_mutex);
        potr_send_queue_dispose(&ctx.send_queue);
    }

    PotrPayloadElem popQueuedElem()
    {
        PotrPayloadElem elem;
        memset(&elem, 0, sizeof(elem));
        EXPECT_EQ(POTR_SUCCESS, potr_send_queue_try_pop(&ctx.send_queue, &elem));
        return elem;
    }

    PotrContext ctx;
    PotrPeerContext peers[2];
};

// TCP は物理パスが active でも論理接続前の送信が拒否されることの確認
TEST_F(potrSendTest, tcp_requires_logical_connected_even_with_active_path)
{
    // Arrange
    NiceMock<Mock_com_util> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "tcp-before-connected"; // [状態] - 送信ペイロードを "tcp-before-connected" とする。

    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.tcp_active_paths = 1;
    ctx.health_alive = 0; // [状態] - TCP_BIDIR で物理パスは active、論理接続 (health_alive) は未成立とする。

    // Pre-Assert

    // Act
    // Assert
    EXPECT_EQ(POTR_ERROR_DISCONNECTED,
              potrSend(&ctx, POTR_PEER_NA, payload, strlen(payload), 0)); // [手順] - potrSend で送信を試みる。
    // [確認_異常系] - POTR_ERROR_DISCONNECTED が返ること。
    EXPECT_EQ(0U, ctx.send_queue.count); // [確認_異常系] - 送信キューに積まれないこと。
}

// N:1 の全 peer 送信で接続済み peer が 1 件もない場合に切断エラーとなることの確認
TEST_F(potrSendTest, peer_all_returns_disconnected_when_no_connected_peers)
{
    // Arrange
    NiceMock<Mock_com_util> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "n1-broadcast"; // [状態] - 送信ペイロードを "n1-broadcast" とする。

    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1;
    peers[0].active = 1;
    peers[0].peer_id = 10;
    peers[0].health_alive = 0; // [状態] - active だが未接続 (health_alive=0) の peer を 1 件だけ用意する。

    // Pre-Assert

    // Act
    // Assert
    EXPECT_EQ(POTR_ERROR_DISCONNECTED, potrSend(&ctx, POTR_PEER_ALL, payload, strlen(payload),
                                                0)); // [手順] - POTR_PEER_ALL 宛てに potrSend で送信を試みる。
                                                     // [確認_異常系] - POTR_ERROR_DISCONNECTED が返ること。
    EXPECT_EQ(0U, ctx.send_queue.count);             // [確認_異常系] - 送信キューに積まれないこと。
}

// N:1 の全 peer 送信が接続済み peer だけへ送られることの確認
TEST_F(potrSendTest, peer_all_sends_only_to_connected_peers)
{
    // Arrange
    NiceMock<Mock_com_util> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "n1-connected-peer"; // [状態] - 送信ペイロードを "n1-connected-peer" とする。

    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1;
    peers[0].active = 1;
    peers[0].peer_id = 10;
    peers[0].health_alive = 1; // [状態] - 接続済み (health_alive=1) の peer 10 を用意する。
    peers[1].active = 1;
    peers[1].peer_id = 11;
    peers[1].health_alive = 0; // [状態] - 未接続の peer 11 を用意する。

    // Pre-Assert

    // Act
    // Assert
    EXPECT_EQ(POTR_SUCCESS, potrSend(&ctx, POTR_PEER_ALL, payload, strlen(payload),
                                     0)); // [手順] - POTR_PEER_ALL 宛てに potrSend で送信する。
                                          // [確認_正常系] - POTR_SUCCESS が返ること。
    EXPECT_EQ(1U, ctx.send_queue.count);  // [確認_正常系] - 送信キューに 1 件だけ積まれること。

    {
        PotrPayloadElem elem = popQueuedElem();
        EXPECT_EQ((PotrPeerId)10, elem.peer_id);              // [確認_正常系] - 宛先が接続済みの peer 10 であること。
        EXPECT_EQ(strlen(payload), (size_t)elem.payload_len); // [確認_正常系] - ペイロード長が一致すること。
        EXPECT_EQ(0, memcmp(elem.payload, payload, strlen(payload))); // [確認_正常系] - ペイロード内容が一致すること。
    }
}

// 片方向 unicast は接続状態がなくても送信できることの確認
TEST_F(potrSendTest, unicast_sender_path_still_sends_without_connected_state)
{
    // Arrange
    NiceMock<Mock_com_util> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "one-way-still-sendable"; // [状態] - 送信ペイロードを "one-way-still-sendable" とする。

    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.health_alive = 0; // [状態] - 片方向 unicast で接続状態 (health_alive) は未成立とする。

    // Pre-Assert

    // Act
    // Assert
    EXPECT_EQ(POTR_SUCCESS,
              potrSend(&ctx, POTR_PEER_NA, payload, strlen(payload), 0)); // [手順] - potrSend で送信する。
                                                                          // [確認_正常系] - POTR_SUCCESS が返ること。
    EXPECT_EQ(1U, ctx.send_queue.count); // [確認_正常系] - 送信キューに 1 件積まれること。

    {
        PotrPayloadElem elem = popQueuedElem();
        EXPECT_EQ(POTR_PEER_NA, elem.peer_id);                // [確認_正常系] - 宛先が POTR_PEER_NA であること。
        EXPECT_EQ(strlen(payload), (size_t)elem.payload_len); // [確認_正常系] - ペイロード長が一致すること。
        EXPECT_EQ(0, memcmp(elem.payload, payload, strlen(payload))); // [確認_正常系] - ペイロード内容が一致すること。
    }
}

// データ送信による health ping 抑止が type 1〜6 (片方向 UDP 系) だけに適用されることの確認
TEST_F(potrSendTest, data_based_health_ping_suppression_applies_only_to_type_1_to_6)
{
    // Arrange

    // Pre-Assert

    // Act
    // Assert
    // [確認_正常系] - 片方向 UDP 系の type 1〜6 が potr_is_oneway_udp_type で真と判定されること。
    EXPECT_TRUE(potr_is_oneway_udp_type(POTR_TYPE_UNICAST_RAW));
    EXPECT_TRUE(potr_is_oneway_udp_type(POTR_TYPE_MULTICAST_RAW));
    EXPECT_TRUE(potr_is_oneway_udp_type(POTR_TYPE_BROADCAST_RAW));
    EXPECT_TRUE(potr_is_oneway_udp_type(POTR_TYPE_UNICAST));
    EXPECT_TRUE(potr_is_oneway_udp_type(POTR_TYPE_MULTICAST));
    EXPECT_TRUE(potr_is_oneway_udp_type(POTR_TYPE_BROADCAST));

    // [確認_正常系] - 双方向系と TCP 系が potr_is_oneway_udp_type で偽と判定されること。
    EXPECT_FALSE(potr_is_oneway_udp_type(POTR_TYPE_UNICAST_BIDIR));
    EXPECT_FALSE(potr_is_oneway_udp_type(POTR_TYPE_UNICAST_BIDIR_N1));
    EXPECT_FALSE(potr_is_oneway_udp_type(POTR_TYPE_TCP));
    EXPECT_FALSE(potr_is_oneway_udp_type(POTR_TYPE_TCP_BIDIR));
}

// 接続直後の immediate health ping が type 1〜6 (片方向 UDP 系) だけで無効になることの確認
TEST_F(potrSendTest, immediate_health_ping_is_disabled_only_for_type_1_to_6)
{
    // Arrange

    // Pre-Assert

    // Act
    // Assert
    // [確認_正常系] - 片方向 UDP 系の type 1〜6 が potr_type_uses_immediate_health_ping で偽と判定されること。
    EXPECT_FALSE(potr_type_uses_immediate_health_ping(POTR_TYPE_UNICAST_RAW));
    EXPECT_FALSE(potr_type_uses_immediate_health_ping(POTR_TYPE_MULTICAST_RAW));
    EXPECT_FALSE(potr_type_uses_immediate_health_ping(POTR_TYPE_BROADCAST_RAW));
    EXPECT_FALSE(potr_type_uses_immediate_health_ping(POTR_TYPE_UNICAST));
    EXPECT_FALSE(potr_type_uses_immediate_health_ping(POTR_TYPE_MULTICAST));
    EXPECT_FALSE(potr_type_uses_immediate_health_ping(POTR_TYPE_BROADCAST));

    // [確認_正常系] - 双方向系と TCP 系が potr_type_uses_immediate_health_ping で真と判定されること。
    EXPECT_TRUE(potr_type_uses_immediate_health_ping(POTR_TYPE_UNICAST_BIDIR));
    EXPECT_TRUE(potr_type_uses_immediate_health_ping(POTR_TYPE_UNICAST_BIDIR_N1));
    EXPECT_TRUE(potr_type_uses_immediate_health_ping(POTR_TYPE_TCP));
    EXPECT_TRUE(potr_type_uses_immediate_health_ping(POTR_TYPE_TCP_BIDIR));
}
