/**
 *******************************************************************************
 *  @file           potrPlatform.h
 *  @brief          プラットフォームの差異を抽象化する内部 API を提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/04/18
 *  @version        1.0.0
 *
 *  OS ごとに異なるスレッド・同期・ソケット・時刻 API を共通インターフェースで抽象化します。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/* NOTE: このヘッダーは多数のソース ファイルから参照されるため、            */
/*       @hideincludedbygraph によって "Included by" グラフを無効にします。 */

#ifndef POTR_PLATFORM_H
#define POTR_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#include <com_util/base/platform.h>
#include <com_util/clock/clock.h>
#include <com_util/sync/sync.h>

/* ============================================================
 * 型定義 (PotrSocket)
 * ============================================================ */
#if defined(PLATFORM_LINUX)
    #include <netinet/in.h>
    #include <unistd.h>
    #include <sys/socket.h>
    #include <poll.h>
typedef int PotrSocket;
    #define POTR_INVALID_SOCKET (-1)
#elif defined(PLATFORM_WINDOWS)
    #include <com_util/base/windows_sdk.h>
typedef SOCKET PotrSocket;
    #define POTR_INVALID_SOCKET INVALID_SOCKET
#endif /* PLATFORM_ */

/* ============================================================
 * static inline 関数
 * ============================================================ */

/**
 *  @brief  ソケットを閉じます。
 *  @param[in]  fd  閉じるソケット。
 */
static inline void potr_close_socket(PotrSocket fd)
{
#if defined(PLATFORM_LINUX)
    close(fd);
#elif defined(PLATFORM_WINDOWS)
    closesocket(fd);
#endif /* PLATFORM_ */
}

/**
 *  @brief  ソケットの送受信を停止します (shutdown)。
 *  @param[in]  fd  停止するソケット。
 */
static inline void potr_shutdown_socket(PotrSocket fd)
{
#if defined(PLATFORM_LINUX)
    shutdown(fd, SHUT_RDWR);
#elif defined(PLATFORM_WINDOWS)
    shutdown(fd, SD_BOTH);
#endif /* PLATFORM_ */
}

/**
 *  @brief  ソケット オプションを設定します。
 *  @param[in]  sock     対象ソケット。
 *  @param[in]  level    プロトコル レベル (SOL_SOCKET, IPPROTO_IP 等)。
 *  @param[in]  optname  オプション名。
 *  @param[in]  optval   オプション値へのポインター。
 *  @param[in]  optlen   オプション値のバイト数。
 *  @return  0: 成功、-1: 失敗。
 */
static inline int potr_setsockopt(PotrSocket sock, int level, int optname, const void *optval, int optlen)
{
#if defined(PLATFORM_LINUX)
    return setsockopt(sock, level, optname, optval, (socklen_t)optlen);
#elif defined(PLATFORM_WINDOWS)
    return setsockopt(sock, level, optname, (const char *)optval, optlen);
#endif /* PLATFORM_ */
}

/* ============================================================
 * extern 関数宣言 (.c で実装)
 * ============================================================ */
#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
 *  @brief  UDP データグラムを送信します。
 *  @param[in]  sock      送信ソケット。
 *  @param[in]  buf       送信データ。
 *  @param[in]  len       送信バイト数。
 *  @param[in]  flags     送信フラグ。
 *  @param[in]  dest      送信先アドレス。
 *  @param[in]  dest_len  送信先アドレス長。
 *  @return 送信バイト数 (負値: エラー)。
 */
    extern int potr_sendto(PotrSocket sock, const uint8_t *buf, size_t len, int flags, const struct sockaddr *dest,
                           int dest_len);

    /**
 *  @brief  UDP データグラムを受信します。
 *  @param[in]  sock     受信ソケット。
 *  @param[out] buf      受信バッファー。
 *  @param[in]  len      バッファー サイズ。
 *  @param[in]  flags    受信フラグ。
 *  @param[out] src      送信元アドレス格納先。
 *  @param[in,out] src_len  送信元アドレス長。
 *  @return 受信バイト数 (負値: エラー)。
 */
    extern int potr_recvfrom(PotrSocket sock, uint8_t *buf, size_t len, int flags, struct sockaddr *src, int *src_len);

    /**
 *  @brief  ソケットが書き込み可能か確認します (poll/WSAPoll)。
 *  @param[in]  fd         対象ソケット。
 *  @param[in]  timeout_ms タイムアウト (ミリ秒、0 = 即時)。
 *  @return  1: 書き込み可能、0: タイムアウト、-1: エラー。
 */
    extern int potr_poll_writable(PotrSocket fd, int timeout_ms);

    /**
 *  @brief  ソケットが読み取り可能か確認します (poll/WSAPoll)。
 *  @param[in]  fd         対象ソケット。
 *  @param[in]  timeout_ms タイムアウト (ミリ秒、0 = 即時)。
 *  @return  1: 読み取り可能、0: タイムアウト、-1: エラー。
 */
    extern int potr_poll_readable(PotrSocket fd, int timeout_ms);

    /**
 *  @brief  TCP ソケットへ全バイトを確実に送信します。
 *  @param[in]  fd   送信ソケット。
 *  @param[in]  buf  送信データ。
 *  @param[in]  len  送信バイト数。
 *  @return  0: 成功 (全バイト送信)、-1: 切断またはエラー。
 *  @note    呼び出し前に送信ミューテックスを取得しておく必要があります。
 */
    extern int potr_tcp_send(PotrSocket fd, const uint8_t *buf, size_t len);

    /**
 *  @brief  TCP ソケットから正確に n バイト読み取ります。
 *  @param[in]   fd   受信ソケット。
 *  @param[out]  buf  受信バッファー (n バイト以上)。
 *  @param[in]   n    受信バイト数。
 *  @return  1: 成功、0: 切断 (recv が 0 を返した)、-1: エラー。
 */
    extern int potr_tcp_recv_all(PotrSocket fd, uint8_t *buf, size_t n);

    /**
 *  @brief  ソケット ライブラリを初期化します (Windows は WSAStartup、Linux は no-op)。
 *  @return  0: 成功、-1: 失敗。
 */
    extern int potr_socket_lib_init(void);

    /**
 *  @brief  ソケット ライブラリを終了します (Windows は WSACleanup、Linux は no-op)。
 */
    extern void potr_socket_lib_cleanup(void);

    /**
 *  @brief  ソケットをノンブロッキング モードに設定します。
 *  @param[in]  fd  対象ソケット。
 *  @return  0: 成功、-1: 失敗。
 */
    extern int potr_set_nonblocking(PotrSocket fd);

    /**
 *  @brief  ソケットをブロッキング モードに戻します。
 *  @param[in]  fd  対象ソケット。
 *  @return  0: 成功、-1: 失敗。
 */
    extern int potr_set_blocking(PotrSocket fd);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_PLATFORM_H */
