/**
 *******************************************************************************
 *  @file           threadRecvSlotTest.cc
 *  @brief          受信スロットの展開先境界 (max_message_size) とコールバック抑止を検証します。
 *
 *******************************************************************************
 */
#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <cplat/compress/compress.h>
#include <cplat/sync/atomic.h>
#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include "thread_recv_slot.h"
#include <cstdint>
#include <cstring>
#include <vector>

using namespace testing;

namespace
{

void dummy_callback(const int64_t service_id, const potr_peer_id peer_id, const potr_event event, const void *data,
                    const size_t len)
{
    (void)service_id;
    (void)peer_id;
    (void)event;
    (void)data;
    (void)len;
}

} // namespace

// max_message_size ちょうどの圧縮メッセージが展開され DATA コールバックへ渡ることの確認
TEST(threadRecvSlotTest, compressed_message_at_max_message_size_delivers_data)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    constexpr size_t kMaxMessageSize = 256;
    std::vector<uint8_t> plain(kMaxMessageSize, 0x41);
    std::vector<uint8_t> compressed(CPLAT_COMPRESS_HEADER_SIZE + kMaxMessageSize + 64U);
    size_t compressed_len = compressed.size();
    ASSERT_EQ(CPLAT_OK, cplat_compress(compressed.data(), &compressed_len, plain.data(), plain.size()));
    // [状態確認] - max_message_size ちょうどのテスト入力を圧縮できること。

    potr_context ctx = {};
    std::vector<uint8_t> recv_compress_buf(kMaxMessageSize);
    std::vector<uint8_t> frag_buf(kMaxMessageSize);
    ctx.service.service_id = 1;
    ctx.global.max_message_size = kMaxMessageSize;
    ctx.compress_buf_size = kMaxMessageSize;
    ctx.recv_compress_buf = recv_compress_buf.data();
    ctx.frag_buf = frag_buf.data();
    ctx.callback = dummy_callback;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);

    thread_recv_slot slot = {};
    thread_recv_slot_init_ctx(&slot, &ctx);

    potr_packet elem = {};
    elem.flags = POTR_FLAG_COMPRESSED;
    elem.payload = compressed.data();
    elem.payload_len = static_cast<uint16_t>(compressed_len);

    std::vector<uint8_t> actual_delivered;
    // [状態] - 展開先サイズを max_message_size と一致させた受信スロットと境界ちょうど入力を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_callback_emit(&ctx, POTR_PEER_NA, POTR_EVENT_DATA, _, kMaxMessageSize))
        .Times(1)
        .WillOnce(
            [&](potr_context *, potr_peer_id, potr_event, const void *data, size_t len)
            {
                const auto *bytes = static_cast<const uint8_t *>(data);
                actual_delivered.assign(bytes, bytes + len);
            });
    // [Pre-Assert確認_正常系] - potr_internal_callback_emit() が 1 回呼ばれること。
    // [Pre-Assert手順] - 渡された展開データを actual_delivered へ保存する。

    // Act
    thread_recv_slot_deliver_payload_elem(&slot, &elem);
    // [手順] - max_message_size ちょうどの圧縮エレメントを受信スロットへ渡す。

    // Assert
    EXPECT_EQ(plain, actual_delivered); // [確認_正常系] - 展開結果が元データと一致すること。
    EXPECT_EQ(kMaxMessageSize, slot.packet_expanded_size);
    // [確認_正常系] - 単一圧縮パケットの展開後の大きさが slot.packet_expanded_size に記録されること。
}

// max_message_size を 1 バイト超過する圧縮メッセージは展開失敗し DATA コールバックを呼ばないことの確認
TEST(threadRecvSlotTest, compressed_message_one_over_max_message_size_skips_data_callback)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    constexpr size_t kMaxMessageSize = 256;
    std::vector<uint8_t> over_plain(kMaxMessageSize + 1U, 0x42);
    std::vector<uint8_t> compressed(CPLAT_COMPRESS_HEADER_SIZE + over_plain.size() + 64U);
    size_t compressed_len = compressed.size();
    ASSERT_EQ(CPLAT_OK, cplat_compress(compressed.data(), &compressed_len, over_plain.data(), over_plain.size()));
    // [状態確認] - max_message_size + 1 のテスト入力を圧縮できること。

    potr_context ctx = {};
    std::vector<uint8_t> recv_compress_buf(kMaxMessageSize);
    std::vector<uint8_t> frag_buf(kMaxMessageSize);
    ctx.service.service_id = 1;
    ctx.global.max_message_size = kMaxMessageSize;
    ctx.compress_buf_size = kMaxMessageSize;
    ctx.recv_compress_buf = recv_compress_buf.data();
    ctx.frag_buf = frag_buf.data();
    ctx.callback = dummy_callback;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);

    thread_recv_slot slot = {};
    thread_recv_slot_init_ctx(&slot, &ctx);

    potr_packet elem = {};
    elem.flags = POTR_FLAG_COMPRESSED;
    elem.payload = compressed.data();
    elem.payload_len = static_cast<uint16_t>(compressed_len);
    // [状態] - 展開先サイズを max_message_size に保ったまま 1 バイト超過の圧縮入力を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_callback_emit(_, _, POTR_EVENT_DATA, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - 上限超過時は DATA コールバックを呼ばないこと。

    // Act
    thread_recv_slot_deliver_payload_elem(&slot, &elem);
    // [手順] - max_message_size + 1 の圧縮エレメントを受信スロットへ渡す。

    // Assert
    // [確認_異常系] - Pre-Assert の Times(0) を満たし DATA コールバックを呼ばないこと。
}

// 分割受信後に結合した圧縮メッセージが max_message_size ちょうどなら展開成功することの確認
TEST(threadRecvSlotTest, fragmented_compressed_message_at_max_message_size_delivers_data)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    constexpr size_t kMaxMessageSize = 256;
    std::vector<uint8_t> plain(kMaxMessageSize, 0x43);
    std::vector<uint8_t> compressed(CPLAT_COMPRESS_HEADER_SIZE + kMaxMessageSize + 64U);
    size_t compressed_len = compressed.size();
    ASSERT_EQ(CPLAT_OK, cplat_compress(compressed.data(), &compressed_len, plain.data(), plain.size()));
    // [状態確認] - max_message_size ちょうどの入力を cplat_compress() で圧縮できること。
    ASSERT_GE(compressed_len, 2U);
    // [状態確認] - 圧縮結果が 2 フラグメントへ分割できる長さ (2 バイト以上) であること。

    potr_context ctx = {};
    std::vector<uint8_t> recv_compress_buf(kMaxMessageSize);
    std::vector<uint8_t> frag_buf(kMaxMessageSize);
    ctx.service.service_id = 1;
    ctx.global.max_message_size = kMaxMessageSize;
    ctx.compress_buf_size = kMaxMessageSize;
    ctx.recv_compress_buf = recv_compress_buf.data();
    ctx.frag_buf = frag_buf.data();
    ctx.callback = dummy_callback;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);

    thread_recv_slot slot = {};
    thread_recv_slot_init_ctx(&slot, &ctx);

    size_t split = compressed_len / 2U;
    potr_packet frag1 = {};
    frag1.flags = POTR_FLAG_COMPRESSED | POTR_FLAG_MORE_FRAG;
    frag1.payload = compressed.data();
    frag1.payload_len = static_cast<uint16_t>(split);

    potr_packet frag2 = {};
    frag2.flags = 0;
    frag2.payload = compressed.data() + split;
    frag2.payload_len = static_cast<uint16_t>(compressed_len - split);

    std::vector<uint8_t> actual_delivered;
    // [状態] - 境界ちょうどの圧縮データを 2 フラグメントへ分割した受信入力を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_callback_emit(&ctx, POTR_PEER_NA, POTR_EVENT_DATA, _, kMaxMessageSize))
        .Times(1)
        .WillOnce(
            [&](potr_context *, potr_peer_id, potr_event, const void *data, size_t len)
            {
                const auto *bytes = static_cast<const uint8_t *>(data);
                actual_delivered.assign(bytes, bytes + len);
            });
    // [Pre-Assert確認_正常系] - potr_internal_callback_emit() が 1 回呼ばれること。
    // [Pre-Assert手順] - 結合展開後のデータを actual_delivered へ保存する。

    // Act
    thread_recv_slot_deliver_payload_elem(&slot, &frag1);
    thread_recv_slot_deliver_payload_elem(&slot, &frag2);
    // [手順] - 分割フラグメントを順次受信スロットへ渡す。

    // Assert
    EXPECT_EQ(plain, actual_delivered); // [確認_正常系] - 展開結果が元データと一致すること。
    EXPECT_EQ((kMaxMessageSize + 1U) / 2U, slot.packet_expanded_size);
    // [確認_正常系] - 2 フラグメント圧縮メッセージの 1 パケットあたりの展開後の大きさが記録されること。
}

// 分割受信後に結合した圧縮メッセージが max_message_size + 1 のときは DATA コールバックを呼ばないことの確認
TEST(threadRecvSlotTest, fragmented_compressed_message_one_over_max_message_size_skips_data_callback)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    constexpr size_t kMaxMessageSize = 256;
    std::vector<uint8_t> over_plain(kMaxMessageSize + 1U, 0x44);
    std::vector<uint8_t> compressed(CPLAT_COMPRESS_HEADER_SIZE + over_plain.size() + 64U);
    size_t compressed_len = compressed.size();
    ASSERT_EQ(CPLAT_OK, cplat_compress(compressed.data(), &compressed_len, over_plain.data(), over_plain.size()));
    // [状態確認] - max_message_size + 1 の入力を cplat_compress() で圧縮できること。
    ASSERT_GE(compressed_len, 2U);
    // [状態確認] - 圧縮結果が 2 フラグメントへ分割できる長さ (2 バイト以上) であること。

    potr_context ctx = {};
    std::vector<uint8_t> recv_compress_buf(kMaxMessageSize);
    std::vector<uint8_t> frag_buf(kMaxMessageSize);
    ctx.service.service_id = 1;
    ctx.global.max_message_size = kMaxMessageSize;
    ctx.compress_buf_size = kMaxMessageSize;
    ctx.recv_compress_buf = recv_compress_buf.data();
    ctx.frag_buf = frag_buf.data();
    ctx.callback = dummy_callback;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);

    thread_recv_slot slot = {};
    thread_recv_slot_init_ctx(&slot, &ctx);

    size_t split = compressed_len / 2U;
    potr_packet frag1 = {};
    frag1.flags = POTR_FLAG_COMPRESSED | POTR_FLAG_MORE_FRAG;
    frag1.payload = compressed.data();
    frag1.payload_len = static_cast<uint16_t>(split);

    potr_packet frag2 = {};
    frag2.flags = 0;
    frag2.payload = compressed.data() + split;
    frag2.payload_len = static_cast<uint16_t>(compressed_len - split);
    // [状態] - max_message_size + 1 の圧縮データを 2 フラグメントへ分割した受信入力を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_callback_emit(_, _, POTR_EVENT_DATA, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - 上限超過時は結合後の展開失敗により DATA コールバックを呼ばないこと。

    // Act
    thread_recv_slot_deliver_payload_elem(&slot, &frag1);
    thread_recv_slot_deliver_payload_elem(&slot, &frag2);
    // [手順] - 1 バイト超過データの分割フラグメントを順次受信スロットへ渡す。

    // Assert
    // [確認_異常系] - Pre-Assert の Times(0) を満たし DATA コールバックを呼ばないこと。
}
