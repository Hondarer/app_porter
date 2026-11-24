#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    // C++17 の std::byte と Windows SDK の byte が衝突するため、std::byte を定義しない。
    // see: https://github.com/microsoft/STL/blob/main/stl/inc/yvals_core.h
    #define _HAS_STD_BYTE 0
    #include <winsock2.h>
#elif defined(PLATFORM_LINUX)
    #include <errno.h>
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

// TCP の lock 生成 1 回目の失敗で、起動成功分の終了待ち後に準備資源を回収することの確認
TEST(potrConnectThreadTest, lock_failure_1_rolls_back)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.n_path = 2;
    ctx.global.max_payload = 64;
    for (int i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        ctx.sock[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
    }

    delegate_real_cplat_local_lock_create(&ctx.tcp_state_mutex);
    delegate_real_cplat_condvar_create(&ctx.tcp_state_cv);
    int creates = 0;
    int joins = 0;
    int allocations = 0;
    int frees = 0;
    int tokens[2] = {};
    bool free_after_join = true;
    ON_CALL(mock_cplat, cplat_local_lock_create(_))
        .WillByDefault(
            [&](cplat_local_lock **out)
            {
                (void)out;
                return CPLAT_ERR_UNKNOWN;
            });
    ON_CALL(mock_cplat, cplat_malloc(_))
        .WillByDefault(
            [&](size_t size) -> void *
            {
                allocations++;

                return malloc(size);
            });
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn, void *)
            {
                *out = reinterpret_cast<cplat_thread *>(&tokens[creates++]);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_thread_join(_))
        .WillByDefault(
            [&](cplat_thread *)
            {
                joins++;
            });
    ON_CALL(mock_cplat, cplat_free(_))
        .WillByDefault(
            [&](void *p)
            {
                if (p != nullptr)
                {
                    frees++;
                    free_after_join = free_after_join && joins == creates;
                    free(p);
                }
            }); // [状態] - 生成成功数、終了待ち数、解放数と順序を記録する。
    // Act
    int actual_ret = potr_internal_connect_thread_start(&ctx); // [手順] - TCP の開始を指定した生成点で失敗させる。
    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret);
    // [確認_異常系] - 最初の生成失敗に対応する分類を返すこと。
    EXPECT_EQ(creates, joins); // [確認_異常系] - 起動成功分だけを終了待ちすること。
    EXPECT_EQ(0, frees);
    // [確認_異常系] - 確保成功した先読みバッファーだけを解放すること。
    EXPECT_TRUE(free_after_join);                    // [確認_異常系] - 終了待ち完了前にバッファーを解放しないこと。
    EXPECT_EQ(nullptr, ctx.session_establish_mutex); // [確認_異常系] - 受信者用ロックを残さないこと。
    EXPECT_EQ(nullptr, ctx.connect_thread[0]);       // [確認_異常系] - 起動成功したハンドルを回収すること。
    EXPECT_EQ(nullptr, ctx.connect_thread[1]);       // [確認_異常系] - 未生成のハンドルを残さないこと。
    delegate_real_cplat_condvar_dispose(ctx.tcp_state_cv);
    delegate_real_cplat_local_lock_dispose(ctx.tcp_state_mutex);
}

// TCP の buffer 生成 1 回目の失敗で、起動成功分の終了待ち後に準備資源を回収することの確認
TEST(potrConnectThreadTest, buffer_failure_1_rolls_back)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.n_path = 2;
    ctx.global.max_payload = 64;
    for (int i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        ctx.sock[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
    }

    delegate_real_cplat_local_lock_create(&ctx.tcp_state_mutex);
    delegate_real_cplat_condvar_create(&ctx.tcp_state_cv);
    int creates = 0;
    int joins = 0;
    int allocations = 0;
    int frees = 0;
    int tokens[2] = {};
    bool free_after_join = true;
    ON_CALL(mock_cplat, cplat_local_lock_create(_))
        .WillByDefault([&](cplat_local_lock **out) { return delegate_real_cplat_local_lock_create(out); });
    ON_CALL(mock_cplat, cplat_malloc(_))
        .WillByDefault(
            [&](size_t size) -> void *
            {
                allocations++;
                if (allocations == 1)
                {
                    return nullptr;
                }
                return malloc(size);
            });
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn, void *)
            {
                *out = reinterpret_cast<cplat_thread *>(&tokens[creates++]);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_thread_join(_))
        .WillByDefault(
            [&](cplat_thread *)
            {
                joins++;
            });
    ON_CALL(mock_cplat, cplat_free(_))
        .WillByDefault(
            [&](void *p)
            {
                if (p != nullptr)
                {
                    frees++;
                    free_after_join = free_after_join && joins == creates;
                    free(p);
                }
            }); // [状態] - 生成成功数、終了待ち数、解放数と順序を記録する。
    // Act
    int actual_ret = potr_internal_connect_thread_start(&ctx); // [手順] - TCP の開始を指定した生成点で失敗させる。
    // Assert
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, actual_ret);
    // [確認_異常系] - 最初の生成失敗に対応する分類を返すこと。
    EXPECT_EQ(creates, joins); // [確認_異常系] - 起動成功分だけを終了待ちすること。
    EXPECT_EQ(0, frees);
    // [確認_異常系] - 確保成功した先読みバッファーだけを解放すること。
    EXPECT_TRUE(free_after_join);                    // [確認_異常系] - 終了待ち完了前にバッファーを解放しないこと。
    EXPECT_EQ(nullptr, ctx.session_establish_mutex); // [確認_異常系] - 受信者用ロックを残さないこと。
    EXPECT_EQ(nullptr, ctx.connect_thread[0]);       // [確認_異常系] - 起動成功したハンドルを回収すること。
    EXPECT_EQ(nullptr, ctx.connect_thread[1]);       // [確認_異常系] - 未生成のハンドルを残さないこと。
    delegate_real_cplat_condvar_dispose(ctx.tcp_state_cv);
    delegate_real_cplat_local_lock_dispose(ctx.tcp_state_mutex);
}

// TCP の buffer 生成 2 回目の失敗で、起動成功分の終了待ち後に準備資源を回収することの確認
TEST(potrConnectThreadTest, buffer_failure_2_rolls_back)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.n_path = 2;
    ctx.global.max_payload = 64;
    for (int i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        ctx.sock[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
    }

    delegate_real_cplat_local_lock_create(&ctx.tcp_state_mutex);
    delegate_real_cplat_condvar_create(&ctx.tcp_state_cv);
    int creates = 0;
    int joins = 0;
    int allocations = 0;
    int frees = 0;
    int tokens[2] = {};
    bool free_after_join = true;
    ON_CALL(mock_cplat, cplat_local_lock_create(_))
        .WillByDefault([&](cplat_local_lock **out) { return delegate_real_cplat_local_lock_create(out); });
    ON_CALL(mock_cplat, cplat_malloc(_))
        .WillByDefault(
            [&](size_t size) -> void *
            {
                allocations++;
                if (allocations == 2)
                {
                    return nullptr;
                }
                return malloc(size);
            });
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn, void *)
            {
                *out = reinterpret_cast<cplat_thread *>(&tokens[creates++]);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_thread_join(_))
        .WillByDefault(
            [&](cplat_thread *)
            {
                joins++;
            });
    ON_CALL(mock_cplat, cplat_free(_))
        .WillByDefault(
            [&](void *p)
            {
                if (p != nullptr)
                {
                    frees++;
                    free_after_join = free_after_join && joins == creates;
                    free(p);
                }
            }); // [状態] - 生成成功数、終了待ち数、解放数と順序を記録する。
    // Act
    int actual_ret = potr_internal_connect_thread_start(&ctx); // [手順] - TCP の開始を指定した生成点で失敗させる。
    // Assert
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, actual_ret);
    // [確認_異常系] - 最初の生成失敗に対応する分類を返すこと。
    EXPECT_EQ(creates, joins); // [確認_異常系] - 起動成功分だけを終了待ちすること。
    EXPECT_EQ(1, frees);
    // [確認_異常系] - 確保成功した先読みバッファーだけを解放すること。
    EXPECT_TRUE(free_after_join);                    // [確認_異常系] - 終了待ち完了前にバッファーを解放しないこと。
    EXPECT_EQ(nullptr, ctx.session_establish_mutex); // [確認_異常系] - 受信者用ロックを残さないこと。
    EXPECT_EQ(nullptr, ctx.connect_thread[0]);       // [確認_異常系] - 起動成功したハンドルを回収すること。
    EXPECT_EQ(nullptr, ctx.connect_thread[1]);       // [確認_異常系] - 未生成のハンドルを残さないこと。
    delegate_real_cplat_condvar_dispose(ctx.tcp_state_cv);
    delegate_real_cplat_local_lock_dispose(ctx.tcp_state_mutex);
}

// TCP の thread 生成 1 回目の失敗で、起動成功分の終了待ち後に準備資源を回収することの確認
TEST(potrConnectThreadTest, thread_failure_1_rolls_back)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.n_path = 2;
    ctx.global.max_payload = 64;
    for (int i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        ctx.sock[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
    }

    delegate_real_cplat_local_lock_create(&ctx.tcp_state_mutex);
    delegate_real_cplat_condvar_create(&ctx.tcp_state_cv);
    int creates = 0;
    int joins = 0;
    int allocations = 0;
    int frees = 0;
    int tokens[2] = {};
    bool free_after_join = true;
    ON_CALL(mock_cplat, cplat_local_lock_create(_))
        .WillByDefault([&](cplat_local_lock **out) { return delegate_real_cplat_local_lock_create(out); });
    ON_CALL(mock_cplat, cplat_malloc(_))
        .WillByDefault(
            [&](size_t size) -> void *
            {
                allocations++;

                return malloc(size);
            });
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn, void *)
            {
                if (creates + 1 == 1)
                {
                    return CPLAT_ERR_UNKNOWN;
                }
                *out = reinterpret_cast<cplat_thread *>(&tokens[creates++]);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_thread_join(_))
        .WillByDefault(
            [&](cplat_thread *)
            {
                joins++;
            });
    ON_CALL(mock_cplat, cplat_free(_))
        .WillByDefault(
            [&](void *p)
            {
                if (p != nullptr)
                {
                    frees++;
                    free_after_join = free_after_join && joins == creates;
                    free(p);
                }
            }); // [状態] - 生成成功数、終了待ち数、解放数と順序を記録する。
    // Act
    int actual_ret = potr_internal_connect_thread_start(&ctx); // [手順] - TCP の開始を指定した生成点で失敗させる。
    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret);
    // [確認_異常系] - 最初の生成失敗に対応する分類を返すこと。
    EXPECT_EQ(creates, joins); // [確認_異常系] - 起動成功分だけを終了待ちすること。
    EXPECT_EQ(2, frees);
    // [確認_異常系] - 確保成功した先読みバッファーだけを解放すること。
    EXPECT_TRUE(free_after_join);                    // [確認_異常系] - 終了待ち完了前にバッファーを解放しないこと。
    EXPECT_EQ(nullptr, ctx.session_establish_mutex); // [確認_異常系] - 受信者用ロックを残さないこと。
    EXPECT_EQ(nullptr, ctx.connect_thread[0]);       // [確認_異常系] - 起動成功したハンドルを回収すること。
    EXPECT_EQ(nullptr, ctx.connect_thread[1]);       // [確認_異常系] - 未生成のハンドルを残さないこと。
    delegate_real_cplat_condvar_dispose(ctx.tcp_state_cv);
    delegate_real_cplat_local_lock_dispose(ctx.tcp_state_mutex);
}

// TCP の thread 生成 2 回目の失敗で、起動成功分の終了待ち後に準備資源を回収することの確認
TEST(potrConnectThreadTest, thread_failure_2_rolls_back)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.n_path = 2;
    ctx.global.max_payload = 64;
    for (int i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        ctx.sock[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
    }

    delegate_real_cplat_local_lock_create(&ctx.tcp_state_mutex);
    delegate_real_cplat_condvar_create(&ctx.tcp_state_cv);
    int creates = 0;
    int joins = 0;
    int allocations = 0;
    int frees = 0;
    int tokens[2] = {};
    bool free_after_join = true;
    ON_CALL(mock_cplat, cplat_local_lock_create(_))
        .WillByDefault([&](cplat_local_lock **out) { return delegate_real_cplat_local_lock_create(out); });
    ON_CALL(mock_cplat, cplat_malloc(_))
        .WillByDefault(
            [&](size_t size) -> void *
            {
                allocations++;

                return malloc(size);
            });
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn, void *)
            {
                if (creates + 1 == 2)
                {
                    return CPLAT_ERR_UNKNOWN;
                }
                *out = reinterpret_cast<cplat_thread *>(&tokens[creates++]);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_thread_join(_))
        .WillByDefault(
            [&](cplat_thread *)
            {
                joins++;
            });
    ON_CALL(mock_cplat, cplat_free(_))
        .WillByDefault(
            [&](void *p)
            {
                if (p != nullptr)
                {
                    frees++;
                    free_after_join = free_after_join && joins == creates;
                    free(p);
                }
            }); // [状態] - 生成成功数、終了待ち数、解放数と順序を記録する。
    // Act
    int actual_ret = potr_internal_connect_thread_start(&ctx); // [手順] - TCP の開始を指定した生成点で失敗させる。
    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret);
    // [確認_異常系] - 最初の生成失敗に対応する分類を返すこと。
    EXPECT_EQ(creates, joins); // [確認_異常系] - 起動成功分だけを終了待ちすること。
    EXPECT_EQ(2, frees);
    // [確認_異常系] - 確保成功した先読みバッファーだけを解放すること。
    EXPECT_TRUE(free_after_join);                    // [確認_異常系] - 終了待ち完了前にバッファーを解放しないこと。
    EXPECT_EQ(nullptr, ctx.session_establish_mutex); // [確認_異常系] - 受信者用ロックを残さないこと。
    EXPECT_EQ(nullptr, ctx.connect_thread[0]);       // [確認_異常系] - 起動成功したハンドルを回収すること。
    EXPECT_EQ(nullptr, ctx.connect_thread[1]);       // [確認_異常系] - 未生成のハンドルを残さないこと。
    delegate_real_cplat_condvar_dispose(ctx.tcp_state_cv);
    delegate_real_cplat_local_lock_dispose(ctx.tcp_state_mutex);
}

// 自然終了済み connect と受信者用準備資源を回収し、二度目は join しないことの確認
TEST(potrConnectThreadTest, stop_joins_finished_thread_and_disposes_preparation)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.n_path = 2;
    ctx.global.max_payload = 64;
    for (int i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        ctx.sock[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
    }
    int token = 0;
    ctx.connect_thread[0] = reinterpret_cast<cplat_thread *>(&token);
    delegate_real_cplat_local_lock_create(&ctx.tcp_state_mutex);
    delegate_real_cplat_condvar_create(&ctx.tcp_state_cv);
    delegate_real_cplat_local_lock_create(&ctx.session_establish_mutex);
    ctx.tcp_first_pkt_buf[0] = (uint8_t *)malloc(POTR_PACKET_HEADER_SIZE + 64);
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_thread_join(ctx.connect_thread[0])).WillOnce(Return());
    // [Pre-Assert確認_正常系] - 稼働フラグが 0 の生成済み connect を 1 回だけ join すること。
    // [Pre-Assert手順] - join 成功を返す。
    // Act
    potr_internal_connect_thread_stop(&ctx); // [手順] - 自然終了済み connect の終了を待つ。
    potr_internal_connect_thread_stop(&ctx); // [手順] - 再度停止する。
    // Assert
    EXPECT_EQ(nullptr, ctx.connect_thread[0]);       // [確認_正常系] - ハンドルを NULL にすること。
    EXPECT_EQ(nullptr, ctx.tcp_first_pkt_buf[0]);    // [確認_正常系] - 先読みバッファーを解放すること。
    EXPECT_EQ(nullptr, ctx.session_establish_mutex); // [確認_正常系] - 受信者用ロックを解放すること。
    delegate_real_cplat_condvar_dispose(ctx.tcp_state_cv);
    delegate_real_cplat_local_lock_dispose(ctx.tcp_state_mutex);
}

TEST(potrConnectThreadTest, sender_calls_preparation_without_publishing_failed_socket)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_SENDER;
    ctx.n_path = 1;
    ctx.tcp_conn_fd[0] = CPLAT_INVALID_SOCKET;
    cplat_thread_fn entry = nullptr;
    void *entry_arg = nullptr;
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn fn, void *arg)
            {
                *out = nullptr;
                entry = fn;
                entry_arg = arg;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_socket_wait_cancel_signal(_, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_wait_cancel_dispose(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_socket_watch_detach(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_socket_close(_)).WillByDefault(Return());
    // [状態] - 管理スレッド本体を実スレッドを生成せずに捕捉する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, thread_connect_with_timeout(&ctx, 0, _, _))
        .WillOnce(
            [&](potr_context *, int, cplat_socket_wait_cancel *, cplat_socket_watch **)
            {
                cplat_atomic_store_i32(&ctx.connect_thread_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_INVALID_SOCKET;
            }); // [Pre-Assert確認_正常系] - 接続準備を 1 回呼ぶこと。
    // [Pre-Assert手順] - 失敗と停止要求を返す。
    // Act
    int actual_ret = potr_internal_connect_thread_start(&ctx); // [手順] - 管理処理を準備する。
    ASSERT_NE(nullptr, entry);                                 // [状態確認] - スレッド本体を捕捉できたこと。
    entry(entry_arg);                                          // [手順] - 管理ループを同期実行する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);                      // [確認_正常系] - 管理処理の準備に成功すること。
    EXPECT_EQ(CPLAT_INVALID_SOCKET, ctx.tcp_conn_fd[0]); // [確認_異常系] - 接続失敗ソケットを登録しないこと。
}

// 再接続の待機に失敗した場合に ERROR を出してスレッドを続けることの確認
TEST(potrConnectThreadTest, reconnect_wait_failure_traces_error)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_SENDER;
    ctx.service.service_id = 11;
    ctx.service.reconnect_interval_ms = 100;
    ctx.n_path = 1;
    ctx.tcp_conn_fd[0] = CPLAT_INVALID_SOCKET;
    cplat_thread_fn entry = nullptr;
    void *entry_arg = nullptr;
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn fn, void *arg)
            {
                *out = nullptr;
                entry = fn;
                entry_arg = arg;
                return CPLAT_OK;
            }); // [状態] - cplat_thread_create() から、スレッド本体を捕捉して CPLAT_OK を返すように既定動作を設定する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_))
        .WillByDefault(Return()); // [状態] - cplat_local_lock_lock() が何もしないように既定動作を設定する。
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_))
        .WillByDefault(Return()); // [状態] - cplat_local_lock_unlock() が何もしないように既定動作を設定する。
    // [状態] - 管理スレッド本体を実スレッドを生成せずに捕捉する。
    ON_CALL(mock_porter, thread_connect_with_timeout(&ctx, 0, _, _))
        .WillByDefault(Return(CPLAT_INVALID_SOCKET)); // [状態] - thread_connect_with_timeout() から CPLAT_INVALID_SOCKET を返すように既定動作を設定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_condvar_timed_wait(_, _, 100))
        .WillOnce(
            [&](cplat_condvar *, cplat_local_lock *, int)
            {
                cplat_atomic_store_i32(&ctx.connect_thread_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_ERR_INVALID_ARGUMENT;
            }); // [Pre-Assert確認_異常系] - cplat_condvar_timed_wait() が 100 を指定して 1 回呼び出されること。
                // [Pre-Assert手順] - ctx.connect_thread_running[0] を 0 にし、cplat_condvar_timed_wait() から CPLAT_ERR_INVALID_ARGUMENT を返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _))
        .Times(AnyNumber()); // [Pre-Assert確認_正常系] - cplat_tracer_writef_at() が任意の回数呼び出されること。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_ERROR, nullptr,
                                                   HasSubstr("reconnect wait failed (result=")))
        .Times(1); // [Pre-Assert確認_異常系] - cplat_tracer_writef_at() が CPLAT_TRACE_LEVEL_ERROR、nullptr、"reconnect wait failed (result=" を含む文字列を指定して 1 回呼び出されること。
    // Act
    int actual_ret = potr_internal_connect_thread_start(&ctx); // [手順] - 管理処理を準備する。
    ASSERT_NE(nullptr, entry);                                 // [状態確認] - スレッド本体を捕捉できたこと。
    entry(entry_arg);                                          // [手順] - 管理ループを同期実行する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 管理処理の準備に成功すること。
}

// 再接続の待機のタイムアウトでは ERROR を出さないことの確認
TEST(potrConnectThreadTest, reconnect_wait_timeout_does_not_trace_error)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_SENDER;
    ctx.service.service_id = 11;
    ctx.service.reconnect_interval_ms = 100;
    ctx.n_path = 1;
    ctx.tcp_conn_fd[0] = CPLAT_INVALID_SOCKET;
    cplat_thread_fn entry = nullptr;
    void *entry_arg = nullptr;
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn fn, void *arg)
            {
                *out = nullptr;
                entry = fn;
                entry_arg = arg;
                return CPLAT_OK;
            }); // [状態] - cplat_thread_create() から、スレッド本体を捕捉して CPLAT_OK を返すように既定動作を設定する。
    ON_CALL(mock_cplat, cplat_local_lock_lock(_))
        .WillByDefault(Return()); // [状態] - cplat_local_lock_lock() が何もしないように既定動作を設定する。
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_))
        .WillByDefault(Return()); // [状態] - cplat_local_lock_unlock() が何もしないように既定動作を設定する。
    // [状態] - 管理スレッド本体を実スレッドを生成せずに捕捉する。
    ON_CALL(mock_porter, thread_connect_with_timeout(&ctx, 0, _, _))
        .WillByDefault(Return(CPLAT_INVALID_SOCKET)); // [状態] - thread_connect_with_timeout() から CPLAT_INVALID_SOCKET を返すように既定動作を設定する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_condvar_timed_wait(_, _, 100))
        .WillOnce(
            [&](cplat_condvar *, cplat_local_lock *, int)
            {
                cplat_atomic_store_i32(&ctx.connect_thread_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_ERR_TIMEOUT;
            }); // [Pre-Assert確認_正常系] - cplat_condvar_timed_wait() が 100 を指定して 1 回呼び出されること。
                // [Pre-Assert手順] - ctx.connect_thread_running[0] を 0 にし、cplat_condvar_timed_wait() から CPLAT_ERR_TIMEOUT を返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _))
        .Times(AnyNumber()); // [Pre-Assert確認_正常系] - cplat_tracer_writef_at() が任意の回数呼び出されること。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_ERROR, _, _))
        .Times(0); // [Pre-Assert確認_正常系] - CPLAT_TRACE_LEVEL_ERROR を指定した cplat_tracer_writef_at() の呼び出しがないこと。
    // Act
    int actual_ret = potr_internal_connect_thread_start(&ctx); // [手順] - 管理処理を準備する。
    ASSERT_NE(nullptr, entry);                                 // [状態確認] - スレッド本体を捕捉できたこと。
    entry(entry_arg);                                          // [手順] - 管理ループを同期実行する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 管理処理の準備に成功すること。
}

// TCP と TCP_BIDIR の受け入れが、先読みの前に TCP_NODELAY を要求し、失敗しても続行することの確認
TEST(potrConnectThreadTest, receiver_sets_nodelay_before_first_packet)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.service.service_id = 11;
    ctx.n_path = 1;
    ctx.global.max_payload = 64;
    for (int i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        ctx.sock[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
    }
    int listen_watch_tok = 0;
    int conn_watch_tok = 0;
    int connect_cancel_tok = 0;
    ctx.tcp_listen_sock[0] = static_cast<cplat_socket>(5);
    ctx.tcp_listen_watch[0] = reinterpret_cast<cplat_socket_watch *>(&listen_watch_tok);
    ctx.tcp_connect_cancel[0] = reinterpret_cast<cplat_socket_wait_cancel *>(&connect_cancel_tok);
    cplat_socket_watch *conn_watch = reinterpret_cast<cplat_socket_watch *>(&conn_watch_tok);
    cplat_thread_fn entry = nullptr;
    void *entry_arg = nullptr;
    int accepts = 0;
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn fn, void *arg)
            {
                *out = nullptr;
                entry = fn;
                entry_arg = arg;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_watch_wait_readable_multi(_, 1U, ctx.tcp_connect_cancel[0], _, _, _))
        .WillByDefault(
            [&](cplat_socket_watch *const *, size_t, cplat_socket_wait_cancel *, int, unsigned char *ready,
                cplat_error *)
            {
                if (accepts == 0)
                {
                    ready[0] = 1U;
                    return CPLAT_OK;
                }
                cplat_atomic_store_i32(&ctx.connect_thread_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_ERR_CANCELED;
            });
    ON_CALL(mock_cplat, cplat_socket_accept(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, cplat_ipv4_endpoint *peer, cplat_socket *out, cplat_error *)
            {
                accepts++;
                peer->address = 1;
                peer->port = 1;
                *out = static_cast<cplat_socket>(7);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_wait_cancel_signal(_, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 双方向の受け入れで、最初の 1 接続だけを成功させ、TCP_NODELAY は失敗させる。
    // Pre-Assert
    cplat_error nodelay_failure = {};
    nodelay_failure.result = CPLAT_ERR_UNKNOWN;
#if defined(PLATFORM_LINUX)
    nodelay_failure.domain = CPLAT_ERROR_DOMAIN_SOCKET_ERRNO;
    nodelay_failure.code = (unsigned long)ENOPROTOOPT;
#elif defined(PLATFORM_WINDOWS)
    nodelay_failure.domain = CPLAT_ERROR_DOMAIN_WINSOCK;
    nodelay_failure.code = (unsigned long)WSAENOPROTOOPT;
#endif
    {
        InSequence order;
        EXPECT_CALL(mock_cplat, cplat_socket_watch_attach(static_cast<cplat_socket>(7), _, _))
            .WillOnce(DoAll(SetArgPointee<1>(conn_watch), Return(CPLAT_OK)));
        EXPECT_CALL(mock_cplat, cplat_socket_set_nodelay(static_cast<cplat_socket>(7), 1, _))
            .WillOnce(DoAll(SetArgPointee<2>(nodelay_failure), Return(CPLAT_ERR_UNKNOWN)));
        // [Pre-Assert確認_異常系] - attach 後に先読みの前で TCP_NODELAY の有効化を 1 回要求すること。
        EXPECT_CALL(mock_porter, thread_connect_read_first_packet(static_cast<cplat_socket>(7), conn_watch,
                                                                  ctx.tcp_connect_cancel[0], _, _, _, _))
            .WillOnce(Return(POTR_ERR_UNKNOWN));
        // [Pre-Assert確認_正常系] - TCP_NODELAY の失敗後も先読みへ進むこと。
        EXPECT_CALL(mock_cplat, cplat_socket_watch_detach(conn_watch)).WillOnce(Return());
        EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(7))).WillOnce(Return());
        // [Pre-Assert確認_異常系] - 先読みの失敗後に watch を detach してからソケットを閉じること。
    }
    // Act
    int actual_ret = potr_internal_connect_thread_start(&ctx); // [手順] - 受け入れ処理を準備する。
    ASSERT_NE(nullptr, entry);                                 // [状態確認] - スレッド本体を捕捉できたこと。
    entry(entry_arg);                                          // [手順] - 受け入れループを同期実行する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);                      // [確認_正常系] - 受け入れ処理の準備に成功すること。
    EXPECT_EQ(CPLAT_INVALID_SOCKET, ctx.tcp_conn_fd[0]); // [確認_異常系] - 先読みに失敗したソケットを登録しないこと。
    potr_internal_connect_thread_stop(&ctx);
}

// TCP 再接続時に、前回の接続で受けた上限なしと抑制要求を未通知・要求なしへ戻すことの確認
TEST(potrConnectThreadTest, tcp_reconnect_resets_flow_control_to_unnotified)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_SENDER;
    ctx.service.type = POTR_TYPE_TCP_BIDIR;
    ctx.service.service_id = 12;
    ctx.service.reconnect_interval_ms = 1U;
    ctx.n_path = 1;
    for (int i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        ctx.sock[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
    }
    cplat_atomic_store_u64(&ctx.flow_send_limit, POTR_FLOW_SEND_LIMIT_UNLIMITED, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_u64(&ctx.flow_request_expire_ms, 9999U, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_thread_fn entry = nullptr;
    void *entry_arg = nullptr;
    int connect_count = 0;
    uint64_t limit_at_first_connect = 0U;
    uint64_t expire_at_first_connect = 0U;
    uint64_t limit_at_reconnect = 0U;
    uint64_t expire_at_reconnect = 0U;
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn fn, void *arg)
            {
                *out = nullptr;
                entry = fn;
                entry_arg = arg;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_))
        .WillByDefault(DoAll(SetArgPointee<0>(1000U), Return(CPLAT_OK)));
    int watch_toks[2] = {};
    int cancel_toks[2] = {};
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_socket_wait_cancel_signal(_, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_wait_cancel_dispose(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_socket_watch_detach(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_socket_close(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_socket_wait_cancel_create(_, _))
        .WillByDefault(
            [&](cplat_socket_wait_cancel **out, cplat_error *)
            {
                *out = reinterpret_cast<cplat_socket_wait_cancel *>(&cancel_toks[connect_count > 0 ? connect_count - 1 : 0]);
                return CPLAT_OK;
            });
    ON_CALL(mock_porter, thread_connect_with_timeout(_, _, _, _))
        .WillByDefault(
            [&](potr_context *, int, cplat_socket_wait_cancel *, cplat_socket_watch **watch_out) -> cplat_socket
            {
                connect_count++;
                if (connect_count <= 2)
                {
                    *watch_out = reinterpret_cast<cplat_socket_watch *>(&watch_toks[connect_count - 1]);
                    return static_cast<cplat_socket>(7 + connect_count);
                }
                cplat_atomic_store_i32(&ctx.connect_thread_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_INVALID_SOCKET;
            });
    ON_CALL(mock_porter, potr_internal_certificate_connect(_, _, _, _, _)).WillByDefault(Return(POTR_OK));
    ON_CALL(mock_porter, potr_internal_start_connected_threads(_, _, _, _))
        .WillByDefault(
            [&](potr_context *c, int, uint64_t, const potr_internal_connected_threads_ops *)
            {
                if (connect_count == 1)
                {
                    limit_at_first_connect = cplat_atomic_load_u64(&c->flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
                    expire_at_first_connect =
                        cplat_atomic_load_u64(&c->flow_request_expire_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
                    /* 1 回目の接続中に「上限なし」と抑制要求を受けた状態を模擬する。 */
                    cplat_atomic_store_u64(&c->flow_send_limit, POTR_FLOW_SEND_LIMIT_UNLIMITED,
                                           CPLAT_MEMORY_ORDER_RELEASE);
                    cplat_atomic_store_u64(&c->flow_request_expire_ms, 8888U, CPLAT_MEMORY_ORDER_RELEASE);
                }
                else if (connect_count == 2)
                {
                    limit_at_reconnect = cplat_atomic_load_u64(&c->flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
                    expire_at_reconnect = cplat_atomic_load_u64(&c->flow_request_expire_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
                    cplat_atomic_store_i32(&c->connect_thread_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                }
                return POTR_OK;
            });
    ON_CALL(mock_cplat, cplat_condvar_timed_wait(_, _, _)).WillByDefault(Return(CPLAT_ERR_TIMEOUT));
    // [状態] - TCP_BIDIR SENDER で 1 回目の接続中に上限なしと抑制要求を受け、切断後に再接続する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_connect_thread_start(&ctx); // [手順] - 接続管理処理を準備する。
    ASSERT_NE(nullptr, entry);                                 // [状態確認] - スレッド本体を捕捉できたこと。
    entry(entry_arg);                                          // [手順] - 1 回目の接続、切断、再接続を同期実行する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 接続管理処理の準備に成功すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNNOTIFIED, limit_at_first_connect);
    // [確認_正常系] - 初回接続の公開前に未通知へ初期化されること。
    EXPECT_EQ(0U, expire_at_first_connect); // [確認_正常系] - 初回接続の公開前に抑制要求なしへ初期化されること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNNOTIFIED, limit_at_reconnect);
    // [確認_正常系] - 切断後の再接続では未通知から始まること。
    EXPECT_EQ(0U, expire_at_reconnect); // [確認_正常系] - 切断後の再接続では抑制要求なしから始まること。
    potr_internal_connect_thread_stop(&ctx);
}

// TCP 再接続時に、保持していた送出上限と抑制要求を破棄して未通知 (初期の幅) と要求なしへ戻すことの確認
TEST(potrConnectThreadTest, tcp_reconnect_discards_held_send_limit_and_suppression)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_SENDER;
    ctx.service.type = POTR_TYPE_TCP;
    ctx.service.service_id = 13;
    ctx.service.reconnect_interval_ms = 1U;
    ctx.n_path = 1;
    for (int i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        ctx.sock[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
    }
    const uint64_t kHeldSendLimit = POTR_FLOW_SEND_LIMIT_HELD | 120U;
    cplat_atomic_store_u64(&ctx.flow_send_limit, kHeldSendLimit, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_atomic_store_u64(&ctx.flow_request_expire_ms, 9999U, CPLAT_MEMORY_ORDER_RELEASE);
    cplat_thread_fn entry = nullptr;
    void *entry_arg = nullptr;
    int connect_count = 0;
    uint64_t limit_at_first_connect = 0U;
    uint64_t expire_at_first_connect = 0U;
    uint64_t limit_at_reconnect = 0U;
    uint64_t expire_at_reconnect = 0U;
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn fn, void *arg)
            {
                *out = nullptr;
                entry = fn;
                entry_arg = arg;
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_clock_get_monotonic_ms(_))
        .WillByDefault(DoAll(SetArgPointee<0>(1000U), Return(CPLAT_OK)));
    int watch_toks[2] = {};
    int cancel_toks[2] = {};
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_socket_wait_cancel_signal(_, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_wait_cancel_dispose(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_socket_watch_detach(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_socket_close(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_socket_wait_cancel_create(_, _))
        .WillByDefault(
            [&](cplat_socket_wait_cancel **out, cplat_error *)
            {
                *out = reinterpret_cast<cplat_socket_wait_cancel *>(&cancel_toks[connect_count > 0 ? connect_count - 1 : 0]);
                return CPLAT_OK;
            });
    ON_CALL(mock_porter, thread_connect_with_timeout(_, _, _, _))
        .WillByDefault(
            [&](potr_context *, int, cplat_socket_wait_cancel *, cplat_socket_watch **watch_out) -> cplat_socket
            {
                connect_count++;
                if (connect_count <= 2)
                {
                    *watch_out = reinterpret_cast<cplat_socket_watch *>(&watch_toks[connect_count - 1]);
                    return static_cast<cplat_socket>(7 + connect_count);
                }
                cplat_atomic_store_i32(&ctx.connect_thread_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_INVALID_SOCKET;
            });
    ON_CALL(mock_porter, potr_internal_certificate_connect(_, _, _, _, _)).WillByDefault(Return(POTR_OK));
    ON_CALL(mock_porter, potr_internal_start_connected_threads(_, _, _, _))
        .WillByDefault(
            [&](potr_context *c, int, uint64_t, const potr_internal_connected_threads_ops *)
            {
                if (connect_count == 1)
                {
                    limit_at_first_connect = cplat_atomic_load_u64(&c->flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
                    expire_at_first_connect =
                        cplat_atomic_load_u64(&c->flow_request_expire_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
                    /* 1 回目の接続中に送出上限 (120) と抑制要求を保持した状態を模擬する。 */
                    cplat_atomic_store_u64(&c->flow_send_limit, kHeldSendLimit, CPLAT_MEMORY_ORDER_RELEASE);
                    cplat_atomic_store_u64(&c->flow_request_expire_ms, 8888U, CPLAT_MEMORY_ORDER_RELEASE);
                }
                else if (connect_count == 2)
                {
                    limit_at_reconnect = cplat_atomic_load_u64(&c->flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
                    expire_at_reconnect = cplat_atomic_load_u64(&c->flow_request_expire_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
                    cplat_atomic_store_i32(&c->connect_thread_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                }
                return POTR_OK;
            });
    ON_CALL(mock_cplat, cplat_condvar_timed_wait(_, _, _)).WillByDefault(Return(CPLAT_ERR_TIMEOUT));
    // [状態] - TCP SENDER で 1 回目の接続中に保持した送出上限 (HELD | 120) と抑制要求を受け、切断後に再接続する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_connect_thread_start(&ctx); // [手順] - 接続管理処理を準備する。
    ASSERT_NE(nullptr, entry);                                 // [状態確認] - スレッド本体を捕捉できたこと。
    entry(entry_arg);                                          // [手順] - 1 回目の接続、切断、再接続を同期実行する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 接続管理処理の準備に成功すること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNNOTIFIED, limit_at_first_connect);
    // [確認_正常系] - 初回接続の公開前に未通知 (初期の幅) へ初期化されること。
    EXPECT_EQ(0U, expire_at_first_connect); // [確認_正常系] - 初回接続の公開前に抑制要求なしへ初期化されること。
    EXPECT_EQ(POTR_FLOW_SEND_LIMIT_UNNOTIFIED, limit_at_reconnect);
    // [確認_正常系] - 再接続時に保持していた送出上限が破棄され、未通知 (初期の幅) へ戻ること。
    EXPECT_EQ(0U, expire_at_reconnect); // [確認_正常系] - 再接続時に保持していた抑制要求が破棄されること。
    potr_internal_connect_thread_stop(&ctx);
}

// 接続の回収順序 (request_close -> recv join -> health stop -> release_conn) と、新セッションが他経路のソケットを直接閉じないことの確認
TEST(potrConnectThreadTest, receiver_new_session_requests_close_on_other_path_and_releases_own_in_order)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.role = POTR_ROLE_RECEIVER;
    ctx.service.type = POTR_TYPE_TCP;
    ctx.service.service_id = 15;
    ctx.n_path = 2;
    ctx.global.max_payload = 64;
    for (int i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        ctx.sock[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
    }
    int listen_watch_tok = 0;
    int conn_watch_tok = 0;
    int connect_cancel_tok = 0;
    int conn_cancel_tok = 0;
    int lifecycle_lock1_tok = 0;
    ctx.tcp_listen_sock[0] = static_cast<cplat_socket>(5);
    ctx.tcp_listen_watch[0] = reinterpret_cast<cplat_socket_watch *>(&listen_watch_tok);
    ctx.tcp_connect_cancel[0] = reinterpret_cast<cplat_socket_wait_cancel *>(&connect_cancel_tok);
    ctx.tcp_lifecycle_mutex[1] = reinterpret_cast<cplat_local_lock *>(&lifecycle_lock1_tok);
    ctx.tcp_conn_fd[1] = static_cast<cplat_socket>(99);
    ctx.tcp_conn_generation[1] = 42U;
    cplat_socket_watch *conn_watch = reinterpret_cast<cplat_socket_watch *>(&conn_watch_tok);
    cplat_socket_wait_cancel *conn_cancel = reinterpret_cast<cplat_socket_wait_cancel *>(&conn_cancel_tok);

    cplat_thread_fn entry = nullptr;
    void *entry_arg = nullptr;
    int accepts = 0;
    ON_CALL(mock_cplat, cplat_local_lock_lock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_local_lock_unlock(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_socket_wait_cancel_signal(_, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_cplat, cplat_socket_wait_cancel_dispose(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_socket_watch_detach(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_socket_close(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn fn, void *arg)
            {
                *out = nullptr;
                if (entry == nullptr)
                {
                    entry = fn;
                    entry_arg = arg;
                }
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_watch_wait_readable_multi(_, 1U, ctx.tcp_connect_cancel[0], _, _, _))
        .WillByDefault(
            [&](cplat_socket_watch *const *, size_t, cplat_socket_wait_cancel *, int, unsigned char *ready,
                cplat_error *)
            {
                if (accepts == 0)
                {
                    ready[0] = 1U;
                    return CPLAT_OK;
                }
                cplat_atomic_store_i32(&ctx.connect_thread_running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
                return CPLAT_ERR_CANCELED;
            });
    ON_CALL(mock_cplat, cplat_socket_accept(_, _, _, _))
        .WillByDefault(
            [&](cplat_socket, cplat_ipv4_endpoint *peer, cplat_socket *out, cplat_error *)
            {
                accepts++;
                peer->address = 1;
                peer->port = 1;
                *out = static_cast<cplat_socket>(7);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_socket_watch_attach(static_cast<cplat_socket>(7), _, _))
        .WillByDefault(DoAll(SetArgPointee<1>(conn_watch), Return(CPLAT_OK)));
    ON_CALL(mock_cplat, cplat_socket_set_nodelay(_, _, _)).WillByDefault(Return(CPLAT_OK));
    ON_CALL(mock_porter, thread_connect_peer_allowed(_, _, _)).WillByDefault(Return(1));
    ON_CALL(mock_porter, thread_connect_read_first_packet(_, _, _, _, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<5>(POTR_PACKET_HEADER_SIZE), Return(POTR_OK)));
    potr_packet first_pkt = {};
    first_pkt.service_id = 15;
    first_pkt.session_id = 100U;
    ON_CALL(mock_porter, potr_internal_packet_parse(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<0>(first_pkt), Return(POTR_OK)));
    ON_CALL(mock_porter, potr_internal_certificate_connect(_, _, _, _, _)).WillByDefault(Return(POTR_OK));
    ON_CALL(mock_porter, thread_connect_session_compare(_, _)).WillByDefault(Return(TCP_SESSION_NEW));
    ON_CALL(mock_cplat, cplat_socket_wait_cancel_create(_, _))
        .WillByDefault(DoAll(SetArgPointee<0>(conn_cancel), Return(CPLAT_OK)));
    ON_CALL(mock_porter, potr_internal_start_connected_threads(_, 0, 1U, _)).WillByDefault(Return(POTR_OK));

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close(static_cast<cplat_socket>(99))).Times(0);
    // [Pre-Assert確認_正常系] - 新セッション受け付け時に他経路 (path 1) のソケットを直接閉じないこと。
    {
        InSequence seq;
        EXPECT_CALL(mock_porter, potr_internal_tcp_conn_request_close(&ctx, 1, 42U)).Times(1);
        EXPECT_CALL(mock_porter, potr_internal_tcp_conn_request_close(&ctx, 0, 1U)).Times(1);
        EXPECT_CALL(mock_porter, potr_internal_tcp_health_thread_stop(&ctx, 0)).Times(1);
        EXPECT_CALL(mock_porter, potr_internal_tcp_conn_release(&ctx, 0, 1U)).Times(1);
        EXPECT_CALL(mock_porter, potr_internal_tcp_health_thread_stop(&ctx, _)).Times(AnyNumber());
    }

    // Act
    ASSERT_EQ(POTR_OK, potr_internal_connect_thread_start(&ctx));
    ASSERT_NE(nullptr, entry);
    entry(entry_arg);
    potr_internal_connect_thread_stop(&ctx);
}
