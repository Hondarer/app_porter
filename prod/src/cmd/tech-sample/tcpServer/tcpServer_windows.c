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
    #include <cplat/sync/sync.h>
    #include <cplat/win32/win32.h>

    #include <stdio.h>
    #include <cplat/runtime/shutdown.h>
    #include <limits.h>
    #include <stdlib.h>
    #include <string.h>

    /** 名前付きパイプ名フォーマット。 */
    #define TCPSERVER_PIPE_NAME_FMT "\\\\.\\pipe\\tcpserver_worker_%d"

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
        CloseHandle(operation.hEvent);
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
    result =
        WSASocketW(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, &protocol_info, 0, WSA_FLAG_OVERLAPPED);
    if (!worker_reply(pipe, WORKER_ACK, result != INVALID_SOCKET))
    {
        if (result != INVALID_SOCKET)
        {
            closesocket(result);
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
    char port_str[8];

    sprintf_s(port_str, sizeof(port_str), "%d", port);

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, port_str, &hints, &result) != 0)
    {
        fprintf(stderr, "getaddrinfo 失敗\n");
        cplat_exit(1);
    }

    SOCKET listen_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (listen_socket == INVALID_SOCKET)
    {
        fprintf(stderr, "socket 失敗\n");
        freeaddrinfo(result);
        cplat_exit(1);
    }

    if (bind(listen_socket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR)
    {
        fprintf(stderr, "bind 失敗\n");
        freeaddrinfo(result);
        cplat_exit(1);
    }

    freeaddrinfo(result);

    if (listen(listen_socket, 128) == SOCKET_ERROR)
    {
        fprintf(stderr, "listen 失敗\n");
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

    printf("[ワーカー PID %lu] 起動完了\n", GetCurrentProcessId());

    HANDLE pipe =
        CreateFileU(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

    if (pipe == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "[ワーカー] パイプ接続失敗\n");
        return;
    }

    if (conns_per_worker == 1)
    {
        /* --- 従来の逐次処理 --- */
        while (1)
        {
            SOCKET client_socket = receive_socket(pipe);
            if (client_socket == INVALID_SOCKET)
            {
                break;
            }

            printf("[ワーカー PID %lu] クライアント接続\n", GetCurrentProcessId());
            g_session_fn(client_socket);
            printf("[ワーカー PID %lu] 次のソケット待機\n", GetCurrentProcessId());

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
            CloseHandle(pipe);
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
                /* アクティブな接続がない場合は、少し待ってからパイプを再確認する。 */
                cplat_sleep_ms(10);
                continue;
            }

            /* select() で全アクティブ ソケットを 10 [ms] のタイムアウト付きで監視する。 */
            fd_set read_fds;
            FD_ZERO(&read_fds);

            for (int i = 0; i < active_count; i++)
            {
                CPLAT_FD_SET(active[i], &read_fds);
            }

            struct timeval tv = {0, 10000}; /* 10 [ms] */

            /*
             * Windows Sockets において select() の第 1 引数 nfds は互換性のためにのみ存在し、無視される。
             * see: https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-select
             */
            select(0, &read_fds, NULL, NULL, &tv);

            /* 末尾から走査して配列詰めを安全に行う。 */
            for (int i = active_count - 1; i >= 0; i--)
            {
                if (!FD_ISSET(active[i], &read_fds))
                {
                    continue;
                }

                int n = recv(active[i], buf, BUFFER_SIZE, 0);
                if (n <= 0)
                {
                    closesocket(active[i]);
                    active[i] = active[--active_count]; /* 末尾と入れ替えて削除 */
                    printf("[ワーカー PID %lu] 接続終了 (残 %d 接続)\n", GetCurrentProcessId(), active_count);

                    /* 親プロセスへ接続 1 本分の処理完了を通知する。 */
                    if (!worker_reply(pipe, WORKER_DONE, 1))
                    {
                        for (int j = 0; j < active_count; ++j)
                        {
                            closesocket(active[j]);
                        }
                        cplat_free(active);
                        CloseHandle(pipe);
                        return;
                    }
                }
                else
                {
                    send(active[i], buf, n, 0);
                }
            }
        }

        for (int i = 0; i < active_count; ++i)
        {
            closesocket(active[i]);
        }
        cplat_free(active);
    }

    CloseHandle(pipe);
    printf("[ワーカー PID %lu] 終了\n", GetCurrentProcessId());
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
            SetEvent(worker->ack_event);
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
        if (worker->alive && worker->conn_count + worker->pending < ctx->conns_per_worker)
        {
            SetEvent(ctx->events[ctx->id]);
        }
        else
        {
            ResetEvent(ctx->events[ctx->id]);
        }
        LeaveCriticalSection(&worker->state_lock);
    }
    DisconnectNamedPipe(worker->pipe);
    EnterCriticalSection(&worker->state_lock);
    worker->alive = 0;
    worker->pending = 0;
    worker->ack_ok = 0;
    SetEvent(worker->ack_event);
    SetEvent(ctx->events[ctx->id]); /* 空き待機側に終了を通知する。 */
    LeaveCriticalSection(&worker->state_lock);
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
                    ResetEvent(worker->ack_event);
                    if (worker->conn_count + worker->pending == conns_per_worker)
                    {
                        ResetEvent(events[i]);
                    }
                    LeaveCriticalSection(&worker->state_lock);
                    return i;
                }
            }
            else
            {
                ResetEvent(events[i]);
            }
            LeaveCriticalSection(&worker->state_lock);
        }
        if (!any_alive || WaitForMultipleObjects((DWORD)n, events, FALSE, INFINITE) == WAIT_FAILED)
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
 *
 *  各ワーカーに名前付きパイプと監視スレッドを作成し、
 *  自分自身を `--worker <pipe_name>` 引数付きで再起動します。
 *
 *  @attention      パイプ作成またはプロセス起動に失敗した場合は cplat_exit() で終了します。
 */
static void start_prefork_workers(WorkerInfo *workers, HANDLE *events, int n, int conns_per_worker)
{
    char pipe_name[64];
    char cmdline[PLATFORM_PATH_MAX + 128];
    char exepath[PLATFORM_PATH_MAX];

    GetModuleFileNameU(NULL, exepath, PLATFORM_PATH_MAX);

    /* 監視スレッドの引数はプロセス終了まで有効である必要があるため、ヒープに確保する。 */
    WorkerMonitorArg *args = (WorkerMonitorArg *)cplat_malloc((size_t)n * sizeof(WorkerMonitorArg));
    if (args == NULL)
    {
        fprintf(stderr, "malloc 失敗\n");
        cplat_exit(1);
    }

    /* 各ワーカー向けの名前付きパイプと監視スレッドを作成し、ワーカー プロセスを起動する。 */
    for (int i = 0; i < n; i++)
    {
        sprintf_s(pipe_name, sizeof(pipe_name), TCPSERVER_PIPE_NAME_FMT, i);

        workers[i].pipe = CreateNamedPipeU(pipe_name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                           PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, NULL);

        if (workers[i].pipe == INVALID_HANDLE_VALUE)
        {
            fprintf(stderr, "パイプ作成失敗: %d\n", i);
            cplat_free(args);
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
            cplat_exit(1);
        }

        /* 1 ワーカーあたりの同時接続数上限を指定してワーカー プロセスを起動する。 */
        sprintf_s(cmdline, sizeof(cmdline), "\"%s\" --worker %s --conns-per-worker %d", exepath, pipe_name,
                  conns_per_worker);

        STARTUPINFOW si = {sizeof(si)};
        PROCESS_INFORMATION pi;

        if (!CreateProcessU(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        {
            fprintf(stderr, "ワーカー起動失敗: %d\n", i);
            cplat_free(args);
            cplat_exit(1);
        }

        workers[i].process = pi.hProcess;
        workers[i].process_id = pi.dwProcessId;
        workers[i].conn_count = 0;
        CloseHandle(pi.hThread);

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
                        if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0)
                        {
                            connected = GetOverlappedResult(workers[i].pipe, &connection, &ignored, FALSE) != 0;
                        }
                        else
                        {
                            CancelIoEx(workers[i].pipe, &connection);
                            (void)GetOverlappedResult(workers[i].pipe, &connection, &ignored, TRUE);
                        }
                    }
                }
                CloseHandle(connection.hEvent);
            }
            if (!connected)
            {
                workers[i].alive = 0;
                ResetEvent(events[i]);
                CloseHandle(workers[i].pipe);
                workers[i].pipe = INVALID_HANDLE_VALUE;
                CloseHandle(workers[i].process);
                workers[i].process = NULL;
                CloseHandle(workers[i].ack_event);
                workers[i].ack_event = NULL;
                fprintf(stderr, "ワーカーのパイプ接続失敗: %d\n", i);
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
            fprintf(stderr, "監視スレッド起動失敗: %d\n", i);
            cplat_free(args);
            cplat_exit(1);
        }

        cplat_thread_detach(monitor_thread);

        printf("[親プロセス] ワーカー %d (PID %lu) 起動完了\n", i, pi.dwProcessId);
    }

    /* args はスレッドが使い続けるため解放しない (プロセス終了時に解放される)。 */
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
        fprintf(stderr, "WSAStartup 失敗\n");
        cplat_exit(1);
    }

    g_session_fn = session_fn;
}

/* Doxygen コメントは、ヘッダーに記載 */

void platform_cleanup(void)
{
    WSACleanup();
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
    char exepath[PLATFORM_PATH_MAX];
    GetModuleFileNameU(NULL, exepath, PLATFORM_PATH_MAX);

    printf("[親プロセス %lu] fork モード、ポート %d で待ち受け開始\n", GetCurrentProcessId(), port);

    /* クライアント接続を受け付けるたびに、子プロセスを生成してソケット ハンドルを引き渡す。 */
    while (1)
    {
        SOCKET client_socket = accept(listen_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET)
        {
            continue;
        }

        printf("[親プロセス] 接続受付、子プロセス生成\n");

        /* 子プロセスへソケット ハンドルを継承させるため、継承可能フラグを設定する。 */
        SetHandleInformation((HANDLE)client_socket, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

        char cmdline[PLATFORM_PATH_MAX + 64];
        sprintf_s(cmdline, sizeof(cmdline), "\"%s\" --child %llu", exepath, (unsigned long long)client_socket);

        STARTUPINFOW si = {sizeof(si)};
        PROCESS_INFORMATION pi;

        if (CreateProcessU(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }

        closesocket(client_socket);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void run_prefork_server(int port, int num_workers, int conns_per_worker)
{
    WorkerInfo *workers = (WorkerInfo *)cplat_malloc((size_t)num_workers * sizeof(WorkerInfo));
    HANDLE *events = (HANDLE *)cplat_malloc((size_t)num_workers * sizeof(HANDLE));

    if (workers == NULL || events == NULL)
    {
        fprintf(stderr, "malloc 失敗\n");
        cplat_exit(1);
    }

    SOCKET listen_socket = create_listen_socket(port);
    printf("[親プロセス %lu] prefork モード、ワーカー %d 個を起動中 (1 ワーカーあたり最大 %d 接続)...\n",
           GetCurrentProcessId(), num_workers, conns_per_worker);
    start_prefork_workers(workers, events, num_workers, conns_per_worker);
    printf("[親プロセス] ポート %d で待ち受け開始\n", port);

    /* クライアント接続を受け付け、空きのあるワーカー プロセスへ名前付きパイプ経由でソケットを引き渡す。 */
    while (1)
    {
        SOCKET client_socket = accept(listen_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET)
        {
            continue;
        }

        int worker_id = find_available_worker(workers, events, num_workers, conns_per_worker);
        if (worker_id < 0)
        {
            closesocket(client_socket);
            fprintf(stderr, "利用可能なワーカーがありません\n");
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
            SetEvent(events[worker_id]);
            LeaveCriticalSection(&worker->state_lock);
            if (duplicated)
            {
                DisconnectNamedPipe(worker->pipe);
            }
            closesocket(client_socket);
            continue;
        }
        /* 受領通知の読み取りは監視スレッドへ委ねる。ワーカー終了も待機を解除する。 */
        HANDLE wait_handles[] = {worker->ack_event, worker->process};
        DWORD wait_result = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
        EnterCriticalSection(&worker->state_lock);
        int accepted = wait_result == WAIT_OBJECT_0 && worker->alive && worker->ack_ok;
        if (wait_result != WAIT_OBJECT_0)
        {
            worker->alive = 0;
            worker->pending = 0;
            SetEvent(events[worker_id]);
        }
        LeaveCriticalSection(&worker->state_lock);
        closesocket(client_socket); /* 子が取得済み、または引き渡し失敗の親側参照を閉じる。 */
        if (!accepted)
        {
            fprintf(stderr, "ワーカー %d へのソケット引き渡しに失敗しました\n", worker_id);
        }
    }

    /* 到達しないが形式上記述する。 */
    cplat_free(workers);
    cplat_free(events);
}

#endif /* PLATFORM_WINDOWS */
