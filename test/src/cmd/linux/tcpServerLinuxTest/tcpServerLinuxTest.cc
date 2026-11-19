#include <testfw.h>
#include <mock_cplat.h>
#include <mock_signal.h>
#include <mock_unistd.h>
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "tcpServer.h"
#include "tcpServer_linux.inject.h"

// セッション処理関数として登録する関数 (呼び出さない)。
static void test_session(ClientFd)
{
}

// platform_init() が SIGPIPE を無視する設定を行うことの確認
TEST(tcpServerLinuxTest, platform_init_ignores_sigpipe)
{
    // Arrange
    NiceMock<Mock_signal> mock_signal;
    void (*actual_handler)(int) = SIG_DFL;
    int actual_flags = -1;
    g_session_fn = nullptr; // [状態] - 登録済みのセッション処理関数を未設定にする。

    // Pre-Assert
    EXPECT_CALL(mock_signal, sigaction(_, _, _, SIGPIPE, NotNull(), nullptr))
        .WillOnce(Invoke(
            [&](const char *, const int, const char *, int, const struct sigaction *act, struct sigaction *) -> int
            {
                actual_handler = act->sa_handler;
                actual_flags = act->sa_flags;
                return 0;
            })); // [Pre-Assert確認_正常系] - sigaction() が SIGPIPE、nullptr 以外、nullptr を指定して 1 回呼び出されること。
                 // [Pre-Assert手順] - sigaction() から、渡された設定を記録して 0 を返す (プロセスの設定は変えない)。

    // Act
    platform_init(test_session); // [手順] - platform_init() を呼び出す。

    // Assert
    EXPECT_EQ(SIG_IGN, actual_handler);     // [確認_正常系] - sigaction() に渡された sa_handler が SIG_IGN であること。
    EXPECT_EQ(0, actual_flags);             // [確認_正常系] - sigaction() に渡された sa_flags が 0 であること。
    EXPECT_EQ(test_session, g_session_fn); // [確認_正常系] - g_session_fn が test_session であること。

    // Cleanup
    g_session_fn = nullptr;
}

// epoll の多重接続のワーカーでエコーの送信が失敗した場合に、診断を出して接続を閉じ、ワーカーが続くことの確認
TEST(tcpServerLinuxTest, worker_closes_connection_when_write_fails)
{
    // Arrange
    // EXPECT_EXIT の子プロセスは exit で mock を破棄しないため、子プロセス側の mock 登録を解放対象外にする。
    NiceMock<Mock_unistd> mock_unistd;
    testing::Mock::AllowLeak(&mock_unistd);
    int server_fd = socket(AF_INET, SOCK_STREAM, 0); // [状態] - ワーカーが待ち受けるソケットを作成する。
    ASSERT_GE(server_fd, 0); // [状態確認] - socket() の戻り値が 0 以上であること。
    struct sockaddr_in addr = {};
    socklen_t addr_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ASSERT_EQ(0, bind(server_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr))); // [状態] - bind() でループバックの空きポートへ割り当てる。
                                                                         // [状態確認] - bind() の戻り値が 0 であること。
    ASSERT_EQ(0, listen(server_fd, 1)); // [状態] - 待ち受けを開始する。
                                        // [状態確認] - listen() の戻り値が 0 であること。
    ASSERT_EQ(0, getsockname(server_fd, reinterpret_cast<struct sockaddr *>(&addr), &addr_len)); // [状態] - 割り当てられたポートを取得する。
                                                                             // [状態確認] - getsockname() の戻り値が 0 であること。
    int client_fd = socket(AF_INET, SOCK_STREAM, 0); // [状態] - クライアントのソケットを作成する。
    ASSERT_GE(client_fd, 0); // [状態確認] - socket() の戻り値が 0 以上であること。
    ASSERT_EQ(0, connect(client_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr))); // [状態] - ワーカーの待ち受けへ接続する。
                                                                             // [状態確認] - connect() の戻り値が 0 であること。
    ASSERT_EQ(5, send(client_fd, "hello", 5, 0)); // [状態] - ワーカーがエコーするデータを送る。
                                                  // [状態確認] - send() の戻り値が 5 であること。
    ON_CALL(mock_unistd, write(_, _, _, _, _, 5U))
        .WillByDefault(Invoke(
            [](const char *, const int, const char *, int, const void *, size_t) -> ssize_t
            {
                errno = EPIPE;
                tcpserver_test_stop_running();
                return -1;
            })); // [状態] - write() から、ワーカーのループの継続フラグを 0 にして、errno に EPIPE を設定して -1 を返すように既定動作を設定する。

    // Pre-Assert

    // Act
    // [手順] - 子プロセスで、worker_loop() を同時接続数 2 で実行する。
    // Assert
    EXPECT_EXIT(tcpserver_test_worker_loop(server_fd, 0, 2), ::testing::ExitedWithCode(0), "write");
    // [確認_異常系] - 子プロセスが終了コード 0 で終了し、標準エラー出力が "write" を含むこと。(送信の失敗を診断して接続を閉じ、ワーカーが終了の処理まで進むことの確認)

    // Cleanup
    (void)close(client_fd);
    (void)close(server_fd);
}
