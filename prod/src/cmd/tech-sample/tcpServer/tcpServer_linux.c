/**
 *******************************************************************************
 *  @file           tcpServer_linux.c
 *  @brief          TCP サーバー サンプルの Linux 向け処理を実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/17
 *  @version        1.0.0
 *
 *  Linux 固有のサーバー処理を実装します。
 *  - fork()  を使った接続ごとプロセス生成 (run_fork_server)
 *  - fork()  を使ったプリフォーク         (run_prefork_server)
 *  - プラットフォーム フック              (platform_init / platform_cleanup /
 *                                          dispatch_internal_args)
 *
 *  main() / handle_client_session() は tcpServer.c に実装します。
 *  g_session_fn の実体は tcpServer_common.c に定義されます。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/base/platform.h>
#include <cplat/crt/stdlib.h>

#if defined(PLATFORM_LINUX)

    #include <cplat/crt/unistd.h>
    #include <cplat/runtime/shutdown.h>

    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <sys/socket.h>
    #include <sys/epoll.h>
    #include <sys/wait.h>
    #include <netinet/in.h>
    #include <signal.h>
    #include <errno.h>
    #include <limits.h>
    #include <assert.h>

    /** epoll_wait で一度に取得する最大イベント数。 */
    #define MAX_EPOLL_EVENTS 64U

static_assert((MAX_EPOLL_EVENTS >= 1U) && (MAX_EPOLL_EVENTS <= (unsigned int)INT_MAX),
              "tcpServer: MAX_EPOLL_EVENTS must fit in int");

    #include "tcpServer.h"

/* 終了シグナルを受けると shutdown_handler() が 0 にする */
static volatile sig_atomic_t running = 1;

/*
 * ============================================================
 *  シグナル ハンドラー
 * ============================================================
 */

/**
 *  @brief          SIGCHLD シグナル ハンドラー。
 *  @param[in]      sig シグナル番号 (未使用)。
 *
 *  waitpid() でゾンビ プロセスを回収します。
 */
static void sigchld_handler(int sig)
{
    (void)sig;

    /*
     * 終了したすべての子プロセスをノンブロッキングで回収し、ゾンビ プロセスの残留を防ぐ。
     * see: https://man7.org/linux/man-pages/man2/waitpid.2.html
     */
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
    }
}

/**
 *  @brief          SIGINT / SIGTERM シグナル ハンドラー。
 *  @param[in]      sig シグナル番号 (未使用)。
 *
 *  running フラグを 0 にセットして prefork のメイン ループを終了させます。
 */
static void shutdown_handler(int sig)
{
    (void)sig;
    running = 0;
}

/*
 * ============================================================
 *  内部ヘルパー
 * ============================================================
 */

/**
 *  @brief          listen ソケットを作成してバインドし、待ち受けを開始します。
 *  @param[in]      port 待ち受けポート番号。
 *  @return         listen ソケットのファイル記述子。
 *
 *  @attention      失敗した場合は cplat_exit() で終了します。
 */
static int create_listen_socket(int port)
{
    int server_fd;
    struct sockaddr_in addr = {0};
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket");
        cplat_exit(1);
    }

    /* SO_REUSEADDR は再起動時の補助であり、失敗の影響は直後の bind の検査で分かるため、戻り値は明示的に破棄する。 */
    (void)setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        cplat_exit(1);
    }

    /*
     * backlog は、接続を待たせるキューの長さである。prefork モードでは、全ワーカーが処理中のときの接続が、ここで待つ。
     * /proc/sys/net/core/somaxconn を超える分は切り詰められる。
     * see: https://man7.org/linux/man-pages/man2/listen.2.html
     */
    if (listen(server_fd, LISTEN_BACKLOG) < 0)
    {
        perror("listen");
        cplat_exit(1);
    }

    return server_fd;
}

/**
 *  @brief          ワーカー プロセスのメイン ループ (prefork モード用)。
 *  @param[in]      server_fd        サーバー ソケットのファイル記述子。
 *  @param[in]      worker_id        ワーカーの識別番号。
 *  @param[in]      conns_per_worker 同時接続数。1 の場合は逐次処理、2 以上は epoll 多重処理。
 *
 *  conns_per_worker == 1 の場合:
 *    accept() → g_session_fn() を繰り返す従来の逐次処理。
 *
 *  conns_per_worker > 1 の場合:
 *    epoll を使ったイベント ループで最大 conns_per_worker 本のコネクションを
 *    シングルスレッドで同時処理します。容量に達すると server_fd を epoll から
 *    除去して新規 accept を止め、空きが生じると再登録して受け付けを再開します。
 */
/*
 * 本関数は fork() した子プロセスからのみ呼び出される。
 * 子で cplat_exit() を使うと、親が登録したシャットダウン コールバックを
 * fork で継承したまま子側で実行してしまうため、生の exit() を使用する。
 */
static void worker_loop(int server_fd, int worker_id, int conns_per_worker)
{
    /* 稼働状況は利用者が画面で確認する表示であり、失敗を伝える先がないため、この関数の printf の戻り値は明示的に破棄する。 */
    (void)printf("[ワーカー %d, PID %lu] 起動完了、接続待機\n", worker_id, (unsigned long)get_pid());

    if (conns_per_worker == CONNS_PER_WORKER_SEQUENTIAL)
    {
        /* --- 従来の逐次処理 --- */
        struct sockaddr_in client_addr;
        socklen_t addr_len;
        int client_fd;

        /* クライアントからの接続を受け付け、1 接続ずつ順次処理する。 */
        while (running)
        {
            addr_len = sizeof(client_addr);
            client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);

            if (client_fd < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }

                perror("accept");
                continue;
            }

            (void)printf("[ワーカー %d] クライアント接続\n", worker_id);
            g_session_fn(client_fd);
            (void)printf("[ワーカー %d] 次の接続待機\n", worker_id);
        }
    }
    else
    {
        /* --- epoll による多重接続処理 --- */
        struct epoll_event ev;
        struct epoll_event events[MAX_EPOLL_EVENTS];
        char buf[BUFFER_SIZE];
        int active_count = 0;
        int accepting = 1; /* server_fd が epoll に登録済みか */

        int epoll_fd = epoll_create1(0);
        if (epoll_fd < 0)
        {
            perror("epoll_create1");
            exit(1);
        }

        ev.events = EPOLLIN;
        ev.data.fd = server_fd;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0)
        {
            perror("epoll_ctl add server_fd");
            exit(1);
        }

        /* epoll イベントを監視し、新規接続の受け付けおよび既存接続のデータ送受信を多重処理する。 */
        while (running)
        {
            int nfds = epoll_wait(epoll_fd, events, (int)MAX_EPOLL_EVENTS, -1);
            if (nfds < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }

                perror("epoll_wait");
                break;
            }

            for (int i = 0; i < nfds; i++)
            {
                int fd = events[i].data.fd;

                if (fd == server_fd)
                {
                    /* 新規クライアントからの接続を受け付け、epoll 監視対象に追加する。 */
                    int client_fd = accept(server_fd, NULL, NULL);
                    if (client_fd < 0)
                    {
                        if (errno != EINTR)
                        {
                            perror("accept");
                        }

                        continue;
                    }

                    ev.events = EPOLLIN;
                    ev.data.fd = client_fd;

                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0)
                    {
                        perror("epoll_ctl add client_fd");
                        /* 登録に失敗した接続を捨てる後始末であり、失敗しても動作が変わらないため、戻り値は明示的に破棄する。 */
                        (void)cplat_close(client_fd, NULL);
                        continue;
                    }

                    active_count++;
                    (void)printf("[ワーカー %d] 新規接続 (計 %d 接続)\n", worker_id, active_count);

                    /*
                     * ワーカーの同時接続数上限に達したため、
                     * server_fd を epoll から除外して新規接続の受け付けを停止する。
                     */
                    if (active_count >= conns_per_worker)
                    {
                        /* server_fd は同じ epoll に登録済みで、削除の失敗の条件に当たらないため、戻り値は明示的に破棄する。 */
                        (void)epoll_ctl(epoll_fd, EPOLL_CTL_DEL, server_fd, NULL);
                        accepting = 0;
                    }
                }
                else
                {
                    /*
                     * 既存接続からのデータ受信とエコーバック、またはクライアントの切断を処理する。
                     * 送信の失敗と一部だけの送信は、エコーが成立しないため、診断を出して切断と同じく接続を閉じる。
                     */
                    ssize_t n = client_recv(fd, buf, sizeof(buf));
                    int send_failed = 0;

                    if (n > 0 && client_send(fd, buf, (size_t)n) != n)
                    {
                        perror("write");
                        send_failed = 1;
                    }

                    if (n <= 0 || send_failed)
                    {
                        /*
                         * 切断した接続、または送信に失敗した接続の後始末であり、直後に閉じる記述子は閉じれば監視からも外れ、
                         * 閉じる処理の失敗でも動作が変わらないため、この 2 つの戻り値は明示的に破棄する。
                         */
                        (void)epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                        (void)client_close(fd);
                        active_count--;
                        (void)printf("[ワーカー %d] 接続終了 (残 %d 接続)\n", worker_id, active_count);

                        /* 接続に空きが生じたため、server_fd を epoll に再登録して新規接続の受け付けを再開する。 */
                        if (!accepting && active_count < conns_per_worker)
                        {
                            ev.events = EPOLLIN;
                            ev.data.fd = server_fd;

                            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == 0)
                            {
                                accepting = 1;
                            }
                        }
                    }
                }
            }
        }

        /* ワーカーの終了の直前に epoll の記述子を閉じる後始末であり、失敗しても動作が変わらないため、戻り値は明示的に破棄する。 */
        (void)cplat_close(epoll_fd, NULL);
    }

    (void)printf("[ワーカー %d] 終了\n", worker_id);
    exit(0);
}

/*
 * ============================================================
 *  プラットフォーム フック
 * ============================================================
 */

/* Doxygen コメントは、ヘッダーに記載 */

void platform_init(ClientSessionFn session_fn)
{
    struct sigaction sa;

    g_session_fn = session_fn;

    /*
     * 相手が切断した後の送信でプロセスが終了しないよう、SIGPIPE を無視する。
     * 送信は EPIPE で失敗し、送信の失敗の診断へ進む。fork した子とワーカーも設定を引き継ぐ。
     * 有効なシグナル番号とローカル変数の構造体を渡し、失敗の条件 (EINVAL、EFAULT) に当たらないため、
     * sigemptyset と sigaction の戻り値は明示的に破棄する。
     * see: https://man7.org/linux/man-pages/man2/sigaction.2.html
     * see: https://man7.org/linux/man-pages/man3/sigsetops.3.html
     */
    sa.sa_handler = SIG_IGN;
    (void)sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    (void)sigaction(SIGPIPE, &sa, NULL);
}

/* Doxygen コメントは、ヘッダーに記載 */

void platform_cleanup(void)
{
    /* Linux では後処理を必要としない。 */
}

/* Doxygen コメントは、ヘッダーに記載 */

int dispatch_internal_args(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

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
    int server_fd, client_fd;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    struct sigaction sa;

    /*
     * 終了した子プロセスのゾンビ化を防ぐため、SIGCHLD ハンドラーを設定する。
     * 有効なシグナル番号とローカル変数の構造体を渡し、失敗の条件 (EINVAL、EFAULT) に当たらないため、
     * sigemptyset と sigaction の戻り値は明示的に破棄する。
     * see: https://man7.org/linux/man-pages/man2/sigaction.2.html
     * see: https://man7.org/linux/man-pages/man3/sigsetops.3.html
     */
    sa.sa_handler = sigchld_handler;
    (void)sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    (void)sigaction(SIGCHLD, &sa, NULL);

    server_fd = create_listen_socket(port);
    /* 稼働状況は利用者が画面で確認する表示であり、失敗を伝える先がないため、この関数の printf の戻り値は明示的に破棄する。 */
    (void)printf("[親プロセス %lu] fork モード、ポート %d で待ち受け開始\n", (unsigned long)get_pid(), port);

    /* クライアントからの接続を受け付け、接続ごとに子プロセスを fork して通信を処理する。 */
    while (1)
    {
        client_fd = accept(server_fd, (struct sockaddr *)&addr, &addr_len);
        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }

        (void)printf("[親プロセス] 接続受付、子プロセス生成\n");

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            /* fork の失敗後、子へ渡せなかった接続を閉じる後始末であり、失敗しても動作が変わらないため、戻り値は明示的に破棄する。 */
            (void)cplat_close(client_fd, NULL);
        }
        else if (pid == 0)
        {
            /*
             * 子プロセス。
             * cplat_exit() は親が登録したシャットダウン コールバックを
             * fork で継承したまま実行してしまうため、生の exit() を使用する。
             * 子プロセスが使わない待ち受けソケットの複製を閉じる処理であり、失敗しても動作が変わらないため、
             * 戻り値は明示的に破棄する。
             */
            (void)cplat_close(server_fd, NULL);
            g_session_fn(client_fd);
            exit(0);
        }
        else
        {
            /*
             * 親プロセス側では不要となったクライアント ソケットを閉じる。
             * 子へ渡した接続の複製を閉じる処理であり、失敗しても動作が変わらないため、戻り値は明示的に破棄する。
             */
            (void)cplat_close(client_fd, NULL);
        }
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void run_prefork_server(int port, int num_workers, int conns_per_worker)
{
    int server_fd;
    struct sigaction sa;

    /*
     * SIGINT と SIGTERM で待機ループを抜けるため、ハンドラーを設定する。
     * 有効なシグナル番号とローカル変数の構造体を渡し、失敗の条件 (EINVAL、EFAULT) に当たらないため、
     * この関数の sigemptyset と sigaction の戻り値は明示的に破棄する。
     * see: https://man7.org/linux/man-pages/man2/sigaction.2.html
     * see: https://man7.org/linux/man-pages/man3/sigsetops.3.html
     */
    sa.sa_handler = shutdown_handler;
    (void)sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGTERM, &sa, NULL);

    /* 終了した子プロセスのゾンビ化を防ぐため、SIGCHLD ハンドラーを設定する。 */
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART;
    (void)sigaction(SIGCHLD, &sa, NULL);

    server_fd = create_listen_socket(port);
    /* 稼働状況は利用者が画面で確認する表示であり、失敗を伝える先がないため、この関数の printf の戻り値は明示的に破棄する。 */
    (void)printf("[親プロセス %lu] prefork モード、ポート %d で待ち受け開始\n", (unsigned long)get_pid(), port);
    (void)printf("[親プロセス] %d 個のワーカープロセスを起動 (1 ワーカーあたり最大 %d 接続)\n", num_workers,
                 conns_per_worker);

    pid_t *worker_pids = cplat_malloc((size_t)num_workers * sizeof(pid_t));
    if (worker_pids == NULL)
    {
        perror("malloc");
        cplat_exit(1);
    }

    /* 指定された数のワーカー プロセスを事前に生成する。 */
    for (int i = 0; i < num_workers; i++)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            cplat_free(worker_pids);
            cplat_exit(1);
        }
        else if (pid == 0)
        {
            cplat_free(worker_pids);
            worker_loop(server_fd, i, conns_per_worker);

            /* worker_loop() 内でプロセスが終了するため、ここには到達しない。 */
        }

        worker_pids[i] = pid;
    }

    (void)printf("[親プロセス] Ctrl+C で終了\n");

    /* シグナルを受信するまで待機を継続する。 */
    while (running)
    {
        pause();
    }

    (void)printf("\n[親プロセス] 全ワーカーを終了させます\n");

    /*
     * 全ワーカー プロセスへ SIGTERM を送信して終了を促す。
     * ワーカーが終了済みで回収された場合 (ESRCH) は送る必要がないため、戻り値は明示的に破棄する。
     */
    for (int i = 0; i < num_workers; i++)
    {
        if (worker_pids[i] > 0)
        {
            (void)kill(worker_pids[i], SIGTERM);
        }
    }

    /*
     * 全ワーカー プロセスの終了を同期待機して回収する。
     * SIGCHLD ハンドラーが先に回収した場合 (ECHILD) もワーカーは終了済みのため、戻り値は明示的に破棄する。
     */
    for (int i = 0; i < num_workers; i++)
    {
        if (worker_pids[i] > 0)
        {
            (void)waitpid(worker_pids[i], NULL, 0);
        }
    }

    cplat_free(worker_pids);
    /* 親プロセスの終了の直前に待ち受けソケットを閉じる後始末であり、失敗しても動作が変わらないため、戻り値は明示的に破棄する。 */
    (void)cplat_close(server_fd, NULL);
    (void)printf("[親プロセス] 終了\n");
}

#elif defined(PLATFORM_WINDOWS) && defined(COMPILER_MSVC)
    #pragma warning(disable : 4206)
#endif
