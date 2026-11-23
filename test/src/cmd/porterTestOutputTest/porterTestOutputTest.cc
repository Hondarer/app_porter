#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <mock_stdio.h>
#include <cplat/crt/stdio.h>
#include <cplat/net/endpoint.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/socket.h>
#include <porter/porter_result.h>
#include <porter_test_helper.h>
#include <wrapmain.h>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
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

// トレースの hook を設定できない場合に、診断を出して開始済みとしないことの確認 (P-032)
TEST(porterTestOutputTest, ensure_tracer_started_diagnoses_set_hook_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    test_reset_tracer_started(); // [状態] - トレースの表示を開始していない状態を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_set_hook(_, _, _)).WillOnce(Return(nullptr));
    // [Pre-Assert確認_異常系] - cplat_tracer_set_hook() が 1 回呼ばれること。
    // [Pre-Assert手順] - nullptr を返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_start(_)).Times(0);
    // [Pre-Assert確認_異常系] - hook を設定できない場合は cplat_tracer_start() を呼ばないこと。

    // Act
    testing::internal::CaptureStderr();
    test_ensure_tracer_started(); // [手順] - ensure_tracer_started() を実行する。
    std::string actual_stderr = testing::internal::GetCapturedStderr();

    // Assert
    EXPECT_EQ(0, test_get_tracer_started()); // [確認_異常系] - 開始済みにならないこと。
    EXPECT_NE(std::string::npos, actual_stderr.find("トレースの表示を開始できませんでした"));
    // [確認_異常系] - 標準エラー出力へトレースの表示を開始できなかった診断を出すこと。

    // Cleanup
    test_reset_tracer_started();
}

// トレースを開始できない場合に、設定した hook を外して診断を出し、次の呼び出しで再び開始を試みることの確認 (P-033)
TEST(porterTestOutputTest, ensure_tracer_started_removes_hook_when_start_fails)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    cplat_tracer_hook_entry *fake_entry = reinterpret_cast<cplat_tracer_hook_entry *>(0x3000);
    test_reset_tracer_started(); // [状態] - トレースの表示を開始していない状態を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_set_hook(_, _, _)).Times(2).WillRepeatedly(Return(fake_entry));
    // [Pre-Assert確認_異常系] - cplat_tracer_set_hook() が 2 回 (失敗の後の再試行を含む) 呼ばれること。
    // [Pre-Assert手順] - hook のエントリを返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_start(_)).WillOnce(Return(CPLAT_ERR_UNKNOWN)).WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_異常系] - cplat_tracer_start() が 2 回呼ばれること。
    // [Pre-Assert手順] - 1 回目は CPLAT_ERR_UNKNOWN、2 回目は CPLAT_OK を返す。
    EXPECT_CALL(mock_cplat, cplat_tracer_remove_hook(_, fake_entry)).Times(1);
    // [Pre-Assert確認_異常系] - 開始の失敗で、設定した hook のエントリを 1 回外すこと。

    // Act
    testing::internal::CaptureStderr();
    test_ensure_tracer_started(); // [手順] - 開始が失敗する条件で ensure_tracer_started() を実行する。
    std::string actual_stderr = testing::internal::GetCapturedStderr();
    int actual_started_after_failure = test_get_tracer_started();
    test_ensure_tracer_started(); // [手順] - 開始が成功する条件で ensure_tracer_started() を再び実行する。
    int actual_started_after_retry = test_get_tracer_started();

    // Assert
    EXPECT_EQ(0, actual_started_after_failure); // [確認_異常系] - 開始の失敗では開始済みにならないこと。
    EXPECT_NE(std::string::npos, actual_stderr.find("トレースの表示を開始できませんでした (result="));
    // [確認_異常系] - 標準エラー出力へ結果コードを含む診断を出すこと。
    EXPECT_EQ(1, actual_started_after_retry); // [確認_異常系] - 再試行で開始に成功すると開始済みになること。

    // Cleanup
    test_reset_tracer_started();
}

// close コマンドでサービスの終了処理が失敗した場合に、結果コードを標準エラー出力のチャネルへ表示することの確認 (P-034)
TEST(porterTestOutputTest, do_close_reports_close_failure)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context *invalid_handle = reinterpret_cast<potr_context *>(0x4000);
    const std::string expected_result = "result=" + std::to_string(POTR_ERR_INVALID_HANDLE);
    // [状態] - potr_service_close() が POTR_ERR_INVALID_HANDLE を返す、開いていないハンドルを用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_pinned_prompt_printf(_, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat,
                cplat_pinned_prompt_printf(_, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                           AllOf(HasSubstr("サービス 7 の終了処理が失敗しました"),
                                                 HasSubstr(expected_result))))
        .Times(1);
    // [Pre-Assert確認_異常系] - 標準エラー出力のチャネルへサービス ID と結果コードを含む警告を 1 回表示すること。
    EXPECT_CALL(mock_cplat, cplat_pinned_prompt_printf(_, _, HasSubstr("サービスを閉じました"))).Times(0);
    // [Pre-Assert確認_異常系] - 終了処理の失敗では「サービスを閉じました」を表示しないこと。

    // Act
    test_do_close_handle(invalid_handle, 7); // [手順] - 開いていないハンドルで do_close() を実行する。

    // Assert
    // [確認_異常系] - Pre-Assert の通り、終了処理の失敗の警告を表示すること。
}

// 終了の直前のサービスの終了処理が失敗した場合に、結果コードを表示し、終了コードは EXIT_SUCCESS のままであることの確認 (P-035)
TEST(porterTestOutputTest, main_reports_close_failure_at_exit)
{
    // Arrange
    const int port = 19362;
    PorterConfigBuilder cfg;
    std::string config_path = cfg.setTcpHealthIntervalMs(0U)
                                  .setTcpHealthTimeoutMs(0U)
                                  .setTcpCloseTimeoutMs(200U)
                                  .addTcpBidirService(3, port)
                                  .build();
    ASSERT_FALSE(config_path.empty()); // [状態確認] - TCP_BIDIR サービス設定ファイルを生成できること。
    // [状態] - FIN_ACK の待機を 200 [ms] とし、定周期のヘルス チェックを無効にした TCP_BIDIR サービスの設定を用意する。
    // 定周期のヘルス チェックが有効だと、接続の直後の終了要求でヘルス スレッドの起動が取り消されて接続が切れ、
    // FIN_ACK を待たずに終了処理が成功することがある。無効にすると、最初のパケットの受信後は必ず FIN_ACK を待つ。
    cplat_socket listen_sock = CPLAT_INVALID_SOCKET;
    cplat_socket accepted_sock = CPLAT_INVALID_SOCKET;
    cplat_ipv4_endpoint listen_addr = {};
    ASSERT_EQ(CPLAT_OK, cplat_ipv4_parse("127.0.0.1", &listen_addr.address)); // [状態確認] - ループバック アドレスを解釈できること。
    listen_addr.port = cplat_hton16((uint16_t)port);
    ASSERT_EQ(CPLAT_OK, cplat_socket_open(CPLAT_SOCKET_TCP, &listen_sock, nullptr)); // [状態確認] - 待ち受けのソケットを作成できること。
    (void)cplat_socket_set_reuse_address(listen_sock, 1, nullptr);
    ASSERT_EQ(CPLAT_OK, cplat_socket_bind(listen_sock, &listen_addr, nullptr)); // [状態確認] - 待ち受けのソケットを bind できること。
    ASSERT_EQ(CPLAT_OK, cplat_socket_listen(listen_sock, 1, nullptr)); // [状態確認] - 待ち受けを開始できること。
    // [状態] - 接続を受け付けるが FIN_ACK を返さない相手を、ループバックの実ソケットで用意する。
    char arg0[] = "porter-test";
    char arg1[] = "sender";
    std::string arg2 = config_path;
    char arg3[] = "3";
    char *argv[] = {arg0, arg1, &arg2[0], arg3};
    cplat_pinned_prompt *fake_screen = reinterpret_cast<cplat_pinned_prompt *>(0x2000);
    int peer_received = 0;
    NiceMock<Mock_cplat> mock_cplat;
    ON_CALL(mock_cplat, cplat_pinned_prompt_create(_)).WillByDefault(Return(fake_screen));
    ON_CALL(mock_cplat, cplat_pinned_prompt_dispose(_)).WillByDefault(Return());
    ON_CALL(mock_cplat, cplat_pinned_prompt_printf(_, _, _)).WillByDefault(Return(0));
    ON_CALL(mock_cplat, cplat_shutdown_request_register(_, _)).WillByDefault(Return(CPLAT_OK));
    // [状態] - 固定プロンプトの作成、破棄、表示と、終了要求 callback の登録を mock で受ける。
    ON_CALL(mock_cplat, cplat_pinned_prompt_readline_fmt_at(_, _, _, _, _, _, _))
        .WillByDefault(Invoke(
            [&](cplat_pinned_prompt *, char *, size_t, const char *, int, const char *, va_list) -> int
            {
                cplat_ipv4_endpoint peer_addr = {};
                uint8_t buffer[256];
                size_t received = 0U;

                if (cplat_socket_timed_wait_readable(listen_sock, 10000, nullptr) == CPLAT_OK &&
                    cplat_socket_accept(listen_sock, &peer_addr, &accepted_sock, nullptr) == CPLAT_OK &&
                    cplat_socket_timed_wait_readable(accepted_sock, 10000, nullptr) == CPLAT_OK &&
                    cplat_socket_recv(accepted_sock, buffer, sizeof(buffer), &received, nullptr) == CPLAT_OK)
                {
                    peer_received = received > 0U;
                }
                return CPLAT_ERR_UNKNOWN;
            }));
    // [状態] - 入力の待機で、接続を受け付けて最初のパケットを受信してから、入力の失敗を返して入力ループを抜ける。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_pinned_prompt_printf(_, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_pinned_prompt_printf(fake_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                                       HasSubstr("サービス 3 の終了処理が失敗しました")))
        .Times(1);
    // [Pre-Assert確認_異常系] - 標準エラー出力のチャネルへ終了処理の失敗の警告を 1 回表示すること。

    // Act
    int exit_code = __real_main(4, argv); // [手順] - 位置引数 3 個で main() を実行する。

    // Assert
    EXPECT_EQ(1, peer_received); // [確認_異常系] - 終了処理の前に、送信者からパケットを受信して接続済みになっていたこと。
    EXPECT_EQ(EXIT_SUCCESS, exit_code); // [確認_異常系] - 終了処理の失敗では終了コードを変えないこと。

    // Cleanup
    if (accepted_sock != CPLAT_INVALID_SOCKET)
    {
        cplat_socket_close(accepted_sock);
    }
    cplat_socket_close(listen_sock);
}

// --help 表示で API 戻り値失敗・fflush 失敗・ferror 検出のいずれでも EXIT_FAILURE と診断を返すことの確認 (P-036)
TEST(porterTestOutputTest, help_returns_exit_failure_when_print_or_stream_fails)
{
    // Arrange
    char arg0[] = "porter-test";
    char arg1[] = "--help";
    char *argv[] = {arg0, arg1};

    {
        // Arrange
        NiceMock<Mock_cplat> mock_cplat;
        NiceMock<Mock_stdio> mock_stdio; // [状態] - print_usage API がエラーを返す条件を用意する。

        // Pre-Assert
        EXPECT_CALL(mock_cplat, cplat_argparser_print_usage(stdout)).WillOnce(Return(CPLAT_ERR_UNKNOWN));
        // [Pre-Assert確認_異常系] - cplat_argparser_print_usage() が 1 回呼ばれること。
        // [Pre-Assert手順] - CPLAT_ERR_UNKNOWN を返す。

        // Act
        testing::internal::CaptureStderr();
        int exit_code = __real_main(2, argv); // [手順] - API 失敗時の --help 経路を実行する。
        std::string errors = testing::internal::GetCapturedStderr();

        // Assert
        EXPECT_EQ(EXIT_FAILURE, exit_code); // [確認_異常系] - API 失敗で EXIT_FAILURE を返すこと。
        EXPECT_NE(std::string::npos, errors.find("使い方を標準出力へ書き込めませんでした"));
        // [確認_異常系] - 標準エラー出力へ使い方を出力できなかった診断を出すこと。
    }

    {
        // Arrange
        NiceMock<Mock_cplat> mock_cplat;
        NiceMock<Mock_stdio> mock_stdio; // [状態] - fflush(stdout) が失敗する条件を用意する。

        // Pre-Assert
        EXPECT_CALL(mock_cplat, cplat_argparser_print_usage(stdout)).WillOnce(Return(CPLAT_OK));
        // [Pre-Assert確認_異常系] - print_usage() が CPLAT_OK を返してもフラッシュ検査へ進むこと。
        // [Pre-Assert手順] - CPLAT_OK を返す。
        EXPECT_CALL(mock_stdio, fflush(_, _, _, stdout)).WillOnce(Return(EOF));
        // [Pre-Assert確認_異常系] - fflush(stdout) が呼ばれること。
        // [Pre-Assert手順] - EOF を返す。

        // Act
        testing::internal::CaptureStderr();
        int exit_code = __real_main(2, argv); // [手順] - フラッシュ失敗時の --help 経路を実行する。
        std::string errors = testing::internal::GetCapturedStderr();

        // Assert
        EXPECT_EQ(EXIT_FAILURE, exit_code); // [確認_異常系] - fflush(stdout) 失敗で EXIT_FAILURE を返すこと。
        EXPECT_NE(std::string::npos, errors.find("使い方を標準出力へ書き込めませんでした"));
        // [確認_異常系] - 標準エラー出力へ使い方を出力できなかった診断を出すこと。
    }

    {
        // Arrange
        NiceMock<Mock_cplat> mock_cplat;
        NiceMock<Mock_stdio> mock_stdio; // [状態] - ferror(stdout) が非 0 になる条件を用意する。

        // Pre-Assert
        EXPECT_CALL(mock_cplat, cplat_argparser_print_usage(stdout)).WillOnce(Return(CPLAT_OK));
        // [Pre-Assert確認_異常系] - print_usage() の成功後にストリーム検査へ進むこと。
        // [Pre-Assert手順] - CPLAT_OK を返す。
        EXPECT_CALL(mock_stdio, fflush(_, _, _, stdout)).WillOnce(Return(0));
        // [Pre-Assert確認_異常系] - fflush(stdout) が 0 を返しても ferror(stdout) 検査へ進むこと。
        // [Pre-Assert手順] - 0 を返す。
        EXPECT_CALL(mock_stdio, ferror(_, _, _, stdout)).WillOnce(Return(1));
        // [Pre-Assert確認_異常系] - ferror(stdout) が呼ばれること。
        // [Pre-Assert手順] - ストリーム エラーを示す 1 を返す。

        // Act
        testing::internal::CaptureStderr();
        int exit_code = __real_main(2, argv); // [手順] - ストリーム エラー検出時の --help 経路を実行する。
        std::string errors = testing::internal::GetCapturedStderr();

        // Assert
        EXPECT_EQ(EXIT_FAILURE, exit_code); // [確認_異常系] - ferror(stdout) 検出で EXIT_FAILURE を返すこと。
        EXPECT_NE(std::string::npos, errors.find("使い方を標準出力へ書き込めませんでした"));
        // [確認_異常系] - 標準エラー出力へ使い方を出力できなかった診断を出すこと。
    }

    // Cleanup
}

// --help 表示と標準出力の検査が成功した場合に EXIT_SUCCESS を返し、診断を出さないことの確認 (P-036)
TEST(porterTestOutputTest, help_returns_exit_success_when_print_succeeds)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    char arg0[] = "porter-test";
    char arg1[] = "--help";
    char *argv[] = {arg0, arg1};
    // [状態] - --help を指定する引数を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_argparser_print_usage(stdout)).WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - cplat_argparser_print_usage() が標準出力を指定して 1 回呼ばれること。
    // [Pre-Assert手順] - CPLAT_OK を返す。

    // Act
    testing::internal::CaptureStderr();
    int exit_code = __real_main(2, argv); // [手順] - --help 経路を実行する。
    std::string errors = testing::internal::GetCapturedStderr();

    // Assert
    EXPECT_EQ(EXIT_SUCCESS, exit_code); // [確認_正常系] - EXIT_SUCCESS を返すこと。
    EXPECT_EQ(std::string::npos, errors.find("使い方を標準出力へ書き込めませんでした"));
    // [確認_正常系] - 標準エラー出力へ診断を出さないこと。
}

// 全経路の状態を、指定した順のカンマ区切りへまとめることの確認
TEST(porterTestOutputTest, format_path_states_joins_all_paths_in_order)
{
    // Arrange
    const int states[POTR_MAX_PATH] = {1, 2, 0, 1, 2, 0, 1, 2};
    char dest[64];
    // [状態] - 8 経路の状態 1, 2, 0, 1, 2, 0, 1, 2 と、結果を受けるバッファーを用意する。

    // Pre-Assert

    // Act
    int actual = test_format_path_states(dest, sizeof(dest), states);
    // [手順] - 8 経路の状態を format_path_states() で連結する。

    // Assert
    EXPECT_EQ(CPLAT_OK, actual); // [確認_正常系] - test_format_path_states() の戻り値が CPLAT_OK であること。
    EXPECT_STREQ("1,2,0,1,2,0,1,2", dest); // [確認_正常系] - dest が "1,2,0,1,2,0,1,2" であること。
}

// 状態の配列が NULL のとき、全経路を 0 として連結することの確認
TEST(porterTestOutputTest, format_path_states_uses_zero_when_states_are_null)
{
    // Arrange
    char dest[64];
    memset(dest, 'Q', sizeof(dest));
    // [状態] - 状態の配列を渡さず、初期値が Q のバッファーを用意する。

    // Pre-Assert

    // Act
    int actual = test_format_path_states(dest, sizeof(dest), nullptr);
    // [手順] - states に nullptr を指定して format_path_states() を呼び出す。

    // Assert
    EXPECT_EQ(CPLAT_OK, actual); // [確認_正常系] - test_format_path_states() の戻り値が CPLAT_OK であること。
    EXPECT_STREQ("0,0,0,0,0,0,0,0", dest); // [確認_正常系] - dest が "0,0,0,0,0,0,0,0" であること。
}

// 各経路が int の最小値でも、porter-test の出力先の容量に収まることの確認
TEST(porterTestOutputTest, format_path_states_fits_longest_state_values)
{
    // Arrange
    int states[POTR_MAX_PATH];
    std::string expected;
    const size_t capacity = test_path_states_capacity();
    std::vector<char> dest(capacity + 1U, 'Q');
    for (size_t index = 0U; index < POTR_MAX_PATH; index++)
    {
        states[index] = INT_MIN;
        if (index != 0U)
        {
            expected.push_back(',');
        }
        expected += std::to_string(INT_MIN);
    }
    // [状態] - 8 経路すべてを INT_MIN にし、on_recv と同じ容量の出力先を用意する。

    // Pre-Assert

    // Act
    int actual = test_format_path_states(dest.data(), capacity, states);
    // [手順] - 最長の状態値を、on_recv と同じ容量で format_path_states() に渡す。

    // Assert
    EXPECT_EQ(CPLAT_OK, actual); // [確認_正常系] - test_format_path_states() の戻り値が CPLAT_OK であること。
    EXPECT_STREQ(expected.c_str(), dest.data()); // [確認_正常系] - dest.data() が expected.c_str() であること。(8 経路の INT_MIN が出力先に収まったことの確認)
    EXPECT_EQ('Q', dest[capacity]); // [確認_正常系] - dest[capacity] が 'Q' であること。(容量の直後を書き換えていないことの確認)
}

// 3 回目の書式化が失敗したとき、空文字列と、その結果コードを返すことの確認
TEST(porterTestOutputTest, format_path_states_clears_output_when_third_format_fails)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const int states[POTR_MAX_PATH] = {1, 2, 0, 1, 2, 0, 1, 2};
    char dest[64];
    memset(dest, 'Z', sizeof(dest) - 1U);
    dest[sizeof(dest) - 1U] = '\0';
    // [状態] - 8 経路の状態と、Z で埋めた出力先を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_snprintf(_, _, _))
        .WillOnce(DoDefault())
        .WillOnce(DoDefault())
        .WillOnce(Return(CPLAT_ERR_UNKNOWN));
    // [Pre-Assert確認_異常系] - cplat_snprintf() が 3 回呼び出されること。
    // [Pre-Assert手順] - cplat_snprintf() から 1 回目と 2 回目は製品 (prod) の処理へ委譲し、3 回目は CPLAT_ERR_UNKNOWN を返す。

    // Act
    int actual = test_format_path_states(dest, sizeof(dest), states);
    // [手順] - 3 回目の書式化が失敗する状態で format_path_states() を呼び出す。

    // Assert
    EXPECT_EQ(CPLAT_ERR_UNKNOWN, actual);
    // [確認_異常系] - test_format_path_states() の戻り値が CPLAT_ERR_UNKNOWN であること。
    EXPECT_STREQ("", dest); // [確認_異常系] - dest が空文字列であること。(不完全な一覧を残さないことの確認)
}

// 書式化の失敗を受けた経路イベントが、不完全な一覧ではなく states=? を表示することの確認
TEST(porterTestOutputTest, path_event_shows_question_mark_when_format_fails)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const int states[POTR_MAX_PATH] = {1, 2, 0, 1, 2, 0, 1, 2};
    // [状態] - 8 経路の状態 1, 2, 0, 1, 2, 0, 1, 2 を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_snprintf(_, _, _))
        .WillOnce(DoDefault())
        .WillOnce(DoDefault())
        .WillOnce(Return(CPLAT_ERR_UNKNOWN));
    // [Pre-Assert確認_異常系] - cplat_snprintf() が 3 回呼び出されること。
    // [Pre-Assert手順] - cplat_snprintf() から 1 回目と 2 回目は製品 (prod) の処理へ委譲し、3 回目は CPLAT_ERR_UNKNOWN を返す。
    EXPECT_CALL(mock_cplat, cplat_pinned_prompt_printf(_, _, HasSubstr("states={"))).Times(0);
    // [Pre-Assert確認_異常系] - states={ を含む文字列を指定した cplat_pinned_prompt_printf() の呼び出しがないこと。
    EXPECT_CALL(mock_cplat,
                cplat_pinned_prompt_printf(_, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                           HasSubstr("[サービス 11] path[4] CONNECTED states=?")))
        .WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_異常系] - 標準出力のチャネルと、"[サービス 11] path[4] CONNECTED states=?" を含む文字列を指定して cplat_pinned_prompt_printf() が 1 回呼び出されること。
    // [Pre-Assert手順] - cplat_pinned_prompt_printf() から CPLAT_OK を返す。

    // Act
    test_on_path_state_event(11, POTR_EVENT_PATH_CONNECTED, 4U, states);
    // [手順] - 経路 4 の CONNECTED を on_recv() へ渡す。

    // Assert
    // [確認_異常系] - Pre-Assert の通り、不完全な一覧を出さず states=? を表示すること。
}

// 出力先が NULL のとき、書式化せず引数の誤りを返すことの確認
TEST(porterTestOutputTest, format_path_states_rejects_null_destination)
{
    // Arrange
    const int states[POTR_MAX_PATH] = {1, 2, 0, 1, 2, 0, 1, 2};
    // [状態] - 8 経路の状態を用意し、出力先は渡さない。

    // Pre-Assert

    // Act
    int actual = test_format_path_states(nullptr, 16U, states);
    // [手順] - dest に nullptr、容量に 16 を指定して format_path_states() を呼び出す。

    // Assert
    EXPECT_EQ(CPLAT_ERR_INVALID_ARGUMENT, actual);
    // [確認_異常系] - test_format_path_states() の戻り値が CPLAT_ERR_INVALID_ARGUMENT であること。
}

// 容量が 0 のとき、出力先を書き換えず引数の誤りを返すことの確認
TEST(porterTestOutputTest, format_path_states_rejects_zero_capacity)
{
    // Arrange
    const int states[POTR_MAX_PATH] = {1, 2, 0, 1, 2, 0, 1, 2};
    char dest[4] = {'A', 'B', 'C', 'D'};
    // [状態] - 先頭が A の出力先と、8 経路の状態を用意する。

    // Pre-Assert

    // Act
    int actual = test_format_path_states(dest, 0U, states);
    // [手順] - 容量に 0 を指定して format_path_states() を呼び出す。

    // Assert
    EXPECT_EQ(CPLAT_ERR_INVALID_ARGUMENT, actual);
    // [確認_異常系] - test_format_path_states() の戻り値が CPLAT_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ('A', dest[0]); // [確認_異常系] - dest[0] が A であること。(容量 0 では出力先を書き換えないことの確認)
}

// 出力先の容量が全経路の連結に足りないとき、空文字列と容量不足を返すことの確認
TEST(porterTestOutputTest, format_path_states_clears_output_when_capacity_is_insufficient)
{
    // Arrange
    const int states[POTR_MAX_PATH] = {1, 2, 0, 1, 2, 0, 1, 2};
    char dest[8];
    memset(dest, 'Q', sizeof(dest));
    // [状態] - 8 経路の状態と、Q で埋めた 8 byte の出力先を用意する。1 経路目の "1" だけが収まる容量 2 を使う。

    // Pre-Assert

    // Act
    int actual = test_format_path_states(dest, 2U, states);
    // [手順] - 容量 2 で format_path_states() を呼び出す。

    // Assert
    EXPECT_EQ(CPLAT_ERR_BUFFER_TOO_SMALL, actual);
    // [確認_異常系] - test_format_path_states() の戻り値が CPLAT_ERR_BUFFER_TOO_SMALL であること。
    EXPECT_STREQ("", dest); // [確認_異常系] - dest が空文字列であること。(途中までの連結を残さないことの確認)
}

// 経路の接続と切断の表示が、全経路の状態を順番どおりに含むことの確認
TEST(porterTestOutputTest, path_event_shows_all_states_in_order)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const int states[POTR_MAX_PATH] = {1, 2, 0, 1, 2, 0, 1, 2};
    // [状態] - 8 経路の状態 1, 2, 0, 1, 2, 0, 1, 2 を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat,
                cplat_pinned_prompt_printf(_, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                           HasSubstr("[サービス 9] path[2] CONNECTED states={1,2,0,1,2,0,1,2}")))
        .WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - 標準出力のチャネルと、"[サービス 9] path[2] CONNECTED states={1,2,0,1,2,0,1,2}" を含む文字列を指定して cplat_pinned_prompt_printf() が 1 回呼び出されること。
    // [Pre-Assert手順] - cplat_pinned_prompt_printf() から CPLAT_OK を返す。
    EXPECT_CALL(mock_cplat,
                cplat_pinned_prompt_printf(_, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                           HasSubstr("[サービス 9] path[5] DISCONNECTED states={1,2,0,1,2,0,1,2}")))
        .WillOnce(Return(CPLAT_OK));
    // [Pre-Assert確認_正常系] - 標準出力のチャネルと、"[サービス 9] path[5] DISCONNECTED states={1,2,0,1,2,0,1,2}" を含む文字列を指定して cplat_pinned_prompt_printf() が 1 回呼び出されること。
    // [Pre-Assert手順] - cplat_pinned_prompt_printf() から CPLAT_OK を返す。

    // Act
    test_on_path_state_event(9, POTR_EVENT_PATH_CONNECTED, 2U, states);
    // [手順] - 経路 2 の CONNECTED を on_recv() へ渡す。
    test_on_path_state_event(9, POTR_EVENT_PATH_DISCONNECTED, 5U, states);
    // [手順] - 経路 5 の DISCONNECTED を on_recv() へ渡す。

    // Assert
    // [確認_正常系] - Pre-Assert の通り、接続と切断の両方で全経路の状態を順番どおりに表示すること。
}
