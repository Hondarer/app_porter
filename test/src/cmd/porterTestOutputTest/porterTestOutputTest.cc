#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <mock_stdio.h>
#include <cstring>
#include "porter-test.inject.h"

// バイナリの部分書き込みに失敗した一時ファイルを残さないことの確認
TEST(porterTestOutputTest, removes_temporary_file_after_partial_write)
{
    // Arrange
    char path[PLATFORM_PATH_MAX] = {};
    FILE *stream = cplat_fopen_temp("pto", "wb", path, sizeof(path), nullptr);
    ASSERT_NE(nullptr, stream); // [状態確認] - 一時ファイルを作成できること。
    NiceMock<Mock_cplat> mock;
    NiceMock<Mock_stdio> stdio_mock;
    const uint8_t data[] = {0, 255, 1}; // [状態] - テキストとして扱わないバイナリを用意する。

    // Pre-Assert
    EXPECT_CALL(mock, cplat_fopen_temp(_, _, _, _, _))
        .WillOnce(Invoke(
            [&](const char *, const char *, char *out, size_t capacity, cplat_error *)
            {
                cplat_strcpy(out, capacity, path);
                return stream;
            }));
    // [Pre-Assert確認_正常系] - 保存用一時ファイルを 1 回開くこと。
    // [Pre-Assert手順] - 作成済みのストリームとパスを返す。
    EXPECT_CALL(stdio_mock, fwrite(_, _, _, _, 1, sizeof(data), stream)).WillOnce(Return(1));
    // [Pre-Assert確認_異常系] - 保存用の書き込みを 1 回試すこと。
    // [Pre-Assert手順] - 1 バイトだけ書いた結果を返す。

    // Act
    test_binary_receive(data, sizeof(data)); // [手順] - 保存失敗時の受信処理を実行する。
    FILE *remaining = fopen(path, "rb");

    // Assert
    EXPECT_EQ(nullptr, remaining); // [確認_異常系] - 失敗した一時ファイルが削除されていること。

    // Cleanup
    if (remaining != nullptr)
    {
        fclose(remaining);
        remove(path);
    }
}

// 作成失敗で返されたパスを削除に使用しないことの確認
TEST(porterTestOutputTest, does_not_remove_path_when_creation_fails)
{
    // Arrange
    char path[PLATFORM_PATH_MAX] = {};
    FILE *stream = cplat_fopen_temp("pto", "wb", path, sizeof(path), nullptr);
    ASSERT_NE(nullptr, stream); // [状態確認] - 保護対象の既存ファイルを用意できること。
    fclose(stream);
    NiceMock<Mock_cplat> mock;
    const uint8_t data[] = {0, 255, 1}; // [状態] - バイナリ データを用意する。

    // Pre-Assert
    EXPECT_CALL(mock, cplat_fopen_temp(_, _, _, _, _))
        .WillOnce(Invoke(
            [&](const char *, const char *, char *out, size_t capacity, cplat_error *) -> FILE *
            {
                cplat_strcpy(out, capacity, path);
                return nullptr;
            }));
    // [Pre-Assert確認_異常系] - ファイル作成を 1 回試すこと。
    // [Pre-Assert手順] - パスは書き込むが、ファイル作成は失敗させる。

    // Act
    test_binary_receive(data, sizeof(data)); // [手順] - 作成失敗を処理する。
    FILE *remaining = fopen(path, "rb");

    // Assert
    EXPECT_NE(nullptr, remaining); // [確認_異常系] - 所有していないファイルを削除しないこと。

    // Cleanup
    if (remaining != nullptr)
    {
        fclose(remaining);
    }
    remove(path);
}
