#ifndef PORTER_PROTOCOL_CONFIG_PARSE_COMMON_H
#define PORTER_PROTOCOL_CONFIG_PARSE_COMMON_H

#include <com_util/crt/stdio.h>
#include <ctype.h>
#include <stddef.h>
#include <string.h>

/** 設定ファイル 1 行の最大長。 */
#define CONFIG_LINE_MAX 256

/** セクション名の最大長。 */
#define CONFIG_SECTION_MAX 64

/** キー名の最大長。 */
#define CONFIG_KEY_MAX 64

/** 値文字列の最大長。 */
#define CONFIG_VAL_MAX 128

/* 読み取り専用で設定ファイルを開く。失敗時は NULL を返す。 */
static FILE *config_open_file_read(const char *path)
{
    if (path == NULL)
    {
        return NULL;
    }

    return com_util_fopen(path, "r", NULL);
}

/* 文字列の先頭・末尾の空白を除去して buf に格納する。 */
static void config_trim(const char *src, char *buf, size_t buf_size)
{
    const char *start;
    size_t      len;

    if (buf == NULL || buf_size == 0)
    {
        return;
    }

    if (src == NULL)
    {
        buf[0] = '\0';
        return;
    }

    start = src;
    while (*start != '\0' && isspace((unsigned char)*start))
    {
        start++;
    }

    len = strlen(start);
    while (len > 0U && isspace((unsigned char)start[len - 1U]))
    {
        len--;
    }

    if (len >= buf_size)
    {
        len = buf_size - 1U;
    }

    memcpy(buf, start, len);
    buf[len] = '\0';
}

/* "[section]" 形式の行から section 名を抽出する。成功時は 1 を返す。 */
static int config_parse_section_name(const char *line,
                                     char       *section_out,
                                     size_t      section_size)
{
    const char *close;
    size_t      section_len;

    if (line == NULL || section_out == NULL || section_size == 0U)
    {
        return 0;
    }

    if (line[0] != '[')
    {
        return 0;
    }

    close = strchr(line, ']');
    if (close == NULL)
    {
        return 0;
    }

    section_len = (size_t)(close - line - 1);
    if (section_len >= section_size)
    {
        section_len = section_size - 1U;
    }

    memcpy(section_out, line + 1, section_len);
    section_out[section_len] = '\0';

    return 1;
}

#endif /* PORTER_PROTOCOL_CONFIG_PARSE_COMMON_H */
