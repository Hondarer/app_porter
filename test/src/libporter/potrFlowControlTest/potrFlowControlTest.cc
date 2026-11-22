/**
 *******************************************************************************
 *  @file           potrFlowControlTest.cc
 *  @brief          受信側の占有量の管理と抑制の要求の送出、送信側の要求の反映と判定を検証します。
 *
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
#include <porter/potr_peer_table.h>
#include <porter/protocol/packet.h>
#include <cplat/net/byteorder.h>
#include <cstring>
#include <vector>
extern "C"
{
#include "thread_recv_session.h"
}
using namespace testing;

namespace
{
const uint8_t kSuppress = (uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS;
const uint8_t kResume = (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME;
// 送出上限 0x0000012C (300) を載せた、抑制の要求と解除のペイロード
const uint8_t kSuppressPayload[POTR_FLOW_CONTROL_PAYLOAD_SIZE] = {kSuppress, 0x00U, 0x00U, 0x01U, 0x2CU};
const uint8_t kResumePayload[POTR_FLOW_CONTROL_PAYLOAD_SIZE] = {kResume, 0x00U, 0x00U, 0x01U, 0x2CU};
} // namespace

class potrFlowControlTest : public Test
{
  protected:
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx;
    uint64_t now_ms = 1000U;
    std::vector<uint8_t> states;        // フロー制御パケットの組み立てで渡された状態の履歴
    std::vector<uint32_t> limits;       // フロー制御パケットの組み立てで渡された送出上限の履歴
    std::vector<potr_peer_id> find_ids; // potr_internal_peer_find_by_id() に渡されたピア識別子の履歴
    int sendto_count = 0;               // cplat_socket_sendto() の呼び出し回数
    int sendto_result = CPLAT_OK;       // cplat_socket_sendto() が返す値
    cplat_ipv4_endpoint last_dest = {}; // 直近の送出先
    uint32_t last_session_id = 0U;      // 直近に組み立てたパケットのセッション識別子
    uint32_t allowance = 0U;            // suppressed が返した、送出上限までに送出できる数
    potr_internal_peer_context *peer10 = nullptr;
    potr_internal_peer_context *peer11 = nullptr;
    cplat_timespec session_ts = {};

    // [サブ手順 名前=potrFlowControlTest.SetUp]
    void SetUp() override
    {
        memset(&ctx, 0, sizeof(ctx));
        ctx.service.type = POTR_TYPE_UNICAST;
        ctx.service.service_id = 1;
        ctx.service.src_port = 5000;
        ctx.role = POTR_ROLE_RECEIVER;
        ctx.n_path = 1;
        ctx.sock[0] = 5;
        ctx.src_addr_resolved[0] = 0x0100007FU;
        ctx.dest_addr[0].address = 0x0200007FU;
        ctx.dest_addr[0].port = cplat_hton16(6000);
        ctx.flow_control_config.capacity = 1000U;
        ctx.flow_control_config.suppress_percent = 80U;
        ctx.flow_control_config.resume_percent = 60U;
        ctx.flow_control_config.request_interval_ms = 200U;
        ctx.flow_control_config.request_lifetime_ms = 1000U;
        ctx.global.max_payload = 100U; // [状態] - 許可数の計算に使う max_payload を 100 にする (要求のしきい値 800 で許可数 9)。
        session_ts.tv_sec = 100;
        session_ts.tv_nsec = 5;
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
                last_dest = *dest;
                if (sent != nullptr)
                {
                    *sent = len;
                }
                return sendto_result;
            }); // [状態] - 送出回数と宛先を記録して sendto_result (既定は成功) を返す。
        ON_CALL(mock_porter, potr_internal_packet_build_flow_control(_, _, _, _, _))
            .WillByDefault([this](potr_packet *packet, const potr_internal_packet_session_hdr *shdr, uint8_t *payload,
                                  uint8_t state, uint32_t send_limit) {
                states.push_back(state);
                limits.push_back(send_limit);
                last_session_id = shdr->session_id;
                memset(payload, 0, POTR_FLOW_CONTROL_PAYLOAD_SIZE);
                payload[0] = state;
                memset(packet, 0, POTR_PACKET_HEADER_SIZE);
                packet->payload = payload;
                packet->payload_len = cplat_hton16((uint16_t)POTR_FLOW_CONTROL_PAYLOAD_SIZE);
                return POTR_OK;
            }); // [状態] - 状態と送出上限を記録し、封緘できる最小のパケットを組み立てて POTR_OK を返す。
        ON_CALL(mock_porter, potr_internal_peer_find_by_id(_, _))
            .WillByDefault([this](const potr_context *, potr_peer_id id) -> potr_internal_peer_context * {
                find_ids.push_back(id);
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
    }
    // [サブ手順終了]

    // [サブ手順 名前=potrFlowControlTest.TearDown]
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
    void enable_multi_peer()
    {
        ctx.is_multi_peer = 1;
        ctx.service.type = POTR_TYPE_UNICAST_BIDIR_N1;
        cplat_local_lock_create(&ctx.peers_mutex);
    }

    // admit を呼び出す。N:1 モードでは呼び出し元の契約に従い peers_mutex を保持する。
    int admit(potr_peer_id peer_id, size_t len, uint32_t session_id = 1U, const cplat_timespec *ts = nullptr)
    {
        int ret;

        if (ts == nullptr)
        {
            ts = &session_ts;
        }
        if (ctx.is_multi_peer)
        {
            cplat_local_lock_lock(ctx.peers_mutex);
        }
        ret = potr_internal_flow_control_admit(&ctx, peer_id, session_id, ts, len);
        if (ctx.is_multi_peer)
        {
            cplat_local_lock_unlock(ctx.peers_mutex);
        }
        return ret;
    }

    // len [byte] を count 回 admit する。戻り値の履歴を返す。
    std::vector<int> admit_repeat(potr_peer_id peer_id, size_t len, int count)
    {
        std::vector<int> rets;
        for (int i = 0; i < count; i++)
        {
            rets.push_back(admit(peer_id, len));
        }
        return rets;
    }

    int refused(potr_peer_id peer_id, uint32_t session_id, const cplat_timespec *ts)
    {
        return potr_internal_flow_control_refused(&ctx, peer_id, session_id, ts);
    }

    // on_received を呼び出す。N:1 モードでは呼び出し元の契約に従い peers_mutex を保持する。
    void received(potr_peer_id peer_id, uint32_t next_seq, int data_packet = 1, uint32_t session_id = 1U,
                  size_t expanded_size = 0U, const cplat_timespec *ts = nullptr)
    {
        if (ts == nullptr)
        {
            ts = &session_ts;
        }
        if (ctx.is_multi_peer)
        {
            cplat_local_lock_lock(ctx.peers_mutex);
        }
        potr_internal_flow_control_on_received(&ctx, peer_id, session_id, ts, next_seq, data_packet, expanded_size);
        if (ctx.is_multi_peer)
        {
            cplat_local_lock_unlock(ctx.peers_mutex);
        }
    }

    // 送出の記録を消す。
    void clear_records()
    {
        states.clear();
        limits.clear();
        sendto_count = 0;
    }

    static potr_packet make_packet(const uint8_t *payload, uint16_t len = (uint16_t)POTR_FLOW_CONTROL_PAYLOAD_SIZE)
    {
        potr_packet pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.payload = payload;
        pkt.payload_len = len;
        return pkt;
    }
};

// 容量が 0 の場合は資源を生成せず、0 以外の場合は生成することの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, start_creates_resource_only_when_capacity_is_set)
{
    // Arrange
    potr_context disabled;
    memset(&disabled, 0, sizeof(disabled));
    // [状態] - 容量が 0 のコンテキストを用意する。
    // Pre-Assert
    // Act
    int actual_ret_disabled = potr_internal_flow_control_start(&disabled); // [手順] - 容量 0 で start を呼び出す。
    int actual_ret_enabled = potr_internal_flow_control_start(&ctx);       // [手順] - 容量 1000 で start を呼び出す。
    int actual_ret_null = potr_internal_flow_control_start(nullptr);       // [手順] - ctx を NULL にして start を呼び出す。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_disabled);                // [確認_正常系] - 容量 0 で POTR_OK であること。
    EXPECT_EQ(nullptr, disabled.flow_control);              // [確認_正常系] - 容量 0 で flow_control が NULL のままであること。
    EXPECT_EQ(POTR_OK, actual_ret_enabled);                 // [確認_正常系] - 容量 1000 で POTR_OK であること。
    EXPECT_NE(nullptr, ctx.flow_control);                   // [確認_正常系] - 容量 1000 で flow_control が NULL でないこと。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret_null);  // [確認_異常系] - ctx が NULL で POTR_ERR_INVALID_ARGUMENT であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// フロー制御が無効の場合に、admit が POTR_OK、refused が 0 を返し、release と tick が何もしないことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, disabled_is_noop)
{
    // Arrange
    ctx.flow_control = nullptr;
    // [状態] - flow_control が NULL (無効) のままにする。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_sendto(_, _, _, _, _, _))
        .Times(0); // [Pre-Assert確認_異常系] - cplat_socket_sendto() が呼び出されないこと。
    // Act
    int actual_ret_admit = admit(POTR_PEER_NA, 5000U); // [手順] - 容量を超える長さで admit を呼び出す。
    int actual_refused = refused(POTR_PEER_NA, 1U, &session_ts); // [手順] - refused を呼び出す。
    potr_internal_flow_control_release(&ctx, POTR_PEER_NA, 100U); // [手順] - release を呼び出す。
    potr_internal_flow_control_tick(&ctx, 99999U);                // [手順] - tick を呼び出す。
    received(POTR_PEER_NA, 10U);                                  // [手順] - on_received を呼び出す。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_admit); // [確認_正常系] - admit が POTR_OK であること。
    EXPECT_EQ(0, actual_refused);         // [確認_正常系] - refused が 0 であること。
    EXPECT_TRUE(states.empty());          // [確認_正常系] - フロー制御パケットを組み立てないこと。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 占有量が 80% ちょうどまでは要求せず、801 で 1 回だけ要求することの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, suppress_request_at_threshold_boundary)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 容量 1000、要求 80%、解除 60% で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    // Pre-Assert
    // Act
    std::vector<int> rets = admit_repeat(POTR_PEER_NA, 100U, 8); // [手順] - 100 [byte] を 8 回 (占有量 800) admit する。
    size_t states_at_800 = states.size();
    int sendto_at_800 = sendto_count;
    int actual_ret_over = admit(POTR_PEER_NA, 1U); // [手順] - 1 [byte] を admit する (占有量 801)。
    // Assert
    EXPECT_EQ(std::vector<int>(8, POTR_OK), rets); // [確認_正常系] - 8 回とも POTR_OK であること。
    EXPECT_EQ(0U, states_at_800);                  // [確認_正常系] - 占有量 800 までは状態を組み立てないこと。
    EXPECT_EQ(0, sendto_at_800);                   // [確認_正常系] - 占有量 800 までは送出しないこと。
    EXPECT_EQ(POTR_OK, actual_ret_over);           // [確認_正常系] - 801 の admit が POTR_OK であること。
    EXPECT_EQ(std::vector<uint8_t>{kSuppress}, states); // [確認_正常系] - 抑制の要求を 1 回だけ組み立てること。
    EXPECT_EQ(1, sendto_count);                    // [確認_正常系] - 1 回送出すること。
    EXPECT_EQ(cplat_hton16(5000), last_dest.port); // [確認_正常系] - 送出先のポートが src_port であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 要求中にさらに admit しても、要求を重ねて送出しないことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, request_is_not_repeated_while_requesting)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 既定の構成で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    // Pre-Assert
    // Act
    admit_repeat(POTR_PEER_NA, 100U, 9); // [手順] - 100 [byte] を 9 回 (占有量 900) admit し、要求を発生させる。
    size_t states_after_request = states.size();
    admit_repeat(POTR_PEER_NA, 10U, 5); // [手順] - 10 [byte] をさらに 5 回 admit する。
    // Assert
    EXPECT_EQ(1U, states_after_request); // [確認_正常系] - 最初の要求が 1 回であること。
    EXPECT_EQ(std::vector<uint8_t>{kSuppress}, states); // [確認_正常系] - 要求中の admit で状態を追加しないこと。
    EXPECT_EQ(1, sendto_count);          // [確認_正常系] - 送出が 1 回のままであること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 占有量が 60% ちょうどまでは解除せず、599 で 1 回だけ解除することの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, resume_at_threshold_boundary)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 既定の構成で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    admit_repeat(POTR_PEER_NA, 100U, 9); // [状態] - 占有量 900 にして要求中にする。
    states.clear();
    sendto_count = 0;
    // Pre-Assert
    // Act
    for (int i = 0; i < 3; i++)
    {
        potr_internal_flow_control_release(&ctx, POTR_PEER_NA, 100U);
    }
    // [手順] - 100 [byte] を 3 回 release する (占有量 600)。
    size_t states_at_600 = states.size();
    potr_internal_flow_control_release(&ctx, POTR_PEER_NA, 1U); // [手順] - 1 [byte] を release する (占有量 599)。
    size_t states_at_599 = states.size();
    potr_internal_flow_control_release(&ctx, POTR_PEER_NA, 1U); // [手順] - さらに 1 [byte] を release する (占有量 598)。
    // Assert
    EXPECT_EQ(0U, states_at_600);                         // [確認_正常系] - 占有量 600 では解除を送出しないこと。
    EXPECT_EQ(1U, states_at_599);                         // [確認_正常系] - 占有量 599 で状態を 1 回組み立てること。
    EXPECT_EQ(std::vector<uint8_t>{kResume}, states);     // [確認_正常系] - 状態が解除であり、以後の release で追加しないこと。
    EXPECT_EQ(1, sendto_count);                           // [確認_正常系] - 解除の送出が 1 回であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 要求中は再送周期の経過後に tick が要求を再送し、経過前と要求なしの間は送出しないことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, tick_resends_request_after_interval)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 既定の構成で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    potr_internal_flow_control_tick(&ctx, 5000U); // [状態] - 要求していない間に tick を呼ぶ。
    size_t states_without_request = states.size();
    now_ms = 1000U;
    admit_repeat(POTR_PEER_NA, 100U, 9); // [状態] - 時刻 1000 で占有量 900 にして要求を発生させる (次の再送は 1200)。
    // Pre-Assert
    // Act
    potr_internal_flow_control_tick(&ctx, 1199U); // [手順] - 再送周期の 1 [ms] 前に tick を呼ぶ。
    size_t states_before = states.size();
    potr_internal_flow_control_tick(&ctx, 1200U); // [手順] - 再送周期ちょうどで tick を呼ぶ。
    size_t states_at_interval = states.size();
    potr_internal_flow_control_tick(&ctx, 1399U); // [手順] - 次の再送周期の 1 [ms] 前に tick を呼ぶ。
    size_t states_after_resend = states.size();
    // Assert
    EXPECT_EQ(0U, states_without_request);  // [確認_正常系] - 要求していない間の tick は送出しないこと。
    EXPECT_EQ(1U, states_before);           // [確認_正常系] - 再送周期の前は再送しないこと。
    EXPECT_EQ(2U, states_at_interval);      // [確認_正常系] - 再送周期の経過後に 1 回再送すること。
    EXPECT_EQ(2U, states_after_resend);     // [確認_正常系] - 再送後の次の周期までは再送しないこと。
    EXPECT_EQ((std::vector<uint8_t>{kSuppress, kSuppress}), states); // [確認_正常系] - 再送した状態が抑制の要求であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 容量ちょうどまでは受け入れ、1 [byte] 超えると POTR_ERR_FULL と refused 1 になることの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, capacity_boundary)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 既定の構成で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    // Pre-Assert
    // Act
    admit_repeat(POTR_PEER_NA, 100U, 9); // [手順] - 占有量 900 にする。
    int actual_ret_exact = admit(POTR_PEER_NA, 100U);       // [手順] - 100 [byte] を admit する (占有量 1000)。
    int refused_after_exact = refused(POTR_PEER_NA, 1U, &session_ts);
    int actual_ret_over = admit(POTR_PEER_NA, 1U);          // [手順] - 容量を 1 [byte] 超える admit を呼び出す。
    int refused_after_over = refused(POTR_PEER_NA, 1U, &session_ts);
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret_exact);      // [確認_正常系] - ちょうど容量になる admit が POTR_OK であること。
    EXPECT_EQ(0, refused_after_exact);         // [確認_正常系] - ちょうど容量では refused が 0 であること。
    EXPECT_EQ(POTR_ERR_FULL, actual_ret_over); // [確認_異常系] - 容量を超える admit が POTR_ERR_FULL であること。
    EXPECT_EQ(1, refused_after_over);          // [確認_異常系] - 容量超過の後に refused が 1 であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 容量超過の admit が占有量に加算されず、解除のしきい値を下回ると refused が 0 に戻ることの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, refused_clears_below_resume_threshold)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 既定の構成で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    admit_repeat(POTR_PEER_NA, 100U, 9); // [状態] - 占有量 900 にする。
    int actual_ret_over = admit(POTR_PEER_NA, 101U); // [状態] - 101 [byte] を admit して容量超過にする (加算されない)。
    ASSERT_EQ(POTR_ERR_FULL, actual_ret_over);
    // [状態確認] - admit の戻り値が POTR_ERR_FULL であること。
    // Pre-Assert
    // Act
    potr_internal_flow_control_release(&ctx, POTR_PEER_NA, 300U); // [手順] - 300 [byte] を release する (占有量 600)。
    int refused_at_600 = refused(POTR_PEER_NA, 1U, &session_ts);
    potr_internal_flow_control_release(&ctx, POTR_PEER_NA, 1U); // [手順] - 1 [byte] を release する (占有量 599)。
    int refused_at_599 = refused(POTR_PEER_NA, 1U, &session_ts);
    // Assert
    EXPECT_EQ(1, refused_at_600); // [確認_正常系] - 占有量 600 (解除のしきい値ちょうど) では refused が 1 のままであること。
    EXPECT_EQ(0, refused_at_599); // [確認_正常系] - 占有量 599 で refused が 0 に戻ること。(超過した 101 [byte] を加算していれば 599 にならず、0 に戻らないことの確認)
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 片方向 UDP では、要求していなくても容量超過で要求を送出し、tick で再送が続くことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, oneway_udp_requests_on_overflow_and_keeps_resending)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 片方向 UDP (POTR_TYPE_UNICAST) の既定の構成で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    now_ms = 1000U;
    // Pre-Assert
    // Act
    int actual_ret = admit(POTR_PEER_NA, 1001U); // [手順] - 占有量 0 のまま容量を超える 1001 [byte] を admit する。
    size_t states_after_admit = states.size();
    potr_internal_flow_control_tick(&ctx, 1200U); // [手順] - 再送周期の経過後に tick を呼ぶ。
    potr_internal_flow_control_tick(&ctx, 1400U); // [手順] - さらに再送周期の経過後に tick を呼ぶ。
    // Assert
    EXPECT_EQ(POTR_ERR_FULL, actual_ret); // [確認_異常系] - admit が POTR_ERR_FULL であること。
    EXPECT_EQ(1U, states_after_admit);    // [確認_正常系] - 容量超過の時点で要求を 1 回送出すること。
    EXPECT_EQ((std::vector<uint8_t>{kSuppress, kSuppress, kSuppress}), states); // [確認_正常系] - tick ごとに要求を再送すること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 双方向 UDP では、容量超過の後は tick で再送しないことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, bidir_udp_stops_requesting_after_overflow)
{
    // Arrange
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    // [状態] - 双方向 UDP にする (返送先は dest_addr[0])。
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 既定の構成で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    now_ms = 1000U;
    // Pre-Assert
    // Act
    admit_repeat(POTR_PEER_NA, 100U, 9); // [手順] - 占有量 900 にして要求を発生させる。
    size_t states_after_request = states.size();
    int actual_ret = admit(POTR_PEER_NA, 101U); // [手順] - 容量を超える 101 [byte] を admit する。
    potr_internal_flow_control_tick(&ctx, 1200U); // [手順] - 再送周期の経過後に tick を呼ぶ。
    potr_internal_flow_control_tick(&ctx, 5000U); // [手順] - さらに十分に経過してから tick を呼ぶ。
    // Assert
    EXPECT_EQ(1U, states_after_request);  // [確認_正常系] - 容量超過の前に要求を 1 回送出していること。
    EXPECT_EQ(POTR_ERR_FULL, actual_ret); // [確認_異常系] - admit が POTR_ERR_FULL であること。
    EXPECT_EQ(1U, states.size());         // [確認_正常系] - 容量超過の後は tick で再送しないこと。
    EXPECT_EQ(cplat_hton16(6000), last_dest.port); // [確認_正常系] - 送出先が dest_addr[0] であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// N:1 モードで占有量がピアごとに数えられ、要求と tick の再送が該当するピア宛になることの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, multi_peer_counts_occupancy_per_peer)
{
    // Arrange
    enable_multi_peer();
    // [状態] - N:1 モード (POTR_TYPE_UNICAST_BIDIR_N1) にし、peers_mutex を生成する。
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 既定の構成で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    now_ms = 1000U;
    // Pre-Assert
    // Act
    admit(11U, 700U); // [手順] - ピア 11 へ 700 [byte] を admit する。
    admit(10U, 801U); // [手順] - ピア 10 へ 801 [byte] を admit する (要求のしきい値超過)。
    size_t states_after_peer10 = states.size();
    std::vector<potr_peer_id> find_after_peer10 = find_ids;
    uint32_t session_after_peer10 = last_session_id;
    admit(11U, 100U); // [手順] - ピア 11 へ 100 [byte] を admit する (ピア 11 は 800 で、しきい値を超えない)。
    size_t states_after_peer11 = states.size();
    potr_internal_flow_control_tick(&ctx, 1200U); // [手順] - 再送周期の経過後に tick を呼ぶ (peers_mutex は呼び出し元で保持しない)。
    std::vector<potr_peer_id> find_after_tick = find_ids;
    potr_internal_flow_control_release(&ctx, 10U, 202U); // [手順] - ピア 10 から 202 [byte] を release する (占有量 599)。
    // Assert
    EXPECT_EQ(1U, states_after_peer10);   // [確認_正常系] - ピア 10 の admit だけが要求を送出すること。
    EXPECT_EQ((std::vector<potr_peer_id>{10U}), find_after_peer10); // [確認_正常系] - 送出先の検索がピア 10 だけであること。
    EXPECT_EQ(0x1010U, session_after_peer10); // [確認_正常系] - ピア 10 のセッション識別子で組み立てること。
    EXPECT_EQ(1U, states_after_peer11);   // [確認_正常系] - ピア 11 の admit は要求を送出しないこと。
    EXPECT_EQ((std::vector<potr_peer_id>{10U, 10U}), find_after_tick); // [確認_正常系] - tick の再送がピア 10 宛だけであること。
    EXPECT_EQ((std::vector<uint8_t>{kSuppress, kSuppress, kResume}), states); // [確認_正常系] - 要求、再送、解除の順で送出すること。
    EXPECT_EQ((std::vector<potr_peer_id>{10U, 10U, 10U}), find_ids); // [確認_正常系] - 解除もピア 10 宛であること。
    EXPECT_EQ(cplat_hton16(7010), last_dest.port); // [確認_正常系] - 送出先がピア 10 の dest_addr であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// N:1 モードで容量超過した相手を、ピア識別子またはセッション識別子で照合することの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, multi_peer_refused_matches_peer_or_session)
{
    // Arrange
    enable_multi_peer();
    // [状態] - N:1 モードにし、peers_mutex を生成する。
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 既定の構成で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    cplat_timespec other_ts = {};
    other_ts.tv_sec = 101;
    other_ts.tv_nsec = 5;
    // Pre-Assert
    // Act
    int actual_ret = admit(10U, 1001U, 77U, &session_ts); // [手順] - ピア 10 (セッション 77) へ容量を超える 1001 [byte] を admit する。
    int refused_same_peer = refused(10U, 0U, &other_ts); // [手順] - 同じ peer_id で別のセッションを照合する。
    int refused_same_session = refused(12U, 77U, &session_ts); // [手順] - 別の peer_id で同じセッションを照合する。
    int refused_other_session_id = refused(12U, 78U, &session_ts); // [手順] - 別の peer_id と別のセッション識別子で照合する。
    int refused_other_session_ts = refused(12U, 77U, &other_ts); // [手順] - 別の peer_id と別のセッション開始時刻で照合する。
    // Assert
    EXPECT_EQ(POTR_ERR_FULL, actual_ret); // [確認_異常系] - admit が POTR_ERR_FULL であること。
    EXPECT_EQ(1, refused_same_peer);      // [確認_正常系] - 同じ peer_id で 1 であること。
    EXPECT_EQ(1, refused_same_session);   // [確認_正常系] - 同じセッション識別子とセッション開始時刻で 1 であること。
    EXPECT_EQ(0, refused_other_session_id); // [確認_正常系] - 別のセッション識別子で 0 であること。
    EXPECT_EQ(0, refused_other_session_ts); // [確認_正常系] - 別のセッション開始時刻で 0 であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 抑制の要求で期限が「現在時刻 + 有効期限」になり、解除で 0 に戻り、送信スレッドを起こすことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, on_packet_updates_context_expiry)
{
    // Arrange
    now_ms = 5000U;
    potr_packet suppress = make_packet(kSuppressPayload);
    potr_packet resume = make_packet(kResumePayload);
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_wake_flow(_))
        .Times(2); // [Pre-Assert確認_正常系] - potr_internal_send_queue_wake_flow() が 2 回呼び出されること。
    // Act
    potr_internal_flow_control_on_packet(&ctx, nullptr, &suppress); // [手順] - 抑制の要求 (送出上限 300) を渡す。
    uint64_t expire_after_suppress = cplat_atomic_load_u64(&ctx.flow_request_expire_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
    uint64_t limit_after_suppress = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    now_ms = 5100U;
    potr_internal_flow_control_on_packet(&ctx, nullptr, &resume); // [手順] - 時刻 5100 に解除 (送出上限 300) を渡す。
    uint64_t expire_after_resume = cplat_atomic_load_u64(&ctx.flow_request_expire_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
    uint64_t limit_after_resume = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    uint64_t received_after_resume = cplat_atomic_load_u64(&ctx.flow_limit_received_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
    // Assert
    EXPECT_EQ(6000U, expire_after_suppress); // [確認_正常系] - 期限が 5000 + 1000 であること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 300U, limit_after_suppress); // [確認_正常系] - 抑制の要求の送出上限 300 を保持すること。
    EXPECT_EQ(0U, expire_after_resume);      // [確認_正常系] - 解除で期限が 0 になること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 300U, limit_after_resume); // [確認_正常系] - 解除でも送出上限を上書きすること。
    EXPECT_EQ(5100U, received_after_resume); // [確認_正常系] - 最後に受けた時刻 5100 を記録すること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// peer を渡した場合に、peer 側だけが更新されることの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, on_packet_updates_peer_expiry_only)
{
    // Arrange
    now_ms = 5000U;
    cplat_atomic_store_u64(&ctx.flow_request_expire_ms, 777U, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - コンテキスト側の期限を 777 にする。
    potr_packet suppress = make_packet(kSuppressPayload);
    potr_packet resume = make_packet(kResumePayload);
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_wake_flow(_))
        .Times(2); // [Pre-Assert確認_正常系] - potr_internal_send_queue_wake_flow() が 2 回呼び出されること。
    // Act
    potr_internal_flow_control_on_packet(&ctx, peer10, &suppress); // [手順] - peer を指定して抑制の要求を渡す。
    uint64_t peer_after_suppress = peer10->flow_request_expire_ms;
    potr_internal_flow_control_on_packet(&ctx, peer10, &resume); // [手順] - peer を指定して解除を渡す。
    uint64_t peer_after_resume = peer10->flow_request_expire_ms;
    uint64_t ctx_expire = cplat_atomic_load_u64(&ctx.flow_request_expire_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
    uint64_t ctx_limit = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    // Assert
    EXPECT_EQ(6000U, peer_after_suppress); // [確認_正常系] - peer の期限が 5000 + 1000 であること。
    EXPECT_EQ(0U, peer_after_resume);      // [確認_正常系] - 解除で peer の期限が 0 になること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 300U, peer10->flow_send_limit); // [確認_正常系] - peer の送出上限が 300 であること。
    EXPECT_EQ(5000U, peer10->flow_limit_received_ms); // [確認_正常系] - peer に受けた時刻を記録すること。
    EXPECT_EQ(777U, ctx_expire);           // [確認_正常系] - コンテキスト側の期限が変わらないこと。
    EXPECT_EQ(0U, ctx_limit);              // [確認_正常系] - コンテキスト側の送出上限が変わらないこと。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 不正なパケットで状態が変わらず、送信スレッドも起こさないことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, on_packet_ignores_malformed_packets)
{
    // Arrange
    const uint8_t undefined_zero[POTR_FLOW_CONTROL_PAYLOAD_SIZE] = {0U, 0U, 0U, 0U, 1U};
    const uint8_t undefined_three[POTR_FLOW_CONTROL_PAYLOAD_SIZE] = {3U, 0U, 0U, 0U, 1U};
    potr_packet cases[7];
    cases[0] = make_packet(kSuppressPayload, 1U);  // ペイロード長が 1 (送出上限なし)
    cases[1] = make_packet(kSuppressPayload, 6U);  // ペイロード長が 6
    cases[2] = make_packet(undefined_zero);        // 状態が 0
    cases[3] = make_packet(undefined_three);       // 状態が 3 (監視対象の一覧。multicast と broadcast の受信者以外は破棄する)
    cases[4] = make_packet(kSuppressPayload);
    cases[4].seq_num = 1U;                         // seq_num が 0 でない
    cases[5] = make_packet(kSuppressPayload);
    cases[5].ack_num = 1U;                         // ack_num が 0 でない
    cases[6] = make_packet(nullptr);               // ペイロードが NULL
    uint64_t ctx_expires[7] = {};
    uint64_t peer_expires[7] = {};
    cplat_atomic_store_u64(&ctx.flow_request_expire_ms, 777U, CPLAT_MEMORY_ORDER_RELEASE);
    peer10->flow_request_expire_ms = 888U;
    // [状態] - コンテキスト側の期限を 777、peer 側を 888 にし、7 種類の不正なパケットを用意する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_send_queue_wake_flow(_))
        .Times(0); // [Pre-Assert確認_異常系] - potr_internal_send_queue_wake_flow() が呼び出されないこと。
    // Act
    for (size_t i = 0; i < 7; i++)
    {
        potr_internal_flow_control_on_packet(&ctx, nullptr, &cases[i]);
        potr_internal_flow_control_on_packet(&ctx, peer10, &cases[i]);
        ctx_expires[i] = cplat_atomic_load_u64(&ctx.flow_request_expire_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
        peer_expires[i] = peer10->flow_request_expire_ms;
    }
    // [手順] - 7 種類のパケットを peer なしと peer ありの両方で渡し、期限を記録する (ループ 7 回)。
    // Assert
    for (size_t i = 0; i < 7; i++)
    {
        EXPECT_EQ(777U, ctx_expires[i]); // [確認_異常系] - コンテキスト側の期限が変わらないこと。(確認 7 回)
        EXPECT_EQ(888U, peer_expires[i]); // [確認_異常系] - peer 側の期限が変わらないこと。(確認 7 回)
    }
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 双方向で、期限が now より後で相手が生きている場合に、抑制して wait_ms が 1 以上 50 以下になることの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, suppressed_bidir_active)
{
    // Arrange
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    int wait_far = 0;
    int wait_near = 0;
    // [状態] - 双方向の送信者で、相手が生きている状態にする。
    // Pre-Assert
    // Act
    cplat_atomic_store_u64(&ctx.flow_request_expire_ms, 2000U, CPLAT_MEMORY_ORDER_RELEASE);
    int actual_far = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_far, &allowance); // [手順] - 期限が 1000 [ms] 先の状態で suppressed を呼び出す。
    cplat_atomic_store_u64(&ctx.flow_request_expire_ms, 1010U, CPLAT_MEMORY_ORDER_RELEASE);
    int actual_near = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_near, &allowance); // [手順] - 期限が 10 [ms] 先の状態で suppressed を呼び出す。
    // Assert
    EXPECT_EQ(1, actual_far);   // [確認_正常系] - 遠い期限で 1 であること。
    EXPECT_EQ(50, wait_far);    // [確認_正常系] - wait_ms が上限の 50 であること。
    EXPECT_EQ(1, actual_near);  // [確認_正常系] - 近い期限で 1 であること。
    EXPECT_EQ(10, wait_near);   // [確認_正常系] - wait_ms が残り時間の 10 であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 双方向で、期限が now 以下の場合に抑制しないことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, suppressed_bidir_expired)
{
    // Arrange
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    int wait_ms = -1;
    // [状態] - 双方向の送信者で、相手が生きている状態にする。
    // Pre-Assert
    // Act
    cplat_atomic_store_u64(&ctx.flow_request_expire_ms, 1000U, CPLAT_MEMORY_ORDER_RELEASE);
    int actual_equal = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance); // [手順] - 期限が now と同じ状態で suppressed を呼び出す。
    cplat_atomic_store_u64(&ctx.flow_request_expire_ms, 999U, CPLAT_MEMORY_ORDER_RELEASE);
    int actual_past = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance); // [手順] - 期限が now より前の状態で suppressed を呼び出す。
    // Assert
    EXPECT_EQ(0, actual_equal); // [確認_正常系] - 期限が now と同じで 0 であること。
    EXPECT_EQ(0, actual_past);  // [確認_正常系] - 期限が now より前で 0 であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 双方向で、相手が切断と判定されている場合に抑制せず、期限を 0 へ戻すことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, suppressed_bidir_dead_peer_clears_request)
{
    // Arrange
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    cplat_atomic_store_i32(&ctx.health_alive, 0, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_u64(&ctx.flow_request_expire_ms, 2000U, CPLAT_MEMORY_ORDER_RELEASE);
    int wait_ms = -1;
    // [状態] - 双方向の送信者で、相手が切断と判定され、期限が先の状態にする。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance); // [手順] - suppressed を呼び出す。
    uint64_t expire_after = cplat_atomic_load_u64(&ctx.flow_request_expire_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
    // Assert
    EXPECT_EQ(0, actual_ret);   // [確認_正常系] - 0 であること。
    EXPECT_EQ(0U, expire_after); // [確認_正常系] - 期限が 0 へ戻ること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 双方向で、close の開始後は抑制しないことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, suppressed_bidir_close_requested)
{
    // Arrange
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_u64(&ctx.flow_request_expire_ms, 2000U, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_i32(&ctx.close_requested, 1, CPLAT_MEMORY_ORDER_RELEASE);
    int wait_ms = -1;
    // [状態] - 期限が先で相手が生きていても、close が開始された状態にする。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance); // [手順] - suppressed を呼び出す。
    // Assert
    EXPECT_EQ(0, actual_ret); // [確認_正常系] - 0 であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// multicast で、要求の可能性がない (flow_request_pending が 0) 場合に、管理表を走査せず 0 を返すことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, suppressed_oneway_without_pending_skips_scan)
{
    // Arrange
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.role = POTR_ROLE_SENDER;
    cplat_atomic_store_i32(&ctx.flow_request_pending, 0, CPLAT_MEMORY_ORDER_RELEASE);
    int wait_ms = -1;
    // [状態] - multicast の送信者で、flow_request_pending を 0 にする。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_receiver_flow_suppressed(_, _, _, _, _))
        .Times(0); // [Pre-Assert確認_異常系] - potr_internal_receiver_flow_suppressed() が呼び出されないこと。
    // Act
    int actual_ret = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance); // [手順] - suppressed を呼び出す。
    // Assert
    EXPECT_EQ(0, actual_ret); // [確認_正常系] - 0 であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 片方向 UDP で、受信者の管理表が抑制を返し、要求が残っている場合に 1 を返し、flow_request_pending が 1 のままであることの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, suppressed_oneway_requesting_keeps_pending)
{
    // Arrange
    ctx.role = POTR_ROLE_SENDER;
    cplat_atomic_store_i32(&ctx.flow_request_pending, 1, CPLAT_MEMORY_ORDER_RELEASE);
    int wait_ms = -1;
    // [状態] - 片方向 UDP の送信者で、flow_request_pending を 1 にする。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_receiver_flow_suppressed(_, 1000U, _, _, _))
        .WillOnce([](potr_context *, uint64_t, uint32_t, size_t *requesting, uint64_t *until_ms) {
            *requesting = 1U;
            *until_ms = 1030U;
            return 1;
        }); // [Pre-Assert確認_正常系] - potr_internal_receiver_flow_suppressed() が now_ms = 1000 で 1 回呼び出されること。
            // [Pre-Assert手順] - 要求中の受信者が 1 台あり、抑制 (期限 1030) を返す。
    // Act
    int actual_ret = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance); // [手順] - suppressed を呼び出す。
    int pending = cplat_atomic_load_i32(&ctx.flow_request_pending, CPLAT_MEMORY_ORDER_ACQUIRE);
    // Assert
    EXPECT_EQ(1, actual_ret); // [確認_正常系] - 1 であること。
    EXPECT_EQ(30, wait_ms);   // [確認_正常系] - wait_ms が残り時間の 30 であること。
    EXPECT_EQ(1, pending);    // [確認_正常系] - flow_request_pending が 1 のままであること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 片方向 UDP で、受信者の管理表が抑制せず requesting が 0 の場合に 0 を返し、flow_request_pending が 0 になることの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, suppressed_oneway_no_requesting_clears_pending)
{
    // Arrange
    ctx.role = POTR_ROLE_SENDER;
    cplat_atomic_store_i32(&ctx.flow_request_pending, 1, CPLAT_MEMORY_ORDER_RELEASE);
    int wait_ms = -1;
    // [状態] - 片方向 UDP の送信者で、flow_request_pending を 1 にする。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_receiver_flow_suppressed(_, _, _, _, _))
        .WillOnce([](potr_context *, uint64_t, uint32_t, size_t *requesting, uint64_t *until_ms) {
            *requesting = 0U;
            *until_ms = 0U;
            return 0;
        }); // [Pre-Assert確認_正常系] - potr_internal_receiver_flow_suppressed() が 1 回呼び出されること。
            // [Pre-Assert手順] - 要求中の受信者がなく、抑制しない (0) を返す。
    // Act
    int actual_ret = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance); // [手順] - suppressed を呼び出す。
    int pending = cplat_atomic_load_i32(&ctx.flow_request_pending, CPLAT_MEMORY_ORDER_ACQUIRE);
    // Assert
    EXPECT_EQ(0, actual_ret); // [確認_正常系] - 0 であること。
    EXPECT_EQ(0, pending);    // [確認_正常系] - flow_request_pending が 0 になること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 片方向 UDP で、抑制しないが要求中の受信者が残っている場合に 0 を返し、flow_request_pending が 1 のままであることの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, suppressed_oneway_requesting_below_condition_keeps_pending)
{
    // Arrange
    ctx.role = POTR_ROLE_SENDER;
    cplat_atomic_store_i32(&ctx.flow_request_pending, 1, CPLAT_MEMORY_ORDER_RELEASE);
    int wait_ms = -1;
    // [状態] - 片方向 UDP の送信者で、flow_request_pending を 1 にする。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_receiver_flow_suppressed(_, _, _, _, _))
        .WillOnce([](potr_context *, uint64_t, uint32_t, size_t *requesting, uint64_t *until_ms) {
            *requesting = 2U;
            *until_ms = 0U;
            return 0;
        }); // [Pre-Assert確認_正常系] - potr_internal_receiver_flow_suppressed() が 1 回呼び出されること。
            // [Pre-Assert手順] - 要求中の受信者が 2 台あり、抑制の条件には達しない (0) を返す。
    // Act
    int actual_ret = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance); // [手順] - suppressed を呼び出す。
    int pending = cplat_atomic_load_i32(&ctx.flow_request_pending, CPLAT_MEMORY_ORDER_ACQUIRE);
    // Assert
    EXPECT_EQ(0, actual_ret); // [確認_正常系] - 0 であること。
    EXPECT_EQ(1, pending);    // [確認_正常系] - flow_request_pending が 1 のままであること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// N:1 モードでは suppressed が常に 0 を返すことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, suppressed_multi_peer_is_always_zero)
{
    // Arrange
    enable_multi_peer();
    ctx.role = POTR_ROLE_SENDER;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_u64(&ctx.flow_request_expire_ms, 2000U, CPLAT_MEMORY_ORDER_RELEASE);
    int wait_ms = -1;
    // [状態] - N:1 モードで、期限が先で相手が生きている状態にする。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance); // [手順] - suppressed を呼び出す。
    // Assert
    EXPECT_EQ(0, actual_ret); // [確認_正常系] - 0 であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// RAW 系の通信種別では suppressed が常に 0 を返すことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, suppressed_raw_types_are_always_zero)
{
    // Arrange
    potr_type types[] = {POTR_TYPE_UNICAST_RAW, POTR_TYPE_MULTICAST_RAW, POTR_TYPE_BROADCAST_RAW};
    int actual_rets[3] = {};
    int wait_ms = -1;
    ctx.role = POTR_ROLE_SENDER;
    cplat_atomic_store_i32(&ctx.flow_request_pending, 1, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_u64(&ctx.flow_request_expire_ms, 2000U, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - 要求が残り、期限が先で相手が生きている状態にする (ループ 3 回)。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_receiver_flow_suppressed(_, _, _, _, _))
        .Times(0); // [Pre-Assert確認_異常系] - potr_internal_receiver_flow_suppressed() が呼び出されないこと。
    // Act
    for (size_t i = 0; i < 3; i++)
    {
        ctx.service.type = types[i];
        actual_rets[i] = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance);
    }
    // [手順] - RAW 系の 3 種別で suppressed を呼び出し、戻り値を記録する。
    // Assert
    for (size_t i = 0; i < 3; i++)
    {
        EXPECT_EQ(0, actual_rets[i]); // [確認_正常系] - 戻り値が 0 であること。(確認 3 回)
    }
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// N:1 モードで、期限が先で相手が生きており close 前の場合に peer_state が 1 を返し、deferred を返すことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, peer_state_active)
{
    // Arrange
    enable_multi_peer();
    peer10->flow_request_expire_ms = 1500U;
    peer10->flow_deferred_entries = 3;
    cplat_atomic_store_i32(&peer10->health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    int deferred = -1;
    // [状態] - N:1 モードで、ピア 10 の期限が先、相手が生きている、保留が 3 件の状態にする。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_flow_control_peer_state(&ctx, 10U, 1000U, &deferred); // [手順] - ピア 10 で peer_state を呼び出す。
    // Assert
    EXPECT_EQ(1, actual_ret);  // [確認_正常系] - 1 であること。
    EXPECT_EQ(3, deferred);    // [確認_正常系] - deferred がピアの保留件数 3 であること。
    EXPECT_EQ((std::vector<potr_peer_id>{10U}), find_ids); // [確認_正常系] - ピア 10 を検索したこと。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// N:1 モードで、期限切れ、切断、close 開始、ピアなしのいずれでも peer_state が 0 を返すことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, peer_state_inactive_cases)
{
    // Arrange
    enable_multi_peer();
    int deferred_expired = -1;
    int deferred_dead = -1;
    int deferred_closing = -1;
    int deferred_missing = -1;
    peer10->flow_deferred_entries = 2;
    // [状態] - N:1 モードにし、ピア 10 の保留を 2 件にする。
    // Pre-Assert
    // Act
    peer10->flow_request_expire_ms = 1000U;
    cplat_atomic_store_i32(&peer10->health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    int actual_expired = potr_internal_flow_control_peer_state(&ctx, 10U, 1000U, &deferred_expired); // [手順] - 期限が now と同じ状態で呼び出す。
    peer10->flow_request_expire_ms = 1500U;
    cplat_atomic_store_i32(&peer10->health_alive, 0, CPLAT_MEMORY_ORDER_RELEASE);
    int actual_dead = potr_internal_flow_control_peer_state(&ctx, 10U, 1000U, &deferred_dead); // [手順] - health_alive が 0 の状態で呼び出す。
    cplat_atomic_store_i32(&peer10->health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_i32(&ctx.close_requested, 1, CPLAT_MEMORY_ORDER_RELEASE);
    int actual_closing = potr_internal_flow_control_peer_state(&ctx, 10U, 1000U, &deferred_closing); // [手順] - close_requested が 1 の状態で呼び出す。
    cplat_atomic_store_i32(&ctx.close_requested, 0, CPLAT_MEMORY_ORDER_RELEASE);
    int actual_missing = potr_internal_flow_control_peer_state(&ctx, 99U, 1000U, &deferred_missing); // [手順] - 存在しないピア 99 で呼び出す。
    // Assert
    EXPECT_EQ(0, actual_expired);   // [確認_正常系] - 期限切れで 0 であること。
    EXPECT_EQ(2, deferred_expired); // [確認_正常系] - 期限切れでも deferred がピアの保留件数であること。
    EXPECT_EQ(0, actual_dead);      // [確認_正常系] - health_alive が 0 で 0 であること。
    EXPECT_EQ(2, deferred_dead);    // [確認_正常系] - health_alive が 0 でも deferred がピアの保留件数であること。
    EXPECT_EQ(0, actual_closing);   // [確認_正常系] - close_requested が 1 で 0 であること。
    EXPECT_EQ(2, deferred_closing); // [確認_正常系] - close_requested が 1 でも deferred がピアの保留件数であること。
    EXPECT_EQ(0, actual_missing);   // [確認_正常系] - ピアが見つからない場合に 0 であること。
    EXPECT_EQ(0, deferred_missing); // [確認_正常系] - ピアが見つからない場合に deferred が 0 であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// N:1 モードでない場合に peer_state が 0 を返し、deferred が 0 になることの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, peer_state_without_multi_peer)
{
    // Arrange
    int deferred = -1;
    // [状態] - N:1 モードでない状態にする (deferred は -1 で初期化する)。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_flow_control_peer_state(&ctx, 10U, 1000U, &deferred); // [手順] - peer_state を呼び出す。
    // Assert
    EXPECT_EQ(0, actual_ret); // [確認_正常系] - 0 であること。
    EXPECT_EQ(0, deferred);   // [確認_正常系] - deferred が 0 であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 採用後の最初の呼び出しで、解除の状態と送出上限 (次に受信する通番 + 許可数) を 1 回だけ送出することの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, on_received_first_notification_carries_send_limit)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 容量 1000、要求 80%、解除 60%、max_payload 100 で start する (要求のしきい値 800、許可数 9)。
    // [状態確認] - start の戻り値が POTR_OK であること。
    // Pre-Assert
    // Act
    received(POTR_PEER_NA, 10U, 0); // [手順] - 次に受信する通番 10 で、PING の受理として on_received を呼び出す。
    received(POTR_PEER_NA, 11U, 0); // [手順] - 次に受信する通番 11 で、もう一度 PING の受理として呼び出す。
    // Assert
    EXPECT_EQ(std::vector<uint8_t>{kResume}, states); // [確認_正常系] - 状態が解除のパケットを 1 回だけ組み立てること。
    EXPECT_EQ(std::vector<uint32_t>{19U}, limits);    // [確認_正常系] - 送出上限が 10 + 9 であること。
    EXPECT_EQ(1, sendto_count);                       // [確認_正常系] - 1 回送出すること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 許可数が、占有量が 0、しきい値の前、しきい値ちょうど、しきい値の超過で正しく計算されることの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, send_allowance_by_occupancy)
{
    // Arrange
    const size_t occupancies[4] = {0U, 750U, 800U, 801U};
    const uint32_t expected_limits[4] = {19U, 11U, 11U, 10U};
    const uint8_t expected_states[4] = {kResume, kResume, kResume, kSuppress};
    uint32_t actual_limits[4] = {};
    uint8_t actual_states[4] = {};
    size_t actual_counts[4] = {};
    // [状態] - 占有量 0、750、800、801 の 4 通りを用意する (ループ 4 回)。
    // Pre-Assert
    // Act
    for (size_t i = 0; i < 4; i++)
    {
        ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
        if (occupancies[i] != 0U)
        {
            admit(POTR_PEER_NA, occupancies[i]);
        }
        clear_records();
        received(POTR_PEER_NA, 10U);
        actual_counts[i] = limits.size();
        actual_limits[i] = limits.empty() ? 0U : limits.back();
        actual_states[i] = states.empty() ? 0U : states.back();
        potr_internal_flow_control_stop(&ctx);
    }
    // [手順] - start、占有量の admit、次に受信する通番 10 での on_received、stop を順に行い、送出の記録を取る。
    // Assert
    for (size_t i = 0; i < 4; i++)
    {
        EXPECT_EQ(1U, actual_counts[i]); // [確認_正常系] - 送出上限を 1 回送出すること。(確認 4 回)
        EXPECT_EQ(expected_limits[i], actual_limits[i]);
        // [確認_正常系] - 送出上限が 10 + 許可数 (9、1、1、0) であること。(確認 4 回)
        EXPECT_EQ(expected_states[i], actual_states[i]);
        // [確認_正常系] - しきい値以下は解除、しきい値の超過 (要求中) は抑制の要求であること。(確認 4 回)
    }
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 前回知らせた範囲の半分以上を受信した DATA パケットで、送出上限の更新を送出することの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, update_after_half_of_notified_range)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 既定の構成で start する (許可数 9)。
    // [状態確認] - start の戻り値が POTR_OK であること。
    received(POTR_PEER_NA, 0U); // [状態] - 次に受信する通番 0 で、送出上限 9 を知らせる。
    // Pre-Assert
    // Act
    for (uint32_t next_seq = 1U; next_seq <= 4U; next_seq++)
    {
        received(POTR_PEER_NA, next_seq);
    }
    // [手順] - 次に受信する通番 1 から 4 で、DATA パケットの処理として on_received を呼び出す (ループ 4 回)。
    size_t count_before_half = limits.size();
    received(POTR_PEER_NA, 5U); // [手順] - 次に受信する通番 5 (範囲 9 の半分以上) で呼び出す。
    for (uint32_t next_seq = 6U; next_seq <= 9U; next_seq++)
    {
        received(POTR_PEER_NA, next_seq);
    }
    // [手順] - 次に受信する通番 6 から 9 で呼び出す (更新後の範囲の半分に満たない。ループ 4 回)。
    // Assert
    EXPECT_EQ(1U, count_before_half); // [確認_正常系] - 範囲の半分に満たない間は更新しないこと。
    EXPECT_EQ((std::vector<uint32_t>{9U, 14U}), limits); // [確認_正常系] - 通番 5 で送出上限 5 + 9 へ更新すること。
    EXPECT_EQ((std::vector<uint8_t>{kResume, kResume}), states); // [確認_正常系] - 更新の状態が解除であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 占有量が解除のしきい値以上の間は更新せず、送出上限まで送り終えた時点で抑制の要求へ遷移することの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, request_at_send_limit_when_occupancy_at_resume_threshold)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 既定の構成で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    admit(POTR_PEER_NA, 600U);  // [状態] - 占有量を解除のしきい値ちょうどの 600 にする (許可数 3)。
    received(POTR_PEER_NA, 0U); // [状態] - 次に受信する通番 0 で、送出上限 3 を知らせる。
    // Pre-Assert
    // Act
    received(POTR_PEER_NA, 1U); // [手順] - 次に受信する通番 1 で呼び出す。
    received(POTR_PEER_NA, 2U); // [手順] - 次に受信する通番 2 (範囲の半分以上) で呼び出す。
    size_t count_at_half = limits.size();
    received(POTR_PEER_NA, 3U); // [手順] - 次に受信する通番 3 (送出上限まで送り終えた) で呼び出す。
    std::vector<uint8_t> states_at_limit = states;
    potr_internal_flow_control_tick(&ctx, 1200U); // [手順] - 再送周期の経過後に tick を呼ぶ。
    // Assert
    EXPECT_EQ(1U, count_at_half); // [確認_正常系] - 占有量が解除のしきい値以上の間は、半分を受信しても更新しないこと。
    EXPECT_EQ((std::vector<uint8_t>{kResume, kSuppress}), states_at_limit); // [確認_正常系] - 送り終えた時点で抑制の要求を送出すること。
    EXPECT_EQ((std::vector<uint8_t>{kResume, kSuppress, kSuppress}), states);
    // [確認_正常系] - 要求中へ遷移したため、tick で抑制の要求を再送すること。
    EXPECT_EQ((std::vector<uint32_t>{3U, 6U, 6U}), limits); // [確認_正常系] - 抑制の要求と再送にも送出上限 3 + 3 を載せること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 送出上限まで送り終えた時点で、占有量が解除のしきい値を下回っていれば更新を送出することの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, update_at_send_limit_when_occupancy_below_resume_threshold)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 既定の構成で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    admit(POTR_PEER_NA, 500U);  // [状態] - 占有量を 500 にする (許可数 4)。
    received(POTR_PEER_NA, 0U); // [状態] - 次に受信する通番 0 で、送出上限 4 を知らせる。
    // Pre-Assert
    // Act
    received(POTR_PEER_NA, 4U); // [手順] - 次に受信する通番 4 (送出上限まで送り終えた) で呼び出す。
    // Assert
    EXPECT_EQ((std::vector<uint8_t>{kResume, kResume}), states); // [確認_正常系] - 更新 (状態は解除) を送出すること。
    EXPECT_EQ((std::vector<uint32_t>{4U, 8U}), limits);          // [確認_正常系] - 送出上限が 4 + 4 であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// DATA パケットの処理でない呼び出し (data_packet が 0) では、最初の送出だけを行い、更新しないことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, non_data_call_does_not_update)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 既定の構成で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    // Pre-Assert
    // Act
    received(POTR_PEER_NA, 0U, 0); // [手順] - 次に受信する通番 0 で、PING の受理として呼び出す。
    received(POTR_PEER_NA, 9U, 0); // [手順] - 次に受信する通番 9 (送出上限ちょうど) で、PING の受理として呼び出す。
    // Assert
    EXPECT_EQ(std::vector<uint32_t>{9U}, limits); // [確認_正常系] - 最初の送出上限 9 だけを送出すること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 抑制の要求と解除のパケットにも、その時点の送出上限を載せることの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, request_and_resume_carry_send_limit)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 既定の構成で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    received(POTR_PEER_NA, 20U, 0); // [状態] - 次に受信する通番 20 で、送出上限 29 を知らせる。
    // Pre-Assert
    // Act
    admit_repeat(POTR_PEER_NA, 100U, 9); // [手順] - 占有量 900 にして抑制の要求を発生させる。
    potr_internal_flow_control_release(&ctx, POTR_PEER_NA, 301U); // [手順] - 301 [byte] を release する (占有量 599)。
    // Assert
    EXPECT_EQ((std::vector<uint8_t>{kResume, kSuppress, kResume}), states); // [確認_正常系] - 最初、要求、解除の順に送出すること。
    EXPECT_EQ((std::vector<uint32_t>{29U, 20U, 23U}), limits);
    // [確認_正常系] - 要求は 20 + 0、解除は 20 + ((800 - 599) ÷ 100 + 1) の送出上限を載せること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 最初の送出に失敗した場合は、tick が成功するまで送出し直すことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, tick_retries_first_notification_until_sent)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 既定の構成で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    sendto_result = CPLAT_ERR_UNKNOWN; // [状態] - cplat_socket_sendto() を失敗させる。
    // Pre-Assert
    // Act
    received(POTR_PEER_NA, 0U, 0);                // [手順] - 次に受信する通番 0 で呼び出す (送出は失敗する)。
    potr_internal_flow_control_tick(&ctx, 5000U); // [手順] - tick を呼ぶ (送出は失敗する)。
    int count_after_failures = sendto_count;
    sendto_result = CPLAT_OK;
    potr_internal_flow_control_tick(&ctx, 5200U); // [手順] - 送出を成功させて tick を呼ぶ。
    potr_internal_flow_control_tick(&ctx, 5400U); // [手順] - もう一度 tick を呼ぶ。
    // Assert
    EXPECT_EQ(2, count_after_failures); // [確認_異常系] - 失敗の間は、呼び出しと tick のたびに送出を試みること。
    EXPECT_EQ(3, sendto_count);         // [確認_正常系] - 成功した後の tick では送出しないこと。
    EXPECT_EQ((std::vector<uint32_t>{9U, 9U, 9U}), limits); // [確認_正常系] - 同じ送出上限で送出し直すこと。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 相手のセッションの変更と、次に受信する通番の後退で、送出上限を知らせ直すことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, renotify_on_session_change_and_backward_sequence)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 既定の構成で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    received(POTR_PEER_NA, 0U, 0, 1U); // [状態] - セッション 1、次に受信する通番 0 で、送出上限 9 を知らせる。
    // Pre-Assert
    // Act
    received(POTR_PEER_NA, 100U, 1, 2U); // [手順] - セッション 2、次に受信する通番 100 で呼び出す。
    received(POTR_PEER_NA, 50U, 1, 2U);  // [手順] - 同じセッションで、次に受信する通番 50 (後退) で呼び出す。
    // Assert
    EXPECT_EQ((std::vector<uint32_t>{9U, 109U, 59U}), limits);
    // [確認_正常系] - セッションの変更と通番の後退のそれぞれで、新しい送出上限を知らせること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 容量超過で受信を再開しない相手には、送出上限を知らせないことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, refused_account_is_not_notified)
{
    // Arrange
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 既定の構成で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    ASSERT_EQ(POTR_ERR_FULL, admit(POTR_PEER_NA, 1001U));
    // [状態] - 容量を超える admit で、受信を再開しない状態にする。
    // [状態確認] - admit の戻り値が POTR_ERR_FULL であること。
    clear_records();
    // Pre-Assert
    // Act
    received(POTR_PEER_NA, 10U); // [手順] - on_received を呼び出す。
    // Assert
    EXPECT_TRUE(states.empty()); // [確認_正常系] - フロー制御パケットを組み立てないこと。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// N:1 モードで、PING だけを受けて解放されたピアの記録を、tick が解放することの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, multi_peer_tick_frees_account_of_released_peer)
{
    // Arrange
    enable_multi_peer();
    // [状態] - N:1 モードにし、peers_mutex を生成する。
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 既定の構成で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    received(12U, 0U, 0); // [状態] - 存在しないピア 12 の PING を受理する (送出先がなく送出しない)。
    // Pre-Assert
    // Act
    potr_internal_flow_control_tick(&ctx, 1000U); // [手順] - tick を呼ぶ。
    size_t find_after_first = find_ids.size();
    potr_internal_flow_control_tick(&ctx, 1200U); // [手順] - もう一度 tick を呼ぶ。
    // Assert
    EXPECT_EQ((std::vector<potr_peer_id>{12U, 12U}), find_ids); // [確認_正常系] - 最初の tick でピア 12 を検索し、2 回目は検索しないこと。
    EXPECT_EQ(2U, find_after_first); // [確認_正常系] - 最初の tick の時点で検索が 2 回であること。
    EXPECT_TRUE(states.empty());     // [確認_正常系] - フロー制御パケットを組み立てないこと。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 双方向で、送出上限の手前では抑制せずに送出できる数を返し、送出上限で抑制することの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, suppressed_bidir_send_limit)
{
    // Arrange
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_u64(&ctx.flow_limit_received_ms, 900U, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_HELD | 103U, CPLAT_MEMORY_ORDER_RELEASE);
    int wait_ms = -1;
    // [状態] - 双方向の送信者で、相手が生きており、要求なしで送出上限 103 (時刻 900 に受けた) を保持する。
    // Pre-Assert
    // Act
    ctx.send_window.next_seq = 100U;
    int actual_below = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance); // [手順] - 次の通番 100 で呼び出す。
    uint32_t allowance_below = allowance;
    ctx.send_window.next_seq = 103U;
    int actual_reached = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance); // [手順] - 次の通番 103 で呼び出す。
    // Assert
    EXPECT_EQ(0, actual_below);       // [確認_正常系] - 送出上限の手前では抑制しないこと。
    EXPECT_EQ(3U, allowance_below);   // [確認_正常系] - 送出できる数が 103 − 100 であること。
    EXPECT_EQ(1, actual_reached);     // [確認_正常系] - 送出上限に達したら抑制すること。
    EXPECT_EQ(10, wait_ms);           // [確認_正常系] - 最初の問い合わせまでの 10 [ms] を待機上限とすること。
    EXPECT_EQ(1000U, ctx.flow_limit_stalled_ms); // [確認_正常系] - 止まった時点 1000 を記録すること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 双方向で、送出上限で止まった時点から有効期限が経過すると相手なしへ遷移し、次のパケットを受けると未通知へ戻ることの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, suppressed_bidir_send_limit_expires_after_lifetime)
{
    // Arrange
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    ctx.send_window.next_seq = 103U;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_u64(&ctx.flow_limit_received_ms, 900U, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_HELD | 103U, CPLAT_MEMORY_ORDER_RELEASE);
    int wait_ms = -1;
    ASSERT_EQ(1, potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance));
    // [状態] - 次の通番が送出上限 103 に達した状態で、時刻 1000 に止まる (有効期限 1000 [ms])。
    // [状態確認] - suppressed の戻り値が 1 であること。
    // Pre-Assert
    // Act
    int actual_before = potr_internal_flow_control_suppressed(&ctx, 1999U, &wait_ms, &allowance); // [手順] - 時刻 1999 に呼び出す。
    int actual_expired = potr_internal_flow_control_suppressed(&ctx, 2000U, &wait_ms, &allowance); // [手順] - 時刻 2000 に呼び出す。
    uint32_t allowance_expired = allowance;
    uint64_t limit_after_expired = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    cplat_atomic_store_u64(&ctx.flow_limit_received_ms, 2100U, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_HELD | 103U, CPLAT_MEMORY_ORDER_RELEASE);
    int actual_again = potr_internal_flow_control_suppressed(&ctx, 2100U, &wait_ms, &allowance);
    // [手順] - 時刻 2100 に同じ送出上限のパケットを受けた状態で、時刻 2100 に呼び出す。
    // Assert
    EXPECT_EQ(1, actual_before);  // [確認_正常系] - 有効期限の経過前は抑制を続けること。
    EXPECT_EQ(0, actual_expired); // [確認_正常系] - 止まった時点から有効期限が経過したら再開すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_NO_PEER, limit_after_expired); // [確認_正常系] - 「相手なし」へ遷移すること。
    EXPECT_EQ(UINT32_MAX, allowance_expired); // [確認_正常系] - 破棄した送出上限は上限なしとして扱うこと。
    EXPECT_EQ(1, actual_again);   // [確認_正常系] - 次のフロー制御パケットを受けたら、再び送出上限に従うこと。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 双方向で、送出上限に達していても、相手を切断と判定した場合は相手なしへ戻し、close の開始後は無視することの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, suppressed_bidir_send_limit_dead_peer_and_close)
{
    // Arrange
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    ctx.send_window.next_seq = 103U;
    cplat_atomic_store_u64(&ctx.flow_limit_received_ms, 900U, CPLAT_MEMORY_ORDER_RELEASE);
    int wait_ms = -1;
    // [状態] - 双方向の送信者で、次の通番を 103 にする。
    // Pre-Assert
    // Act
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_HELD | 103U, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_i32(&ctx.close_requested, 1, CPLAT_MEMORY_ORDER_RELEASE);
    int actual_closing = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance);
    // [手順] - 送出上限に達し、close が開始された状態で呼び出す。
    cplat_atomic_store_i32(&ctx.close_requested, 0, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_i32(&ctx.health_alive, 0, CPLAT_MEMORY_ORDER_RELEASE);
    int actual_dead = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance);
    // [手順] - 送出上限に達し、相手を切断と判定した状態で呼び出す。
    uint64_t limit_after_dead = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    // Assert
    EXPECT_EQ(0, actual_closing);   // [確認_正常系] - close の開始後は送出上限を無視すること。
    EXPECT_EQ(0, actual_dead);      // [確認_正常系] - 相手を切断と判定した場合は抑制しないこと。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_NO_PEER, limit_after_dead); // [確認_正常系] - 「相手なし」へ戻すこと。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// N:1 モードで、ピアごとの送出上限で抑制し、有効期限の経過と切断で相手なしへ遷移することの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, peer_state_send_limit)
{
    // Arrange
    enable_multi_peer();
    int deferred = -1;
    peer10->send_window.next_seq = 5U;
    peer10->flow_limit_received_ms = 900U;
    cplat_atomic_store_i32(&peer10->health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - N:1 モードで、ピア 10 の次の通番を 5、相手が生きている状態にする。
    // Pre-Assert
    // Act
    peer10->flow_send_limit = POTR_FLOW_SEND_LIMIT_HELD | 6U;
    int actual_below = potr_internal_flow_control_peer_state(&ctx, 10U, 1000U, &deferred); // [手順] - 送出上限 6 で呼び出す。
    peer10->flow_send_limit = POTR_FLOW_SEND_LIMIT_HELD | 5U;
    int actual_reached = potr_internal_flow_control_peer_state(&ctx, 10U, 1000U, &deferred); // [手順] - 送出上限 5 で呼び出す。
    int actual_expired = potr_internal_flow_control_peer_state(&ctx, 10U, 2000U, &deferred);
    // [手順] - 止まった時点 1000 から有効期限 1000 [ms] が経過した時刻 2000 に呼び出す。
    uint64_t limit_after_expiry = peer10->flow_send_limit;
    peer10->flow_send_limit = POTR_FLOW_SEND_LIMIT_HELD | 5U;
    peer10->flow_limit_received_ms = 2500U;
    cplat_atomic_store_i32(&peer10->health_alive, 0, CPLAT_MEMORY_ORDER_RELEASE);
    int actual_dead = potr_internal_flow_control_peer_state(&ctx, 10U, 2500U, &deferred);
    // [手順] - 新しい送出上限 5 を受けた後、相手を切断と判定した状態で呼び出す。
    // Assert
    EXPECT_EQ(0, actual_below);   // [確認_正常系] - 送出上限の手前では抑制しないこと。
    EXPECT_EQ(1, actual_reached); // [確認_正常系] - 送出上限に達したら抑制すること。
    EXPECT_EQ(0, actual_expired); // [確認_正常系] - 有効期限が経過したら抑制しないこと。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_NO_PEER, limit_after_expiry); // [確認_正常系] - 有効期限の経過で「相手なし」へ遷移すること。
    EXPECT_EQ(0, actual_dead);    // [確認_正常系] - 相手を切断と判定した場合は抑制しないこと。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_NO_PEER, peer10->flow_send_limit); // [確認_正常系] - 切断の判定で「相手なし」へ遷移すること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 送出上限の判定で、上限なし・相手なし・未通知、循環をまたぐ比較、止まった時刻の記録が正しいことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, flow_send_limit_judge_cases)
{
    // Arrange
    uint64_t stalled_ms = 777U;
    uint64_t until_ms = 0U;
    uint32_t judged_allowance = 0U;
    // [状態] - 止まった時刻の格納先を 777 で初期化する。
    // Pre-Assert
    // Act
    potr_internal_send_limit_state actual_unlimited = flow_send_limit_judge(
        POTR_FLOW_SEND_LIMIT_UNLIMITED, 0U, &stalled_ms, 10U, 1000U, 1000U, &until_ms, &judged_allowance);
    // [手順] - 状態「上限なし」で判定する。
    uint64_t stalled_after_none = stalled_ms;
    uint32_t allowance_none = judged_allowance;
    potr_internal_send_limit_state actual_no_peer = flow_send_limit_judge(
        POTR_FLOW_SEND_LIMIT_NO_PEER, 0U, &stalled_ms, 10U, 1000U, 1000U, &until_ms, &judged_allowance);
    // [手順] - 状態「相手なし」で判定する。
    potr_internal_send_limit_state actual_unnotified_open = flow_send_limit_judge(
        POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 10U, 0U, &stalled_ms, 13U, 1000U, 1000U, &until_ms, &judged_allowance);
    uint32_t allowance_unnotified = judged_allowance;
    // [手順] - 状態「未通知」(基準 10、初期の幅 4 で上限 14)、次の通番 13 で判定する。
    potr_internal_send_limit_state actual_unnotified_reached = flow_send_limit_judge(
        POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 10U, 0U, &stalled_ms, 14U, 1000U, 1000U, &until_ms, &judged_allowance);
    // [手順] - 状態「未通知」(基準 10、上限 14)、次の通番 14 で判定する。
    stalled_ms = 0U;
    potr_internal_send_limit_state actual_wrap = flow_send_limit_judge(
        POTR_FLOW_SEND_LIMIT_HELD | 2U, 0U, &stalled_ms, 0xFFFFFFFFU, 1000U, 1000U, &until_ms, &judged_allowance);
    // [手順] - 送出上限 2、次の通番 0xFFFFFFFF (循環の直前) で判定する。
    uint32_t allowance_wrap = judged_allowance;
    potr_internal_send_limit_state actual_reached = flow_send_limit_judge(
        POTR_FLOW_SEND_LIMIT_HELD | 2U, 0U, &stalled_ms, 3U, 1000U, 1000U, &until_ms, &judged_allowance);
    // [手順] - 送出上限 2、次の通番 3 (送出上限を 1 超えた) で判定する。
    // Assert
    EXPECT_EQ(POTR_SEND_LIMIT_OPEN, actual_unlimited); // [確認_正常系] - 上限なしは POTR_SEND_LIMIT_OPEN であること。
    EXPECT_EQ(POTR_SEND_LIMIT_OPEN, actual_no_peer);   // [確認_正常系] - 相手なしは POTR_SEND_LIMIT_OPEN であること。
    EXPECT_EQ(0U, stalled_after_none);            // [確認_正常系] - 止まった時刻を 0 へ戻すこと。
    EXPECT_EQ(UINT32_MAX, allowance_none);        // [確認_正常系] - 送出できる数が UINT32_MAX であること。
    EXPECT_EQ(POTR_SEND_LIMIT_OPEN, actual_unnotified_open);       // [確認_正常系] - 初期の幅の手前では OPEN であること。
    EXPECT_EQ(1U, allowance_unnotified);                           // [確認_正常系] - 残り 1 パケットであること。
    EXPECT_EQ(POTR_SEND_LIMIT_REACHED, actual_unnotified_reached); // [確認_正常系] - 初期の幅に達したら REACHED であること。
    EXPECT_EQ(POTR_SEND_LIMIT_OPEN, actual_wrap); // [確認_正常系] - 循環をまたぐ手前の通番では上限に達していないこと。
    EXPECT_EQ(3U, allowance_wrap);                // [確認_正常系] - 送出できる数が 3 (0xFFFFFFFF、0、1) であること。
    EXPECT_EQ(POTR_SEND_LIMIT_REACHED, actual_reached); // [確認_正常系] - 送出上限を超えた通番で上限に達すること。
    EXPECT_EQ(1000U, stalled_ms);                 // [確認_正常系] - 止まった時刻 1000 を記録すること。
    EXPECT_EQ(2000U, until_ms);                   // [確認_正常系] - 破棄する時刻が 1000 + 1000 であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// N:1 モードで、フロー制御パケットを受けたピアを、要求または送出上限を保持しているピアの数へ 1 回だけ数えることの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, peer_tracking_counts_each_peer_once_on_packet)
{
    // Arrange
    enable_multi_peer();
    now_ms = 5000U;
    potr_packet suppress = make_packet(kSuppressPayload);
    potr_packet resume = make_packet(kResumePayload);
    int idle_before = potr_internal_flow_control_peers_idle(&ctx);
    // [状態] - N:1 モードで、どのピアからもフロー制御パケットを受けていない状態にする。
    // Pre-Assert
    // Act
    potr_internal_flow_control_on_packet(&ctx, peer10, &suppress); // [手順] - ピア 10 から抑制の要求を受ける。
    int32_t count_after_first = cplat_atomic_load_i32(&ctx.flow_tracked_peers, CPLAT_MEMORY_ORDER_ACQUIRE);
    potr_internal_flow_control_on_packet(&ctx, peer10, &resume); // [手順] - ピア 10 から解除 (送出上限つき) を受ける。
    int32_t count_after_resume = cplat_atomic_load_i32(&ctx.flow_tracked_peers, CPLAT_MEMORY_ORDER_ACQUIRE);
    potr_internal_flow_control_on_packet(&ctx, peer11, &suppress); // [手順] - ピア 11 から抑制の要求を受ける。
    int32_t count_after_second = cplat_atomic_load_i32(&ctx.flow_tracked_peers, CPLAT_MEMORY_ORDER_ACQUIRE);
    int idle_after = potr_internal_flow_control_peers_idle(&ctx);
    // Assert
    EXPECT_EQ(1, idle_before);        // [確認_正常系] - 受ける前は、ピアの判定を省ける状態であること。
    EXPECT_EQ(1, count_after_first);  // [確認_正常系] - ピア 10 を 1 として数えること。
    EXPECT_EQ(1, count_after_resume); // [確認_正常系] - 送出上限を保持するピア 10 を、二重に数えないこと。
    EXPECT_EQ(2, count_after_second); // [確認_正常系] - ピア 11 を加えて 2 として数えること。
    EXPECT_EQ(1, peer10->flow_tracked); // [確認_正常系] - ピア 10 を数えている印が付くこと。
    EXPECT_EQ(1, peer11->flow_tracked); // [確認_正常系] - ピア 11 を数えている印が付くこと。
    EXPECT_EQ(0, idle_after);         // [確認_正常系] - 受けた後は、ピアの判定を省けない状態であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// N:1 モードで、要求の期限の経過と送出上限の破棄の両方を経たピアを、数から外すことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, peer_tracking_returns_to_zero_after_request_expiry_and_limit_discard)
{
    // Arrange
    enable_multi_peer();
    now_ms = 5000U;
    potr_packet suppress = make_packet(kSuppressPayload);
    int deferred = -1;
    peer10->send_window.next_seq = 300U;
    cplat_atomic_store_i32(&peer10->health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    potr_internal_flow_control_on_packet(&ctx, peer10, &suppress);
    // [状態] - 時刻 5000 にピア 10 から抑制の要求 (期限 6000、送出上限 300) を受け、次の通番を 300 (送出上限に到達) にする。
    // Pre-Assert
    // Act
    int actual_requesting = potr_internal_flow_control_peer_state(&ctx, 10U, 5500U, &deferred);
    // [手順] - 要求の期限内の時刻 5500 に判定する (送出上限で止まった時点を 5500 と記録する)。
    int32_t count_requesting = cplat_atomic_load_i32(&ctx.flow_tracked_peers, CPLAT_MEMORY_ORDER_ACQUIRE);
    int actual_limited = potr_internal_flow_control_peer_state(&ctx, 10U, 6000U, &deferred);
    // [手順] - 要求の期限 6000 に判定する (送出上限は止まった時点 5500 から有効期限内)。
    uint64_t expire_after_expiry = peer10->flow_request_expire_ms;
    int32_t count_limited = cplat_atomic_load_i32(&ctx.flow_tracked_peers, CPLAT_MEMORY_ORDER_ACQUIRE);
    int actual_discarded = potr_internal_flow_control_peer_state(&ctx, 10U, 6500U, &deferred);
    // [手順] - 止まった時点 5500 から有効期限 1000 [ms] が経過した時刻 6500 に判定する。
    int32_t count_discarded = cplat_atomic_load_i32(&ctx.flow_tracked_peers, CPLAT_MEMORY_ORDER_ACQUIRE);
    // Assert
    EXPECT_EQ(1, actual_requesting); // [確認_正常系] - 要求の期限内は抑制すること。
    EXPECT_EQ(1, count_requesting);  // [確認_正常系] - 要求を保持する間は 1 として数えること。
    EXPECT_EQ(1, actual_limited);    // [確認_正常系] - 要求の期限の経過後も、送出上限に達している間は抑制すること。
    EXPECT_EQ(0U, expire_after_expiry); // [確認_正常系] - 期限が経過した要求を、要求なし (0) へ戻すこと。
    EXPECT_EQ(1, count_limited);     // [確認_正常系] - 送出上限を保持する間は 1 のままであること。
    EXPECT_EQ(0, actual_discarded);  // [確認_正常系] - 送出上限の破棄の後は抑制しないこと。
    EXPECT_EQ(0, count_discarded);   // [確認_正常系] - 要求と送出上限をどちらも持たないピアを数から外し、0 へ戻すこと。
    EXPECT_EQ(0, peer10->flow_tracked); // [確認_正常系] - ピア 10 を数えている印が外れること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// N:1 モードで、切断と判定したピアの要求と送出上限を取り消し、数から外すことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, peer_tracking_returns_to_zero_on_dead_peer)
{
    // Arrange
    enable_multi_peer();
    now_ms = 5000U;
    potr_packet suppress = make_packet(kSuppressPayload);
    int deferred = -1;
    potr_internal_flow_control_on_packet(&ctx, peer10, &suppress);
    cplat_atomic_store_i32(&peer10->health_alive, 0, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - 時刻 5000 にピア 10 から抑制の要求を受け、その後に相手を切断と判定した状態にする。
    // Pre-Assert
    // Act
    int32_t count_before = cplat_atomic_load_i32(&ctx.flow_tracked_peers, CPLAT_MEMORY_ORDER_ACQUIRE);
    int actual_ret = potr_internal_flow_control_peer_state(&ctx, 10U, 5500U, &deferred); // [手順] - 時刻 5500 に判定する。
    int32_t count_after = cplat_atomic_load_i32(&ctx.flow_tracked_peers, CPLAT_MEMORY_ORDER_ACQUIRE);
    // Assert
    EXPECT_EQ(1, count_before); // [確認_正常系] - 切断の判定の前は 1 として数えていること。
    EXPECT_EQ(0, actual_ret);   // [確認_正常系] - 切断と判定したピアは抑制しないこと。
    EXPECT_EQ(0, count_after);  // [確認_正常系] - 要求と送出上限を取り消したピアを数から外し、0 へ戻すこと。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 数の反映 (sync) と解放時の除外 (untrack) が、同じピアを二重に数えず、二重に外さないことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, peer_sync_and_untrack_keep_count_consistent)
{
    // Arrange
    peer10->flow_send_limit = POTR_FLOW_SEND_LIMIT_HELD | 5U;
    // [状態] - ピア 10 が送出上限を保持し、まだ数えていない状態にする。
    // Pre-Assert
    // Act
    potr_internal_flow_control_peer_sync(&ctx, peer10); // [手順] - 数へ反映する。
    potr_internal_flow_control_peer_sync(&ctx, peer10); // [手順] - 状態を変えずにもう一度反映する。
    int32_t count_synced = cplat_atomic_load_i32(&ctx.flow_tracked_peers, CPLAT_MEMORY_ORDER_ACQUIRE);
    peer10->flow_send_limit = POTR_FLOW_SEND_LIMIT_UNLIMITED;
    peer10->flow_request_expire_ms = 0U;
    potr_internal_flow_control_peer_sync(&ctx, peer10); // [手順] - 上限なし (通知済み) へ戻した後に反映する。
    int32_t count_cleared = cplat_atomic_load_i32(&ctx.flow_tracked_peers, CPLAT_MEMORY_ORDER_ACQUIRE);
    peer10->flow_request_expire_ms = 2000U;
    potr_internal_flow_control_peer_sync(&ctx, peer10); // [手順] - 抑制の要求を保持した後に反映する。
    potr_internal_flow_control_peer_untrack(&ctx, peer10); // [手順] - ピアの解放と同じく数から外す。
    potr_internal_flow_control_peer_untrack(&ctx, peer10); // [手順] - もう一度外す。
    int32_t count_untracked = cplat_atomic_load_i32(&ctx.flow_tracked_peers, CPLAT_MEMORY_ORDER_ACQUIRE);
    // Assert
    EXPECT_EQ(1, count_synced);    // [確認_正常系] - 2 回反映しても 1 であること。
    EXPECT_EQ(0, count_cleared);   // [確認_正常系] - 要求と送出上限をどちらも持たなくなったら 0 へ戻すこと。
    EXPECT_EQ(0, count_untracked); // [確認_正常系] - 解放で 0 へ戻し、2 回外しても負にならないこと。
    EXPECT_EQ(0, peer10->flow_tracked); // [確認_正常系] - ピア 10 を数えている印が外れていること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 初期の幅: 最初の通知まで 4 パケットで止まり、送出上限と上限なしのどちらでも解け、TCP では問い合わせを送出しないことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, initial_width_blocks_at_four_and_unblocks_on_limit_or_unlimited)
{
    // Arrange
    const uint8_t unlimited_payload[POTR_FLOW_CONTROL_PAYLOAD_SIZE] = {
        (uint8_t)POTR_FLOW_CONTROL_STATE_UNLIMITED, 0U, 0U, 0U, 0U};
    potr_packet resume = make_packet(kResumePayload);
    potr_packet unlimited = make_packet(unlimited_payload);
    int wait_ms = -1;
    ctx.service.type = POTR_TYPE_TCP;
    ctx.role = POTR_ROLE_SENDER;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - TCP 送信者で、未通知 (基準通番 0、初期の幅 4) の状態にする。
    // Pre-Assert
    // Act
    ctx.send_window.next_seq = 3U;
    int before_limit = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance);
    uint32_t allowance_before = allowance;
    ctx.send_window.next_seq = 4U;
    int at_limit_first = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance);
    int wait_tcp = wait_ms;
    int at_limit_after_10ms = potr_internal_flow_control_suppressed(&ctx, 1010U, &wait_ms, &allowance);
    int sent_on_tcp = sendto_count;
    // [手順] - 次の通番 3 と 4 で判定し、TCP では 10 [ms] 経過しても問い合わせを送出しないことを確認する。
    potr_internal_flow_control_on_packet(&ctx, nullptr, &resume);
    int after_resume = potr_internal_flow_control_suppressed(&ctx, 1020U, &wait_ms, &allowance);
    // [手順] - 送出上限 300 の解除パケットを受理して判定する。
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 4U, CPLAT_MEMORY_ORDER_RELEASE);
    ctx.send_window.next_seq = 8U;
    int re_stalled = potr_internal_flow_control_suppressed(&ctx, 1030U, &wait_ms, &allowance);
    potr_internal_flow_control_on_packet(&ctx, nullptr, &unlimited);
    int after_unlimited = potr_internal_flow_control_suppressed(&ctx, 1040U, &wait_ms, &allowance);
    uint32_t allowance_unlimited = allowance;
    // [手順] - 未通知 (基準 4、上限 8) で止まった後、上限なしパケットを受理して判定する。
    // Assert
    EXPECT_EQ(0, before_limit);        // [確認_正常系] - 3 パケット目までは止まらないこと。
    EXPECT_EQ(1U, allowance_before);   // [確認_正常系] - 残り 1 パケットであること。
    EXPECT_EQ(1, at_limit_first);      // [確認_正常系] - 4 パケットで止まること。
    EXPECT_EQ(50, wait_tcp);           // [確認_正常系] - TCP は問い合わせを行わず通常の待機上限であること。
    EXPECT_EQ(1, at_limit_after_10ms); // [確認_正常系] - 10 [ms] 後も止まり続けること。
    EXPECT_EQ(0, sent_on_tcp);         // [確認_正常系] - TCP は問い合わせを送出しないこと。
    EXPECT_EQ(0, after_resume);        // [確認_正常系] - 送出上限の通知で解けること。
    EXPECT_EQ(1, re_stalled);          // [確認_正常系] - 未通知へ戻すと再び 4 パケットで止まること。
    EXPECT_EQ(0, after_unlimited);     // [確認_正常系] - 上限なしの通知でも解けること。
    EXPECT_EQ(UINT32_MAX, allowance_unlimited); // [確認_正常系] - 上限なしの送出可能数が UINT32_MAX であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 双方向 UDP で、止まっている間に 10 ms 後から 2 倍ずつ上限まで問い合わせを送出し、解けると元に戻り、抑制の要求だけでは送出しないことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, query_backoff_reset_and_suppress_only_no_query)
{
    // Arrange
    int wait_ms = -1;
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    ctx.send_window.next_seq = 4U;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - 双方向 UDP 送信者で未通知 (上限 4) に達し、再送周期上限 200 [ms] の状態にする。
    // Pre-Assert
    // Act
    EXPECT_EQ(1, potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance));
    int count_at_start = sendto_count;
    EXPECT_EQ(1, potr_internal_flow_control_suppressed(&ctx, 1010U, &wait_ms, &allowance)); // +10 ms -> 1 回目
    int count_10 = sendto_count;
    EXPECT_EQ(1, potr_internal_flow_control_suppressed(&ctx, 1029U, &wait_ms, &allowance)); // +19 ms -> まだ
    int count_29 = sendto_count;
    EXPECT_EQ(1, potr_internal_flow_control_suppressed(&ctx, 1030U, &wait_ms, &allowance)); // +20 ms -> 2 回目
    int count_30 = sendto_count;
    EXPECT_EQ(1, potr_internal_flow_control_suppressed(&ctx, 1070U, &wait_ms, &allowance)); // +40 ms -> 3 回目
    EXPECT_EQ(1, potr_internal_flow_control_suppressed(&ctx, 1150U, &wait_ms, &allowance)); // +80 ms -> 4 回目
    EXPECT_EQ(1, potr_internal_flow_control_suppressed(&ctx, 1310U, &wait_ms, &allowance)); // +160 ms -> 5 回目
    EXPECT_EQ(1, potr_internal_flow_control_suppressed(&ctx, 1510U, &wait_ms, &allowance)); // +200 ms (上限) -> 6 回目
    int count_cap = sendto_count;
    // [手順] - 10, 20, 40, 80, 160, 200 [ms] の間隔で問い合わせを送出させる。
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_HELD | 10U, CPLAT_MEMORY_ORDER_RELEASE);
    EXPECT_EQ(0, potr_internal_flow_control_suppressed(&ctx, 1520U, &wait_ms, &allowance));
    // [手順] - 送出上限を進めて止まりを解き、間隔を元に戻させる。
    cplat_atomic_store_u64(&ctx.flow_request_expire_ms, 2000U, CPLAT_MEMORY_ORDER_RELEASE);
    EXPECT_EQ(1, potr_internal_flow_control_suppressed(&ctx, 1530U, &wait_ms, &allowance));
    EXPECT_EQ(1, potr_internal_flow_control_suppressed(&ctx, 1550U, &wait_ms, &allowance));
    int count_suppress_only = sendto_count;
    // [手順] - 抑制の要求だけで止まっている間に 20 [ms] 進める。
    cplat_atomic_store_u64(&ctx.flow_request_expire_ms, 0U, CPLAT_MEMORY_ORDER_RELEASE);
    ctx.send_window.next_seq = 10U;
    EXPECT_EQ(1, potr_internal_flow_control_suppressed(&ctx, 1600U, &wait_ms, &allowance));
    int wait_after_reset = wait_ms;
    EXPECT_EQ(1, potr_internal_flow_control_suppressed(&ctx, 1610U, &wait_ms, &allowance));
    // [手順] - 再び送出上限に達した 10 [ms] 後に問い合わせが送出されることを確認する。
    // Assert
    EXPECT_EQ(0, count_at_start);        // [確認_正常系] - 止まり始めた直後は問い合わせを送出しないこと。
    EXPECT_EQ(1, count_10);              // [確認_正常系] - 10 [ms] 後に 1 回目の問い合わせを送出すること。
    EXPECT_EQ(1, count_29);              // [確認_正常系] - 20 [ms] 未満では 2 回目を送出しないこと。
    EXPECT_EQ(2, count_30);              // [確認_正常系] - さらに 20 [ms] 後に 2 回目を送出すること。
    EXPECT_EQ(6, count_cap);             // [確認_正常系] - 上限 200 [ms] で 6 回送出すること。
    EXPECT_EQ(6, count_suppress_only);   // [確認_正常系] - 抑制の要求だけでは問い合わせを送出しないこと。
    EXPECT_EQ(10, wait_after_reset);     // [確認_正常系] - 解けた後は最初の間隔 10 [ms] に戻ること。
    EXPECT_EQ(7, sendto_count);          // [確認_正常系] - 再停止の 10 [ms] 後に 7 回目を送出すること。
    EXPECT_EQ((uint8_t)POTR_FLOW_CONTROL_STATE_QUERY, states.back()); // [確認_正常系] - 送出状態が「問い合わせ」であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 双方向 UDP で、相手から何らかのパケットが届いている間は破棄せず、届かない場合だけ相手なしへ遷移し、再到着で未通知へ戻り問い合わせを送出することの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, udp_expiry_keeps_limit_with_packets_and_recovers_from_no_peer)
{
    // Arrange
    int wait_ms = -1;
    potr_packet ping = {};
    ping.flags = POTR_FLAG_PING;
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    ctx.send_window.next_seq = 10U;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_HELD | 10U, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_u64(&ctx.flow_limit_received_ms, 1000U, CPLAT_MEMORY_ORDER_RELEASE);
    ASSERT_EQ(1, potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance));
    // [状態] - 時刻 1000 に送出上限 10 で止める (有効期限 1000 [ms])。
    // Pre-Assert
    // Act
    now_ms = 1800U;
    potr_internal_flow_control_on_peer_packet(&ctx, nullptr, &ping); // [手順] - 時刻 1800 に相手から PING を受ける。
    int still_held = potr_internal_flow_control_suppressed(&ctx, 2500U, &wait_ms, &allowance);
    // [手順] - 元の期限 (2000) を過ぎた時刻 2500 に判定する (PING 到着 1800 から 700 [ms])。
    int expired = potr_internal_flow_control_suppressed(&ctx, 2800U, &wait_ms, &allowance);
    // [手順] - PING 到着 1800 から有効期限 1000 [ms] 経過した時刻 2800 に判定する。
    uint64_t state_no_peer = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    int sendto_before_recovery = sendto_count;
    ctx.send_window.next_seq = 30U;
    now_ms = 3000U;
    potr_internal_flow_control_on_peer_packet(&ctx, nullptr, &ping);
    // [手順] - 相手なし状態で次の通番 30 のときに相手から PING を受ける。
    uint64_t state_recovered = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    // Assert
    EXPECT_EQ(1, still_held); // [確認_正常系] - パケットが届いている間は破棄しないこと。
    EXPECT_EQ(0, expired);    // [確認_正常系] - 何も届かずに有効期限が経過したら破棄すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_NO_PEER, state_no_peer); // [確認_正常系] - 「相手なし」へ遷移すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 30U, state_recovered);
    // [確認_正常系] - パケット到着で「その時点の通番 30 を基準とする未通知」へ戻ること。
    EXPECT_EQ(sendto_before_recovery + 1, sendto_count); // [確認_正常系] - 未通知へ戻ると同時に問い合わせを 1 回送出すること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 片方向 unicast の送信側が、受信者未登録の間に初期の幅で止まり、問い合わせを送出し、有効期限後に相手なしへ遷移することの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, unicast_oneway_initial_width_query_and_no_peer)
{
    // Arrange
    int wait_ms = -1;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.role = POTR_ROLE_SENDER;
    ctx.send_window.next_seq = 4U;
    // [状態] - 片方向 unicast 送信者、受信者未登録 (flow_request_pending == 0)、次の通番 4 にする。
    // Pre-Assert
    // Act
    int stalled = potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance);
    int wait_initial = wait_ms;
    int queried = potr_internal_flow_control_suppressed(&ctx, 1010U, &wait_ms, &allowance);
    int expired = potr_internal_flow_control_suppressed(&ctx, 2000U, &wait_ms, &allowance);
    uint64_t state_after = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    // [手順] - 時刻 1000 で初期の幅に到達し、1010 で問い合わせ、2000 で有効期限を経過させる。
    // Assert
    EXPECT_EQ(1, stalled);      // [確認_正常系] - 初期の幅 4 で止まること。
    EXPECT_EQ(10, wait_initial); // [確認_正常系] - 最初の問い合わせまでの 10 [ms] を返すこと。
    EXPECT_EQ(1, queried);      // [確認_正常系] - 10 [ms] 後も止まり続けること。
    EXPECT_EQ(1, sendto_count); // [確認_正常系] - 問い合わせを 1 回送出すること。
    EXPECT_EQ(0, expired);      // [確認_正常系] - 有効期限の経過後は止まらないこと。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_NO_PEER, state_after); // [確認_正常系] - 「相手なし」へ遷移すること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// N:1 モードで、未通知の初期の幅、問い合わせ送出、相手パケットによる保持、相手なしからの復帰がピアごとに動くことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, multi_peer_initial_width_query_and_no_peer_recovery)
{
    // Arrange
    enable_multi_peer();
    int deferred = -1;
    potr_packet ping = {};
    ping.flags = POTR_FLAG_PING;
    peer10->peer_id = 10U;
    peer10->send_window.next_seq = 4U;
    cplat_atomic_store_i32(&peer10->health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - N:1 ピア 10 が未通知 (基準 0、初期の幅 4)、次の通番 4、生存中の状態にする。
    // Pre-Assert
    // Act
    int stalled = potr_internal_flow_control_peer_state(&ctx, 10U, 1000U, &deferred);
    int queried = potr_internal_flow_control_peer_state(&ctx, 10U, 1010U, &deferred);
    int sent_after_10 = sendto_count;
    now_ms = 1800U;
    cplat_local_lock_lock(ctx.peers_mutex);
    potr_internal_flow_control_on_peer_packet(&ctx, peer10, &ping);
    cplat_local_lock_unlock(ctx.peers_mutex);
    int kept = potr_internal_flow_control_peer_state(&ctx, 10U, 2500U, &deferred);
    int expired = potr_internal_flow_control_peer_state(&ctx, 10U, 2800U, &deferred);
    uint64_t state_no_peer = peer10->flow_send_limit;
    int sent_before_recovery = sendto_count;
    peer10->send_window.next_seq = 20U;
    now_ms = 3000U;
    cplat_local_lock_lock(ctx.peers_mutex);
    potr_internal_flow_control_on_peer_packet(&ctx, peer10, &ping);
    cplat_local_lock_unlock(ctx.peers_mutex);
    // [手順] - 未通知で停止、10 [ms] 後に問い合わせ、PING で保持、有効期限後に相手なし、PING 再到着で復帰を確認する。
    // Assert
    EXPECT_EQ(1, stalled);       // [確認_正常系] - 初期の幅 4 で止まること。
    EXPECT_EQ(1, queried);       // [確認_正常系] - 10 [ms] 後も止まり続けること。
    EXPECT_EQ(1, sent_after_10); // [確認_正常系] - 10 [ms] 後に問い合わせを 1 回送出すること。
    EXPECT_EQ(0, kept);          // [確認_正常系] - 未通知では PING が届いても停止開始からの期限で破棄すること。
    EXPECT_EQ(0, expired);       // [確認_正常系] - 何も届かずに有効期限が経過したら破棄すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_NO_PEER, state_no_peer); // [確認_正常系] - 「相手なし」へ遷移すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 20U, peer10->flow_send_limit);
    // [確認_正常系] - パケット再到着で次の通番 20 を基準とする未通知へ戻ること。
    EXPECT_EQ(sent_before_recovery + 1, sendto_count); // [確認_正常系] - 復帰時に問い合わせを送出すること。
    EXPECT_EQ(1, peer10->flow_tracked); // [確認_正常系] - 未通知へ戻ったピアが追跡対象に戻ること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 送信が途切れた後の許可の短縮で、上限ありの小さいほうを採り、上限あり以外の状態を変えないことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, flow_send_limit_clamp_initial_cases)
{
    // Arrange
    // Pre-Assert
    // Act
    uint64_t clamped = flow_send_limit_clamp_initial(POTR_FLOW_SEND_LIMIT_HELD | 100U, 10U);
    uint64_t kept_smaller = flow_send_limit_clamp_initial(POTR_FLOW_SEND_LIMIT_HELD | 12U, 10U);
    uint64_t wrapped = flow_send_limit_clamp_initial(POTR_FLOW_SEND_LIMIT_HELD | 50U, 0xFFFFFFFEU);
    uint64_t unnotified = flow_send_limit_clamp_initial(POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 0U, 10U);
    uint64_t unlimited = flow_send_limit_clamp_initial(POTR_FLOW_SEND_LIMIT_UNLIMITED, 10U);
    uint64_t no_peer = flow_send_limit_clamp_initial(POTR_FLOW_SEND_LIMIT_NO_PEER, 10U);
    // [手順] - 各状態と通番で flow_send_limit_clamp_initial() を呼び出す。
    // Assert
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 14U, clamped);      // [確認_正常系] - 10 + 4 と 100 の小さいほう 14 になること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 12U, kept_smaller); // [確認_正常系] - 12 < 14 のため 12 のままであること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 2U, wrapped);       // [確認_正常系] - 循環をまたぐ 0xFFFFFFFE + 4 = 2 になること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 0U, unnotified); // [確認_正常系] - 未通知は変えないこと。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNLIMITED, unlimited);        // [確認_正常系] - 上限なしは変えないこと。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_NO_PEER, no_peer);            // [確認_正常系] - 相手なしは変えないこと。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 送信が途切れた後の許可: 双方向 UDP と N:1 で有効期限ちょうどから短縮し、未満と TCP では短縮しないことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, idle_send_reduces_allowance_on_udp_and_not_tcp)
{
    // Arrange
    int wait_ms = -1;
    int deferred = -1;
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    ctx.send_window.next_seq = 10U;
    ctx.flow_last_data_send_ms = 1000U;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_HELD | 100U, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - 双方向 UDP 送信者で最終 DATA 送出 1000、有効期限 1000、次の通番 10、送出上限 100 にする。
    // Pre-Assert
    // Act
    EXPECT_EQ(0, potr_internal_flow_control_suppressed(&ctx, 1999U, &wait_ms, &allowance));
    uint64_t limit_before = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    EXPECT_EQ(0, potr_internal_flow_control_suppressed(&ctx, 2000U, &wait_ms, &allowance));
    uint64_t limit_at_boundary = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    uint32_t allowance_after = allowance;
    // [手順] - 有効期限の 1 [ms] 前 (1999) とちょうど (2000) で判定する。
    ctx.service.type = POTR_TYPE_TCP;
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_HELD | 100U, CPLAT_MEMORY_ORDER_RELEASE);
    EXPECT_EQ(0, potr_internal_flow_control_suppressed(&ctx, 5000U, &wait_ms, &allowance));
    uint64_t limit_tcp = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    // [手順] - TCP では有効期限経過後も短縮しないことを確認する。
    enable_multi_peer();
    peer10->peer_id = 10U;
    peer10->send_window.next_seq = 20U;
    peer10->flow_last_data_send_ms = 1000U;
    peer10->flow_send_limit = POTR_FLOW_SEND_LIMIT_HELD | 100U;
    cplat_atomic_store_i32(&peer10->health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    EXPECT_EQ(0, potr_internal_flow_control_peer_state(&ctx, 10U, 1999U, &deferred));
    uint64_t peer_before = peer10->flow_send_limit;
    EXPECT_EQ(0, potr_internal_flow_control_peer_state(&ctx, 10U, 2000U, &deferred));
    uint64_t peer_after = peer10->flow_send_limit;
    // [手順] - N:1 ピア 10 で有効期限の 1 [ms] 前とちょうどで判定する。
    // Assert
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 100U, limit_before);     // [確認_正常系] - 有効期限未満では短縮しないこと。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 14U, limit_at_boundary); // [確認_正常系] - 有効期限ちょうどで 10 + 4 へ短縮すること。
    EXPECT_EQ(4U, allowance_after);                                // [確認_正常系] - 許可数が初期の幅 4 になること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 100U, limit_tcp);        // [確認_正常系] - TCP では短縮しないこと。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 100U, peer_before);      // [確認_正常系] - N:1 で有効期限未満は短縮しないこと。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 24U, peer_after);        // [確認_正常系] - N:1 で有効期限ちょうどで 20 + 4 へ短縮すること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 逆方向の DATA への相乗り送出: 送出上限の取得、「前回知らせた範囲」の更新による更新パケット抑止、抑制/解除の継続、対象外構成の確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, piggyback_limit_updates_range_and_respects_mode)
{
    // Arrange
    uint32_t limit = 999U;
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.role = POTR_ROLE_RECEIVER;
    // [状態] - 双方向 UDP 受信側 (容量 1000、要求しきい値 800、解除しきい値 600、max_payload 100 -> 許可数 9)。
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx)); // [状態確認] - 資源の生成に成功すること。
    // Pre-Assert
    // Act
    int before_seq = potr_internal_flow_control_piggyback_limit(&ctx, POTR_PEER_NA, &limit);
    uint32_t limit_before_seq = limit;
    // [手順] - まだ通番を受け取っていない状態で相乗りを試みる。
    potr_internal_flow_control_on_received(&ctx, POTR_PEER_NA, 1U, &session_ts, 10U, 0, 0U);
    int sent_after_initial = sendto_count;
    // [手順] - 初回 PING で next_seq=10 を登録する (notified_base=10, notified_limit=19)。
    potr_internal_flow_control_on_received(&ctx, POTR_PEER_NA, 1U, &session_ts, 11U, 1, 0U);
    potr_internal_flow_control_on_received(&ctx, POTR_PEER_NA, 1U, &session_ts, 12U, 1, 0U);
    potr_internal_flow_control_on_received(&ctx, POTR_PEER_NA, 1U, &session_ts, 13U, 1, 0U);
    potr_internal_flow_control_on_received(&ctx, POTR_PEER_NA, 1U, &session_ts, 14U, 1, 0U);
    int sent_before_piggyback = sendto_count;
    // [手順] - 4 個受信 (14 - 10 = 4 < 9/2) で更新パケットはまだ出ない。
    int piggybacked = potr_internal_flow_control_piggyback_limit(&ctx, POTR_PEER_NA, &limit);
    uint32_t limit_piggybacked = limit;
    // [手順] - 逆方向 DATA に相乗りする (notified_base=14, notified_limit=23 に更新される)。
    potr_internal_flow_control_on_received(&ctx, POTR_PEER_NA, 1U, &session_ts, 15U, 1, 0U);
    int sent_after_fifth_data = sendto_count;
    // [手順] - 5 個目の DATA (next_seq=15) を処理しても、前回知らせた範囲が 14..23 に更新済みのため更新パケットが出ないこと。
    EXPECT_EQ(POTR_OK, potr_internal_flow_control_admit(&ctx, POTR_PEER_NA, 1U, &session_ts, 850U));
    int sent_after_suppress = sendto_count;
    potr_internal_flow_control_release(&ctx, POTR_PEER_NA, 300U);
    int sent_after_resume = sendto_count;
    // [手順] - 抑制の要求と解除は従来どおりフロー制御パケットで送出されることを確認する。
    // Assert
    EXPECT_EQ(0, before_seq);                    // [確認_正常系] - 通番未受領の間は相乗りしないこと。
    EXPECT_EQ(0U, limit_before_seq);             // [確認_正常系] - 相乗りしない場合は 0 を格納すること。
    EXPECT_EQ(1, sent_after_initial);            // [確認_正常系] - 初回通知はフロー制御パケット 1 回であること。
    EXPECT_EQ(sent_after_initial, sent_before_piggyback);
    // [確認_正常系] - 範囲の半分未満の受信では更新パケットを送出しないこと。
    EXPECT_EQ(1, piggybacked);                   // [確認_正常系] - 通番受領後は相乗りできること。
    EXPECT_EQ(23U, limit_piggybacked);           // [確認_正常系] - 14 + 9 = 23 が相乗りされること。
    EXPECT_EQ(sent_before_piggyback, sent_after_fifth_data);
    // [確認_正常系] - DATA 相乗りで前回知らせた範囲が更新され、5 個目の DATA で更新パケットを送出しないこと。
    EXPECT_EQ(sent_after_fifth_data + 1, sent_after_suppress);
    // [確認_正常系] - 抑制の要求はフロー制御パケットで送出されること。
    EXPECT_EQ(sent_after_suppress + 1, sent_after_resume);
    // [確認_正常系] - 解除はフロー制御パケットで送出されること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 対象外種別 (片方向・TCP) と上限なし構成では相乗りせず、fc->mutex も取得しないことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, piggyback_limit_skips_non_bidir_and_unlimited_without_lock)
{
    // Arrange
    uint32_t limit = 77U;
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.role = POTR_ROLE_RECEIVER;
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx)); // [状態確認] - unicast で資源を生成できること。
    potr_internal_flow_control_on_received(&ctx, POTR_PEER_NA, 1U, &session_ts, 10U, 0, 0U);
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(_)).Times(0);
    // [Pre-Assert確認_正常系] - 相乗り対象外の通信種別ではロックを取得しないこと。
    // Act
    int ret_unicast = potr_internal_flow_control_piggyback_limit(&ctx, POTR_PEER_NA, &limit);
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    int ret_tcp = potr_internal_flow_control_piggyback_limit(&ctx, POTR_PEER_NA, &limit);
    // [手順] - unicast と tcp_bidir で相乗りを呼び出す。
    // Assert
    EXPECT_EQ(0, ret_unicast); // [確認_正常系] - 片方向 unicast では相乗りしないこと。
    EXPECT_EQ(0, ret_tcp);     // [確認_正常系] - TCP では相乗りしないこと。
    EXPECT_EQ(0U, limit);      // [確認_正常系] - 対象外では 0 を格納すること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 逆方向の DATA への相乗り受理: 未通知からの初回通知、先へ進む値の更新、古い値・同値・フラグなし・上限なしの無視 (1:1 と N:1)
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, piggyback_receive_advances_only_and_ignores_stale_or_unflagged)
{
    // Arrange
    potr_packet pkt = {};
    int wake_count = 0;
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 0U, CPLAT_MEMORY_ORDER_RELEASE);
    ON_CALL(mock_porter, potr_internal_send_queue_wake_flow(&ctx.send_queue)).WillByDefault([&](potr_internal_send_queue *) {
        wake_count++;
    });
    // [状態] - 1:1 双方向 UDP で未通知状態にする。
    // Pre-Assert
    // Act
    pkt.flags = (uint16_t)(POTR_FLAG_DATA | POTR_FLAG_SEND_LIMIT);
    pkt.ack_num = 50U;
    now_ms = 2000U;
    potr_internal_flow_control_on_peer_packet(&ctx, nullptr, &pkt);
    uint64_t after_first = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    int wake_after_first = wake_count;
    // [手順] - 未通知状態で送出上限 50 の相乗り DATA を受理する。
    pkt.ack_num = 45U;
    now_ms = 2100U;
    potr_internal_flow_control_on_peer_packet(&ctx, nullptr, &pkt);
    uint64_t after_stale = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    pkt.ack_num = 50U;
    potr_internal_flow_control_on_peer_packet(&ctx, nullptr, &pkt);
    uint64_t after_equal = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    int wake_after_ignored = wake_count;
    // [手順] - 再送などの古い値 (45) と同値 (50) を受理する。
    pkt.flags = POTR_FLAG_DATA;
    pkt.ack_num = 999U;
    potr_internal_flow_control_on_peer_packet(&ctx, nullptr, &pkt);
    uint64_t after_unflagged = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    // [手順] - POTR_FLAG_SEND_LIMIT が立っていない DATA は ack_num を無視する。
    pkt.flags = (uint16_t)(POTR_FLAG_DATA | POTR_FLAG_SEND_LIMIT);
    pkt.ack_num = 60U;
    now_ms = 2200U;
    potr_internal_flow_control_on_peer_packet(&ctx, nullptr, &pkt);
    uint64_t after_advance = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    // [手順] - 先へ進む値 (60) を受理する。
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_HELD | 0xFFFFFFFEU, CPLAT_MEMORY_ORDER_RELEASE);
    pkt.ack_num = 5U;
    potr_internal_flow_control_on_peer_packet(&ctx, nullptr, &pkt);
    uint64_t after_wrap = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    // [手順] - 循環をまたいで先へ進む値 (0xFFFFFFFE -> 5) を受理する。
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNLIMITED, CPLAT_MEMORY_ORDER_RELEASE);
    pkt.ack_num = 100U;
    potr_internal_flow_control_on_peer_packet(&ctx, nullptr, &pkt);
    uint64_t after_unlimited = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    // [手順] - 上限なし状態では相乗り DATA で上書きしない。
    enable_multi_peer();
    peer10->peer_id = 10U;
    peer10->flow_send_limit = POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 0U;
    pkt.flags = (uint16_t)(POTR_FLAG_DATA | POTR_FLAG_SEND_LIMIT);
    pkt.ack_num = 70U;
    cplat_local_lock_lock(ctx.peers_mutex);
    potr_internal_flow_control_on_peer_packet(&ctx, peer10, &pkt);
    uint64_t peer_first = peer10->flow_send_limit;
    pkt.ack_num = 65U;
    potr_internal_flow_control_on_peer_packet(&ctx, peer10, &pkt);
    uint64_t peer_stale = peer10->flow_send_limit;
    pkt.ack_num = 80U;
    potr_internal_flow_control_on_peer_packet(&ctx, peer10, &pkt);
    uint64_t peer_advance = peer10->flow_send_limit;
    cplat_local_lock_unlock(ctx.peers_mutex);
    // [手順] - N:1 ピアでも初回受理、古い値の無視、先へ進む値の更新を確認する。
    // Assert
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 50U, after_first);     // [確認_正常系] - 未通知から初回通知として 50 を保持すること。
    EXPECT_EQ(1, wake_after_first);                              // [確認_正常系] - 初回通知で送信スレッドを起こすこと。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 50U, after_stale);     // [確認_正常系] - 古い値 45 を無視すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 50U, after_equal);     // [確認_正常系] - 同値 50 を無視すること。
    EXPECT_EQ(wake_after_first, wake_after_ignored);             // [確認_正常系] - 無視したパケットでは送信スレッドを起こさないこと。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 50U, after_unflagged); // [確認_正常系] - フラグなしの ack_num を無視すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 60U, after_advance);   // [確認_正常系] - 先へ進む 60 へ更新すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 5U, after_wrap);       // [確認_正常系] - 循環をまたぐ 5 へ更新すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNLIMITED, after_unlimited);  // [確認_正常系] - 上限なしを上書きしないこと。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 70U, peer_first);      // [確認_正常系] - N:1 で未通知から 70 を保持すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 70U, peer_stale);      // [確認_正常系] - N:1 で古い値 65 を無視すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 80U, peer_advance);    // [確認_正常系] - N:1 で先へ進む 80 へ更新すること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 圧縮した DATA の展開後の大きさの最大値が max_payload より大きい場合に許可数が小さくなり、max_payload 以下では変わらないことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, compressed_data_expanded_size_reduces_allowance_only_above_max_payload)
{
    // Arrange
    uint32_t report_below = 0U;
    uint32_t report_exact = 0U;
    uint32_t report_above = 0U;
    uint32_t report_after_smaller = 0U;
    uint32_t max_below = 0U;
    uint32_t max_exact = 0U;
    uint32_t max_above = 0U;
    uint32_t max_after_smaller = 0U;
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 容量 1000、要求 80% (800 [byte])、max_payload 100 [byte] で start する (既定の許可数 800 ÷ 100 + 1 = 9)。
    // [状態確認] - start の戻り値が POTR_OK であること。
    // Pre-Assert
    // Act
    received(POTR_PEER_NA, 10U, 1, 1U, 80U);
    int held_below = potr_internal_flow_control_report_limit(&ctx, &report_below, &max_below);
    // [手順] - max_payload (100) 未満の展開後サイズ 80 [byte] を受信する。
    received(POTR_PEER_NA, 11U, 1, 1U, 100U);
    int held_exact = potr_internal_flow_control_report_limit(&ctx, &report_exact, &max_exact);
    // [手順] - max_payload (100) ちょうどの展開後サイズ 100 [byte] を受信する。
    received(POTR_PEER_NA, 12U, 1, 1U, 400U);
    int held_above = potr_internal_flow_control_report_limit(&ctx, &report_above, &max_above);
    // [手順] - max_payload を超える展開後サイズ 400 [byte] を受信する (許可数 800 ÷ 400 + 1 = 3)。
    received(POTR_PEER_NA, 13U, 1, 1U, 50U);
    int held_after_smaller = potr_internal_flow_control_report_limit(&ctx, &report_after_smaller, &max_after_smaller);
    // [手順] - より小さい 50 [byte] を受信しても、同一セッション内の最大値 400 [byte] が維持されることを確認する。
    // Assert
    EXPECT_EQ(1, held_below);          // [確認_正常系] - 80 [byte] 受信後に送出上限が取得できること。
    EXPECT_EQ(19U, report_below);      // [確認_正常系] - max_payload 未満では 10 + 9 = 19 で変わらないこと。
    EXPECT_EQ(9U, max_below);          // [確認_正常系] - max_payload 未満では許可数の上限が 9 であること。
    EXPECT_EQ(1, held_exact);          // [確認_正常系] - 100 [byte] 受信後に送出上限が取得できること。
    EXPECT_EQ(20U, report_exact);      // [確認_正常系] - max_payload ちょうどでは 11 + 9 = 20 で変わらないこと。
    EXPECT_EQ(9U, max_exact);          // [確認_正常系] - max_payload ちょうどでは許可数の上限が 9 であること。
    EXPECT_EQ(1, held_above);          // [確認_正常系] - 400 [byte] 受信後に送出上限が取得できること。
    EXPECT_EQ(15U, report_above);      // [確認_正常系] - 400 [byte] により許可数が 3 へ減り、12 + 3 = 15 になること。
    EXPECT_EQ(3U, max_above);          // [確認_正常系] - 400 [byte] により許可数の上限が 3 へ減ること。
    EXPECT_EQ(1, held_after_smaller);  // [確認_正常系] - 50 [byte] 受信後も送出上限が取得できること。
    EXPECT_EQ(16U, report_after_smaller);
    // [確認_正常系] - 最大値 400 [byte] が維持され、13 + 3 = 16 になること。
    EXPECT_EQ(3U, max_after_smaller);  // [確認_正常系] - 最大値 400 [byte] が維持され、許可数の上限が 3 であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// セッションの変更 (session_id または session_ts の変更) で展開後の大きさの最大値が 0 に戻ることの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, compressed_data_max_size_resets_on_session_change)
{
    // Arrange
    cplat_timespec next_ts = session_ts;
    next_ts.tv_sec += 1;
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 容量 1000、要求 80% (800 [byte])、max_payload 100 [byte] で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    // Pre-Assert
    // Act
    received(POTR_PEER_NA, 10U, 1, 1U, 400U);
    // [手順] - セッション 1 で展開後サイズ 400 [byte] を受信する (初回通知: 10 + 3 = 13)。
    received(POTR_PEER_NA, 20U, 1, 2U, 0U);
    // [手順] - session_id を 2 へ変えて受信する (最大値が 0 に戻り、初回通知: 20 + 9 = 29)。
    received(POTR_PEER_NA, 21U, 1, 2U, 800U);
    // [手順] - セッション 2 で展開後サイズ 800 [byte] を受信する (最大値 800、許可数 2)。
    received(POTR_PEER_NA, 30U, 1, 2U, 0U, &next_ts);
    // [手順] - session_ts を変えて受信する (最大値が 0 に戻り、初回通知: 30 + 9 = 39)。
    // Assert
    EXPECT_EQ((std::vector<uint32_t>{13U, 29U, 39U}), limits);
    // [確認_正常系] - セッション 1 は許可数 3、session_id 変更後と session_ts 変更後は最大値が 0 に戻り許可数 9 になること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// N:1 モードで、展開後の大きさの最大値がピアごとに独立して保持・リセットされることの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, multi_peer_compressed_data_max_size_is_independent_per_peer)
{
    // Arrange
    enable_multi_peer();
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - N:1 モード、容量 1000、要求 80% (800 [byte])、max_payload 100 [byte] で start する。
    // [状態確認] - start の戻り値が POTR_OK であること。
    uint32_t piggy_p10_before = 0U;
    uint32_t piggy_p11_before = 0U;
    uint32_t piggy_p10_after_reset = 0U;
    uint32_t piggy_p11_after_reset = 0U;
    // Pre-Assert
    // Act
    received(10U, 10U, 1, 1U, 400U);
    // [手順] - ピア 10 (セッション 1) で展開後サイズ 400 [byte] を受信する (許可数 3、送出上限 13)。
    received(11U, 10U, 1, 1U, 50U);
    // [手順] - ピア 11 (セッション 1) で max_payload 以下の 50 [byte] を受信する (許可数 9、送出上限 19)。
    cplat_local_lock_lock(ctx.peers_mutex);
    int held_p10_before = potr_internal_flow_control_piggyback_limit(&ctx, 10U, &piggy_p10_before);
    int held_p11_before = potr_internal_flow_control_piggyback_limit(&ctx, 11U, &piggy_p11_before);
    cplat_local_lock_unlock(ctx.peers_mutex);
    // [手順] - 各ピアの相乗り送出上限を取得する。
    received(10U, 20U, 1, 2U, 0U);
    // [手順] - ピア 10 だけセッションを 2 へ変更する (ピア 10 の最大値だけ 0 に戻り許可数 9)。
    received(11U, 11U, 1, 1U, 800U);
    // [手順] - ピア 11 で展開後サイズ 800 [byte] を受信する (ピア 11 の許可数 2)。
    cplat_local_lock_lock(ctx.peers_mutex);
    int held_p10_after = potr_internal_flow_control_piggyback_limit(&ctx, 10U, &piggy_p10_after_reset);
    int held_p11_after = potr_internal_flow_control_piggyback_limit(&ctx, 11U, &piggy_p11_after_reset);
    cplat_local_lock_unlock(ctx.peers_mutex);
    // [手順] - セッション変更後の各ピアの相乗り送出上限を取得する。
    // Assert
    EXPECT_EQ(1, held_p10_before);           // [確認_正常系] - ピア 10 の送出上限が取得できること。
    EXPECT_EQ(13U, piggy_p10_before);        // [確認_正常系] - ピア 10 は 400 [byte] により 10 + 3 = 13 であること。
    EXPECT_EQ(1, held_p11_before);           // [確認_正常系] - ピア 11 の送出上限が取得できること。
    EXPECT_EQ(19U, piggy_p11_before);        // [確認_正常系] - ピア 11 はピア 10 の影響を受けず 10 + 9 = 19 であること。
    EXPECT_EQ(1, held_p10_after);            // [確認_正常系] - セッション変更後のピア 10 の送出上限が取得できること。
    EXPECT_EQ(29U, piggy_p10_after_reset);   // [確認_正常系] - ピア 10 はセッション変更で最大値が 0 に戻り 20 + 9 = 29 であること。
    EXPECT_EQ(1, held_p11_after);            // [確認_正常系] - ピア 11 の送出上限が取得できること。
    EXPECT_EQ(13U, piggy_p11_after_reset);   // [確認_正常系] - ピア 11 は 800 [byte] により 11 + 2 = 13 であること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// UDP の未通知ではパケットが届いていても停止開始から有効期限で相手なしへ移ることの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, udp_unnotified_activity_does_not_extend_deadline)
{
    // Arrange
    ctx.role = POTR_ROLE_SENDER;
    ctx.send_window.next_seq = 4U;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    int wait_ms = 0;
    int expired[2] = {};
    uint64_t states_after[2] = {};
    const potr_type types[] = {POTR_TYPE_UNICAST, POTR_TYPE_UNICAST_BIDIR};
    // [状態] - UDP のハンドル側 2 経路で初期幅を使い切る。
    // Pre-Assert
    // Act
    for (size_t i = 0U; i < 2U; i++)
    {
        ctx.service.type = types[i];
        ctx.flow_limit_stalled_ms = 0U;
        cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNNOTIFIED, CPLAT_MEMORY_ORDER_RELEASE);
        (void)potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance);
        cplat_atomic_store_u64(&ctx.flow_last_packet_ms, 1999U, CPLAT_MEMORY_ORDER_RELEASE);
        cplat_atomic_store_u64(&ctx.flow_limit_received_ms, 1999U, CPLAT_MEMORY_ORDER_RELEASE);
        expired[i] = potr_internal_flow_control_suppressed(&ctx, 2000U, &wait_ms, &allowance);
        states_after[i] = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    }
    // [手順] - 最初の通知を受けずにパケット受信時刻だけを更新し、停止開始から 1000 [ms] で判定する。
    // Assert
    for (size_t i = 0U; i < 2U; i++)
    {
        EXPECT_EQ(0, expired[i]); // [確認_正常系] - 受信の継続によらず停止を解くこと。(確認 2 回)
        EXPECT_EQ(POTR_FLOW_SEND_LIMIT_NO_PEER, states_after[i]); // [確認_正常系] - 相手なしへ移ること。(確認 2 回)
    }
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 期限判定に使う時刻の読み取り後に同値通知が入った場合、相手なしへの比較交換をやめることの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, expiry_rechecks_both_timestamps_before_compare_exchange)
{
    // Arrange
    ctx.role = POTR_ROLE_SENDER;
    ctx.send_window.next_seq = 300U;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    int lock_token = 0;
    ctx.send_window_mutex = reinterpret_cast<cplat_local_lock *>(&lock_token);
    const potr_type types[] = {POTR_TYPE_UNICAST, POTR_TYPE_UNICAST, POTR_TYPE_UNICAST_BIDIR, POTR_TYPE_UNICAST_BIDIR};
    int suppressed[4] = {};
    uint64_t states_after[4] = {};
    int wait_ms = 0;
    // [状態] - 2 通信種別と、通知時刻だけ・パケット時刻だけを更新する 4 条件を用意する。
    // Pre-Assert
    // Act
    for (size_t i = 0U; i < 4U; i++)
    {
        ctx.service.type = types[i];
        ctx.flow_limit_stalled_ms = 1000U;
        cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_HELD | 300U, CPLAT_MEMORY_ORDER_RELEASE);
        cplat_atomic_store_u64(&ctx.flow_limit_received_ms, 1000U, CPLAT_MEMORY_ORDER_RELEASE);
        cplat_atomic_store_u64(&ctx.flow_last_packet_ms, 1000U, CPLAT_MEMORY_ORDER_RELEASE);
        ON_CALL(mock_cplat, cplat_local_lock_lock(ctx.send_window_mutex)).WillByDefault([&](cplat_local_lock *) {
            if (i % 2U == 0U)
            {
                cplat_atomic_store_u64(&ctx.flow_limit_received_ms, 2000U, CPLAT_MEMORY_ORDER_RELEASE);
            }
            else
            {
                cplat_atomic_store_u64(&ctx.flow_last_packet_ms, 2000U, CPLAT_MEMORY_ORDER_RELEASE);
            }
            cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_HELD | 300U, CPLAT_MEMORY_ORDER_RELEASE);
        });
        ON_CALL(mock_cplat, cplat_local_lock_unlock(ctx.send_window_mutex)).WillByDefault(Return());
        suppressed[i] = potr_internal_flow_control_suppressed(&ctx, 2000U, &wait_ms, &allowance);
        states_after[i] = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    }
    ctx.send_window_mutex = nullptr;
    // [手順] - 古い時刻を読んだ後の送信ウィンドウのロック取得時に、受信側と同じ順序で時刻と同値上限を書き込む。
    // Assert
    for (size_t i = 0U; i < 4U; i++)
    {
        EXPECT_EQ(1, suppressed[i]); // [確認_正常系] - 通知の競合を検出した判定では制限を外さないこと。(確認 4 回)
        EXPECT_EQ(POTR_FLOW_SEND_LIMIT_HELD | 300U, states_after[i]); // [確認_正常系] - 同値の新しい通知を消さないこと。(確認 4 回)
    }
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 逆方向の初回 DATA より先に受けた TCP の上限なしを維持し、後続 6 DATA で初期幅に止まらないことの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, tcp_first_reverse_data_keeps_unlimited_notification)
{
    // Arrange
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    ctx.send_window.next_seq = 1U;
    ctx.peer_session_known = 0;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    thread_recv_slot slot = {};
    slot.ctx = &ctx;
    slot.peer_session_id = &ctx.peer_session_id;
    slot.peer_session_ts = &ctx.peer_session_ts;
    slot.peer_session_known = &ctx.peer_session_known;
    slot.reorder_pending = &ctx.reorder_pending;
    slot.pending_fin = &ctx.pending_fin;
    slot.fin_target_seq = &ctx.fin_target_seq;
    slot.recv_window = &ctx.recv_window;
    const uint8_t unlimited_payload[POTR_FLOW_CONTROL_PAYLOAD_SIZE] = {POTR_FLOW_CONTROL_STATE_UNLIMITED};
    potr_packet notification = {};
    notification.flags = POTR_FLAG_FLOW_CONTROL;
    notification.payload = unlimited_payload;
    notification.payload_len = POTR_FLOW_CONTROL_PAYLOAD_SIZE;
    potr_packet data = {};
    data.flags = POTR_FLAG_DATA;
    data.session_id = 42U;
    data.session_tv_sec = 100;
    int waits[6] = {};
    int wait_ms = 0;
    // [状態] - 自分の DATA 1 個を送出済みで、相手の受信セッションはまだ採用していない。
    // Pre-Assert
    // Act
    potr_internal_flow_control_on_packet(&ctx, nullptr, &notification);
    int adopted = thread_recv_session_adopt(&slot, &data);
    uint64_t retained = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    for (size_t i = 0U; i < 6U; i++)
    {
        ctx.send_window.next_seq = (uint32_t)i + 1U;
        waits[i] = potr_internal_flow_control_suppressed(&ctx, 1001U, &wait_ms, &allowance);
    }
    // [手順] - 相手の上限なし通知、相手の初回 DATA 採用、後続 6 DATA の送出判定を順に行う。
    // Assert
    EXPECT_EQ(1, adopted); // [確認_正常系] - 相手の初回 DATA でセッションを採用すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNLIMITED, retained); // [確認_正常系] - 採用時に上限なしを取り消さないこと。
    for (size_t i = 0U; i < 6U; i++)
    {
        EXPECT_EQ(0, waits[i]); // [確認_正常系] - 後続 DATA は初期幅で止まらないこと。(確認 6 回)
    }
    potr_internal_window_dispose(&ctx.recv_window);
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 未通知の期限切れ (相手なし) から、送出上限のないパケットで未通知 (現在通番) へ戻り、振動せず通知で回復することの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, udp_unnotified_no_peer_recovers_and_does_not_oscillate)
{
    // Arrange
    ctx.role = POTR_ROLE_SENDER;
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.send_window.next_seq = 4U;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    now_ms = 1000U;
    potr_packet ping_no_limit = {};
    ping_no_limit.flags = POTR_FLAG_PING;
    potr_packet notified = {};
    notified.flags = POTR_FLAG_FLOW_CONTROL;
    notified.payload = kResumePayload;
    notified.payload_len = POTR_FLOW_CONTROL_PAYLOAD_SIZE;
    int wait_ms = 0;
    // [状態] - unicast_bidir と unicast_bidir_n1 ピアで、最初の 4 DATA が失われた状態を模擬する。
    // Pre-Assert
    // Act
    ctx.flow_limit_stalled_ms = 0U;
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNNOTIFIED, CPLAT_MEMORY_ORDER_RELEASE);
    (void)potr_internal_flow_control_suppressed(&ctx, 1000U, &wait_ms, &allowance);
    now_ms = 1999U;
    potr_internal_flow_control_on_peer_packet(&ctx, nullptr, &ping_no_limit);
    int bidir_expired = potr_internal_flow_control_suppressed(&ctx, 2000U, &wait_ms, &allowance);
    now_ms = 2001U;
    potr_internal_flow_control_on_peer_packet(&ctx, nullptr, &ping_no_limit);
    uint64_t bidir_state_after_return = cplat_atomic_load_u64(&ctx.flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
    uint64_t bidir_stalled_after_return = ctx.flow_limit_stalled_ms;
    uint32_t bidir_reopened_allowance = 0U;
    int bidir_reopened = potr_internal_flow_control_suppressed(&ctx, 2001U, &wait_ms, &bidir_reopened_allowance);
    ctx.send_window.next_seq = 8U;
    int bidir_restopped = potr_internal_flow_control_suppressed(&ctx, 2010U, &wait_ms, &allowance);
    now_ms = 2015U;
    potr_internal_flow_control_on_peer_packet(&ctx, nullptr, &ping_no_limit);
    int bidir_no_oscillation = potr_internal_flow_control_suppressed(&ctx, 3009U, &wait_ms, &allowance);
    now_ms = 3009U;
    potr_internal_flow_control_on_packet(&ctx, nullptr, &notified);
    int bidir_recovered = potr_internal_flow_control_suppressed(&ctx, 3009U, &wait_ms, &allowance);

    enable_multi_peer();
    int deferred = -1;
    peer10->peer_id = 10U;
    peer10->send_window.next_seq = 4U;
    peer10->flow_send_limit = POTR_FLOW_SEND_LIMIT_UNNOTIFIED;
    cplat_atomic_store_i32(&peer10->health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    now_ms = 1000U;
    (void)potr_internal_flow_control_peer_state(&ctx, 10U, 1000U, &deferred);
    now_ms = 1999U;
    cplat_local_lock_lock(ctx.peers_mutex);
    potr_internal_flow_control_on_peer_packet(&ctx, peer10, &ping_no_limit);
    cplat_local_lock_unlock(ctx.peers_mutex);
    int n1_expired = potr_internal_flow_control_peer_state(&ctx, 10U, 2000U, &deferred);
    now_ms = 2001U;
    cplat_local_lock_lock(ctx.peers_mutex);
    potr_internal_flow_control_on_peer_packet(&ctx, peer10, &ping_no_limit);
    cplat_local_lock_unlock(ctx.peers_mutex);
    uint64_t n1_state_after_return = peer10->flow_send_limit;
    uint64_t n1_stalled_after_return = peer10->flow_limit_stalled_ms;
    int n1_reopened = potr_internal_flow_control_peer_state(&ctx, 10U, 2001U, &deferred);
    peer10->send_window.next_seq = 8U;
    int n1_restopped = potr_internal_flow_control_peer_state(&ctx, 10U, 2010U, &deferred);
    now_ms = 2015U;
    cplat_local_lock_lock(ctx.peers_mutex);
    potr_internal_flow_control_on_peer_packet(&ctx, peer10, &ping_no_limit);
    cplat_local_lock_unlock(ctx.peers_mutex);
    int n1_no_oscillation = potr_internal_flow_control_peer_state(&ctx, 10U, 3009U, &deferred);
    now_ms = 3009U;
    cplat_local_lock_lock(ctx.peers_mutex);
    potr_internal_flow_control_on_packet(&ctx, peer10, &notified);
    cplat_local_lock_unlock(ctx.peers_mutex);
    int n1_recovered = potr_internal_flow_control_peer_state(&ctx, 10U, 3009U, &deferred);
    // [手順] - 期限切れ→上限なしパケットで未通知 (基準 4) へ復帰→追加 DATA 4 個→再停止後に新期限直前まで振動しないこと→通知で回復を確認する。
    // Assert
    EXPECT_EQ(0, bidir_expired); // [確認_正常系] - unicast_bidir が初期幅の停止期限で相手なしへ移ること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 4U, bidir_state_after_return);
    // [確認_正常系] - unicast_bidir が復帰時に現在通番 4 を基準とする未通知へ戻ること。
    EXPECT_EQ(0U, bidir_stalled_after_return); // [確認_正常系] - unicast_bidir の復帰直後の停止開始時刻が 0 であること。
    EXPECT_EQ(0, bidir_reopened);              // [確認_正常系] - unicast_bidir が新しい初期幅で追加の DATA を送出できること。
    EXPECT_EQ(POTR_FLOW_INITIAL_WIDTH, bidir_reopened_allowance);
    // [確認_正常系] - unicast_bidir の復帰後の許可数が初期の幅 (4) であること。
    EXPECT_EQ(1, bidir_restopped);      // [確認_正常系] - unicast_bidir が新しい初期幅を使い切ると再び止まること。
    EXPECT_EQ(1, bidir_no_oscillation); // [確認_正常系] - unicast_bidir が新期限直前まで振動せず止まり続けること。
    EXPECT_EQ(0, bidir_recovered);      // [確認_正常系] - unicast_bidir が相手の通知で回復すること。
    EXPECT_EQ(0, n1_expired);           // [確認_正常系] - N:1 ピアが初期幅の停止期限で相手なしへ移ること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNNOTIFIED | 4U, n1_state_after_return);
    // [確認_正常系] - N:1 ピアが復帰時に現在通番 4 を基準とする未通知へ戻ること。
    EXPECT_EQ(0U, n1_stalled_after_return); // [確認_正常系] - N:1 ピアの復帰直後の停止開始時刻が 0 であること。
    EXPECT_EQ(0, n1_reopened);              // [確認_正常系] - N:1 ピアが新しい初期幅で追加の DATA を送出できること。
    EXPECT_EQ(1, n1_restopped);             // [確認_正常系] - N:1 ピアが新しい初期幅を使い切ると再び止まること。
    EXPECT_EQ(1, n1_no_oscillation);        // [確認_正常系] - N:1 ピアが新期限直前まで振動せず止まり続けること。
    EXPECT_EQ(0, n1_recovered);             // [確認_正常系] - N:1 ピアが相手の通知で回復すること。
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// TCP 受信側が DATA 受信に伴い、解除または抑制の要求と送出上限を cplat_socket_send_all で送出することの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, tcp_receiver_on_received_sends_resume_and_suppress_with_limit)
{
    // Arrange
    int send_all_count = 0;
    ctx.service.type = POTR_TYPE_TCP;
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.tcp_conn_fd[0] = (cplat_socket)7;
    ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&ctx.tcp_send_mutex[0]));
    // [状態確認] - TCP 送信用ロックを生成できること。
    ON_CALL(mock_cplat, cplat_socket_send_all(ctx.tcp_conn_fd[0], _, _, _))
        .WillByDefault([&](cplat_socket, const void *, size_t, cplat_error *) {
            send_all_count++;
            return CPLAT_OK;
        }); // [状態] - TCP 経路の cplat_socket_send_all() 呼び出し回数を記録して成功を返す。
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 容量 1000、要求 80%、解除 60%、max_payload 100 の TCP 受信側を開始する (許可数 9)。
    // [状態確認] - start の戻り値が POTR_OK であること。
    // Pre-Assert
    // Act
    received(POTR_PEER_NA, 1U, 1);
    int send_all_after_resume = send_all_count;
    std::vector<uint8_t> states_resume = states;
    std::vector<uint32_t> limits_resume = limits;
    // [手順] - 占有量 0 で DATA 受信 (next_seq=1) を通知する。
    potr_internal_flow_control_stop(&ctx);
    clear_records();
    send_all_count = 0;
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態確認] - 抑制の要求の検証用に再度 start できること。
    EXPECT_EQ(POTR_OK, admit(POTR_PEER_NA, 850U));
    clear_records();
    send_all_count = 0;
    received(POTR_PEER_NA, 1U, 1);
    int send_all_after_suppress = send_all_count;
    std::vector<uint8_t> states_suppress = states;
    std::vector<uint32_t> limits_suppress = limits;
    // [手順] - 占有量 850 (要求しきい値超過) の状態で DATA 受信 (next_seq=1) を通知する。
    // Assert
    EXPECT_EQ(1, send_all_after_resume);                  // [確認_正常系] - 解除通知が cplat_socket_send_all() で 1 回送出されること。
    EXPECT_EQ(0, sendto_count);                           // [確認_正常系] - UDP の cplat_socket_sendto() は使わないこと。
    EXPECT_EQ(std::vector<uint8_t>{kResume}, states_resume);
    // [確認_正常系] - しきい値以下では状態が解除 (RESUME) であること。
    EXPECT_EQ(std::vector<uint32_t>{10U}, limits_resume); // [確認_正常系] - 送出上限が 1 + 9 = 10 であること。
    EXPECT_EQ(1, send_all_after_suppress);                // [確認_正常系] - 抑制の要求が cplat_socket_send_all() で 1 回送出されること。
    EXPECT_EQ(std::vector<uint8_t>{kSuppress}, states_suppress);
    // [確認_正常系] - しきい値超過では状態が抑制の要求 (SUPPRESS) であること。
    EXPECT_EQ(std::vector<uint32_t>{1U}, limits_suppress); // [確認_正常系] - 要求中の送出上限が 1 + 0 = 1 であること。
    // Cleanup
    potr_internal_flow_control_stop(&ctx);
    cplat_local_lock_dispose(ctx.tcp_send_mutex[0]);
    ctx.tcp_send_mutex[0] = nullptr;
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]

// 送出上限を使わない TCP 受信側が DATA 受信に伴い「上限なし」を 1 回だけ送出することの確認
// [サブ手順参照 名前=potrFlowControlTest.SetUp]
TEST_F(potrFlowControlTest, tcp_receiver_without_limit_sends_unlimited_once)
{
    // Arrange
    int send_all_count = 0;
    ctx.service.type = POTR_TYPE_TCP;
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.flow_control_config.capacity = 0U;
    ctx.flow_control_config.receive_buffer_percent = 0U;
    ctx.tcp_conn_fd[0] = (cplat_socket)7;
    ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&ctx.tcp_send_mutex[0]));
    // [状態確認] - TCP 送信用ロックを生成できること。
    ON_CALL(mock_cplat, cplat_socket_send_all(ctx.tcp_conn_fd[0], _, _, _))
        .WillByDefault([&](cplat_socket, const void *, size_t, cplat_error *) {
            send_all_count++;
            return CPLAT_OK;
        }); // [状態] - TCP 経路の cplat_socket_send_all() 呼び出し回数を記録して成功を返す。
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&ctx));
    // [状態] - 容量 0・受信バッファー上限なしの TCP 受信側を開始する。
    // [状態確認] - 上限なし通知用の資源が生成され POTR_OK を返すこと。
    // Pre-Assert
    // Act
    received(POTR_PEER_NA, 1U, 1);
    received(POTR_PEER_NA, 2U, 1);
    // [手順] - 同じセッションで DATA 受信を 2 回続けて通知する。
    // Assert
    EXPECT_EQ(1, send_all_count); // [確認_正常系] - cplat_socket_send_all() で 1 回だけ送出されること。
    EXPECT_EQ(0, sendto_count);   // [確認_正常系] - UDP の cplat_socket_sendto() は使わないこと。
    EXPECT_EQ(std::vector<uint8_t>{(uint8_t)POTR_FLOW_CONTROL_STATE_UNLIMITED}, states);
    // [確認_正常系] - 状態が上限なし (UNLIMITED) のパケットを 1 回だけ組み立てること。
    EXPECT_EQ(std::vector<uint32_t>{0U}, limits); // [確認_正常系] - 上限なしの送出上限の欄が 0 であること。
    // Cleanup
    potr_internal_flow_control_stop(&ctx);
    cplat_local_lock_dispose(ctx.tcp_send_mutex[0]);
    ctx.tcp_send_mutex[0] = nullptr;
}
// [サブ手順参照 名前=potrFlowControlTest.TearDown]
