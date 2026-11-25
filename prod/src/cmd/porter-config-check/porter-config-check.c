/**
 *******************************************************************************
 *  @file           porter-config-check.c
 *  @brief          設定ファイルを検証して結果を報告するコマンドを実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/11/24
 *  @version        1.0.0
 *
 *  porter の設定ファイルを、サービスを開かずに検証する CLI コマンドです。\n
 *  1 回の読み取り・解析で構文、定義、値域、制約、必須項目、および (指定時は) 認証ファイルの
 *  存在と種別を検査し、結果を標準出力へ出力します。\n
 *  エラーの識別と終了コードの扱いは、共通のコーディング規範の「プロセスの終了コード」に従います。
 *
 *  @par            使用方法
    @code{.sh}
    porter-config-check [-h] [--check-files] [--role ROLE] config_path
    @endcode
 *
 *  @par            オプション
 *  @parblock
 *  Table: porter-config-check のコマンドライン オプション
 *
 *  | オプション       | 説明                                                                      |
 *  | ---------------- | ------------------------------------------------------------------------- |
 *  | -h, --help       | 使い方を標準出力へ出力し、終了コード 0 で終了します。                     |
 *  | --check-files    | 証明書方式の認証ファイルが通常ファイルとして読み取れることも検証します。  |
 *  | --role \<ROLE\>  | 役割依存の条件を検査する対象 (sender / receiver / all)。既定は all です。 |
 *  @endparblock
 *
 *  @attention      --check-files を指定した場合、認証ファイルの相対パスは本コマンドの
 *                  作業ディレクトリから解決します。サービスを開く処理も設定ファイル内のパスのまま
 *                  開くため、確認はサービスのプロセスと同じ作業ディレクトリで実行してください。
 *
 *  @par            終了コード
 *  @parblock
 *  Table: porter-config-check の終了コード
 *
 *  | 終了コード | 意味                                                                          |
 *  | ---------- | ----------------------------------------------------------------------------- |
 *  | 0          | 誤りがありません (-h の表示の成功を含みます)。                                |
 *  | 1          | 実行の失敗 (引数の誤り、I/O 失敗、メモリ不足、出力書き込み・フラッシュ失敗)。 |
 *  | 2          | 設定の誤りが 1 件以上あります (構文の誤り、--check-files の誤りを含みます)。  |
 *  @endparblock
 *
 *******************************************************************************
 */

#include <cplat/argparser/argparser.h>
#include <cplat/base/platform.h>
#include <cplat/base/result.h>
#include <cplat/crt/stdio.h>
#include <cplat/crt/stdlib.h>
#include <cplat/crt/sys/stat.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <porter.h>
#include <porter/porter_result.h>

/** 終了コード: 誤りなし。 */
#define EXIT_CODE_OK 0
/** 終了コード: コマンド自身の実行失敗。 */
#define EXIT_CODE_EXEC_FAILURE 1
/** 終了コード: 設定の誤りあり。 */
#define EXIT_CODE_INVALID_CONFIG 2

/** サービス ID 一覧の初期確保要素数。 */
#define SERVICE_LIST_INIT_CAP 16U

/** 記録したサービスごとの集計です。 */
typedef struct config_service_entry
{
    int64_t id;         /**< サービスの ID。 */
    size_t issue_count; /**< そのサービスに属する誤りの件数。 */
} config_service_entry;

/** コールバック間で共有する実行状態です。 */
typedef struct config_check_state
{
    const char *config_path;       /**< コマンド ラインで受け取った設定ファイル パス。 */
    config_service_entry *services; /**< サービス ID の登場順の記録配列。 */
    size_t service_count;          /**< 記録済みサービス数。 */
    size_t service_capacity;       /**< 確保済みサービス配列容量。 */
    size_t total_issues;           /**< 合計誤り件数 (ルート・global・ファイル確認を含む)。 */
    int validate_ret;              /**< potr_config_validate_file() の戻り値。 */
    int record_oom;                /**< サービス配列の確保に失敗した場合に 1。 */
    int stdout_failed;             /**< 標準出力への書き込み・フラッシュに失敗した場合に 1。 */
    unsigned int pad;              /**< 明示的アラインメントです。0 を指定します。 */
} config_check_state;

/**
 *  @brief          結果コードに対応する識別子名を返します。
 *  @param[in]      err     結果コード。
 *  @return         結果コードの名前。未定義の場合は "POTR_ERR_UNKNOWN"。
 */
static const char *potr_err_name(int err)
{
    switch (err)
    {
    case POTR_OK:
        return "POTR_OK";
    case POTR_ERR_UNKNOWN:
        return "POTR_ERR_UNKNOWN";
    case POTR_ERR_INVALID_ARGUMENT:
        return "POTR_ERR_INVALID_ARGUMENT";
    case POTR_ERR_UNSUPPORTED:
        return "POTR_ERR_UNSUPPORTED";
    case POTR_ERR_NOT_FOUND:
        return "POTR_ERR_NOT_FOUND";
    case POTR_ERR_INVALID_HANDLE:
        return "POTR_ERR_INVALID_HANDLE";
    case POTR_ERR_INVALID_CONFIG:
        return "POTR_ERR_INVALID_CONFIG";
    case POTR_ERR_OUT_OF_MEMORY:
        return "POTR_ERR_OUT_OF_MEMORY";
    case POTR_ERR_FULL:
        return "POTR_ERR_FULL";
    case POTR_ERR_EMPTY:
        return "POTR_ERR_EMPTY";
    case POTR_ERR_OUT_OF_WINDOW:
        return "POTR_ERR_OUT_OF_WINDOW";
    case POTR_ERR_DISCONNECTED:
        return "POTR_ERR_DISCONNECTED";
    case POTR_ERR_TIMEOUT:
        return "POTR_ERR_TIMEOUT";
    case POTR_ERR_EOF:
        return "POTR_ERR_EOF";
    case POTR_ERR_IO:
        return "POTR_ERR_IO";
    case POTR_ERR_PROTOCOL:
        return "POTR_ERR_PROTOCOL";
    case POTR_ERR_CANCELED:
        return "POTR_ERR_CANCELED";
    default:
        return "POTR_ERR_UNKNOWN";
    }
}

/**
 *  @brief          設定の誤りの種別に対応する CLI 表示名を返します。
 *  @param[in]      kind    設定の誤りの種別。
 *  @return         出力書式の `<kind>` 文字列。
 */
static const char *issue_kind_name(potr_config_issue_kind kind)
{
    switch (kind)
    {
    case POTR_CONFIG_ISSUE_SYNTAX:
        return "syntax";
    case POTR_CONFIG_ISSUE_UNKNOWN_KEY:
        return "unknown-key";
    case POTR_CONFIG_ISSUE_DUPLICATE_KEY:
        return "duplicate-key";
    case POTR_CONFIG_ISSUE_INVALID_SERVICE_ID:
        return "service-id";
    case POTR_CONFIG_ISSUE_TYPE_MISMATCH:
        return "type";
    case POTR_CONFIG_ISSUE_INVALID_VALUE:
        return "value";
    case POTR_CONFIG_ISSUE_OUT_OF_RANGE:
        return "range";
    case POTR_CONFIG_ISSUE_NOT_APPLICABLE:
        return "not-applicable";
    case POTR_CONFIG_ISSUE_MISSING:
        return "missing";
    case POTR_CONFIG_ISSUE_CONSTRAINT:
        return "constraint";
    case POTR_CONFIG_ISSUE_KEY_DERIVATION:
        return "key-derivation";
    default:
        return "unknown";
    }
}

/**
 *  @brief          文字列をそのまま出力ストリームへ書き込みます (`mock_stdio` 対応のため `fwrite` を使用)。
 *  @param[in,out]  fp      出力先ストリーム。
 *  @param[in]      text    書き込む文字列。
 *  @param[in,out]  failed  失敗時に 1 を設定するフラグ。
 */
static void write_raw_text(FILE *fp, const char *text, int *failed)
{
    size_t len;

    if (fp == NULL || text == NULL || failed == NULL)
    {
        return;
    }

    len = strlen(text);
    if (len == 0U)
    {
        return;
    }

    if (fwrite(text, 1U, len, fp) != len)
    {
        *failed = 1;
    }
}

/**
 *  @brief          制御文字 (0x00..0x1F, 0x7F) とバックスラッシュをエスケープして書き込みます。
 *  @param[in,out]  fp      出力先ストリーム。
 *  @param[in]      text    書き込む文字列。NULL の場合は何もしません。
 *  @param[in,out]  failed  失敗時に 1 を設定するフラグ。
 */
static void write_escaped(FILE *fp, const char *text, int *failed)
{
    size_t span_start = 0U;
    size_t i = 0U;

    if (fp == NULL || text == NULL || failed == NULL)
    {
        return;
    }

    while (text[i] != '\0')
    {
        unsigned char ch = (unsigned char)text[i];

        if (ch < 0x20U || ch == 0x7FU || ch == (unsigned char)'\\')
        {
            if (i > span_start)
            {
                size_t span_len = i - span_start;
                if (fwrite(text + span_start, 1U, span_len, fp) != span_len)
                {
                    *failed = 1;
                }
            }

            if (ch == (unsigned char)'\\')
            {
                if (fwrite("\\\\", 1U, 2U, fp) != 2U)
                {
                    *failed = 1;
                }
            }
            else
            {
                if (fprintf(fp, "\\x%02X", (unsigned int)ch) < 0)
                {
                    *failed = 1;
                }
            }

            span_start = i + 1U;
        }

        i++;
    }

    if (i > span_start)
    {
        size_t span_len = i - span_start;
        if (fwrite(text + span_start, 1U, span_len, fp) != span_len)
        {
            *failed = 1;
        }
    }
}

/**
 *  @brief          指定されたサービス ID に対応する集計エントリへ誤り件数を加算します。
 *  @param[in,out]  state       実行状態。
 *  @param[in]      service_id  加算対象のサービス ID。
 */
static void increment_service_issue(config_check_state *state, int64_t service_id)
{
    size_t i;

    for (i = 0U; i < state->service_count; i++)
    {
        if (state->services[i].id == service_id)
        {
            state->services[i].issue_count++;
            return;
        }
    }
}

/**
 *  @brief          同じ解析結果のサービス ID を順番に記録します。
 *  @param[in]      service_id  サービスの ID。
 *  @param[in,out]  user_data   @ref config_check_state へのポインター。
 */
static void on_service_cb(int64_t service_id, void *user_data)
{
    config_check_state *state = (config_check_state *)user_data;

    if (state == NULL || state->record_oom != 0)
    {
        return;
    }

    if (state->service_count == state->service_capacity)
    {
        size_t new_cap;
        config_service_entry *next_buf;

        if (state->service_capacity == 0U)
        {
            new_cap = SERVICE_LIST_INIT_CAP;
        }
        else if (state->service_capacity > (SIZE_MAX / 2U) / sizeof(config_service_entry))
        {
            state->record_oom = 1;
            return;
        }
        else
        {
            new_cap = state->service_capacity * 2U;
        }

        next_buf = (config_service_entry *)cplat_malloc(new_cap * sizeof(config_service_entry));
        if (next_buf == NULL)
        {
            state->record_oom = 1;
            return;
        }

        if (state->services != NULL)
        {
            memcpy(next_buf, state->services, state->service_count * sizeof(config_service_entry));
            cplat_free(state->services);
        }

        state->services = next_buf;
        state->service_capacity = new_cap;
    }

    state->services[state->service_count].id = service_id;
    state->services[state->service_count].issue_count = 0U;
    state->service_count++;
}

/**
 *  @brief          設定検証が検出した誤り 1 件を標準出力へ出力します。
 *  @param[in]      issue      誤りの内容。
 *  @param[in,out]  user_data  @ref config_check_state へのポインター。
 */
static void on_issue_cb(const potr_config_issue *issue, void *user_data)
{
    config_check_state *state = (config_check_state *)user_data;
    const char *key_path;

    if (state == NULL || issue == NULL)
    {
        return;
    }

    state->total_issues++;
    if (issue->has_service_id != 0)
    {
        increment_service_issue(state, issue->service_id);
    }

    write_escaped(stdout, state->config_path, &state->stdout_failed);

    if (issue->kind == POTR_CONFIG_ISSUE_SYNTAX)
    {
        if (fprintf(stdout, ":%" PRIu32 ":%" PRIu32 ": error: syntax: %s\n", issue->line, issue->column,
                    (issue->message != NULL) ? issue->message : "syntax error") < 0)
        {
            state->stdout_failed = 1;
        }
        return;
    }

    if (fprintf(stdout, ": error: %s: ", issue_kind_name(issue->kind)) < 0)
    {
        state->stdout_failed = 1;
    }

    key_path = (issue->key_path != NULL && issue->key_path[0] != '\0') ? issue->key_path : "/";
    write_escaped(stdout, key_path, &state->stdout_failed);

    if (fprintf(stdout, ": %s", (issue->message != NULL) ? issue->message : "invalid config") < 0)
    {
        state->stdout_failed = 1;
    }

    if (issue->expected != NULL && issue->expected[0] != '\0')
    {
        if (fprintf(stdout, " [expected: %s]", issue->expected) < 0)
        {
            state->stdout_failed = 1;
        }
    }

    write_raw_text(stdout, "\n", &state->stdout_failed);
}

/**
 *  @brief          `--check-files` 指定時に、証明書方式の参照ファイルが読み取れることを確認します。
 *  @param[in]      file       ファイル参照情報。
 *  @param[in,out]  user_data  @ref config_check_state へのポインター。
 */
static void on_auth_file_cb(const potr_config_file_ref *file, void *user_data)
{
    config_check_state *state = (config_check_state *)user_data;
    cplat_file_stat_t st;
    const char *reason = NULL;
    const char *key_path;

    if (state == NULL || file == NULL || file->path == NULL)
    {
        return;
    }

    if (cplat_stat(&st, NULL, file->path) != CPLAT_OK)
    {
        reason = "cannot access file";
    }
    else if (cplat_file_stat_is_regular(&st) == 0)
    {
        reason = "not a regular file";
    }
    else
    {
        FILE *fp = cplat_fopen(file->path, "rb", NULL);
        if (fp == NULL)
        {
            reason = "cannot open file";
        }
        else
        {
            /* 読み取り可否の確認だけであり、変更書き込みは行っていないため、fclose() の戻り値は明示的に破棄する。 */
            (void)fclose(fp);
            return;
        }
    }

    state->total_issues++;
    increment_service_issue(state, file->service_id);

    write_escaped(stdout, state->config_path, &state->stdout_failed);
    write_raw_text(stdout, ": error: file: ", &state->stdout_failed);

    key_path = (file->key_path != NULL && file->key_path[0] != '\0') ? file->key_path : "/";
    write_escaped(stdout, key_path, &state->stdout_failed);

    if (fprintf(stdout, ": %s: ", reason) < 0)
    {
        state->stdout_failed = 1;
    }

    write_escaped(stdout, file->path, &state->stdout_failed);
    write_raw_text(stdout, "\n", &state->stdout_failed);
}

/**
 *  @brief          `--role` の文字列を役割フラグへ変換します。
 *  @param[in]      role_str   役割の文字列。NULL の場合は @ref POTR_CONFIG_ROLE_ALL 。
 *  @param[out]     roles_out  変換結果の格納先。
 *  @return         妥当な役割名なら 1、不正な値なら 0。
 */
static int parse_role_option(const char *role_str, uint32_t *roles_out)
{
    if (roles_out == NULL)
    {
        return 0;
    }

    if (role_str == NULL || strcmp(role_str, "all") == 0)
    {
        *roles_out = POTR_CONFIG_ROLE_ALL;
        return 1;
    }

    if (strcmp(role_str, "sender") == 0)
    {
        *roles_out = POTR_CONFIG_ROLE_SENDER;
        return 1;
    }

    if (strcmp(role_str, "receiver") == 0)
    {
        *roles_out = POTR_CONFIG_ROLE_RECEIVER;
        return 1;
    }

    return 0;
}

/**
 *  @brief          標準エラー出力をフラッシュし、エラー状態を確認します。
 *  @return         フラッシュとエラー状態が正常なら 1、失敗なら 0。
 */
static int flush_stderr_ok(void)
{
    if (fflush(stderr) != 0 || ferror(stderr) != 0)
    {
        return 0;
    }

    return 1;
}

/**
 *  @brief          サービスごとの結果行と全体の要約行を標準出力へ出力します。
 *  @param[in,out]  state  実行状態。
 */
static void print_summary(config_check_state *state)
{
    size_t i;

    for (i = 0U; i < state->service_count; i++)
    {
        write_escaped(stdout, state->config_path, &state->stdout_failed);

        if (state->services[i].issue_count == 0U)
        {
            if (fprintf(stdout, ": service %" PRId64 ": OK\n", state->services[i].id) < 0)
            {
                state->stdout_failed = 1;
            }
        }
        else
        {
            if (fprintf(stdout, ": service %" PRId64 ": %zu error(s)\n", state->services[i].id,
                        state->services[i].issue_count) < 0)
            {
                state->stdout_failed = 1;
            }
        }
    }

    write_escaped(stdout, state->config_path, &state->stdout_failed);

    if (state->total_issues == 0U)
    {
        if (fprintf(stdout, ": OK (%zu service(s))\n", state->service_count) < 0)
        {
            state->stdout_failed = 1;
        }
    }
    else
    {
        if (fprintf(stdout, ": %zu error(s)\n", state->total_issues) < 0)
        {
            state->stdout_failed = 1;
        }
    }
}

/**
 *  @brief          porter-config-check のエントリ ポイントです。
 *  @param[in]      argc  コマンド ライン引数の数。
 *  @param[in]      argv  コマンド ライン引数の配列。
 *  @return         @ref EXIT_CODE_OK 、@ref EXIT_CODE_EXEC_FAILURE 、@ref EXIT_CODE_INVALID_CONFIG のいずれか。
 */
int main(int argc, char *argv[])
{
    int need_help = 0;
    int check_files = 0;
    const char *role_str = NULL;
    const char *config_path = NULL;
    uint32_t roles = POTR_CONFIG_ROLE_ALL;
    int parse_result;
    potr_config_validate_options opts;
    config_check_state state;
    int exec_failed = 0;

    cplat_argparser_init(
        argc, argv,
        "porter の設定ファイルを、サービスを開かずに検証します。\n"
        "--check-files の相対パスは作業ディレクトリから解決するため、"
        "サービスのプロセスと同じ作業ディレクトリで実行してください。");

    /* 登録の失敗は直後の cplat_argparser_get_register_error_count() でまとめて検査するため、戻り値は明示的に破棄する。 */
    (void)cplat_argparser_register_flag("-h", "--help", "使い方を表示して終了します。", &need_help);
    (void)cplat_argparser_register_flag(NULL, "--check-files",
                                        "証明書方式の認証ファイルが読み取れることも確認します。", &check_files);
    (void)cplat_argparser_register_option_string(NULL, "--role", "ROLE",
                                                 "役割に依存する条件を検査する役割 (sender / receiver / all)。既定は all。",
                                                 0, &role_str);
    (void)cplat_argparser_register_positional_string("config_path", "検証する設定ファイルのパス。",
                                                     CPLAT_ARGPARSER_REQUIRED, &config_path);

    if (cplat_argparser_get_register_error_count() > 0)
    {
        /* 診断の出力先がほかにないため、出力の戻り値は明示的に破棄し、実行失敗の終了コードを返す。 */
        (void)cplat_argparser_print_register_error_messages(stderr);
        (void)flush_stderr_ok();
        return EXIT_CODE_EXEC_FAILURE;
    }

    parse_result = cplat_argparser_parse();

    if (need_help != 0)
    {
        if (cplat_argparser_print_usage(stdout) != CPLAT_OK || fflush(stdout) != 0 || ferror(stdout) != 0)
        {
            /* 標準出力の失敗後に診断を書くため、戻り値は明示的に破棄する。 */
            (void)fprintf(stderr, "エラー: 使い方を標準出力へ書き込めませんでした。\n");
            (void)flush_stderr_ok();
            return EXIT_CODE_EXEC_FAILURE;
        }

        return EXIT_CODE_OK;
    }

    if (parse_result != CPLAT_OK)
    {
        /* 解析エラーの表示後はそのまま実行失敗として終了するため、出力関数の戻り値は明示的に破棄する。 */
        (void)cplat_argparser_print_error_messages(stderr);
        (void)cplat_argparser_print_usage(stderr);
        (void)flush_stderr_ok();
        return EXIT_CODE_EXEC_FAILURE;
    }

    if (parse_role_option(role_str, &roles) == 0)
    {
        /* 複製に失敗しても結果コードが記録され、既定の文で表示して終了するため、戻り値は明示的に破棄する。 */
        (void)cplat_argparser_set_validation_error(CPLAT_ERR_OUT_OF_RANGE, "--role",
                                                   "value of '--role' must be sender, receiver, or all");
        /* 診断の表示後はそのまま実行失敗として終了するため、出力関数の戻り値は明示的に破棄する。 */
        (void)cplat_argparser_print_error_messages(stderr);
        (void)cplat_argparser_print_usage(stderr);
        (void)flush_stderr_ok();
        return EXIT_CODE_EXEC_FAILURE;
    }

    memset(&state, 0, sizeof(state));
    state.config_path = config_path;

    memset(&opts, 0, sizeof(opts));
    opts.roles = roles;
    opts.reserved = 0U;
    opts.on_issue = on_issue_cb;
    opts.on_service = on_service_cb;
    opts.on_auth_file = (check_files != 0) ? on_auth_file_cb : NULL;
    opts.user_data = &state;

    state.validate_ret = potr_config_validate_file(config_path, &opts, NULL);

    if (state.validate_ret != POTR_OK && state.validate_ret != POTR_ERR_INVALID_CONFIG)
    {
        const char *reason_text = (state.validate_ret == POTR_ERR_IO)
                                      ? "設定ファイルを読み取れません"
                                      : "設定ファイルを検証できません";

        exec_failed = 1;
        if (fprintf(stderr, "エラー: %s: %s (%s)\n", reason_text, config_path, potr_err_name(state.validate_ret)) < 0)
        {
            exec_failed = 1;
        }
    }
    else if (state.record_oom != 0)
    {
        exec_failed = 1;
        if (fprintf(stderr, "エラー: サービス一覧の記録領域を確保できませんでした (%s)\n",
                    potr_err_name(POTR_ERR_OUT_OF_MEMORY)) < 0)
        {
            exec_failed = 1;
        }
    }
    else
    {
        print_summary(&state);
    }

    if (fflush(stdout) != 0 || ferror(stdout) != 0)
    {
        state.stdout_failed = 1;
    }

    if (state.stdout_failed != 0 && exec_failed == 0)
    {
        exec_failed = 1;
        /* 標準出力失敗の診断後に終了コード 1 で終了するため、戻り値は明示的に破棄する。 */
        (void)fprintf(stderr, "エラー: 検証結果を標準出力へ書き込めませんでした。\n");
    }

    if (exec_failed != 0 && flush_stderr_ok() == 0)
    {
        exec_failed = 1;
    }

    if (state.services != NULL)
    {
        cplat_free(state.services);
        state.services = NULL;
    }

    if (exec_failed != 0 || state.stdout_failed != 0)
    {
        return EXIT_CODE_EXEC_FAILURE;
    }

    if (state.validate_ret == POTR_ERR_INVALID_CONFIG || state.total_issues > 0U)
    {
        return EXIT_CODE_INVALID_CONFIG;
    }

    return EXIT_CODE_OK;
}
