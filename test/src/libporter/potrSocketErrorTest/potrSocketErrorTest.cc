#include <testfw.h>
#include <com_util/base/error_message.h>
#include <porter/infra/potrSocketError.h>
#include <porter/infra/potrSocketErrorPlatform.h>
#include <porter/infra/potrPlatform.h>
#include <porter/porter_result.h>

#if defined(PLATFORM_LINUX)
    #include <errno.h>
    #include <sys/socket.h>
#elif defined(PLATFORM_WINDOWS)
    #include <com_util/base/windows_sdk.h>
#endif /* PLATFORM_ */

class potrSocketErrorTest : public Test
{
};

// 空の詳細エラーが成功へ変換されることの確認
TEST_F(potrSocketErrorTest, empty_detail_maps_to_ok)
{
    // Arrange
    com_util_error detail;
    com_util_error_clear(&detail); // [状態] - エラーを保持しない詳細情報を用意する。

    // Pre-Assert

    // Act
    const int result = potr_result_from_socket_error(&detail); // [手順] - 空の詳細情報を porter 結果へ変換する。

    // Assert
    EXPECT_EQ(POTR_OK, result); // [確認_正常系] - potr_result_from_socket_error の戻り値が POTR_OK であること。
}

// エラー報告経路がネイティブ コード 0 を成功として扱わないことの確認
TEST_F(potrSocketErrorTest, report_code_zero_maps_to_unknown)
{
    // Arrange
    com_util_error detail;

    // Pre-Assert

    // Act
    const int result =
        potr_socket_error_report_code(&detail, 0UL); // [手順] - ネイティブ コード 0 を失敗として報告する。

    // Assert
    EXPECT_EQ(POTR_ERR_UNKNOWN,
              result); // [確認_異常系] - potr_socket_error_report_code の戻り値が POTR_ERR_UNKNOWN であること。
}

// 不正引数の報告が詳細情報と TLS を同じ値へ更新することの確認
TEST_F(potrSocketErrorTest, invalid_argument_updates_detail_and_last_error)
{
    // Arrange
    com_util_error detail;
    com_util_error last;
    char message[256];

    // Pre-Assert

    // Act
    const int result = potr_socket_error_report_invalid_argument(&detail); // [手順] - 不正引数を報告する。
    com_util_error_get_last(&last);                                        // [手順] - スレッドの直前エラーを取得する。
    const int message_result =
        com_util_error_message(message, sizeof(message), &detail); // [手順] - 不正引数の詳細メッセージを取得する。

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT,
              result); // [確認_異常系] - potr_socket_error_report_invalid_argument の戻り値が不正引数であること。
#if defined(PLATFORM_LINUX)
    EXPECT_EQ(COM_UTIL_ERROR_DOMAIN_ERRNO,
              detail.domain);                      // [確認_異常系] - Linux の詳細情報が errno ドメインであること。
    EXPECT_EQ((unsigned long)EINVAL, detail.code); // [確認_異常系] - Linux の詳細情報が EINVAL を保持すること。
#elif defined(PLATFORM_WINDOWS)
    EXPECT_EQ(COM_UTIL_ERROR_DOMAIN_WINDOWS,
              detail.domain); // [確認_異常系] - Windows の詳細情報が Windows ドメインであること。
    EXPECT_EQ((unsigned long)WSAEINVAL,
              detail.code); // [確認_異常系] - Windows の詳細情報が WSAEINVAL を保持すること。
#endif /* PLATFORM_ */
    EXPECT_EQ(COM_UTIL_ERR_INVALID_ARGUMENT,
              detail.result);                     // [確認_異常系] - 詳細情報が com_util の不正引数結果を保持すること。
    EXPECT_EQ(detail.result, last.result);        // [確認_異常系] - TLS と出力引数の結果が一致すること。
    EXPECT_EQ(1, com_util_error_is_set(&detail)); // [確認_異常系] - 詳細情報が設定済みと判定されること。
    EXPECT_EQ(COM_UTIL_OK,
              message_result); // [確認_正常系] - com_util_error_message の戻り値が COM_UTIL_OK であること。
    EXPECT_STRNE("no error", message); // [確認_異常系] - 不正引数の詳細メッセージが no error ではないこと。
}

// 詳細情報のクリアが出力引数と TLS を成功状態へ更新することの確認
TEST_F(potrSocketErrorTest, clear_updates_detail_and_last_error)
{
    // Arrange
    com_util_error detail;
    com_util_error last;
    (void)potr_socket_error_report_invalid_argument(&detail); // [状態] - クリア前の詳細情報を設定する。

    // Pre-Assert

    // Act
    potr_socket_error_clear(&detail); // [手順] - 詳細情報と TLS をクリアする。
    com_util_error_get_last(&last);   // [手順] - クリア後の TLS を取得する。

    // Assert
    EXPECT_EQ(COM_UTIL_ERROR_DOMAIN_NONE, detail.domain); // [確認_正常系] - 出力引数のドメインが NONE であること。
    EXPECT_EQ(COM_UTIL_OK, detail.result);                // [確認_正常系] - 出力引数の結果が COM_UTIL_OK であること。
    EXPECT_EQ(COM_UTIL_ERROR_DOMAIN_NONE, last.domain);   // [確認_正常系] - TLS のドメインが NONE であること。
}

// ソケット エラー要因の一致判定が分類結果を返すことの確認
TEST_F(potrSocketErrorTest, is_matches_classified_cause)
{
    // Arrange
    com_util_error detail;
#if defined(PLATFORM_LINUX)
    com_util_error_capture_errno(&detail, ETIMEDOUT); // [状態] - Linux のタイムアウト詳細を用意する。
#elif defined(PLATFORM_WINDOWS)
    com_util_error_capture_windows_error(&detail, WSAETIMEDOUT); // [状態] - Windows のタイムアウト詳細を用意する。
#endif /* PLATFORM_ */

    // Pre-Assert

    // Act
    const int is_timeout = potr_socket_error_is(&detail, POTR_SOCKET_CAUSE_TIMED_OUT);
    const int is_reset = potr_socket_error_is(&detail, POTR_SOCKET_CAUSE_CONNECTION_RESET);

    // Assert
    EXPECT_EQ(1, is_timeout); // [確認_正常系] - タイムアウト要因との一致判定が真であること。
    EXPECT_EQ(0, is_reset);   // [確認_正常系] - 接続リセット要因との一致判定が偽であること。
}

// 非同期接続処理中の要因が WOULD_BLOCK と IN_PROGRESS を受理することの確認
TEST_F(potrSocketErrorTest, connect_in_progress_accepts_would_block_and_in_progress)
{
    // Arrange
    com_util_error would_block;
    com_util_error in_progress;
#if defined(PLATFORM_LINUX)
    com_util_error_capture_errno(&would_block, EAGAIN);      // [状態] - Linux の WOULD_BLOCK 詳細を用意する。
    com_util_error_capture_errno(&in_progress, EINPROGRESS); // [状態] - Linux の IN_PROGRESS 詳細を用意する。
#elif defined(PLATFORM_WINDOWS)
    com_util_error_capture_windows_error(&would_block,
                                         WSAEWOULDBLOCK); // [状態] - Windows の WOULD_BLOCK 詳細を用意する。
    com_util_error_capture_windows_error(&in_progress,
                                         WSAEINPROGRESS); // [状態] - Windows の IN_PROGRESS 詳細を用意する。
#endif /* PLATFORM_ */

    // Pre-Assert

    // Act
    const int would_block_result = potr_socket_error_is_connect_in_progress(&would_block);
    const int in_progress_result = potr_socket_error_is_connect_in_progress(&in_progress);

    // Assert
    EXPECT_EQ(1, would_block_result); // [確認_正常系] - WOULD_BLOCK が接続処理中と判定されること。
    EXPECT_EQ(1, in_progress_result); // [確認_正常系] - IN_PROGRESS が接続処理中と判定されること。
}

// OS が保持する直前のソケット エラーが詳細情報へ取り込まれることの確認
TEST_F(potrSocketErrorTest, report_captures_native_socket_error)
{
    // Arrange
    com_util_error detail;
#if defined(PLATFORM_LINUX)
    errno = ETIMEDOUT; // [状態] - Linux の直前エラーを ETIMEDOUT にする。
#elif defined(PLATFORM_WINDOWS)
    WSASetLastError(WSAETIMEDOUT); // [状態] - Windows の直前エラーを WSAETIMEDOUT にする。
#endif /* PLATFORM_ */

    // Pre-Assert

    // Act
    const int result = potr_socket_error_report(&detail); // [手順] - OS の直前ソケット エラーを報告する。

    // Assert
    EXPECT_EQ(POTR_ERR_TIMEOUT, result); // [確認_異常系] - potr_socket_error_report の戻り値がタイムアウトであること。
    EXPECT_EQ(COM_UTIL_ERR_TIMEOUT,
              detail.result); // [確認_異常系] - 詳細情報が com_util のタイムアウト結果を保持すること。
}

// 全ソケットが無効な poll が読み取り可能なしとして成功することの確認
TEST_F(potrSocketErrorTest, poll_multi_with_only_invalid_sockets_returns_no_ready)
{
    // Arrange
    const PotrSocket sockets[2] = {POTR_INVALID_SOCKET, POTR_INVALID_SOCKET}; // [状態] - 無効ソケットだけを用意する。
    unsigned char ready[2] = {1U, 1U};
    com_util_error detail;
    (void)potr_socket_error_report_invalid_argument(&detail); // [状態] - 呼び出し前の詳細情報を設定する。

    // Pre-Assert

    // Act
    const int result =
        potr_poll_readable_multi(sockets, 2U, 0, ready, &detail); // [手順] - 無効ソケットだけを poll する。

    // Assert
    EXPECT_EQ(POTR_OK, result); // [確認_正常系] - potr_poll_readable_multi の戻り値が POTR_OK であること。
    EXPECT_EQ(0U, ready[0]);    // [確認_正常系] - 1 番目のソケットが読み取り不可であること。
    EXPECT_EQ(0U, ready[1]);    // [確認_正常系] - 2 番目のソケットが読み取り不可であること。
    EXPECT_EQ(COM_UTIL_ERROR_DOMAIN_NONE, detail.domain); // [確認_正常系] - 詳細情報がクリアされること。
}

#if defined(PLATFORM_LINUX)
// TCP の EOF が呼び出し前の詳細情報を残さないことの確認
TEST_F(potrSocketErrorTest, tcp_recv_eof_clears_detail)
{
    // Arrange
    int sockets[2];
    uint8_t value = 0U;
    com_util_error detail;
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)); // [状態] - 接続済みソケット ペアを用意する。
    (void)potr_socket_error_report_invalid_argument(&detail);   // [状態] - 呼び出し前の詳細情報を設定する。
    potr_close_socket(sockets[1]); // [状態] - 受信側で EOF になるよう対向ソケットを閉じる。

    // Pre-Assert

    // Act
    const int result =
        potr_tcp_recv_all(sockets[0], &value, 1U, &detail); // [手順] - 閉じられた対向から 1 バイト受信する。

    // Assert
    EXPECT_EQ(POTR_ERR_EOF, result); // [確認_正常系] - potr_tcp_recv_all の戻り値が POTR_ERR_EOF であること。
    EXPECT_EQ(COM_UTIL_ERROR_DOMAIN_NONE, detail.domain); // [確認_正常系] - EOF 時に詳細情報がクリアされること。

    // Cleanup
    potr_close_socket(sockets[0]);
}
#endif /* PLATFORM_LINUX */

#if defined(PLATFORM_LINUX)
// Linux の代表的な errno がソケット エラー要因と porter 結果へ変換されることの確認
TEST_F(potrSocketErrorTest, errno_values_map_to_porter_causes_and_results)
{
    // Arrange
    com_util_error timeout_error;
    com_util_error refused_error;
    com_util_error_capture_errno(&timeout_error, ETIMEDOUT);    // [状態] - タイムアウトの errno 詳細を用意する。
    com_util_error_capture_errno(&refused_error, ECONNREFUSED); // [状態] - 接続拒否の errno 詳細を用意する。

    // Pre-Assert

    // Act
    const potr_socket_cause_t timeout_cause = potr_socket_error_get_cause(&timeout_error);
    const int timeout_result = potr_result_from_socket_error(&timeout_error);
    const int refused_result = potr_result_from_socket_error(&refused_error);

    // Assert
    EXPECT_EQ(POTR_SOCKET_CAUSE_TIMED_OUT, timeout_cause); // [確認_正常系] - ETIMEDOUT がタイムアウトへ分類されること。
    EXPECT_EQ(POTR_ERR_TIMEOUT, timeout_result); // [確認_正常系] - タイムアウトが POTR_ERR_TIMEOUT へ変換されること。
    EXPECT_EQ(POTR_ERR_DISCONNECTED,
              refused_result); // [確認_正常系] - 接続拒否が POTR_ERR_DISCONNECTED へ変換されること。
}
#elif defined(PLATFORM_WINDOWS)
// Windows の代表的な Winsock エラーがソケット エラー要因へ変換されることの確認
TEST_F(potrSocketErrorTest, winsock_values_map_to_porter_causes)
{
    // Arrange
    com_util_error timeout_error;
    com_util_error_capture_windows_error(&timeout_error,
                                         WSAETIMEDOUT); // [状態] - Winsock のタイムアウト詳細を用意する。

    // Pre-Assert

    // Act
    const potr_socket_cause_t cause = potr_socket_error_get_cause(&timeout_error);

    // Assert
    EXPECT_EQ(POTR_SOCKET_CAUSE_TIMED_OUT, cause); // [確認_正常系] - WSAETIMEDOUT がタイムアウトへ分類されること。
}
#endif /* PLATFORM_ */
