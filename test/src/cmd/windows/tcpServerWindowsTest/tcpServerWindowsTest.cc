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
static int s_fork_client_closed = 0;
static int s_fork_create_process_called = 0;

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
    EXPECT_CALL(mock_winsock, recv(_, _, _, _, _, BUFFER_SIZE, 0))
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
    s_fork_client_closed = 0;
    s_fork_create_process_called = 0; // [状態] - 呼び出しの記録を初期化する。
    ON_CALL(mock_winsock, accept(_, _, _, _, _, _))
        .WillByDefault(Invoke(
            [](const char *, const int, const char *, SOCKET, struct sockaddr *, int *) -> SOCKET
            {
                if (s_fork_accept_count++ == 0)
                {
                    return s_fork_client;
                }
                exit((s_fork_client_closed && !s_fork_create_process_called) ? 3 : 4);
            })); // [状態] - accept() から、1 回目はクライアントのソケットを返し、2 回目は接続を閉じて子プロセスを起動しなかった場合に終了コード 3 (それ以外は 4) で終了するように既定動作を設定する。
    ON_CALL(mock_windows, SetHandleInformation(_, _, _, _, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
        .WillByDefault(Return(FALSE)); // [状態] - SetHandleInformation() から FALSE を返すように既定動作を設定する。
    ON_CALL(mock_winsock, closesocket(_, _, _, _))
        .WillByDefault(Invoke(
            [](const char *file, const int line, const char *func, SOCKET s) -> int
            {
                if (s == s_fork_client)
                {
                    s_fork_client_closed = 1;
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
        .WillByDefault(Return(0U)); // [状態] - GetModuleFileNameU() から 0 を返すように既定動作を設定する。

    // Pre-Assert

    // Act
    // [手順] - 子プロセスで run_fork_server() をポート 0 で実行する。
    // Assert
    EXPECT_EXIT(run_fork_server(0), ::testing::ExitedWithCode(1), "GetModuleFileName");
    // [確認_異常系] - 子プロセスが終了コード 1 で終了し、標準エラー出力が "GetModuleFileName" を含むこと。

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
        .WillByDefault(Return(0U)); // [状態] - GetModuleFileNameU() から 0 を返すように既定動作を設定する。

    // Pre-Assert

    // Act
    // [手順] - 子プロセスで run_prefork_server() をポート 0、ワーカー数 1、同時接続数 1 で実行する。
    // Assert
    EXPECT_EXIT(run_prefork_server(0, 1, 1), ::testing::ExitedWithCode(1), "GetModuleFileName");
    // [確認_異常系] - 子プロセスが終了コード 1 で終了し、標準エラー出力が "GetModuleFileName" を含むこと。

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
