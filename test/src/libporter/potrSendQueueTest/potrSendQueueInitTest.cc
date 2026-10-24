#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <porter/porter_result.h>
#include <porter/infra/potr_send_queue.h>

using namespace testing;

// 送信キューの同期資源生成 1 回目の失敗で全確保済み資源を回収することの確認
TEST(potrSendQueueInitTest, sync_failure_1_releases_allocations)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_send_queue q = {};
    int attempts = 0;
    int allocated = 0;
    int disposed = 0;
    ON_CALL(mock_cplat, cplat_local_lock_create(_))
        .WillByDefault(
            [&](cplat_local_lock **out)
            {
                if (++attempts == 1)
                    return CPLAT_ERR_UNKNOWN;
                allocated++;
                return delegate_real_cplat_local_lock_create(out);
            });
    ON_CALL(mock_cplat, cplat_condvar_create(_))
        .WillByDefault(
            [&](cplat_condvar **out)
            {
                if (++attempts == 1)
                    return CPLAT_ERR_UNKNOWN;
                allocated++;
                return delegate_real_cplat_condvar_create(out);
            });
    ON_CALL(mock_cplat, cplat_local_lock_dispose(_))
        .WillByDefault(
            [&](cplat_local_lock *p)
            {
                if (p != nullptr)
                    disposed++;
                delegate_real_cplat_local_lock_dispose(p);
            });
    ON_CALL(mock_cplat, cplat_condvar_dispose(_))
        .WillByDefault(
            [&](cplat_condvar *p)
            {
                if (p != nullptr)
                    disposed++;
                delegate_real_cplat_condvar_dispose(p);
            }); // [状態] - 生成成功数と破棄数を記録し、指定回で生成失敗を返す。
    // Act
    int actual_ret = potr_internal_send_queue_init(&q, 2, 64); // [手順] - キューの初期化を失敗させる。
    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret); // [確認_異常系] - 同期資源の生成失敗を返すこと。
    EXPECT_EQ(allocated, disposed);          // [確認_異常系] - 生成成功した同期資源を回収すること。
    EXPECT_EQ(nullptr, q.entries);           // [確認_異常系] - エントリ領域を残さないこと。
    EXPECT_EQ(nullptr, q.payload_pool);      // [確認_異常系] - ペイロード領域を残さないこと。
}

// 送信キューの同期資源生成 2 回目の失敗で全確保済み資源を回収することの確認
TEST(potrSendQueueInitTest, sync_failure_2_releases_allocations)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_send_queue q = {};
    int attempts = 0;
    int allocated = 0;
    int disposed = 0;
    ON_CALL(mock_cplat, cplat_local_lock_create(_))
        .WillByDefault(
            [&](cplat_local_lock **out)
            {
                if (++attempts == 2)
                    return CPLAT_ERR_UNKNOWN;
                allocated++;
                return delegate_real_cplat_local_lock_create(out);
            });
    ON_CALL(mock_cplat, cplat_condvar_create(_))
        .WillByDefault(
            [&](cplat_condvar **out)
            {
                if (++attempts == 2)
                    return CPLAT_ERR_UNKNOWN;
                allocated++;
                return delegate_real_cplat_condvar_create(out);
            });
    ON_CALL(mock_cplat, cplat_local_lock_dispose(_))
        .WillByDefault(
            [&](cplat_local_lock *p)
            {
                if (p != nullptr)
                    disposed++;
                delegate_real_cplat_local_lock_dispose(p);
            });
    ON_CALL(mock_cplat, cplat_condvar_dispose(_))
        .WillByDefault(
            [&](cplat_condvar *p)
            {
                if (p != nullptr)
                    disposed++;
                delegate_real_cplat_condvar_dispose(p);
            }); // [状態] - 生成成功数と破棄数を記録し、指定回で生成失敗を返す。
    // Act
    int actual_ret = potr_internal_send_queue_init(&q, 2, 64); // [手順] - キューの初期化を失敗させる。
    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret); // [確認_異常系] - 同期資源の生成失敗を返すこと。
    EXPECT_EQ(allocated, disposed);          // [確認_異常系] - 生成成功した同期資源を回収すること。
    EXPECT_EQ(nullptr, q.entries);           // [確認_異常系] - エントリ領域を残さないこと。
    EXPECT_EQ(nullptr, q.payload_pool);      // [確認_異常系] - ペイロード領域を残さないこと。
}

// 送信キューの同期資源生成 3 回目の失敗で全確保済み資源を回収することの確認
TEST(potrSendQueueInitTest, sync_failure_3_releases_allocations)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_send_queue q = {};
    int attempts = 0;
    int allocated = 0;
    int disposed = 0;
    ON_CALL(mock_cplat, cplat_local_lock_create(_))
        .WillByDefault(
            [&](cplat_local_lock **out)
            {
                if (++attempts == 3)
                    return CPLAT_ERR_UNKNOWN;
                allocated++;
                return delegate_real_cplat_local_lock_create(out);
            });
    ON_CALL(mock_cplat, cplat_condvar_create(_))
        .WillByDefault(
            [&](cplat_condvar **out)
            {
                if (++attempts == 3)
                    return CPLAT_ERR_UNKNOWN;
                allocated++;
                return delegate_real_cplat_condvar_create(out);
            });
    ON_CALL(mock_cplat, cplat_local_lock_dispose(_))
        .WillByDefault(
            [&](cplat_local_lock *p)
            {
                if (p != nullptr)
                    disposed++;
                delegate_real_cplat_local_lock_dispose(p);
            });
    ON_CALL(mock_cplat, cplat_condvar_dispose(_))
        .WillByDefault(
            [&](cplat_condvar *p)
            {
                if (p != nullptr)
                    disposed++;
                delegate_real_cplat_condvar_dispose(p);
            }); // [状態] - 生成成功数と破棄数を記録し、指定回で生成失敗を返す。
    // Act
    int actual_ret = potr_internal_send_queue_init(&q, 2, 64); // [手順] - キューの初期化を失敗させる。
    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret); // [確認_異常系] - 同期資源の生成失敗を返すこと。
    EXPECT_EQ(allocated, disposed);          // [確認_異常系] - 生成成功した同期資源を回収すること。
    EXPECT_EQ(nullptr, q.entries);           // [確認_異常系] - エントリ領域を残さないこと。
    EXPECT_EQ(nullptr, q.payload_pool);      // [確認_異常系] - ペイロード領域を残さないこと。
}

// 送信キューの同期資源生成 4 回目の失敗で全確保済み資源を回収することの確認
TEST(potrSendQueueInitTest, sync_failure_4_releases_allocations)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_internal_send_queue q = {};
    int attempts = 0;
    int allocated = 0;
    int disposed = 0;
    ON_CALL(mock_cplat, cplat_local_lock_create(_))
        .WillByDefault(
            [&](cplat_local_lock **out)
            {
                if (++attempts == 4)
                    return CPLAT_ERR_UNKNOWN;
                allocated++;
                return delegate_real_cplat_local_lock_create(out);
            });
    ON_CALL(mock_cplat, cplat_condvar_create(_))
        .WillByDefault(
            [&](cplat_condvar **out)
            {
                if (++attempts == 4)
                    return CPLAT_ERR_UNKNOWN;
                allocated++;
                return delegate_real_cplat_condvar_create(out);
            });
    ON_CALL(mock_cplat, cplat_local_lock_dispose(_))
        .WillByDefault(
            [&](cplat_local_lock *p)
            {
                if (p != nullptr)
                    disposed++;
                delegate_real_cplat_local_lock_dispose(p);
            });
    ON_CALL(mock_cplat, cplat_condvar_dispose(_))
        .WillByDefault(
            [&](cplat_condvar *p)
            {
                if (p != nullptr)
                    disposed++;
                delegate_real_cplat_condvar_dispose(p);
            }); // [状態] - 生成成功数と破棄数を記録し、指定回で生成失敗を返す。
    // Act
    int actual_ret = potr_internal_send_queue_init(&q, 2, 64); // [手順] - キューの初期化を失敗させる。
    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret); // [確認_異常系] - 同期資源の生成失敗を返すこと。
    EXPECT_EQ(allocated, disposed);          // [確認_異常系] - 生成成功した同期資源を回収すること。
    EXPECT_EQ(nullptr, q.entries);           // [確認_異常系] - エントリ領域を残さないこと。
    EXPECT_EQ(nullptr, q.payload_pool);      // [確認_異常系] - ペイロード領域を残さないこと。
}
