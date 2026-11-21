/**
 *******************************************************************************
 *  @file           threadRecvWindowRejectTest.cc
 *  @brief          REJECT 受信時の切断・復旧および受信ウィンドウ範囲外 DATA の処理を検証します。
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
#include <porter/porter_const.h>
#include <porter/porter_result.h>
extern "C"
{
#include <porter/potr_notify.h>
#include <porter/potr_path_event.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/packet_seal.h>
#include <porter/protocol/window.h>
#include "thread_recv_session.h"
#include "thread_recv_window.h"
}
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace testing;

namespace
{

// 受信ウィンドウ投入および accept_outer 用のパック済み DATA ペイロードとパケットを構築する。
struct PackedDataFixture
{
    std::vector<uint8_t> packed_payload;
    potr_packet packet = {};
};

PackedDataFixture makePackedDataPacket(uint32_t seq_num, uint32_t session_id, int64_t tv_sec, int32_t tv_nsec,
                                       const char *text)
{
    PackedDataFixture out;
    const size_t text_len = std::strlen(text);
    const uint16_t elem_flags = cplat_hton16(0U);
    const uint32_t elem_len = cplat_hton32(static_cast<uint32_t>(text_len));

    out.packed_payload.resize(6U + text_len);
    std::memcpy(out.packed_payload.data(), &elem_flags, sizeof(elem_flags));
    std::memcpy(out.packed_payload.data() + 2U, &elem_len, sizeof(elem_len));
    if (text_len > 0U)
    {
        std::memcpy(out.packed_payload.data() + 6U, text, text_len);
    }

    out.packet.service_id = 101;
    out.packet.session_tv_sec = tv_sec;
    out.packet.session_id = session_id;
    out.packet.session_tv_nsec = tv_nsec;
    out.packet.seq_num = seq_num;
    out.packet.ack_num = 0U;
    out.packet.flags = POTR_FLAG_DATA;
    out.packet.payload_len = static_cast<uint16_t>(out.packed_payload.size());
    out.packet.protocol_version = POTR_PROTOCOL_VERSION;
    out.packet.payload = out.packed_payload.data();
    return out;
}

potr_packet makeRejectPacket(uint32_t ack_num, uint32_t session_id, int64_t tv_sec, int32_t tv_nsec)
{
    potr_packet pkt = {};
    pkt.service_id = 101;
    pkt.session_tv_sec = tv_sec;
    pkt.session_id = session_id;
    pkt.session_tv_nsec = tv_nsec;
    pkt.seq_num = 0U;
    pkt.ack_num = ack_num;
    pkt.flags = POTR_FLAG_REJECT;
    pkt.payload_len = 0U;
    pkt.protocol_version = POTR_PROTOCOL_VERSION;
    return pkt;
}

} // namespace

class threadRecvWindowRejectTest : public Test
{
  protected:
    // [サブ手順 名前=threadRecvWindowRejectTest.SetUp]
    void SetUp() override
    {
        current_instance = this;
        ctx = {};
        slot = {};
        sender = {};
        events.clear();
        frag_storage.assign(64U, 0x5AU);

        ctx.role = POTR_ROLE_RECEIVER;
        ctx.service.service_id = 101;
        ctx.service.type = POTR_TYPE_UNICAST;
        ctx.service.src_port = 5000U;
        ctx.service.encrypt_enabled = 0;
        ctx.global.window_size = 8U;
        ctx.global.max_payload = 64U;
        ctx.global.reorder_timeout_ms = 0U;
        ctx.n_path = 1;
        for (size_t i = 0; i < POTR_MAX_PATH; ++i)
        {
            ctx.sock[i] = CPLAT_INVALID_SOCKET;
            ctx.tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
            ctx.tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
        }
        ctx.sock[0] = static_cast<cplat_socket>(7);
        ctx.dest_addr[0].address = cplat_hton32(0x7F000001U);
        ctx.dest_addr[0].port = cplat_hton16(5000U);
        sender = ctx.dest_addr[0];
        ctx.session_id = 1U;
        ctx.session_ts.tv_sec = 10;
        ctx.session_ts.tv_nsec = 20;
        potr_internal_tx_session_init(&ctx.tx_session);
        ctx.callback = &threadRecvWindowRejectTest::onRecvEvent;

        ASSERT_EQ(POTR_OK, potr_internal_callback_mutex_init(&ctx));
        // [状態確認] - potr_internal_callback_mutex_init() の戻り値が POTR_OK であること。
        ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&ctx.send_window_mutex));
        // [状態確認] - cplat_local_lock_create() の戻り値が CPLAT_OK であること。
        ASSERT_EQ(POTR_OK, potr_internal_notify_start(&ctx));
        // [状態確認] - potr_internal_notify_start() の戻り値が POTR_OK であること。
        ASSERT_EQ(POTR_OK, potr_internal_window_init(&ctx.recv_window, 100U, 8U, 64U));
        // [状態確認] - potr_internal_window_init() の戻り値が POTR_OK であること。

        ctx.peer_session_id = 42U;
        ctx.peer_session_ts.tv_sec = 1000;
        ctx.peer_session_ts.tv_nsec = 500;
        ctx.peer_session_known = 1;
        cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELAXED);
        ctx.path_logical_alive[0] = 1;
        cplat_atomic_store_u8(&ctx.path_ping_state[0], POTR_PING_STATE_NORMAL, CPLAT_MEMORY_ORDER_RELAXED);
        ctx.last_recv_ts.tv_sec = 12345;
        ctx.last_recv_ts.tv_nsec = 67890;
        ctx.frag_buf = frag_storage.data();
        ctx.frag_buf_len = 16U;
        ctx.frag_compressed = 1;
        ctx.reorder_pending = 1;
        ctx.reorder_nack_num = 100U;

        slot.ctx = &ctx;
        slot.peer = nullptr;
        slot.peer_id = POTR_PEER_NA;
        slot.recv_window = &ctx.recv_window;
        slot.send_window = &ctx.send_window;
        slot.send_window_mutex = ctx.send_window_mutex;
        slot.session_id = &ctx.session_id;
        slot.session_ts = &ctx.session_ts;
        slot.dest_addr = ctx.dest_addr;
        slot.nack_dedup_buf = ctx.nack_dedup_buf;
        slot.nack_dedup_next = &ctx.nack_dedup_next;
        slot.peer_session_id = &ctx.peer_session_id;
        slot.peer_session_ts = &ctx.peer_session_ts;
        slot.peer_session_known = &ctx.peer_session_known;
        slot.reorder_pending = &ctx.reorder_pending;
        slot.reorder_nack_num = &ctx.reorder_nack_num;
        slot.reorder_deadline_ts = &ctx.reorder_deadline_ts;
        slot.pending_fin = &ctx.pending_fin;
        slot.fin_target_seq = &ctx.fin_target_seq;
        slot.frag_buf = ctx.frag_buf;
        slot.frag_buf_len = &ctx.frag_buf_len;
        slot.frag_count = &ctx.frag_count;
        slot.frag_compressed = &ctx.frag_compressed;
        slot.health_alive = &ctx.health_alive;
        slot.last_recv_ts = &ctx.last_recv_ts;
        slot.path_last_recv_ts = ctx.path_last_recv_ts;
        slot.path_ping_state = ctx.path_ping_state;
        slot.remote_path_ping_state = ctx.remote_path_ping_state;

        ON_CALL(mock_porter, thread_recv_fin_pending_reached(_, _)).WillByDefault(Return(0));
    }
    // [サブ手順終了]

    // [サブ手順 名前=threadRecvWindowRejectTest.TearDown]
    void TearDown() override
    {
        potr_internal_notify_stop(&ctx);
        potr_internal_window_dispose(&ctx.recv_window);
        if (ctx.send_window_mutex != nullptr)
        {
            cplat_local_lock_dispose(ctx.send_window_mutex);
            ctx.send_window_mutex = nullptr;
        }
        potr_internal_callback_mutex_dispose(&ctx);
        current_instance = nullptr;
    }
    // [サブ手順終了]

    static void onRecvEvent(int64_t, potr_peer_id, potr_event event, const void *, size_t)
    {
        if (current_instance != nullptr)
        {
            current_instance->events.push_back(event);
        }
    }

    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    thread_recv_slot slot = {};
    cplat_ipv4_endpoint sender = {};
    std::vector<uint8_t> frag_storage;
    std::vector<potr_event> events;
    static threadRecvWindowRejectTest *current_instance;
};

threadRecvWindowRejectTest *threadRecvWindowRejectTest::current_instance = nullptr;

// next_seq と同じ通番の REJECT を欠番として処理し、切断通知と受信状態の破棄を行うことの確認
// [サブ手順参照 名前=threadRecvWindowRejectTest.SetUp]
TEST_F(threadRecvWindowRejectTest, reject_at_next_seq_disconnects_and_discards_recv_state)
{
    // Arrange
    const PackedDataFixture buffered = makePackedDataPacket(101U, 42U, 1000, 500, "hold");
    ASSERT_EQ(POTR_OK, potr_internal_window_recv_push(&ctx.recv_window, &buffered.packet));
    // [状態確認] - 欠番 100 より先の通番 101 を受信ウィンドウへ事前保持できること。
    const potr_packet reject_pkt = makeRejectPacket(100U, 42U, 1000, 500);
    // [状態] - next_seq と同じ通番 100 の REJECT パケットを用意する。

    // Pre-Assert

    // Act
    thread_recv_window_on_reject(&slot, &reject_pkt, 0, &sender); // [手順] - next_seq と一致する REJECT を処理する。
    potr_internal_notify_stop(&ctx);                         // [手順] - 通知スレッドを停止して全イベントを回収する。

    // Assert
    EXPECT_EQ(nullptr, ctx.notifications); // [確認_正常系] - potr_internal_notify_stop() で通知資源を回収すること。
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.health_alive, CPLAT_MEMORY_ORDER_RELAXED)); // [確認_正常系] - ctx.health_alive が 0 であること。
    EXPECT_EQ(0, ctx.path_logical_alive[0]); // [確認_正常系] - ctx.path_logical_alive[0] が 0 であること。
    EXPECT_EQ(0, ctx.peer_session_known); // [確認_正常系] - ctx.peer_session_known が 0 であること。
    EXPECT_EQ(0U, ctx.recv_window.base_seq); // [確認_正常系] - ctx.recv_window.base_seq が 0U であること。
    EXPECT_EQ(0, potr_internal_window_recv_has_packet(&ctx.recv_window, 101U));
    // [確認_正常系] - potr_internal_window_recv_has_packet() の戻り値が 0 であること。(保持パケット破棄の確認)
    EXPECT_EQ(0U, ctx.frag_buf_len);    // [確認_正常系] - ctx.frag_buf_len が 0U であること。
    EXPECT_EQ(0, ctx.frag_compressed);  // [確認_正常系] - ctx.frag_compressed が 0 であること。
    EXPECT_EQ(0, ctx.reorder_pending);  // [確認_正常系] - ctx.reorder_pending が 0 であること。
    EXPECT_EQ(12345, ctx.last_recv_ts.tv_sec); // [確認_正常系] - ctx.last_recv_ts.tv_sec が 12345 であること。(維持の確認)
    EXPECT_EQ(67890, ctx.last_recv_ts.tv_nsec); // [確認_正常系] - ctx.last_recv_ts.tv_nsec が 67890 であること。(維持の確認)
    EXPECT_NE(events.end(), std::find(events.begin(), events.end(), POTR_EVENT_DISCONNECTED));
    // [確認_正常系] - std::find() の戻り値が events.end() 以外であること。(POTR_EVENT_DISCONNECTED 発火の確認)
}
// [サブ手順参照 名前=threadRecvWindowRejectTest.TearDown]

// 受信ウィンドウより先の通番の REJECT を欠番として処理し、切断通知と受信状態の破棄を行うことの確認
// [サブ手順参照 名前=threadRecvWindowRejectTest.SetUp]
TEST_F(threadRecvWindowRejectTest, reject_beyond_window_disconnects_and_discards_recv_state)
{
    // Arrange
    const PackedDataFixture buffered = makePackedDataPacket(101U, 42U, 1000, 500, "hold");
    ASSERT_EQ(POTR_OK, potr_internal_window_recv_push(&ctx.recv_window, &buffered.packet));
    // [状態確認] - 受信ウィンドウ内の通番 101 を事前保持できること。
    const potr_packet reject_pkt = makeRejectPacket(108U, 42U, 1000, 500);
    // [状態] - 受信ウィンドウ上限 107 より先の通番 108 の REJECT パケットを用意する。

    // Pre-Assert

    // Act
    thread_recv_window_on_reject(&slot, &reject_pkt, 0, &sender); // [手順] - 受信ウィンドウより先の REJECT を処理する。
    potr_internal_notify_stop(&ctx);                         // [手順] - 通知スレッドを停止して全イベントを回収する。

    // Assert
    EXPECT_EQ(nullptr, ctx.notifications); // [確認_正常系] - potr_internal_notify_stop() で通知資源を回収すること。
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.health_alive, CPLAT_MEMORY_ORDER_RELAXED)); // [確認_正常系] - ctx.health_alive が 0 であること。
    EXPECT_EQ(0, ctx.path_logical_alive[0]); // [確認_正常系] - ctx.path_logical_alive[0] が 0 であること。
    EXPECT_EQ(0, ctx.peer_session_known); // [確認_正常系] - ctx.peer_session_known が 0 であること。
    EXPECT_EQ(0U, ctx.recv_window.base_seq); // [確認_正常系] - ctx.recv_window.base_seq が 0U であること。
    EXPECT_EQ(0, potr_internal_window_recv_has_packet(&ctx.recv_window, 101U));
    // [確認_正常系] - potr_internal_window_recv_has_packet() の戻り値が 0 であること。(保持パケット破棄の確認)
    EXPECT_EQ(0U, ctx.frag_buf_len);    // [確認_正常系] - ctx.frag_buf_len が 0U であること。
    EXPECT_EQ(0, ctx.frag_compressed);  // [確認_正常系] - ctx.frag_compressed が 0 であること。
    EXPECT_EQ(0, ctx.reorder_pending);  // [確認_正常系] - ctx.reorder_pending が 0 であること。
    EXPECT_EQ(12345, ctx.last_recv_ts.tv_sec); // [確認_正常系] - ctx.last_recv_ts.tv_sec が 12345 であること。(維持の確認)
    EXPECT_EQ(67890, ctx.last_recv_ts.tv_nsec); // [確認_正常系] - ctx.last_recv_ts.tv_nsec が 67890 であること。(維持の確認)
    EXPECT_NE(events.end(), std::find(events.begin(), events.end(), POTR_EVENT_DISCONNECTED));
    // [確認_正常系] - std::find() の戻り値が events.end() 以外であること。(POTR_EVENT_DISCONNECTED 発火の確認)
}
// [サブ手順参照 名前=threadRecvWindowRejectTest.TearDown]

// 配信済み通番に対する遅着 REJECT を欠番ではないとして無視し、切断も状態破棄も行わないことの確認
// [サブ手順参照 名前=threadRecvWindowRejectTest.SetUp]
TEST_F(threadRecvWindowRejectTest, reject_already_delivered_seq_is_ignored)
{
    // Arrange
    const PackedDataFixture buffered = makePackedDataPacket(101U, 42U, 1000, 500, "hold");
    ASSERT_EQ(POTR_OK, potr_internal_window_recv_push(&ctx.recv_window, &buffered.packet));
    // [状態確認] - 受信ウィンドウ内の通番 101 を事前保持できること。
    const potr_packet reject_pkt = makeRejectPacket(99U, 42U, 1000, 500);
    // [状態] - 配信済み通番 99 の遅着 REJECT パケットを用意する。

    // Pre-Assert

    // Act
    thread_recv_window_on_reject(&slot, &reject_pkt, 0, &sender); // [手順] - 配信済み通番の REJECT を処理する。
    potr_internal_notify_stop(&ctx);                         // [手順] - 通知スレッドを停止して全イベントを回収する。

    // Assert
    EXPECT_EQ(nullptr, ctx.notifications); // [確認_正常系] - potr_internal_notify_stop() で通知資源を回収すること。
    EXPECT_EQ(1, cplat_atomic_load_i32(&ctx.health_alive, CPLAT_MEMORY_ORDER_RELAXED)); // [確認_正常系] - ctx.health_alive が 1 であること。
    EXPECT_EQ(1, ctx.path_logical_alive[0]); // [確認_正常系] - ctx.path_logical_alive[0] が 1 であること。
    EXPECT_EQ(1, ctx.peer_session_known); // [確認_正常系] - ctx.peer_session_known が 1 であること。
    EXPECT_EQ(100U, ctx.recv_window.base_seq); // [確認_正常系] - ctx.recv_window.base_seq が 100U であること。
    EXPECT_NE(0, potr_internal_window_recv_has_packet(&ctx.recv_window, 101U));
    // [確認_正常系] - potr_internal_window_recv_has_packet() の戻り値が 0 以外であること。(保持パケット維持の確認)
    EXPECT_EQ(16U, ctx.frag_buf_len);  // [確認_正常系] - ctx.frag_buf_len が 16U であること。
    EXPECT_EQ(1, ctx.frag_compressed); // [確認_正常系] - ctx.frag_compressed が 1 であること。
    EXPECT_EQ(1, ctx.reorder_pending); // [確認_正常系] - ctx.reorder_pending が 1 であること。
    EXPECT_TRUE(events.empty());       // [確認_正常系] - events.empty() の戻り値が true であること。(切断未発火の確認)
}
// [サブ手順参照 名前=threadRecvWindowRejectTest.TearDown]

// 受信ウィンドウ内で既に保持済みの通番に対する遅着 REJECT を無視し、切断も状態破棄も行わないことの確認
// [サブ手順参照 名前=threadRecvWindowRejectTest.SetUp]
TEST_F(threadRecvWindowRejectTest, reject_already_buffered_in_window_is_ignored)
{
    // Arrange
    const PackedDataFixture buffered = makePackedDataPacket(101U, 42U, 1000, 500, "hold");
    ASSERT_EQ(POTR_OK, potr_internal_window_recv_push(&ctx.recv_window, &buffered.packet));
    // [状態確認] - 受信ウィンドウ内の通番 101 を事前保持できること。
    const potr_packet reject_pkt = makeRejectPacket(101U, 42U, 1000, 500);
    // [状態] - 受信ウィンドウ内で保持済みの通番 101 の遅着 REJECT パケットを用意する。

    // Pre-Assert

    // Act
    thread_recv_window_on_reject(&slot, &reject_pkt, 0, &sender); // [手順] - 保持済み通番の REJECT を処理する。
    potr_internal_notify_stop(&ctx);                         // [手順] - 通知スレッドを停止して全イベントを回収する。

    // Assert
    EXPECT_EQ(nullptr, ctx.notifications); // [確認_正常系] - potr_internal_notify_stop() で通知資源を回収すること。
    EXPECT_EQ(1, cplat_atomic_load_i32(&ctx.health_alive, CPLAT_MEMORY_ORDER_RELAXED)); // [確認_正常系] - ctx.health_alive が 1 であること。
    EXPECT_EQ(1, ctx.path_logical_alive[0]); // [確認_正常系] - ctx.path_logical_alive[0] が 1 であること。
    EXPECT_EQ(1, ctx.peer_session_known); // [確認_正常系] - ctx.peer_session_known が 1 であること。
    EXPECT_EQ(100U, ctx.recv_window.base_seq); // [確認_正常系] - ctx.recv_window.base_seq が 100U であること。
    EXPECT_NE(0, potr_internal_window_recv_has_packet(&ctx.recv_window, 101U));
    // [確認_正常系] - potr_internal_window_recv_has_packet() の戻り値が 0 以外であること。(保持パケット維持の確認)
    EXPECT_EQ(16U, ctx.frag_buf_len);  // [確認_正常系] - ctx.frag_buf_len が 16U であること。
    EXPECT_EQ(1, ctx.frag_compressed); // [確認_正常系] - ctx.frag_compressed が 1 であること。
    EXPECT_EQ(1, ctx.reorder_pending); // [確認_正常系] - ctx.reorder_pending が 1 であること。
    EXPECT_TRUE(events.empty());       // [確認_正常系] - events.empty() の戻り値が true であること。(切断未発火の確認)
}
// [サブ手順参照 名前=threadRecvWindowRejectTest.TearDown]

// FIN を保留中の欠番 REJECT で保留中の FIN を発火して即座に戻り、切断通知を二重発火しないことの確認
// [サブ手順参照 名前=threadRecvWindowRejectTest.SetUp]
TEST_F(threadRecvWindowRejectTest, reject_gap_with_pending_fin_fires_fin_and_returns_immediately)
{
    // Arrange
    ctx.pending_fin = 1;
    ctx.fin_target_seq = 102U;
    const potr_packet reject_pkt = makeRejectPacket(100U, 42U, 1000, 500);
    // [状態] - 保留 FIN がある状態で欠番通番 100 の REJECT パケットを用意する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, thread_recv_fin_fire(&slot, 102U)).Times(1);
    // [Pre-Assert確認_正常系] - thread_recv_fin_fire() が 1 回呼ばれること。

    // Act
    thread_recv_window_on_reject(&slot, &reject_pkt, 0, &sender); // [手順] - 保留 FIN 中の欠番 REJECT を処理する。
    potr_internal_notify_stop(&ctx);                         // [手順] - 通知スレッドを停止して全イベントを回収する。

    // Assert
    EXPECT_EQ(nullptr, ctx.notifications); // [確認_正常系] - potr_internal_notify_stop() で通知資源を回収すること。
    EXPECT_TRUE(events.empty());
    // [確認_正常系] - events.empty() の戻り値が true であること。(on_reject からの切断通知二重発火なしの確認)
}
// [サブ手順参照 名前=threadRecvWindowRejectTest.TearDown]

// REJECT の後に届いた DATA で受信ウィンドウを再初期化し、データ配信と再接続通知を行うことの確認
// [サブ手順参照 名前=threadRecvWindowRejectTest.SetUp]
TEST_F(threadRecvWindowRejectTest, reject_then_new_data_readopts_session_and_delivers)
{
    // Arrange
    const PackedDataFixture buffered = makePackedDataPacket(101U, 42U, 1000, 500, "stale");
    ASSERT_EQ(POTR_OK, potr_internal_window_recv_push(&ctx.recv_window, &buffered.packet));
    // [状態確認] - REJECT 前に受信ウィンドウ内の通番 101 を保持できること。
    const potr_packet reject_pkt = makeRejectPacket(100U, 42U, 1000, 500);
    const PackedDataFixture next_data = makePackedDataPacket(200U, 42U, 1000, 500, "abcd");
    std::string delivered;
    // [状態] - REJECT とその後に同一セッションから通番 200 で届く DATA を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, thread_recv_slot_deliver_payload_elem(&slot, _))
        .Times(1)
        .WillOnce([&](thread_recv_slot *, const potr_packet *elem) {
            if (elem != nullptr && elem->payload != nullptr)
            {
                delivered.assign(reinterpret_cast<const char *>(elem->payload), elem->payload_len);
            }
        });
    // [Pre-Assert確認_正常系] - thread_recv_slot_deliver_payload_elem() が 1 回呼ばれること。
    // [Pre-Assert手順] - 配信されたペイロードを保存する。

    // Act
    thread_recv_window_on_reject(&slot, &reject_pkt, 0, &sender); // [手順] - 欠番 REJECT を処理して状態を破棄する。

    // Assert
    EXPECT_EQ(0, ctx.peer_session_known); // [確認_正常系] - ctx.peer_session_known が 0 であること。
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.health_alive, CPLAT_MEMORY_ORDER_RELAXED));       // [確認_正常系] - ctx.health_alive が 0 であること。

    // Act_2
    const int adopted = thread_recv_session_adopt(&slot, &next_data.packet); // [手順] - 後続 DATA でセッションを再採用する。
    thread_recv_window_accept_outer(&slot, &next_data.packet, 0);            // [手順] - 後続 DATA を受信ウィンドウへ受理する。
    potr_internal_notify_stop(&ctx);                                    // [手順] - 通知スレッドを停止して全イベントを回収する。

    // Assert_2
    EXPECT_EQ(1, adopted);        // [確認_正常系] - thread_recv_session_adopt() の戻り値が 1 であること。
    EXPECT_EQ(nullptr, ctx.notifications); // [確認_正常系] - potr_internal_notify_stop() で通知資源を回収すること。
    EXPECT_EQ(1, ctx.peer_session_known); // [確認_正常系] - ctx.peer_session_known が 1 であること。
    EXPECT_EQ(201U, ctx.recv_window.base_seq);
    // [確認_正常系] - ctx.recv_window.base_seq が 201U であること。(200 を起点に再初期化して配信済みの確認)
    EXPECT_EQ(1, cplat_atomic_load_i32(&ctx.health_alive, CPLAT_MEMORY_ORDER_RELAXED));       // [確認_正常系] - ctx.health_alive が 1 であること。
    EXPECT_STREQ("abcd", delivered.c_str()); // [確認_正常系] - delivered.c_str() が "abcd" であること。
    const auto disconnected_it = std::find(events.begin(), events.end(), POTR_EVENT_DISCONNECTED);
    ASSERT_NE(events.end(), disconnected_it);
    // [確認_正常系] - POTR_EVENT_DISCONNECTED を探した std::find() の戻り値が events.end() 以外であること。
    const auto connected_it = std::find(disconnected_it, events.end(), POTR_EVENT_CONNECTED);
    EXPECT_NE(events.end(), connected_it);
    // [確認_正常系] - DISCONNECTED 以降から POTR_EVENT_CONNECTED を探した std::find() の戻り値が events.end() 以外であること。
}
// [サブ手順参照 名前=threadRecvWindowRejectTest.TearDown]

// 受信ウィンドウより先の DATA を破棄し、next_seq に対して NACK を送信することの確認
// [サブ手順参照 名前=threadRecvWindowRejectTest.SetUp]
TEST_F(threadRecvWindowRejectTest, data_beyond_window_drops_and_sends_nack_for_next_seq)
{
    // Arrange
    const PackedDataFixture ahead_data = makePackedDataPacket(108U, 42U, 1000, 500, "ahead");
    std::vector<uint8_t> sent_wire;
    potr_packet sent_pkt = {};
    // [状態] - 受信ウィンドウ範囲 100〜107 より先の通番 108 の DATA を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, thread_recv_slot_deliver_payload_elem(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - thread_recv_slot_deliver_payload_elem() が呼ばれないこと。
    EXPECT_CALL(mock_cplat, cplat_socket_sendto(_, _, _, _, _, _))
        .Times(1)
        .WillOnce([&](cplat_socket, const void *wire, size_t len, const cplat_ipv4_endpoint *, size_t *sent,
                      cplat_error *) {
            const auto *bytes = static_cast<const uint8_t *>(wire);
            sent_wire.assign(bytes, bytes + len);
            *sent = len;
            return CPLAT_OK;
        });
    // [Pre-Assert確認_正常系] - cplat_socket_sendto() が 1 回呼ばれること。
    // [Pre-Assert手順] - 送信されたワイヤー パケットを保存する。

    // Act
    thread_recv_window_accept_outer(&slot, &ahead_data.packet, 0); // [手順] - 受信ウィンドウより先の DATA を処理する。

    // Assert
    EXPECT_EQ(100U, ctx.recv_window.base_seq); // [確認_正常系] - ctx.recv_window.base_seq が 100U であること。
    EXPECT_EQ(0, potr_internal_window_recv_has_packet(&ctx.recv_window, 108U));
    // [確認_正常系] - potr_internal_window_recv_has_packet() の戻り値が 0 であること。
    ASSERT_FALSE(sent_wire.empty()); // [確認_正常系] - sent_wire.empty() の戻り値が false であること。
    ASSERT_EQ(POTR_OK, potr_internal_packet_parse(&sent_pkt, sent_wire.data(), sent_wire.size()));
    // [確認_正常系] - potr_internal_packet_parse() の戻り値が POTR_OK であること。
    EXPECT_EQ(POTR_FLAG_NACK, sent_pkt.flags); // [確認_正常系] - sent_pkt.flags が POTR_FLAG_NACK であること。
    EXPECT_EQ(100U, sent_pkt.ack_num);
    // [確認_正常系] - sent_pkt.ack_num が 100U であること。(next_seq に対する NACK 送信の確認)
}
// [サブ手順参照 名前=threadRecvWindowRejectTest.TearDown]

// 配信済みの古い通番の DATA を破棄し、NACK を送信しないことの確認
// [サブ手順参照 名前=threadRecvWindowRejectTest.SetUp]
TEST_F(threadRecvWindowRejectTest, data_already_delivered_drops_without_sending_nack)
{
    // Arrange
    const PackedDataFixture old_data = makePackedDataPacket(99U, 42U, 1000, 500, "old");
    // [状態] - 配信済みの古い通番 99 の DATA を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, thread_recv_slot_deliver_payload_elem(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - thread_recv_slot_deliver_payload_elem() が呼ばれないこと。
    EXPECT_CALL(mock_cplat, cplat_socket_sendto(_, _, _, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - cplat_socket_sendto() が呼ばれないこと。

    // Act
    thread_recv_window_accept_outer(&slot, &old_data.packet, 0); // [手順] - 配信済みの古い DATA を処理する。

    // Assert
    EXPECT_EQ(100U, ctx.recv_window.base_seq); // [確認_正常系] - ctx.recv_window.base_seq が 100U であること。
    EXPECT_EQ(0, potr_internal_window_recv_has_packet(&ctx.recv_window, 99U));
    // [確認_正常系] - potr_internal_window_recv_has_packet() の戻り値が 0 であること。
}
// [サブ手順参照 名前=threadRecvWindowRejectTest.TearDown]

// 受信ウィンドウから順に取り出した DATA パケットごとに、次に受信する通番をフロー制御へ渡すことの確認
// (フラグメントの途中のパケットを含む)
// [サブ手順参照 名前=threadRecvWindowRejectTest.SetUp]
TEST_F(threadRecvWindowRejectTest, drained_data_packets_report_next_seq_to_flow_control)
{
    // Arrange
    PackedDataFixture fragment = makePackedDataPacket(100U, 42U, 1000, 500, "frag");
    const uint16_t more_frag = cplat_hton16((uint16_t)POTR_FLAG_MORE_FRAG);
    std::memcpy(fragment.packed_payload.data(), &more_frag, sizeof(more_frag));
    // [状態] - 通番 100 を、フラグメントの途中の断片を格納した DATA パケットにする。
    const PackedDataFixture last = makePackedDataPacket(101U, 42U, 1000, 500, "last");
    ASSERT_EQ(POTR_OK, potr_internal_window_recv_push(&ctx.recv_window, &last.packet));
    // [状態] - 通番 101 の DATA パケットを、先に受信ウィンドウへ保持する。
    // [状態確認] - 受信ウィンドウへの保持の戻り値が POTR_OK であること。
    // Pre-Assert
    {
        InSequence sequence;
        EXPECT_CALL(mock_porter, potr_internal_flow_control_on_received(&ctx, POTR_PEER_NA, 42U, _, 101U, 1, _))
            .Times(1);
        // [Pre-Assert確認_正常系] - 通番 100 の取り出しの後に、次に受信する通番 101 で 1 回呼ばれること。
        EXPECT_CALL(mock_porter, potr_internal_flow_control_on_received(&ctx, POTR_PEER_NA, 42U, _, 102U, 1, _))
            .Times(1);
        // [Pre-Assert確認_正常系] - 通番 101 の取り出しの後に、次に受信する通番 102 で 1 回呼ばれること。
    }
    // Act
    thread_recv_window_accept_outer(&slot, &fragment.packet, 0); // [手順] - 通番 100 の DATA パケットを受理する。
    // Assert
    EXPECT_EQ(102U, ctx.recv_window.base_seq); // [確認_正常系] - 通番 100 と 101 を取り出したこと。
}
// [サブ手順参照 名前=threadRecvWindowRejectTest.TearDown]

// 新しいセッションを採用した場合に、その相手から受けていた抑制の要求と送出上限を取り消すことの確認
// [サブ手順参照 名前=threadRecvWindowRejectTest.SetUp]
TEST_F(threadRecvWindowRejectTest, new_session_adoption_clears_flow_control_state)
{
    // Arrange
    const PackedDataFixture same_session = makePackedDataPacket(100U, 42U, 1000, 500, "same");
    const PackedDataFixture new_session = makePackedDataPacket(200U, 43U, 1000, 500, "new");
    cplat_atomic_store_u64(&ctx.flow_request_expire_ms, 9999U, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_HELD | 5U, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - 相手から抑制の要求 (期限 9999) と送出上限 5 を受けている状態にする。
    // Pre-Assert
    // Act
    const int adopted_same = thread_recv_session_adopt(&slot, &same_session.packet);
    // [手順] - 同じセッション (識別子 42) のパケットで呼び出す。
    uint64_t limit_after_same = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    const int adopted_new = thread_recv_session_adopt(&slot, &new_session.packet);
    // [手順] - 開始時刻が同じで識別子が大きい新しいセッション (識別子 43) のパケットで呼び出す。
    // Assert
    EXPECT_EQ(1, adopted_same); // [確認_正常系] - 同じセッションを受理すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 5U, limit_after_same); // [確認_正常系] - 同じセッションでは送出上限を保つこと。
    EXPECT_EQ(1, adopted_new);  // [確認_正常系] - 新しいセッションを採用すること。
    EXPECT_EQ(0U, cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE));
    // [確認_正常系] - 新しいセッションの採用で送出上限を取り消すこと。
    EXPECT_EQ(0U, cplat_atomic_load_u64(&ctx.flow_request_expire_ms, CPLAT_MEMORY_ORDER_ACQUIRE));
    // [確認_正常系] - 新しいセッションの採用で抑制の要求を取り消すこと。
}
// [サブ手順参照 名前=threadRecvWindowRejectTest.TearDown]

// 新しいセッションを採用した場合に、その時点で次に割り当てる通番を基準とする「未通知」へ戻ることの確認
// [サブ手順参照 名前=threadRecvWindowRejectTest.SetUp]
TEST_F(threadRecvWindowRejectTest, new_session_adoption_resets_to_unnotified_at_next_seq)
{
    // Arrange
    const PackedDataFixture new_session = makePackedDataPacket(200U, 43U, 1000, 500, "new");
    ctx.send_window.next_seq = 50U;
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNLIMITED, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - 送信ウィンドウの次の通番を 50、送出上限を「上限なし (通知済み)」にする。
    // Pre-Assert
    // Act
    const int adopted_new = thread_recv_session_adopt(&slot, &new_session.packet);
    // [手順] - 新しいセッション (識別子 43) のパケットで呼び出す。
    // Assert
    EXPECT_EQ(1, adopted_new); // [確認_正常系] - 新しいセッションを採用すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 50U,
              cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE));
    // [確認_正常系] - 次の通番 50 を基準とする未通知へ戻ること。
}
// [サブ手順参照 名前=threadRecvWindowRejectTest.TearDown]
