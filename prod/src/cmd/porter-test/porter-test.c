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
 *  受信データがバイナリと判定された場合は一時ファイルに保存してパスを表示します。\n
 *  \n
 *  フロー制御の確認のため、受信コールバックを遅らせる指定 (--recv-delay-ms) と、
 *  通し番号つきのメッセージを連続して送信する対話コマンド burst を備えます。\n
 *  burst のメッセージを受けた側は、通し番号の順序を検証し、集計を 1 行で表示します。
 *
 *  @par            使用方法
    @code{.sh}
    porter-test [-l <level>] [--recv-delay-ms <ms>] [<role> <config_path> <service_id>]
    @endcode
 *
 *  @par            オプション
 *  | オプション                | 説明                                                                               |
 *  | ------------------------- | ---------------------------------------------------------------------------------- |
 *  | -l \<level\>              | ログレベルを指定します。指定がない場合はログ出力なし。                             |
 *  | --recv-delay-ms \<ms\>    | POTR_EVENT_DATA を受けるたびに、指定した時間 [ms] だけ待ちます。0 から 600000。既定は 0。 |
 *
 *  Table: porter-test のコマンドライン オプション
 *
 *  role に指定可能な値: sender, receiver\n
 *  level に指定可能な値: VERBOSE, INFO, WARNING, ERROR, CRITICAL (大文字小文字不問)\n
 *  位置引数 (role/config_path/service_id) を省略した場合は対話コンソールで open を実行します。
 *
 *  @par            burst コマンドと表示
 *  対話コマンド `burst [-b|--blocking] <count> <bytes>` は、通し番号 (0 始まり) と総件数を先頭に入れた
 *  bytes [byte] のメッセージを、count 件、間隔なしで送ります。bytes は 16 以上です。\n
 *  既定は POTR_SEND_BLOCKING を使わない送信で、POTR_ERR_FULL を受けた場合は 1 [ms] 待って送り直し、
 *  送り直した回数を数えます。-b を指定すると POTR_SEND_BLOCKING を使います。\n
 *  終わると、送信側は次の 1 行を表示します。\n
 *  `BURST_SENT sent=<n> expected=<n> full=<n> elapsed_ms=<n> bytes=<n> blocking=<0|1> result=<ok|full_timeout|interrupted|error=<code>>`\n
 *  受信側は、最後のメッセージ (通し番号 = 総件数 - 1) を受けたとき、次の 1 行を表示します。\n
 *  `BURST_RESULT received=<n> expected=<n> out_of_order=<n>`\n
 *  途中の経過は、1000 件ごとに `BURST_PROGRESS received=<n> expected=<n> out_of_order=<n>` で表示します。\n
 *  out_of_order は、期待した通し番号と一致しなかったメッセージの数です。
 *
 *  @par            使用例
    @code{.sh}
    porter-test sender porter-services.jsonc 10
    porter-test receiver porter-services.jsonc 10
    porter-test -l INFO sender porter-services.jsonc 10
    porter-test --recv-delay-ms 5 -l INFO receiver porter-services.jsonc 10
    porter-test
    @endcode
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/argparser/argparser.h>
#include <cplat/crt/stdlib.h>
#include <cplat/base/platform.h>
#include <cplat/clock/clock.h>
#include <cplat/crt/path.h>
#include <cplat/crt/stdio.h>
#include <cplat/prompt/pinned_prompt.h>
#include <cplat/runtime/shutdown.h>
#include <cplat/sync/atomic.h>
#include <cplat/sync/sync.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cplat/console/console.h>
#include <porter.h>
#include <porter/porter_result.h>

/** 入力バッファー サイズ。POTR_MAX_MESSAGE_SIZE + 改行 + NUL。 */
#define INPUT_BUF_SIZE (POTR_MAX_MESSAGE_SIZE + 2U)
/** プロンプト状態文字列のバッファー サイズ。 */
#define PROMPT_STATE_SIZE 32

/** 受信コールバックを遅らせる時間の上限 [ms]。 */
#define RECV_DELAY_MAX_MS 600000
/** burst のメッセージの先頭に入れる識別子 (8 [byte])。 */
#define BURST_MAGIC "PTBURST1"
/** burst の識別子の長さ [byte]。 */
#define BURST_MAGIC_SIZE 8U
/** burst のメッセージのヘッダー長 [byte]。識別子 8、通し番号 4、総件数 4 (数値はリトル エンディアン)。 */
#define BURST_HEADER_SIZE 16U
/** burst で指定できる件数の上限。 */
#define BURST_MAX_COUNT 1000000
/** burst で POTR_ERR_FULL を受けたときの、送り直しまでの待ち時間 [ms]。 */
#define BURST_RETRY_WAIT_MS 1
/** burst で 1 件が POTR_ERR_FULL のまま進まない場合に、送信を打ち切るまでの時間 [ms]。 */
#define BURST_STALL_LIMIT_MS 60000U
/** 受信側が burst の経過を表示する件数の間隔。 */
#define BURST_PROGRESS_INTERVAL 1000U

/** REPL 継続フラグ。終了要求 callback で 0 に設定される。 */
static cplat_atomic_i32 s_running = CPLAT_ATOMIC_INIT(1);

/** 終了要求の受信有無。main 側の表示制御に使う。 */
static volatile sig_atomic_t s_shutdown_requested = 0;

/** pinned prompt ハンドル。on_recv や trace hook から参照する。 */
static cplat_pinned_prompt *s_screen = NULL;

/** 終了要求 callback と画面破棄の競合を防ぐ永続ロックの 1 回限り初期化フラグ。 */
static cplat_once_flag s_screen_stop_lock_once = {0};

/** 終了要求 callback の登録解除 API がなく main 終了処理後も参照され得るため、同じロック下での s_stop_target_screen NULL 化と直列化するプロセス寿命の永続ロック。 */
static cplat_local_lock *s_screen_stop_lock = NULL;

/** 終了要求 callback が中止対象として参照する画面ハンドル。同じ永続ロック下で破棄開始前に NULL 化する。 */
static cplat_pinned_prompt *s_stop_target_screen = NULL;

/* 登録解除できない終了要求 callback が main 終了処理後も安全に参照できるよう、永続ロックを 1 回だけ生成してプロセス終了まで破棄しない。 */
static void init_screen_stop_lock(void)
{
    if (cplat_local_lock_create(&s_screen_stop_lock) != CPLAT_OK)
    {
        s_screen_stop_lock = NULL;
    }
}

/* 終了要求 callback が使う永続ロックを返す。 */
static cplat_local_lock *get_screen_stop_lock(void)
{
    cplat_call_once(&s_screen_stop_lock_once, init_screen_stop_lock);
    return s_screen_stop_lock;
}

/* 終了要求 callback から参照される画面ハンドルを更新し、破棄直前に NULL 化する。 */
static void set_stop_target_screen(cplat_local_lock *stop_lock, cplat_pinned_prompt *screen)
{
    cplat_local_lock_lock(stop_lock);
    s_stop_target_screen = screen;
    cplat_local_lock_unlock(stop_lock);
}

/** トレースしきい値レベル。trace hook が context として参照するため寿命を持続させる。 */
static cplat_trace_level s_trace_level = CPLAT_TRACE_LEVEL_NONE;

/** トレーサーへの hook 設定と開始を実施済みかどうか。 */
static int s_tracer_started = 0;

/** 受信コールバックを遅らせる時間 [ms]。--recv-delay-ms で設定する。受信スレッドから参照する。 */
static int s_recv_delay_ms = 0;

/** burst のメッセージを受けた側の集計。受信コールバックだけが更新する。 */
typedef struct porter_test_burst_receive
{
    uint32_t expected_total; /**< 先頭のメッセージが示す総件数。 */
    uint32_t next_sequence;  /**< 次に期待する通し番号。 */
    uint32_t received;       /**< 受けた件数。 */
    uint32_t out_of_order;   /**< 期待した通し番号と一致しなかった件数。 */
} porter_test_burst_receive;

/** burst の受信側の集計。 */
static porter_test_burst_receive s_burst_receive;

/** 対話セッションの状態。1 セッションで 1 サービスを保持する。 */
typedef struct porter_test_session
{
    potr_context *handle; /**< サービス ハンドル。未オープン時は NULL。 */
    int64_t service_id;   /**< 開いているサービスの ID。 */
    potr_role role;       /**< 開いているサービスのロール。 */
    int is_open;          /**< サービスを開いているかどうか。 */
    int is_bidir;         /**< 双方向サービスかどうか。 */
    int can_send;         /**< このロールで送信できるかどうか。 */
} porter_test_session;

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
static void porter_test_shutdown_request_callback(const cplat_shutdown_event *event, void *context)
{
    cplat_local_lock *stop_lock;
    int stop_result = CPLAT_OK;

    (void)event;
    (void)context;
    s_shutdown_requested = 1;
    cplat_atomic_store_i32(&s_running, 0, CPLAT_MEMORY_ORDER_RELEASE);

    /* 画面破棄開始前か完了後かを永続ロックで直列化し、dispose 中の stop_input 呼び出しを防ぐ。 */
    stop_lock = get_screen_stop_lock();
    if (stop_lock != NULL)
    {
        cplat_local_lock_lock(stop_lock);
        if (s_stop_target_screen != NULL)
        {
            stop_result = cplat_pinned_prompt_stop_input(s_stop_target_screen);
        }
        cplat_local_lock_unlock(stop_lock);
    }

    if (stop_result != CPLAT_OK)
    {
        /* 診断の出力先がほかにないため、戻り値は明示的に破棄する。 */
        (void)fprintf(stderr, "警告: プロンプト入力の中止に失敗しました (result=%d)。\n", stop_result);
    }
}

/**
 *  @brief          32 ビット整数をリトル エンディアンで書き込みます。
 *  @param[out]     dest    書き込み先 (4 [byte] 以上)。
 *  @param[in]      value   書き込む値。
 */
static void write_u32_le(unsigned char *dest, uint32_t value)
{
    dest[0] = (unsigned char)(value & 0xFFU);
    dest[1] = (unsigned char)((value >> 8) & 0xFFU);
    dest[2] = (unsigned char)((value >> 16) & 0xFFU);
    dest[3] = (unsigned char)((value >> 24) & 0xFFU);
}

/**
 *  @brief          リトル エンディアンの 32 ビット整数を読み取ります。
 *  @param[in]      src     読み取り元 (4 [byte] 以上)。
 *  @return         読み取った値を返します。
 */
static uint32_t read_u32_le(const unsigned char *src)
{
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

/**
 *  @brief          単調クロックの現在値を [ms] で返します。
 *  @return         現在値を返します。取得に失敗した場合は 0 を返します。
 *
 *  経過時間の表示と送信の打ち切り判定だけに使うため、失敗した場合は経過時間が 0 になるだけで、呼び出し元へ通知しません。
 */
static uint64_t monotonic_ms(void)
{
    uint64_t now = 0U;

    if (cplat_clock_get_monotonic_ms(&now) != CPLAT_OK)
    {
        now = 0U;
    }

    return now;
}

/**
 *  @brief          burst のメッセージかどうかを判定します。
 *  @param[in]      data    受信データ。
 *  @param[in]      len     データのバイト数。
 *  @return         burst のメッセージの場合は 1、それ以外は 0 を返します。
 */
static int is_burst_message(const void *data, size_t len)
{
    return len >= BURST_HEADER_SIZE && memcmp(data, BURST_MAGIC, BURST_MAGIC_SIZE) == 0;
}

/**
 *  @brief          burst のメッセージを検証し、集計と表示を更新します。
 *  @param[in]      data    受信データ (is_burst_message が 1 を返したもの)。
 *
 *  通し番号 0 のメッセージで集計を初期化します。\n
 *  期待した通し番号と一致しない場合は out_of_order を数え、受けた通し番号の次を期待し直します。\n
 *  最後のメッセージ (通し番号 = 総件数 - 1) を受けたとき BURST_RESULT を、BURST_PROGRESS_INTERVAL 件ごとに
 *  BURST_PROGRESS を表示します。
 */
static void on_burst_message(const void *data)
{
    const unsigned char *bytes = (const unsigned char *)data;
    const uint32_t sequence = read_u32_le(bytes + BURST_MAGIC_SIZE);
    const uint32_t total = read_u32_le(bytes + BURST_MAGIC_SIZE + 4U);

    if (sequence == 0U)
    {
        s_burst_receive.expected_total = total;
        s_burst_receive.next_sequence = 0U;
        s_burst_receive.received = 0U;
        s_burst_receive.out_of_order = 0U;
    }

    if (sequence != s_burst_receive.next_sequence)
    {
        s_burst_receive.out_of_order++;
    }

    s_burst_receive.next_sequence = sequence + 1U;
    s_burst_receive.received++;

    /*
     * 集計の表示は試験スクリプトと利用者が画面で確認する表示であり、診断も出力先がほかにないため、
     * この関数の cplat_pinned_prompt_printf の戻り値は明示的に破棄する。
     */
    if (total != 0U && sequence == total - 1U)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                         "BURST_RESULT received=%u expected=%u out_of_order=%u\n",
                                         s_burst_receive.received, total, s_burst_receive.out_of_order);
    }
    else if (s_burst_receive.received % BURST_PROGRESS_INTERVAL == 0U)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                         "BURST_PROGRESS received=%u expected=%u out_of_order=%u\n",
                                         s_burst_receive.received, total, s_burst_receive.out_of_order);
    }
}

/**
 *  @brief          受信コールバック関数。
 *  @param[in]      service_id  サービスの ID。
 *  @param[in]      peer_id     ピア識別子 (N:1 モード時は非ゼロ、1:1 モード時は 0)。
 *  @param[in]      event       イベント種別。
 *  @param[in]      data        受信データまたは path 状態配列。
 *  @param[in]      len         受信データ長または path index。
 */
static void on_recv(int64_t service_id, potr_peer_id peer_id, potr_event event, const void *data, size_t len)
{
    char buf[POTR_MAX_PAYLOAD + 1];
    size_t copy_len;
    const int *path_states;
    int path_idx;

    (void)peer_id;

    /* POTR_EVENT_DATA を受けるたびに指定した時間だけ待ち、受信側の処理が遅い状況を作る。 */
    if (event == POTR_EVENT_DATA)
    {
        if (s_recv_delay_ms > 0)
        {
            cplat_sleep_ms(s_recv_delay_ms);
        }

        if (is_burst_message(data, len))
        {
            on_burst_message(data);
            return;
        }
    }

    /*
     * 受信と接続の状態の表示は利用者が画面で確認する表示であり、診断も出力先がほかにないため、
     * この関数の cplat_pinned_prompt_printf の戻り値は明示的に破棄する。
     */
    switch (event)
    {
    case POTR_EVENT_CONNECTED:
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                         "[サービス %" PRId64 "] 接続確立\n", service_id);
        break;

    case POTR_EVENT_DISCONNECTED:
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                         "[サービス %" PRId64 "] 切断検知\n", service_id);
        break;

    case POTR_EVENT_PATH_CONNECTED:
    case POTR_EVENT_PATH_DISCONNECTED:
    {
        const char *event_str;
        char states[POTR_MAX_PATH * 12U + 1U] = {0};
        size_t offset = 0U;
        size_t i;

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

        for (i = 0; i < POTR_MAX_PATH; i++)
        {
            const char *separator = ",";
            int state = 0;
            int written;

            if (i == 0U)
            {
                separator = "";
            }
            if (path_states != NULL)
            {
                state = path_states[i];
            }
            written = cplat_snprintf(states + offset, sizeof(states) - offset, "%s%d", separator, state);
            if (written < 0 || (size_t)written >= sizeof(states) - offset)
            {
                break;
            }
            offset += (size_t)written;
        }

        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                         "[サービス %" PRId64 "] path[%d] %s states={%s}\n", service_id, path_idx,
                                         event_str, states);
        break;
    }

    case POTR_EVENT_ERROR:
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "[サービス %" PRId64 "] 通知継続不能: %d\n", service_id, *(const int *)data);
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
            (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                             "[サービス %" PRId64 "] 受信 (%zu バイト): %s\n", service_id, len, buf);
        }
        else
        {
            char tmp_path[PLATFORM_PATH_MAX] = {0};
            FILE *fp = cplat_fopen_temp("ptr", "wb", tmp_path, sizeof(tmp_path), NULL);

            const int created = fp != NULL;
            int saved = 0;
            if (fp != NULL)
            {
                saved = fwrite(data, 1, len, fp) == len;
                if (fclose(fp) != 0)
                {
                    saved = 0;
                }
                fp = NULL;
            }
            if (saved)
            {
                (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                                 "[サービス %" PRId64
                                                 "] 受信 (%zu バイト): バイナリ データを保存しました: %s\n",
                                                 service_id, len, tmp_path);
            }
            else
            {
                if (created && tmp_path[0] != '\0' && remove(tmp_path) != 0)
                {
                    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                                     "一時ファイルを削除できませんでした: %s\n", tmp_path);
                }

                (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
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
 *  @param[in]      context   しきい値レベル (cplat_trace_level *) を指すポインター。
 */
static void trace_console_hook(cplat_tracer_hook_entry *prev, cplat_tracer *handle, cplat_trace_level level,
                               const cplat_timespec *timestamp, const char *message, void *context)
{
    static const char lc_table[] = {'C', 'E', 'W', 'I', 'V', 'D'};
    cplat_trace_level threshold = *(const cplat_trace_level *)context;
    cplat_trace_level display_level = level;
    char ts[CPLAT_CLOCK_ISO8601_LOCAL_MSEC_LEN + 1];
    char lc;

    /* 強制出力は、しきい値によらず出力する指定であり、記録の重大度は対応する通常のレベルと同じである。 */
    if (CPLAT_TRACE_LEVEL_IS_FORCE(level))
    {
        display_level = CPLAT_TRACE_LEVEL_FROM_FORCE(level);
    }

    if (threshold != CPLAT_TRACE_LEVEL_NONE && (int)level <= (int)threshold)
    {
        if ((int)display_level >= 0 && (int)display_level < (int)CPLAT_TRACE_LEVEL_NONE)
        {
            lc = lc_table[(int)display_level];
        }
        else
        {
            lc = 'D';
        }

        /* 失敗時も既定の時刻文字列が格納され、表示の時刻欄だけが変わるため、戻り値は明示的に破棄する。 */
        (void)cplat_clock_format_realtime_iso8601_local(ts, sizeof(ts), timestamp);
        /* 利用者が画面で確認するトレースの表示であり、失敗を伝える先がないため、戻り値は明示的に破棄する。 */
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR, "%s %c %s\n", ts, lc, message);
    }

    cplat_tracer_call_next_hook(prev, handle, level, timestamp, message);
}

/**
 *  @brief          ログ レベル文字列を cplat_trace_level に変換します。
 *  @param[in]      str     レベル文字列 (VERBOSE/INFO/WARNING/ERROR/CRITICAL)。
 *  @param[out]     out     変換結果の格納先。
 *  @return         変換に成功した場合は 1、未知の文字列の場合は 0 を返します。
 */
static int parse_trace_level(const char *str, cplat_trace_level *out)
{
    static const struct
    {
        const char *name;
        cplat_trace_level level;
        uint32_t _pad;
    } tbl[] = {
        {"VERBOSE", CPLAT_TRACE_LEVEL_VERBOSE, 0U},   {"INFO", CPLAT_TRACE_LEVEL_INFO, 0U},
        {"WARNING", CPLAT_TRACE_LEVEL_WARNING, 0U},   {"ERROR", CPLAT_TRACE_LEVEL_ERROR, 0U},
        {"CRITICAL", CPLAT_TRACE_LEVEL_CRITICAL, 0U},
    };
    char upper[16];
    size_t i;
    size_t j;

    /* 大文字小文字を区別せずに照合するため、入力文字列を大文字へ変換する。 */
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

    /* 定義テーブルと照合して一致するログ レベルを取得する。 */
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
 *  @brief          ロール文字列を potr_role に変換します。
 *  @param[in]      str     ロール文字列 (sender/receiver、大文字小文字不問)。
 *  @param[out]     out     変換結果の格納先。
 *  @return         変換に成功した場合は 1、未知の文字列の場合は 0 を返します。
 */
static int parse_role(const char *str, potr_role *out)
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
    /* 利用者が画面で確認するヘルプの表示であり、失敗を伝える先がないため、この関数の表示の戻り値は明示的に破棄する。 */
    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT, "コマンド:\n");
    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                     "  open <role> <config_path> <service_id>  サービスを開きます。\n");
    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                     "                                          role: sender | receiver\n");
    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                     "  close                                   サービスを閉じます。\n");
    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                     "  send [-c|--compress] <message>          テキストを送信します。\n");
    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                     "  file [-c|--compress] <path>             ファイルを送信します。\n");
    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                     "  burst [-b|--blocking] <count> <bytes>   通し番号つきのメッセージを間隔なしで連続して送信します。\n");
    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                     "                                          bytes: 16 以上。-b で POTR_SEND_BLOCKING を使います。\n");
    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                     "  log <level>                             ログレベルを設定します。\n");
    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                     "  help                                    このヘルプを表示します。\n");
    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                     "  exit, quit                              終了します。\n");
}

/**
 *  @brief          ファイルをバイナリ モードで読み込む。
 *  @param[in]      path        ファイル パス。
 *  @param[out]     data_out    読み込んだデータの格納先 (malloc 確保、呼び出し元が free する)。
 *  @param[out]     len_out     読み込んだデータのバイト数の格納先。
 *  @return         成功時は 0、失敗時は -1 を返します。
 *                  失敗時はエラー メッセージを stderr に出力します。
 */
static int read_file_data(const char *path, unsigned char **data_out, size_t *len_out)
{
    FILE *fp = NULL;
    int64_t file_size;
    unsigned char *buf;
    size_t read_count;

    fp = cplat_fopen(path, "rb", NULL);

    /* 診断の出力先がほかにないため、この関数の cplat_pinned_prompt_printf の戻り値は明示的に破棄する。 */
    if (fp == NULL)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: ファイル \"%s\" を開けませんでした。\n", path);
        return -1;
    }

    if (cplat_fseek(fp, 0, SEEK_END) != 0)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: ファイルの読み込みに失敗しました。\n");
        (void)fclose(fp); /* 読み取り専用のファイルを失敗の経路で閉じるため、戻り値は明示的に破棄する */
        return -1;
    }

    file_size = cplat_ftell(fp);
    if (file_size < 0)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: ファイルの読み込みに失敗しました。\n");
        (void)fclose(fp); /* 読み取り専用のファイルを失敗の経路で閉じるため、戻り値は明示的に破棄する */
        return -1;
    }

    if (file_size == 0)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR, "エラー: ファイルが空です。\n");
        (void)fclose(fp); /* 読み取り専用のファイルを失敗の経路で閉じるため、戻り値は明示的に破棄する */
        return -1;
    }

    if ((uint64_t)file_size > POTR_MAX_MESSAGE_SIZE)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: ファイルサイズ (%" PRId64
                                         " バイト) が最大送信サイズ (%u バイト) を超えています。\n",
                                         file_size, (unsigned)POTR_MAX_MESSAGE_SIZE);
        (void)fclose(fp); /* 読み取り専用のファイルを失敗の経路で閉じるため、戻り値は明示的に破棄する */
        return -1;
    }

    buf = (unsigned char *)cplat_malloc((size_t)file_size);
    if (buf == NULL)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: メモリ確保に失敗しました。\n");
        (void)fclose(fp); /* 読み取り専用のファイルを失敗の経路で閉じるため、戻り値は明示的に破棄する */
        return -1;
    }

    rewind(fp);
    read_count = fread(buf, 1, (size_t)file_size, fp);
    /* 読み取り専用のファイルを閉じるだけで、読み込みの成否は read_count で検査するため、戻り値は明示的に破棄する。 */
    (void)fclose(fp);

    if (read_count != (size_t)file_size)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: ファイルの読み込みに失敗しました。\n");
        cplat_free(buf);
        return -1;
    }

    *data_out = buf;
    *len_out = (size_t)file_size;

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
static void apply_send_command(potr_context *handle, int is_file, char *cursor)
{
    char *payload;
    unsigned char *file_data = NULL;
    size_t file_len = 0;
    const void *send_data;
    size_t send_len;
    int compress = 0;
    const char *compress_label;
    unsigned int send_flags;
    int send_ret;

    /*
     * 送信の経過は利用者が画面で確認する表示であり、診断も出力先がほかにないため、
     * この関数の cplat_pinned_prompt_printf の戻り値は明示的に破棄する。
     */

    /* 圧縮オプション (-c または --compress) の有無を判定する。 */
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
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: 送信内容を指定してください。\n");
        return;
    }

    /* ファイル送信時はファイル全体を読み込み、テキスト送信時は文字列をそのまま送信対象とする。 */
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
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                         "ファイル送信中: \"%s\" (%zu バイト)%s\n", payload, send_len, compress_label);
    }
    else
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
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

    /* send_flags と POTR_SEND_* は int に収まる。 */
    send_ret = potr_service_send(handle, POTR_PEER_NA, send_data, send_len, (int)(send_flags | POTR_SEND_BLOCKING));
    if (send_ret != POTR_OK)
    {
        if (send_ret == POTR_ERR_DISCONNECTED)
        {
            (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                             "エラー: 未接続のため送信できません。\n");
        }
        else if (send_ret == POTR_ERR_FULL)
        {
            (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                             "エラー: 送信キューが満杯のため送信できません (POTR_ERR_FULL)。\n");
        }
        else
        {
            (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                             "エラー: 送信に失敗しました (result=%d)。\n", send_ret);
        }

        cplat_free(file_data);
        return;
    }

    if (is_file)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                         "ファイル送信完了: \"%s\" (%zu バイト)\n", payload, send_len);
    }
    else
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT, "送信完了。\n");
    }

    cplat_free(file_data); /* テキスト送信時は NULL */
}

/**
 *  @brief          burst コマンドを処理して、通し番号つきのメッセージを連続して送信します。
 *  @param[in]      handle  サービス ハンドル。
 *  @param[in,out]  cursor  コマンド名の次を指す解析位置。解析中に一部を書き換えます。
 *
 *  書式は `burst [-b|--blocking] <count> <bytes>` です。\n
 *  メッセージの先頭 16 [byte] は、識別子、通し番号 (0 始まり)、総件数で、残りは固定のパターンです。\n
 *  POTR_SEND_BLOCKING を指定しない場合に POTR_ERR_FULL を受けたときは、BURST_RETRY_WAIT_MS 待って送り直し、
 *  送り直した回数を数えます。1 件が BURST_STALL_LIMIT_MS 進まない場合は打ち切ります。\n
 *  終了時に BURST_SENT の 1 行を表示します。
 */
static void apply_burst_command(potr_context *handle, char *cursor)
{
    char *token;
    char *count_token = NULL;
    char *bytes_token = NULL;
    int blocking = 0;
    int64_t count_value = 0;
    int64_t bytes_value = 0;
    uint32_t count;
    size_t bytes;
    unsigned char *buffer;
    uint32_t sequence;
    uint32_t sent = 0U;
    uint64_t full_count = 0U;
    uint64_t started_ms;
    uint64_t elapsed_ms;
    const char *result_label = "ok";
    int result_code = POTR_OK;
    int send_flags = 0;
    size_t i;

    /*
     * 送信の結果は試験スクリプトと利用者が画面で確認する表示であり、診断も出力先がほかにないため、
     * この関数の cplat_pinned_prompt_printf の戻り値は明示的に破棄する。
     */
    token = next_token(&cursor);
    if (token != NULL && (strcmp(token, "-b") == 0 || strcmp(token, "--blocking") == 0))
    {
        blocking = 1;
        token = next_token(&cursor);
    }

    count_token = token;
    bytes_token = next_token(&cursor);
    if (count_token == NULL || bytes_token == NULL || next_token(&cursor) != NULL)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: burst [-b|--blocking] <count> <bytes> を指定してください。\n");
        return;
    }

    if (cplat_parse_int64(&count_value, count_token, 10) != CPLAT_OK || count_value < 1 ||
        count_value > BURST_MAX_COUNT)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: count は 1 から %d の整数で指定してください。\n", BURST_MAX_COUNT);
        return;
    }

    if (cplat_parse_int64(&bytes_value, bytes_token, 10) != CPLAT_OK || bytes_value < (int64_t)BURST_HEADER_SIZE ||
        bytes_value > (int64_t)POTR_MAX_MESSAGE_SIZE)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: bytes は %u から %u の整数で指定してください。\n",
                                         (unsigned)BURST_HEADER_SIZE, (unsigned)POTR_MAX_MESSAGE_SIZE);
        return;
    }

    count = (uint32_t)count_value;
    bytes = (size_t)bytes_value;

    buffer = (unsigned char *)cplat_malloc(bytes);
    if (buffer == NULL)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: メモリ確保に失敗しました。\n");
        return;
    }

    memcpy(buffer, BURST_MAGIC, BURST_MAGIC_SIZE);
    write_u32_le(buffer + BURST_MAGIC_SIZE + 4U, count);
    for (i = BURST_HEADER_SIZE; i < bytes; i++)
    {
        buffer[i] = (unsigned char)('a' + (i % 26U));
    }

    if (blocking)
    {
        send_flags = (int)POTR_SEND_BLOCKING;
    }

    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                     "burst 送信中: %u 件、%zu バイト、blocking=%d\n", count, bytes, blocking);

    started_ms = monotonic_ms();
    for (sequence = 0U; sequence < count; sequence++)
    {
        uint64_t stall_started_ms = 0U;
        int send_ret;

        if (cplat_atomic_load_i32(&s_running, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
        {
            result_label = "interrupted";
            break;
        }

        write_u32_le(buffer + BURST_MAGIC_SIZE, sequence);
        for (;;)
        {
            send_ret = potr_service_send(handle, POTR_PEER_NA, buffer, bytes, send_flags);
            if (send_ret != POTR_ERR_FULL)
            {
                break;
            }

            full_count++;
            if (stall_started_ms == 0U)
            {
                stall_started_ms = monotonic_ms();
            }
            else if (monotonic_ms() - stall_started_ms > BURST_STALL_LIMIT_MS)
            {
                break;
            }

            if (cplat_atomic_load_i32(&s_running, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
            {
                break;
            }

            cplat_sleep_ms(BURST_RETRY_WAIT_MS);
        }

        if (send_ret == POTR_ERR_FULL)
        {
            result_label = "full_timeout";
            break;
        }

        if (send_ret != POTR_OK)
        {
            result_label = "error";
            result_code = send_ret;
            break;
        }

        sent++;
    }

    elapsed_ms = monotonic_ms() - started_ms;
    cplat_free(buffer);

    if (result_code != POTR_OK)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                         "BURST_SENT sent=%u expected=%u full=%" PRIu64 " elapsed_ms=%" PRIu64
                                         " bytes=%zu blocking=%d result=error=%d\n",
                                         sent, count, full_count, elapsed_ms, bytes, blocking, result_code);
    }
    else
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                         "BURST_SENT sent=%u expected=%u full=%" PRIu64 " elapsed_ms=%" PRIu64
                                         " bytes=%zu blocking=%d result=%s\n",
                                         sent, count, full_count, elapsed_ms, bytes, blocking, result_label);
    }
}

/**
 *  @brief          トレーサーの hook 設定と開始を一度だけ実施します。
 *
 *  しきい値は s_trace_level を参照するため、設定後の log コマンドによる変更も反映されます。\n
 *  hook の設定または開始に失敗した場合は、診断を出して開始済みとせず、呼び出し元へ通知しません
 *  (呼び出し元に取れる行動がないため)。開始に失敗した場合は、設定した hook を外します。
 *  hook が二重に登録されて表示が重複することを防ぎ、次の呼び出しで再び開始を試みるためです。\n
 *  診断の出力先は、s_screen が NULL の場合は標準エラー出力、それ以外は s_screen の標準エラー出力のチャネルです。
 */
static void ensure_tracer_started(void)
{
    cplat_tracer *tracer;
    cplat_tracer_hook_entry *hook_entry;
    int ret;

    if (s_tracer_started)
    {
        return;
    }

    /* 診断の出力先がほかにないため、この関数の fprintf と cplat_pinned_prompt_printf の戻り値は明示的に破棄する。 */
    tracer = potr_tracer_get();
    hook_entry = cplat_tracer_set_hook(tracer, trace_console_hook, &s_trace_level);
    if (hook_entry == NULL)
    {
        if (s_screen == NULL)
        {
            (void)fprintf(stderr, "エラー: トレースの表示を開始できませんでした。\n");
        }
        else
        {
            (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                             "エラー: トレースの表示を開始できませんでした。\n");
        }
        return;
    }

    ret = cplat_tracer_start(tracer);
    if (ret != CPLAT_OK)
    {
        cplat_tracer_remove_hook(tracer, hook_entry);
        if (s_screen == NULL)
        {
            (void)fprintf(stderr, "エラー: トレースの表示を開始できませんでした (result=%d)。\n", ret);
        }
        else
        {
            (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                             "エラー: トレースの表示を開始できませんでした (result=%d)。\n", ret);
        }
        return;
    }

    s_tracer_started = 1;
}

/**
 *  @brief          サービスを開きます。
 *  @param[in,out]  session     セッション状態。
 *  @param[in]      role        ロール。
 *  @param[in]      config_path 設定ファイルのパス。
 *  @param[in]      service_id  サービスの ID。
 *  @return         成功時は 0、失敗時は -1 を返します。
 */
static int do_open(porter_test_session *session, potr_role role, const char *config_path, int64_t service_id)
{
    potr_type svc_type;
    int is_bidir = 0;
    int can_send = 0;
    potr_recv_fn callback = NULL;
    potr_context *handle;

    /*
     * サービスを開く経過は利用者が画面で確認する表示であり、診断も出力先がほかにないため、
     * この関数の cplat_pinned_prompt_printf の戻り値は明示的に破棄する。
     */
    if (session->is_open)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: 既にサービスを開いています。先に close してください。\n");
        return -1;
    }

    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                     "サービス %" PRId64 " を開いています... (設定: %s)\n", service_id, config_path);

    /*
     * サービス種別を取得して双方向サービスかどうか判定する。
     * sender・receiver ともに unicast_bidir / tcp_bidir の双方を双方向として扱う。
     */
    if (potr_service_get_type(config_path, service_id, &svc_type) == POTR_OK)
    {
        if (svc_type == POTR_TYPE_UNICAST_BIDIR || svc_type == POTR_TYPE_TCP_BIDIR)
        {
            is_bidir = 1;
        }
    }

    /* receiver は受信のためコールバックが必須である。sender は双方向時のみコールバックを設定する。 */
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

    if (potr_service_open_from_config(config_path, service_id, role, callback, &handle) != POTR_OK)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
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
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                         "双方向モード。相手からの受信メッセージと接続状態も表示します。\n");
    }

    if (role == POTR_ROLE_SENDER)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                         "送信準備完了。Ctrl+C または Ctrl+D で終了します。\n");
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                         "help でコマンド一覧を表示します。\n");
    }
    else
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                         "受信待機中... (Ctrl+C で終了)\n");

        if (can_send)
        {
            (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                             "send / file で送信もできます。help でコマンド一覧を表示します。\n");
        }
    }

    return 0;
}

/**
 *  @brief          サービスを閉じます。
 *  @param[in,out]  session セッション状態。
 */
static void do_close(porter_test_session *session)
{
    int ret;

    /*
     * 利用者が画面で確認する表示であり、診断も出力先がほかにないため、
     * この関数の cplat_pinned_prompt_printf の戻り値は明示的に破棄する。
     */
    if (!session->is_open)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: サービスは開いていません。\n");
        return;
    }

    /*
     * 失敗の結果 (タイムアウトなど) でも資源は回収されてハンドルは無効になるため、状態のクリアは結果によらず続ける。
     * コールバックの中からの呼び出しで返る POTR_ERR_UNSUPPORTED は、コマンドの処理から呼ぶため起きない。
     */
    ret = potr_service_close(session->handle);
    session->handle = NULL;
    session->is_open = 0;
    session->is_bidir = 0;
    session->can_send = 0;

    if (ret != POTR_OK)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "警告: サービス %" PRId64 " の終了処理が失敗しました (result=%d)。\n",
                                         session->service_id, ret);
        return;
    }

    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT, "サービスを閉じました。\n");
}

/**
 *  @brief          open コマンドを処理します。
 *  @param[in,out]  session セッション状態。
 *  @param[in,out]  cursor  コマンド名の次を指す解析位置。
 */
static void handle_open_command(porter_test_session *session, char *cursor)
{
    char *role_token;
    char *config_token;
    char *id_token;
    potr_role role;
    int64_t service_id;

    role_token = next_token(&cursor);
    config_token = next_token(&cursor);
    id_token = next_token(&cursor);

    /* 診断の出力先がほかにないため、この関数の cplat_pinned_prompt_printf の戻り値は明示的に破棄する。 */
    if (role_token == NULL || config_token == NULL || id_token == NULL)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: open <role> <config_path> <service_id> を指定してください。\n");
        return;
    }

    if (!parse_role(role_token, &role))
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: 不明なロール \"%s\"。sender または receiver を指定してください。\n",
                                         role_token);
        return;
    }

    if (cplat_parse_int64(&service_id, id_token, 10) != CPLAT_OK)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: service_id \"%s\" を整数として解釈できません。\n", id_token);
        return;
    }

    /* 失敗は do_open が表示済みで、対話コンソールを続けるだけのため、戻り値は明示的に破棄する。 */
    (void)do_open(session, role, config_token, service_id);
}

/**
 *  @brief          log コマンドを処理します。
 *  @param[in,out]  cursor コマンド名の次を指す解析位置。
 */
static void handle_log_command(char *cursor)
{
    char *level_token;
    cplat_trace_level level;

    level_token = next_token(&cursor);

    /*
     * 設定の結果は利用者が画面で確認する表示であり、診断も出力先がほかにないため、
     * この関数の cplat_pinned_prompt_printf の戻り値は明示的に破棄する。
     */
    if (level_token == NULL)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: log <level> を指定してください。\n");
        return;
    }

    if (!parse_trace_level(level_token, &level))
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: 不明なログレベル \"%s\"。"
                                         "VERBOSE/INFO/WARNING/ERROR/CRITICAL のいずれかを指定してください。\n",
                                         level_token);
        return;
    }

    s_trace_level = level;
    ensure_tracer_started();
    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT, "ログレベルを %s に設定しました。\n",
                                     level_token);
}

/**
 *  @brief          対話コマンド 1 行を処理します。
 *  @param[in,out]  session セッション状態。
 *  @param[in,out]  line    入力行。解析中に一部を書き換えます。
 *  @return         1: 継続、0: 終了。
 */
static int process_line(porter_test_session *session, char *line)
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

    /* 診断の出力先がほかにないため、この関数の cplat_pinned_prompt_printf の戻り値は明示的に破棄する。 */
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

    if (strcmp(command, "send") == 0 || strcmp(command, "file") == 0 || strcmp(command, "burst") == 0)
    {
        if (!session->is_open)
        {
            (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                             "エラー: サービスが開いていません。"
                                             "open <role> <config_path> <service_id> で開いてください。\n");
            return 1;
        }

        if (!session->can_send)
        {
            (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                             "エラー: 受信専用サービスのため送信できません。\n");
            return 1;
        }

        if (strcmp(command, "burst") == 0)
        {
            apply_burst_command(session->handle, cursor);
            return 1;
        }

        is_file = (strcmp(command, "file") == 0);
        apply_send_command(session->handle, is_file, cursor);
        return 1;
    }

    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR, "エラー: 不明なコマンドです: %s\n",
                                     command);
    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                     "help でコマンド一覧を表示します。\n");

    return 1;
}

/**
 *  @brief          プロンプトに表示する状態文字列を生成します。
 *  @param[in]      session セッション状態。
 *  @param[out]     buf     格納先バッファー。
 *  @param[in]      size    バッファーのサイズ。
 */
static void build_prompt_state(const porter_test_session *session, char *buf, size_t size)
{
    /*
     * 呼び出し元のバッファー (PROMPT_STATE_SIZE = 32) に最長の文字列 (30 byte) が収まり、失敗しても表示が空になるだけのため、
     * この関数の cplat_snprintf の戻り値は明示的に破棄する。
     */
    if (!session->is_open)
    {
        (void)cplat_snprintf(buf, size, "closed");
    }
    else if (session->role == POTR_ROLE_SENDER)
    {
        (void)cplat_snprintf(buf, size, "sender:%" PRId64, session->service_id);
    }
    else
    {
        (void)cplat_snprintf(buf, size, "receiver:%" PRId64, session->service_id);
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
    porter_test_session session;
    int need_help = 0;
    const char *log_level = NULL;
    int recv_delay_ms = 0;

    cplat_console_init();

    session.handle = NULL;
    session.service_id = 0;
    session.role = POTR_ROLE_SENDER;
    session.is_open = 0;
    session.is_bidir = 0;
    session.can_send = 0;

    cplat_argparser_init(argc, argv, "porter の送受信動作を対話的に検証します。");
    /* 登録の失敗は直後の cplat_argparser_get_register_error_count() でまとめて検査するため、戻り値は明示的に破棄する。 */
    (void)cplat_argparser_register_flag("-h", "--help", "ヘルプを表示します。", &need_help);
    (void)cplat_argparser_register_option_string("-l", NULL, "level", "ログレベル。", 0, &log_level);
    (void)cplat_argparser_register_option_int(NULL, "--recv-delay-ms", "ms",
                                              "POTR_EVENT_DATA を受けるたびに待つ時間 [ms]。既定は 0。", 0, &recv_delay_ms);
    (void)cplat_argparser_register_positional_string("role", "sender または receiver。", 0, &role_arg);
    (void)cplat_argparser_register_positional_string("config_path", "設定ファイル。", 0, &config_path_arg);
    (void)cplat_argparser_register_positional_string("service_id", "サービス ID。", 0, &service_id_arg);

    if (cplat_argparser_get_register_error_count() > 0)
    {
        /* 診断の出力先がほかになく、終了コードはすでに失敗に決まっているため、戻り値は明示的に破棄する。 */
        (void)cplat_argparser_print_register_error_messages(stderr);
        return EXIT_FAILURE;
    }

    int parse_result = cplat_argparser_parse();

    if (need_help != 0)
    {
        if (cplat_argparser_print_usage(stdout) != CPLAT_OK || fflush(stdout) != 0 || ferror(stdout) != 0)
        {
            /* 診断の出力先がほかにないため、戻り値は明示的に破棄する。 */
            (void)fprintf(stderr, "エラー: 使い方を標準出力へ書き込めませんでした。\n");
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }

    if (parse_result != CPLAT_OK)
    {
        /* 診断の出力先がほかになく、終了コードはすでに失敗に決まっているため、この 2 つの表示の関数の戻り値は明示的に破棄する。 */
        (void)cplat_argparser_print_error_messages(stderr);
        (void)cplat_argparser_print_usage(stderr);
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

    if (recv_delay_ms < 0 || recv_delay_ms > RECV_DELAY_MAX_MS)
    {
        /* 診断の出力先がほかになく、終了コードはすでに失敗に決まっているため、この 2 つの出力の戻り値は明示的に破棄する。 */
        (void)fprintf(stderr, "エラー: --recv-delay-ms は 0 から %d の整数で指定してください。\n\n", RECV_DELAY_MAX_MS);
        (void)cplat_argparser_print_usage(stderr);
        return EXIT_FAILURE;
    }

    s_recv_delay_ms = recv_delay_ms;

    if (log_level != NULL)
    {
        if (!parse_trace_level(log_level, &s_trace_level))
        {
            /* 診断の出力先がほかになく、終了コードはすでに失敗に決まっているため、この 2 つの出力の戻り値は明示的に破棄する。 */
            (void)fprintf(stderr,
                          "エラー: 不明なログレベル \"%s\"。"
                          "VERBOSE/INFO/WARNING/ERROR/CRITICAL のいずれかを指定してください。\n\n",
                          log_level);
            (void)cplat_argparser_print_usage(stderr);
            return EXIT_FAILURE;
        }

        s_tracer_started = 0; /* 後段の ensure_tracer_started で開始する */
        ensure_tracer_started();
    }

    /* 位置引数は 3 個すべて指定するか、すべて省略して対話コンソールで開くかのいずれかとする。 */
    if (positional_count != 0 && positional_count != 3)
    {
        /* 診断の出力先がほかになく、終了コードはすでに失敗に決まっているため、この 2 つの出力の戻り値は明示的に破棄する。 */
        (void)fprintf(stderr, "エラー: role、config_path、service_id は 3 個すべてを指定してください。\n\n");
        (void)cplat_argparser_print_usage(stderr);
        return EXIT_FAILURE;
    }

    cplat_atomic_store_i32(&s_running, 1, CPLAT_MEMORY_ORDER_RELEASE);
    s_shutdown_requested = 0;

    cplat_local_lock *stop_lock;

    s_screen = cplat_pinned_prompt_create(NULL);
    if (s_screen == NULL)
    {
        /* 診断の出力先がほかにないため、戻り値は明示的に破棄する。 */
        (void)fprintf(stderr, "エラー: プロンプトの初期化に失敗しました。\n");
        return EXIT_FAILURE;
    }

    /*
     * 対話の案内と入力の表示は利用者が画面で確認する表示であり、診断も出力先がほかにないため、
     * この関数の cplat_pinned_prompt_printf の戻り値は明示的に破棄する。
     */
    stop_lock = get_screen_stop_lock();
    if (stop_lock == NULL)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: 終了同期用ロックの初期化に失敗しました。\n");
        cplat_pinned_prompt_dispose(s_screen);
        s_screen = NULL;
        return EXIT_FAILURE;
    }

    set_stop_target_screen(stop_lock, s_screen);

    if (cplat_shutdown_request_register(porter_test_shutdown_request_callback, NULL) != CPLAT_OK)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                         "エラー: 終了要求 callback の登録に失敗しました。\n");
        set_stop_target_screen(stop_lock, NULL);
        cplat_pinned_prompt_dispose(s_screen);
        s_screen = NULL;
        return EXIT_FAILURE;
    }

    /* 位置引数が指定された場合は起動時に open を実行する。失敗時も対話コンソールへ移行する。 */
    if (positional_count == 3)
    {
        potr_role role;

        if (!parse_role(role_arg, &role))
        {
            (void)cplat_pinned_prompt_printf(
                s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                "エラー: 不明なロール \"%s\"。sender または receiver を指定してください。\n", role_arg);
        }
        else
        {
            int64_t service_id;

            if (cplat_parse_int64(&service_id, service_id_arg, 10) != CPLAT_OK)
            {
                (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                                 "エラー: service_id \"%s\" を整数として解釈できません。\n",
                                                 service_id_arg);
            }
            else
            {
                /* 失敗は do_open が表示済みで、失敗時も対話コンソールへ移行するため、戻り値は明示的に破棄する。 */
                (void)do_open(&session, role, config_path_arg, service_id);
            }
        }
    }
    else
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT,
                                         "open <role> <config_path> <service_id> でサービスを開きます。"
                                         "help でコマンド一覧を表示します。\n");
    }

    /* 対話入力ループを実行し、終了指示があるまでコマンドを順次処理する。 */
    while (cplat_atomic_load_i32(&s_running, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
    {
        build_prompt_state(&session, prompt_state, sizeof(prompt_state));

        if (cplat_pinned_prompt_readline_fmt(s_screen, line, sizeof(line), "porter-test[%s]> ", prompt_state) !=
            CPLAT_OK)
        {
            break;
        }

        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT, "porter-test[%s]> %s\n",
                                         prompt_state, line);

        if (process_line(&session, line) == 0)
        {
            break;
        }
    }

    if (s_shutdown_requested)
    {
        (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT, "\n終了中...\n");
    }

    if (session.is_open)
    {
        /* 対話の途中の失敗と同じく、終了処理の失敗は表示だけとし、終了コードへ反映しない。 */
        int close_ret = potr_service_close(session.handle);

        session.is_open = 0;
        if (close_ret != POTR_OK)
        {
            (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDERR,
                                             "警告: サービス %" PRId64 " の終了処理が失敗しました (result=%d)。\n",
                                             session.service_id, close_ret);
        }
    }

    (void)cplat_pinned_prompt_printf(s_screen, CPLAT_PINNED_PROMPT_CHANNEL_STDOUT, "終了しました。\n");
    /* callback の登録解除 API がなく main 終了処理後も実行され得るため、同じ永続ロック下で画面ハンドルを先に NULL 化してから破棄する。 */
    set_stop_target_screen(stop_lock, NULL);
    cplat_pinned_prompt_dispose(s_screen);
    s_screen = NULL;

    return EXIT_SUCCESS;
}
