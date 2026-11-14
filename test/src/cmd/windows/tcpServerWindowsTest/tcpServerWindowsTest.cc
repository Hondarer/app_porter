#define _HAS_STD_BYTE 0
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_windows.h>
#include <mock_winsock.h>
#include <mock_stdio.h>
#include "tcpServer_windows.inject.h"

// 親参照の閉鎖結果を通常無出力の NONE トレースで記録することの確認。
TEST(tcpServerWindowsTest, records_parent_close_with_none_level)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(nullptr, CPLAT_TRACE_LEVEL_NONE, nullptr,
                                                   StrEq("parent_close worker=42 connection=17 result=0")))
        .WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - ハンドルとタイムスタンプに nullptr、NONE レベル、worker=42・connection=17・result=0 の parent_close 記録を指定して 1 回呼び出すこと。
    // [Pre-Assert手順] - トレース呼び出しから CPLAT_OK を返す。
    // Act
    tcpserver_trace_test_emit(); // [手順] - 製品ソースの内部トレース関数を呼び出す。
    // Assert
}
