#ifndef PORTER_PROTOCOL_CONFIG_PARSE_KV_COMMON_H
#define PORTER_PROTOCOL_CONFIG_PARSE_KV_COMMON_H

#include "configParseCommon.h"

/* "key = value" 行を解析して key_out, val_out に格納する。成功時は 1 を返す。 */
static int config_parse_kv(const char *line, char *key_out, size_t key_size,
                           char *val_out, size_t val_size)
{
    const char *eq;
    char        key_raw[CONFIG_KEY_MAX];
    size_t      key_len;

    if (line == NULL || key_out == NULL || val_out == NULL)
    {
        return 0;
    }

    eq = strchr(line, '=');
    if (eq == NULL)
    {
        return 0;
    }

    key_len = (size_t)(eq - line);
    if (key_len >= CONFIG_KEY_MAX)
    {
        return 0;
    }

    memcpy(key_raw, line, key_len);
    key_raw[key_len] = '\0';

    config_trim(key_raw, key_out, key_size);
    config_trim(eq + 1, val_out, val_size);

    return 1;
}

#endif /* PORTER_PROTOCOL_CONFIG_PARSE_KV_COMMON_H */
