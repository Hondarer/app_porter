#include <cplat/base/platform.h>
#include <cplat/base/result.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_stdio.h>
#if defined(PLATFORM_WINDOWS)
    #include <mock_winsock.h>
#else
    #include <mock_unistd.h>
#endif
#include <wrapmain.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include "tcpServer.h"

// プラットフォームのフックは、テストの側で受けて呼び出しを記録する (プラットフォームのファイルは組み込まない)。
static int s_platform_cleanup_count = 0;
static int s_fork_server_port = -1;
static int s_prefork_server_port = -1;

static void reset_platform_records(void)
{
    s_platform_cleanup_count = 0;
    s_fork_server_port = -1;
    s_prefork_server_port = -1;
}

void platform_init(ClientSessionFn session_fn)
{
    g_session_fn = session_fn;
}

void platform_cleanup(void)
{
    s_platform_cleanup_count++;
}

int dispatch_internal_args(int, char *[])
{
    return 0;
}

void run_fork_server(int port)
{
    s_fork_server_port = port;
}

void run_prefork_server(int port, int, int)
{
    s_prefork_server_port = port;
}

// --port が下限より小さい場合に、サーバーを起動せず EXIT_FAILURE と診断を返すことの確認
TEST(tcpServerTest, rejects_port_below_range)
{
    // Arrange
    char arg0[] = "tcpServer";
    char arg1[] = "--port";
    char arg2[] = "0";
    char *argv[] = {arg0, arg1, arg2};
    reset_platform_records(); // [状態] - プラットフォームのフックの記録を初期化する。

    // Pre-Assert

    // Act
    testing::internal::CaptureStderr();
    int exit_code = __real_main(3, argv); // [手順] - --port 0 を指定して main() を実行する。
    std::string captured = testing::internal::GetCapturedStderr();

    // Assert
    EXPECT_EQ(EXIT_FAILURE, exit_code); // [確認_異常系] - main() の戻り値が EXIT_FAILURE であること。
    EXPECT_THAT(captured, HasSubstr("--port には 1 から 65535 を指定してください。")); // [確認_異常系] - 標準エラー出力が "--port には 1 から 65535 を指定してください。" を含む文字列であること。
    EXPECT_EQ(-1, s_fork_server_port);    // [確認_異常系] - run_fork_server() が呼び出されないこと。
    EXPECT_EQ(-1, s_prefork_server_port); // [確認_異常系] - run_prefork_server() が呼び出されないこと。
    EXPECT_EQ(1, s_platform_cleanup_count); // [確認_異常系] - platform_cleanup() が 1 回呼び出されること。
}

// --port が上限より大きい場合に、サーバーを起動せず EXIT_FAILURE と診断を返すことの確認
TEST(tcpServerTest, rejects_port_above_range)
{
    // Arrange
    char arg0[] = "tcpServer";
    char arg1[] = "--port";
    char arg2[] = "65536";
    char *argv[] = {arg0, arg1, arg2};
    reset_platform_records(); // [状態] - プラットフォームのフックの記録を初期化する。

    // Pre-Assert

    // Act
    testing::internal::CaptureStderr();
    int exit_code = __real_main(3, argv); // [手順] - --port 65536 を指定して main() を実行する。
    std::string captured = testing::internal::GetCapturedStderr();

    // Assert
    EXPECT_EQ(EXIT_FAILURE, exit_code); // [確認_異常系] - main() の戻り値が EXIT_FAILURE であること。
    EXPECT_THAT(captured, HasSubstr("--port には 1 から 65535 を指定してください。")); // [確認_異常系] - 標準エラー出力が "--port には 1 から 65535 を指定してください。" を含む文字列であること。
    EXPECT_EQ(-1, s_fork_server_port);    // [確認_異常系] - run_fork_server() が呼び出されないこと。
    EXPECT_EQ(-1, s_prefork_server_port); // [確認_異常系] - run_prefork_server() が呼び出されないこと。
    EXPECT_EQ(1, s_platform_cleanup_count); // [確認_異常系] - platform_cleanup() が 1 回呼び出されること。
}

// --port の下限ちょうどの値で、範囲の検査を通り prefork モードのサーバーを起動することの確認
TEST(tcpServerTest, accepts_port_lower_bound)
{
    // Arrange
    char arg0[] = "tcpServer";
    char arg1[] = "--port";
    char arg2[] = "1";
    char *argv[] = {arg0, arg1, arg2};
    reset_platform_records(); // [状態] - プラットフォームのフックの記録を初期化する。

    // Pre-Assert

    // Act
    int exit_code = __real_main(3, argv); // [手順] - --port 1 を指定して main() を実行する。

    // Assert
    EXPECT_EQ(EXIT_SUCCESS, exit_code);  // [確認_正常系] - main() の戻り値が EXIT_SUCCESS であること。
    EXPECT_EQ(1, s_prefork_server_port); // [確認_正常系] - run_prefork_server() に渡されたポートが 1 であること。
    EXPECT_EQ(-1, s_fork_server_port);   // [確認_正常系] - run_fork_server() が呼び出されないこと。
}

// --port の上限ちょうどの値で、範囲の検査を通り fork モードのサーバーを起動することの確認
TEST(tcpServerTest, accepts_port_upper_bound)
{
    // Arrange
    char arg0[] = "tcpServer";
    char arg1[] = "--mode";
    char arg2[] = "fork";
    char arg3[] = "--port";
    char arg4[] = "65535";
    char *argv[] = {arg0, arg1, arg2, arg3, arg4};
    reset_platform_records(); // [状態] - プラットフォームのフックの記録を初期化する。

    // Pre-Assert

    // Act
    int exit_code = __real_main(5, argv); // [手順] - --mode fork と --port 65535 を指定して main() を実行する。

    // Assert
    EXPECT_EQ(EXIT_SUCCESS, exit_code);    // [確認_正常系] - main() の戻り値が EXIT_SUCCESS であること。
    EXPECT_EQ(65535, s_fork_server_port);  // [確認_正常系] - run_fork_server() に渡されたポートが 65535 であること。
    EXPECT_EQ(-1, s_prefork_server_port); // [確認_正常系] - run_prefork_server() が呼び出されないこと。
}

// --help の使い方の表示の関数が失敗した場合に、EXIT_FAILURE と診断を返すことの確認
TEST(tcpServerTest, help_returns_failure_when_print_usage_fails)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    char arg0[] = "tcpServer";
    char arg1[] = "--help";
    char *argv[] = {arg0, arg1};
    reset_platform_records(); // [状態] - プラットフォームのフックの記録を初期化する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_argparser_print_usage(stdout))
        .WillOnce(Return(CPLAT_ERR_UNKNOWN)); // [Pre-Assert確認_異常系] - cplat_argparser_print_usage() が stdout を指定して 1 回呼び出されること。
                                              // [Pre-Assert手順] - cplat_argparser_print_usage() から CPLAT_ERR_UNKNOWN を返す。

    // Act
    testing::internal::CaptureStderr();
    int exit_code = __real_main(2, argv); // [手順] - --help を指定して main() を実行する。
    std::string captured = testing::internal::GetCapturedStderr();

    // Assert
    EXPECT_EQ(EXIT_FAILURE, exit_code); // [確認_異常系] - main() の戻り値が EXIT_FAILURE であること。
    EXPECT_THAT(captured, HasSubstr("使い方を標準出力へ書き込めませんでした。")); // [確認_異常系] - 標準エラー出力が "使い方を標準出力へ書き込めませんでした。" を含む文字列であること。
    EXPECT_EQ(1, s_platform_cleanup_count); // [確認_異常系] - platform_cleanup() が 1 回呼び出されること。
}

// --help の標準出力のフラッシュが失敗した場合に、EXIT_FAILURE と診断を返すことの確認
TEST(tcpServerTest, help_returns_failure_when_fflush_fails)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    char arg0[] = "tcpServer";
    char arg1[] = "--help";
    char *argv[] = {arg0, arg1};
    reset_platform_records(); // [状態] - プラットフォームのフックの記録を初期化する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_argparser_print_usage(stdout))
        .WillOnce(Return(CPLAT_OK)); // [Pre-Assert確認_異常系] - cplat_argparser_print_usage() が stdout を指定して 1 回呼び出されること。
                                     // [Pre-Assert手順] - cplat_argparser_print_usage() から CPLAT_OK を返す。
    EXPECT_CALL(mock_stdio, fflush(_, _, _, stdout))
        .WillOnce(Return(EOF)); // [Pre-Assert確認_異常系] - fflush() が stdout を指定して 1 回呼び出されること。
                                // [Pre-Assert手順] - fflush() から EOF を返す。

    // Act
    testing::internal::CaptureStderr();
    int exit_code = __real_main(2, argv); // [手順] - --help を指定して main() を実行する。
    std::string captured = testing::internal::GetCapturedStderr();

    // Assert
    EXPECT_EQ(EXIT_FAILURE, exit_code); // [確認_異常系] - main() の戻り値が EXIT_FAILURE であること。
    EXPECT_THAT(captured, HasSubstr("使い方を標準出力へ書き込めませんでした。")); // [確認_異常系] - 標準エラー出力が "使い方を標準出力へ書き込めませんでした。" を含む文字列であること。
    EXPECT_EQ(1, s_platform_cleanup_count); // [確認_異常系] - platform_cleanup() が 1 回呼び出されること。
}

// --help の標準出力にエラーが残っている場合に、EXIT_FAILURE と診断を返すことの確認
TEST(tcpServerTest, help_returns_failure_when_ferror_is_set)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    char arg0[] = "tcpServer";
    char arg1[] = "--help";
    char *argv[] = {arg0, arg1};
    reset_platform_records(); // [状態] - プラットフォームのフックの記録を初期化する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_argparser_print_usage(stdout))
        .WillOnce(Return(CPLAT_OK)); // [Pre-Assert確認_異常系] - cplat_argparser_print_usage() が stdout を指定して 1 回呼び出されること。
                                     // [Pre-Assert手順] - cplat_argparser_print_usage() から CPLAT_OK を返す。
    EXPECT_CALL(mock_stdio, fflush(_, _, _, stdout))
        .WillOnce(Return(0)); // [Pre-Assert確認_異常系] - fflush() が stdout を指定して 1 回呼び出されること。
                              // [Pre-Assert手順] - fflush() から 0 を返す。
    EXPECT_CALL(mock_stdio, ferror(_, _, _, stdout))
        .WillOnce(Return(1)); // [Pre-Assert確認_異常系] - ferror() が stdout を指定して 1 回呼び出されること。
                              // [Pre-Assert手順] - ferror() から 1 を返す。

    // Act
    testing::internal::CaptureStderr();
    int exit_code = __real_main(2, argv); // [手順] - --help を指定して main() を実行する。
    std::string captured = testing::internal::GetCapturedStderr();

    // Assert
    EXPECT_EQ(EXIT_FAILURE, exit_code); // [確認_異常系] - main() の戻り値が EXIT_FAILURE であること。
    EXPECT_THAT(captured, HasSubstr("使い方を標準出力へ書き込めませんでした。")); // [確認_異常系] - 標準エラー出力が "使い方を標準出力へ書き込めませんでした。" を含む文字列であること。
    EXPECT_EQ(1, s_platform_cleanup_count); // [確認_異常系] - platform_cleanup() が 1 回呼び出されること。
}

// --help の表示と書き込みが成功した場合に、EXIT_SUCCESS を返しサーバーを起動しないことの確認
TEST(tcpServerTest, help_returns_success_when_output_succeeds)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    char arg0[] = "tcpServer";
    char arg1[] = "--help";
    char *argv[] = {arg0, arg1};
    reset_platform_records(); // [状態] - プラットフォームのフックの記録を初期化する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_argparser_print_usage(stdout))
        .WillOnce(Return(CPLAT_OK)); // [Pre-Assert確認_正常系] - cplat_argparser_print_usage() が stdout を指定して 1 回呼び出されること。
                                     // [Pre-Assert手順] - cplat_argparser_print_usage() から CPLAT_OK を返す。
    EXPECT_CALL(mock_stdio, fflush(_, _, _, stdout))
        .WillOnce(Return(0)); // [Pre-Assert確認_正常系] - fflush() が stdout を指定して 1 回呼び出されること。
                              // [Pre-Assert手順] - fflush() から 0 を返す。
    EXPECT_CALL(mock_stdio, ferror(_, _, _, stdout))
        .WillOnce(Return(0)); // [Pre-Assert確認_正常系] - ferror() が stdout を指定して 1 回呼び出されること。
                              // [Pre-Assert手順] - ferror() から 0 を返す。

    // Act
    int exit_code = __real_main(2, argv); // [手順] - --help を指定して main() を実行する。

    // Assert
    EXPECT_EQ(EXIT_SUCCESS, exit_code);     // [確認_正常系] - main() の戻り値が EXIT_SUCCESS であること。
    EXPECT_EQ(1, s_platform_cleanup_count); // [確認_正常系] - platform_cleanup() が 1 回呼び出されること。
    EXPECT_EQ(-1, s_fork_server_port);      // [確認_正常系] - run_fork_server() が呼び出されないこと。
    EXPECT_EQ(-1, s_prefork_server_port);   // [確認_正常系] - run_prefork_server() が呼び出されないこと。
}

#if defined(PLATFORM_WINDOWS)

// エコーの送信が失敗した場合に、診断を出して受信のループを抜け、接続を閉じることの確認
TEST(tcpServerTest, session_stops_when_send_fails)
{
    // Arrange
    NiceMock<Mock_winsock> mock_winsock;
    const SOCKET client = (SOCKET)123; // [状態] - クライアントのソケットの値を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_winsock, recv(_, _, _, client, _, _, 0))
        .WillOnce(Invoke(
            [](const char *, const int, const char *, SOCKET, char *buf, int, int) -> int
            {
                memcpy(buf, "hello", 5);
                return 5;
            })); // [Pre-Assert確認_異常系] - recv() が client、0 を指定して 1 回呼び出されること。
                 // [Pre-Assert手順] - recv() から、"hello" を出力バッファーに設定して 5 を返す。
    EXPECT_CALL(mock_winsock, send(_, _, _, client, _, 5, 0))
        .WillOnce(Return(SOCKET_ERROR)); // [Pre-Assert確認_異常系] - send() が client、5、0 を指定して 1 回呼び出されること。
                                         // [Pre-Assert手順] - send() から SOCKET_ERROR を返す。
    EXPECT_CALL(mock_winsock, closesocket(_, _, _, client))
        .WillOnce(Return(0)); // [Pre-Assert確認_異常系] - closesocket() が client を指定して 1 回呼び出されること。
                              // [Pre-Assert手順] - closesocket() から 0 を返す。

    // Act
    testing::internal::CaptureStderr();
    handle_client_session(client); // [手順] - handle_client_session() を呼び出す。
    std::string captured = testing::internal::GetCapturedStderr();

    // Assert
    EXPECT_THAT(captured, HasSubstr("送信に失敗しました")); // [確認_異常系] - 標準エラー出力が "送信に失敗しました" を含む文字列であること。
}

// エコーの送信が一部だけの場合に、診断を出して受信のループを抜け、接続を閉じることの確認
TEST(tcpServerTest, session_stops_when_send_is_partial)
{
    // Arrange
    NiceMock<Mock_winsock> mock_winsock;
    const SOCKET client = (SOCKET)123; // [状態] - クライアントのソケットの値を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_winsock, recv(_, _, _, client, _, _, 0))
        .WillOnce(Invoke(
            [](const char *, const int, const char *, SOCKET, char *buf, int, int) -> int
            {
                memcpy(buf, "hello", 5);
                return 5;
            })); // [Pre-Assert確認_異常系] - recv() が client、0 を指定して 1 回呼び出されること。
                 // [Pre-Assert手順] - recv() から、"hello" を出力バッファーに設定して 5 を返す。
    EXPECT_CALL(mock_winsock, send(_, _, _, client, _, 5, 0))
        .WillOnce(Return(2)); // [Pre-Assert確認_異常系] - send() が client、5、0 を指定して 1 回呼び出されること。
                              // [Pre-Assert手順] - send() から、一部だけの送信を表す 2 を返す。
    EXPECT_CALL(mock_winsock, closesocket(_, _, _, client))
        .WillOnce(Return(0)); // [Pre-Assert確認_異常系] - closesocket() が client を指定して 1 回呼び出されること。
                              // [Pre-Assert手順] - closesocket() から 0 を返す。

    // Act
    testing::internal::CaptureStderr();
    handle_client_session(client); // [手順] - handle_client_session() を呼び出す。
    std::string captured = testing::internal::GetCapturedStderr();

    // Assert
    EXPECT_THAT(captured, HasSubstr("送信に失敗しました")); // [確認_異常系] - 標準エラー出力が "送信に失敗しました" を含む文字列であること。
}

#else // PLATFORM_WINDOWS

// エコーの送信が失敗した場合に、診断を出して受信のループを抜け、接続を閉じることの確認
TEST(tcpServerTest, session_stops_when_send_fails)
{
    // Arrange
    NiceMock<Mock_unistd> mock_unistd;
    NiceMock<Mock_cplat> mock_cplat;
    const int client = 123; // [状態] - クライアントの記述子の値を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_unistd, read(_, _, _, client, _, _))
        .WillOnce(Invoke(
            [](const char *, const int, const char *, int, void *buf, size_t) -> ssize_t
            {
                memcpy(buf, "hello", 5);
                return 5;
            })); // [Pre-Assert確認_異常系] - read() が client を指定して 1 回呼び出されること。
                 // [Pre-Assert手順] - read() から、"hello" を出力バッファーに設定して 5 を返す。
    EXPECT_CALL(mock_unistd, write(_, _, _, client, _, 5U))
        .WillOnce(Return((ssize_t)-1)); // [Pre-Assert確認_異常系] - write() が client、5U を指定して 1 回呼び出されること。
                                        // [Pre-Assert手順] - write() から -1 を返す。
    EXPECT_CALL(mock_cplat, cplat_close(client, nullptr))
        .WillOnce(Return(CPLAT_OK)); // [Pre-Assert確認_異常系] - cplat_close() が client、nullptr を指定して 1 回呼び出されること。
                                     // [Pre-Assert手順] - cplat_close() から CPLAT_OK を返す。

    // Act
    testing::internal::CaptureStderr();
    handle_client_session(client); // [手順] - handle_client_session() を呼び出す。
    std::string captured = testing::internal::GetCapturedStderr();

    // Assert
    EXPECT_THAT(captured, HasSubstr("送信に失敗しました")); // [確認_異常系] - 標準エラー出力が "送信に失敗しました" を含む文字列であること。
}

// エコーの送信が一部だけの場合に、診断を出して受信のループを抜け、接続を閉じることの確認
TEST(tcpServerTest, session_stops_when_send_is_partial)
{
    // Arrange
    NiceMock<Mock_unistd> mock_unistd;
    NiceMock<Mock_cplat> mock_cplat;
    const int client = 123; // [状態] - クライアントの記述子の値を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_unistd, read(_, _, _, client, _, _))
        .WillOnce(Invoke(
            [](const char *, const int, const char *, int, void *buf, size_t) -> ssize_t
            {
                memcpy(buf, "hello", 5);
                return 5;
            })); // [Pre-Assert確認_異常系] - read() が client を指定して 1 回呼び出されること。
                 // [Pre-Assert手順] - read() から、"hello" を出力バッファーに設定して 5 を返す。
    EXPECT_CALL(mock_unistd, write(_, _, _, client, _, 5U))
        .WillOnce(Return((ssize_t)2)); // [Pre-Assert確認_異常系] - write() が client、5U を指定して 1 回呼び出されること。
                                       // [Pre-Assert手順] - write() から、一部だけの送信を表す 2 を返す。
    EXPECT_CALL(mock_cplat, cplat_close(client, nullptr))
        .WillOnce(Return(CPLAT_OK)); // [Pre-Assert確認_異常系] - cplat_close() が client、nullptr を指定して 1 回呼び出されること。
                                     // [Pre-Assert手順] - cplat_close() から CPLAT_OK を返す。

    // Act
    testing::internal::CaptureStderr();
    handle_client_session(client); // [手順] - handle_client_session() を呼び出す。
    std::string captured = testing::internal::GetCapturedStderr();

    // Assert
    EXPECT_THAT(captured, HasSubstr("送信に失敗しました")); // [確認_異常系] - 標準エラー出力が "送信に失敗しました" を含む文字列であること。
}

#endif // PLATFORM_WINDOWS
