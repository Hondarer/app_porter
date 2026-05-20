/**
 *******************************************************************************
 *  @file           send.c
 *  @brief          送信テストコマンド。
 *  @author         Tetsuo Honda
 *  @date           2026/03/22
 *  @version        1.4.0
 *
 *  指定サービスへデータを対話式に送信する CLI テストコマンドです。\n
 *  1 回のセッション内でメッセージを連続して送信できます。\n
 *  各送信ごとにテキストメッセージまたはファイル (バイナリ) を選択できます。\n
 *  各送信後に次のメッセージを送るか終了するかを選択できます。\n
 *  \n
 *  サービス種別が unicast_bidir または tcp_bidir の場合は双方向モードで動作します。\n
 *  双方向モードでは相手から受信したメッセージと接続状態を標準出力に表示します。\n
 *  受信データがバイナリと判定された場合は一時ファイルに保存してパスを表示します。
 *
 *  @par            使用方法
 *  @code{.sh}
    send [-l <level>] <config_path> <service_id>
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
    send porter-services.conf 10
    send -l INFO porter-services.conf 10
    send -l VERBOSE porter-services.conf 1031
 *  @endcode
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>
#include <com_util/crt/path.h>
#include <com_util/crt/stdio.h>
#include <com_util/prompt/pinned_prompt.h>
#include <com_util/runtime/shutdown.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(PLATFORM_LINUX)
    #include <unistd.h>
#endif /* PLATFORM_LINUX */

#include <com_util/console/console.h>
#include <porter.h>

/** 入力バッファサイズ。POTR_MAX_MESSAGE_SIZE + 改行 + NUL。 */
#define INPUT_BUF_SIZE (POTR_MAX_MESSAGE_SIZE + 2U)

/** 送信ループ継続フラグ。終了要求 callback で 0 に設定される。 */
static volatile int g_running = 1;
/** 終了要求の受信有無。main 側の表示制御に使う。 */
static volatile sig_atomic_t g_shutdown_requested = 0;
/** pinned prompt ハンドル。on_recv や trace hook から参照する。 */
static com_util_pinned_prompt_t *g_screen = NULL;

/**
 *  @brief          データがテキストかバイナリかを判定する。
 *  @param[in]      data    判定対象のデータ。
 *  @param[in]      len     データのバイト数。
 *  @return         テキストと判定した場合は 1、バイナリと判定した場合は 0 を返します。
 *
 *  全バイトを走査し、NUL (0x00)、\\t / \\n / \\r 以外の C0 制御文字
 *  (0x01-0x08, 0x0B, 0x0C, 0x0E-0x1F)、DEL (0x7F)、
 *  UTF-8 で無効な 0xFE / 0xFF のいずれかが含まれる場合にバイナリと判定します。
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
 *  @brief          終了要求 callback。
 *  @param[in]      event   終了イベント。
 *  @param[in]      context 未使用。
 */
static void send_shutdown_request_callback(const com_util_shutdown_event_t *event, void *context)
{
    (void)event;
    (void)context;
    g_shutdown_requested = 1;
    g_running = 0;

#if defined(PLATFORM_LINUX)
    close(STDIN_FILENO); /* fgets のブロックを解除する */
#endif /* PLATFORM_ */
}

/**
 *  @brief          受信コールバック関数 (双方向サービス用)。
 *  @param[in]      service_id  サービスの ID。
 *  @param[in]      peer_id     ピア識別子 (N:1 モード時は非ゼロ、1:1 モード時は 0)。
 *  @param[in]      event       イベント種別。
 *  @param[in]      data        受信データまたは path 状態配列。
 *  @param[in]      len         受信データ長または path index。
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
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                      "\n[サービス %" PRId64 "] 接続確立\n", service_id);
        break;

    case POTR_EVENT_DISCONNECTED:
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                      "\n[サービス %" PRId64 "] 切断検知\n", service_id);
        break;

    case POTR_EVENT_PATH_CONNECTED:
    case POTR_EVENT_PATH_DISCONNECTED:
    {
        const char *path_event_str;
        int path_state0;
        int path_state1;
        int path_state2;
        int path_state3;
        path_states = (const int *)data;
        path_idx    = (int)len;
        if (event == POTR_EVENT_PATH_CONNECTED)
        {
            path_event_str = "CONNECTED";
        }
        else
        {
            path_event_str = "DISCONNECTED";
        }
        if (path_states != NULL)
        {
            path_state0 = path_states[0];
        }
        else
        {
            path_state0 = 0;
        }
        if (path_states != NULL)
        {
            path_state1 = path_states[1];
        }
        else
        {
            path_state1 = 0;
        }
        if (path_states != NULL)
        {
            path_state2 = path_states[2];
        }
        else
        {
            path_state2 = 0;
        }
        if (path_states != NULL)
        {
            path_state3 = path_states[3];
        }
        else
        {
            path_state3 = 0;
        }
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                      "\n[サービス %" PRId64 "] path[%d] %s states={%d,%d,%d,%d}\n",
                                      service_id,
                                      path_idx,
                                      path_event_str,
                                      path_state0,
                                      path_state1,
                                      path_state2,
                                      path_state3);
        break;
    }

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
            com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                          "\n[サービス %" PRId64 "] 受信 (%zu バイト): %s\n", service_id, len, buf);
        }
        else
        {
            char  tmp_path[PLATFORM_PATH_MAX];
            FILE *fp = com_util_fopen_temp("ptr", "wb", tmp_path, sizeof(tmp_path), NULL);

            if (fp != NULL && com_util_fwrite(data, 1, len, fp) == len)
            {
                com_util_fclose(fp);
                com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                              "\n[サービス %" PRId64 "] 受信 (%zu バイト): バイナリデータを保存しました: %s\n",
                                              service_id, len, tmp_path);
            }
            else
            {
                if (fp != NULL)
                {
                    com_util_fclose(fp);
                }
                com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                              "\n[サービス %" PRId64 "] 受信 (%zu バイト): バイナリデータの保存に失敗しました。\n",
                                              service_id, len);
            }
        }
        break;
    }
}

/**
 *  @brief          トレースフックコールバック。指定レベル以上のメッセージを stderr へ出力する。
 *  @param[in]      prev      チェーン継続用の前エントリ。
 *  @param[in]      handle    trace を行った tracer ハンドル。
 *  @param[in]      level     trace レベル。
 *  @param[in]      timestamp 解決済みタイムスタンプ。
 *  @param[in]      message   解決済みメッセージ文字列。
 *  @param[in]      context   閾値レベル (com_util_trace_level_t *) を指すポインタ。
 */
static void trace_console_hook(
    com_util_tracer_hook_entry_t        *prev,
    com_util_tracer_t                   *handle,
    com_util_trace_level_t               level,
    const com_util_realtime_timestamp_t *timestamp,
    const char                          *message,
    void                                *context)
{
    static const char lc_table[] = {'C', 'E', 'W', 'I', 'V', 'D'};
    com_util_trace_level_t threshold = *(const com_util_trace_level_t *)context;
    char ts[COM_UTIL_CLOCK_ISO8601_LOCAL_MSEC_LEN + 1];
    char lc;

    if (threshold != COM_UTIL_TRACE_LEVEL_NONE && (int)level <= (int)threshold)
    {
        if ((int)level >= 0 && (int)level < (int)COM_UTIL_TRACE_LEVEL_NONE)
        {
            lc = lc_table[(int)level];
        }
        else
        {
            lc = 'D';
        }
        com_util_format_realtime_iso8601_local(ts, sizeof(ts),
                                               timestamp->tv_sec, timestamp->tv_nsec);
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "%s %c %s\n", ts, lc, message);
    }
    com_util_tracer_call_next_hook(prev, handle, level, timestamp, message);
}

/**
 *  @brief          ログレベル文字列を com_util_trace_level_t に変換する。
 *  @param[in]      str     レベル文字列 (VERBOSE/INFO/WARNING/ERROR/CRITICAL)。
 *  @param[out]     out     変換結果の格納先。
 *  @return         変換に成功した場合は 1、未知の文字列の場合は 0 を返します。
 */
static int parse_trace_level(const char *str, com_util_trace_level_t *out)
{
    static const struct
    {
        const char *name;
        com_util_trace_level_t level;
        uint32_t _pad;
    } tbl[] = {
        {"VERBOSE", COM_UTIL_TRACE_LEVEL_VERBOSE, 0U}, {"INFO", COM_UTIL_TRACE_LEVEL_INFO, 0U},         {"WARNING", COM_UTIL_TRACE_LEVEL_WARNING, 0U},
        {"ERROR", COM_UTIL_TRACE_LEVEL_ERROR, 0U},     {"CRITICAL", COM_UTIL_TRACE_LEVEL_CRITICAL, 0U},
    };
    char upper[16];
    size_t i;
    size_t j;

    for (j = 0; j < sizeof(upper) - 1U && str[j] != '\0'; j++)
    {
        if (str[j] >= 'a' && str[j] <= 'z')
        {
            upper[j] = (char)(str[j] - ('a' - 'A'));
        }
        else
        {
            upper[j] = str[j];
        }
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

/**
 *  @brief          文字列先頭の空白を読み飛ばす。
 *  @param[in]      p 文字列。
 *  @return         空白以外の先頭位置を返します。
 */
static char *skip_spaces(char *p)
{
    while (p != NULL && (*p == ' ' || *p == '\t'))
    {
        p++;
    }
    return p;
}

/**
 *  @brief          文字列末尾の空白を削除する。
 *  @param[in,out]  s 文字列。
 */
static void trim_right(char *s)
{
    size_t len;

    if (s == NULL)
    {
        return;
    }

    len = strlen(s);
    while (len > 0U && (s[len - 1U] == ' ' || s[len - 1U] == '\t'))
    {
        len--;
    }
    s[len] = '\0';
}

/**
 *  @brief          次の空白区切りトークンを取得する。
 *  @param[in,out]  cursor 解析位置。
 *  @return         トークン先頭。トークンがない場合は NULL。
 */
static char *next_token(char **cursor)
{
    char *p;
    char *start;

    if (cursor == NULL || *cursor == NULL)
    {
        return NULL;
    }

    p = skip_spaces(*cursor);
    if (*p == '\0')
    {
        *cursor = p;
        return NULL;
    }

    start = p;
    while (*p != '\0' && *p != ' ' && *p != '\t')
    {
        p++;
    }
    if (*p != '\0')
    {
        *p = '\0';
        p++;
    }
    *cursor = p;
    return start;
}

/**
 *  @brief          前後が同じ引用符なら引用符を外す。
 *  @param[in,out]  value 文字列。
 *  @return         引用符を外した文字列。
 */
static char *strip_matching_quotes(char *value)
{
    size_t len;

    value = skip_spaces(value);
    trim_right(value);

    len = strlen(value);
    if (len >= 2U
        && ((value[0] == '"' && value[len - 1U] == '"')
            || (value[0] == '\'' && value[len - 1U] == '\'')))
    {
        value[len - 1U] = '\0';
        value++;
    }
    return value;
}

/**
 *  @brief          対話コマンドのヘルプを表示する。
 */
static void print_interactive_help(void)
{
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT, "コマンド:\n");
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                  "  send [-c|--compress] <message>  テキストを送信します。\n");
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                  "  file [-c|--compress] <path>     ファイルを送信します。\n");
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                  "  help                            このヘルプを表示します。\n");
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                  "  exit, quit                      終了します。\n");
}

static int read_file_data(const char *path, unsigned char **out_data, size_t *out_len);

/**
 *  @brief          対話コマンド 1 行を処理する。
 *  @param[in]      handle サービスハンドル。
 *  @param[in,out]  line   入力行。解析中に一部を書き換えます。
 *  @return         1: 継続、0: 終了、-1: 送信失敗。
 */
static int process_interactive_command(PotrHandle handle, char *line)
{
    char *cursor;
    char *command;
    char *payload;
    unsigned char *file_data = NULL;
    size_t file_len = 0;
    const void *send_data;
    size_t send_len;
    int compress = 0;
    int is_file;
    const char *compress_label;
    unsigned int send_flags;
    int send_rtc;

    cursor = line;
    command = next_token(&cursor);
    if (command == NULL)
    {
        return 1;
    }

    if (strcmp(command, "help") == 0)
    {
        print_interactive_help();
        return 1;
    }
    if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0)
    {
        return 0;
    }

    if (strcmp(command, "send") != 0 && strcmp(command, "file") != 0)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: 不明なコマンドです: %s\n", command);
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "help でコマンド一覧を表示します。\n");
        return 1;
    }

    is_file = (strcmp(command, "file") == 0);
    payload = skip_spaces(cursor);
    if (strncmp(payload, "-c", 2) == 0
        && (payload[2] == '\0' || payload[2] == ' ' || payload[2] == '\t'))
    {
        compress = 1;
        payload = skip_spaces(payload + 2);
    }
    else if (strncmp(payload, "--compress", 10) == 0
             && (payload[10] == '\0' || payload[10] == ' ' || payload[10] == '\t'))
    {
        compress = 1;
        payload = skip_spaces(payload + 10);
    }

    if (is_file)
    {
        payload = strip_matching_quotes(payload);
    }
    else
    {
        trim_right(payload);
    }

    if (payload[0] == '\0')
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: %s の引数を指定してください。\n", command);
        return 1;
    }

    if (is_file)
    {
        if (read_file_data(payload, &file_data, &file_len) != 0)
        {
            return 1;
        }
        send_data = file_data;
        send_len = file_len;
    }
    else
    {
        send_data = payload;
        send_len = strlen(payload);
    }

    if (compress)
    {
        compress_label = " [圧縮あり]";
    }
    else
    {
        compress_label = "";
    }
    if (is_file)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                      "ファイル送信中: \"%s\" (%zu バイト)%s\n", payload, send_len, compress_label);
    }
    else
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                      "送信中: \"%s\" (%zu バイト)%s\n", payload, send_len, compress_label);
    }

    if (compress)
    {
        send_flags = POTR_SEND_COMPRESS;
    }
    else
    {
        send_flags = 0;
    }
    send_rtc = potrSend(handle, POTR_PEER_NA, send_data, send_len,
                        send_flags | POTR_SEND_BLOCKING);
    if (send_rtc != POTR_SUCCESS)
    {
        if (send_rtc == POTR_ERROR_DISCONNECTED)
        {
            com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                          "エラー: 未接続のため送信できません。\n");
        }
        else
        {
            com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                          "エラー: 送信に失敗しました。\n");
        }
        free(file_data);
        return -1;
    }

    if (is_file)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                      "ファイル送信完了: \"%s\" (%zu バイト)\n", payload, send_len);
    }
    else
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT, "送信完了。\n");
    }

    free(file_data);
    return 1;
}

/**
 *  @brief          ファイルをバイナリモードで読み込む。
 *  @param[in]      path        ファイルパス。
 *  @param[out]     out_data    読み込んだデータの格納先 (malloc 確保、呼び出し元が free する)。
 *  @param[out]     out_len     読み込んだデータのバイト数の格納先。
 *  @return         成功時は 0、失敗時は -1 を返します。
 *                  失敗時はエラーメッセージを stderr に出力します。
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
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: ファイル \"%s\" を開けませんでした。\n", path);
        return -1;
    }

    if (com_util_fseek(fp, 0, SEEK_END) != 0)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: ファイルの読み込みに失敗しました。\n");
        com_util_fclose(fp);
        return -1;
    }

    file_size = com_util_ftell(fp);
    if (file_size < 0)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: ファイルの読み込みに失敗しました。\n");
        com_util_fclose(fp);
        return -1;
    }

    if (file_size == 0)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: ファイルが空です。\n");
        com_util_fclose(fp);
        return -1;
    }

    if ((uint64_t)file_size > POTR_MAX_MESSAGE_SIZE)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: ファイルサイズ (%" PRId64 " バイト) が最大送信サイズ (%u バイト) を超えています。\n",
                                      file_size, (unsigned)POTR_MAX_MESSAGE_SIZE);
        com_util_fclose(fp);
        return -1;
    }

    buf = (unsigned char *)malloc((size_t)file_size);
    if (buf == NULL)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: メモリ確保に失敗しました。\n");
        com_util_fclose(fp);
        return -1;
    }

    com_util_rewind(fp);
    read_count = com_util_fread(buf, 1, (size_t)file_size, fp);
    com_util_fclose(fp);

    if (read_count != (size_t)file_size)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: ファイルの読み込みに失敗しました。\n");
        free(buf);
        return -1;
    }

    *out_data = buf;
    *out_len = (size_t)file_size;
    return 0;
}

/**
 *  @brief          メインエントリーポイント。
 *  @param[in]      argc コマンドライン引数の数。
 *  @param[in]      argv コマンドライン引数の配列。
 *  @return         成功時は EXIT_SUCCESS、失敗時は EXIT_FAILURE を返します。
 */
int main(int argc, char *argv[])
{
    const char *config_path;
    int64_t service_id;
    PotrHandle handle;
    char line[INPUT_BUF_SIZE];
    int ret = EXIT_SUCCESS;
    int i;
    com_util_trace_level_t trace_level = COM_UTIL_TRACE_LEVEL_NONE;
    int trace_level_set = 0;
    PotrType svc_type;
    int is_bidir;
    PotrRecvCallback callback;

    com_util_console_init();

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
            if (!parse_trace_level(argv[i], &trace_level))
            {
                fprintf(stderr,
                        "エラー: 不明なログレベル \"%s\"。"
                        "VERBOSE/INFO/WARNING/ERROR/CRITICAL のいずれかを指定してください。\n",
                        argv[i]);
                return EXIT_FAILURE;
            }
            trace_level_set = 1;
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
    g_running = 1;
    g_shutdown_requested = 0;

    g_screen = com_util_pinned_prompt_create(NULL);
    if (g_screen == NULL)
    {
        fprintf(stderr, "エラー: プロンプトの初期化に失敗しました。\n");
        return EXIT_FAILURE;
    }

    /* トレーサー設定 (フック経由コンソール出力) */
    if (trace_level_set)
    {
        com_util_tracer_t *tracer = potrGetTracer();
        com_util_tracer_set_hook(tracer, trace_console_hook, &trace_level);
        com_util_tracer_start(tracer);
    }

    if (com_util_shutdown_request_register(send_shutdown_request_callback, NULL) != 0)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: 終了要求 callback の登録に失敗しました。\n");
        com_util_pinned_prompt_dispose(g_screen);
        return EXIT_FAILURE;
    }

    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                  "サービス %" PRId64 " を開いています... (設定: %s)\n", service_id, config_path);

    /* サービス種別を取得して双方向サービスかどうか判定する */
    is_bidir = 0;
    if (potrGetServiceType(config_path, service_id, &svc_type) == POTR_SUCCESS
        && (svc_type == POTR_TYPE_UNICAST_BIDIR || svc_type == POTR_TYPE_TCP_BIDIR))
    {
        is_bidir = 1;
    }

    /* 双方向サービスの場合は接続状態と受信データ表示のためコールバックを設定する */
    if (is_bidir)
    {
        callback = on_recv;
    }
    else
    {
        callback = NULL;
    }

    if (potrOpenServiceFromConfig(config_path, service_id, POTR_ROLE_SENDER, callback, &handle) != POTR_SUCCESS)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: サービス %" PRId64 " を開けませんでした。\n", service_id);
        com_util_pinned_prompt_dispose(g_screen);
        return EXIT_FAILURE;
    }

    if (is_bidir)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                      "双方向モード。相手からの受信メッセージと接続状態も表示します。\n");
    }
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                  "送信準備完了。Ctrl+C または Ctrl+D で終了します。\n");
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                  "help でコマンド一覧を表示します。\n");

    while (g_running)
    {
        int command_result;
        if (com_util_pinned_prompt_readline_fmt(g_screen, line, sizeof(line),
                                                "porter-send[%" PRId64 "]> ", service_id) == 0)
        {
            break;
        }
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                      "porter-send[%" PRId64 "]> %s\n", service_id, line);

        command_result = process_interactive_command(handle, line);
        if (command_result == 0)
        {
            break;
        }
        if (command_result < 0)
        {
            ret = EXIT_FAILURE;
            break;
        }
    }

    if (g_shutdown_requested)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT, "\n終了中...\n");
    }

    potrCloseService(handle);
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT, "終了しました。\n");
    com_util_pinned_prompt_dispose(g_screen);
    return ret;
}
