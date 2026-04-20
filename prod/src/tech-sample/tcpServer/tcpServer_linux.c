/**
 *******************************************************************************
 *  @file           tcpServer_linux.c
 *  @brief          TCP サーバーサンプル Linux 実装。
 *  @author         Tetsuo Honda
 *  @date           2026/03/17
 *  @version        1.0.0
 *
 *  Linux 固有のサーバー処理を実装します。
 *  - fork()  を使った接続ごとプロセス生成 (run_fork_server)
 *  - fork()  を使ったプリフォーク       (run_prefork_server)
 *  - プラットフォームフック              (platform_init / platform_cleanup /
 *                                        dispatch_internal_args)
 *
 *  main() / handle_client_session() / parse_args() は tcpServer.c に実装します。
 *  g_session_fn の実体は tcpServer_common.c に定義されます。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>

#if defined(PLATFORM_LINUX)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <signal.h>
#include <errno.h>

/** epoll_wait で一度に取得する最大イベント数。 */
#define MAX_EPOLL_EVENTS 64

#include "tcpServer.h"

static volatile sig_atomic_t running = 1;

/* ============================================================
 *  シグナルハンドラ
 * ============================================================ */

/**
 *******************************************************************************
 *  @brief          SIGCHLD シグナルハンドラ。
 *  @param[in]      sig シグナル番号 (未使用)。
 *
 *  waitpid() でゾンビプロセスを回収します。
 *******************************************************************************
 */
static void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

/**
 *******************************************************************************
 *  @brief          SIGINT / SIGTERM シグナルハンドラ。
 *  @param[in]      sig シグナル番号 (未使用)。
 *
 *  running フラグを 0 にセットして prefork のメインループを終了させます。
 *******************************************************************************
 */
static void shutdown_handler(int sig) {
    (void)sig;
    running = 0;
}

/* ============================================================
 *  内部ヘルパー
 * ============================================================ */

/**
 *******************************************************************************
 *  @brief          listen ソケットを作成してバインドし、待ち受けを開始します。
 *  @param[in]      port 待ち受けポート番号。
 *  @return         listen ソケットのファイルディスクリプタ。
 *
 *  @attention      失敗した場合は exit() で終了します。
 *******************************************************************************
 */
static int create_listen_socket(int port) {
    int server_fd;
    struct sockaddr_in addr;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(1);
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(server_fd, 128) < 0) {
        perror("listen");
        exit(1);
    }

    return server_fd;
}

/**
 *******************************************************************************
 *  @brief          ワーカープロセスのメインループ (prefork モード用)。
 *  @param[in]      server_fd        サーバーソケットのファイルディスクリプタ。
 *  @param[in]      worker_id        ワーカーの識別番号。
 *  @param[in]      conns_per_worker 同時接続数。1 の場合は逐次処理、2 以上は epoll 多重処理。
 *
 *  conns_per_worker == 1 の場合:
 *    accept() → g_session_fn() を繰り返す従来の逐次処理。
 *
 *  conns_per_worker > 1 の場合:
 *    epoll を使ったイベントループで最大 conns_per_worker 本のコネクションを
 *    シングルスレッドで同時処理します。容量に達すると server_fd を epoll から
 *    除去して新規 accept を止め、空きが生じると再登録して受け付けを再開します。
 *******************************************************************************
 */
static void worker_loop(int server_fd, int worker_id, int conns_per_worker) {
    printf("[ワーカー %d, PID %lu] 起動完了、接続待機\n", worker_id, (unsigned long)get_pid());

    if (conns_per_worker == 1) {
        /* --- 従来の逐次処理 --- */
        struct sockaddr_in client_addr;
        socklen_t          addr_len;
        int                client_fd;

        while (running) {
            addr_len  = sizeof(client_addr);
            client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);

            if (client_fd < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("accept");
                continue;
            }

            printf("[ワーカー %d] クライアント接続\n", worker_id);
            g_session_fn(client_fd);
            printf("[ワーカー %d] 次の接続待機\n", worker_id);
        }
    } else {
        /* --- epoll による多重接続処理 --- */
        struct epoll_event ev;
        struct epoll_event events[MAX_EPOLL_EVENTS];
        char               buf[BUFFER_SIZE];
        int                active_count = 0;
        int                accepting    = 1; /* server_fd が epoll に登録済みか */

        int epoll_fd = epoll_create1(0);
        if (epoll_fd < 0) {
            perror("epoll_create1");
            exit(1);
        }

        ev.events  = EPOLLIN;
        ev.data.fd = server_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
            perror("epoll_ctl add server_fd");
            exit(1);
        }

        while (running) {
            int nfds = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);
            if (nfds < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("epoll_wait");
                break;
            }

            for (int i = 0; i < nfds; i++) {
                int fd = events[i].data.fd;

                if (fd == server_fd) {
                    /* 新規接続 */
                    int client_fd = accept(server_fd, NULL, NULL);
                    if (client_fd < 0) {
                        if (errno != EINTR) {
                            perror("accept");
                        }
                        continue;
                    }

                    ev.events  = EPOLLIN;
                    ev.data.fd = client_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
                        perror("epoll_ctl add client_fd");
                        close(client_fd);
                        continue;
                    }

                    active_count++;
                    printf("[ワーカー %d] 新規接続 (計 %d 接続)\n", worker_id, active_count);

                    /* 容量到達 → server_fd を epoll から除去して新規 accept を止める */
                    if (active_count >= conns_per_worker) {
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, server_fd, NULL);
                        accepting = 0;
                    }

                } else {
                    /* 既存接続のデータ到着または切断 */
                    ssize_t n = client_recv(fd, buf, sizeof(buf));
                    if (n <= 0) {
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                        client_close(fd);
                        active_count--;
                        printf("[ワーカー %d] 接続終了 (残 %d 接続)\n", worker_id, active_count);

                        /* 空きが生じた → server_fd を再登録して accept を再開 */
                        if (!accepting && active_count < conns_per_worker) {
                            ev.events  = EPOLLIN;
                            ev.data.fd = server_fd;
                            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == 0) {
                                accepting = 1;
                            }
                        }
                    } else {
                        client_send(fd, buf, (size_t)n);
                    }
                }
            }
        }

        close(epoll_fd);
    }

    printf("[ワーカー %d] 終了\n", worker_id);
    exit(0);
}

/* ============================================================
 *  プラットフォームフック
 * ============================================================ */

/* doxygen コメントは、ヘッダに記載 */
void platform_init(ClientSessionFn session_fn) {
    g_session_fn = session_fn;
}

/* doxygen コメントは、ヘッダに記載 */
void platform_cleanup(void) {
    /* Linux では後処理不要 */
}

/* doxygen コメントは、ヘッダに記載 */
int dispatch_internal_args(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return 0;
}

/* ============================================================
 *  サーバー実装
 * ============================================================ */

/* doxygen コメントは、ヘッダに記載 */
void run_fork_server(int port) {
    int                server_fd, client_fd;
    struct sockaddr_in addr;
    socklen_t          addr_len = sizeof(addr);
    struct sigaction   sa;

    /* SIGCHLD ハンドラ設定 (ゾンビプロセス回避) */
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);

    server_fd = create_listen_socket(port);
    printf("[親プロセス %lu] fork モード、ポート %d で待ち受け開始\n", (unsigned long)get_pid(), port);

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&addr, &addr_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        printf("[親プロセス] 接続受付、子プロセス生成\n");

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(client_fd);
        } else if (pid == 0) {
            /* 子プロセス */
            close(server_fd);
            g_session_fn(client_fd);
            exit(0);
        } else {
            /* 親プロセス */
            close(client_fd);
        }
    }
}

/* doxygen コメントは、ヘッダに記載 */
void run_prefork_server(int port, int num_workers, int conns_per_worker) {
    int              server_fd;
    struct sigaction sa;

    /* SIGINT/SIGTERM ハンドラ設定 */
    sa.sa_handler = shutdown_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* SIGCHLD ハンドラ設定 */
    sa.sa_handler = sigchld_handler;
    sa.sa_flags   = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);

    server_fd = create_listen_socket(port);
    printf("[親プロセス %lu] prefork モード、ポート %d で待ち受け開始\n", (unsigned long)get_pid(), port);
    printf("[親プロセス] %d 個のワーカープロセスを起動 (1 ワーカーあたり最大 %d 接続)\n",
           num_workers, conns_per_worker);

    pid_t *worker_pids = malloc((size_t)num_workers * sizeof(pid_t));
    if (!worker_pids) {
        perror("malloc");
        exit(1);
    }

    for (int i = 0; i < num_workers; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            free(worker_pids);
            exit(1);
        } else if (pid == 0) {
            free(worker_pids);
            worker_loop(server_fd, i, conns_per_worker);
            /* ここには到達しない */
        }
        worker_pids[i] = pid;
    }

    printf("[親プロセス] Ctrl+C で終了\n");

    while (running) {
        pause();
    }

    printf("\n[親プロセス] 全ワーカーを終了させます\n");
    for (int i = 0; i < num_workers; i++) {
        if (worker_pids[i] > 0) {
            kill(worker_pids[i], SIGTERM);
        }
    }

    for (int i = 0; i < num_workers; i++) {
        if (worker_pids[i] > 0) {
            waitpid(worker_pids[i], NULL, 0);
        }
    }

    free(worker_pids);
    close(server_fd);
    printf("[親プロセス] 終了\n");
}

#elif defined(PLATFORM_WINDOWS) && defined(COMPILER_MSVC)
    #pragma warning(disable : 4206)
#endif
