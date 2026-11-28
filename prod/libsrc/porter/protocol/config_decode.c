/**
 *******************************************************************************
 *  @file           config_decode.c
 *  @brief          設定 JSON ツリーのルート・項目解釈 (段階 2 と段階 3) を実装します。
 *******************************************************************************
 */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <cJSON.h>
#include <cJSON_PatchCheck.h>
#include <cJSON_Integer.h>

#include <cplat/crt/stdlib.h>
#include <cplat/crypto/crypto.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/endpoint.h>
#include <cplat/runtime/memory_lock.h>

#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include <porter/protocol/config_check.h>
#include <porter/protocol/config_decode.h>
#include <porter/protocol/config_schema.h>

static int push_service_id(int64_t **arr, size_t *count, size_t *cap, int64_t id);
static int parse_canonical_service_id(const char *key, int64_t *id_out);
static int is_duplicate_sibling(const cJSON *obj, const cJSON *target);
static int has_key_conflict(const cJSON *obj, const cJSON *target);
static int report_with_key(potr_internal_config_reporter *reporter, potr_config_issue_kind kind, int has_service_id,
                           int64_t service_id, const char *parent_path, const char *key_name, const char *message,
                           const char *expected);
static void mark_global_err(potr_internal_config_field_errors *errors, unsigned int field_id);
static void mark_service_err(potr_internal_config_field_errors *errors, unsigned int field_id);
static int parse_u32_field_item(const cJSON *item, const potr_internal_config_field_spec *spec, uint32_t *val_out,
                                potr_config_issue_kind *err_kind_out, const char **err_msg_out);
static void store_numeric_field(void *base, const potr_internal_config_field_spec *spec, uint32_t val);
static int hex_nibble_value(char ch);
static int parse_encrypt_key_value(const char *text, potr_service_def *service);
static int parse_auth_enum_item(const cJSON *item, potr_auth *auth_out, potr_config_issue_kind *err_kind_out,
                                const char **err_msg_out);
static int decode_addr_string(const cJSON *item, char *dest, size_t dest_size, potr_internal_config_reporter *reporter,
                              int64_t service_id, const char *service_path, const char *key_name, const char *expected,
                              int *err_out);
static int inspect_root_structure(const cJSON *root, potr_internal_config_reporter *reporter,
                                  const cJSON **global_obj_out, const cJSON **services_obj_out,
                                  potr_internal_config_id_list *id_list, int *root_has_error);
static int decode_global_object(const cJSON *global_obj, potr_global_config *global,
                                potr_internal_config_field_errors *errors, potr_internal_config_reporter *reporter,
                                int *has_err_out);
static void prescan_service_context(const cJSON *svc_obj, int *has_valid_type, potr_type *resolved_type,
                                    int *has_valid_auth, potr_auth *resolved_auth, int *has_ca_file_key);
static int check_auth_applicability(const potr_internal_config_field_spec *spec, int has_valid_auth, potr_auth auth,
                                    int has_ca_file_key);
static void emit_auth_file(potr_config_file_fn on_auth_file, void *user_data, int64_t service_id,
                           const char *key_path, const char *path);
static int decode_string_array_field(const cJSON *item, int64_t service_id, const char *service_path,
                                     const char *key_name, const char *expected, const char ***arr_out,
                                     size_t *count_out, int is_pinned_certs, potr_config_file_fn on_auth_file,
                                     void *user_data, potr_internal_config_reporter *reporter, int *field_err_out);
static int decode_service_object(const cJSON *svc_obj, const potr_global_config *global, int64_t service_id,
                                 const char *service_path, uint32_t roles, potr_config_file_fn on_auth_file,
                                 void *user_data, const potr_internal_config_field_errors *global_errors,
                                 potr_internal_config_service *owned, potr_internal_config_reporter *reporter,
                                 int *has_err_out);

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_config_decode_root(const struct cJSON *root, potr_internal_config_reporter *reporter,
                                     potr_internal_config_id_list *id_list)
{
    const cJSON *global_obj = NULL;
    const cJSON *services_obj = NULL;
    int root_has_error = 0;

    if (root == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    return inspect_root_structure(root, reporter, &global_obj, &services_obj, id_list, &root_has_error);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_config_decode_validate_all(const struct cJSON *root, uint32_t roles,
                                             potr_config_service_fn on_service, potr_config_file_fn on_auth_file,
                                             void *user_data, potr_internal_config_reporter *reporter)
{
    const cJSON *global_obj = NULL;
    const cJSON *services_obj = NULL;
    int root_has_error = 0;
    int any_error = 0;
    potr_global_config global;
    potr_internal_config_field_errors global_errors;
    int ret;

    if (root == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    memset(&global_errors, 0, sizeof(global_errors));

    ret = inspect_root_structure(root, reporter, &global_obj, &services_obj, NULL, &root_has_error);
    if (ret == POTR_ERR_OUT_OF_MEMORY)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }
    if (root_has_error != 0)
    {
        any_error = 1;
        if (!cjson_app_cJSON_IsObject(root))
        {
            return POTR_ERR_INVALID_CONFIG;
        }
    }

    if (decode_global_object(global_obj, &global, &global_errors, reporter, &any_error) != POTR_OK)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    if (services_obj != NULL)
    {
        const cJSON *svc_child;

        for (svc_child = services_obj->child; svc_child != NULL; svc_child = svc_child->next)
        {
            const char *id_key = (svc_child->string != NULL) ? svc_child->string : "";
            int64_t sid = 0;
            potr_internal_config_path_buf svc_path;
            potr_internal_config_service owned;

            if (has_key_conflict(services_obj, svc_child) != 0 || parse_canonical_service_id(id_key, &sid) == 0)
            {
                continue;
            }

            if (root_has_error == 0 && on_service != NULL)
            {
                on_service(sid, user_data);
            }

            if (potr_internal_config_path_push(&svc_path, "/services", id_key) != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }

            memset(&owned, 0, sizeof(owned));
            ret = decode_service_object(svc_child, &global, sid, svc_path.ptr, roles, on_auth_file, user_data,
                                        &global_errors, &owned, reporter, &any_error);
            potr_internal_config_path_dispose(&svc_path);
            potr_internal_config_service_dispose(&owned);

            if (ret == POTR_ERR_OUT_OF_MEMORY)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
        }
    }

    return (any_error != 0) ? POTR_ERR_INVALID_CONFIG : POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_config_decode_target_service(const struct cJSON *root, int64_t service_id, uint32_t roles,
                                               potr_global_config *global_out,
                                               potr_internal_config_service *service_out,
                                               potr_internal_config_reporter *reporter)
{
    const cJSON *global_obj = NULL;
    const cJSON *services_obj = NULL;
    int root_has_error = 0;
    int global_has_error = 0;
    int target_has_error = 0;
    potr_global_config global;
    potr_internal_config_field_errors global_errors;
    const cJSON *target_node = NULL;
    const char *target_key = NULL;
    int ret;

    if (root == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    memset(&global_errors, 0, sizeof(global_errors));

    ret = inspect_root_structure(root, reporter, &global_obj, &services_obj, NULL, &root_has_error);
    if (ret == POTR_ERR_OUT_OF_MEMORY)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    if (!cjson_app_cJSON_IsObject(root))
    {
        return POTR_ERR_INVALID_CONFIG;
    }

    if (decode_global_object(global_obj, &global, &global_errors, reporter, &global_has_error) != POTR_OK)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    if (services_obj != NULL)
    {
        const cJSON *svc_child;

        for (svc_child = services_obj->child; svc_child != NULL; svc_child = svc_child->next)
        {
            const char *id_key = (svc_child->string != NULL) ? svc_child->string : "";
            int64_t parsed_id = 0;

            if (has_key_conflict(services_obj, svc_child) != 0)
            {
                continue;
            }

            if (parse_canonical_service_id(id_key, &parsed_id) != 0 && parsed_id == service_id)
            {
                target_node = svc_child;
                target_key = id_key;
                break;
            }
        }
    }

    if (target_node == NULL)
    {
        if (root_has_error != 0 || global_has_error != 0)
        {
            return POTR_ERR_INVALID_CONFIG;
        }
        return POTR_ERR_NOT_FOUND;
    }

    {
        potr_internal_config_path_buf svc_path;
        potr_internal_config_service owned;

        if (potr_internal_config_path_push(&svc_path, "/services", target_key) != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }

        memset(&owned, 0, sizeof(owned));
        ret = decode_service_object(target_node, &global, service_id, svc_path.ptr, roles, NULL, NULL, &global_errors,
                                    &owned, reporter, &target_has_error);
        potr_internal_config_path_dispose(&svc_path);

        if (ret == POTR_ERR_OUT_OF_MEMORY)
        {
            potr_internal_config_service_dispose(&owned);
            return POTR_ERR_OUT_OF_MEMORY;
        }

        if (root_has_error != 0 || global_has_error != 0 || target_has_error != 0)
        {
            potr_internal_config_service_dispose(&owned);
            return POTR_ERR_INVALID_CONFIG;
        }

        if (global_out != NULL)
        {
            *global_out = global;
        }
        if (service_out != NULL)
        {
            *service_out = owned;
        }
        else
        {
            potr_internal_config_service_dispose(&owned);
        }
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_config_service_dispose(potr_internal_config_service *loaded)
{
    if (loaded == NULL)
    {
        return;
    }

    if (loaded->pinned_certs != NULL)
    {
        cplat_free(loaded->pinned_certs);
        loaded->pinned_certs = NULL;
    }
    if (loaded->peer_names != NULL)
    {
        cplat_free(loaded->peer_names);
        loaded->peer_names = NULL;
    }
    if (loaded->document != NULL)
    {
        cjson_app_cJSON_Delete(loaded->document);
        loaded->document = NULL;
    }
    cplat_secure_zero(&loaded->definition, sizeof(loaded->definition));
}

/* 配列の拡張時に古い配列の内容を消去せずに再利用できるため、ここでは明示的に確保と複写を行う。 */
static int push_service_id(int64_t **arr, size_t *count, size_t *cap, int64_t id)
{
    if (*count >= *cap)
    {
        size_t next_cap = (*cap == 0U) ? (size_t)POTR_MAX_SERVICES : (*cap * 2U);
        int64_t *next_arr;

        if (*cap > SIZE_MAX / 2U || next_cap > SIZE_MAX / sizeof(int64_t))
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
        next_arr = (int64_t *)cplat_malloc(next_cap * sizeof(int64_t));
        if (next_arr == NULL)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
        if (*count > 0U && *arr != NULL)
        {
            memcpy(next_arr, *arr, *count * sizeof(int64_t));
            cplat_free(*arr);
        }
        *arr = next_arr;
        *cap = next_cap;
    }

    (*arr)[*count] = id;
    (*count)++;
    return POTR_OK;
}

static int parse_canonical_service_id(const char *key, int64_t *id_out)
{
    size_t i = 0U;

    if (key == NULL || key[0] == '\0')
    {
        return 0;
    }

    if (key[0] == '-')
    {
        if (key[1] < '1' || key[1] > '9')
        {
            return 0;
        }
        i = 2U;
    }
    else if (key[0] == '0')
    {
        if (key[1] != '\0')
        {
            return 0;
        }
        *id_out = 0;
        return 1;
    }
    else if (key[0] >= '1' && key[0] <= '9')
    {
        i = 1U;
    }
    else
    {
        return 0;
    }

    while (key[i] != '\0')
    {
        if (key[i] < '0' || key[i] > '9')
        {
            return 0;
        }
        i++;
    }

    return (cplat_parse_int64(id_out, key, 10) == CPLAT_OK) ? 1 : 0;
}

static int is_duplicate_sibling(const cJSON *obj, const cJSON *target)
{
    const cJSON *prev;

    if (target->string == NULL)
    {
        return 0;
    }

    for (prev = obj->child; prev != NULL && prev != target; prev = prev->next)
    {
        if (prev->string != NULL && strcmp(prev->string, target->string) == 0)
        {
            return 1;
        }
    }

    return 0;
}

static int has_key_conflict(const cJSON *obj, const cJSON *target)
{
    const cJSON *cur;

    if (target->string == NULL)
    {
        return 0;
    }

    for (cur = obj->child; cur != NULL; cur = cur->next)
    {
        if (cur != target && cur->string != NULL && strcmp(cur->string, target->string) == 0)
        {
            return 1;
        }
    }

    return 0;
}

static int report_with_key(potr_internal_config_reporter *reporter, potr_config_issue_kind kind, int has_service_id,
                           int64_t service_id, const char *parent_path, const char *key_name, const char *message,
                           const char *expected)
{
    potr_internal_config_path_buf pbuf;
    int ret;

    ret = potr_internal_config_path_push(&pbuf, parent_path, key_name);
    if (ret != POTR_OK)
    {
        return ret;
    }
    potr_internal_config_report_issue(reporter, kind, has_service_id, service_id, pbuf.ptr, message, expected, 0U, 0U);
    potr_internal_config_path_dispose(&pbuf);
    return POTR_OK;
}

static void mark_global_err(potr_internal_config_field_errors *errors, unsigned int field_id)
{
    if (errors != NULL && field_id < (unsigned int)POTR_CONFIG_GFIELD_COUNT)
    {
        errors->global_mask |= ((uint32_t)1U << field_id);
    }
}

static void mark_service_err(potr_internal_config_field_errors *errors, unsigned int field_id)
{
    if (errors != NULL && field_id < (unsigned int)POTR_CONFIG_SFIELD_COUNT)
    {
        errors->service_mask |= ((uint64_t)1ULL << field_id);
    }
}

static int parse_u32_field_item(const cJSON *item, const potr_internal_config_field_spec *spec, uint32_t *val_out,
                                potr_config_issue_kind *err_kind_out, const char **err_msg_out)
{
    if (cjson_app_cJSON_IsNumber(item))
    {
        int64_t i64 = 0;
        uint64_t u64 = 0U;

        if (cjson_app_cJSON_GetInt64Value(item, &i64))
        {
            if (spec->allow_minus_one != 0 && i64 == -1)
            {
                *val_out = POTR_FLOW_CONTROL_DISABLED;
                return 1;
            }
            if (i64 < (int64_t)spec->min_val || i64 > (int64_t)spec->max_val)
            {
                *err_kind_out = POTR_CONFIG_ISSUE_OUT_OF_RANGE;
                *err_msg_out = "value is out of range";
                return 0;
            }
            *val_out = (uint32_t)i64;
            return 1;
        }

        if (cjson_app_cJSON_GetUInt64Value(item, &u64) || cjson_app_cJSON_HasExactInteger(item) ||
            (isfinite(item->valuedouble) && trunc(item->valuedouble) == item->valuedouble))
        {
            *err_kind_out = POTR_CONFIG_ISSUE_OUT_OF_RANGE;
            *err_msg_out = "value is out of range";
            return 0;
        }

        *err_kind_out = POTR_CONFIG_ISSUE_TYPE_MISMATCH;
        *err_msg_out = "expected integer or numeric string";
        return 0;
    }

    if (cjson_app_cJSON_IsString(item) && item->valuestring != NULL)
    {
        const char *text = item->valuestring;
        const char *trimmed = text;
        uint64_t u64 = 0U;
        int parse_ret;

        while (*trimmed != '\0' && isspace((unsigned char)*trimmed) != 0)
        {
            trimmed++;
        }

        if (*trimmed == '\0')
        {
            *err_kind_out = POTR_CONFIG_ISSUE_INVALID_VALUE;
            *err_msg_out = "invalid integer string";
            return 0;
        }

        if (*trimmed == '-')
        {
            int64_t s64 = 0;

            if (spec->allow_minus_one != 0 && strcmp(trimmed, "-1") == 0)
            {
                *val_out = POTR_FLOW_CONTROL_DISABLED;
                return 1;
            }
            if (strcmp(trimmed, "-0") == 0)
            {
                *err_kind_out = POTR_CONFIG_ISSUE_INVALID_VALUE;
                *err_msg_out = "negative zero is not allowed";
                return 0;
            }
            parse_ret = cplat_parse_int64(&s64, text, 10);
            if (parse_ret == CPLAT_ERR_INVALID_INTEGER)
            {
                *err_kind_out = POTR_CONFIG_ISSUE_INVALID_VALUE;
                *err_msg_out = "invalid integer string";
                return 0;
            }
            *err_kind_out = POTR_CONFIG_ISSUE_OUT_OF_RANGE;
            *err_msg_out = "value is out of range";
            return 0;
        }

        parse_ret = cplat_parse_uint64(&u64, text, 10);
        if (parse_ret == CPLAT_ERR_INVALID_INTEGER)
        {
            *err_kind_out = POTR_CONFIG_ISSUE_INVALID_VALUE;
            *err_msg_out = "invalid integer string";
            return 0;
        }
        if (parse_ret == CPLAT_ERR_OUT_OF_RANGE || u64 < (uint64_t)spec->min_val || u64 > (uint64_t)spec->max_val)
        {
            *err_kind_out = POTR_CONFIG_ISSUE_OUT_OF_RANGE;
            *err_msg_out = "value is out of range";
            return 0;
        }

        *val_out = (uint32_t)u64;
        return 1;
    }

    *err_kind_out = POTR_CONFIG_ISSUE_TYPE_MISMATCH;
    *err_msg_out = "expected integer or numeric string";
    return 0;
}

static void store_numeric_field(void *base, const potr_internal_config_field_spec *spec, uint32_t val)
{
    uint8_t *dst = (uint8_t *)base + spec->offset;

    if (spec->kind == POTR_CONFIG_VAL_U8)
    {
        uint8_t v = (uint8_t)val;
        memcpy(dst, &v, sizeof(v));
    }
    else if (spec->kind == POTR_CONFIG_VAL_U16)
    {
        uint16_t v = (uint16_t)val;
        memcpy(dst, &v, sizeof(v));
    }
    else
    {
        memcpy(dst, &val, sizeof(val));
    }
}

static int hex_nibble_value(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return ch - 'A' + 10;
    }
    return -1;
}

static int parse_encrypt_key_value(const char *text, potr_service_def *service)
{
    size_t len = strlen(text);
    size_t i;
    int derive_ret;

    if (len == (size_t)POTR_CRYPTO_KEY_SIZE * 2U)
    {
        int all_hex = 1;
        for (i = 0U; i < len; i++)
        {
            if (hex_nibble_value(text[i]) < 0)
            {
                all_hex = 0;
                break;
            }
        }

        if (all_hex != 0)
        {
            for (i = 0U; i < (size_t)POTR_CRYPTO_KEY_SIZE; i++)
            {
                int hi = hex_nibble_value(text[i * 2U]);
                int lo = hex_nibble_value(text[i * 2U + 1U]);
                service->encrypt_key[i] = (uint8_t)(((unsigned int)hi << 4U) | (unsigned int)lo);
            }
            service->encrypt_enabled = 1;
            return POTR_OK;
        }
    }

    derive_ret = cplat_passphrase_to_key(service->encrypt_key, (const uint8_t *)text, len);
    if (derive_ret != CPLAT_OK)
    {
        cplat_secure_zero(service->encrypt_key, sizeof(service->encrypt_key));
        service->encrypt_enabled = 0;
        if (derive_ret == CPLAT_ERR_OUT_OF_MEMORY)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
        return POTR_ERR_INVALID_CONFIG;
    }

    service->encrypt_enabled = 1;
    return POTR_OK;
}

static int parse_auth_enum_item(const cJSON *item, potr_auth *auth_out, potr_config_issue_kind *err_kind_out,
                                const char **err_msg_out)
{
    if (!cjson_app_cJSON_IsString(item) || item->valuestring == NULL)
    {
        *err_kind_out = POTR_CONFIG_ISSUE_TYPE_MISMATCH;
        *err_msg_out = "expected string";
        return 0;
    }

    if (strcmp(item->valuestring, "psk") == 0)
    {
        *auth_out = POTR_AUTH_PSK;
        return 1;
    }
    if (strcmp(item->valuestring, "cert") == 0)
    {
        *auth_out = POTR_AUTH_CERT;
        return 1;
    }

    *err_kind_out = POTR_CONFIG_ISSUE_INVALID_VALUE;
    *err_msg_out = "unknown authentication mode";
    return 0;
}

static int decode_addr_string(const cJSON *item, char *dest, size_t dest_size, potr_internal_config_reporter *reporter,
                              int64_t service_id, const char *service_path, const char *key_name, const char *expected,
                              int *err_out)
{
    size_t len;

    if (!cjson_app_cJSON_IsString(item) || item->valuestring == NULL)
    {
        *err_out = 1;
        return report_with_key(reporter, POTR_CONFIG_ISSUE_TYPE_MISMATCH, 1, service_id, service_path, key_name,
                               "expected string", expected);
    }

    len = strlen(item->valuestring);
    if (len == 0U)
    {
        *err_out = 1;
        return report_with_key(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, 1, service_id, service_path, key_name,
                               "string must not be empty", expected);
    }

    if (len >= dest_size)
    {
        *err_out = 1;
        return report_with_key(reporter, POTR_CONFIG_ISSUE_OUT_OF_RANGE, 1, service_id, service_path, key_name,
                               "string exceeds 63 bytes", expected);
    }

    memcpy(dest, item->valuestring, len + 1U);
    return POTR_OK;
}

static int inspect_root_structure(const cJSON *root, potr_internal_config_reporter *reporter,
                                  const cJSON **global_obj_out, const cJSON **services_obj_out,
                                  potr_internal_config_id_list *id_list, int *root_has_error)
{
    const cJSON *global_obj = NULL;
    const cJSON *services_obj = NULL;
    const cJSON *child;
    int64_t *ids = NULL;
    size_t count = 0U;
    size_t cap = 0U;

    *global_obj_out = NULL;
    *services_obj_out = NULL;
    *root_has_error = 0;

    if (id_list != NULL)
    {
        id_list->ids = NULL;
        id_list->count = 0U;
    }

    if (!cjson_app_cJSON_IsObject(root))
    {
        *root_has_error = 1;
        potr_internal_config_report_issue(reporter, POTR_CONFIG_ISSUE_TYPE_MISMATCH, 0, 0, "",
                                          "root value must be a JSON object", "object", 0U, 0U);
        return POTR_ERR_INVALID_CONFIG;
    }

    for (child = root->child; child != NULL; child = child->next)
    {
        const char *key = (child->string != NULL) ? child->string : "";

        if (is_duplicate_sibling(root, child) != 0)
        {
            *root_has_error = 1;
            if (report_with_key(reporter, POTR_CONFIG_ISSUE_DUPLICATE_KEY, 0, 0, "", key, "duplicate key", NULL) !=
                POTR_OK)
            {
                cplat_free(ids);
                return POTR_ERR_OUT_OF_MEMORY;
            }
            continue;
        }

        if (strcmp(key, "global") == 0)
        {
            if (!cjson_app_cJSON_IsObject(child))
            {
                *root_has_error = 1;
                if (report_with_key(reporter, POTR_CONFIG_ISSUE_TYPE_MISMATCH, 0, 0, "", "global",
                                    "global must be a JSON object", "object") != POTR_OK)
                {
                    cplat_free(ids);
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
            else
            {
                global_obj = child;
            }
        }
        else if (strcmp(key, "services") == 0)
        {
            if (!cjson_app_cJSON_IsObject(child))
            {
                *root_has_error = 1;
                if (report_with_key(reporter, POTR_CONFIG_ISSUE_TYPE_MISMATCH, 0, 0, "", "services",
                                    "services must be a JSON object", "object") != POTR_OK)
                {
                    cplat_free(ids);
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
            else
            {
                services_obj = child;
            }
        }
        else
        {
            *root_has_error = 1;
            if (report_with_key(reporter, POTR_CONFIG_ISSUE_UNKNOWN_KEY, 0, 0, "", key, "unknown key", NULL) != POTR_OK)
            {
                cplat_free(ids);
                return POTR_ERR_OUT_OF_MEMORY;
            }
        }
    }

    if (services_obj != NULL)
    {
        const cJSON *svc_child;

        for (svc_child = services_obj->child; svc_child != NULL; svc_child = svc_child->next)
        {
            const char *id_key = (svc_child->string != NULL) ? svc_child->string : "";
            int64_t parsed_id = 0;
            int valid_id = parse_canonical_service_id(id_key, &parsed_id);

            if (is_duplicate_sibling(services_obj, svc_child) != 0)
            {
                *root_has_error = 1;
                if (report_with_key(reporter, POTR_CONFIG_ISSUE_DUPLICATE_KEY, valid_id, (valid_id != 0) ? parsed_id : 0,
                                    "/services", id_key, "duplicate key", NULL) != POTR_OK)
                {
                    cplat_free(ids);
                    return POTR_ERR_OUT_OF_MEMORY;
                }
                continue;
            }

            if (valid_id == 0)
            {
                *root_has_error = 1;
                if (report_with_key(reporter, POTR_CONFIG_ISSUE_INVALID_SERVICE_ID, 0, 0, "/services", id_key,
                                    "service id must be a canonical decimal int64",
                                    "canonical decimal int64 (-9223372036854775808..9223372036854775807)") != POTR_OK)
                {
                    cplat_free(ids);
                    return POTR_ERR_OUT_OF_MEMORY;
                }
                continue;
            }

            if (id_list != NULL)
            {
                if (push_service_id(&ids, &count, &cap, parsed_id) != POTR_OK)
                {
                    cplat_free(ids);
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
        }
    }

    *global_obj_out = global_obj;
    *services_obj_out = services_obj;

    if (id_list != NULL)
    {
        if (*root_has_error != 0)
        {
            cplat_free(ids);
            id_list->ids = NULL;
            id_list->count = 0U;
        }
        else
        {
            id_list->ids = ids;
            id_list->count = count;
        }
    }
    else
    {
        cplat_free(ids);
    }

    return (*root_has_error != 0) ? POTR_ERR_INVALID_CONFIG : POTR_OK;
}

static int decode_global_object(const cJSON *global_obj, potr_global_config *global,
                                potr_internal_config_field_errors *errors, potr_internal_config_reporter *reporter,
                                int *has_err_out)
{
    const cJSON *child;
    int ret;

    potr_internal_config_global_init_defaults(global);
    if (errors != NULL)
    {
        errors->global_mask = 0U;
    }

    if (global_obj == NULL)
    {
        ret = potr_internal_config_check_global(global, errors, "/global", reporter);
        if (ret == POTR_ERR_OUT_OF_MEMORY)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
        if (ret != POTR_OK)
        {
            *has_err_out = 1;
        }
        return POTR_OK;
    }

    for (child = global_obj->child; child != NULL; child = child->next)
    {
        const char *key = (child->string != NULL) ? child->string : "";
        const potr_internal_config_field_spec *spec;
        uint32_t parsed_u32 = 0U;
        potr_config_issue_kind err_kind = POTR_CONFIG_ISSUE_INVALID_VALUE;
        const char *err_msg = NULL;

        if (is_duplicate_sibling(global_obj, child) != 0)
        {
            *has_err_out = 1;
            if (report_with_key(reporter, POTR_CONFIG_ISSUE_DUPLICATE_KEY, 0, 0, "/global", key, "duplicate key",
                                NULL) != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
            continue;
        }

        spec = potr_internal_config_schema_find_global(key);
        if (spec == NULL)
        {
            *has_err_out = 1;
            if (report_with_key(reporter, POTR_CONFIG_ISSUE_UNKNOWN_KEY, 0, 0, "/global", key, "unknown key", NULL) !=
                POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
            continue;
        }

        if (parse_u32_field_item(child, spec, &parsed_u32, &err_kind, &err_msg) == 0)
        {
            *has_err_out = 1;
            mark_global_err(errors, spec->id);
            if (report_with_key(reporter, err_kind, 0, 0, "/global", key, err_msg, spec->expected) != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
            continue;
        }

        store_numeric_field(global, spec, parsed_u32);
    }

    ret = potr_internal_config_check_global(global, errors, "/global", reporter);
    if (ret == POTR_ERR_OUT_OF_MEMORY)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }
    if (ret != POTR_OK)
    {
        *has_err_out = 1;
    }

    return POTR_OK;
}

static void prescan_service_context(const cJSON *svc_obj, int *has_valid_type, potr_type *resolved_type,
                                    int *has_valid_auth, potr_auth *resolved_auth, int *has_ca_file_key)
{
    const cJSON *child;
    int seen_type = 0;
    int seen_auth = 0;

    *has_valid_type = 0;
    *resolved_type = (potr_type)0;
    *has_valid_auth = 1;
    *resolved_auth = POTR_AUTH_PSK;
    *has_ca_file_key = 0;

    for (child = svc_obj->child; child != NULL; child = child->next)
    {
        const char *key = (child->string != NULL) ? child->string : "";

        if (seen_type == 0 && strcmp(key, "type") == 0)
        {
            seen_type = 1;
            if (cjson_app_cJSON_IsString(child) && child->valuestring != NULL)
            {
                potr_type t = potr_internal_config_type_from_string(child->valuestring);
                if (t != (potr_type)0)
                {
                    *has_valid_type = 1;
                    *resolved_type = t;
                }
            }
        }
        else if (seen_auth == 0 && strcmp(key, "auth") == 0)
        {
            potr_config_issue_kind unused_kind = POTR_CONFIG_ISSUE_INVALID_VALUE;
            const char *unused_msg = NULL;
            potr_auth a = POTR_AUTH_PSK;

            seen_auth = 1;
            if (parse_auth_enum_item(child, &a, &unused_kind, &unused_msg) != 0)
            {
                *has_valid_auth = 1;
                *resolved_auth = a;
            }
            else
            {
                *has_valid_auth = 0;
            }
        }
        else if (*has_ca_file_key == 0 && strcmp(key, "ca_file") == 0)
        {
            *has_ca_file_key = 1;
        }
    }
}

static int check_auth_applicability(const potr_internal_config_field_spec *spec, int has_valid_auth, potr_auth auth,
                                    int has_ca_file_key)
{
    if (has_valid_auth == 0 || spec->auth_req == POTR_CONFIG_AUTH_ANY)
    {
        return 1;
    }

    if (spec->auth_req == POTR_CONFIG_AUTH_PSK_ONLY)
    {
        return (auth == POTR_AUTH_PSK) ? 1 : 0;
    }

    if (auth != POTR_AUTH_CERT)
    {
        return 0;
    }

    if (spec->auth_req == POTR_CONFIG_AUTH_CERT_WITH_CA && has_ca_file_key == 0)
    {
        return 0;
    }

    return 1;
}

static void emit_auth_file(potr_config_file_fn on_auth_file, void *user_data, int64_t service_id,
                           const char *key_path, const char *path)
{
    potr_config_file_ref ref;

    if (on_auth_file == NULL || key_path == NULL || path == NULL)
    {
        return;
    }

    ref.service_id = service_id;
    ref.key_path = key_path;
    ref.path = path;
    on_auth_file(&ref, user_data);
}

static int decode_string_array_field(const cJSON *item, int64_t service_id, const char *service_path,
                                     const char *key_name, const char *expected, const char ***arr_out,
                                     size_t *count_out, int is_pinned_certs, potr_config_file_fn on_auth_file,
                                     void *user_data, potr_internal_config_reporter *reporter, int *field_err_out)
{
    int count_int;
    size_t count;
    size_t i;
    potr_internal_config_path_buf arr_path;
    const cJSON *elem;
    const char **ptrs = NULL;
    int any_elem_err = 0;

    *arr_out = NULL;
    *count_out = 0U;
    *field_err_out = 0;

    if (!cjson_app_cJSON_IsArray(item))
    {
        *field_err_out = 1;
        return report_with_key(reporter, POTR_CONFIG_ISSUE_TYPE_MISMATCH, 1, service_id, service_path, key_name,
                               "expected array of strings", expected);
    }

    count_int = cjson_app_cJSON_GetArraySize(item);
    if (count_int <= 0)
    {
        *field_err_out = 1;
        return report_with_key(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, 1, service_id, service_path, key_name,
                               "array must contain at least one entry", expected);
    }

    count = (size_t)count_int;
    if (count > SIZE_MAX / sizeof(char *))
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    if (potr_internal_config_path_push(&arr_path, service_path, key_name) != POTR_OK)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    ptrs = (const char **)cplat_malloc(count * sizeof(const char *));
    if (ptrs == NULL)
    {
        potr_internal_config_path_dispose(&arr_path);
        return POTR_ERR_OUT_OF_MEMORY;
    }

    i = 0U;
    for (elem = item->child; elem != NULL && i < count; elem = elem->next, i++)
    {
        potr_internal_config_path_buf elem_path;

        if (potr_internal_config_path_push_index(&elem_path, arr_path.ptr, i) != POTR_OK)
        {
            cplat_free(ptrs);
            potr_internal_config_path_dispose(&arr_path);
            return POTR_ERR_OUT_OF_MEMORY;
        }

        if (!cjson_app_cJSON_IsString(elem) || elem->valuestring == NULL)
        {
            any_elem_err = 1;
            potr_internal_config_report_issue(reporter, POTR_CONFIG_ISSUE_TYPE_MISMATCH, 1, service_id, elem_path.ptr,
                                              "array element must be a string", "non-empty string", 0U, 0U);
        }
        else if (elem->valuestring[0] == '\0')
        {
            any_elem_err = 1;
            potr_internal_config_report_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, 1, service_id, elem_path.ptr,
                                              "string must not be empty", "non-empty string", 0U, 0U);
        }
        else
        {
            ptrs[i] = elem->valuestring;
            if (is_pinned_certs != 0)
            {
                emit_auth_file(on_auth_file, user_data, service_id, elem_path.ptr, elem->valuestring);
            }
        }

        potr_internal_config_path_dispose(&elem_path);
    }

    potr_internal_config_path_dispose(&arr_path);

    if (any_elem_err != 0)
    {
        cplat_free(ptrs);
        *field_err_out = 1;
        return POTR_OK;
    }

    *arr_out = ptrs;
    *count_out = count;
    return POTR_OK;
}

static int decode_service_object(const cJSON *svc_obj, const potr_global_config *global, int64_t service_id,
                                 const char *service_path, uint32_t roles, potr_config_file_fn on_auth_file,
                                 void *user_data, const potr_internal_config_field_errors *global_errors,
                                 potr_internal_config_service *owned, potr_internal_config_reporter *reporter,
                                 int *has_err_out)
{
    potr_internal_config_field_errors errors;
    int has_valid_type = 0;
    potr_type resolved_type = (potr_type)0;
    int has_valid_auth = 1;
    potr_auth resolved_auth = POTR_AUTH_PSK;
    int has_ca_file_key = 0;
    int seen_type = 0;
    int seen_dst_port = 0;
    const cJSON *child;
    int ret;

    memset(&errors, 0, sizeof(errors));
    if (global_errors != NULL)
    {
        errors.global_mask = global_errors->global_mask;
    }

    potr_internal_config_service_init_defaults(&owned->definition, service_id);

    if (!cjson_app_cJSON_IsObject(svc_obj))
    {
        *has_err_out = 1;
        potr_internal_config_report_issue(reporter, POTR_CONFIG_ISSUE_TYPE_MISMATCH, 1, service_id, service_path,
                                          "service definition must be a JSON object", "object", 0U, 0U);
        return POTR_OK;
    }

    prescan_service_context(svc_obj, &has_valid_type, &resolved_type, &has_valid_auth, &resolved_auth,
                            &has_ca_file_key);

    for (child = svc_obj->child; child != NULL; child = child->next)
    {
        const char *key = (child->string != NULL) ? child->string : "";
        const potr_internal_config_field_spec *spec;
        potr_internal_config_path_key_kind path_kind = POTR_CONFIG_PATH_SRC_ADDR;
        size_t path_idx = 0U;
        int path_ret;

        if (is_duplicate_sibling(svc_obj, child) != 0)
        {
            *has_err_out = 1;
            if (report_with_key(reporter, POTR_CONFIG_ISSUE_DUPLICATE_KEY, 1, service_id, service_path, key,
                                "duplicate key", NULL) != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
            continue;
        }

        path_ret = potr_internal_config_schema_parse_path_key(key, &path_kind, &path_idx);
        if (path_ret == POTR_OK)
        {
            if (has_valid_type != 0 && potr_internal_config_path_key_is_applicable(path_kind, resolved_type) == 0)
            {
                *has_err_out = 1;
                if (path_kind == POTR_CONFIG_PATH_SRC_ADDR)
                {
                    errors.src_addr_mask |= (uint8_t)(1U << path_idx);
                }
                else if (path_kind == POTR_CONFIG_PATH_DST_ADDR)
                {
                    errors.dst_addr_mask |= (uint8_t)(1U << path_idx);
                }
                else
                {
                    errors.path_group_mask |= (uint8_t)(1U << path_idx);
                }
                if (report_with_key(reporter, POTR_CONFIG_ISSUE_NOT_APPLICABLE, 1, service_id, service_path, key,
                                    "key is not applicable to the service type", NULL) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
                continue;
            }

            if (path_kind == POTR_CONFIG_PATH_SRC_ADDR)
            {
                int addr_err = 0;
                if (decode_addr_string(child, owned->definition.src_addr[path_idx],
                                       sizeof(owned->definition.src_addr[path_idx]), reporter, service_id, service_path,
                                       key, "1..63 bytes", &addr_err) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
                if (addr_err != 0)
                {
                    *has_err_out = 1;
                    errors.src_addr_mask |= (uint8_t)(1U << path_idx);
                }
            }
            else if (path_kind == POTR_CONFIG_PATH_DST_ADDR)
            {
                int addr_err = 0;
                if (decode_addr_string(child, owned->definition.dst_addr[path_idx],
                                       sizeof(owned->definition.dst_addr[path_idx]), reporter, service_id, service_path,
                                       key, "1..63 bytes", &addr_err) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
                if (addr_err != 0)
                {
                    *has_err_out = 1;
                    errors.dst_addr_mask |= (uint8_t)(1U << path_idx);
                }
            }
            else /* POTR_CONFIG_PATH_GROUP */
            {
                static const potr_internal_config_field_spec pg_spec = {
                    0U, 0U, "path_group", POTR_CONFIG_VAL_U8, 1U, POTR_MAX_PATH_GROUP, 1U, 0, 1, 0U,
                    POTR_CONFIG_AUTH_ANY, 0U, "1..4"};
                uint32_t pg_val = 0U;
                potr_config_issue_kind err_kind = POTR_CONFIG_ISSUE_INVALID_VALUE;
                const char *err_msg = NULL;

                errors.has_path_group_bitmask |= (uint8_t)(1U << path_idx);
                if (parse_u32_field_item(child, &pg_spec, &pg_val, &err_kind, &err_msg) == 0)
                {
                    *has_err_out = 1;
                    errors.path_group_mask |= (uint8_t)(1U << path_idx);
                    if (report_with_key(reporter, err_kind, 1, service_id, service_path, key, err_msg,
                                        pg_spec.expected) != POTR_OK)
                    {
                        return POTR_ERR_OUT_OF_MEMORY;
                    }
                }
                else
                {
                    owned->definition.path_group[path_idx] = (uint8_t)pg_val;
                }
            }
            continue;
        }
        else if (path_ret == POTR_ERR_INVALID_CONFIG)
        {
            *has_err_out = 1;
            if (report_with_key(reporter, POTR_CONFIG_ISSUE_OUT_OF_RANGE, 1, service_id, service_path, key,
                                "path index is out of range", "1..8") != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
            continue;
        }

        spec = potr_internal_config_schema_find_service(key);
        if (spec == NULL)
        {
            *has_err_out = 1;
            if (report_with_key(reporter, POTR_CONFIG_ISSUE_UNKNOWN_KEY, 1, service_id, service_path, key,
                                "unknown key", NULL) != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
            continue;
        }

        if (spec->id == POTR_CONFIG_SFIELD_TYPE)
        {
            seen_type = 1;
        }
        else if (spec->id == POTR_CONFIG_SFIELD_DST_PORT)
        {
            seen_dst_port = 1;
        }

        if (has_valid_type != 0 && potr_internal_config_type_is_applicable(resolved_type, spec->applicable_types) == 0)
        {
            *has_err_out = 1;
            mark_service_err(&errors, spec->id);
            if (report_with_key(reporter, POTR_CONFIG_ISSUE_NOT_APPLICABLE, 1, service_id, service_path, key,
                                "key is not applicable to the service type", NULL) != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
            continue;
        }

        if (check_auth_applicability(spec, has_valid_auth, resolved_auth, has_ca_file_key) == 0)
        {
            *has_err_out = 1;
            mark_service_err(&errors, spec->id);
            if (report_with_key(reporter, POTR_CONFIG_ISSUE_NOT_APPLICABLE, 1, service_id, service_path, key,
                                "key is not applicable to authentication settings", NULL) != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
            continue;
        }

        switch (spec->kind)
        {
        case POTR_CONFIG_VAL_U8:
        case POTR_CONFIG_VAL_U16:
        case POTR_CONFIG_VAL_U32:
        {
            uint32_t parsed_u32 = 0U;
            potr_config_issue_kind err_kind = POTR_CONFIG_ISSUE_INVALID_VALUE;
            const char *err_msg = NULL;

            if (parse_u32_field_item(child, spec, &parsed_u32, &err_kind, &err_msg) == 0)
            {
                *has_err_out = 1;
                mark_service_err(&errors, spec->id);
                if (report_with_key(reporter, err_kind, 1, service_id, service_path, key, err_msg, spec->expected) !=
                    POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
            else
            {
                store_numeric_field(&owned->definition, spec, parsed_u32);
            }
            break;
        }
        case POTR_CONFIG_VAL_TYPE_ENUM:
        {
            potr_type parsed_type;
            if (!cjson_app_cJSON_IsString(child) || child->valuestring == NULL)
            {
                *has_err_out = 1;
                mark_service_err(&errors, spec->id);
                if (report_with_key(reporter, POTR_CONFIG_ISSUE_TYPE_MISMATCH, 1, service_id, service_path, key,
                                    "expected string", spec->expected) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
                break;
            }
            parsed_type = potr_internal_config_type_from_string(child->valuestring);
            if (parsed_type == (potr_type)0)
            {
                *has_err_out = 1;
                mark_service_err(&errors, spec->id);
                if (report_with_key(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, 1, service_id, service_path, key,
                                    "unknown service type", spec->expected) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
            else
            {
                owned->definition.type = parsed_type;
            }
            break;
        }
        case POTR_CONFIG_VAL_AUTH_ENUM:
        {
            potr_auth parsed_auth = POTR_AUTH_PSK;
            potr_config_issue_kind err_kind = POTR_CONFIG_ISSUE_INVALID_VALUE;
            const char *err_msg = NULL;

            if (parse_auth_enum_item(child, &parsed_auth, &err_kind, &err_msg) == 0)
            {
                *has_err_out = 1;
                mark_service_err(&errors, spec->id);
                if (report_with_key(reporter, err_kind, 1, service_id, service_path, key, err_msg, spec->expected) !=
                    POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
            else
            {
                owned->definition.auth = parsed_auth;
            }
            break;
        }
        case POTR_CONFIG_VAL_MULTICAST_GROUP:
        {
            int addr_err = 0;
            uint32_t net_ip = 0U;

            if (decode_addr_string(child, owned->definition.multicast_group, sizeof(owned->definition.multicast_group),
                                   reporter, service_id, service_path, key, spec->expected, &addr_err) != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
            if (addr_err != 0)
            {
                *has_err_out = 1;
                mark_service_err(&errors, spec->id);
                break;
            }
            if (cplat_ipv4_parse(owned->definition.multicast_group, &net_ip) != CPLAT_OK)
            {
                *has_err_out = 1;
                mark_service_err(&errors, spec->id);
                if (report_with_key(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, 1, service_id, service_path, key,
                                    "invalid IPv4 multicast address", spec->expected) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
            else
            {
                uint32_t first_octet = (cplat_ntoh32(net_ip) >> 24U) & 0xFFU;
                if (first_octet < 224U || first_octet > 239U)
                {
                    *has_err_out = 1;
                    mark_service_err(&errors, spec->id);
                    if (report_with_key(reporter, POTR_CONFIG_ISSUE_OUT_OF_RANGE, 1, service_id, service_path, key,
                                        "multicast_group is outside 224.0.0.0..239.255.255.255",
                                        spec->expected) != POTR_OK)
                    {
                        return POTR_ERR_OUT_OF_MEMORY;
                    }
                }
            }
            break;
        }
        case POTR_CONFIG_VAL_BROADCAST_ADDR:
        {
            int addr_err = 0;
            uint32_t net_ip = 0U;

            if (decode_addr_string(child, owned->definition.broadcast_addr, sizeof(owned->definition.broadcast_addr),
                                   reporter, service_id, service_path, key, spec->expected, &addr_err) != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
            if (addr_err != 0)
            {
                *has_err_out = 1;
                mark_service_err(&errors, spec->id);
                break;
            }
            if (cplat_ipv4_parse(owned->definition.broadcast_addr, &net_ip) != CPLAT_OK)
            {
                *has_err_out = 1;
                mark_service_err(&errors, spec->id);
                if (report_with_key(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, 1, service_id, service_path, key,
                                    "invalid IPv4 address", spec->expected) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
            break;
        }
        case POTR_CONFIG_VAL_ENCRYPT_KEY:
        {
            if (!cjson_app_cJSON_IsString(child) || child->valuestring == NULL)
            {
                *has_err_out = 1;
                mark_service_err(&errors, spec->id);
                if (report_with_key(reporter, POTR_CONFIG_ISSUE_TYPE_MISMATCH, 1, service_id, service_path, key,
                                    "expected string", spec->expected) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
                break;
            }
            if (child->valuestring[0] == '\0')
            {
                *has_err_out = 1;
                mark_service_err(&errors, spec->id);
                if (report_with_key(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, 1, service_id, service_path, key,
                                    "encrypt_key must not be empty", spec->expected) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
                break;
            }
            ret = parse_encrypt_key_value(child->valuestring, &owned->definition);
            if (ret == POTR_ERR_OUT_OF_MEMORY)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
            if (ret != POTR_OK)
            {
                *has_err_out = 1;
                mark_service_err(&errors, spec->id);
                if (report_with_key(reporter, POTR_CONFIG_ISSUE_KEY_DERIVATION, 1, service_id, service_path, key,
                                    "failed to derive key from encrypt_key", NULL) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
            break;
        }
        case POTR_CONFIG_VAL_FILE_STR:
        {
            const char *val_str;
            const char **dst_ptr = (const char **)((uint8_t *)&owned->definition + spec->offset);

            if (!cjson_app_cJSON_IsString(child) || child->valuestring == NULL)
            {
                *has_err_out = 1;
                mark_service_err(&errors, spec->id);
                if (report_with_key(reporter, POTR_CONFIG_ISSUE_TYPE_MISMATCH, 1, service_id, service_path, key,
                                    "expected string", spec->expected) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
                break;
            }
            val_str = child->valuestring;
            if (val_str[0] == '\0')
            {
                *has_err_out = 1;
                mark_service_err(&errors, spec->id);
                if (report_with_key(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, 1, service_id, service_path, key,
                                    "string must not be empty", spec->expected) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
                break;
            }

            *dst_ptr = val_str;
            if (on_auth_file != NULL)
            {
                potr_internal_config_path_buf fpath;
                if (potr_internal_config_path_push(&fpath, service_path, key) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
                emit_auth_file(on_auth_file, user_data, service_id, fpath.ptr, val_str);
                potr_internal_config_path_dispose(&fpath);
            }
            break;
        }
        case POTR_CONFIG_VAL_STR_ARRAY:
        {
            int arr_err = 0;
            const char **arr = NULL;
            size_t arr_count = 0U;
            int is_pinned = (spec->id == POTR_CONFIG_SFIELD_PINNED_CERTS) ? 1 : 0;

            ret = decode_string_array_field(child, service_id, service_path, key, spec->expected, &arr, &arr_count,
                                            is_pinned, on_auth_file, user_data, reporter, &arr_err);
            if (ret != POTR_OK)
            {
                return ret;
            }
            if (arr_err != 0)
            {
                *has_err_out = 1;
                mark_service_err(&errors, spec->id);
            }
            else if (is_pinned != 0)
            {
                owned->pinned_certs = arr;
                owned->definition.pinned_certs = arr;
                owned->definition.pinned_cert_count = arr_count;
            }
            else
            {
                owned->peer_names = arr;
                owned->definition.peer_names = arr;
                owned->definition.peer_name_count = arr_count;
            }
            break;
        }
        default:
            break;
        }
    }

    if (seen_type == 0)
    {
        *has_err_out = 1;
        mark_service_err(&errors, POTR_CONFIG_SFIELD_TYPE);
        if (report_with_key(reporter, POTR_CONFIG_ISSUE_MISSING, 1, service_id, service_path, "type",
                            "required key is missing", potr_internal_config_schema_find_service("type")->expected) !=
            POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }

    if (seen_dst_port == 0)
    {
        *has_err_out = 1;
        mark_service_err(&errors, POTR_CONFIG_SFIELD_DST_PORT);
        if (report_with_key(reporter, POTR_CONFIG_ISSUE_MISSING, 1, service_id, service_path, "dst_port",
                            "required key is missing", "1..65535") != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }

    ret = potr_internal_config_check_service(global, &owned->definition, roles, 1, has_ca_file_key, &errors,
                                             service_path, 1, service_id, reporter);
    if (ret == POTR_ERR_OUT_OF_MEMORY)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }
    if (ret != POTR_OK)
    {
        *has_err_out = 1;
    }

    return POTR_OK;
}
