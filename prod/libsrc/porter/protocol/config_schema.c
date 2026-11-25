/**
 *******************************************************************************
 *  @file           config_schema.c
 *  @brief          設定ファイルの既知キー表・値域・通信種別と認証方式の対応表を実装します。
 *******************************************************************************
 */

#include <stddef.h>
#include <string.h>

#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include <porter/potr_context.h>
#include <porter/protocol/config_schema.h>

#define TYPE_ALL \
    (POTR_CONFIG_TYPE_BIT(POTR_TYPE_UNICAST) | POTR_CONFIG_TYPE_BIT(POTR_TYPE_MULTICAST) | \
     POTR_CONFIG_TYPE_BIT(POTR_TYPE_BROADCAST) | POTR_CONFIG_TYPE_BIT(POTR_TYPE_UNICAST_RAW) | \
     POTR_CONFIG_TYPE_BIT(POTR_TYPE_MULTICAST_RAW) | POTR_CONFIG_TYPE_BIT(POTR_TYPE_BROADCAST_RAW) | \
     POTR_CONFIG_TYPE_BIT(POTR_TYPE_UNICAST_BIDIR) | POTR_CONFIG_TYPE_BIT(POTR_TYPE_UNICAST_BIDIR_N1) | \
     POTR_CONFIG_TYPE_BIT(POTR_TYPE_TCP) | POTR_CONFIG_TYPE_BIT(POTR_TYPE_TCP_BIDIR))

#define TYPE_RAW_MASK \
    (POTR_CONFIG_TYPE_BIT(POTR_TYPE_UNICAST_RAW) | POTR_CONFIG_TYPE_BIT(POTR_TYPE_MULTICAST_RAW) | \
     POTR_CONFIG_TYPE_BIT(POTR_TYPE_BROADCAST_RAW))

#define TYPE_NON_RAW_MASK (TYPE_ALL & ~TYPE_RAW_MASK)

#define TYPE_TCP_MASK (POTR_CONFIG_TYPE_BIT(POTR_TYPE_TCP) | POTR_CONFIG_TYPE_BIT(POTR_TYPE_TCP_BIDIR))

#define TYPE_UDP_MASK (TYPE_ALL & ~TYPE_TCP_MASK)

#define TYPE_MCAST_MASK (POTR_CONFIG_TYPE_BIT(POTR_TYPE_MULTICAST) | POTR_CONFIG_TYPE_BIT(POTR_TYPE_MULTICAST_RAW))

#define TYPE_BCAST_MASK (POTR_CONFIG_TYPE_BIT(POTR_TYPE_BROADCAST) | POTR_CONFIG_TYPE_BIT(POTR_TYPE_BROADCAST_RAW))

#define TYPE_SRC_ADDR_MASK (TYPE_ALL & ~POTR_CONFIG_TYPE_BIT(POTR_TYPE_UNICAST_BIDIR_N1))
#define TYPE_DST_ADDR_MASK (TYPE_ALL & ~TYPE_BCAST_MASK)
#define TYPE_MAX_PEERS_MASK (TYPE_MCAST_MASK | TYPE_BCAST_MASK | POTR_CONFIG_TYPE_BIT(POTR_TYPE_UNICAST_BIDIR_N1))
#define TYPE_1TON_EXCLUDED_MASK \
    (POTR_CONFIG_TYPE_BIT(POTR_TYPE_MULTICAST) | POTR_CONFIG_TYPE_BIT(POTR_TYPE_BROADCAST) | \
     POTR_CONFIG_TYPE_BIT(POTR_TYPE_UNICAST_BIDIR_N1))
#define TYPE_FEEDBACK_MASK (POTR_CONFIG_TYPE_BIT(POTR_TYPE_MULTICAST) | POTR_CONFIG_TYPE_BIT(POTR_TYPE_BROADCAST))
#define TYPE_RCVBUF_PCT_MASK (TYPE_UDP_MASK & ~TYPE_RAW_MASK)

#define TCP_HEALTH_TIMEOUT_LIMIT (POTR_WAIT_MS_MAX / POTR_TCP_FIRST_PKT_TIMEOUT_SCALE)

static const potr_internal_config_field_spec s_global_schema[] = {
    {POTR_CONFIG_GFIELD_WINDOW_SIZE, 0U, "window_size", POTR_CONFIG_VAL_U16, 2U, POTR_MAX_WINDOW_SIZE,
     POTR_DEFAULT_WINDOW_SIZE, 0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY, offsetof(potr_global_config, window_size),
     "2..256"},
    {POTR_CONFIG_GFIELD_MAX_PAYLOAD, 0U, "max_payload", POTR_CONFIG_VAL_U16, 64U, POTR_MAX_PAYLOAD,
     POTR_DEFAULT_MAX_PAYLOAD, 0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY, offsetof(potr_global_config, max_payload),
     "64..65435"},
    {POTR_CONFIG_GFIELD_MAX_MESSAGE_SIZE, 0U, "max_message_size", POTR_CONFIG_VAL_U32, 0U, POTR_CONFIG_U32_MAX,
     POTR_MAX_MESSAGE_SIZE, 0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY, offsetof(potr_global_config, max_message_size),
     "0..4294967294"},
    {POTR_CONFIG_GFIELD_SEND_QUEUE_DEPTH, 0U, "send_queue_depth", POTR_CONFIG_VAL_U32, 2U, POTR_CONFIG_U32_MAX,
     POTR_SEND_QUEUE_DEPTH, 0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY, offsetof(potr_global_config, send_queue_depth),
     "2..4294967294"},
    {POTR_CONFIG_GFIELD_UDP_HEALTH_INTERVAL_MS, 0U, "udp_health_interval_ms", POTR_CONFIG_VAL_U32, 0U, POTR_WAIT_MS_MAX,
     POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS, 0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_global_config, udp_health_interval_ms), "0..2147483646"},
    {POTR_CONFIG_GFIELD_UDP_HEALTH_TIMEOUT_MS, 0U, "udp_health_timeout_ms", POTR_CONFIG_VAL_U32, 0U, POTR_WAIT_MS_MAX,
     POTR_DEFAULT_UDP_HEALTH_TIMEOUT_MS, 0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_global_config, udp_health_timeout_ms), "0..2147483646"},
    {POTR_CONFIG_GFIELD_TCP_HEALTH_INTERVAL_MS, 0U, "tcp_health_interval_ms", POTR_CONFIG_VAL_U32, 0U, POTR_WAIT_MS_MAX,
     POTR_DEFAULT_TCP_HEALTH_INTERVAL_MS, 0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_global_config, tcp_health_interval_ms), "0..2147483646"},
    {POTR_CONFIG_GFIELD_TCP_HEALTH_TIMEOUT_MS, 0U, "tcp_health_timeout_ms", POTR_CONFIG_VAL_U32, 0U,
     TCP_HEALTH_TIMEOUT_LIMIT, POTR_DEFAULT_TCP_HEALTH_TIMEOUT_MS, 0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_global_config, tcp_health_timeout_ms), "0..715827882"},
    {POTR_CONFIG_GFIELD_TCP_CLOSE_TIMEOUT_MS, 0U, "tcp_close_timeout_ms", POTR_CONFIG_VAL_U32, 0U, POTR_WAIT_MS_MAX,
     POTR_DEFAULT_TCP_CLOSE_TIMEOUT_MS, 0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_global_config, tcp_close_timeout_ms), "0..2147483646"},
    {POTR_CONFIG_GFIELD_REORDER_TIMEOUT_MS, 0U, "reorder_timeout_ms", POTR_CONFIG_VAL_U32, 0U, POTR_WAIT_MS_MAX, 0U,
     0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY, offsetof(potr_global_config, reorder_timeout_ms), "0..2147483646"},
    {POTR_CONFIG_GFIELD_FC_CAPACITY, 0U, "flow_control_capacity", POTR_CONFIG_VAL_U32, 0U, POTR_CONFIG_U32_MAX, 0U, 0,
     0, TYPE_ALL, POTR_CONFIG_AUTH_ANY, offsetof(potr_global_config, flow_control_capacity), "0..4294967294"},
    {POTR_CONFIG_GFIELD_FC_SUPPRESS_PERCENT, 0U, "flow_control_suppress_percent", POTR_CONFIG_VAL_U32, 0U, 100U,
     POTR_DEFAULT_FLOW_CONTROL_SUPPRESS_PERCENT, 0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_global_config, flow_control_suppress_percent), "0..100"},
    {POTR_CONFIG_GFIELD_FC_RESUME_PERCENT, 0U, "flow_control_resume_percent", POTR_CONFIG_VAL_U32, 0U, 99U,
     POTR_DEFAULT_FLOW_CONTROL_RESUME_PERCENT, 0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_global_config, flow_control_resume_percent), "0..99"},
    {POTR_CONFIG_GFIELD_FC_REQUEST_INTERVAL_MS, 0U, "flow_control_request_interval_ms", POTR_CONFIG_VAL_U32, 0U,
     POTR_WAIT_MS_MAX, POTR_DEFAULT_FLOW_CONTROL_REQUEST_INTERVAL_MS, 0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_global_config, flow_control_request_interval_ms), "0..2147483646"},
    {POTR_CONFIG_GFIELD_FC_REQUEST_LIFETIME_MS, 0U, "flow_control_request_lifetime_ms", POTR_CONFIG_VAL_U32, 0U,
     POTR_WAIT_MS_MAX, POTR_DEFAULT_FLOW_CONTROL_REQUEST_LIFETIME_MS, 0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_global_config, flow_control_request_lifetime_ms), "0..2147483646"},
    {POTR_CONFIG_GFIELD_FC_EXCLUDED_RECEIVER_COUNT, 0U, "flow_control_excluded_receiver_count", POTR_CONFIG_VAL_U32, 0U,
     POTR_CONFIG_U32_MAX, POTR_DEFAULT_FLOW_CONTROL_EXCLUDED_RECEIVER_COUNT, 1, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_global_config, flow_control_excluded_receiver_count), "-1 or 0..4294967294"},
    {POTR_CONFIG_GFIELD_FC_EXCLUDED_RECEIVER_PERCENT, 0U, "flow_control_excluded_receiver_percent", POTR_CONFIG_VAL_U32,
     0U, 100U, POTR_DEFAULT_FLOW_CONTROL_EXCLUDED_RECEIVER_PERCENT, 1, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_global_config, flow_control_excluded_receiver_percent), "-1 or 0..100"},
    {POTR_CONFIG_GFIELD_FC_GRACE_MS, 0U, "flow_control_grace_ms", POTR_CONFIG_VAL_U32, 0U, POTR_WAIT_MS_MAX,
     POTR_DEFAULT_FLOW_CONTROL_GRACE_MS, 0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_global_config, flow_control_grace_ms), "0..2147483646"},
    {POTR_CONFIG_GFIELD_FC_FEEDBACK_PERCENT, 0U, "flow_control_feedback_percent", POTR_CONFIG_VAL_U32, 0U, 100U,
     POTR_DEFAULT_FLOW_CONTROL_FEEDBACK_PERCENT, 0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_global_config, flow_control_feedback_percent), "0..100"},
    {POTR_CONFIG_GFIELD_FC_MONITORED_RECEIVER_COUNT, 0U, "flow_control_monitored_receiver_count", POTR_CONFIG_VAL_U32,
     0U, POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX, POTR_DEFAULT_FLOW_CONTROL_MONITORED_RECEIVER_COUNT, 0, 0, TYPE_ALL,
     POTR_CONFIG_AUTH_ANY, offsetof(potr_global_config, flow_control_monitored_receiver_count), "0..64"},
    {POTR_CONFIG_GFIELD_FC_RECEIVE_BUFFER_PERCENT, 0U, "flow_control_receive_buffer_percent", POTR_CONFIG_VAL_U32, 0U,
     100U, POTR_DEFAULT_FLOW_CONTROL_RECEIVE_BUFFER_PERCENT, 1, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_global_config, flow_control_receive_buffer_percent), "-1 or 0..100"},
    {POTR_CONFIG_GFIELD_UDP_RECEIVE_BUFFER_SIZE, 0U, "udp_receive_buffer_size", POTR_CONFIG_VAL_U32, 0U,
     POTR_CONFIG_U32_MAX, POTR_DEFAULT_UDP_RECEIVE_BUFFER_SIZE, 1, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_global_config, udp_receive_buffer_size), "-1 or 0..4294967294"},
};

static const potr_internal_config_field_spec s_service_schema[] = {
    {POTR_CONFIG_SFIELD_TYPE, 0U, "type", POTR_CONFIG_VAL_TYPE_ENUM, 0U, 0U, 0U, 0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_service_def, type),
     "unicast|multicast|broadcast|unicast_raw|multicast_raw|broadcast_raw|unicast_bidir|unicast_bidir_n1|tcp|tcp_"
     "bidir"},
    {POTR_CONFIG_SFIELD_DST_PORT, 0U, "dst_port", POTR_CONFIG_VAL_U16, 1U, UINT16_MAX, 0U, 0, 0, TYPE_ALL,
     POTR_CONFIG_AUTH_ANY, offsetof(potr_service_def, dst_port), "1..65535"},
    {POTR_CONFIG_SFIELD_SRC_PORT, 0U, "src_port", POTR_CONFIG_VAL_U16, 0U, UINT16_MAX, 0U, 0, 0, TYPE_ALL,
     POTR_CONFIG_AUTH_ANY, offsetof(potr_service_def, src_port), "0..65535"},
    {POTR_CONFIG_SFIELD_MULTICAST_GROUP, 0U, "multicast_group", POTR_CONFIG_VAL_MULTICAST_GROUP, 0U, 0U, 0U, 0, 0,
     TYPE_MCAST_MASK, POTR_CONFIG_AUTH_ANY, offsetof(potr_service_def, multicast_group),
     "IPv4 multicast address (224.0.0.0..239.255.255.255)"},
    {POTR_CONFIG_SFIELD_TTL, 0U, "ttl", POTR_CONFIG_VAL_U8, 1U, UINT8_MAX, POTR_DEFAULT_TTL, 0, 1, TYPE_MCAST_MASK,
     POTR_CONFIG_AUTH_ANY, offsetof(potr_service_def, ttl), "1..255"},
    {POTR_CONFIG_SFIELD_BROADCAST_ADDR, 0U, "broadcast_addr", POTR_CONFIG_VAL_BROADCAST_ADDR, 0U, 0U, 0U, 0, 0,
     TYPE_BCAST_MASK, POTR_CONFIG_AUTH_ANY, offsetof(potr_service_def, broadcast_addr), "IPv4 address"},
    {POTR_CONFIG_SFIELD_PACK_WAIT_MS, 0U, "pack_wait_ms", POTR_CONFIG_VAL_U32, 0U, POTR_WAIT_MS_MAX,
     POTR_DEFAULT_PACK_WAIT_MS, 0, 0, TYPE_ALL, POTR_CONFIG_AUTH_ANY, offsetof(potr_service_def, pack_wait_ms),
     "0..2147483646"},
    {POTR_CONFIG_SFIELD_MAX_PEERS, 0U, "max_peers", POTR_CONFIG_VAL_U32, 1U, (uint32_t)INT32_MAX, POTR_DEFAULT_MAX_PEERS,
     0, 1, TYPE_MAX_PEERS_MASK, POTR_CONFIG_AUTH_ANY, offsetof(potr_service_def, max_peers), "1..2147483647"},
    {POTR_CONFIG_SFIELD_HEALTH_INTERVAL_MS, 0U, "health_interval_ms", POTR_CONFIG_VAL_U32, 0U, POTR_WAIT_MS_MAX, 0U, 0,
     0, TYPE_ALL, POTR_CONFIG_AUTH_ANY, offsetof(potr_service_def, health_interval_ms), "0..2147483646"},
    {POTR_CONFIG_SFIELD_HEALTH_TIMEOUT_MS, 0U, "health_timeout_ms", POTR_CONFIG_VAL_U32, 0U, POTR_WAIT_MS_MAX, 0U, 0,
     0, TYPE_ALL, POTR_CONFIG_AUTH_ANY, offsetof(potr_service_def, health_timeout_ms), "0..2147483646"},
    {POTR_CONFIG_SFIELD_RECONNECT_INTERVAL_MS, 0U, "reconnect_interval_ms", POTR_CONFIG_VAL_U32, 0U, POTR_WAIT_MS_MAX,
     POTR_DEFAULT_RECONNECT_INTERVAL_MS, 0, 0, TYPE_TCP_MASK, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_service_def, reconnect_interval_ms), "0..2147483646"},
    {POTR_CONFIG_SFIELD_CONNECT_TIMEOUT_MS, 0U, "connect_timeout_ms", POTR_CONFIG_VAL_U32, 0U, POTR_WAIT_MS_MAX,
     POTR_DEFAULT_CONNECT_TIMEOUT_MS, 0, 0, TYPE_TCP_MASK, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_service_def, connect_timeout_ms), "0..2147483646"},
    {POTR_CONFIG_SFIELD_GROUP_FAILBACK_MS, 0U, "group_failback_ms", POTR_CONFIG_VAL_U32, 0U, POTR_CONFIG_U32_MAX, 0U, 0,
     0, TYPE_NON_RAW_MASK, POTR_CONFIG_AUTH_ANY, offsetof(potr_service_def, group_failback_ms), "0..4294967294"},
    {POTR_CONFIG_SFIELD_AUTH, 0U, "auth", POTR_CONFIG_VAL_AUTH_ENUM, 0U, 0U, (uint32_t)POTR_AUTH_PSK, 0, 0, TYPE_ALL,
     POTR_CONFIG_AUTH_ANY, offsetof(potr_service_def, auth), "psk|cert"},
    {POTR_CONFIG_SFIELD_ENCRYPT_KEY, 0U, "encrypt_key", POTR_CONFIG_VAL_ENCRYPT_KEY, 0U, 0U, 0U, 0, 0, TYPE_ALL,
     POTR_CONFIG_AUTH_PSK_ONLY, 0U, "non-empty string or 64 hex chars"},
    {POTR_CONFIG_SFIELD_CERT_FILE, 0U, "cert_file", POTR_CONFIG_VAL_FILE_STR, 0U, 0U, 0U, 0, 0, TYPE_ALL,
     POTR_CONFIG_AUTH_CERT_ONLY, offsetof(potr_service_def, cert_file), "non-empty string"},
    {POTR_CONFIG_SFIELD_KEY_FILE, 0U, "key_file", POTR_CONFIG_VAL_FILE_STR, 0U, 0U, 0U, 0, 0, TYPE_ALL,
     POTR_CONFIG_AUTH_CERT_ONLY, offsetof(potr_service_def, key_file), "non-empty string"},
    {POTR_CONFIG_SFIELD_CA_FILE, 0U, "ca_file", POTR_CONFIG_VAL_FILE_STR, 0U, 0U, 0U, 0, 0, TYPE_ALL,
     POTR_CONFIG_AUTH_CERT_ONLY, offsetof(potr_service_def, ca_file), "non-empty string"},
    {POTR_CONFIG_SFIELD_CRL_FILE, 0U, "crl_file", POTR_CONFIG_VAL_FILE_STR, 0U, 0U, 0U, 0, 0, TYPE_ALL,
     POTR_CONFIG_AUTH_CERT_WITH_CA, offsetof(potr_service_def, crl_file), "non-empty string"},
    {POTR_CONFIG_SFIELD_PINNED_CERTS, 0U, "pinned_certs", POTR_CONFIG_VAL_STR_ARRAY, 0U, 0U, 0U, 0, 0, TYPE_ALL,
     POTR_CONFIG_AUTH_CERT_ONLY, 0U, "array of non-empty strings"},
    {POTR_CONFIG_SFIELD_PEER_NAMES, 0U, "peer_names", POTR_CONFIG_VAL_STR_ARRAY, 0U, 0U, 0U, 0, 0, TYPE_ALL,
     POTR_CONFIG_AUTH_CERT_WITH_CA, 0U, "array of non-empty strings"},
    {POTR_CONFIG_SFIELD_FC_CAPACITY, 0U, "flow_control_capacity", POTR_CONFIG_VAL_U32, 0U, POTR_CONFIG_U32_MAX, 0U, 1,
     0, TYPE_NON_RAW_MASK, POTR_CONFIG_AUTH_ANY, offsetof(potr_service_def, flow_control_capacity),
     "-1 or 0..4294967294"},
    {POTR_CONFIG_SFIELD_FC_SUPPRESS_PERCENT, 0U, "flow_control_suppress_percent", POTR_CONFIG_VAL_U32, 0U, 100U, 0U, 0,
     0, TYPE_NON_RAW_MASK, POTR_CONFIG_AUTH_ANY, offsetof(potr_service_def, flow_control_suppress_percent), "0..100"},
    {POTR_CONFIG_SFIELD_FC_RESUME_PERCENT, 0U, "flow_control_resume_percent", POTR_CONFIG_VAL_U32, 0U, 99U, 0U, 0, 0,
     TYPE_NON_RAW_MASK, POTR_CONFIG_AUTH_ANY, offsetof(potr_service_def, flow_control_resume_percent), "0..99"},
    {POTR_CONFIG_SFIELD_FC_REQUEST_INTERVAL_MS, 0U, "flow_control_request_interval_ms", POTR_CONFIG_VAL_U32, 0U,
     POTR_WAIT_MS_MAX, 0U, 0, 0, TYPE_NON_RAW_MASK, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_service_def, flow_control_request_interval_ms), "0..2147483646"},
    {POTR_CONFIG_SFIELD_FC_REQUEST_LIFETIME_MS, 0U, "flow_control_request_lifetime_ms", POTR_CONFIG_VAL_U32, 0U,
     POTR_WAIT_MS_MAX, 0U, 0, 0, TYPE_NON_RAW_MASK, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_service_def, flow_control_request_lifetime_ms), "0..2147483646"},
    {POTR_CONFIG_SFIELD_FC_EXCLUDED_RECEIVER_COUNT, 0U, "flow_control_excluded_receiver_count", POTR_CONFIG_VAL_U32, 0U,
     POTR_CONFIG_U32_MAX, 0U, 1, 0, TYPE_1TON_EXCLUDED_MASK, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_service_def, flow_control_excluded_receiver_count), "-1 or 0..4294967294"},
    {POTR_CONFIG_SFIELD_FC_EXCLUDED_RECEIVER_PERCENT, 0U, "flow_control_excluded_receiver_percent", POTR_CONFIG_VAL_U32,
     0U, 100U, 0U, 1, 0, TYPE_1TON_EXCLUDED_MASK, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_service_def, flow_control_excluded_receiver_percent), "-1 or 0..100"},
    {POTR_CONFIG_SFIELD_FC_GRACE_MS, 0U, "flow_control_grace_ms", POTR_CONFIG_VAL_U32, 0U, POTR_WAIT_MS_MAX, 0U, 0, 0,
     TYPE_1TON_EXCLUDED_MASK, POTR_CONFIG_AUTH_ANY, offsetof(potr_service_def, flow_control_grace_ms),
     "0..2147483646"},
    {POTR_CONFIG_SFIELD_FC_FEEDBACK_PERCENT, 0U, "flow_control_feedback_percent", POTR_CONFIG_VAL_U32, 0U, 100U, 0U, 0,
     0, TYPE_FEEDBACK_MASK, POTR_CONFIG_AUTH_ANY, offsetof(potr_service_def, flow_control_feedback_percent), "0..100"},
    {POTR_CONFIG_SFIELD_FC_MONITORED_RECEIVER_COUNT, 0U, "flow_control_monitored_receiver_count", POTR_CONFIG_VAL_U32,
     0U, POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX, 0U, 1, 0, TYPE_FEEDBACK_MASK, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_service_def, flow_control_monitored_receiver_count), "-1 or 0..64"},
    {POTR_CONFIG_SFIELD_FC_RECEIVE_BUFFER_PERCENT, 0U, "flow_control_receive_buffer_percent", POTR_CONFIG_VAL_U32, 0U,
     100U, 0U, 1, 0, TYPE_RCVBUF_PCT_MASK, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_service_def, flow_control_receive_buffer_percent), "-1 or 0..100"},
    {POTR_CONFIG_SFIELD_UDP_RECEIVE_BUFFER_SIZE, 0U, "udp_receive_buffer_size", POTR_CONFIG_VAL_U32, 0U,
     POTR_CONFIG_U32_MAX, 0U, 1, 0, TYPE_UDP_MASK, POTR_CONFIG_AUTH_ANY,
     offsetof(potr_service_def, udp_receive_buffer_size), "-1 or 0..4294967294"},
};

static const potr_internal_config_field_spec *find_in_table(const potr_internal_config_field_spec *table, size_t count,
                                                            const char *name)
{
    size_t i;

    if (name == NULL)
    {
        return NULL;
    }

    for (i = 0U; i < count; i++)
    {
        if (strcmp(table[i].name, name) == 0)
        {
            return &table[i];
        }
    }

    return NULL;
}

/* Doxygen コメントは、ヘッダーに記載 */

const potr_internal_config_field_spec *potr_internal_config_schema_global_table(size_t *count_out)
{
    if (count_out != NULL)
    {
        *count_out = sizeof(s_global_schema) / sizeof(s_global_schema[0]);
    }
    return s_global_schema;
}

/* Doxygen コメントは、ヘッダーに記載 */

const potr_internal_config_field_spec *potr_internal_config_schema_service_table(size_t *count_out)
{
    if (count_out != NULL)
    {
        *count_out = sizeof(s_service_schema) / sizeof(s_service_schema[0]);
    }
    return s_service_schema;
}

/* Doxygen コメントは、ヘッダーに記載 */

const potr_internal_config_field_spec *potr_internal_config_schema_find_global(const char *name)
{
    return find_in_table(s_global_schema, sizeof(s_global_schema) / sizeof(s_global_schema[0]), name);
}

/* Doxygen コメントは、ヘッダーに記載 */

const potr_internal_config_field_spec *potr_internal_config_schema_find_service(const char *name)
{
    return find_in_table(s_service_schema, sizeof(s_service_schema) / sizeof(s_service_schema[0]), name);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_config_schema_parse_path_key(const char *name, potr_internal_config_path_key_kind *kind_out,
                                               size_t *index_out)
{
    const char *suffix = NULL;
    potr_internal_config_path_key_kind kind = POTR_CONFIG_PATH_SRC_ADDR;
    size_t i;

    if (name == NULL || kind_out == NULL || index_out == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (strncmp(name, "src_addr", 8U) == 0)
    {
        kind = POTR_CONFIG_PATH_SRC_ADDR;
        suffix = name + 8U;
    }
    else if (strncmp(name, "dst_addr", 8U) == 0)
    {
        kind = POTR_CONFIG_PATH_DST_ADDR;
        suffix = name + 8U;
    }
    else if (strncmp(name, "path_group", 10U) == 0)
    {
        kind = POTR_CONFIG_PATH_GROUP;
        suffix = name + 10U;
    }
    else
    {
        return POTR_ERR_NOT_FOUND;
    }

    if (suffix[0] == '\0')
    {
        return POTR_ERR_NOT_FOUND;
    }

    for (i = 0U; suffix[i] != '\0'; i++)
    {
        if (suffix[i] < '0' || suffix[i] > '9')
        {
            return POTR_ERR_NOT_FOUND;
        }
    }

    /* 先頭に 0 を含む複数桁の番号 (例: "01") は経路キーではなく知らないキーとして扱う。 */
    if (suffix[0] == '0' && suffix[1] != '\0')
    {
        return POTR_ERR_NOT_FOUND;
    }

    *kind_out = kind;

    if (suffix[1] == '\0' && suffix[0] >= '1' && suffix[0] <= '8')
    {
        *index_out = (size_t)(suffix[0] - '1');
        return POTR_OK;
    }

    return POTR_ERR_INVALID_CONFIG;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_config_type_is_applicable(potr_type type, uint32_t mask)
{
    if (type < POTR_TYPE_UNICAST_RAW || type > POTR_TYPE_TCP_BIDIR)
    {
        return 0;
    }

    return (mask & POTR_CONFIG_TYPE_BIT(type)) != 0U ? 1 : 0;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_config_path_key_is_applicable(potr_internal_config_path_key_kind kind, potr_type type)
{
    if (kind == POTR_CONFIG_PATH_SRC_ADDR)
    {
        return potr_internal_config_type_is_applicable(type, TYPE_SRC_ADDR_MASK);
    }
    if (kind == POTR_CONFIG_PATH_DST_ADDR)
    {
        return potr_internal_config_type_is_applicable(type, TYPE_DST_ADDR_MASK);
    }
    return potr_internal_config_type_is_applicable(type, TYPE_NON_RAW_MASK);
}

/* Doxygen コメントは、ヘッダーに記載 */

potr_type potr_internal_config_type_from_string(const char *text)
{
    typedef struct type_entry
    {
        const char *name;
        potr_type value;
        unsigned int pad;
    } type_entry;

    static const type_entry table[] = {
        {"unicast", POTR_TYPE_UNICAST, 0U},
        {"multicast", POTR_TYPE_MULTICAST, 0U},
        {"broadcast", POTR_TYPE_BROADCAST, 0U},
        {"unicast_raw", POTR_TYPE_UNICAST_RAW, 0U},
        {"multicast_raw", POTR_TYPE_MULTICAST_RAW, 0U},
        {"broadcast_raw", POTR_TYPE_BROADCAST_RAW, 0U},
        {"unicast_bidir", POTR_TYPE_UNICAST_BIDIR, 0U},
        {"unicast_bidir_n1", POTR_TYPE_UNICAST_BIDIR_N1, 0U},
        {"tcp", POTR_TYPE_TCP, 0U},
        {"tcp_bidir", POTR_TYPE_TCP_BIDIR, 0U},
    };
    size_t i;

    if (text == NULL)
    {
        return (potr_type)0;
    }

    for (i = 0U; i < sizeof(table) / sizeof(table[0]); i++)
    {
        if (strcmp(table[i].name, text) == 0)
        {
            return table[i].value;
        }
    }

    return (potr_type)0;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_config_global_init_defaults(potr_global_config *global)
{
    size_t i;
    size_t count = sizeof(s_global_schema) / sizeof(s_global_schema[0]);

    if (global == NULL)
    {
        return;
    }

    memset(global, 0, sizeof(*global));

    for (i = 0U; i < count; i++)
    {
        const potr_internal_config_field_spec *spec = &s_global_schema[i];
        void *field_ptr = (uint8_t *)global + spec->offset;

        if (spec->kind == POTR_CONFIG_VAL_U16)
        {
            uint16_t v = (uint16_t)spec->default_val;
            memcpy(field_ptr, &v, sizeof(v));
        }
        else if (spec->kind == POTR_CONFIG_VAL_U32)
        {
            uint32_t v = spec->default_val;
            memcpy(field_ptr, &v, sizeof(v));
        }
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_config_service_init_defaults(potr_service_def *service, int64_t service_id)
{
    size_t i;
    size_t count = sizeof(s_service_schema) / sizeof(s_service_schema[0]);
    const char *default_bcast = "255.255.255.255";

    if (service == NULL)
    {
        return;
    }

    memset(service, 0, sizeof(*service));
    service->service_id = service_id;

    for (i = 0U; i < count; i++)
    {
        const potr_internal_config_field_spec *spec = &s_service_schema[i];
        void *field_ptr = (uint8_t *)service + spec->offset;

        if (spec->kind == POTR_CONFIG_VAL_U8)
        {
            uint8_t v = (uint8_t)spec->default_val;
            memcpy(field_ptr, &v, sizeof(v));
        }
        else if (spec->kind == POTR_CONFIG_VAL_U16)
        {
            uint16_t v = (uint16_t)spec->default_val;
            memcpy(field_ptr, &v, sizeof(v));
        }
        else if (spec->kind == POTR_CONFIG_VAL_U32)
        {
            uint32_t v = spec->default_val;
            memcpy(field_ptr, &v, sizeof(v));
        }
        else if (spec->kind == POTR_CONFIG_VAL_AUTH_ENUM)
        {
            potr_auth v = (potr_auth)spec->default_val;
            memcpy(field_ptr, &v, sizeof(v));
        }
    }

    for (i = 0U; i < (size_t)POTR_MAX_PATH; i++)
    {
        service->path_group[i] = 1U;
    }

    memcpy(service->broadcast_addr, default_bcast, strlen(default_bcast) + 1U);
}
