#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <mock_string.h>
#include <porter/porter_result.h>
#include <algorithm>
#include <cstring>
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
    EXPECT_CALL(mock_cplat, cplat_thread_join(ctx.send_thread, CPLAT_SYNC_WAIT_FOREVER)).WillOnce(Return(CPLAT_OK));
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
    ctx.is_multi_peer = 1;                  // [状態] - N:1 モードとする。
    ctx.global.max_payload = 1400U;         // [状態] - 最大ペイロード長を設定する。
    ctx.send_wire_buf = wire_buf.data();    // [状態] - 送信 wire 組立バッファーを設定する。
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
    EXPECT_CALL(mock_porter, potr_internal_peer_find_by_id(&ctx, 5U))
        .WillOnce(Return(&peer))
        .WillOnce(Return(nullptr));
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
