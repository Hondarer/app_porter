#define _HAS_STD_BYTE 0
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_windows.h>
#include <mock_winsock.h>
#include <mock_stdio.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include "tcpServer.h"
#include "tcpServer_windows.inject.h"

// worker_loop() が親プロセスへ返す応答 (WorkerReply と同じ配置)。
struct test_worker_reply
{
    DWORD type;
    DWORD accepted;
};

// fork モードの death test の子プロセスで、呼び出しを記録する。
static SOCKET s_fork_client = INVALID_SOCKET;
static int s_fork_accept_count = 0;
static int s_fork_client_close_count = 0;
static int s_fork_create_process_called = 0;
static int s_fork_expect_process_failure = 0;
static int s_fork_process_close_count = 0;

// プロセスとテストごとに異なるパイプ名を作る。
static std::string make_test_pipe_name(const char *suffix)
{
    return std::string("\\\\.\\pipe\\tcpserver_test_") + std::to_string(GetCurrentProcessId()) + "_" + suffix;
}

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

// 多重接続のワーカーでエコーの送信が失敗した場合に、診断を出して接続を閉じ、親へ完了を通知することの確認
TEST(tcpServerWindowsTest, worker_closes_connection_when_send_fails)
{
    // Arrange
    NiceMock<Mock_winsock> mock_winsock;
    WSADATA wsa_data;
    ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &wsa_data)); // [状態] - Winsock を初期化する。
                                                         // [状態確認] - WSAStartup() の戻り値が 0 であること。
    SOCKET original = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // [状態] - ワーカーへ渡す TCP ソケットを作成する。
    ASSERT_NE(INVALID_SOCKET, original); // [状態確認] - socket() の戻り値が INVALID_SOCKET 以外であること。
    WSAPROTOCOL_INFOW protocol_info;
    ASSERT_EQ(0, WSADuplicateSocketW(original, GetCurrentProcessId(), &protocol_info)); // [状態] - ソケットを自プロセス向けに複製する情報を作る。
                                                                                         // [状態確認] - WSADuplicateSocketW() の戻り値が 0 であること。
    std::string pipe_name = make_test_pipe_name("send");
    HANDLE server_pipe = CreateNamedPipeA(pipe_name.c_str(), PIPE_ACCESS_DUPLEX,
                                          PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0,
                                          NULL); // [状態] - 親プロセス側の名前付きパイプを作成する。
    ASSERT_NE(INVALID_HANDLE_VALUE, server_pipe); // [状態確認] - CreateNamedPipeA() の戻り値が INVALID_HANDLE_VALUE 以外であること。
    SOCKET received_socket = INVALID_SOCKET;
    SOCKET closed_socket = INVALID_SOCKET;
    test_worker_reply ack_reply = {};
    test_worker_reply done_reply = {};
    DWORD transferred = 0;

    // Pre-Assert
    EXPECT_CALL(mock_winsock, select(_, _, _, 0, NotNull(), nullptr, nullptr, NotNull()))
        .WillOnce(Return(1)); // [Pre-Assert確認_異常系] - select() が 0、nullptr 以外、nullptr、nullptr、nullptr 以外を指定して 1 回呼び出されること。
                              // [Pre-Assert手順] - select() から、読み込み可能なソケットが 1 個あることを表す 1 を返す。
    EXPECT_CALL(mock_winsock, recv(_, _, _, _, _, (int)BUFFER_SIZE, 0))
        .WillOnce(Invoke(
            [&](const char *, const int, const char *, SOCKET s, char *buf, int, int) -> int
            {
                received_socket = s;
                memcpy(buf, "hello", 5);
                return 5;
            })); // [Pre-Assert確認_異常系] - recv() が BUFFER_SIZE、0 を指定して 1 回呼び出されること。
                 // [Pre-Assert手順] - recv() から、ソケットを記録し、"hello" を出力バッファーに設定して 5 を返す。
    EXPECT_CALL(mock_winsock, send(_, _, _, _, _, 5, 0))
        .WillOnce(Return(SOCKET_ERROR)); // [Pre-Assert確認_異常系] - send() が 5、0 を指定して 1 回呼び出されること。
                                         // [Pre-Assert手順] - send() から SOCKET_ERROR を返す。
    EXPECT_CALL(mock_winsock, closesocket(_, _, _, _))
        .WillOnce(Invoke(
            [&](const char *file, const int line, const char *func, SOCKET s) -> int
            {
                closed_socket = s;
                return delegate_real_closesocket(file, line, func, s);
            })); // [Pre-Assert確認_異常系] - closesocket() が 1 回呼び出されること。
                 // [Pre-Assert手順] - closesocket() から、ソケットを記録して実関数の結果を返す。

    // Act
    testing::internal::CaptureStderr();
    std::thread worker(tcpserver_test_worker_loop, pipe_name.c_str(), 2); // [手順] - 別スレッドで worker_loop() を同時接続数 2 で実行する。
    BOOL connected = ConnectNamedPipe(server_pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED; // [手順] - ワーカーのパイプ接続を待つ。
    BOOL wrote_info = WriteFile(server_pipe, &protocol_info, sizeof(protocol_info), &transferred, NULL); // [手順] - ソケットの複製情報をワーカーへ送る。
    BOOL read_ack = ReadFile(server_pipe, &ack_reply, sizeof(ack_reply), &transferred, NULL); // [手順] - ワーカーの受領通知を読む。
    BOOL read_done = ReadFile(server_pipe, &done_reply, sizeof(done_reply), &transferred, NULL); // [手順] - ワーカーの完了通知を読む。
    (void)DisconnectNamedPipe(server_pipe);
    (void)CloseHandle(server_pipe); // [手順] - パイプを閉じてワーカーのループを終える。
    worker.join();
    std::string captured = testing::internal::GetCapturedStderr();

    // Assert
    EXPECT_TRUE(connected);  // [確認_正常系] - ワーカーがパイプへ接続すること。
    EXPECT_TRUE(wrote_info); // [確認_正常系] - 複製情報をワーカーへ送れること。
    EXPECT_TRUE(read_ack);   // [確認_正常系] - 受領通知を読めること。
    EXPECT_EQ(1U, ack_reply.type);     // [確認_正常系] - 受領通知の type が 1 (WORKER_ACK) であること。
    EXPECT_EQ(1U, ack_reply.accepted); // [確認_正常系] - 受領通知の accepted が 1 であること。
    EXPECT_TRUE(read_done);            // [確認_異常系] - 完了通知を読めること。
    EXPECT_EQ(2U, done_reply.type);    // [確認_異常系] - 完了通知の type が 2 (WORKER_DONE) であること。
    EXPECT_NE(INVALID_SOCKET, received_socket); // [確認_異常系] - recv() に渡されたソケットが INVALID_SOCKET 以外であること。
    EXPECT_EQ(received_socket, closed_socket);  // [確認_異常系] - closesocket() に渡されたソケットが recv() に渡されたソケットと等しいこと。
    EXPECT_THAT(captured, HasSubstr("send 失敗")); // [確認_異常系] - 標準エラー出力が "send 失敗" を含む文字列であること。

    // Cleanup
    (void)closesocket(original);
    (void)WSACleanup();
}

// 多重接続のワーカーで select() が失敗した場合に、診断を出してループを抜け、接続を閉じて戻ることの確認
TEST(tcpServerWindowsTest, worker_returns_when_select_fails)
{
    // Arrange
    NiceMock<Mock_winsock> mock_winsock;
    WSADATA wsa_data;
    ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &wsa_data)); // [状態] - Winsock を初期化する。
                                                         // [状態確認] - WSAStartup() の戻り値が 0 であること。
    SOCKET original = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // [状態] - ワーカーへ渡す TCP ソケットを作成する。
    ASSERT_NE(INVALID_SOCKET, original); // [状態確認] - socket() の戻り値が INVALID_SOCKET 以外であること。
    WSAPROTOCOL_INFOW protocol_info;
    ASSERT_EQ(0, WSADuplicateSocketW(original, GetCurrentProcessId(), &protocol_info)); // [状態] - ソケットを自プロセス向けに複製する情報を作る。
                                                                                         // [状態確認] - WSADuplicateSocketW() の戻り値が 0 であること。
    std::string pipe_name = make_test_pipe_name("select");
    HANDLE server_pipe = CreateNamedPipeA(pipe_name.c_str(), PIPE_ACCESS_DUPLEX,
                                          PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0,
                                          NULL); // [状態] - 親プロセス側の名前付きパイプを作成する。
    ASSERT_NE(INVALID_HANDLE_VALUE, server_pipe); // [状態確認] - CreateNamedPipeA() の戻り値が INVALID_HANDLE_VALUE 以外であること。
    SOCKET selected_socket = INVALID_SOCKET;
    SOCKET closed_socket = INVALID_SOCKET;
    test_worker_reply ack_reply = {};
    DWORD transferred = 0;

    // Pre-Assert
    EXPECT_CALL(mock_winsock, select(_, _, _, 0, NotNull(), nullptr, nullptr, NotNull()))
        .WillOnce(Invoke(
            [&](const char *, const int, const char *, int, fd_set *read_fds, fd_set *, fd_set *,
                const struct timeval *) -> int
            {
                selected_socket = read_fds->fd_array[0];
                return SOCKET_ERROR;
            })); // [Pre-Assert確認_異常系] - select() が 0、nullptr 以外、nullptr、nullptr、nullptr 以外を指定して 1 回呼び出されること。
                 // [Pre-Assert手順] - select() から、監視対象のソケットを記録して SOCKET_ERROR を返す。
    EXPECT_CALL(mock_winsock, recv(_, _, _, _, _, _, _))
        .Times(0); // [Pre-Assert確認_異常系] - recv() が呼び出されないこと。
    EXPECT_CALL(mock_winsock, closesocket(_, _, _, _))
        .WillOnce(Invoke(
            [&](const char *file, const int line, const char *func, SOCKET s) -> int
            {
                closed_socket = s;
                return delegate_real_closesocket(file, line, func, s);
            })); // [Pre-Assert確認_異常系] - closesocket() が 1 回呼び出されること。
                 // [Pre-Assert手順] - closesocket() から、ソケットを記録して実関数の結果を返す。

    // Act
    testing::internal::CaptureStderr();
    std::thread worker(tcpserver_test_worker_loop, pipe_name.c_str(), 2); // [手順] - 別スレッドで worker_loop() を同時接続数 2 で実行する。
    BOOL connected = ConnectNamedPipe(server_pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED; // [手順] - ワーカーのパイプ接続を待つ。
    BOOL wrote_info = WriteFile(server_pipe, &protocol_info, sizeof(protocol_info), &transferred, NULL); // [手順] - ソケットの複製情報をワーカーへ送る。
    BOOL read_ack = ReadFile(server_pipe, &ack_reply, sizeof(ack_reply), &transferred, NULL); // [手順] - ワーカーの受領通知を読む。
    worker.join(); // [手順] - worker_loop() が自ら戻るまで待つ。
    std::string captured = testing::internal::GetCapturedStderr();

    // Assert
    EXPECT_TRUE(connected);          // [確認_正常系] - ワーカーがパイプへ接続すること。
    EXPECT_TRUE(wrote_info);         // [確認_正常系] - 複製情報をワーカーへ送れること。
    EXPECT_TRUE(read_ack);           // [確認_正常系] - 受領通知を読めること。
    EXPECT_EQ(1U, ack_reply.type);   // [確認_正常系] - 受領通知の type が 1 (WORKER_ACK) であること。
    EXPECT_NE(INVALID_SOCKET, selected_socket); // [確認_異常系] - select() の監視対象のソケットが INVALID_SOCKET 以外であること。
    EXPECT_EQ(selected_socket, closed_socket);  // [確認_異常系] - closesocket() に渡されたソケットが監視対象のソケットと等しいこと。
    EXPECT_THAT(captured, HasSubstr("select 失敗")); // [確認_異常系] - 標準エラー出力が "select 失敗" を含む文字列であること。

    // Cleanup
    (void)DisconnectNamedPipe(server_pipe);
    (void)CloseHandle(server_pipe);
    (void)closesocket(original);
    (void)WSACleanup();
}

// fork モードでソケットの継承の設定が失敗した場合に、子プロセスを起動せず接続を閉じて次の接続へ進むことの確認
TEST(tcpServerWindowsTest, fork_server_skips_child_when_inherit_flag_fails)
{
    // Arrange
    // EXPECT_EXIT の子プロセスは exit で mock を破棄しないため、子プロセス側の mock 登録を解放対象外にする。
    NiceMock<Mock_winsock> mock_winsock;
    NiceMock<Mock_windows> mock_windows;
    NiceMock<Mock_cplat> mock_cplat;
    testing::Mock::AllowLeak(&mock_winsock);
    testing::Mock::AllowLeak(&mock_windows);
    testing::Mock::AllowLeak(&mock_cplat);
    WSADATA wsa_data;
    ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &wsa_data)); // [状態] - Winsock を初期化する。
                                                         // [状態確認] - WSAStartup() の戻り値が 0 であること。
    s_fork_client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // [状態] - accept() が返すクライアントのソケットを作成する。
    ASSERT_NE(INVALID_SOCKET, s_fork_client); // [状態確認] - socket() の戻り値が INVALID_SOCKET 以外であること。
    s_fork_accept_count = 0;
    s_fork_client_close_count = 0;
    s_fork_create_process_called = 0; // [状態] - 呼び出しの記録を初期化する。
    s_fork_expect_process_failure = 0;
    ON_CALL(mock_winsock, accept(_, _, _, _, _, _))
        .WillByDefault(Invoke(
            [](const char *, const int, const char *, SOCKET, struct sockaddr *, int *) -> SOCKET
            {
                if (s_fork_accept_count++ == 0)
                {
                    return s_fork_client;
                }
                int process_failure_matches = s_fork_expect_process_failure ? s_fork_create_process_called
                                                                            : !s_fork_create_process_called;
                exit((s_fork_client_close_count == 1 && process_failure_matches) ? 3 : 4);
            })); // [状態] - accept() から、1 回目はクライアントのソケットを返し、2 回目は接続を 1 回だけ閉じ、期待する起動結果を確認した場合に終了コード 3 (それ以外は 4) で終了するように既定動作を設定する。
    ON_CALL(mock_windows, SetHandleInformation(_, _, _, _, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
        .WillByDefault(Return(FALSE)); // [状態] - SetHandleInformation() から FALSE を返すように既定動作を設定する。
    ON_CALL(mock_winsock, closesocket(_, _, _, _))
        .WillByDefault(Invoke(
            [](const char *file, const int line, const char *func, SOCKET s) -> int
            {
                if (s == s_fork_client)
                {
                    ++s_fork_client_close_count;
                }
                return delegate_real_closesocket(file, line, func, s);
            })); // [状態] - closesocket() から、クライアントのソケットを閉じたことを記録して実関数の結果を返すように既定動作を設定する。
    ON_CALL(mock_cplat, CreateProcessU(_, _, _, _, _, _, _, _, _, _))
        .WillByDefault(Invoke(
            [](const char *, const char *, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID,
               const char *, LPSTARTUPINFOW, LPPROCESS_INFORMATION) -> BOOL
            {
                s_fork_create_process_called = 1;
                return FALSE;
            })); // [状態] - CreateProcessU() から、呼び出しを記録して FALSE を返すように既定動作を設定する。

    // Pre-Assert

    // Act
    // [手順] - 子プロセスで run_fork_server() をポート 0 で実行する。
    // Assert
    EXPECT_EXIT(run_fork_server(0), ::testing::ExitedWithCode(3), "SetHandleInformation");
    // [確認_異常系] - 子プロセスが終了コード 3 で終了し、標準エラー出力が "SetHandleInformation" を含むこと。(接続を閉じ、子プロセスを起動せずに次の accept() へ進むことの確認)

    // Cleanup
    (void)closesocket(s_fork_client);
    s_fork_client = INVALID_SOCKET;
    (void)WSACleanup();
}

// fork モードで実行ファイルのパスを取得できない場合に、診断を出して終了コード 1 で終了することの確認
TEST(tcpServerWindowsTest, fork_server_exits_when_module_path_fails)
{
    // Arrange
    // EXPECT_EXIT の子プロセスは exit で mock を破棄しないため、子プロセス側の mock 登録を解放対象外にする。
    NiceMock<Mock_cplat> mock_cplat;
    testing::Mock::AllowLeak(&mock_cplat);
    WSADATA wsa_data;
    ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &wsa_data)); // [状態] - Winsock を初期化する。
                                                         // [状態確認] - WSAStartup() の戻り値が 0 であること。
    ON_CALL(mock_cplat, GetModuleFileNameU(nullptr, _, _))
        .WillByDefault(Invoke(
            [](HMODULE, char *, DWORD) -> DWORD
            {
                SetLastError(ERROR_ACCESS_DENIED);
                return 0U;
            })); // [状態] - GetModuleFileNameU() から失敗を返し、直後に取得する OS エラーを設定する。

    // Pre-Assert

    // Act
    // [手順] - 子プロセスで run_fork_server() をポート 0 で実行する。
    // Assert
    EXPECT_EXIT(run_fork_server(0), ::testing::ExitedWithCode(1), "パスを取得できません");
    // [確認_異常系] - 子プロセスが終了コード 1 で終了し、標準エラー出力がパス取得失敗の診断を含むこと。

    // Cleanup
    (void)WSACleanup();
}

// prefork モードで実行ファイルのパスを取得できない場合に、診断を出して終了コード 1 で終了することの確認
TEST(tcpServerWindowsTest, prefork_server_exits_when_module_path_fails)
{
    // Arrange
    // EXPECT_EXIT の子プロセスは exit で mock を破棄しないため、子プロセス側の mock 登録を解放対象外にする。
    NiceMock<Mock_cplat> mock_cplat;
    testing::Mock::AllowLeak(&mock_cplat);
    WSADATA wsa_data;
    ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &wsa_data)); // [状態] - Winsock を初期化する。
                                                         // [状態確認] - WSAStartup() の戻り値が 0 であること。
    ON_CALL(mock_cplat, GetModuleFileNameU(nullptr, _, _))
        .WillByDefault(Invoke(
            [](HMODULE, char *, DWORD) -> DWORD
            {
                SetLastError(ERROR_ACCESS_DENIED);
                return 0U;
            })); // [状態] - GetModuleFileNameU() から失敗を返し、直後に取得する OS エラーを設定する。

    // Pre-Assert

    // Act
    // [手順] - 子プロセスで run_prefork_server() をポート 0、ワーカー数 1、同時接続数 1 で実行する。
    // Assert
    EXPECT_EXIT(run_prefork_server(0, 1, 1), ::testing::ExitedWithCode(1), "パスを取得できません.*GetLastError=5");
    // [確認_異常系] - 子プロセスが終了コード 1 で終了し、標準エラー出力がパス取得失敗の診断を含むこと。

    // Cleanup
    (void)WSACleanup();
}

// 空きワーカーの予約でイベントをリセットできない場合に、診断を出して終了コード 1 で終了することの確認
TEST(tcpServerWindowsTest, find_worker_exits_when_reset_event_fails)
{
    // Arrange
    // EXPECT_EXIT の子プロセスは exit で mock を破棄しないため、子プロセス側の mock 登録を解放対象外にする。
    NiceMock<Mock_windows> mock_windows;
    testing::Mock::AllowLeak(&mock_windows);
    ON_CALL(mock_windows, ResetEvent(_, _, _, _))
        .WillByDefault(Return(FALSE)); // [状態] - ResetEvent() から FALSE を返すように既定動作を設定する。

    // Pre-Assert

    // Act
    // [手順] - 子プロセスで、1 ワーカーが空いている状態の find_available_worker() を呼び出す。
    // Assert
    EXPECT_EXIT(tcpserver_test_find_available_worker(), ::testing::ExitedWithCode(1), "ResetEvent");
    // [確認_異常系] - 子プロセスが終了コード 1 で終了し、標準エラー出力が "ResetEvent" を含むこと。
}

// 監視スレッドが受領通知のイベントを設定できない場合に、診断を出して終了コード 1 で終了することの確認
TEST(tcpServerWindowsTest, monitor_exits_when_set_event_fails)
{
    // Arrange
    // EXPECT_EXIT の子プロセスは exit で mock を破棄しないため、子プロセス側の mock 登録を解放対象外にする。
    NiceMock<Mock_windows> mock_windows;
    testing::Mock::AllowLeak(&mock_windows);
    ON_CALL(mock_windows, SetEvent(_, _, _, _))
        .WillByDefault(Return(FALSE)); // [状態] - SetEvent() から FALSE を返すように既定動作を設定する。
    std::string pipe_name = make_test_pipe_name("monitor");

    // Pre-Assert

    // Act
    // [手順] - 子プロセスで、受領通知 (ACK) を 1 件書いたパイプの worker_monitor_thread() を呼び出す。
    // Assert
    EXPECT_EXIT(tcpserver_test_worker_monitor_ack(pipe_name.c_str()), ::testing::ExitedWithCode(1), "SetEvent");
    // [確認_異常系] - 子プロセスが終了コード 1 で終了し、標準エラー出力が "SetEvent" を含むこと。
}

// 実行ファイルのパス取得が拡張パス容量を使い、パス表記をそのまま返すことの確認
TEST(tcpServerWindowsTest, module_path_uses_extended_capacity_and_preserves_path)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const DWORD expected_capacity = static_cast<DWORD>(32767U * 3U + 1U);
    const std::string expected_path = "\\\\?\\C:\\Program Files\\長い実行ファイル.exe";
    // [状態] - 拡張パス接頭辞、空白、非 ASCII 文字を含む実行ファイル パスを用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_malloc(expected_capacity)).WillOnce(Invoke(delegate_real_cplat_malloc));
    // [Pre-Assert確認_正常系] - cplat_malloc() が拡張パス容量を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - cplat_malloc() から実関数へ委譲する。
    EXPECT_CALL(mock_cplat, GetModuleFileNameU(nullptr, NotNull(), expected_capacity))
        .WillOnce(Invoke(
            [&](HMODULE, char *path, DWORD) -> DWORD
            {
                memcpy(path, expected_path.c_str(), expected_path.size() + 1U);
                return static_cast<DWORD>(expected_path.size());
            })); // [Pre-Assert確認_正常系] - GetModuleFileNameU() が拡張パス容量を指定して 1 回呼び出されること。
                 // [Pre-Assert手順] - GetModuleFileNameU() から用意した UTF-8 パスの文字数を返す。

    // Act
    char *actual_path = nullptr;
    DWORD actual_result = tcpserver_test_get_native_executable_path(&actual_path);
    // [手順] - 拡張パスの容量を使って実行ファイルのパスを取得する。

    // Assert
    EXPECT_EQ(ERROR_SUCCESS, actual_result); // [確認_正常系] - 結果が ERROR_SUCCESS であること。
    ASSERT_NE(nullptr, actual_path); // [確認_正常系] - 成功したパスの所有権が返されること。
    EXPECT_STREQ(expected_path.c_str(), actual_path); // [確認_正常系] - 接頭辞、区切り、大小文字、空白、非 ASCII 文字が保たれること。

    // Cleanup
    cplat_free(actual_path);
}

// 実行ファイルのパス用領域の確保に失敗した場合はパス取得を呼ばないことの確認
TEST(tcpServerWindowsTest, module_path_returns_out_of_memory_when_allocation_fails)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const DWORD expected_capacity = static_cast<DWORD>(32767U * 3U + 1U);
    char *actual_path = reinterpret_cast<char *>(1);
    // [状態] - 出力を NULL 以外にして、初期化も確認できるようにする。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_malloc(expected_capacity)).WillOnce(Return(nullptr));
    // [Pre-Assert確認_異常系] - cplat_malloc() が拡張パス容量で 1 回呼び出されること。
    // [Pre-Assert手順] - cplat_malloc() から nullptr を返す。
    EXPECT_CALL(mock_cplat, GetModuleFileNameU(_, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - 確保失敗後に GetModuleFileNameU() が呼び出されないこと。

    // Act
    DWORD actual_result = tcpserver_test_get_native_executable_path(&actual_path);
    // [手順] - パス用領域を確保できない状態で実行ファイルのパスを取得する。

    // Assert
    EXPECT_EQ(ERROR_OUTOFMEMORY, actual_result); // [確認_異常系] - 結果が ERROR_OUTOFMEMORY であること。
    EXPECT_EQ(nullptr, actual_path); // [確認_異常系] - 出力が NULL に初期化されること。
}

// GetModuleFileNameU() の失敗と容量以上の戻り値を検出して領域を解放することの確認
TEST(tcpServerWindowsTest, module_path_rejects_failure_and_truncation)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_windows> mock_windows;
    const DWORD expected_capacity = static_cast<DWORD>(32767U * 3U + 1U);
    char *actual_path = nullptr;
    // [状態] - 0、容量と同じ値、容量を超える値を順に注入する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_malloc(expected_capacity))
        .Times(3)
        .WillRepeatedly(Invoke(delegate_real_cplat_malloc));
    EXPECT_CALL(mock_cplat, GetModuleFileNameU(nullptr, NotNull(), expected_capacity))
        .WillOnce(Return(0U))
        .WillOnce(Return(expected_capacity))
        .WillOnce(Return(expected_capacity + 1U));
    EXPECT_CALL(mock_windows, GetLastError(_, _, _)).WillOnce(Return(ERROR_ACCESS_DENIED));
    EXPECT_CALL(mock_cplat, cplat_free(NotNull())).Times(3).WillRepeatedly(Invoke(delegate_real_cplat_free));
    // [Pre-Assert確認_異常系] - 0 と切り詰めを検出した後に確保領域を各 1 回解放すること。
    // [Pre-Assert手順] - 0 の場合は ERROR_ACCESS_DENIED、容量以上の場合は切り詰めを表す値を返す。

    // Act
    DWORD failed_result = tcpserver_test_get_native_executable_path(&actual_path);
    DWORD equal_result = tcpserver_test_get_native_executable_path(&actual_path);
    DWORD larger_result = tcpserver_test_get_native_executable_path(&actual_path);
    // [手順] - 失敗、容量と同じ戻り値、容量より大きい戻り値で取得関数を呼び出す。

    // Assert
    EXPECT_EQ(ERROR_ACCESS_DENIED, failed_result); // [確認_異常系] - 0 の直後に取得した OS エラーが返ること。
    EXPECT_EQ(ERROR_INSUFFICIENT_BUFFER, equal_result); // [確認_異常系] - 容量と同じ戻り値を切り詰めとして扱うこと。
    EXPECT_EQ(ERROR_INSUFFICIENT_BUFFER, larger_result); // [確認_異常系] - 容量より大きい戻り値を切り詰めとして扱うこと。
    EXPECT_EQ(nullptr, actual_path); // [確認_異常系] - 失敗した各回の出力が NULL であること。
}

// 300 文字以上の拡張パスを明示したアプリケーション名と引用済み引数へ渡すことの確認
TEST(tcpServerWindowsTest, child_process_uses_long_unicode_path_as_application_name)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_windows> mock_windows;
    const std::string executable_path = "\\\\?\\C:\\Program Files\\サンプル " + std::string(320U, 'x') +
                                       "\\tcpServer.exe";
    const std::string expected_prefix = "\"" + executable_path + "\" --worker ";
    PROCESS_INFORMATION process_info = {};
    DWORD actual_error = ERROR_INVALID_DATA;
    // [状態] - 300 文字を超え、空白、非 ASCII 文字、拡張パス接頭辞を含むパスを用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, CreateProcessU(StrEq(executable_path.c_str()), _, nullptr, nullptr, FALSE, 0, nullptr,
                                           nullptr, NotNull(), NotNull()))
        .WillOnce(Invoke(
            [&](const char *, const char *command_line, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD,
                LPVOID, const char *, LPSTARTUPINFOW, LPPROCESS_INFORMATION output) -> BOOL
            {
                EXPECT_THAT(command_line, StartsWith(expected_prefix));
                output->hProcess = reinterpret_cast<HANDLE>(1);
                output->hThread = reinterpret_cast<HANDLE>(2);
                return TRUE;
            })); // [Pre-Assert確認_正常系] - 同じ実行ファイル パスと、引用済みのコマンド ラインで CreateProcessU() を呼び出すこと。
                 // [Pre-Assert手順] - CreateProcessU() から成功し、テスト用ハンドルを設定する。

    // Act
    BOOL actual_result = tcpserver_test_start_child_process(executable_path.c_str(), " --worker test-pipe", FALSE,
                                                             &process_info, &actual_error);
    // [手順] - 長いパスで子プロセス起動の共通処理を呼び出す。

    // Assert
    EXPECT_TRUE(actual_result); // [確認_正常系] - 子プロセスの起動が成功すること。
    EXPECT_EQ(ERROR_SUCCESS, actual_error); // [確認_正常系] - エラー出力が ERROR_SUCCESS であること。
    EXPECT_EQ(reinterpret_cast<HANDLE>(1), process_info.hProcess); // [確認_正常系] - プロセス ハンドルを受け取ること。
    EXPECT_EQ(reinterpret_cast<HANDLE>(2), process_info.hThread); // [確認_正常系] - スレッド ハンドルを受け取ること。
}

// UTF-16 の終端 NUL を含めた上限 32,767 code unit の境界を確認
TEST(tcpServerWindowsTest, command_line_accepts_32767_units_and_rejects_32768)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    const char *executable_path = "C:\\a.exe";
    const size_t quoted_path_length = strlen(executable_path) + 2U;
    const size_t accepted_length = 32766U;
    const size_t rejected_length = 32767U;
    std::string accepted_arguments = " " + std::string(accepted_length - quoted_path_length - 1U, 'a');
    std::string rejected_arguments = " " + std::string(rejected_length - quoted_path_length - 1U, 'a');
    char *accepted_line = nullptr;
    char *rejected_line = nullptr;
    // [状態] - 終端 NUL を含めて上限内になる長さと、1 code unit 超える長さを用意する。

    // Pre-Assert
    // [Pre-Assert確認_正常系] - ASCII の UTF-8 文字が同じ数の UTF-16 code unit になること。
    // [Pre-Assert手順] - cplat の実際の書式化と UTF-8 から UTF-16 への変換へ委譲する。

    // Act
    DWORD accepted_result = tcpserver_test_build_command_line(executable_path, accepted_arguments.c_str(),
                                                               &accepted_line);
    DWORD rejected_result = tcpserver_test_build_command_line(executable_path, rejected_arguments.c_str(),
                                                               &rejected_line);
    // [手順] - 終端 NUL を含めて 32,767 と 32,768 code unit になるコマンド ラインを組み立てる。

    // Assert
    EXPECT_EQ(ERROR_SUCCESS, accepted_result); // [確認_正常系] - 32,767 code unit のコマンド ラインが受理されること。
    ASSERT_NE(nullptr, accepted_line); // [確認_正常系] - 上限内の文字列が返ること。
    EXPECT_EQ(accepted_length, strlen(accepted_line)); // [確認_正常系] - 終端 NUL より前の長さが 32,766 であること。
    EXPECT_EQ(ERROR_INSUFFICIENT_BUFFER, rejected_result); // [確認_異常系] - 32,768 code unit のコマンド ラインが拒否されること。
    EXPECT_EQ(nullptr, rejected_line); // [確認_異常系] - 上限超過時に出力が NULL であること。

    // Cleanup
    cplat_free(accepted_line);
}

// 子プロセス起動の確保、書式化、文字コード変換に失敗したときは起動しないことの確認
TEST(tcpServerWindowsTest, child_process_does_not_start_after_build_failures)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_windows> mock_windows;
    PROCESS_INFORMATION process_info = {};
    DWORD actual_error = ERROR_SUCCESS;
    // [状態] - コマンド ラインの確保、書式化、UTF-8 から UTF-16 への変換の各失敗を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_malloc(_))
        .WillOnce(Return(nullptr))
        .WillOnce(Invoke(delegate_real_cplat_malloc))
        .WillOnce(Invoke(delegate_real_cplat_malloc));
    EXPECT_CALL(mock_cplat, cplat_snprintf(_, _, StrEq("\"C:\\a.exe\" --child 1")))
        .WillOnce(Return(CPLAT_ERR_UNKNOWN))
        .WillOnce(DoDefault());
    EXPECT_CALL(mock_cplat, cplat_utf8_to_wstr_alloc(_, _)).WillOnce(Return(CPLAT_ERR_INVALID_ENCODING));
    EXPECT_CALL(mock_cplat, cplat_free(_)).Times(3).WillRepeatedly(Invoke(delegate_real_cplat_free));
    EXPECT_CALL(mock_cplat, CreateProcessU(_, _, _, _, _, _, _, _, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - 各失敗後に CreateProcessU() を呼ばず、確保した中間領域を解放すること。
    // [Pre-Assert手順] - 確保、書式化、変換を順に失敗させ、解放は実関数へ委譲する。

    // Act
    BOOL allocation_result = tcpserver_test_start_child_process("C:\\a.exe", " --child 1", FALSE, &process_info,
                                                                 &actual_error);
    DWORD allocation_error = actual_error;
    BOOL format_result = tcpserver_test_start_child_process("C:\\a.exe", " --child 1", FALSE, &process_info,
                                                              &actual_error);
    DWORD format_error = actual_error;
    BOOL conversion_result = tcpserver_test_start_child_process("C:\\a.exe", " --child 1", FALSE, &process_info,
                                                                 &actual_error);
    DWORD conversion_error = actual_error;
    // [手順] - 3 種類の組み立て失敗をそれぞれ注入して起動関数を呼び出す。

    // Assert
    EXPECT_FALSE(allocation_result); // [確認_異常系] - 確保失敗で起動しないこと。
    EXPECT_EQ(ERROR_OUTOFMEMORY, allocation_error); // [確認_異常系] - 確保失敗を ERROR_OUTOFMEMORY で返すこと。
    EXPECT_FALSE(format_result); // [確認_異常系] - 書式化失敗で起動しないこと。
    EXPECT_EQ(ERROR_INVALID_DATA, format_error); // [確認_異常系] - 書式化失敗を ERROR_INVALID_DATA で返すこと。
    EXPECT_FALSE(conversion_result); // [確認_異常系] - 文字コード変換失敗で起動しないこと。
    EXPECT_EQ(ERROR_NO_UNICODE_TRANSLATION, conversion_error); // [確認_異常系] - 変換失敗を対応する OS エラーで返すこと。
}

// CreateProcessU() の失敗直後に保存した最終エラーを解放後も返し、ハンドルを閉じないことの確認
TEST(tcpServerWindowsTest, child_process_preserves_create_error_after_free)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_windows> mock_windows;
    PROCESS_INFORMATION process_info = {};
    DWORD actual_error = ERROR_SUCCESS;
    // [状態] - CreateProcessU() の失敗後に解放が最終エラーを変更する状態を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, CreateProcessU(_, _, _, _, _, _, _, _, _, _))
        .WillOnce(Return(FALSE));
    EXPECT_CALL(mock_windows, GetLastError(_, _, _)).WillOnce(Return(ERROR_FILE_NOT_FOUND));
    EXPECT_CALL(mock_cplat, cplat_free(_))
        .WillRepeatedly(Invoke(
            [](void *pointer)
            {
                delegate_real_cplat_free(pointer);
                SetLastError(ERROR_ACCESS_DENIED);
            }));
    EXPECT_CALL(mock_windows, CloseHandle(_, _, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - 起動失敗後に最終エラーを取得し、ハンドルを閉じないこと。
    // [Pre-Assert手順] - 解放後の OS エラーを ERROR_ACCESS_DENIED に変え、取得時には ERROR_FILE_NOT_FOUND を返す。

    // Act
    BOOL actual_result = tcpserver_test_start_child_process("C:\\a.exe", " --child 1", TRUE, &process_info,
                                                             &actual_error);
    // [手順] - CreateProcessU() が失敗する状態で子プロセス起動を試みる。

    // Assert
    EXPECT_FALSE(actual_result); // [確認_異常系] - 子プロセス起動が失敗すること。
    EXPECT_EQ(ERROR_FILE_NOT_FOUND, actual_error); // [確認_異常系] - 解放後の値ではなく保存した最終エラーが返ること。
}

// fork の子プロセス起動失敗で診断し、親接続を一度閉じて次の accept() へ進むことの確認
TEST(tcpServerWindowsTest, fork_server_closes_client_once_and_continues_after_create_failure)
{
    // Arrange
    NiceMock<Mock_winsock> mock_winsock;
    NiceMock<Mock_windows> mock_windows;
    NiceMock<Mock_cplat> mock_cplat;
    testing::Mock::AllowLeak(&mock_winsock);
    testing::Mock::AllowLeak(&mock_windows);
    testing::Mock::AllowLeak(&mock_cplat);
    WSADATA wsa_data;
    ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &wsa_data)); // [状態] - Winsock を初期化する。
    s_fork_client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // [状態] - accept() が返すクライアントのソケットを作成する。
    ASSERT_NE(INVALID_SOCKET, s_fork_client); // [状態確認] - ソケットが INVALID_SOCKET 以外であること。
    s_fork_accept_count = 0;
    s_fork_client_close_count = 0;
    s_fork_create_process_called = 0;
    s_fork_process_close_count = 0;
    s_fork_expect_process_failure = 1;
    const std::string executable_path = "\\\\?\\C:\\Program Files\\tcpServer.exe";
    // [状態] - 起動失敗後の接続回収と次の受け付けを判定する状態を初期化する。

    // Pre-Assert
    ON_CALL(mock_cplat, GetModuleFileNameU(nullptr, _, _))
        .WillByDefault(Invoke(
            [&](HMODULE, char *path, DWORD capacity) -> DWORD
            {
                EXPECT_GT(capacity, static_cast<DWORD>(executable_path.size()));
                memcpy(path, executable_path.c_str(), executable_path.size() + 1U);
                return static_cast<DWORD>(executable_path.size());
            }));
    ON_CALL(mock_windows, SetHandleInformation(_, _, _, _, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
        .WillByDefault(Return(TRUE));
    ON_CALL(mock_winsock, accept(_, _, _, _, _, _))
        .WillByDefault(Invoke(
            [](const char *, const int, const char *, SOCKET, struct sockaddr *, int *) -> SOCKET
            {
                if (s_fork_accept_count++ == 0)
                {
                    return s_fork_client;
                }
                int process_failure_matches = s_fork_expect_process_failure && s_fork_create_process_called == 1 &&
                                              s_fork_process_close_count == 0;
                exit((s_fork_client_close_count == 1 && process_failure_matches) ? 3 : 4);
            }));
    ON_CALL(mock_winsock, closesocket(_, _, _, _))
        .WillByDefault(Invoke(
            [](const char *file, const int line, const char *func, SOCKET socket_value) -> int
            {
                if (socket_value == s_fork_client)
                {
                    ++s_fork_client_close_count;
                }
                return delegate_real_closesocket(file, line, func, socket_value);
            }));
    ON_CALL(mock_cplat, CreateProcessU(StrEq(executable_path.c_str()), _, _, _, TRUE, 0, _, _, _, _))
        .WillByDefault(Invoke(
            [](const char *, const char *, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID,
               const char *, LPSTARTUPINFOW, LPPROCESS_INFORMATION) -> BOOL
            {
                ++s_fork_create_process_called;
                return FALSE;
            }));
    ON_CALL(mock_windows, GetLastError(_, _, _)).WillByDefault(Return(ERROR_FILE_NOT_FOUND));
    ON_CALL(mock_windows, CloseHandle(_, _, _, _))
        .WillByDefault(Invoke(
            [](const char *, const int, const char *, HANDLE) -> BOOL
            {
                ++s_fork_process_close_count;
                return TRUE;
            }));
    // [Pre-Assert確認_異常系] - 子プロセス起動失敗の最終エラーを保持し、プロセス情報のハンドルを閉じないこと。
    // [Pre-Assert手順] - accept() の 2 回目で、接続を 1 回閉じた後に再び受け付けへ進んだ状態を確認して終了する。

    // Act
    // [手順] - CreateProcessU() が失敗する fork サーバーを子プロセスで実行する。

    // Assert
    EXPECT_EXIT(run_fork_server(0), ::testing::ExitedWithCode(3), "GetLastError=2");
    // [確認_異常系] - 最終エラー 2 を診断し、接続を 1 回閉じて次の accept() へ進むこと。

    // Cleanup
    (void)closesocket(s_fork_client);
    s_fork_client = INVALID_SOCKET;
    s_fork_expect_process_failure = 0;
    s_fork_process_close_count = 0;
    (void)WSACleanup();
}
