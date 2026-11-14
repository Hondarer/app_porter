#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/packet_seal.h>
#include "thread_recv_validate.h"
#include <array>
#include <cstring>
#include <iterator>
#include <set>
#include <thread>
#include <vector>

using namespace testing;

// 平文の封緘と開封でペイロードとヘッダーを保持することの確認
TEST(packetSealTest, plaintext_round_trip)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_tx_session session = {};
    potr_internal_packet_session_hdr identity = {17, 23, 41, 7};
    uint8_t payload[] = {1, 2, 3, 4};
    uint8_t wire[128] = {};
    potr_packet packet = {};
    potr_packet opened = {};
    size_t wire_len = sizeof(wire);
    potr_internal_tx_session_init(&session);
    ASSERT_EQ(POTR_OK, potr_internal_packet_build_packed(&packet, &identity, 9, payload, sizeof(payload)));
    // [状態確認] - 平文 DATA の構築が成功すること。
    packet.reserved = UINT32_MAX; // [状態] - 予約フィールドを非ゼロで用意する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_crypto_encrypt(_, _, _, _, _, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 平文では暗号化を呼ばないこと。
    // Act
    int actual_ret_seal = potr_internal_packet_seal(&session, &packet, nullptr, wire, &wire_len);
    ASSERT_EQ(POTR_OK, potr_internal_packet_parse(&opened, wire, wire_len));
    // [状態確認] - 封緘結果を解析できること。
    int actual_ret_open = potr_internal_packet_open(&opened, wire, nullptr, nullptr, 0);
    // [手順] - 平文を封緘して開封する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_seal); // [確認_正常系] - 封緘が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_open); // [確認_正常系] - 開封が成功すること。
    EXPECT_EQ(POTR_PACKET_HEADER_SIZE + sizeof(payload), wire_len); // [確認_正常系] - ヘッダー長が 72 [byte]であること。
    EXPECT_EQ(0U, opened.tx_counter); // [確認_正常系] - 最初の送出通番が 0 であること。
    EXPECT_EQ(0U, opened.reserved); // [確認_正常系] - 予約フィールドが 0 であること。
    EXPECT_EQ(9U, opened.seq_num); // [確認_正常系] - DATA 通番を保持すること。
    EXPECT_EQ(0, std::memcmp(payload, opened.payload, sizeof(payload))); // [確認_正常系] - 平文が一致すること。
}

// 暗号化の封緘と開封で平文へ復元できることの確認
TEST(packetSealTest, encrypted_round_trip)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_tx_session session = {};
    potr_internal_packet_session_hdr identity = {17, 23, 41, 7};
    uint8_t payload[] = {1, 2, 3, 4};
    uint8_t key[POTR_CRYPTO_KEY_SIZE] = {};
    uint8_t wire[128] = {};
    uint8_t plaintext[32] = {};
    potr_packet packet = {};
    potr_packet opened = {};
    size_t wire_len = sizeof(wire);
    potr_internal_tx_session_init(&session);
    ASSERT_EQ(POTR_OK, potr_internal_packet_build_packed(&packet, &identity, 9, payload, sizeof(payload)));
    // [状態確認] - 暗号化対象の DATA を構築できること。
    // Pre-Assert
    // Act
    int actual_ret_seal = potr_internal_packet_seal(&session, &packet, key, wire, &wire_len);
    ASSERT_EQ(POTR_OK, potr_internal_packet_parse(&opened, wire, wire_len));
    // [状態確認] - 暗号化したワイヤーを解析できること。
    int actual_ret_open = potr_internal_packet_open(&opened, wire, key, plaintext, sizeof(plaintext));
    // [手順] - 暗号化して開封する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_seal); // [確認_正常系] - 暗号化が成功すること。
    EXPECT_EQ(POTR_OK, actual_ret_open); // [確認_正常系] - 認証と復号が成功すること。
    EXPECT_EQ(sizeof(payload), opened.payload_len); // [確認_正常系] - 認証タグを除いた長さになること。
    EXPECT_EQ(0, std::memcmp(payload, opened.payload, sizeof(payload))); // [確認_正常系] - 平文が復元されること。
    EXPECT_EQ(POTR_FLAG_DATA, opened.flags); // [確認_正常系] - 認証後に暗号化フラグを除くこと。
    EXPECT_EQ(sizeof(payload), cplat_ntoh16(packet.payload_len)); // [確認_正常系] - 封緘元は平文のままであること。
}

// 平文と暗号化の既定最大ペイロードで IPv4 UDP データグラムを 1,500 [byte] 以下に保つことの確認
TEST(packetSealTest, default_max_payload_fits_ipv4_udp_datagram)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_tx_session plaintext_session = {};
    potr_internal_tx_session encrypted_session = {};
    potr_internal_packet_session_hdr identity = {17, 23, 41, 7};
    std::vector<uint8_t> plaintext_payload(POTR_DEFAULT_MAX_PAYLOAD, 0x5aU);
    std::vector<uint8_t> encrypted_payload(POTR_DEFAULT_MAX_PAYLOAD - POTR_CRYPTO_TAG_SIZE, 0x5aU);
    uint8_t key[POTR_CRYPTO_KEY_SIZE] = {};
    std::array<uint8_t, POTR_PACKET_HEADER_SIZE + POTR_DEFAULT_MAX_PAYLOAD> plaintext_wire = {};
    std::array<uint8_t, POTR_PACKET_HEADER_SIZE + POTR_DEFAULT_MAX_PAYLOAD> encrypted_wire = {};
    potr_packet plaintext_packet = {};
    potr_packet encrypted_packet = {};
    size_t plaintext_wire_len = plaintext_wire.size();
    size_t encrypted_wire_len = encrypted_wire.size();
    potr_internal_tx_session_init(&plaintext_session);
    potr_internal_tx_session_init(&encrypted_session);
    ASSERT_EQ(POTR_OK, potr_internal_packet_build_packed(&plaintext_packet, &identity, 9,
                                                          plaintext_payload.data(), plaintext_payload.size()));
    // [状態確認] - 既定上限の平文 DATA を構築できること。
    ASSERT_EQ(POTR_OK, potr_internal_packet_build_packed(&encrypted_packet, &identity, 10,
                                                          encrypted_payload.data(), encrypted_payload.size()));
    // [状態確認] - 認証タグ分を差し引いた暗号化 DATA を構築できること。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_crypto_encrypt(_, _, _, _, _, _, _, POTR_PACKET_HEADER_SIZE))
        .WillOnce([](uint8_t *out, size_t *len, const uint8_t *in, size_t input_len,
                     const uint8_t *actual_key, const uint8_t *nonce, const uint8_t *aad, size_t aad_len) {
            return delegate_real_cplat_crypto_encrypt(out, len, in, input_len, actual_key, nonce, aad, aad_len);
        });
    // [Pre-Assert確認_正常系] - 72 [byte]の AAD を指定して暗号化が 1 回呼ばれること。
    // Act
    int plaintext_result = potr_internal_packet_seal(&plaintext_session, &plaintext_packet, nullptr,
                                                      plaintext_wire.data(), &plaintext_wire_len);
    int encrypted_result = potr_internal_packet_seal(&encrypted_session, &encrypted_packet, key,
                                                      encrypted_wire.data(), &encrypted_wire_len);
    // [手順] - 平文 1,400 [byte]と暗号化平文 1,384 [byte]を封緘する。
    // Assert
    EXPECT_EQ(POTR_OK, plaintext_result); // [確認_正常系] - 平文パケットの封緘が成功すること。
    EXPECT_EQ(POTR_PACKET_HEADER_SIZE + POTR_DEFAULT_MAX_PAYLOAD, plaintext_wire_len);
    // [確認_正常系] - ヘッダーと既定最大ペイロードの合計長になること。
    EXPECT_EQ(1472U, plaintext_wire_len); // [確認_正常系] - 平文パケットのワイヤー長が 1,472 [byte]であること。
    EXPECT_LE(plaintext_wire_len + 28U, 1500U);
    // [確認_正常系] - IPv4 と UDP のヘッダーを加えた長さが 1,500 [byte]以下であること。
    EXPECT_EQ(POTR_OK, encrypted_result); // [確認_正常系] - 暗号化パケットの封緘が成功すること。
    EXPECT_EQ(POTR_PACKET_HEADER_SIZE + (POTR_DEFAULT_MAX_PAYLOAD - POTR_CRYPTO_TAG_SIZE) + POTR_CRYPTO_TAG_SIZE,
              encrypted_wire_len);
    // [確認_正常系] - ヘッダー、暗号化平文、認証タグの合計長になること。
    EXPECT_EQ(1472U, encrypted_wire_len); // [確認_正常系] - 暗号化パケットのワイヤー長が 1,472 [byte]であること。
    EXPECT_LE(encrypted_wire_len + 28U, 1500U);
    // [確認_正常系] - IPv4 と UDP のヘッダーを加えた長さが 1,500 [byte]以下であること。
}

// PING、経路複製、再送、制御パケットのすべてで nonce を更新することの確認
TEST(packetSealTest, counters_and_nonces_are_unique_for_every_emission)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_tx_session session = {};
    potr_internal_packet_session_hdr identity = {17, 23, 41, 7};
    const uint16_t flags[] = {POTR_FLAG_PING, POTR_FLAG_PING, POTR_FLAG_DATA, POTR_FLAG_DATA,
                              POTR_FLAG_DATA, POTR_FLAG_NACK, POTR_FLAG_REJECT, POTR_FLAG_FIN, POTR_FLAG_FIN_ACK};
    uint8_t payload[] = {1, 2, 3, 4};
    uint8_t key[POTR_CRYPTO_KEY_SIZE] = {};
    std::set<std::array<uint8_t, POTR_CRYPTO_NONCE_SIZE>> nonces;
    std::vector<uint64_t> counters;
    int actual_ret[9] = {};
    potr_internal_tx_session_init(&session);
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_crypto_encrypt(_, _, _, _, _, _, _, POTR_PACKET_HEADER_SIZE))
        .Times(9)
        .WillRepeatedly([&](uint8_t *out, size_t *len, const uint8_t *in, size_t input_len,
                            const uint8_t *actual_key, const uint8_t *nonce, const uint8_t *aad, size_t aad_len) {
            std::array<uint8_t, POTR_CRYPTO_NONCE_SIZE> saved = {};
            std::memcpy(saved.data(), nonce, saved.size());
            nonces.insert(saved);
            uint64_t counter = 0;
            for (size_t i = 4; i < saved.size(); i++)
            {
                counter = (counter << 8) | saved[i];
            }
            counters.push_back(counter);
            EXPECT_EQ(0, std::memcmp(nonce, aad + 16, 4));
            // [Pre-Assert確認_正常系 回数=9] - nonce の先頭がワイヤーの session_id であること。
            EXPECT_EQ(0, std::memcmp(nonce + 4, aad + 40, 8));
            // [Pre-Assert確認_正常系 回数=9] - nonce の後半がワイヤーの tx_counter であること。
            return delegate_real_cplat_crypto_encrypt(out, len, in, input_len, actual_key, nonce, aad, aad_len);
        });
    // [Pre-Assert確認_正常系] - 72 [byte]の AAD を指定して暗号化が 9 回呼ばれること。
    // [Pre-Assert手順] - nonce を収集して暗号化処理へ委譲する。
    // Act
    for (size_t i = 0; i < 9; i++)
    {
        potr_packet packet = {};
        uint8_t wire[128] = {};
        size_t wire_len = sizeof(wire);
        ASSERT_EQ(POTR_OK, potr_internal_packet_build_ping(&packet, &identity, 42, payload, sizeof(payload)));
        // [状態確認] - 同じ next_seq を持つ送出元を構築できること。
        packet.flags = cplat_hton16(flags[i]);
        if (i >= 5)
        {
            packet.payload_len = 0;
            packet.payload = nullptr;
        }
        actual_ret[i] = potr_internal_packet_seal(&session, &packet, key, wire, &wire_len);
        // [手順] - 同じセッションで PING 2 回、DATA の経路複製と再送、各制御パケットを封緘する。
    }
    // Assert
    EXPECT_EQ(9U, nonces.size()); // [確認_正常系] - 同じ鍵で nonce が 9 個すべて異なること。
    ASSERT_EQ(9U, counters.size()); // [確認_正常系] - すべての採番を収集できること。
    for (size_t i = 0; i < 9; i++)
    {
        EXPECT_EQ(POTR_OK, actual_ret[i]); // [確認_正常系 回数=9] - 各送出の封緘が成功すること。
        EXPECT_EQ(i, counters[i]); // [確認_正常系 回数=9] - 送出ごとに通番が 1 増えること。
    }
}

// 経路複製と再送の封緘ごとに送信時刻を取り直すことの確認
TEST(packetSealTest, clocks_are_sampled_for_each_path_and_retransmission)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_tx_session session = {};
    potr_packet packet = {};
    potr_packet emitted[3] = {};
    uint8_t payload = 1;
    packet.protocol_version = cplat_hton32(POTR_PROTOCOL_VERSION);
    packet.payload_len = cplat_hton16(1);
    packet.payload = &payload;
    int64_t seconds = 10;
    uint64_t nanoseconds = 1000;
    ON_CALL(mock_cplat, cplat_clock_get_realtime(_)).WillByDefault([&](cplat_timespec *out) {
        out->tv_sec = seconds++;
        out->tv_nsec = 123;
        return CPLAT_OK;
    }); // [状態] - 呼び出しごとに実時刻の秒部を進める。
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_high_resolution_ns(_)).WillByDefault([&](uint64_t *out) {
        *out = nanoseconds;
        nanoseconds += 100;
        return CPLAT_OK;
    }); // [状態] - 起点と各送出の単調増加時計を 100 [ns] ずつ進める。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_clock_get_realtime(_)).Times(3);
    // [Pre-Assert確認_正常系] - 実時刻を送出ごとに合計 3 回取得すること。
    EXPECT_CALL(mock_cplat, cplat_clock_get_monotonic_high_resolution_ns(_)).Times(4);
    // [Pre-Assert確認_正常系] - 起点と送出ごとに単調増加時計を合計 4 回取得すること。
    // Act
    potr_internal_tx_session_init(&session);
    for (size_t i = 0; i < 3; i++)
    {
        uint8_t wire[128] = {};
        size_t wire_len = sizeof(wire);
        ASSERT_EQ(POTR_OK, potr_internal_packet_seal(&session, &packet, nullptr, wire, &wire_len));
        // [状態確認] - 各経路と再送の封緘が成功すること。
        ASSERT_EQ(POTR_OK, potr_internal_packet_parse(&emitted[i], wire, wire_len));
        // [状態確認] - 時刻を解析できること。
    }
    // [手順] - 同じ平文から経路複製 2 回と再送 1 回の封緘を行う。
    // Assert
    for (size_t i = 0; i < 3; i++)
    {
        EXPECT_EQ(10 + static_cast<int64_t>(i), emitted[i].tx_tv_sec); // [確認_正常系 回数=3] - 実時刻を更新すること。
        EXPECT_EQ(123, emitted[i].tx_tv_nsec); // [確認_正常系 回数=3] - 実時刻のナノ秒部を保持すること。
        EXPECT_EQ((i + 1) * 100U, emitted[i].tx_mono_ns); // [確認_正常系 回数=3] - 起点からの差分を格納すること。
    }
}

// 時計取得失敗時も既定値で送出を続けることの確認
TEST(packetSealTest, clock_failures_use_sentinels)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_tx_session session = {};
    potr_packet packet = {};
    potr_packet emitted[3] = {};
    packet.protocol_version = cplat_hton32(POTR_PROTOCOL_VERSION);
    int actual_ret[3] = {};
    ON_CALL(mock_cplat, cplat_clock_get_realtime(_)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    // [状態] - 送出時の実時刻は取得に失敗する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_clock_get_monotonic_high_resolution_ns(_))
        .WillOnce(Return(CPLAT_ERR_UNKNOWN))
        .WillOnce(DoAll(SetArgPointee<0>(100U), Return(CPLAT_OK)))
        .WillOnce(DoAll(SetArgPointee<0>(200U), Return(CPLAT_OK)))
        .WillOnce(Return(CPLAT_ERR_UNKNOWN))
        .WillOnce(DoAll(SetArgPointee<0>(199U), Return(CPLAT_OK)));
    // [Pre-Assert確認_異常系] - 起点 2 回と送出 3 回の時計取得が呼ばれること。
    // [Pre-Assert手順] - 起点の失敗、送出時計の失敗、起点より前の値を順に用意する。
    // Act
    potr_internal_tx_session_init(&session);
    for (size_t i = 0; i < 3; i++)
    {
        if (i == 1)
        {
            potr_internal_tx_session_init(&session);
        }
        uint8_t wire[128] = {};
        size_t wire_len = sizeof(wire);
        actual_ret[i] = potr_internal_packet_seal(&session, &packet, nullptr, wire, &wire_len);
        ASSERT_EQ(POTR_OK, potr_internal_packet_parse(&emitted[i], wire, wire_len));
        // [状態確認] - 既定値を含む送出結果を解析できること。
    }
    // [手順] - 起点失敗、送出時計失敗、時計逆行の各状態で封緘する。
    // Assert
    for (size_t i = 0; i < 3; i++)
    {
        EXPECT_EQ(POTR_OK, actual_ret[i]); // [確認_正常系 回数=3] - 時計取得失敗時も封緘が成功すること。
        EXPECT_EQ(0, emitted[i].tx_tv_sec); // [確認_異常系 回数=3] - 実時刻の秒部が 0 であること。
        EXPECT_EQ(-1, emitted[i].tx_tv_nsec); // [確認_異常系 回数=3] - 実時刻のナノ秒部が -1 であること。
        EXPECT_EQ(UINT64_MAX, emitted[i].tx_mono_ns); // [確認_異常系 回数=3] - 単調増加時刻が未取得値であること。
    }
}

// 通番と時刻と予約領域の書き換えを認証で検出することの確認
TEST(packetSealTest, header_tampering_fails_authentication)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_tx_session session = {};
    potr_packet packet = {};
    uint8_t key[POTR_CRYPTO_KEY_SIZE] = {};
    uint8_t wire[128] = {};
    size_t wire_len = sizeof(wire);
    const size_t offsets[] = {40, 48, 56, 60, 64};
    int actual_ret[5] = {};
    packet.protocol_version = cplat_hton32(POTR_PROTOCOL_VERSION);
    potr_internal_tx_session_init(&session);
    ASSERT_EQ(POTR_OK, potr_internal_packet_seal(&session, &packet, key, wire, &wire_len));
    // [状態確認] - 認証タグだけを持つパケットの準備が成功すること。
    // Pre-Assert
    // Act
    for (size_t i = 0; i < 5; i++)
    {
        std::array<uint8_t, 128> tampered = {};
        std::memcpy(tampered.data(), wire, wire_len);
        tampered[offsets[i]] ^= 1;
        potr_packet opened = {};
        uint8_t plaintext[1] = {};
        ASSERT_EQ(POTR_OK, potr_internal_packet_parse(&opened, tampered.data(), wire_len));
        // [状態確認] - 改ざん後のヘッダーを解析できること。
        actual_ret[i] = potr_internal_packet_open(&opened, tampered.data(), key, plaintext, sizeof(plaintext));
        // [手順] - 通番、実時刻の各部、予約領域、単調増加時刻を書き換えて開封する。
    }
    // Assert
    for (int actual : actual_ret)
    {
        EXPECT_EQ(POTR_ERR_PROTOCOL, actual); // [確認_異常系 回数=5] - 各ヘッダー改ざんが認証失敗になること。
    }
}

// 256 通番の窓が重複と窓外を拒否し、窓内の逆順を受理することの確認
TEST(packetSealTest, replay_window_handles_reordering_and_large_advances)
{
    // Arrange
    potr_internal_replay_window window = {};
    potr_packet packet = {};
    const uint64_t counters[] = {0, 0, 255, 1, 1, 256, 0, 1000, 744, 745, 999, 1000};
    const int expected[] = {POTR_OK, POTR_ERR_PROTOCOL, POTR_OK, POTR_OK, POTR_ERR_PROTOCOL, POTR_OK,
                            POTR_ERR_PROTOCOL, POTR_OK, POTR_ERR_PROTOCOL, POTR_OK, POTR_OK, POTR_ERR_PROTOCOL};
    int actual_ret[12] = {};
    packet.session_id = 1;
    // Pre-Assert
    // Act
    for (size_t i = 0; i < 12; i++)
    {
        packet.tx_counter = counters[i];
        actual_ret[i] = potr_internal_replay_window_accept(&window, &packet);
    }
    // [手順] - 窓境界、順序逆転、256 個を超える進行と重複を順に検査する。
    // Assert
    for (size_t i = 0; i < 12; i++)
    {
        EXPECT_EQ(expected[i], actual_ret[i]); // [確認_正常系 回数=12] - 各通番の受理と破棄が期待どおりであること。
    }
    EXPECT_EQ(1000U, window.highest); // [確認_正常系] - 最大通番を保持すること。
}

// 相手セッション変更で記録を初期化し、旧セッションを受理しないことの確認
TEST(packetSealTest, replay_window_resets_for_new_session)
{
    // Arrange
    potr_internal_replay_window window = {};
    potr_packet packet = {};
    int actual_ret[5] = {};
    // Pre-Assert
    // Act
    packet.tx_counter = 1000;
    actual_ret[0] = potr_internal_replay_window_accept(&window, &packet);
    packet.session_id = 1;
    packet.tx_counter = 0;
    actual_ret[1] = potr_internal_replay_window_accept(&window, &packet);
    actual_ret[2] = potr_internal_replay_window_accept(&window, &packet);
    packet.session_tv_sec = 1;
    packet.session_id = 0;
    actual_ret[3] = potr_internal_replay_window_accept(&window, &packet);
    packet.session_tv_sec = 0;
    actual_ret[4] = potr_internal_replay_window_accept(&window, &packet);
    // [手順] - 識別子と開始時刻の更新で同じ通番を受信し、最後に旧セッションを受信する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret[0]); // [確認_正常系] - 最初のセッションを受理すること。
    EXPECT_EQ(POTR_OK, actual_ret[1]); // [確認_正常系] - 識別子更新後の通番 0 を受理すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret[2]); // [確認_異常系] - 同一セッションの重複は破棄すること。
    EXPECT_EQ(POTR_OK, actual_ret[3]); // [確認_正常系] - 開始時刻更新後の通番 0 を受理すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret[4]); // [確認_異常系] - 旧セッションでは窓を戻さないこと。
}

// 平文パケットではリプレイの検査と記録を行わないことの確認
TEST(packetSealTest, plaintext_does_not_change_replay_window)
{
    // Arrange
    potr_internal_replay_window window = {};
    potr_internal_replay_window before = window;
    potr_packet packet = {};
    uint8_t header[POTR_PACKET_HEADER_SIZE] = {};
    int actual_ret[2] = {};
    // Pre-Assert
    // Act
    actual_ret[0] = thread_recv_replay_packet(&window, &packet, header);
    actual_ret[1] = thread_recv_replay_packet(&window, &packet, header);
    // [手順] - 同じ平文パケットを 2 回受信する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret[0]); // [確認_正常系] - 初回を受理すること。
    EXPECT_EQ(POTR_OK, actual_ret[1]); // [確認_正常系] - 同じ平文通番も受理すること。
    EXPECT_EQ(0, std::memcmp(&before, &window, sizeof(window))); // [確認_正常系] - 窓を変更しないこと。
}

// 複数スレッドで採番しても送出通番が重複しないことの確認
TEST(packetSealTest, concurrent_sealing_does_not_duplicate_counters)
{
    // Arrange
    potr_internal_tx_session session = {};
    potr_internal_packet_session_hdr identity = {17, 23, 41, 7};
    potr_packet packet = {};
    uint8_t payload = 1;
    std::array<std::thread, 4> workers;
    uint64_t counters[4][64] = {};
    int failures[4] = {};
    potr_internal_tx_session_init(&session);
    ASSERT_EQ(POTR_OK, potr_internal_packet_build_ping(&packet, &identity, 42, &payload, 1));
    // [状態確認] - 並行送出元の PING が構築できること。
    // Pre-Assert
    // Act
    for (size_t thread = 0; thread < 4; thread++)
    {
        workers[thread] = std::thread([&, thread] {
            for (size_t i = 0; i < 64; i++)
            {
                uint8_t wire[128] = {};
                size_t wire_len = sizeof(wire);
                potr_packet opened = {};
                if (potr_internal_packet_seal(&session, &packet, nullptr, wire, &wire_len) != POTR_OK ||
                    potr_internal_packet_parse(&opened, wire, wire_len) != POTR_OK)
                {
                    failures[thread]++;
                }
                counters[thread][i] = opened.tx_counter;
            }
        });
    }
    for (auto &worker : workers)
    {
        worker.join();
    }
    // [手順] - 4 スレッドから各 64 回封緘する。
    std::set<uint64_t> unique;
    for (const auto &row : counters)
    {
        unique.insert(std::begin(row), std::end(row));
    }
    // Assert
    for (int failure : failures)
    {
        EXPECT_EQ(0, failure); // [確認_正常系 回数=4] - 各スレッドで封緘と解析の失敗がないこと。
    }
    EXPECT_EQ(256U, unique.size()); // [確認_正常系] - 256 回の通番がすべて異なること。
    EXPECT_EQ(256U, cplat_atomic_load_u64(&session.next_counter, CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_正常系] - 送出回数だけ次の通番が進むこと。
}

// 上限に達した送出通番を消費せず、通知継続不能の経路で送信を禁止することの確認
TEST(packetSealTest, counter_exhaustion_stops_session_without_wrapping)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_packet packet = {};
    uint8_t wire[128] = {};
    size_t wire_len = sizeof(wire);
    packet.protocol_version = cplat_hton32(POTR_PROTOCOL_VERSION);
    cplat_atomic_store_u64(&ctx.tx_session.next_counter, UINT64_MAX - 1U, CPLAT_MEMORY_ORDER_RELAXED);
    ASSERT_EQ(POTR_OK, potr_internal_packet_seal(&ctx.tx_session, &packet, nullptr, wire, &wire_len));
    // [状態確認] - 最後の有効通番 UINT64_MAX-1 を採番できること。
    std::array<uint8_t, 128> before = {};
    std::memcpy(before.data(), wire, sizeof(wire));
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_clock_get_realtime(_)).Times(0);
    // [Pre-Assert確認_異常系] - 枯渇時に実時刻を取得しないこと。
    EXPECT_CALL(mock_cplat, cplat_clock_get_monotonic_high_resolution_ns(_)).Times(0);
    // [Pre-Assert確認_異常系] - 枯渇時に単調増加時計を取得しないこと。
    EXPECT_CALL(mock_cplat, cplat_crypto_encrypt(_, _, _, _, _, _, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - 枯渇時に暗号化を行わないこと。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_abort(&ctx.send_queue, POTR_ERR_DISCONNECTED)).Times(1);
    // [Pre-Assert確認_異常系] - 送信待機を切断理由で 1 回解除すること。
    // Act
    int actual_ret = potr_internal_packet_seal_for_context(&ctx, &ctx.tx_session, &packet, wire, &wire_len);
    // [手順] - UINT64_MAX に達したセッションを封緘する。
    // Assert
    EXPECT_EQ(POTR_ERR_DISCONNECTED, actual_ret); // [確認_異常系] - 送出を禁止すること。
    EXPECT_EQ(UINT64_MAX, cplat_atomic_load_u64(&ctx.tx_session.next_counter, CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_異常系] - 通番を 0 に戻さないこと。
    EXPECT_EQ(POTR_ERR_DISCONNECTED, cplat_atomic_load_i32(&ctx.fatal_error, CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_異常系] - 以降のサービス送信を切断として拒否すること。
    EXPECT_EQ(0, std::memcmp(before.data(), wire, sizeof(wire))); // [確認_異常系] - 出力ワイヤーを変更しないこと。
}

// N:1 の通番枯渇が対象ピアの切断要求となり、別ピアの送信を止めないことの確認
TEST(packetSealTest, multi_peer_exhaustion_requests_only_peer_disconnect)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context peer = {};
    potr_packet packet = {};
    uint8_t wire[128] = {};
    size_t wire_len = sizeof(wire);
    ctx.is_multi_peer = 1;
    cplat_atomic_store_u64(&peer.tx_session.next_counter, UINT64_MAX, CPLAT_MEMORY_ORDER_RELAXED);
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_abort(_, _)).Times(0);
    // [Pre-Assert確認_異常系] - サービス全体の送信待機を解除しないこと。
    // Act
    int actual_ret = potr_internal_packet_seal_for_context(&ctx, &peer.tx_session, &packet, wire, &wire_len);
    // [手順] - ピア固有の送出通番の枯渇を検出する。
    // Assert
    EXPECT_EQ(POTR_ERR_DISCONNECTED, actual_ret); // [確認_異常系] - 当該セッションの送出を拒否すること。
    EXPECT_EQ(1, cplat_atomic_load_i32(&peer.tx_session.exhausted, CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_異常系] - 呼び出し元が論理切断するための要求を設定すること。
    EXPECT_EQ(POTR_OK, cplat_atomic_load_i32(&ctx.fatal_error, CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_正常系] - 別ピアが使うサービスを停止しないこと。
}

// 認証済みの時刻と予約領域の値を受信判定に使わないことの確認
TEST(packetSealTest, authenticated_timestamp_and_reserved_values_do_not_reject_packet)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_tx_session session = {};
    potr_packet packet = {};
    uint8_t key[POTR_CRYPTO_KEY_SIZE] = {};
    uint8_t wire[128] = {};
    uint8_t nonce[POTR_CRYPTO_NONCE_SIZE] = {};
    uint8_t plaintext[1] = {};
    size_t wire_len = sizeof(wire);
    packet.protocol_version = cplat_hton32(POTR_PROTOCOL_VERSION);
    ASSERT_EQ(POTR_OK, potr_internal_packet_seal(&session, &packet, key, wire, &wire_len));
    // [状態確認] - 認証タグだけのパケットを作成できること。
    std::memset(wire + 48, 0xff, 12);
    std::memset(wire + 64, 0xff, 8);
    wire[63] = 1;
    std::memcpy(nonce, wire + 16, 4);
    std::memcpy(nonce + 4, wire + 40, 8);
    size_t tag_len = POTR_CRYPTO_TAG_SIZE;
    ASSERT_EQ(CPLAT_OK, delegate_real_cplat_crypto_encrypt(wire + POTR_PACKET_HEADER_SIZE, &tag_len, nullptr, 0,
                                                          key, nonce, wire, POTR_PACKET_HEADER_SIZE));
    // [状態確認] - 時刻未取得と非ゼロの予約領域を含むヘッダーを認証できること。
    ASSERT_EQ(POTR_OK, potr_internal_packet_parse(&packet, wire, wire_len));
    // [状態確認] - 対象の時刻と予約領域を解析できること。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_packet_open(&packet, wire, key, plaintext, sizeof(plaintext));
    // [手順] - 認証済みの制御パケットを開封する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 時刻と予約領域の値によらず受理すること。
    EXPECT_EQ(-1, packet.tx_tv_sec); // [確認_正常系] - 実時刻の秒部を判定に使わないこと。
    EXPECT_EQ(-1, packet.tx_tv_nsec); // [確認_正常系] - 実時刻のナノ秒部を判定に使わないこと。
    EXPECT_EQ(UINT64_MAX, packet.tx_mono_ns); // [確認_正常系] - 未取得の単調増加時刻を判定に使わないこと。
    EXPECT_EQ(1U, packet.reserved); // [確認_正常系] - 非ゼロの予約領域を破棄条件にしないこと。
}

// セッション採用時は認証直後の通番を保持し、追跡再開時は異なるセッションへ初期化することの確認
TEST(packetSealTest, preparing_session_preserves_authenticated_packet_and_resets_different_session)
{
    // Arrange
    potr_internal_replay_window window = {};
    potr_packet packet = {};
    packet.session_tv_sec = 10;
    ASSERT_EQ(POTR_OK, potr_internal_replay_window_accept(&window, &packet));
    // [状態確認] - 認証直後に記録した通番を用意できること。
    // Pre-Assert
    // Act
    potr_internal_replay_window_prepare(&window, &packet);
    int actual_ret_same = potr_internal_replay_window_accept(&window, &packet);
    packet.session_tv_sec = 1;
    potr_internal_replay_window_prepare(&window, &packet);
    int actual_ret_new = potr_internal_replay_window_accept(&window, &packet);
    int actual_ret_duplicate = potr_internal_replay_window_accept(&window, &packet);
    // [手順] - 採用済みセッションを準備し、追跡再開時に異なるセッションへ切り替える。
    // Assert
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_same); // [確認_異常系] - 採用時に初回の受信記録を失わないこと。
    EXPECT_EQ(POTR_OK, actual_ret_new); // [確認_正常系] - 追跡を再開するセッションの時刻が小さくても受理すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_ret_duplicate); // [確認_異常系] - 初期化後も重複を拒否すること。
}
