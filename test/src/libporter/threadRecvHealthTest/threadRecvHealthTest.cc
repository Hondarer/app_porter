#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <porter/porter_result.h>
#include <cstring>
using namespace testing;

TEST(threadRecvHealthTest, service_disabled)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    thread_recv_slot slot = {};
    slot.ctx = &ctx;
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.n_path = 1;
    ctx.health_timeout_ms = 0;
    ctx.path_last_recv_ts[0].tv_sec = 10;
    ctx.last_recv_ts.tv_sec = 10;
    ctx.peer_port[0] = 123;
    ctx.dest_addr[0].address = 42;
    ctx.dest_addr[0].port = 123;
    ctx.pending_fin = 1;
    ctx.fin_target_seq = 7;
    ctx.peer_session_known = 1;
    ctx.reorder_pending = 1;
    ctx.remote_path_ping_state[0] = 1;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic(_))
        .WillByDefault(
            [](cplat_timespec *out)
            {
                out->tv_sec = 11;
                out->tv_nsec = 0;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<2>(int64_t{100}), Return(CPLAT_OK)));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - 受信済みセッションと監視期限を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_clock_get_monotonic(_))
        .Times(0); // [Pre-Assert確認_正常系] - 監視有効時だけ時刻を取得すること。
    EXPECT_CALL(mock_porter, thread_recv_sync_path_state(&slot))
        .Times(0); // [Pre-Assert確認_正常系] - 期限到達時だけパス状態を同期すること。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_service_all_paths(&ctx))
        .Times(0); // [Pre-Assert確認_正常系] - サービス期限到達時だけ切断すること。
    EXPECT_CALL(mock_porter, potr_internal_window_init(&ctx.recv_window, 0, _, _))
        .Times(0); // [Pre-Assert確認_正常系] - 切断後に受信ウィンドウを初期化すること。
    EXPECT_CALL(mock_porter, thread_recv_wake_udp_interrupt_ping(&ctx, 0))
        .Times(0); // [Pre-Assert確認_正常系] - 有効なサービスの監視末尾で必要な起床を行うこと。
    // Act
    thread_recv_health_check_service(&slot); // [手順] - 1:1 の受信期限を確認する。
    // Assert
    EXPECT_EQ(123, ctx.peer_port[0]);            // [確認_正常系] - パス期限に従って送信元ポートを解除すること。
    EXPECT_EQ(42u, ctx.dest_addr[0].address);    // [確認_正常系] - 動的学習したアドレスを期限到達時に解除すること。
    EXPECT_EQ(123, ctx.dest_addr[0].port);       // [確認_正常系] - 動的学習した宛先ポートを期限到達時に解除すること。
    EXPECT_EQ(1, ctx.pending_fin);               // [確認_正常系] - サービス切断時だけ FIN を解除すること。
    EXPECT_EQ(7u, ctx.fin_target_seq);           // [確認_正常系] - サービス切断時だけ FIN 対象番号を初期化すること。
    EXPECT_EQ(1, ctx.peer_session_known);        // [確認_正常系] - サービス切断時だけセッションを解除すること。
    EXPECT_EQ(1, ctx.reorder_pending);           // [確認_正常系] - サービス切断時だけリオーダー状態を解除すること。
    EXPECT_EQ(1, ctx.remote_path_ping_state[0]); // [確認_正常系] - サービス切断時だけ相手の受信状態を消去すること。
}

TEST(threadRecvHealthTest, service_before)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    thread_recv_slot slot = {};
    slot.ctx = &ctx;
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.n_path = 1;
    ctx.health_timeout_ms = 100;
    ctx.path_last_recv_ts[0].tv_sec = 10;
    ctx.last_recv_ts.tv_sec = 10;
    ctx.peer_port[0] = 123;
    ctx.dest_addr[0].address = 42;
    ctx.dest_addr[0].port = 123;
    ctx.pending_fin = 1;
    ctx.fin_target_seq = 7;
    ctx.peer_session_known = 1;
    ctx.reorder_pending = 1;
    ctx.remote_path_ping_state[0] = 1;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic(_))
        .WillByDefault(
            [](cplat_timespec *out)
            {
                out->tv_sec = 11;
                out->tv_nsec = 0;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<2>(int64_t{99}), Return(CPLAT_OK)));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - 受信済みセッションと監視期限を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_clock_get_monotonic(_))
        .Times(1); // [Pre-Assert確認_正常系] - 監視有効時だけ時刻を取得すること。
    EXPECT_CALL(mock_porter, thread_recv_sync_path_state(&slot))
        .Times(0); // [Pre-Assert確認_正常系] - 期限到達時だけパス状態を同期すること。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_service_all_paths(&ctx))
        .Times(0); // [Pre-Assert確認_正常系] - サービス期限到達時だけ切断すること。
    EXPECT_CALL(mock_porter, potr_internal_window_init(&ctx.recv_window, 0, _, _))
        .Times(0); // [Pre-Assert確認_正常系] - 切断後に受信ウィンドウを初期化すること。
    EXPECT_CALL(mock_porter, thread_recv_wake_udp_interrupt_ping(&ctx, 0))
        .Times(1); // [Pre-Assert確認_正常系] - 有効なサービスの監視末尾で必要な起床を行うこと。
    // Act
    thread_recv_health_check_service(&slot); // [手順] - 1:1 の受信期限を確認する。
    // Assert
    EXPECT_EQ(123, ctx.peer_port[0]);            // [確認_正常系] - パス期限に従って送信元ポートを解除すること。
    EXPECT_EQ(42u, ctx.dest_addr[0].address);    // [確認_正常系] - 動的学習したアドレスを期限到達時に解除すること。
    EXPECT_EQ(123, ctx.dest_addr[0].port);       // [確認_正常系] - 動的学習した宛先ポートを期限到達時に解除すること。
    EXPECT_EQ(1, ctx.pending_fin);               // [確認_正常系] - サービス切断時だけ FIN を解除すること。
    EXPECT_EQ(7u, ctx.fin_target_seq);           // [確認_正常系] - サービス切断時だけ FIN 対象番号を初期化すること。
    EXPECT_EQ(1, ctx.peer_session_known);        // [確認_正常系] - サービス切断時だけセッションを解除すること。
    EXPECT_EQ(1, ctx.reorder_pending);           // [確認_正常系] - サービス切断時だけリオーダー状態を解除すること。
    EXPECT_EQ(1, ctx.remote_path_ping_state[0]); // [確認_正常系] - サービス切断時だけ相手の受信状態を消去すること。
}

TEST(threadRecvHealthTest, service_at_deadline)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    thread_recv_slot slot = {};
    slot.ctx = &ctx;
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.n_path = 1;
    ctx.health_timeout_ms = 100;
    ctx.path_last_recv_ts[0].tv_sec = 10;
    ctx.last_recv_ts.tv_sec = 10;
    ctx.peer_port[0] = 123;
    ctx.dest_addr[0].address = 42;
    ctx.dest_addr[0].port = 123;
    ctx.pending_fin = 1;
    ctx.fin_target_seq = 7;
    ctx.peer_session_known = 1;
    ctx.reorder_pending = 1;
    ctx.remote_path_ping_state[0] = 1;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic(_))
        .WillByDefault(
            [](cplat_timespec *out)
            {
                out->tv_sec = 11;
                out->tv_nsec = 0;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<2>(int64_t{100}), Return(CPLAT_OK)));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - 受信済みセッションと監視期限を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_clock_get_monotonic(_))
        .Times(1); // [Pre-Assert確認_正常系] - 監視有効時だけ時刻を取得すること。
    EXPECT_CALL(mock_porter, thread_recv_sync_path_state(&slot))
        .Times(1); // [Pre-Assert確認_正常系] - 期限到達時だけパス状態を同期すること。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_service_all_paths(&ctx))
        .Times(1); // [Pre-Assert確認_正常系] - サービス期限到達時だけ切断すること。
    EXPECT_CALL(mock_porter, potr_internal_window_init(&ctx.recv_window, 0, _, _))
        .Times(1); // [Pre-Assert確認_正常系] - 切断後に受信ウィンドウを初期化すること。
    EXPECT_CALL(mock_porter, thread_recv_wake_udp_interrupt_ping(&ctx, 1))
        .Times(1); // [Pre-Assert確認_正常系] - 有効なサービスの監視末尾で必要な起床を行うこと。
    // Act
    thread_recv_health_check_service(&slot); // [手順] - 1:1 の受信期限を確認する。
    // Assert
    EXPECT_EQ(0, ctx.peer_port[0]);              // [確認_正常系] - パス期限に従って送信元ポートを解除すること。
    EXPECT_EQ(0u, ctx.dest_addr[0].address);     // [確認_正常系] - 動的学習したアドレスを期限到達時に解除すること。
    EXPECT_EQ(0, ctx.dest_addr[0].port);         // [確認_正常系] - 動的学習した宛先ポートを期限到達時に解除すること。
    EXPECT_EQ(0, ctx.pending_fin);               // [確認_正常系] - サービス切断時だけ FIN を解除すること。
    EXPECT_EQ(0u, ctx.fin_target_seq);           // [確認_正常系] - サービス切断時だけ FIN 対象番号を初期化すること。
    EXPECT_EQ(0, ctx.peer_session_known);        // [確認_正常系] - サービス切断時だけセッションを解除すること。
    EXPECT_EQ(0, ctx.reorder_pending);           // [確認_正常系] - サービス切断時だけリオーダー状態を解除すること。
    EXPECT_EQ(0, ctx.remote_path_ping_state[0]); // [確認_正常系] - サービス切断時だけ相手の受信状態を消去すること。
}

TEST(threadRecvHealthTest, service_after_deadline)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    thread_recv_slot slot = {};
    slot.ctx = &ctx;
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.n_path = 1;
    ctx.health_timeout_ms = 100;
    ctx.path_last_recv_ts[0].tv_sec = 10;
    ctx.last_recv_ts.tv_sec = 10;
    ctx.peer_port[0] = 123;
    ctx.dest_addr[0].address = 42;
    ctx.dest_addr[0].port = 123;
    ctx.pending_fin = 1;
    ctx.fin_target_seq = 7;
    ctx.peer_session_known = 1;
    ctx.reorder_pending = 1;
    ctx.remote_path_ping_state[0] = 1;
    cplat_atomic_store_i32(&ctx.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic(_))
        .WillByDefault(
            [](cplat_timespec *out)
            {
                out->tv_sec = 11;
                out->tv_nsec = 0;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<2>(int64_t{101}), Return(CPLAT_OK)));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - 受信済みセッションと監視期限を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_clock_get_monotonic(_))
        .Times(1); // [Pre-Assert確認_正常系] - 監視有効時だけ時刻を取得すること。
    EXPECT_CALL(mock_porter, thread_recv_sync_path_state(&slot))
        .Times(1); // [Pre-Assert確認_正常系] - 期限到達時だけパス状態を同期すること。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_service_all_paths(&ctx))
        .Times(1); // [Pre-Assert確認_正常系] - サービス期限到達時だけ切断すること。
    EXPECT_CALL(mock_porter, potr_internal_window_init(&ctx.recv_window, 0, _, _))
        .Times(1); // [Pre-Assert確認_正常系] - 切断後に受信ウィンドウを初期化すること。
    EXPECT_CALL(mock_porter, thread_recv_wake_udp_interrupt_ping(&ctx, 1))
        .Times(1); // [Pre-Assert確認_正常系] - 有効なサービスの監視末尾で必要な起床を行うこと。
    // Act
    thread_recv_health_check_service(&slot); // [手順] - 1:1 の受信期限を確認する。
    // Assert
    EXPECT_EQ(0, ctx.peer_port[0]);              // [確認_正常系] - パス期限に従って送信元ポートを解除すること。
    EXPECT_EQ(0u, ctx.dest_addr[0].address);     // [確認_正常系] - 動的学習したアドレスを期限到達時に解除すること。
    EXPECT_EQ(0, ctx.dest_addr[0].port);         // [確認_正常系] - 動的学習した宛先ポートを期限到達時に解除すること。
    EXPECT_EQ(0, ctx.pending_fin);               // [確認_正常系] - サービス切断時だけ FIN を解除すること。
    EXPECT_EQ(0u, ctx.fin_target_seq);           // [確認_正常系] - サービス切断時だけ FIN 対象番号を初期化すること。
    EXPECT_EQ(0, ctx.peer_session_known);        // [確認_正常系] - サービス切断時だけセッションを解除すること。
    EXPECT_EQ(0, ctx.reorder_pending);           // [確認_正常系] - サービス切断時だけリオーダー状態を解除すること。
    EXPECT_EQ(0, ctx.remote_path_ping_state[0]); // [確認_正常系] - サービス切断時だけ相手の受信状態を消去すること。
}

TEST(threadRecvHealthTest, service_inactive_service)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    thread_recv_slot slot = {};
    slot.ctx = &ctx;
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.n_path = 1;
    ctx.health_timeout_ms = 100;
    ctx.path_last_recv_ts[0].tv_sec = 10;
    ctx.last_recv_ts.tv_sec = 10;
    ctx.peer_port[0] = 123;
    ctx.dest_addr[0].address = 42;
    ctx.dest_addr[0].port = 123;
    ctx.pending_fin = 1;
    ctx.fin_target_seq = 7;
    ctx.peer_session_known = 1;
    ctx.reorder_pending = 1;
    ctx.remote_path_ping_state[0] = 1;
    cplat_atomic_store_i32(&ctx.health_alive, 0, CPLAT_MEMORY_ORDER_RELEASE);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic(_))
        .WillByDefault(
            [](cplat_timespec *out)
            {
                out->tv_sec = 11;
                out->tv_nsec = 0;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<2>(int64_t{100}), Return(CPLAT_OK)));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - 受信済みセッションと監視期限を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_clock_get_monotonic(_))
        .Times(1); // [Pre-Assert確認_正常系] - 監視有効時だけ時刻を取得すること。
    EXPECT_CALL(mock_porter, thread_recv_sync_path_state(&slot))
        .Times(1); // [Pre-Assert確認_正常系] - 期限到達時だけパス状態を同期すること。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_service_all_paths(&ctx))
        .Times(0); // [Pre-Assert確認_正常系] - サービス期限到達時だけ切断すること。
    EXPECT_CALL(mock_porter, potr_internal_window_init(&ctx.recv_window, 0, _, _))
        .Times(0); // [Pre-Assert確認_正常系] - 切断後に受信ウィンドウを初期化すること。
    EXPECT_CALL(mock_porter, thread_recv_wake_udp_interrupt_ping(&ctx, 1))
        .Times(0); // [Pre-Assert確認_正常系] - 有効なサービスの監視末尾で必要な起床を行うこと。
    // Act
    thread_recv_health_check_service(&slot); // [手順] - 1:1 の受信期限を確認する。
    // Assert
    EXPECT_EQ(0, ctx.peer_port[0]);              // [確認_正常系] - パス期限に従って送信元ポートを解除すること。
    EXPECT_EQ(0u, ctx.dest_addr[0].address);     // [確認_正常系] - 動的学習したアドレスを期限到達時に解除すること。
    EXPECT_EQ(0, ctx.dest_addr[0].port);         // [確認_正常系] - 動的学習した宛先ポートを期限到達時に解除すること。
    EXPECT_EQ(1, ctx.pending_fin);               // [確認_正常系] - サービス切断時だけ FIN を解除すること。
    EXPECT_EQ(7u, ctx.fin_target_seq);           // [確認_正常系] - サービス切断時だけ FIN 対象番号を初期化すること。
    EXPECT_EQ(1, ctx.peer_session_known);        // [確認_正常系] - サービス切断時だけセッションを解除すること。
    EXPECT_EQ(1, ctx.reorder_pending);           // [確認_正常系] - サービス切断時だけリオーダー状態を解除すること。
    EXPECT_EQ(1, ctx.remote_path_ping_state[0]); // [確認_正常系] - サービス切断時だけ相手の受信状態を消去すること。
}

TEST(threadRecvHealthTest, peers_disabled)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context peer = {};
    ctx.peers = &peer;
    ctx.max_peers = 1;
    ctx.health_timeout_ms = 0;
    peer.active = 1;
    peer.dest_addr[0].address = 42;
    peer.dest_addr[0].port = 123;
    peer.path_last_recv_ts[0].tv_sec = 10;
    peer.last_recv_ts.tv_sec = 10;
    peer.remote_path_ping_state[0] = 1;
    cplat_atomic_store_i32(&peer.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic(_))
        .WillByDefault(
            [](cplat_timespec *out)
            {
                out->tv_sec = 11;
                out->tv_nsec = 0;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<2>(int64_t{100}), Return(CPLAT_OK)));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - ピアとパスの受信時刻、監視状態を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.peers_mutex))
        .Times(0); // [Pre-Assert確認_正常系] - 有効時だけピアのロックを取得すること。
    EXPECT_CALL(mock_porter, potr_internal_peer_path_clear(&ctx, &peer, 0))
        .Times(0); // [Pre-Assert確認_正常系] - 期限に達したパスだけを解除すること。
    EXPECT_CALL(mock_porter, thread_recv_sync_path_state(_))
        .Times(0); // [Pre-Assert確認_正常系] - パス解除後に状態を同期すること。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_peer_all_paths(&ctx, &peer))
        .Times(0); // [Pre-Assert確認_正常系] - ピア期限到達時だけ切断すること。
    EXPECT_CALL(mock_porter, potr_internal_peer_free(&ctx, &peer))
        .Times(0); // [Pre-Assert確認_正常系] - 切断したピアを解放すること。
    EXPECT_CALL(mock_cplat, cplat_local_lock_unlock(ctx.peers_mutex))
        .Times(0); // [Pre-Assert確認_正常系] - 起床前にロックを解放すること。
    EXPECT_CALL(mock_porter, potr_internal_health_thread_wake(&ctx))
        .Times(0); // [Pre-Assert確認_正常系] - ロック解放後に変化を通知すること。
    // Act
    thread_recv_health_check_peers(&ctx); // [手順] - N:1 の受信期限を確認する。
    // Assert
    EXPECT_EQ(1, peer.remote_path_ping_state[0]); // [確認_正常系] - ピア切断時だけ相手の受信状態を消去すること。
}

TEST(threadRecvHealthTest, peers_before)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context peer = {};
    ctx.peers = &peer;
    ctx.max_peers = 1;
    ctx.health_timeout_ms = 100;
    peer.active = 1;
    peer.dest_addr[0].address = 42;
    peer.dest_addr[0].port = 123;
    peer.path_last_recv_ts[0].tv_sec = 10;
    peer.last_recv_ts.tv_sec = 10;
    peer.remote_path_ping_state[0] = 1;
    cplat_atomic_store_i32(&peer.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic(_))
        .WillByDefault(
            [](cplat_timespec *out)
            {
                out->tv_sec = 11;
                out->tv_nsec = 0;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<2>(int64_t{99}), Return(CPLAT_OK)));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - ピアとパスの受信時刻、監視状態を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.peers_mutex))
        .Times(1)
        .WillRepeatedly(Return()); // [Pre-Assert確認_正常系] - 有効時だけピアのロックを取得すること。
    // [Pre-Assert手順] - ロック取得成功を返す。
    EXPECT_CALL(mock_porter, potr_internal_peer_path_clear(&ctx, &peer, 0))
        .Times(0); // [Pre-Assert確認_正常系] - 期限に達したパスだけを解除すること。
    EXPECT_CALL(mock_porter, thread_recv_sync_path_state(_))
        .Times(0); // [Pre-Assert確認_正常系] - パス解除後に状態を同期すること。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_peer_all_paths(&ctx, &peer))
        .Times(0); // [Pre-Assert確認_正常系] - ピア期限到達時だけ切断すること。
    EXPECT_CALL(mock_porter, potr_internal_peer_free(&ctx, &peer))
        .Times(0); // [Pre-Assert確認_正常系] - 切断したピアを解放すること。
    EXPECT_CALL(mock_cplat, cplat_local_lock_unlock(ctx.peers_mutex))
        .Times(1)
        .WillRepeatedly(Return()); // [Pre-Assert確認_正常系] - 起床前にロックを解放すること。
    // [Pre-Assert手順] - ロック解放成功を返す。
    EXPECT_CALL(mock_porter, potr_internal_health_thread_wake(&ctx))
        .Times(0); // [Pre-Assert確認_正常系] - ロック解放後に変化を通知すること。
    // Act
    thread_recv_health_check_peers(&ctx); // [手順] - N:1 の受信期限を確認する。
    // Assert
    EXPECT_EQ(1, peer.remote_path_ping_state[0]); // [確認_正常系] - ピア切断時だけ相手の受信状態を消去すること。
}

TEST(threadRecvHealthTest, peers_at_deadline)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context peer = {};
    ctx.peers = &peer;
    ctx.max_peers = 1;
    ctx.health_timeout_ms = 100;
    peer.active = 1;
    peer.dest_addr[0].address = 42;
    peer.dest_addr[0].port = 123;
    peer.path_last_recv_ts[0].tv_sec = 10;
    peer.last_recv_ts.tv_sec = 10;
    peer.remote_path_ping_state[0] = 1;
    cplat_atomic_store_i32(&peer.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic(_))
        .WillByDefault(
            [](cplat_timespec *out)
            {
                out->tv_sec = 11;
                out->tv_nsec = 0;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<2>(int64_t{100}), Return(CPLAT_OK)));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - ピアとパスの受信時刻、監視状態を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.peers_mutex))
        .Times(1)
        .WillRepeatedly(Return()); // [Pre-Assert確認_正常系] - 有効時だけピアのロックを取得すること。
    // [Pre-Assert手順] - ロック取得成功を返す。
    EXPECT_CALL(mock_porter, potr_internal_peer_path_clear(&ctx, &peer, 0))
        .Times(1); // [Pre-Assert確認_正常系] - 期限に達したパスだけを解除すること。
    EXPECT_CALL(mock_porter, thread_recv_sync_path_state(_))
        .Times(1); // [Pre-Assert確認_正常系] - パス解除後に状態を同期すること。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_peer_all_paths(&ctx, &peer))
        .Times(1); // [Pre-Assert確認_正常系] - ピア期限到達時だけ切断すること。
    EXPECT_CALL(mock_porter, potr_internal_peer_free(&ctx, &peer))
        .Times(1); // [Pre-Assert確認_正常系] - 切断したピアを解放すること。
    EXPECT_CALL(mock_cplat, cplat_local_lock_unlock(ctx.peers_mutex))
        .Times(1)
        .WillRepeatedly(Return()); // [Pre-Assert確認_正常系] - 起床前にロックを解放すること。
    // [Pre-Assert手順] - ロック解放成功を返す。
    EXPECT_CALL(mock_porter, potr_internal_health_thread_wake(&ctx))
        .Times(1); // [Pre-Assert確認_正常系] - ロック解放後に変化を通知すること。
    // Act
    thread_recv_health_check_peers(&ctx); // [手順] - N:1 の受信期限を確認する。
    // Assert
    EXPECT_EQ(0, peer.remote_path_ping_state[0]); // [確認_正常系] - ピア切断時だけ相手の受信状態を消去すること。
}

TEST(threadRecvHealthTest, peers_after_deadline)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context peer = {};
    ctx.peers = &peer;
    ctx.max_peers = 1;
    ctx.health_timeout_ms = 100;
    peer.active = 1;
    peer.dest_addr[0].address = 42;
    peer.dest_addr[0].port = 123;
    peer.path_last_recv_ts[0].tv_sec = 10;
    peer.last_recv_ts.tv_sec = 10;
    peer.remote_path_ping_state[0] = 1;
    cplat_atomic_store_i32(&peer.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic(_))
        .WillByDefault(
            [](cplat_timespec *out)
            {
                out->tv_sec = 11;
                out->tv_nsec = 0;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<2>(int64_t{101}), Return(CPLAT_OK)));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - ピアとパスの受信時刻、監視状態を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.peers_mutex))
        .Times(1)
        .WillRepeatedly(Return()); // [Pre-Assert確認_正常系] - 有効時だけピアのロックを取得すること。
    // [Pre-Assert手順] - ロック取得成功を返す。
    EXPECT_CALL(mock_porter, potr_internal_peer_path_clear(&ctx, &peer, 0))
        .Times(1); // [Pre-Assert確認_正常系] - 期限に達したパスだけを解除すること。
    EXPECT_CALL(mock_porter, thread_recv_sync_path_state(_))
        .Times(1); // [Pre-Assert確認_正常系] - パス解除後に状態を同期すること。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_peer_all_paths(&ctx, &peer))
        .Times(1); // [Pre-Assert確認_正常系] - ピア期限到達時だけ切断すること。
    EXPECT_CALL(mock_porter, potr_internal_peer_free(&ctx, &peer))
        .Times(1); // [Pre-Assert確認_正常系] - 切断したピアを解放すること。
    EXPECT_CALL(mock_cplat, cplat_local_lock_unlock(ctx.peers_mutex))
        .Times(1)
        .WillRepeatedly(Return()); // [Pre-Assert確認_正常系] - 起床前にロックを解放すること。
    // [Pre-Assert手順] - ロック解放成功を返す。
    EXPECT_CALL(mock_porter, potr_internal_health_thread_wake(&ctx))
        .Times(1); // [Pre-Assert確認_正常系] - ロック解放後に変化を通知すること。
    // Act
    thread_recv_health_check_peers(&ctx); // [手順] - N:1 の受信期限を確認する。
    // Assert
    EXPECT_EQ(0, peer.remote_path_ping_state[0]); // [確認_正常系] - ピア切断時だけ相手の受信状態を消去すること。
}

TEST(threadRecvHealthTest, peers_inactive_peer)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context peer = {};
    ctx.peers = &peer;
    ctx.max_peers = 1;
    ctx.health_timeout_ms = 100;
    peer.active = 0;
    peer.dest_addr[0].address = 42;
    peer.dest_addr[0].port = 123;
    peer.path_last_recv_ts[0].tv_sec = 10;
    peer.last_recv_ts.tv_sec = 10;
    peer.remote_path_ping_state[0] = 1;
    cplat_atomic_store_i32(&peer.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic(_))
        .WillByDefault(
            [](cplat_timespec *out)
            {
                out->tv_sec = 11;
                out->tv_nsec = 0;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<2>(int64_t{100}), Return(CPLAT_OK)));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - ピアとパスの受信時刻、監視状態を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.peers_mutex))
        .Times(1)
        .WillRepeatedly(Return()); // [Pre-Assert確認_正常系] - 有効時だけピアのロックを取得すること。
    // [Pre-Assert手順] - ロック取得成功を返す。
    EXPECT_CALL(mock_porter, potr_internal_peer_path_clear(&ctx, &peer, 0))
        .Times(0); // [Pre-Assert確認_正常系] - 期限に達したパスだけを解除すること。
    EXPECT_CALL(mock_porter, thread_recv_sync_path_state(_))
        .Times(0); // [Pre-Assert確認_正常系] - パス解除後に状態を同期すること。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_peer_all_paths(&ctx, &peer))
        .Times(0); // [Pre-Assert確認_正常系] - ピア期限到達時だけ切断すること。
    EXPECT_CALL(mock_porter, potr_internal_peer_free(&ctx, &peer))
        .Times(0); // [Pre-Assert確認_正常系] - 切断したピアを解放すること。
    EXPECT_CALL(mock_cplat, cplat_local_lock_unlock(ctx.peers_mutex))
        .Times(1)
        .WillRepeatedly(Return()); // [Pre-Assert確認_正常系] - 起床前にロックを解放すること。
    // [Pre-Assert手順] - ロック解放成功を返す。
    EXPECT_CALL(mock_porter, potr_internal_health_thread_wake(&ctx))
        .Times(0); // [Pre-Assert確認_正常系] - ロック解放後に変化を通知すること。
    // Act
    thread_recv_health_check_peers(&ctx); // [手順] - N:1 の受信期限を確認する。
    // Assert
    EXPECT_EQ(1, peer.remote_path_ping_state[0]); // [確認_正常系] - ピア切断時だけ相手の受信状態を消去すること。
}

TEST(threadRecvHealthTest, peers_dead_peer)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context peer = {};
    ctx.peers = &peer;
    ctx.max_peers = 1;
    ctx.health_timeout_ms = 100;
    peer.active = 1;
    peer.dest_addr[0].address = 42;
    peer.dest_addr[0].port = 123;
    peer.path_last_recv_ts[0].tv_sec = 10;
    peer.last_recv_ts.tv_sec = 10;
    peer.remote_path_ping_state[0] = 1;
    cplat_atomic_store_i32(&peer.health_alive, 0, CPLAT_MEMORY_ORDER_RELEASE);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic(_))
        .WillByDefault(
            [](cplat_timespec *out)
            {
                out->tv_sec = 11;
                out->tv_nsec = 0;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<2>(int64_t{100}), Return(CPLAT_OK)));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - ピアとパスの受信時刻、監視状態を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.peers_mutex))
        .Times(1)
        .WillRepeatedly(Return()); // [Pre-Assert確認_正常系] - 有効時だけピアのロックを取得すること。
    // [Pre-Assert手順] - ロック取得成功を返す。
    EXPECT_CALL(mock_porter, potr_internal_peer_path_clear(&ctx, &peer, 0))
        .Times(0); // [Pre-Assert確認_正常系] - 期限に達したパスだけを解除すること。
    EXPECT_CALL(mock_porter, thread_recv_sync_path_state(_))
        .Times(0); // [Pre-Assert確認_正常系] - パス解除後に状態を同期すること。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_peer_all_paths(&ctx, &peer))
        .Times(0); // [Pre-Assert確認_正常系] - ピア期限到達時だけ切断すること。
    EXPECT_CALL(mock_porter, potr_internal_peer_free(&ctx, &peer))
        .Times(0); // [Pre-Assert確認_正常系] - 切断したピアを解放すること。
    EXPECT_CALL(mock_cplat, cplat_local_lock_unlock(ctx.peers_mutex))
        .Times(1)
        .WillRepeatedly(Return()); // [Pre-Assert確認_正常系] - 起床前にロックを解放すること。
    // [Pre-Assert手順] - ロック解放成功を返す。
    EXPECT_CALL(mock_porter, potr_internal_health_thread_wake(&ctx))
        .Times(0); // [Pre-Assert確認_正常系] - ロック解放後に変化を通知すること。
    // Act
    thread_recv_health_check_peers(&ctx); // [手順] - N:1 の受信期限を確認する。
    // Assert
    EXPECT_EQ(1, peer.remote_path_ping_state[0]); // [確認_正常系] - ピア切断時だけ相手の受信状態を消去すること。
}

TEST(threadRecvHealthTest, peers_path_only)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context peer = {};
    ctx.peers = &peer;
    ctx.max_peers = 1;
    ctx.health_timeout_ms = 100;
    peer.active = 1;
    peer.dest_addr[0].address = 42;
    peer.dest_addr[0].port = 123;
    peer.path_last_recv_ts[0].tv_sec = 10;
    peer.last_recv_ts.tv_sec = 0;
    peer.remote_path_ping_state[0] = 1;
    cplat_atomic_store_i32(&peer.health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic(_))
        .WillByDefault(
            [](cplat_timespec *out)
            {
                out->tv_sec = 11;
                out->tv_nsec = 0;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<2>(int64_t{100}), Return(CPLAT_OK)));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - ピアとパスの受信時刻、監視状態を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.peers_mutex))
        .Times(1)
        .WillRepeatedly(Return()); // [Pre-Assert確認_正常系] - 有効時だけピアのロックを取得すること。
    // [Pre-Assert手順] - ロック取得成功を返す。
    EXPECT_CALL(mock_porter, potr_internal_peer_path_clear(&ctx, &peer, 0))
        .Times(1); // [Pre-Assert確認_正常系] - 期限に達したパスだけを解除すること。
    EXPECT_CALL(mock_porter, thread_recv_sync_path_state(_))
        .Times(1); // [Pre-Assert確認_正常系] - パス解除後に状態を同期すること。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_peer_all_paths(&ctx, &peer))
        .Times(0); // [Pre-Assert確認_正常系] - ピア期限到達時だけ切断すること。
    EXPECT_CALL(mock_porter, potr_internal_peer_free(&ctx, &peer))
        .Times(0); // [Pre-Assert確認_正常系] - 切断したピアを解放すること。
    EXPECT_CALL(mock_cplat, cplat_local_lock_unlock(ctx.peers_mutex))
        .Times(1)
        .WillRepeatedly(Return()); // [Pre-Assert確認_正常系] - 起床前にロックを解放すること。
    // [Pre-Assert手順] - ロック解放成功を返す。
    EXPECT_CALL(mock_porter, potr_internal_health_thread_wake(&ctx))
        .Times(1); // [Pre-Assert確認_正常系] - ロック解放後に変化を通知すること。
    // Act
    thread_recv_health_check_peers(&ctx); // [手順] - N:1 の受信期限を確認する。
    // Assert
    EXPECT_EQ(1, peer.remote_path_ping_state[0]); // [確認_正常系] - ピア切断時だけ相手の受信状態を消去すること。
}

TEST(threadRecvHealthTest, tcp_udp)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.health_interval_ms = 100;
    cplat_atomic_store_i32(&ctx.health_running[0], 1, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - 通信方式と health の稼働状態を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_tcp_health_thread_wake_all(&ctx))
        .Times(0); // [Pre-Assert確認_正常系] - 定周期 health 稼働時だけ全パスを起床すること。
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_ping_now(&ctx, 0))
        .Times(0); // [Pre-Assert確認_正常系] - TCP の状態変化時に health を起床できなければ即時 PING を送ること。
    // Act
    thread_recv_health_wake_tcp(&ctx, 0, 1); // [手順] - 状態変化の起床判断を行う。
}

TEST(threadRecvHealthTest, tcp_unchanged)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.health_interval_ms = 100;
    cplat_atomic_store_i32(&ctx.health_running[0], 1, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - 通信方式と health の稼働状態を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_tcp_health_thread_wake_all(&ctx))
        .Times(0); // [Pre-Assert確認_正常系] - 定周期 health 稼働時だけ全パスを起床すること。
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_ping_now(&ctx, 0))
        .Times(0); // [Pre-Assert確認_正常系] - TCP の状態変化時に health を起床できなければ即時 PING を送ること。
    // Act
    thread_recv_health_wake_tcp(&ctx, 0, 0); // [手順] - 状態変化の起床判断を行う。
}

TEST(threadRecvHealthTest, tcp_wake_running)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.health_interval_ms = 100;
    cplat_atomic_store_i32(&ctx.health_running[0], 1, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - 通信方式と health の稼働状態を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_tcp_health_thread_wake_all(&ctx))
        .Times(1); // [Pre-Assert確認_正常系] - 定周期 health 稼働時だけ全パスを起床すること。
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_ping_now(&ctx, 0))
        .Times(0); // [Pre-Assert確認_正常系] - TCP の状態変化時に health を起床できなければ即時 PING を送ること。
    // Act
    thread_recv_health_wake_tcp(&ctx, 0, 1); // [手順] - 状態変化の起床判断を行う。
}

TEST(threadRecvHealthTest, tcp_no_interval)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.health_interval_ms = 0;
    cplat_atomic_store_i32(&ctx.health_running[0], 1, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - 通信方式と health の稼働状態を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_tcp_health_thread_wake_all(&ctx))
        .Times(0); // [Pre-Assert確認_正常系] - 定周期 health 稼働時だけ全パスを起床すること。
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_ping_now(&ctx, 0))
        .Times(1); // [Pre-Assert確認_正常系] - TCP の状態変化時に health を起床できなければ即時 PING を送ること。
    // Act
    thread_recv_health_wake_tcp(&ctx, 0, 1); // [手順] - 状態変化の起床判断を行う。
}

TEST(threadRecvHealthTest, tcp_stopped_health)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.health_interval_ms = 100;
    cplat_atomic_store_i32(&ctx.health_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - 通信方式と health の稼働状態を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_tcp_health_thread_wake_all(&ctx))
        .Times(0); // [Pre-Assert確認_正常系] - 定周期 health 稼働時だけ全パスを起床すること。
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_ping_now(&ctx, 0))
        .Times(1); // [Pre-Assert確認_正常系] - TCP の状態変化時に health を起床できなければ即時 PING を送ること。
    // Act
    thread_recv_health_wake_tcp(&ctx, 0, 1); // [手順] - 状態変化の起床判断を行う。
}

// 即時の PING の送信に失敗した場合に WARNING を出すことの確認
TEST(threadRecvHealthTest, tcp_immediate_ping_failure_traces_warning)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.service.service_id = 11;
    ctx.health_interval_ms = 0;
    // [状態] - 定周期の health を使わない TCP の通信方式を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_ping_now(&ctx, 0))
        .WillOnce(Return(POTR_ERR_IO)); // [Pre-Assert確認_異常系] - potr_internal_tcp_send_ping_now() が &ctx、0 を指定して 1 回呼び出されること。
                                        // [Pre-Assert手順] - potr_internal_tcp_send_ping_now() から POTR_ERR_IO を返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _))
        .Times(AnyNumber()); // [Pre-Assert確認_正常系] - cplat_tracer_writef_at() が任意の回数呼び出されること。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_WARNING, nullptr,
                                                   HasSubstr("immediate PING send failed (result=")))
        .Times(1); // [Pre-Assert確認_異常系] - cplat_tracer_writef_at() が CPLAT_TRACE_LEVEL_WARNING、nullptr、"immediate PING send failed (result=" を含む文字列を指定して 1 回呼び出されること。
    // Act
    thread_recv_health_wake_tcp(&ctx, 0, 1); // [手順] - 状態変化の起床判断を行う。
}

// 未接続 (POTR_ERR_DISCONNECTED) の即時の PING では WARNING を出さないことの確認
TEST(threadRecvHealthTest, tcp_immediate_ping_disconnected_does_not_trace_warning)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.service.service_id = 11;
    ctx.health_interval_ms = 0;
    // [状態] - 定周期の health を使わない TCP の通信方式を指定する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_ping_now(&ctx, 0))
        .WillOnce(Return(POTR_ERR_DISCONNECTED)); // [Pre-Assert確認_正常系] - potr_internal_tcp_send_ping_now() が &ctx、0 を指定して 1 回呼び出されること。
                                                  // [Pre-Assert手順] - potr_internal_tcp_send_ping_now() から POTR_ERR_DISCONNECTED を返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _))
        .Times(AnyNumber()); // [Pre-Assert確認_正常系] - cplat_tracer_writef_at() が任意の回数呼び出されること。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_WARNING, _, _))
        .Times(0); // [Pre-Assert確認_正常系] - CPLAT_TRACE_LEVEL_WARNING を指定した cplat_tracer_writef_at() の呼び出しがないこと。
    // Act
    thread_recv_health_wake_tcp(&ctx, 0, 1); // [手順] - 状態変化の起床判断を行う。
}

// 時刻差分を計算できない場合に受信状態を保持することの確認
TEST(threadRecvHealthTest, preserves_service_state_when_time_arithmetic_fails)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    thread_recv_slot slot = {};
    slot.ctx = &ctx;
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.n_path = 1;
    ctx.health_timeout_ms = 100;
    ctx.path_last_recv_ts[0].tv_sec = 10;
    ctx.last_recv_ts.tv_sec = 10;
    ctx.peer_port[0] = 123;
    ctx.pending_fin = 1; // [状態] - 接続中のパスを用意する。
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _, _)).WillByDefault(Return(CPLAT_ERR_OUT_OF_RANGE));

    // Pre-Assert
    EXPECT_CALL(mock_porter, thread_recv_disconnect_service_all_paths(_))
        .Times(0); // [Pre-Assert確認_異常系] - 計算失敗だけでは切断しないこと。

    // Act
    thread_recv_health_check_service(&slot); // [手順] - 時刻計算失敗中の監視を実行する。

    // Assert
    EXPECT_EQ(123, ctx.peer_port[0]); // [確認_異常系] - 接続情報を保持すること。
    EXPECT_EQ(1, ctx.pending_fin);
}

// 時計取得失敗時に接続状態を更新せず致命的エラーを通知することの確認
TEST(threadRecvHealthTest, clock_failure_preserves_path_state)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    thread_recv_slot slot = {};
    slot.ctx = &ctx;
    ctx.service.type = POTR_TYPE_UNICAST_BIDIR;
    ctx.n_path = 1;
    ctx.health_timeout_ms = 100;
    ctx.path_last_recv_ts[0].tv_sec = 10;
    ctx.last_recv_ts.tv_sec = 10;
    ctx.peer_port[0] = 123;

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_clock_get_monotonic(_)).WillOnce(Return(CPLAT_ERR_UNKNOWN));

    // [Pre-Assert確認_異常系] - 監視用の時計を 1 回取得すること。
    // [Pre-Assert手順] - 時計取得失敗を返す。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_service_all_paths(_)).Times(0);

    // [Pre-Assert確認_異常系] - 不正な時刻で切断を判断しないこと。

    EXPECT_CALL(mock_porter, potr_internal_send_queue_abort(&ctx.send_queue, POTR_ERR_IO)).Times(1);

    // [Pre-Assert確認_異常系] - 送信キューの待機を 1 回解除すること。

    // Act
    thread_recv_health_check_service(&slot); // [手順] - 監視中の時刻取得を失敗させる。

    // Assert
    EXPECT_EQ(POTR_ERR_IO, cplat_atomic_load_i32(
                               &ctx.fatal_error,
                               CPLAT_MEMORY_ORDER_ACQUIRE)); // [確認_異常系] - 呼び出し元へ入出力失敗を通知すること。
    EXPECT_EQ(123, ctx.peer_port[0]);                        // [確認_異常系] - パス状態を変更しないこと。
}

// 妨げ記録もキュー満杯記録もない場合はロックや収集を行わずに 0 を返すことの確認
TEST(threadRecvHealthTest, slow_peers_idle_returns_zero_without_locking)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.is_multi_peer = 1;
    // [状態] - N:1 サーバーで flow_blocking_peers と flow_queue_full_ms が 0 のコンテキストを用意する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_flow_control_collect_slow_peers_locked(_, _, _, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - 妨げ記録がないため収集関数が呼ばれないこと。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_peer_all_paths(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - 切断通知が呼ばれないこと。
    EXPECT_CALL(mock_porter, potr_internal_peer_free(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - ピア解放が呼ばれないこと。
    // Act
    uint64_t next_ms = thread_recv_health_check_slow_peers(&ctx, 1000U); // [手順] - 遅いピアの確認を呼ぶ。
    // Assert
    EXPECT_EQ(0U, next_ms); // [確認_正常系] - 次回確認時刻が 0 であること。
}

// 猶予時間に達した遅いピアをヘルス チェックのタイムアウトと同じ手順で切断し、送信側を起床させることの確認
TEST(threadRecvHealthTest, slow_peers_disconnects_same_as_health_timeout)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context peer = {};
    cplat_local_lock *lock = reinterpret_cast<cplat_local_lock *>(0x100);
    ctx.service.service_id = 1;
    ctx.is_multi_peer = 1;
    ctx.peers_mutex = lock;
    ctx.flow_control_config.grace_ms = 200U;
    cplat_atomic_store_i32(&ctx.flow_blocking_peers, 1, CPLAT_MEMORY_ORDER_RELEASE);
    peer.peer_id = 42U;
    peer.active = 1;
    peer.remote_path_ping_state[0] = POTR_PING_STATE_NORMAL;
    // [状態] - 妨げ記録がある N:1 サーバーと、ピア 42 を用意する。
    ON_CALL(mock_porter, potr_internal_peer_find_by_id(&ctx, 42U)).WillByDefault(Return(&peer));
    ON_CALL(mock_porter, potr_internal_flow_control_collect_slow_peers_locked(&ctx, 1200U, _, _, _, _))
        .WillByDefault([](potr_context *, uint64_t, potr_peer_id *out_peers, uint64_t *out_accum_ms, size_t,
                          uint64_t *next_check_ms) -> size_t {
            out_peers[0] = 42U;
            out_accum_ms[0] = 200U;
            *next_check_ms = 1250U;
            return 1U;
        }); // [状態] - 収集関数がピア 42 (積算 200 [ms]) と次回確認時刻 1250 を返すようにする。
    // Pre-Assert
    InSequence seq;
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(lock)).Times(1).WillOnce(Return());
    // [Pre-Assert確認_正常系] - peers_mutex を取得すること。
    EXPECT_CALL(mock_porter, potr_internal_flow_control_collect_slow_peers_locked(&ctx, 1200U, _, _, _, _)).Times(1);
    // [Pre-Assert確認_正常系] - peers_mutex 保持下で切断対象を収集すること。
    EXPECT_CALL(mock_porter, thread_recv_disconnect_peer_all_paths(&ctx, &peer)).Times(1);
    // [Pre-Assert確認_正常系] - ヘルス チェックのタイムアウトと同じ切断通知を呼ぶこと。
    EXPECT_CALL(mock_porter, potr_internal_peer_free(&ctx, &peer)).Times(1);
    // [Pre-Assert確認_正常系] - 切断通知の後にピアを解放すること。
    EXPECT_CALL(mock_cplat, cplat_local_lock_unlock(lock)).Times(1).WillOnce(Return());
    // [Pre-Assert確認_正常系] - peers_mutex を解放すること。
    EXPECT_CALL(mock_porter, potr_internal_send_queue_wake_flow(&ctx.send_queue)).Times(1);
    // [Pre-Assert確認_正常系] - ロック解放後に送信キューの待機を起床させること。
    // Act
    uint64_t next_ms = thread_recv_health_check_slow_peers(&ctx, 1200U); // [手順] - 遅いピアの切断確認を呼ぶ。
    // Assert
    EXPECT_EQ(1250U, next_ms); // [確認_正常系] - 次回確認時刻 1250 が返ること。
    EXPECT_EQ(0U, peer.remote_path_ping_state[0]);
    // [確認_正常系] - ヘルス チェックのタイムアウトと同じく remote_path_ping_state が 0 クリアされること。
}

// 次の判定予定時刻に達する前はロックと走査を省き、判定間隔の上限が POTR_INTERNAL_FLOW_RECHECK_MS を超えないことの確認
TEST(threadRecvHealthTest, slow_peers_skips_scan_before_next_check_ms_and_clamps_interval)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    cplat_local_lock *lock = reinterpret_cast<cplat_local_lock *>(0x100);
    ctx.service.service_id = 1;
    ctx.is_multi_peer = 1;
    ctx.peers_mutex = lock;
    ctx.flow_control_config.grace_ms = 200U;
    cplat_atomic_store_i32(&ctx.flow_blocking_peers, 1, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - 妨げ記録が 1 件ある N:1 サーバーを用意する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(lock)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(lock)).WillByDefault(Return());
    ON_CALL(mock_porter, potr_internal_flow_control_collect_slow_peers_locked(&ctx, _, _, _, _, _))
        .WillByDefault([](potr_context *, uint64_t now_ms, potr_peer_id *, uint64_t *, size_t,
                          uint64_t *next_check_ms) -> size_t {
            *next_check_ms = now_ms + 200U;
            return 0U;
        }); // [状態] - 収集関数が 200 [ms] 後を返すようにする (POTR_INTERNAL_FLOW_RECHECK_MS を超える値)。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(lock)).Times(2);
    // [Pre-Assert確認_正常系] - 初回と予定時刻到達後の 2 回だけロックを取得すること (間隔内の呼び出しは省く)。
    EXPECT_CALL(mock_porter, potr_internal_flow_control_collect_slow_peers_locked(&ctx, _, _, _, _, _)).Times(2);
    // [Pre-Assert確認_正常系] - 初回 (1000) と予定時刻到達後 (1050) の 2 回だけ収集が呼ばれること。
    // Act
    uint64_t next1 = thread_recv_health_check_slow_peers(&ctx, 1000U); // [手順] - 時刻 1000 で初回判定する。
    uint64_t next2 = thread_recv_health_check_slow_peers(&ctx, 1049U); // [手順] - 予定時刻 (1050) 未満の 1049 で呼ぶ。
    uint64_t next3 = thread_recv_health_check_slow_peers(&ctx, 1050U); // [手順] - 予定時刻 (1050) で呼ぶ。
    // Assert
    EXPECT_EQ(1000U + (uint64_t)POTR_INTERNAL_FLOW_RECHECK_MS, next1);
    // [確認_正常系] - 200 [ms] 後の戻り値が POTR_INTERNAL_FLOW_RECHECK_MS (50 [ms]) で上限丸めされること。
    EXPECT_EQ(next1, next2); // [確認_正常系] - 間隔内の呼び出しでは前回の予定時刻をそのまま返すこと。
    EXPECT_EQ(1050U + (uint64_t)POTR_INTERNAL_FLOW_RECHECK_MS, next3);
    // [確認_正常系] - 予定時刻到達後の呼び出しで次の予定時刻へ進むこと。
}

// 判定予定時刻に達する前でも、妨げの記録が新しく増えた場合とキュー満杯が新しく記録された場合は即座に判定することの確認
TEST(threadRecvHealthTest, slow_peers_scans_immediately_on_new_blocking_or_queue_full)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    cplat_local_lock *lock = reinterpret_cast<cplat_local_lock *>(0x100);
    ctx.service.service_id = 1;
    ctx.is_multi_peer = 1;
    ctx.peers_mutex = lock;
    ctx.flow_control_config.grace_ms = 200U;
    cplat_atomic_store_i32(&ctx.flow_blocking_peers, 1, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - 妨げ記録が 1 件ある N:1 サーバーを用意する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(lock)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(lock)).WillByDefault(Return());
    ON_CALL(mock_porter, potr_internal_flow_control_collect_slow_peers_locked(&ctx, _, _, _, _, _))
        .WillByDefault([](potr_context *c, uint64_t now_ms, potr_peer_id *, uint64_t *, size_t,
                          uint64_t *next_check_ms) -> size_t {
            cplat_atomic_store_u64(&c->flow_queue_full_ms, 0U, CPLAT_MEMORY_ORDER_RELEASE);
            *next_check_ms = now_ms + (uint64_t)POTR_INTERNAL_FLOW_RECHECK_MS;
            return 0U;
        }); // [状態] - 収集関数が flow_queue_full_ms を消費し、50 [ms] 後を返すようにする。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(lock)).Times(3);
    // [Pre-Assert確認_正常系] - 初回、妨げ増加時、キュー満杯新規記録時の 3 回ロックを取得すること。
    EXPECT_CALL(mock_porter, potr_internal_flow_control_collect_slow_peers_locked(&ctx, _, _, _, _, _)).Times(3);
    // [Pre-Assert確認_正常系] - 予定時刻未満でも新しい記録のたびに即座に収集が呼ばれること。
    // Act
    uint64_t next1 = thread_recv_health_check_slow_peers(&ctx, 1000U); // [手順] - 時刻 1000 で初回判定する (次回 1050)。
    cplat_atomic_store_i32(&ctx.flow_blocking_peers, 2, CPLAT_MEMORY_ORDER_RELEASE);
    uint64_t next2 = thread_recv_health_check_slow_peers(&ctx, 1010U); // [手順] - 妨げ記録増加後に時刻 1010 で呼ぶ。
    cplat_atomic_store_u64(&ctx.flow_queue_full_ms, 1020U, CPLAT_MEMORY_ORDER_RELEASE);
    uint64_t next3 = thread_recv_health_check_slow_peers(&ctx, 1020U); // [手順] - キュー満杯新規記録後に時刻 1020 で呼ぶ。
    // Assert
    EXPECT_EQ(1050U, next1); // [確認_正常系] - 初回の次回予定時刻が 1050 であること。
    EXPECT_EQ(1060U, next2); // [確認_正常系] - 妨げ増加で時刻 1010 に即座に判定され次回が 1060 になること。
    EXPECT_EQ(1070U, next3); // [確認_正常系] - キュー満杯記録で時刻 1020 に即座に判定され次回が 1070 になること。
}

// 遅いピアの切断で解放前にセッションが拒否表へ記録され、WARNING に受け入れない期間が出ることの確認
TEST(threadRecvHealthTest, slow_peers_records_disconnected_session_before_free)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context peer = {};
    cplat_local_lock *lock = reinterpret_cast<cplat_local_lock *>(0x100);
    ctx.service.service_id = 1;
    ctx.is_multi_peer = 1;
    ctx.peers_mutex = lock;
    ctx.flow_control_config.grace_ms = 200U;
    cplat_atomic_store_i32(&ctx.flow_blocking_peers, 1, CPLAT_MEMORY_ORDER_RELEASE);
    peer.peer_id = 42U;
    peer.active = 1;
    peer.peer_session_id = 777U;
    peer.peer_session_ts = {100, 200};
    peer.peer_session_known = 1;
    // [状態] - セッション (777, 100.200) を持つ遅いピア 42 と、猶予時間 200 [ms] の N:1 サーバーを用意する。
    ON_CALL(mock_porter, potr_internal_peer_find_by_id(&ctx, 42U)).WillByDefault(Return(&peer));
    ON_CALL(mock_porter, potr_internal_flow_control_collect_slow_peers_locked(&ctx, 1200U, _, _, _, _))
        .WillByDefault([](potr_context *, uint64_t, potr_peer_id *out_peers, uint64_t *out_accum_ms, size_t,
                          uint64_t *next_check_ms) -> size_t {
            out_peers[0] = 42U;
            out_accum_ms[0] = 200U;
            *next_check_ms = 1250U;
            return 1U;
        });
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat,
                cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_WARNING, nullptr, HasSubstr("reject=2000ms")))
        .Times(1);
    // [Pre-Assert確認_正常系] - 切断の WARNING に受け入れない期間 (reject=2000ms) が含まれること。
    {
        InSequence seq;
        EXPECT_CALL(mock_cplat, cplat_local_lock_lock(lock)).Times(1).WillOnce(Return());
        // [Pre-Assert確認_正常系] - peers_mutex を取得すること。
        EXPECT_CALL(mock_porter, thread_recv_disconnect_peer_all_paths(&ctx, &peer)).Times(1);
        // [Pre-Assert確認_正常系] - 切断通知を呼ぶこと。
        EXPECT_CALL(mock_porter,
                    potr_internal_flow_control_slow_peer_record_locked(&ctx, 777U, &peer.peer_session_ts, 1200U))
            .Times(1);
        // [Pre-Assert確認_正常系] - ピアを解放する前に、セッションと切断時刻を拒否表へ記録すること。
        EXPECT_CALL(mock_porter, potr_internal_peer_free(&ctx, &peer)).Times(1);
        // [Pre-Assert確認_正常系] - 記録の後にピアを解放すること。
        EXPECT_CALL(mock_cplat, cplat_local_lock_unlock(lock)).Times(1).WillOnce(Return());
        // [Pre-Assert確認_正常系] - peers_mutex を解放すること。
    }
    // Act
    uint64_t next_ms = thread_recv_health_check_slow_peers(&ctx, 1200U); // [手順] - 遅いピアの切断確認を呼ぶ。
    // Assert
    EXPECT_EQ(1250U, next_ms); // [確認_正常系] - 次回確認時刻 1250 が返ること。
}

// ヘルス チェックのタイムアウトで切断したピアは、遅いピアの拒否表へ記録しないことの確認
TEST(threadRecvHealthTest, health_timeout_does_not_record_slow_peer_reject)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    potr_internal_peer_context peers[1] = {};
    cplat_local_lock *lock = reinterpret_cast<cplat_local_lock *>(0x100);
    ctx.service.service_id = 1;
    ctx.is_multi_peer = 1;
    ctx.peers = peers;
    ctx.max_peers = 1;
    ctx.peers_mutex = lock;
    ctx.health_timeout_ms = 500U;
    peers[0].peer_id = 99U;
    peers[0].active = 1;
    peers[0].peer_session_id = 888U;
    peers[0].peer_session_ts = {50, 0};
    peers[0].peer_session_known = 1;
    peers[0].last_recv_ts = {10, 0};
    cplat_atomic_store_i32(&peers[0].health_alive, 1, CPLAT_MEMORY_ORDER_RELEASE);
    // [状態] - タイムアウト期限を超過した N:1 ピア 99 を用意する。
    ON_CALL(mock_cplat, cplat_clock_get_monotonic(_)).WillByDefault([](cplat_timespec *ts) {
        ts->tv_sec = 20;
        ts->tv_nsec = 0;
        return CPLAT_OK;
    });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _, _)).WillByDefault([](const cplat_timespec *, const cplat_timespec *, int64_t *diff) {
        *diff = 1000;
        return CPLAT_OK;
    });
    ON_CALL(mock_cplat, cplat_local_lock_lock(lock)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(lock)).WillByDefault(Return());
    // Pre-Assert
    EXPECT_CALL(mock_porter, thread_recv_disconnect_peer_all_paths(&ctx, &peers[0])).Times(1);
    // [Pre-Assert確認_正常系] - タイムアウトで切断通知を呼ぶこと。
    EXPECT_CALL(mock_porter, potr_internal_peer_free(&ctx, &peers[0])).Times(1);
    // [Pre-Assert確認_正常系] - タイムアウトでピアを解放すること。
    EXPECT_CALL(mock_porter, potr_internal_flow_control_slow_peer_record_locked(_, _, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - ヘルス チェックのタイムアウトでは拒否表へ記録しないこと。
    // Act
    thread_recv_health_check_peers(&ctx); // [手順] - N:1 のヘルス チェック タイムアウト確認を呼ぶ。
    // Assert
    EXPECT_EQ(0U, ctx.flow_slow_rejected_count); // [確認_正常系] - 拒否記録の件数が 0 のままであること。
}
