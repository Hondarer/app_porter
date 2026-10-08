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
    // [サブ手順 名前=potrSendTest.SetUp]
    void SetUp() override
    {
        memset(&ctx, 0, sizeof(ctx));
        memset(peers, 0, sizeof(peers));

        ctx.service.service_id = 42;
        ctx.global.max_payload = 1400;
        ctx.global.max_message_size = 4096;
        cplat_atomic_store_i32(&ctx.send_thread_running, 1, CPLAT_MEMORY_ORDER_RELAXED);
        ctx.max_peers = (int)(sizeof(peers) / sizeof(peers[0]));
        ctx.peers = peers;

        ASSERT_EQ(POTR_OK, potr_internal_send_queue_init(&ctx.send_queue, 8, 1400));
        // [状態確認] - `potr_internal_send_queue_init(&ctx.send_queue, 8, 1400)` の戻り値が `POTR_OK` であること。
        cplat_local_lock_create(&ctx.peers_mutex);
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrSendTest.TearDown]
    void TearDown() override
    {
        cplat_local_lock_dispose(ctx.peers_mutex);
        potr_internal_send_queue_dispose(&ctx.send_queue);
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrSendTest.popQueuedElem]
    potr_internal_payload_elem popQueuedElem()
    {
        potr_internal_payload_elem elem = {};
        EXPECT_EQ(POTR_OK, potr_internal_send_queue_try_pop(&ctx.send_queue, &elem));
        // [状態確認] - `potr_internal_send_queue_try_pop(&ctx.send_queue, &elem)` の戻り値が `POTR_OK` であること。
        return elem;
    }
    // [サブ手順終了]

    potr_context ctx;
    potr_internal_peer_context peers[2];
};

// 終了処理中の送信が中止コードで拒否されることの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, close_requested_returns_canceled)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "closing"; // [状態] - 送信ペイロードを "closing" とする。
    cplat_atomic_store_i32(&ctx.close_requested, 1,
                           CPLAT_MEMORY_ORDER_RELAXED); // [状態] - サービスの終了処理中とする。

    // Pre-Assert

    // Act
    int actual_ret =
        potr_service_send(&ctx, POTR_PEER_NA, payload, strlen(payload), 0); // [手順] - 終了処理中に送信を試みる。

    // Assert
    EXPECT_EQ(POTR_ERR_CANCELED,
              actual_ret);               // [確認_異常系] - potr_service_send の戻り値が POTR_ERR_CANCELED であること。
    EXPECT_EQ(0U, ctx.send_queue.count); // [確認_異常系] - 送信キューに積まれないこと。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// N:1 モードで POTR_PEER_NA を指定すると引数不正になることの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
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
    int actual_ret = potr_service_send(&ctx, POTR_PEER_NA, payload, strlen(payload),
                                       0); // [手順] - POTR_PEER_NA 宛てに送信を試みる。

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT,
              actual_ret); // [確認_異常系] - potr_service_send の戻り値が POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(0U, ctx.send_queue.count); // [確認_異常系] - 送信キューに積まれないこと。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// N:1 モードで存在しないピアを指定すると未検出コードになることの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
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
    int actual_ret =
        potr_service_send(&ctx, 123U, payload, strlen(payload), 0); // [手順] - 未登録のピア ID 宛てに送信を試みる。

    // Assert
    EXPECT_EQ(POTR_ERR_NOT_FOUND,
              actual_ret);               // [確認_異常系] - potr_service_send の戻り値が POTR_ERR_NOT_FOUND であること。
    EXPECT_EQ(0U, ctx.send_queue.count); // [確認_異常系] - 送信キューに積まれないこと。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// TCP は物理パスが active でも論理接続前の送信が拒否されることの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, tcp_requires_logical_connected_even_with_active_path)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "tcp-before-connected"; // [状態] - 送信ペイロードを "tcp-before-connected" とする。

    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    cplat_atomic_store_i32(&ctx.tcp_active_paths, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(
        &ctx.health_alive, 0,
        CPLAT_MEMORY_ORDER_RELAXED); // [状態] - TCP_BIDIR で物理パスは active、論理接続 (health_alive) は未成立とする。

    // Pre-Assert

    // Act
    int actual_ret = potr_service_send(&ctx, POTR_PEER_NA, payload, strlen(payload),
                                       0); // [手順] - potr_service_send で送信を試みる。

    // Assert
    EXPECT_EQ(POTR_ERR_DISCONNECTED,
              actual_ret); // [確認_異常系] - potr_service_send の戻り値が POTR_ERR_DISCONNECTED であること。
    EXPECT_EQ(0U, ctx.send_queue.count); // [確認_異常系] - 送信キューに積まれないこと。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// N:1 の全 peer 送信で接続済み peer が 1 件もない場合に切断エラーとなることの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
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
    cplat_atomic_store_i32(
        &peers[0].health_alive, 0,
        CPLAT_MEMORY_ORDER_RELAXED); // [状態] - active だが未接続 (health_alive=0) の peer を 1 件だけ用意する。

    // Pre-Assert

    // Act
    int actual_ret = potr_service_send(&ctx, POTR_PEER_ALL, payload, strlen(payload),
                                       0); // [手順] - POTR_PEER_ALL 宛てに potr_service_send で送信を試みる。

    // Assert
    EXPECT_EQ(POTR_ERR_DISCONNECTED,
              actual_ret); // [確認_異常系] - potr_service_send の戻り値が POTR_ERR_DISCONNECTED であること。
    EXPECT_EQ(0U, ctx.send_queue.count); // [確認_異常系] - 送信キューに格納されないこと。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// N:1 の全 peer 送信が接続済み peer だけへ送信されることの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
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
    cplat_atomic_store_i32(&peers[0].health_alive, 1,
                           CPLAT_MEMORY_ORDER_RELAXED); // [状態] - 接続済み (health_alive=1) の peer 10 を用意する。
    peers[1].active = 1;
    peers[1].peer_id = 11;
    cplat_atomic_store_i32(&peers[1].health_alive, 0,
                           CPLAT_MEMORY_ORDER_RELAXED); // [状態] - 未接続の peer 11 を用意する。

    // Pre-Assert

    // Act
    int actual_ret = potr_service_send(&ctx, POTR_PEER_ALL, payload, strlen(payload),
                                       0); // [手順] - POTR_PEER_ALL 宛てに potr_service_send で送信する。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);      // [確認_正常系] - potr_service_send の戻り値が POTR_OK であること。
    EXPECT_EQ(1U, ctx.send_queue.count); // [確認_正常系] - 送信キューに 1 件だけ積まれること。

    {
        // [サブ手順参照 名前=potrSendTest.popQueuedElem]
        potr_internal_payload_elem elem = popQueuedElem();
        EXPECT_EQ((potr_peer_id)10, elem.peer_id);            // [確認_正常系] - 宛先が接続済みの peer 10 であること。
        EXPECT_EQ(strlen(payload), (size_t)elem.payload_len); // [確認_正常系] - ペイロード長が一致すること。
        EXPECT_EQ(0, memcmp(elem.payload, payload, strlen(payload))); // [確認_正常系] - ペイロード内容が一致すること。
    }
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// 片方向 unicast は接続状態がなくても送信できることの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, unicast_sender_path_still_sends_without_connected_state)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "one-way-still-sendable"; // [状態] - 送信ペイロードを "one-way-still-sendable" とする。

    ctx.service.type = POTR_TYPE_UNICAST;
    cplat_atomic_store_i32(
        &ctx.health_alive, 0,
        CPLAT_MEMORY_ORDER_RELAXED); // [状態] - 片方向 unicast で接続状態 (health_alive) は未成立とする。

    // Pre-Assert

    // Act
    int actual_ret =
        potr_service_send(&ctx, POTR_PEER_NA, payload, strlen(payload), 0); // [手順] - potr_service_send で送信する。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);      // [確認_正常系] - potr_service_send の戻り値が POTR_OK であること。
    EXPECT_EQ(1U, ctx.send_queue.count); // [確認_正常系] - 送信キューに 1 件積まれること。

    {
        // [サブ手順参照 名前=potrSendTest.popQueuedElem]
        potr_internal_payload_elem elem = popQueuedElem();
        EXPECT_EQ(POTR_PEER_NA, elem.peer_id);                // [確認_正常系] - 宛先が POTR_PEER_NA であること。
        EXPECT_EQ(strlen(payload), (size_t)elem.payload_len); // [確認_正常系] - ペイロード長が一致すること。
        EXPECT_EQ(0, memcmp(elem.payload, payload, strlen(payload))); // [確認_正常系] - ペイロード内容が一致すること。
    }
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// データ送信による health ping 抑止が type 1〜6 (片方向 UDP 系) だけに適用されることの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, data_based_health_ping_suppression_applies_only_to_type_1_to_6)
{
    // Arrange

    // Pre-Assert

    // Act
    // [手順] - 各 type を is_oneway_udp_type で判定する。
    int oneway_unicast_raw = is_oneway_udp_type(POTR_TYPE_UNICAST_RAW);
    int oneway_multicast_raw = is_oneway_udp_type(POTR_TYPE_MULTICAST_RAW);
    int oneway_broadcast_raw = is_oneway_udp_type(POTR_TYPE_BROADCAST_RAW);
    int oneway_unicast = is_oneway_udp_type(POTR_TYPE_UNICAST);
    int oneway_multicast = is_oneway_udp_type(POTR_TYPE_MULTICAST);
    int oneway_broadcast = is_oneway_udp_type(POTR_TYPE_BROADCAST);
    int oneway_unicast_bidir = is_oneway_udp_type(POTR_TYPE_UNICAST_BIDIR);
    int oneway_unicast_bidir_n1 = is_oneway_udp_type(POTR_TYPE_UNICAST_BIDIR_N1);
    int oneway_tcp = is_oneway_udp_type(POTR_TYPE_TCP);
    int oneway_tcp_bidir = is_oneway_udp_type(POTR_TYPE_TCP_BIDIR);

    // Assert
    // 片方向 UDP 系の type 1〜6 が is_oneway_udp_type で真と判定されること。
    EXPECT_TRUE(oneway_unicast_raw);
    // [確認_正常系] - `oneway_unicast_raw` が true であること。
    EXPECT_TRUE(oneway_multicast_raw);
    // [確認_正常系] - `oneway_multicast_raw` が true であること。
    EXPECT_TRUE(oneway_broadcast_raw);
    // [確認_正常系] - `oneway_broadcast_raw` が true であること。
    EXPECT_TRUE(oneway_unicast);
    // [確認_正常系] - `oneway_unicast` が true であること。
    EXPECT_TRUE(oneway_multicast);
    // [確認_正常系] - `oneway_multicast` が true であること。
    EXPECT_TRUE(oneway_broadcast);
    // [確認_正常系] - `oneway_broadcast` が true であること。

    // 双方向系と TCP 系が is_oneway_udp_type で偽と判定されること。
    EXPECT_FALSE(oneway_unicast_bidir);
    // [確認_正常系] - `oneway_unicast_bidir` が false であること。
    EXPECT_FALSE(oneway_unicast_bidir_n1);
    // [確認_正常系] - `oneway_unicast_bidir_n1` が false であること。
    EXPECT_FALSE(oneway_tcp);
    // [確認_正常系] - `oneway_tcp` が false であること。
    EXPECT_FALSE(oneway_tcp_bidir);
    // [確認_正常系] - `oneway_tcp_bidir` が false であること。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// 接続直後の immediate health ping が type 1〜6 (片方向 UDP 系) だけで無効になることの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, immediate_health_ping_is_disabled_only_for_type_1_to_6)
{
    // Arrange

    // Pre-Assert

    // Act
    // [手順] - 各 type を type_uses_immediate_health_ping で判定する。
    int immediate_unicast_raw = type_uses_immediate_health_ping(POTR_TYPE_UNICAST_RAW);
    int immediate_multicast_raw = type_uses_immediate_health_ping(POTR_TYPE_MULTICAST_RAW);
    int immediate_broadcast_raw = type_uses_immediate_health_ping(POTR_TYPE_BROADCAST_RAW);
    int immediate_unicast = type_uses_immediate_health_ping(POTR_TYPE_UNICAST);
    int immediate_multicast = type_uses_immediate_health_ping(POTR_TYPE_MULTICAST);
    int immediate_broadcast = type_uses_immediate_health_ping(POTR_TYPE_BROADCAST);
    int immediate_unicast_bidir = type_uses_immediate_health_ping(POTR_TYPE_UNICAST_BIDIR);
    int immediate_unicast_bidir_n1 = type_uses_immediate_health_ping(POTR_TYPE_UNICAST_BIDIR_N1);
    int immediate_tcp = type_uses_immediate_health_ping(POTR_TYPE_TCP);
    int immediate_tcp_bidir = type_uses_immediate_health_ping(POTR_TYPE_TCP_BIDIR);

    // Assert
    // 片方向 UDP 系の type 1〜6 が type_uses_immediate_health_ping で偽と判定されること。
    EXPECT_FALSE(immediate_unicast_raw);
    // [確認_正常系] - `immediate_unicast_raw` が false であること。
    EXPECT_FALSE(immediate_multicast_raw);
    // [確認_正常系] - `immediate_multicast_raw` が false であること。
    EXPECT_FALSE(immediate_broadcast_raw);
    // [確認_正常系] - `immediate_broadcast_raw` が false であること。
    EXPECT_FALSE(immediate_unicast);
    // [確認_正常系] - `immediate_unicast` が false であること。
    EXPECT_FALSE(immediate_multicast);
    // [確認_正常系] - `immediate_multicast` が false であること。
    EXPECT_FALSE(immediate_broadcast);
    // [確認_正常系] - `immediate_broadcast` が false であること。

    // 双方向系と TCP 系が type_uses_immediate_health_ping で真と判定されること。
    EXPECT_TRUE(immediate_unicast_bidir);
    // [確認_正常系] - `immediate_unicast_bidir` が true であること。
    EXPECT_TRUE(immediate_unicast_bidir_n1);
    // [確認_正常系] - `immediate_unicast_bidir_n1` が true であること。
    EXPECT_TRUE(immediate_tcp);
    // [確認_正常系] - `immediate_tcp` が true であること。
    EXPECT_TRUE(immediate_tcp_bidir);
    // [確認_正常系] - `immediate_tcp_bidir` が true であること。
}
// [サブ手順参照 名前=potrSendTest.TearDown]
