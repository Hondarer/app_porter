#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <cplat/crt/stdlib.h>
#include <porter/porter_result.h>
#include <porter/protocol/packet_seal.h>
#include "thread_recv_validate.h"
#include "thread_recv_dispatch.h"
#include "potr_send_thread.inject.h"
#include "thread_recv_window.inject.h"
#include "potr_recv_thread.inject.h"
#include "potr_health_thread.inject.h"
#include <array>
#include <cstring>
#include <set>
#include <vector>

using namespace testing;

// DATA の経路複製、同じ next_seq の PING と再送で nonce と時刻を更新することの確認
TEST(packetEmissionTest, udp_paths_ping_and_retransmissions_are_sealed_independently)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    thread_recv_slot slot = {};
    uint8_t packed[256] = {};
    uint8_t sealed[256] = {};
    uint8_t received[256] = {};
    uint8_t plaintext[256] = {};
    uint8_t health[POTR_MAX_PATH] = {};
    std::vector<std::vector<uint8_t>> emitted;
    std::set<std::array<uint8_t, POTR_CRYPTO_NONCE_SIZE>> nonces;
    uint64_t monotonic = 1000;
    int64_t seconds = 10;
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.service.encrypt_enabled = 1;
    ctx.global.max_payload = 128;
    ctx.n_path = 2;
    ctx.send_wire_buf = packed;
    ctx.crypto_buf = sealed;
    ctx.crypto_buf_size = sizeof(sealed);
    ctx.recv_buf = received;
    ctx.recv_crypto_buf = plaintext;
    packed[POTR_PACKET_HEADER_SIZE] = 7;
    packed[POTR_PACKET_HEADER_SIZE + 1] = 8;
    ASSERT_EQ(CPLAT_OK, delegate_real_cplat_local_lock_create(&ctx.callback_mutex));
    // [状態確認] - グループ状態のロックを作成できること。
    ASSERT_EQ(CPLAT_OK, delegate_real_cplat_local_lock_create(&ctx.send_window_mutex));
    // [状態確認] - 送信ウィンドウのロックを作成できること。
    ASSERT_EQ(POTR_OK, potr_internal_window_init(&ctx.send_window, 0, 4, 128));
    // [状態確認] - 平文を保持する送信ウィンドウを作成できること。
    slot.ctx = &ctx;
    slot.send_window = &ctx.send_window;
    slot.send_window_mutex = ctx.send_window_mutex;
    slot.dest_addr = ctx.dest_addr;
    potr_internal_packet_session_hdr identity = {};
    potr_packet ping = {};
    ASSERT_EQ(POTR_OK, potr_internal_packet_build_ping(&ping, &identity, 1, health, sizeof(health)));
    // [状態確認] - DATA の次の通番を持つ PING を作成できること。
    ON_CALL(mock_cplat, cplat_clock_get_monotonic(_)).WillByDefault([&](cplat_timespec *out) {
        out->tv_sec = (time_t)(monotonic / 1000000000U);
        out->tv_nsec = (int64_t)(monotonic % 1000000000U);
        monotonic += 100;
        return CPLAT_OK;
    }); // [状態] - 各送出の単調増加時計を進める。
    ON_CALL(mock_cplat, cplat_clock_get_realtime(_)).WillByDefault([&](cplat_timespec *out) {
        out->tv_sec = seconds++;
        out->tv_nsec = 20;
        return CPLAT_OK;
    }); // [状態] - 各送出の実時刻を進める。
    potr_internal_tx_session_init(&ctx.tx_session);
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_crypto_encrypt(_, _, _, _, _, _, _, POTR_PACKET_HEADER_SIZE))
        .Times(10)
        .WillRepeatedly([&](uint8_t *out, size_t *len, const uint8_t *input, size_t input_len,
                            const uint8_t *key, const uint8_t *nonce, const uint8_t *aad, size_t aad_len) {
            std::array<uint8_t, POTR_CRYPTO_NONCE_SIZE> saved = {};
            std::memcpy(saved.data(), nonce, saved.size());
            nonces.insert(saved);
            return delegate_real_cplat_crypto_encrypt(out, len, input, input_len, key, nonce, aad, aad_len);
        });
    // [Pre-Assert確認_正常系] - 各経路への送出に対応して暗号化を 10 回呼ぶこと。
    // [Pre-Assert手順] - nonce を収集し暗号化へ委譲する。
    EXPECT_CALL(mock_porter, potr_internal_udp_sendto(_, _, _, _, _, _)).Times(10)
        .WillRepeatedly([&](potr_context *, int, const void *wire, size_t len, const cplat_ipv4_endpoint *, cplat_error *) {
            const auto *bytes = static_cast<const uint8_t *>(wire);
            emitted.emplace_back(bytes, bytes + len);
            return POTR_OK;
        });
    // [Pre-Assert確認_正常系] - DATA 2 回、PING 4 回、再送 4 回の計 10 回送出すること。
    // [Pre-Assert手順] - ワイヤーを保存して送信成功を返す。
    // Act
    int actual_ret = test_flush_packed(&ctx, 2);
    test_slot_send_packet(&slot, &ping);
    test_slot_send_packet(&slot, &ping);
    test_slot_retransmit(&slot, 0);
    test_slot_retransmit(&slot, 0);
    // [手順] - DATA、PING 2 回、同じ DATA の再送 2 回をそれぞれ 2 経路へ送出する。
    potr_packet retained = {};
    ASSERT_EQ(POTR_OK, potr_internal_window_send_get(&ctx.send_window, 0, &retained));
    // [状態確認] - 保持された DATA を取り出せること。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - DATA の送出が成功すること。
    EXPECT_EQ(10U, nonces.size()); // [確認_正常系] - すべての nonce が異なること。
    ASSERT_EQ(10U, emitted.size()); // [確認_正常系] - 全経路への送出を捕捉できること。
    EXPECT_EQ(2U, cplat_ntoh16(retained.payload_len)); // [確認_正常系] - ウィンドウは認証タグを保持しないこと。
    EXPECT_EQ(POTR_FLAG_DATA, cplat_ntoh16(retained.flags)); // [確認_正常系] - ウィンドウは暗号化フラグを保持しないこと。
    EXPECT_EQ(0, std::memcmp(retained.payload, packed + POTR_PACKET_HEADER_SIZE, 2));
    // [確認_正常系] - 再送用の平文が元の DATA と一致すること。
    for (size_t i = 0; i < emitted.size(); i++)
    {
        potr_packet packet = {};
        ASSERT_EQ(POTR_OK, potr_internal_packet_parse(&packet, emitted[i].data(), emitted[i].size()));
        // [確認_正常系 回数=10] - 各送出ワイヤーが解析可能であること。
        EXPECT_EQ(i, packet.tx_counter); // [確認_正常系 回数=10] - 経路複製と再送でも採番すること。
        EXPECT_EQ(10 + static_cast<int64_t>(i), packet.tx_tv_sec); // [確認_正常系 回数=10] - 実時刻を取得し直すこと。
        EXPECT_EQ((i + 1) * 100U, packet.tx_mono_ns); // [確認_正常系 回数=10] - 経過時間を取得し直すこと。
        uint32_t expected_sequence = 0;
        if (i >= 2 && i < 6)
        {
            expected_sequence = 1;
        }
        EXPECT_EQ(expected_sequence, packet.seq_num); // [確認_正常系 回数=10] - PING の next_seq と再送の DATA 通番を保持すること。
    }
    // Cleanup
    potr_internal_window_dispose(&ctx.send_window);
    delegate_real_cplat_local_lock_dispose(ctx.send_window_mutex);
    delegate_real_cplat_local_lock_dispose(ctx.callback_mutex);
}

// TCP の封緘から書き込みまでを経路ごとの送信ロック内で行うことの確認
TEST(packetEmissionTest, tcp_locks_cover_sealing_and_writing_on_each_path)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint8_t packed[256] = {};
    uint8_t sealed[256] = {};
    std::set<cplat_local_lock *> locked;
    std::vector<uint64_t> counters;
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.service.encrypt_enabled = 1;
    ctx.global.max_payload = 128;
    ctx.n_path = 2;
    ctx.send_wire_buf = packed;
    ctx.crypto_buf = sealed;
    ctx.crypto_buf_size = sizeof(sealed);
    ctx.tcp_conn_fd[0] = static_cast<cplat_socket>(7);
    ctx.tcp_conn_fd[1] = static_cast<cplat_socket>(8);
    ASSERT_EQ(POTR_OK, potr_internal_window_init(&ctx.send_window, 0, 4, 128));
    // [状態確認] - TCP の保持ウィンドウを作成できること。
    cplat_atomic_store_i32(&ctx.tcp_active_paths, 2, CPLAT_MEMORY_ORDER_RELAXED);
    ASSERT_EQ(CPLAT_OK, delegate_real_cplat_local_lock_create(&ctx.callback_mutex));
    // [状態確認] - グループ状態のロックを作成できること。
    ASSERT_EQ(CPLAT_OK, delegate_real_cplat_local_lock_create(&ctx.send_window_mutex));
    // [状態確認] - 送信ウィンドウのロックを作成できること。
    for (size_t i = 0; i < 2; i++)
    {
        ASSERT_EQ(CPLAT_OK, delegate_real_cplat_local_lock_create(&ctx.tcp_send_mutex[i]));
        // [状態確認] - 経路ごとのロックを作成できること。
    }
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault([&](cplat_local_lock *lock) {
        delegate_real_cplat_local_lock_lock(lock);
        locked.insert(lock);
    }); // [状態] - ロック保持を記録する。
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault([&](cplat_local_lock *lock) {
        locked.erase(lock);
        delegate_real_cplat_local_lock_unlock(lock);
    }); // [状態] - ロック解除を記録する。
    ON_CALL(mock_porter, potr_internal_tcp_check_writable(_, _))
        .WillByDefault(Return(POTR_OK));
    // [状態] - 2 経路とも書き込み可能とする。
    potr_internal_tx_session_init(&ctx.tx_session);
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_crypto_encrypt(_, _, _, _, _, _, _, _)).Times(8)
        .WillRepeatedly([&](uint8_t *out, size_t *len, const uint8_t *input, size_t input_len,
                            const uint8_t *key, const uint8_t *nonce, const uint8_t *aad, size_t aad_len) {
            EXPECT_TRUE(locked.count(ctx.tcp_send_mutex[0]) || locked.count(ctx.tcp_send_mutex[1]));
            // [Pre-Assert確認_正常系 回数=8] - 暗号化時に送信経路のロックを保持すること。
            return delegate_real_cplat_crypto_encrypt(out, len, input, input_len, key, nonce, aad, aad_len);
        });
    // [Pre-Assert確認_正常系] - PING、DATA、FIN、FIN_ACK をそれぞれ 2 経路で暗号化すること。
    // [Pre-Assert手順] - ロック保持を確認して暗号化へ委譲する。
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_all_locked(_, _, _, _, _, _)).Times(8)
        .WillRepeatedly([&](potr_context *, int path_idx, const void *wire, size_t len, size_t *, cplat_error *) {
            size_t path = static_cast<size_t>(path_idx);
            EXPECT_EQ(1U, locked.count(ctx.tcp_send_mutex[path]));
            // [Pre-Assert確認_正常系 回数=8] - 書き込み時に同じ経路のロックを保持すること。
            potr_packet packet = {};
            EXPECT_EQ(POTR_OK, potr_internal_packet_parse(&packet, wire, len));
            // [Pre-Assert確認_正常系 回数=8] - 各書き込みのヘッダーを解析できること。
            counters.push_back(packet.tx_counter);
            return POTR_OK;
        });
    // [Pre-Assert確認_正常系] - 各封緘の直後に TCP へ計 8 回書き込むこと。
    // [Pre-Assert手順] - ワイヤーの送出通番を収集する。
    // Act
    int actual_ret_data = test_flush_packed(&ctx, 2);
    potr_packet control = {};
    potr_internal_packet_session_hdr identity = {};
    ASSERT_EQ(POTR_OK, potr_internal_packet_build_fin(&control, &identity));
    // [状態確認] - FIN を構築できること。
    int actual_ret_fin = potr_internal_tcp_send_control_packet(&ctx, &control);
    ASSERT_EQ(POTR_OK, potr_internal_packet_build_fin_ack(&control, &identity, 1));
    // [状態確認] - FIN_ACK を構築できること。
    int actual_ret_ack = potr_internal_tcp_send_control_packet(&ctx, &control);
    // [手順] - PING が先行する DATA、FIN、FIN_ACK を各経路へ書き込む。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_data); // [確認_正常系] - DATA の送出が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_fin); // [確認_正常系] - FIN の送出が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_ack); // [確認_正常系] - FIN_ACK の送出が成功すること。
    ASSERT_EQ(8U, counters.size()); // [確認_正常系] - 8 回の書き込みを捕捉すること。
    for (size_t i = 0; i < counters.size(); i++)
    {
        EXPECT_EQ(i, counters[i]); // [確認_正常系 回数=8] - 経路内で採番順と書き込み順が一致すること。
    }
    EXPECT_TRUE(locked.empty()); // [確認_正常系] - 送出終了時にロックを解除すること。
    // Cleanup
    potr_internal_window_dispose(&ctx.send_window);
    for (size_t i = 0; i < 2; i++)
    {
        delegate_real_cplat_local_lock_dispose(ctx.tcp_send_mutex[i]);
    }
    delegate_real_cplat_local_lock_dispose(ctx.send_window_mutex);
    delegate_real_cplat_local_lock_dispose(ctx.callback_mutex);
}

// 認証後のリプレイ破棄で経路と受信ウィンドウとセッションを変更しないことの確認
TEST(packetEmissionTest, replayed_tcp_ping_does_not_change_receive_state)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    thread_recv_slot slot = {};
    slot.ctx = &ctx;
    ctx.service.encrypt_enabled = 1;
    ctx.role = POTR_ROLE_SENDER;
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.recv_window.base_seq = 17;
    ctx.recv_window.next_seq = 19;
    ctx.peer_session_known = 1;
    ctx.peer_session_id = 7;
    ctx.path_logical_alive[0] = 1;
    cplat_atomic_store_u8(&ctx.path_ping_state[0], POTR_PING_STATE_UNDEFINED, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_u64(&ctx.tcp_last_ping_recv_ms[0], 123, CPLAT_MEMORY_ORDER_RELAXED);
    uint8_t key[POTR_CRYPTO_KEY_SIZE] = {};
    uint8_t plaintext[64] = {};
    ctx.recv_crypto_buf = plaintext;
    ctx.crypto_buf_size = sizeof(plaintext);
    uint8_t health[POTR_MAX_PATH] = {};
    uint8_t wire[128] = {};
    size_t wire_len = sizeof(wire);
    potr_packet packet = {};
    potr_internal_tx_session sender = {};
    potr_internal_packet_session_hdr identity = {};
    ASSERT_EQ(POTR_OK, potr_internal_packet_build_ping(&packet, &identity, 55, health, sizeof(health)));
    // [状態確認] - リプレイする PING を構築できること。
    ASSERT_EQ(POTR_OK, potr_internal_packet_seal(&sender, &packet, key, wire, &wire_len));
    // [状態確認] - PING の暗号化が成功すること。
    ASSERT_EQ(POTR_OK, potr_internal_packet_parse(&packet, wire, wire_len));
    // [状態確認] - 認証済み受信記録の準備に使うヘッダーを解析できること。
    ASSERT_EQ(POTR_OK, potr_internal_replay_window_accept(&ctx.replay_window, &packet));
    // [状態確認] - 同じ送出通番を受信済みとして記録できること。
    potr_internal_replay_window before = ctx.replay_window;
    // Pre-Assert
    EXPECT_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).Times(0);
    // [Pre-Assert確認_異常系] - 経路の PING 状態を更新しないこと。
    EXPECT_CALL(mock_porter, thread_recv_session_adopt(_, _)).Times(0);
    // [Pre-Assert確認_異常系] - セッション採用処理を行わないこと。
    EXPECT_CALL(mock_porter, potr_internal_window_recv_push(_, _)).Times(0);
    // [Pre-Assert確認_異常系] - 受信ウィンドウへ投入しないこと。
    EXPECT_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).Times(0);
    // [Pre-Assert確認_異常系] - 経路の最終受信時刻を取得しないこと。
    // Act
    int actual_ret = test_tcp_handle_packet(&ctx, &slot, wire, static_cast<uint16_t>(wire_len - POTR_PACKET_HEADER_SIZE));
    // [手順] - 受信済みの認証付き PING を再受信する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - ストリームを切断せず破棄すること。
    EXPECT_EQ(17U, ctx.recv_window.base_seq); // [確認_異常系] - ウィンドウの先頭を変更しないこと。
    EXPECT_EQ(19U, ctx.recv_window.next_seq); // [確認_異常系] - 受信通番を変更しないこと。
    EXPECT_EQ(1, ctx.peer_session_known); // [確認_異常系] - セッション追跡状態を変更しないこと。
    EXPECT_EQ(7U, ctx.peer_session_id); // [確認_異常系] - 相手セッション識別子を変更しないこと。
    EXPECT_EQ(1, ctx.path_logical_alive[0]); // [確認_異常系] - 論理経路状態を変更しないこと。
    EXPECT_EQ(POTR_PING_STATE_UNDEFINED, cplat_atomic_load_u8(&ctx.path_ping_state[0], CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_異常系] - 経路の PING 受信状態を変更しないこと。
    EXPECT_EQ(123U, cplat_atomic_load_u64(&ctx.tcp_last_ping_recv_ms[0], CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_異常系] - 最終受信時刻を変更しないこと。
    EXPECT_EQ(0, std::memcmp(&before, &ctx.replay_window, sizeof(before))); // [確認_異常系] - リプレイ窓を変更しないこと。
}

// 片方向 UDP の複数返送元の通番を独立して記録することの確認
TEST(packetEmissionTest, one_way_udp_replay_records_are_per_return_session)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    potr_packet packet = {};
    uint8_t wire[POTR_PACKET_HEADER_SIZE] = {};
    uint16_t encrypted = cplat_hton16(POTR_FLAG_ENCRYPTED);
    std::memcpy(wire + 32, &encrypted, sizeof(encrypted));
    ctx.role = POTR_ROLE_SENDER;
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.service.max_peers = 2;
    ASSERT_EQ(CPLAT_OK, delegate_real_cplat_local_lock_create(&ctx.callback_mutex));
    // [状態確認] - 受信者表のロックを作成できること。
    packet.flags = POTR_FLAG_NACK;
    int actual_ret[4] = {};
    // Pre-Assert
    // Act
    packet.session_id = 2;
    actual_ret[0] = test_check_service_replay(&ctx, &packet, wire);
    packet.session_id = 1;
    actual_ret[1] = test_check_service_replay(&ctx, &packet, wire);
    actual_ret[2] = test_check_service_replay(&ctx, &packet, wire);
    packet.session_id = 2;
    actual_ret[3] = test_check_service_replay(&ctx, &packet, wire);
    // [手順] - セッション識別子の異なる返送元から同じ通番を受信し、各返送元で再受信する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret[0]); // [確認_正常系] - 最初の返送元を受理すること。
    EXPECT_EQ(POTR_OK, actual_ret[1]); // [確認_正常系] - 小さい識別子の別返送元も受理すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret[2]); // [確認_異常系] - 同じ返送元の重複を破棄すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret[3]); // [確認_異常系] - 先の返送元の重複も破棄すること。
    // Cleanup
    while (ctx.receivers != nullptr)
    {
        potr_internal_receiver *source = ctx.receivers;
        ctx.receivers = source->next;
        cplat_free(source);
    }
    delegate_real_cplat_local_lock_dispose(ctx.callback_mutex);
}

// DATA の優先グループが変わっても PING と FIN は全経路へ送ることの確認
TEST(packetEmissionTest, selected_data_paths_and_all_control_paths)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    thread_recv_slot slot = {};
    uint8_t packed[256] = {};
    uint8_t sealed[256] = {};
    uint8_t received[256] = {};
    std::vector<cplat_socket> data_paths;
    std::vector<cplat_socket> control_paths;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.service.path_group[0] = 1;
    ctx.service.path_group[1] = 3;
    ctx.health_interval_ms = 10;
    ctx.health_timeout_ms = 100;
    ctx.global.max_payload = 128;
    ctx.n_path = 2;
    ctx.sock[0] = static_cast<cplat_socket>(7);
    ctx.sock[1] = static_cast<cplat_socket>(8);
    ctx.send_wire_buf = packed;
    ctx.crypto_buf = sealed;
    ctx.crypto_buf_size = sizeof(sealed);
    ctx.recv_buf = received;
    slot.ctx = &ctx;
    slot.dest_addr = ctx.dest_addr;
    slot.send_window = &ctx.send_window;
    slot.session_id = &ctx.session_id;
    slot.session_ts = &ctx.session_ts;
    ctx.recv_crypto_buf = sealed;
    ASSERT_EQ(CPLAT_OK, delegate_real_cplat_local_lock_create(&ctx.callback_mutex));
    // [状態確認] - グループ状態のロックを作成できること。
    ASSERT_EQ(CPLAT_OK, delegate_real_cplat_local_lock_create(&ctx.send_window_mutex));
    // [状態確認] - ウィンドウのロックを作成できること。
    slot.send_window_mutex = ctx.send_window_mutex;
    ASSERT_EQ(POTR_OK, potr_internal_window_init(&ctx.send_window, 0, 4, 128));
    // [状態確認] - 送信ウィンドウを初期化できること。
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault(DoAll(SetArgPointee<0>(1000), Return(CPLAT_OK)));
    ctx.started_ms = 1000;
    potr_internal_tx_session_init(&ctx.tx_session);
    potr_packet ping = {};
    potr_packet fin = {};
    potr_internal_packet_session_hdr identity = {};
    ASSERT_EQ(POTR_OK, potr_internal_packet_build_ping(&ping, &identity, 0, nullptr, 0));
    // [状態確認] - PING を構築できること。
    ASSERT_EQ(POTR_OK, potr_internal_packet_build_fin(&fin, &identity));
    // [状態確認] - FIN を構築できること。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_udp_sendto(_, _, _, _, _, _)).Times(7)
        .WillRepeatedly([&](potr_context *c, int path_idx, const void *wire, size_t len,
                            const cplat_ipv4_endpoint *, cplat_error *) {
            potr_packet packet = {};
            if (potr_internal_packet_parse(&packet, wire, len) == POTR_OK)
            {
                if (packet.flags == POTR_FLAG_DATA)
                {
                    data_paths.push_back(c->sock[path_idx]);
                }
                else
                {
                    control_paths.push_back(c->sock[path_idx]);
                }
            }
            return POTR_OK;
        });
    // [Pre-Assert確認_正常系] - DATA と再送・REJECT 各 1 経路、PING と FIN 各 2 経路の計 7 回送ること。
    // [Pre-Assert手順] - 種別ごとの送出経路を記録する。
    // Act
    int actual_ret = test_flush_packed(&ctx, 2);
    test_slot_retransmit(&slot, 0);
    test_slot_retransmit(&slot, 99);
    test_slot_send_packet(&slot, &ping);
    test_slot_send_packet(&slot, &fin);
    // [手順] - 起動直後の DATA、再送、保持範囲外の REJECT、全経路対象の PING・FIN を送る。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - DATA の送信が成功すること。
    EXPECT_EQ((std::vector<cplat_socket>{7, 7}), data_paths); // [確認_正常系] - DATA と再送は最優先グループだけに送ること。
    EXPECT_EQ((std::vector<cplat_socket>{7, 7, 8, 7, 8}), control_paths);
    // [確認_正常系] - REJECT は選択中だけへ送り、PING と FIN は待機グループにも送ること。
    // Cleanup
    potr_internal_window_dispose(&ctx.send_window);
    delegate_real_cplat_local_lock_dispose(ctx.send_window_mutex);
    delegate_real_cplat_local_lock_dispose(ctx.callback_mutex);
}

// 待機グループの PING を他経路の DATA が抑制しないことの確認
TEST(packetEmissionTest, standby_path_ping_is_not_suppressed_by_selected_path_data)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    std::vector<cplat_socket> emitted;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.health_interval_ms = 100;
    ctx.n_path = 2;
    ctx.sock[0] = static_cast<cplat_socket>(7);
    ctx.sock[1] = static_cast<cplat_socket>(8);
    cplat_atomic_store_i32(&ctx.health_running[0], 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_u64(&ctx.last_valid_data_send_ms[0], 1000, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_u64(&ctx.last_ping_send_ms[1], 800, CPLAT_MEMORY_ORDER_RELAXED);
    ASSERT_EQ(CPLAT_OK, delegate_real_cplat_local_lock_create(&ctx.send_window_mutex));
    // [状態確認] - ヘルス スレッドが参照するロックを作成できること。
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault(DoAll(SetArgPointee<0>(1000), Return(CPLAT_OK)));
    potr_internal_tx_session_init(&ctx.tx_session);
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_udp_sendto(_, _, _, _, _, _)).Times(1)
        .WillOnce([&](potr_context *c, int path_idx, const void *, size_t, const cplat_ipv4_endpoint *, cplat_error *) {
            emitted.push_back(c->sock[path_idx]);
            cplat_atomic_store_i32(&ctx.health_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
            return POTR_OK;
        });
    // [Pre-Assert確認_正常系] - 待機グループに PING を 1 回送ること。
    // [Pre-Assert手順] - 送出経路を記録し、ヘルス スレッドを停止する。
    // Act
    test_health_thread_run(&ctx);
    // [手順] - 経路 0 が DATA を送った直後に経路 1 の PING 期限を迎える。
    // Assert
    EXPECT_EQ((std::vector<cplat_socket>{8}), emitted); // [確認_正常系] - 待機経路だけに PING を送ること。
    EXPECT_EQ(1000U, cplat_atomic_load_u64(&ctx.last_ping_send_ms[1], CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_正常系] - 送った経路だけの PING 時刻を更新すること。
    // Cleanup
    delegate_real_cplat_local_lock_dispose(ctx.send_window_mutex);
}

// TCP 切り替え時に保持範囲内の DATA を新グループへ通番順に送り直すことの確認
TEST(packetEmissionTest, tcp_failover_replays_retained_window_before_new_data)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    uint8_t packed[256] = {};
    uint8_t sealed[256] = {};
    std::vector<uint32_t> replayed;
    std::vector<cplat_socket> emitted_paths;
    int actual_ret_initial[5] = {};
    ctx.service.type = POTR_TYPE_TCP;
    ctx.service.path_group[0] = 1;
    ctx.service.path_group[1] = 2;
    ctx.health_interval_ms = 10;
    ctx.health_timeout_ms = 100;
    ctx.n_path = 2;
    ctx.global.max_payload = 128;
    ctx.send_wire_buf = packed;
    ctx.crypto_buf = sealed;
    ctx.crypto_buf_size = sizeof(sealed);
    ctx.tcp_conn_fd[0] = static_cast<cplat_socket>(7);
    ctx.tcp_conn_fd[1] = static_cast<cplat_socket>(8);
    ctx.path_logical_alive[0] = ctx.path_logical_alive[1] = 1;
    cplat_atomic_store_i32(&ctx.tcp_active_paths, 2, CPLAT_MEMORY_ORDER_RELAXED);
    ASSERT_EQ(CPLAT_OK, delegate_real_cplat_local_lock_create(&ctx.callback_mutex));
    // [状態確認] - グループ状態のロックを作成できること。
    ASSERT_EQ(CPLAT_OK, delegate_real_cplat_local_lock_create(&ctx.send_window_mutex));
    // [状態確認] - 送信ウィンドウのロックを作成できること。
    for (size_t i = 0; i < 2; i++)
    {
        ASSERT_EQ(CPLAT_OK, delegate_real_cplat_local_lock_create(&ctx.tcp_send_mutex[i]));
        // [状態確認 回数=2] - TCP 経路ごとのロックを作成できること。
    }
    ASSERT_EQ(POTR_OK, potr_internal_window_init(&ctx.send_window, 0, 3, 128));
    // [状態確認] - 直近 3 通番のウィンドウを用意できること。
    ON_CALL(mock_porter, potr_internal_tcp_check_writable(_, _))
        .WillByDefault(Return(POTR_OK));
    potr_internal_tx_session_init(&ctx.tx_session);
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_all_locked(_, _, _, _, _, _)).Times(11)
        .WillRepeatedly([&](potr_context *c, int path_idx, const void *wire, size_t len, size_t *, cplat_error *) {
            cplat_socket fd = c->tcp_conn_fd[path_idx];
            potr_packet packet = {};
            if (potr_internal_packet_parse(&packet, wire, len) == POTR_OK && packet.flags == POTR_FLAG_DATA)
            {
                emitted_paths.push_back(fd);
                if (fd == static_cast<cplat_socket>(8))
                {
                    replayed.push_back(packet.seq_num);
                }
            }
            return POTR_OK;
        });
    // [Pre-Assert確認_正常系] - 旧経路の PING と DATA 5 件、新経路の PING と DATA 4 件の計 11 回書くこと。
    // [Pre-Assert手順] - DATA の通番と経路を保存する。
    // Act
    for (size_t i = 0; i < 5; i++)
    {
        actual_ret_initial[i] = test_flush_packed(&ctx, 2);
    }
    ctx.path_logical_alive[0] = 0;
    int actual_ret_switch = test_flush_packed(&ctx, 2);
    // [手順] - 5 通番を送った後に旧グループを不達にして次の DATA を送る。
    // Assert
    for (size_t i = 0; i < 5; i++)
    {
        EXPECT_EQ(POTR_OK, actual_ret_initial[i]); // [確認_正常系 回数=5] - 旧グループへの送信が成功すること。
    }
    EXPECT_EQ(POTR_OK, actual_ret_switch); // [確認_正常系] - 切り替え後の送信が成功すること。
    EXPECT_EQ((std::vector<uint32_t>{2, 3, 4, 5}), replayed);
    // [確認_正常系] - 直近 3 通番を古い順に送り直してから新しい通番を書くこと。
    EXPECT_EQ((std::vector<cplat_socket>{7, 7, 7, 7, 7, 8, 8, 8, 8}), emitted_paths);
    // [確認_正常系] - DATA を選択したグループだけに送ること。
    EXPECT_EQ(6U, ctx.send_window.next_seq); // [確認_正常系] - 送り直しでは DATA 通番を消費しないこと。
    // Cleanup
    potr_internal_window_dispose(&ctx.send_window);
    delegate_real_cplat_local_lock_dispose(ctx.send_window_mutex);
    delegate_real_cplat_local_lock_dispose(ctx.callback_mutex);
    for (size_t i = 0; i < 2; i++)
    {
        delegate_real_cplat_local_lock_dispose(ctx.tcp_send_mutex[i]);
    }
}

// 相乗りの受理が、採用している相手のセッションの DATA だけを対象にし、旧セッションの DATA を無視することの確認
TEST(packetEmissionTest, piggyback_dispatch_accepts_only_adopted_session_data)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    thread_recv_slot slot = {};
    cplat_ipv4_endpoint sender = {};
    potr_packet stale_pkt = {};
    potr_packet adopted_pkt = {};
    uint8_t payload[8] = {};
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    ctx.global.window_size = 16U;
    ctx.global.max_payload = 128U;
    ctx.peer_session_known = 1;
    ctx.peer_session_id = 200U;
    ctx.peer_session_ts.tv_sec = 500;
    ctx.peer_session_ts.tv_nsec = 100;
    ASSERT_EQ(POTR_OK, potr_internal_window_init(&ctx.recv_window, 0U, 16U, 128U));
    // [状態確認] - 採用中のセッション (ts=500.100, id=200) と受信ウィンドウを用意できること。
    slot.ctx = &ctx;
    slot.peer = nullptr;
    slot.peer_id = POTR_PEER_NA;
    slot.peer_session_known = &ctx.peer_session_known;
    slot.peer_session_id = &ctx.peer_session_id;
    slot.peer_session_ts = &ctx.peer_session_ts;
    slot.recv_window = &ctx.recv_window;
    slot.dest_addr = ctx.dest_addr;
    slot.reorder_pending = &ctx.reorder_pending;
    slot.reorder_nack_num = &ctx.reorder_nack_num;
    slot.reorder_deadline_ts = &ctx.reorder_deadline_ts;
    slot.pending_fin = &ctx.pending_fin;
    slot.fin_target_seq = &ctx.fin_target_seq;
    slot.frag_buf = ctx.frag_buf;
    slot.frag_buf_len = &ctx.frag_buf_len;
    slot.frag_count = &ctx.frag_count;
    slot.frag_compressed = &ctx.frag_compressed;
    slot.path_ping_state = ctx.path_ping_state;
    slot.remote_path_ping_state = ctx.remote_path_ping_state;
    slot.last_recv_ts = &ctx.last_recv_ts;
    slot.path_last_recv_ts = ctx.path_last_recv_ts;
    stale_pkt.flags = (uint16_t)(POTR_FLAG_DATA | POTR_FLAG_SEND_LIMIT);
    stale_pkt.session_tv_sec = 400;
    stale_pkt.session_tv_nsec = 0;
    stale_pkt.session_id = 100U;
    stale_pkt.seq_num = 0U;
    stale_pkt.ack_num = 999U;
    stale_pkt.payload = payload;
    stale_pkt.payload_len = (uint16_t)sizeof(payload);
    adopted_pkt = stale_pkt;
    adopted_pkt.session_tv_sec = 500;
    adopted_pkt.session_tv_nsec = 100;
    adopted_pkt.session_id = 200U;
    adopted_pkt.ack_num = 77U;
    // [状態] - 旧セッションの DATA (ack_num=999) と採用中セッションの DATA (ack_num=77) を用意する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_on_peer_packet(&ctx, nullptr, &stale_pkt)).Times(0);
    // [Pre-Assert確認_正常系] - 旧セッションの DATA では相乗り受理処理を呼び出さないこと。
    EXPECT_CALL(mock_porter, potr_internal_flow_control_on_peer_packet(&ctx, nullptr, &adopted_pkt)).Times(1);
    // [Pre-Assert確認_正常系] - 採用中セッションの DATA だけで相乗り受理処理を呼び出すこと。
    // Act
    int ret_stale = thread_recv_dispatch_packet(&slot, &stale_pkt, 0, &sender);
    int ret_adopted = thread_recv_dispatch_packet(&slot, &adopted_pkt, 0, &sender);
    // [手順] - 旧セッションと採用中セッションの相乗り DATA を順に振り分ける。
    // Assert
    EXPECT_EQ(0, ret_stale);   // [確認_正常系] - 旧セッションの DATA の振り分けが正常終了すること。
    EXPECT_EQ(0, ret_adopted); // [確認_正常系] - 採用中セッションの DATA の振り分けが正常終了すること。
    // Cleanup
    potr_internal_window_dispose(&ctx.recv_window);
}

// 新しい通信として採用した相手から DATA より先に PING を受理した時点で、最初の送出上限通知処理を呼び出すことの確認
TEST(packetEmissionTest, ping_adoption_triggers_initial_flow_control_notification)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    thread_recv_slot slot = {};
    cplat_ipv4_endpoint sender = {};
    potr_packet ping_pkt = {};
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.global.window_size = 64U;
    ctx.global.max_payload = 128U;
    ctx.peer_session_known = 0;
    ASSERT_EQ(POTR_OK, potr_internal_window_init(&ctx.recv_window, 0U, 64U, 128U));
    // [状態確認] - 未採用状態 (peer_session_known=0) と受信ウィンドウを用意できること。
    slot.ctx = &ctx;
    slot.peer = nullptr;
    slot.peer_id = POTR_PEER_NA;
    slot.peer_session_known = &ctx.peer_session_known;
    slot.peer_session_id = &ctx.peer_session_id;
    slot.peer_session_ts = &ctx.peer_session_ts;
    slot.recv_window = &ctx.recv_window;
    slot.dest_addr = ctx.dest_addr;
    slot.reorder_pending = &ctx.reorder_pending;
    slot.reorder_nack_num = &ctx.reorder_nack_num;
    slot.reorder_deadline_ts = &ctx.reorder_deadline_ts;
    slot.pending_fin = &ctx.pending_fin;
    slot.fin_target_seq = &ctx.fin_target_seq;
    slot.frag_buf = ctx.frag_buf;
    slot.frag_buf_len = &ctx.frag_buf_len;
    slot.frag_count = &ctx.frag_count;
    slot.frag_compressed = &ctx.frag_compressed;
    slot.path_ping_state = ctx.path_ping_state;
    slot.remote_path_ping_state = ctx.remote_path_ping_state;
    slot.last_recv_ts = &ctx.last_recv_ts;
    slot.path_last_recv_ts = ctx.path_last_recv_ts;
    ping_pkt.flags = POTR_FLAG_PING;
    ping_pkt.session_tv_sec = 600;
    ping_pkt.session_tv_nsec = 200;
    ping_pkt.session_id = 300U;
    ping_pkt.seq_num = 0U;
    ON_CALL(mock_cplat, cplat_clock_get_monotonic(_)).WillByDefault([](cplat_timespec *out) {
        out->tv_sec = 10;
        out->tv_nsec = 20;
        return CPLAT_OK;
    });
    ON_CALL(mock_porter, potr_internal_flow_control_refused(_, _, _, _)).WillByDefault(Return(0));
    // [状態] - 未採用の相手 (ts=600.200, id=300) から DATA より先に届く PING パケットを用意する。
    // Pre-Assert
    EXPECT_CALL(mock_porter,
                potr_internal_flow_control_on_received(&ctx, POTR_PEER_NA, 300U, &ctx.peer_session_ts, 0U, 0, 0U))
        .Times(2);
    // [Pre-Assert確認_正常系] - 2 回の PING で data_packet=0 の送出上限通知処理を行うこと。
    // Act
    int actual_ret = thread_recv_dispatch_packet(&slot, &ping_pkt, 0, &sender);
    // [手順] - DATA より先に PING を受信ディスパッチへ渡す。
    // Assert
    EXPECT_EQ(1, actual_ret);             // [確認_正常系] - PING で経路状態が変わったことを返す。
    EXPECT_EQ(1, ctx.peer_session_known); // [確認_正常系] - 新しい相手のセッションが採用されること。
    EXPECT_EQ(300U, ctx.peer_session_id); // [確認_正常系] - 採用したセッション識別子が 300 であること。
    EXPECT_EQ(POTR_PING_STATE_NORMAL,
              cplat_atomic_load_u8(&ctx.path_ping_state[0], CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_正常系] - PING 受信で path_ping_state[0] が NORMAL になること。
    ON_CALL(mock_cplat, cplat_clock_get_monotonic(_)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    const int fatal_ret = thread_recv_dispatch_packet(&slot, &ping_pkt, 0, &sender);
    // [手順] - 単調時計の失敗を発生させて、致命的エラー後に同じ PING を振り分ける。
    EXPECT_EQ(0, fatal_ret); // [確認_異常系] - 致命的な PING 処理失敗を真偽値の 0 として返す。
    EXPECT_EQ(POTR_ERR_IO, cplat_atomic_load_i32(&ctx.fatal_error, CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_異常系] - 致命的な失敗を ctx.fatal_error へ設定する。
    // Cleanup
    potr_internal_window_dispose(&ctx.recv_window);
}

// 双方向 UDP の起床ヘルパーが両通信種別だけを対象にすることの確認
TEST(packetEmissionTest, udp_interrupt_ping_wake_targets_both_bidirectional_types)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    // Pre-Assert
    ASSERT_EQ(CPLAT_OK, delegate_real_cplat_local_lock_create(&ctx.health_mutex[0]));
    ASSERT_EQ(CPLAT_OK, delegate_real_cplat_condvar_create(&ctx.health_wakeup[0]));
    cplat_atomic_store_i32(&ctx.health_running[0], 1, CPLAT_MEMORY_ORDER_RELEASE);
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    EXPECT_CALL(mock_cplat, cplat_condvar_signal(ctx.health_wakeup[0])).Times(2);
    // [Pre-Assert確認_正常系] - UDP 双方向 2 種別だけで health thread の条件変数へ通知すること。
    // Act
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    thread_recv_wake_udp_interrupt_ping(&ctx, 1);
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    thread_recv_wake_udp_interrupt_ping(&ctx, 1);
    ctx.service.type = POTR_TYPE_UNICAST;
    thread_recv_wake_udp_interrupt_ping(&ctx, 1);
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    thread_recv_wake_udp_interrupt_ping(&ctx, 1);
    thread_recv_wake_udp_interrupt_ping(&ctx, 0);
    // [手順] - 対象の 2 種別、対象外の種別、状態変化なしでヘルパーを呼ぶ。
    // Assert
    SUCCEED();
    delegate_real_cplat_condvar_dispose(ctx.health_wakeup[0]);
    delegate_real_cplat_local_lock_dispose(ctx.health_mutex[0]);
}
