/**
 *******************************************************************************
 *  @file           tcpServer_windows.c
 *  @brief          TCP サーバー サンプルの Windows 向け処理を実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/17
 *  @version        1.0.0
 *
 *  Windows 固有のサーバー処理を実装します。
 *  - CreateProcess() を使った接続ごとプロセス生成 (run_fork_server)
 *  - 名前付きパイプを使ったプリフォーク           (run_prefork_server)
 *  - プラットフォーム フック                      (platform_init / platform_cleanup /
 *                                                  dispatch_internal_args)
 *
 *  main() / handle_client_session() は tcpServer.c に実装します。
 *  g_session_fn の実体は tcpServer_common.c に定義されます。
 *
 *  内部起動パターン (ユーザーは直接使わない):
 *  - `tcpServer --child <socket_handle>` : fork 版の子プロセス
 *  - `tcpServer --worker <pipe_name>`    : prefork 版のワーカー プロセス
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/base/platform.h>
#include <cplat/crt/stdlib.h>

#if defined(PLATFORM_WINDOWS)

    #include "tcpServer.h" /* WIN32_LEAN_AND_MEAN / windows.h / winsock2.h / ws2tcpip.h を内包 */
    #include <cplat/crt/select.h>
    #include <cplat/crt/path.h>
    #include <cplat/crt/stdio.h>
    #include <cplat/crt/wchar_conv.h>
    #include <cplat/sync/sync.h>
    #include <cplat/trace/tracer.h>

    #if defined(TCPSERVER_PREFORK_TEST)
        #include "prefork_probe.h"
    #endif
    #include <cplat/win32/win32.h>

    #include <stdio.h>
    #include <cplat/runtime/shutdown.h>
    #include <limits.h>
    #include <stdlib.h>
    #include <stdint.h>
    #include <string.h>
    #include <wchar.h>

    /** 名前付きパイプ名フォーマット。 */
    #define TCPSERVER_PIPE_NAME_FMT "\\\\.\\pipe\\tcpserver_worker_%d"

    /** 名前付きパイプ名を格納する配列の大きさ [byte]。書式と int の最大桁数から決まる長さ以上とする。 */
    #define TCPSERVER_PIPE_NAME_SIZE 64U

    /** ポート番号の文字列を格納する配列の大きさ [byte]。 */
    #define TCPSERVER_PORT_STR_SIZE 8U

    /** Windows の拡張パス上限を UTF-8 で収める実行ファイル パスの容量 [byte]。 */
    #define TCPSERVER_EXE_PATH_UTF8_CAP ((size_t)32767U * 3U + 1U)

    /** CreateProcessW のコマンド ライン上限 [UTF-16 code unit]。終端 NUL を含む。 */
    #define TCPSERVER_CMDLINE_WCHAR_MAX 32767U

    /** ワーカー引数だけを格納する容量 [byte]。実行ファイルのパスは含まない。 */
    #define TCPSERVER_WORKER_ARGS_UTF8_CAP \
        ((sizeof(" --worker ") - 1U) + (TCPSERVER_PIPE_NAME_SIZE - 1U) + (sizeof(" --conns-per-worker ") - 1U) + \
         sizeof("-2147483648"))

    /** fork 引数だけを格納する容量 [byte]。実行ファイルのパスは含まない。 */
    #define TCPSERVER_CHILD_ARGS_UTF8_CAP sizeof(" --child 18446744073709551615")

    /** ワーカーごとの名前付きパイプの最大インスタンス数。 */
    #define TCPSERVER_PIPE_MAX_INSTANCES 1U

    /** 名前付きパイプの送受信バッファーの容量 [byte]。ソケットの複製情報 (WSAPROTOCOL_INFOW) を収める。 */
    #define TCPSERVER_PIPE_BUF_SIZE 4096U

    /**
     * 多重接続のワーカーが名前付きパイプを確認する周期 [ms]。新しい接続の受け取りの、待機による遅れの上界になる。
     * select() の tv_usec へ換算して渡すため、1000 [ms] 未満とする。
     */
    #define TCPSERVER_WORKER_PIPE_POLL_MS 10

static_assert(TCPSERVER_PIPE_NAME_SIZE >= sizeof("\\\\.\\pipe\\tcpserver_worker_-2147483648"),
              "tcpServer: TCPSERVER_PIPE_NAME_SIZE too small");
static_assert(TCPSERVER_PORT_STR_SIZE >= sizeof("65535"), "tcpServer: TCPSERVER_PORT_STR_SIZE too small");
static_assert(TCPSERVER_WORKER_ARGS_UTF8_CAP >=
                  sizeof(" --worker \\\\.\\pipe\\tcpserver_worker_-2147483648 --conns-per-worker -2147483648"),
              "tcpServer: TCPSERVER_WORKER_ARGS_UTF8_CAP too small");
static_assert(TCPSERVER_PIPE_BUF_SIZE >= sizeof(WSAPROTOCOL_INFOW), "tcpServer: TCPSERVER_PIPE_BUF_SIZE too small");
static_assert(TCPSERVER_WORKER_PIPE_POLL_MS > 0 && TCPSERVER_WORKER_PIPE_POLL_MS < 1000,
              "tcpServer: TCPSERVER_WORKER_PIPE_POLL_MS must be in (0, 1000)");

/**
 *  @brief          実行ファイルの UTF-8 パスを取得します。
 *  @param[out]     path_out 成功時にパスの所有権を格納するポインター。
 *  @return         成功時は ERROR_SUCCESS、失敗時は Windows のエラー コードを返します。
 *
 *  Windows の拡張パス上限に対応する容量をヒープに確保します。取得失敗または切り詰めの場合は領域を解放し、
 *  成功時だけ呼び出し元へ所有権を渡します。UTF-16 の 1 code unit あたり最大 3 [byte] を見込みます。
 *
 *  see: https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamew
 *  see: https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation
 */
static DWORD get_native_executable_path(char **path_out)
{
    char *path;
    DWORD copied;
    DWORD error;

    if (path_out == NULL)
    {
        return ERROR_INVALID_PARAMETER;
    }

    *path_out = NULL;
    path = (char *)cplat_malloc(TCPSERVER_EXE_PATH_UTF8_CAP);
    if (path == NULL)
    {
        return ERROR_OUTOFMEMORY;
    }

    copied = GetModuleFileNameU(NULL, path, (DWORD)TCPSERVER_EXE_PATH_UTF8_CAP);
    if (copied == 0U)
    {
        error = GetLastError();
        cplat_free(path);
        return error;
    }
    if (copied >= (DWORD)TCPSERVER_EXE_PATH_UTF8_CAP)
    {
        cplat_free(path);
        return ERROR_INSUFFICIENT_BUFFER;
    }

    *path_out = path;
    return ERROR_SUCCESS;
}

/**
 *  @brief          実行ファイルのパスと引数から起動用コマンド ラインを作成します。
 *  @param[in]      executable_path 実行ファイルの UTF-8 パス。
 *  @param[in]      arguments       引数部分。先頭は空白です。
 *  @param[out]     command_line_out 成功時にコマンド ラインの所有権を格納するポインター。
 *  @return         成功時は ERROR_SUCCESS、失敗時は Windows のエラー コードを返します。
 *
 *  必要な UTF-8 容量をオーバーフローなしで計算し、UTF-16 code unit 数と終端 NUL が上限内であることを確認します。
 *  成功時は呼び出し元が領域を解放します。失敗時は途中の領域を解放します。
 *
 *  see: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
 */
static DWORD build_command_line(const char *executable_path, const char *arguments, char **command_line_out)
{
    size_t executable_length;
    size_t arguments_length;
    size_t capacity;
    char *command_line;
    wchar_t *wide_command_line = NULL;
    int result;

    if (command_line_out == NULL)
    {
        return ERROR_INVALID_PARAMETER;
    }

    *command_line_out = NULL;
    if (executable_path == NULL || arguments == NULL)
    {
        return ERROR_INVALID_PARAMETER;
    }

    executable_length = strlen(executable_path);
    arguments_length = strlen(arguments);
    if (arguments_length > SIZE_MAX - 3U || executable_length > SIZE_MAX - arguments_length - 3U)
    {
        return ERROR_INSUFFICIENT_BUFFER;
    }
    capacity = executable_length + arguments_length + 3U;

    command_line = (char *)cplat_malloc(capacity);
    if (command_line == NULL)
    {
        return ERROR_OUTOFMEMORY;
    }

    result = cplat_snprintf(command_line, capacity, "\"%s\"%s", executable_path, arguments);
    if (result != CPLAT_OK)
    {
        cplat_free(command_line);
        return (result == CPLAT_ERR_BUFFER_TOO_SMALL) ? ERROR_INSUFFICIENT_BUFFER : ERROR_INVALID_DATA;
    }

    result = cplat_utf8_to_wstr_alloc(&wide_command_line, command_line);
    if (result != CPLAT_OK)
    {
        cplat_free(wide_command_line);
        cplat_free(command_line);
        return (result == CPLAT_ERR_OUT_OF_MEMORY) ? ERROR_OUTOFMEMORY : ERROR_NO_UNICODE_TRANSLATION;
    }

    /* CreateProcessW の上限は終端 NUL を含む UTF-16 code unit 数です。 */
    if (wcslen(wide_command_line) >= TCPSERVER_CMDLINE_WCHAR_MAX)
    {
        cplat_free(wide_command_line);
        cplat_free(command_line);
        return ERROR_INSUFFICIENT_BUFFER;
    }

    cplat_free(wide_command_line);
    *command_line_out = command_line;
    return ERROR_SUCCESS;
}

/**
 *  @brief          指定したアプリケーション名とコマンド ラインで子プロセスを起動します。
 *  @param[in]      executable_path 実行ファイルの UTF-8 パス。
 *  @param[in]      arguments       引数部分。先頭は空白です。
 *  @param[in]      inherit_handles ハンドル継承の有無。
 *  @param[out]     process_info 成功時のプロセス情報。
 *  @param[out]     error_out 失敗時の Windows エラー コード。
 *  @return         起動成功時は TRUE、失敗時は FALSE を返します。
 *
 *  アプリケーション名には引用符のない実行ファイルのパスを渡し、コマンド ラインの先頭では同じパスを引用符で囲みます。
 *  CreateProcessU() の失敗直後に最終エラーを保存し、領域の解放後もその値を返します。
 *
 *  see: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
 */
static BOOL start_child_process(const char *executable_path, const char *arguments, const BOOL inherit_handles,
                                LPPROCESS_INFORMATION process_info, DWORD *error_out)
{
    char *command_line = NULL;
    DWORD error;
    STARTUPINFOW startup_info = {sizeof(startup_info)};

    if (error_out == NULL)
    {
        return FALSE;
    }
    *error_out = ERROR_INVALID_PARAMETER;
    if (process_info == NULL)
    {
        return FALSE;
    }

    error = build_command_line(executable_path, arguments, &command_line);
    if (error != ERROR_SUCCESS)
    {
        *error_out = error;
        return FALSE;
    }

    if (!CreateProcessU(executable_path, command_line, NULL, NULL, inherit_handles, 0, NULL, NULL, &startup_info,
                        process_info))
    {
        error = GetLastError();
        cplat_free(command_line);
        *error_out = error;
        return FALSE;
    }

    cplat_free(command_line);
    *error_out = ERROR_SUCCESS;
    return TRUE;
}

/* NONE は通常の出力先へ送らず、テスト時のプロセス内フックでのみ記録する。 */
static void prefork_trace(const char *stage, const DWORD worker_pid, const SOCKET connection, const int result)
{
    cplat_tracer *tracer = NULL;
    #if defined(TCPSERVER_PREFORK_TEST)
    tracer = prefork_probe_tracer();
    #endif
    /* トレースはテストの観測用の補助の記録であり、出力の失敗で処理を変えないため、戻り値は明示的に破棄する。 */
    (void)cplat_tracer_writef_at(tracer, CPLAT_TRACE_LEVEL_NONE, NULL, "%s worker=%lu connection=%llu result=%d", stage,
                                 worker_pid, (unsigned long long)connection, result);
}

/* ワーカーへの引き渡し後も、親が所有する記述子の解放は親の責務である。
 * see: https://learn.microsoft.com/en-us/windows/win32/winsock/shared-sockets-2
 */
static void close_parent_socket(const SOCKET connection, const DWORD worker_pid)
{
    #if defined(TCPSERVER_PREFORK_TEST)
    if (prefork_probe_skip_close(connection))
    {
        prefork_trace("parent_close_skipped", worker_pid, connection, 0);
        return;
    }
    #endif
    int result = closesocket(connection);
    prefork_trace("parent_close", worker_pid, connection, result);
}

/*
 * ============================================================
 *  内部型定義
 * ============================================================
 */

/**
 *  @brief          ワーカー プロセスの情報を管理する構造体。
 */
typedef struct WorkerInfo
{
    HANDLE pipe;                 /**< ワーカーとの通信用パイプ。 */
    HANDLE process;              /**< ワーカー プロセスのハンドル。 */
    int conn_count;              /**< 受領済みのアクティブ接続数。 */
    int pending;                 /**< 受領通知待ちの予約数 (0 または 1)。 */
    int alive;                   /**< パイプを使用できる場合は 1。 */
    int ack_ok;                  /**< 最後の受領結果。 */
    DWORD process_id;            /**< 複製先のプロセス ID。 */
    HANDLE ack_event;            /**< 監視スレッドが受領結果または切断を通知するイベント。 */
    CRITICAL_SECTION state_lock; /**< 接続数、予約、稼働状態を保護する。 */
} WorkerInfo;

/**
 *  @brief          ワーカー監視スレッドへ渡すコンテキスト。
 */
typedef struct WorkerMonitorArg
{
    int id;               /**< ワーカー ID。 */
    WorkerInfo *workers;  /**< ワーカー情報配列へのポインター。 */
    HANDLE *events;       /**< ワーカー イベント配列へのポインター。 */
    int conns_per_worker; /**< 1 ワーカーあたりの同時接続数上限。 */
} WorkerMonitorArg;

enum WorkerReplyType
{
    WORKER_ACK = 1,
    WORKER_DONE = 2
};

typedef struct WorkerReply
{
    DWORD type;
    DWORD accepted;
} WorkerReply;

/* byte モードのパイプでは短い読み書きを繰り返して構造体全体を転送する。
 * see: https://learn.microsoft.com/windows/win32/ipc/named-pipe-type-read-and-wait-modes
 * see: https://learn.microsoft.com/windows/win32/ipc/synchronous-and-overlapped-input-and-output
 */
static int pipe_transfer(const HANDLE pipe, void *data, DWORD length, const int writing)
{
    unsigned char *cursor = (unsigned char *)data;
    while (length > 0U)
    {
        DWORD transferred = 0;
        OVERLAPPED operation = {0};
        BOOL ok;
        operation.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (operation.hEvent == NULL)
        {
            return 0;
        }
        ok = writing ? WriteFile(pipe, cursor, length, &transferred, &operation)
                     : ReadFile(pipe, cursor, length, &transferred, &operation);
        if (ok || GetLastError() == ERROR_IO_PENDING)
        {
            ok = GetOverlappedResult(pipe, &operation, &transferred, TRUE);
        }
        /* 1 回の転送に使ったイベント ハンドルの後始末であり、転送の結果は ok で判定するため、戻り値は明示的に破棄する。 */
        (void)CloseHandle(operation.hEvent);
        if (!ok || transferred == 0U)
        {
            return 0;
        }
        cursor += transferred;
        length -= transferred;
    }
    return 1;
}

static int worker_reply(const HANDLE pipe, const DWORD type, const DWORD accepted)
{
    #if defined(TCPSERVER_PREFORK_TEST)
    if (type == WORKER_DONE)
    {
        prefork_probe_before_done();
    }
    #endif
    WorkerReply reply = {type, accepted};
    return pipe_transfer(pipe, &reply, sizeof(reply), 1);
}

/* 親が WSADuplicateSocketW で作成した情報を、対象ワーカー内でのみ復元する。
 * see: https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-wsaduplicatesocketw
 */
static SOCKET receive_socket(const HANDLE pipe)
{
    WSAPROTOCOL_INFOW protocol_info;
    SOCKET result;
    if (!pipe_transfer(pipe, &protocol_info, sizeof(protocol_info), 0))
    {
        return INVALID_SOCKET;
    }
    #if defined(TCPSERVER_PREFORK_TEST)
    result = prefork_probe_restore(&protocol_info);
    prefork_probe_before_ack(pipe, result);
    #else
    result =
        WSASocketW(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, &protocol_info, 0, WSA_FLAG_OVERLAPPED);
    #endif
    prefork_trace("restore", GetCurrentProcessId(), result, result != INVALID_SOCKET);
    if (!worker_reply(pipe, WORKER_ACK, result != INVALID_SOCKET))
    {
        if (result != INVALID_SOCKET)
        {
            /*
             * 受領通知を送れなかったソケットを捨てる後始末であり、すでに INVALID_SOCKET を返す経路のため、
             * 戻り値は明示的に破棄する。
             */
            (void)closesocket(result);
        }
        return INVALID_SOCKET;
    }
    return result;
}

/*
 * ============================================================
 *  内部ヘルパー
 * ============================================================
 */

/**
 *  @brief          listen ソケットを作成してバインドし、待ち受けを開始します。
 *  @param[in]      port 待ち受けポート番号。
 *  @return         listen ソケット。
 *
 *  @attention      失敗した場合は cplat_exit() で終了します。
 */
static SOCKET create_listen_socket(int port)
{
    struct addrinfo hints = {0}, *result;
    char port_str[TCPSERVER_PORT_STR_SIZE];

    /*
     * port は main() で PORT_MIN から PORT_MAX に検査済みで、5 文字と終端が port_str に収まるため、
     * 戻り値は明示的に破棄する。
     */
    (void)sprintf_s(port_str, sizeof(port_str), "%d", port);

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    /* 診断の出力先がほかになく、直後にプロセスを終了するため、この関数の fprintf の戻り値は明示的に破棄する。 */
    if (getaddrinfo(NULL, port_str, &hints, &result) != 0)
    {
        (void)fprintf(stderr, "getaddrinfo 失敗\n");
        cplat_exit(1);
    }

    SOCKET listen_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (listen_socket == INVALID_SOCKET)
    {
        (void)fprintf(stderr, "socket 失敗\n");
        freeaddrinfo(result);
        cplat_exit(1);
    }

    if (bind(listen_socket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR)
    {
        (void)fprintf(stderr, "bind 失敗\n");
        freeaddrinfo(result);
        cplat_exit(1);
    }

    freeaddrinfo(result);

    /*
     * backlog は、接続を待たせるキューの長さである。prefork モードでは、全ワーカーが処理中のときの接続が、ここで待つ。
     * 指定値は、下位のサービス プロバイダーが制限する。実際の値を得る標準の手段はない。
     * see: https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-listen
     */
    if (listen(listen_socket, LISTEN_BACKLOG) == SOCKET_ERROR)
    {
        (void)fprintf(stderr, "listen 失敗\n");
        cplat_exit(1);
    }

    return listen_socket;
}

/**
 *  @brief          fork モードの子プロセスとして起動された場合のクライアント処理。
 *  @param[in]      client_socket `--child` 引数で受け取ったクライアント ソケット。
 *
 *  g_session_fn() で通信メイン ループを実行し、platform_cleanup() 後に ExitProcess() で終了します。
 */
__declspec(noreturn) static void run_as_fork_child(SOCKET client_socket)
{
    g_session_fn(client_socket);
    platform_cleanup();
    ExitProcess(0);
}

/**
 *  @brief          prefork モードのワーカー プロセスとして起動された場合の処理。
 *  @param[in]      pipe_name        `--worker` 引数で受け取ったパイプ名。
 *  @param[in]      conns_per_worker 同時接続数。1 の場合は逐次処理、2 以上は多重処理。
 *
 *  conns_per_worker == 1 の場合:
 *    パイプからソケットを受け取り g_session_fn() で処理し完了通知を返す従来の逐次処理。
 *
 *  conns_per_worker > 1 の場合:
 *    PeekNamedPipe() でパイプを非ブロッキング チェックしながら、select() で
 *    アクティブな複数ソケットを監視するイベント ループ。接続が閉じるたびに親へ
 *    型付き完了通知を送信します。
 */
static void worker_loop(const char *pipe_name, int conns_per_worker)
{
    /*
     * 稼働状況は利用者が画面で確認する表示であり、診断も出力先がほかにないため、
     * この関数の printf と fprintf の戻り値は明示的に破棄する。
     */
    (void)printf("[ワーカー PID %lu] 起動完了\n", GetCurrentProcessId());

    HANDLE pipe =
        CreateFileU(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

    if (pipe == INVALID_HANDLE_VALUE)
    {
        (void)fprintf(stderr, "[ワーカー] パイプ接続失敗\n");
        return;
    }

    if (conns_per_worker == CONNS_PER_WORKER_SEQUENTIAL)
    {
        /* --- 従来の逐次処理 --- */
        while (1)
        {
            SOCKET client_socket = receive_socket(pipe);
            if (client_socket == INVALID_SOCKET)
            {
                break;
            }

            (void)printf("[ワーカー PID %lu] クライアント接続\n", GetCurrentProcessId());
            g_session_fn(client_socket);
            (void)printf("[ワーカー PID %lu] 次のソケット待機\n", GetCurrentProcessId());

            /* 親プロセスへ処理完了を通知する。 */
            if (!worker_reply(pipe, WORKER_DONE, 1))
            {
                break;
            }
        }
    }
    else
    {
        /* --- PeekNamedPipe + select による多重接続処理 --- */
        SOCKET *active = (SOCKET *)cplat_malloc((size_t)conns_per_worker * sizeof(SOCKET));
        if (active == NULL)
        {
            /*
             * メモリ確保の失敗でワーカーを終える経路のパイプの後始末であり、失敗しても動作が変わらないため、
             * 戻り値は明示的に破棄する。
             */
            (void)CloseHandle(pipe);
            return;
        }

        int active_count = 0;
        char buf[BUFFER_SIZE];

        /* 名前付きパイプからの新規ソケット受信とアクティブ ソケットの I/O を多重監視する。 */
        while (1)
        {
            /* パイプに受信可能な新規ソケットがあるかを非ブロッキングで確認する。 */
            DWORD avail = 0;
            if (!PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL))
            {
                break; /* パイプ切断 */
            }

            if (avail >= sizeof(WSAPROTOCOL_INFOW) && active_count < conns_per_worker)
            {
                SOCKET new_sock = receive_socket(pipe);
                if (new_sock == INVALID_SOCKET)
                {
                    break;
                }
                active[active_count++] = new_sock;
            }

            if (active_count == 0)
            {
                /* アクティブな接続がない場合は、パイプを確認する周期だけ待ってからパイプを再確認する。 */
                cplat_sleep_ms(TCPSERVER_WORKER_PIPE_POLL_MS);
                continue;
            }

            /* select() で全アクティブ ソケットを、パイプを確認する周期のタイムアウト付きで監視する。 */
            fd_set read_fds;
            FD_ZERO(&read_fds);

            for (int i = 0; i < active_count; i++)
            {
                CPLAT_FD_SET(active[i], &read_fds);
            }

            struct timeval tv = {0, TCPSERVER_WORKER_PIPE_POLL_MS *
                                        1000}; /* パイプを確認する周期 ([ms] から [us] へ換算) */

            /*
             * Windows Sockets において select() の第 1 引数 nfds は互換性のためにのみ存在し、無視される。
             * 監視に失敗した場合は、診断を出してループを抜け、残りの接続とパイプを閉じてワーカーを終える。
             * see: https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-select
             */
            if (select(0, &read_fds, NULL, NULL, &tv) == SOCKET_ERROR)
            {
                (void)fprintf(stderr, "[ワーカー PID %lu] select 失敗: %d\n", GetCurrentProcessId(), WSAGetLastError());
                break;
            }

            /* 末尾から走査して配列詰めを安全に行う。 */
            for (int i = active_count - 1; i >= 0; i--)
            {
                if (!FD_ISSET(active[i], &read_fds))
                {
                    continue;
                }

                /*
                 * 受信したデータをエコーバックする。送信の失敗と一部だけの送信は、エコーが成立しないため、
                 * 診断を出して切断と同じく接続を閉じる。
                 */
                int n = recv(active[i], buf, (int)BUFFER_SIZE, 0);
                int send_failed = 0;

                if (n > 0 && send(active[i], buf, n, 0) != n)
                {
                    (void)fprintf(stderr, "[ワーカー PID %lu] send 失敗: %d\n", GetCurrentProcessId(), WSAGetLastError());
                    send_failed = 1;
                }

                if (n <= 0 || send_failed)
                {
                    /*
                     * 切断した接続、または送信に失敗した接続の後始末であり、失敗しても動作が変わらないため、
                     * 戻り値は明示的に破棄する。
                     */
                    (void)closesocket(active[i]);
                    active[i] = active[--active_count]; /* 末尾と入れ替えて削除 */
                    (void)printf("[ワーカー PID %lu] 接続終了 (残 %d 接続)\n", GetCurrentProcessId(), active_count);

                    /* 親プロセスへ接続 1 本分の処理完了を通知する。 */
                    if (!worker_reply(pipe, WORKER_DONE, 1))
                    {
                        /*
                         * 親への完了通知に失敗してワーカーを終える経路の接続とパイプの後始末であり、
                         * 失敗しても動作が変わらないため、closesocket と CloseHandle の戻り値は明示的に破棄する。
                         */
                        for (int j = 0; j < active_count; ++j)
                        {
                            (void)closesocket(active[j]);
                        }
                        cplat_free(active);
                        (void)CloseHandle(pipe);
                        return;
                    }
                }
            }
        }

        /* ループを抜けてワーカーを終える経路の接続の後始末であり、失敗しても動作が変わらないため、戻り値は明示的に破棄する。 */
        for (int i = 0; i < active_count; ++i)
        {
            (void)closesocket(active[i]);
        }
        cplat_free(active);
    }

    /* ワーカーの終了時のパイプの後始末であり、失敗しても動作が変わらないため、戻り値は明示的に破棄する。 */
    (void)CloseHandle(pipe);
    (void)printf("[ワーカー PID %lu] 終了\n", GetCurrentProcessId());
}

/**
 *  @brief          イベントをシグナル状態にします。失敗した場合はプロセスを終了します。
 *  @param[in]      event 対象のイベント。
 *
 *  イベントはワーカーの空きと受領の通知そのものであり、状態を変えられない場合は
 *  接続の振り分けが成立しないため、診断を出して終了します。
 *
 *  @attention      失敗した場合は cplat_exit() で終了します。
 */
static void set_event_or_exit(const HANDLE event)
{
    if (!SetEvent(event))
    {
        /* 診断の出力先がほかになく、直後にプロセスを終了するため、戻り値は明示的に破棄する。 */
        (void)fprintf(stderr, "SetEvent 失敗: %lu\n", GetLastError());
        cplat_exit(1);
    }
}

/**
 *  @brief          イベントを非シグナル状態にします。失敗した場合はプロセスを終了します。
 *  @param[in]      event 対象のイベント。
 *
 *  理由は set_event_or_exit() と同じです。
 *
 *  @attention      失敗した場合は cplat_exit() で終了します。
 */
static void reset_event_or_exit(const HANDLE event)
{
    if (!ResetEvent(event))
    {
        /* 診断の出力先がほかになく、直後にプロセスを終了するため、戻り値は明示的に破棄する。 */
        (void)fprintf(stderr, "ResetEvent 失敗: %lu\n", GetLastError());
        cplat_exit(1);
    }
}

/**
 *  @brief          ワーカーの完了を監視するスレッド。
 *  @param[in]      arg WorkerMonitorArg へのポインター。
 *
 *  ワーカーからの完了通知を受け取ると、ワーカーの状態を「空き」に更新し
 *  イベントをシグナル状態にします。
 */
static void worker_monitor_thread(void *arg)
{
    WorkerMonitorArg *ctx = (WorkerMonitorArg *)arg;
    WorkerInfo *worker = &ctx->workers[ctx->id];
    WorkerReply reply;
    /* このスレッドだけが親側パイプを読み、ACK と DONE を振り分ける。 */
    while (pipe_transfer(worker->pipe, &reply, sizeof(reply), 0))
    {
        EnterCriticalSection(&worker->state_lock);
        if (reply.type == WORKER_ACK && worker->pending)
        {
            worker->ack_ok = reply.accepted != 0U;
            worker->pending = 0;
            if (worker->ack_ok)
            {
                ++worker->conn_count;
            }
            set_event_or_exit(worker->ack_event);
        }
        else if (reply.type == WORKER_DONE && worker->conn_count > 0)
        {
            --worker->conn_count;
        }
        else
        {
            LeaveCriticalSection(&worker->state_lock);
            break;
        }
        int connection_count = worker->conn_count;
        if (worker->alive && worker->conn_count + worker->pending < ctx->conns_per_worker)
        {
            set_event_or_exit(ctx->events[ctx->id]);
        }
        else
        {
            reset_event_or_exit(ctx->events[ctx->id]);
        }
        LeaveCriticalSection(&worker->state_lock);
        prefork_trace(reply.type == WORKER_ACK ? "ack" : "done", worker->process_id, INVALID_SOCKET,
                      reply.type == WORKER_ACK ? (int)reply.accepted : connection_count);
    }
    /* ワーカーの終了後にパイプを切る後始末であり、以後このパイプを使わないため、戻り値は明示的に破棄する。 */
    (void)DisconnectNamedPipe(worker->pipe);
    EnterCriticalSection(&worker->state_lock);
    worker->alive = 0;
    worker->pending = 0;
    worker->ack_ok = 0;
    set_event_or_exit(worker->ack_event);
    set_event_or_exit(ctx->events[ctx->id]); /* 空き待機側に終了を通知する。 */
    LeaveCriticalSection(&worker->state_lock);
    prefork_trace("monitor_dead", worker->process_id, INVALID_SOCKET, 0);
}

/**
 *  @brief          接続を受け付けられるワーカーのインデックスを返します。
 *  @param[in]      workers          ワーカー情報配列。
 *  @param[in]      events           ワーカー イベント配列。
 *  @param[in]      n                ワーカー数。
 *  @param[in]      conns_per_worker 1 ワーカーあたりの同時接続数上限。
 *  @return         空きワーカーのインデックス (0 〜 n - 1)。
 *
 *  conn_count < conns_per_worker のワーカーを探します。
 *  全ワーカーが満杯の場合は WaitForMultipleObjects でいずれかに空きが生じるまで待機します。
 */
static int find_available_worker(WorkerInfo *workers, HANDLE *events, int n, int conns_per_worker)
{
    for (;;)
    {
        int any_alive = 0;
        for (int i = 0; i < n; ++i)
        {
            WorkerInfo *worker = &workers[i];
            EnterCriticalSection(&worker->state_lock);
            if (worker->alive)
            {
                any_alive = 1;
                if (worker->conn_count + worker->pending < conns_per_worker)
                {
                    worker->pending = 1;
                    worker->ack_ok = 0;
                    reset_event_or_exit(worker->ack_event);
                    if (worker->conn_count + worker->pending == conns_per_worker)
                    {
                        reset_event_or_exit(events[i]);
                    }
                    LeaveCriticalSection(&worker->state_lock);
                    return i;
                }
            }
            else
            {
                reset_event_or_exit(events[i]);
            }
            LeaveCriticalSection(&worker->state_lock);
        }
        if (!any_alive)
        {
            return -1;
        }
        prefork_trace("capacity_wait", 0, INVALID_SOCKET, 0);
        if (WaitForMultipleObjects((DWORD)n, events, FALSE, INFINITE) == WAIT_FAILED)
        {
            return -1;
        }
    }
}

/**
 *  @brief          n 個のワーカー プロセスを起動します。
 *  @param[in]      workers ワーカー情報配列。
 *  @param[in]      events  ワーカー イベント配列。
 *  @param[in]      n       ワーカー数。
 *  @param[in]      conns_per_worker 1 ワーカーあたりの同時接続数。
 *
 *  各ワーカーに名前付きパイプと監視スレッドを作成し、
 *  自分自身を `--worker <pipe_name>` 引数付きで再起動します。
 *
 *  @attention      実行ファイルのパスの取得、パイプ作成、またはプロセス起動に失敗した場合は cplat_exit() で終了します。
 */
static void start_prefork_workers(WorkerInfo *workers, HANDLE *events, int n, int conns_per_worker)
{
    char pipe_name[TCPSERVER_PIPE_NAME_SIZE];
    char *exepath = NULL;
    DWORD path_error;

    /*
     * 診断の出力先がほかになく、起動状況は利用者が画面で確認する表示であるため、
     * この関数の fprintf と printf の戻り値は明示的に破棄する。
     */
    path_error = get_native_executable_path(&exepath);
    if (path_error != ERROR_SUCCESS)
    {
        (void)fprintf(stderr, "実行ファイルのパスを取得できません (GetLastError=%lu)\n", path_error);
        cplat_exit(1);
    }

    /* 監視スレッドの引数はプロセス終了まで有効である必要があるため、ヒープに確保する。 */
    WorkerMonitorArg *args = (WorkerMonitorArg *)cplat_malloc((size_t)n * sizeof(WorkerMonitorArg));
    if (args == NULL)
    {
        cplat_free(exepath);
        (void)fprintf(stderr, "malloc 失敗\n");
        cplat_exit(1);
    }

    /* 各ワーカー向けの名前付きパイプと監視スレッドを作成し、ワーカー プロセスを起動する。 */
    for (int i = 0; i < n; i++)
    {
        /* パイプ名は書式と int の最大桁数から pipe_name に収まるため、戻り値は明示的に破棄する。 */
        (void)sprintf_s(pipe_name, sizeof(pipe_name), TCPSERVER_PIPE_NAME_FMT, i);

        workers[i].pipe = CreateNamedPipeU(
            pipe_name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            TCPSERVER_PIPE_MAX_INSTANCES, TCPSERVER_PIPE_BUF_SIZE, TCPSERVER_PIPE_BUF_SIZE, 0, NULL);

        if (workers[i].pipe == INVALID_HANDLE_VALUE)
        {
            (void)fprintf(stderr, "パイプ作成失敗: %d\n", i);
            cplat_free(args);
            cplat_free(exepath);
            cplat_exit(1);
        }

        /* ワーカーの空き状態を通知するイベントを作成する (初期状態はシグナル状態)。 */
        events[i] = CreateEvent(NULL, TRUE, TRUE, NULL);
        workers[i].ack_event = CreateEvent(NULL, TRUE, FALSE, NULL);
        InitializeCriticalSection(&workers[i].state_lock);
        workers[i].pending = 0;
        workers[i].ack_ok = 0;
        workers[i].alive = 1;
        if (events[i] == NULL || workers[i].ack_event == NULL)
        {
            cplat_free(args);
            cplat_free(exepath);
            cplat_exit(1);
        }

        char arguments[TCPSERVER_WORKER_ARGS_UTF8_CAP];
        int format_result = cplat_snprintf(arguments, sizeof(arguments), " --worker %s --conns-per-worker %d",
                                           pipe_name, conns_per_worker);
        PROCESS_INFORMATION pi = {0};
        DWORD process_error = ERROR_SUCCESS;

        if (format_result != CPLAT_OK ||
            !start_child_process(exepath, arguments, FALSE, &pi, &process_error))
        {
            if (format_result != CPLAT_OK)
            {
                process_error = (format_result == CPLAT_ERR_BUFFER_TOO_SMALL) ? ERROR_INSUFFICIENT_BUFFER
                                                                               : ERROR_INVALID_DATA;
            }
            cplat_free(args);
            cplat_free(exepath);
            (void)fprintf(stderr, "ワーカー起動失敗: %d (GetLastError=%lu)\n", i, process_error);
            cplat_exit(1);
        }

        workers[i].process = pi.hProcess;
        workers[i].process_id = pi.dwProcessId;
    #if defined(TCPSERVER_PREFORK_TEST)
        prefork_probe_worker_started(i, pi.dwProcessId);
    #endif
        workers[i].conn_count = 0;
        /* 使わないワーカーのスレッド ハンドルを閉じる処理であり、失敗しても動作が変わらないため、戻り値は明示的に破棄する。 */
        (void)CloseHandle(pi.hThread);

        /* ワーカー プロセスが名前付きパイプに接続するのを待機する。 */
        {
            OVERLAPPED connection = {0};
            DWORD ignored;
            int connected = 0;
            connection.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
            if (connection.hEvent != NULL)
            {
                if (ConnectNamedPipe(workers[i].pipe, &connection))
                {
                    connected = 1;
                }
                else
                {
                    DWORD error = GetLastError();
                    if (error == ERROR_PIPE_CONNECTED)
                    {
                        connected = 1;
                    }
                    else if (error == ERROR_IO_PENDING)
                    {
                        HANDLE waits[] = {connection.hEvent, pi.hProcess};
                        if (WaitForMultipleObjects((DWORD)(sizeof(waits) / sizeof(waits[0])), waits, FALSE, INFINITE) ==
                            WAIT_OBJECT_0)
                        {
                            connected = GetOverlappedResult(workers[i].pipe, &connection, &ignored, FALSE) != 0;
                        }
                        else
                        {
                            /*
                             * 取り消す I/O がすでに完了していても直後に完了を待ち、取り消した接続待ちの結果は使わず
                             * ワーカーの接続の失敗として後始末へ進むため、この 2 つの戻り値は明示的に破棄する。
                             */
                            (void)CancelIoEx(workers[i].pipe, &connection);
                            (void)GetOverlappedResult(workers[i].pipe, &connection, &ignored, TRUE);
                        }
                    }
                }
                /* パイプ接続の待機に使ったイベント ハンドルの後始末であり、動作が変わらないため、戻り値は明示的に破棄する。 */
                (void)CloseHandle(connection.hEvent);
            }
            if (!connected)
            {
                /*
                 * パイプ接続に失敗したワーカーの後始末であり、このワーカーは alive = 0 で以後使わず、
                 * イベントも find_available_worker() が再びリセットするため、この節の戻り値は明示的に破棄する。
                 */
                workers[i].alive = 0;
                (void)ResetEvent(events[i]);
                (void)CloseHandle(workers[i].pipe);
                workers[i].pipe = INVALID_HANDLE_VALUE;
                (void)CloseHandle(workers[i].process);
                workers[i].process = NULL;
                (void)CloseHandle(workers[i].ack_event);
                workers[i].ack_event = NULL;
                (void)fprintf(stderr, "ワーカーのパイプ接続失敗: %d\n", i);
                continue;
            }
        }

        /* ワーカーからの完了通知を受信する監視スレッドを起動する。 */
        cplat_thread *monitor_thread;

        args[i].id = i;
        args[i].workers = workers;
        args[i].events = events;
        args[i].conns_per_worker = conns_per_worker;

        if (cplat_thread_create(&monitor_thread, worker_monitor_thread, (void *)&args[i]) != CPLAT_OK)
        {
            (void)fprintf(stderr, "監視スレッド起動失敗: %d\n", i);
            cplat_free(args);
            cplat_free(exepath);
            cplat_exit(1);
        }

        cplat_thread_detach(monitor_thread);

        (void)printf("[親プロセス] ワーカー %d (PID %lu) 起動完了\n", i, pi.dwProcessId);
    }

    /* args はスレッドが使い続けるため解放しない (プロセス終了時に解放される)。 */
    cplat_free(exepath);
}

/*
 * ============================================================
 *  プラットフォーム フック
 * ============================================================
 */

/* Doxygen コメントは、ヘッダーに記載 */

void platform_init(ClientSessionFn session_fn)
{
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        (void)fprintf(stderr, "WSAStartup 失敗\n"); /* 診断の出力先がほかにないため、戻り値は明示的に破棄する */
        cplat_exit(1);
    }

    g_session_fn = session_fn;
    #if defined(TCPSERVER_PREFORK_TEST)
    prefork_probe_initialize();
    #endif
}

/* Doxygen コメントは、ヘッダーに記載 */

void platform_cleanup(void)
{
    /* プロセスの終了の直前の Winsock の後始末であり、失敗しても動作が変わらないため、戻り値は明示的に破棄する。 */
    (void)WSACleanup();
}

/* Doxygen コメントは、ヘッダーに記載 */

int dispatch_internal_args(int argc, char *argv[])
{
    /* コマンド ライン引数を走査し、内部起動オプション (--child または --worker) を処理する。 */
    for (int i = 1; i < argc - 1; i++)
    {
        if (strcmp(argv[i], "--child") == 0)
        {
            SOCKET sock = (SOCKET)_strtoui64(argv[i + 1], NULL, 10);
            run_as_fork_child(sock);
        }

        if (strcmp(argv[i], "--worker") == 0)
        {
            /* --conns-per-worker が続く場合は、その値を読み取る。 */
            int conns_per_worker = DEFAULT_CONNS_PER_WORKER;

            for (int j = i + 2; j < argc - 1; j++)
            {
                if (strcmp(argv[j], "--conns-per-worker") == 0)
                {
                    {
                        int64_t parsed;

                        if (cplat_parse_int64(&parsed, argv[j + 1], 10) == CPLAT_OK && parsed > 0 &&
                            parsed <= (int64_t)INT_MAX)
                        {
                            conns_per_worker = (int)parsed;
                        }
                    }

                    break;
                }
            }

            worker_loop(argv[i + 1], conns_per_worker);

            return 1;
        }
    }

    return 0;
}

/*
 * ============================================================
 *  サーバー実装
 * ============================================================
 */

/* Doxygen コメントは、ヘッダーに記載 */

void run_fork_server(int port)
{
    SOCKET listen_socket = create_listen_socket(port);
    char *exepath = NULL;
    DWORD path_error;

    /*
     * 稼働状況は利用者が画面で確認する表示であり、診断も出力先がほかにないため、
     * この関数の printf と fprintf の戻り値は明示的に破棄する。
     */
    path_error = get_native_executable_path(&exepath);
    if (path_error != ERROR_SUCCESS)
    {
        (void)fprintf(stderr, "実行ファイルのパスを取得できません (GetLastError=%lu)\n", path_error);
        cplat_exit(1);
    }

    (void)printf("[親プロセス %lu] fork モード、ポート %d で待ち受け開始\n", GetCurrentProcessId(), port);

    /* クライアント接続を受け付けるたびに、子プロセスを生成してソケット ハンドルを引き渡す。 */
    while (1)
    {
        SOCKET client_socket = accept(listen_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET)
        {
            continue;
        }

        (void)printf("[親プロセス] 接続受付、子プロセス生成\n");

        /*
         * 子プロセスへソケット ハンドルを継承させるため、継承可能フラグを設定する。
         * 設定できない場合は子プロセスが接続を使えないため、子プロセスを起動せずに接続を閉じ、次の接続を受け付ける。
         */
        if (!SetHandleInformation((HANDLE)client_socket, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
        {
            (void)fprintf(stderr, "[親プロセス] SetHandleInformation 失敗: %lu\n", GetLastError());
            /* 子へ渡せなかった接続を閉じる後始末であり、失敗しても動作が変わらないため、戻り値は明示的に破棄する。 */
            (void)closesocket(client_socket);
            continue;
        }

        char arguments[TCPSERVER_CHILD_ARGS_UTF8_CAP];
        int format_result = cplat_snprintf(arguments, sizeof(arguments), " --child %llu",
                                           (unsigned long long)client_socket);
        PROCESS_INFORMATION pi = {0};
        DWORD process_error = ERROR_SUCCESS;
        BOOL process_started = FALSE;

        if (format_result == CPLAT_OK)
        {
            process_started = start_child_process(exepath, arguments, TRUE, &pi, &process_error);
        }
        else
        {
            process_error = (format_result == CPLAT_ERR_BUFFER_TOO_SMALL) ? ERROR_INSUFFICIENT_BUFFER
                                                                           : ERROR_INVALID_DATA;
        }

        if (process_started)
        {
            /* 起動した子プロセスのハンドルは使わず、閉じる処理の失敗で動作が変わらないため、この 2 つの戻り値は明示的に破棄する。 */
            (void)CloseHandle(pi.hProcess);
            (void)CloseHandle(pi.hThread);
        }
        else
        {
            (void)fprintf(stderr, "[親プロセス] 子プロセスの起動に失敗しました (GetLastError=%lu)\n", process_error);
        }

        /* 子プロセスの起動の後に親が持つ接続の複製を閉じる処理であり、失敗しても動作が変わらないため、戻り値は明示的に破棄する。 */
        (void)closesocket(client_socket);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void run_prefork_server(int port, int num_workers, int conns_per_worker)
{
    WorkerInfo *workers = (WorkerInfo *)cplat_malloc((size_t)num_workers * sizeof(WorkerInfo));
    HANDLE *events = (HANDLE *)cplat_malloc((size_t)num_workers * sizeof(HANDLE));

    /*
     * 診断の出力先がほかになく、稼働状況は利用者が画面で確認する表示であるため、
     * この関数の fprintf と printf の戻り値は明示的に破棄する。
     */
    if (workers == NULL || events == NULL)
    {
        (void)fprintf(stderr, "malloc 失敗\n");
        cplat_exit(1);
    }

    SOCKET listen_socket = create_listen_socket(port);
    (void)printf("[親プロセス %lu] prefork モード、ワーカー %d 個を起動中 (1 ワーカーあたり最大 %d 接続)...\n",
                 GetCurrentProcessId(), num_workers, conns_per_worker);
    start_prefork_workers(workers, events, num_workers, conns_per_worker);
    (void)printf("[親プロセス] ポート %d で待ち受け開始\n", port);

    /* クライアント接続を受け付け、空きのあるワーカー プロセスへ名前付きパイプ経由でソケットを引き渡す。 */
    while (1)
    {
        SOCKET client_socket = accept(listen_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET)
        {
            continue;
        }

    #if defined(TCPSERVER_PREFORK_TEST)
        prefork_probe_accepted();
    #endif
        int worker_id = find_available_worker(workers, events, num_workers, conns_per_worker);
        if (worker_id < 0)
        {
            close_parent_socket(client_socket, 0);
            prefork_trace("no_worker", 0, client_socket, 0);
            (void)fprintf(stderr, "利用可能なワーカーがありません\n");
            continue;
        }
        WorkerInfo *worker = &workers[worker_id];
        WSAPROTOCOL_INFOW protocol_info;
        int duplicated = WSADuplicateSocketW(client_socket, worker->process_id, &protocol_info) == 0;
        if (!duplicated || !pipe_transfer(worker->pipe, &protocol_info, sizeof(protocol_info), 1))
        {
            EnterCriticalSection(&worker->state_lock);
            worker->pending = 0;
            if (duplicated)
            {
                worker->alive = 0; /* 書きかけの転送があるパイプは再利用しない。 */
            }
            set_event_or_exit(events[worker_id]);
            LeaveCriticalSection(&worker->state_lock);
            if (duplicated)
            {
                /*
                 * 書きかけの転送があるパイプを再利用しないための後始末であり、以後このパイプを使わないため、
                 * 戻り値は明示的に破棄する。
                 */
                (void)DisconnectNamedPipe(worker->pipe);
            }
            close_parent_socket(client_socket, worker->process_id);
            continue;
        }
        /* 受領通知の読み取りは監視スレッドへ委ねる。ワーカー終了も待機を解除する。 */
        HANDLE wait_handles[] = {worker->ack_event, worker->process};
        prefork_trace("ack_wait", worker->process_id, client_socket, 0);
    #if defined(TCPSERVER_PREFORK_TEST)
        prefork_probe_before_wait();
    #endif
        DWORD wait_result = WaitForMultipleObjects((DWORD)(sizeof(wait_handles) / sizeof(wait_handles[0])),
                                                   wait_handles, FALSE, INFINITE);
        EnterCriticalSection(&worker->state_lock);
        int accepted = wait_result == WAIT_OBJECT_0 && worker->alive && worker->ack_ok;
        if (wait_result != WAIT_OBJECT_0)
        {
            worker->alive = 0;
            worker->pending = 0;
            set_event_or_exit(events[worker_id]);
        }
        LeaveCriticalSection(&worker->state_lock);
        close_parent_socket(client_socket, worker->process_id);
        if (!accepted)
        {
            (void)fprintf(stderr, "ワーカー %d へのソケット引き渡しに失敗しました\n", worker_id);
        }
    }
}

#endif /* PLATFORM_WINDOWS */
