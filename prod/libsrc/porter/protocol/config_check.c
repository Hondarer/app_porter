/**
 *******************************************************************************
 *  @file           config_check.c
 *  @brief          設定構造体の検証 (段階 4) と共通の実効値計算関数を実装します。
 *******************************************************************************
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <cplat/net/byteorder.h>
#include <cplat/net/endpoint.h>

#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include <porter/potr_context.h>
#include <porter/potr_flow_control.h>
#include <porter/protocol/config_check.h>
#include <porter/protocol/config_schema.h>

static int has_g_err(const potr_internal_config_field_errors *errors, unsigned int field_id)
{
    return (errors != NULL && (errors->global_mask & ((uint32_t)1U << field_id)) != 0U) ? 1 : 0;
}

static int has_s_err(const potr_internal_config_field_errors *errors, unsigned int field_id)
{
    return (errors != NULL && (errors->service_mask & ((uint64_t)1ULL << field_id)) != 0U) ? 1 : 0;
}

static int has_src_err(const potr_internal_config_field_errors *errors, size_t idx)
{
    return (errors != NULL && (errors->src_addr_mask & ((uint8_t)1U << idx)) != 0U) ? 1 : 0;
}

static int has_dst_err(const potr_internal_config_field_errors *errors, size_t idx)
{
    return (errors != NULL && (errors->dst_addr_mask & ((uint8_t)1U << idx)) != 0U) ? 1 : 0;
}

static int has_pg_err(const potr_internal_config_field_errors *errors, size_t idx)
{
    return (errors != NULL && (errors->path_group_mask & ((uint8_t)1U << idx)) != 0U) ? 1 : 0;
}

static int has_explicit_pg(const potr_internal_config_field_errors *errors, size_t idx)
{
    return (errors != NULL && (errors->has_path_group_bitmask & ((uint8_t)1U << idx)) != 0U) ? 1 : 0;
}

static int report_field_issue(potr_internal_config_reporter *reporter, potr_config_issue_kind kind, int has_service_id,
                              int64_t service_id, const char *base_path, const char *field_name, const char *message,
                              const char *expected)
{
    potr_internal_config_path_buf path_buf;
    int ret;

    if (field_name != NULL && field_name[0] != '\0')
    {
        ret = potr_internal_config_path_push(&path_buf, base_path, field_name);
        if (ret != POTR_OK)
        {
            return ret;
        }
        potr_internal_config_report_issue(reporter, kind, has_service_id, service_id, path_buf.ptr, message, expected,
                                          0U, 0U);
        potr_internal_config_path_dispose(&path_buf);
    }
    else
    {
        potr_internal_config_report_issue(reporter, kind, has_service_id, service_id, base_path, message, expected, 0U,
                                          0U);
    }

    return POTR_OK;
}

static int report_indexed_field_issue(potr_internal_config_reporter *reporter, potr_config_issue_kind kind,
                                      int has_service_id, int64_t service_id, const char *base_path,
                                      const char *prefix, size_t idx, const char *message, const char *expected)
{
    char key_buf[32];

    (void)snprintf(key_buf, sizeof(key_buf), "%s%zu", prefix, idx + 1U);
    /* 32 byte バッファーに 1..8 番の経路キー名は必ず収まるため、戻り値は明示的に破棄する */

    return report_field_issue(reporter, kind, has_service_id, service_id, base_path, key_buf, message, expected);
}

static uint32_t read_u32_field(const void *base, const potr_internal_config_field_spec *spec)
{
    const uint8_t *ptr = (const uint8_t *)base + spec->offset;

    if (spec->kind == POTR_CONFIG_VAL_U8)
    {
        uint8_t v = 0U;
        memcpy(&v, ptr, sizeof(v));
        return (uint32_t)v;
    }
    if (spec->kind == POTR_CONFIG_VAL_U16)
    {
        uint16_t v = 0U;
        memcpy(&v, ptr, sizeof(v));
        return (uint32_t)v;
    }

    {
        uint32_t v = 0U;
        memcpy(&v, ptr, sizeof(v));
        return v;
    }
}

static int check_ipv4_multicast_string(const char *text, int *is_syntax_err)
{
    uint32_t net_ip = 0U;
    uint32_t host_ip;
    uint32_t first_octet;

    *is_syntax_err = 0;
    if (cplat_ipv4_parse(text, &net_ip) != CPLAT_OK)
    {
        *is_syntax_err = 1;
        return 0;
    }

    host_ip = cplat_ntoh32(net_ip);
    first_octet = (host_ip >> 24U) & 0xFFU;
    if (first_octet < 224U || first_octet > 239U)
    {
        return 0;
    }

    return 1;
}

static int validate_paths_and_groups(const potr_service_def *service, uint32_t roles, int is_json_config,
                                     const potr_internal_config_field_errors *errors, const int src_nul_ok[POTR_MAX_PATH],
                                     const int dst_nul_ok[POTR_MAX_PATH], const char *service_path, int has_service_id,
                                     int64_t service_id, potr_internal_config_reporter *reporter, int *found_err)
{
    int src_occ[POTR_MAX_PATH];
    int dst_occ[POTR_MAX_PATH];
    size_t consec = 0U;
    size_t i;
    potr_type type = service->type;

    for (i = 0U; i < (size_t)POTR_MAX_PATH; i++)
    {
        int has_src = (src_nul_ok[i] != 0 && service->src_addr[i][0] != '\0') ? 1 : 0;
        int has_dst = (dst_nul_ok[i] != 0 && service->dst_addr[i][0] != '\0') ? 1 : 0;
        src_occ[i] = (has_src != 0 || has_src_err(errors, i) != 0) ? 1 : 0;
        dst_occ[i] = (has_dst != 0 || has_dst_err(errors, i) != 0) ? 1 : 0;
    }

    if (type == POTR_TYPE_UNICAST || type == POTR_TYPE_UNICAST_RAW || type == POTR_TYPE_MULTICAST ||
        type == POTR_TYPE_MULTICAST_RAW || type == POTR_TYPE_BROADCAST || type == POTR_TYPE_BROADCAST_RAW)
    {
        while (consec < (size_t)POTR_MAX_PATH && src_occ[consec] != 0)
        {
            consec++;
        }

        if (src_occ[0] == 0)
        {
            *found_err = 1;
            if (report_field_issue(reporter, POTR_CONFIG_ISSUE_MISSING, has_service_id, service_id, service_path,
                                   "src_addr1", "required key is missing", "1..63 bytes") != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
        }

        for (i = 0U; i < (size_t)POTR_MAX_PATH; i++)
        {
            if (i >= consec && src_occ[i] != 0 && has_src_err(errors, i) == 0)
            {
                *found_err = 1;
                if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id,
                                               service_path, "src_addr", i, "path numbers must be contiguous from 1",
                                               NULL) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }

            if (type == POTR_TYPE_UNICAST || type == POTR_TYPE_UNICAST_RAW)
            {
                if (i < consec && dst_occ[i] == 0)
                {
                    potr_config_issue_kind kind =
                        (i == 0U) ? POTR_CONFIG_ISSUE_MISSING : POTR_CONFIG_ISSUE_INVALID_VALUE;
                    const char *msg =
                        (i == 0U) ? "required key is missing" : "dst_addr is required for configured src_addr";
                    *found_err = 1;
                    if (report_indexed_field_issue(reporter, kind, has_service_id, service_id, service_path, "dst_addr",
                                                   i, msg, "1..63 bytes") != POTR_OK)
                    {
                        return POTR_ERR_OUT_OF_MEMORY;
                    }
                }
                else if (i >= consec && dst_occ[i] != 0 && has_dst_err(errors, i) == 0)
                {
                    *found_err = 1;
                    if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id,
                                                   service_id, service_path, "dst_addr", i,
                                                   "path address requires corresponding src_addr", NULL) != POTR_OK)
                    {
                        return POTR_ERR_OUT_OF_MEMORY;
                    }
                }
            }
            else if (type == POTR_TYPE_MULTICAST || type == POTR_TYPE_MULTICAST_RAW)
            {
                if (i >= consec && dst_occ[i] != 0 && has_dst_err(errors, i) == 0)
                {
                    *found_err = 1;
                    if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id,
                                                   service_id, service_path, "dst_addr", i,
                                                   "path address requires corresponding src_addr", NULL) != POTR_OK)
                    {
                        return POTR_ERR_OUT_OF_MEMORY;
                    }
                }
            }

            if (is_json_config != 0 && !is_raw_type(type) && i >= consec && has_explicit_pg(errors, i) != 0 &&
                has_pg_err(errors, i) == 0)
            {
                *found_err = 1;
                if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id,
                                               service_path, "path_group", i, "path_group requires corresponding path",
                                               NULL) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
        }
    }
    else if (type == POTR_TYPE_UNICAST_BIDIR)
    {
        int dynamic_receiver = (src_occ[0] == 0 && (roles & POTR_CONFIG_ROLE_RECEIVER) != 0U) ? 1 : 0;

        while (consec < (size_t)POTR_MAX_PATH && dst_occ[consec] != 0)
        {
            consec++;
        }

        if (dst_occ[0] == 0)
        {
            if (src_occ[0] != 0)
            {
                *found_err = 1;
                if (report_field_issue(reporter, POTR_CONFIG_ISSUE_MISSING, has_service_id, service_id, service_path,
                                       "dst_addr1", "static unicast_bidir requires dst_addr1", "1..63 bytes") != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
            else if ((roles & POTR_CONFIG_ROLE_SENDER) != 0U)
            {
                *found_err = 1;
                if (report_field_issue(reporter, POTR_CONFIG_ISSUE_CONSTRAINT, has_service_id, service_id, service_path,
                                       "dst_addr1", "unicast_bidir sender requires dst_addr1", "1..63 bytes") != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
        }

        for (i = 0U; i < (size_t)POTR_MAX_PATH; i++)
        {
            if (dynamic_receiver != 0 && i >= 1U)
            {
                if (dst_occ[i] != 0 && has_dst_err(errors, i) == 0)
                {
                    *found_err = 1;
                    if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_CONSTRAINT, has_service_id, service_id,
                                                   service_path, "dst_addr", i,
                                                   "dynamic unicast_bidir receiver only supports path 1", NULL) !=
                        POTR_OK)
                    {
                        return POTR_ERR_OUT_OF_MEMORY;
                    }
                }
                if (src_occ[i] != 0 && has_src_err(errors, i) == 0)
                {
                    *found_err = 1;
                    if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_CONSTRAINT, has_service_id, service_id,
                                                   service_path, "src_addr", i,
                                                   "dynamic unicast_bidir receiver only supports path 1", NULL) !=
                        POTR_OK)
                    {
                        return POTR_ERR_OUT_OF_MEMORY;
                    }
                }
                if (is_json_config != 0 && has_explicit_pg(errors, i) != 0 && has_pg_err(errors, i) == 0)
                {
                    *found_err = 1;
                    if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_CONSTRAINT, has_service_id, service_id,
                                                   service_path, "path_group", i,
                                                   "dynamic unicast_bidir receiver only supports path 1", NULL) !=
                        POTR_OK)
                    {
                        return POTR_ERR_OUT_OF_MEMORY;
                    }
                }
                continue;
            }

            if (i >= consec && dst_occ[i] != 0 && has_dst_err(errors, i) == 0)
            {
                *found_err = 1;
                if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id,
                                               service_path, "dst_addr", i, "path numbers must be contiguous from 1",
                                               NULL) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }

            if (src_occ[0] != 0 && (roles & POTR_CONFIG_ROLE_RECEIVER) != 0U && i < consec && src_occ[i] == 0)
            {
                *found_err = 1;
                if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_CONSTRAINT, has_service_id, service_id,
                                               service_path, "src_addr", i,
                                               "static unicast_bidir receiver requires src_addr for all paths",
                                               "1..63 bytes") != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
            else if (i >= consec && src_occ[i] != 0 && has_src_err(errors, i) == 0)
            {
                *found_err = 1;
                if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id,
                                               service_path, "src_addr", i,
                                               "path address requires corresponding dst_addr", NULL) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }

            if (is_json_config != 0 && i >= 1U && i >= consec && has_explicit_pg(errors, i) != 0 &&
                has_pg_err(errors, i) == 0)
            {
                *found_err = 1;
                if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id,
                                               service_path, "path_group", i, "path_group requires corresponding path",
                                               NULL) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
        }
    }
    else if (type == POTR_TYPE_UNICAST_BIDIR_N1)
    {
        if (dst_occ[0] == 0)
        {
            for (i = 1U; i < (size_t)POTR_MAX_PATH; i++)
            {
                if (dst_occ[i] != 0 && has_dst_err(errors, i) == 0)
                {
                    *found_err = 1;
                    if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id,
                                                   service_id, service_path, "dst_addr", i,
                                                   "path numbers must be contiguous from 1", NULL) != POTR_OK)
                    {
                        return POTR_ERR_OUT_OF_MEMORY;
                    }
                }
                if (is_json_config != 0 && has_explicit_pg(errors, i) != 0 && has_pg_err(errors, i) == 0)
                {
                    *found_err = 1;
                    if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id,
                                                   service_id, service_path, "path_group", i,
                                                   "path_group requires corresponding path", NULL) != POTR_OK)
                    {
                        return POTR_ERR_OUT_OF_MEMORY;
                    }
                }
            }
        }
        else
        {
            while (consec < (size_t)POTR_MAX_PATH && dst_occ[consec] != 0)
            {
                consec++;
            }
            for (i = consec; i < (size_t)POTR_MAX_PATH; i++)
            {
                if (dst_occ[i] != 0 && has_dst_err(errors, i) == 0)
                {
                    *found_err = 1;
                    if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id,
                                                   service_id, service_path, "dst_addr", i,
                                                   "path numbers must be contiguous from 1", NULL) != POTR_OK)
                    {
                        return POTR_ERR_OUT_OF_MEMORY;
                    }
                }
                if (is_json_config != 0 && has_explicit_pg(errors, i) != 0 && has_pg_err(errors, i) == 0)
                {
                    *found_err = 1;
                    if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id,
                                                   service_id, service_path, "path_group", i,
                                                   "path_group requires corresponding path", NULL) != POTR_OK)
                    {
                        return POTR_ERR_OUT_OF_MEMORY;
                    }
                }
            }
        }
    }
    else if (is_tcp_type(type))
    {
        while (consec < (size_t)POTR_MAX_PATH && dst_occ[consec] != 0)
        {
            consec++;
        }

        if (dst_occ[0] == 0)
        {
            *found_err = 1;
            if (report_field_issue(reporter, POTR_CONFIG_ISSUE_MISSING, has_service_id, service_id, service_path,
                                   "dst_addr1", "required key is missing", "1..63 bytes") != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
        }

        for (i = consec; i < (size_t)POTR_MAX_PATH; i++)
        {
            if (dst_occ[i] != 0 && has_dst_err(errors, i) == 0)
            {
                *found_err = 1;
                if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id,
                                               service_path, "dst_addr", i, "path numbers must be contiguous from 1",
                                               NULL) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
            if (src_occ[i] != 0 && has_src_err(errors, i) == 0)
            {
                *found_err = 1;
                if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id,
                                               service_path, "src_addr", i,
                                               "path address requires corresponding dst_addr", NULL) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
            if (is_json_config != 0 && has_explicit_pg(errors, i) != 0 && has_pg_err(errors, i) == 0)
            {
                *found_err = 1;
                if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id,
                                               service_path, "path_group", i, "path_group requires corresponding path",
                                               NULL) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
        }
    }

    return POTR_OK;
}

static int validate_auth_combinations(const potr_service_def *service, uint32_t roles, int type_ok,
                                        int is_json_config, int has_ca_file,
                                        const potr_internal_config_field_errors *errors, const char *service_path,
                                        int has_service_id, int64_t service_id,
                                        potr_internal_config_reporter *reporter, int *found_err)
{
    int effective_has_ca;

    if (has_s_err(errors, POTR_CONFIG_SFIELD_AUTH) != 0)
    {
        return POTR_OK;
    }

    if (service->auth != POTR_AUTH_PSK && service->auth != POTR_AUTH_CERT)
    {
        *found_err = 1;
        return report_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id, service_path,
                                  "auth", "invalid authentication mode", "psk|cert");
    }

    if (service->auth != POTR_AUTH_CERT)
    {
        return POTR_OK;
    }

    if ((service->cert_file == NULL || service->cert_file[0] == '\0') &&
        has_s_err(errors, POTR_CONFIG_SFIELD_CERT_FILE) == 0)
    {
        *found_err = 1;
        if (report_field_issue(reporter, POTR_CONFIG_ISSUE_MISSING, has_service_id, service_id, service_path,
                               "cert_file", "cert_file is required for cert auth", "non-empty string") != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }

    if ((service->key_file == NULL || service->key_file[0] == '\0') &&
        has_s_err(errors, POTR_CONFIG_SFIELD_KEY_FILE) == 0)
    {
        *found_err = 1;
        if (report_field_issue(reporter, POTR_CONFIG_ISSUE_MISSING, has_service_id, service_id, service_path,
                               "key_file", "key_file is required for cert auth", "non-empty string") != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }

    effective_has_ca = (is_json_config != 0) ? has_ca_file : (service->ca_file != NULL ? 1 : 0);

    if (is_json_config == 0)
    {
        if (service->ca_file != NULL && service->ca_file[0] == '\0')
        {
            *found_err = 1;
            if (report_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id, service_path,
                                   "ca_file", "string must not be empty", "non-empty string") != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
        }
        if (service->crl_file != NULL && effective_has_ca == 0)
        {
            *found_err = 1;
            if (report_field_issue(reporter, POTR_CONFIG_ISSUE_NOT_APPLICABLE, has_service_id, service_id, service_path,
                                   "crl_file", "crl_file requires ca_file", NULL) != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
        }
        else if (service->crl_file != NULL && service->crl_file[0] == '\0')
        {
            *found_err = 1;
            if (report_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id, service_path,
                                   "crl_file", "string must not be empty", "non-empty string") != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
        }

        if (service->pinned_cert_count > 0U && service->pinned_certs == NULL)
        {
            *found_err = 1;
            if (report_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id, service_path,
                                   "pinned_certs", "pinned_certs array is NULL with non-zero count", NULL) != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
        }
        else
        {
            size_t i;
            for (i = 0U; i < service->pinned_cert_count; i++)
            {
                if (service->pinned_certs[i] == NULL || service->pinned_certs[i][0] == '\0')
                {
                    *found_err = 1;
                    if (report_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id,
                                           service_path, "pinned_certs", "pinned_certs contains empty entry",
                                           "non-empty string") != POTR_OK)
                    {
                        return POTR_ERR_OUT_OF_MEMORY;
                    }
                    break;
                }
            }
        }

        if (service->peer_name_count > 0U && service->peer_names == NULL)
        {
            *found_err = 1;
            if (report_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id, service_path,
                                   "peer_names", "peer_names array is NULL with non-zero count", NULL) != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
        }
        else
        {
            size_t i;
            for (i = 0U; i < service->peer_name_count; i++)
            {
                if (service->peer_names[i] == NULL || service->peer_names[i][0] == '\0')
                {
                    *found_err = 1;
                    if (report_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id,
                                           service_path, "peer_names", "peer_names contains empty entry",
                                           "non-empty string") != POTR_OK)
                    {
                        return POTR_ERR_OUT_OF_MEMORY;
                    }
                    break;
                }
            }
        }
    }

    if (effective_has_ca == 0 && service->pinned_cert_count == 0U && has_s_err(errors, POTR_CONFIG_SFIELD_CA_FILE) == 0 &&
        has_s_err(errors, POTR_CONFIG_SFIELD_PINNED_CERTS) == 0)
    {
        *found_err = 1;
        if (report_field_issue(reporter, POTR_CONFIG_ISSUE_MISSING, has_service_id, service_id, service_path, "ca_file",
                               "either ca_file or pinned_certs is required for cert auth", NULL) != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }

    if (effective_has_ca != 0 && service->peer_name_count == 0U && has_s_err(errors, POTR_CONFIG_SFIELD_PEER_NAMES) == 0)
    {
        *found_err = 1;
        if (report_field_issue(reporter, POTR_CONFIG_ISSUE_MISSING, has_service_id, service_id, service_path,
                               "peer_names", "peer_names is required when ca_file is specified",
                               "array of non-empty strings") != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }

    if (type_ok != 0 && is_oneway_udp_type(service->type) && (roles & POTR_CONFIG_ROLE_SENDER) != 0U &&
        has_s_err(errors, POTR_CONFIG_SFIELD_SRC_PORT) == 0 && service->src_port == 0U)
    {
        *found_err = 1;
        if (report_field_issue(reporter, POTR_CONFIG_ISSUE_CONSTRAINT, has_service_id, service_id, service_path,
                               "src_port", "one-way UDP sender with cert auth requires src_port >= 1",
                               "1..65535") != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }

    return POTR_OK;
}

static int validate_flow_control_constraints(const potr_global_config *global, const potr_service_def *service,
                                             uint32_t roles, uint32_t eff_health_interval,
                                             const potr_internal_config_field_errors *errors, const char *service_path,
                                             int has_service_id, int64_t service_id,
                                             potr_internal_config_reporter *reporter, int *found_err)
{
    potr_internal_flow_control_config fc;
    uint32_t disabled_err = 0U;
    int cap_err;
    int sup_err;
    int res_err;
    int int_err;
    int life_err;
    int health_err;

    if (potr_internal_flow_control_resolve_values(service, global, POTR_ROLE_RECEIVER, eff_health_interval, &fc,
                                                  &disabled_err) != POTR_OK)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    /* DISABLED 指定不可項目 (主に構造体直接渡し時の検出) */
    if ((disabled_err & (1U << 1U)) != 0U && has_s_err(errors, POTR_CONFIG_SFIELD_FC_SUPPRESS_PERCENT) == 0)
    {
        *found_err = 1;
        if (report_field_issue(reporter, POTR_CONFIG_ISSUE_OUT_OF_RANGE, has_service_id, service_id, service_path,
                               "flow_control_suppress_percent",
                               "POTR_FLOW_CONTROL_DISABLED is not allowed for this field", "0..100") != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }
    if ((disabled_err & (1U << 2U)) != 0U && has_s_err(errors, POTR_CONFIG_SFIELD_FC_RESUME_PERCENT) == 0)
    {
        *found_err = 1;
        if (report_field_issue(reporter, POTR_CONFIG_ISSUE_OUT_OF_RANGE, has_service_id, service_id, service_path,
                               "flow_control_resume_percent",
                               "POTR_FLOW_CONTROL_DISABLED is not allowed for this field", "0..99") != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }
    if ((disabled_err & (1U << 3U)) != 0U && has_s_err(errors, POTR_CONFIG_SFIELD_FC_REQUEST_INTERVAL_MS) == 0)
    {
        *found_err = 1;
        if (report_field_issue(reporter, POTR_CONFIG_ISSUE_OUT_OF_RANGE, has_service_id, service_id, service_path,
                               "flow_control_request_interval_ms",
                               "POTR_FLOW_CONTROL_DISABLED is not allowed for this field", "0..2147483646") != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }
    if ((disabled_err & (1U << 4U)) != 0U && has_s_err(errors, POTR_CONFIG_SFIELD_FC_REQUEST_LIFETIME_MS) == 0)
    {
        *found_err = 1;
        if (report_field_issue(reporter, POTR_CONFIG_ISSUE_OUT_OF_RANGE, has_service_id, service_id, service_path,
                               "flow_control_request_lifetime_ms",
                               "POTR_FLOW_CONTROL_DISABLED is not allowed for this field", "0..2147483646") != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }
    if ((disabled_err & (1U << 5U)) != 0U && has_s_err(errors, POTR_CONFIG_SFIELD_FC_GRACE_MS) == 0)
    {
        *found_err = 1;
        if (report_field_issue(reporter, POTR_CONFIG_ISSUE_OUT_OF_RANGE, has_service_id, service_id, service_path,
                               "flow_control_grace_ms", "POTR_FLOW_CONTROL_DISABLED is not allowed for this field",
                               "0..2147483646") != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }
    if ((disabled_err & (1U << 6U)) != 0U && has_s_err(errors, POTR_CONFIG_SFIELD_FC_FEEDBACK_PERCENT) == 0)
    {
        *found_err = 1;
        if (report_field_issue(reporter, POTR_CONFIG_ISSUE_OUT_OF_RANGE, has_service_id, service_id, service_path,
                               "flow_control_feedback_percent",
                               "POTR_FLOW_CONTROL_DISABLED is not allowed for this field", "0..100") != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }

    sup_err = has_g_err(errors, POTR_CONFIG_GFIELD_FC_SUPPRESS_PERCENT) ||
              has_s_err(errors, POTR_CONFIG_SFIELD_FC_SUPPRESS_PERCENT);
    res_err = has_g_err(errors, POTR_CONFIG_GFIELD_FC_RESUME_PERCENT) ||
              has_s_err(errors, POTR_CONFIG_SFIELD_FC_RESUME_PERCENT);
    if (!sup_err && !res_err && (disabled_err & ((1U << 1U) | (1U << 2U))) == 0U && fc.suppress_percent <= 100U &&
        fc.resume_percent < 100U && fc.resume_percent >= fc.suppress_percent)
    {
        *found_err = 1;
        if (report_field_issue(reporter, POTR_CONFIG_ISSUE_CONSTRAINT, has_service_id, service_id, service_path,
                               "flow_control_resume_percent",
                               "effective flow_control_resume_percent must be less than flow_control_suppress_percent",
                               "1 <= resume < suppress <= 100") != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }

    int_err = has_g_err(errors, POTR_CONFIG_GFIELD_FC_REQUEST_INTERVAL_MS) ||
              has_s_err(errors, POTR_CONFIG_SFIELD_FC_REQUEST_INTERVAL_MS);
    life_err = has_g_err(errors, POTR_CONFIG_GFIELD_FC_REQUEST_LIFETIME_MS) ||
               has_s_err(errors, POTR_CONFIG_SFIELD_FC_REQUEST_LIFETIME_MS);
    if (!int_err && !life_err && (disabled_err & ((1U << 3U) | (1U << 4U))) == 0U &&
        fc.request_interval_ms <= POTR_WAIT_MS_MAX && fc.request_lifetime_ms <= POTR_WAIT_MS_MAX &&
        fc.request_lifetime_ms <= fc.request_interval_ms)
    {
        *found_err = 1;
        if (report_field_issue(
                reporter, POTR_CONFIG_ISSUE_CONSTRAINT, has_service_id, service_id, service_path,
                "flow_control_request_lifetime_ms",
                "effective flow_control_request_lifetime_ms must exceed flow_control_request_interval_ms",
                "0 < interval < lifetime") != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }

    cap_err = (service->flow_control_capacity != 0U)
                  ? has_s_err(errors, POTR_CONFIG_SFIELD_FC_CAPACITY)
                  : (has_g_err(errors, POTR_CONFIG_GFIELD_FC_CAPACITY) || ((disabled_err & (1U << 8U)) != 0U));
    if (!cap_err && !sup_err && has_g_err(errors, POTR_CONFIG_GFIELD_MAX_MESSAGE_SIZE) == 0 &&
        (disabled_err & (1U << 1U)) == 0U && fc.capacity > 0U && fc.suppress_percent <= 100U)
    {
        uint64_t margin = (uint64_t)fc.capacity - (uint64_t)fc.capacity * fc.suppress_percent / 100U;
        if (margin < (uint64_t)global->max_message_size)
        {
            *found_err = 1;
            if (report_field_issue(reporter, POTR_CONFIG_ISSUE_CONSTRAINT, has_service_id, service_id, service_path,
                                   "flow_control_capacity",
                                   "capacity margin above suppress threshold must be at least max_message_size",
                                   NULL) != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
        }
    }

    health_err =
        has_g_err(errors, POTR_CONFIG_GFIELD_UDP_HEALTH_INTERVAL_MS) ||
        has_s_err(errors, POTR_CONFIG_SFIELD_HEALTH_INTERVAL_MS);
    if (!cap_err && !health_err &&
        (roles & POTR_CONFIG_ROLE_RECEIVER) != 0U &&
        (service->type == POTR_TYPE_MULTICAST || service->type == POTR_TYPE_BROADCAST) && fc.capacity > 0U &&
        eff_health_interval == 0U)
    {
        *found_err = 1;
        if (report_field_issue(reporter, POTR_CONFIG_ISSUE_CONSTRAINT, has_service_id, service_id, service_path,
                               "health_interval_ms",
                               "multicast/broadcast receiver with flow control requires health_interval_ms > 0",
                               "1..2147483646") != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

unsigned int potr_internal_config_effective_ttl(uint8_t configured_ttl)
{
    return (configured_ttl == 0U) ? (unsigned int)POTR_DEFAULT_TTL : (unsigned int)configured_ttl;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_config_resolve_health(const potr_service_def *service, const potr_global_config *global,
                                         uint32_t *interval_ms_out, uint32_t *timeout_ms_out)
{
    uint32_t interval_ms;
    uint32_t timeout_ms;

    if (service == NULL || global == NULL)
    {
        return;
    }

    if (is_tcp_type(service->type))
    {
        interval_ms = global->tcp_health_interval_ms;
        timeout_ms = global->tcp_health_timeout_ms;
    }
    else
    {
        interval_ms = global->udp_health_interval_ms;
        timeout_ms = global->udp_health_timeout_ms;
    }

    if (service->health_interval_ms != 0U)
    {
        interval_ms = service->health_interval_ms;
    }

    if (service->health_timeout_ms != 0U)
    {
        timeout_ms = service->health_timeout_ms;
    }

    if (interval_ms_out != NULL)
    {
        *interval_ms_out = interval_ms;
    }
    if (timeout_ms_out != NULL)
    {
        *timeout_ms_out = timeout_ms;
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_config_check_global(const potr_global_config *global,
                                      const potr_internal_config_field_errors *errors, const char *global_path,
                                      potr_internal_config_reporter *reporter)
{
    size_t count = 0U;
    const potr_internal_config_field_spec *table = potr_internal_config_schema_global_table(&count);
    const char *base_path = (global_path != NULL) ? global_path : "/global";
    size_t i;
    int found_err = 0;

    if (global == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    for (i = 0U; i < count; i++)
    {
        const potr_internal_config_field_spec *spec = &table[i];
        uint32_t val;

        if (has_g_err(errors, spec->id) != 0)
        {
            continue;
        }

        val = read_u32_field(global, spec);
        if (spec->allow_minus_one != 0 && val == POTR_FLOW_CONTROL_DISABLED)
        {
            continue;
        }

        if (val < spec->min_val || val > spec->max_val)
        {
            found_err = 1;
            if (report_field_issue(reporter, POTR_CONFIG_ISSUE_OUT_OF_RANGE, 0, 0, base_path, spec->name,
                                   "value is out of range", spec->expected) != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
        }
    }

    if (has_g_err(errors, POTR_CONFIG_GFIELD_MAX_PAYLOAD) == 0 &&
        has_g_err(errors, POTR_CONFIG_GFIELD_MAX_MESSAGE_SIZE) == 0 &&
        global->max_message_size < (uint32_t)global->max_payload)
    {
        found_err = 1;
        if (report_field_issue(reporter, POTR_CONFIG_ISSUE_CONSTRAINT, 0, 0, base_path, "max_message_size",
                               "max_message_size must be at least max_payload",
                               "max_message_size >= max_payload") != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }

    return (found_err != 0) ? POTR_ERR_INVALID_CONFIG : POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_config_check_service(const potr_global_config *global, const potr_service_def *service,
                                       uint32_t roles, int is_json_config, int has_ca_file,
                                       const potr_internal_config_field_errors *errors, const char *service_path,
                                       int has_service_id, int64_t service_id, potr_internal_config_reporter *reporter)
{
    int src_nul_ok[POTR_MAX_PATH];
    int dst_nul_ok[POTR_MAX_PATH];
    int mcast_nul_ok = 1;
    int bcast_nul_ok = 1;
    int type_ok;
    int found_err = 0;
    size_t i;
    const char *base_path = (service_path != NULL) ? service_path : "";
    uint32_t eff_health_interval = 0U;

    if (global == NULL || service == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    /* 固定長文字配列に NUL 終端があるか先に確認し、後続の文字列長・IP 解釈での範囲外読み取りを防ぐ。 */
    for (i = 0U; i < (size_t)POTR_MAX_PATH; i++)
    {
        src_nul_ok[i] = (memchr(service->src_addr[i], '\0', sizeof(service->src_addr[i])) != NULL) ? 1 : 0;
        if (src_nul_ok[i] == 0)
        {
            found_err = 1;
            if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id,
                                           base_path, "src_addr", i, "string buffer is not NUL-terminated",
                                           "1..63 bytes") != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
        }

        dst_nul_ok[i] = (memchr(service->dst_addr[i], '\0', sizeof(service->dst_addr[i])) != NULL) ? 1 : 0;
        if (dst_nul_ok[i] == 0)
        {
            found_err = 1;
            if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id,
                                           base_path, "dst_addr", i, "string buffer is not NUL-terminated",
                                           "1..63 bytes") != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
        }
    }

    if (memchr(service->multicast_group, '\0', sizeof(service->multicast_group)) == NULL)
    {
        mcast_nul_ok = 0;
        found_err = 1;
        if (report_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id, base_path,
                               "multicast_group", "string buffer is not NUL-terminated",
                               "IPv4 multicast address (224.0.0.0..239.255.255.255)") != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }

    if (memchr(service->broadcast_addr, '\0', sizeof(service->broadcast_addr)) == NULL)
    {
        bcast_nul_ok = 0;
        found_err = 1;
        if (report_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id, base_path,
                               "broadcast_addr", "string buffer is not NUL-terminated", "IPv4 address") != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }

    type_ok = (has_s_err(errors, POTR_CONFIG_SFIELD_TYPE) == 0 && service->type >= POTR_TYPE_UNICAST_RAW &&
               service->type <= POTR_TYPE_TCP_BIDIR)
                  ? 1
                  : 0;

    if (is_json_config == 0)
    {
        size_t count = 0U;
        const potr_internal_config_field_spec *table = potr_internal_config_schema_service_table(&count);

        if (has_s_err(errors, POTR_CONFIG_SFIELD_TYPE) == 0 && type_ok == 0)
        {
            found_err = 1;
            if (report_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id, base_path,
                                   "type", "unsupported service type", table[0].expected) != POTR_OK)
            {
                return POTR_ERR_OUT_OF_MEMORY;
            }
        }

        for (i = 0U; i < count; i++)
        {
            const potr_internal_config_field_spec *spec = &table[i];
            uint32_t val;

            if (spec->kind != POTR_CONFIG_VAL_U8 && spec->kind != POTR_CONFIG_VAL_U16 &&
                spec->kind != POTR_CONFIG_VAL_U32)
            {
                continue;
            }

            if (spec->id == POTR_CONFIG_SFIELD_TTL)
            {
                unsigned int eff_ttl = potr_internal_config_effective_ttl(service->ttl);
                if (eff_ttl < 1U || eff_ttl > UINT8_MAX)
                {
                    found_err = 1;
                    if (report_field_issue(reporter, POTR_CONFIG_ISSUE_OUT_OF_RANGE, has_service_id, service_id,
                                           base_path, spec->name, "value is out of range", spec->expected) != POTR_OK)
                    {
                        return POTR_ERR_OUT_OF_MEMORY;
                    }
                }
                continue;
            }

            val = read_u32_field(service, spec);
            if (spec->zero_is_default_in_struct != 0 && val == 0U)
            {
                continue;
            }
            if (spec->allow_minus_one != 0 && val == POTR_FLOW_CONTROL_DISABLED)
            {
                continue;
            }

            if (val < spec->min_val || val > spec->max_val)
            {
                found_err = 1;
                if (report_field_issue(reporter, POTR_CONFIG_ISSUE_OUT_OF_RANGE, has_service_id, service_id, base_path,
                                       spec->name, "value is out of range", spec->expected) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
        }

        for (i = 0U; i < (size_t)POTR_MAX_PATH; i++)
        {
            uint8_t pg = service->path_group[i];
            if (pg > POTR_MAX_PATH_GROUP)
            {
                found_err = 1;
                if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_OUT_OF_RANGE, has_service_id, service_id,
                                               base_path, "path_group", i, "value is out of range", "1..4") != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
            else if (type_ok != 0 && is_raw_type(service->type) && pg >= 2U)
            {
                found_err = 1;
                if (report_indexed_field_issue(reporter, POTR_CONFIG_ISSUE_NOT_APPLICABLE, has_service_id, service_id,
                                               base_path, "path_group", i, "key is not applicable to RAW service type",
                                               NULL) != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
        }
    }

    if (type_ok != 0)
    {
        if ((service->type == POTR_TYPE_MULTICAST || service->type == POTR_TYPE_MULTICAST_RAW) &&
            has_s_err(errors, POTR_CONFIG_SFIELD_MULTICAST_GROUP) == 0 && mcast_nul_ok != 0)
        {
            if (service->multicast_group[0] == '\0')
            {
                found_err = 1;
                if (report_field_issue(reporter, POTR_CONFIG_ISSUE_MISSING, has_service_id, service_id, base_path,
                                       "multicast_group", "required key is missing",
                                       "IPv4 multicast address (224.0.0.0..239.255.255.255)") != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
            else if (is_json_config == 0)
            {
                int is_syntax = 0;
                if (check_ipv4_multicast_string(service->multicast_group, &is_syntax) == 0)
                {
                    potr_config_issue_kind kind =
                        (is_syntax != 0) ? POTR_CONFIG_ISSUE_INVALID_VALUE : POTR_CONFIG_ISSUE_OUT_OF_RANGE;
                    const char *msg =
                        (is_syntax != 0) ? "invalid IPv4 multicast address" : "multicast_group is outside 224.0.0.0..239.255.255.255";
                    found_err = 1;
                    if (report_field_issue(reporter, kind, has_service_id, service_id, base_path, "multicast_group", msg,
                                           "IPv4 multicast address (224.0.0.0..239.255.255.255)") != POTR_OK)
                    {
                        return POTR_ERR_OUT_OF_MEMORY;
                    }
                }
            }
        }

        if ((service->type == POTR_TYPE_BROADCAST || service->type == POTR_TYPE_BROADCAST_RAW) &&
            is_json_config == 0 && has_s_err(errors, POTR_CONFIG_SFIELD_BROADCAST_ADDR) == 0 && bcast_nul_ok != 0 &&
            service->broadcast_addr[0] != '\0')
        {
            uint32_t tmp_ip = 0U;
            if (cplat_ipv4_parse(service->broadcast_addr, &tmp_ip) != CPLAT_OK)
            {
                found_err = 1;
                if (report_field_issue(reporter, POTR_CONFIG_ISSUE_INVALID_VALUE, has_service_id, service_id, base_path,
                                       "broadcast_addr", "invalid IPv4 address", "IPv4 address") != POTR_OK)
                {
                    return POTR_ERR_OUT_OF_MEMORY;
                }
            }
        }

        if (validate_paths_and_groups(service, roles, is_json_config, errors, src_nul_ok, dst_nul_ok, base_path,
                                      has_service_id, service_id, reporter, &found_err) != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }

    if (validate_auth_combinations(service, roles, type_ok, is_json_config, has_ca_file, errors, base_path,
                                   has_service_id, service_id, reporter, &found_err) != POTR_OK)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    potr_internal_config_resolve_health(service, global, &eff_health_interval, NULL);

    if (type_ok != 0 && !is_raw_type(service->type))
    {
        if (validate_flow_control_constraints(global, service, roles, eff_health_interval, errors, base_path,
                                              has_service_id, service_id, reporter, &found_err) != POTR_OK)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
    }

    return (found_err != 0) ? POTR_ERR_INVALID_CONFIG : POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_config_check_all(const potr_global_config *global, const potr_service_def *service, potr_role role,
                                   potr_internal_config_reporter *reporter)
{
    uint32_t role_mask = 0U;
    int ret_global;
    int ret_service;
    int has_err = 0;

    if (global == NULL || service == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (role == POTR_ROLE_SENDER)
    {
        role_mask = POTR_CONFIG_ROLE_SENDER;
    }
    else if (role == POTR_ROLE_RECEIVER)
    {
        role_mask = POTR_CONFIG_ROLE_RECEIVER;
    }
    else
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    ret_global = potr_internal_config_check_global(global, NULL, "/global", reporter);
    if (ret_global == POTR_ERR_OUT_OF_MEMORY)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }
    if (ret_global != POTR_OK)
    {
        has_err = 1;
    }

    ret_service = potr_internal_config_check_service(global, service, role_mask, 0, 0, NULL, "", 1, service->service_id,
                                                     reporter);
    if (ret_service == POTR_ERR_OUT_OF_MEMORY)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }
    if (ret_service != POTR_OK)
    {
        has_err = 1;
    }

    return (has_err != 0) ? POTR_ERR_INVALID_CONFIG : POTR_OK;
}
