/**
 *******************************************************************************
 *  @file           config_decode.h
 *  @brief          設定 JSON ツリーのルート・項目解釈 (段階 2 と段階 3) を提供します。
 *******************************************************************************
 */

#ifndef POTR_PROTOCOL_CONFIG_DECODE_H
#define POTR_PROTOCOL_CONFIG_DECODE_H

#include <stddef.h>
#include <stdint.h>

#include <porter/porter_type.h>
#include <porter/protocol/config.h>
#include <porter/protocol/config_report.h>

struct cJSON;

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /** 段階 2 のサービス ID 列挙結果です。 */
    typedef struct potr_internal_config_id_list
    {
        int64_t *ids; /**< 呼び出し元へ引き渡すか cplat_free() で解放する配列。 */
        size_t count; /**< 格納された ID 件数。 */
    } potr_internal_config_id_list;

    /**
     *  @brief          段階 2 のルートの構造を検証し、必要であれば正規のサービス ID を収集します。
     *  @param[in]      root       段階 1 で解析したルート JSON。NULL は不可。
     *  @param[in,out]  reporter   誤り報告先。NULL 可。
     *  @param[out]     id_list    成功時かつ非 NULL 時にサービス ID 一覧を格納します。失敗時は NULL/0。
     *  @return         ルートの構造に誤りがなければ POTR_OK、誤りがあれば POTR_ERR_INVALID_CONFIG、
     *                  引数不正時は POTR_ERR_INVALID_ARGUMENT、配列確保失敗時は POTR_ERR_OUT_OF_MEMORY。
     */
    extern int potr_internal_config_decode_root(const struct cJSON *root, potr_internal_config_reporter *reporter,
                                                potr_internal_config_id_list *id_list);

    /**
     *  @brief          設定ファイル全体を検証します (validate_file 用、global と全サービスを対象)。
     *  @param[in]      root         段階 1 で解析したルート JSON。NULL は不可。
     *  @param[in]      roles        段階 4 で検査する役割ビット (POTR_CONFIG_ROLE_*)。
     *  @param[in]      on_service   ルート構造が正常な場合に各サービス ID を通知するコールバック。NULL 可。
     *  @param[in]      on_auth_file 証明書方式の有効なファイル パスを通知するコールバック。NULL 可。
     *  @param[in]      user_data    コールバックへ渡す利用者データ。
     *  @param[in,out]  reporter     誤り報告先。NULL 可。
     *  @return         誤り 0 件なら POTR_OK、1 件以上なら POTR_ERR_INVALID_CONFIG、
     *                  引数不正時は POTR_ERR_INVALID_ARGUMENT、メモリ不足時は POTR_ERR_OUT_OF_MEMORY。
     */
    extern int potr_internal_config_decode_validate_all(const struct cJSON *root, uint32_t roles,
                                                        potr_config_service_fn on_service,
                                                        potr_config_file_fn on_auth_file, void *user_data,
                                                        potr_internal_config_reporter *reporter);

    /**
     *  @brief          ルート、global、指定サービス 1 件を検証して定義構造体を構築します。
     *  @param[in]      root         段階 1 で解析したルート JSON (所有権は loaded へ移動しません。呼び出し元で管理、または loaded->document へ移譲)。
     *  @param[in]      service_id   対象のサービス ID。
     *  @param[in]      roles        段階 4 で検査する役割ビット (POTR_CONFIG_ROLE_*。get_type は 0)。
     *  @param[out]     global_out   成功時にグローバル設定を格納します。NULL 可。
     *  @param[out]     service_out  成功時にサービス定義を格納します。NULL 可。文字列は root 内を指すため document と寿命を共有します。
     *  @param[in,out]  reporter     誤り報告先。NULL 可。
     *  @return         成功時は POTR_OK、ルートの構造または対象・global に誤りがあれば POTR_ERR_INVALID_CONFIG、
     *                  ルートと global に誤りがなく対象サービスが存在しない場合は POTR_ERR_NOT_FOUND、
     *                  引数不正時は POTR_ERR_INVALID_ARGUMENT、メモリ不足時は POTR_ERR_OUT_OF_MEMORY。
     */
    extern int potr_internal_config_decode_target_service(const struct cJSON *root, int64_t service_id, uint32_t roles,
                                                          potr_global_config *global_out,
                                                          potr_internal_config_service *service_out,
                                                          potr_internal_config_reporter *reporter);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_PROTOCOL_CONFIG_DECODE_H */
