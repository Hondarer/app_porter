/**
 *******************************************************************************
 *  @file           porter-test.c
 *  @brief          porter の送受信動作を検証するコマンドを実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/06/23
 *  @version        1.0.0
 *
 *  porter ライブラリの送受信を対話式に確認する CLI テスト コマンドです。\n
 *  送信者 (sender) と受信者 (receiver) の双方を 1 つのコマンドで扱います。\n
 *  \n
 *  ロールはコマンド ライン引数または対話コマンド open で指定します。\n
 *  対話コンソールでは open でサービスを開き、close で閉じ、別ロールへ切り替えられます。\n
 *  \n
 *  サービス種別が unicast_bidir または tcp_bidir の場合は双方向モードで動作します。\n
 *  双方向モードでは相手から受信したメッセージと接続状態を表示します。\n
 *  受信データがバイナリと判定された場合は一時ファイルに保存してパスを表示します。
 *
 *  @par            使用方法
    @code{.sh}
    porter-test [-l <level>] [<role> <config_path> <service_id>]
    @endcode
 *
 *  @par            オプション
 *  | オプション       | 説明                                                        |
 *  | ---------------- | ----------------------------------------------------------- |
 *  | -l \<level\>     | ログレベルを指定します。指定がない場合はログ出力なし。      |
 *
 *  role に指定可能な値: sender, receiver\n
 *  level に指定可能な値: VERBOSE, INFO, WARNING, ERROR, CRITICAL (大文字小文字不問)\n
 *  位置引数 (role/config_path/service_id) を省略した場合は対話コンソールで open を実行します。
 *
 *  @par            使用例
    @code{.sh}
    porter-test sender porter-services.conf 10
    porter-test receiver porter-services.conf 10
    porter-test -l INFO sender porter-services.conf 10
    porter-test
    @endcode
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/argparser/argparser.h>
#include <com_util/base/platform.h>
#include <com_util/crt/path.h>
#include <com_util/crt/stdio.h>
#include <com_util/crt/unistd.h>
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

/** 入力バッファー サイズ。POTR_MAX_MESSAGE_SIZE + 改行 + NUL。 */
#define INPUT_BUF_SIZE (POTR_MAX_MESSAGE_SIZE + 2U)
/** プロンプト状態文字列のバッファー サイズ。 */
#define PROMPT_STATE_SIZE 32

/** REPL 継続フラグ。終了要求 callback で 0 に設定される。 */
static volatile int g_running = 1;
/** 終了要求の受信有無。main 側の表示制御に使う。 */
static volatile sig_atomic_t g_shutdown_requested = 0;
/** pinned prompt ハンドル。on_recv や trace hook から参照する。 */
static com_util_pinned_prompt *g_screen = NULL;
/** トレース閾値レベル。trace hook が context として参照するため寿命を持続させる。 */
static com_util_trace_level_t g_trace_level = COM_UTIL_TRACE_LEVEL_NONE;
/** トレーサーへの hook 設定と開始を実施済みかどうか。 */
static int g_tracer_started = 0;

/** 対話セッションの状態。1 セッションで 1 サービスを保持する。 */
typedef struct PorterTestSession
{
    PotrContext *handle; /**< サービス ハンドル。未オープン時は NULL。 */
    int64_t service_id;  /**< 開いているサービスの ID。 */
    PotrRole role;       /**< 開いているサービスのロール。 */
    int is_open;         /**< サービスを開いているかどうか。 */
    int is_bidir;        /**< 双方向サービスかどうか。 */
    int can_send;        /**< このロールで送信できるかどうか。 */
} PorterTestSession;

/**
 *  @brief          データがテキストかバイナリかを判定します。
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
static void porter_test_shutdown_request_callback(const com_util_shutdown_event *event, void *context)
{
    (void)event;
    (void)context;
    g_shutdown_requested = 1;
    g_running = 0;

#if defined(PLATFORM_LINUX)
    com_util_close(STDIN_FILENO); /* readline (fgets) のブロックを解除する */
#endif                            /* PLATFORM_LINUX */
}

/**
 *  @brief          受信コールバック関数。
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
    int path_idx;

    (void)peer_id;
    switch (event)
    {
    case POTR_EVENT_CONNECTED:
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                      "[サービス %" PRId64 "] 接続確立\n", service_id);
        break;

    case POTR_EVENT_DISCONNECTED:
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                      "[サービス %" PRId64 "] 切断検知\n", service_id);
        break;

    case POTR_EVENT_PATH_CONNECTED:
    case POTR_EVENT_PATH_DISCONNECTED:
    {
        const char *event_str;
        int path_state0;
        int path_state1;
        int path_state2;
        int path_state3;

        path_states = (const int *)data;
        path_idx = (int)len;
        if (event == POTR_EVENT_PATH_CONNECTED)
        {
            event_str = "CONNECTED";
        }
        else
        {
            event_str = "DISCONNECTED";
        }
        if (path_states != NULL)
        {
            path_state0 = path_states[0];
            path_state1 = path_states[1];
            path_state2 = path_states[2];
            path_state3 = path_states[3];
        }
        else
        {
            path_state0 = 0;
            path_state1 = 0;
            path_state2 = 0;
            path_state3 = 0;
        }
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                      "[サービス %" PRId64 "] path[%d] %s states={%d,%d,%d,%d}\n", service_id, path_idx,
                                      event_str, path_state0, path_state1, path_state2, path_state3);
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
                                          "[サービス %" PRId64 "] 受信 (%zu バイト): %s\n", service_id, len, buf);
        }
        else
        {
            char tmp_path[PLATFORM_PATH_MAX];
            FILE *fp = com_util_fopen_temp("ptr", "wb", tmp_path, sizeof(tmp_path), NULL);

            if (fp != NULL && com_util_fwrite(data, 1, len, fp) == len)
            {
                com_util_fclose(fp);
                com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                              "[サービス %" PRId64
                                              "] 受信 (%zu バイト): バイナリ データを保存しました: %s\n",
                                              service_id, len, tmp_path);
            }
            else
            {
                if (fp != NULL)
                {
                    com_util_fclose(fp);
                }
                com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                              "[サービス %" PRId64
                                              "] 受信 (%zu バイト): バイナリ データの保存に失敗しました。\n",
                                              service_id, len);
            }
        }
        break;
    }
}

/**
 *  @brief          トレース フック コールバック。指定レベル以上のメッセージを stderr へ出力します。
 *  @param[in]      prev      チェーン継続用の前エントリ。
 *  @param[in]      handle    trace を行った tracer ハンドル。
 *  @param[in]      level     trace レベル。
 *  @param[in]      timestamp 解決済みタイムスタンプ。
 *  @param[in]      message   解決済みメッセージ文字列。
 *  @param[in]      context   閾値レベル (com_util_trace_level_t *) を指すポインター。
 */
static void trace_console_hook(com_util_tracer_hook_entry *prev, com_util_tracer *handle, com_util_trace_level_t level,
                               const com_util_timespec *timestamp, const char *message, void *context)
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
        com_util_format_realtime_iso8601_local(ts, sizeof(ts), timestamp);
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR, "%s %c %s\n", ts, lc, message);
    }
    com_util_tracer_call_next_hook(prev, handle, level, timestamp, message);
}

/**
 *  @brief          ログ レベル文字列を com_util_trace_level_t に変換します。
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
        {"VERBOSE", COM_UTIL_TRACE_LEVEL_VERBOSE, 0U},   {"INFO", COM_UTIL_TRACE_LEVEL_INFO, 0U},
        {"WARNING", COM_UTIL_TRACE_LEVEL_WARNING, 0U},   {"ERROR", COM_UTIL_TRACE_LEVEL_ERROR, 0U},
        {"CRITICAL", COM_UTIL_TRACE_LEVEL_CRITICAL, 0U},
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
 *  @brief          ロール文字列を PotrRole に変換します。
 *  @param[in]      str     ロール文字列 (sender/receiver、大文字小文字不問)。
 *  @param[out]     out     変換結果の格納先。
 *  @return         変換に成功した場合は 1、未知の文字列の場合は 0 を返します。
 */
static int parse_role(const char *str, PotrRole *out)
{
    if (str == NULL)
    {
        return 0;
    }
    if (strcmp(str, "sender") == 0 || strcmp(str, "SENDER") == 0)
    {
        *out = POTR_ROLE_SENDER;
        return 1;
    }
    if (strcmp(str, "receiver") == 0 || strcmp(str, "RECEIVER") == 0)
    {
        *out = POTR_ROLE_RECEIVER;
        return 1;
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
 *  @brief          文字列末尾の空白を削除します。
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
 *  @brief          次の空白区切りトークンを取得します。
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
    if (len >= 2U && ((value[0] == '"' && value[len - 1U] == '"') || (value[0] == '\'' && value[len - 1U] == '\'')))
    {
        value[len - 1U] = '\0';
        value++;
    }
    return value;
}

/**
 *  @brief          対話コマンドのヘルプを表示します。
 */
static void print_interactive_help(void)
{
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT, "コマンド:\n");
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                  "  open <role> <config_path> <service_id>  サービスを開きます。\n");
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                  "                                          role: sender | receiver\n");
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                  "  close                                   サービスを閉じます。\n");
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                  "  send [-c|--compress] <message>          テキストを送信します。\n");
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                  "  file [-c|--compress] <path>             ファイルを送信します。\n");
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                  "  log <level>                             ログレベルを設定します。\n");
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                  "  help                                    このヘルプを表示します。\n");
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                  "  exit, quit                              終了します。\n");
}

/**
 *  @brief          ファイルをバイナリ モードで読み込む。
 *  @param[in]      path        ファイル パス。
 *  @param[out]     out_data    読み込んだデータの格納先 (malloc 確保、呼び出し元が free する)。
 *  @param[out]     out_len     読み込んだデータのバイト数の格納先。
 *  @return         成功時は 0、失敗時は -1 を返します。
 *                  失敗時はエラー メッセージを stderr に出力します。
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
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR, "エラー: ファイルが空です。\n");
        com_util_fclose(fp);
        return -1;
    }

    if ((uint64_t)file_size > POTR_MAX_MESSAGE_SIZE)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: ファイルサイズ (%" PRId64
                                      " バイト) が最大送信サイズ (%u バイト) を超えています。\n",
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
 *  @brief          send / file コマンドを処理して 1 件送信します。
 *  @param[in]      handle  サービス ハンドル。
 *  @param[in]      is_file ファイル送信の場合は 1、テキスト送信の場合は 0。
 *  @param[in,out]  cursor  コマンド名の次を指す解析位置。解析中に一部を書き換えます。
 *
 *  送信に失敗してもエラーを表示するのみで REPL は継続します。
 */
static void apply_send_command(PotrContext *handle, int is_file, char *cursor)
{
    char *payload;
    unsigned char *file_data = NULL;
    size_t file_len = 0;
    const void *send_data;
    size_t send_len;
    int compress = 0;
    const char *compress_label;
    unsigned int send_flags;
    int send_rtc;

    payload = skip_spaces(cursor);
    if (strncmp(payload, "-c", 2) == 0 && (payload[2] == '\0' || payload[2] == ' ' || payload[2] == '\t'))
    {
        compress = 1;
        payload = skip_spaces(payload + 2);
    }
    else if (strncmp(payload, "--compress", 10) == 0 &&
             (payload[10] == '\0' || payload[10] == ' ' || payload[10] == '\t'))
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
                                      "エラー: 送信内容を指定してください。\n");
        return;
    }

    if (is_file)
    {
        if (read_file_data(payload, &file_data, &file_len) != 0)
        {
            return;
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
    send_rtc = potrSend(handle, POTR_PEER_NA, send_data, send_len, send_flags | POTR_SEND_BLOCKING);
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
        return;
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
}

/**
 *  @brief          トレーサーの hook 設定と開始を一度だけ実施します。
 *
 *  閾値は g_trace_level を参照するため、設定後の log コマンドによる変更も反映されます。
 */
static void ensure_tracer_started(void)
{
    if (!g_tracer_started)
    {
        com_util_tracer *tracer = potrGetTracer();
        com_util_tracer_set_hook(tracer, trace_console_hook, &g_trace_level);
        com_util_tracer_start(tracer);
        g_tracer_started = 1;
    }
}

/**
 *  @brief          サービスを開きます。
 *  @param[in,out]  session     セッション状態。
 *  @param[in]      role        ロール。
 *  @param[in]      config_path 設定ファイルのパス。
 *  @param[in]      service_id  サービスの ID。
 *  @return         成功時は 0、失敗時は -1 を返します。
 */
static int do_open(PorterTestSession *session, PotrRole role, const char *config_path, int64_t service_id)
{
    PotrType svc_type;
    int is_bidir = 0;
    int can_send = 0;
    PotrRecvCallback callback = NULL;
    PotrContext *handle;

    if (session->is_open)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: 既にサービスを開いています。先に close してください。\n");
        return -1;
    }

    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                  "サービス %" PRId64 " を開いています... (設定: %s)\n", service_id, config_path);

    /* サービス種別を取得して双方向サービスかどうか判定する。 */
    /* sender は unicast_bidir / tcp_bidir の双方を、receiver は unicast_bidir を双方向として扱う */
    /* (送受信コマンド統合前の send / recv それぞれの判定を踏襲)。 */
    if (potrGetServiceType(config_path, service_id, &svc_type) == POTR_SUCCESS)
    {
        if (role == POTR_ROLE_SENDER)
        {
            if (svc_type == POTR_TYPE_UNICAST_BIDIR || svc_type == POTR_TYPE_TCP_BIDIR)
            {
                is_bidir = 1;
            }
        }
        else
        {
            if (svc_type == POTR_TYPE_UNICAST_BIDIR)
            {
                is_bidir = 1;
            }
        }
    }

    /* receiver は受信のためコールバック必須。sender は双方向時のみコールバックを設定する。 */
    if (role == POTR_ROLE_RECEIVER)
    {
        callback = on_recv;
        can_send = is_bidir;
    }
    else
    {
        can_send = 1;
        if (is_bidir)
        {
            callback = on_recv;
        }
    }

    if (potrOpenServiceFromConfig(config_path, service_id, role, callback, &handle) != POTR_SUCCESS)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: サービス %" PRId64 " を開けませんでした。\n", service_id);
        return -1;
    }

    session->handle = handle;
    session->service_id = service_id;
    session->role = role;
    session->is_open = 1;
    session->is_bidir = is_bidir;
    session->can_send = can_send;

    if (is_bidir)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                      "双方向モード。相手からの受信メッセージと接続状態も表示します。\n");
    }

    if (role == POTR_ROLE_SENDER)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                      "送信準備完了。Ctrl+C または Ctrl+D で終了します。\n");
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                      "help でコマンド一覧を表示します。\n");
    }
    else
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                      "受信待機中... (Ctrl+C で終了)\n");
        if (can_send)
        {
            com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                          "send / file で送信もできます。help でコマンド一覧を表示します。\n");
        }
    }
    return 0;
}

/**
 *  @brief          サービスを閉じます。
 *  @param[in,out]  session セッション状態。
 */
static void do_close(PorterTestSession *session)
{
    if (!session->is_open)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: サービスは開いていません。\n");
        return;
    }

    potrCloseService(session->handle);
    session->handle = NULL;
    session->is_open = 0;
    session->is_bidir = 0;
    session->can_send = 0;
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT, "サービスを閉じました。\n");
}

/**
 *  @brief          open コマンドを処理します。
 *  @param[in,out]  session セッション状態。
 *  @param[in,out]  cursor  コマンド名の次を指す解析位置。
 */
static void handle_open_command(PorterTestSession *session, char *cursor)
{
    char *role_token;
    char *config_token;
    char *id_token;
    PotrRole role;
    int64_t service_id;

    role_token = next_token(&cursor);
    config_token = next_token(&cursor);
    id_token = next_token(&cursor);

    if (role_token == NULL || config_token == NULL || id_token == NULL)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: open <role> <config_path> <service_id> を指定してください。\n");
        return;
    }
    if (!parse_role(role_token, &role))
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: 不明なロール \"%s\"。sender または receiver を指定してください。\n",
                                      role_token);
        return;
    }

    service_id = (int64_t)strtoll(id_token, NULL, 10);
    do_open(session, role, config_token, service_id);
}

/**
 *  @brief          log コマンドを処理します。
 *  @param[in,out]  cursor コマンド名の次を指す解析位置。
 */
static void handle_log_command(char *cursor)
{
    char *level_token;
    com_util_trace_level_t level;

    level_token = next_token(&cursor);
    if (level_token == NULL)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: log <level> を指定してください。\n");
        return;
    }
    if (!parse_trace_level(level_token, &level))
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: 不明なログレベル \"%s\"。"
                                      "VERBOSE/INFO/WARNING/ERROR/CRITICAL のいずれかを指定してください。\n",
                                      level_token);
        return;
    }

    g_trace_level = level;
    ensure_tracer_started();
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT, "ログレベルを %s に設定しました。\n",
                                  level_token);
}

/**
 *  @brief          対話コマンド 1 行を処理します。
 *  @param[in,out]  session セッション状態。
 *  @param[in,out]  line    入力行。解析中に一部を書き換えます。
 *  @return         1: 継続、0: 終了。
 */
static int process_line(PorterTestSession *session, char *line)
{
    char *cursor;
    char *command;
    int is_file;

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
    if (strcmp(command, "open") == 0)
    {
        handle_open_command(session, cursor);
        return 1;
    }
    if (strcmp(command, "close") == 0)
    {
        do_close(session);
        return 1;
    }
    if (strcmp(command, "log") == 0)
    {
        handle_log_command(cursor);
        return 1;
    }

    if (strcmp(command, "send") == 0 || strcmp(command, "file") == 0)
    {
        if (!session->is_open)
        {
            com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                          "エラー: サービスが開いていません。"
                                          "open <role> <config_path> <service_id> で開いてください。\n");
            return 1;
        }
        if (!session->can_send)
        {
            com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                          "エラー: 受信専用サービスのため送信できません。\n");
            return 1;
        }
        is_file = (strcmp(command, "file") == 0);
        apply_send_command(session->handle, is_file, cursor);
        return 1;
    }

    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR, "エラー: 不明なコマンドです: %s\n",
                                  command);
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                  "help でコマンド一覧を表示します。\n");
    return 1;
}

/**
 *  @brief          プロンプトに表示する状態文字列を生成します。
 *  @param[in]      session セッション状態。
 *  @param[out]     buf     格納先バッファー。
 *  @param[in]      size    バッファーのサイズ。
 */
static void build_prompt_state(const PorterTestSession *session, char *buf, size_t size)
{
    if (!session->is_open)
    {
        snprintf(buf, size, "closed");
    }
    else if (session->role == POTR_ROLE_SENDER)
    {
        snprintf(buf, size, "sender:%" PRId64, session->service_id);
    }
    else
    {
        snprintf(buf, size, "receiver:%" PRId64, session->service_id);
    }
}

/**
 *  @brief          メイン エントリ ポイント。
 *  @param[in]      argc コマンド ライン引数の数。
 *  @param[in]      argv コマンド ライン引数の配列。
 *  @return         成功時は EXIT_SUCCESS、失敗時は EXIT_FAILURE を返します。
 */
int main(int argc, char *argv[])
{
    char line[INPUT_BUF_SIZE];
    char prompt_state[PROMPT_STATE_SIZE];
    const char *role_arg = NULL;
    const char *config_path_arg = NULL;
    const char *service_id_arg = NULL;
    int positional_count = 0;
    PorterTestSession session;

    com_util_console_init();

    session.handle = NULL;
    session.service_id = 0;
    session.role = POTR_ROLE_SENDER;
    session.is_open = 0;
    session.is_bidir = 0;
    session.can_send = 0;

    int need_help = 0;
    const char *log_level = NULL;

    com_util_argparser_init("porter の送受信動作を対話的に検証します。");
    com_util_argparser_register_flag("-h", "--help", "ヘルプを表示します。", &need_help);
    com_util_argparser_register_option_string("-l", NULL, "level", "ログレベル。", 0, &log_level);
    com_util_argparser_register_positional_string("role", "sender または receiver。", 0, &role_arg);
    com_util_argparser_register_positional_string("config_path", "設定ファイル。", 0, &config_path_arg);
    com_util_argparser_register_positional_string("service_id", "サービス ID。", 0, &service_id_arg);

    if (com_util_argparser_get_register_error_count() > 0)
    {
        com_util_argparser_print_register_error_messages(stderr);
        return EXIT_FAILURE;
    }

    int parse_result = com_util_argparser_parse(argc, argv);

    if (need_help != 0)
    {
        com_util_argparser_print_usage(stdout);
        return EXIT_SUCCESS;
    }

    if (parse_result != COM_UTIL_ARGPARSER_OK)
    {
        com_util_argparser_print_error_messages(stderr);
        com_util_argparser_print_usage(stderr);
        return EXIT_FAILURE;
    }

    if (role_arg != NULL)
    {
        positional_count++;
    }
    if (config_path_arg != NULL)
    {
        positional_count++;
    }
    if (service_id_arg != NULL)
    {
        positional_count++;
    }

    if (log_level != NULL)
    {
        if (!parse_trace_level(log_level, &g_trace_level))
        {
            fprintf(stderr,
                    "エラー: 不明なログレベル \"%s\"。"
                    "VERBOSE/INFO/WARNING/ERROR/CRITICAL のいずれかを指定してください。\n\n",
                    log_level);
            com_util_argparser_print_usage(stderr);
            return EXIT_FAILURE;
        }
        g_tracer_started = 0; /* 後段の ensure_tracer_started で開始する。 */
        ensure_tracer_started();
    }

    if (positional_count != 0 && positional_count != 3)
    {
        fprintf(stderr, "エラー: role、config_path、service_id は 3 個すべてを指定してください。\n\n");
        com_util_argparser_print_usage(stderr);
        return EXIT_FAILURE;
    }

    g_running = 1;
    g_shutdown_requested = 0;

    g_screen = com_util_pinned_prompt_create(NULL);
    if (g_screen == NULL)
    {
        fprintf(stderr, "エラー: プロンプトの初期化に失敗しました。\n");
        return EXIT_FAILURE;
    }

    if (com_util_shutdown_request_register(porter_test_shutdown_request_callback, NULL) != 0)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                      "エラー: 終了要求 callback の登録に失敗しました。\n");
        com_util_pinned_prompt_dispose(g_screen);
        return EXIT_FAILURE;
    }

    /* 位置引数が指定された場合は起動時に open を実行する。失敗時も対話コンソールへ移行する。 */
    if (positional_count == 3)
    {
        PotrRole role;
        if (!parse_role(role_arg, &role))
        {
            com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDERR,
                                          "エラー: 不明なロール \"%s\"。sender または receiver を指定してください。\n",
                                          role_arg);
        }
        else
        {
            int64_t service_id = (int64_t)strtoll(service_id_arg, NULL, 10);
            do_open(&session, role, config_path_arg, service_id);
        }
    }
    else
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT,
                                      "open <role> <config_path> <service_id> でサービスを開きます。"
                                      "help でコマンド一覧を表示します。\n");
    }

    while (g_running)
    {
        build_prompt_state(&session, prompt_state, sizeof(prompt_state));
        if (com_util_pinned_prompt_readline_fmt(g_screen, line, sizeof(line), "porter-test[%s]> ", prompt_state) == 0)
        {
            break;
        }
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT, "porter-test[%s]> %s\n",
                                      prompt_state, line);

        if (process_line(&session, line) == 0)
        {
            break;
        }
    }

    if (g_shutdown_requested)
    {
        com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT, "\n終了中...\n");
    }

    if (session.is_open)
    {
        potrCloseService(session.handle);
        session.is_open = 0;
    }
    com_util_pinned_prompt_printf(g_screen, COM_UTIL_PINNED_PROMPT_CHANNEL_STDOUT, "終了しました。\n");
    com_util_pinned_prompt_dispose(g_screen);
    return EXIT_SUCCESS;
}
