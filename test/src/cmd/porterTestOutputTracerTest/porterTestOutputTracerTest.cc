#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <mock_stdio.h>
#include <porter/porter_result.h>
#include "porter-test.inject.h"

using namespace testing;

// トレーサーを取得できない間は開始処理を行わず、次回の呼び出しで開始を再試行することの確認
TEST(porterTestOutputTracerTest, retries_start_after_tracer_get_returns_null)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    cplat_tracer *const tracer = reinterpret_cast<cplat_tracer *>(1);
    cplat_tracer_hook_entry *const hook_entry = reinterpret_cast<cplat_tracer_hook_entry *>(1);
    test_reset_tracer_started();
    test_reset_tracer_screen();
    // [状態] - 開始前の状態と、2 回目に取得するトレーサーを用意する。

    // Pre-Assert
    InSequence sequence;
    EXPECT_CALL(mock_porter, potr_tracer_get()).WillOnce(Return(nullptr));
    // [Pre-Assert確認_異常系] - 最初の potr_tracer_get() が NULL を返すこと。
    // [Pre-Assert手順] - 1 回目の取得で NULL を返す。
    EXPECT_CALL(mock_stdio, fprintf(_, _, _, stderr, HasSubstr("トレースの表示を開始できませんでした。")))
        .WillOnce(Return(0));
    // [Pre-Assert確認_異常系] - NULL の診断を標準エラー出力へ 1 回表示すること。
    // [Pre-Assert手順] - 診断出力を成功として返す。
    EXPECT_CALL(mock_porter, potr_tracer_get()).WillOnce(Return(tracer));
    // [Pre-Assert確認_正常系] - 次の potr_tracer_get() がトレーサーを返すこと。
    // [Pre-Assert手順] - 2 回目の取得で共有ハンドルを返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_set_hook(tracer, _, _)).WillOnce(Return(hook_entry));
    // [Pre-Assert確認_正常系] - トレーサー取得後に hook を 1 回設定すること。
    // [Pre-Assert手順] - 有効な hook entry を返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_start(tracer)).WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - hook の設定後にトレーサーを 1 回開始すること。
    // [Pre-Assert手順] - 開始を成功として返す。

    // Act
    test_ensure_tracer_started();
    // [手順] - トレーサーの取得が NULL になる最初の開始を試す。

    // Assert
    EXPECT_EQ(0, test_get_tracer_started()); // [確認_異常系] - NULL では開始済みの印を設定しないこと。

    // Act_2
    test_ensure_tracer_started();
    // [手順_2] - 後続の呼び出しで取得と開始を再試行する。

    // Assert_2
    EXPECT_EQ(1, test_get_tracer_started()); // [確認_正常系] - 2 回目に取得と開始が成功すると開始済みにすること。

    // Cleanup
    test_reset_tracer_started();
    test_reset_tracer_screen();
}
