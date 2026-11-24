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

#include "../../../include/porter_handle_test.h"

using namespace testing;

// マルチキャストの close で、参加時に保持した NIC を指定してグループから離脱することの確認
TEST(potrServiceCloseTest, multicast_close_leaves_joined_interface)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    porter_handle_test owned_handle(&ctx); // [サブ手順参照 名前=porter_handle_test.constructor]
    ctx.service.type = POTR_TYPE_MULTICAST;
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.n_path = 1;
    ctx.sock[0] = static_cast<cplat_socket>(9);
    ctx.multicast_if_resolved[0] = 77U;
    ctx.src_addr_resolved[0] = 42U;
    std::memcpy(ctx.service.multicast_group, "239.0.0.2", sizeof("239.0.0.2"));
    ON_CALL(mock_cplat, cplat_socket_leave_multicast_group(_, _, _, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - グループ離脱を成功させる。
    ON_CALL(mock_cplat, cplat_socket_close(_)).WillByDefault(Return());
    // [状態] - ソケットのクローズを実ソケットへ渡さない。
    ON_CALL(mock_cplat, cplat_free(&ctx)).WillByDefault(Return());
    // [状態] - スタック上のコンテキストを解放しない。
    // Pre-Assert
    EXPECT_CALL(mock_porter, api_context_dispose_resources(&ctx)).Times(1);
    // [Pre-Assert確認_正常系] - 共通解放 (マルチキャスト離脱・watch 解除・ソケット閉鎖を含む) が 1 回呼ばれること。
    // Act
    int actual_ret = potr_service_close(owned_handle.get()); // [手順] - マルチキャスト サービスを終了する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - potr_service_close() の戻り値が POTR_OK であること。
}

// UDP 通常終了で停止手順の後に共通解放と本体解放を呼ぶことの確認
TEST(potrServiceCloseTest, udp_stop_precedes_shared_cleanup)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    porter_handle_test owned_handle(&ctx); // [サブ手順参照 名前=porter_handle_test.constructor]
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.role = POTR_ROLE_SENDER;
    int token = 0;
    std::vector<int> order;
    ctx.send_thread = reinterpret_cast<cplat_thread *>(&token);
    ON_CALL(mock_cplat, cplat_free(&ctx)).WillByDefault([&](void *) { order.push_back(5); });
    ON_CALL(mock_porter, api_context_dispose_resources(_)).WillByDefault([&](potr_context *) { order.push_back(4); });
    ON_CALL(mock_porter, api_context_dispose_tcp_sync(_)).WillByDefault([&](potr_context *) { order.push_back(3); });
    ON_CALL(mock_porter, potr_internal_send_queue_dispose(_))
        .WillByDefault([&](potr_internal_send_queue *) { order.push_back(2); });
    ON_CALL(mock_porter, potr_internal_send_thread_stop(_))
        .WillByDefault(
            [&](potr_context *p)
            {
                order.push_back(1);
                p->send_thread = nullptr;
            });
    // Pre-Assert
    EXPECT_CALL(mock_porter, api_context_dispose_resources(&ctx)).Times(1);
    // [Pre-Assert確認_正常系] - 通常終了で共通解放を一度呼ぶこと。
    // Act
    int actual_ret = potr_service_close(owned_handle.get()); // [手順] - UDP サービスを終了する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 通常終了が成功すること。
    EXPECT_EQ((std::vector<int>{1, 4, 5}), order);
    // [確認_正常系] - スレッド停止、共通解放 (キューと同期資源の破棄を含む)、本体解放の順序を保持すること。
}

// TCP 通常終了で停止手順の後に共通解放と本体解放を呼ぶことの確認
TEST(potrServiceCloseTest, tcp_stop_precedes_shared_cleanup)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    porter_handle_test owned_handle(&ctx); // [サブ手順参照 名前=porter_handle_test.constructor]
    ctx.service.type = POTR_TYPE_TCP;
    ctx.role = POTR_ROLE_SENDER;
    int token = 0;
    std::vector<int> order;
    ctx.send_thread = reinterpret_cast<cplat_thread *>(&token);
    ON_CALL(mock_cplat, cplat_free(&ctx)).WillByDefault([&](void *) { order.push_back(5); });
    ON_CALL(mock_porter, api_context_dispose_resources(_)).WillByDefault([&](potr_context *) { order.push_back(4); });
    ON_CALL(mock_porter, api_context_dispose_tcp_sync(_)).WillByDefault([&](potr_context *) { order.push_back(3); });
    ON_CALL(mock_porter, potr_internal_send_queue_dispose(_))
        .WillByDefault([&](potr_internal_send_queue *) { order.push_back(2); });
    ON_CALL(mock_porter, potr_internal_connect_thread_stop(_))
        .WillByDefault(
            [&](potr_context *p)
            {
                order.push_back(1);
                p->send_thread = nullptr;
            });
    // Pre-Assert
    EXPECT_CALL(mock_porter, api_context_dispose_resources(&ctx)).Times(1);
    // [Pre-Assert確認_正常系] - 通常終了で共通解放を一度呼ぶこと。
    // Act
    int actual_ret = potr_service_close(owned_handle.get()); // [手順] - TCP サービスを終了する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 通常終了が成功すること。
    EXPECT_EQ((std::vector<int>{1, 4, 5}), order);
    // [確認_正常系] - スレッド停止、共通解放 (キューと TCP 同期資源の破棄を含む)、本体解放の順序を保持すること。
}

// 自サービスのコールバック内から呼ぶと、閉じずに未サポート コードを返すことの確認
TEST(potrServiceCloseTest, call_from_own_callback_returns_unsupported)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    porter_handle_test owned_handle(&ctx); // [サブ手順参照 名前=porter_handle_test.constructor]
    ctx.service.type = POTR_TYPE_UNICAST;
    ctx.role = POTR_ROLE_RECEIVER;
    ON_CALL(mock_porter, potr_internal_in_own_callback(&ctx))
        .WillByDefault(Return(1)); // [状態] - 自サービスのコールバック内からの呼び出しとする。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_comm_recv_thread_stop(_)).Times(0);
    // [Pre-Assert確認_異常系] - 受信スレッドを停止しないこと。
    EXPECT_CALL(mock_porter, api_context_dispose_resources(_)).Times(0);
    // [Pre-Assert確認_異常系] - 共通資源を解放しないこと。
    EXPECT_CALL(mock_cplat, cplat_free(_)).Times(AnyNumber());
    // [Pre-Assert確認_正常系] - 試験用制御オブジェクトの解放を許可する。
    EXPECT_CALL(mock_cplat, cplat_free(&ctx)).Times(0);
    // [Pre-Assert確認_異常系] - コンテキスト本体を解放しないこと。
    // Act
    int actual_ret = potr_service_close(owned_handle.get()); // [手順] - コールバック内からサービスを閉じる。
    // Assert
    EXPECT_EQ(POTR_ERR_UNSUPPORTED, actual_ret); // [確認_異常系] - 未サポート コードを返すこと。
    EXPECT_EQ(0, cplat_atomic_load_i32(&ctx.close_requested, CPLAT_MEMORY_ORDER_RELAXED));
    // [確認_異常系] - 終了処理中の状態にしないこと。(ハンドルが有効なまま残ることの確認)
}

// TCP 送信者の close が、送信ウィンドウの保護下で取得した送信済み状態で切断エラーを判定することの確認
TEST(potrServiceCloseTest, tcp_close_without_active_path_after_data_returns_disconnected)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    porter_handle_test owned_handle(&ctx); // [サブ手順参照 名前=porter_handle_test.constructor]
    ctx.service.type = POTR_TYPE_TCP;
    ctx.role = POTR_ROLE_SENDER;
    ctx.send_has_data = 1; // [状態] - 現セッションで DATA を送信済みとする。
    cplat_atomic_store_i32(&ctx.send_thread_running, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_active_paths, 0,
                           CPLAT_MEMORY_ORDER_RELAXED); // [状態] - 全 TCP パスが切断済みとする。
    delegate_real_cplat_local_lock_create(&ctx.send_window_mutex);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault(DoAll(SetArgPointee<0>(1000U), Return(CPLAT_OK)));
    ON_CALL(mock_cplat, cplat_free(&ctx)).WillByDefault(Return()); // [状態] - スタック上の ctx は解放しない。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(_)).Times(AnyNumber());
    // [Pre-Assert確認_正常系] - ハンドル管理用ロックの取得も許可する。
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.send_window_mutex)).Times(AtLeast(1));
    // [Pre-Assert確認_正常系] - 送信済み状態を send_window_mutex の保護下で取得すること。
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_control_packet(_, _)).Times(0);
    // [Pre-Assert確認_異常系] - 切断済みのため FIN を送信しないこと。
    // Act
    int actual_ret = potr_service_close(owned_handle.get()); // [手順] - 全パス切断後の TCP サービスを閉じる。
    // Assert
    EXPECT_EQ(POTR_ERR_DISCONNECTED, actual_ret); // [確認_異常系] - 切断エラーを返すこと。
    // Cleanup
    delegate_real_cplat_local_lock_dispose(ctx.send_window_mutex);
}

// 相手 FIN 受理済み・送信済みデータあり・FIN 受理後の送信なしなら FIN を送らず POTR_OK を返すことの確認
TEST(potrServiceCloseTest, tcp_close_after_peer_fin_without_post_fin_send_returns_ok)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    porter_handle_test owned_handle(&ctx); // [サブ手順参照 名前=porter_handle_test.constructor]
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.send_has_data = 1; // [状態] - 相手 FIN 受理前には現セッションで DATA を送信済みとする。
    cplat_atomic_store_i32(&ctx.send_thread_running, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_active_paths, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_peer_fin_received, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_sent_after_peer_fin, 0, CPLAT_MEMORY_ORDER_RELAXED);
    delegate_real_cplat_local_lock_create(&ctx.send_window_mutex);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault(DoAll(SetArgPointee<0>(1000U), Return(CPLAT_OK)));
    ON_CALL(mock_cplat, cplat_free(&ctx)).WillByDefault(Return()); // [状態] - スタック上の ctx は解放しない。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(_)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.send_window_mutex)).Times(AtLeast(1));
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_control_packet(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - 相手 FIN 受理済みのため FIN を送信しないこと。
    EXPECT_CALL(mock_cplat, cplat_condvar_timed_wait(_, _, _)).Times(0);
    // [Pre-Assert確認_正常系] - FIN_ACK 待機を行わないこと。

    // Act
    int actual_ret = potr_service_close(owned_handle.get()); // [手順] - 相手 FIN 受理済みのサービスを閉じる。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - POTR_OK を返すこと。

    // Cleanup
    delegate_real_cplat_local_lock_dispose(ctx.send_window_mutex);
}

// 相手 FIN 受理済みかつ FIN 受理後の送信ありなら FIN を送らず POTR_ERR_DISCONNECTED を返すことの確認
TEST(potrServiceCloseTest, tcp_close_after_peer_fin_with_post_fin_send_returns_disconnected)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    porter_handle_test owned_handle(&ctx); // [サブ手順参照 名前=porter_handle_test.constructor]
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    ctx.send_has_data = 1;
    cplat_atomic_store_i32(&ctx.send_thread_running, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_active_paths, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_peer_fin_received, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_sent_after_peer_fin, 1, CPLAT_MEMORY_ORDER_RELAXED);
    delegate_real_cplat_local_lock_create(&ctx.send_window_mutex);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault(DoAll(SetArgPointee<0>(1000U), Return(CPLAT_OK)));
    ON_CALL(mock_cplat, cplat_free(&ctx)).WillByDefault(Return()); // [状態] - スタック上の ctx は解放しない。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(_)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.send_window_mutex)).Times(AtLeast(1));
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_control_packet(_, _)).Times(0);
    // [Pre-Assert確認_異常系] - 相手 FIN 受理済みのため FIN を送信しないこと。
    EXPECT_CALL(mock_cplat, cplat_condvar_timed_wait(_, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - FIN_ACK 待機を行わないこと。

    // Act
    int actual_ret = potr_service_close(owned_handle.get()); // [手順] - 相手 FIN 受理後に DATA を送信したサービスを閉じる。

    // Assert
    EXPECT_EQ(POTR_ERR_DISCONNECTED, actual_ret); // [確認_異常系] - 切断エラーを返すこと。

    // Cleanup
    delegate_real_cplat_local_lock_dispose(ctx.send_window_mutex);
}

// 相手 FIN 未受理で FIN_ACK 待機中に全経路が切断された場合、has_data == 0 なら即時 POTR_OK を返すことの確認
TEST(potrServiceCloseTest, tcp_close_returns_ok_immediately_when_paths_drop_during_wait_without_data)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int close_lock_token = 0;
    int close_cv_token = 0;
    porter_handle_test owned_handle(&ctx); // [サブ手順参照 名前=porter_handle_test.constructor]
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    ctx.global.tcp_close_timeout_ms = 5000U;
    ctx.send_has_data = 0;
    ctx.tcp_close_mutex = reinterpret_cast<cplat_local_lock *>(&close_lock_token);
    ctx.tcp_close_cv = reinterpret_cast<cplat_condvar *>(&close_cv_token);
    cplat_atomic_store_i32(&ctx.send_thread_running, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_active_paths, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_peer_fin_received, 0, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_sent_after_peer_fin, 0, CPLAT_MEMORY_ORDER_RELAXED);
    delegate_real_cplat_local_lock_create(&ctx.send_window_mutex);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault(DoAll(SetArgPointee<0>(1000U), Return(CPLAT_OK)));
    ON_CALL(mock_cplat, cplat_free(&ctx)).WillByDefault(Return()); // [状態] - スタック上の ctx は解放しない。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(_)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_local_lock_unlock(_)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.tcp_close_mutex)).WillRepeatedly(Return());
    // [Pre-Assert確認_正常系] - cplat_local_lock_lock() が ctx.tcp_close_mutex を指定して任意の回数呼び出されること。
    // [Pre-Assert手順] - cplat_local_lock_lock() から、本物でないトークンであるため実関数へ委譲せず戻る。
    EXPECT_CALL(mock_cplat, cplat_local_lock_unlock(ctx.tcp_close_mutex)).WillRepeatedly(Return());
    // [Pre-Assert確認_正常系] - cplat_local_lock_unlock() が ctx.tcp_close_mutex を指定して任意の回数呼び出されること。
    // [Pre-Assert手順] - cplat_local_lock_unlock() から、本物でないトークンであるため実関数へ委譲せず戻る。
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_control_packet(_, _)).WillOnce(Return(POTR_OK));
    // [Pre-Assert確認_正常系] - FIN を 1 回送信すること。
    EXPECT_CALL(mock_cplat, cplat_condvar_timed_wait(ctx.tcp_close_cv, ctx.tcp_close_mutex, 5000))
        .WillOnce(
            [&ctx](cplat_condvar *, cplat_local_lock *, int)
            {
                cplat_atomic_store_i32(&ctx.tcp_active_paths, 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_OK;
            });
    // [Pre-Assert確認_正常系] - 起床時に全経路切断済みなら 1 回の待機でループを終了しタイムアウトまで待たないこと。

    // Act
    int actual_ret = potr_service_close(owned_handle.get()); // [手順] - has_data == 0 で待機中に全経路が切断された場合を再現して閉じる。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - POTR_OK を返すこと。

    // Cleanup
    delegate_real_cplat_local_lock_dispose(ctx.send_window_mutex);
}

// 相手 FIN 未受理で FIN_ACK 待機中に全経路が切断された場合、has_data != 0 なら即時 POTR_ERR_DISCONNECTED を返すことの確認
TEST(potrServiceCloseTest, tcp_close_returns_disconnected_immediately_when_paths_drop_during_wait_with_data)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int close_lock_token = 0;
    int close_cv_token = 0;
    porter_handle_test owned_handle(&ctx); // [サブ手順参照 名前=porter_handle_test.constructor]
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.role = POTR_ROLE_SENDER;
    ctx.global.tcp_close_timeout_ms = 5000U;
    ctx.send_has_data = 1;
    ctx.tcp_close_mutex = reinterpret_cast<cplat_local_lock *>(&close_lock_token);
    ctx.tcp_close_cv = reinterpret_cast<cplat_condvar *>(&close_cv_token);
    cplat_atomic_store_i32(&ctx.send_thread_running, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_active_paths, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_peer_fin_received, 0, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_sent_after_peer_fin, 0, CPLAT_MEMORY_ORDER_RELAXED);
    delegate_real_cplat_local_lock_create(&ctx.send_window_mutex);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault(DoAll(SetArgPointee<0>(1000U), Return(CPLAT_OK)));
    ON_CALL(mock_cplat, cplat_free(&ctx)).WillByDefault(Return()); // [状態] - スタック上の ctx は解放しない。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(_)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_local_lock_unlock(_)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.tcp_close_mutex)).WillRepeatedly(Return());
    // [Pre-Assert確認_正常系] - cplat_local_lock_lock() が ctx.tcp_close_mutex を指定して任意の回数呼び出されること。
    // [Pre-Assert手順] - cplat_local_lock_lock() から、本物でないトークンであるため実関数へ委譲せず戻る。
    EXPECT_CALL(mock_cplat, cplat_local_lock_unlock(ctx.tcp_close_mutex)).WillRepeatedly(Return());
    // [Pre-Assert確認_正常系] - cplat_local_lock_unlock() が ctx.tcp_close_mutex を指定して任意の回数呼び出されること。
    // [Pre-Assert手順] - cplat_local_lock_unlock() から、本物でないトークンであるため実関数へ委譲せず戻る。
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_control_packet(_, _)).WillOnce(Return(POTR_OK));
    // [Pre-Assert確認_正常系] - FIN を 1 回送信すること。
    EXPECT_CALL(mock_cplat, cplat_condvar_timed_wait(ctx.tcp_close_cv, ctx.tcp_close_mutex, 5000))
        .WillOnce(
            [&ctx](cplat_condvar *, cplat_local_lock *, int)
            {
                cplat_atomic_store_i32(&ctx.tcp_active_paths, 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_OK;
            });
    // [Pre-Assert確認_異常系] - 起床時に全経路切断済みなら 1 回の待機でループを終了しタイムアウトまで待たないこと。

    // Act
    int actual_ret = potr_service_close(owned_handle.get()); // [手順] - has_data != 0 で待機中に全経路が切断された場合を再現して閉じる。

    // Assert
    EXPECT_EQ(POTR_ERR_DISCONNECTED, actual_ret); // [確認_異常系] - POTR_ERR_DISCONNECTED を返すこと。

    // Cleanup
    delegate_real_cplat_local_lock_dispose(ctx.send_window_mutex);
}

// FIN_ACK 待機中に相手 FIN 受理と全経路切断が起きた場合、FIN 受理後の送信なしなら has_data != 0 でも即時 POTR_OK を返すことの確認
TEST(potrServiceCloseTest, tcp_close_returns_ok_immediately_when_peer_fin_and_paths_drop_during_wait)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int close_lock_token = 0;
    int close_cv_token = 0;
    porter_handle_test owned_handle(&ctx); // [サブ手順参照 名前=porter_handle_test.constructor]
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.global.tcp_close_timeout_ms = 5000U;
    ctx.send_has_data = 1;
    ctx.tcp_close_mutex = reinterpret_cast<cplat_local_lock *>(&close_lock_token);
    ctx.tcp_close_cv = reinterpret_cast<cplat_condvar *>(&close_cv_token);
    cplat_atomic_store_i32(&ctx.send_thread_running, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_active_paths, 1, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_peer_fin_received, 0, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&ctx.tcp_sent_after_peer_fin, 0, CPLAT_MEMORY_ORDER_RELAXED);
    delegate_real_cplat_local_lock_create(&ctx.send_window_mutex);
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_)).WillByDefault(DoAll(SetArgPointee<0>(1000U), Return(CPLAT_OK)));
    ON_CALL(mock_cplat, cplat_free(&ctx)).WillByDefault(Return()); // [状態] - スタック上の ctx は解放しない。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(_)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_local_lock_unlock(_)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.tcp_close_mutex)).WillRepeatedly(Return());
    // [Pre-Assert確認_正常系] - cplat_local_lock_lock() が ctx.tcp_close_mutex を指定して任意の回数呼び出されること。
    // [Pre-Assert手順] - cplat_local_lock_lock() から、本物でないトークンであるため実関数へ委譲せず戻る。
    EXPECT_CALL(mock_cplat, cplat_local_lock_unlock(ctx.tcp_close_mutex)).WillRepeatedly(Return());
    // [Pre-Assert確認_正常系] - cplat_local_lock_unlock() が ctx.tcp_close_mutex を指定して任意の回数呼び出されること。
    // [Pre-Assert手順] - cplat_local_lock_unlock() から、本物でないトークンであるため実関数へ委譲せず戻る。
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_control_packet(_, _)).WillOnce(Return(POTR_OK));
    // [Pre-Assert確認_正常系] - FIN を 1 回送信すること。
    EXPECT_CALL(mock_cplat, cplat_condvar_timed_wait(ctx.tcp_close_cv, ctx.tcp_close_mutex, 5000))
        .WillOnce(
            [&ctx](cplat_condvar *, cplat_local_lock *, int)
            {
                cplat_atomic_store_i32(&ctx.tcp_peer_fin_received, 1, CPLAT_MEMORY_ORDER_RELEASE);
                cplat_atomic_store_i32(&ctx.tcp_active_paths, 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_OK;
            });
    // [Pre-Assert確認_正常系] - 起床時に相手 FIN 受理かつ全経路切断済みなら 1 回の待機で終了すること。

    // Act
    int actual_ret = potr_service_close(owned_handle.get()); // [手順] - 待機中に相手 FIN 受理と全経路切断が起きた場合を再現して閉じる。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - POTR_OK を返すこと。

    // Cleanup
    delegate_real_cplat_local_lock_dispose(ctx.send_window_mutex);
}
