/**
 *******************************************************************************
 *  @file           tcpServer.c
 *  @brief          TCP サーバーサンプル共通実装。
 *  @author         Tetsuo Honda
 *  @date           2026/03/17
 *  @version        1.0.0
 *
 *  プラットフォーム共通の処理を実装します。
 *  - handle_client_session() : TCP 通信メインループ (受信 → printf → 送信)
 *  - parse_args()            : コマンドライン引数解析
 *  - main()                  : エントリーポイント
 *
 *  プラットフォーム差異は tcpServer.h のマクロ
 *  (ClientFd / client_recv / client_send / client_close / get_pid)
 *  および各プラットフォームファイルが実装するフック関数
 *  (platform_init / platform_cleanup / dispatch_internal_args)
 *  で吸収します。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <com_util/console/console.h>

#include "tcpServer.h"

/* doxygen コメントは、ヘッダに記載 */
void handle_client_session(ClientFd fd) {
    char buffer[BUFFER_SIZE];
    int  n;

    printf("[PID %lu] クライアント処理開始\n", (unsigned long)get_pid());

    while ((n = (int)client_recv(fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';
        printf("[PID %lu] 受信: %s", (unsigned long)get_pid(), buffer);
        client_send(fd, buffer, (size_t)n);
    }

    printf("[PID %lu] クライアント切断\n", (unsigned long)get_pid());
    client_close(fd);
}

/**
 *******************************************************************************
 *  @brief          コマンドライン引数を解析します。
 *  @param[in]      argc             引数の数。
 *  @param[in]      argv             引数の配列。
 *  @param[out]     mode             動作モード (MODE_FORK / MODE_PREFORK)。
 *  @param[out]     port             待ち受けポート番号。
 *  @param[out]     workers          ワーカー数。
 *  @param[out]     conns_per_worker 1 ワーカーあたりの同時接続数。
 *
 *  `--mode fork|prefork` / `--port <num>` / `--workers <num>` /
 *  `--conns-per-worker <num>` を解析します。
 *  内部起動引数 (`--child`, `--worker`) は dispatch_internal_args() で処理済みの
 *  ため、本関数では無視します。
 *******************************************************************************
 */
static void parse_args(int argc, char *argv[],
                       ServerMode *mode, int *port, int *workers,
                       int *conns_per_worker) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "fork") == 0) {
                *mode = MODE_FORK;
            } else if (strcmp(argv[i], "prefork") == 0) {
                *mode = MODE_PREFORK;
            }
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            *port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--workers") == 0 && i + 1 < argc) {
            *workers = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--conns-per-worker") == 0 && i + 1 < argc) {
            *conns_per_worker = atoi(argv[++i]);
        }
    }
}

/**
 *******************************************************************************
 *  @brief          メインエントリーポイント。
 *  @param[in]      argc コマンドライン引数の数。
 *  @param[in]      argv コマンドライン引数の配列。
 *  @return         正常終了時は 0 を返します。
 *
 *  1. platform_init() でプラットフォーム初期化。
 *  2. dispatch_internal_args() で内部起動引数 (--child / --worker) を確認。
 *     処理済みの場合は platform_cleanup() して終了します。
 *  3. parse_args() でモード・ポート・ワーカー数を解析。
 *  4. 指定されたモードでサーバーを起動します。
 *******************************************************************************
 */
int main(int argc, char *argv[]) {
    com_util_console_init();
    platform_init(handle_client_session);

    if (dispatch_internal_args(argc, argv)) {
        platform_cleanup();
        return 0;
    }

    ServerMode mode             = MODE_PREFORK;
    int        port             = DEFAULT_PORT;
    int        workers          = DEFAULT_WORKERS;
    int        conns_per_worker = DEFAULT_CONNS_PER_WORKER;

    parse_args(argc, argv, &mode, &port, &workers, &conns_per_worker);

    if (mode == MODE_FORK) {
        run_fork_server(port);
    } else {
        run_prefork_server(port, workers, conns_per_worker);
    }

    platform_cleanup();
    return 0;
}
