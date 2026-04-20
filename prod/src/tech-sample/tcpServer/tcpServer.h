/**
 *******************************************************************************
 *  @file           tcpServer.h
 *  @brief          TCP サーバーサンプル共通定義。
 *  @author         Tetsuo Honda
 *  @date           2026/03/17
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <com_util/base/platform.h>

/* ============================================================
 *  プラットフォーム抽象化
 *  handle_client_session() が ClientFd / get_pid() / client_recv() 等を
 *  プラットフォームを意識せずに使えるようにします。
 * ============================================================ */

#if defined(PLATFORM_LINUX)

    #include <sys/types.h>
    #include <unistd.h>

    /** クライアントソケットの型。Linux では int、Windows では SOCKET。 */
    typedef int     ClientFd;
    /** プロセス ID の型。Linux では pid_t、Windows では DWORD。 */
    typedef pid_t   PidType;

    /** 現在のプロセス ID を取得する。 */
    #define get_pid()                    ((PidType)getpid())
    /** クライアントからデータを受信する。 */
    #define client_recv(fd, buf, len)    read((fd), (buf), (len))
    /** クライアントへデータを送信する。 */
    #define client_send(fd, buf, len)    write((fd), (buf), (len))
    /** クライアントソケットを閉じる。 */
    #define client_close(fd)             close(fd)

#elif defined(PLATFORM_WINDOWS)

    #include <com_util/base/windows_sdk.h>
    #pragma comment(lib, "ws2_32.lib")

    /** クライアントソケットの型。Linux では int、Windows では SOCKET。 */
    typedef SOCKET  ClientFd;
    /** プロセス ID の型。Linux では pid_t、Windows では DWORD。 */
    typedef DWORD   PidType;

    /** 現在のプロセス ID を取得する。 */
    #define get_pid()                    ((PidType)GetCurrentProcessId())
    /** クライアントからデータを受信する。 */
    #define client_recv(fd, buf, len)    recv((fd), (buf), (int)(len), 0)
    /** クライアントへデータを送信する。 */
    #define client_send(fd, buf, len)    send((fd), (buf), (int)(len), 0)
    /** クライアントソケットを閉じる。 */
    #define client_close(fd)             closesocket(fd)

#endif /* PLATFORM_ */

/* ============================================================
 *  共通定数
 * ============================================================ */

/** デフォルト待ち受けポート番号。 */
#define DEFAULT_PORT    8080
/** デフォルト prefork ワーカー数。 */
#define DEFAULT_WORKERS          4
/** デフォルト 1 ワーカーあたりの同時接続数。 */
#define DEFAULT_CONNS_PER_WORKER 1
/** 送受信バッファサイズ (バイト)。 */
#define BUFFER_SIZE              1024

/* ============================================================
 *  共通型定義
 * ============================================================ */

/**
 *******************************************************************************
 *  @brief          サーバー動作モード。
 *******************************************************************************
 */
typedef enum {
    MODE_PREFORK = 0, /**< プリフォークモード (デフォルト)。 */
    MODE_FORK    = 1  /**< 接続ごと fork モード。 */
} ServerMode;

/**
 *******************************************************************************
 *  @brief          セッション処理関数の型。
 *
 *  クライアントソケットを受け取り、通信処理を行い、ソケットを閉じます。
 *******************************************************************************
 */
typedef void (*ClientSessionFn)(ClientFd fd);

/**
 *******************************************************************************
 *  @brief          登録済みセッション処理関数。
 *
 *  platform_init() で設定します。
 *******************************************************************************
 */
extern ClientSessionFn g_session_fn;

/* ============================================================
 *  関数プロトタイプ
 * ============================================================ */

/* --- 共通実装 (tcpServer.c) --- */

/**
 *******************************************************************************
 *  @brief          TCP 通信メインループ (デフォルト実装)。
 *  @param[in]      fd クライアントソケット。
 *
 *  受信したデータをそのまま返します。クライアントが切断すると戻ります。
 *  ソケットは本関数内で閉じます。fork モード・prefork モード共用。
 *******************************************************************************
 */
void handle_client_session(ClientFd fd);

/* --- プラットフォームフック (各プラットフォームファイルで実装) --- */

/**
 *******************************************************************************
 *  @brief          プラットフォーム初期化 (Windows: WSAStartup / Linux: no-op)。
 *  @param[in]      session_fn セッション処理関数。g_session_fn に保存されます。
 *******************************************************************************
 */
void platform_init(ClientSessionFn session_fn);

/**
 *******************************************************************************
 *  @brief          プラットフォーム後処理 (Windows: WSACleanup / Linux: no-op)。
 *******************************************************************************
 */
void platform_cleanup(void);

/**
 *******************************************************************************
 *  @brief          内部起動引数を処理します。
 *  @param[in]      argc コマンドライン引数の数。
 *  @param[in]      argv コマンドライン引数の配列。
 *  @return         内部起動引数を処理した場合は 1、通常起動の場合は 0 を返します。
 *
 *  Windows では `--child <handle>` / `--worker <pipe>` を検出して処理します。
 *  Linux では常に 0 を返します。
 *******************************************************************************
 */
int dispatch_internal_args(int argc, char *argv[]);

/**
 *******************************************************************************
 *  @brief          fork モードのサーバーを起動します。
 *  @param[in]      port 待ち受けポート番号。
 *******************************************************************************
 */
void run_fork_server(int port);

/**
 *******************************************************************************
 *  @brief          prefork モードのサーバーを起動します。
 *  @param[in]      port             待ち受けポート番号。
 *  @param[in]      num_workers      事前生成するワーカープロセス数。
 *  @param[in]      conns_per_worker 1 ワーカーあたりの同時接続数。
 *                                   1 の場合は従来の逐次処理。
 *                                   2 以上の場合はイベント駆動型の多重接続処理。
 *******************************************************************************
 */
void run_prefork_server(int port, int num_workers, int conns_per_worker);

#endif /* TCPSERVER_H */
