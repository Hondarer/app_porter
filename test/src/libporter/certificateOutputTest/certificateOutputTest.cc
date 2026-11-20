/**
 *******************************************************************************
 *  @file           certificateOutputTest.cc
 *  @brief          証明書方式のハンドシェイクの出力の失敗の診断と、クライアントの再試行と DTLS の再送の出力を確認します。
 *
 *  認証の処理は mock_porter の mock で置き換え、cplat_malloc の失敗で出力の失敗を起こします。
 *  再試行と再送の出力は、新しい試行の作成 (potr_internal_auth_session_create) と古い試行の再送
 *  (potr_internal_auth_session_handle_timeout) の呼び出しで判定します。
 *  結合テスト integration/certificateTest と区別するため、名前に Output を付けます。
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
#include <porter/potr_context.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/receiver_report.h>
#include <porter/security/certificate.h>
#include <cstdlib>

using namespace testing;

namespace
{
/** テストの最大ペイロード長 [byte]。出力レコードの確保の大きさとして照合する。 */
constexpr uint16_t kMaxPayload = 1400U;

/** 認証セッションの代わりに渡す値。mock だけが受け取り、参照しない。 */
potr_internal_auth_session *const kFakeSession = reinterpret_cast<potr_internal_auth_session *>(0x1000);

/** 再試行で作成する新しい認証セッションの代わりに渡す値。mock だけが受け取り、参照しない。 */
potr_internal_auth_session *const kNewSession = reinterpret_cast<potr_internal_auth_session *>(0x2000);
} // namespace

class certificateOutputTest : public Test
{
  protected:
    // [サブ手順 名前=certificateOutputTest.SetUp]
    void SetUp() override
    {
        ctx = static_cast<potr_context *>(std::calloc(1U, sizeof(potr_context)));
        ASSERT_NE(nullptr, ctx); // [状態確認] - コンテキストを確保できること。
        ctx->service.auth = POTR_AUTH_CERT;
        ctx->service.service_id = 42;
        ctx->global.max_payload = kMaxPayload;
        ctx->n_path = 1;
        ctx->sock[0] = CPLAT_INVALID_SOCKET;
        // [状態] - 証明書方式、サービス ID 42、最大ペイロード長 1400、経路 1 本のコンテキストを用意する。
        ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&ctx->certificate_mutex)); // [状態確認] - certificate_mutex を作成できること。
        ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&ctx->callback_mutex)); // [状態確認] - callback_mutex を作成できること。
    }
    // [サブ手順終了]

    // [サブ手順 名前=certificateOutputTest.TearDown]
    void TearDown() override
    {
        if (ctx == nullptr)
        {
            return;
        }

        while (ctx->receivers != nullptr)
        {
            potr_internal_receiver *receiver = ctx->receivers;

            ctx->receivers = receiver->next;
            potr_internal_certificate_state_dispose(&receiver->certificate);
            cplat_free(receiver);
        }
        potr_internal_certificate_state_dispose(&ctx->certificate);
        cplat_local_lock_dispose(ctx->callback_mutex);
        cplat_local_lock_dispose(ctx->certificate_mutex);
        std::free(ctx);
        ctx = nullptr;
        // [手順] - 受信者の管理表、認証の状態、ロック、コンテキストを破棄する。
    }
    // [サブ手順終了]

    // [サブ手順 名前=certificateOutputTest.PrepareClient]
    void PrepareClient(Mock_porter &mock_porter, uint32_t retry_interval_ms)
    {
        ctx->service.type = POTR_TYPE_UNICAST;
        ctx->role = POTR_ROLE_RECEIVER;
        ctx->health_interval_ms = retry_interval_ms;
        // [状態] - 片方向 UDP の受信者 (クライアント側) で、再試行の周期を retry_interval_ms とする条件を用意する。
        ON_CALL(mock_porter, potr_internal_auth_session_create(_, _, _, _))
            .WillByDefault(DoAll(SetArgPointee<3>(kNewSession), Return(POTR_OK)));
        // [状態] - potr_internal_auth_session_create() から新しい認証セッションを返すように既定動作を設定する。
        ON_CALL(mock_porter, potr_internal_auth_session_receive(_, _, _, _))
            .WillByDefault(DoAll(SetArgPointee<3>(POTR_INTERNAL_AUTH_STATE_IN_PROGRESS), Return(POTR_OK)));
        // [状態] - potr_internal_auth_session_receive() から認証の進行中を返すように既定動作を設定する。
        ON_CALL(mock_porter, potr_internal_auth_session_handle_timeout(_, _))
            .WillByDefault(DoAll(SetArgPointee<1>(POTR_INTERNAL_AUTH_STATE_IN_PROGRESS), Return(POTR_OK)));
        // [状態] - potr_internal_auth_session_handle_timeout() から認証の進行中を返すように既定動作を設定する。
        ON_CALL(mock_porter, potr_internal_auth_session_take_output(_, _, _, _))
            .WillByDefault(DoAll(SetArgPointee<3>(0U), Return(POTR_OK)));
        // [状態] - potr_internal_auth_session_take_output() から出力なし (0 byte) を返すように既定動作を設定する。
    }
    // [サブ手順終了]

    potr_context *ctx = nullptr;
};

// 受信したハンドシェイクの出力の確保に失敗した場合に、WARNING のトレースを 1 回出すことの確認 (P-022)
// [サブ手順参照 名前=certificateOutputTest.SetUp]
TEST_F(certificateOutputTest, receive_warns_when_handshake_output_fails)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    const uint8_t payload[] = {22U, 1U, 2U, 3U};
    const uint8_t wire_header[POTR_PACKET_HEADER_SIZE] = {};
    cplat_ipv4_endpoint sender = {};
    potr_packet packet = {};
    ctx->service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx->role = POTR_ROLE_RECEIVER;
    packet.service_id = 42;
    packet.flags = POTR_FLAG_HANDSHAKE;
    packet.payload = payload;
    packet.payload_len = (uint16_t)sizeof(payload);
    sender.address = 0x0100007FU;
    sender.port = 0x3930U;
    // [状態] - 双方向 UDP の受信者 (サーバー側) が、最初のハンドシェイクを受信する条件を用意する。
    ON_CALL(mock_porter, potr_internal_auth_session_create(_, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(kFakeSession), Return(POTR_OK)));
    // [状態] - potr_internal_auth_session_create() から認証セッションを返すように既定動作を設定する。
    ON_CALL(mock_porter, potr_internal_auth_session_receive(_, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(POTR_INTERNAL_AUTH_STATE_IN_PROGRESS), Return(POTR_OK)));
    // [状態] - potr_internal_auth_session_receive() から認証の進行中を返すように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_malloc(_)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_malloc(kMaxPayload)).WillOnce(Return(nullptr));
    // [Pre-Assert確認_異常系] - 出力レコードの確保 (最大ペイロード長) が 1 回呼ばれること。
    // [Pre-Assert手順] - nullptr を返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_WARNING, nullptr,
                                                   HasSubstr("handshake output failed")))
        .Times(1);
    // [Pre-Assert確認_異常系] - WARNING と出力の失敗を示す文字列でトレースが 1 回出力されること。

    // Act
    int actual_ret = potr_internal_certificate_receive(ctx, &packet, wire_header, &sender, 0);
    // [手順] - potr_internal_certificate_receive() でハンドシェイクを処理する。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_異常系] - 出力の失敗でも potr_internal_certificate_receive() が POTR_OK を返すこと。
}
// [サブ手順参照 名前=certificateOutputTest.TearDown]

// 認証の失敗でセッションを破棄した後の出力 (POTR_ERR_DISCONNECTED) では、出力の失敗の WARNING を出さないことの確認 (P-022)
// [サブ手順参照 名前=certificateOutputTest.SetUp]
TEST_F(certificateOutputTest, receive_does_not_warn_when_session_is_disconnected)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    const uint8_t payload[] = {22U, 1U, 2U, 3U};
    const uint8_t wire_header[POTR_PACKET_HEADER_SIZE] = {};
    cplat_ipv4_endpoint sender = {};
    potr_packet packet = {};
    ctx->service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx->role = POTR_ROLE_RECEIVER;
    packet.service_id = 42;
    packet.flags = POTR_FLAG_HANDSHAKE;
    packet.payload = payload;
    packet.payload_len = (uint16_t)sizeof(payload);
    sender.address = 0x0100007FU;
    sender.port = 0x3930U;
    // [状態] - 双方向 UDP の受信者 (サーバー側) が、最初のハンドシェイクを受信する条件を用意する。
    ON_CALL(mock_porter, potr_internal_auth_session_create(_, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(kFakeSession), Return(POTR_OK)));
    // [状態] - potr_internal_auth_session_create() から認証セッションを返すように既定動作を設定する。
    ON_CALL(mock_porter, potr_internal_auth_session_receive(_, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(POTR_INTERNAL_AUTH_STATE_FAILED), Return(POTR_OK)));
    // [状態] - potr_internal_auth_session_receive() から認証の失敗を返すように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_malloc(_)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_malloc(kMaxPayload)).Times(0);
    // [Pre-Assert確認_異常系] - セッションの破棄後は出力レコードを確保しないこと。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_WARNING, nullptr,
                                                   HasSubstr("authentication failed")))
        .Times(1);
    // [Pre-Assert確認_異常系] - 認証の失敗の WARNING が 1 回出力されること。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, HasSubstr("handshake output failed"))).Times(0);
    // [Pre-Assert確認_異常系] - 出力の失敗の WARNING を出力しないこと。

    // Act
    int actual_ret = potr_internal_certificate_receive(ctx, &packet, wire_header, &sender, 0);
    // [手順] - potr_internal_certificate_receive() で認証に失敗するハンドシェイクを処理する。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_異常系] - potr_internal_certificate_receive() が POTR_OK を返すこと。
    EXPECT_EQ(nullptr, ctx->certificate.session); // [確認_異常系] - 認証セッションが破棄されていること。
}
// [サブ手順参照 名前=certificateOutputTest.TearDown]

// マルチキャストの送信者でグループ鍵の送出の確保に失敗した場合に、WARNING のトレースを 1 回出すことの確認 (P-023)
// [サブ手順参照 名前=certificateOutputTest.SetUp]
TEST_F(certificateOutputTest, receive_warns_when_group_key_send_fails)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    const uint8_t payload[] = {22U, 1U, 2U, 3U};
    const uint8_t wire_header[POTR_PACKET_HEADER_SIZE] = {};
    cplat_ipv4_endpoint sender = {};
    potr_packet packet = {};
    ctx->service.type = POTR_TYPE_MULTICAST;
    ctx->role = POTR_ROLE_SENDER;
    packet.service_id = 42;
    packet.flags = POTR_FLAG_HANDSHAKE;
    packet.payload = payload;
    packet.payload_len = (uint16_t)sizeof(payload);
    sender.address = 0x0100007FU;
    sender.port = 0x3930U;
    // [状態] - マルチキャストの送信者 (サーバー側) が、受信者からハンドシェイクを受信する条件を用意する。
    ON_CALL(mock_porter, potr_internal_auth_session_create(_, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(kFakeSession), Return(POTR_OK)));
    // [状態] - potr_internal_auth_session_create() から認証セッションを返すように既定動作を設定する。
    ON_CALL(mock_porter, potr_internal_auth_session_receive(_, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(POTR_INTERNAL_AUTH_STATE_ESTABLISHED), Return(POTR_OK)));
    // [状態] - potr_internal_auth_session_receive() から認証の確立を返すように既定動作を設定する。
    ON_CALL(mock_porter, potr_internal_auth_session_export_keys(_, _, _, _, _)).WillByDefault(Return(POTR_OK));
    // [状態] - potr_internal_auth_session_export_keys() から鍵の導出の成功を返すように既定動作を設定する。
    ON_CALL(mock_porter, potr_internal_auth_session_take_output(_, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(0U), Return(POTR_OK)));
    // [状態] - potr_internal_auth_session_take_output() から出力なし (0 byte) を返すように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_malloc(_)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_malloc(POTR_PACKET_HEADER_SIZE + kMaxPayload)).WillOnce(Return(nullptr));
    // [Pre-Assert確認_異常系] - グループ鍵の送出のパケットの確保が 1 回呼ばれること。
    // [Pre-Assert手順] - nullptr を返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_WARNING, nullptr,
                                                   HasSubstr("group key send failed")))
        .Times(1);
    // [Pre-Assert確認_異常系] - WARNING とグループ鍵の送出の失敗を示す文字列でトレースが 1 回出力されること。

    // Act
    int actual_ret = potr_internal_certificate_receive(ctx, &packet, wire_header, &sender, 0);
    // [手順] - potr_internal_certificate_receive() で認証を確立するハンドシェイクを処理する。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_異常系] - 送出の失敗でも potr_internal_certificate_receive() が POTR_OK を返すこと。
    ASSERT_NE(nullptr, ctx->receivers); // [状態確認] - 受信者の管理エントリが作成されていること。
    EXPECT_NE(0, ctx->receivers->certificate.established); // [確認_異常系] - 受信者との認証は確立した状態のままであること。
}
// [サブ手順参照 名前=certificateOutputTest.TearDown]

// DTLS の再送の出力の確保に失敗した場合に、WARNING のトレースを 1 回出すことの確認 (P-024)
// [サブ手順参照 名前=certificateOutputTest.SetUp]
TEST_F(certificateOutputTest, tick_warns_when_retransmission_output_fails)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ctx->service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx->role = POTR_ROLE_RECEIVER;
    ctx->certificate.session = kFakeSession;
    // [状態] - 双方向 UDP の受信者 (サーバー側) が、認証の進行中のセッションを持つ状態を用意する。
    ON_CALL(mock_porter, potr_internal_auth_session_get_timeout_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<1>(1), SetArgPointee<2>(0U), Return(POTR_OK)));
    // [状態] - potr_internal_auth_session_get_timeout_ms() から再送の期限の到来 (残り 0 [ms]) を返すように既定動作を設定する。
    ON_CALL(mock_porter, potr_internal_auth_session_handle_timeout(_, _))
        .WillByDefault(DoAll(SetArgPointee<1>(POTR_INTERNAL_AUTH_STATE_IN_PROGRESS), Return(POTR_OK)));
    // [状態] - potr_internal_auth_session_handle_timeout() から認証の進行中を返すように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_malloc(_)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_malloc(kMaxPayload)).WillOnce(Return(nullptr));
    // [Pre-Assert確認_異常系] - 再送の出力レコードの確保が 1 回呼ばれること。
    // [Pre-Assert手順] - nullptr を返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_WARNING, nullptr,
                                                   HasSubstr("handshake output failed")))
        .Times(1);
    // [Pre-Assert確認_異常系] - WARNING と出力の失敗を示す文字列でトレースが 1 回出力されること。

    // Act
    potr_internal_certificate_tick(ctx, 1000U); // [手順] - potr_internal_certificate_tick() で再送の期限を処理する。

    // Assert
    EXPECT_EQ(kFakeSession, ctx->certificate.session); // [確認_異常系] - 出力の失敗では認証セッションを破棄しないこと。
    EXPECT_NE(0, ctx->certificate.retransmitted); // [確認_異常系] - 再送を試みた状態が記録されていること。
}
// [サブ手順参照 名前=certificateOutputTest.TearDown]

// クライアントの最初の送出の確保に失敗した場合に、WARNING のトレースを 1 回出すことの確認 (P-025)
// [サブ手順参照 名前=certificateOutputTest.SetUp]
TEST_F(certificateOutputTest, tick_warns_when_client_first_output_fails)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    ctx->service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx->role = POTR_ROLE_SENDER;
    // [状態] - 双方向 UDP の送信者 (クライアント側) が、認証セッションを持たない状態を用意する。
    ON_CALL(mock_porter, potr_internal_auth_session_create(_, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(kFakeSession), Return(POTR_OK)));
    // [状態] - potr_internal_auth_session_create() から認証セッションを返すように既定動作を設定する。
    ON_CALL(mock_porter, potr_internal_auth_session_receive(_, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(POTR_INTERNAL_AUTH_STATE_IN_PROGRESS), Return(POTR_OK)));
    // [状態] - potr_internal_auth_session_receive() から認証の進行中を返すように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_auth_session_create(_, POTR_INTERNAL_AUTH_ROLE_CLIENT, _, _)).Times(1);
    // [Pre-Assert確認_異常系] - クライアントの役割で認証セッションを 1 回作成すること。
    EXPECT_CALL(mock_cplat, cplat_malloc(_)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_malloc(kMaxPayload)).WillOnce(Return(nullptr));
    // [Pre-Assert確認_異常系] - 最初の送出の出力レコードの確保が 1 回呼ばれること。
    // [Pre-Assert手順] - nullptr を返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_WARNING, nullptr,
                                                   HasSubstr("handshake output failed")))
        .Times(1);
    // [Pre-Assert確認_異常系] - WARNING と出力の失敗を示す文字列でトレースが 1 回出力されること。

    // Act
    potr_internal_certificate_tick(ctx, 1000U); // [手順] - potr_internal_certificate_tick() でクライアントの再試行を処理する。

    // Assert
    EXPECT_EQ(kFakeSession, ctx->certificate.session); // [確認_異常系] - 出力の失敗では作成した認証セッションを破棄しないこと。
}
// [サブ手順参照 名前=certificateOutputTest.TearDown]

// DTLS の再送の期限と再試行の周期 (DTLS の初回の再送タイマーより短い) が同じ周期処理で到来した場合に、
// 古い試行を再送せず、新しい試行だけを開始することの確認
// [サブ手順参照 名前=certificateOutputTest.SetUp]
TEST_F(certificateOutputTest, tick_client_starts_only_new_attempt_when_retransmission_and_retry_are_due)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    PrepareClient(mock_porter, 500U); // [サブ手順参照 名前=certificateOutputTest.PrepareClient]
    ctx->certificate.session = kFakeSession;
    ctx->certificate.retry_ms = 0U;
    // [状態] - 時刻 0 [ms] に開始し、まだ再送していない試行の認証セッションを持つ状態を用意する。
    ON_CALL(mock_porter, potr_internal_auth_session_get_timeout_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<1>(1), SetArgPointee<2>(0U), Return(POTR_OK)));
    // [状態] - potr_internal_auth_session_get_timeout_ms() から再送の期限の到来 (残り 0 [ms]) を返すように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_auth_session_handle_timeout(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - 古い試行の再送 (potr_internal_auth_session_handle_timeout()) を呼ばないこと。
    EXPECT_CALL(mock_porter, potr_internal_auth_session_dispose(_)).Times(AnyNumber());
    EXPECT_CALL(mock_porter, potr_internal_auth_session_dispose(kFakeSession)).Times(1);
    // [Pre-Assert確認_正常系] - 古い試行の認証セッションを 1 回破棄すること。
    EXPECT_CALL(mock_porter, potr_internal_auth_session_create(_, POTR_INTERNAL_AUTH_ROLE_CLIENT, _, _)).Times(1);
    // [Pre-Assert確認_正常系] - クライアントの役割で新しい試行の認証セッションを 1 回作成すること。
    EXPECT_CALL(mock_porter, potr_internal_auth_session_receive(_, _, _, _)).Times(0);
    EXPECT_CALL(mock_porter, potr_internal_auth_session_receive(kNewSession, nullptr, 0U, _)).Times(1);
    // [Pre-Assert確認_正常系] - 新しい試行の最初の flight (ClientHello) の生成を 1 回だけ行うこと。

    // Act
    potr_internal_certificate_tick(ctx, 1000U); // [手順] - 時刻 1000 [ms] で potr_internal_certificate_tick() を呼ぶ。

    // Assert
    EXPECT_EQ(kNewSession, ctx->certificate.session); // [確認_正常系] - 認証セッションが新しい試行のものに替わること。
    EXPECT_EQ(1000U, ctx->certificate.retry_ms); // [確認_正常系] - 試行の開始時刻が 1000 [ms] であること。
    EXPECT_EQ(0, ctx->certificate.retransmitted); // [確認_正常系] - 新しい試行は再送していない状態であること。
}
// [サブ手順参照 名前=certificateOutputTest.TearDown]

// 再送済みの試行で、DTLS の再送の期限と再試行の周期 (DTLS の再送タイマーより長い) が同じ周期処理で到来した場合に、
// 古い試行を再送せず、新しい試行だけを開始することの確認
// [サブ手順参照 名前=certificateOutputTest.SetUp]
TEST_F(certificateOutputTest, tick_client_starts_only_new_attempt_when_retransmitted_attempt_reaches_retry)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    PrepareClient(mock_porter, 3000U); // [サブ手順参照 名前=certificateOutputTest.PrepareClient]
    ctx->certificate.session = kFakeSession;
    ctx->certificate.retry_ms = 0U;
    ctx->certificate.retransmitted = 1;
    // [状態] - 時刻 0 [ms] に開始し、すでに 1 回再送した試行の認証セッションを持つ状態を用意する。
    ON_CALL(mock_porter, potr_internal_auth_session_get_timeout_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<1>(1), SetArgPointee<2>(0U), Return(POTR_OK)));
    // [状態] - potr_internal_auth_session_get_timeout_ms() から再送の期限の到来 (残り 0 [ms]) を返すように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_auth_session_handle_timeout(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - 古い試行の再送 (potr_internal_auth_session_handle_timeout()) を呼ばないこと。
    EXPECT_CALL(mock_porter, potr_internal_auth_session_create(_, POTR_INTERNAL_AUTH_ROLE_CLIENT, _, _)).Times(1);
    // [Pre-Assert確認_正常系] - クライアントの役割で新しい試行の認証セッションを 1 回作成すること。
    EXPECT_CALL(mock_porter, potr_internal_auth_session_receive(_, _, _, _)).Times(0);
    EXPECT_CALL(mock_porter, potr_internal_auth_session_receive(kNewSession, nullptr, 0U, _)).Times(1);
    // [Pre-Assert確認_正常系] - 新しい試行の最初の flight (ClientHello) の生成を 1 回だけ行うこと。

    // Act
    potr_internal_certificate_tick(ctx, 3000U); // [手順] - 時刻 3000 [ms] で potr_internal_certificate_tick() を呼ぶ。

    // Assert
    EXPECT_EQ(kNewSession, ctx->certificate.session); // [確認_正常系] - 認証セッションが新しい試行のものに替わること。
    EXPECT_EQ(3000U, ctx->certificate.retry_ms); // [確認_正常系] - 試行の開始時刻が 3000 [ms] であること。
    EXPECT_EQ(0, ctx->certificate.retransmitted); // [確認_正常系] - 新しい試行は再送していない状態であること。
}
// [サブ手順参照 名前=certificateOutputTest.TearDown]

// 再試行の周期が経過していない周期処理で DTLS の再送の期限が到来した場合に、古い試行の再送だけを行うことの確認
// [サブ手順参照 名前=certificateOutputTest.SetUp]
TEST_F(certificateOutputTest, tick_client_only_retransmits_when_retry_interval_has_not_elapsed)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    PrepareClient(mock_porter, 3000U); // [サブ手順参照 名前=certificateOutputTest.PrepareClient]
    ctx->certificate.session = kFakeSession;
    ctx->certificate.retry_ms = 0U;
    // [状態] - 時刻 0 [ms] に開始し、まだ再送していない試行の認証セッションを持つ状態を用意する。
    ON_CALL(mock_porter, potr_internal_auth_session_get_timeout_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<1>(1), SetArgPointee<2>(0U), Return(POTR_OK)));
    // [状態] - potr_internal_auth_session_get_timeout_ms() から再送の期限の到来 (残り 0 [ms]) を返すように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_auth_session_handle_timeout(kFakeSession, _)).Times(1);
    // [Pre-Assert確認_正常系] - 古い試行の認証セッションで再送 (potr_internal_auth_session_handle_timeout()) を 1 回呼ぶこと。
    EXPECT_CALL(mock_porter, potr_internal_auth_session_dispose(_)).Times(0);
    // [Pre-Assert確認_正常系] - 認証セッションを破棄しないこと。
    EXPECT_CALL(mock_porter, potr_internal_auth_session_create(_, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 新しい試行の認証セッションを作成しないこと。

    // Act
    potr_internal_certificate_tick(ctx, 1000U); // [手順] - 時刻 1000 [ms] で potr_internal_certificate_tick() を呼ぶ。

    // Assert
    EXPECT_EQ(kFakeSession, ctx->certificate.session); // [確認_正常系] - 認証セッションが古い試行のままであること。
    EXPECT_EQ(0U, ctx->certificate.retry_ms); // [確認_正常系] - 試行の開始時刻が 0 [ms] のままであること。
    EXPECT_NE(0, ctx->certificate.retransmitted); // [確認_正常系] - 再送した状態が記録されていること。
}
// [サブ手順参照 名前=certificateOutputTest.TearDown]

// 前の周期処理で再送した試行は、DTLS の再送の期限が到来していなくても、再試行の周期の経過で再試行することの確認
// [サブ手順参照 名前=certificateOutputTest.SetUp]
TEST_F(certificateOutputTest, tick_client_retries_retransmitted_attempt_after_retry_interval)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    PrepareClient(mock_porter, 500U); // [サブ手順参照 名前=certificateOutputTest.PrepareClient]
    ctx->certificate.session = kFakeSession;
    ctx->certificate.retry_ms = 0U;
    ctx->certificate.retransmitted = 1;
    // [状態] - 時刻 0 [ms] に開始し、すでに 1 回再送した試行の認証セッションを持つ状態を用意する。
    ON_CALL(mock_porter, potr_internal_auth_session_get_timeout_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<1>(1), SetArgPointee<2>(500U), Return(POTR_OK)));
    // [状態] - potr_internal_auth_session_get_timeout_ms() から再送の期限の未到来 (残り 500 [ms]) を返すように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_auth_session_handle_timeout(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - 古い試行の再送 (potr_internal_auth_session_handle_timeout()) を呼ばないこと。
    EXPECT_CALL(mock_porter, potr_internal_auth_session_create(_, POTR_INTERNAL_AUTH_ROLE_CLIENT, _, _)).Times(1);
    // [Pre-Assert確認_正常系] - クライアントの役割で新しい試行の認証セッションを 1 回作成すること。

    // Act
    potr_internal_certificate_tick(ctx, 1000U); // [手順] - 時刻 1000 [ms] で potr_internal_certificate_tick() を呼ぶ。

    // Assert
    EXPECT_EQ(kNewSession, ctx->certificate.session); // [確認_正常系] - 認証セッションが新しい試行のものに替わること。
    EXPECT_EQ(1000U, ctx->certificate.retry_ms); // [確認_正常系] - 試行の開始時刻が 1000 [ms] であること。
}
// [サブ手順参照 名前=certificateOutputTest.TearDown]

// 再送の期限が到来せず、再送もしていない試行は、再試行の周期が経過しても再試行も再送もしないことの確認
// [サブ手順参照 名前=certificateOutputTest.SetUp]
TEST_F(certificateOutputTest, tick_client_keeps_attempt_without_retransmission)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    PrepareClient(mock_porter, 500U); // [サブ手順参照 名前=certificateOutputTest.PrepareClient]
    ctx->certificate.session = kFakeSession;
    ctx->certificate.retry_ms = 0U;
    // [状態] - 時刻 0 [ms] に開始し、まだ再送していない試行の認証セッションを持つ状態を用意する。
    ON_CALL(mock_porter, potr_internal_auth_session_get_timeout_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<1>(1), SetArgPointee<2>(500U), Return(POTR_OK)));
    // [状態] - potr_internal_auth_session_get_timeout_ms() から再送の期限の未到来 (残り 500 [ms]) を返すように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_auth_session_handle_timeout(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - 再送 (potr_internal_auth_session_handle_timeout()) を呼ばないこと。
    EXPECT_CALL(mock_porter, potr_internal_auth_session_create(_, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 新しい試行の認証セッションを作成しないこと。

    // Act
    potr_internal_certificate_tick(ctx, 1000U); // [手順] - 時刻 1000 [ms] で potr_internal_certificate_tick() を呼ぶ。

    // Assert
    EXPECT_EQ(kFakeSession, ctx->certificate.session); // [確認_正常系] - 認証セッションが進行中の試行のままであること。
    EXPECT_EQ(0, ctx->certificate.retransmitted); // [確認_正常系] - 再送していない状態のままであること。
}
// [サブ手順参照 名前=certificateOutputTest.TearDown]

// 認証に失敗した後は、再試行の周期の経過で新しい試行を開始することの確認
// [サブ手順参照 名前=certificateOutputTest.SetUp]
TEST_F(certificateOutputTest, tick_client_retries_after_failure)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    PrepareClient(mock_porter, 500U); // [サブ手順参照 名前=certificateOutputTest.PrepareClient]
    ctx->certificate.failed = 1;
    ctx->certificate.retry_ms = 0U;
    // [状態] - 時刻 0 [ms] に開始した試行が失敗し、認証セッションを破棄した状態を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_auth_session_get_timeout_ms(_, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 認証セッションがないため、再送の期限を問い合わせないこと。
    EXPECT_CALL(mock_porter, potr_internal_auth_session_handle_timeout(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - 再送 (potr_internal_auth_session_handle_timeout()) を呼ばないこと。
    EXPECT_CALL(mock_porter, potr_internal_auth_session_create(_, POTR_INTERNAL_AUTH_ROLE_CLIENT, _, _)).Times(1);
    // [Pre-Assert確認_正常系] - クライアントの役割で新しい試行の認証セッションを 1 回作成すること。

    // Act
    potr_internal_certificate_tick(ctx, 500U); // [手順] - 時刻 500 [ms] で potr_internal_certificate_tick() を呼ぶ。

    // Assert
    EXPECT_EQ(kNewSession, ctx->certificate.session); // [確認_正常系] - 新しい試行の認証セッションを持つこと。
    EXPECT_EQ(0, ctx->certificate.failed); // [確認_正常系] - 失敗の状態が解除されていること。
    EXPECT_EQ(500U, ctx->certificate.retry_ms); // [確認_正常系] - 試行の開始時刻が 500 [ms] であること。
}
// [サブ手順参照 名前=certificateOutputTest.TearDown]

// 認証に失敗した後は、再試行の周期が経過するまで新しい試行を開始しないことの確認
// [サブ手順参照 名前=certificateOutputTest.SetUp]
TEST_F(certificateOutputTest, tick_client_waits_retry_interval_after_failure)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    PrepareClient(mock_porter, 500U); // [サブ手順参照 名前=certificateOutputTest.PrepareClient]
    ctx->certificate.failed = 1;
    ctx->certificate.retry_ms = 0U;
    // [状態] - 時刻 0 [ms] に開始した試行が失敗し、認証セッションを破棄した状態を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_auth_session_create(_, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 新しい試行の認証セッションを作成しないこと。

    // Act
    potr_internal_certificate_tick(ctx, 499U); // [手順] - 時刻 499 [ms] で potr_internal_certificate_tick() を呼ぶ。

    // Assert
    EXPECT_EQ(nullptr, ctx->certificate.session); // [確認_正常系] - 認証セッションを持たないままであること。
    EXPECT_NE(0, ctx->certificate.failed); // [確認_正常系] - 失敗の状態のままであること。
}
// [サブ手順参照 名前=certificateOutputTest.TearDown]

// 認証セッションを持たない最初の周期処理では、再試行の周期を待たずに試行を開始することの確認
// [サブ手順参照 名前=certificateOutputTest.SetUp]
TEST_F(certificateOutputTest, tick_client_starts_first_attempt_without_session)
{
    // Arrange
    NiceMock<Mock_porter> mock_porter;
    PrepareClient(mock_porter, 500U); // [サブ手順参照 名前=certificateOutputTest.PrepareClient]
    ctx->certificate.retry_ms = 0U;
    // [状態] - 認証セッションを持たず、失敗もしていない状態を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_auth_session_handle_timeout(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - 再送 (potr_internal_auth_session_handle_timeout()) を呼ばないこと。
    EXPECT_CALL(mock_porter, potr_internal_auth_session_create(_, POTR_INTERNAL_AUTH_ROLE_CLIENT, _, _)).Times(1);
    // [Pre-Assert確認_正常系] - クライアントの役割で認証セッションを 1 回作成すること。
    EXPECT_CALL(mock_porter, potr_internal_auth_session_receive(_, _, _, _)).Times(0);
    EXPECT_CALL(mock_porter, potr_internal_auth_session_receive(kNewSession, nullptr, 0U, _)).Times(1);
    // [Pre-Assert確認_正常系] - 最初の flight (ClientHello) の生成を 1 回だけ行うこと。

    // Act
    potr_internal_certificate_tick(ctx, 100U); // [手順] - 時刻 100 [ms] で potr_internal_certificate_tick() を呼ぶ。

    // Assert
    EXPECT_EQ(kNewSession, ctx->certificate.session); // [確認_正常系] - 認証セッションを持つこと。
    EXPECT_EQ(100U, ctx->certificate.retry_ms); // [確認_正常系] - 試行の開始時刻が 100 [ms] であること。
}
// [サブ手順参照 名前=certificateOutputTest.TearDown]
