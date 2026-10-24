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

// N:1 ピアテーブルの allocation 失敗時にテーブルを残さないことの確認
TEST(potrPeerTableTest, allocation_failure_does_not_retain_table)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.max_peers = 2;
    ON_CALL(mock_cplat, cplat_calloc(_, _)).WillByDefault(Return(nullptr));
    // Act
    int actual_ret = potr_internal_peer_table_init(&ctx); // [手順] - ピアテーブルの初期化を失敗させる。
    // Assert
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, actual_ret);
    // [確認_異常系] - 失敗原因に対応した分類を返すこと。
    EXPECT_EQ(nullptr, ctx.peers);       // [確認_異常系] - テーブルを残さないこと。
    EXPECT_EQ(nullptr, ctx.peers_mutex); // [確認_異常系] - ロックを残さないこと。
}

// N:1 ピアテーブルの mutex 失敗時にテーブルを残さないことの確認
TEST(potrPeerTableTest, mutex_failure_does_not_retain_table)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.max_peers = 2;
    ON_CALL(mock_cplat, cplat_local_lock_create(_)).WillByDefault(Return(CPLAT_ERR_UNKNOWN));
    // Act
    int actual_ret = potr_internal_peer_table_init(&ctx); // [手順] - ピアテーブルの初期化を失敗させる。
    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret);
    // [確認_異常系] - 失敗原因に対応した分類を返すこと。
    EXPECT_EQ(nullptr, ctx.peers);       // [確認_異常系] - テーブルを残さないこと。
    EXPECT_EQ(nullptr, ctx.peers_mutex); // [確認_異常系] - ロックを残さないこと。
}

// N:1 テーブルの通常破棄で所有ポインターを未生成状態へ戻すことの確認
TEST(potrPeerTableTest, dispose_clears_ownership)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.max_peers = 2;
    (void)potr_internal_peer_table_init(&ctx);
    // Act
    potr_internal_peer_table_dispose(&ctx); // [手順] - 初期化したテーブルを破棄する。
    potr_internal_peer_table_dispose(&ctx); // [手順] - 再度破棄を呼ぶ。
    // Assert
    EXPECT_EQ(nullptr, ctx.peers);       // [確認_正常系] - テーブルを NULL にすること。
    EXPECT_EQ(nullptr, ctx.peers_mutex); // [確認_正常系] - ロックを NULL にすること。
}
