#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <mock_stdio.h>
#include <cplat/crt/stdio.h>
#include <porter_test_helper.h>
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
    FILE *remaining = cplat_fopen(path, "rb", nullptr);

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
    FILE *remaining = cplat_fopen(path, "rb", nullptr);

    // Assert
    EXPECT_NE(nullptr, remaining); // [確認_異常系] - 所有していないファイルを削除しないこと。

    // Cleanup
    if (remaining != nullptr)
    {
        fclose(remaining);
    }
    remove(path);
}

// TCP 双方向サービスの RECEIVER で送信可能状態 (can_send) が有効になり、SENDER 判定も維持することの確認
TEST(porterTestOutputTest, tcp_bidir_receiver_enables_can_send)
{
    // Arrange
    PorterConfigBuilder cfg;
    std::string config_path = cfg.addTcpBidirService(1, 19360).build();
    ASSERT_FALSE(config_path.empty()); // [状態確認] - TCP_BIDIR サービス設定ファイルを生成できること。
    int recv_is_bidir = -1;
    int recv_can_send = -1;
    int send_is_bidir = -1;
    int send_can_send = -1;
    // [状態] - TCP_BIDIR の RECEIVER と SENDER を開く条件を用意する。

    // Pre-Assert

    // Act
    int actual_recv_rc = test_do_open(POTR_ROLE_RECEIVER, config_path.c_str(), 1, &recv_is_bidir, &recv_can_send);
    int actual_send_rc = test_do_open(POTR_ROLE_SENDER, config_path.c_str(), 1, &send_is_bidir, &send_can_send);
    // [手順] - TCP_BIDIR の RECEIVER と SENDER で do_open() を実行する。

    // Assert
    EXPECT_EQ(0, actual_recv_rc); // [確認_正常系] - TCP_BIDIR RECEIVER のオープンに成功すること。
    EXPECT_EQ(1, recv_is_bidir);  // [確認_正常系] - TCP_BIDIR RECEIVER が双方向扱いになること。
    EXPECT_EQ(1, recv_can_send);  // [確認_正常系] - TCP_BIDIR RECEIVER で送信可能になること。
    EXPECT_EQ(0, actual_send_rc); // [確認_正常系] - TCP_BIDIR SENDER のオープンに成功すること。
    EXPECT_EQ(1, send_is_bidir);  // [確認_正常系] - TCP_BIDIR SENDER が双方向扱いを維持すること。
    EXPECT_EQ(1, send_can_send);  // [確認_正常系] - TCP_BIDIR SENDER で送信可能を維持すること。
}

// 片方向サービスの RECEIVER では送信可能状態 (can_send) を無効に保つことの確認
TEST(porterTestOutputTest, one_way_receiver_keeps_can_send_disabled)
{
    // Arrange
    PorterConfigBuilder cfg;
    std::string config_path = cfg.addUnicastService(2, 19361).build();
    ASSERT_FALSE(config_path.empty()); // [状態確認] - 片方向 UNICAST サービス設定ファイルを生成できること。
    int actual_is_bidir = -1;
    int actual_can_send = -1;
    // [状態] - 片方向 UNICAST の RECEIVER を開く条件を用意する。

    // Pre-Assert

    // Act
    int actual_rc = test_do_open(POTR_ROLE_RECEIVER, config_path.c_str(), 2, &actual_is_bidir, &actual_can_send);
    // [手順] - 片方向 UNICAST の RECEIVER として do_open() を実行する。

    // Assert
    EXPECT_EQ(0, actual_rc);       // [確認_正常系] - 片方向 UNICAST RECEIVER のオープンに成功すること。
    EXPECT_EQ(0, actual_is_bidir); // [確認_正常系] - 片方向 RECEIVER は非双方向を維持すること。
    EXPECT_EQ(0, actual_can_send); // [確認_正常系] - 片方向 RECEIVER は送信無効を維持すること。
}

// 終了要求 callback が stdin を閉じずに cplat_pinned_prompt_stop_input() を呼ぶことの確認
TEST(porterTestOutputTest, shutdown_callback_stops_pinned_prompt_without_closing_stdin)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    cplat_pinned_prompt *fake_screen = reinterpret_cast<cplat_pinned_prompt *>(0x2000);
    test_set_stop_target_screen(fake_screen);
    // [状態] - 破棄前の固定プロンプト ハンドルが登録済みである状態を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_close(_, _)).Times(0);
    // [Pre-Assert確認_正常系] - 標準入力のクローズを行わないこと。
    EXPECT_CALL(mock_cplat, cplat_pinned_prompt_stop_input(fake_screen)).Times(1).WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - cplat_pinned_prompt_stop_input() が 1 回呼ばれること。
    // [Pre-Assert手順] - CPLAT_OK を返す。

    // Act
    test_invoke_shutdown_callback();
    // [手順] - 終了要求 callback を実行する。

    // Assert
    // [確認_正常系] - Pre-Assert の通り stdin を閉じず stop_input が呼ばれること。

    // Cleanup
    test_set_stop_target_screen(nullptr);
}

// 終了要求 callback が cplat_pinned_prompt_stop_input() の失敗を診断することの確認
TEST(porterTestOutputTest, shutdown_callback_diagnoses_stop_input_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    cplat_pinned_prompt *fake_screen = reinterpret_cast<cplat_pinned_prompt *>(0x2000);
    test_set_stop_target_screen(fake_screen);
    // [状態] - 破棄前の固定プロンプト ハンドルが登録済みである状態を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_pinned_prompt_stop_input(fake_screen)).Times(1).WillOnce(Return(CPLAT_ERR_UNKNOWN));
    // [Pre-Assert確認_異常系] - cplat_pinned_prompt_stop_input() が 1 回呼ばれること。
    // [Pre-Assert手順] - CPLAT_ERR_UNKNOWN を返す。

    // Act
    testing::internal::CaptureStderr();
    test_invoke_shutdown_callback();
    std::string actual_stderr = testing::internal::GetCapturedStderr();
    // [手順] - stop_input 失敗条件で終了要求 callback を実行し標準エラー出力を採取する。

    // Assert
    EXPECT_NE(std::string::npos, actual_stderr.find("プロンプト入力の中止に失敗しました"));
    // [確認_異常系] - stop_input の失敗診断メッセージが標準エラー出力へ記録されること。

    // Cleanup
    test_set_stop_target_screen(nullptr);
}

// 画面破棄開始前に保護対象を解除した後は終了要求 callback が stop_input を呼ばないことの確認
TEST(porterTestOutputTest, shutdown_callback_skips_stop_input_after_screen_cleared)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    cplat_pinned_prompt *fake_screen = reinterpret_cast<cplat_pinned_prompt *>(0x2000);
    test_set_stop_target_screen(fake_screen);
    test_set_stop_target_screen(nullptr);
    // [状態] - 破棄直前の手順として中止対象の画面ハンドルを NULL 化した状態を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_close(_, _)).Times(0);
    // [Pre-Assert確認_異常系] - cplat_close() が呼ばれないこと。
    EXPECT_CALL(mock_cplat, cplat_pinned_prompt_stop_input(_)).Times(0);
    // [Pre-Assert確認_異常系] - cplat_pinned_prompt_stop_input() が呼ばれないこと。

    // Act
    test_invoke_shutdown_callback();
    // [手順] - 保護対象解除後に終了要求 callback を実行する。

    // Assert
    // [確認_異常系] - Pre-Assert の通り stop_input を呼び出さないこと。
}
