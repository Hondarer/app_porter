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

#include <cplat/argparser/argparser.h>
#include <cplat/console/console.h>

#include "tcpServer.h"

/**
 *  @brief          メイン エントリ ポイント。
 *  @param[in]      argc コマンド ライン引数の数。
 *  @param[in]      argv コマンド ライン引数の配列。
 *  @return         正常終了時は EXIT_SUCCESS (0) を返します。
 *  @return         引数が不正な場合 (解析の失敗、--mode の不正、--port が 1 から 65535 の範囲外) と、
 *                  --help の使い方を標準出力へ書き込めなかった場合は EXIT_FAILURE を返します。
 *
 *  1. platform_init() でプラットフォーム初期化。
 *  2. dispatch_internal_args() で内部起動引数 (--child / --worker) を確認。
 *     処理済みの場合は platform_cleanup() して終了します。
 *  3. コマンド ライン引数を解析してモード・ポート・ワーカー数を決定します。
 *     `--mode fork|prefork` / `--port <num>` / `--workers <num>` /
 *     `--conns-per-worker <num>` を解析します。
 *  4. 指定されたモードでサーバーを起動します。
 */
/*
 * C4702: Windows 版 run_fork_server / run_prefork_server は while(1) 不脱出ループのため
 * LTCG が関数を noreturn と推論し、後続の platform_cleanup() / return EXIT_SUCCESS を到達不能と判定する。
 * Linux 版は SIGINT/SIGTERM で正常脱出して以下に到達するため、コード自体は仕様上必要である。
 * プラットフォーム差異による副次警告のため main 関数全体に push/pop で局所抑制をかける。
 */
#if defined(COMPILER_MSVC)
    #pragma warning(push)
    #pragma warning(disable : 4702)
#endif
int main(int argc, char *argv[])
{
    cplat_console_init();
    platform_init(handle_client_session);

    /* 子プロセスやワーカーとして起動された場合の内部起動引数を処理する。 */
    if (dispatch_internal_args(argc, argv))
    {
        platform_cleanup();
        return EXIT_SUCCESS;
    }

    ServerMode mode = MODE_PREFORK;
    int port = DEFAULT_PORT;
    int workers = DEFAULT_WORKERS;
    int conns_per_worker = DEFAULT_CONNS_PER_WORKER;
    int need_help = 0;
    const char *mode_str = NULL;

    if (cplat_argparser_init(argc, argv, "複数のプロセス モードを確認する TCP サーバーを起動します。") != CPLAT_OK)
    {
        /* 診断の出力先がほかにないため、戻り値は明示的に破棄する。 */
        (void)fprintf(stderr, "引数の解析の準備に失敗しました。\n");
        return EXIT_FAILURE;
    }
    /* 登録の失敗は直後の cplat_argparser_get_register_error_count() でまとめて検査するため、戻り値は明示的に破棄する。 */
    (void)cplat_argparser_register_flag("-h", "--help", "ヘルプを表示します。", &need_help);
    (void)cplat_argparser_register_option_string(NULL, "--mode", "mode", "fork または prefork。", 0, &mode_str);
    (void)cplat_argparser_register_option_int(NULL, "--port", "port", "待ち受けポート番号。", 0, &port);
    (void)cplat_argparser_register_option_int(NULL, "--workers", "count", "ワーカー数。", 0, &workers);
    (void)cplat_argparser_register_option_int(NULL, "--conns-per-worker", "count", "ワーカーごとの接続数。", 0,
                                              &conns_per_worker);

    if (cplat_argparser_get_register_error_count() > 0)
    {
        /* 診断の出力先がほかになく、終了コードはすでに失敗に決まっているため、戻り値は明示的に破棄する。 */
        (void)cplat_argparser_print_register_error_messages(stderr);
        platform_cleanup();
        return EXIT_FAILURE;
    }

    int parse_result = cplat_argparser_parse();

    if (need_help != 0)
    {
        if (cplat_argparser_print_usage(stdout) != CPLAT_OK || fflush(stdout) != 0 || ferror(stdout) != 0)
        {
            /* 診断の出力先がほかにないため、戻り値は明示的に破棄する。 */
            (void)fprintf(stderr, "エラー: 使い方を標準出力へ書き込めませんでした。\n");
            platform_cleanup();
            return EXIT_FAILURE;
        }

        platform_cleanup();
        return EXIT_SUCCESS;
    }

    if (parse_result != CPLAT_OK)
    {
        /* 診断の出力先がほかになく、終了コードはすでに失敗に決まっているため、この 2 つの表示の関数の戻り値は明示的に破棄する。 */
        (void)cplat_argparser_print_error_messages(stderr);
        (void)cplat_argparser_print_usage(stderr);
        platform_cleanup();
        return EXIT_FAILURE;
    }

    /* ポート番号は 16 bit の範囲だけを受け付ける。範囲外の値は、別のポートへの切り詰めや書式化の失敗を招くため。 */
    if (port < PORT_MIN || port > PORT_MAX)
    {
        /* 診断の出力先がほかになく、終了コードはすでに失敗に決まっているため、この 2 つの出力の戻り値は明示的に破棄する。 */
        (void)fprintf(stderr, "エラー: --port には %d から %d を指定してください。\n\n", PORT_MIN, PORT_MAX);
        (void)cplat_argparser_print_usage(stderr);
        platform_cleanup();
        return EXIT_FAILURE;
    }

    /* 指定されたサーバー動作モードを検証して設定する。 */
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
            /* 診断の出力先がほかになく、終了コードはすでに失敗に決まっているため、この 2 つの出力の戻り値は明示的に破棄する。 */
            (void)fprintf(stderr, "エラー: --mode には fork または prefork を指定してください。\n\n");
            (void)cplat_argparser_print_usage(stderr);
            platform_cleanup();
            return EXIT_FAILURE;
        }
    }

    /* 決定した動作モードに従ってサーバーを起動する。 */
    if (mode == MODE_FORK)
    {
        run_fork_server(port);
    }
    else
    {
        run_prefork_server(port, workers, conns_per_worker);
    }

    platform_cleanup();

    return EXIT_SUCCESS;
}
#if defined(COMPILER_MSVC)
    #pragma warning(pop)
#endif

/* Doxygen コメントは、ヘッダーに記載 */

void handle_client_session(ClientFd fd)
{
    char buffer[BUFFER_SIZE];
    int n;

    /* 稼働状況は利用者が画面で確認する表示であり、失敗を伝える先がないため、この関数の printf の戻り値は明示的に破棄する。 */
    (void)printf("[PID %lu] クライアント処理開始\n", (unsigned long)get_pid());

    /* クライアントからデータを受信し、受信した内容をそのままエコーバックする。 */
    while ((n = (int)client_recv(fd, buffer, sizeof(buffer) - 1)) > 0)
    {
        buffer[n] = '\0';
        (void)printf("[PID %lu] 受信: %s", (unsigned long)get_pid(), buffer);

        /* 送信の失敗と一部だけの送信は、どちらもエコーが成立しないため、診断を出して受信のループを抜ける。 */
        if ((int)client_send(fd, buffer, (size_t)n) != n)
        {
            /* 診断の出力先がほかにないため、戻り値は明示的に破棄する。 */
            (void)fprintf(stderr, "[PID %lu] 送信に失敗しました\n", (unsigned long)get_pid());
            break;
        }
    }

    (void)printf("[PID %lu] クライアント切断\n", (unsigned long)get_pid());
    (void)client_close(fd); /* エコーを終えた接続の後始末であり、結果が変わらないため、戻り値は明示的に破棄する */
}
