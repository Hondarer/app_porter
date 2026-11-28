/**
 *******************************************************************************
 *  @file           potr_config_validate_file.c
 *  @brief          設定ファイル全体を検証する potr_config_validate_file 関数を提供します。
 *******************************************************************************
 */

#include <cJSON.h>
#include <cJSON_PatchCheck.h>

#include <porter/porter_result.h>
#include <porter/porter_spec.h>
#include <porter/protocol/config_decode.h>
#include <porter/protocol/config_document.h>
#include <porter/protocol/config_report.h>

/* Doxygen コメントは、ヘッダーに記載 */

int POTR_API potr_config_validate_file(const char *config_path, const potr_config_validate_options *options,
                                       size_t *issue_count)
{
    uint32_t roles = POTR_CONFIG_ROLE_ALL;
    potr_config_issue_fn on_issue = NULL;
    potr_config_file_fn on_auth_file = NULL;
    potr_config_service_fn on_service = NULL;
    void *user_data = NULL;
    potr_internal_config_reporter reporter;
    cJSON *root = NULL;
    int ret;

    if (issue_count != NULL)
    {
        *issue_count = 0U;
    }

    if (config_path == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (options != NULL)
    {
        if (options->reserved != 0U || (options->roles & ~(uint32_t)POTR_CONFIG_ROLE_ALL) != 0U)
        {
            return POTR_ERR_INVALID_ARGUMENT;
        }
        roles = options->roles;
        on_issue = options->on_issue;
        on_auth_file = options->on_auth_file;
        on_service = options->on_service;
        user_data = options->user_data;
    }

    potr_internal_config_reporter_init(&reporter, config_path, on_issue, user_data, 0);

    ret = potr_internal_config_document_load(config_path, &reporter, &root);
    if (ret == POTR_OK)
    {
        ret = potr_internal_config_decode_validate_all(root, roles, on_service, on_auth_file, user_data, &reporter);
        cjson_app_cJSON_Delete(root);
    }

    if (issue_count != NULL)
    {
        *issue_count = reporter.issue_count;
    }

    return ret;
}
