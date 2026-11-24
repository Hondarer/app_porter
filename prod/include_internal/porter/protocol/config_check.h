/**
 *******************************************************************************
 *  @file           config_check.h
 *  @brief          設定構造体の検証 (段階 4) と共通の実効値計算関数を提供します。
 *******************************************************************************
 */

#ifndef POTR_PROTOCOL_CONFIG_CHECK_H
#define POTR_PROTOCOL_CONFIG_CHECK_H

#include <stdint.h>

#include <porter/porter_type.h>
#include <porter/protocol/config_report.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /** 段階 3 で誤りが見つかった項目を記録し、段階 4 の前提崩れ制約を省くためのビット集合です。 */
    typedef struct potr_internal_config_field_errors
    {
        uint32_t global_mask;            /**< global 既知キー (GFIELD_*) の誤りビット。 */
        uint64_t service_mask;           /**< service 固定キー (SFIELD_*) の誤りビット。 */
        uint8_t src_addr_mask;           /**< src_addr1..8 (bit 0..7) の誤りビット。 */
        uint8_t dst_addr_mask;           /**< dst_addr1..8 (bit 0..7) の誤りビット。 */
        uint8_t path_group_mask;         /**< path_group1..8 (bit 0..7) の誤りビット。 */
        uint8_t has_path_group_bitmask;  /**< JSONC に明示指定された path_group1..8 (bit 0..7)。 */
    } potr_internal_config_field_errors;

    /**
     *  @brief          設定値のマルチキャスト送信 TTL をソケット用の実効値へ変換します。
     *  @param[in]      configured_ttl 設定値。0 は構造体での未指定 (既定値 POTR_DEFAULT_TTL) を表します。
     *  @return         実効 TTL (1..255)。
     *
     *  設定の検証および通信側のマルチキャスト ソケット初期化の双方がこの関数を使用します。\n
     *  入力 0 は POTR_DEFAULT_TTL (1) へ変換し、1 から 255 はそのまま返します。
     */
    extern unsigned int potr_internal_config_effective_ttl(uint8_t configured_ttl);

    /**
     *  @brief          通信種別とサービス単位の上書きを考慮した実効ヘルス チェック間隔とタイムアウトを求めます。
     *  @param[in]      service         サービス定義。NULL は不可。
     *  @param[in]      global          グローバル設定。NULL は不可。
     *  @param[out]     interval_ms_out 実効ヘルス チェック間隔 [ms]。NULL 可。
     *  @param[out]     timeout_ms_out  実効ヘルス チェック タイムアウト [ms]。NULL 可。
     */
    extern void potr_internal_config_resolve_health(const potr_service_def *service, const potr_global_config *global,
                                                    uint32_t *interval_ms_out, uint32_t *timeout_ms_out);

    /**
     *  @brief          グローバル設定構造体の値域と項目間制約を検査します。
     *  @param[in]      global      検査対象のグローバル設定。NULL は不可。
     *  @param[in]      errors      段階 3 の誤り記録。構造体の直接検証では NULL を指定します。
     *  @param[in]      global_path 誤り報告時の基準 JSON Pointer。NULL の場合は "/global" を使用します。
     *  @param[in,out]  reporter    誤り報告先。NULL 可。
     *  @return         誤りが 0 件なら POTR_OK、1 件以上見つけた場合は POTR_ERR_INVALID_CONFIG、
     *                  global が NULL なら POTR_ERR_INVALID_ARGUMENT、経路構築でメモリ不足時は POTR_ERR_OUT_OF_MEMORY。
     */
    extern int potr_internal_config_check_global(const potr_global_config *global,
                                                 const potr_internal_config_field_errors *errors,
                                                 const char *global_path, potr_internal_config_reporter *reporter);

    /**
     *  @brief          サービス定義構造体の制約・経路・認証組み合わせ・役割依存条件を検査します。
     *  @param[in]      global         グローバル設定。NULL は不可。
     *  @param[in]      service        検査対象のサービス定義。NULL は不可。
     *  @param[in]      roles          検査する役割の論理和 (POTR_CONFIG_ROLE_*。0 は役割依存条件を省く)。
     *  @param[in]      is_json_config JSONC 読み込み由来なら非 0、potr_service_open() からの直接渡しなら 0。
     *  @param[in]      has_ca_file    認証で ca_file が存在する場合に非 0 (is_json_config == 0 の場合は構造体から判定可)。
     *  @param[in]      errors         段階 3 の誤り記録。直接検証時は NULL。
     *  @param[in]      service_path   誤り報告時の基準 JSON Pointer (例: "/services/1001" または "")。
     *  @param[in]      has_service_id 報告時の service_id 付与フラグ。
     *  @param[in]      service_id     サービス ID。
     *  @param[in,out]  reporter       誤り報告先。NULL 可。
     *  @return         誤り 0 件なら POTR_OK、1 件以上なら POTR_ERR_INVALID_CONFIG、
     *                  引数不正時は POTR_ERR_INVALID_ARGUMENT、メモリ不足時は POTR_ERR_OUT_OF_MEMORY。
     */
    extern int potr_internal_config_check_service(const potr_global_config *global, const potr_service_def *service,
                                                  uint32_t roles, int is_json_config, int has_ca_file,
                                                  const potr_internal_config_field_errors *errors,
                                                  const char *service_path, int has_service_id, int64_t service_id,
                                                  potr_internal_config_reporter *reporter);

    /**
     *  @brief          構造体の直接渡し (potr_service_open) 向けに global と service の両方を検査します。
     *  @param[in]      global      グローバル設定。NULL は不可。
     *  @param[in]      service     サービス定義。NULL は不可。
     *  @param[in]      role        開く役割 (POTR_ROLE_SENDER または POTR_ROLE_RECEIVER)。
     *  @param[in,out]  reporter    誤り報告先。NULL 可。
     *  @return         誤り 0 件なら POTR_OK、1 件以上なら POTR_ERR_INVALID_CONFIG、
     *                  引数不正時は POTR_ERR_INVALID_ARGUMENT、メモリ不足時は POTR_ERR_OUT_OF_MEMORY。
     */
    extern int potr_internal_config_check_all(const potr_global_config *global, const potr_service_def *service,
                                              potr_role role, potr_internal_config_reporter *reporter);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_PROTOCOL_CONFIG_CHECK_H */
