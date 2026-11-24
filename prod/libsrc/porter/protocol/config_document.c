/**
 *******************************************************************************
 *  @file           config_document.c
 *  @brief          設定ファイルのバイナリ読み取りと JSONC 解析 (段階 1) を実装します。
 *******************************************************************************
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cJSON.h>
#include <cJSON_JSONC.h>

#include <cplat/crt/stdio.h>
#include <cplat/crt/stdlib.h>
#include <cplat/runtime/memory_lock.h>

#include <porter/porter_result.h>
#include <porter/protocol/config_document.h>

/** 1 回の fread() で読むチャンク サイズと初期読み取りバッファー容量 [byte]。 */
#define CONFIG_DOCUMENT_CHUNK_SIZE 4096U

/** 文字列内の "\u0000" 検出で使う字句解析状態。 */
typedef enum jsonc_scan_state
{
    JSONC_SCAN_NORMAL = 0,
    JSONC_SCAN_STRING = 1,
    JSONC_SCAN_LINE_COMMENT = 2,
    JSONC_SCAN_BLOCK_COMMENT = 3
} jsonc_scan_state;

/*
 * byte オフセット (0 始まり) を、1 始まりの行番号と byte 単位の列番号へ変換する。
 * 行区切りは '\n' とし、CRLF では '\r' も通常文字として列を 1 進める。
 */
static void offset_to_line_column(const char *buf, size_t len, size_t offset, uint32_t *line_out, uint32_t *col_out)
{
    size_t limit = (offset <= len) ? offset : len;
    uint32_t line = 1U;
    uint32_t col = 1U;
    size_t i;

    for (i = 0U; i < limit; i++)
    {
        if (buf[i] == '\n')
        {
            line++;
            col = 1U;
        }
        else
        {
            col++;
        }
    }

    *line_out = line;
    *col_out = col;
}

/*
 * コメントを除いた JSON 文字列中の "\u0000" エスケープを検出し、最初の '\' オフセットを返す。
 * "\\" は 2 byte 一組で消費するため、"\\u0000" のようにリテラルのバックスラッシュに続く "u0000" は許可する。
 */
static int find_string_u0000_escape(const char *buf, size_t len, size_t *offset_out)
{
    jsonc_scan_state state = JSONC_SCAN_NORMAL;
    size_t i = 0U;

    while (i < len)
    {
        char ch = buf[i];

        if (state == JSONC_SCAN_NORMAL)
        {
            if (ch == '"')
            {
                state = JSONC_SCAN_STRING;
            }
            else if (ch == '/' && (i + 1U) < len)
            {
                if (buf[i + 1U] == '/')
                {
                    state = JSONC_SCAN_LINE_COMMENT;
                    i += 2U;
                    continue;
                }
                if (buf[i + 1U] == '*')
                {
                    state = JSONC_SCAN_BLOCK_COMMENT;
                    i += 2U;
                    continue;
                }
            }
            i++;
        }
        else if (state == JSONC_SCAN_LINE_COMMENT)
        {
            if (ch == '\n')
            {
                state = JSONC_SCAN_NORMAL;
            }
            i++;
        }
        else if (state == JSONC_SCAN_BLOCK_COMMENT)
        {
            if (ch == '*' && (i + 1U) < len && buf[i + 1U] == '/')
            {
                state = JSONC_SCAN_NORMAL;
                i += 2U;
                continue;
            }
            i++;
        }
        else /* JSONC_SCAN_STRING */
        {
            if (ch == '\\')
            {
                if ((i + 5U) < len && buf[i + 1U] == 'u' && buf[i + 2U] == '0' && buf[i + 3U] == '0' &&
                    buf[i + 4U] == '0' && buf[i + 5U] == '0')
                {
                    *offset_out = i;
                    return 1;
                }
                if ((i + 1U) < len)
                {
                    i += 2U;
                }
                else
                {
                    i++;
                }
            }
            else
            {
                if (ch == '"')
                {
                    state = JSONC_SCAN_NORMAL;
                }
                i++;
            }
        }
    }

    return 0;
}

static int check_no_nul_or_u0000(const char *buf, size_t used, potr_internal_config_reporter *reporter)
{
    size_t i;
    size_t esc_offset = 0U;

    for (i = 0U; i < used; i++)
    {
        if (buf[i] == '\0')
        {
            uint32_t line = 1U;
            uint32_t col = 1U;
            offset_to_line_column(buf, used, i, &line, &col);
            potr_internal_config_report_issue(reporter, POTR_CONFIG_ISSUE_SYNTAX, 0, 0, "",
                                              "NUL byte is not allowed in config file", NULL, line, col);
            return POTR_ERR_INVALID_CONFIG;
        }
    }

    if (find_string_u0000_escape(buf, used, &esc_offset) != 0)
    {
        uint32_t line = 1U;
        uint32_t col = 1U;
        offset_to_line_column(buf, used, esc_offset, &line, &col);
        potr_internal_config_report_issue(reporter, POTR_CONFIG_ISSUE_SYNTAX, 0, 0, "",
                                          "U+0000 escape is not allowed in JSON strings", NULL, line, col);
        return POTR_ERR_INVALID_CONFIG;
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_config_document_load(const char *config_path, potr_internal_config_reporter *reporter,
                                       struct cJSON **root_out)
{
    FILE *fp = NULL;
    char chunk[CONFIG_DOCUMENT_CHUNK_SIZE];
    char *buf = NULL;
    size_t cap = CONFIG_DOCUMENT_CHUNK_SIZE;
    size_t used = 0U;
    int ret = POTR_ERR_UNKNOWN;
    cjson_app_jsonc_status jsonc_status = CJSON_APP_JSONC_SYNTAX_ERROR;
    size_t error_offset = 0U;
    cJSON *root = NULL;

    if (root_out != NULL)
    {
        *root_out = NULL;
    }

    if (config_path == NULL || root_out == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    /*
     * Windows のテキスト モードで 0x1A が終端扱いされることを避けるため、バイナリ モードで開く。
     * see: https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/fopen-wfopen
     */
    fp = cplat_fopen(config_path, "rb", NULL);
    if (fp == NULL)
    {
        return POTR_ERR_IO;
    }

    /* 鍵やパスフレーズの断片が stdio バッファーへ残留するのを防ぐため、バッファリングを無効にする。 */
    if (setvbuf(fp, NULL, _IONBF, 0U) != 0)
    {
        /* 読み取り専用ストリームであり、setvbuf 失敗で破棄するため fclose の戻り値は明示的に破棄する */
        (void)fclose(fp);
        return POTR_ERR_IO;
    }

    buf = (char *)cplat_malloc(cap);
    if (buf == NULL)
    {
        /* メモリ不足で中断する読み取り専用ストリームのため、fclose の戻り値は明示的に破棄する */
        (void)fclose(fp);
        return POTR_ERR_OUT_OF_MEMORY;
    }

    for (;;)
    {
        size_t n = fread(chunk, 1U, sizeof(chunk), fp);

        if (n > 0U)
        {
            if (used > (SIZE_MAX - 1U) - n)
            {
                ret = POTR_ERR_OUT_OF_MEMORY;
                goto cleanup;
            }

            while (used + n + 1U > cap)
            {
                size_t next_cap;
                char *next_buf;

                if (cap > SIZE_MAX / 2U)
                {
                    next_cap = used + n + 1U;
                }
                else
                {
                    next_cap = cap * 2U;
                }

                /* 旧バッファーを解放前に容量全体へゼロ埋めするため、cplat_realloc() は使用しない。 */
                next_buf = (char *)cplat_malloc(next_cap);
                if (next_buf == NULL)
                {
                    ret = POTR_ERR_OUT_OF_MEMORY;
                    goto cleanup;
                }

                if (used > 0U)
                {
                    memcpy(next_buf, buf, used);
                }
                cplat_secure_zero(buf, cap);
                cplat_free(buf);
                buf = next_buf;
                cap = next_cap;
            }

            memcpy(buf + used, chunk, n);
            used += n;
        }

        if (n < sizeof(chunk))
        {
            /*
             * 短縮読み取りが正常な EOF か入出力エラーかを ferror() で区別する。
             * see: https://en.cppreference.com/w/c/io/ferror
             */
            if (ferror(fp) != 0)
            {
                ret = POTR_ERR_IO;
                goto cleanup;
            }
            break;
        }
    }

    /* 読み取り完了後の入力専用ストリームであり、エラーは ferror() で確認済みのため戻り値は明示的に破棄する */
    (void)fclose(fp);
    fp = NULL;

    buf[used] = '\0';

    ret = check_no_nul_or_u0000(buf, used, reporter);
    if (ret != POTR_OK)
    {
        goto cleanup;
    }

    /* cJSON_GetErrorPtr() はプロセスで共有する静的変数でありスレッド セーフでないため使用しない。 */
    root = cjson_app_cJSON_ParseJSONCWithLengthOpts(buf, used, &jsonc_status, &error_offset);
    if (jsonc_status == CJSON_APP_JSONC_OUT_OF_MEMORY)
    {
        if (root != NULL)
        {
            cjson_app_cJSON_Delete(root);
        }
        ret = POTR_ERR_OUT_OF_MEMORY;
        goto cleanup;
    }

    if (jsonc_status == CJSON_APP_JSONC_SYNTAX_ERROR)
    {
        uint32_t line = 1U;
        uint32_t col = 1U;

        if (root != NULL)
        {
            cjson_app_cJSON_Delete(root);
        }
        offset_to_line_column(buf, used, error_offset, &line, &col);
        potr_internal_config_report_issue(reporter, POTR_CONFIG_ISSUE_SYNTAX, 0, 0, "", "JSONC syntax error", NULL,
                                          line, col);
        ret = POTR_ERR_INVALID_CONFIG;
        goto cleanup;
    }

    if (jsonc_status != CJSON_APP_JSONC_OK || root == NULL)
    {
        if (root != NULL)
        {
            cjson_app_cJSON_Delete(root);
        }
        ret = (jsonc_status == CJSON_APP_JSONC_INVALID_ARGUMENT) ? POTR_ERR_UNKNOWN : POTR_ERR_OUT_OF_MEMORY;
        goto cleanup;
    }

    *root_out = root;
    ret = POTR_OK;

cleanup:
    if (fp != NULL)
    {
        /* 異常終了時の読み取り専用ストリーム解放のため、fclose の戻り値は明示的に破棄する */
        (void)fclose(fp);
    }
    cplat_secure_zero(chunk, sizeof(chunk));
    if (buf != NULL)
    {
        cplat_secure_zero(buf, cap);
        cplat_free(buf);
    }
    return ret;
}
