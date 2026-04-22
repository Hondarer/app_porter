/**
 *******************************************************************************
 *  @file           recv.c
 *  @brief          受信テストコマンド。
 *  @author         Tetsuo Honda
 *  @date           2026/03/22
 *  @version        1.3.0
 *
 *  @details
 *  指定サービスでデータを受信し続ける CLI テストコマンドです。\n
 *  Ctrl+C で終了します。\n
 *  \n
 *  受信データがテキストと判定された場合はそのまま表示し、\n
 *  バイナリと判定された場合は一時ファイルに保存してパスを表示します。\n
 *  \n
 *  サービス種別が unicast_bidir の場合は双方向モードで動作します。\n
 *  双方向モードでは受信待機中に標準入力からメッセージまたはファイルを送信できます。
 *
 *  @par            使用方法
 *  @code{.sh}
    recv [-l <level>] <config_path> <service_id>
 *  @endcode
 *
 *  @par            オプション
 *  | オプション       | 説明                                                        |
 *  | ---------------- | ----------------------------------------------------------- |
 *  | -l \<level\>     | ログレベルを指定します。指定がない場合はログ出力なし。      |
 *
 *  level に指定可能な値: VERBOSE, INFO, WARNING, ERROR, CRITICAL (大文字小文字不問)
 *
 *  @par            使用例
 *  @code{.sh}
    recv porter-services.conf 10
    recv -l INFO porter-services.conf 10
    recv -l VERBOSE porter-services.conf 1031
 *  @endcode
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>
#include <com_util/crt/path.h>
#include <com_util/crt/stdio.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(PLATFORM_LINUX)
    #include <pthread.h>
    #include <unistd.h>
#elif defined(PLATFORM_WINDOWS)
    #include <process.h>
#endif /* PLATFORM_ */

#include <com_util/console/console.h>
#include <porter.h>

/** 受信ループ継続フラグ。シグナルハンドラーで 0 に設定される。 */
static volatile int g_running = 1;

/**
 *******************************************************************************
 *  @brief          データがテキストかバイナリかを判定する。
 *  @param[in]      data    判定対象のデータ。
 *  @param[in]      len     データのバイト数。
 *  @return         テキストと判定した場合は 1、バイナリと判定した場合は 0 を返します。
 *
 *  @details
 *  全バイトを走査し、NUL (0x00)、\\t / \\n / \\r 以外の C0 制御文字
 *  (0x01-0x08, 0x0B, 0x0C, 0x0E-0x1F)、DEL (0x7F)、
 *  UTF-8 で無効な 0xFE / 0xFF のいずれかが含まれる場合にバイナリと判定します。
 *******************************************************************************
 */
static int is_text_data(const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t i;

    for (i = 0; i < len; i++)
    {
        unsigned char c = p[i];

        if (c == 0x00)
        {
            return 0; /* NUL */
        }
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r')
        {
            return 0; /* C0 制御文字 (タブ/改行/復帰以外) */
        }
        if (c == 0x7F)
        {
            return 0; /* DEL */
        }
        if (c == 0xFE || c == 0xFF)
        {
            return 0; /* UTF-8 無効バイト */
        }
    }
    return 1;
}

/**
 *******************************************************************************
 *  @brief          バイナリデータを一時ファイルに保存する。
 *  @param[in]      data        保存するデータ。
 *  @param[in]      len         データのバイト数。
 *  @param[out]     path_out    保存先パスの格納先バッファ。
 *  @param[in]      path_size   path_out バッファのサイズ (バイト)。
 *  @return         成功時は 0、失敗時は -1 を返します。
 *******************************************************************************
 */
static int save_to_temp_file(const void *data, size_t len, char *path_out, size_t path_size)
{
#if defined(PLATFORM_LINUX)
    int fd;
    ssize_t written;

    snprintf(path_out, path_size, "/tmp/porter_recv_XXXXXX");
    fd = mkstemp(path_out);
    if (fd == -1)
    {
        return -1;
    }
    written = write(fd, data, len);
    close(fd);
    if (written < 0 || (size_t)written != len)
    {
        return -1;
    }
    return 0;
#elif defined(PLATFORM_WINDOWS)
    char tmp_dir[PLATFORM_PATH_MAX];
    FILE *fp = NULL;
    size_t written;

    if (path_out == NULL || path_size < PLATFORM_PATH_MAX)
    {
        return -1;
    }
    if (GetTempPathA(sizeof(tmp_dir), tmp_dir) == 0)
    {
        return -1;
    }
    if (GetTempFileNameA(tmp_dir, "ptr", 0, path_out) == 0)
    {
        return -1;
    }
    fp = com_util_fopen(path_out, "wb", NULL);
    if (fp == NULL)
    {
        return -1;
    }
    written = com_util_fwrite(data, 1, len, fp);
    com_util_fclose(fp);
    if (written != len)
    {
        return -1;
    }
    return 0;
#endif /* PLATFORM_ */
}

#if defined(PLATFORM_LINUX)
/**
 *******************************************************************************
 *  @brief          Linux SIGINT シグナルハンドラー。
 *  @param[in]      sig シグナル番号。
 *******************************************************************************
 */
static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
    printf("\n終了中...\n");
    fflush(stdout);
}
#elif defined(PLATFORM_WINDOWS)
/**
 *******************************************************************************
 *  @brief          Windows コンソール制御イベントハンドラー。
 *  @param[in]      type    コンソール制御イベント種別。\n
 *                          (CTRL_C_EVENT / CTRL_BREAK_EVENT / 
 *                           CTRL_CLOSE_EVENT / CTRL_SHUTDOWN_EVENT など)
 *  @return         イベントを処理した場合は TRUE を返します。
 *                  TRUE を返すことで既定の強制終了処理を抑止します。
 *
 *  @details
 *  GenerateConsoleCtrlEvent() や Ctrl+C / Ctrl+Break により送信される
 *  コンソール制御イベントを受信します。\n
 *  本ハンドラーでは受信ループ終了フラグをクリアし、
 *  メインスレッドに正常終了を要求します。
 *
 *  TRUE を返さない場合、Windows の既定動作によりプロセスは
 *  STATUS_CONTROL_C_EXIT (0xC000013A) で終了します。
 *******************************************************************************
 */
static BOOL WINAPI console_ctrl_handler(DWORD type)
{
    switch (type)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_running = 0;
        printf("\n終了中...\n");
        fflush(stdout);
        return TRUE;
    }

    return FALSE;
}
#endif /* PLATFORM_ */

/**
 *******************************************************************************
 *  @brief          受信コールバック関数。
 *  @param[in]      service_id  サービスの ID。
 *  @param[in]      peer_id     ピア識別子 (N:1 モード時は非ゼロ、1:1 モード時は 0)。
 *  @param[in]      event       イベント種別。
 *  @param[in]      data        受信データまたは path 状態配列。
 *  @param[in]      len         受信データ長または path index。
 *******************************************************************************
 */
static void on_recv(int64_t service_id, PotrPeerId peer_id, PotrEvent event, const void *data, size_t len)
{
    char buf[POTR_MAX_PAYLOAD + 1];
    size_t copy_len;
    const int *path_states;
    int        path_idx;

    (void)peer_id;
    switch (event)
    {
    case POTR_EVENT_CONNECTED:
        printf("[サービス %" PRId64 "] 接続確立\n", service_id);
        fflush(stdout);
        break;

    case POTR_EVENT_DISCONNECTED:
        printf("[サービス %" PRId64 "] 切断検知\n", service_id);
        fflush(stdout);
        break;

    case POTR_EVENT_PATH_CONNECTED:
    case POTR_EVENT_PATH_DISCONNECTED:
        path_states = (const int *)data;
        path_idx    = (int)len;
        printf("[サービス %" PRId64 "] path[%d] %s states={%d,%d,%d,%d}\n",
               service_id,
               path_idx,
               (event == POTR_EVENT_PATH_CONNECTED) ? "CONNECTED" : "DISCONNECTED",
               path_states != NULL ? path_states[0] : 0,
               path_states != NULL ? path_states[1] : 0,
               path_states != NULL ? path_states[2] : 0,
               path_states != NULL ? path_states[3] : 0);
        fflush(stdout);
        break;

    case POTR_EVENT_DATA:
    default:
        if (is_text_data(data, len))
        {
            if (len < POTR_MAX_PAYLOAD)
            {
                copy_len = len;
            }
            else
            {
                copy_len = POTR_MAX_PAYLOAD;
            }
            memcpy(buf, data, copy_len);
            buf[copy_len] = '\0';
            printf("[サービス %" PRId64 "] 受信 (%zu バイト): %s\n", service_id, len, buf);
        }
        else
        {
            char tmp_path[4096];
            if (save_to_temp_file(data, len, tmp_path, sizeof(tmp_path)) == 0)
            {
                printf("[サービス %" PRId64 "] 受信 (%zu バイト): バイナリデータを保存しました: %s\n", service_id, len,
                       tmp_path);
            }
            else
            {
                fprintf(stderr, "[サービス %" PRId64 "] 受信 (%zu バイト): バイナリデータの保存に失敗しました。\n",
                        service_id, len);
            }
        }
        fflush(stdout);
        break;
    }
}

/**
 *******************************************************************************
 *  @brief          ログレベル文字列を trace_level_t に変換する。
 *  @param[in]      str     レベル文字列 (VERBOSE/INFO/WARNING/ERROR/CRITICAL)。
 *  @param[out]     out     変換結果の格納先。
 *  @return         変換に成功した場合は 1、未知の文字列の場合は 0 を返します。
 *******************************************************************************
 */
static int parse_log_level(const char *str, trace_level_t *out)
{
    static const struct
    {
        const char *name;
        trace_level_t level;
        uint32_t _pad;
    } tbl[] = {
        {"VERBOSE", TRACE_LEVEL_VERBOSE, 0U}, {"INFO", TRACE_LEVEL_INFO, 0U},         {"WARNING", TRACE_LEVEL_WARNING, 0U},
        {"ERROR", TRACE_LEVEL_ERROR, 0U},     {"CRITICAL", TRACE_LEVEL_CRITICAL, 0U},
    };
    char upper[16];
    size_t i;
    size_t j;

    for (j = 0; j < sizeof(upper) - 1U && str[j] != '\0'; j++)
    {
        upper[j] = (str[j] >= 'a' && str[j] <= 'z') ? (char)(str[j] - ('a' - 'A')) : str[j];
    }
    upper[j] = '\0';

    for (i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++)
    {
        if (strcmp(upper, tbl[i].name) == 0)
        {
            *out = tbl[i].level;
            return 1;
        }
    }
    return 0;
}

/* ============================================================ */
/* unicast_bidir 送信スレッド                                     */
/* ============================================================ */

/** bidir 送信スレッドに渡すコンテキスト。 */
typedef struct
{
    PotrHandle handle;
    volatile int *running;
} BidirSendCtx;

/**
 *******************************************************************************
 *  @brief          標準入力から1行読み込み、末尾の改行を取り除く。
 *  @param[out]     buf     読み込み先バッファ。
 *  @param[in]      size    バッファサイズ (バイト)。
 *  @return         入力があれば 1、EOF またはエラーなら 0 を返します。
 *******************************************************************************
 */
static int read_line(char *buf, size_t size)
{
    if (fgets(buf, (int)size, stdin) == NULL)
    {
        return 0;
    }
    buf[strcspn(buf, "\n")] = '\0';
    return 1;
}

/**
 *******************************************************************************
 *  @brief          ファイルをバイナリモードで読み込む。
 *  @param[in]      path        ファイルパス。
 *  @param[out]     out_data    読み込んだデータの格納先 (malloc 確保、呼び出し元が free する)。
 *  @param[out]     out_len     読み込んだデータのバイト数の格納先。
 *  @return         成功時は 0、失敗時は -1 を返します。
 *                  失敗時はエラーメッセージを stderr に出力します。
 *******************************************************************************
 */
static int read_file_data(const char *path, unsigned char **out_data, size_t *out_len)
{
    FILE *fp = NULL;
    int64_t file_size;
    unsigned char *buf;
    size_t read_count;

    fp = com_util_fopen(path, "rb", NULL);

    if (fp == NULL)
    {
        fprintf(stderr, "エラー: ファイル \"%s\" を開けませんでした。\n", path);
        return -1;
    }

    if (com_util_fseek(fp, 0, SEEK_END) != 0)
    {
        fprintf(stderr, "エラー: ファイルの読み込みに失敗しました。\n");
        com_util_fclose(fp);
        return -1;
    }

    file_size = com_util_ftell(fp);
    if (file_size < 0)
    {
        fprintf(stderr, "エラー: ファイルの読み込みに失敗しました。\n");
        com_util_fclose(fp);
        return -1;
    }

    if (file_size == 0)
    {
        fprintf(stderr, "エラー: ファイルが空です。\n");
        com_util_fclose(fp);
        return -1;
    }

    if ((uint64_t)file_size > POTR_MAX_MESSAGE_SIZE)
    {
        fprintf(stderr, "エラー: ファイルサイズ (%" PRId64 " バイト) が最大送信サイズ (%u バイト) を超えています。\n",
                file_size, (unsigned)POTR_MAX_MESSAGE_SIZE);
        com_util_fclose(fp);
        return -1;
    }

    buf = (unsigned char *)malloc((size_t)file_size);
    if (buf == NULL)
    {
        fprintf(stderr, "エラー: メモリ確保に失敗しました。\n");
        com_util_fclose(fp);
        return -1;
    }

    com_util_rewind(fp);
    read_count = com_util_fread(buf, 1, (size_t)file_size, fp);
    com_util_fclose(fp);

    if (read_count != (size_t)file_size)
    {
        fprintf(stderr, "エラー: ファイルの読み込みに失敗しました。\n");
        free(buf);
        return -1;
    }

    *out_data = buf;
    *out_len = (size_t)file_size;
    return 0;
}

#if defined(PLATFORM_LINUX)
typedef pthread_t BidirThread;

/**
 *******************************************************************************
 *  @brief          bidir 送信スレッド関数 (Linux)。
 *  @param[in]      arg BidirSendCtx へのポインタ。
 *  @return         NULL
 *******************************************************************************
 */
static void *bidir_send_thread_func(void *arg)
#elif defined(PLATFORM_WINDOWS)
typedef HANDLE BidirThread;

/**
 *******************************************************************************
 *  @brief          bidir 送信スレッド関数 (Windows)。
 *  @param[in]      arg BidirSendCtx へのポインタ。
 *  @return         0
 *******************************************************************************
 */
static unsigned __stdcall bidir_send_thread_func(void *arg)
#endif /* PLATFORM_ */
{
    BidirSendCtx *ctx = (BidirSendCtx *)arg;
    char msg_buf[POTR_MAX_MESSAGE_SIZE + 2U];
    char ans_buf[8];
    size_t msg_len;
    int compress;
    const char *compress_label;

    while (*ctx->running)
    {
        unsigned char *file_data = NULL;
        size_t file_len = 0;
        const void *send_data;
        size_t send_len;
        int is_file = 0;

        printf("\n送信方法を選択してください [T: テキスト / f: ファイル]> ");
        fflush(stdout);

        if (!read_line(ans_buf, sizeof(ans_buf)))
        {
            break;
        }

        if (ans_buf[0] == 'f' || ans_buf[0] == 'F')
        {
            /* ファイル送信モード */
            is_file = 1;

            printf("ファイルパス> ");
            fflush(stdout);

            if (!read_line(msg_buf, sizeof(msg_buf)))
            {
                break;
            }

            if (strlen(msg_buf) == 0U)
            {
                break;
            }

            if (read_file_data(msg_buf, &file_data, &file_len) != 0)
            {
                goto bidir_ask_continue;
            }

            send_data = file_data;
            send_len = file_len;
        }
        else
        {
            /* テキスト送信モード */
            printf("メッセージ> ");
            fflush(stdout);

            if (!read_line(msg_buf, sizeof(msg_buf)))
            {
                break; /* EOF またはシグナル割り込み */
            }

            msg_len = strlen(msg_buf);
            if (msg_len == 0U)
            {
                break; /* 空行で送信終了 */
            }

            send_data = msg_buf;
            send_len = msg_len;
        }

        printf("圧縮送信しますか？ [y/N]> ");
        fflush(stdout);

        compress = 0;
        if (read_line(ans_buf, sizeof(ans_buf)))
        {
            if (ans_buf[0] == 'y' || ans_buf[0] == 'Y')
            {
                compress = 1;
            }
        }

        compress_label = compress ? " [圧縮あり]" : "";

        if (is_file)
        {
            printf("ファイル送信中: \"%s\" (%zu バイト)%s\n", msg_buf, send_len, compress_label);
        }
        else
        {
            printf("送信中: \"%s\" (%zu バイト)%s\n", msg_buf, send_len, compress_label);
        }

        if (potrSend(ctx->handle, POTR_PEER_NA, send_data, send_len,
                     (compress ? POTR_SEND_COMPRESS : 0) | POTR_SEND_BLOCKING) != POTR_SUCCESS)
        {
            fprintf(stderr, "エラー: 送信に失敗しました。\n");
            free(file_data);
            break;
        }

        if (is_file)
        {
            printf("ファイル送信完了: \"%s\" (%zu バイト)\n", msg_buf, send_len);
        }
        else
        {
            printf("送信完了。");
        }

        free(file_data);

    bidir_ask_continue:
        printf(" 続けて送信しますか？ [Y/n]> ");
        fflush(stdout);

        if (!read_line(ans_buf, sizeof(ans_buf)))
        {
            break;
        }

        if (ans_buf[0] == 'n' || ans_buf[0] == 'N')
        {
            break;
        }
    }

    *ctx->running = 0; /* 送信終了時に受信ループも停止させる */

#if defined(PLATFORM_LINUX)
    return NULL;
#elif defined(PLATFORM_WINDOWS)
    return 0U;
#endif /* PLATFORM_ */
}

/**
 *******************************************************************************
 *  @brief          bidir 送信スレッドを起動する。
 *  @param[out]     thread  スレッドハンドルの格納先。
 *  @param[in]      ctx     スレッドに渡すコンテキスト。
 *  @return         成功時は 1、失敗時は 0 を返します。
 *******************************************************************************
 */
static int start_bidir_send_thread(BidirThread *thread, BidirSendCtx *ctx)
{
#if defined(PLATFORM_LINUX)
    return pthread_create(thread, NULL, bidir_send_thread_func, ctx) == 0;
#elif defined(PLATFORM_WINDOWS)
    uintptr_t h = _beginthreadex(NULL, 0U, bidir_send_thread_func, ctx, 0U, NULL);
    if (h == 0U)
    {
        return 0;
    }
    *thread = (HANDLE)h;
    return 1;
#endif /* PLATFORM_ */
}

/**
 *******************************************************************************
 *  @brief          bidir 送信スレッドの終了を待機して破棄する。
 *  @param[in]      thread  スレッドハンドル。
 *******************************************************************************
 */
static void join_bidir_send_thread(BidirThread thread)
{
#if defined(PLATFORM_LINUX)
    pthread_cancel(thread); /* fgets でブロック中のスレッドを中断する */
    pthread_join(thread, NULL);
#elif defined(PLATFORM_WINDOWS)
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#endif /* PLATFORM_ */
}

/**
 *******************************************************************************
 *  @brief          メインエントリーポイント。
 *  @param[in]      argc コマンドライン引数の数。
 *  @param[in]      argv コマンドライン引数の配列。
 *  @return         成功時は EXIT_SUCCESS、失敗時は EXIT_FAILURE を返します。
 *******************************************************************************
 */
int main(int argc, char *argv[])
{
    const char *config_path;
    int64_t service_id;
    PotrHandle handle;
    int i;
    trace_level_t log_level = TRACE_LEVEL_NONE;
    int log_level_set = 0;
    PotrType svc_type;
    int is_bidir;
    BidirSendCtx bidir_ctx;
    BidirThread bidir_thread = 0;
    int bidir_started = 0;

    console_init();

    /* オプション解析 */
    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-l") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "エラー: -l オプションにレベルを指定してください。\n");
                fprintf(stderr, "使用方法: %s [-l <level>] <config_path> <service_id>\n", argv[0]);
                return EXIT_FAILURE;
            }
            i++;
            if (!parse_log_level(argv[i], &log_level))
            {
                fprintf(stderr,
                        "エラー: 不明なログレベル \"%s\"。"
                        "VERBOSE/INFO/WARNING/ERROR/CRITICAL のいずれかを指定してください。\n",
                        argv[i]);
                return EXIT_FAILURE;
            }
            log_level_set = 1;
        }
        else
        {
            break; /* 最初の非オプション引数で停止 */
        }
    }

    /* positional 引数チェック */
    if (argc - i < 2)
    {
        fprintf(stderr, "使用方法: %s [-l <level>] <config_path> <service_id>\n", argv[0]);
        fprintf(stderr, "  -l <level>  ログレベル (VERBOSE/INFO/WARNING/ERROR/CRITICAL)\n");
        fprintf(stderr, "例: %s porter-services.conf 10\n", argv[0]);
        fprintf(stderr, "例: %s -l INFO porter-services.conf 10\n", argv[0]);
        return EXIT_FAILURE;
    }

    config_path = argv[i];
    service_id = (int64_t)strtoll(argv[i + 1], NULL, 10);

    /* ロガー設定 (stderr 出力) */
    if (log_level_set)
    {
        trace_logger_t *logger = potrGetLogger();
        trace_logger_set_stderr_level(logger, log_level);
        trace_logger_start(logger);
    }

#if defined(PLATFORM_LINUX)
    signal(SIGINT, sig_handler);
#elif defined(PLATFORM_WINDOWS)
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#endif /* PLATFORM_ */

    printf("サービス %" PRId64 " を開いています... (設定: %s)\n", service_id, config_path);
    fflush(stdout);

    /* サービス種別を取得して unicast_bidir かどうか判定する */
    is_bidir = 0;
    if (potrGetServiceType(config_path, service_id, &svc_type) == POTR_SUCCESS && svc_type == POTR_TYPE_UNICAST_BIDIR)
    {
        is_bidir = 1;
    }

    if (potrOpenServiceFromConfig(config_path, service_id, POTR_ROLE_RECEIVER, on_recv, &handle) != POTR_SUCCESS)
    {
        fprintf(stderr, "エラー: サービス %" PRId64 " を開けませんでした。\n", service_id);
        return EXIT_FAILURE;
    }

    if (is_bidir)
    {
        printf("双方向モード (unicast_bidir)。\n");
        printf("メッセージを入力して送信できます (空行または Ctrl+D で送信終了)。\n");
        fflush(stdout);
        bidir_ctx.handle = handle;
        bidir_ctx.running = &g_running;
        if (start_bidir_send_thread(&bidir_thread, &bidir_ctx))
        {
            bidir_started = 1;
        }
        else
        {
            fprintf(stderr, "警告: 送信スレッドの起動に失敗しました。受信専用モードで動作します。\n");
        }
    }

    printf("受信待機中... (Ctrl+C で終了)\n");
    fflush(stdout);

    while (g_running)
    {
#if defined(PLATFORM_LINUX)
        usleep(100000);
#elif defined(PLATFORM_WINDOWS)
        Sleep(100);
#endif /* PLATFORM_ */
    }

    if (bidir_started)
    {
        join_bidir_send_thread(bidir_thread);
    }

    potrCloseService(handle);
    printf("終了しました。\n");
    fflush(stdout);
    return EXIT_SUCCESS;
}
