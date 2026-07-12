/**
 *******************************************************************************
 *  @file           tcpServer.c
 *  @brief          TCP サーバー サンプルの共通処理を実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/17
 *  @version        1.0.0
 *
 *  プラットフォーム共通の処理を実装します。
 *  - handle_client_session() : TCP 通信メイン ループ (受信 → printf → 送信)
 *  - main()                  : エントリ ポイント (コマンド ライン引数解析を含む)
 *
 *  プラットフォーム差異は tcpServer.h のマクロ
 *  (ClientFd / client_recv / client_send / client_close / get_pid)
 *  および各プラットフォーム ファイルが実装するフック関数
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

#include <com_util/argparser/argparser.h>
#include <com_util/console/console.h>

#include "tcpServer.h"

/* Doxygen コメントは、ヘッダーに記載 */

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
 *  @brief          メイン エントリ ポイント。
 *  @param[in]      argc コマンド ライン引数の数。
 *  @param[in]      argv コマンド ライン引数の配列。
 *  @return         正常終了時は 0 を返します。
 *
 *  1. platform_init() でプラットフォーム初期化。
 *  2. dispatch_internal_args() で内部起動引数 (--child / --worker) を確認。
 *     処理済みの場合は platform_cleanup() して終了します。
 *  3. コマンド ライン引数を解析してモード・ポート・ワーカー数を決定します。
 *     `--mode fork|prefork` / `--port <num>` / `--workers <num>` /
 *     `--conns-per-worker <num>` を解析します。
 *  4. 指定されたモードでサーバーを起動します。
 */
/* C4702: Windows 版 run_fork_server / run_prefork_server は while(1) 不脱出ループのため
 * LTCG が関数を noreturn と推論し、後続の platform_cleanup() / return EXIT_SUCCESS を到達不能と判定する。
 * Linux 版は SIGINT/SIGTERM で正常脱出して以下に到達するため、コード自体は仕様上必要。
 * プラットフォーム差異による副次警告のため main 関数全体に push/pop で局所抑制をかける。 */
#if defined(COMPILER_MSVC)
#pragma warning(push)
#pragma warning(disable: 4702)
#endif
int main(int argc, char *argv[]) {
    com_util_console_init();
    platform_init(handle_client_session);

    if (dispatch_internal_args(argc, argv)) {
        platform_cleanup();
        return EXIT_SUCCESS;
    }

    ServerMode mode             = MODE_PREFORK;
    int        port             = DEFAULT_PORT;
    int        workers          = DEFAULT_WORKERS;
    int        conns_per_worker = DEFAULT_CONNS_PER_WORKER;
    int need_help = 0;
    const char *mode_str = NULL;

    com_util_argparser_init("複数のプロセス モードを確認する TCP サーバーを起動します。");
    com_util_argparser_register_flag("-h", "--help", "ヘルプを表示します。", &need_help);
    com_util_argparser_register_option_string(NULL, "--mode", "mode", "fork または prefork。", 0, &mode_str);
    com_util_argparser_register_option_int(NULL, "--port", "port", "待ち受けポート番号。", 0, &port);
    com_util_argparser_register_option_int(NULL, "--workers", "count", "ワーカー数。", 0, &workers);
    com_util_argparser_register_option_int(NULL, "--conns-per-worker", "count", "ワーカーごとの接続数。", 0,
                                           &conns_per_worker);

    if (com_util_argparser_get_register_error_count() > 0)
    {
        com_util_argparser_print_register_error_messages(stderr);
        platform_cleanup();
        return EXIT_FAILURE;
    }

    int parse_result = com_util_argparser_parse(argc, argv);

    if (need_help != 0)
    {
        com_util_argparser_print_usage(stdout);
        platform_cleanup();
        return EXIT_SUCCESS;
    }

    if (parse_result != COM_UTIL_ARGPARSER_OK)
    {
        com_util_argparser_print_error_messages(stderr);
        com_util_argparser_print_usage(stderr);
        platform_cleanup();
        return EXIT_FAILURE;
    }

    if (mode_str != NULL)
    {
        if (strcmp(mode_str, "fork") == 0)
        {
            mode = MODE_FORK;
        }
        else if (strcmp(mode_str, "prefork") == 0)
        {
            mode = MODE_PREFORK;
        }
        else
        {
            fprintf(stderr, "エラー: --mode には fork または prefork を指定してください。\n\n");
            com_util_argparser_print_usage(stderr);
            platform_cleanup();
            return EXIT_FAILURE;
        }
    }

    if (mode == MODE_FORK) {
        run_fork_server(port);
    } else {
        run_prefork_server(port, workers, conns_per_worker);
    }

    platform_cleanup();

    return EXIT_SUCCESS;
}
#if defined(COMPILER_MSVC)
#pragma warning(pop)
#endif
