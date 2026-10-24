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
    ON_CALL(mock_cplat, cplat_thread_join(_, _))
        .WillByDefault(
            [&](cplat_thread *, int)
            {
                joins++;
                return CPLAT_OK;
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
                    return nullptr;
                return malloc(size);
            });
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn, void *)
            {
                *out = reinterpret_cast<cplat_thread *>(&tokens[creates++]);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_thread_join(_, _))
        .WillByDefault(
            [&](cplat_thread *, int)
            {
                joins++;
                return CPLAT_OK;
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
                    return nullptr;
                return malloc(size);
            });
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _))
        .WillByDefault(
            [&](cplat_thread **out, cplat_thread_fn, void *)
            {
                *out = reinterpret_cast<cplat_thread *>(&tokens[creates++]);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_thread_join(_, _))
        .WillByDefault(
            [&](cplat_thread *, int)
            {
                joins++;
                return CPLAT_OK;
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
                    return CPLAT_ERR_UNKNOWN;
                *out = reinterpret_cast<cplat_thread *>(&tokens[creates++]);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_thread_join(_, _))
        .WillByDefault(
            [&](cplat_thread *, int)
            {
                joins++;
                return CPLAT_OK;
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
                    return CPLAT_ERR_UNKNOWN;
                *out = reinterpret_cast<cplat_thread *>(&tokens[creates++]);
                return CPLAT_OK;
            });
    ON_CALL(mock_cplat, cplat_thread_join(_, _))
        .WillByDefault(
            [&](cplat_thread *, int)
            {
                joins++;
                return CPLAT_OK;
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
    EXPECT_CALL(mock_cplat, cplat_thread_join(ctx.connect_thread[0], CPLAT_SYNC_WAIT_FOREVER))
        .WillOnce(Return(CPLAT_OK));
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
