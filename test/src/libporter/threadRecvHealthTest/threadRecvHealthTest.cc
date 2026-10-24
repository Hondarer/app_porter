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
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _)).WillByDefault(Return(100));
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
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _)).WillByDefault(Return(99));
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
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _)).WillByDefault(Return(100));
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
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _)).WillByDefault(Return(101));
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
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _)).WillByDefault(Return(100));
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
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _)).WillByDefault(Return(100));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - ピアとパスの受信時刻、監視状態を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.peers_mutex, CPLAT_SYNC_WAIT_FOREVER))
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
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _)).WillByDefault(Return(99));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - ピアとパスの受信時刻、監視状態を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.peers_mutex, CPLAT_SYNC_WAIT_FOREVER))
        .Times(1)
        .WillRepeatedly(Return(CPLAT_OK)); // [Pre-Assert確認_正常系] - 有効時だけピアのロックを取得すること。
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
        .WillRepeatedly(Return(CPLAT_OK)); // [Pre-Assert確認_正常系] - 起床前にロックを解放すること。
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
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _)).WillByDefault(Return(100));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - ピアとパスの受信時刻、監視状態を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.peers_mutex, CPLAT_SYNC_WAIT_FOREVER))
        .Times(1)
        .WillRepeatedly(Return(CPLAT_OK)); // [Pre-Assert確認_正常系] - 有効時だけピアのロックを取得すること。
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
        .WillRepeatedly(Return(CPLAT_OK)); // [Pre-Assert確認_正常系] - 起床前にロックを解放すること。
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
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _)).WillByDefault(Return(101));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - ピアとパスの受信時刻、監視状態を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.peers_mutex, CPLAT_SYNC_WAIT_FOREVER))
        .Times(1)
        .WillRepeatedly(Return(CPLAT_OK)); // [Pre-Assert確認_正常系] - 有効時だけピアのロックを取得すること。
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
        .WillRepeatedly(Return(CPLAT_OK)); // [Pre-Assert確認_正常系] - 起床前にロックを解放すること。
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
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _)).WillByDefault(Return(100));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - ピアとパスの受信時刻、監視状態を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.peers_mutex, CPLAT_SYNC_WAIT_FOREVER))
        .Times(1)
        .WillRepeatedly(Return(CPLAT_OK)); // [Pre-Assert確認_正常系] - 有効時だけピアのロックを取得すること。
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
        .WillRepeatedly(Return(CPLAT_OK)); // [Pre-Assert確認_正常系] - 起床前にロックを解放すること。
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
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _)).WillByDefault(Return(100));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - ピアとパスの受信時刻、監視状態を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.peers_mutex, CPLAT_SYNC_WAIT_FOREVER))
        .Times(1)
        .WillRepeatedly(Return(CPLAT_OK)); // [Pre-Assert確認_正常系] - 有効時だけピアのロックを取得すること。
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
        .WillRepeatedly(Return(CPLAT_OK)); // [Pre-Assert確認_正常系] - 起床前にロックを解放すること。
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
            });
    ON_CALL(mock_cplat, cplat_timespec_diff_ms(_, _)).WillByDefault(Return(100));
    ON_CALL(mock_porter, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(1));
    // [状態] - ピアとパスの受信時刻、監視状態を指定する。
    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.peers_mutex, CPLAT_SYNC_WAIT_FOREVER))
        .Times(1)
        .WillRepeatedly(Return(CPLAT_OK)); // [Pre-Assert確認_正常系] - 有効時だけピアのロックを取得すること。
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
        .WillRepeatedly(Return(CPLAT_OK)); // [Pre-Assert確認_正常系] - 起床前にロックを解放すること。
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
