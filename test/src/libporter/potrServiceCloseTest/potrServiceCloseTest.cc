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
    ON_CALL(mock_cplat, cplat_free(_)).WillByDefault([&](void *) { order.push_back(5); });
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
    EXPECT_EQ((std::vector<int>{1, 2, 4, 5}), order);
    // [確認_正常系] - スレッド停止、キュー破棄、同期資源破棄、共通解放、本体解放の順序を保持すること。
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
    ON_CALL(mock_cplat, cplat_free(_)).WillByDefault([&](void *) { order.push_back(5); });
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
    EXPECT_EQ((std::vector<int>{1, 2, 3, 4, 5}), order);
    // [確認_正常系] - スレッド停止、キュー破棄、同期資源破棄、共通解放、本体解放の順序を保持すること。
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
    ON_CALL(mock_cplat, cplat_free(&ctx)).WillByDefault(Return()); // [状態] - スタック上の ctx は解放しない。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(_, _)).Times(AnyNumber());
    // [Pre-Assert確認_正常系] - ハンドル管理用ロックの取得も許可する。
    EXPECT_CALL(mock_cplat, cplat_local_lock_lock(ctx.send_window_mutex, _)).Times(AtLeast(1));
    // [Pre-Assert確認_正常系] - 送信済み状態を send_window_mutex の保護下で取得すること。
    EXPECT_CALL(mock_porter, potr_internal_tcp_send_control_packet(_, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - 切断済みのため FIN を送信しないこと。
    // Act
    int actual_ret = potr_service_close(owned_handle.get()); // [手順] - 全パス切断後の TCP サービスを閉じる。
    // Assert
    EXPECT_EQ(POTR_ERR_DISCONNECTED, actual_ret); // [確認_異常系] - 切断エラーを返すこと。
    // Cleanup
    delegate_real_cplat_local_lock_dispose(ctx.send_window_mutex);
}

// 終了待ち未完了の TCP コンテキストを解放しないことの確認
TEST(potrServiceCloseTest, incomplete_join_retains_shared_resources)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    porter_handle_test owned_handle(&ctx); // [サブ手順参照 名前=porter_handle_test.constructor]
    ctx.service.type = POTR_TYPE_TCP;
    ON_CALL(mock_porter, api_context_has_threads(_)).WillByDefault(Return(1));
    // Pre-Assert
    EXPECT_CALL(mock_porter, api_context_dispose_resources(_)).Times(0);
    // [Pre-Assert確認_異常系] - 終了待ち未完了で共通資源を解放しないこと。
    EXPECT_CALL(mock_porter, api_context_dispose_tcp_sync(_)).Times(0);
    // [Pre-Assert確認_異常系] - 終了待ち未完了で同期資源を解放しないこと。
    EXPECT_CALL(mock_cplat, cplat_free(_)).Times(AnyNumber());
    // [Pre-Assert確認_正常系] - 試験用制御オブジェクトの解放を許可する。
    EXPECT_CALL(mock_cplat, cplat_free(&ctx)).Times(0);
    // [Pre-Assert確認_異常系] - コンテキスト本体を解放しないこと。
    // Act
    int actual_ret = potr_service_close(owned_handle.get()); // [手順] - 終了待ちが完了しないサービスを閉じる。
    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret); // [確認_異常系] - 終了待ち失敗を成功としないこと。
}
