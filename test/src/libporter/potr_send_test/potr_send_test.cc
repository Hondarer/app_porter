#include <cplat/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/porter_spec.h>
#include <porter/potr_context.h>
#include <porter/infra/potr_send_queue.h>

#if defined(PLATFORM_LINUX)
    #include <pthread.h>
#elif defined(PLATFORM_WINDOWS)
    #include <cplat/base/windows_sdk.h>
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

        ASSERT_EQ(POTR_OK, potr_internal_send_queue_init(&ctx.send_queue, 8, 1400));
        cplat_local_lock_create(&ctx.peers_mutex);
    }

    void TearDown() override
    {
        cplat_local_lock_dispose(ctx.peers_mutex);
        potr_internal_send_queue_dispose(&ctx.send_queue);
    }

    potr_internal_payload_elem popQueuedElem()
    {
        potr_internal_payload_elem elem = {};
        EXPECT_EQ(POTR_OK, potr_internal_send_queue_try_pop(&ctx.send_queue, &elem));
        return elem;
    }

    potr_context ctx;
    potr_internal_peer_context peers[2];
};

// 終了処理中の送信が中止コードで拒否されることの確認
TEST_F(potrSendTest, close_requested_returns_canceled)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "closing"; // [状態] - 送信ペイロードを "closing" とする。
    ctx.close_requested = 1;          // [状態] - サービスの終了処理中とする。

    // Pre-Assert

    // Act
    int actual_ret = potr_send(&ctx, POTR_PEER_NA, payload, strlen(payload), 0); // [手順] - 終了処理中に送信を試みる。

    // Assert
    EXPECT_EQ(POTR_ERR_CANCELED, actual_ret);   // [確認_異常系] - potr_send の戻り値が POTR_ERR_CANCELED であること。
    EXPECT_EQ(0U, ctx.send_queue.count); // [確認_異常系] - 送信キューに積まれないこと。
}

// N:1 モードで POTR_PEER_NA を指定すると引数不正になることの確認
TEST_F(potrSendTest, n1_peer_na_returns_invalid_argument)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "n1-invalid-peer"; // [状態] - 送信ペイロードを "n1-invalid-peer" とする。
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1; // [状態] - N:1 モードとする。

    // Pre-Assert

    // Act
    int actual_ret = potr_send(&ctx, POTR_PEER_NA, payload, strlen(payload), 0); // [手順] - POTR_PEER_NA 宛てに送信を試みる。

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT,
              actual_ret);                      // [確認_異常系] - potr_send の戻り値が POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(0U, ctx.send_queue.count); // [確認_異常系] - 送信キューに積まれないこと。
}

// N:1 モードで存在しないピアを指定すると未検出コードになることの確認
TEST_F(potrSendTest, n1_unknown_peer_returns_not_found)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "n1-missing-peer"; // [状態] - 送信ペイロードを "n1-missing-peer" とする。
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1; // [状態] - N:1 モードとし、ピア テーブルは空のままとする。

    // Pre-Assert

    // Act
    int actual_ret = potr_send(&ctx, 123U, payload, strlen(payload), 0); // [手順] - 未登録のピア ID 宛てに送信を試みる。

    // Assert
    EXPECT_EQ(POTR_ERR_NOT_FOUND, actual_ret);  // [確認_異常系] - potr_send の戻り値が POTR_ERR_NOT_FOUND であること。
    EXPECT_EQ(0U, ctx.send_queue.count); // [確認_異常系] - 送信キューに積まれないこと。
}

// TCP は物理パスが active でも論理接続前の送信が拒否されることの確認
TEST_F(potrSendTest, tcp_requires_logical_connected_even_with_active_path)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "tcp-before-connected"; // [状態] - 送信ペイロードを "tcp-before-connected" とする。

    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.tcp_active_paths = 1;
    ctx.health_alive = 0; // [状態] - TCP_BIDIR で物理パスは active、論理接続 (health_alive) は未成立とする。

    // Pre-Assert

    // Act
    int actual_ret = potr_send(&ctx, POTR_PEER_NA, payload, strlen(payload), 0); // [手順] - potr_send で送信を試みる。

    // Assert
    EXPECT_EQ(POTR_ERR_DISCONNECTED, actual_ret); // [確認_異常系] - potr_send の戻り値が POTR_ERR_DISCONNECTED であること。
    EXPECT_EQ(0U, ctx.send_queue.count);   // [確認_異常系] - 送信キューに積まれないこと。
}

// N:1 の全 peer 送信で接続済み peer が 1 件もない場合に切断エラーとなることの確認
TEST_F(potrSendTest, peer_all_returns_disconnected_when_no_connected_peers)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "n1-broadcast"; // [状態] - 送信ペイロードを "n1-broadcast" とする。

    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1;
    peers[0].active = 1;
    peers[0].peer_id = 10;
    peers[0].health_alive = 0; // [状態] - active だが未接続 (health_alive=0) の peer を 1 件だけ用意する。

    // Pre-Assert

    // Act
    int actual_ret = potr_send(&ctx, POTR_PEER_ALL, payload, strlen(payload),
                       0); // [手順] - POTR_PEER_ALL 宛てに potr_send で送信を試みる。

    // Assert
    EXPECT_EQ(POTR_ERR_DISCONNECTED, actual_ret); // [確認_異常系] - potr_send の戻り値が POTR_ERR_DISCONNECTED であること。
    EXPECT_EQ(0U, ctx.send_queue.count);   // [確認_異常系] - 送信キューに積まれないこと。
}

// N:1 の全 peer 送信が接続済み peer だけへ送られることの確認
TEST_F(potrSendTest, peer_all_sends_only_to_connected_peers)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
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
    int actual_ret = potr_send(&ctx, POTR_PEER_ALL, payload, strlen(payload),
                       0); // [手順] - POTR_PEER_ALL 宛てに potr_send で送信する。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);             // [確認_正常系] - potr_send の戻り値が POTR_OK であること。
    EXPECT_EQ(1U, ctx.send_queue.count); // [確認_正常系] - 送信キューに 1 件だけ積まれること。

    {
        potr_internal_payload_elem elem = popQueuedElem();
        EXPECT_EQ((potr_peer_id)10, elem.peer_id);              // [確認_正常系] - 宛先が接続済みの peer 10 であること。
        EXPECT_EQ(strlen(payload), (size_t)elem.payload_len); // [確認_正常系] - ペイロード長が一致すること。
        EXPECT_EQ(0, memcmp(elem.payload, payload, strlen(payload))); // [確認_正常系] - ペイロード内容が一致すること。
    }
}

// 片方向 unicast は接続状態がなくても送信できることの確認
TEST_F(potrSendTest, unicast_sender_path_still_sends_without_connected_state)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "one-way-still-sendable"; // [状態] - 送信ペイロードを "one-way-still-sendable" とする。

    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.health_alive = 0; // [状態] - 片方向 unicast で接続状態 (health_alive) は未成立とする。

    // Pre-Assert

    // Act
    int actual_ret = potr_send(&ctx, POTR_PEER_NA, payload, strlen(payload), 0); // [手順] - potr_send で送信する。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);             // [確認_正常系] - potr_send の戻り値が POTR_OK であること。
    EXPECT_EQ(1U, ctx.send_queue.count); // [確認_正常系] - 送信キューに 1 件積まれること。

    {
        potr_internal_payload_elem elem = popQueuedElem();
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
    // [手順] - 各 type を potr_is_oneway_udp_type で判定する。
    int oneway_unicast_raw = potr_is_oneway_udp_type(POTR_TYPE_UNICAST_RAW);
    int oneway_multicast_raw = potr_is_oneway_udp_type(POTR_TYPE_MULTICAST_RAW);
    int oneway_broadcast_raw = potr_is_oneway_udp_type(POTR_TYPE_BROADCAST_RAW);
    int oneway_unicast = potr_is_oneway_udp_type(POTR_TYPE_UNICAST);
    int oneway_multicast = potr_is_oneway_udp_type(POTR_TYPE_MULTICAST);
    int oneway_broadcast = potr_is_oneway_udp_type(POTR_TYPE_BROADCAST);
    int oneway_unicast_bidir = potr_is_oneway_udp_type(POTR_TYPE_UNICAST_BIDIR);
    int oneway_unicast_bidir_n1 = potr_is_oneway_udp_type(POTR_TYPE_UNICAST_BIDIR_N1);
    int oneway_tcp = potr_is_oneway_udp_type(POTR_TYPE_TCP);
    int oneway_tcp_bidir = potr_is_oneway_udp_type(POTR_TYPE_TCP_BIDIR);

    // Assert
    // [確認_正常系] - 片方向 UDP 系の type 1〜6 が potr_is_oneway_udp_type で真と判定されること。
    EXPECT_TRUE(oneway_unicast_raw);
    EXPECT_TRUE(oneway_multicast_raw);
    EXPECT_TRUE(oneway_broadcast_raw);
    EXPECT_TRUE(oneway_unicast);
    EXPECT_TRUE(oneway_multicast);
    EXPECT_TRUE(oneway_broadcast);

    // [確認_正常系] - 双方向系と TCP 系が potr_is_oneway_udp_type で偽と判定されること。
    EXPECT_FALSE(oneway_unicast_bidir);
    EXPECT_FALSE(oneway_unicast_bidir_n1);
    EXPECT_FALSE(oneway_tcp);
    EXPECT_FALSE(oneway_tcp_bidir);
}

// 接続直後の immediate health ping が type 1〜6 (片方向 UDP 系) だけで無効になることの確認
TEST_F(potrSendTest, immediate_health_ping_is_disabled_only_for_type_1_to_6)
{
    // Arrange

    // Pre-Assert

    // Act
    // [手順] - 各 type を potr_type_uses_immediate_health_ping で判定する。
    int immediate_unicast_raw = potr_type_uses_immediate_health_ping(POTR_TYPE_UNICAST_RAW);
    int immediate_multicast_raw = potr_type_uses_immediate_health_ping(POTR_TYPE_MULTICAST_RAW);
    int immediate_broadcast_raw = potr_type_uses_immediate_health_ping(POTR_TYPE_BROADCAST_RAW);
    int immediate_unicast = potr_type_uses_immediate_health_ping(POTR_TYPE_UNICAST);
    int immediate_multicast = potr_type_uses_immediate_health_ping(POTR_TYPE_MULTICAST);
    int immediate_broadcast = potr_type_uses_immediate_health_ping(POTR_TYPE_BROADCAST);
    int immediate_unicast_bidir = potr_type_uses_immediate_health_ping(POTR_TYPE_UNICAST_BIDIR);
    int immediate_unicast_bidir_n1 = potr_type_uses_immediate_health_ping(POTR_TYPE_UNICAST_BIDIR_N1);
    int immediate_tcp = potr_type_uses_immediate_health_ping(POTR_TYPE_TCP);
    int immediate_tcp_bidir = potr_type_uses_immediate_health_ping(POTR_TYPE_TCP_BIDIR);

    // Assert
    // [確認_正常系] - 片方向 UDP 系の type 1〜6 が potr_type_uses_immediate_health_ping で偽と判定されること。
    EXPECT_FALSE(immediate_unicast_raw);
    EXPECT_FALSE(immediate_multicast_raw);
    EXPECT_FALSE(immediate_broadcast_raw);
    EXPECT_FALSE(immediate_unicast);
    EXPECT_FALSE(immediate_multicast);
    EXPECT_FALSE(immediate_broadcast);

    // [確認_正常系] - 双方向系と TCP 系が potr_type_uses_immediate_health_ping で真と判定されること。
    EXPECT_TRUE(immediate_unicast_bidir);
    EXPECT_TRUE(immediate_unicast_bidir_n1);
    EXPECT_TRUE(immediate_tcp);
    EXPECT_TRUE(immediate_tcp_bidir);
}
