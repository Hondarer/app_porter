#include <cplat/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_cplat.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/porter_type.h>
#include <porter/protocol/packet.h>

#if defined(PLATFORM_LINUX)
    #include <arpa/inet.h>
#elif defined(PLATFORM_WINDOWS)
    #include <cplat/base/windows_sdk.h>
#endif /* PLATFORM_ */

#include <string.h>

using namespace testing;

class packetTest : public Test
{
  protected:
    static uint64_t hton64_test(uint64_t v)
    {
        uint32_t hi = htonl((uint32_t)(v >> 32));
        uint32_t lo = htonl((uint32_t)(v & 0xFFFFFFFFUL));
        return ((uint64_t)lo << 32) | (uint64_t)hi;
    }

    static void write_u16_be(uint8_t *buf, size_t offset, uint16_t value)
    {
        uint16_t nbo = htons(value);
        memcpy(buf + offset, &nbo, sizeof(nbo));
    }

    static void write_u32_be(uint8_t *buf, size_t offset, uint32_t value)
    {
        uint32_t nbo = htonl(value);
        memcpy(buf + offset, &nbo, sizeof(nbo));
    }

    static void write_i64_be(uint8_t *buf, size_t offset, int64_t value)
    {
        uint64_t nbo = hton64_test((uint64_t)value);
        memcpy(buf + offset, &nbo, sizeof(nbo));
    }

    static void build_wire_packet(uint8_t *buf, uint32_t protocol_version)
    {
        memset(buf, 0, POTR_PACKET_HEADER_SIZE);
        write_i64_be(buf, 0, 42);
        write_i64_be(buf, 8, 1000);
        write_u32_be(buf, 16, 1234U);
        write_u32_be(buf, 20, 5678U);
        write_u32_be(buf, 24, 9U);
        write_u32_be(buf, 28, 0U);
        write_u16_be(buf, 32, POTR_FLAG_PING);
        write_u16_be(buf, 34, 0U);
        write_u32_be(buf, 36, protocol_version);
    }
};

// potr_internal_packet_build_packed が現行プロトコル バージョンをヘッダーへ設定することの確認
TEST_F(packetTest, potr_internal_packet_build_packed_sets_protocol_version)
{
    // Arrange
    potr_packet pkt = {};
    potr_internal_packet_session_hdr shdr = {};
    uint8_t payload[1] = {0xAB}; // [状態] - 1 バイトのペイロード 0xAB を用意する。

    shdr.service_id = 42;
    shdr.session_tv_sec = 1000;
    shdr.session_id = 1234U;
    shdr.session_tv_nsec = 5678; // [状態] - セッション ヘッダーを service_id=42、session_id=1234 などで初期化する。

    // Pre-Assert

    // Act
    int actual_ret = potr_internal_packet_build_packed(
        &pkt, &shdr, 9U, payload,
        sizeof(payload)); // [手順] - 1 バイトのペイロードを指定して potr_internal_packet_build_packed() を呼び出す。

    // Assert
    ASSERT_EQ(POTR_OK,
              actual_ret); // [確認_正常系] - potr_internal_packet_build_packed() の戻り値が POTR_OK であること。
    EXPECT_EQ(
        POTR_PROTOCOL_VERSION,
        ntohl(
            pkt.protocol_version)); // [確認_正常系] - pkt.protocol_version に対する ntohl() の戻り値が POTR_PROTOCOL_VERSION であること。(ワイヤー上の protocol_version が現行バージョンであることの確認)
}

// potr_internal_packet_parse が現行プロトコル バージョンのパケットを受理することの確認
TEST_F(packetTest, potr_internal_packet_parse_accepts_current_protocol_version)
{
    // Arrange
    uint8_t wire[POTR_PACKET_HEADER_SIZE];
    potr_packet pkt;

    build_wire_packet(wire, POTR_PROTOCOL_VERSION); // [状態] - 現行バージョンの wire パケットを組み立てる。

    // Pre-Assert

    // Act
    int actual_ret = potr_internal_packet_parse(
        &pkt, wire, sizeof(wire)); // [手順] - wire パケットを指定して potr_internal_packet_parse() を呼び出す。

    // Assert
    ASSERT_EQ(
        POTR_OK,
        actual_ret); // [確認_正常系] - 現行プロトコル バージョンに対する potr_internal_packet_parse() の戻り値が POTR_OK であること。
    EXPECT_EQ(
        42, pkt.service_id); // [確認_正常系] - pkt.service_id が 42 であること。(service_id 42 が復元されたことの確認)
    EXPECT_EQ(POTR_PROTOCOL_VERSION,
              pkt.protocol_version); // [確認_正常系] - pkt.protocol_version が POTR_PROTOCOL_VERSION であること。
}

// potr_internal_packet_parse が異なるプロトコル バージョンのパケットを拒否することの確認
TEST_F(packetTest, potr_internal_packet_parse_rejects_different_protocol_version)
{
    // Arrange
    uint8_t wire[POTR_PACKET_HEADER_SIZE];
    potr_packet pkt;

    build_wire_packet(wire, POTR_PROTOCOL_VERSION + 1U); // [状態] - 現行バージョン + 1 の wire パケットを組み立てる。

    // Pre-Assert

    // Act
    int actual_ret = potr_internal_packet_parse(
        &pkt, wire, sizeof(wire)); // [手順] - wire パケットを指定して potr_internal_packet_parse() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_PROTOCOL,
        actual_ret); // [確認_異常系] - 現行と異なるプロトコル バージョンに対する potr_internal_packet_parse() の戻り値が POTR_ERR_PROTOCOL であること。
}

// potr_internal_packet_parse がバージョン 0 (旧 reserved 領域) のパケットを拒否することの確認
TEST_F(packetTest, potr_internal_packet_parse_rejects_legacy_reserved_zero)
{
    // Arrange
    uint8_t wire[POTR_PACKET_HEADER_SIZE];
    potr_packet pkt;

    build_wire_packet(wire, 0U); // [状態] - protocol_version が 0 の wire パケットを組み立てる。

    // Pre-Assert

    // Act
    int actual_ret = potr_internal_packet_parse(
        &pkt, wire, sizeof(wire)); // [手順] - wire パケットを指定して potr_internal_packet_parse() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_PROTOCOL,
        actual_ret); // [確認_異常系] - プロトコル バージョン 0 に対する potr_internal_packet_parse() の戻り値が POTR_ERR_PROTOCOL であること。
}

// 外側ヘッダーの長さと最大ペイロードがワイヤー仕様に一致することの確認
TEST_F(packetTest, wire_layout_matches_protocol)
{
    // Arrange
    // Pre-Assert
    // Act
    size_t header_size = POTR_PACKET_HEADER_SIZE; // [手順] - 外側ヘッダーの長さを取得する。
    // Assert
    EXPECT_EQ(72U, header_size); // [確認_正常系] - ヘッダー長が 72 であること。
    EXPECT_EQ(3U, POTR_PROTOCOL_VERSION); // [確認_正常系] - プロトコル版が 3 であること。
    EXPECT_EQ(65435U, POTR_MAX_PAYLOAD); // [確認_正常系] - 最大ペイロードが 65435 であること。
    EXPECT_EQ(8U, POTR_MAX_PATH); // [確認_正常系] - 最大経路数が 8 であること。
}

// build が送出情報と予約領域をゼロで符号化し parse が復号することの確認
TEST_F(packetTest, builds_zero_transmission_fields)
{
    // Arrange
    potr_packet packet;
    potr_packet parsed = {};
    potr_internal_packet_session_hdr session = {};
    uint8_t payload = 0xAB;
    uint8_t wire[POTR_PACKET_HEADER_WIRE_SIZE + 1U];
    memset(&packet, 0xFF, sizeof(packet)); // [状態] - 出力領域を非ゼロにする。
    // Pre-Assert
    // Act
    int built = potr_internal_packet_build_packed(&packet, &session, 1U, &payload, 1U);
    // [手順] - DATA パケットを構築する。
    memcpy(wire, &packet, POTR_PACKET_HEADER_SIZE);
    wire[POTR_PACKET_HEADER_SIZE] = payload;
    int decoded = potr_internal_packet_parse(&parsed, wire, sizeof(wire));
    // [手順] - 構築したワイヤー データを解析する。
    // Assert
    EXPECT_EQ(POTR_OK, built); // [確認_正常系] - build の戻り値が POTR_OK であること。
    ASSERT_EQ(POTR_OK, decoded); // [確認_正常系] - parse の戻り値が POTR_OK であること。
    EXPECT_EQ(0U, parsed.tx_counter); // [確認_正常系] - 送出通番が 0 であること。
    EXPECT_EQ(0, parsed.tx_tv_sec); // [確認_正常系] - 送出秒部が 0 であること。
    EXPECT_EQ(0, parsed.tx_tv_nsec); // [確認_正常系] - 送出ナノ秒部が 0 であること。
    EXPECT_EQ(0U, parsed.reserved); // [確認_正常系] - 予約領域が 0 であること。
    EXPECT_EQ(0U, parsed.tx_mono_ns); // [確認_正常系] - 経過時間が 0 であること。
    EXPECT_EQ(0xABU, parsed.payload[0]); // [確認_正常系] - ペイロードが復元されること。
}

// parse が送出情報を NBO から復号し、予約領域の値にかかわらず受理することの確認
TEST_F(packetTest, parses_transmission_fields_and_ignores_reserved)
{
    // Arrange
    uint8_t wire[POTR_PACKET_HEADER_WIRE_SIZE];
    potr_packet parsed = {};
    build_wire_packet(wire, POTR_PROTOCOL_VERSION);
    write_i64_be(wire, 40, INT64_C(0x123456789ABCDEF));
    write_i64_be(wire, 48, -123456789);
    write_u32_be(wire, 56, 987654321U);
    write_u32_be(wire, 60, 0xFFFFFFFFU);
    write_i64_be(wire, 64, INT64_C(0x23456789ABCDEF0));
    // [状態] - 送出情報と非ゼロの予約領域を NBO で設定する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_packet_parse(&parsed, wire, sizeof(wire));
    // [手順] - 72 バイトのヘッダーを解析する。
    // Assert
    ASSERT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 予約領域が非ゼロでも POTR_OK であること。
    EXPECT_EQ(UINT64_C(0x123456789ABCDEF), parsed.tx_counter); // [確認_正常系] - 送出通番が復元されること。
    EXPECT_EQ(-123456789, parsed.tx_tv_sec); // [確認_正常系] - 符号付きの送出秒部が復元されること。
    EXPECT_EQ(987654321, parsed.tx_tv_nsec); // [確認_正常系] - 送出ナノ秒部が復元されること。
    EXPECT_EQ(UINT64_C(0x23456789ABCDEF0), parsed.tx_mono_ns); // [確認_正常系] - 経過時間が復元されること。
    EXPECT_EQ(wire + 72, parsed.payload); // [確認_正常系] - ペイロード位置がオフセット 72 であること。
}

// parse が、フロー制御パケットを持たない版 2 のパケットを拒否することの確認
TEST_F(packetTest, rejects_protocol_version_two)
{
    // Arrange
    uint8_t wire[POTR_PACKET_HEADER_WIRE_SIZE];
    potr_packet parsed = {};
    build_wire_packet(wire, 2U); // [状態] - protocol_version を 2 にする。
    // Pre-Assert
    // Act
    int version_ret = potr_internal_packet_parse(&parsed, wire, sizeof(wire));
    // [手順] - 72 バイトで解析する。
    // Assert
    EXPECT_EQ(POTR_ERR_PROTOCOL, version_ret); // [確認_異常系] - 版 2 のパケットを拒否すること。
}

// フロー制御パケットの構築で、種別、状態と送出上限のペイロード、通番が設定されることの確認
TEST_F(packetTest, build_flow_control_sets_flag_state_and_send_limit)
{
    // Arrange
    potr_packet packet;
    potr_internal_packet_session_hdr session = {};
    uint8_t payload[POTR_FLOW_CONTROL_PAYLOAD_SIZE];
    const uint8_t expected_payload[POTR_FLOW_CONTROL_PAYLOAD_SIZE] = {(uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS, 0x12U,
                                                                       0x34U, 0x56U, 0x78U};
    session.service_id = 42;
    session.session_id = 7U;
    memset(&packet, 0xFF, sizeof(packet));   // [状態] - 出力領域を非ゼロにする。
    memset(payload, 0xEE, sizeof(payload)); // [状態] - ペイロードの格納先を非ゼロにする。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_packet_build_flow_control(&packet, &session, payload,
                                                             (uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS, 0x12345678U);
    // [手順] - 状態を「抑制の要求」、送出上限を 0x12345678 として potr_internal_packet_build_flow_control() を呼び出す。
    // Assert
    ASSERT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 戻り値が POTR_OK であること。
    EXPECT_EQ(htons(POTR_FLAG_FLOW_CONTROL), packet.flags); // [確認_正常系] - flags が POTR_FLAG_FLOW_CONTROL であること。
    EXPECT_EQ(htons(5U), packet.payload_len); // [確認_正常系] - ペイロード長が 5 バイトであること。
    EXPECT_EQ(payload, packet.payload); // [確認_正常系] - ペイロードが格納先を指すこと。
    EXPECT_EQ(0, memcmp(expected_payload, payload, sizeof(payload)));
    // [確認_正常系] - ペイロードが状態 1 バイトと、ネットワーク バイト オーダーの送出上限 4 バイトであること。
    EXPECT_EQ(0U, packet.seq_num); // [確認_正常系] - seq_num が 0 であること。
    EXPECT_EQ(0U, packet.ack_num); // [確認_正常系] - ack_num が 0 であること。
    EXPECT_EQ(htonl(POTR_PROTOCOL_VERSION), packet.protocol_version);
    // [確認_正常系] - protocol_version が現行の版であること。
    EXPECT_EQ(0U, packet.tx_counter); // [確認_正常系] - 送出通番が 0 であること。
}

// フロー制御パケットの構築が、定義されていない状態の値と NULL の格納先を拒否することの確認
TEST_F(packetTest, build_flow_control_rejects_undefined_state)
{
    // Arrange
    potr_packet packet = {};
    potr_internal_packet_session_hdr session = {};
    uint8_t payload[POTR_FLOW_CONTROL_PAYLOAD_SIZE] = {};
    // Pre-Assert
    // Act
    int undefined_zero_ret = potr_internal_packet_build_flow_control(&packet, &session, payload, 0U, 0U);
    int undefined_three_ret = potr_internal_packet_build_flow_control(&packet, &session, payload, 3U, 0U);
    int null_ret = potr_internal_packet_build_flow_control(&packet, &session, nullptr,
                                                           (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME, 0U);
    int resume_ret = potr_internal_packet_build_flow_control(&packet, &session, payload,
                                                             (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME, 0xFFFFFFFFU);
    // [手順] - 状態が 0、3、格納先が NULL、「解除」(送出上限 0xFFFFFFFF) の場合でそれぞれ構築する。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, undefined_zero_ret);  // [確認_異常系] - 状態 0 を拒否すること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, undefined_three_ret); // [確認_異常系] - 状態 3 (監視対象の一覧は専用の関数で構築する) を拒否すること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, null_ret); // [確認_異常系] - 格納先が NULL の場合を拒否すること。
    EXPECT_EQ(POTR_OK, resume_ret); // [確認_正常系] - 状態「解除」の戻り値が POTR_OK であること。
    EXPECT_EQ((uint8_t)POTR_FLOW_CONTROL_STATE_RESUME, payload[0]); // [確認_正常系] - 先頭が状態「解除」であること。
    EXPECT_EQ(0xFFU, payload[4]); // [確認_正常系] - 送出上限の最下位バイトが格納されること。
}

// 開封済みのフロー制御パケットから状態と送出上限を取り出し、構築と往復で一致することの確認
TEST_F(packetTest, flow_control_payload_decode_round_trip)
{
    // Arrange
    potr_packet built;
    potr_packet received;
    potr_internal_packet_session_hdr session = {};
    uint8_t payload[POTR_FLOW_CONTROL_PAYLOAD_SIZE];
    uint8_t state = 0U;
    uint32_t send_limit = 0U;
    ASSERT_EQ(POTR_OK, potr_internal_packet_build_flow_control(&built, &session, payload,
                                                               (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME, 0xFFFFFFFEU));
    // [状態] - 状態「解除」、送出上限 0xFFFFFFFE (通番の循環の直前) でパケットを構築する。
    // [状態確認] - 構築の戻り値が POTR_OK であること。
    memset(&received, 0, sizeof(received));
    received.payload = built.payload;
    received.payload_len = ntohs(built.payload_len); // [状態] - 開封後と同じく、ペイロード長をホスト バイト オーダーにする。
    // Pre-Assert
    // Act
    int actual_ret = flow_control_payload_decode(&received, &state, &send_limit); // [手順] - 状態と送出上限を取り出す。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 戻り値が POTR_OK であること。
    EXPECT_EQ((uint8_t)POTR_FLOW_CONTROL_STATE_RESUME, state); // [確認_正常系] - 状態が「解除」であること。
    EXPECT_EQ(0xFFFFFFFEU, send_limit); // [確認_正常系] - 送出上限が構築した値と一致すること。
}

// 長さが 5 バイトでないペイロード、不正な状態、0 でない通番、NULL のペイロードを破棄することの確認
TEST_F(packetTest, flow_control_payload_decode_rejects_malformed)
{
    // Arrange
    const uint8_t valid[6] = {(uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS, 0U, 0U, 0U, 1U, 0U};
    const uint8_t state_zero[5] = {0U, 0U, 0U, 0U, 1U};
    const uint8_t state_three[5] = {3U, 0U, 0U, 0U, 1U};
    potr_packet cases[7];
    int actual_rets[7] = {};
    uint8_t state = 0U;
    uint32_t send_limit = 0U;
    for (size_t i = 0; i < 7; i++)
    {
        memset(&cases[i], 0, sizeof(cases[i]));
        cases[i].payload = valid;
        cases[i].payload_len = 5U;
    }
    cases[0].payload_len = 1U;      // 版 3 の以前の長さ (1 バイト)
    cases[1].payload_len = 6U;      // 長すぎる
    cases[2].payload = state_zero;  // 状態が 0
    cases[3].payload = state_three; // 状態が 3
    cases[4].seq_num = 1U;          // seq_num が 0 でない
    cases[5].ack_num = 1U;          // ack_num が 0 でない
    cases[6].payload = nullptr;     // ペイロードが NULL
    // [状態] - 7 種類の不正なパケットを用意する (ループ 7 回)。
    // Pre-Assert
    // Act
    for (size_t i = 0; i < 7; i++)
    {
        actual_rets[i] = flow_control_payload_decode(&cases[i], &state, &send_limit);
    }
    // [手順] - 7 種類のパケットで状態と送出上限を取り出し、戻り値を記録する。
    // Assert
    for (size_t i = 0; i < 7; i++)
    {
        EXPECT_EQ(POTR_ERR_PROTOCOL, actual_rets[i]);
        // [確認_異常系] - 戻り値が POTR_ERR_PROTOCOL であること。(確認 7 回)
    }
}

// parse が版 1 のヘッダーと、版 1 を指定した十分な長さのパケットを拒否することの確認
TEST_F(packetTest, rejects_protocol_version_one)
{
    // Arrange
    uint8_t wire[POTR_PACKET_HEADER_WIRE_SIZE];
    potr_packet parsed = {};
    build_wire_packet(wire, 1U); // [状態] - protocol_version を 1 にする。
    // Pre-Assert
    // Act
    int short_ret = potr_internal_packet_parse(&parsed, wire, 40U);
    int version_ret = potr_internal_packet_parse(&parsed, wire, sizeof(wire));
    // [手順] - 40 バイトと 72 バイトでそれぞれ解析する。
    // Assert
    EXPECT_EQ(POTR_ERR_PROTOCOL, short_ret); // [確認_異常系] - 40 バイトのパケットを拒否すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, version_ret); // [確認_異常系] - 版 1 のパケットを拒否すること。
}

// NBO の送出情報を構造体からワイヤーへコピーして復号できることの確認
TEST_F(packetTest, transmission_fields_round_trip)
{
    // Arrange
    potr_packet encoded = {};
    potr_packet decoded = {};
    uint8_t wire[POTR_PACKET_HEADER_WIRE_SIZE];
    encoded.protocol_version = htonl(POTR_PROTOCOL_VERSION);
    encoded.tx_counter = hton64_test(UINT64_C(0xFEDCBA9876543210));
    encoded.tx_tv_sec = (int64_t)hton64_test((uint64_t)INT64_C(-123456789012));
    encoded.tx_tv_nsec = (int32_t)htonl(123456789U);
    encoded.tx_mono_ns = hton64_test(UINT64_C(0xABCDEF0123456789));
    // [状態] - 送出情報を NBO で設定し、予約領域は 0 にする。
    // Pre-Assert
    // Act
    memcpy(wire, &encoded, POTR_PACKET_HEADER_SIZE);
    int actual_ret = potr_internal_packet_parse(&decoded, wire, sizeof(wire));
    // [手順] - 構造体の固定長ヘッダーをワイヤーへコピーして解析する。
    // Assert
    ASSERT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 戻り値が POTR_OK であること。
    EXPECT_EQ(UINT64_C(0xFEDCBA9876543210), decoded.tx_counter); // [確認_正常系] - 送出通番が復元されること。
    EXPECT_EQ(INT64_C(-123456789012), decoded.tx_tv_sec); // [確認_正常系] - 送出秒部が復元されること。
    EXPECT_EQ(123456789, decoded.tx_tv_nsec); // [確認_正常系] - 送出ナノ秒部が復元されること。
    EXPECT_EQ(0U, decoded.reserved); // [確認_正常系] - 予約領域が 0 であること。
    EXPECT_EQ(UINT64_C(0xABCDEF0123456789), decoded.tx_mono_ns); // [確認_正常系] - 経過時間が復元されること。
}

// 制御パケットの build が送出情報と予約領域をゼロ初期化することの確認
TEST_F(packetTest, control_builders_clear_transmission_fields)
{
    // Arrange
    potr_packet packets[5];
    potr_internal_packet_session_hdr session = {};
    int results[5];
    memset(packets, 0xFF, sizeof(packets)); // [状態] - 各出力領域を非ゼロにする。
    // Pre-Assert
    // Act
    results[0] = potr_internal_packet_build_nack(&packets[0], &session, 1U);
    results[1] = potr_internal_packet_build_ping(&packets[1], &session, 1U, nullptr, 0U);
    results[2] = potr_internal_packet_build_reject(&packets[2], &session, 1U);
    results[3] = potr_internal_packet_build_fin(&packets[3], &session);
    results[4] = potr_internal_packet_build_fin_ack(&packets[4], &session, 1U);
    // [手順] - NACK、PING、REJECT、FIN、FIN_ACK を構築する。
    // Assert
    for (size_t i = 0; i < 5U; i++)
    {
        EXPECT_EQ(POTR_OK, results[i]); // [確認_正常系 回数=5] - build の戻り値が POTR_OK であること。
        EXPECT_EQ(0U, packets[i].tx_counter); // [確認_正常系 回数=5] - 送出通番が 0 であること。
        EXPECT_EQ(0, packets[i].tx_tv_sec); // [確認_正常系 回数=5] - 送出秒部が 0 であること。
        EXPECT_EQ(0, packets[i].tx_tv_nsec); // [確認_正常系 回数=5] - 送出ナノ秒部が 0 であること。
        EXPECT_EQ(0U, packets[i].reserved); // [確認_正常系 回数=5] - 予約領域が 0 であること。
        EXPECT_EQ(0U, packets[i].tx_mono_ns); // [確認_正常系 回数=5] - 経過時間が 0 であること。
    }
}

// 監視対象の一覧のパケットを構築し、状態、台数、受信者のセッション識別子を格納し、解析と往復で一致することの確認
TEST_F(packetTest, build_flow_control_list_round_trip)
{
    // Arrange
    potr_packet packet;
    potr_packet received;
    potr_internal_packet_session_hdr session = {};
    potr_internal_packet_session_hdr receivers[2] = {};
    potr_internal_packet_session_hdr decoded[2] = {};
    uint8_t payload[POTR_FLOW_CONTROL_MONITOR_PAYLOAD_MAX];
    const uint8_t expected_first[POTR_FLOW_CONTROL_MONITOR_ENTRY_SIZE] = {
        0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U, 0xA1U, 0xA2U, 0xA3U, 0xA4U, 0x00U, 0x00U, 0x00U, 0x09U};
    size_t count = 0U;
    session.service_id = 42;
    session.session_id = 7U;
    receivers[0].session_tv_sec = 0x0102030405060708LL;
    receivers[0].session_id = 0xA1A2A3A4U;
    receivers[0].session_tv_nsec = 9;
    receivers[1].session_tv_sec = 100;
    receivers[1].session_id = 0x55U;
    receivers[1].session_tv_nsec = 999999999;
    memset(&packet, 0xFF, sizeof(packet));   // [状態] - 出力領域を非ゼロにする。
    memset(payload, 0xEE, sizeof(payload)); // [状態] - ペイロードの格納先を非ゼロにする。
    // [状態] - 2 台の受信者のセッション識別子を用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_packet_build_flow_control_list(&packet, &session, payload, receivers, 2U);
    // [手順] - 2 台の一覧で potr_internal_packet_build_flow_control_list() を呼び出す。
    memset(&received, 0, sizeof(received));
    received.payload = packet.payload;
    received.payload_len = ntohs(packet.payload_len); // [手順] - 開封後と同じく、ペイロード長をホスト バイト オーダーにする。
    int decode_ret = flow_control_monitor_decode(&received, &count); // [手順] - 台数を取り出す。
    flow_control_monitor_entry(&received, 0U, &decoded[0]);
    flow_control_monitor_entry(&received, 1U, &decoded[1]);
    // [手順] - 2 台分の受信者のセッション識別子を取り出す。
    // Assert
    ASSERT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 戻り値が POTR_OK であること。
    EXPECT_EQ(htons(POTR_FLAG_FLOW_CONTROL), packet.flags); // [確認_正常系] - flags が POTR_FLAG_FLOW_CONTROL であること。
    EXPECT_EQ(htons(34U), packet.payload_len); // [確認_正常系] - ペイロード長が 2 + 16 × 2 バイトであること。
    EXPECT_EQ(payload, packet.payload); // [確認_正常系] - ペイロードが格納先を指すこと。
    EXPECT_EQ((uint8_t)POTR_FLOW_CONTROL_STATE_MONITOR, payload[0]); // [確認_正常系] - 先頭が状態 3 であること。
    EXPECT_EQ(2U, payload[1]); // [確認_正常系] - 次の 1 バイトが台数 2 であること。
    EXPECT_EQ(0, memcmp(expected_first, payload + 2, sizeof(expected_first)));
    // [確認_正常系] - 1 台目が session_tv_sec 8 バイト、session_id 4 バイト、session_tv_nsec 4 バイトのネットワーク バイト オーダーであること。
    EXPECT_EQ(0U, packet.seq_num); // [確認_正常系] - seq_num が 0 であること。
    EXPECT_EQ(0U, packet.ack_num); // [確認_正常系] - ack_num が 0 であること。
    EXPECT_EQ(htonl(7U), packet.session_id); // [確認_正常系] - 送信者自身のセッション識別子であること。
    EXPECT_EQ(POTR_OK, decode_ret); // [確認_正常系] - 解析の戻り値が POTR_OK であること。
    EXPECT_EQ(2U, count); // [確認_正常系] - 台数が 2 であること。
    EXPECT_EQ(receivers[0].session_tv_sec, decoded[0].session_tv_sec); // [確認_正常系] - 1 台目の session_tv_sec が一致すること。
    EXPECT_EQ(receivers[0].session_id, decoded[0].session_id);         // [確認_正常系] - 1 台目の session_id が一致すること。
    EXPECT_EQ(receivers[0].session_tv_nsec, decoded[0].session_tv_nsec); // [確認_正常系] - 1 台目の session_tv_nsec が一致すること。
    EXPECT_EQ(receivers[1].session_tv_sec, decoded[1].session_tv_sec); // [確認_正常系] - 2 台目の session_tv_sec が一致すること。
    EXPECT_EQ(receivers[1].session_id, decoded[1].session_id);         // [確認_正常系] - 2 台目の session_id が一致すること。
    EXPECT_EQ(receivers[1].session_tv_nsec, decoded[1].session_tv_nsec); // [確認_正常系] - 2 台目の session_tv_nsec が一致すること。
}

// 監視対象の一覧の構築が、台数が範囲外の場合と NULL の引数を拒否することの確認
TEST_F(packetTest, build_flow_control_list_rejects_invalid_arguments)
{
    // Arrange
    potr_packet packet = {};
    potr_internal_packet_session_hdr session = {};
    potr_internal_packet_session_hdr receivers[POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX + 1U] = {};
    uint8_t payload[POTR_FLOW_CONTROL_MONITOR_PAYLOAD_MAX + POTR_FLOW_CONTROL_MONITOR_ENTRY_SIZE] = {};
    // Pre-Assert
    // Act
    int zero_ret = potr_internal_packet_build_flow_control_list(&packet, &session, payload, receivers, 0U);
    int over_ret = potr_internal_packet_build_flow_control_list(&packet, &session, payload, receivers,
                                                                POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX + 1U);
    int max_ret = potr_internal_packet_build_flow_control_list(&packet, &session, payload, receivers,
                                                               POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX);
    int null_receivers_ret = potr_internal_packet_build_flow_control_list(&packet, &session, payload, nullptr, 1U);
    int null_payload_ret = potr_internal_packet_build_flow_control_list(&packet, &session, nullptr, receivers, 1U);
    // [手順] - 台数 0、65、64、受信者の配列が NULL、格納先が NULL の場合でそれぞれ構築する。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, zero_ret); // [確認_異常系] - 台数 0 を拒否すること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, over_ret); // [確認_異常系] - 台数 65 を拒否すること。
    EXPECT_EQ(POTR_OK, max_ret); // [確認_正常系] - 台数 64 を受け付けること。
    EXPECT_EQ(htons((uint16_t)POTR_FLOW_CONTROL_MONITOR_PAYLOAD_MAX), packet.payload_len);
    // [確認_正常系] - 台数 64 のペイロード長が 2 + 16 × 64 バイトであること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, null_receivers_ret); // [確認_異常系] - 受信者の配列が NULL の場合を拒否すること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, null_payload_ret); // [確認_異常系] - 格納先が NULL の場合を拒否すること。
}

// 状態、台数、長さ、seq_num、ack_num が不正な監視対象の一覧を解析で拒否することの確認
TEST_F(packetTest, flow_control_monitor_decode_rejects_malformed)
{
    // Arrange
    uint8_t valid[POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE + POTR_FLOW_CONTROL_MONITOR_ENTRY_SIZE] = {
        (uint8_t)POTR_FLOW_CONTROL_STATE_MONITOR, 1U};
    uint8_t wrong_state[sizeof(valid)] = {(uint8_t)POTR_FLOW_CONTROL_STATE_RESUME, 1U};
    uint8_t zero_count[POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE] = {(uint8_t)POTR_FLOW_CONTROL_STATE_MONITOR, 0U};
    uint8_t over_count[POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE] = {(uint8_t)POTR_FLOW_CONTROL_STATE_MONITOR, 65U};
    potr_packet cases[7];
    size_t count = 0U;
    memset(cases, 0, sizeof(cases));
    cases[0].payload = wrong_state;
    cases[0].payload_len = (uint16_t)sizeof(wrong_state); // 状態が 3 でない
    cases[1].payload = zero_count;
    cases[1].payload_len = (uint16_t)sizeof(zero_count); // 台数が 0
    cases[2].payload = over_count;
    cases[2].payload_len = (uint16_t)(POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE + POTR_FLOW_CONTROL_MONITOR_ENTRY_SIZE * 65U);
    // 台数が 65
    cases[3].payload = valid;
    cases[3].payload_len = (uint16_t)(sizeof(valid) - 1U); // 長さが台数に合わない
    cases[4].payload = valid;
    cases[4].payload_len = (uint16_t)sizeof(valid);
    cases[4].seq_num = 1U; // seq_num が 0 でない
    cases[5].payload = valid;
    cases[5].payload_len = (uint16_t)sizeof(valid);
    cases[5].ack_num = 1U; // ack_num が 0 でない
    cases[6].payload = nullptr;
    cases[6].payload_len = (uint16_t)sizeof(valid); // ペイロードが NULL
    potr_packet accepted = {};
    accepted.payload = valid;
    accepted.payload_len = (uint16_t)sizeof(valid);
    int actual_rets[7] = {};
    // [状態] - 7 種類の不正な一覧と、正しい 1 台の一覧を用意する (ループ 7 回)。
    // Pre-Assert
    // Act
    for (size_t i = 0; i < 7; i++)
    {
        actual_rets[i] = flow_control_monitor_decode(&cases[i], &count);
    }
    // [手順] - 7 種類の不正な一覧を順に解析する。
    int accepted_ret = flow_control_monitor_decode(&accepted, &count); // [手順] - 正しい 1 台の一覧を解析する。
    // Assert
    for (size_t i = 0; i < 7; i++)
    {
        EXPECT_EQ(POTR_ERR_PROTOCOL, actual_rets[i]); // [確認_異常系] - 不正な一覧を POTR_ERR_PROTOCOL とすること。(確認 7 回)
    }
    EXPECT_EQ(POTR_OK, accepted_ret); // [確認_正常系] - 正しい一覧を受け付けること。
    EXPECT_EQ(1U, count);             // [確認_正常系] - 台数が 1 であること。
}
