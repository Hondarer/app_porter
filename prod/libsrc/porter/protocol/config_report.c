/**
 *******************************************************************************
 *  @file           config_report.c
 *  @brief          設定の誤りの報告と RFC 6901 JSON Pointer 構築を実装します。
 *******************************************************************************
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <cplat/crt/stdlib.h>

#include <porter/porter_result.h>
#include <porter/infra/potr_trace.h>
#include <porter/protocol/config_report.h>

/** トレース出力用の制御文字エスケープ後パス領域上限 [byte]。 */
#define CONFIG_REPORT_TRACE_TEXT_CAP 512U

static void escape_for_trace(char *dest, size_t dest_size, const char *src);
static size_t measure_pointer_segment(const char *segment);
static void write_pointer_segment(char *dest, const char *segment);

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_config_reporter_init(potr_internal_config_reporter *reporter, const char *config_path,
                                        potr_config_issue_fn on_issue, void *user_data, int trace_errors)
{
    if (reporter == NULL)
    {
        return;
    }

    reporter->config_path = (config_path != NULL) ? config_path : "(null)";
    reporter->on_issue = on_issue;
    reporter->user_data = user_data;
    reporter->issue_count = 0U;
    reporter->trace_errors = trace_errors;
    reporter->pad = 0U;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_config_report_issue(potr_internal_config_reporter *reporter, potr_config_issue_kind kind,
                                       int has_service_id, int64_t service_id, const char *key_path,
                                       const char *message, const char *expected, uint32_t line, uint32_t column)
{
    potr_config_issue issue;
    const char *safe_path;
    const char *safe_message;

    if (reporter == NULL)
    {
        return;
    }

    safe_path = (key_path != NULL) ? key_path : "";
    safe_message = (message != NULL) ? message : "";

    issue.kind = kind;
    issue.has_service_id = (has_service_id != 0) ? 1 : 0;
    issue.service_id = (has_service_id != 0) ? service_id : 0;
    issue.key_path = safe_path;
    issue.message = safe_message;
    issue.expected = expected;
    issue.line = (kind == POTR_CONFIG_ISSUE_SYNTAX) ? line : 0U;
    issue.column = (kind == POTR_CONFIG_ISSUE_SYNTAX) ? column : 0U;

    reporter->issue_count++;

    if (reporter->on_issue != NULL)
    {
        reporter->on_issue(&issue, reporter->user_data);
    }

    if (reporter->trace_errors != 0)
    {
        char escaped_cfg[CONFIG_REPORT_TRACE_TEXT_CAP];
        char escaped_key[CONFIG_REPORT_TRACE_TEXT_CAP];
        char expected_buf[192];
        char service_buf[48];
        const char *key_label;

        escape_for_trace(escaped_cfg, sizeof(escaped_cfg), reporter->config_path);

        if (kind == POTR_CONFIG_ISSUE_SYNTAX)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "config: %s: line %u, column %u: %s", escaped_cfg, (unsigned)issue.line,
                       (unsigned)issue.column, safe_message);
            return;
        }

        if (safe_path[0] == '\0')
        {
            key_label = "(root)";
        }
        else
        {
            escape_for_trace(escaped_key, sizeof(escaped_key), safe_path);
            key_label = escaped_key;
        }

        if (expected != NULL)
        {
            (void)snprintf(expected_buf, sizeof(expected_buf), " (expected: %s)", expected);
            /* 書式長の上限を超える場合も末尾が切り詰められるだけで安全に継続するため、戻り値は明示的に破棄する */
        }
        else
        {
            expected_buf[0] = '\0';
        }

        if (issue.has_service_id != 0)
        {
            (void)snprintf(service_buf, sizeof(service_buf), " (service_id=%" PRId64 ")", issue.service_id);
            /* 固定幅の整数文字列はバッファーに収まるため、戻り値は明示的に破棄する */
        }
        else
        {
            service_buf[0] = '\0';
        }

        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "config: %s: %s: %s%s%s", escaped_cfg, key_label, safe_message,
                   expected_buf, service_buf);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_config_path_push(potr_internal_config_path_buf *buf, const char *parent, const char *segment)
{
    const char *base;
    size_t base_len;
    size_t seg_len;
    size_t total_len;
    char *target;

    if (buf == NULL || segment == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    buf->heap_buf = NULL;
    buf->ptr = NULL;
    base = (parent != NULL) ? parent : "";
    base_len = strlen(base);
    seg_len = measure_pointer_segment(segment);

    if (seg_len == SIZE_MAX || base_len > SIZE_MAX - 2U || seg_len > SIZE_MAX - base_len - 2U)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    total_len = base_len + 1U + seg_len + 1U;
    if (total_len <= sizeof(buf->inline_buf))
    {
        target = buf->inline_buf;
    }
    else
    {
        target = (char *)cplat_malloc(total_len);
        if (target == NULL)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
        buf->heap_buf = target;
    }

    if (base_len > 0U)
    {
        memcpy(target, base, base_len);
    }
    target[base_len] = '/';
    write_pointer_segment(target + base_len + 1U, segment);
    buf->ptr = target;

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_config_path_push_index(potr_internal_config_path_buf *buf, const char *parent, size_t index)
{
    char idx_str[32];

    (void)snprintf(idx_str, sizeof(idx_str), "%zu", index);
    /* 32 byte バッファーに size_t の十進文字列は必ず収まるため、戻り値は明示的に破棄する */

    return potr_internal_config_path_push(buf, parent, idx_str);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_config_path_dispose(potr_internal_config_path_buf *buf)
{
    if (buf == NULL)
    {
        return;
    }

    if (buf->heap_buf != NULL)
    {
        cplat_free(buf->heap_buf);
        buf->heap_buf = NULL;
    }
    buf->ptr = NULL;
}

/*
 * 制御文字 (0x00..0x1F および 0x7F) を単一行のエスケープ表記へ変換する。
 * ログ インジェクションや改行による 1 行崩れを防ぐために使う。
 */
static void escape_for_trace(char *dest, size_t dest_size, const char *src)
{
    size_t out = 0U;
    size_t i = 0U;
    static const char hex_digits[] = "0123456789ABCDEF";

    if (dest == NULL || dest_size == 0U)
    {
        return;
    }

    if (src == NULL)
    {
        dest[0] = '\0';
        return;
    }

    while (src[i] != '\0')
    {
        unsigned char ch = (unsigned char)src[i];

        if (ch == (unsigned char)'\n')
        {
            if (out + 2U >= dest_size)
            {
                break;
            }
            dest[out++] = '\\';
            dest[out++] = 'n';
        }
        else if (ch == (unsigned char)'\r')
        {
            if (out + 2U >= dest_size)
            {
                break;
            }
            dest[out++] = '\\';
            dest[out++] = 'r';
        }
        else if (ch == (unsigned char)'\t')
        {
            if (out + 2U >= dest_size)
            {
                break;
            }
            dest[out++] = '\\';
            dest[out++] = 't';
        }
        else if (ch < 0x20U || ch == 0x7FU)
        {
            if (out + 4U >= dest_size)
            {
                break;
            }
            dest[out++] = '\\';
            dest[out++] = 'x';
            dest[out++] = hex_digits[(ch >> 4U) & 0x0FU];
            dest[out++] = hex_digits[ch & 0x0FU];
        }
        else
        {
            if (out + 1U >= dest_size)
            {
                break;
            }
            dest[out++] = (char)ch;
        }
        i++;
    }

    if (src[i] != '\0' && dest_size >= 4U)
    {
        dest[dest_size - 4U] = '.';
        dest[dest_size - 3U] = '.';
        dest[dest_size - 2U] = '.';
        dest[dest_size - 1U] = '\0';
    }
    else
    {
        dest[out] = '\0';
    }
}

/*
 * キー文字列を RFC 6901 JSON Pointer のトークンとしてエスケープしつつ書き出す。
 * see: https://www.rfc-editor.org/rfc/rfc6901
 */
static size_t measure_pointer_segment(const char *segment)
{
    size_t len = 0U;
    size_t i = 0U;

    while (segment[i] != '\0')
    {
        if (segment[i] == '~' || segment[i] == '/')
        {
            if (len > SIZE_MAX - 2U)
            {
                return SIZE_MAX;
            }
            len += 2U;
        }
        else
        {
            if (len == SIZE_MAX)
            {
                return SIZE_MAX;
            }
            len += 1U;
        }
        i++;
    }

    return len;
}

static void write_pointer_segment(char *dest, const char *segment)
{
    size_t out = 0U;
    size_t i = 0U;

    while (segment[i] != '\0')
    {
        if (segment[i] == '~')
        {
            dest[out++] = '~';
            dest[out++] = '0';
        }
        else if (segment[i] == '/')
        {
            dest[out++] = '~';
            dest[out++] = '1';
        }
        else
        {
            dest[out++] = segment[i];
        }
        i++;
    }
    dest[out] = '\0';
}
