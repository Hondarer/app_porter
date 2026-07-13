#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>

#include <porter/porter_const.h>
#include <porter/porter_type.h>
#include <porter/protocol/packet.h>

#if defined(PLATFORM_LINUX)
    #include <arpa/inet.h>
#elif defined(PLATFORM_WINDOWS)
    #include <com_util/base/windows_sdk.h>
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
        memset(buf, 0, PACKET_HEADER_SIZE);
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

// packet_build_packed が現行プロトコル バージョンをヘッダーへ設定することの確認
TEST_F(packetTest, packet_build_packed_sets_protocol_version)
{
    // Arrange
    PotrPacket pkt;
    PotrPacketSessionHdr shdr;
    uint8_t payload[1] = {0xAB}; // [状態] - 1 バイトのペイロード 0xAB を用意する。

    memset(&pkt, 0, sizeof(pkt));
    memset(&shdr, 0, sizeof(shdr));
    shdr.service_id = 42;
    shdr.session_tv_sec = 1000;
    shdr.session_id = 1234U;
    shdr.session_tv_nsec = 5678; // [状態] - セッション ヘッダーを service_id=42、session_id=1234 などで初期化する。

    // Pre-Assert

    // Act
    int rtc = packet_build_packed(&pkt, &shdr, 9U, payload,
                                  sizeof(payload)); // [手順] - packet_build_packed でパケットを構築する。

    // Assert
    ASSERT_EQ(POTR_SUCCESS, rtc); // [確認_正常系] - 戻り値が POTR_SUCCESS であること。
    EXPECT_EQ(POTR_PROTOCOL_VERSION,
              ntohl(pkt.protocol_version)); // [確認_正常系] - protocol_version に現行バージョンが設定されること。
}

// packet_parse が現行プロトコル バージョンのパケットを受理することの確認
TEST_F(packetTest, packet_parse_accepts_current_protocol_version)
{
    // Arrange
    uint8_t wire[PACKET_HEADER_SIZE];
    PotrPacket pkt;

    build_wire_packet(wire, POTR_PROTOCOL_VERSION); // [状態] - 現行バージョンの wire パケットを組み立てる。

    // Pre-Assert

    // Act
    int rtc = packet_parse(&pkt, wire, sizeof(wire)); // [手順] - packet_parse で wire パケットを解析する。

    // Assert
    ASSERT_EQ(POTR_SUCCESS, rtc);  // [確認_正常系] - 戻り値が POTR_SUCCESS であること。
    EXPECT_EQ(42, pkt.service_id); // [確認_正常系] - service_id 42 が復元されること。
    EXPECT_EQ(POTR_PROTOCOL_VERSION,
              pkt.protocol_version); // [確認_正常系] - protocol_version が現行バージョンであること。
}

// packet_parse が異なるプロトコル バージョンのパケットを拒否することの確認
TEST_F(packetTest, packet_parse_rejects_different_protocol_version)
{
    // Arrange
    uint8_t wire[PACKET_HEADER_SIZE];
    PotrPacket pkt;

    build_wire_packet(wire, POTR_PROTOCOL_VERSION + 1U); // [状態] - 現行バージョン + 1 の wire パケットを組み立てる。

    // Pre-Assert

    // Act
    int rtc = packet_parse(&pkt, wire, sizeof(wire)); // [手順] - packet_parse で wire パケットを解析する。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc); // [確認_異常系] - POTR_ERROR が返ること。
}

// packet_parse がバージョン 0 (旧 reserved 領域) のパケットを拒否することの確認
TEST_F(packetTest, packet_parse_rejects_legacy_reserved_zero)
{
    // Arrange
    uint8_t wire[PACKET_HEADER_SIZE];
    PotrPacket pkt;

    build_wire_packet(wire, 0U); // [状態] - protocol_version が 0 の wire パケットを組み立てる。

    // Pre-Assert

    // Act
    int rtc = packet_parse(&pkt, wire, sizeof(wire)); // [手順] - packet_parse で wire パケットを解析する。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc); // [確認_異常系] - POTR_ERROR が返ること。
}
