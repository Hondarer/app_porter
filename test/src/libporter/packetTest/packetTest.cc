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
    EXPECT_EQ(2U, POTR_PROTOCOL_VERSION); // [確認_正常系] - プロトコル版が 2 であること。
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
    build_wire_packet(wire, 2U);
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
    encoded.protocol_version = htonl(2U);
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
