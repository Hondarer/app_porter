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
 *  本ヘッダーの OS ラッパー関数は、元 API (BSD ソケット / Winsock) の戻り値規約を
 *  保存する層であるため、共通結果コード (@ref POTR_RESULT) の適用対象外です。\n
 *  ただし、複数の OS 呼び出しを合成するヘルパー (potr_tcp_send()、potr_tcp_recv_all())
 *  は独自の規約を持つため、共通結果コードの適用対象とします。\n
 *  詳細は docs/coding-guideline.md の「適用対象外」を参照してください。
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
#include <com_util/crt/unistd.h>
#include <com_util/base/error.h>
#include <com_util/sync/sync.h>
#include <porter/porter_result.h>

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
    com_util_error saved_error;

    /* Linux の com_util_close() が TLS の直前エラーを更新するため、OS によらず呼び出し前の診断を保存します。 */
    com_util_error_get_last(&saved_error);
#if defined(PLATFORM_LINUX)
    com_util_close(fd, NULL);
#elif defined(PLATFORM_WINDOWS)
    closesocket(fd);
#endif /* PLATFORM_ */
    com_util_error_set_last(&saved_error);
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
 *  @brief          16 ビット値をホストからネットワーク バイト オーダーへ変換します。
 *  @param[in]      value 変換する値。
 *  @return         変換後の値を返します。
 */
static inline uint16_t potr_hton16(const uint16_t value)
{
    return htons(value);
}
/**
 *  @brief          16 ビット値をネットワークからホスト バイト オーダーへ変換します。
 *  @param[in]      value 変換する値。
 *  @return         変換後の値を返します。
 */
static inline uint16_t potr_ntoh16(const uint16_t value)
{
    return ntohs(value);
}
/**
 *  @brief          32 ビット値をホストからネットワーク バイト オーダーへ変換します。
 *  @param[in]      value 変換する値。
 *  @return         変換後の値を返します。
 */
static inline uint32_t potr_hton32(const uint32_t value)
{
    return htonl(value);
}
/**
 *  @brief          32 ビット値をネットワークからホスト バイト オーダーへ変換します。
 *  @param[in]      value 変換する値。
 *  @return         変換後の値を返します。
 */
static inline uint32_t potr_ntoh32(const uint32_t value)
{
    return ntohl(value);
}

/* ============================================================
 * extern 関数宣言 (.c で実装)
 * ============================================================ */
#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          指定形式の IPv4 ソケットを作成します。
     *  @param[in]      type ソケット形式。
     *  @param[out]     sock_out 作成したソケット。NULL 不可。
     *  @param[out]     detail_out 詳細情報。NULL 可。
     *  @return         成功時は POTR_OK、失敗時は POTR_ERR_* を返します。
     */
    extern int potr_socket_open(int type, PotrSocket *sock_out, com_util_error *detail_out);
    /**
     *  @brief          IPv4 ソケットを指定アドレスへ bind します。
     *  @param[in]      sock 対象ソケット。
     *  @param[in]      addr bind するアドレス。NULL 不可。
     *  @param[out]     detail_out 詳細情報。NULL 可。
     *  @return         成功時は POTR_OK、失敗時は POTR_ERR_* を返します。
     */
    extern int potr_bind(PotrSocket sock, const struct sockaddr_in *addr, com_util_error *detail_out);
    /**
     *  @brief          TCP ソケットを接続待ち状態へ変更します。
     *  @param[in]      sock 対象ソケット。
     *  @param[in]      backlog 保留接続数。
     *  @param[out]     detail_out 詳細情報。NULL 可。
     *  @return         成功時は POTR_OK、失敗時は POTR_ERR_* を返します。
     */
    extern int potr_listen(PotrSocket sock, int backlog, com_util_error *detail_out);
    /**
     *  @brief          TCP 接続を受け入れます。
     *  @param[in]      sock 接続待ちソケット。
     *  @param[out]     addr 接続元アドレス。NULL 不可。
     *  @param[out]     sock_out 接続済みソケット。NULL 不可。
     *  @param[out]     detail_out 詳細情報。NULL 可。
     *  @return         成功時は POTR_OK、失敗時は POTR_ERR_* を返します。
     */
    extern int potr_accept(PotrSocket sock, struct sockaddr_in *addr, PotrSocket *sock_out, com_util_error *detail_out);
    /**
     *  @brief          IPv4 の接続先へ接続します。
     *  @param[in]      sock 対象ソケット。
     *  @param[in]      addr 接続先アドレス。NULL 不可。
     *  @param[out]     detail_out 詳細情報。NULL 可。
     *  @return         成功時は POTR_OK、失敗時は POTR_ERR_* を返します。
     */
    extern int potr_connect(PotrSocket sock, const struct sockaddr_in *addr, com_util_error *detail_out);
    /**
     *  @brief          ソケット オプションを設定します。
     *  @param[in]      sock 対象ソケット。
     *  @param[in]      level プロトコル レベル。
     *  @param[in]      optname オプション名。
     *  @param[in]      optval オプション値。NULL 不可。
     *  @param[in]      optlen オプション値のバイト数。正値を指定します。
     *  @param[out]     detail_out 詳細情報。NULL 可。
     *  @return         成功時は POTR_OK、失敗時は POTR_ERR_* を返します。
     */
    extern int potr_setsockopt(PotrSocket sock, int level, int optname, const void *optval, int optlen,
                               com_util_error *detail_out);
    /**
     *  @brief          非同期接続の保留エラーを取得します。
     *  @param[in]      sock 対象ソケット。
     *  @param[out]     detail_out 保留エラーの詳細情報。NULL 可。
     *  @return         保留エラーがなければ POTR_OK、存在する場合は対応する POTR_ERR_* を返します。
     */
    extern int potr_socket_get_pending_error(PotrSocket sock, com_util_error *detail_out);
    /**
     *  @brief          複数ソケットの読み取り可否を取得します。
     *  @param[in]      fds 対象ソケット配列。NULL 不可。
     *  @param[in]      count 配列要素数。1 以上 POTR_MAX_PATH 以下。
     *  @param[in]      timeout_ms タイムアウト ミリ秒。
     *  @param[out]     ready_out 読み取り可能状態の出力配列。NULL 不可。
     *  @param[out]     detail_out 詳細情報。NULL 可。
     *  @return         成功時は POTR_OK、失敗時は POTR_ERR_* を返します。
     *  @note           全要素が POTR_INVALID_SOCKET の場合は @p timeout_ms だけ待機し、
     *                  全出力を 0 にして POTR_OK を返します。
     */
    extern int potr_poll_readable_multi(const PotrSocket *fds, size_t count, int timeout_ms, unsigned char *ready_out,
                                        com_util_error *detail_out);
    /**
     *  @brief          受信待ちを解除します。
     *  @param[in,out]  sock_inout 対象ソケット。NULL 不可。
     *  @param[out]     detail_out 詳細情報。NULL 可。
     *  @return         成功時は POTR_OK、失敗時は POTR_ERR_* を返します。
     *
     *  Linux は読み取り側だけを shutdown してハンドルを保持します。Windows はソケットを閉じ、成否にかかわらず
     *  @p sock_inout を POTR_INVALID_SOCKET へ変更します。
     */
    extern int potr_shutdown_receive(PotrSocket *sock_inout, com_util_error *detail_out);

    /**
     *  @brief  UDP データグラムを送信します。
     *  @param[in]  sock      送信ソケット。
     *  @param[in]  buf       送信データ。
     *  @param[in]  len       送信バイト数。
     *  @param[in]  flags     送信フラグ。
     *  @param[in]  dest      送信先アドレス。
     *  @param[in]  dest_len  送信先アドレス長。
     *  @param[out] detail_out 失敗時の詳細情報。NULL 可。
     *  @return 送信バイト数 (負値: エラー)。sendto の規約を保存するため共通結果コードの適用対象外。
     */
    extern int potr_sendto(PotrSocket sock, const uint8_t *buf, size_t len, int flags, const struct sockaddr *dest,
                           int dest_len, com_util_error *detail_out);

    /**
     *  @brief  UDP データグラムを受信します。
     *  @param[in]  sock     受信ソケット。
     *  @param[out] buf      受信バッファー。
     *  @param[in]  len      バッファー サイズ。
     *  @param[in]  flags    受信フラグ。
     *  @param[out] src      送信元アドレス格納先。
     *  @param[in,out] src_len  送信元アドレス長。
     *  @param[out] detail_out 失敗時の詳細情報。NULL 可。
     *  @return 受信バイト数 (負値: エラー)。recvfrom の規約を保存するため共通結果コードの適用対象外。
     */
    extern int potr_recvfrom(PotrSocket sock, uint8_t *buf, size_t len, int flags, struct sockaddr *src, int *src_len,
                             com_util_error *detail_out);

    /**
     *  @brief  ソケットが書き込み可能か確認します (poll/WSAPoll)。
     *  @param[in]  fd         対象ソケット。
     *  @param[in]  timeout_ms タイムアウト (ミリ秒、0 = 即時)。
     *  @param[out] detail_out 失敗時の詳細情報。NULL 可。
     *  @return  1: 書き込み可能、0: タイムアウト、-1: エラー。poll の規約を保存するため共通結果コードの適用対象外。
     */
    extern int potr_poll_writable(PotrSocket fd, int timeout_ms, com_util_error *detail_out);

    /**
     *  @brief  ソケットが読み取り可能か確認します (poll/WSAPoll)。
     *  @param[in]  fd         対象ソケット。
     *  @param[in]  timeout_ms タイムアウト (ミリ秒、0 = 即時)。
     *  @param[out] detail_out 失敗時の詳細情報。NULL 可。
     *  @return  1: 読み取り可能、0: タイムアウト、-1: エラー。poll の規約を保存するため共通結果コードの適用対象外。
     */
    extern int potr_poll_readable(PotrSocket fd, int timeout_ms, com_util_error *detail_out);

    /**
     *  @brief  TCP ソケットへ全バイトを確実に送信します。
     *  @param[in]  fd   送信ソケット。
     *  @param[in]  buf  送信データ。
     *  @param[in]  len  送信バイト数。
     *  @param[out] detail_out 失敗時の詳細情報。NULL 可。
     *  @return  成功時 (全バイト送信) は POTR_OK、切断またはエラー時は POTR_ERR_IO。
     *  @note    複数の OS 呼び出しを合成するヘルパーのため、共通結果コード (POTR_RESULT) の適用対象。\n
     *           呼び出し前に送信ミューテックスを取得しておく必要があります。
     */
    extern int potr_tcp_send(PotrSocket fd, const uint8_t *buf, size_t len, com_util_error *detail_out);

    /**
     *  @brief  TCP ソケットから正確に n バイト読み取ります。
     *  @param[in]   fd   受信ソケット。
     *  @param[out]  buf  受信バッファー (n バイト以上)。
     *  @param[in]   n    受信バイト数。
     *  @param[out]  detail_out 失敗時の詳細情報。NULL 可。
     *  @return  成功時は POTR_OK、切断時 (recv が 0 を返した) は POTR_ERR_EOF、エラー時は POTR_ERR_IO。
     *  @note    複数の OS 呼び出しを合成するヘルパーのため、共通結果コード (POTR_RESULT) の適用対象。
     */
    extern int potr_tcp_recv_all(PotrSocket fd, uint8_t *buf, size_t n, com_util_error *detail_out);

    /**
     *  @brief  ソケット ライブラリを初期化します (Windows は WSAStartup、Linux は no-op)。
     *  @param[out] detail_out 失敗時の詳細情報。NULL 可。
     *  @return 成功時は POTR_OK、失敗時は対応する POTR_ERR_* を返します。
     */
    extern int potr_socket_lib_init(com_util_error *detail_out);

    /**
     *  @brief  ソケット ライブラリを終了します (Windows は WSACleanup、Linux は no-op)。
     */
    extern void potr_socket_lib_cleanup(void);

    /**
     *  @brief  ソケットを非ブロッキング モードに設定します。
     *  @param[in]  fd  対象ソケット。
     *  @param[out] detail_out 失敗時の詳細情報。NULL 可。
     *  @return  0: 成功、-1: 失敗。fcntl/ioctlsocket の規約を保存するため共通結果コードの適用対象外。
     */
    extern int potr_set_nonblocking(PotrSocket fd, com_util_error *detail_out);

    /**
     *  @brief  ソケットをブロッキング モードに戻します。
     *  @param[in]  fd  対象ソケット。
     *  @param[out] detail_out 失敗時の詳細情報。NULL 可。
     *  @return  0: 成功、-1: 失敗。fcntl/ioctlsocket の規約を保存するため共通結果コードの適用対象外。
     */
    extern int potr_set_blocking(PotrSocket fd, com_util_error *detail_out);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_PLATFORM_H */
