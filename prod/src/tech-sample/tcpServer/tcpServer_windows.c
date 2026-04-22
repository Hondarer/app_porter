/**
 *******************************************************************************
 *  @file           tcpServer_windows.c
 *  @brief          TCP サーバーサンプル Windows 実装。
 *  @author         Tetsuo Honda
 *  @date           2026/03/17
 *  @version        1.0.0
 *
 *  Windows 固有のサーバー処理を実装します。
 *  - CreateProcess() を使った接続ごとプロセス生成 (run_fork_server)
 *  - 名前付きパイプを使ったプリフォーク         (run_prefork_server)
 *  - プラットフォームフック                      (platform_init / platform_cleanup /
 *                                                 dispatch_internal_args)
 *
 *  main() / handle_client_session() / parse_args() は tcpServer.c に実装します。
 *  g_session_fn の実体は tcpServer_common.c に定義されます。
 *
 *  内部起動パターン (ユーザーは直接使わない):
 *  - `tcpServer --child <socket_handle>` : fork 版の子プロセス
 *  - `tcpServer --worker <pipe_name>`    : prefork 版のワーカープロセス
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)

#include "tcpServer.h"   /* WIN32_LEAN_AND_MEAN / windows.h / winsock2.h / ws2tcpip.h を内包 */
#include <com_util/crt/path.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>     /* _beginthreadex */

/** 名前付きパイプ名フォーマット。 */
#define TCPSERVER_PIPE_NAME_FMT "\\\\.\\pipe\\tcpserver_worker_%d"

/* ============================================================
 *  内部型定義
 * ============================================================ */

/**
 *******************************************************************************
 *  @brief          ワーカープロセスの情報を管理する構造体。
 *******************************************************************************
 */
typedef struct {
    HANDLE pipe;       /**< ワーカーとの通信用パイプ。 */
    HANDLE process;    /**< ワーカープロセスのハンドル。 */
    int    conn_count; /**< 現在処理中のアクティブ接続数。 */
} WorkerInfo;

/**
 *******************************************************************************
 *  @brief          ワーカー監視スレッドへ渡すコンテキスト。
 *******************************************************************************
 */
typedef struct {
    int          id;              /**< ワーカー ID。 */
    WorkerInfo  *workers;         /**< ワーカー情報配列へのポインタ。 */
    HANDLE      *events;          /**< ワーカーイベント配列へのポインタ。 */
    int          conns_per_worker; /**< 1 ワーカーあたりの同時接続数上限。 */
} WorkerMonitorArg;

/* ============================================================
 *  内部ヘルパー
 * ============================================================ */

/**
 *******************************************************************************
 *  @brief          listen ソケットを作成してバインドし、待ち受けを開始します。
 *  @param[in]      port 待ち受けポート番号。
 *  @return         listen ソケット。
 *
 *  @attention      失敗した場合は exit() で終了します。
 *******************************************************************************
 */
static SOCKET create_listen_socket(int port) {
    struct addrinfo hints = {0}, *result;
    char port_str[8];

    sprintf_s(port_str, sizeof(port_str), "%d", port);

    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags    = AI_PASSIVE;

    if (getaddrinfo(NULL, port_str, &hints, &result) != 0) {
        fprintf(stderr, "getaddrinfo 失敗\n");
        exit(1);
    }

    SOCKET listen_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (listen_socket == INVALID_SOCKET) {
        fprintf(stderr, "socket 失敗\n");
        freeaddrinfo(result);
        exit(1);
    }

    if (bind(listen_socket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        fprintf(stderr, "bind 失敗\n");
        freeaddrinfo(result);
        exit(1);
    }

    freeaddrinfo(result);

    if (listen(listen_socket, 128) == SOCKET_ERROR) {
        fprintf(stderr, "listen 失敗\n");
        exit(1);
    }

    return listen_socket;
}

/**
 *******************************************************************************
 *  @brief          fork モードの子プロセスとして起動された場合のクライアント処理。
 *  @param[in]      client_socket `--child` 引数で受け取ったクライアントソケット。
 *
 *  g_session_fn() で通信メインループを実行し、platform_cleanup() 後に ExitProcess() で終了します。
 *******************************************************************************
 */
__declspec(noreturn) static void run_as_fork_child(SOCKET client_socket) {
    g_session_fn(client_socket);
    platform_cleanup();
    ExitProcess(0);
}

/**
 *******************************************************************************
 *  @brief          prefork モードのワーカープロセスとして起動された場合の処理。
 *  @param[in]      pipe_name        `--worker` 引数で受け取ったパイプ名。
 *  @param[in]      conns_per_worker 同時接続数。1 の場合は逐次処理、2 以上は多重処理。
 *
 *  conns_per_worker == 1 の場合:
 *    パイプからソケットを受け取り g_session_fn() で処理し完了通知を返す従来の逐次処理。
 *
 *  conns_per_worker > 1 の場合:
 *    PeekNamedPipe() でパイプを非ブロッキングチェックしながら、select() で
 *    アクティブな複数ソケットを監視するイベントループ。接続が閉じるたびに親へ
 *    完了通知 (done=1) を 1 バイト送信します。
 *******************************************************************************
 */
static void worker_loop(const char *pipe_name, int conns_per_worker) {
    DWORD bytes_read, bytes_written;

    printf("[ワーカー PID %lu] 起動完了\n", GetCurrentProcessId());

    HANDLE pipe = CreateFileA(
        pipe_name,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL
    );

    if (pipe == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[ワーカー] パイプ接続失敗\n");
        return;
    }

    if (conns_per_worker == 1) {
        /* --- 従来の逐次処理 --- */
        while (1) {
            SOCKET client_socket;
            if (!ReadFile(pipe, &client_socket, sizeof(client_socket), &bytes_read, NULL)) {
                break;
            }
            if (bytes_read != sizeof(client_socket)) {
                break;
            }

            printf("[ワーカー PID %lu] クライアント接続\n", GetCurrentProcessId());
            g_session_fn(client_socket);
            printf("[ワーカー PID %lu] 次のソケット待機\n", GetCurrentProcessId());

            /* 親に処理完了を通知 */
            char done = 1;
            WriteFile(pipe, &done, 1, &bytes_written, NULL);
        }
    } else {
        /* --- PeekNamedPipe + select による多重接続処理 --- */
        SOCKET *active = (SOCKET *)malloc((size_t)conns_per_worker * sizeof(SOCKET));
        if (!active) {
            CloseHandle(pipe);
            return;
        }

        int  active_count = 0;
        char buf[BUFFER_SIZE];

        while (1) {
            /* パイプから新規ソケットを非ブロッキングチェック */
            DWORD avail = 0;
            if (!PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL)) {
                break; /* パイプ切断 */
            }
            if (avail >= sizeof(SOCKET)) {
                SOCKET new_sock;
                if (ReadFile(pipe, &new_sock, sizeof(new_sock), &bytes_read, NULL)
                        && bytes_read == sizeof(new_sock)) {
                    active[active_count++] = new_sock;
                    printf("[ワーカー PID %lu] 新規接続 (計 %d 接続)\n",
                           GetCurrentProcessId(), active_count);
                }
            }

            if (active_count == 0) {
                /* アクティブな接続がない場合は少し待ってパイプを再チェック */
                Sleep(10);
                continue;
            }

            /* select で全アクティブソケットを監視 (10ms タイムアウト) */
            fd_set read_fds;
            FD_ZERO(&read_fds);
            for (int i = 0; i < active_count; i++) {
                FD_SET(active[i], &read_fds);
            }
            struct timeval tv = {0, 10000}; /* 10ms */
            /* Windows では select の第 1 引数は無視される */
            select(0, &read_fds, NULL, NULL, &tv);

            /* 末尾から走査して配列詰めを安全に行う */
            for (int i = active_count - 1; i >= 0; i--) {
                if (!FD_ISSET(active[i], &read_fds)) {
                    continue;
                }

                int n = recv(active[i], buf, BUFFER_SIZE, 0);
                if (n <= 0) {
                    closesocket(active[i]);
                    active[i] = active[--active_count]; /* 末尾と入れ替えて削除 */
                    printf("[ワーカー PID %lu] 接続終了 (残 %d 接続)\n",
                           GetCurrentProcessId(), active_count);
                    /* 親に接続 1 本分の完了を通知 */
                    char done = 1;
                    WriteFile(pipe, &done, 1, &bytes_written, NULL);
                } else {
                    send(active[i], buf, n, 0);
                }
            }
        }

        free(active);
    }

    CloseHandle(pipe);
    printf("[ワーカー PID %lu] 終了\n", GetCurrentProcessId());
}

/**
 *******************************************************************************
 *  @brief          ワーカーの完了を監視するスレッド。
 *  @param[in]      arg WorkerMonitorArg へのポインタ。
 *  @return         スレッド終了時は 0 を返します。
 *
 *  ワーカーからの完了通知を受け取ると、ワーカーの状態を「空き」に更新し
 *  イベントをシグナル状態にします。
 *******************************************************************************
 */
static unsigned __stdcall worker_monitor_thread(void *arg) {
    WorkerMonitorArg *ctx              = (WorkerMonitorArg *)arg;
    int               id              = ctx->id;
    WorkerInfo       *workers         = ctx->workers;
    HANDLE           *events          = ctx->events;
    int               conns_per_worker = ctx->conns_per_worker;
    char              done;
    DWORD             bytes_read;

    while (1) {
        if (ReadFile(workers[id].pipe, &done, 1, &bytes_read, NULL)) {
            /* 接続 1 本分の完了通知 → conn_count を減らし、空きができたらイベントをシグナル */
            workers[id].conn_count--;
            if (workers[id].conn_count < conns_per_worker) {
                SetEvent(events[id]);
            }
        } else {
            break;
        }
    }
    return 0;
}

/**
 *******************************************************************************
 *  @brief          接続を受け付けられるワーカーのインデックスを返します。
 *  @param[in]      workers          ワーカー情報配列。
 *  @param[in]      events           ワーカーイベント配列。
 *  @param[in]      n                ワーカー数。
 *  @param[in]      conns_per_worker 1 ワーカーあたりの同時接続数上限。
 *  @return         空きワーカーのインデックス (0 〜 n - 1)。
 *
 *  conn_count < conns_per_worker のワーカーを探します。
 *  全ワーカーが満杯の場合は WaitForMultipleObjects でいずれかに空きが生じるまで待機します。
 *******************************************************************************
 */
static int find_available_worker(WorkerInfo *workers, HANDLE *events, int n,
                                 int conns_per_worker) {
    while (1) {
        for (int i = 0; i < n; i++) {
            if (workers[i].conn_count < conns_per_worker) {
                return i;
            }
        }
        WaitForMultipleObjects((DWORD)n, events, FALSE, INFINITE);
    }
}

/**
 *******************************************************************************
 *  @brief          n 個のワーカープロセスを起動します。
 *  @param[in]      workers ワーカー情報配列。
 *  @param[in]      events  ワーカーイベント配列。
 *  @param[in]      n       ワーカー数。
 *
 *  各ワーカーに名前付きパイプと監視スレッドを作成し、
 *  自分自身を `--worker <pipe_name>` 引数付きで再起動します。
 *
 *  @attention      パイプ作成またはプロセス起動に失敗した場合は exit() で終了します。
 *******************************************************************************
 */
static void start_prefork_workers(WorkerInfo *workers, HANDLE *events, int n,
                                  int conns_per_worker) {
    char pipe_name[64];
    char cmdline[PLATFORM_PATH_MAX + 128];
    char exepath[PLATFORM_PATH_MAX];

    GetModuleFileNameA(NULL, exepath, PLATFORM_PATH_MAX);

    /* 監視スレッド引数はプロセス終了まで有効である必要があるため heap 確保 */
    WorkerMonitorArg *args = (WorkerMonitorArg *)malloc((size_t)n * sizeof(WorkerMonitorArg));
    if (!args) {
        fprintf(stderr, "malloc 失敗\n");
        exit(1);
    }

    for (int i = 0; i < n; i++) {
        sprintf_s(pipe_name, sizeof(pipe_name), TCPSERVER_PIPE_NAME_FMT, i);

        workers[i].pipe = CreateNamedPipeA(
            pipe_name,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 4096, 4096, 0, NULL
        );

        if (workers[i].pipe == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "パイプ作成失敗: %d\n", i);
            free(args);
            exit(1);
        }

        /* イベント作成 (初期状態: 空き) */
        events[i] = CreateEvent(NULL, FALSE, TRUE, NULL);

        /* ワーカープロセス起動 (--conns-per-worker を渡す) */
        sprintf_s(cmdline, sizeof(cmdline), "\"%s\" --worker %s --conns-per-worker %d",
                  exepath, pipe_name, conns_per_worker);

        STARTUPINFOA        si = {sizeof(si)};
        PROCESS_INFORMATION pi;

        if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
            fprintf(stderr, "ワーカー起動失敗: %d\n", i);
            free(args);
            exit(1);
        }

        workers[i].process    = pi.hProcess;
        workers[i].conn_count = 0;
        CloseHandle(pi.hThread);

        /* ワーカーがパイプに接続するのを待つ */
        ConnectNamedPipe(workers[i].pipe, NULL);

        /* 監視スレッド起動 */
        args[i].id               = i;
        args[i].workers          = workers;
        args[i].events           = events;
        args[i].conns_per_worker = conns_per_worker;
        _beginthreadex(NULL, 0, worker_monitor_thread, (void *)&args[i], 0, NULL);

        printf("[親プロセス] ワーカー %d (PID %lu) 起動完了\n", i, pi.dwProcessId);
    }

    /* args はスレッドが使い続けるため解放しない (プロセス終了時に解放される) */
}

/* ============================================================
 *  プラットフォームフック
 * ============================================================ */

/* doxygen コメントは、ヘッダに記載 */
void platform_init(ClientSessionFn session_fn) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup 失敗\n");
        exit(1);
    }
    g_session_fn = session_fn;
}

/* doxygen コメントは、ヘッダに記載 */
void platform_cleanup(void) {
    WSACleanup();
}

/* doxygen コメントは、ヘッダに記載 */
int dispatch_internal_args(int argc, char *argv[]) {
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--child") == 0) {
            SOCKET sock = (SOCKET)_strtoui64(argv[i + 1], NULL, 10);
            run_as_fork_child(sock);
        }
        if (strcmp(argv[i], "--worker") == 0) {
            /* --conns-per-worker が続く場合は読み取る */
            int conns_per_worker = DEFAULT_CONNS_PER_WORKER;
            for (int j = i + 2; j < argc - 1; j++) {
                if (strcmp(argv[j], "--conns-per-worker") == 0) {
                    conns_per_worker = atoi(argv[j + 1]);
                    break;
                }
            }
            worker_loop(argv[i + 1], conns_per_worker);
            return 1;
        }
    }
    return 0;
}

/* ============================================================
 *  サーバー実装
 * ============================================================ */

/* doxygen コメントは、ヘッダに記載 */
void run_fork_server(int port) {
    SOCKET listen_socket = create_listen_socket(port);
    char   exepath[PLATFORM_PATH_MAX];
    GetModuleFileNameA(NULL, exepath, PLATFORM_PATH_MAX);

    printf("[親プロセス %lu] fork モード、ポート %d で待ち受け開始\n",
           GetCurrentProcessId(), port);

    while (1) {
        SOCKET client_socket = accept(listen_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET) {
            continue;
        }

        printf("[親プロセス] 接続受付、子プロセス生成\n");

        /* ソケットハンドルを継承可能に設定 */
        SetHandleInformation((HANDLE)client_socket, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

        char cmdline[PLATFORM_PATH_MAX + 64];
        sprintf_s(cmdline, sizeof(cmdline), "\"%s\" --child %llu",
                  exepath, (unsigned long long)client_socket);

        STARTUPINFOA        si = {sizeof(si)};
        PROCESS_INFORMATION pi;

        if (CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }

        closesocket(client_socket);
    }
}

/* doxygen コメントは、ヘッダに記載 */
void run_prefork_server(int port, int num_workers, int conns_per_worker) {
    WorkerInfo *workers = (WorkerInfo *)malloc((size_t)num_workers * sizeof(WorkerInfo));
    HANDLE     *events  = (HANDLE *)malloc((size_t)num_workers * sizeof(HANDLE));

    if (!workers || !events) {
        fprintf(stderr, "malloc 失敗\n");
        exit(1);
    }

    SOCKET listen_socket = create_listen_socket(port);
    printf("[親プロセス %lu] prefork モード、ワーカー %d 個を起動中 (1 ワーカーあたり最大 %d 接続)...\n",
           GetCurrentProcessId(), num_workers, conns_per_worker);
    start_prefork_workers(workers, events, num_workers, conns_per_worker);
    printf("[親プロセス] ポート %d で待ち受け開始\n", port);

    while (1) {
        SOCKET client_socket = accept(listen_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET) {
            continue;
        }

        /* ソケットを継承可能に */
        SetHandleInformation((HANDLE)client_socket, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

        int worker_id = find_available_worker(workers, events, num_workers, conns_per_worker);
        workers[worker_id].conn_count++;

        /* 容量に達した場合はイベントを非シグナルにして次の割り当てを防ぐ */
        if (workers[worker_id].conn_count >= conns_per_worker) {
            ResetEvent(events[worker_id]);
        }

        printf("[親プロセス] ワーカー %d に接続を割り当て (接続数: %d/%d)\n",
               worker_id, workers[worker_id].conn_count, conns_per_worker);

        DWORD bytes_written;
        WriteFile(workers[worker_id].pipe, &client_socket, sizeof(client_socket),
                  &bytes_written, NULL);

        /* ソケットはワーカーが処理完了後に closesocket する */
    }

    /* 到達しないが形式上記述 */
    free(workers);
    free(events);
}

#endif /* PLATFORM_WINDOWS */
