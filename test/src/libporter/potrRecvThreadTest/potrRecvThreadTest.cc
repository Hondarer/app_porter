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

// UDP recv の生成失敗で状態を戻すことの確認
TEST(potrRecvThreadTest, udp_create_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_thread_join(_, _)).Times(0);
    // [Pre-Assert確認_異常系] - 未生成のスレッドを join しないこと。

    // Act
    int actual_ret = potr_internal_comm_recv_thread_start(&ctx); // [手順] - recv の開始を失敗させる。

    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret); // [確認_異常系] - 生成失敗を返すこと。
    EXPECT_EQ(nullptr, ctx.recv_thread[0]);  // [確認_異常系] - ハンドルが NULL であること。
}

// UDP recv の自然終了したスレッドを一度だけ回収することの確認
TEST(potrRecvThreadTest, udp_stop_joins_finished_thread_once)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int token = 0;
    ctx.recv_thread[0] = reinterpret_cast<cplat_thread *>(&token);
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_thread_join(ctx.recv_thread[0], CPLAT_SYNC_WAIT_FOREVER)).WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - 生成済み recv を 1 回だけ join すること。
    // [Pre-Assert手順] - join 成功を返す。
    // Act
    int actual_ret = potr_internal_comm_recv_thread_stop(&ctx); // [手順] - recv の終了を待つ。
    (void)potr_internal_comm_recv_thread_stop(&ctx);            // [手順] - 停止済み recv を再度停止する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);         // [確認_正常系] - 停止成功を返すこと。
    EXPECT_EQ(nullptr, ctx.recv_thread[0]); // [確認_正常系] - ハンドルが NULL であること。
}

// TCP recv の生成失敗で状態を戻すことの確認
TEST(potrRecvThreadTest, tcp_create_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ON_CALL(mock_cplat, cplat_thread_create(_, _, _)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_thread_join(_, _)).Times(0);
    // [Pre-Assert確認_異常系] - 未生成のスレッドを join しないこと。

    // Act
    int actual_ret = potr_internal_tcp_recv_thread_start(&ctx, 0); // [手順] - recv の開始を失敗させる。

    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret); // [確認_異常系] - 生成失敗を返すこと。
    EXPECT_EQ(nullptr, ctx.recv_thread[0]);  // [確認_異常系] - ハンドルが NULL であること。
}

// TCP recv の自然終了したスレッドを一度だけ回収することの確認
TEST(potrRecvThreadTest, tcp_stop_joins_finished_thread_once)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    int token = 0;
    ctx.recv_thread[0] = reinterpret_cast<cplat_thread *>(&token);
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_thread_join(ctx.recv_thread[0], CPLAT_SYNC_WAIT_FOREVER)).WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - 生成済み recv を 1 回だけ join すること。
    // [Pre-Assert手順] - join 成功を返す。
    // Act
    int actual_ret = potr_internal_tcp_recv_thread_stop(&ctx, 0); // [手順] - recv の終了を待つ。
    (void)potr_internal_tcp_recv_thread_stop(&ctx, 0);            // [手順] - 停止済み recv を再度停止する。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);         // [確認_正常系] - 停止成功を返すこと。
    EXPECT_EQ(nullptr, ctx.recv_thread[0]); // [確認_正常系] - ハンドルが NULL であること。
}
