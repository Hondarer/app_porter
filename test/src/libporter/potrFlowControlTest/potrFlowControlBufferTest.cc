/**
 *******************************************************************************
 *  @file           potrFlowControlBufferTest.cc
 *  @brief          OS の受信バッファーに基づく送出上限と、multicast と broadcast の監視対象の扱いを検証します。
 *
 *  OS の受信バッファーの大きさは cplat_socket_get_receive_buffer_size() の mock で与えます。
 *  フロー制御パケットの組み立ては Mock_porter で置き換え、状態と送出上限の値を記録します。
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
#include <vector>
using namespace testing;

namespace
{
const uint8_t kSuppress = (uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS;
const uint8_t kResume = (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME;
const uint8_t kQuery = (uint8_t)POTR_FLOW_CONTROL_STATE_QUERY;
const uint8_t kUnlimited = (uint8_t)POTR_FLOW_CONTROL_STATE_UNLIMITED;
const int kLinuxDefaultBuffer = 212992; // Linux の既定の OS の受信バッファーの大きさ [byte]
const int kWindowsDefaultBuffer = 65536; // Windows の既定の OS の受信バッファーの大きさ [byte]
const uint32_t kSenderSessionId = 1U;   // 採用している送信者のセッション識別子
const uint32_t kOwnSessionId = 0x77U;   // 受信者自身のセッション識別子

// 監視対象の一覧のペイロードの 1 台分を、ヘッダーと同じ並び (ネットワーク バイト オーダー) で書き込む。
void PutEntry(uint8_t *entry, int64_t tv_sec, uint32_t session_id, int32_t tv_nsec)
{
    for (int i = 0; i < 8; i++)
    {
        entry[i] = (uint8_t)((uint64_t)tv_sec >> (8 * (7 - i)));
    }
    for (int i = 0; i < 4; i++)
    {
        entry[8 + i] = (uint8_t)(session_id >> (8 * (3 - i)));
        entry[12 + i] = (uint8_t)((uint32_t)tv_nsec >> (8 * (3 - i)));
    }
}
} // namespace

class potrFlowControlBufferTest : public Test
{
  protected:
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx;
    uint64_t now_ms = 1000U;
    std::vector<uint8_t> states;  // フロー制御パケットの組み立てで渡された状態の履歴
    std::vector<uint32_t> limits; // フロー制御パケットの組み立てで渡された送出上限の履歴
    int sendto_count = 0;         // cplat_socket_sendto() の呼び出し回数
    int receive_buffer_query_count = 0; // cplat_socket_get_receive_buffer_size() の呼び出し回数
    int receive_buffer_query_fails = 0; // cplat_socket_get_receive_buffer_size() を失敗させるか (1: 失敗させる)
    int buffer_sizes[2] = {kLinuxDefaultBuffer, kLinuxDefaultBuffer}; // ソケット 5 と 6 の OS の受信バッファーの大きさ
    int fixture_padding = 0; // クラスのサイズを 8 バイト境界にそろえるためのパディング
    std::vector<cplat_ipv4_endpoint> destinations; // 送出先の履歴
    potr_internal_peer_context *peer10 = nullptr;
    potr_internal_peer_context *peer11 = nullptr;
    cplat_timespec sender_ts = {};

    // cplat_socket_get_receive_buffer_size() の mock の応答。ソケット 6 は buffer_sizes[1]、それ以外は buffer_sizes[0] を返す。
    int fill_buffer_size(intptr_t socket_value, size_t *size_out)
    {
        receive_buffer_query_count++;
        if (receive_buffer_query_fails != 0)
        {
            return CPLAT_ERR_UNKNOWN;
        }
        *size_out = (size_t)(socket_value == 6 ? buffer_sizes[1] : buffer_sizes[0]);
        return CPLAT_OK;
    }

    // [サブ手順 名前=potrFlowControlBufferTest.SetUp]
    void SetUp() override
    {
        memset(&ctx, 0, sizeof(ctx));
        ctx.service.type = POTR_TYPE_UNICAST;
        ctx.service.service_id = 1;
        ctx.service.src_port = 5000;
        ctx.role = POTR_ROLE_RECEIVER;
        ctx.n_path = 1;
        ctx.sock[0] = 5;
        ctx.sock[1] = 6;
        ctx.src_addr_resolved[0] = 0x0100007FU;
        ctx.src_addr_resolved[1] = 0x0100007FU;
        ctx.dest_addr[0].address = 0x0200007FU;
        ctx.dest_addr[0].port = cplat_hton16(6000);
        ctx.dest_addr[1].address = 0x0300007FU;
        ctx.dest_addr[1].port = cplat_hton16(6001);
        ctx.session_id = kOwnSessionId;
        ctx.session_ts.tv_sec = 200;
        ctx.session_ts.tv_nsec = 7;
        ctx.flow_control_config.capacity = 0U;
        ctx.flow_control_config.suppress_percent = 80U;
        ctx.flow_control_config.resume_percent = 60U;
        ctx.flow_control_config.request_interval_ms = 200U;
        ctx.flow_control_config.request_lifetime_ms = 1000U;
        ctx.flow_control_config.receive_buffer_percent = 100U;
        ctx.global.max_payload = 1400U;
        // [状態] - unicast の受信者で、容量 0、受信バッファーの幅の割合 100、max_payload 1400 とする
        //          (OS の受信バッファーが 212992 の場合に幅 42)。
        sender_ts.tv_sec = 100;
        sender_ts.tv_nsec = 5;
        ctx.peer_session_known = 1;
        ctx.peer_session_id = kSenderSessionId;
        ctx.peer_session_ts = sender_ts;
        // [状態] - 採用している送信者を、セッション識別子 1、開始時刻 100 秒 5 ナノ秒とする。
        peer10 = new potr_internal_peer_context();
        peer11 = new potr_internal_peer_context();
        peer10->session_id = 0x1010U;
        peer11->session_id = 0x1111U;
        peer10->dest_addr[0].address = 0x0A00007FU;
        peer10->dest_addr[0].port = cplat_hton16(7010);
        peer11->dest_addr[0].address = 0x0B00007FU;
        peer11->dest_addr[0].port = cplat_hton16(7011);

        ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault([this](uint64_t *out) {
            *out = now_ms;
            return CPLAT_OK;
        }); // [状態] - 現在時刻を now_ms で制御する。
        ON_CALL(mock_cplat, cplat_socket_sendto(_, _, _, _, _, _))
            .WillByDefault([this](cplat_socket, const void *, size_t len, const cplat_ipv4_endpoint *dest,
                                  size_t *sent, cplat_error *) {
                sendto_count++;
                destinations.push_back(*dest);
                if (sent != nullptr)
                {
                    *sent = len;
                }
                return CPLAT_OK;
            }); // [状態] - 送出回数と宛先を記録して成功を返す。
        ON_CALL(mock_porter, potr_internal_packet_build_flow_control(_, _, _, _, _))
            .WillByDefault([this](potr_packet *packet, const potr_internal_packet_session_hdr *, uint8_t *payload,
                                  uint8_t state, uint32_t send_limit) {
                states.push_back(state);
                limits.push_back(send_limit);
                memset(payload, 0, POTR_FLOW_CONTROL_PAYLOAD_SIZE);
                payload[0] = state;
                memset(packet, 0, POTR_PACKET_HEADER_SIZE);
                packet->payload = payload;
                packet->payload_len = cplat_hton16((uint16_t)POTR_FLOW_CONTROL_PAYLOAD_SIZE);
                return POTR_OK;
            }); // [状態] - 状態と送出上限を記録し、封緘できる最小のパケットを組み立てて POTR_OK を返す。
        ON_CALL(mock_porter, potr_internal_peer_find_by_id(_, _))
            .WillByDefault([this](const potr_context *, potr_peer_id id) -> potr_internal_peer_context * {
                if (id == 10U)
                {
                    return peer10;
                }
                if (id == 11U)
                {
                    return peer11;
                }
                return nullptr;
            }); // [状態] - ピア識別子 10 と 11 だけ存在するピアを返す。
        ON_CALL(mock_cplat, cplat_socket_get_receive_buffer_size(_, _, _))
            .WillByDefault([this](cplat_socket sock, size_t *size_out, cplat_error *)
                           { return fill_buffer_size((intptr_t)sock, size_out); });
        // [状態] - cplat_socket_get_receive_buffer_size() で、ソケットごとの buffer_sizes を返す (receive_buffer_query_fails が 1 なら失敗)。
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrFlowControlBufferTest.TearDown]
    void TearDown() override
    {
        potr_internal_flow_control_stop(&ctx);
        if (ctx.peers_mutex != nullptr)
        {
            cplat_local_lock_dispose(ctx.peers_mutex);
            ctx.peers_mutex = nullptr;
        }
        delete peer10;
        delete peer11;
    }
    // [サブ手順終了]

    // N:1 モードの構成にする。
    void enable_multi_peer(int peer_count)
    {
        ctx.is_multi_peer = 1;
        ctx.n_peers = peer_count;
        ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
        cplat_local_lock_create(&ctx.peers_mutex);
    }

    // multicast の受信者の構成にする。
    void enable_multicast_receiver()
    {
        ctx.service.type = POTR_TYPE_MULTICAST;
        ctx.role = POTR_ROLE_RECEIVER;
    }

    // on_received を呼び出す。N:1 モードでは呼び出し元の契約に従い peers_mutex を保持する。
    void received(potr_peer_id peer_id, uint32_t next_seq, int data_packet = 1, uint32_t session_id = kSenderSessionId)
    {
        if (ctx.is_multi_peer)
        {
            cplat_local_lock_lock(ctx.peers_mutex);
        }
        potr_internal_flow_control_on_received(&ctx, peer_id, session_id, &sender_ts, next_seq, data_packet, 0U);
        if (ctx.is_multi_peer)
        {
            cplat_local_lock_unlock(ctx.peers_mutex);
        }
    }

    // admit を呼び出す (N:1 モード以外)。
    int admit(size_t len)
    {
        return potr_internal_flow_control_admit(&ctx, POTR_PEER_NA, kSenderSessionId, &sender_ts, len);
    }

    // 監視対象の一覧のパケットを組み立てる。include_self が非 0 なら、受信者自身を 2 台目に含める。
    potr_packet make_list(std::vector<uint8_t> &payload, int include_self, uint32_t sender_session = kSenderSessionId)
    {
        potr_packet pkt;
        payload.assign(POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE + POTR_FLOW_CONTROL_MONITOR_ENTRY_SIZE * 2U, 0U);
        payload[0] = (uint8_t)POTR_FLOW_CONTROL_STATE_MONITOR;
        payload[1] = 2U;
        PutEntry(&payload[2], 300, 0x99U, 1);
        if (include_self != 0)
        {
            PutEntry(&payload[18], (int64_t)ctx.session_ts.tv_sec, ctx.session_id, (int32_t)ctx.session_ts.tv_nsec);
        }
        else
        {
            PutEntry(&payload[18], 400, 0x98U, 2);
        }
        memset(&pkt, 0, sizeof(pkt));
        pkt.flags = POTR_FLAG_FLOW_CONTROL;
        pkt.session_id = sender_session;
        pkt.session_tv_sec = (int64_t)sender_ts.tv_sec;
        pkt.session_tv_nsec = (int32_t)sender_ts.tv_nsec;
        pkt.payload = payload.data();
        pkt.payload_len = (uint16_t)payload.size();
        return pkt;
    }

    // 送出の記録を消す。
    void clear_records()
    {
        states.clear();
        limits.clear();
        sendto_count = 0;
        destinations.clear();
    }
};

// 受信バッファーの幅を、既定の Linux と Windows の大きさ、割合の指定で求めることの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, receive_buffer_width_examples)
{
    // Arrange
    // Pre-Assert
    // Act
    uint64_t width_linux = flow_receive_buffer_width((uint64_t)kLinuxDefaultBuffer, 100U, 1400U);
    // [手順] - 212992 [byte]、100%、max_payload 1400 で幅を求める。
    uint64_t width_windows = flow_receive_buffer_width((uint64_t)kWindowsDefaultBuffer, 100U, 1400U);
    // [手順] - 65536 [byte]、100%、max_payload 1400 で幅を求める。
    uint64_t width_half = flow_receive_buffer_width((uint64_t)kLinuxDefaultBuffer, 50U, 1400U);
    // [手順] - 212992 [byte]、50%、max_payload 1400 で幅を求める。
    uint64_t width_small_payload = flow_receive_buffer_width((uint64_t)kLinuxDefaultBuffer, 100U, 100U);
    // [手順] - 212992 [byte]、100%、max_payload 100 で幅を求める。
    // Assert
    EXPECT_EQ(42U, width_linux);   // [確認_正常系] - 212992 ÷ (2 × (72 + 1400 + 16) + 2048) = 42 であること。
    EXPECT_EQ(13U, width_windows); // [確認_正常系] - 65536 ÷ 5024 = 13 であること。
    EXPECT_EQ(21U, width_half);    // [確認_正常系] - 106496 ÷ 5024 = 21 であること。
    EXPECT_EQ(87U, width_small_payload); // [確認_正常系] - 212992 ÷ (2 × (72 + 100 + 16) + 2048) = 87 であること。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// 容量が 0 の受信側が、資源を生成し、最初の通知と、範囲の半分を受信したときの更新を送出することの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, zero_capacity_receiver_notifies_first_and_updates)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 容量 0、OS の受信バッファー 212992 [byte] で start する (幅 42)。
    // [状態確認] - start の戻り値が POTR_OK であること。
    // Pre-Assert
    // Act
    int actual_ret_admit = admit(1000000U); // [手順] - 容量 0 の受信側で、1000000 [byte] を admit する。
    received(POTR_PEER_NA, 10U, 0); // [手順] - 次に受信する通番 10 で、PING の受理として on_received を呼び出す。
    received(POTR_PEER_NA, 30U);    // [手順] - 次に受信する通番 30 (範囲 42 のうち 20 を受信) で on_received を呼び出す。
    size_t count_at_30 = states.size();
    received(POTR_PEER_NA, 31U);    // [手順] - 次に受信する通番 31 (範囲の半分の 21 を受信) で on_received を呼び出す。
    // Assert
    EXPECT_NE(nullptr, ctx.flow_control);            // [確認_正常系] - 容量 0 でも資源を生成すること。
    EXPECT_EQ(POTR_OK, actual_ret_admit);            // [確認_正常系] - 占有量を数えず POTR_OK を返すこと。
    EXPECT_EQ(1U, count_at_30);                      // [確認_正常系] - 範囲の半分に満たない間は更新しないこと。
    EXPECT_EQ((std::vector<uint8_t>{kResume, kResume}), states); // [確認_正常系] - 最初の通知と更新がどちらも解除の状態であること。
    EXPECT_EQ((std::vector<uint32_t>{52U, 73U}), limits);        // [確認_正常系] - 送出上限が 10 + 42 と 31 + 42 であること。
    EXPECT_EQ(2, sendto_count);                      // [確認_正常系] - 2 回送出すること。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// 容量が 0 の受信側は、相手が送出上限まで送り終えた時点で、抑制の要求ではなく更新を送出することの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, zero_capacity_receiver_updates_at_send_limit_without_request)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 容量 0、幅 42 で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    received(POTR_PEER_NA, 10U, 0); // [状態] - 次に受信する通番 10 で、送出上限 52 を知らせる。
    clear_records();
    // Pre-Assert
    // Act
    received(POTR_PEER_NA, 52U); // [手順] - 次に受信する通番 52 (送出上限ちょうど) で on_received を呼び出す。
    potr_internal_flow_control_tick(&ctx, now_ms + 1000U); // [手順] - 再送周期より後の時刻で tick を呼び出す。
    // Assert
    EXPECT_EQ(std::vector<uint8_t>{kResume}, states); // [確認_正常系] - 抑制の要求ではなく、解除の状態の更新を送出すること。
    EXPECT_EQ(std::vector<uint32_t>{94U}, limits);    // [確認_正常系] - 送出上限が 52 + 42 であること。
    EXPECT_EQ(1, sendto_count);                       // [確認_正常系] - tick で要求を再送しないこと。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// 容量を設定した受信側が、占有量による許可数と受信バッファーの幅の小さいほうを使うことの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, capacity_receiver_uses_smaller_allowance)
{
    // Arrange
    ctx.flow_control_config.capacity = 10000000U;
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 容量 10000000 (要求のしきい値 8000000、占有量 0 で許可数 5715)、幅 42 で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    // Pre-Assert
    // Act
    received(POTR_PEER_NA, 10U, 0); // [手順] - 次に受信する通番 10 で、最初の通知を送出させる。
    int actual_ret_admit = admit(7999000U); // [手順] - 7999000 [byte] を admit する (占有量による許可数 1)。
    received(POTR_PEER_NA, 5U, 0); // [手順] - 次に受信する通番を 5 へ戻し、送出上限を知らせ直させる。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_admit); // [確認_正常系] - admit が POTR_OK であること。
    EXPECT_EQ((std::vector<uint32_t>{52U, 6U}), limits);
    // [確認_正常系] - 送出上限が 10 + min(5715, 42) と 5 + min(1, 42) であること。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// 受信バッファーの幅の割合が 0 で容量も 0 の unicast 受信側が、大きさを取得せず「上限なし」を 1 回だけ送出することの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, disabled_receive_buffer_does_not_query_or_notify)
{
    // Arrange
    ctx.flow_control_config.receive_buffer_percent = 0U;
    // [状態] - unicast 受信者で容量 0、受信バッファーの幅の割合を 0 にする。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_flow_control_start(&ctx); // [手順] - start を呼び出す。
    received(POTR_PEER_NA, 10U, 0); // [手順] - 次に受信する通番 10 で on_received を呼び出す。
    received(POTR_PEER_NA, 11U, 1); // [手順] - 同じセッションで 2 回目の on_received を呼び出す。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - start が POTR_OK であること。
    EXPECT_EQ(0, receive_buffer_query_count); // [確認_正常系] - OS の受信バッファーの大きさを取得しないこと。
    EXPECT_NE(nullptr, ctx.flow_control); // [確認_正常系] - 上限なしを送出するために資源を生成すること。
    EXPECT_EQ(std::vector<uint8_t>{kUnlimited}, states); // [確認_正常系] - 状態「上限なし」を 1 回だけ送出すること。
    EXPECT_EQ(std::vector<uint32_t>{0U}, limits);        // [確認_正常系] - 送出上限の欄が 0 であること。
    EXPECT_EQ(1, sendto_count);                          // [確認_正常系] - 1 回だけ送出すること。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// OS の受信バッファーの大きさを取得できなかった場合に、受信バッファーの幅を使わず上限なしまたは容量による許可数を使うことの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, query_failure_does_not_use_receive_buffer)
{
    // Arrange
    receive_buffer_query_fails = 1;
    // [状態] - cplat_socket_get_receive_buffer_size() を失敗させる。
    // Pre-Assert
    // Act
    int actual_ret_zero = potr_internal_flow_control_start(&ctx); // [手順] - 容量 0 で start を呼び出す。
    received(POTR_PEER_NA, 10U, 0); // [手順] - 容量 0 の状態で on_received を呼び出す。
    std::vector<uint8_t> states_zero = states;
    std::vector<uint32_t> limits_zero = limits;
    potr_internal_flow_control_stop(&ctx);
    clear_records();
    ctx.flow_control_config.capacity = 10000000U;
    int actual_ret_capacity = potr_internal_flow_control_start(&ctx); // [手順] - 容量 10000000 で start を呼び出す。
    received(POTR_PEER_NA, 10U, 0); // [手順] - 次に受信する通番 10 で on_received を呼び出す。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_zero); // [確認_正常系] - 容量 0 の start が POTR_OK であること。
    EXPECT_EQ(std::vector<uint8_t>{kUnlimited}, states_zero); // [確認_正常系] - 容量 0 では「上限なし」を送出すること。
    EXPECT_EQ(std::vector<uint32_t>{0U}, limits_zero);        // [確認_正常系] - 「上限なし」の送出上限が 0 であること。
    EXPECT_EQ(POTR_OK, actual_ret_capacity); // [確認_正常系] - 容量を設定した start が POTR_OK であること。
    EXPECT_NE(nullptr, ctx.flow_control);    // [確認_正常系] - 容量を設定した場合は資源を生成すること。
    EXPECT_EQ(std::vector<uint32_t>{5725U}, limits); // [確認_正常系] - 送出上限が 10 + 占有量による許可数 5715 であること。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// 経路が複数ある場合に、最も小さい OS の受信バッファーの大きさを使うことの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, smallest_buffer_among_paths_is_used)
{
    // Arrange
    ctx.n_path = 2;
    buffer_sizes[0] = kLinuxDefaultBuffer;
    buffer_sizes[1] = kWindowsDefaultBuffer;
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 経路 2 本の OS の受信バッファーを 212992 と 65536 [byte] にして start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    // Pre-Assert
    // Act
    received(POTR_PEER_NA, 10U, 0); // [手順] - 次に受信する通番 10 で on_received を呼び出す。
    // Assert
    EXPECT_EQ(2, receive_buffer_query_count);                  // [確認_正常系] - 経路ごとに大きさを取得すること。
    EXPECT_EQ(std::vector<uint32_t>{23U}, limits);   // [確認_正常系] - 送出上限が 10 + 13 (65536 の幅) であること。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// N:1 サーバーで、受信バッファーの幅を接続中のピア数で割り、下限 4 を適用することの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, multi_peer_divides_width_by_peer_count)
{
    // Arrange
    enable_multi_peer(4);
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - N:1 モードで接続中のピアを 4 つにして start する (幅 42)。
    // [状態確認] - start の戻り値が POTR_OK であること。
    // Pre-Assert
    // Act
    received(10U, 10U, 0); // [手順] - ピア 10 について、次に受信する通番 10 で on_received を呼び出す。
    ctx.n_peers = 20;
    received(11U, 10U, 0); // [手順] - 接続中のピアを 20 にして、ピア 11 について on_received を呼び出す。
    // Assert
    EXPECT_EQ((std::vector<uint32_t>{20U, 14U}), limits);
    // [確認_正常系] - 送出上限が 10 + 42 ÷ 4 (10) と、10 + 下限 4 (42 ÷ 20 = 2) であること。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// 受信報告に載せる送出上限を、送信者を採用した後に返し、それ以外では返さないことの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, report_limit_after_adoption)
{
    // Arrange
    uint32_t limit_none = 99U;
    uint32_t limit_adopted = 0U;
    uint32_t limit_disabled = 99U;
    int held_disabled = potr_internal_flow_control_report_limit(&ctx, &limit_disabled);
    // [状態] - 資源を生成する前 (フロー制御パケットを送出しない受信側) に report_limit を呼び出す。
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 容量 0、幅 42 で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    // Pre-Assert
    // Act
    int held_none = potr_internal_flow_control_report_limit(&ctx, &limit_none);
    // [手順] - 送信者を採用する前に report_limit を呼び出す。
    received(POTR_PEER_NA, 10U, 0);
    int held_adopted = potr_internal_flow_control_report_limit(&ctx, &limit_adopted);
    // [手順] - 次に受信する通番 10 で採用した後に report_limit を呼び出す。
    int held_null_limit = potr_internal_flow_control_report_limit(&ctx, nullptr);
    int held_null_ctx = potr_internal_flow_control_report_limit(nullptr, &limit_none);
    // [手順] - send_limit と ctx を順に NULL にして report_limit を呼び出す。
    // Assert
    EXPECT_EQ(0, held_disabled);    // [確認_正常系] - 資源がない受信側では送出上限がないこと。
    EXPECT_EQ(0U, limit_disabled);  // [確認_正常系] - 資源がない受信側で 0 を格納すること。
    EXPECT_EQ(0, held_none);        // [確認_正常系] - 採用前は送出上限がないこと。
    EXPECT_EQ(1, held_adopted);     // [確認_正常系] - 採用後は送出上限があること。
    EXPECT_EQ(52U, limit_adopted);  // [確認_正常系] - 送出上限が 10 + 42 であること。
    EXPECT_EQ(0, held_null_limit);  // [確認_異常系] - send_limit が NULL で 0 であること。
    EXPECT_EQ(0, held_null_ctx);    // [確認_異常系] - ctx が NULL で 0 であること。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// multicast の受信者が、監視対象でない間は送出上限を送出せず、受信報告には送出上限を載せることの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, multicast_receiver_not_monitored_does_not_notify)
{
    // Arrange
    enable_multicast_receiver();
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - multicast の受信者で、容量 0、幅 42 で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    uint32_t report_limit = 0U;
    // Pre-Assert
    // Act
    received(POTR_PEER_NA, 10U, 0); // [手順] - 次に受信する通番 10 で、PING の受理として on_received を呼び出す。
    received(POTR_PEER_NA, 52U);    // [手順] - 次に受信する通番 52 で on_received を呼び出す。
    potr_internal_flow_control_tick(&ctx, now_ms + 1000U); // [手順] - tick を呼び出す。
    int held = potr_internal_flow_control_report_limit(&ctx, &report_limit); // [手順] - report_limit を呼び出す。
    // Assert
    EXPECT_TRUE(states.empty());  // [確認_正常系] - フロー制御パケットを組み立てないこと。
    EXPECT_EQ(0, sendto_count);   // [確認_正常系] - 送出しないこと。
    EXPECT_EQ(1, held);           // [確認_正常系] - 受信報告に載せる送出上限があること。
    EXPECT_EQ(94U, report_limit); // [確認_正常系] - 受信報告に載せる送出上限が 52 + 42 であること。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// 一覧に自身が含まれると最初の通知から始め、更新を行い、含まれない一覧で送出上限を送出しなくなることの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, monitor_list_including_self_starts_and_excluding_stops)
{
    // Arrange
    enable_multicast_receiver();
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - multicast の受信者で、容量 0、幅 42 で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    std::vector<uint8_t> included_payload;
    std::vector<uint8_t> excluded_payload;
    potr_packet included = make_list(included_payload, 1);
    potr_packet excluded = make_list(excluded_payload, 0);
    received(POTR_PEER_NA, 10U, 0); // [状態] - 次に受信する通番 10 で送信者を採用する。
    // Pre-Assert
    // Act
    potr_internal_flow_control_on_packet(&ctx, nullptr, &included); // [手順] - 自身を含む一覧を渡す。
    size_t count_after_included = states.size();
    received(POTR_PEER_NA, 31U); // [手順] - 次に受信する通番 31 (範囲の半分) で on_received を呼び出す。
    size_t count_after_update = states.size();
    potr_internal_flow_control_on_packet(&ctx, nullptr, &excluded); // [手順] - 自身を含まない一覧を渡す。
    received(POTR_PEER_NA, 60U); // [手順] - 次に受信する通番 60 で on_received を呼び出す。
    potr_internal_flow_control_tick(&ctx, now_ms + 100U); // [手順] - tick を呼び出す。
    size_t count_after_excluded = states.size();
    potr_internal_flow_control_on_packet(&ctx, nullptr, &included); // [手順] - 自身を含む一覧をもう一度渡す。
    // Assert
    EXPECT_EQ(1U, count_after_included); // [確認_正常系] - 監視対象になった時点で最初の通知を送出すること。
    EXPECT_EQ(2U, count_after_update);   // [確認_正常系] - 監視対象の間は、範囲の半分で更新を送出すること。
    EXPECT_EQ(2U, count_after_excluded); // [確認_正常系] - 監視対象でなくなった後は送出しないこと。
    EXPECT_EQ((std::vector<uint32_t>{52U, 73U, 102U}), limits);
    // [確認_正常系] - 送出上限が 10 + 42、31 + 42、再び監視対象になった時点の 60 + 42 であること。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// 監視対象の一覧を有効期限の間受け取らなかった場合に、監視対象でなくなることの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, monitor_list_expires_after_lifetime)
{
    // Arrange
    enable_multicast_receiver();
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - multicast の受信者で、容量 0、幅 42、有効期限 1000 [ms] で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    std::vector<uint8_t> payload;
    potr_packet included = make_list(payload, 1);
    received(POTR_PEER_NA, 10U, 0); // [状態] - 次に受信する通番 10 で送信者を採用する。
    potr_internal_flow_control_on_packet(&ctx, nullptr, &included); // [状態] - 時刻 1000 に自身を含む一覧を渡す。
    // Pre-Assert
    // Act
    potr_internal_flow_control_tick(&ctx, 1999U); // [手順] - 有効期限の 1 [ms] 前の時刻 1999 で tick を呼び出す。
    received(POTR_PEER_NA, 31U);                  // [手順] - 次に受信する通番 31 で on_received を呼び出す。
    size_t count_before_expiry = states.size();
    potr_internal_flow_control_tick(&ctx, 2000U); // [手順] - 有効期限ちょうどの時刻 2000 で tick を呼び出す。
    received(POTR_PEER_NA, 60U);                  // [手順] - 次に受信する通番 60 で on_received を呼び出す。
    potr_internal_flow_control_tick(&ctx, 2100U); // [手順] - 時刻 2100 で tick を呼び出す。
    // Assert
    EXPECT_EQ(2U, count_before_expiry); // [確認_正常系] - 期限の前は監視対象として更新を送出すること。
    EXPECT_EQ(2U, states.size());       // [確認_正常系] - 期限の経過の後は送出しないこと。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// 送信者のセッションが変わった場合に、監視対象でなくなることの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, sender_session_change_clears_monitored)
{
    // Arrange
    enable_multicast_receiver();
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - multicast の受信者で、容量 0、幅 42 で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    std::vector<uint8_t> payload;
    potr_packet included = make_list(payload, 1);
    received(POTR_PEER_NA, 10U, 0); // [状態] - 次に受信する通番 10 で送信者を採用する。
    potr_internal_flow_control_on_packet(&ctx, nullptr, &included); // [状態] - 自身を含む一覧を渡して監視対象にする。
    clear_records();
    // Pre-Assert
    // Act
    received(POTR_PEER_NA, 0U, 0, 2U); // [手順] - 新しいセッション 2 の送信者について、PING の受理として on_received を呼び出す。
    received(POTR_PEER_NA, 30U, 1, 2U); // [手順] - 同じセッションで、次に受信する通番 30 で on_received を呼び出す。
    // Assert
    EXPECT_TRUE(states.empty()); // [確認_正常系] - 新しいセッションの送信者へ送出上限を送出しないこと。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// 形式が不正な一覧と、ピアを指定した一覧を破棄することの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, invalid_monitor_list_is_ignored)
{
    // Arrange
    std::vector<uint8_t> valid_payload;
    std::vector<uint8_t> zero_payload;
    std::vector<uint8_t> over_payload(POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE +
                                          POTR_FLOW_CONTROL_MONITOR_ENTRY_SIZE * (POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX + 1U),
                                      0U);
    std::vector<uint8_t> mismatch_payload;
    std::vector<uint8_t> sequence_payload;
    enable_multicast_receiver();
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - multicast の受信者で、容量 0、幅 42 で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    received(POTR_PEER_NA, 10U, 0); // [状態] - 次に受信する通番 10 で送信者を採用する。
    potr_packet valid = make_list(valid_payload, 1);
    potr_packet zero = make_list(zero_payload, 1);
    zero_payload[1] = 0U;
    zero.payload_len = (uint16_t)POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE;
    // [状態] - 台数 0 の一覧を用意する。
    potr_packet over = valid;
    over_payload[0] = (uint8_t)POTR_FLOW_CONTROL_STATE_MONITOR;
    over_payload[1] = (uint8_t)(POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX + 1U);
    over.payload = over_payload.data();
    over.payload_len = (uint16_t)over_payload.size();
    // [状態] - 台数 65 の一覧を用意する。
    potr_packet mismatch = make_list(mismatch_payload, 1);
    mismatch.payload_len = (uint16_t)(mismatch_payload.size() - 1U);
    // [状態] - 台数 2 に対してペイロード長が 1 [byte] 短い一覧を用意する。
    potr_packet sequence = make_list(sequence_payload, 1);
    sequence.seq_num = 1U;
    // [状態] - seq_num が 0 でない一覧を用意する。
    // Pre-Assert
    // Act
    potr_internal_flow_control_on_packet(&ctx, nullptr, &zero);     // [手順] - 台数 0 の一覧を渡す。
    potr_internal_flow_control_on_packet(&ctx, nullptr, &over);     // [手順] - 台数 65 の一覧を渡す。
    potr_internal_flow_control_on_packet(&ctx, nullptr, &mismatch); // [手順] - 長さが合わない一覧を渡す。
    potr_internal_flow_control_on_packet(&ctx, nullptr, &sequence); // [手順] - seq_num が 0 でない一覧を渡す。
    potr_internal_flow_control_on_packet(&ctx, peer10, &valid);     // [手順] - ピアを指定して、正しい一覧を渡す。
    size_t count_after_invalid = states.size();
    potr_internal_flow_control_on_packet(&ctx, nullptr, &valid);    // [手順] - ピアを指定せずに、正しい一覧を渡す。
    // Assert
    EXPECT_EQ(0U, count_after_invalid); // [確認_異常系] - 不正な一覧とピアを指定した一覧では監視対象にならず、送出しないこと。
    EXPECT_EQ(1U, states.size());       // [確認_正常系] - 正しい一覧で監視対象になり、最初の通知を送出すること。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// multicast の送信者が、監視対象の一覧を受信した場合に破棄することの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, sender_ignores_monitor_list)
{
    // Arrange
    std::vector<uint8_t> payload;
    potr_packet included = make_list(payload, 1);
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.role = POTR_ROLE_SENDER;
    ctx.flow_control_config.capacity = 1000U;
    ctx.flow_control_config.receive_buffer_percent = 0U;
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - multicast の送信者で、容量 1000 の資源を start で生成する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_wake_flow(_))
        .Times(0); // [Pre-Assert確認_正常系] - potr_internal_send_queue_wake_flow() が呼び出されないこと。
    // Act
    potr_internal_flow_control_on_packet(&ctx, nullptr, &included); // [手順] - 監視対象の一覧を渡す。
    // Assert
    EXPECT_TRUE(states.empty()); // [確認_正常系] - フロー制御パケットを組み立てないこと。
    EXPECT_EQ(0U, cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE));
    // [確認_正常系] - 送信側の送出上限を変えないこと。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// 採用していない送信者の一覧を破棄することの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, monitor_list_from_other_sender_is_ignored)
{
    // Arrange
    enable_multicast_receiver();
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - multicast の受信者で、容量 0、幅 42 で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    std::vector<uint8_t> other_payload;
    std::vector<uint8_t> adopted_payload;
    potr_packet other = make_list(other_payload, 1, 2U);
    potr_packet adopted = make_list(adopted_payload, 1);
    received(POTR_PEER_NA, 10U, 0); // [状態] - 次に受信する通番 10 で、セッション 1 の送信者を採用する。
    // Pre-Assert
    // Act
    potr_internal_flow_control_on_packet(&ctx, nullptr, &other); // [手順] - 採用していないセッション 2 の送信者の一覧を渡す。
    size_t count_after_other = states.size();
    ctx.peer_session_known = 0;
    potr_internal_flow_control_on_packet(&ctx, nullptr, &adopted); // [手順] - 送信者を追跡していない状態で、セッション 1 の一覧を渡す。
    size_t count_after_unknown = states.size();
    ctx.peer_session_known = 1;
    potr_internal_flow_control_on_packet(&ctx, nullptr, &adopted); // [手順] - 採用しているセッション 1 の送信者の一覧を渡す。
    // Assert
    EXPECT_EQ(0U, count_after_other);   // [確認_異常系] - 採用していない送信者の一覧では監視対象にならないこと。
    EXPECT_EQ(0U, count_after_unknown); // [確認_異常系] - 送信者を追跡していない間の一覧では監視対象にならないこと。
    EXPECT_EQ(1U, states.size());       // [確認_正常系] - 採用している送信者の一覧で監視対象になり、最初の通知を送出すること。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// unicast の受信者が、監視対象の一覧を破棄することの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, unicast_receiver_ignores_monitor_list)
{
    // Arrange
    std::vector<uint8_t> payload;
    potr_packet included = make_list(payload, 1);
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - unicast の受信者で、容量 0、幅 42 で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    received(POTR_PEER_NA, 10U, 0); // [状態] - 次に受信する通番 10 で、最初の通知を送出させる。
    clear_records();
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_wake_flow(_))
        .Times(0); // [Pre-Assert確認_正常系] - 送信側の状態を変えないため、potr_internal_send_queue_wake_flow() が呼び出されないこと。
    // Act
    potr_internal_flow_control_on_packet(&ctx, nullptr, &included); // [手順] - 自身を含む一覧を渡す。
    received(POTR_PEER_NA, 30U); // [手順] - 次に受信する通番 30 で on_received を呼び出す。
    // Assert
    EXPECT_TRUE(states.empty()); // [確認_正常系] - 一覧によって送出が変わらないこと。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// 監視対象でない multicast の受信者も、容量を設定していれば抑制の要求と解除を送出することの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, non_monitored_receiver_still_requests_and_resumes)
{
    // Arrange
    enable_multicast_receiver();
    ctx.flow_control_config.capacity = 1000U;
    ctx.global.max_payload = 100U;
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - multicast の受信者で、容量 1000、要求 80%、解除 60%、max_payload 100 で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    received(POTR_PEER_NA, 10U, 0); // [状態] - 次に受信する通番 10 で送信者を採用する。
    // Pre-Assert
    // Act
    for (int i = 0; i < 9; i++)
    {
        admit(100U);
    }
    // [手順] - 100 [byte] を 9 回 (占有量 900) admit する。
    for (int i = 0; i < 4; i++)
    {
        potr_internal_flow_control_release(&ctx, POTR_PEER_NA, 100U);
    }
    // [手順] - 100 [byte] を 4 回 release する (占有量 500)。
    received(POTR_PEER_NA, 40U); // [手順] - 次に受信する通番 40 で on_received を呼び出す。
    // Assert
    EXPECT_EQ((std::vector<uint8_t>{kSuppress, kResume}), states);
    // [確認_正常系] - 監視対象でなくても、抑制の要求と解除を送出し、その後の更新は送出しないこと。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// multicast の送信者が、監視対象の一覧を、変わった時点と周期で送出し、0 台の間は送出しないことの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, monitor_tick_sends_on_change_and_interval)
{
    // Arrange
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.role = POTR_ROLE_SENDER;
    ctx.n_path = 2;
    size_t list_count = 0U;
    int list_changed = 0;
    std::vector<size_t> built_counts;
    ON_CALL(mock_porter, potr_internal_receiver_monitor_list(_, _, _, _, _))
        .WillByDefault([&](potr_context *, uint64_t, potr_internal_packet_session_hdr *list, size_t,
                           int *changed) -> size_t {
            for (size_t i = 0; i < list_count; i++)
            {
                list[i].session_id = (uint32_t)(i + 1U);
            }
            *changed = list_changed;
            return list_count;
        }); // [状態] - 監視対象の一覧の台数と変化の有無を list_count と list_changed で制御する。
    ON_CALL(mock_porter, potr_internal_packet_build_flow_control_list(_, _, _, _, _))
        .WillByDefault([&](potr_packet *packet, const potr_internal_packet_session_hdr *, uint8_t *payload,
                           const potr_internal_packet_session_hdr *, size_t count) {
            built_counts.push_back(count);
            memset(packet, 0, POTR_PACKET_HEADER_SIZE);
            payload[0] = (uint8_t)POTR_FLOW_CONTROL_STATE_MONITOR;
            packet->payload = payload;
            packet->payload_len = cplat_hton16((uint16_t)1U);
            return POTR_OK;
        }); // [状態] - 台数を記録し、封緘できる最小のパケットを組み立てて POTR_OK を返す。
    // [状態] - multicast の送信者で、経路 2 本、再送周期 200 [ms] とする。
    // Pre-Assert
    // Act
    potr_internal_flow_control_monitor_tick(&ctx, 1000U); // [手順] - 監視対象 0 台で、時刻 1000 に monitor_tick を呼び出す。
    size_t built_at_zero = built_counts.size();
    list_count = 2U;
    list_changed = 1;
    potr_internal_flow_control_monitor_tick(&ctx, 1000U); // [手順] - 監視対象 2 台に変わった状態で、monitor_tick を呼び出す。
    int sent_on_change = sendto_count;
    list_changed = 0;
    potr_internal_flow_control_monitor_tick(&ctx, 1199U); // [手順] - 変化のないまま、周期の 1 [ms] 前の時刻 1199 で呼び出す。
    int sent_before_interval = sendto_count;
    potr_internal_flow_control_monitor_tick(&ctx, 1200U); // [手順] - 周期ちょうどの時刻 1200 で呼び出す。
    // Assert
    EXPECT_EQ(0U, built_at_zero);    // [確認_正常系] - 監視対象 0 台の間は一覧を組み立てないこと。
    EXPECT_EQ(2, sent_on_change);    // [確認_正常系] - 監視対象が変わった時点で、全経路 (2 本) へ送出すること。
    EXPECT_EQ(2, sent_before_interval); // [確認_正常系] - 周期の前は送出しないこと。
    EXPECT_EQ(4, sendto_count);      // [確認_正常系] - 周期ちょうどで、全経路へ送出すること。
    EXPECT_EQ((std::vector<size_t>{2U, 2U}), built_counts); // [確認_正常系] - 2 台の一覧を 2 回組み立てること。
    ASSERT_EQ(4U, destinations.size()); // [確認_正常系] - 送出先が 4 件記録されていること。
    EXPECT_EQ(ctx.dest_addr[0].address, destinations[0].address); // [確認_正常系] - 1 本目の宛先が DATA と同じであること。
    EXPECT_EQ(ctx.dest_addr[1].address, destinations[1].address); // [確認_正常系] - 2 本目の宛先が DATA と同じであること。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// multicast と broadcast の送信者以外では、監視対象の一覧を扱わないことの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, monitor_tick_is_noop_for_other_types)
{
    // Arrange
    // [状態] - unicast の受信者のままとする。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_receiver_monitor_list(_, _, _, _, _))
        .Times(0); // [Pre-Assert確認_正常系] - potr_internal_receiver_monitor_list() が呼び出されないこと。
    // Act
    potr_internal_flow_control_monitor_tick(&ctx, 1000U); // [手順] - unicast の受信者で monitor_tick を呼び出す。
    ctx.service.type = POTR_TYPE_MULTICAST;
    potr_internal_flow_control_monitor_tick(&ctx, 1000U); // [手順] - multicast の受信者で monitor_tick を呼び出す。
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.role = POTR_ROLE_SENDER;
    potr_internal_flow_control_monitor_tick(&ctx, 1000U); // [手順] - unicast の送信者で monitor_tick を呼び出す。
    potr_internal_flow_control_monitor_tick(nullptr, 1000U); // [手順] - ctx を NULL にして monitor_tick を呼び出す。
    // Assert
    EXPECT_EQ(0, sendto_count); // [確認_正常系] - 送出しないこと。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// multicast と broadcast の受信者が、送出上限を使わない構成で「上限なし」を送出しないことの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, multicast_and_broadcast_receivers_do_not_send_unlimited)
{
    // Arrange
    ctx.flow_control_config.receive_buffer_percent = 0U;
    ctx.service.type = POTR_TYPE_MULTICAST;
    // [状態] - multicast 受信者で容量 0、受信バッファーの幅の割合を 0 にする。
    // Pre-Assert
    // Act
    int ret_mc = potr_internal_flow_control_start(&ctx); // [手順] - multicast で start を呼び出す。
    struct potr_internal_flow_control *mc_fc = ctx.flow_control;
    received(POTR_PEER_NA, 10U, 0); // [手順] - multicast で on_received を呼び出す。
    ctx.service.type = POTR_TYPE_BROADCAST;
    int ret_bc = potr_internal_flow_control_start(&ctx); // [手順] - broadcast で start を呼び出す。
    struct potr_internal_flow_control *bc_fc = ctx.flow_control;
    received(POTR_PEER_NA, 10U, 0); // [手順] - broadcast で on_received を呼び出す。
    // Assert
    EXPECT_EQ(POTR_OK, ret_mc);  // [確認_正常系] - multicast の start が POTR_OK であること。
    EXPECT_EQ(nullptr, mc_fc);   // [確認_正常系] - multicast では資源を生成しないこと。
    EXPECT_EQ(POTR_OK, ret_bc);  // [確認_正常系] - broadcast の start が POTR_OK であること。
    EXPECT_EQ(nullptr, bc_fc);   // [確認_正常系] - broadcast では資源を生成しないこと。
    EXPECT_TRUE(states.empty()); // [確認_正常系] - フロー制御パケットを送出しないこと。
    EXPECT_EQ(0, sendto_count);  // [確認_正常系] - ソケット送出を行わないこと。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// 問い合わせに対して、解除、抑制の要求、上限なしの 3 通りの状態と送出上限を 1 回返送することの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, query_responds_with_resume_suppress_and_unlimited)
{
    // Arrange
    uint8_t query_payload[POTR_FLOW_CONTROL_QUERY_PAYLOAD_SIZE] = {kQuery};
    potr_packet query = {};
    query.flags = POTR_FLAG_FLOW_CONTROL;
    query.session_id = 1U;
    query.session_tv_sec = (int64_t)sender_ts.tv_sec;
    query.session_tv_nsec = (int32_t)sender_ts.tv_nsec;
    query.payload = query_payload;
    query.payload_len = POTR_FLOW_CONTROL_QUERY_PAYLOAD_SIZE;
    ctx.peer_session_known = 1;
    ctx.peer_session_id = 1U;
    ctx.peer_session_ts = sender_ts;
    ctx.flow_control_config.capacity = 1000U;
    ctx.flow_control_config.suppress_percent = 80U;
    ctx.flow_control_config.resume_percent = 50U;
    ctx.flow_control_config.receive_buffer_percent = 0U;
    ctx.global.max_payload = 100U;
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    received(POTR_PEER_NA, 20U, 0); // [状態] - 容量 1000、要求なし、次に受信する通番 20 の状態にする (許可数 9)。
    clear_records();
    // Pre-Assert
    // Act
    potr_internal_flow_control_on_packet(&ctx, nullptr, &query); // [手順] - 要求なしの状態で問い合わせを受ける。
    std::vector<uint8_t> states_resume = states;
    std::vector<uint32_t> limits_resume = limits;
    ASSERT_EQ(POTR_OK, admit(850U)); // [手順] - 要求のしきい値 (800) を超えて抑制の要求中へ遷移させる。
    clear_records();
    potr_internal_flow_control_on_packet(&ctx, nullptr, &query); // [手順] - 抑制の要求中の状態で問い合わせを受ける。
    std::vector<uint8_t> states_suppress = states;
    std::vector<uint32_t> limits_suppress = limits;
    potr_internal_flow_control_stop(&ctx);
    clear_records();
    ctx.flow_control_config.capacity = 0U;
    ctx.flow_control_config.receive_buffer_percent = 0U;
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 送出上限を使わない受信側 (容量 0、受信バッファーの幅の割合 0) に切り替える。
    potr_internal_flow_control_on_packet(&ctx, nullptr, &query); // [手順] - 送出上限を使わない状態で問い合わせを受ける。
    // Assert
    EXPECT_EQ(std::vector<uint8_t>{kResume}, states_resume);
    // [確認_正常系] - 要求なしの応答が状態「解除」であること。
    EXPECT_EQ(std::vector<uint32_t>{29U}, limits_resume);
    // [確認_正常系] - 要求なしの送出上限が 20 + 9 = 29 であること。
    EXPECT_EQ(std::vector<uint8_t>{kSuppress}, states_suppress);
    // [確認_正常系] - 抑制の要求中の応答が状態「抑制の要求」であること。
    EXPECT_EQ(std::vector<uint32_t>{20U}, limits_suppress);
    // [確認_正常系] - 抑制の要求中の送出上限が 20 + 0 = 20 であること。
    EXPECT_EQ(std::vector<uint8_t>{kUnlimited}, states);
    // [確認_正常系] - 送出上限を使わない場合の応答が状態「上限なし」であること。
    EXPECT_EQ(std::vector<uint32_t>{0U}, limits);
    // [確認_正常系] - 上限なしの送出上限の欄が 0 であること。
    EXPECT_EQ(1, sendto_count); // [確認_正常系] - 上限なしを 1 回送出すること。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// 問い合わせに対して、要求なしの状態で解除と送出上限を 1 回返送し、採用していないセッションの問い合わせを受理しないことの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, query_responds_resume_and_ignores_other_sessions)
{
    // Arrange
    uint8_t query_payload[POTR_FLOW_CONTROL_QUERY_PAYLOAD_SIZE] = {kQuery};
    potr_packet adopted = {};
    potr_packet other_id = {};
    potr_packet other_ts = {};
    adopted.flags = POTR_FLAG_FLOW_CONTROL;
    adopted.session_id = 1U;
    adopted.session_tv_sec = (int64_t)sender_ts.tv_sec;
    adopted.session_tv_nsec = (int32_t)sender_ts.tv_nsec;
    adopted.payload = query_payload;
    adopted.payload_len = POTR_FLOW_CONTROL_QUERY_PAYLOAD_SIZE;
    other_id = adopted;
    other_id.session_id = 2U;
    other_ts = adopted;
    other_ts.session_tv_nsec = (int32_t)sender_ts.tv_nsec + 1;
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    received(POTR_PEER_NA, 10U, 0); // [状態] - 幅 42、次に受信する通番 10 で採用する (送出上限 52)。
    clear_records();
    // Pre-Assert
    // Act
    ctx.peer_session_known = 0;
    potr_internal_flow_control_on_packet(&ctx, nullptr, &adopted); // [手順] - 相手セッション未採用の状態で問い合わせを受ける。
    ctx.peer_session_known = 1;
    ctx.peer_session_id = 1U;
    ctx.peer_session_ts = sender_ts;
    potr_internal_flow_control_on_packet(&ctx, nullptr, &other_id); // [手順] - session_id が異なる問い合わせを受ける。
    potr_internal_flow_control_on_packet(&ctx, nullptr, &other_ts); // [手順] - session_ts が異なる問い合わせを受ける。
    size_t count_before_adopted = states.size();
    potr_internal_flow_control_on_packet(&ctx, nullptr, &adopted); // [手順] - 採用しているセッションからの問い合わせを受ける。
    // Assert
    EXPECT_EQ(0U, count_before_adopted); // [確認_異常系] - 未採用および他セッションの問い合わせには応答しないこと。
    EXPECT_EQ(std::vector<uint8_t>{kResume}, states); // [確認_正常系] - 採用しているセッションには状態「解除」で応答すること。
    EXPECT_EQ(std::vector<uint32_t>{52U}, limits);    // [確認_正常系] - 送出上限が 10 + 42 = 52 であること。
    EXPECT_EQ(1, sendto_count);                       // [確認_正常系] - 1 回だけ送出すること。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// 送信側が「上限なし」を受けると保持していた送出上限と抑制の要求を解除することの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, sender_clears_limit_on_unlimited)
{
    // Arrange
    uint8_t suppress_payload[POTR_FLOW_CONTROL_PAYLOAD_SIZE] = {kSuppress, 0U, 0U, 0U, 10U};
    uint8_t unlimited_payload[POTR_FLOW_CONTROL_PAYLOAD_SIZE] = {kUnlimited, 0U, 0U, 0U, 0U};
    potr_packet suppress_pkt = {};
    potr_packet unlimited_pkt = {};
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    suppress_pkt.flags = POTR_FLAG_FLOW_CONTROL;
    suppress_pkt.payload = suppress_payload;
    suppress_pkt.payload_len = POTR_FLOW_CONTROL_PAYLOAD_SIZE;
    unlimited_pkt.flags = POTR_FLAG_FLOW_CONTROL;
    unlimited_pkt.payload = unlimited_payload;
    unlimited_pkt.payload_len = POTR_FLOW_CONTROL_PAYLOAD_SIZE;
    potr_internal_flow_control_on_packet(&ctx, nullptr, &suppress_pkt);
    // [状態] - 抑制の要求と送出上限 10 を保持した状態にする。
    // Pre-Assert
    // Act
    potr_internal_flow_control_on_packet(&ctx, nullptr, &unlimited_pkt); // [手順] - 状態「上限なし」を受ける。
    int wait_ms = 0;
    uint32_t allowance = 0U;
    int suppressed = potr_internal_flow_control_suppressed(&ctx, now_ms, &wait_ms, &allowance);
    // [手順] - 抑制の判定を行う。
    // Assert
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNLIMITED, cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE));
    // [確認_正常系] - 送出上限の状態が「上限なし (通知済み)」になること。
    EXPECT_EQ(0U, cplat_atomic_load_u64(&ctx.flow_request_expire_ms, CPLAT_MEMORY_ORDER_ACQUIRE));
    // [確認_正常系] - 抑制の要求の期限が 0 に戻ること。
    EXPECT_EQ(0, suppressed);           // [確認_正常系] - 抑制されないこと。
    EXPECT_EQ(UINT32_MAX, allowance);   // [確認_正常系] - 送出可能数が UINT32_MAX であること。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]

// 受信側が最後の DATA 受理から有効期限の半分以上途切れた後の DATA で更新を送出し、未満および TCP では送出しないことの確認
// [サブ手順参照 名前=potrFlowControlBufferTest.SetUp]
TEST_F(potrFlowControlBufferTest, receiver_updates_after_half_lifetime_idle_gap)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - unicast 受信者、幅 42、有効期限 1000 [ms] (半分は 500 [ms]) で start する。
    now_ms = 1000U;
    received(POTR_PEER_NA, 10U, 1); // [状態] - 時刻 1000 に初回 DATA を受理する (送出上限 52 を初回送出)。
    clear_records();
    // Pre-Assert
    // Act
    now_ms = 1499U;
    received(POTR_PEER_NA, 11U, 1); // [手順] - 半分未満 (499 [ms] 後) に次の DATA を受理する。
    size_t count_before_half = states.size();
    now_ms = 1999U;
    received(POTR_PEER_NA, 12U, 1); // [手順] - 前回受理 (1499) からちょうど 500 [ms] 後に DATA を受理する。
    std::vector<uint8_t> udp_states = states;
    std::vector<uint32_t> udp_limits = limits;
    potr_internal_flow_control_stop(&ctx);
    clear_records();
    ctx.service.type = POTR_TYPE_TCP;
    ctx.flow_control_config.capacity = 100000U;
    ctx.flow_control_config.receive_buffer_percent = 0U;
    ctx.tcp_conn_fd[0] = 5;
    cplat_local_lock_create(&ctx.tcp_send_mutex[0]);
    ON_CALL(mock_cplat, cplat_socket_send_all(_, _, _, _)).WillByDefault(Return(CPLAT_OK));
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    now_ms = 1000U;
    received(POTR_PEER_NA, 10U, 1);
    clear_records();
    now_ms = 2000U;
    received(POTR_PEER_NA, 11U, 1);
    size_t tcp_count = states.size();
    cplat_local_lock_dispose(ctx.tcp_send_mutex[0]);
    ctx.tcp_send_mutex[0] = nullptr;
    // [手順] - TCP 受信側では有効期限の半分以上たっても途中更新を送出しないことを確認する。
    // Assert
    EXPECT_EQ(0U, count_before_half);                // [確認_正常系] - 半分未満 (499 [ms]) では更新を送出しないこと。
    EXPECT_EQ(std::vector<uint8_t>{kResume}, udp_states); // [確認_正常系] - ちょうど半分 (500 [ms]) で更新を送出すること。
    EXPECT_EQ(std::vector<uint32_t>{54U}, udp_limits);    // [確認_正常系] - 送出上限が 12 + 42 = 54 であること。
    EXPECT_EQ(0U, tcp_count);                        // [確認_正常系] - TCP では途絶による更新を送出しないこと。
}
// [サブ手順参照 名前=potrFlowControlBufferTest.TearDown]
