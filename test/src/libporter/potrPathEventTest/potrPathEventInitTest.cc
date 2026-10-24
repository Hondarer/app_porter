#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <porter/porter_result.h>
#include <porter/potr_path_event.h>

using namespace testing;

// コールバック用ロックの生成失敗を呼び出し元へ返すことの確認
TEST(potrPathEventInitTest, callback_lock_failure_is_reported)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    ON_CALL(mock_cplat, cplat_local_lock_create(_)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    // Act
    int actual_ret = potr_internal_callback_mutex_init(&ctx); // [手順] - コールバック用ロックの生成を失敗させる。
    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret); // [確認_異常系] - 生成失敗を返すこと。
    EXPECT_EQ(nullptr, ctx.callback_mutex);  // [確認_異常系] - 未生成状態を保持すること。
}

// コールバック用ロックを一度だけ解放し、所有権を戻すことの確認
TEST(potrPathEventInitTest, callback_dispose_clears_ownership)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    (void)potr_internal_callback_mutex_init(&ctx);
    // Act
    potr_internal_callback_mutex_dispose(&ctx); // [手順] - ロックを破棄する。
    potr_internal_callback_mutex_dispose(&ctx); // [手順] - 再度破棄を呼ぶ。
    // Assert
    EXPECT_EQ(nullptr, ctx.callback_mutex); // [確認_正常系] - 解放後のロックを NULL にすること。
}
