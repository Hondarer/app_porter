#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <mock_string.h>
#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/packet_seal.h>
#include <cplat/net/byteorder.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstring>
#include <deque>
#include <vector>

using namespace testing;

// send の lock 生成失敗時にロックと稼働状態を戻すことの確認
TEST(potrSendThreadTest, lock_failure_rolls_back)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ON_CALL(mock_cplat, cplat_local_lock_create(_))
        .WillByDefault(Return(CPLAT_ERR_UNKNOWN)); // [状態] - 生成処理を失敗させる。
    // Act
    int actual_ret = potr_internal_send_thread_start(&ctx); // [手順] - send を開始する。
    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret);   // [確認_異常系] - 生成失敗を返すこと。
    EXPECT_EQ(nullptr, ctx.send_window_mutex); // [確認_異常系] - ロックを残さないこと。
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.send_thread_running, CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_異常系] - 稼働状態を 0 に戻すこと。
}

// send の thread 生成失敗時にロックと稼働状態を戻すことの確認
TEST(potrSendThreadTest, thread_failure_rolls_back)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(Return(CPLAT_ERR_UNKNOWN)); // [状態] - 生成処理を失敗させる。
    // Act
    int actual_ret = potr_internal_send_thread_start(&ctx); // [手順] - send を開始する。
    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret);   // [確認_異常系] - 生成失敗を返すこと。
    EXPECT_EQ(nullptr, ctx.send_window_mutex); // [確認_異常系] - ロックを残さないこと。
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.send_thread_running, CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_異常系] - 稼働状態を 0 に戻すこと。
}

// send の稼働終了後も一度だけ join してロックを破棄することの確認
TEST(potrSendThreadTest, stop_joins_finished_thread_once)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int token = 0;
    ctx.send_thread = reinterpret_cast<cplat_thread *>(&token);
    delegate_real_cplat_local_lock_create(&ctx.send_window_mutex);
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_thread_join(ctx.send_thread)).WillOnce(Return());
    // [Pre-Assert確認_正常系] - 生成済み send を 1 回だけ join すること。
    // [Pre-Assert手順] - join 成功を返す。
    // Act
    potr_internal_send_thread_stop(&ctx); // [手順] - send を停止する。
    potr_internal_send_thread_stop(&ctx); // [手順] - 停止済み send を再度停止する。
    // Assert
    EXPECT_EQ(nullptr, ctx.send_thread);       // [確認_正常系] - ハンドルを NULL にすること。
    EXPECT_EQ(nullptr, ctx.send_window_mutex); // [確認_正常系] - ロックを NULL にすること。
}

// N:1 で取り出し後にピアが解放された場合、送信せずに取り出した件数分を完了させることの確認
TEST(potrSendThreadTest, n1_peer_freed_before_flush_drops_entries)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context peer = {};
    std::vector<uint8_t> wire_buf(POTR_PACKET_HEADER_SIZE + 1400U);
    uint8_t payload[4] = {1U, 2U, 3U, 4U};
    potr_internal_payload_elem elem = {};
    ctx.is_multi_peer = 1;               // [状態] - N:1 モードとする。
    ctx.global.max_payload = 1400U;      // [状態] - 最大ペイロード長を設定する。
    ctx.send_wire_buf = wire_buf.data(); // [状態] - 送信 wire 組立バッファーを設定する。
    delegate_real_cplat_local_lock_create(&ctx.peers_mutex);
    peer.active = 1;
    peer.peer_id = 5U;
    elem.peer_id = 5U;
    elem.flags = 0U;
    elem.payload_len = (uint16_t)sizeof(payload);
    elem.payload = payload;
    ON_CALL(mock_porter, potr_internal_send_queue_peek(_, _))
        .WillByDefault(Return(POTR_ERR_EMPTY)); // [状態] - 追加のエントリはないものとする。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_pop(&ctx.send_queue, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(elem), Return(POTR_OK)))
        .WillRepeatedly(Return(POTR_ERR_CANCELED));
    // [Pre-Assert手順] - 1 回目はピア 5 宛のエントリを返し、以降は停止を返す。
    EXPECT_CALL(mock_porter, potr_internal_peer_find_by_id(&ctx, 5U)).WillOnce(Return(&peer)).WillOnce(Return(nullptr));
    // [Pre-Assert確認_正常系] - 存在確認と送信直前の再検索で、ピアを 2 回検索すること。
    // [Pre-Assert手順] - 存在確認ではピアを返し、再検索では解放済みとして NULL を返す。
    EXPECT_CALL(mock_porter, potr_internal_packet_build_packed(_, _, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 解放済みピアへのパケットを構築しないこと。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(1);
    // [Pre-Assert確認_正常系] - 取り出した 1 件分の完了を通知すること。
    // Act
    int actual_ret = potr_internal_send_thread_start(&ctx); // [手順] - send を開始する。
    potr_internal_send_thread_stop(&ctx);                   // [手順] - send を停止して終了を待つ。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);      // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(nullptr, ctx.send_thread); // [確認_正常系] - 送信スレッドが終了していること。
    // Cleanup
    delegate_real_cplat_local_lock_dispose(ctx.peers_mutex);
}

class potrSendThreadTcpTest : public Test
{
  protected:
    // [サブ手順 名前=potrSendThreadTcpTest.SetUp]
    void SetUp() override
    {
        memset(&ctx, 0, sizeof(ctx));
        wire_buf.assign(POTR_PACKET_HEADER_SIZE + 1400U, 0U);
        ctx.service.type = POTR_TYPE_TCP_BIDIR; // [状態] - TCP_BIDIR とする。
        ctx.global.max_payload = 1400U;         // [状態] - 最大ペイロード長を設定する。
        sealed_buf.assign(POTR_PACKET_HEADER_SIZE + 1400U + POTR_CRYPTO_TAG_SIZE, 0U);
        ctx.crypto_buf = sealed_buf.data();
        ctx.crypto_buf_size = sealed_buf.size();
        ctx.send_wire_buf = wire_buf.data();    // [状態] - 送信 wire 組立バッファーを設定する。
        ctx.n_path = 1;                         // [状態] - path 数を 1 とする。
        ctx.tcp_conn_fd[0] = conn_fd;           // [状態] - path 0 を接続済みとする。
        cplat_atomic_store_i32(&ctx.tcp_active_paths, 1, CPLAT_MEMORY_ORDER_RELAXED);
        delegate_real_cplat_local_lock_create(&ctx.tcp_send_mutex[0]);
        elem.peer_id = POTR_PEER_NA;
        elem.flags = 0U;
        elem.payload_len = (uint16_t)sizeof(payload);
        elem.payload = payload;
        ON_CALL(mock_porter, potr_internal_window_send_push(&ctx.send_window, _))
            .WillByDefault([](potr_internal_window *window, const potr_packet *) {
                window->next_seq++;
                return POTR_OK;
            }); // [状態] - DATA 保持時に送信通番を進める。
        ON_CALL(mock_porter, potr_internal_send_queue_peek(_, _))
            .WillByDefault(Return(POTR_ERR_EMPTY)); // [状態] - 追加のエントリはないものとする。
        ON_CALL(mock_porter, potr_internal_send_queue_pop(&ctx.send_queue, _, _))
            .WillByDefault(Return(POTR_ERR_CANCELED));
        ON_CALL(mock_porter, potr_internal_tcp_check_writable(&ctx, 0))
            .WillByDefault(Return(POTR_OK)); // [状態] - 送信バッファーに空きがあるものとする。
        ON_CALL(mock_porter, potr_internal_tcp_send_all_locked(&ctx, 0, _, _, _, _))
            .WillByDefault(Return(POTR_OK)); // [状態] - 書き込みは成功するものとする。
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrSendThreadTcpTest.TearDown]
    void TearDown() override
    {
        delegate_real_cplat_local_lock_dispose(ctx.tcp_send_mutex[0]);
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrSendThreadTcpTest.runOneEntry]
    int runOneEntry()
    {
        EXPECT_CALL(mock_porter, potr_internal_send_queue_pop(&ctx.send_queue, _, _))
            .WillOnce(DoAll(SetArgPointee<1>(elem), Return(POTR_OK)))
            .WillRepeatedly(Return(POTR_ERR_CANCELED));
        // [Pre-Assert手順] - 1 回目はエントリを 1 件返し、以降は停止を返す。
        int ret = potr_internal_send_thread_start(&ctx); // [手順] - send を開始する。
        potr_internal_send_thread_stop(&ctx);            // [手順] - send を停止して終了を待つ。
        return ret;
    }
    // [サブ手順終了]

    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx;
    std::vector<uint8_t> wire_buf;
    std::vector<uint8_t> sealed_buf;
    uint8_t payload[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
    potr_internal_payload_elem elem = {};
    const cplat_socket conn_fd = (cplat_socket)7;
};

// TCP の DATA の前に、同じ path へ未通知の経路状態を書き込むことの確認
// [サブ手順参照 名前=potrSendThreadTcpTest.SetUp]
TEST_F(potrSendThreadTcpTest, announces_path_state_before_data)
{
    // Arrange

    // Pre-Assert
    {
        InSequence seq;
        EXPECT_CALL(mock_porter, potr_internal_tcp_announce_ping_state_locked(&ctx, 0)).WillOnce(Return(POTR_OK));
        // [Pre-Assert確認_正常系] - DATA の前に path 0 の経路状態の通知を 1 回呼び出すこと。
        // [Pre-Assert手順] - 通知の成功を返す。
        EXPECT_CALL(mock_porter, potr_internal_tcp_send_all_locked(&ctx, 0, _, _, _, _)).WillOnce(Return(POTR_OK));
        // [Pre-Assert確認_正常系] - 通知の後に DATA を 1 回書き込むこと。
        // [Pre-Assert手順] - 書き込みの成功を返す。
    }
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(1);
    // [Pre-Assert確認_正常系] - 取り出した 1 件を成功として完了させること。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete_failed(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - 送信失敗として完了させないこと。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpTest.runOneEntry]
    int actual_ret = runOneEntry();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);          // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(1U, ctx.send_window.next_seq); // [確認_正常系] - 書き込んだ DATA の通番を消費すること。
}
// [サブ手順参照 名前=potrSendThreadTcpTest.TearDown]

// TCP の唯一の path の送信バッファーに空きがない場合も、DATA を捨てずに書き込むことの確認
// [サブ手順参照 名前=potrSendThreadTcpTest.SetUp]
TEST_F(potrSendThreadTcpTest, full_single_path_writes_instead_of_dropping)
{
    // Arrange
    ON_CALL(mock_porter, potr_internal_tcp_check_writable(&ctx, 0))
        .WillByDefault(Return(POTR_ERR_TIMEOUT)); // [状態] - 送信バッファーに空きがないものとする。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_all_locked(&ctx, 0, _, _, _, _)).WillOnce(Return(POTR_OK));
    // [Pre-Assert確認_正常系] - 空きを待って DATA を 1 回書き込むこと。
    // [Pre-Assert手順] - 書き込みの成功を返す。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(1);
    // [Pre-Assert確認_正常系] - 取り出した 1 件を成功として完了させること。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpTest.runOneEntry]
    int actual_ret = runOneEntry();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);          // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(1U, ctx.send_window.next_seq); // [確認_正常系] - 書き込んだ DATA の通番を消費すること。
}
// [サブ手順参照 名前=potrSendThreadTcpTest.TearDown]

// TCP の書き込みに失敗した場合、path を停止し、取り出した件数分を送信失敗として完了させることの確認
// [サブ手順参照 名前=potrSendThreadTcpTest.SetUp]
TEST_F(potrSendThreadTcpTest, write_failure_completes_entries_as_failed)
{
    // Arrange

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_all_locked(&ctx, 0, _, _, _, _)).WillOnce(Return(POTR_ERR_IO));
    // [Pre-Assert確認_異常系] - DATA の書き込みを 1 回試みること。
    // [Pre-Assert手順] - 書き込みの失敗を返す。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete_failed(&ctx.send_queue, POTR_ERR_DISCONNECTED)).Times(1);
    // [Pre-Assert確認_異常系] - 取り出した 1 件を POTR_ERR_DISCONNECTED の送信失敗として完了させること。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(_)).Times(0);
    // [Pre-Assert確認_異常系] - 成功として完了させないこと。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpTest.runOneEntry]
    int actual_ret = runOneEntry();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);          // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(0U, ctx.send_window.next_seq); // [確認_異常系] - 書き込めなかった DATA の通番を消費しないこと。
}
// [サブ手順参照 名前=potrSendThreadTcpTest.TearDown]

// 経路状態の通知に失敗した場合、DATA を書き込まずに path を停止することの確認
// [サブ手順参照 名前=potrSendThreadTcpTest.SetUp]
TEST_F(potrSendThreadTcpTest, announce_failure_stops_path_without_data)
{
    // Arrange

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_tcp_announce_ping_state_locked(&ctx, 0)).WillOnce(Return(POTR_ERR_IO));
    // [Pre-Assert確認_異常系] - path 0 の経路状態の通知を 1 回呼び出すこと。
    // [Pre-Assert手順] - 通知の失敗を返す。
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_all_locked(_, _, _, _, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - DATA を書き込まないこと。
    EXPECT_CALL(mock_porter, potr_internal_tcp_conn_request_close(&ctx, 0, 0U)).Times(1);
    // [Pre-Assert確認_異常系] - 通知できなかった path へ閉じる要求を発行すること。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete_failed(&ctx.send_queue, POTR_ERR_DISCONNECTED)).Times(1);
    // [Pre-Assert確認_異常系] - 取り出した 1 件を POTR_ERR_DISCONNECTED の送信失敗として完了させること。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpTest.runOneEntry]
    int actual_ret = runOneEntry();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);          // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(0U, ctx.send_window.next_seq); // [確認_異常系] - 書き込めなかった DATA の通番を消費しないこと。
}
// [サブ手順参照 名前=potrSendThreadTcpTest.TearDown]

// 送信バッファー警告抑止カウンターが 0 から 0 になることの確認
// [サブ手順参照 名前=potrSendThreadTcpTest.SetUp]
TEST_F(potrSendThreadTcpTest, suppress_counter_zero_stays_zero)
{
    // Arrange
    ctx.buf_full_suppress_cnt[0] = 0; // [状態] - 送信前の抑止カウンターを設定する。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpTest.runOneEntry]
    int actual_ret = runOneEntry();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);             // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(0, ctx.buf_full_suppress_cnt[0]); // [確認_正常系] - 送信後の抑止カウンターが 0 であること。
}
// [サブ手順参照 名前=potrSendThreadTcpTest.TearDown]

// 送信バッファー警告抑止カウンターが POTR_TCP_BUF_FULL_SUPPRESS_COUNT - 1 から POTR_TCP_BUF_FULL_SUPPRESS_COUNT になることの確認
// [サブ手順参照 名前=potrSendThreadTcpTest.SetUp]
TEST_F(potrSendThreadTcpTest, suppress_counter_below_limit_reaches_limit)
{
    // Arrange
    ctx.buf_full_suppress_cnt[0] = POTR_TCP_BUF_FULL_SUPPRESS_COUNT - 1; // [状態] - 送信前の抑止カウンターを設定する。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpTest.runOneEntry]
    int actual_ret = runOneEntry();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);              // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(POTR_TCP_BUF_FULL_SUPPRESS_COUNT,
              ctx.buf_full_suppress_cnt[0]); // [確認_正常系] - 送信後の抑止カウンターが POTR_TCP_BUF_FULL_SUPPRESS_COUNT であること。
}
// [サブ手順参照 名前=potrSendThreadTcpTest.TearDown]

// 送信バッファー警告抑止カウンターが POTR_TCP_BUF_FULL_SUPPRESS_COUNT から 0 になることの確認
// [サブ手順参照 名前=potrSendThreadTcpTest.SetUp]
TEST_F(potrSendThreadTcpTest, suppress_counter_exceeding_limit_resets)
{
    // Arrange
    ctx.buf_full_suppress_cnt[0] = POTR_TCP_BUF_FULL_SUPPRESS_COUNT; // [状態] - 送信前の抑止カウンターを設定する。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpTest.runOneEntry]
    int actual_ret = runOneEntry();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);             // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(0, ctx.buf_full_suppress_cnt[0]); // [確認_正常系] - 送信後の抑止カウンターが 0 であること。
}
// [サブ手順参照 名前=potrSendThreadTcpTest.TearDown]

// 暗号化時にパッキング平文を認証タグ分の上限内に保つことの確認
// [サブ手順参照 名前=potrSendThreadTcpTest.SetUp]
TEST_F(potrSendThreadTcpTest, encrypted_packet_packing_reserves_tag_capacity)
{
    // Arrange
    std::vector<uint8_t> first_payload(POTR_DEFAULT_MAX_PAYLOAD - POTR_CRYPTO_TAG_SIZE - POTR_PAYLOAD_ELEM_HDR_SIZE,
                                       1U);
    uint8_t next_payload = 2U;
    potr_internal_payload_elem next_elem = {};
    size_t encrypted_plaintext_len = 0U;
    ctx.global.max_payload = POTR_DEFAULT_MAX_PAYLOAD;
    ctx.service.encrypt_enabled = 1; // [状態] - パケットを暗号化する。
    elem.payload_len = static_cast<uint16_t>(first_payload.size());
    elem.payload = first_payload.data();
    next_elem.peer_id = POTR_PEER_NA;
    next_elem.flags = 0U;
    next_elem.payload_len = 1U;
    next_elem.payload = &next_payload;

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_peek(&ctx.send_queue, _))
        .WillOnce(DoAll(SetArgPointee<1>(next_elem), Return(POTR_OK)));
    // [Pre-Assert確認_正常系] - 追加エレメントを 1 回確認すること。
    // [Pre-Assert手順] - 認証タグ分の上限を超える追加エレメントを返す。
    EXPECT_CALL(mock_cplat, cplat_crypto_encrypt(_, _, _, _, _, _, _, POTR_PACKET_HEADER_SIZE))
        .WillOnce([&](uint8_t *out, size_t *len, const uint8_t *in, size_t input_len,
                      const uint8_t *key, const uint8_t *nonce, const uint8_t *aad, size_t aad_len) {
            encrypted_plaintext_len = input_len;
            return delegate_real_cplat_crypto_encrypt(out, len, in, input_len, key, nonce, aad, aad_len);
        });
    // [Pre-Assert確認_正常系] - 72 [byte]の AAD を指定して暗号化が 1 回呼ばれること。
    // [Pre-Assert手順] - 暗号化する平文長を記録して実際の暗号化へ委譲する。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_try_pop(&ctx.send_queue, _)).Times(0);
    // [Pre-Assert確認_正常系] - 上限を超える追加エレメントを取り出さないこと。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpTest.runOneEntry]
    int actual_ret = runOneEntry();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - パケットの送信処理が成功すること。
    EXPECT_EQ(POTR_DEFAULT_MAX_PAYLOAD - POTR_CRYPTO_TAG_SIZE, encrypted_plaintext_len);
    // [確認_正常系] - 暗号化する平文長が最大ペイロードから認証タグ長を引いた値であること。
    EXPECT_LE(encrypted_plaintext_len, POTR_DEFAULT_MAX_PAYLOAD - POTR_CRYPTO_TAG_SIZE);
    // [確認_正常系] - 暗号化する平文長が最大ペイロードから認証タグ長を引いた値を超えないこと。
}
// [サブ手順参照 名前=potrSendThreadTcpTest.TearDown]

// TCP の DATA 書き込み成功時に tcp_peer_fin_received == 1 なら tcp_sent_after_peer_fin が 1 になることの確認
// [サブ手順参照 名前=potrSendThreadTcpTest.SetUp]
TEST_F(potrSendThreadTcpTest, data_write_after_peer_fin_sets_sent_after_peer_fin)
{
    // Arrange
    cplat_atomic_store_i32(&ctx.tcp_peer_fin_received, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_sent_after_peer_fin, 0, CPLAT_MEMORY_ORDER_RELAXED);

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_all_locked(&ctx, 0, _, _, _, _)).WillOnce(Return(POTR_OK));
    // [Pre-Assert確認_正常系] - DATA の書き込みを 1 回行うこと。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpTest.runOneEntry]
    int actual_ret = runOneEntry();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(1, cplat_atomic_load_i32(&ctx.tcp_sent_after_peer_fin, CPLAT_MEMORY_ORDER_ACQUIRE));
    // [確認_正常系] - 相手 FIN 受理後の送信フラグが 1 になること。
}
// [サブ手順参照 名前=potrSendThreadTcpTest.TearDown]

// TCP の DATA 書き込み成功時に tcp_peer_fin_received == 0 なら tcp_sent_after_peer_fin が 0 のままであることの確認
// [サブ手順参照 名前=potrSendThreadTcpTest.SetUp]
TEST_F(potrSendThreadTcpTest, data_write_before_peer_fin_keeps_sent_after_peer_fin_zero)
{
    // Arrange
    cplat_atomic_store_i32(&ctx.tcp_peer_fin_received, 0, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_sent_after_peer_fin, 0, CPLAT_MEMORY_ORDER_RELAXED);

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_all_locked(&ctx, 0, _, _, _, _)).WillOnce(Return(POTR_OK));
    // [Pre-Assert確認_正常系] - DATA の書き込みを 1 回行うこと。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpTest.runOneEntry]
    int actual_ret = runOneEntry();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.tcp_sent_after_peer_fin, CPLAT_MEMORY_ORDER_ACQUIRE));
    // [確認_正常系] - 相手 FIN 未受理のため送信フラグが 0 のままであること。
}
// [サブ手順参照 名前=potrSendThreadTcpTest.TearDown]

// TCP の DATA 書き込み失敗時は tcp_peer_fin_received == 1 でも tcp_sent_after_peer_fin が 0 のままであることの確認
// [サブ手順参照 名前=potrSendThreadTcpTest.SetUp]
TEST_F(potrSendThreadTcpTest, data_write_failure_keeps_sent_after_peer_fin_zero)
{
    // Arrange
    cplat_atomic_store_i32(&ctx.tcp_peer_fin_received, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_sent_after_peer_fin, 0, CPLAT_MEMORY_ORDER_RELAXED);

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_all_locked(&ctx, 0, _, _, _, _)).WillOnce(Return(POTR_ERR_IO));
    // [Pre-Assert確認_異常系] - DATA の書き込みに失敗すること。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpTest.runOneEntry]
    int actual_ret = runOneEntry();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.tcp_sent_after_peer_fin, CPLAT_MEMORY_ORDER_ACQUIRE));
    // [確認_異常系] - 書き込み失敗時は送信フラグが 0 のままであること。
}
// [サブ手順参照 名前=potrSendThreadTcpTest.TearDown]

// 書き込まれたバイト列を外側パケットに分ける。payload_len はワイヤ上の値 (ネットワーク バイト オーダー) から求める。
static std::vector<potr_packet> split_packets(const std::vector<uint8_t> &bytes)
{
    std::vector<potr_packet> packets;
    size_t offset = 0U;

    while (offset + POTR_PACKET_HEADER_SIZE <= bytes.size())
    {
        uint16_t payload_len_nbo = 0U;
        potr_packet packet = {};
        std::memcpy(&payload_len_nbo, bytes.data() + offset + 34U, sizeof(payload_len_nbo));
        const size_t packet_len = POTR_PACKET_HEADER_SIZE + cplat_ntoh16(payload_len_nbo);
        if (offset + packet_len > bytes.size() ||
            potr_internal_packet_parse(&packet, bytes.data() + offset, packet_len) != POTR_OK)
        {
            break;
        }
        packets.push_back(packet);
        offset += packet_len;
    }
    return packets;
}

// まとめ送りの確認用 fixture。送信キューを模擬し、書き込みを記録する。
// -Wpadded の警告を避けるため、メンバーは 8 [byte] 境界のものだけとし、ソケット値は静的定数とする。
class potrSendThreadTcpBatchTest : public Test
{
  protected:
    static constexpr cplat_socket conn_fd = (cplat_socket)7;
    static constexpr cplat_socket second_fd = (cplat_socket)8;

    // [サブ手順 名前=potrSendThreadTcpBatchTest.SetUp]
    void SetUp() override
    {
        memset(&ctx, 0, sizeof(ctx));
        wire_buf.assign(POTR_PACKET_HEADER_SIZE + 1400U, 0U);
        sealed_buf.assign(POTR_PACKET_HEADER_SIZE + 1400U + POTR_CRYPTO_TAG_SIZE, 0U);
        batch_plain_buf.assign(POTR_SEND_BATCH_MAX_PACKETS * 1400U, 0U);
        batch_wire_buf.assign(POTR_SEND_BATCH_MAX_PACKETS * sealed_buf.size(), 0U);
        ctx.service.type = POTR_TYPE_TCP_BIDIR; // [状態] - TCP_BIDIR とする。
        ctx.global.max_payload = 1400U;         // [状態] - 最大ペイロード長を設定する。
        ctx.crypto_buf = sealed_buf.data();
        ctx.crypto_buf_size = sealed_buf.size();
        ctx.send_wire_buf = wire_buf.data();    // [状態] - 送信 wire 組立バッファーを設定する。
        ctx.send_batch_plain_buf = batch_plain_buf.data();
        ctx.send_batch_wire_buf = batch_wire_buf.data();
        ctx.send_batch_cap = POTR_SEND_BATCH_MAX_PACKETS; // [状態] - まとめ送りの上限を 16 個とする。
        ctx.send_queue.depth = 64U;                     // [状態] - 送信キューの深さを 64 とする。
        ctx.n_path = 1;                                 // [状態] - path 数を 1 とする。
        ctx.tcp_conn_fd[0] = conn_fd;                   // [状態] - path 0 を接続済みとする。
        cplat_atomic_store_i32(&ctx.tcp_active_paths, 1, CPLAT_MEMORY_ORDER_RELAXED);
        delegate_real_cplat_local_lock_create(&ctx.tcp_send_mutex[0]);
        ON_CALL(mock_porter, potr_internal_window_send_push(&ctx.send_window, _))
            .WillByDefault([](potr_internal_window *window, const potr_packet *) {
                window->next_seq++;
                return POTR_OK;
            }); // [状態] - DATA 保持時に送信通番を進める。
        ON_CALL(mock_cplat, cplat_clock_get_realtime(_))
            .WillByDefault(Return(CPLAT_ERR_UNKNOWN)); // [状態] - 封緘結果を固定するため、実時刻の取得を失敗させる。
        ON_CALL(mock_porter, potr_internal_tcp_check_writable(&ctx, 0))
            .WillByDefault(Return(POTR_OK)); // [状態] - path 0 の送信バッファーに空きがあるものとする。
        ON_CALL(mock_porter, potr_internal_send_queue_pop(&ctx.send_queue, _, _))
            .WillByDefault([this](potr_internal_send_queue *, potr_internal_payload_elem *out, cplat_atomic_i32 *) {
                return takeFront(out, POTR_ERR_CANCELED);
            }); // [状態] - 模擬キューの先頭を取り出し、空なら停止を返す。
        ON_CALL(mock_porter, potr_internal_send_queue_try_pop(&ctx.send_queue, _))
            .WillByDefault([this](potr_internal_send_queue *, potr_internal_payload_elem *out) {
                return takeFront(out, POTR_ERR_EMPTY);
            }); // [状態] - 模擬キューの先頭を取り出す。
        ON_CALL(mock_porter, potr_internal_send_queue_peek(&ctx.send_queue, _))
            .WillByDefault([this](potr_internal_send_queue *, potr_internal_payload_elem *out) {
                return peekFront(out, POTR_ERR_EMPTY);
            }); // [状態] - 模擬キューの先頭を参照する。
        ON_CALL(mock_porter, potr_internal_tcp_send_all_locked(_, _, _, _, _, _))
            .WillByDefault([this](potr_context *c, int path_idx, const void *buf, size_t len, size_t *, cplat_error *) {
                const uint8_t *bytes = static_cast<const uint8_t *>(buf);
                write_fds.push_back(c->tcp_conn_fd[path_idx]);
                write_bufs.push_back(buf);
                writes.emplace_back(bytes, bytes + len);
                return POTR_OK;
            }); // [状態] - 書き込みを記録して成功を返す。
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrSendThreadTcpBatchTest.TearDown]
    void TearDown() override
    {
        delegate_real_cplat_local_lock_dispose(ctx.tcp_send_mutex[0]);
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrSendThreadTcpBatchTest.addMessage]
    // len [byte] のメッセージを、送信 API と同じ規則で断片に分けて登録対象へ加える。
    void addMessage(size_t len, uint8_t seed)
    {
        const size_t max_chunk = 1400U - POTR_PAYLOAD_ELEM_HDR_SIZE;
        size_t offset = 0U;

        messages.emplace_back(len);
        std::vector<uint8_t> &data = messages.back();
        for (size_t i = 0U; i < len; ++i)
        {
            data[i] = static_cast<uint8_t>(seed + i);
        }
        while (offset < len)
        {
            potr_internal_payload_elem entry = {};
            const size_t chunk = (std::min)(max_chunk, len - offset);
            entry.peer_id = POTR_PEER_NA;
            entry.flags = (offset + chunk < len) ? POTR_FLAG_MORE_FRAG : 0U;
            entry.payload_len = static_cast<uint16_t>(chunk);
            entry.payload = data.data() + offset;
            entries.push_back(entry);
            offset += chunk;
        }
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrSendThreadTcpBatchTest.runQueue]
    // 登録対象を模擬キューへ積み、送信スレッドを開始して、キューが空になって終了するまで待つ。
    int runQueue()
    {
        queue.assign(entries.begin(), entries.end());
        clearWrites();
        int ret = potr_internal_send_thread_start(&ctx); // [手順] - send を開始する。
        potr_internal_send_thread_stop(&ctx);            // [手順] - send を停止して終了を待つ。
        return ret;
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrSendThreadTcpBatchTest.runReference]
    // まとめ送りを使わない従来の手順で同じ登録対象を送り、書き込まれたバイト列を連結して返す。
    // 送出通番と送信通番は、実行前の値へ戻す。
    std::vector<uint8_t> runReference(size_t *write_count)
    {
        std::vector<uint8_t> joined;
        const size_t cap = ctx.send_batch_cap;

        ctx.send_batch_cap = 0U; // [状態] - まとめ送りを使わない。
        // [サブ手順参照 名前=potrSendThreadTcpBatchTest.runQueue]
        (void)runQueue();
        for (const std::vector<uint8_t> &w : writes)
        {
            joined.insert(joined.end(), w.begin(), w.end());
        }
        *write_count = writes.size();
        clearWrites();
        ctx.send_batch_cap = cap;
        ctx.send_window.next_seq = 0U;
        cplat_atomic_store_u64(&ctx.tx_session.next_counter, 0U, CPLAT_MEMORY_ORDER_RELAXED);
        return joined;
    }
    // [サブ手順終了]

    void clearWrites()
    {
        write_fds.clear();
        write_bufs.clear();
        writes.clear();
    }

    int takeFront(potr_internal_payload_elem *out, int empty_result)
    {
        if (queue.empty())
        {
            return empty_result;
        }
        *out = queue.front();
        queue.pop_front();
        return POTR_OK;
    }

    int peekFront(potr_internal_payload_elem *out, int empty_result)
    {
        if (queue.empty())
        {
            return empty_result;
        }
        *out = queue.front();
        return POTR_OK;
    }

    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx;
    std::vector<uint8_t> wire_buf;
    std::vector<uint8_t> sealed_buf;
    std::vector<uint8_t> batch_plain_buf;
    std::vector<uint8_t> batch_wire_buf;
    std::deque<std::vector<uint8_t>> messages;
    std::vector<potr_internal_payload_elem> entries;
    std::deque<potr_internal_payload_elem> queue;
    std::vector<cplat_socket> write_fds;
    std::vector<const void *> write_bufs;
    std::vector<std::vector<uint8_t>> writes;
};
// 分割されたメッセージの全パケットを、1 回の送信呼び出しで、従来と同じバイト列で書き込むことの確認
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.SetUp]
TEST_F(potrSendThreadTcpBatchTest, fragmented_message_written_once_with_same_bytes)
{
    // Arrange
    size_t reference_writes = 0U;
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.addMessage]
    addMessage(5000U, 0x10U); // [状態] - 4 個の断片に分かれる 5000 [byte] のメッセージを登録する。
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.runReference]
    std::vector<uint8_t> expected = runReference(&reference_writes);

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(4);
    // [Pre-Assert確認_正常系] - 取り出した 4 件を成功として完了させること。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);      // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(4U, reference_writes);     // [確認_正常系] - 従来の手順では 4 回に分けて書き込むこと。
    ASSERT_EQ(1U, writes.size());        // [確認_正常系] - まとめ送りでは 1 回の送信呼び出しで書き込むこと。
    EXPECT_EQ(conn_fd, write_fds[0]);    // [確認_正常系] - path 0 のソケットへ書き込むこと。
    EXPECT_EQ(expected, writes[0]); // [確認_正常系] - 従来の 4 回分を連結したバイト列と一致すること。
    EXPECT_EQ(4U, ctx.send_window.next_seq); // [確認_正常系] - 4 個の DATA の通番を消費すること。
}
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.TearDown]

// キューにある複数のメッセージを、メッセージの境界をまたいで 1 回で書き込むことの確認
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.SetUp]
TEST_F(potrSendThreadTcpBatchTest, messages_across_boundaries_written_once)
{
    // Arrange
    size_t reference_writes = 0U;
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.addMessage 回数=3]
    addMessage(8U, 0x20U);    // [状態] - 分割されないメッセージを登録する。
    addMessage(2000U, 0x30U); // [状態] - 2 個の断片に分かれるメッセージを登録する。
    addMessage(8U, 0x40U);    // [状態] - 分割されないメッセージを登録する。
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.runReference]
    std::vector<uint8_t> expected = runReference(&reference_writes);

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(4);
    // [Pre-Assert確認_正常系] - 取り出した 4 件を成功として完了させること。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);  // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(3U, reference_writes); // [確認_正常系] - 従来の手順では外側パケット 3 個を 3 回で書き込むこと。
    ASSERT_EQ(1U, writes.size());    // [確認_正常系] - まとめ送りでは 1 回の送信呼び出しで書き込むこと。
    EXPECT_EQ(expected, writes[0]); // [確認_正常系] - 従来の 3 回分を連結したバイト列と一致すること。
    EXPECT_EQ(3U, ctx.send_window.next_seq); // [確認_正常系] - 3 個の DATA の通番を消費すること。
}
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.TearDown]

// 上限 (16 個) を超える分は、次の送信呼び出しに分かれることの確認
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.SetUp]
TEST_F(potrSendThreadTcpBatchTest, packets_over_cap_split_into_next_write)
{
    // Arrange
    size_t reference_writes = 0U;
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.addMessage]
    addMessage(20U * (1400U - POTR_PAYLOAD_ELEM_HDR_SIZE), 0x50U); // [状態] - 20 個の断片に分かれるメッセージを登録する。
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.runReference]
    std::vector<uint8_t> expected = runReference(&reference_writes);

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(20);
    // [Pre-Assert確認_正常系] - 取り出した 20 件を成功として完了させること。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);   // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(20U, reference_writes); // [確認_正常系] - 従来の手順では 20 回に分けて書き込むこと。
    ASSERT_EQ(2U, writes.size());     // [確認_正常系] - 2 回の送信呼び出しに分かれること。
    EXPECT_EQ(16U, split_packets(writes[0]).size()); // [確認_正常系] - 1 回目は上限の 16 個であること。
    EXPECT_EQ(4U, split_packets(writes[1]).size());  // [確認_正常系] - 2 回目は残りの 4 個であること。
    std::vector<uint8_t> joined = writes[0];
    joined.insert(joined.end(), writes[1].begin(), writes[1].end());
    EXPECT_EQ(expected, joined); // [確認_正常系] - 2 回分を連結したバイト列が従来の 20 回分と一致すること。
    EXPECT_EQ(20U, ctx.send_window.next_seq); // [確認_正常系] - 20 個の DATA の通番を消費すること。
}
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.TearDown]

// 送出上限までに送出できる数 (3 個) が上限 (16 個) より少ない場合に、まとめ送りをその数で区切ることの確認
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.SetUp]
TEST_F(potrSendThreadTcpBatchTest, batch_is_limited_by_send_limit_allowance)
{
    // Arrange
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.addMessage]
    addMessage(8U * (1400U - POTR_PAYLOAD_ELEM_HDR_SIZE), 0x58U); // [状態] - 8 個の断片に分かれるメッセージを登録する。
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_HELD | 3U, CPLAT_MEMORY_ORDER_RELAXED);
    // [状態] - 相手から送出上限を受けている状態にし、送信スレッドが抑制の判定を行うようにする。
    ON_CALL(mock_porter, potr_internal_flow_control_suppressed(&ctx, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(3U), Return(0)));
    // [状態] - 抑制せず、送出上限までに送出できる数を 3 個として返す。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(8);
    // [Pre-Assert確認_正常系] - 取り出した 8 件を成功として完了させること。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 開始に成功すること。
    ASSERT_EQ(3U, writes.size());   // [確認_正常系] - 3 回の送信呼び出しに分かれること。
    EXPECT_EQ(3U, split_packets(writes[0]).size()); // [確認_正常系] - 1 回目は送出できる数の 3 個であること。
    EXPECT_EQ(3U, split_packets(writes[1]).size()); // [確認_正常系] - 2 回目も 3 個であること。
    EXPECT_EQ(2U, split_packets(writes[2]).size()); // [確認_正常系] - 3 回目は残りの 2 個であること。
    EXPECT_EQ(8U, ctx.send_window.next_seq);        // [確認_正常系] - 8 個の DATA の通番を消費すること。
}
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.TearDown]

// 1 パケットだけの場合は、従来の手順 (crypto_buf への封緘) で書き込むことの確認
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.SetUp]
TEST_F(potrSendThreadTcpBatchTest, single_packet_uses_single_packet_path)
{
    // Arrange
    size_t reference_writes = 0U;
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.addMessage]
    addMessage(8U, 0x60U); // [状態] - 分割されないメッセージを 1 件登録する。
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.runReference]
    std::vector<uint8_t> expected = runReference(&reference_writes);

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);                         // [確認_正常系] - 開始に成功すること。
    ASSERT_EQ(1U, writes.size());                           // [確認_正常系] - 1 回で書き込むこと。
    EXPECT_EQ(static_cast<const void *>(ctx.crypto_buf), write_bufs[0]); // [確認_正常系] - crypto_buf から書き込むこと。
    EXPECT_EQ(expected, writes[0]);                   // [確認_正常系] - 従来の手順と同じバイト列であること。
    EXPECT_EQ(1U, ctx.send_window.next_seq);                // [確認_正常系] - 1 個の DATA の通番を消費すること。
}
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.TearDown]

// まとめた書き込みに失敗した場合、path を停止し、取り出した全件を失敗として完了させ、通番を進めないことの確認
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.SetUp]
TEST_F(potrSendThreadTcpBatchTest, batch_write_failure_completes_all_entries_as_failed)
{
    // Arrange
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.addMessage]
    addMessage(5000U, 0x70U); // [状態] - 4 個の断片に分かれるメッセージを登録する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_all_locked(&ctx, 0, _, _, _, _)).WillOnce(Return(POTR_ERR_IO));
    // [Pre-Assert確認_異常系] - まとめた DATA の書き込みを 1 回試みること。
    // [Pre-Assert手順] - 書き込みの失敗を返す。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete_failed(&ctx.send_queue, POTR_ERR_DISCONNECTED)).Times(4);
    // [Pre-Assert確認_異常系] - 取り出した 4 件を POTR_ERR_DISCONNECTED の送信失敗として完了させること。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(_)).Times(0);
    // [Pre-Assert確認_異常系] - 成功として完了させないこと。
    EXPECT_CALL(mock_porter, potr_internal_window_send_push(_, _)).Times(0);
    // [Pre-Assert確認_異常系] - 送信ウィンドウへ登録しないこと。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);          // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(0U, ctx.send_window.next_seq); // [確認_異常系] - 書き込めなかった DATA の通番を消費しないこと。
}
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.TearDown]

// 途中のパケットの封緘に失敗した場合、どのパケットも書き込まずに path を停止し、全件を失敗として完了させることの確認
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.SetUp]
TEST_F(potrSendThreadTcpBatchTest, batch_seal_failure_writes_nothing)
{
    // Arrange
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.addMessage]
    addMessage(5000U, 0x78U); // [状態] - 4 個の断片に分かれるメッセージを登録する。
    cplat_atomic_store_u64(&ctx.tx_session.next_counter, UINT64_MAX - 2U, CPLAT_MEMORY_ORDER_RELAXED);
    // [状態] - 3 個目の封緘で送出通番が尽きるようにする。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_all_locked(_, _, _, _, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - 封緘できたパケットも含めて書き込まないこと。
    EXPECT_CALL(mock_porter, potr_internal_tcp_conn_request_close(&ctx, 0, 0U)).Times(1);
    // [Pre-Assert確認_異常系] - 封緘できなかった path へ閉じる要求を発行すること。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete_failed(&ctx.send_queue, POTR_ERR_DISCONNECTED)).Times(4);
    // [Pre-Assert確認_異常系] - 取り出した 4 件を POTR_ERR_DISCONNECTED の送信失敗として完了させること。
    EXPECT_CALL(mock_porter, potr_internal_window_send_push(_, _)).Times(0);
    // [Pre-Assert確認_異常系] - 送信ウィンドウへ登録しないこと。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);          // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(0U, ctx.send_window.next_seq); // [確認_異常系] - 書き込めなかった DATA の通番を消費しないこと。
}
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.TearDown]

// 2 経路で、両方の経路へ同じパケット列を 1 回ずつ書き込み、送出通番のずれを受信側の重複検出が受け入れることの確認
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.SetUp]
TEST_F(potrSendThreadTcpBatchTest, two_paths_receive_same_packets)
{
    // Arrange
    potr_internal_replay_window replay = {};
    int replay_results[8] = {};
    ctx.n_path = 2;                  // [状態] - path 数を 2 とする。
    ctx.tcp_conn_fd[1] = second_fd;  // [状態] - path 1 を接続済みとする。
    cplat_atomic_store_i32(&ctx.tcp_active_paths, 2, CPLAT_MEMORY_ORDER_RELAXED);
    delegate_real_cplat_local_lock_create(&ctx.tcp_send_mutex[1]);
    ON_CALL(mock_porter, potr_internal_tcp_check_writable(&ctx, 1))
        .WillByDefault(Return(POTR_OK)); // [状態] - path 1 の送信バッファーに空きがあるものとする。
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.addMessage]
    addMessage(5000U, 0x80U); // [状態] - 4 個の断片に分かれるメッセージを登録する。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.runQueue]
    int actual_ret = runQueue();
    // 受信側が path 1 のパケット列を先に受け取った順で、重複検出へ渡す。
    std::vector<potr_packet> first_path = writes.size() == 2U ? split_packets(writes[0]) : std::vector<potr_packet>();
    std::vector<potr_packet> second_path = writes.size() == 2U ? split_packets(writes[1]) : std::vector<potr_packet>();
    for (size_t i = 0U; i < second_path.size() && i < 4U; ++i)
    {
        replay_results[i] = potr_internal_replay_window_accept(&replay, &second_path[i]); // [手順] - path 1 の分を渡す。
    }
    for (size_t i = 0U; i < first_path.size() && i < 4U; ++i)
    {
        replay_results[4U + i] = potr_internal_replay_window_accept(&replay, &first_path[i]); // [手順] - path 0 の分を渡す。
    }

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);       // [確認_正常系] - 開始に成功すること。
    ASSERT_EQ(2U, writes.size());         // [確認_正常系] - 経路ごとに 1 回ずつ書き込むこと。
    EXPECT_EQ(conn_fd, write_fds[0]);     // [確認_正常系] - 1 回目は path 0 へ書き込むこと。
    EXPECT_EQ(second_fd, write_fds[1]);   // [確認_正常系] - 2 回目は path 1 へ書き込むこと。
    ASSERT_EQ(4U, first_path.size());     // [確認_正常系] - path 0 へ 4 個のパケットを書き込むこと。
    ASSERT_EQ(4U, second_path.size());    // [確認_正常系] - path 1 へ 4 個のパケットを書き込むこと。
    for (size_t i = 0U; i < 4U; ++i)
    {
        EXPECT_EQ(first_path[i].seq_num, second_path[i].seq_num);
        // [確認_正常系 回数=4] - 両経路の i 番目のパケットの通番が一致すること。
        EXPECT_EQ(0, std::memcmp(first_path[i].payload, second_path[i].payload, first_path[i].payload_len));
        // [確認_正常系 回数=4] - 両経路の i 番目のパケットのペイロードが一致すること。
        EXPECT_EQ(static_cast<uint64_t>(i), first_path[i].tx_counter);
        // [確認_正常系 回数=4] - path 0 の送出通番が 0 から続けて割り当てられること。
        EXPECT_EQ(static_cast<uint64_t>(4U + i), second_path[i].tx_counter);
        // [確認_正常系 回数=4] - path 1 の送出通番が path 0 の後に続けて割り当てられること。
    }
    for (size_t i = 0U; i < 8U; ++i)
    {
        EXPECT_EQ(POTR_OK, replay_results[i]);
        // [確認_正常系 回数=8] - 経路間で順序が入れ替わって届いても、重複検出がすべて受け入れること。
    }
    EXPECT_EQ(4U, ctx.send_window.next_seq); // [確認_正常系] - 4 個の DATA の通番を 1 回だけ消費すること。

    // Cleanup
    delegate_real_cplat_local_lock_dispose(ctx.tcp_send_mutex[1]);
}
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.TearDown]

// 2 経路で片方の送信バッファーに空きがない場合、従来と同じく他方へだけ書き込んで成功とすることの確認
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.SetUp]
TEST_F(potrSendThreadTcpBatchTest, two_paths_full_path_is_skipped_as_before)
{
    // Arrange
    ctx.n_path = 2;                 // [状態] - path 数を 2 とする。
    ctx.tcp_conn_fd[1] = second_fd; // [状態] - path 1 を接続済みとする。
    cplat_atomic_store_i32(&ctx.tcp_active_paths, 2, CPLAT_MEMORY_ORDER_RELAXED);
    delegate_real_cplat_local_lock_create(&ctx.tcp_send_mutex[1]);
    ON_CALL(mock_porter, potr_internal_tcp_check_writable(&ctx, 1))
        .WillByDefault(Return(POTR_ERR_TIMEOUT)); // [状態] - path 1 の送信バッファーに空きがないものとする。
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.addMessage]
    addMessage(5000U, 0x90U); // [状態] - 4 個の断片に分かれるメッセージを登録する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(4);
    // [Pre-Assert確認_正常系] - 取り出した 4 件を成功として完了させること。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);             // [確認_正常系] - 開始に成功すること。
    ASSERT_EQ(1U, writes.size());               // [確認_正常系] - 1 回だけ書き込むこと。
    EXPECT_EQ(conn_fd, write_fds[0]);           // [確認_正常系] - 空きのある path 0 へ書き込むこと。
    EXPECT_EQ(4U, split_packets(writes[0]).size()); // [確認_正常系] - 4 個のパケットをまとめて書き込むこと。
    EXPECT_EQ(1, ctx.buf_full_suppress_cnt[1]); // [確認_正常系] - path 1 を飛ばした警告を記録すること。
    EXPECT_EQ(4U, ctx.send_window.next_seq);    // [確認_正常系] - 4 個の DATA の通番を消費すること。

    // Cleanup
    delegate_real_cplat_local_lock_dispose(ctx.tcp_send_mutex[1]);
}
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.TearDown]

// 再送が必要な場合、保持中の DATA を新しい DATA より先に書き込むことの確認
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.SetUp]
TEST_F(potrSendThreadTcpBatchTest, retained_data_written_before_new_batch)
{
    // Arrange
    uint8_t retained_payload[8] = {9U, 9U, 9U, 9U, 9U, 9U, 9U, 9U};
    potr_internal_packet_session_hdr shdr = {};
    potr_packet retained = {};
    (void)potr_internal_packet_build_packed(&retained, &shdr, 0U, retained_payload, sizeof(retained_payload));
    ctx.send_window.base_seq = 0U; // [状態] - 通番 0 の DATA を保持しているものとする。
    ctx.send_window.next_seq = 1U;
    delegate_real_cplat_local_lock_create(&ctx.callback_mutex);
    ON_CALL(mock_porter, potr_internal_path_group_select(_, _, _))
        .WillByDefault([](potr_context *, potr_internal_peer_context *, potr_internal_path_group_result *selection) {
            *selection = {};
            selection->data_paths = 1U;
            selection->selected_paths = 1U;
            selection->tcp_resend = 1;
            return POTR_OK;
        }); // [状態] - 再送が必要な経路選択とする。
    ON_CALL(mock_porter, potr_internal_window_send_get(&ctx.send_window, 0U, _))
        .WillByDefault(DoAll(SetArgPointee<2>(retained), Return(POTR_OK))); // [状態] - 保持中の DATA を返す。
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.addMessage]
    addMessage(5000U, 0xA0U); // [状態] - 4 個の断片に分かれるメッセージを登録する。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 開始に成功すること。
    ASSERT_EQ(2U, writes.size());   // [確認_正常系] - 再送と新しい DATA を 2 回で書き込むこと。
    std::vector<potr_packet> resent = split_packets(writes[0]);
    std::vector<potr_packet> fresh = split_packets(writes[1]);
    ASSERT_EQ(1U, resent.size());   // [確認_正常系] - 1 回目は保持中の DATA 1 個であること。
    EXPECT_EQ(0U, resent[0].seq_num); // [確認_正常系] - 1 回目は通番 0 の保持中の DATA であること。
    ASSERT_EQ(4U, fresh.size());    // [確認_正常系] - 2 回目は新しい DATA 4 個であること。
    EXPECT_EQ(1U, fresh[0].seq_num); // [確認_正常系] - 新しい DATA は通番 1 から始まること。
    EXPECT_EQ(5U, ctx.send_window.next_seq); // [確認_正常系] - 新しい 4 個の DATA の通番を消費すること。

    // Cleanup
    delegate_real_cplat_local_lock_dispose(ctx.callback_mutex);
}
// [サブ手順参照 名前=potrSendThreadTcpBatchTest.TearDown]
// N:1 で宛先のピアが抑制中の場合、取り出したエントリを送出せずに保留することの確認
TEST(potrSendThreadTest, n1_suppressed_peer_entry_is_deferred_without_sending)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context peer = {};
    std::vector<uint8_t> wire_buf(POTR_PACKET_HEADER_SIZE + 1400U);
    uint8_t payload[4] = {1U, 2U, 3U, 4U};
    potr_internal_payload_elem elem = {};
    ctx.is_multi_peer = 1;               // [状態] - N:1 モードとする。
    ctx.global.max_payload = 1400U;      // [状態] - 最大ペイロード長を設定する。
    ctx.send_wire_buf = wire_buf.data(); // [状態] - 送信 wire 組立バッファーを設定する。
    delegate_real_cplat_local_lock_create(&ctx.peers_mutex);
    peer.active = 1;
    peer.peer_id = 5U;
    elem.peer_id = 5U;
    elem.flags = 0U;
    elem.payload_len = (uint16_t)sizeof(payload);
    elem.payload = payload;
    cplat_atomic_store_i32(&ctx.flow_tracked_peers, 1, CPLAT_MEMORY_ORDER_RELAXED);
    // [状態] - 抑制の要求または送出上限を保持しているピアが 1 つあるものとする。
    ON_CALL(mock_porter, potr_internal_flow_control_peer_state(&ctx, 5U, _, _, _))
        .WillByDefault(Return(1)); // [状態] - ピア 5 がフロー制御で抑制中であるものとする。
    ON_CALL(mock_porter, potr_internal_send_queue_peek_timed(_, _, _))
        .WillByDefault(Return(POTR_ERR_TIMEOUT)); // [状態] - 保留中の再確認では、追加のエントリはないものとする。
    ON_CALL(mock_porter, potr_internal_peer_find_by_id(&ctx, 5U))
        .WillByDefault(Return(&peer)); // [状態] - ピア 5 を検索できるようにする。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_pop(&ctx.send_queue, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(elem), Return(POTR_OK)))
        .WillRepeatedly(Return(POTR_ERR_CANCELED));
    // [Pre-Assert手順] - 1 回目はピア 5 宛のエントリを返し、以降は停止を返す。
    EXPECT_CALL(mock_porter, potr_internal_packet_build_packed(_, _, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 抑制中のピア宛のパケットを構築しないこと。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_defer(&ctx.send_queue)).Times(1);
    // [Pre-Assert確認_正常系] - 取り出した 1 件を保留として通知すること。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(0);
    // [Pre-Assert確認_正常系] - 送出していないエントリを完了として通知しないこと。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete_deferred(&ctx.send_queue)).Times(1);
    // [Pre-Assert確認_正常系] - 終了時に残った保留の 1 件を、送出せずに解放して通知すること。
    // Act
    int actual_ret = potr_internal_send_thread_start(&ctx); // [手順] - send を開始する。
    while (peer.flow_deferred_entries == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } // [手順] - 送信スレッドがエントリを保留するまで待つ。
    int32_t total_while_deferred = cplat_atomic_load_i32(&ctx.flow_deferred_total, CPLAT_MEMORY_ORDER_ACQUIRE);
    potr_internal_send_thread_stop(&ctx); // [手順] - send を停止して終了を待つ。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(1, peer.flow_deferred_entries); // [確認_正常系] - ピア 5 宛の保留が 1 件と記録されていること。
    EXPECT_EQ(1, total_while_deferred); // [確認_正常系] - 保留の総数が、保留の時点で 1 に増えていること。
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.flow_deferred_total, CPLAT_MEMORY_ORDER_ACQUIRE));
    // [確認_正常系] - 終了時に保留を破棄した後、保留の総数が 0 へ戻ること。
    EXPECT_EQ(nullptr, ctx.flow_deferred_head); // [確認_正常系] - 終了時に保留のリストが空になっていること。
    // Cleanup
    delegate_real_cplat_local_lock_dispose(ctx.peers_mutex);
}

// N:1 で保留したエントリが、宛先のピアの抑制の解除後に取り出されて完了することの確認
TEST(potrSendThreadTest, n1_deferred_entry_is_released_after_peer_resumes)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context peer = {};
    std::vector<uint8_t> wire_buf(POTR_PACKET_HEADER_SIZE + 1400U);
    uint8_t payload[4] = {1U, 2U, 3U, 4U};
    potr_internal_payload_elem elem = {};
    cplat_atomic_i32 suppressed;
    cplat_atomic_i32 released;
    cplat_atomic_store_i32(&suppressed, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&released, 0, CPLAT_MEMORY_ORDER_RELAXED);
    ctx.is_multi_peer = 1;               // [状態] - N:1 モードとする。
    ctx.global.max_payload = 1400U;      // [状態] - 最大ペイロード長を設定する。
    ctx.send_wire_buf = wire_buf.data(); // [状態] - 送信 wire 組立バッファーを設定する。
    delegate_real_cplat_local_lock_create(&ctx.peers_mutex);
    peer.active = 1;
    peer.peer_id = 5U;
    elem.peer_id = 5U;
    elem.flags = 0U;
    elem.payload_len = (uint16_t)sizeof(payload);
    elem.payload = payload;
    cplat_atomic_store_i32(&ctx.flow_tracked_peers, 1, CPLAT_MEMORY_ORDER_RELAXED);
    // [状態] - 抑制の要求または送出上限を保持しているピアが 1 つあるものとする。
    ON_CALL(mock_porter, potr_internal_flow_control_peer_state(&ctx, 5U, _, _, _))
        .WillByDefault(Invoke(
            [&](potr_context *, potr_peer_id, uint64_t, int *, uint32_t *)
            { return (int)cplat_atomic_load_i32(&suppressed, CPLAT_MEMORY_ORDER_ACQUIRE); }));
    // [状態] - ピア 5 の抑制の状態を、テストから切り替えられるようにする (最初は抑制中)。
    ON_CALL(mock_porter, potr_internal_send_queue_peek_timed(_, _, _))
        .WillByDefault(Return(POTR_ERR_TIMEOUT)); // [状態] - 保留中の再確認では、追加のエントリはないものとする。
    ON_CALL(mock_porter, potr_internal_send_queue_complete_deferred(&ctx.send_queue))
        .WillByDefault(Invoke([&](potr_internal_send_queue *)
                              { cplat_atomic_store_i32(&released, 1, CPLAT_MEMORY_ORDER_RELEASE); }));
    // [状態] - 保留の解放の通知を、テストから検出できるようにする。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_pop(&ctx.send_queue, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(elem), Return(POTR_OK)))
        .WillRepeatedly(Return(POTR_ERR_CANCELED));
    // [Pre-Assert手順] - 1 回目はピア 5 宛のエントリを返し、以降は停止を返す。
    EXPECT_CALL(mock_porter, potr_internal_peer_find_by_id(&ctx, 5U))
        .WillOnce(Return(&peer))
        .WillRepeatedly(Return(nullptr));
    // [Pre-Assert手順] - 保留の記録ではピアを返し、送出の時点では切断済みとして NULL を返す。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_defer(&ctx.send_queue)).Times(1);
    // [Pre-Assert確認_正常系] - 取り出した 1 件を保留として通知すること。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete_deferred(&ctx.send_queue)).Times(1);
    // [Pre-Assert確認_正常系] - 抑制の解除後に、保留していた 1 件を解放して通知すること。
    // Act
    int actual_ret = potr_internal_send_thread_start(&ctx); // [手順] - send を開始する。
    while (peer.flow_deferred_entries == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } // [手順] - 送信スレッドがエントリを保留するまで待つ。
    int released_while_suppressed = (int)cplat_atomic_load_i32(&released, CPLAT_MEMORY_ORDER_ACQUIRE);
    cplat_atomic_store_i32(&suppressed, 0, CPLAT_MEMORY_ORDER_RELEASE); // [手順] - ピア 5 の抑制を解除する。
    while (cplat_atomic_load_i32(&released, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } // [手順] - 送信スレッドが保留を解放するまで待つ。
    potr_internal_send_thread_stop(&ctx); // [手順] - send を停止して終了を待つ。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(0, released_while_suppressed); // [確認_正常系] - 抑制中は保留が解放されていないこと。
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.flow_deferred_total, CPLAT_MEMORY_ORDER_ACQUIRE));
    // [確認_正常系] - 保留を送出の時点で破棄した後、保留の総数が 0 へ戻ること。
    EXPECT_EQ(nullptr, ctx.flow_deferred_head); // [確認_正常系] - 保留のリストが空になっていること。
    // Cleanup
    delegate_real_cplat_local_lock_dispose(ctx.peers_mutex);
}

// 1:1 で相手が抑制を要求している間は DATA の送出を待ち、解除後に送出して完了することの確認
TEST(potrSendThreadTest, suppressed_sender_waits_until_resume_before_sending)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    std::vector<uint8_t> wire_buf(POTR_PACKET_HEADER_SIZE + 1400U);
    uint8_t payload[4] = {1U, 2U, 3U, 4U};
    potr_internal_payload_elem elem = {};
    cplat_atomic_i32 completed;
    cplat_atomic_store_i32(&completed, 0, CPLAT_MEMORY_ORDER_RELAXED);
    ctx.service.type = POTR_TYPE_UNICAST; // [状態] - 片方向 unicast の送信者とする。
    ctx.role = POTR_ROLE_SENDER;
    ctx.global.max_payload = 1400U;      // [状態] - 最大ペイロード長を設定する。
    ctx.send_wire_buf = wire_buf.data(); // [状態] - 送信 wire 組立バッファーを設定する。
    elem.peer_id = POTR_PEER_NA;
    elem.flags = 0U;
    elem.payload_len = (uint16_t)sizeof(payload);
    elem.payload = payload;
    ON_CALL(mock_porter, potr_internal_send_queue_peek(_, _))
        .WillByDefault(Return(POTR_ERR_EMPTY)); // [状態] - 追加のエントリはないものとする。
    cplat_atomic_store_i32(&ctx.flow_request_pending, 1, CPLAT_MEMORY_ORDER_RELAXED);
    // [状態] - 受信者の管理表に抑制の要求が残っている可能性がある状態にする。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_pop(&ctx.send_queue, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(elem), Return(POTR_OK)))
        .WillRepeatedly(Return(POTR_ERR_CANCELED));
    // [Pre-Assert手順] - 1 回目はエントリを返し、以降は停止を返す。
    {
        InSequence sequence;
        EXPECT_CALL(mock_porter, potr_internal_flow_control_suppressed(&ctx, _, _, _))
            .Times(2)
            .WillRepeatedly(DoAll(SetArgPointee<2>(1), Return(1)));
        // [Pre-Assert手順] - 抑制の判定の 1 回目と 2 回目は、抑制中を返す。
        EXPECT_CALL(mock_porter, potr_internal_flow_control_suppressed(&ctx, _, _, _)).WillOnce(Return(0));
        // [Pre-Assert手順] - 抑制の判定の 3 回目は、抑制なしを返す。
        EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue))
            .WillOnce(Invoke([&](potr_internal_send_queue *)
                             { cplat_atomic_store_i32(&completed, 1, CPLAT_MEMORY_ORDER_RELEASE); }));
        // [Pre-Assert確認_正常系] - 抑制の解除の後に、取り出した 1 件の完了を 1 回通知すること。
    }
    EXPECT_CALL(mock_porter, potr_internal_send_queue_wait_flow(&ctx.send_queue, _, 1)).Times(2);
    // [Pre-Assert確認_正常系] - 抑制中の 2 回、判定が返した待機時間 (1 ms) で状態の変化を待つこと。
    // Act
    int actual_ret = potr_internal_send_thread_start(&ctx); // [手順] - send を開始する。
    while (cplat_atomic_load_i32(&completed, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } // [手順] - 送信スレッドが、取り出した 1 件を完了させるまで待つ。
    potr_internal_send_thread_stop(&ctx); // [手順] - send を停止して終了を待つ。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);      // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(nullptr, ctx.send_thread); // [確認_正常系] - 送信スレッドが終了していること。
}

// N:1 で、抑制の要求または送出上限を保持しているピアがなく保留もない場合に、ピアの抑制の判定を行わずに送出へ進むことの確認
TEST(potrSendThreadTest, n1_idle_peers_skip_flow_control_check)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context peer = {};
    std::vector<uint8_t> wire_buf(POTR_PACKET_HEADER_SIZE + 1400U);
    uint8_t payload[4] = {1U, 2U, 3U, 4U};
    potr_internal_payload_elem elem = {};
    ctx.is_multi_peer = 1;               // [状態] - N:1 モードとする。
    ctx.global.max_payload = 1400U;      // [状態] - 最大ペイロード長を設定する。
    ctx.send_wire_buf = wire_buf.data(); // [状態] - 送信 wire 組立バッファーを設定する。
    delegate_real_cplat_local_lock_create(&ctx.peers_mutex);
    peer.active = 1;
    peer.peer_id = 5U;
    elem.peer_id = 5U;
    elem.flags = 0U;
    elem.payload_len = (uint16_t)sizeof(payload);
    elem.payload = payload;
    // [状態] - flow_tracked_peers と flow_deferred_head は 0 のまま (フロー制御パケットを受けていない) とする。
    ON_CALL(mock_porter, potr_internal_send_queue_peek(_, _))
        .WillByDefault(Return(POTR_ERR_EMPTY)); // [状態] - 追加のエントリはないものとする。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_pop(&ctx.send_queue, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(elem), Return(POTR_OK)))
        .WillRepeatedly(Return(POTR_ERR_CANCELED));
    // [Pre-Assert手順] - 1 回目はピア 5 宛のエントリを返し、以降は停止を返す。
    EXPECT_CALL(mock_porter, potr_internal_peer_find_by_id(&ctx, 5U)).WillOnce(Return(&peer)).WillOnce(Return(nullptr));
    // [Pre-Assert確認_正常系] - 送出の処理の存在確認と再検索だけで、ピアを 2 回検索すること。
    // [Pre-Assert手順] - 存在確認ではピアを返し、再検索では解放済みとして NULL を返す。
    EXPECT_CALL(mock_porter, potr_internal_flow_control_peer_state(_, _, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - ピアの抑制の判定を呼び出さないこと。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_defer(_)).Times(0);
    // [Pre-Assert確認_正常系] - エントリを保留しないこと。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(1);
    // [Pre-Assert確認_正常系] - 取り出した 1 件分の完了を、送出の処理で通知すること。
    // Act
    int actual_ret = potr_internal_send_thread_start(&ctx); // [手順] - send を開始する。
    potr_internal_send_thread_stop(&ctx);                   // [手順] - send を停止して終了を待つ。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.flow_deferred_total, CPLAT_MEMORY_ORDER_ACQUIRE));
    // [確認_正常系] - 保留の総数が 0 のままであること。
    // Cleanup
    delegate_real_cplat_local_lock_dispose(ctx.peers_mutex);
}

// 1:1 の片方向 UDP で、受信者の管理表に要求が残っている可能性がない場合に、抑制の判定を行わずに送出することの確認
TEST(potrSendThreadTest, oneway_idle_sender_skips_flow_control_check)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    std::vector<uint8_t> wire_buf(POTR_PACKET_HEADER_SIZE + 1400U);
    uint8_t payload[4] = {1U, 2U, 3U, 4U};
    potr_internal_payload_elem elem = {};
    cplat_atomic_i32 completed;
    cplat_atomic_store_i32(&completed, 0, CPLAT_MEMORY_ORDER_RELAXED);
    ctx.service.type = POTR_TYPE_UNICAST; // [状態] - 片方向 unicast の送信者とする。
    ctx.role = POTR_ROLE_SENDER;
    ctx.global.max_payload = 1400U;      // [状態] - 最大ペイロード長を設定する。
    ctx.send_wire_buf = wire_buf.data(); // [状態] - 送信 wire 組立バッファーを設定する。
    elem.peer_id = POTR_PEER_NA;
    elem.flags = 0U;
    elem.payload_len = (uint16_t)sizeof(payload);
    elem.payload = payload;
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNLIMITED, CPLAT_MEMORY_ORDER_RELAXED);
    // [状態] - flow_request_pending は 0、上限なし (通知済み) の状態とする。
    ON_CALL(mock_porter, potr_internal_send_queue_peek(_, _))
        .WillByDefault(Return(POTR_ERR_EMPTY)); // [状態] - 追加のエントリはないものとする。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_pop(&ctx.send_queue, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(elem), Return(POTR_OK)))
        .WillRepeatedly(Return(POTR_ERR_CANCELED));
    // [Pre-Assert手順] - 1 回目はエントリを返し、以降は停止を返す。
    EXPECT_CALL(mock_porter, potr_internal_flow_control_suppressed(_, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 抑制の判定を呼び出さないこと。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_wait_flow(_, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 抑制の解除を待たないこと。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue))
        .WillOnce(Invoke([&](potr_internal_send_queue *)
                         { cplat_atomic_store_i32(&completed, 1, CPLAT_MEMORY_ORDER_RELEASE); }));
    // [Pre-Assert確認_正常系] - 取り出した 1 件の完了を 1 回通知すること。
    // Act
    int actual_ret = potr_internal_send_thread_start(&ctx); // [手順] - send を開始する。
    while (cplat_atomic_load_i32(&completed, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } // [手順] - 送信スレッドが、取り出した 1 件を完了させるまで待つ。
    potr_internal_send_thread_stop(&ctx); // [手順] - send を停止して終了を待つ。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);      // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(nullptr, ctx.send_thread); // [確認_正常系] - 送信スレッドが終了していること。
}

// TCP で、相手から抑制の要求も送出上限も受けていない場合に、抑制の判定を行わずに送出し、止まった時刻を 0 へ戻すことの確認
// [サブ手順参照 名前=potrSendThreadTcpTest.SetUp]
TEST_F(potrSendThreadTcpTest, idle_sender_skips_flow_control_check)
{
    // Arrange
    ctx.flow_limit_stalled_ms = 777U;
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNLIMITED, CPLAT_MEMORY_ORDER_RELAXED);
    // [状態] - 要求の期限は 0、送出上限は「上限なし (通知済み)」、止まった時刻が残っている状態にする。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_suppressed(_, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 抑制の判定を呼び出さないこと。
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_all_locked(&ctx, 0, _, _, _, _)).WillOnce(Return(POTR_OK));
    // [Pre-Assert確認_正常系] - DATA を 1 回書き込むこと。
    // [Pre-Assert手順] - 書き込みの成功を返す。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(1);
    // [Pre-Assert確認_正常系] - 取り出した 1 件を成功として完了させること。

    // Act
    // [サブ手順参照 名前=potrSendThreadTcpTest.runOneEntry]
    int actual_ret = runOneEntry();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);          // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(1U, ctx.send_window.next_seq); // [確認_正常系] - 書き込んだ DATA の通番を消費すること。
    EXPECT_EQ(0U, ctx.flow_limit_stalled_ms); // [確認_正常系] - 送出上限を保持していない状態として、止まった時刻を 0 へ戻すこと。
}
// [サブ手順参照 名前=potrSendThreadTcpTest.TearDown]

// unicast_bidir で DATA 送出時に POTR_FLAG_SEND_LIMIT と ack_num へ送出上限を相乗りさせることの確認
TEST(potrSendThreadTest, unicast_bidir_piggybacks_send_limit_on_data)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    std::vector<uint8_t> wire_buf(POTR_PACKET_HEADER_SIZE + 1400U);
    std::vector<uint8_t> crypto_buf(POTR_PACKET_HEADER_SIZE + 1400U + POTR_CRYPTO_TAG_SIZE);
    uint8_t payload[4] = {1U, 2U, 3U, 4U};
    potr_internal_payload_elem elem = {};
    potr_packet captured = {};
    int flow_token = 0;
    cplat_atomic_i32 completed;
    cplat_atomic_store_i32(&completed, 0, CPLAT_MEMORY_ORDER_RELAXED);
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    ctx.n_path = 1;
    ctx.sock[0] = (cplat_socket)5;
    ctx.global.max_payload = 1400U;
    ctx.send_wire_buf = wire_buf.data();
    ctx.crypto_buf = crypto_buf.data();
    ctx.crypto_buf_size = crypto_buf.size();
    ctx.flow_control = reinterpret_cast<struct potr_internal_flow_control *>(&flow_token);
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNLIMITED, CPLAT_MEMORY_ORDER_RELAXED);
    elem.peer_id = POTR_PEER_NA;
    elem.flags = 0U;
    elem.payload_len = (uint16_t)sizeof(payload);
    elem.payload = payload;
    ON_CALL(mock_porter, potr_internal_send_queue_peek(_, _)).WillByDefault(Return(POTR_ERR_EMPTY));
    // [状態] - unicast_bidir で flow_control を非 NULL にし、相乗り対象の構成にする。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_pop(&ctx.send_queue, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(elem), Return(POTR_OK)))
        .WillRepeatedly(Return(POTR_ERR_CANCELED));
    EXPECT_CALL(mock_porter, potr_internal_flow_control_piggyback_limit(&ctx, POTR_PEER_NA, _))
        .WillOnce(DoAll(SetArgPointee<2>(123U), Return(1)));
    // [Pre-Assert確認_正常系] - 送出上限の相乗り関数を 1 回呼び出し、123 を受け取ること。
    EXPECT_CALL(mock_porter, potr_internal_flow_control_piggyback_complete(&ctx, POTR_PEER_NA, 123U, 1)).Times(1);
    // [Pre-Assert確認_正常系] - DATA 送出の成功後に相乗り上限を記録すること。
    EXPECT_CALL(mock_porter, potr_internal_udp_sendto(&ctx, 0, _, _, _, _))
        .WillOnce([&](potr_context *, int, const void *buf, size_t len, const cplat_ipv4_endpoint *,
                      cplat_error *) {
            EXPECT_EQ(POTR_OK, potr_internal_packet_parse(&captured, buf, len));
            return POTR_OK;
        });
    // [Pre-Assert確認_正常系] - 送出された DATA パケットを解析できること。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue))
        .WillOnce([&](potr_internal_send_queue *) {
            cplat_atomic_store_i32(&completed, 1, CPLAT_MEMORY_ORDER_RELEASE);
        });
    // Act
    int actual_ret = potr_internal_send_thread_start(&ctx); // [手順] - 送信スレッドを開始する。
    while (cplat_atomic_load_i32(&completed, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    potr_internal_send_thread_stop(&ctx); // [手順] - 送信スレッドを停止する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ((uint16_t)(POTR_FLAG_DATA | POTR_FLAG_SEND_LIMIT), captured.flags);
    // [確認_正常系] - DATA フラグと POTR_FLAG_SEND_LIMIT の両方が立つこと。
    EXPECT_EQ(123U, captured.ack_num); // [確認_正常系] - ack_num に送出上限 123 が載ること。
}

// unicast_bidir_n1 サーバーで DATA 送出時にピアごとの送出上限を相乗りさせることの確認
TEST(potrSendThreadTest, unicast_bidir_n1_piggybacks_send_limit_on_data)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context peer = {};
    std::vector<uint8_t> wire_buf(POTR_PACKET_HEADER_SIZE + 1400U);
    std::vector<uint8_t> crypto_buf(POTR_PACKET_HEADER_SIZE + 1400U + POTR_CRYPTO_TAG_SIZE);
    uint8_t payload[4] = {1U, 2U, 3U, 4U};
    potr_internal_payload_elem elem = {};
    potr_packet captured = {};
    int flow_token = 0;
    cplat_atomic_i32 completed;
    cplat_atomic_store_i32(&completed, 0, CPLAT_MEMORY_ORDER_RELAXED);
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    ctx.is_multi_peer = 1;
    ctx.n_path = 1;
    ctx.sock[0] = (cplat_socket)6;
    ctx.global.max_payload = 1400U;
    ctx.send_wire_buf = wire_buf.data();
    ctx.crypto_buf = crypto_buf.data();
    ctx.crypto_buf_size = crypto_buf.size();
    ctx.flow_control = reinterpret_cast<struct potr_internal_flow_control *>(&flow_token);
    delegate_real_cplat_local_lock_create(&ctx.peers_mutex);
    delegate_real_cplat_local_lock_create(&peer.send_window_mutex);
    peer.active = 1;
    peer.peer_id = 5U;
    peer.dest_addr[0].address = 0x0100007FU;
    peer.dest_addr[0].port = cplat_hton16(9000);
    elem.peer_id = 5U;
    elem.flags = 0U;
    elem.payload_len = (uint16_t)sizeof(payload);
    elem.payload = payload;
    ON_CALL(mock_porter, potr_internal_send_queue_peek(_, _)).WillByDefault(Return(POTR_ERR_EMPTY));
    // [状態] - N:1 サーバーのピア 5 宛 DATA 送出で相乗り対象にする。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_pop(&ctx.send_queue, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(elem), Return(POTR_OK)))
        .WillRepeatedly(Return(POTR_ERR_CANCELED));
    EXPECT_CALL(mock_porter, potr_internal_peer_find_by_id(&ctx, 5U)).WillRepeatedly(Return(&peer));
    EXPECT_CALL(mock_porter, potr_internal_flow_control_piggyback_limit(&ctx, 5U, _))
        .WillOnce(DoAll(SetArgPointee<2>(456U), Return(1)));
    // [Pre-Assert確認_正常系] - ピア 5 の送出上限 456 を取得すること。
    EXPECT_CALL(mock_porter, potr_internal_flow_control_piggyback_complete(&ctx, 5U, 456U, 1)).Times(1);
    // [Pre-Assert確認_正常系] - DATA 送出の成功後に相乗り上限を記録すること。
    EXPECT_CALL(mock_porter, potr_internal_udp_sendto(&ctx, 0, _, _, _, _))
        .WillOnce([&](potr_context *, int, const void *buf, size_t len, const cplat_ipv4_endpoint *,
                      cplat_error *) {
            EXPECT_EQ(POTR_OK, potr_internal_packet_parse(&captured, buf, len));
            return POTR_OK;
        });
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue))
        .WillOnce([&](potr_internal_send_queue *) {
            cplat_atomic_store_i32(&completed, 1, CPLAT_MEMORY_ORDER_RELEASE);
        });
    // Act
    int actual_ret = potr_internal_send_thread_start(&ctx); // [手順] - 送信スレッドを開始する。
    while (cplat_atomic_load_i32(&completed, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    potr_internal_send_thread_stop(&ctx); // [手順] - 送信スレッドを停止する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ((uint16_t)(POTR_FLAG_DATA | POTR_FLAG_SEND_LIMIT), captured.flags);
    // [確認_正常系] - DATA フラグと POTR_FLAG_SEND_LIMIT の両方が立つこと。
    EXPECT_EQ(456U, captured.ack_num); // [確認_正常系] - ack_num に送出上限 456 が載ること。
    // Cleanup
    delegate_real_cplat_local_lock_dispose(peer.send_window_mutex);
    delegate_real_cplat_local_lock_dispose(ctx.peers_mutex);
}

// 片方向 unicast では flow_control があっても相乗り関数を呼ばず POTR_FLAG_SEND_LIMIT を立てないことの確認
TEST(potrSendThreadTest, oneway_unicast_does_not_piggyback_send_limit)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    std::vector<uint8_t> wire_buf(POTR_PACKET_HEADER_SIZE + 1400U);
    std::vector<uint8_t> crypto_buf(POTR_PACKET_HEADER_SIZE + 1400U + POTR_CRYPTO_TAG_SIZE);
    uint8_t payload[4] = {1U, 2U, 3U, 4U};
    potr_internal_payload_elem elem = {};
    potr_packet captured = {};
    int flow_token = 0;
    cplat_atomic_i32 completed;
    cplat_atomic_store_i32(&completed, 0, CPLAT_MEMORY_ORDER_RELAXED);
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.role = POTR_ROLE_SENDER;
    ctx.n_path = 1;
    ctx.sock[0] = (cplat_socket)5;
    ctx.global.max_payload = 1400U;
    ctx.send_wire_buf = wire_buf.data();
    ctx.crypto_buf = crypto_buf.data();
    ctx.crypto_buf_size = crypto_buf.size();
    ctx.flow_control = reinterpret_cast<struct potr_internal_flow_control *>(&flow_token);
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNLIMITED, CPLAT_MEMORY_ORDER_RELAXED);
    elem.peer_id = POTR_PEER_NA;
    elem.flags = 0U;
    elem.payload_len = (uint16_t)sizeof(payload);
    elem.payload = payload;
    ON_CALL(mock_porter, potr_internal_send_queue_peek(_, _)).WillByDefault(Return(POTR_ERR_EMPTY));
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_pop(&ctx.send_queue, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(elem), Return(POTR_OK)))
        .WillRepeatedly(Return(POTR_ERR_CANCELED));
    EXPECT_CALL(mock_porter, potr_internal_flow_control_piggyback_limit(_, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 片方向 unicast では相乗り関数を呼び出さないこと。
    EXPECT_CALL(mock_porter, potr_internal_udp_sendto(&ctx, 0, _, _, _, _))
        .WillOnce([&](potr_context *, int, const void *buf, size_t len, const cplat_ipv4_endpoint *,
                      cplat_error *) {
            EXPECT_EQ(POTR_OK, potr_internal_packet_parse(&captured, buf, len));
            return POTR_OK;
        });
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue))
        .WillOnce([&](potr_internal_send_queue *) {
            cplat_atomic_store_i32(&completed, 1, CPLAT_MEMORY_ORDER_RELEASE);
        });
    // Act
    int actual_ret = potr_internal_send_thread_start(&ctx); // [手順] - 送信スレッドを開始する。
    while (cplat_atomic_load_i32(&completed, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    potr_internal_send_thread_stop(&ctx); // [手順] - 送信スレッドを停止する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);             // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(POTR_FLAG_DATA, captured.flags);  // [確認_正常系] - POTR_FLAG_SEND_LIMIT が立たないこと。
    EXPECT_EQ(0U, captured.ack_num);            // [確認_正常系] - ack_num が 0 であること。
}

/**
 * @brief 片方向 unicast の DATA 送出で、ヘルス スレッドを起こさずに最終 DATA 送信時刻を更新することの確認
 */
TEST(potrSendThreadTest, oneway_unicast_updates_data_send_time_without_health_wake)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    std::vector<uint8_t> wire_buf(POTR_PACKET_HEADER_SIZE + 1400U);
    std::vector<uint8_t> crypto_buf(POTR_PACKET_HEADER_SIZE + 1400U + POTR_CRYPTO_TAG_SIZE);
    uint8_t payload[4] = {1U, 2U, 3U, 4U};
    potr_internal_payload_elem elem = {};
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.role = POTR_ROLE_SENDER;
    ctx.n_path = 1;
    ctx.sock[0] = (cplat_socket)5;
    ctx.health_interval_ms = 100U;
    ctx.global.max_payload = 1400U;
    ctx.send_wire_buf = wire_buf.data();
    ctx.crypto_buf = crypto_buf.data();
    ctx.crypto_buf_size = crypto_buf.size();
    cplat_atomic_store_u64(&ctx.last_valid_data_send_ms[0], 1000U, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNLIMITED, CPLAT_MEMORY_ORDER_RELAXED);
    // [状態] - PING 間隔 100 ms の片方向 unicast 送信者で、最終 DATA 送信時刻を 1000 ms とする。
    elem.peer_id = POTR_PEER_NA;
    elem.payload_len = (uint16_t)sizeof(payload);
    elem.payload = payload;
    ON_CALL(mock_porter, potr_internal_send_queue_peek(_, _))
        .WillByDefault(Return(POTR_ERR_EMPTY)); // [状態] - 追加のエントリはないものとする。
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_))
        .WillByDefault(DoAll(SetArgPointee<0>(1234U), Return(CPLAT_OK)));
    // [状態] - 単調増加時計は 1234 ms を返す。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_pop(&ctx.send_queue, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(elem), Return(POTR_OK)))
        .WillRepeatedly(Return(POTR_ERR_CANCELED));
    // [Pre-Assert手順] - 1 回目は DATA エントリを返し、以降は停止を返す。
    EXPECT_CALL(mock_porter, potr_internal_udp_sendto(&ctx, 0, _, _, _, _)).WillOnce(Return(POTR_OK));
    // [Pre-Assert確認_正常系] - DATA を 1 回送出すること。
    // [Pre-Assert手順] - DATA の送出成功を返す。
    EXPECT_CALL(mock_porter, potr_internal_health_thread_wake(_)).Times(0);
    // [Pre-Assert確認_正常系] - ヘルス スレッドの起床を要求しないこと。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(1);
    // [Pre-Assert確認_正常系] - 取り出した DATA エントリを成功として 1 回完了させること。

    // Act
    int actual_ret = potr_internal_send_thread_start(&ctx); // [手順] - 送信スレッドを開始する。
    potr_internal_send_thread_stop(&ctx); // [手順] - 送信スレッドを停止して終了を待つ。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(1234U, cplat_atomic_load_u64(&ctx.last_valid_data_send_ms[0], CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_正常系] - 最終 DATA 送信時刻を送出時の 1234 ms に更新すること。
}

// DATA の送出失敗では相乗りの通知時刻を更新しないことの確認
TEST(potrSendThreadTest, failed_data_send_does_not_record_piggyback)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    std::vector<uint8_t> wire_buf(POTR_PACKET_HEADER_SIZE + 1400U);
    std::vector<uint8_t> crypto_buf(POTR_PACKET_HEADER_SIZE + 1400U + POTR_CRYPTO_TAG_SIZE);
    uint8_t payload[4] = {1U, 2U, 3U, 4U};
    potr_internal_payload_elem elem = {};
    potr_packet captured = {};
    int flow_token = 0;
    cplat_atomic_i32 completed;
    cplat_atomic_store_i32(&completed, 0, CPLAT_MEMORY_ORDER_RELAXED);
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    ctx.n_path = 1;
    ctx.sock[0] = (cplat_socket)5;
    ctx.global.max_payload = 1400U;
    ctx.send_wire_buf = wire_buf.data();
    ctx.crypto_buf = crypto_buf.data();
    ctx.crypto_buf_size = crypto_buf.size();
    ctx.flow_control = reinterpret_cast<struct potr_internal_flow_control *>(&flow_token);
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNLIMITED, CPLAT_MEMORY_ORDER_RELAXED);
    elem.peer_id = POTR_PEER_NA;
    elem.flags = 0U;
    elem.payload_len = (uint16_t)sizeof(payload);
    elem.payload = payload;
    ON_CALL(mock_porter, potr_internal_send_queue_peek(_, _)).WillByDefault(Return(POTR_ERR_EMPTY));
    // [状態] - unicast_bidir で flow_control を非 NULL にし、相乗り対象の構成にする。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_pop(&ctx.send_queue, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(elem), Return(POTR_OK)))
        .WillRepeatedly(Return(POTR_ERR_CANCELED));
    EXPECT_CALL(mock_porter, potr_internal_flow_control_piggyback_limit(&ctx, POTR_PEER_NA, _))
        .WillOnce(DoAll(SetArgPointee<2>(123U), Return(1)));
    // [Pre-Assert確認_正常系] - 送出上限の相乗り関数を 1 回呼び出し、123 を受け取ること。
    EXPECT_CALL(mock_porter, potr_internal_flow_control_piggyback_complete(&ctx, POTR_PEER_NA, 123U, 0)).Times(1);
    // [Pre-Assert確認_正常系] - DATA 送出に失敗した場合は通知を記録せず予約を解除すること。
    EXPECT_CALL(mock_porter, potr_internal_udp_sendto(&ctx, 0, _, _, _, _))
        .WillOnce([&](potr_context *, int, const void *buf, size_t len, const cplat_ipv4_endpoint *,
                      cplat_error *) {
            EXPECT_EQ(POTR_OK, potr_internal_packet_parse(&captured, buf, len));
            return POTR_ERR_IO;
        });
    // [Pre-Assert確認_正常系] - 送出された DATA パケットを解析できること。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue))
        .WillOnce([&](potr_internal_send_queue *) {
            cplat_atomic_store_i32(&completed, 1, CPLAT_MEMORY_ORDER_RELEASE);
        });
    // Act
    int actual_ret = potr_internal_send_thread_start(&ctx); // [手順] - 送信スレッドを開始する。
    while (cplat_atomic_load_i32(&completed, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    potr_internal_send_thread_stop(&ctx); // [手順] - 送信スレッドを停止する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ((uint16_t)(POTR_FLAG_DATA | POTR_FLAG_SEND_LIMIT), captured.flags);
    // [確認_正常系] - DATA フラグと POTR_FLAG_SEND_LIMIT の両方が立つこと。
    EXPECT_EQ(123U, captured.ack_num); // [確認_正常系] - ack_num に送出上限 123 が載ること。
}

// 1:1 と N:1 で全経路の DATA 送信が失敗しても、last_valid_data_send_ms は変えず、flow_last_data_send_ms は更新し、時計取得失敗時は前回値を保つことの確認
TEST(potrSendThreadTest, all_path_send_failure_updates_flow_last_send_only_and_preserves_on_clock_failure)
{
    // 1:1 ケース (時計成功 -> flow_last_data_send_ms 更新、last_valid_data_send_ms 不変)
    {
        NiceMock<Mock_cplat> mock_cplat;
        NiceMock<Mock_porter> mock_porter;
        potr_context ctx = {};
        std::vector<uint8_t> wire_buf(POTR_PACKET_HEADER_SIZE + 1400U);
        std::vector<uint8_t> crypto_buf(POTR_PACKET_HEADER_SIZE + 1400U + POTR_CRYPTO_TAG_SIZE);
        uint8_t payload[4] = {1U, 2U, 3U, 4U};
        potr_internal_payload_elem elem = {};
        ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
        ctx.role = POTR_ROLE_SENDER;
        ctx.n_path = 1;
        ctx.sock[0] = (cplat_socket)5;
        ctx.global.max_payload = 1400U;
        ctx.send_wire_buf = wire_buf.data();
        ctx.crypto_buf = crypto_buf.data();
        ctx.crypto_buf_size = crypto_buf.size();
        cplat_atomic_store_u64(&ctx.last_valid_data_send_ms[0], 100U, CPLAT_MEMORY_ORDER_RELAXED);
        ctx.flow_last_data_send_ms = 200U;
        cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNLIMITED, CPLAT_MEMORY_ORDER_RELAXED);
        elem.peer_id = POTR_PEER_NA;
        elem.payload_len = (uint16_t)sizeof(payload);
        elem.payload = payload;
        ON_CALL(mock_porter, potr_internal_send_queue_peek(_, _)).WillByDefault(Return(POTR_ERR_EMPTY));
        ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault(DoAll(SetArgPointee<0>(999U), Return(CPLAT_OK)));
        EXPECT_CALL(mock_porter, potr_internal_send_queue_pop(&ctx.send_queue, _, _))
            .WillOnce(DoAll(SetArgPointee<1>(elem), Return(POTR_OK)))
            .WillRepeatedly(Return(POTR_ERR_CANCELED));
        EXPECT_CALL(mock_porter, potr_internal_udp_sendto(&ctx, 0, _, _, _, _)).WillOnce(Return(POTR_ERR_IO));
        EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(1);

        ASSERT_EQ(POTR_OK, potr_internal_send_thread_start(&ctx));
        potr_internal_send_thread_stop(&ctx);

        EXPECT_EQ(100U, cplat_atomic_load_u64(&ctx.last_valid_data_send_ms[0], CPLAT_MEMORY_ORDER_RELAXED));
        EXPECT_EQ(999U, ctx.flow_last_data_send_ms);
    }

    // N:1 ケース (時計成功で peer.flow_last_data_send_ms 更新、時計失敗で前回値保持)
    {
        NiceMock<Mock_cplat> mock_cplat;
        NiceMock<Mock_porter> mock_porter;
        potr_context ctx = {};
        potr_internal_peer_context peer = {};
        std::vector<uint8_t> wire_buf(POTR_PACKET_HEADER_SIZE + 1400U);
        std::vector<uint8_t> crypto_buf(POTR_PACKET_HEADER_SIZE + 1400U + POTR_CRYPTO_TAG_SIZE);
        uint8_t payload[4] = {1U, 2U, 3U, 4U};
        potr_internal_payload_elem elem = {};
        ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
        ctx.is_multi_peer = 1;
        ctx.n_path = 1;
        ctx.sock[0] = (cplat_socket)6;
        ctx.global.max_payload = 1400U;
        ctx.send_wire_buf = wire_buf.data();
        ctx.crypto_buf = crypto_buf.data();
        ctx.crypto_buf_size = crypto_buf.size();
        delegate_real_cplat_local_lock_create(&ctx.peers_mutex);
        delegate_real_cplat_local_lock_create(&peer.send_window_mutex);
        peer.active = 1;
        peer.peer_id = 5U;
        peer.dest_addr[0].address = 0x0100007FU;
        peer.dest_addr[0].port = cplat_hton16(9000);
        peer.flow_last_data_send_ms = 300U;
        elem.peer_id = 5U;
        elem.payload_len = (uint16_t)sizeof(payload);
        elem.payload = payload;
        ON_CALL(mock_porter, potr_internal_send_queue_peek(_, _)).WillByDefault(Return(POTR_ERR_EMPTY));
        ON_CALL(mock_porter, potr_internal_peer_find_by_id(&ctx, 5U)).WillByDefault(Return(&peer));
        EXPECT_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_))
            .WillOnce(DoAll(SetArgPointee<0>(888U), Return(CPLAT_OK)))
            .WillRepeatedly(Return(CPLAT_ERR_UNKNOWN));
        EXPECT_CALL(mock_porter, potr_internal_send_queue_pop(&ctx.send_queue, _, _))
            .WillOnce(DoAll(SetArgPointee<1>(elem), Return(POTR_OK)))
            .WillOnce(DoAll(SetArgPointee<1>(elem), Return(POTR_OK)))
            .WillRepeatedly(Return(POTR_ERR_CANCELED));
        EXPECT_CALL(mock_porter, potr_internal_udp_sendto(&ctx, 0, _, _, _, _))
            .Times(2)
            .WillRepeatedly(Return(POTR_ERR_IO));
        EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(2);

        ASSERT_EQ(POTR_OK, potr_internal_send_thread_start(&ctx));
        potr_internal_send_thread_stop(&ctx);

        EXPECT_EQ(888U, peer.flow_last_data_send_ms);
        delegate_real_cplat_local_lock_dispose(peer.send_window_mutex);
        delegate_real_cplat_local_lock_dispose(ctx.peers_mutex);
    }
}

// UDP のまとめ送りの確認用 fixture。送信キューを模擬し、データグラムの送信を記録する。
// -Wpadded の警告を避けるため、メンバーは 8 [byte] 境界のものだけとし、ソケット値は静的定数とする。
class potrSendThreadUdpBatchTest : public Test
{
  protected:
    static constexpr cplat_socket first_sock = (cplat_socket)5;
    static constexpr cplat_socket second_sock = (cplat_socket)6;

    // [サブ手順 名前=potrSendThreadUdpBatchTest.SetUp]
    void SetUp() override
    {
        memset(&ctx, 0, sizeof(ctx));
        wire_buf.assign(POTR_PACKET_HEADER_SIZE + 1400U, 0U);
        sealed_buf.assign(POTR_PACKET_HEADER_SIZE + 1400U + POTR_CRYPTO_TAG_SIZE, 0U);
        batch_plain_buf.assign(POTR_SEND_BATCH_MAX_PACKETS * 1400U, 0U);
        batch_wire_buf.assign(POTR_SEND_BATCH_MAX_PACKETS * sealed_buf.size(), 0U);
        ctx.service.type = POTR_TYPE_UNICAST; // [状態] - 片方向の unicast とする。
        ctx.role = POTR_ROLE_SENDER;          // [状態] - 送信者とする。
        ctx.global.max_payload = 1400U;       // [状態] - 最大ペイロード長を設定する。
        ctx.crypto_buf = sealed_buf.data();
        ctx.crypto_buf_size = sealed_buf.size();
        ctx.send_wire_buf = wire_buf.data(); // [状態] - 送信 wire 組立バッファーを設定する。
        ctx.send_batch_plain_buf = batch_plain_buf.data();
        ctx.send_batch_wire_buf = batch_wire_buf.data();
        ctx.send_batch_cap = POTR_SEND_BATCH_MAX_PACKETS; // [状態] - まとめ送りの上限を 16 個とする。
        ctx.n_path = 1;                                   // [状態] - path 数を 1 とする。
        ctx.sock[0] = first_sock;                         // [状態] - path 0 のソケットを開いているものとする。
        cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNLIMITED, CPLAT_MEMORY_ORDER_RELAXED);
        ON_CALL(mock_porter, potr_internal_window_send_push(_, _))
            .WillByDefault([](potr_internal_window *window, const potr_packet *) {
                window->next_seq++;
                return POTR_OK;
            }); // [状態] - DATA 保持時に送信通番を進める。
        ON_CALL(mock_cplat, cplat_clock_get_realtime(_))
            .WillByDefault(Return(CPLAT_ERR_UNKNOWN)); // [状態] - 封緘結果を固定するため、実時刻の取得を失敗させる。
        ON_CALL(mock_porter, potr_internal_send_queue_pop(&ctx.send_queue, _, _))
            .WillByDefault([this](potr_internal_send_queue *, potr_internal_payload_elem *out, cplat_atomic_i32 *) {
                return takeFront(out, POTR_ERR_CANCELED);
            }); // [状態] - 模擬キューの先頭を取り出し、空なら停止を返す。
        ON_CALL(mock_porter, potr_internal_send_queue_try_pop(&ctx.send_queue, _))
            .WillByDefault([this](potr_internal_send_queue *, potr_internal_payload_elem *out) {
                return takeFront(out, POTR_ERR_EMPTY);
            }); // [状態] - 模擬キューの先頭を取り出す。
        ON_CALL(mock_porter, potr_internal_send_queue_peek(&ctx.send_queue, _))
            .WillByDefault([this](potr_internal_send_queue *, potr_internal_payload_elem *out) {
                return peekFront(out, POTR_ERR_EMPTY);
            }); // [状態] - 模擬キューの先頭を参照する。
        ON_CALL(mock_porter, potr_internal_udp_sendto_multi(_, _, _, _, _))
            .WillByDefault([this](potr_context *, int path_idx, const cplat_socket_datagram *datagrams, size_t count,
                                  int *results_out) {
                std::vector<std::vector<uint8_t>> call;
                std::vector<int> results;
                if (!multi_results.empty())
                {
                    results = multi_results.front();
                    multi_results.pop_front();
                }
                for (size_t i = 0U; i < count; ++i)
                {
                    const uint8_t *bytes = static_cast<const uint8_t *>(datagrams[i].buf);
                    call.emplace_back(bytes, bytes + datagrams[i].len);
                    multi_endpoints.push_back(datagrams[i].endpoint);
                    results_out[i] = (i < results.size()) ? results[i] : POTR_OK;
                }
                multi_paths.push_back(path_idx);
                multi_calls.push_back(call);
                return POTR_OK;
            }); // [状態] - 一括の送信を記録し、指定した結果 (既定は全件成功) を返す。
        ON_CALL(mock_porter, potr_internal_udp_sendto(_, _, _, _, _, _))
            .WillByDefault([this](potr_context *, int path_idx, const void *buf, size_t len,
                                  const cplat_ipv4_endpoint *, cplat_error *) {
                const uint8_t *bytes = static_cast<const uint8_t *>(buf);
                single_paths.push_back(path_idx);
                single_bufs.push_back(buf);
                single_calls.emplace_back(bytes, bytes + len);
                return POTR_OK;
            }); // [状態] - 1 個の送信を記録して成功を返す。
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrSendThreadUdpBatchTest.setupN1]
    // N:1 サーバーとし、ピア 5 とピア 6 を、それぞれの送信先とともに検索できるようにする。
    void setupN1()
    {
        ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1; // [状態] - N:1 サーバーとする。
        ctx.role = POTR_ROLE_RECEIVER;
        ctx.is_multi_peer = 1; // [状態] - N:1 モードとする。
        delegate_real_cplat_local_lock_create(&ctx.peers_mutex);
        for (size_t i = 0U; i < 2U; ++i)
        {
            peers[i] = {};
            peers[i].active = 1;
            peers[i].peer_id = static_cast<potr_peer_id>(5U + i);
            peers[i].dest_addr[0].address = 0x0100007FU;
            peers[i].dest_addr[0].port = cplat_hton16(static_cast<uint16_t>(9000U + i));
            delegate_real_cplat_local_lock_create(&peers[i].send_window_mutex);
        } // [状態] - ピア 5 とピア 6 を、送信先のポートを分けて用意する。
        ON_CALL(mock_porter, potr_internal_peer_find_by_id(&ctx, 5U))
            .WillByDefault(Return(&peers[0])); // [状態] - ピア 5 を検索できるようにする。
        ON_CALL(mock_porter, potr_internal_peer_find_by_id(&ctx, 6U))
            .WillByDefault(Return(&peers[1])); // [状態] - ピア 6 を検索できるようにする。
        n1_ready = 1;
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrSendThreadUdpBatchTest.TearDown]
    void TearDown() override
    {
        if (n1_ready != 0)
        {
            for (size_t i = 0U; i < 2U; ++i)
            {
                delegate_real_cplat_local_lock_dispose(peers[i].send_window_mutex);
            }
            delegate_real_cplat_local_lock_dispose(ctx.peers_mutex);
        }
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrSendThreadUdpBatchTest.addMessage]
    // len [byte] のメッセージを、送信 API と同じ規則で断片に分けて、peer_id 宛ての登録対象へ加える。
    void addMessage(size_t len, uint8_t seed, potr_peer_id peer_id = POTR_PEER_NA)
    {
        const size_t max_chunk = 1400U - POTR_PAYLOAD_ELEM_HDR_SIZE;
        size_t offset = 0U;

        messages.emplace_back(len);
        std::vector<uint8_t> &data = messages.back();
        for (size_t i = 0U; i < len; ++i)
        {
            data[i] = static_cast<uint8_t>(seed + i);
        }
        while (offset < len)
        {
            potr_internal_payload_elem entry = {};
            const size_t chunk = (std::min)(max_chunk, len - offset);
            entry.peer_id = peer_id;
            entry.flags = (offset + chunk < len) ? POTR_FLAG_MORE_FRAG : 0U;
            entry.payload_len = static_cast<uint16_t>(chunk);
            entry.payload = data.data() + offset;
            entries.push_back(entry);
            offset += chunk;
        }
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrSendThreadUdpBatchTest.runQueue]
    // 登録対象を模擬キューへ積み、送信スレッドを開始して、キューが空になって終了するまで待つ。
    int runQueue()
    {
        queue.assign(entries.begin(), entries.end());
        clearSends();
        int ret = potr_internal_send_thread_start(&ctx); // [手順] - send を開始する。
        potr_internal_send_thread_stop(&ctx);            // [手順] - send を停止して終了を待つ。
        return ret;
    }
    // [サブ手順終了]

    void clearSends()
    {
        multi_paths.clear();
        multi_calls.clear();
        multi_endpoints.clear();
        single_paths.clear();
        single_bufs.clear();
        single_calls.clear();
    }

    int takeFront(potr_internal_payload_elem *out, int empty_result)
    {
        if (queue.empty())
        {
            return empty_result;
        }
        *out = queue.front();
        queue.pop_front();
        return POTR_OK;
    }

    int peekFront(potr_internal_payload_elem *out, int empty_result)
    {
        if (queue.empty())
        {
            return empty_result;
        }
        *out = queue.front();
        return POTR_OK;
    }

    static potr_packet parseDatagram(const std::vector<uint8_t> &bytes)
    {
        potr_packet packet = {};
        EXPECT_EQ(POTR_OK, potr_internal_packet_parse(&packet, bytes.data(), bytes.size()));
        return packet;
    }

    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx;
    potr_internal_peer_context peers[2];
    size_t n1_ready = 0U;
    std::vector<uint8_t> wire_buf;
    std::vector<uint8_t> sealed_buf;
    std::vector<uint8_t> batch_plain_buf;
    std::vector<uint8_t> batch_wire_buf;
    std::deque<std::vector<uint8_t>> messages;
    std::vector<potr_internal_payload_elem> entries;
    std::deque<potr_internal_payload_elem> queue;
    std::deque<std::vector<int>> multi_results;
    std::vector<int> multi_paths;
    std::vector<std::vector<std::vector<uint8_t>>> multi_calls;
    std::vector<const cplat_ipv4_endpoint *> multi_endpoints;
    std::vector<int> single_paths;
    std::vector<const void *> single_bufs;
    std::vector<std::vector<uint8_t>> single_calls;
};

// 5000 [byte] のメッセージの 4 個の断片を、連番の 4 個のデータグラムとして 1 回で送り、送信ウィンドウに 4 個登録することの確認
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.SetUp]
TEST_F(potrSendThreadUdpBatchTest, udp_fragmented_message_sent_in_one_call)
{
    // Arrange
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.addMessage]
    addMessage(5000U, 0x10U); // [状態] - 4 個の断片に分かれる 5000 [byte] のメッセージを登録する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_udp_sendto(_, _, _, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - potr_internal_udp_sendto() が呼び出されないこと。
    EXPECT_CALL(mock_porter, potr_internal_window_send_push(&ctx.send_window, _)).Times(4);
    // [Pre-Assert確認_正常系] - potr_internal_window_send_push() が 4 回呼び出されること。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(4);
    // [Pre-Assert確認_正常系] - 取り出した 4 件を成功として完了させること。

    // Act
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);       // [確認_正常系] - 開始に成功すること。
    ASSERT_EQ(1U, multi_calls.size());    // [確認_正常系] - 一括の送信を 1 回呼び出すこと。
    EXPECT_EQ(0, multi_paths[0]);         // [確認_正常系] - path 0 へ送ること。
    ASSERT_EQ(4U, multi_calls[0].size()); // [確認_正常系] - 4 個のデータグラムを送ること。
    for (size_t i = 0U; i < 4U; ++i)
    {
        potr_packet packet = parseDatagram(multi_calls[0][i]);
        EXPECT_EQ(static_cast<uint32_t>(i), packet.seq_num);
        // [確認_正常系 回数=4] - i 番目のデータグラムの通番が i であること。
        EXPECT_EQ(&ctx.dest_addr[0], multi_endpoints[i]);
        // [確認_正常系 回数=4] - i 番目のデータグラムの送信先が path 0 の送信先であること。
    }
    EXPECT_EQ(4U, ctx.send_window.next_seq); // [確認_正常系] - 4 個の DATA の通番を消費すること。
}
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.TearDown]

// キューにある複数のメッセージを、メッセージの境界をまたいで 1 回の一括の送信にまとめることの確認
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.SetUp]
TEST_F(potrSendThreadUdpBatchTest, udp_messages_across_boundaries_sent_in_one_call)
{
    // Arrange
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.addMessage 回数=3]
    addMessage(8U, 0x20U);    // [状態] - 分割されないメッセージを登録する。
    addMessage(2000U, 0x30U); // [状態] - 2 個の断片に分かれるメッセージを登録する。
    addMessage(8U, 0x40U);    // [状態] - 分割されないメッセージを登録する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(4);
    // [Pre-Assert確認_正常系] - 取り出した 4 件を成功として完了させること。

    // Act
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);          // [確認_正常系] - 開始に成功すること。
    ASSERT_EQ(1U, multi_calls.size());       // [確認_正常系] - 一括の送信を 1 回呼び出すこと。
    EXPECT_EQ(3U, multi_calls[0].size());    // [確認_正常系] - 外側パケット 3 個を 1 回で送ること。
    EXPECT_EQ(0U, single_calls.size());      // [確認_正常系] - 1 個の送信を呼び出さないこと。
    EXPECT_EQ(3U, ctx.send_window.next_seq); // [確認_正常系] - 3 個の DATA の通番を消費すること。
}
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.TearDown]

// 上限 (16 個) を超える分は、次の一括の送信に分かれることの確認
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.SetUp]
TEST_F(potrSendThreadUdpBatchTest, udp_packets_over_cap_split_into_next_call)
{
    // Arrange
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.addMessage]
    addMessage(20U * (1400U - POTR_PAYLOAD_ELEM_HDR_SIZE), 0x50U); // [状態] - 20 個の断片に分かれるメッセージを登録する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(20);
    // [Pre-Assert確認_正常系] - 取り出した 20 件を成功として完了させること。

    // Act
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);        // [確認_正常系] - 開始に成功すること。
    ASSERT_EQ(2U, multi_calls.size());     // [確認_正常系] - 2 回の一括の送信に分かれること。
    EXPECT_EQ(16U, multi_calls[0].size()); // [確認_正常系] - 1 回目は上限の 16 個であること。
    EXPECT_EQ(4U, multi_calls[1].size());  // [確認_正常系] - 2 回目は残りの 4 個であること。
    EXPECT_EQ(16U, parseDatagram(multi_calls[1][0]).seq_num); // [確認_正常系] - 2 回目は通番 16 から続くこと。
    EXPECT_EQ(20U, ctx.send_window.next_seq); // [確認_正常系] - 20 個の DATA の通番を消費すること。
}
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.TearDown]

// 送出上限までに送出できる数 (3 個) が上限 (16 個) より少ない場合に、一括の送信をその数で区切ることの確認
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.SetUp]
TEST_F(potrSendThreadUdpBatchTest, udp_batch_is_limited_by_send_limit_allowance)
{
    // Arrange
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.addMessage]
    addMessage(8U * (1400U - POTR_PAYLOAD_ELEM_HDR_SIZE), 0x58U); // [状態] - 8 個の断片に分かれるメッセージを登録する。
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_HELD | 3U, CPLAT_MEMORY_ORDER_RELAXED);
    // [状態] - 相手から送出上限を受けている状態にし、送信スレッドが抑制の判定を行うようにする。
    ON_CALL(mock_porter, potr_internal_flow_control_suppressed(&ctx, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(3U), Return(0)));
    // [状態] - 抑制せず、送出上限までに送出できる数を 3 個として返す。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(8);
    // [Pre-Assert確認_正常系] - 取り出した 8 件を成功として完了させること。

    // Act
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);          // [確認_正常系] - 開始に成功すること。
    ASSERT_EQ(3U, multi_calls.size());       // [確認_正常系] - 3 回の一括の送信に分かれること。
    EXPECT_EQ(3U, multi_calls[0].size());    // [確認_正常系] - 1 回目は送出できる数の 3 個であること。
    EXPECT_EQ(3U, multi_calls[1].size());    // [確認_正常系] - 2 回目も 3 個であること。
    EXPECT_EQ(2U, multi_calls[2].size());    // [確認_正常系] - 3 回目は残りの 2 個であること。
    EXPECT_EQ(8U, ctx.send_window.next_seq); // [確認_正常系] - 8 個の DATA の通番を消費すること。
}
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.TearDown]

// 暗号化で、各データグラムを個別に封緘し、受信側の開封で元の外側パケットに戻ることの確認
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.SetUp]
TEST_F(potrSendThreadUdpBatchTest, udp_encrypted_batch_seals_each_packet)
{
    // Arrange
    std::vector<std::vector<uint8_t>> plain;
    std::vector<uint8_t> opened(1400U, 0U);
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.addMessage]
    addMessage(5000U, 0x60U); // [状態] - 4 個の断片に分かれるメッセージを登録する。
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.runQueue]
    (void)runQueue(); // [状態] - 暗号化しない場合のデータグラムを、元の外側パケットとして記録する。
    ASSERT_EQ(1U, multi_calls.size()); // [状態確認] - 暗号化しない場合も 1 回で送ること。
    for (const std::vector<uint8_t> &d : multi_calls[0])
    {
        potr_packet packet = parseDatagram(d);
        plain.emplace_back(packet.payload, packet.payload + packet.payload_len);
    }
    ctx.send_window.next_seq = 0U;
    cplat_atomic_store_u64(&ctx.tx_session.next_counter, 0U, CPLAT_MEMORY_ORDER_RELAXED);
    ctx.service.encrypt_enabled = 1; // [状態] - パケットを暗号化する。
    for (size_t i = 0U; i < sizeof(ctx.service.encrypt_key); ++i)
    {
        ctx.service.encrypt_key[i] = static_cast<uint8_t>(0xA0U + i);
    } // [状態] - 事前共有鍵を設定する。

    // Act
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);       // [確認_正常系] - 開始に成功すること。
    ASSERT_EQ(1U, multi_calls.size());    // [確認_正常系] - 一括の送信を 1 回呼び出すこと。
    ASSERT_EQ(4U, multi_calls[0].size()); // [確認_正常系] - 4 個のデータグラムを送ること。
    ASSERT_EQ(4U, plain.size());          // [確認_正常系] - 元の外側パケットが 4 個あること。
    for (size_t i = 0U; i < 4U; ++i)
    {
        potr_packet packet = parseDatagram(multi_calls[0][i]);
        EXPECT_EQ(static_cast<uint64_t>(i), packet.tx_counter);
        // [確認_正常系 回数=4] - i 番目のデータグラムを送出通番 i で個別に封緘すること。
        EXPECT_EQ(POTR_OK, potr_internal_packet_open(&packet, multi_calls[0][i].data(), ctx.service.encrypt_key,
                                                     opened.data(), opened.size()));
        // [確認_正常系 回数=4] - i 番目のデータグラムを事前共有鍵で開封できること。
        EXPECT_EQ(static_cast<uint32_t>(i), packet.seq_num);
        // [確認_正常系 回数=4] - 開封した i 番目の外側パケットの通番が i であること。
        EXPECT_EQ(plain[i], std::vector<uint8_t>(packet.payload, packet.payload + packet.payload_len));
        // [確認_正常系 回数=4] - 開封した i 番目のペイロードが元の外側パケットと一致すること。
    }
}
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.TearDown]

// 2 経路で、経路ごとに 1 回ずつ、同じ通番の 4 個を送ることの確認
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.SetUp]
TEST_F(potrSendThreadUdpBatchTest, udp_two_paths_send_same_packets_per_path)
{
    // Arrange
    ctx.n_path = 2;            // [状態] - path 数を 2 とする。
    ctx.sock[1] = second_sock; // [状態] - path 1 のソケットを開いているものとする。
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.addMessage]
    addMessage(5000U, 0x70U); // [状態] - 4 個の断片に分かれるメッセージを登録する。

    // Act
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);       // [確認_正常系] - 開始に成功すること。
    ASSERT_EQ(2U, multi_calls.size());    // [確認_正常系] - 経路ごとに 1 回ずつ一括の送信を呼び出すこと。
    EXPECT_EQ(0, multi_paths[0]);         // [確認_正常系] - 1 回目は path 0 へ送ること。
    EXPECT_EQ(1, multi_paths[1]);         // [確認_正常系] - 2 回目は path 1 へ送ること。
    ASSERT_EQ(4U, multi_calls[0].size()); // [確認_正常系] - path 0 へ 4 個を送ること。
    ASSERT_EQ(4U, multi_calls[1].size()); // [確認_正常系] - path 1 へ 4 個を送ること。
    for (size_t i = 0U; i < 4U; ++i)
    {
        potr_packet first_path = parseDatagram(multi_calls[0][i]);
        potr_packet second_path = parseDatagram(multi_calls[1][i]);
        EXPECT_EQ(first_path.seq_num, second_path.seq_num);
        // [確認_正常系 回数=4] - 両経路の i 番目のデータグラムの通番が一致すること。
        EXPECT_EQ(&ctx.dest_addr[1], multi_endpoints[4U + i]);
        // [確認_正常系 回数=4] - path 1 の i 番目のデータグラムの送信先が path 1 の送信先であること。
    }
    EXPECT_EQ(4U, ctx.send_window.next_seq); // [確認_正常系] - 4 個の DATA の通番を 1 回だけ消費すること。
}
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.TearDown]

// 一部のデータグラムの送信が失敗しても、4 個すべてを送信ウィンドウに登録し、エントリを成功として完了させることの確認
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.SetUp]
TEST_F(potrSendThreadUdpBatchTest, udp_partial_failure_registers_all_packets)
{
    // Arrange
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.addMessage]
    addMessage(5000U, 0x78U); // [状態] - 4 個の断片に分かれるメッセージを登録する。
    multi_results.push_back({POTR_OK, POTR_ERR_IO, POTR_OK, POTR_ERR_TIMEOUT});
    // [状態] - 一括の送信で、2 個目と 4 個目の送信が失敗したことを返す。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_window_send_push(&ctx.send_window, _)).Times(4);
    // [Pre-Assert確認_正常系] - potr_internal_window_send_push() が 4 回呼び出されること。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(4);
    // [Pre-Assert確認_正常系] - 取り出した 4 件を成功として完了させること。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete_failed(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - 送信失敗として完了させないこと。

    // Act
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);          // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(1U, multi_calls.size());       // [確認_正常系] - 一括の送信を 1 回呼び出すこと。
    EXPECT_EQ(4U, ctx.send_window.next_seq); // [確認_正常系] - 4 個の DATA の通番を消費すること。
}
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.TearDown]

// 最初の経路の封緘に失敗した場合、何も送らず、送信ウィンドウに登録せず、通番を進めないことの確認
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.SetUp]
TEST_F(potrSendThreadUdpBatchTest, udp_seal_failure_on_first_path_sends_nothing)
{
    // Arrange
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.addMessage]
    addMessage(5000U, 0x80U); // [状態] - 4 個の断片に分かれるメッセージを登録する。
    cplat_atomic_store_u64(&ctx.tx_session.next_counter, UINT64_MAX - 2U, CPLAT_MEMORY_ORDER_RELAXED);
    // [状態] - 3 個目の封緘で送出通番が尽きるようにする。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_udp_sendto_multi(_, _, _, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - 封緘できたパケットも含めて一括の送信を呼び出さないこと。
    EXPECT_CALL(mock_porter, potr_internal_udp_sendto(_, _, _, _, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - 1 個の送信も呼び出さないこと。
    EXPECT_CALL(mock_porter, potr_internal_window_send_push(_, _)).Times(0);
    // [Pre-Assert確認_異常系] - 送信ウィンドウへ登録しないこと。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete_failed(&ctx.send_queue, POTR_ERR_DISCONNECTED)).Times(4);
    // [Pre-Assert確認_異常系] - 取り出した 4 件を POTR_ERR_DISCONNECTED の送信失敗として完了させること。

    // Act
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);          // [確認_正常系] - 開始に成功すること。
    EXPECT_EQ(0U, ctx.send_window.next_seq); // [確認_異常系] - 送れなかった DATA の通番を消費しないこと。
}
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.TearDown]

// unicast_bidir で、送出上限を先頭のパケットにだけ載せ、先頭を送れた場合に限り記録することの確認
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.SetUp]
TEST_F(potrSendThreadUdpBatchTest, udp_piggyback_on_first_packet_only)
{
    // Arrange
    int flow_token = 0;
    std::vector<std::vector<std::vector<uint8_t>>> sent_calls;
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR; // [状態] - unicast_bidir とする。
    ctx.flow_control = reinterpret_cast<struct potr_internal_flow_control *>(&flow_token);
    // [状態] - フロー制御を有効にし、相乗り対象の構成にする。
    ON_CALL(mock_porter, potr_internal_flow_control_piggyback_limit(&ctx, POTR_PEER_NA, _))
        .WillByDefault(DoAll(SetArgPointee<2>(123U), Return(1))); // [状態] - 相乗りする送出上限として 123 を返す。
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.addMessage]
    addMessage(5000U, 0x88U); // [状態] - 4 個の断片に分かれるメッセージを登録する。
    multi_results.push_back({POTR_OK, POTR_OK, POTR_OK, POTR_OK});
    // [状態] - 1 回目の一括の送信は全件成功とする。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_piggyback_limit(&ctx, POTR_PEER_NA, _)).Times(2);
    // [Pre-Assert確認_正常系] - 相乗りの値を、一括の送信ごとに 1 回ずつ、2 回求めること。
    EXPECT_CALL(mock_porter, potr_internal_flow_control_piggyback_complete(&ctx, POTR_PEER_NA, 123U, 1)).Times(1);
    // [Pre-Assert確認_正常系] - 先頭を送れた 1 回目に、相乗りを記録済みにすること。
    EXPECT_CALL(mock_porter, potr_internal_flow_control_piggyback_complete(&ctx, POTR_PEER_NA, 123U, 0)).Times(1);
    // [Pre-Assert確認_正常系] - 先頭を送れなかった 2 回目に、相乗りを記録しないこと。

    // Act
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.runQueue]
    int first_ret = runQueue();
    sent_calls = multi_calls;
    multi_results.push_back({POTR_ERR_IO, POTR_OK, POTR_OK, POTR_OK});
    // [手順] - 2 回目の一括の送信は先頭だけ失敗とする。
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.runQueue]
    int second_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, first_ret);        // [確認_正常系] - 1 回目の開始に成功すること。
    EXPECT_EQ(POTR_OK, second_ret);       // [確認_正常系] - 2 回目の開始に成功すること。
    ASSERT_EQ(1U, sent_calls.size());     // [確認_正常系] - 1 回目に一括の送信を 1 回呼び出すこと。
    ASSERT_EQ(4U, sent_calls[0].size());  // [確認_正常系] - 1 回目に 4 個のデータグラムを送ること。
    potr_packet head = parseDatagram(sent_calls[0][0]);
    EXPECT_EQ((uint16_t)(POTR_FLAG_DATA | POTR_FLAG_SEND_LIMIT), head.flags);
    // [確認_正常系] - 先頭のデータグラムに DATA フラグと POTR_FLAG_SEND_LIMIT の両方が立つこと。
    EXPECT_EQ(123U, head.ack_num); // [確認_正常系] - 先頭のデータグラムの ack_num に送出上限 123 が載ること。
    for (size_t i = 1U; i < 4U; ++i)
    {
        potr_packet rest = parseDatagram(sent_calls[0][i]);
        EXPECT_EQ((uint16_t)POTR_FLAG_DATA, rest.flags);
        // [確認_正常系 回数=3] - 2 個目以降のデータグラムに POTR_FLAG_SEND_LIMIT が立たないこと。
    }
}
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.TearDown]

// 1 個の外側パケットの場合と、パッキングの待ちがある場合に、従来の 1 個の送信 (crypto_buf への封緘) で送ることの確認
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.SetUp]
TEST_F(potrSendThreadUdpBatchTest, udp_single_packet_uses_single_packet_path)
{
    // Arrange
    std::vector<const void *> first_bufs;
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.addMessage]
    addMessage(8U, 0x90U); // [状態] - 分割されないメッセージを 1 件登録する。
    ON_CALL(mock_porter, potr_internal_send_queue_peek_timed(&ctx.send_queue, _, _))
        .WillByDefault(Return(POTR_ERR_TIMEOUT)); // [状態] - パッキングの待ちでは、追加のエントリはないものとする。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_udp_sendto_multi(_, _, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 一括の送信を呼び出さないこと。

    // Act
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.runQueue]
    int no_wait_ret = runQueue();
    first_bufs = single_bufs;
    ctx.service.pack_wait_ms = 10U; // [手順] - パッキングの待ちを 10 [ms] とする。
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.runQueue]
    int wait_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, no_wait_ret); // [確認_正常系] - 待ちなしの開始に成功すること。
    EXPECT_EQ(POTR_OK, wait_ret);    // [確認_正常系] - 待ちありの開始に成功すること。
    ASSERT_EQ(1U, first_bufs.size()); // [確認_正常系] - 待ちなしで 1 個の送信を 1 回呼び出すこと。
    EXPECT_EQ(static_cast<const void *>(ctx.crypto_buf), first_bufs[0]); // [確認_正常系] - 待ちなしで crypto_buf から送ること。
    ASSERT_EQ(1U, single_bufs.size()); // [確認_正常系] - 待ちありで 1 個の送信を 1 回呼び出すこと。
    EXPECT_EQ(static_cast<const void *>(ctx.crypto_buf), single_bufs[0]); // [確認_正常系] - 待ちありで crypto_buf から送ること。
    EXPECT_EQ(2U, ctx.send_window.next_seq); // [確認_正常系] - 2 回の実行で 2 個の DATA の通番を消費すること。
}
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.TearDown]

// N:1 で、同じピア宛ての断片が 1 回の一括の送信にまとまり、別のピア宛てのエントリで区切られることの確認
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.SetUp]
TEST_F(potrSendThreadUdpBatchTest, n1_same_peer_entries_batched_and_other_peer_ends_batch)
{
    // Arrange
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.setupN1]
    setupN1();
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.addMessage 回数=3]
    addMessage(5000U, 0xA0U, 5U); // [状態] - ピア 5 宛てに、4 個の断片に分かれるメッセージを登録する。
    addMessage(2000U, 0xB0U, 6U); // [状態] - ピア 6 宛てに、2 個の断片に分かれるメッセージを登録する。
    addMessage(8U, 0xC0U, 5U);    // [状態] - ピア 5 宛てに、分割されないメッセージを登録する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(7);
    // [Pre-Assert確認_正常系] - 取り出した 7 件を完了させること。

    // Act
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);       // [確認_正常系] - 開始に成功すること。
    ASSERT_EQ(2U, multi_calls.size());    // [確認_正常系] - 一括の送信を 2 回呼び出すこと。
    ASSERT_EQ(4U, multi_calls[0].size()); // [確認_正常系] - 1 回目はピア 5 宛ての 4 個であること。
    ASSERT_EQ(2U, multi_calls[1].size()); // [確認_正常系] - 2 回目はピア 6 宛ての 2 個であること。
    for (size_t i = 0U; i < 4U; ++i)
    {
        EXPECT_EQ(&peers[0].dest_addr[0], multi_endpoints[i]);
        // [確認_正常系 回数=4] - 1 回目の i 番目のデータグラムの送信先がピア 5 であること。
    }
    for (size_t i = 0U; i < 2U; ++i)
    {
        EXPECT_EQ(&peers[1].dest_addr[0], multi_endpoints[4U + i]);
        // [確認_正常系 回数=2] - 2 回目の i 番目のデータグラムの送信先がピア 6 であること。
    }
    ASSERT_EQ(1U, single_calls.size()); // [確認_正常系] - ピア 5 宛ての最後の 1 個は、1 個の送信で送ること。
    EXPECT_EQ(5U, peers[0].send_window.next_seq); // [確認_正常系] - ピア 5 の通番を 5 個消費すること。
    EXPECT_EQ(2U, peers[1].send_window.next_seq); // [確認_正常系] - ピア 6 の通番を 2 個消費すること。
}
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.TearDown]

// N:1 で、ピアの送出上限までに送出できる数 (3 個) を超えてまとめないことの確認
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.SetUp]
TEST_F(potrSendThreadUdpBatchTest, n1_batch_is_limited_by_peer_allowance)
{
    // Arrange
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.setupN1]
    setupN1();
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.addMessage]
    addMessage(8U * (1400U - POTR_PAYLOAD_ELEM_HDR_SIZE), 0xD0U, 5U); // [状態] - ピア 5 宛てに、8 個の断片に分かれるメッセージを登録する。
    cplat_atomic_store_i32(&ctx.flow_tracked_peers, 1, CPLAT_MEMORY_ORDER_RELAXED);
    // [状態] - 抑制の要求または送出上限を保持しているピアが 1 つあるものとする。
    ON_CALL(mock_porter, potr_internal_flow_control_peer_state(&ctx, 5U, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<4>(3U), Return(0)));
    // [状態] - ピア 5 を抑制せず、送出上限までに送出できる数を 3 個として返す。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_peer_state(&ctx, 5U, _, _, NotNull())).Times(3);
    // [Pre-Assert確認_正常系] - 一括の送信ごとに、送出できる数の格納先を指定してピア 5 の状態を 3 回判定すること。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_complete(&ctx.send_queue)).Times(8);
    // [Pre-Assert確認_正常系] - 取り出した 8 件を完了させること。

    // Act
    // [サブ手順参照 名前=potrSendThreadUdpBatchTest.runQueue]
    int actual_ret = runQueue();

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);       // [確認_正常系] - 開始に成功すること。
    ASSERT_EQ(3U, multi_calls.size());    // [確認_正常系] - 3 回の一括の送信に分かれること。
    EXPECT_EQ(3U, multi_calls[0].size()); // [確認_正常系] - 1 回目は送出できる数の 3 個であること。
    EXPECT_EQ(3U, multi_calls[1].size()); // [確認_正常系] - 2 回目も 3 個であること。
    EXPECT_EQ(2U, multi_calls[2].size()); // [確認_正常系] - 3 回目は残りの 2 個であること。
    EXPECT_EQ(8U, peers[0].send_window.next_seq); // [確認_正常系] - ピア 5 の通番を 8 個消費すること。
}
// [サブ手順参照 名前=potrSendThreadUdpBatchTest.TearDown]
