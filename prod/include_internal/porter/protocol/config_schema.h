/**
 *******************************************************************************
 *  @file           config_schema.h
 *  @brief          設定ファイルの既知キー定義・値域・通信種別と認証方式の対応表を提供します。
 *******************************************************************************
 */

#ifndef POTR_PROTOCOL_CONFIG_SCHEMA_H
#define POTR_PROTOCOL_CONFIG_SCHEMA_H

#include <stddef.h>
#include <stdint.h>

#include <porter/porter_type.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /** 設定ファイルの uint32 項目の上限値 (4294967295 は POTR_FLOW_CONTROL_DISABLED 専用)。 */
#define POTR_CONFIG_U32_MAX ((uint32_t)4294967294U)

    /** 通信種別に対応するビット集合 (bit i = 1 << (unsigned)potr_type)。 */
#define POTR_CONFIG_TYPE_BIT(t) ((uint32_t)1U << (unsigned)(t))

    /** 値の内部表現種別です。 */
    typedef enum potr_internal_config_value_kind
    {
        POTR_CONFIG_VAL_U8 = 1,
        POTR_CONFIG_VAL_U16 = 2,
        POTR_CONFIG_VAL_U32 = 3,
        POTR_CONFIG_VAL_TYPE_ENUM = 4,
        POTR_CONFIG_VAL_AUTH_ENUM = 5,
        POTR_CONFIG_VAL_MULTICAST_GROUP = 6,
        POTR_CONFIG_VAL_BROADCAST_ADDR = 7,
        POTR_CONFIG_VAL_ENCRYPT_KEY = 8,
        POTR_CONFIG_VAL_FILE_STR = 9,
        POTR_CONFIG_VAL_STR_ARRAY = 10
    } potr_internal_config_value_kind;

    /** 認証方式の適用条件です。 */
    typedef enum potr_internal_config_auth_req
    {
        POTR_CONFIG_AUTH_ANY = 0,
        POTR_CONFIG_AUTH_PSK_ONLY = 1,
        POTR_CONFIG_AUTH_CERT_ONLY = 2,
        POTR_CONFIG_AUTH_CERT_WITH_CA = 3
    } potr_internal_config_auth_req;

    /** 経路キーの種別です。 */
    typedef enum potr_internal_config_path_key_kind
    {
        POTR_CONFIG_PATH_SRC_ADDR = 1,
        POTR_CONFIG_PATH_DST_ADDR = 2,
        POTR_CONFIG_PATH_GROUP = 3
    } potr_internal_config_path_key_kind;

    /** global オブジェクトの既知キー識別子です (0..21)。 */
    typedef enum potr_internal_config_global_field_id
    {
        POTR_CONFIG_GFIELD_WINDOW_SIZE = 0,
        POTR_CONFIG_GFIELD_MAX_PAYLOAD,
        POTR_CONFIG_GFIELD_MAX_MESSAGE_SIZE,
        POTR_CONFIG_GFIELD_SEND_QUEUE_DEPTH,
        POTR_CONFIG_GFIELD_UDP_HEALTH_INTERVAL_MS,
        POTR_CONFIG_GFIELD_UDP_HEALTH_TIMEOUT_MS,
        POTR_CONFIG_GFIELD_TCP_HEALTH_INTERVAL_MS,
        POTR_CONFIG_GFIELD_TCP_HEALTH_TIMEOUT_MS,
        POTR_CONFIG_GFIELD_TCP_CLOSE_TIMEOUT_MS,
        POTR_CONFIG_GFIELD_REORDER_TIMEOUT_MS,
        POTR_CONFIG_GFIELD_FC_CAPACITY,
        POTR_CONFIG_GFIELD_FC_SUPPRESS_PERCENT,
        POTR_CONFIG_GFIELD_FC_RESUME_PERCENT,
        POTR_CONFIG_GFIELD_FC_REQUEST_INTERVAL_MS,
        POTR_CONFIG_GFIELD_FC_REQUEST_LIFETIME_MS,
        POTR_CONFIG_GFIELD_FC_EXCLUDED_RECEIVER_COUNT,
        POTR_CONFIG_GFIELD_FC_EXCLUDED_RECEIVER_PERCENT,
        POTR_CONFIG_GFIELD_FC_GRACE_MS,
        POTR_CONFIG_GFIELD_FC_FEEDBACK_PERCENT,
        POTR_CONFIG_GFIELD_FC_MONITORED_RECEIVER_COUNT,
        POTR_CONFIG_GFIELD_FC_RECEIVE_BUFFER_PERCENT,
        POTR_CONFIG_GFIELD_UDP_RECEIVE_BUFFER_SIZE,
        POTR_CONFIG_GFIELD_COUNT
    } potr_internal_config_global_field_id;

    /** services 配下の固定キー識別子です (0..32)。 */
    typedef enum potr_internal_config_service_field_id
    {
        POTR_CONFIG_SFIELD_TYPE = 0,
        POTR_CONFIG_SFIELD_DST_PORT,
        POTR_CONFIG_SFIELD_SRC_PORT,
        POTR_CONFIG_SFIELD_MULTICAST_GROUP,
        POTR_CONFIG_SFIELD_TTL,
        POTR_CONFIG_SFIELD_BROADCAST_ADDR,
        POTR_CONFIG_SFIELD_PACK_WAIT_MS,
        POTR_CONFIG_SFIELD_MAX_PEERS,
        POTR_CONFIG_SFIELD_HEALTH_INTERVAL_MS,
        POTR_CONFIG_SFIELD_HEALTH_TIMEOUT_MS,
        POTR_CONFIG_SFIELD_RECONNECT_INTERVAL_MS,
        POTR_CONFIG_SFIELD_CONNECT_TIMEOUT_MS,
        POTR_CONFIG_SFIELD_GROUP_FAILBACK_MS,
        POTR_CONFIG_SFIELD_AUTH,
        POTR_CONFIG_SFIELD_ENCRYPT_KEY,
        POTR_CONFIG_SFIELD_CERT_FILE,
        POTR_CONFIG_SFIELD_KEY_FILE,
        POTR_CONFIG_SFIELD_CA_FILE,
        POTR_CONFIG_SFIELD_CRL_FILE,
        POTR_CONFIG_SFIELD_PINNED_CERTS,
        POTR_CONFIG_SFIELD_PEER_NAMES,
        POTR_CONFIG_SFIELD_FC_CAPACITY,
        POTR_CONFIG_SFIELD_FC_SUPPRESS_PERCENT,
        POTR_CONFIG_SFIELD_FC_RESUME_PERCENT,
        POTR_CONFIG_SFIELD_FC_REQUEST_INTERVAL_MS,
        POTR_CONFIG_SFIELD_FC_REQUEST_LIFETIME_MS,
        POTR_CONFIG_SFIELD_FC_EXCLUDED_RECEIVER_COUNT,
        POTR_CONFIG_SFIELD_FC_EXCLUDED_RECEIVER_PERCENT,
        POTR_CONFIG_SFIELD_FC_GRACE_MS,
        POTR_CONFIG_SFIELD_FC_FEEDBACK_PERCENT,
        POTR_CONFIG_SFIELD_FC_MONITORED_RECEIVER_COUNT,
        POTR_CONFIG_SFIELD_FC_RECEIVE_BUFFER_PERCENT,
        POTR_CONFIG_SFIELD_UDP_RECEIVE_BUFFER_SIZE,
        POTR_CONFIG_SFIELD_COUNT
    } potr_internal_config_service_field_id;

    /** スキーマ 1 項目分の仕様です。 */
    typedef struct potr_internal_config_field_spec
    {
        unsigned int id;                      /**< 項目識別子 (GFIELD_* または SFIELD_*)。 */
        const char *name;                     /**< JSON キー名。 */
        potr_internal_config_value_kind kind; /**< 型の種別。 */
        uint32_t min_val;                     /**< 数値項目の下限。 */
        uint32_t max_val;                     /**< 数値項目の上限。 */
        uint32_t default_val;                 /**< 既定値 (構造体初期値)。 */
        int allow_minus_one;                  /**< JSON の -1 (POTR_FLOW_CONTROL_DISABLED) を許すか。 */
        int zero_is_default_in_struct;        /**< 直接構造体で 0 を既定値扱いするか (ttl、max_peers)。 */
        uint32_t applicable_types;            /**< 使える通信種別のビット集合。 */
        potr_internal_config_auth_req auth_req; /**< 認証方式の適用条件。 */
        size_t offset;                        /**< 対象構造体先頭からのバイト オフセット。 */
        const char *expected;                 /**< 誤り報告に使う期待形式・値域文字列。 */
    } potr_internal_config_field_spec;

    /**
     *  @brief          global オブジェクトの既知キー表を返します。
     *  @param[out]     count_out 件数の格納先。NULL 可。
     *  @return         既知キー定義配列の先頭。
     */
    extern const potr_internal_config_field_spec *potr_internal_config_schema_global_table(size_t *count_out);

    /**
     *  @brief          service オブジェクトの固定キー表を返します。
     *  @param[out]     count_out 件数の格納先。NULL 可。
     *  @return         固定キー定義配列の先頭。
     */
    extern const potr_internal_config_field_spec *potr_internal_config_schema_service_table(size_t *count_out);

    /**
     *  @brief          global オブジェクトのキー名に対応する定義を検索します。
     *  @param[in]      name キー名。
     *  @return         見つかれば定義へのポインター、未知キーは NULL。
     */
    extern const potr_internal_config_field_spec *potr_internal_config_schema_find_global(const char *name);

    /**
     *  @brief          service オブジェクトの固定キー名に対応する定義を検索します。
     *  @param[in]      name キー名。
     *  @return         見つかれば定義へのポインター、未知または経路キーの場合は NULL。
     */
    extern const potr_internal_config_field_spec *potr_internal_config_schema_find_service(const char *name);

    /**
     *  @brief          経路キー名 (src_addrN、dst_addrN、path_groupN) を解析します。
     *  @param[in]      name      キー名。
     *  @param[out]     kind_out  見つかれば経路キー種別を格納します。
     *  @param[out]     index_out 1 から 8 に対応する 0 始まり添字 (0..7) を格納します。
     *  @return         1 から 8 の正規表記なら POTR_OK、接尾辞が数値のみだが 1..8 外なら POTR_ERR_INVALID_CONFIG、
     *                  経路キーの命名でない場合 (先頭 0、空、非数字接尾辞など) は POTR_ERR_NOT_FOUND、
     *                  引数が NULL の場合は POTR_ERR_INVALID_ARGUMENT。
     */
    extern int potr_internal_config_schema_parse_path_key(const char *name, potr_internal_config_path_key_kind *kind_out,
                                                          size_t *index_out);

    /**
     *  @brief          通信種別のビット集合が指定種別を含むか判定します。
     *  @param[in]      type 通信種別。
     *  @param[in]      mask ビット集合。
     *  @return         使用可能なら 1、それ以外は 0。
     */
    extern int potr_internal_config_type_is_applicable(potr_type type, uint32_t mask);

    /**
     *  @brief          経路キー種別が指定通信種別で使えるか判定します。
     *  @param[in]      kind 経路キー種別。
     *  @param[in]      type 通信種別。
     *  @return         使用可能なら 1、種別に合わない場合は 0。
     */
    extern int potr_internal_config_path_key_is_applicable(potr_internal_config_path_key_kind kind, potr_type type);

    /**
     *  @brief          通信種別の文字列表記を列挙値へ変換します。
     *  @param[in]      text 文字列。
     *  @return         一致すれば POTR_TYPE_*、それ以外は 0 ((potr_type)0)。
     */
    extern potr_type potr_internal_config_type_from_string(const char *text);

    /**
     *  @brief          potr_global_config を設定ファイルの既定値で初期化します。
     *  @param[out]     global 初期化先。NULL は何もしません。
     */
    extern void potr_internal_config_global_init_defaults(potr_global_config *global);

    /**
     *  @brief          potr_service_def を設定ファイルの既定値で初期化します。
     *  @param[out]     service    初期化先。NULL は何もしません。
     *  @param[in]      service_id サービス ID。
     */
    extern void potr_internal_config_service_init_defaults(potr_service_def *service, int64_t service_id);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_PROTOCOL_CONFIG_SCHEMA_H */
