/**
 *******************************************************************************
 *  @file           potrFlowControlN1WidthTest.cc
 *  @brief          N 対 1 サーバーの送信中のピアによる幅の配分と順番の許可を検証します。
 *
 *  OS の受信バッファーの大きさは cplat_socket_get_receive_buffer_size() の mock で与えます。
 *  フロー制御パケットの組み立ては Mock_porter で置き換え、宛先ピアごとの状態と送出上限を記録します。
 *  封緘は実物 (packet_seal.c) を使い、送出は cplat_socket_sendto() の呼び出しで確認します。
 *******************************************************************************
 */
#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/potr_context.h>
#include <porter/potr_flow_control.h>
#include <porter/protocol/packet.h>
#include <cplat/net/byteorder.h>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
using namespace testing;

namespace
{
const uint8_t kSuppress = (uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS;
const uint8_t kResume = (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME;
const uint8_t kQuery = (uint8_t)POTR_FLOW_CONTROL_STATE_QUERY;
// max_payload=1400 の 1 パケットの見積もり = 2 * (72 + 1400 + 16) + 2048 = 5024 [byte]
const size_t kPacketEstimateBytes = 5024U;
const uint32_t kPeerSessionBase = 0x100U; // ピアごとの相手セッション識別子の基準値

struct SentRecord
{
    potr_peer_id peer_id;
    uint32_t send_limit;
    uint8_t state;
    uint8_t _pad[3];
};
} // namespace

class potrFlowControlN1WidthTest : public Test
{
  protected:
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx;
    uint64_t now_ms = 1000U;
    size_t buffer_size = 212992U; // 既定は Linux 相当 (全体の幅 42)
    int sendto_count = 0;
    int sendto_result = CPLAT_OK;
    std::vector<SentRecord> records;
    std::map<potr_peer_id, std::unique_ptr<potr_internal_peer_context>> peers;
    std::map<uint32_t, potr_peer_id> session_to_peer;
    cplat_timespec sender_ts = {};

    // [サブ手順 名前=potrFlowControlN1WidthTest.SetUp]
    void SetUp() override
    {
        memset(&ctx, 0, sizeof(ctx));
        ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
        ctx.service.service_id = 1;
        ctx.role = POTR_ROLE_RECEIVER;
        ctx.is_multi_peer = 1;
        ctx.n_path = 1;
        ctx.sock[0] = 5;
        ctx.flow_control_config.capacity = 0U;
        ctx.flow_control_config.suppress_percent = 80U;
        ctx.flow_control_config.resume_percent = 60U;
        ctx.flow_control_config.request_interval_ms = 200U;
        ctx.flow_control_config.request_lifetime_ms = 1000U;
        ctx.flow_control_config.receive_buffer_percent = 100U;
        ctx.global.max_payload = 1400U;
        sender_ts.tv_sec = 100;
        sender_ts.tv_nsec = 5;
        cplat_local_lock_create(&ctx.peers_mutex);
        // [状態] - N:1 サーバー、容量 0、受信バッファー割合 100%、再送周期 200 [ms]、有効期限 1000 [ms]、max_payload 1400 とする。

        ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([this](uint64_t *out) {
            *out = now_ms;
            return CPLAT_OK;
        }); // [状態] - 現在時刻を now_ms で制御する。

        ON_CALL(mock_cplat, cplat_socket_get_receive_buffer_size(_, _, _))
            .WillByDefault([this](cplat_socket, size_t *size_out, cplat_error *) {
                *size_out = buffer_size;
                return CPLAT_OK;
            }); // [状態] - OS の受信バッファーの大きさを buffer_size で返す。

        ON_CALL(mock_cplat, cplat_socket_sendto(_, _, _, _, _, _))
            .WillByDefault([this](cplat_socket, const void *, size_t len, const cplat_ipv4_endpoint *,
                                  size_t *sent, cplat_error *) {
                sendto_count++;
                if (sent != nullptr)
                {
                    *sent = len;
                }
                return sendto_result;
            }); // [状態] - ソケット送出の戻り値を制御し、送出回数を数える。

        ON_CALL(mock_porter, potr_internal_packet_build_flow_control(_, _, _, _, _))
            .WillByDefault([this](potr_packet *packet, const potr_internal_packet_session_hdr *shdr, uint8_t *payload,
                                  uint8_t state, uint32_t send_limit) {
                potr_peer_id target = POTR_PEER_NA;
                auto it = session_to_peer.find(shdr->session_id);
                if (it != session_to_peer.end())
                {
                    target = it->second;
                }
                records.push_back(SentRecord{target, send_limit, state, {0U, 0U, 0U}});
                memset(payload, 0, POTR_FLOW_CONTROL_PAYLOAD_SIZE);
                payload[0] = state;
                memset(packet, 0, POTR_PACKET_HEADER_SIZE);
                packet->payload = payload;
                packet->payload_len = cplat_hton16((uint16_t)POTR_FLOW_CONTROL_PAYLOAD_SIZE);
                return POTR_OK;
            }); // [状態] - 宛先ピア・状態・送出上限を記録し、封緘できる最小のパケットを組み立てる。

        ON_CALL(mock_porter, potr_internal_peer_find_by_id(_, _))
            .WillByDefault([this](const potr_context *, potr_peer_id id) -> potr_internal_peer_context * {
                auto it = peers.find(id);
                if (it != peers.end())
                {
                    return it->second.get();
                }
                return nullptr;
            }); // [状態] - 登録済みのピアを peers から返す。
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrFlowControlN1WidthTest.TearDown]
    void TearDown() override
    {
        potr_internal_flow_control_stop(&ctx);
        if (ctx.peers_mutex != nullptr)
        {
            cplat_local_lock_dispose(ctx.peers_mutex);
            ctx.peers_mutex = nullptr;
        }
        peers.clear();
        session_to_peer.clear();
    }
    // [サブ手順終了]

    // ピアを登録する。
    potr_internal_peer_context *add_peer(potr_peer_id peer_id)
    {
        auto p = std::make_unique<potr_internal_peer_context>();
        memset(p.get(), 0, sizeof(*p));
        p->peer_id = peer_id;
        p->active = 1;
        p->session_id = 0x1000U + peer_id;
        p->peer_session_known = 1;
        p->peer_session_id = kPeerSessionBase + peer_id;
        p->peer_session_ts = sender_ts;
        p->dest_addr[0].address = 0x0A00007FU + peer_id;
        p->dest_addr[0].port = cplat_hton16((uint16_t)(7000U + peer_id));
        potr_internal_peer_context *raw = p.get();
        session_to_peer[p->session_id] = peer_id;
        peers[peer_id] = std::move(p);
        ctx.n_peers = (int)peers.size();
        return raw;
    }

    // ピアを削除し、解放通知を行う。
    void free_peer(potr_peer_id peer_id)
    {
        cplat_local_lock_lock(ctx.peers_mutex);
        auto it = peers.find(peer_id);
        if (it != peers.end())
        {
            session_to_peer.erase(it->second->session_id);
            peers.erase(it);
            ctx.n_peers = (int)peers.size();
        }
        potr_internal_flow_control_on_peer_freed(&ctx, peer_id);
        cplat_local_lock_unlock(ctx.peers_mutex);
    }

    void received(potr_peer_id peer_id, uint32_t next_seq, int data_packet = 1)
    {
        cplat_local_lock_lock(ctx.peers_mutex);
        potr_internal_flow_control_on_received(&ctx, peer_id, kPeerSessionBase + peer_id, &sender_ts, next_seq,
                                               data_packet, 0U);
        cplat_local_lock_unlock(ctx.peers_mutex);
    }

    int admit(potr_peer_id peer_id, size_t len)
    {
        cplat_local_lock_lock(ctx.peers_mutex);
        int ret = potr_internal_flow_control_admit(&ctx, peer_id, kPeerSessionBase + peer_id, &sender_ts, len);
        cplat_local_lock_unlock(ctx.peers_mutex);
        return ret;
    }

    void send_query(potr_peer_id peer_id)
    {
        uint8_t payload[POTR_FLOW_CONTROL_QUERY_PAYLOAD_SIZE] = {kQuery};
        potr_packet pkt = {};
        pkt.flags = POTR_FLAG_FLOW_CONTROL;
        pkt.session_id = kPeerSessionBase + peer_id;
        pkt.session_tv_sec = (int64_t)sender_ts.tv_sec;
        pkt.session_tv_nsec = (int32_t)sender_ts.tv_nsec;
        pkt.payload = payload;
        pkt.payload_len = POTR_FLOW_CONTROL_QUERY_PAYLOAD_SIZE;

        cplat_local_lock_lock(ctx.peers_mutex);
        potr_internal_peer_context *peer = peers.count(peer_id) ? peers[peer_id].get() : nullptr;
        potr_internal_flow_control_on_packet(&ctx, peer, &pkt);
        cplat_local_lock_unlock(ctx.peers_mutex);
    }

    int piggyback(potr_peer_id peer_id, uint32_t *limit_out)
    {
        cplat_local_lock_lock(ctx.peers_mutex);
        int ret = potr_internal_flow_control_piggyback_limit(&ctx, peer_id, limit_out);
        if (ret != 0)
        {
            potr_internal_flow_control_piggyback_complete(&ctx, peer_id, *limit_out, 1);
        }
        cplat_local_lock_unlock(ctx.peers_mutex);
        return ret;
    }

    void clear_records()
    {
        records.clear();
        sendto_count = 0;
    }
};

// 送信中のピア数 (0、1、2、3) に応じてピアごとの幅が決まり、DATA と問い合わせの両方が数えられ、
// 期限切れと切断で数が減ることの確認
// [サブ手順参照 名前=potrFlowControlN1WidthTest.SetUp]
TEST_F(potrFlowControlN1WidthTest, active_senders_width_query_expiry_and_disconnect)
{
    // Arrange
    add_peer(1U);
    add_peer(2U);
    add_peer(3U);
    add_peer(4U);
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 全体の幅 42、接続中のピア 4 台で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    // Pre-Assert
    // Act
    received(1U, 10U, 0); // [手順] - ピア 1 が PING だけを受信する (送信中 0 -> 幅 42)。
    received(1U, 11U, 1); // [手順] - ピア 1 が DATA を受信する (送信中 1)。
    received(2U, 10U, 0); // [手順] - ピア 2 が PING だけを受信する (送信中 1 -> 幅 42)。
    received(2U, 11U, 1); // [手順] - ピア 2 が DATA を受信する (送信中 2)。
    received(3U, 10U, 0); // [手順] - ピア 3 が PING を受信する (送信中 2 -> 幅 21)。
    clear_records();
    send_query(3U); // [手順] - ピア 3 が問い合わせを送る (DATA 未送信だが送信中 3 -> 幅 14)。
    uint32_t limit_p3_query = records.back().send_limit;
    clear_records();
    received(4U, 10U, 1); // [手順] - ピア 4 が DATA を受信する (送信中 4 -> 幅 10)。
    uint32_t limit_p4_active4 = records.back().send_limit;
    clear_records();
    now_ms = 1200U;
    potr_internal_flow_control_tick(&ctx, now_ms); // [手順] - request_interval_ms (200) 経過後に tick を呼ぶ (全ピア期限切れ -> 送信中 0)。
    received(1U, 60U, 1); // [手順] - ピア 1 だけが DATA を受信する (送信中 1 -> 幅 42)。
    uint32_t limit_p1_after_expiry = records.back().send_limit;
    clear_records();
    received(2U, 60U, 1); // [手順] - ピア 2 も DATA を受信する (送信中 2 -> 幅 21)。
    uint32_t limit_p2_active2 = records.back().send_limit;
    free_peer(2U); // [手順] - ピア 2 を切断する (送信中 1 に減る)。
    clear_records();
    send_query(1U); // [手順] - ピア 1 が問い合わせを送る (送信中 1 -> 幅 42)。
    uint32_t limit_p1_after_disconnect = records.back().send_limit;
    // Assert
    EXPECT_EQ(31U, limit_p3_query);
    // [確認_正常系] - ピア 3 の問い合わせで送信中 3 となり幅 14、すでに知らせた 31 (10 + 21) を戻さないこと。
    EXPECT_EQ(20U, limit_p4_active4);
    // [確認_正常系] - 送信中 4 のピア 4 の最初の通知が 10 + 42 ÷ 4 = 20 であること。
    EXPECT_EQ(102U, limit_p1_after_expiry);
    // [確認_正常系] - 期限切れ後の送信中 1 でピア 1 の送出上限が 60 + 42 = 102 であること。
    EXPECT_EQ(81U, limit_p2_active2);
    // [確認_正常系] - 送信中 2 でピア 2 の送出上限が 60 + 21 = 81 であること。
    EXPECT_EQ(102U, limit_p1_after_disconnect);
    // [確認_正常系] - ピア 2 切断後の送信中 1 でピア 1 の送出上限が 60 + 42 = 102 であること。
}
// [サブ手順参照 名前=potrFlowControlN1WidthTest.TearDown]

// 送信中のピア数が増えてピアごとの幅が小さくなっても、すでに知らせた送出上限を戻さないことの確認
// [サブ手順参照 名前=potrFlowControlN1WidthTest.SetUp]
TEST_F(potrFlowControlN1WidthTest, increasing_active_senders_never_shrinks_notified_limit)
{
    // Arrange
    for (potr_peer_id id = 1U; id <= 5U; id++)
    {
        add_peer(id);
    }
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 全体の幅 42、ピア 5 台で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    received(1U, 10U, 1); // [状態] - ピア 1 が送信中 1 で送出上限 52 (10 + 42) を受ける。
    for (potr_peer_id id = 2U; id <= 5U; id++)
    {
        received(id, 10U, 1);
    }
    // [状態] - ピア 2〜5 も DATA を受信し、送信中 5 (ピアごとの幅 8) にする。
    clear_records();
    // Pre-Assert
    // Act
    uint32_t piggyback_limit = 0U;
    int has_piggyback = piggyback(1U, &piggyback_limit); // [手順] - ピア 1 へ DATA 相乗りを取得する。
    send_query(1U);                                       // [手順] - ピア 1 から問い合わせを受ける。
    received(1U, 31U, 1); // [手順] - ピア 1 が範囲の半分 (10->31、残り 21 > 幅 8) を受信する。
    size_t count_at_half = records.size();
    received(1U, 45U, 1); // [手順] - ピア 1 が残り 7 (< 幅 8) の通番 45 を受信する (45 + 8 = 53 > 52)。
    // Assert
    EXPECT_EQ(1, has_piggyback);  // [確認_正常系] - 相乗り送出上限が取得できること。
    EXPECT_EQ(52U, piggyback_limit); // [確認_正常系] - 相乗りで 10 + 8 (18) へ戻さず 52 を維持すること。
    ASSERT_EQ(2U, records.size());   // [確認_正常系] - 問い合わせ応答と、上限が進む 45 での更新の 2 回送出すること。
    EXPECT_EQ(1U, count_at_half);    // [確認_正常系] - 上限が進まない 31 では重複更新を送出しないこと。
    EXPECT_EQ(52U, records[0].send_limit); // [確認_正常系] - 問い合わせ応答でも 52 を戻さないこと。
    EXPECT_EQ(53U, records[1].send_limit); // [確認_正常系] - 残りより幅が大きくなった 45 で 45 + 8 = 53 へ進むこと。
}
// [サブ手順参照 名前=potrFlowControlN1WidthTest.TearDown]

// ピアごとの幅が 4 未満の順番の許可で、枠の上限、残りの許可数、待ちの許可数 0、
// 最も長く待ったピア (未受領優先・管理表の順) への割り当てを確かめる確認
// [サブ手順参照 名前=potrFlowControlN1WidthTest.SetUp]
TEST_F(potrFlowControlN1WidthTest, turn_mode_slot_limit_remaining_and_fair_order)
{
    // Arrange
    buffer_size = kPacketEstimateBytes * 8U; // 全体の幅 8 -> 同時枠数 = 8 ÷ 4 = 2
    for (potr_peer_id id = 1U; id <= 4U; id++)
    {
        add_peer(id);
    }
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 全体の幅 8 (同時枠数 2)、ピア 1〜4 で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    // Pre-Assert
    // Act
    received(1U, 10U, 1); // [手順] - ピア 1: 送信中 1 (通常、幅 8) -> limit 18 (枠 1)。
    received(2U, 20U, 1); // [手順] - ピア 2: 送信中 2 (通常、幅 4) -> limit 24 (枠 2)。
    received(3U, 30U, 1); // [手順] - ピア 3: 送信中 3 (幅 2 < 4、順番の許可へ切替)。枠 2/2 満杯 -> limit 30 (許可数 0)。
    received(4U, 40U, 1); // [手順] - ピア 4: 送信中 4 (順番の許可)。枠 2/2 満杯 -> limit 40 (許可数 0)。
    std::vector<SentRecord> initial_records = records;
    clear_records();

    // 許可を持つピア 2 が途中 (22、残り 2) を受信しても 4 を足さず、相乗りと問い合わせが残り (24) を返すこと
    received(2U, 22U, 1);
    size_t count_mid_data = records.size();
    uint32_t pb_p2 = 0U;
    uint32_t pb_p3 = 0U;
    int has_pb_p2 = piggyback(2U, &pb_p2);
    int has_pb_p3 = piggyback(3U, &pb_p3);
    send_query(2U); // ピア 2 へ問い合わせ応答 -> 24 (残り 2)
    send_query(3U); // 待っているピア 3 へ問い合わせ応答 -> 30 (進めない)
    std::vector<SentRecord> mid_records = records;
    clear_records();

    // ピア 2 が許可を使い終える (24) -> 未受領のピア 3 と 4 のうち管理表の順でピア 3 へ 30 + 4 = 34
    received(2U, 24U, 1);
    // ピア 1 が許可を使い終える (18) -> 未受領のピア 4 へ 40 + 4 = 44
    received(1U, 18U, 1);
    // ピア 3 が許可を使い終える (34) -> 待機中はピア 1 と 2 (2 が先に許可を受けたが 1 はもっと先に受けた: 1 の方が古い) -> ピア 1 へ 18 + 4 = 22
    received(3U, 34U, 1);
    // ピア 4 が許可を使い終える (44) -> 待機中はピア 2 と 3 (2 の方が古い) -> ピア 2 へ 24 + 4 = 28
    received(4U, 44U, 1);

    // Assert
    ASSERT_EQ(4U, initial_records.size()); // [確認_正常系] - 初期 4 ピアにそれぞれ 1 回ずつ通知すること。
    EXPECT_EQ(18U, initial_records[0].send_limit); // [確認_正常系] - ピア 1 が 10 + 8 = 18 であること。
    EXPECT_EQ(24U, initial_records[1].send_limit); // [確認_正常系] - ピア 2 が 20 + 4 = 24 であること。
    EXPECT_EQ(30U, initial_records[2].send_limit); // [確認_正常系] - 順番切替時のピア 3 が許可数 0 (30) であること。
    EXPECT_EQ(40U, initial_records[3].send_limit); // [確認_正常系] - 待っているピア 4 が許可数 0 (40) であること。

    EXPECT_EQ(0U, count_mid_data); // [確認_正常系] - 許可の途中で半分到達による延長を送出しないこと。
    EXPECT_EQ(1, has_pb_p2);       // [確認_正常系] - ピア 2 の相乗りが取得できること。
    EXPECT_EQ(24U, pb_p2);         // [確認_正常系] - ピア 2 の相乗りが残りの許可数 (22 + 2 = 24) であること。
    EXPECT_EQ(1, has_pb_p3);       // [確認_正常系] - 待っているピア 3 の相乗りが取得できること。
    EXPECT_EQ(30U, pb_p3);         // [確認_正常系] - 待っているピア 3 の相乗りが許可数 0 (30) であること。
    ASSERT_EQ(2U, mid_records.size()); // [確認_正常系] - 問い合わせ応答が 2 件であること。
    EXPECT_EQ(24U, mid_records[0].send_limit); // [確認_正常系] - ピア 2 の問い合わせ応答が 24 (残り 2) であること。
    EXPECT_EQ(30U, mid_records[1].send_limit); // [確認_正常系] - 待っているピア 3 の問い合わせ応答が 30 (進めない) であること。

    ASSERT_EQ(4U, records.size()); // [確認_正常系] - 使い終えた 4 回で順番の許可が 4 回送出されること。
    EXPECT_EQ(3U, records[0].peer_id);
    EXPECT_EQ(34U, records[0].send_limit); // [確認_正常系] - 1 回目は未受領・管理表順でピア 3 へ 30 + 4 = 34 であること。
    EXPECT_EQ(4U, records[1].peer_id);
    EXPECT_EQ(44U, records[1].send_limit); // [確認_正常系] - 2 回目は未受領のピア 4 へ 40 + 4 = 44 であること。
    EXPECT_EQ(1U, records[2].peer_id);
    EXPECT_EQ(22U, records[2].send_limit); // [確認_正常系] - 3 回目は最古受領のピア 1 へ 18 + 4 = 22 であること。
    EXPECT_EQ(2U, records[3].peer_id);
    EXPECT_EQ(28U, records[3].send_limit); // [確認_正常系] - 4 回目は次に古いピア 2 へ 24 + 4 = 28 であること。
}
// [サブ手順参照 名前=potrFlowControlN1WidthTest.TearDown]

// 順番の許可では、送信をやめたピアの未使用許可も数え続け、切断した時点で空き分を渡すことの確認
// [サブ手順参照 名前=potrFlowControlN1WidthTest.SetUp]
TEST_F(potrFlowControlN1WidthTest, turn_mode_idle_holder_keeps_budget_until_disconnect)
{
    // Arrange
    buffer_size = kPacketEstimateBytes * 2U; // 計算幅 2 -> 全体の幅の下限 4 -> 同時枠数 = 4 ÷ 4 = 1
    add_peer(1U);
    add_peer(2U);
    add_peer(3U);
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 計算幅 2 (全体の幅の下限 4、同時枠数 1)、ピア 1〜3 で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    now_ms = 1000U;
    received(1U, 10U, 1); // [状態] - ピア 1 が送信中 1 (幅 4) で limit 14 (枠 1/1) を受ける。
    now_ms = 1100U;
    received(2U, 20U, 1); // [状態] - ピア 2 が送信中 2 (幅 2 < 4、順番の許可)。枠満杯 -> limit 20。
    received(3U, 30U, 1); // [状態] - ピア 3 が送信中 3 (順番の許可)。枠満杯 -> limit 30。
    clear_records();
    // Pre-Assert
    // Act
    now_ms = 1200U;
    potr_internal_flow_control_tick(&ctx, now_ms);
    // [手順] - 時刻 1200 で tick を呼ぶ。ピア 1 (1000+200<=1200) が送信中でなくなり枠が空く (送信中 2、順番の許可継続)。
    std::vector<SentRecord> after_tick = records;
    clear_records();
    now_ms = 1250U;
    send_query(2U); // ピア 2 の活動時刻を更新
    send_query(3U); // ピア 3 の活動時刻を更新
    clear_records();
    free_peer(1U); // [手順] - 送信中でない許可保持者を切断し、空いた 4 パケットをピア 2 へ渡す。
    std::vector<SentRecord> after_free_holder = records;
    clear_records();
    free_peer(2U); // [手順] - 許可を持つピア 2 を切断する (送信中 1 -> 通常モードへ戻る)。
    std::vector<SentRecord> after_free_to_normal = records;
    send_query(3U); // [手順] - 通常モードへ戻った後にピア 3 から問い合わせを受ける (幅 4 -> limit 34)。
    // Assert
    EXPECT_TRUE(after_tick.empty()); // [確認_正常系] - 送信中でなくなっても未使用許可を再利用しないこと。
    ASSERT_EQ(1U, after_free_holder.size()); // [確認_正常系] - 保持者の切断で次の待機ピアへ許可すること。
    EXPECT_EQ(2U, after_free_holder[0].peer_id); // [確認_正常系] - 最も長く待ったピア 2 を選ぶこと。
    EXPECT_EQ(24U, after_free_holder[0].send_limit); // [確認_正常系] - ピア 2 へ 20 + 4 = 24 を知らせること。
    EXPECT_TRUE(after_free_to_normal.empty());
    // [確認_正常系] - 順番の許可から通常の配分へ戻った時点では即時に一斉送出せず次の契機を待つこと。
    ASSERT_EQ(1U, records.size());
    EXPECT_EQ(3U, records[0].peer_id);
    EXPECT_EQ(34U, records[0].send_limit); // [確認_正常系] - 通常の配分へ戻った後の問い合わせで 30 + 4 = 34 が返ること。
}
// [サブ手順参照 名前=potrFlowControlN1WidthTest.TearDown]

// 順番の許可で切断後にまだ順番の許可が続く場合に、空いた枠が即座に次の待っているピアへ割り当てられることの確認
// [サブ手順参照 名前=potrFlowControlN1WidthTest.SetUp]
TEST_F(potrFlowControlN1WidthTest, turn_mode_disconnect_immediately_grants_next_waiting_peer)
{
    // Arrange
    buffer_size = kPacketEstimateBytes * 4U; // 全体の幅 4 -> 同時枠数 1
    add_peer(1U);
    add_peer(2U);
    add_peer(3U);
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 全体の幅 4 (同時枠数 1)、ピア 1〜3 で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    received(1U, 10U, 1); // 送信中 1 -> limit 14 (枠 1/1)
    received(2U, 20U, 1); // 送信中 2 (順番) -> limit 20 (待ち)
    received(3U, 30U, 1); // 送信中 3 (順番) -> limit 30 (待ち)
    clear_records();
    // Pre-Assert
    // Act
    free_peer(1U); // [手順] - 許可を持つピア 1 を切断する (送信中 2 -> 順番の許可が継続、枠 0/1)。
    // Assert
    ASSERT_EQ(1U, records.size()); // [確認_正常系] - 切断で空いた枠へ即座に 1 件の許可が送出されること。
    EXPECT_EQ(2U, records[0].peer_id);
    EXPECT_EQ(24U, records[0].send_limit); // [確認_正常系] - 管理表の順でピア 2 へ 20 + 4 = 24 が知らされること。
}
// [サブ手順参照 名前=potrFlowControlN1WidthTest.TearDown]

// 通常の配分から順番の許可へ切り替わった際に枠を超過している間は新規許可を出さず、
// 順番の許可から通常の配分へ戻った際に周期処理で待っていたピアへ通常の配分を知らせることの確認
// [サブ手順参照 名前=potrFlowControlN1WidthTest.SetUp]
TEST_F(potrFlowControlN1WidthTest, mode_switch_over_quota_and_return_to_normal_via_tick)
{
    // Arrange
    buffer_size = kPacketEstimateBytes * 8U; // 全体の幅 8 -> 同時枠数 2
    add_peer(1U);
    add_peer(2U);
    add_peer(3U);
    add_peer(4U);
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 全体の幅 8 (同時枠数 2)、ピア 1〜4 で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    now_ms = 1000U;
    received(1U, 10U, 1); // 通常 (幅 8): limit 18
    received(2U, 20U, 1); // 通常 (幅 4): limit 24
    // ピア 3 が PING だけで通常モード (幅 4) の最初の通知を受けてから DATA を送るケース (枠超過 3 > 2 を作る)
    received(3U, 30U, 0); // 通常 (送信中 2、幅 4): limit 34
    now_ms = 1100U;
    received(3U, 31U, 1); // 送信中 3 (幅 2 < 4、順番切替): 1, 2, 3 の 3 台が許可を持つ (枠超過 3/2)
    received(4U, 40U, 1); // 送信中 4 (順番): limit 40 (待ち)
    clear_records();
    // Pre-Assert
    // Act
    received(2U, 24U, 1); // [手順] - ピア 2 が使い終える (残り枠 2/2: まだ枠上限のためピア 4 へは出さない)。
    size_t count_while_full = records.size();
    received(3U, 34U, 1); // [手順] - ピア 3 が使い終える (残り枠 1/2 -> 空き 1 ができてピア 4 へ 40 + 4 = 44)。
    std::vector<SentRecord> after_p3_done = records;
    clear_records();
    received(1U, 14U, 1); // [手順] - 旧許可を 4 パケットまで使い、合計を全体の幅より小さくする。
    send_query(4U); // [手順] - 問い合わせで空き 4 パケットを最も長く待ったピア 4 へ割り当てる。
    std::vector<SentRecord> after_query = records;
    clear_records();
    // 次に、ピア 1 と 2 が期限切れになり送信中 2 (ピア 3, 4) へ減って通常モードへ戻るケース
    now_ms = 1200U; // ピア 1 (1000) は期限切れ、ピア 2 (1100) は 1200 でまだ有効、3/4 (1100) も有効
    received(4U, 44U, 1); // ピア 4 が使い終える (時刻 1200、送信中 3、枠が空く -> ピア 3 へ 34 + 4 = 38)
    clear_records();
    now_ms = 1350U; // ピア 2 (1100+200<=1350) とピア 3 (1100+200<=1350) が期限切れ -> 送信中 1 (ピア 4 のみ、通常モードへ復帰)
    potr_internal_flow_control_tick(&ctx, now_ms);
    // [手順] - tick で期限切れを落とし、通常モード (幅 8) へ戻す。待っていたピア 4 へ通常配分が知らされる。
    // Assert
    EXPECT_EQ(0U, count_while_full); // [確認_正常系] - 枠超過から上限ちょうどへ減った段階では新規許可を出さないこと。
    EXPECT_TRUE(after_p3_done.empty()); // [確認_正常系] - 旧許可の合計が全体の幅と等しい間は新しい許可を出さないこと。
    ASSERT_EQ(1U, after_query.size()); // [確認_正常系] - 空きができた後の問い合わせで 1 回許可すること。
    EXPECT_EQ(4U, after_query[0].peer_id); // [確認_正常系] - 未受領のピア 4 を先に選ぶこと。
    EXPECT_EQ(44U, after_query[0].send_limit); // [確認_正常系] - 空き分 4 パケットを割り当てること。
    ASSERT_EQ(1U, records.size());
    EXPECT_EQ(4U, records[0].peer_id);
    EXPECT_EQ(52U, records[0].send_limit); // [確認_正常系] - 通常モード復帰後の tick で待っていたピア 4 へ 44 + 8 = 52 が出ること。
}
// [サブ手順参照 名前=potrFlowControlN1WidthTest.TearDown]

// 順番の許可で、接続直後の未通知ピアが初期の幅で送出した後続 DATA が届いても誤って枠を空けず、
// 容量設定がある場合は 4 と占有量による許可数の小さいほうを使うことの確認
// [サブ手順参照 名前=potrFlowControlN1WidthTest.SetUp]
TEST_F(potrFlowControlN1WidthTest, turn_mode_initial_width_data_and_capacity_limit)
{
    // Arrange
    buffer_size = kPacketEstimateBytes * 4U; // 全体の幅 4 -> 同時枠数 1
    ctx.flow_control_config.capacity = 10000U;
    ctx.flow_control_config.suppress_percent = 80U; // 要求のしきい値 = 8000
    ctx.flow_control_config.resume_percent = 50U;
    ctx.global.max_payload = 1400U;
    add_peer(1U);
    add_peer(2U);
    add_peer(3U);
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 全体の幅 4 (同時枠数 1)、容量 10000 (しきい値 8000)、max_payload 1400、ピア 1〜3 で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    received(1U, 1U, 1); // ピア 1: 送信中 1 (通常、幅 4) -> limit 5 (枠 1/1)
    received(2U, 1U, 1); // ピア 2: 送信中 2 (順番)、枠満杯 -> 最初の通知 limit 1 (許可数 0)
    received(3U, 1U, 1); // ピア 3: 送信中 3 (順番)、枠満杯 -> 最初の通知 limit 1 (許可数 0)
    // ピア 2 の占有量を 6000 にして、占有量による許可数を (8000 - 6000) / 1400 + 1 = 2 (< 4) にする。
    ASSERT_EQ(POTR_OK, admit(2U, 6000U));
    clear_records();
    // Pre-Assert
    // Act
    received(2U, 2U, 1); // [手順] - ピア 2 が初期の幅で送出済みだった後続 DATA (通番 1 -> next_seq 2) を受信する。
    received(2U, 3U, 1); // [手順] - 同様に next_seq 3 を受信する。
    size_t count_after_initial_burst = records.size();
    received(1U, 5U, 1); // [手順] - 許可を持つピア 1 が使い終える -> ピア 2 へ順番の許可 (min(4, 2) = 2)。
    // Assert
    EXPECT_EQ(0U, count_after_initial_burst);
    // [確認_正常系] - 許可を持たないピア 2 の初期の幅の残りの DATA で、他ピアへ誤って許可を出さないこと。
    ASSERT_EQ(2U, records.size()); // [確認_正常系] - 容量による許可が小さい場合、残りの枠を次のピアへ割り当てること。
    EXPECT_EQ(2U, records[0].peer_id);
    EXPECT_EQ(kResume, records[0].state);
    EXPECT_EQ(5U, records[0].send_limit);
    // [確認_正常系] - 容量設定ありのピア 2 へ 3 + min(4, 2) = 5 の許可が出ること。
    EXPECT_EQ(3U, records[1].peer_id); // [確認_正常系] - もう 1 台の未受領ピアへ残りを渡すこと。
    EXPECT_EQ(3U, records[1].send_limit); // [確認_正常系] - ピア 3 の許可は残り枠の 2 パケットまでであること。
    EXPECT_EQ(4U, (records[0].send_limit - 3U) + (records[1].send_limit - 1U));
    // [確認_正常系] - 未使用許可の合計が全体の幅 4 を超えないこと。
}
// [サブ手順参照 名前=potrFlowControlN1WidthTest.TearDown]

// OS の受信バッファーに基づく送出上限を使わない構成 (receive_buffer_used == 0) では、
// 送信中のピア数による配分と順番の許可を行わないことの確認
// [サブ手順参照 名前=potrFlowControlN1WidthTest.SetUp]
TEST_F(potrFlowControlN1WidthTest, disabled_receive_buffer_skips_n1_distribution)
{
    // Arrange
    ctx.flow_control_config.capacity = 20000U;
    ctx.flow_control_config.suppress_percent = 80U; // しきい値 16000 -> 16000 / 1400 + 1 = 12
    ctx.flow_control_config.resume_percent = 50U;
    ctx.flow_control_config.receive_buffer_percent = 0U;
    for (potr_peer_id id = 1U; id <= 5U; id++)
    {
        add_peer(id);
    }
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - N:1 サーバーで容量 20000、receive_buffer_percent 0 (receive_buffer_used == 0)、ピア 5 台で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    // Pre-Assert
    // Act
    for (potr_peer_id id = 1U; id <= 5U; id++)
    {
        received(id, 10U, 1);
    }
    // [手順] - 5 台すべてから DATA を受信する。
    // Assert
    ASSERT_EQ(5U, records.size()); // [確認_正常系] - 5 台すべてに最初の通知を送出すること。
    for (size_t i = 0U; i < 5U; i++)
    {
        EXPECT_EQ(22U, records[i].send_limit);
        // [確認_正常系] - 送信中のピア数で割らず、全ピアに占有量による許可数 10 + 12 = 22 を返すこと (5 回)。
    }
}
// [サブ手順参照 名前=potrFlowControlN1WidthTest.TearDown]

// 順番の許可で、枠に空きがある状態で新しいピアが問い合わせまたは DATA で送信中になった場合に、
// その場で 4 パケットの許可を 1 回だけ送出することの確認
// [サブ手順参照 名前=potrFlowControlN1WidthTest.SetUp]
TEST_F(potrFlowControlN1WidthTest, turn_mode_newly_active_peer_gets_grant_when_slot_available)
{
    // Arrange
    buffer_size = kPacketEstimateBytes * 8U; // 全体の幅 8 -> 同時枠数 2
    for (potr_peer_id id = 1U; id <= 4U; id++)
    {
        add_peer(id);
    }
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 全体の幅 8 (同時枠数 2)、ピア 1〜4 で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    now_ms = 1000U;
    received(1U, 10U, 1); // 送信中 1 (通常、幅 8): limit 18 (枠 1)
    received(2U, 20U, 1); // 送信中 2 (通常、幅 4): limit 24 (枠 2)
    received(3U, 30U, 1); // 送信中 3 (順番): limit 30 (待ち)
    received(4U, 40U, 0); // ピア 4 は PING のみ (送信中でない): limit 40 (許可数 0)
    // ピア 1 と 2 が両方使い終える -> ピア 3 に許可 (34) が出るが、枠は 1/2 で 1 枠空く
    // (ピア 1 と 2 が使い終えるとピア 3 が枠 1、ピア 1 が枠 2 を取るので、ピア 1 を切断して枠を 1/2 にする)
    received(1U, 18U, 1); // ピア 1 使い終え -> ピア 3 へ 34 (枠: 2, 3)
    free_peer(2U);        // ピア 2 切断 (送信中 2: ピア 1, 3 -> 通常へ戻る前にピア 4 を追加するため先にピア 5 を入れる)
    add_peer(5U);
    received(5U, 50U, 1); // 送信中 3 (ピア 1, 3, 5、順番継続): 枠 1/2 (ピア 3 のみ) -> ピア 5 へ即座に 54 (枠 2/2)
    free_peer(5U);        // ピア 5 切断 -> 送信中 2 (ピア 1, 3)
    // ピア 1, 3 は送信中、枠はピア 3 のみ (1/2)。ピア 4 が問い合わせで送信中 3 (順番モード) になると枠 1/2 で空き 1！
    clear_records();
    // Pre-Assert
    // Act
    send_query(4U); // [手順] - ピア 4 が問い合わせを送り送信中 3 (順番モード、空き枠 1) になる。
    // Assert
    ASSERT_EQ(1U, records.size()); // [確認_正常系] - 応答と許可を重ねず、1 回だけ送出すること。
    EXPECT_EQ(4U, records[0].peer_id);
    EXPECT_EQ(kResume, records[0].state);
    EXPECT_EQ(44U, records[0].send_limit); // [確認_正常系] - 空き枠に対してピア 4 へ 40 + 4 = 44 が送出されること。
}
// [サブ手順参照 名前=potrFlowControlN1WidthTest.TearDown]

// 許可を使い終えた時点の送出失敗後、query で待機中の送信中ピアへ再割り当てすることの確認
// [サブ手順参照 名前=potrFlowControlN1WidthTest.SetUp]
TEST_F(potrFlowControlN1WidthTest, failed_turn_grant_retries_on_query)
{
    // Arrange
    buffer_size = kPacketEstimateBytes * 2U;
    add_peer(1U);
    add_peer(2U);
    add_peer(3U);
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx)); // [状態確認] - 開始に成功すること。
    received(1U, 10U, 1);
    received(2U, 20U, 1);
    received(3U, 30U, 1);
    // [状態] - 全体幅 4、枠 1。ピア 1 が許可 14 を持ち、2 と 3 は許可数 0 で待つ。
    sendto_result = CPLAT_ERR_UNKNOWN;
    // Pre-Assert
    // Act
    received(1U, 14U, 1); // [手順] - 最後の許可を使い終えた際の割り当ての送出をすべて失敗させる。
    uint32_t failed_limit = 0U;
    int failed_held = piggyback(2U, &failed_limit);
    sendto_result = CPLAT_OK;
    clear_records();
    now_ms += 10U;
    send_query(2U); // [手順] - 活動期限が来る前に query を受理し、送出は成功させる。
    uint32_t recovered_limit = 0U;
    int recovered_held = piggyback(2U, &recovered_limit);
    // Assert
    EXPECT_EQ(1, failed_held); // [確認_異常系] - 失敗後も既存の通知状態を保持すること。
    EXPECT_EQ(20U, failed_limit); // [確認_異常系] - 送出失敗ではピア 2 の許可が進まないこと。
    EXPECT_EQ(1, recovered_held); // [確認_正常系] - 再試行後に相乗りの上限を取得できること。
    EXPECT_EQ(24U, recovered_limit); // [確認_正常系] - ピア 2 に 4 DATA の許可が出ること。
    ASSERT_EQ(1U, records.size()); // [確認_正常系] - 再試行の通知を 1 回送出すること。
    EXPECT_EQ(2U, records[0].peer_id); // [確認_正常系] - 最も長く待つピア 2 へ送出すること。
}
// [サブ手順参照 名前=potrFlowControlN1WidthTest.TearDown]

// 許可を使い終えた時点の送出失敗後、tick で待機中の送信中ピアへ再割り当てすることの確認
// [サブ手順参照 名前=potrFlowControlN1WidthTest.SetUp]
TEST_F(potrFlowControlN1WidthTest, failed_turn_grant_retries_on_tick)
{
    // Arrange
    buffer_size = kPacketEstimateBytes * 2U;
    add_peer(1U);
    add_peer(2U);
    add_peer(3U);
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx)); // [状態確認] - 開始に成功すること。
    received(1U, 10U, 1);
    received(2U, 20U, 1);
    received(3U, 30U, 1);
    // [状態] - 全体幅 4、枠 1。ピア 1 が許可 14 を持ち、2 と 3 は許可数 0 で待つ。
    sendto_result = CPLAT_ERR_UNKNOWN;
    // Pre-Assert
    // Act
    received(1U, 14U, 1); // [手順] - 最後の許可を使い終えた際の割り当ての送出をすべて失敗させる。
    uint32_t failed_limit = 0U;
    int failed_held = piggyback(2U, &failed_limit);
    sendto_result = CPLAT_OK;
    clear_records();
    now_ms += 10U;
    potr_internal_flow_control_tick(&ctx, now_ms); // [手順] - 活動期限が来る前に tick を受理し、送出は成功させる。
    uint32_t recovered_limit = 0U;
    int recovered_held = piggyback(2U, &recovered_limit);
    // Assert
    EXPECT_EQ(1, failed_held); // [確認_異常系] - 失敗後も既存の通知状態を保持すること。
    EXPECT_EQ(20U, failed_limit); // [確認_異常系] - 送出失敗ではピア 2 の許可が進まないこと。
    EXPECT_EQ(1, recovered_held); // [確認_正常系] - 再試行後に相乗りの上限を取得できること。
    EXPECT_EQ(24U, recovered_limit); // [確認_正常系] - ピア 2 に 4 DATA の許可が出ること。
    ASSERT_EQ(1U, records.size()); // [確認_正常系] - 再試行の通知を 1 回送出すること。
    EXPECT_EQ(2U, records[0].peer_id); // [確認_正常系] - 最も長く待つピア 2 へ送出すること。
}
// [サブ手順参照 名前=potrFlowControlN1WidthTest.TearDown]

// 許可を使い終えた時点の送出失敗後、data で待機中の送信中ピアへ再割り当てすることの確認
// [サブ手順参照 名前=potrFlowControlN1WidthTest.SetUp]
TEST_F(potrFlowControlN1WidthTest, failed_turn_grant_retries_on_data)
{
    // Arrange
    buffer_size = kPacketEstimateBytes * 2U;
    add_peer(1U);
    add_peer(2U);
    add_peer(3U);
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx)); // [状態確認] - 開始に成功すること。
    received(1U, 10U, 1);
    received(2U, 20U, 1);
    received(3U, 30U, 1);
    // [状態] - 全体幅 4、枠 1。ピア 1 が許可 14 を持ち、2 と 3 は許可数 0 で待つ。
    sendto_result = CPLAT_ERR_UNKNOWN;
    // Pre-Assert
    // Act
    received(1U, 14U, 1); // [手順] - 最後の許可を使い終えた際の割り当ての送出をすべて失敗させる。
    uint32_t failed_limit = 0U;
    int failed_held = piggyback(2U, &failed_limit);
    sendto_result = CPLAT_OK;
    clear_records();
    now_ms += 10U;
    received(2U, 20U, 1); // [手順] - 活動期限が来る前に data を受理し、送出は成功させる。
    uint32_t recovered_limit = 0U;
    int recovered_held = piggyback(2U, &recovered_limit);
    // Assert
    EXPECT_EQ(1, failed_held); // [確認_異常系] - 失敗後も既存の通知状態を保持すること。
    EXPECT_EQ(20U, failed_limit); // [確認_異常系] - 送出失敗ではピア 2 の許可が進まないこと。
    EXPECT_EQ(1, recovered_held); // [確認_正常系] - 再試行後に相乗りの上限を取得できること。
    EXPECT_EQ(24U, recovered_limit); // [確認_正常系] - ピア 2 に 4 DATA の許可が出ること。
    ASSERT_EQ(1U, records.size()); // [確認_正常系] - 再試行の通知を 1 回送出すること。
    EXPECT_EQ(2U, records[0].peer_id); // [確認_正常系] - 最も長く待つピア 2 へ送出すること。
}
// [サブ手順参照 名前=potrFlowControlN1WidthTest.TearDown]

// 順番の許可で、許可の残りが半分以下になっても使い切るまでは補充せず、使い切った時点で
// 枠に余裕があれば待機中および残り半分以下の別ピアへ最古受領順に合計が全体の幅を超えない範囲で補充し、
// 枠に余裕がなければ補充しないことの確認
// [サブ手順参照 名前=potrFlowControlN1WidthTest.SetUp]
TEST_F(potrFlowControlN1WidthTest, turn_mode_refills_on_exhaustion_including_half_remaining_and_skips_when_full)
{
    // Arrange
    buffer_size = kPacketEstimateBytes * 10U; // 全体の幅 10 (送信中 3 でピアごとの幅 3 < 4 -> 順番の許可)
    add_peer(1U);
    add_peer(2U);
    add_peer(3U);
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx)); // [状態確認] - 開始に成功すること。
    received(1U, 10U, 1); // 通常 (幅 10): limit 20 (残り 10)
    received(2U, 20U, 1); // 通常 (幅 5): limit 25 (残り 5)
    received(3U, 30U, 1); // 順番切替 (幅 3 < 4)、枠超過 (used=15 >= 10): limit 30 (残り 0)
    clear_records();
    // [状態] - 全体の幅 10、送信中 3 台 (順番の許可)。ピア 1 が残り 10、ピア 2 が残り 5、ピア 3 が残り 0。
    // Pre-Assert
    // Act
    // 枠に余裕がない状態 (used=12 -> 10 >= 10) でピア 2 が残り半分以下になっても使い切っても補充されない。
    received(2U, 23U, 1); // [手順] - 枠に余裕がない状態でピア 2 が残り 2 まで消費する。
    received(2U, 25U, 1); // [手順] - 枠に余裕がない状態 (used=10 >= 10) でピア 2 が使い切る。
    size_t count_when_full = records.size();
    // ピア 1 が旧許可を使い終えると used=0 (< 10) になり、ピア 3 へ 4 (limit 34)、ピア 1 へ 4 (limit 24)、
    // ピア 2 へ残り枠の 2 (limit 27) が配られる (used=10)。
    // 状態: ピア 3 (limit 34, 残り 4, 最古)、ピア 1 (limit 24, 残り 4)、ピア 2 (limit 27, 残り 2, 最新)。
    received(1U, 20U, 1);
    clear_records();

    received(3U, 32U, 1); // [手順] - ピア 3 が 2 パケット消費する (残り 2 <= 半分、used=8 < 10)。
    size_t count_at_half_p3 = records.size();
    received(1U, 22U, 1); // [手順] - ピア 1 も 2 パケット消費する (残り 2 <= 半分、used=6 < 10)。
    size_t count_at_half_p1 = records.size();
    received(2U, 27U, 1); // [手順] - ピア 2 が残り 2 パケットを使い切る (used=4 < 10、空き 6)。
    // Assert
    EXPECT_EQ(0U, count_when_full);  // [確認_正常系] - 枠に余裕がない状態では使い切っても補充しないこと。
    EXPECT_EQ(0U, count_at_half_p3); // [確認_正常系] - 枠に余裕があっても残り半分以下になった時点では補充しないこと。
    EXPECT_EQ(0U, count_at_half_p1); // [確認_正常系] - 別ピアが残り半分以下になっても使い切るまでは補充しないこと。
    ASSERT_EQ(3U, records.size());
    // [確認_正常系] - 使い切った時点で残り半分以下の別ピアと使い切ったピアの 3 台へ補充すること。
    EXPECT_EQ(3U, records[0].peer_id);
    EXPECT_EQ(36U, records[0].send_limit);
    // [確認_正常系] - 最古受領で残り半分のピア 3 へ 2 パケットを足し limit 36 とすること。
    EXPECT_EQ(1U, records[1].peer_id);
    EXPECT_EQ(26U, records[1].send_limit);
    // [確認_正常系] - 次に古い残り半分のピア 1 へ 2 パケットを足し limit 26 とすること。
    EXPECT_EQ(2U, records[2].peer_id);
    EXPECT_EQ(29U, records[2].send_limit);
    // [確認_正常系] - 使い切ったピア 2 へ残り枠の 2 パケットを足し limit 29 とすること。
    EXPECT_EQ(10U, (records[0].send_limit - 32U) + (records[1].send_limit - 22U) + (records[2].send_limit - 27U));
    // [確認_正常系] - 補充後の未使用許可の合計が全体の幅 10 を超えないこと。
}
// [サブ手順参照 名前=potrFlowControlN1WidthTest.TearDown]

// 順番の許可で、送信をやめたピアの未使用の許可は最後の DATA から有効期限未満の間は数えられ、
// 有効期限以上経過した周期処理で合計から外れて待っているピアへ許可が出ることの確認
// [サブ手順参照 名前=potrFlowControlN1WidthTest.SetUp]
TEST_F(potrFlowControlN1WidthTest, turn_mode_expired_idle_grant_excluded_and_dispatched_on_tick)
{
    // Arrange
    buffer_size = kPacketEstimateBytes * 6U; // 全体の幅 6 (送信中 2 以上でピアごとの幅 <= 3 < 4 -> 順番の許可)
    add_peer(1U);
    add_peer(2U);
    add_peer(3U);
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx)); // [状態確認] - 開始に成功すること。
    now_ms = 1000U;
    received(1U, 10U, 1); // 通常 (幅 6): ピア 1 へ limit 16 (未使用 6, last_data_ms = 1000)
    now_ms = 1100U;
    received(2U, 20U, 1); // 順番切替 (幅 3 < 4): used = 6 >= 6 -> ピア 2 へ limit 20 (待ち)
    received(3U, 30U, 1); // 順番 (幅 2 < 4): used = 6 >= 6 -> ピア 3 へ limit 30 (待ち)
    clear_records();
    // [状態] - 全体の幅 6、有効期限 1000 [ms]。ピア 1 が未使用の許可 6 を持ち、ピア 2・3 が待っている。
    // Pre-Assert
    // Act
    now_ms = 1250U;
    potr_internal_flow_control_tick(&ctx, now_ms);
    // [手順] - ピア 1 が送信中でなくなる時刻 1250 (最後の DATA から 250 [ms] < 1000 [ms]) で tick を呼ぶ。
    std::vector<SentRecord> after_inactive_tick = records;
    clear_records();
    now_ms = 1900U;
    send_query(2U); // [手順] - 有効期限前の時刻 1900 にピア 2 から問い合わせを受ける (送信中を維持)。
    send_query(3U); // [手順] - 有効期限前の時刻 1900 にピア 3 から問い合わせを受ける (送信中を維持)。
    std::vector<SentRecord> before_expiry_queries = records;
    clear_records();
    now_ms = 1999U;
    potr_internal_flow_control_tick(&ctx, now_ms);
    // [手順] - 有効期限 1 [ms] 前の時刻 1999 (経過 999 [ms] < 1000 [ms]) で tick を呼ぶ。
    std::vector<SentRecord> just_before_expiry = records;
    clear_records();
    now_ms = 2000U;
    potr_internal_flow_control_tick(&ctx, now_ms);
    // [手順] - 有効期限ちょうどの時刻 2000 (経過 1000 [ms] >= 1000 [ms]) で tick を呼ぶ。
    auto newly_granted = records;
    clear_records();
    now_ms = 2001U;
    send_query(2U);
    send_query(3U);
    auto followup = records;
    clear_records();
    potr_internal_flow_control_tick(&ctx, now_ms);
    auto following_tick = records;
    records = newly_granted;
    // [手順] - DATA を受けずに別ピアの問い合わせと周期処理を続ける。
    // Assert
    ASSERT_EQ(2U, followup.size()); // [確認_正常系] - 再配分直後も問い合わせへ応答すること。
    EXPECT_EQ(24U, followup[0].send_limit); // [確認_正常系] - ピア 2 の新規許可を期限切れにしないこと。
    EXPECT_EQ(32U, followup[1].send_limit); // [確認_正常系] - ピア 3 の新規許可を増やさないこと。
    EXPECT_EQ(6U, (followup[0].send_limit - 20U) + (followup[1].send_limit - 30U));
    // [確認_正常系] - 合計に数える新規許可が全体の幅 6 に収まること。
    EXPECT_TRUE(following_tick.empty()); // [確認_正常系] - 続く周期処理でも重複配分しないこと。
    EXPECT_TRUE(after_inactive_tick.empty());
    // [確認_正常系] - 送信中でなくなっても有効期限未満の間は許可が出ないこと。
    ASSERT_EQ(2U, before_expiry_queries.size());
    EXPECT_EQ(20U, before_expiry_queries[0].send_limit);
    // [確認_正常系] - 有効期限前の問い合わせではピア 2 へ許可が出ないこと。
    EXPECT_EQ(30U, before_expiry_queries[1].send_limit);
    // [確認_正常系] - 有効期限前の問い合わせではピア 3 へ許可が出ないこと。
    EXPECT_TRUE(just_before_expiry.empty());
    // [確認_正常系] - 有効期限 1 [ms] 前の周期処理では許可が出ないこと。
    ASSERT_EQ(2U, records.size());
    // [確認_正常系] - 有効期限経過の周期処理で空いた 6 パケットが 2 ピアへ配られること。
    EXPECT_EQ(2U, records[0].peer_id);
    EXPECT_EQ(24U, records[0].send_limit); // [確認_正常系] - 管理表順でピア 2 へ 20 + 4 = 24 が配られること。
    EXPECT_EQ(3U, records[1].peer_id);
    EXPECT_EQ(32U, records[1].send_limit); // [確認_正常系] - 残りの 2 パケットがピア 3 へ 30 + 2 = 32 で配られること。
    EXPECT_EQ(6U, (records[0].send_limit - 20U) + (records[1].send_limit - 30U));
    // [確認_正常系] - 配った許可の合計が全体の幅 6 を超えないこと。
}
// [サブ手順参照 名前=potrFlowControlN1WidthTest.TearDown]

// DATA 未受理のピアが PING と問い合わせだけを続けても、許可の期限後に待機ピアが再開することの確認
// [サブ手順参照 名前=potrFlowControlN1WidthTest.SetUp]
TEST_F(potrFlowControlN1WidthTest, turn_mode_no_data_peer_grant_expires_from_notification)
{
    // Arrange
    buffer_size = kPacketEstimateBytes * 4U;
    add_peer(1U);
    add_peer(2U);
    add_peer(3U);
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx)); // [状態確認] - potr_internal_flow_control_start(&ctx) が POTR_OK であること。(開始に成功することの確認)
    now_ms = 1000U;
    received(1U, 10U, 0);
    now_ms = 1100U;
    received(2U, 20U, 1);
    received(3U, 30U, 1);
    received(2U, 24U, 1);
    clear_records();
    // [状態] - DATA 未受理のピア 1 の許可 4 を数え、使い切ったピア 2 と未許可のピア 3 が待つ。
    // Pre-Assert
    // Act
    now_ms = 1900U;
    received(1U, 10U, 0);
    send_query(1U);
    send_query(2U);
    send_query(3U);
    clear_records();
    now_ms = 1999U;
    potr_internal_flow_control_tick(&ctx, now_ms);
    auto before = records;
    clear_records();
    now_ms = 2000U;
    potr_internal_flow_control_tick(&ctx, now_ms);
    auto granted = records;
    clear_records();
    send_query(2U);
    send_query(3U);
    auto after = records;
    clear_records();
    potr_internal_flow_control_tick(&ctx, now_ms);
    auto after_tick = records;
    now_ms = 3000U;
    send_query(1U);
    send_query(2U);
    send_query(3U);
    uint32_t resumed_limit = 0U;
    for (const auto &record : records)
    {
        if (record.peer_id == 2U)
        {
            resumed_limit = record.send_limit;
        }
    }
    // [手順] - 期限の直前、境界と追加問い合わせを処理し、さらに次の期限でも全ピアの問い合わせを受ける。
    // Assert
    EXPECT_TRUE(before.empty()); // [確認_正常系] - before.empty() が true であること。(最初の通知から 999 [ms] では再配分しないことの確認)
    ASSERT_EQ(1U, granted.size()); // [確認_正常系] - granted.size() が 1U であること。(1000 [ms] で待機ピアへ許可を配ることの確認)
    EXPECT_EQ(3U, granted[0].peer_id); // [確認_正常系] - granted[0].peer_id が 3U であること。(一度も許可を受けていないピアを優先することの確認)
    EXPECT_EQ(34U, granted[0].send_limit); // [確認_正常系] - granted[0].send_limit が 34U であること。(全体の幅 4 を配ることの確認)
    ASSERT_EQ(2U, after.size()); // [確認_正常系] - after.size() が 2U であること。(後続の問い合わせに応答することの確認)
    EXPECT_EQ(24U, after[0].send_limit); // [確認_正常系] - after[0].send_limit が 24U であること。(別のピアへ新しい許可を重ねて配らないことの確認)
    EXPECT_EQ(34U, after[1].send_limit); // [確認_正常系] - after[1].send_limit が 34U であること。(新しい許可を期限切れにせず維持することの確認)
    EXPECT_TRUE(after_tick.empty()); // [確認_正常系] - after_tick.empty() が true であること。(周期処理でも追加の許可を配らないことの確認)
    EXPECT_EQ(28U, resumed_limit); // [確認_正常系] - resumed_limit が 28U であること。(同値の旧許可を持つピアを飛ばし、次の期限ではピア 2 が再開することの確認)
}
// [サブ手順参照 名前=potrFlowControlN1WidthTest.TearDown]

// DATA を受けずに進めた相乗り上限も送出成功から期限まで数え、再通知や失敗では延長しないことの確認
// [サブ手順参照 名前=potrFlowControlN1WidthTest.SetUp]
TEST_F(potrFlowControlN1WidthTest, turn_mode_counts_advanced_piggyback_until_notification_lifetime)
{
    // Arrange
    buffer_size = kPacketEstimateBytes * 4U;
    add_peer(1U);
    add_peer(2U);
    add_peer(3U);
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx)); // [状態確認] - potr_internal_flow_control_start(&ctx) が POTR_OK であること。(開始に成功することの確認)
    now_ms = 1000U;
    received(1U, 10U, 1);
    now_ms = 1500U;
    received(1U, 11U, 0);
    uint32_t limit = 0U;
    int held = piggyback(1U, &limit);
    now_ms = 1600U;
    received(2U, 20U, 1);
    received(3U, 30U, 1);
    clear_records();
    // [状態] - 最後の DATA は 1000 [ms]、相乗り上限 15 の送出成功は 1500 [ms]。全体の幅 4 をピア 1 が持つ。
    // Pre-Assert
    // Act
    now_ms = 2400U;
    send_query(1U);
    send_query(2U);
    send_query(3U);
    clear_records();
    sendto_result = CPLAT_ERR_UNKNOWN;
    send_query(1U);
    sendto_result = CPLAT_OK;
    clear_records();
    now_ms = 2499U;
    potr_internal_flow_control_tick(&ctx, now_ms);
    auto before = records;
    clear_records();
    now_ms = 2500U;
    potr_internal_flow_control_tick(&ctx, now_ms);
    // [手順] - 同値の通知と送出失敗の後、相乗り成功から 999 [ms] と 1000 [ms] の周期処理を実行する。
    // Assert
    EXPECT_EQ(1, held); // [確認_正常系] - held が 1 であること。(相乗り上限を取得できることの確認)
    EXPECT_EQ(15U, limit); // [確認_正常系] - limit が 15U であること。(DATA の時刻を変えずに上限を 14 から 15 へ進めることの確認)
    EXPECT_TRUE(before.empty()); // [確認_正常系] - before.empty() が true であること。(DATA の期限が過ぎても新しい通知の期限前は数えることの確認)
    ASSERT_EQ(1U, records.size()); // [確認_正常系] - records.size() が 1U であること。(同値通知と送出失敗で延長せず期限で再配分することの確認)
    EXPECT_EQ(2U, records[0].peer_id); // [確認_正常系] - records[0].peer_id が 2U であること。(未許可のピア 2 を再開することの確認)
    EXPECT_EQ(24U, records[0].send_limit); // [確認_正常系] - records[0].send_limit が 24U であること。(全体の幅 4 を超えず配ることの確認)
}
// [サブ手順参照 名前=potrFlowControlN1WidthTest.TearDown]

// 相乗りの送出準備中に通常配分から順番の許可へ変わっても枠を重ねて配らず、失敗で予約を解くことの確認
// [サブ手順参照 名前=potrFlowControlN1WidthTest.SetUp]
TEST_F(potrFlowControlN1WidthTest, pending_piggyback_reserves_width_and_failure_releases_it)
{
    // Arrange
    buffer_size = kPacketEstimateBytes * 6U;
    add_peer(1U);
    add_peer(2U);
    add_peer(3U);
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx)); // [状態確認] - 開始に成功すること。
    now_ms = 1000U;
    received(1U, 10U, 1);
    received(1U, 13U, 0);
    uint32_t limit = 0U;
    clear_records();
    // [状態] - ピア 1 に上限 16 を知らせ、次通番 13 まで進み、相乗りで上限 19 を知らせられる通常配分とする。
    // Pre-Assert
    // Act
    cplat_local_lock_lock(ctx.peers_mutex);
    int held = potr_internal_flow_control_piggyback_limit(&ctx, 1U, &limit);
    cplat_local_lock_unlock(ctx.peers_mutex);
    now_ms = 1600U;
    received(2U, 20U, 1);
    received(3U, 30U, 1);
    auto during_send = records;
    clear_records();
    cplat_local_lock_lock(ctx.peers_mutex);
    potr_internal_flow_control_piggyback_complete(&ctx, 1U, limit, 0);
    cplat_local_lock_unlock(ctx.peers_mutex);
    send_query(2U);
    // [手順] - 相乗りを予約し、別ピアの DATA で順番の許可へ切り替え、送出失敗による予約解除の後に問い合わせを受ける。
    // Assert
    EXPECT_EQ(1, held); // [確認_正常系] - held が 1 であること。
    EXPECT_EQ(19U, limit); // [確認_正常系] - limit が 19 であること。
    ASSERT_EQ(2U, during_send.size()); // [確認_正常系] - during_send.size() の戻り値が 2 であること。
    EXPECT_EQ(20U, during_send[0].send_limit); // [確認_正常系] - during_send[0].send_limit が 20 であること。
    EXPECT_EQ(30U, during_send[1].send_limit); // [確認_正常系] - during_send[1].send_limit が 30 であること。
    ASSERT_EQ(1U, records.size()); // [確認_正常系] - records.size() の戻り値が 1 であること。
    EXPECT_EQ(2U, records[0].peer_id); // [確認_正常系] - records[0].peer_id が 2 であること。
    EXPECT_EQ(23U, records[0].send_limit); // [確認_正常系] - records[0].send_limit が 23 であること。
    EXPECT_EQ(6U, (16U - 13U) + (records[0].send_limit - 20U));
    // [確認_正常系] - 旧許可の残り 3 と新しい許可の合計が 6 であること。
}
// [サブ手順参照 名前=potrFlowControlN1WidthTest.TearDown]
