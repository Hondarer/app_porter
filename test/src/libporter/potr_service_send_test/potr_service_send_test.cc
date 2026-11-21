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
#include <porter/potr_handle.h>
#include <porter/infra/potr_send_queue.h>

#if defined(PLATFORM_LINUX)
    #include <pthread.h>
#elif defined(PLATFORM_WINDOWS)
    #include <cplat/base/windows_sdk.h>
#endif /* PLATFORM_ */
#include <string.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace testing;

class potrSendTest : public Test
{
  protected:
    // [サブ手順 名前=potrSendTest.SetUp]
    void SetUp() override
    {
        memset(&ctx, 0, sizeof(ctx));
        ASSERT_EQ(POTR_OK, potr_internal_handle_prepare(&ctx));
        // [状態確認] - potr_internal_handle_prepare() が成功すること。
        handle = potr_internal_handle_public(&ctx);
        memset(peers, 0, sizeof(peers));

        ctx.service.service_id = 42;
        ctx.global.max_payload = 1400;
        ctx.global.max_message_size = 4096;
        cplat_atomic_store_i32(&ctx.send_thread_running, 1, CPLAT_MEMORY_ORDER_RELAXED);
        ctx.max_peers = (int)(sizeof(peers) / sizeof(peers[0]));
        ctx.peers = peers;

        ASSERT_EQ(POTR_OK, potr_internal_send_queue_init(&ctx.send_queue, 8, 1400));
        // [状態確認] - potr_internal_send_queue_init() の戻り値が POTR_OK であること。
        cplat_local_lock_create(&ctx.peers_mutex);
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrSendTest.TearDown]
    void TearDown() override
    {
        potr_internal_handle_discard(&ctx);
        cplat_local_lock_dispose(ctx.peers_mutex);
        potr_internal_send_queue_dispose(&ctx.send_queue);
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrSendTest.popQueuedElem]
    potr_internal_payload_elem popQueuedElem()
    {
        potr_internal_payload_elem elem = {};
        EXPECT_EQ(POTR_OK, potr_internal_send_queue_try_pop(&ctx.send_queue, &elem));
        // [状態確認] - potr_internal_send_queue_try_pop() の戻り値が POTR_OK であること。
        return elem;
    }
    // [サブ手順終了]

    potr_context *handle = nullptr;
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
    int actual_ret = potr_service_send(handle, POTR_PEER_NA, payload, strlen(payload),
                                       0); // [手順] - 終了処理中のサービスを指定して potr_service_send() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERR_CANCELED,
              actual_ret); // [確認_異常系] - 終了処理中の potr_service_send() の戻り値が POTR_ERR_CANCELED であること。
    EXPECT_EQ(
        0U,
        ctx.send_queue
            .count); // [確認_異常系] - ctx.send_queue.count が 0U であること。(終了処理中の送信がキューに積まれないことの確認)
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
    int actual_ret =
        potr_service_send(handle, POTR_PEER_NA, payload, strlen(payload),
                          0); // [手順] - peer_id に POTR_PEER_NA を指定して potr_service_send() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_INVALID_ARGUMENT,
        actual_ret); // [確認_異常系] - POTR_PEER_NA を指定した potr_service_send() の戻り値が POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(
        0U,
        ctx.send_queue
            .count); // [確認_異常系] - ctx.send_queue.count が 0U であること。(引数不正のためキューに積まれないことの確認)
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// N:1 モードで自サービスのコールバック内から送信を受け付けることの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, n1_call_from_own_callback_is_accepted)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_porter;
    const char payload[] = "n1-in-callback"; // [状態] - 送信ペイロードを "n1-in-callback" とする。
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1; // [状態] - N:1 モードとする。
    peers[0].active = 1;
    peers[0].peer_id = 10;
    cplat_atomic_store_i32(&peers[0].health_alive, 1,
                           CPLAT_MEMORY_ORDER_RELAXED); // [状態] - 接続済みの peer 10 を用意する。
    ON_CALL(mock_porter, potr_internal_in_own_callback(&ctx))
        .WillByDefault(Return(1)); // [状態] - 自サービスのコールバック内からの呼び出しとする。

    ON_CALL(mock_porter, potr_internal_peer_find_by_id(&ctx, 10U)).WillByDefault(Return(&peers[0]));
    // [状態] - 接続済みピアを検索できるようにする。

    // Pre-Assert

    // Act
    int actual_ret = potr_service_send(handle, 10U, payload, strlen(payload),
                                       0); // [手順] - 接続済みの peer 10 を指定して potr_service_send() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_OK,
              actual_ret); // [確認_正常系] - コールバック内からの potr_service_send() の戻り値が POTR_OK であること。
    EXPECT_EQ(
        1U,
        ctx.send_queue
            .count); // [確認_正常系] - ctx.send_queue.count が 1U であること。(コールバック内からの送信がキューに積まれることの確認)
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// N:1 以外では自サービスのコールバック内からでも送信できることの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, non_n1_call_from_own_callback_is_accepted)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_porter;
    const char payload[] = "reply-in-callback"; // [状態] - 送信ペイロードを "reply-in-callback" とする。
    ctx.service.type = POTR_TYPE_UNICAST;       // [状態] - 片方向 unicast とする。
    ON_CALL(mock_porter, potr_internal_in_own_callback(&ctx))
        .WillByDefault(Return(1)); // [状態] - 自サービスのコールバック内からの呼び出しとする。

    // Pre-Assert

    // Act
    int actual_ret =
        potr_service_send(handle, POTR_PEER_NA, payload, strlen(payload),
                          0); // [手順] - 片方向 unicast のサービスを指定して potr_service_send() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_OK,
        actual_ret); // [確認_正常系] - N:1 以外のコールバック内からの potr_service_send() の戻り値が POTR_OK であること。
    EXPECT_EQ(1U, ctx.send_queue.count); // [確認_正常系] - ctx.send_queue.count が 1U であること。
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
    int actual_ret = potr_service_send(handle, 123U, payload, strlen(payload),
                                       0); // [手順] - 未登録のピア ID を指定して potr_service_send() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_NOT_FOUND,
        actual_ret); // [確認_異常系] - 未登録の peer を指定した potr_service_send() の戻り値が POTR_ERR_NOT_FOUND であること。
    EXPECT_EQ(
        0U,
        ctx.send_queue
            .count); // [確認_異常系] - ctx.send_queue.count が 0U であること。(未登録の peer への送信がキューに積まれないことの確認)
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
    int actual_ret =
        potr_service_send(handle, POTR_PEER_NA, payload, strlen(payload),
                          0); // [手順] - 論理接続が未成立のサービスを指定して potr_service_send() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_DISCONNECTED,
        actual_ret); // [確認_異常系] - 論理接続前の TCP に対する potr_service_send() の戻り値が POTR_ERR_DISCONNECTED であること。
    EXPECT_EQ(
        0U,
        ctx.send_queue
            .count); // [確認_異常系] - ctx.send_queue.count が 0U であること。(論理接続前の送信がキューに積まれないことの確認)
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
    int actual_ret =
        potr_service_send(handle, POTR_PEER_ALL, payload, strlen(payload),
                          0); // [手順] - peer_id に POTR_PEER_ALL を指定して potr_service_send() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_DISCONNECTED,
        actual_ret); // [確認_異常系] - 接続済み peer がない場合の potr_service_send() の戻り値が POTR_ERR_DISCONNECTED であること。
    EXPECT_EQ(
        0U,
        ctx.send_queue
            .count); // [確認_異常系] - ctx.send_queue.count が 0U であること。(接続済み peer がないためキューに積まれないことの確認)
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
    int actual_ret =
        potr_service_send(handle, POTR_PEER_ALL, payload, strlen(payload),
                          0); // [手順] - peer_id に POTR_PEER_ALL を指定して potr_service_send() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_OK,
        actual_ret); // [確認_正常系] - 接続済み peer がある場合の potr_service_send() の戻り値が POTR_OK であること。
    EXPECT_EQ(
        1U,
        ctx.send_queue
            .count); // [確認_正常系] - ctx.send_queue.count が 1U であること。(接続済み peer への送信だけが 1 件キューに積まれたことの確認)

    // Act_2
    // [サブ手順参照 名前=potrSendTest.popQueuedElem]
    potr_internal_payload_elem elem = popQueuedElem();

    // Assert_2
    EXPECT_EQ(
        (potr_peer_id)10,
        elem.peer_id); // [確認_正常系] - elem.peer_id が 10 と一致すること。(接続済みの peer 10 だけが宛先であることの確認)
    EXPECT_EQ(
        strlen(payload),
        (size_t)
            elem.payload_len); // [確認_正常系] - elem.payload_len が payload に対する strlen() の戻り値と一致すること。
    EXPECT_EQ(
        0,
        memcmp(
            elem.payload, payload,
            strlen(
                payload))); // [確認_正常系] - memcmp() の戻り値が 0 であること。(キューに積んだ内容が送信ペイロードと一致することの確認)
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// TCP のブロッキング送信で、送信スレッドが送信失敗として完了させた場合に失敗を返すことの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, tcp_blocking_send_returns_recorded_send_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "tcp-send-failure"; // [状態] - 送信ペイロードを "tcp-send-failure" とする。
    potr_internal_payload_elem elem = {};
    int actual_ret = POTR_OK;

    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    cplat_atomic_store_i32(&ctx.tcp_active_paths, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(
        &ctx.health_alive, 1,
        CPLAT_MEMORY_ORDER_RELAXED); // [状態] - TCP_BIDIR で物理パスと論理接続が成立しているものとする。

    // Pre-Assert

    // Act
    std::thread sender(
        [&]()
        {
            actual_ret = potr_service_send(handle, POTR_PEER_NA, payload, strlen(payload), POTR_SEND_BLOCKING);
        }); // [手順] - 別スレッドで POTR_SEND_BLOCKING を指定して potr_service_send() を呼び出す。
    while (potr_internal_send_queue_try_pop(&ctx.send_queue, &elem) != POTR_OK)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } // [手順] - 送信スレッドの代わりに、登録された 1 件を取り出す。
    potr_internal_send_queue_complete_failed(
        &ctx.send_queue,
        POTR_ERR_DISCONNECTED); // [手順] - 取り出した 1 件を送信失敗として完了させる。
    sender.join();

    // Assert
    EXPECT_EQ(POTR_ERR_DISCONNECTED, actual_ret);
    // [確認_異常系] - ブロッキング送信の potr_service_send() の戻り値が、記録した POTR_ERR_DISCONNECTED であること。
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
        potr_service_send(handle, POTR_PEER_NA, payload, strlen(payload),
                          0); // [手順] - 片方向 unicast のサービスを指定して potr_service_send() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_OK,
        actual_ret); // [確認_正常系] - 接続状態がない片方向 unicast に対する potr_service_send() の戻り値が POTR_OK であること。
    EXPECT_EQ(
        1U,
        ctx.send_queue
            .count); // [確認_正常系] - ctx.send_queue.count が 1U であること。(接続状態がなくても送信が 1 件キューに積まれたことの確認)

    // Act_2
    // [サブ手順参照 名前=potrSendTest.popQueuedElem]
    potr_internal_payload_elem elem = popQueuedElem();

    // Assert_2
    EXPECT_EQ(POTR_PEER_NA, elem.peer_id); // [確認_正常系] - elem.peer_id が POTR_PEER_NA であること。
    EXPECT_EQ(
        strlen(payload),
        (size_t)
            elem.payload_len); // [確認_正常系] - elem.payload_len が payload に対する strlen() の戻り値と一致すること。
    EXPECT_EQ(
        0,
        memcmp(
            elem.payload, payload,
            strlen(
                payload))); // [確認_正常系] - memcmp() の戻り値が 0 であること。(キューに積んだ内容が送信ペイロードと一致することの確認)
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// データ送信による health ping 抑止が type 1〜6 (片方向 UDP 系) だけに適用されることの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, data_based_health_ping_suppression_applies_only_to_type_1_to_6)
{
    // Arrange

    // Pre-Assert

    // Act
    // [手順] - 各 type を指定して is_oneway_udp_type() を呼び出す。
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
    // [確認_正常系] - POTR_TYPE_UNICAST_RAW に対する is_oneway_udp_type() の戻り値が true であること。
    EXPECT_TRUE(oneway_multicast_raw);
    // [確認_正常系] - POTR_TYPE_MULTICAST_RAW に対する is_oneway_udp_type() の戻り値が true であること。
    EXPECT_TRUE(oneway_broadcast_raw);
    // [確認_正常系] - POTR_TYPE_BROADCAST_RAW に対する is_oneway_udp_type() の戻り値が true であること。
    EXPECT_TRUE(oneway_unicast);
    // [確認_正常系] - POTR_TYPE_UNICAST に対する is_oneway_udp_type() の戻り値が true であること。
    EXPECT_TRUE(oneway_multicast);
    // [確認_正常系] - POTR_TYPE_MULTICAST に対する is_oneway_udp_type() の戻り値が true であること。
    EXPECT_TRUE(oneway_broadcast);
    // [確認_正常系] - POTR_TYPE_BROADCAST に対する is_oneway_udp_type() の戻り値が true であること。

    // 双方向系と TCP 系が is_oneway_udp_type で偽と判定されること。
    EXPECT_FALSE(oneway_unicast_bidir);
    // [確認_正常系] - POTR_TYPE_UNICAST_BIDIR に対する is_oneway_udp_type() の戻り値が false であること。
    EXPECT_FALSE(oneway_unicast_bidir_n1);
    // [確認_正常系] - POTR_TYPE_UNICAST_BIDIR_N1 に対する is_oneway_udp_type() の戻り値が false であること。
    EXPECT_FALSE(oneway_tcp);
    // [確認_正常系] - POTR_TYPE_TCP に対する is_oneway_udp_type() の戻り値が false であること。
    EXPECT_FALSE(oneway_tcp_bidir);
    // [確認_正常系] - POTR_TYPE_TCP_BIDIR に対する is_oneway_udp_type() の戻り値が false であること。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// 接続直後の immediate health ping が type 1〜6 (片方向 UDP 系) だけで無効になることの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, immediate_health_ping_is_disabled_only_for_type_1_to_6)
{
    // Arrange

    // Pre-Assert

    // Act
    // [手順] - 各 type を指定して type_uses_immediate_health_ping() を呼び出す。
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
    // [確認_正常系] - POTR_TYPE_UNICAST_RAW に対する type_uses_immediate_health_ping() の戻り値が false であること。
    EXPECT_FALSE(immediate_multicast_raw);
    // [確認_正常系] - POTR_TYPE_MULTICAST_RAW に対する type_uses_immediate_health_ping() の戻り値が false であること。
    EXPECT_FALSE(immediate_broadcast_raw);
    // [確認_正常系] - POTR_TYPE_BROADCAST_RAW に対する type_uses_immediate_health_ping() の戻り値が false であること。
    EXPECT_FALSE(immediate_unicast);
    // [確認_正常系] - POTR_TYPE_UNICAST に対する type_uses_immediate_health_ping() の戻り値が false であること。
    EXPECT_FALSE(immediate_multicast);
    // [確認_正常系] - POTR_TYPE_MULTICAST に対する type_uses_immediate_health_ping() の戻り値が false であること。
    EXPECT_FALSE(immediate_broadcast);
    // [確認_正常系] - POTR_TYPE_BROADCAST に対する type_uses_immediate_health_ping() の戻り値が false であること。

    // 双方向系と TCP 系が type_uses_immediate_health_ping で真と判定されること。
    EXPECT_TRUE(immediate_unicast_bidir);
    // [確認_正常系] - POTR_TYPE_UNICAST_BIDIR に対する type_uses_immediate_health_ping() の戻り値が true であること。
    EXPECT_TRUE(immediate_unicast_bidir_n1);
    // [確認_正常系] - POTR_TYPE_UNICAST_BIDIR_N1 に対する type_uses_immediate_health_ping() の戻り値が true であること。
    EXPECT_TRUE(immediate_tcp);
    // [確認_正常系] - POTR_TYPE_TCP に対する type_uses_immediate_health_ping() の戻り値が true であること。
    EXPECT_TRUE(immediate_tcp_bidir);
    // [確認_正常系] - POTR_TYPE_TCP_BIDIR に対する type_uses_immediate_health_ping() の戻り値が true であること。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// 片方向の受信専用サービスからの送信を拒否することの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, receive_only_service_rejects_send)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.service.type = POTR_TYPE_UNICAST;
    char data = 'r';
    // Pre-Assert
    // Act
    int actual_ret = potr_service_send(handle, POTR_PEER_NA, &data, sizeof(data), 0);
    // [手順] - 受信専用ハンドルから送信する。
    // Assert
    EXPECT_EQ(POTR_ERR_UNSUPPORTED, actual_ret);
    // [確認_異常系] - 送信キューを使う前に未対応の操作を拒否すること。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// 境界サイズ (max_message_size) の非圧縮性データを COMPRESS 指定で送っても非圧縮へフォールバックし送信できることの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, incompressible_boundary_message_falls_back_to_uncompressed)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    ctx.role = POTR_ROLE_SENDER;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.global.max_message_size = 256;
    ctx.compress_buf_size = ctx.global.max_message_size;
    std::vector<uint8_t> data(ctx.global.max_message_size);
    uint32_t state = 0x12345678U;
    for (size_t i = 0; i < data.size(); i++)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        data[i] = static_cast<uint8_t>(state & 0xFFU);
    }
    // [状態] - compress_buf_size を max_message_size に一致させ、圧縮で小さくならない境界長 256 バイトの入力を用意する。

    // Pre-Assert

    // Act
    int actual_ret = potr_service_send(handle, POTR_PEER_NA, data.data(), data.size(), POTR_SEND_COMPRESS);
    potr_internal_payload_elem queued = popQueuedElem();
    // [手順] - POTR_SEND_COMPRESS 指定で送信し、送信キューへ積まれたエレメントを取り出す。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 圧縮先容量超過でも非圧縮フォールバックで送信成功すること。
    EXPECT_EQ(0U, static_cast<unsigned>(queued.flags & POTR_FLAG_COMPRESSED));
    // [確認_正常系] - キューへ積まれたエレメントに POTR_FLAG_COMPRESSED が付かないこと。
    EXPECT_EQ(data.size(), static_cast<size_t>(queued.payload_len));
    // [確認_正常系] - キューへ積まれた長さが元のデータ長と一致すること。
    EXPECT_EQ(0, memcmp(queued.payload, data.data(), data.size()));
    // [確認_正常系] - キューへ積まれたデータが元の非圧縮データと一致すること。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// CPLAT_ERR_BUFFER_TOO_SMALL 以外の圧縮失敗では従来通り POTR_ERR_UNKNOWN を返しキューへ積まないことの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, other_compress_error_returns_unknown_without_enqueue)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    ctx.role = POTR_ROLE_SENDER;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.global.max_message_size = 256;
    ctx.compress_buf_size = ctx.global.max_message_size;
    std::vector<uint8_t> data(64, 0x5AU);
    potr_internal_payload_elem queued = {};
    // [状態] - 圧縮時に CPLAT_ERR_UNKNOWN を返す条件を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_compress(_, _, _, _)).Times(1).WillOnce(Return(CPLAT_ERR_UNKNOWN));
    // [Pre-Assert確認_異常系] - cplat_compress() が 1 回呼ばれること。
    // [Pre-Assert手順] - CPLAT_ERR_UNKNOWN を返す。

    // Act
    int actual_ret = potr_service_send(handle, POTR_PEER_NA, data.data(), data.size(), POTR_SEND_COMPRESS);
    int pop_ret = potr_internal_send_queue_try_pop(&ctx.send_queue, &queued);
    // [手順] - 圧縮エラー発生時の送信結果と送信キュー状態を確認する。

    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN,
              actual_ret); // [確認_異常系] - BUFFER_TOO_SMALL 以外の圧縮失敗は POTR_ERR_UNKNOWN を返すこと。
    EXPECT_EQ(POTR_ERR_EMPTY, pop_ret); // [確認_異常系] - 送信キューにデータが積まれないこと。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// 待ち合わせを指定しない送信が、送信キューに空きがない場合に待たずに容量不足を返すことの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, nonblocking_send_returns_full_when_queue_has_no_space)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "queue-full"; // [状態] - 送信ペイロードを "queue-full" とする。
    ctx.role = POTR_ROLE_SENDER;
    ctx.service.type = POTR_TYPE_UNICAST;
    for (int i = 0; i < 8; i++)
    {
        ASSERT_EQ(POTR_OK, potr_service_send(handle, POTR_PEER_NA, payload, strlen(payload), 0));
        // [状態確認] - 容量 8 の送信キューを満杯にする送信 (8 回) の戻り値が POTR_OK であること。
    }

    // Pre-Assert

    // Act
    int actual_ret =
        potr_service_send(handle, POTR_PEER_NA, payload, strlen(payload),
                          0); // [手順] - 送信キューが満杯の状態で、待ち合わせを指定せずに potr_service_send() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERR_FULL,
              actual_ret); // [確認_異常系] - 満杯時の potr_service_send() の戻り値が POTR_ERR_FULL であること。
    EXPECT_EQ(
        8U,
        ctx.send_queue
            .count); // [確認_異常系] - ctx.send_queue.count が 8U のままであること。(受け付けなかったメッセージが登録されないことの確認)
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// 待ち合わせを指定しない送信が、全断片分の空きがない場合に断片を 1 つも登録しないことの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, nonblocking_send_does_not_queue_part_of_fragmented_message)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "filler"; // [状態] - 空きを減らすための送信ペイロードを "filler" とする。
    std::vector<uint8_t> large(3000, 0x5AU); // [状態] - 3 断片になる 3000 バイトのメッセージを用意する。
    ctx.role = POTR_ROLE_SENDER;
    ctx.service.type = POTR_TYPE_UNICAST;
    for (int i = 0; i < 6; i++)
    {
        ASSERT_EQ(POTR_OK, potr_service_send(handle, POTR_PEER_NA, payload, strlen(payload), 0));
        // [状態確認] - 容量 8 の送信キューの空きを 2 にする送信 (6 回) の戻り値が POTR_OK であること。
    }

    // Pre-Assert

    // Act
    int actual_ret =
        potr_service_send(handle, POTR_PEER_NA, large.data(), large.size(),
                          0); // [手順] - 空きが 2 の状態で、3 断片になるメッセージを待ち合わせを指定せずに送信する。

    // Assert
    EXPECT_EQ(POTR_ERR_FULL,
              actual_ret); // [確認_異常系] - 空きが断片数に満たない場合の戻り値が POTR_ERR_FULL であること。
    EXPECT_EQ(
        6U,
        ctx.send_queue
            .count); // [確認_異常系] - ctx.send_queue.count が 6U のままであること。(一部の断片だけが登録されないことの確認)

    // Act_2
    potr_internal_payload_elem elem = {};
    ASSERT_EQ(POTR_OK, potr_internal_send_queue_try_pop(&ctx.send_queue, &elem));
    // [状態確認] - potr_internal_send_queue_try_pop() の戻り値が POTR_OK であること。
    potr_internal_send_queue_complete(&ctx.send_queue); // [手順] - 1 件を取り出して完了させ、空きを 3 にする。
    int actual_ret_2 =
        potr_service_send(handle, POTR_PEER_NA, large.data(), large.size(),
                          0); // [手順] - 空きが 3 の状態で、同じメッセージを待ち合わせを指定せずに送信する。

    // Assert_2
    EXPECT_EQ(POTR_OK, actual_ret_2); // [確認_正常系] - 全断片分の空きがある場合の戻り値が POTR_OK であること。
    EXPECT_EQ(8U, ctx.send_queue.count); // [確認_正常系] - ctx.send_queue.count が 8U であること。(3 断片がすべて登録されたことの確認)
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// 待ち合わせを指定しない全 peer 宛の送信が、全 peer 分の空きがない場合にどの peer 宛も登録しないことの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, nonblocking_peer_all_queues_no_peer_when_space_is_short)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "n1-all"; // [状態] - 送信ペイロードを "n1-all" とする。

    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1;
    peers[0].active = 1;
    peers[0].peer_id = 10;
    cplat_atomic_store_i32(&peers[0].health_alive, 1, CPLAT_MEMORY_ORDER_RELAXED);
    peers[1].active = 1;
    peers[1].peer_id = 11;
    cplat_atomic_store_i32(&peers[1].health_alive, 1,
                           CPLAT_MEMORY_ORDER_RELAXED); // [状態] - 接続済みの peer 10 と peer 11 を用意する。
    EXPECT_CALL(mock_peer_table, potr_internal_peer_find_by_id(_, 10)).WillRepeatedly(Return(&peers[0]));
    // [状態] - peer 10 を検索できるようにする。
    for (int i = 0; i < 7; i++)
    {
        ASSERT_EQ(POTR_OK, potr_service_send(handle, 10, payload, strlen(payload), 0));
        // [状態確認] - 容量 8 の送信キューの空きを 1 にする送信 (7 回) の戻り値が POTR_OK であること。
    }

    // Pre-Assert

    // Act
    int actual_ret =
        potr_service_send(handle, POTR_PEER_ALL, payload, strlen(payload),
                          0); // [手順] - 空きが 1 の状態で、2 つの peer 宛になる POTR_PEER_ALL の送信を待ち合わせを指定せずに呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERR_FULL,
              actual_ret); // [確認_異常系] - 全 peer 分の空きがない場合の戻り値が POTR_ERR_FULL であること。
    EXPECT_EQ(
        7U,
        ctx.send_queue
            .count); // [確認_異常系] - ctx.send_queue.count が 7U のままであること。(どの peer 宛も登録されないことの確認)
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// 待ち合わせを指定した送信が、送信キューに空きがない場合に空きを待って登録することの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, blocking_send_waits_for_space_when_queue_is_full)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_peer_table;
    const char payload[] = "wait-space"; // [状態] - 送信ペイロードを "wait-space" とする。
    potr_internal_payload_elem elem = {};
    int actual_ret = POTR_ERR_UNKNOWN;
    ctx.role = POTR_ROLE_SENDER;
    ctx.service.type = POTR_TYPE_UNICAST;
    for (int i = 0; i < 8; i++)
    {
        ASSERT_EQ(POTR_OK, potr_service_send(handle, POTR_PEER_NA, payload, strlen(payload), 0));
        // [状態確認] - 容量 8 の送信キューを満杯にする送信 (8 回) の戻り値が POTR_OK であること。
    }

    // Pre-Assert

    // Act
    std::thread sender(
        [&]()
        {
            actual_ret = potr_service_send(handle, POTR_PEER_NA, payload, strlen(payload), POTR_SEND_BLOCKING);
        }); // [手順] - 別スレッドで POTR_SEND_BLOCKING を指定して potr_service_send() を呼び出す。
    for (int i = 0; i < 9; i++)
    {
        while (potr_internal_send_queue_try_pop(&ctx.send_queue, &elem) != POTR_OK)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        potr_internal_send_queue_complete(&ctx.send_queue);
    } // [手順] - 送信スレッドの代わりに、登録済みの 8 件と、空きを待って登録された 1 件を取り出して完了させる。
    sender.join();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);
    // [確認_正常系] - 満杯時に待ち合わせを指定した potr_service_send() の戻り値が POTR_OK であること。(容量不足を返さず、空きを待って登録したことの確認)
    EXPECT_EQ(0U, ctx.send_queue.count); // [確認_正常系] - ctx.send_queue.count が 0U であること。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// 待ち合わせを指定しない送信が、宛先のピアがフロー制御で抑制中の場合に、空きがあっても容量不足を返すことの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, n1_nonblocking_send_returns_full_when_peer_is_suppressed)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_porter;
    const char payload[] = "n1-suppressed"; // [状態] - 送信ペイロードを "n1-suppressed" とする。

    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1;
    peers[0].active = 1;
    peers[0].peer_id = 10;
    cplat_atomic_store_i32(&peers[0].health_alive, 1,
                           CPLAT_MEMORY_ORDER_RELAXED); // [状態] - 接続済みの peer 10 を用意する。
    ON_CALL(mock_porter, potr_internal_peer_find_by_id(_, 10U)).WillByDefault(Return(&peers[0]));
    // [状態] - peer 10 を検索できるようにする。
    cplat_atomic_store_i32(&ctx.flow_tracked_peers, 1, CPLAT_MEMORY_ORDER_RELAXED);
    // [状態] - 抑制の要求または送出上限を保持しているピアが 1 つあるものとする。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_peer_state(&ctx, 10U, _, _)).Times(1).WillOnce(Return(1));
    // [Pre-Assert確認_異常系] - potr_internal_flow_control_peer_state() が peer 10 を指定して 1 回呼ばれること。
    // [Pre-Assert手順] - peer 10 が抑制中であるとして 1 を返す。

    // Act
    int actual_ret = potr_service_send(handle, 10U, payload, strlen(payload),
                                       0); // [手順] - 抑制中の peer 10 を指定して、待ち合わせを指定せずに送信する。

    // Assert
    EXPECT_EQ(POTR_ERR_FULL,
              actual_ret); // [確認_異常系] - 抑制中の peer 宛の potr_service_send() の戻り値が POTR_ERR_FULL であること。
    EXPECT_EQ(
        0U,
        ctx.send_queue
            .count); // [確認_異常系] - ctx.send_queue.count が 0U であること。(送信キューに空きがあっても登録されないことの確認)
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// 待ち合わせを指定しない全 peer 宛の送信が、1 つの peer だけが抑制中でもどの peer 宛も登録しないことの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, n1_nonblocking_peer_all_queues_no_peer_when_one_peer_is_suppressed)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_porter;
    const char payload[] = "n1-all-suppressed"; // [状態] - 送信ペイロードを "n1-all-suppressed" とする。

    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1;
    peers[0].active = 1;
    peers[0].peer_id = 10;
    cplat_atomic_store_i32(&peers[0].health_alive, 1, CPLAT_MEMORY_ORDER_RELAXED);
    peers[1].active = 1;
    peers[1].peer_id = 11;
    cplat_atomic_store_i32(&peers[1].health_alive, 1,
                           CPLAT_MEMORY_ORDER_RELAXED); // [状態] - 接続済みの peer 10 と peer 11 を用意する。
    ON_CALL(mock_porter, potr_internal_flow_control_peer_state(_, 10U, _, _)).WillByDefault(Return(0));
    ON_CALL(mock_porter, potr_internal_flow_control_peer_state(_, 11U, _, _)).WillByDefault(Return(1));
    // [状態] - peer 10 は抑制中でなく、peer 11 は抑制中とする。
    cplat_atomic_store_i32(&ctx.flow_tracked_peers, 1, CPLAT_MEMORY_ORDER_RELAXED);
    // [状態] - 抑制の要求または送出上限を保持しているピアが 1 つあるものとする。

    // Pre-Assert

    // Act
    int actual_ret =
        potr_service_send(handle, POTR_PEER_ALL, payload, strlen(payload),
                          0); // [手順] - 待ち合わせを指定せずに POTR_PEER_ALL を指定して potr_service_send() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_FULL,
        actual_ret); // [確認_異常系] - 一部の peer が抑制中の POTR_PEER_ALL の戻り値が POTR_ERR_FULL であること。
    EXPECT_EQ(
        0U,
        ctx.send_queue
            .count); // [確認_異常系] - ctx.send_queue.count が 0U であること。(抑制中でない peer 10 宛も登録されないことの確認)
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// 待ち合わせを指定した送信が、宛先のピアの抑制が解除されるまで待ってから登録することの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, n1_blocking_send_waits_until_peer_suppression_is_released)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_porter;
    const char payload[] = "n1-wait-release"; // [状態] - 送信ペイロードを "n1-wait-release" とする。
    potr_internal_payload_elem elem = {};
    int actual_ret = POTR_ERR_UNKNOWN;
    std::atomic<int> state_calls(0);
    std::atomic<int> queued_while_suppressed(0);

    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1;
    peers[0].active = 1;
    peers[0].peer_id = 10;
    cplat_atomic_store_i32(&peers[0].health_alive, 1,
                           CPLAT_MEMORY_ORDER_RELAXED); // [状態] - 接続済みの peer 10 を用意する。
    ON_CALL(mock_porter, potr_internal_peer_find_by_id(_, 10U)).WillByDefault(Return(&peers[0]));
    // [状態] - peer 10 を検索できるようにする。

    cplat_atomic_store_i32(&ctx.flow_tracked_peers, 1, CPLAT_MEMORY_ORDER_RELAXED);
    // [状態] - 抑制の要求または送出上限を保持しているピアが 1 つあり、送信スレッドが保留しているエントリはないものとする。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_peer_state(&ctx, 10U, _, _))
        .Times(4)
        .WillRepeatedly(Invoke(
            [&](potr_context *, potr_peer_id, uint64_t, int *)
            {
                int n = ++state_calls;
                if (n <= 3)
                {
                    if (ctx.send_queue.count != 0U)
                    {
                        queued_while_suppressed = 1;
                    }
                    return 1;
                }
                return 0;
            }));
    // [Pre-Assert確認_正常系] - potr_internal_flow_control_peer_state() が 4 回呼ばれること。(登録前の確認が 4 回。保留の総数が 0 のため、完了後の確認は行わない)
    // [Pre-Assert手順] - 最初の 3 回は抑制中として 1 を、以降は 0 を返す。

    // Act
    std::thread sender(
        [&]()
        {
            actual_ret = potr_service_send(handle, 10U, payload, strlen(payload), POTR_SEND_BLOCKING);
        }); // [手順] - 別スレッドで POTR_SEND_BLOCKING を指定して potr_service_send() を呼び出す。
    while (potr_internal_send_queue_try_pop(&ctx.send_queue, &elem) != POTR_OK)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } // [手順] - 送信スレッドの代わりに、抑制の解除後に登録された 1 件を取り出す。
    potr_internal_send_queue_complete(&ctx.send_queue); // [手順] - 取り出した 1 件を完了させる。
    sender.join();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);
    // [確認_正常系] - 抑制の解除を待った potr_service_send() の戻り値が POTR_OK であること。
    EXPECT_EQ(0, queued_while_suppressed.load());
    // [確認_正常系] - 抑制中の確認 (3 回) の時点で、送信キューへ登録されていないこと。
    EXPECT_EQ((potr_peer_id)10,
              elem.peer_id); // [確認_正常系] - 取り出した elem.peer_id が 10 と一致すること。
    EXPECT_EQ(0U, ctx.send_queue.count); // [確認_正常系] - ctx.send_queue.count が 0U であること。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// 待ち合わせを指定した送信が、送信スレッドの保留したエントリが送出されるまで戻らないことの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, n1_blocking_send_waits_until_deferred_entries_are_released)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_porter;
    const char payload[] = "n1-wait-deferred"; // [状態] - 送信ペイロードを "n1-wait-deferred" とする。
    potr_internal_payload_elem elem = {};
    int actual_ret = POTR_ERR_UNKNOWN;
    std::atomic<int> deferred_marked(0);
    std::atomic<int> post_defer_calls(0);
    std::atomic<int> sender_done(0);

    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1;
    peers[0].active = 1;
    peers[0].peer_id = 10;
    cplat_atomic_store_i32(&peers[0].health_alive, 1,
                           CPLAT_MEMORY_ORDER_RELAXED); // [状態] - 接続済みの peer 10 を用意する。
    ON_CALL(mock_porter, potr_internal_peer_find_by_id(_, 10U)).WillByDefault(Return(&peers[0]));
    // [状態] - peer 10 を検索できるようにする。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_peer_state(&ctx, 10U, _, _))
        .Times(AtLeast(1))
        .WillRepeatedly(Invoke(
            [&](potr_context *, potr_peer_id, uint64_t, int *deferred)
            {
                if (deferred_marked.load() == 0)
                {
                    *deferred = 0;
                    return 0;
                }
                int n = ++post_defer_calls;
                *deferred = (n <= 3) ? 1 : 0;
                return 0;
            }));
    // [Pre-Assert確認_正常系] - potr_internal_flow_control_peer_state() が 1 回以上呼ばれること。
    // [Pre-Assert手順] - 保留の前は抑制なし・保留 0 を返し、保留の後は 3 回まで保留 1 を、以降は保留 0 を返す。

    // Act
    std::thread sender(
        [&]()
        {
            actual_ret = potr_service_send(handle, 10U, payload, strlen(payload), POTR_SEND_BLOCKING);
            sender_done = 1;
        }); // [手順] - 別スレッドで POTR_SEND_BLOCKING を指定して potr_service_send() を呼び出す。
    while (potr_internal_send_queue_try_pop(&ctx.send_queue, &elem) != POTR_OK)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } // [手順] - 送信スレッドの代わりに、登録された 1 件を取り出す。
    deferred_marked = 1;
    cplat_atomic_store_i32(&ctx.flow_deferred_total, 1, CPLAT_MEMORY_ORDER_RELEASE);
    // [手順] - 送信スレッドと同じく、完了待ちの番号を進める前に保留の総数を 1 にする。
    potr_internal_send_queue_defer(&ctx.send_queue); // [手順] - 取り出した 1 件を、送出を保留したものとしてマークする。
    while (post_defer_calls.load() < 3)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } // [手順] - 保留 1 が 3 回返されるまで待つ。
    int done_while_deferred = sender_done.load();
    sender.join();

    // Assert
    EXPECT_EQ(0, done_while_deferred);
    // [確認_正常系] - 完了待ちの番号が進んでいても、保留が残っている間は potr_service_send() が戻らないこと。
    EXPECT_EQ(POTR_OK, actual_ret);
    // [確認_正常系] - 保留がなくなった後の potr_service_send() の戻り値が POTR_OK であること。
    EXPECT_EQ(4, post_defer_calls.load());
    // [確認_正常系] - 保留の後の potr_internal_flow_control_peer_state() の呼び出しが 4 回であること。(保留 1 が 3 回、保留 0 が 1 回)

    // 後始末
    potr_internal_send_queue_complete_deferred(&ctx.send_queue); // [手順] - 保留したエントリを完了させ、キューの状態を戻す。
    cplat_atomic_store_i32(&ctx.flow_deferred_total, 0, CPLAT_MEMORY_ORDER_RELEASE); // [手順] - 保留の総数を 0 へ戻す。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// 待ち合わせを指定した送信が、宛先のピアが抑制中のまま送信スレッドが停止した場合に中止コードを返すことの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, n1_blocking_send_returns_canceled_when_send_thread_stops_while_suppressed)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_porter;
    const char payload[] = "n1-canceled"; // [状態] - 送信ペイロードを "n1-canceled" とする。
    int actual_ret = POTR_OK;
    std::atomic<int> state_calls(0);

    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1;
    peers[0].active = 1;
    peers[0].peer_id = 10;
    cplat_atomic_store_i32(&peers[0].health_alive, 1,
                           CPLAT_MEMORY_ORDER_RELAXED); // [状態] - 接続済みの peer 10 を用意する。
    ON_CALL(mock_porter, potr_internal_peer_find_by_id(_, 10U)).WillByDefault(Return(&peers[0]));
    // [状態] - peer 10 を検索できるようにする。
    ON_CALL(mock_porter, potr_internal_flow_control_peer_state(&ctx, 10U, _, _))
        .WillByDefault(Invoke(
            [&](potr_context *, potr_peer_id, uint64_t, int *)
            {
                ++state_calls;
                return 1;
            }));
    // [状態] - peer 10 を常に抑制中とする。
    cplat_atomic_store_i32(&ctx.flow_tracked_peers, 1, CPLAT_MEMORY_ORDER_RELAXED);
    // [状態] - 抑制の要求または送出上限を保持しているピアが 1 つあるものとする。

    // Pre-Assert

    // Act
    std::thread sender(
        [&]()
        {
            actual_ret = potr_service_send(handle, 10U, payload, strlen(payload), POTR_SEND_BLOCKING);
        }); // [手順] - 別スレッドで POTR_SEND_BLOCKING を指定して potr_service_send() を呼び出す。
    while (state_calls.load() < 1)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } // [手順] - 送信が抑制中の確認に入るまで待つ。
    cplat_atomic_store_i32(&ctx.send_thread_running, 0,
                           CPLAT_MEMORY_ORDER_RELEASE); // [手順] - 送信スレッドが停止したものとする。
    sender.join();

    // Assert
    EXPECT_EQ(POTR_ERR_CANCELED,
              actual_ret); // [確認_異常系] - 送信スレッドの停止後の potr_service_send() の戻り値が POTR_ERR_CANCELED であること。
    EXPECT_EQ(
        0U,
        ctx.send_queue
            .count); // [確認_異常系] - ctx.send_queue.count が 0U であること。(抑制中のため登録されなかったことの確認)
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// N:1 で、抑制の要求または送出上限を保持しているピアがなく、保留もない場合に、ピアの抑制の判定を行わずに登録することの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, n1_send_skips_peer_state_when_no_peer_is_tracked)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_porter;
    const char payload[] = "n1-idle"; // [状態] - 送信ペイロードを "n1-idle" とする。
    potr_internal_payload_elem elem = {};
    int actual_blocking_ret = POTR_ERR_UNKNOWN;

    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1;
    peers[0].active = 1;
    peers[0].peer_id = 10;
    cplat_atomic_store_i32(&peers[0].health_alive, 1,
                           CPLAT_MEMORY_ORDER_RELAXED); // [状態] - 接続済みの peer 10 を用意する。
    ON_CALL(mock_porter, potr_internal_peer_find_by_id(_, 10U)).WillByDefault(Return(&peers[0]));
    // [状態] - peer 10 を検索できるようにする。flow_tracked_peers と flow_deferred_total は 0 のままとする。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_peer_state(_, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - potr_internal_flow_control_peer_state() が呼ばれないこと。(登録前と完了後の確認を省くことの確認)

    // Act
    int actual_ret = potr_service_send(handle, 10U, payload, strlen(payload),
                                       0); // [手順] - peer 10 を指定して、待ち合わせを指定せずに送信する。
    std::thread sender(
        [&]()
        {
            actual_blocking_ret = potr_service_send(handle, 10U, payload, strlen(payload), POTR_SEND_BLOCKING);
        }); // [手順] - 別スレッドで POTR_SEND_BLOCKING を指定して peer 10 宛に送信する。
    for (int i = 0; i < 2; i++)
    {
        while (potr_internal_send_queue_try_pop(&ctx.send_queue, &elem) != POTR_OK)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        potr_internal_send_queue_complete(&ctx.send_queue);
    } // [手順] - 送信スレッドの代わりに、登録された 2 件を取り出して完了させる。
    sender.join();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 待ち合わせを指定しない送信の戻り値が POTR_OK であること。
    EXPECT_EQ(POTR_OK, actual_blocking_ret); // [確認_正常系] - 待ち合わせを指定した送信の戻り値が POTR_OK であること。
    EXPECT_EQ((potr_peer_id)10, elem.peer_id); // [確認_正常系] - 取り出した elem.peer_id が 10 と一致すること。
    EXPECT_EQ(0U, ctx.send_queue.count);       // [確認_正常系] - ctx.send_queue.count が 0U であること。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// N:1 で、特定ピア宛の送信だけが止める状態で拒否された場合に妨げを記録せず、切断もしないことの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, n1_single_peer_suppressed_does_not_mark_blocking_or_disconnect)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_porter;
    const char payload[] = "n1-single-suppressed"; // [状態] - 送信ペイロードを用意する。

    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1;
    cplat_atomic_store_i32(&ctx.flow_tracked_peers, 1, CPLAT_MEMORY_ORDER_RELEASE);
    peers[0].active = 1;
    peers[0].peer_id = 10U;
    cplat_atomic_store_i32(&peers[0].health_alive, 1, CPLAT_MEMORY_ORDER_RELAXED);
    ON_CALL(mock_porter, potr_internal_peer_find_by_id(_, 10U)).WillByDefault(Return(&peers[0]));
    ON_CALL(mock_porter, potr_internal_flow_control_peer_state(&ctx, 10U, _, _)).WillByDefault(Return(1));
    // [状態] - ピア 10 が止める状態であるとする。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_peer_mark_blocking(_, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 特定ピア宛では妨げを記録しないこと。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_peer_all_paths(_, _)).Times(0);
    EXPECT_CALL(mock_porter, potr_internal_peer_free(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - 送信 API スレッドからは切断しないこと。

    // Act
    int actual_ret = potr_service_send(handle, 10U, payload, strlen(payload), 0);
    // [手順] - ピア 10 を指定して非ブロッキング送信する。

    // Assert
    EXPECT_EQ(POTR_ERR_FULL, actual_ret); // [確認_正常系] - 戻り値が POTR_ERR_FULL であること。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// N:1 で、全ピア宛 (POTR_PEER_ALL) の非ブロッキング送信が止める状態のピアで拒否された場合に妨げを記録し切断しないことの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, n1_peer_all_nonblocking_marks_blocking_without_disconnect)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_porter;
    const char payload[] = "n1-all-nb"; // [状態] - 送信ペイロードを用意する。

    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1;
    cplat_atomic_store_i32(&ctx.flow_tracked_peers, 1, CPLAT_MEMORY_ORDER_RELEASE);
    peers[0].active = 1;
    peers[0].peer_id = 10U;
    cplat_atomic_store_i32(&peers[0].health_alive, 1, CPLAT_MEMORY_ORDER_RELAXED);
    peers[1].active = 1;
    peers[1].peer_id = 11U;
    cplat_atomic_store_i32(&peers[1].health_alive, 1, CPLAT_MEMORY_ORDER_RELAXED);
    ON_CALL(mock_porter, potr_internal_flow_control_peer_state(&ctx, 10U, _, _)).WillByDefault(Return(0));
    ON_CALL(mock_porter, potr_internal_flow_control_peer_state(&ctx, 11U, _, _)).WillByDefault(Return(1));
    // [状態] - ピア 10 は送信可能、ピア 11 は止める状態とする。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_peer_mark_blocking(&ctx, 11U, _)).Times(1);
    // [Pre-Assert確認_正常系] - 止める状態のピア 11 に対して妨げが 1 回記録されること。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_peer_all_paths(_, _)).Times(0);
    EXPECT_CALL(mock_porter, potr_internal_peer_free(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - 送信 API スレッドからは切断しないこと。

    // Act
    int actual_ret = potr_service_send(handle, POTR_PEER_ALL, payload, strlen(payload), 0);
    // [手順] - POTR_PEER_ALL で非ブロッキング送信する。

    // Assert
    EXPECT_EQ(POTR_ERR_FULL, actual_ret); // [確認_正常系] - 戻り値が POTR_ERR_FULL であること。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// N:1 で、全ピア宛 (POTR_PEER_ALL) のブロッキング送信が止める状態のピアで待たされた場合に妨げを記録し、
// 切断は行わず、そのピアの切断後に待機から抜けて進むことの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, n1_peer_all_blocking_marks_blocking_and_proceeds_after_peer_disconnect)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_porter;
    const char payload[] = "n1-all-blk"; // [状態] - 送信ペイロードを用意する。
    std::atomic<int> mark_calls{0};
    std::atomic<int> peer11_suppressed{1};
    potr_internal_payload_elem elem = {};
    int actual_ret = POTR_ERR_UNKNOWN;

    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1;
    cplat_atomic_store_i32(&ctx.flow_tracked_peers, 1, CPLAT_MEMORY_ORDER_RELEASE);
    peers[0].active = 1;
    peers[0].peer_id = 10U;
    cplat_atomic_store_i32(&peers[0].health_alive, 1, CPLAT_MEMORY_ORDER_RELAXED);
    peers[1].active = 1;
    peers[1].peer_id = 11U;
    cplat_atomic_store_i32(&peers[1].health_alive, 1, CPLAT_MEMORY_ORDER_RELAXED);
    ON_CALL(mock_porter, potr_internal_flow_control_peer_state(&ctx, 10U, _, _)).WillByDefault(Return(0));
    ON_CALL(mock_porter, potr_internal_flow_control_peer_state(&ctx, 11U, _, _))
        .WillByDefault([&](potr_context *, potr_peer_id, uint64_t, int *deferred) {
            *deferred = 0;
            return peer11_suppressed.load();
        });
    ON_CALL(mock_porter, potr_internal_flow_control_peer_mark_blocking(&ctx, 11U, _))
        .WillByDefault([&](potr_context *, potr_peer_id, uint64_t) { mark_calls.fetch_add(1); });
    // [状態] - ピア 11 が止める状態の間は待ち、切断後に 0 を返すようにする。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_peer_mark_blocking(&ctx, 11U, _)).Times(AtLeast(1));
    // [Pre-Assert確認_正常系] - 待たされたピア 11 に妨げが記録されること。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_peer_all_paths(_, _)).Times(0);
    EXPECT_CALL(mock_porter, potr_internal_peer_free(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - 送信 API スレッドからは切断しないこと。

    // Act
    std::thread sender([&]() {
        actual_ret = potr_service_send(handle, POTR_PEER_ALL, payload, strlen(payload), POTR_SEND_BLOCKING);
    }); // [手順] - 別スレッドで POTR_PEER_ALL のブロッキング送信を開始する。
    // ピア 10 の登録分を先に完了させる。
    while (potr_internal_send_queue_try_pop(&ctx.send_queue, &elem) != POTR_OK)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    potr_internal_send_queue_complete(&ctx.send_queue);
    // ピア 11 の妨げ記録が行われるまで待つ。
    while (mark_calls.load() < 1)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // 別スレッド (受信スレッド相当) でピア 11 が切断されたものとし、flow_changed を起床させる。
    peer11_suppressed.store(0);
    potr_internal_send_queue_wake_flow(&ctx.send_queue);
    // ピア 11 宛の登録分を取り出して完了させる (送信スレッドでは切断済みピア宛は破棄される)。
    while (potr_internal_send_queue_try_pop(&ctx.send_queue, &elem) != POTR_OK)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    potr_internal_send_queue_complete(&ctx.send_queue);
    sender.join();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - ピア 11 の切断後にブロッキング送信が完了すること。
}
// [サブ手順参照 名前=potrSendTest.TearDown]

// N:1 で、送信キューが満杯で保留エントリがある状態で、非ブロッキング送信とブロッキング送信の両方が
// キュー満杯を記録し、切断は行わないことの確認
// [サブ手順参照 名前=potrSendTest.SetUp]
TEST_F(potrSendTest, n1_queue_full_with_deferred_notes_queue_full_for_nonblocking_and_blocking)
{
    // Arrange
    NiceMock<Mock_cplat> mock_log;
    NiceMock<Mock_porter> mock_porter;
    const char payload[] = "n1-qfull"; // [状態] - 送信ペイロードを用意する。
    std::atomic<int> note_calls{0};
    potr_internal_payload_elem elem = {};
    int blocking_ret = POTR_ERR_UNKNOWN;

    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1;
    cplat_atomic_store_i32(&ctx.flow_deferred_total, 1, CPLAT_MEMORY_ORDER_RELEASE);
    peers[0].active = 1;
    peers[0].peer_id = 10U;
    cplat_atomic_store_i32(&peers[0].health_alive, 1, CPLAT_MEMORY_ORDER_RELAXED);
    ON_CALL(mock_porter, potr_internal_peer_find_by_id(_, 10U)).WillByDefault(Return(&peers[0]));
    ON_CALL(mock_porter, potr_internal_flow_control_note_queue_full(&ctx, _))
        .WillByDefault([&](potr_context *, uint64_t) { note_calls.fetch_add(1); });

    // 送信キューを満杯にする。
    for (size_t i = 0U; i < ctx.send_queue.depth; i++)
    {
        ASSERT_EQ(POTR_OK, potr_internal_send_queue_push(&ctx.send_queue, 20U, 0U, payload, (uint16_t)strlen(payload),
                                                         nullptr));
    }
    // [状態] - flow_deferred_total = 1 の状態で送信キューを満杯にする。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_note_queue_full(&ctx, _)).Times(2);
    // [Pre-Assert確認_正常系] - 非ブロッキングとブロッキングの 2 回、キュー満杯が記録されること。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_peer_all_paths(_, _)).Times(0);
    EXPECT_CALL(mock_porter, potr_internal_peer_free(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - 送信 API スレッドからは切断しないこと。

    // Act
    int nb_ret = potr_service_send(handle, 10U, payload, strlen(payload), 0);
    // [手順] - 満杯の状態で非ブロッキング送信し、POTR_ERR_FULL になることを確認する。

    std::thread sender([&]() {
        blocking_ret = potr_service_send(handle, 10U, payload, strlen(payload), POTR_SEND_BLOCKING);
    }); // [手順] - 満杯の状態でブロッキング送信を開始する。
    while (note_calls.load() < 2)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // 1 件取り出して完了させ (キューに空きを作り)、続けてブロッキング送信分も完了させる。
    for (size_t i = 0U; i < ctx.send_queue.depth + 1U; i++)
    {
        while (potr_internal_send_queue_try_pop(&ctx.send_queue, &elem) != POTR_OK)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        potr_internal_send_queue_complete(&ctx.send_queue);
    }
    cplat_atomic_store_i32(&ctx.flow_deferred_total, 0, CPLAT_MEMORY_ORDER_RELEASE);
    sender.join();

    // Assert
    EXPECT_EQ(POTR_ERR_FULL, nb_ret); // [確認_正常系] - 非ブロッキング送信が POTR_ERR_FULL を返すこと。
    EXPECT_EQ(POTR_OK, blocking_ret); // [確認_正常系] - ブロッキング送信が空き後に POTR_OK で完了すること。
}
// [サブ手順参照 名前=potrSendTest.TearDown]
