/**
 *******************************************************************************
 *  @file           certificate.c
 *  @brief          証明書方式のハンドシェイクとセッション別の鍵を管理します。
 *
 *******************************************************************************
 */
#include <string.h>
#include <cplat/crt/stdlib.h>
#include <cplat/crypto/random.h>
#include <cplat/runtime/memory_lock.h>
#include <cplat/net/byteorder.h>
#include <porter/potr_context.h>
#include <porter/potr_peer_table.h>
#include <porter/protocol/packet.h>
#include <porter/infra/potr_trace.h>
#include <porter/security/certificate.h>

/** 直近の DTLS flight を構成する出力レコード。状態が所有します。 */
struct potr_internal_certificate_flight
{
    struct potr_internal_certificate_flight *next; /**< 次の出力レコード。 */
    uint8_t *bytes; /**< 所有するバイト列。 */
    size_t length; /**< バイト列の長さ。 */
};

static void dispose_flight(potr_internal_certificate_state *state)
{
    while (state->flight != NULL)
    {
        struct potr_internal_certificate_flight *record = state->flight;
        state->flight = record->next;
        cplat_secure_zero(record->bytes, record->length);
        cplat_free(record->bytes);
        cplat_free(record);
    }
}

static int uses_group_key(const potr_context *ctx)
{
    potr_type type = raw_base_type(ctx->service.type);
    return type == POTR_TYPE_MULTICAST || type == POTR_TYPE_BROADCAST;
}

static int is_client(const potr_context *ctx)
{
    if (is_oneway_udp_type(ctx->service.type))
    {
        return ctx->role == POTR_ROLE_RECEIVER;
    }
    return !ctx->is_multi_peer && ctx->role == POTR_ROLE_SENDER;
}

static uint64_t retry_interval(const potr_context *ctx)
{
    if (ctx->health_interval_ms != 0U)
    {
        return ctx->health_interval_ms;
    }
    /* ヘルス チェック無効時も相互認証と DTLS 再送を継続します。 */
    return POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS;
}

static void identity_bytes(uint8_t out[16], int64_t seconds, uint32_t identifier, int32_t nanoseconds)
{
    uint64_t value = (uint64_t)seconds;
    size_t i;

    for (i = 0U; i < 8U; i++)
    {
        out[i] = (uint8_t)(value >> ((7U - i) * 8U));
    }
    identifier = cplat_hton32(identifier);
    nanoseconds = (int32_t)cplat_hton32((uint32_t)nanoseconds);
    memcpy(out + 8U, &identifier, 4U);
    memcpy(out + 12U, &nanoseconds, 4U);
}

static int matches_identity(const potr_internal_certificate_state *state, const potr_packet *packet)
{
    uint8_t identity[16];

    identity_bytes(identity, packet->session_tv_sec, packet->session_id, packet->session_tv_nsec);
    return state->peer_known && memcmp(identity, state->peer_identity, sizeof(identity)) == 0;
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_certificate_state_dispose(potr_internal_certificate_state *state)
{
    if (state != NULL)
    {
        potr_internal_auth_session_dispose(state->session);
        dispose_flight(state);
        cplat_secure_zero(state, sizeof(*state));
    }
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_certificate_initialize(potr_context *ctx, const potr_service_def *service)
{
    potr_internal_auth_config config = {0};
    potr_internal_auth_transport transport = POTR_INTERNAL_AUTH_TRANSPORT_DATAGRAM;
    int ret;

    if (ctx == NULL || service == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    ctx->service.cert_file = NULL;
    ctx->service.key_file = NULL;
    ctx->service.ca_file = NULL;
    ctx->service.crl_file = NULL;
    ctx->service.pinned_certs = NULL;
    ctx->service.peer_names = NULL;
    ctx->service.pinned_cert_count = 0U;
    ctx->service.peer_name_count = 0U;
    if (service->auth == POTR_AUTH_PSK)
    {
        return POTR_OK;
    }
    if (service->auth != POTR_AUTH_CERT ||
        (ctx->role == POTR_ROLE_SENDER && is_oneway_udp_type(ctx->service.type) && service->src_port == 0U))
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    config.cert_file = service->cert_file;
    config.key_file = service->key_file;
    config.ca_file = service->ca_file;
    config.crl_file = service->crl_file;
    config.pinned_certs = service->pinned_certs;
    config.pinned_cert_count = service->pinned_cert_count;
    config.peer_names = service->peer_names;
    config.peer_name_count = service->peer_name_count;
    if (is_tcp_type(service->type))
    {
        transport = POTR_INTERNAL_AUTH_TRANSPORT_STREAM;
    }
    ret = potr_internal_auth_credentials_create(&config, transport, &ctx->credentials);
    if (ret != POTR_OK)
    {
        return ret;
    }
    if (cplat_local_lock_create(&ctx->certificate_mutex) != CPLAT_OK)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }
    ctx->service.encrypt_enabled = 1;
    cplat_secure_zero(ctx->service.encrypt_key, sizeof(ctx->service.encrypt_key));
    if (ctx->role == POTR_ROLE_SENDER && uses_group_key(ctx))
    {
        if (cplat_random_bytes(ctx->group_key, sizeof(ctx->group_key)) != CPLAT_OK)
        {
            return POTR_ERR_IO;
        }
        ctx->group_key_known = 1;
    }
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_certificate_dispose(potr_context *ctx)
{
    if (ctx == NULL)
    {
        return;
    }
    potr_internal_certificate_state_dispose(&ctx->certificate);
    potr_internal_auth_credentials_dispose(ctx->credentials);
    ctx->credentials = NULL;
    cplat_secure_zero(ctx->group_key, sizeof(ctx->group_key));
    ctx->group_key_known = 0;
    if (ctx->certificate_mutex != NULL)
    {
        cplat_local_lock_dispose(ctx->certificate_mutex);
        ctx->certificate_mutex = NULL;
    }
}

static int start_session(potr_context *ctx, potr_internal_certificate_state *state, int client, uint64_t now_ms)
{
    potr_internal_auth_role role = POTR_INTERNAL_AUTH_ROLE_SERVER;
    int ret;

    state->retry_ms = now_ms;
    if (client)
    {
        role = POTR_INTERNAL_AUTH_ROLE_CLIENT;
    }
    state->local_identifier = ctx->session_id;
    state->local_timestamp = ctx->session_ts;
    if (ctx->is_multi_peer)
    {
        if (cplat_random_bytes(&state->local_identifier, sizeof(state->local_identifier)) != CPLAT_OK ||
            cplat_clock_get_realtime(&state->local_timestamp) != CPLAT_OK)
        {
            state->failed = 1;
            return POTR_ERR_IO;
        }
        potr_internal_tx_session_init(&state->transmit);
    }
    state->client = client;
    ret = potr_internal_auth_session_create(ctx->credentials, role, ctx->global.max_payload, &state->session);
    if (ret != POTR_OK)
    {
        state->failed = 1;
        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING, "certificate: session creation failed result=%d", ret);
    }
    return ret;
}

static int advance_session(potr_internal_certificate_state *state,
    const uint8_t *input, size_t length)
{
    potr_internal_auth_state result;
    uint8_t context[32];
    uint8_t client_key[POTR_CRYPTO_KEY_SIZE];
    uint8_t server_key[POTR_CRYPTO_KEY_SIZE];
    uint8_t own_identity[16];
    int ret;

    ret = potr_internal_auth_session_receive(state->session, input, length, &result);
    if (ret != POTR_OK || result == POTR_INTERNAL_AUTH_STATE_FAILED)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING, "certificate: authentication failed: %s",
            potr_internal_auth_session_failure_reason(state->session));
        state->failed = 1;
        state->established = 0;
        potr_internal_auth_session_dispose(state->session);
        state->session = NULL;
        dispose_flight(state);
        cplat_secure_zero(state->send_key, sizeof(state->send_key));
        cplat_secure_zero(state->receive_key, sizeof(state->receive_key));
        return POTR_ERR_PROTOCOL;
    }
    if (result != POTR_INTERNAL_AUTH_STATE_ESTABLISHED || state->established || !state->peer_known)
    {
        return POTR_OK;
    }
    identity_bytes(own_identity, state->local_timestamp.tv_sec, state->local_identifier, (int32_t)state->local_timestamp.tv_nsec);
    if (state->client)
    {
        memcpy(context, own_identity, 16U);
        memcpy(context + 16U, state->peer_identity, 16U);
    }
    else
    {
        memcpy(context, state->peer_identity, 16U);
        memcpy(context + 16U, own_identity, 16U);
    }
    ret = potr_internal_auth_session_export_keys(state->session, context, sizeof(context), client_key, server_key);
    if (ret == POTR_OK)
    {
        if (state->client)
        {
            memcpy(state->send_key, client_key, sizeof(client_key));
            memcpy(state->receive_key, server_key, sizeof(server_key));
        }
        else
        {
            memcpy(state->send_key, server_key, sizeof(server_key));
            memcpy(state->receive_key, client_key, sizeof(client_key));
        }
        state->established = 1;
    }
    else
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING, "certificate: key derivation failed result=%d", ret);
        state->failed = 1;
        potr_internal_auth_session_dispose(state->session);
        state->session = NULL;
        dispose_flight(state);
    }
    cplat_secure_zero(client_key, sizeof(client_key));
    cplat_secure_zero(server_key, sizeof(server_key));
    return ret;
}

static int send_record(potr_context *ctx, potr_internal_certificate_state *state, const uint8_t *record,
    size_t length, uint16_t flags, cplat_socket stream)
{
    potr_internal_packet_session_hdr header;
    potr_packet packet;
    uint8_t *wire;
    const uint8_t *key = NULL;
    size_t capacity = POTR_PACKET_HEADER_SIZE + ctx->global.max_payload;
    int ret;
    int i;
    potr_internal_tx_session *transmit = &ctx->tx_session;

    if (ctx->is_multi_peer)
    {
        transmit = &state->transmit;
        for (i = 0; i < ctx->max_peers; i++)
        {
            if (ctx->peers[i].active && state == &ctx->peers[i].certificate)
            {
                transmit = &ctx->peers[i].tx_session;
                break;
            }
        }
    }
    header.service_id = ctx->service.service_id;
    header.session_id = state->local_identifier;
    session_ts_to_hdr(&state->local_timestamp, &header.session_tv_sec, &header.session_tv_nsec);
    ret = potr_internal_packet_build_ping(&packet, &header, 0U, record, (uint16_t)length);
    if (ret != POTR_OK)
    {
        return ret;
    }
    packet.flags = cplat_hton16(flags);
    if (flags == POTR_FLAG_KEY)
    {
        key = state->send_key;
    }
    wire = (uint8_t *)cplat_malloc(capacity);
    if (wire == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }
    for (i = 0; i < ctx->n_path; i++)
    {
        size_t wire_len = capacity;
        size_t sent;

        if (stream == CPLAT_INVALID_SOCKET &&
            (endpoint_is_unset(&state->destination[i]) || ctx->sock[i] == CPLAT_INVALID_SOCKET))
        {
            continue;
        }
        ret = potr_internal_packet_seal(transmit, &packet, key, wire, &wire_len);
        if (ret != POTR_OK)
        {
            if (ret == POTR_ERR_DISCONNECTED)
            {
                state->failed = 1;
                state->established = 0;
                if (ctx->is_multi_peer)
                {
                    cplat_atomic_store_i32(&transmit->exhausted, 1, CPLAT_MEMORY_ORDER_RELEASE);
                }
                else
                {
                    cplat_atomic_store_i32(&ctx->fatal_error, ret, CPLAT_MEMORY_ORDER_RELEASE);
                    potr_internal_send_queue_abort(&ctx->send_queue, ret);
                }
            }
            break;
        }
        if (stream != CPLAT_INVALID_SOCKET)
        {
            if (cplat_socket_send_all(stream, wire, wire_len, NULL) != CPLAT_OK)
            {
                ret = POTR_ERR_IO;
            }
            break;
        }
        (void)cplat_socket_sendto(ctx->sock[i], wire, wire_len, &state->destination[i], &sent, NULL);
    }
    cplat_secure_zero(wire, capacity);
    cplat_free(wire);
    return ret;
}

static int flush_output(potr_context *ctx, potr_internal_certificate_state *state, cplat_socket stream, int *produced)
{
    uint8_t *record;
    size_t length;
    int ret;
    int first_output = 1;
    struct potr_internal_certificate_flight **tail = &state->flight;

    if (produced != NULL)
    {
        *produced = 0;
    }
    if (state->session == NULL)
    {
        return POTR_ERR_DISCONNECTED;
    }
    record = (uint8_t *)cplat_malloc(ctx->global.max_payload);
    if (record == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }
    for (;;)
    {
        ret = potr_internal_auth_session_take_output(state->session, record, ctx->global.max_payload, &length);
        if (ret != POTR_OK || length == 0U)
        {
            break;
        }
        if (stream == CPLAT_INVALID_SOCKET)
        {
            struct potr_internal_certificate_flight *cached;
            if (first_output)
            {
                dispose_flight(state);
                first_output = 0;
                tail = &state->flight;
            }
            cached = (struct potr_internal_certificate_flight *)cplat_calloc(1U, sizeof(*cached));
            if (cached == NULL)
            {
                ret = POTR_ERR_OUT_OF_MEMORY;
                break;
            }
            cached->bytes = (uint8_t *)cplat_malloc(length);
            if (cached->bytes == NULL)
            {
                cplat_free(cached);
                ret = POTR_ERR_OUT_OF_MEMORY;
                break;
            }
            memcpy(cached->bytes, record, length);
            cached->length = length;
            *tail = cached;
            tail = &cached->next;
        }
        if (produced != NULL)
        {
            *produced = 1;
        }
        ret = send_record(ctx, state, record, length, POTR_FLAG_HANDSHAKE, stream);
        if (ret != POTR_OK)
        {
            break;
        }
    }
    cplat_secure_zero(record, ctx->global.max_payload);
    cplat_free(record);
    return ret;
}

/* callback_mutex と certificate_mutex を保持して受信者管理表を共用します。 */
static potr_internal_receiver *find_receiver(potr_context *ctx, const potr_packet *packet, int create, uint64_t now_ms)
{
    potr_internal_receiver *receiver;
    potr_internal_receiver *oldest = NULL;
    size_t count = 0U;
    size_t limit = ctx->service.max_peers;

    if (limit == 0U)
    {
        limit = 1024U;
    }
    for (receiver = ctx->receivers; receiver != NULL; receiver = receiver->next)
    {
        if (receiver->session_id == packet->session_id && receiver->session_tv_sec == packet->session_tv_sec &&
            receiver->session_tv_nsec == packet->session_tv_nsec)
        {
            return receiver;
        }
        if (!receiver->active && now_ms - receiver->certificate.retry_ms >= retry_interval(ctx) &&
            (oldest == NULL || receiver->retired_ms < oldest->retired_ms))
        {
            oldest = receiver;
        }
        ++count;
    }
    if (!create)
    {
        return NULL;
    }
    if (count < limit)
    {
        receiver = (potr_internal_receiver *)cplat_calloc(1U, sizeof(*receiver));
        if (receiver == NULL)
        {
            return NULL;
        }
        receiver->next = ctx->receivers;
        ctx->receivers = receiver;
    }
    else if (oldest != NULL)
    {
        receiver = oldest;
        potr_internal_certificate_state_dispose(&receiver->certificate);
        memset(&receiver->replay_window, 0, sizeof(receiver->replay_window));
        memset(receiver->path_state, 0, sizeof(receiver->path_state));
    }
    else
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING, "certificate: max_peers reached, dropping receiver");
        return NULL;
    }
    receiver->session_id = packet->session_id;
    receiver->session_tv_sec = packet->session_tv_sec;
    receiver->session_tv_nsec = packet->session_tv_nsec;
    receiver->retired_ms = now_ms;
    return receiver;
}

static potr_internal_peer_context *find_peer(potr_context *ctx, const potr_packet *packet)
{
    cplat_timespec timestamp;

    session_ts_from_hdr(packet->session_tv_sec, packet->session_tv_nsec, &timestamp);
    return potr_internal_peer_find_by_session(ctx, packet->session_id, &timestamp);
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_certificate_receive(potr_context *ctx, potr_packet *packet, const uint8_t *wire_header,
    const cplat_ipv4_endpoint *sender, int path_idx)
{
    potr_internal_certificate_state *state;
    potr_internal_receiver *receiver = NULL;
    potr_internal_peer_context *peer = NULL;
    uint64_t now_ms;
    int managed;
    int ret;
    int new_path;
    int produced;

    if (ctx == NULL || packet == NULL || wire_header == NULL || sender == NULL ||
        path_idx < 0 || path_idx >= ctx->n_path)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    state = &ctx->certificate;
    managed = ctx->is_multi_peer || (is_oneway_udp_type(ctx->service.type) && ctx->role == POTR_ROLE_SENDER);
    if ((packet->flags & (POTR_FLAG_HANDSHAKE | POTR_FLAG_KEY)) == 0U)
    {
        return POTR_ERR_NOT_FOUND;
    }
    if (ctx->service.auth != POTR_AUTH_CERT || packet->service_id != ctx->service.service_id ||
        (packet->flags != POTR_FLAG_HANDSHAKE && packet->flags != (POTR_FLAG_KEY | POTR_FLAG_ENCRYPTED)))
    {
        return POTR_OK;
    }
    if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
    {
        return POTR_OK;
    }
    if (ctx->is_multi_peer)
    {
        cplat_local_lock_lock(ctx->peers_mutex, CPLAT_SYNC_WAIT_FOREVER);
    }
    cplat_local_lock_lock(ctx->certificate_mutex, CPLAT_SYNC_WAIT_FOREVER);
    cplat_local_lock_lock(ctx->callback_mutex, CPLAT_SYNC_WAIT_FOREVER);
    if (ctx->is_multi_peer)
    {
        peer = find_peer(ctx, packet);
        if (peer != NULL)
        {
            state = &peer->certificate;
        }
    }
    if (managed && peer == NULL)
    {
        receiver = find_receiver(ctx, packet, packet->flags == POTR_FLAG_HANDSHAKE, now_ms);
        if (receiver == NULL)
        {
            goto out_unlock;
        }
        state = &receiver->certificate;
    }
    if (packet->flags & POTR_FLAG_KEY)
    {
        uint8_t key[POTR_CRYPTO_KEY_SIZE];

        if (ctx->role == POTR_ROLE_RECEIVER && uses_group_key(ctx) && state->established &&
            packet->seq_num == 0U && packet->ack_num == 0U && matches_identity(state, packet) &&
            potr_internal_packet_open(packet, wire_header, state->receive_key, key, sizeof(key)) == POTR_OK &&
            packet->payload_len == POTR_CRYPTO_KEY_SIZE &&
            potr_internal_replay_window_accept(&ctx->replay_window, packet) == POTR_OK)
        {
            memcpy(ctx->group_key, key, sizeof(key));
            ctx->group_key_known = 1;
        }
        cplat_secure_zero(key, sizeof(key));
        goto out_unlock;
    }
    if (packet->payload_len == 0U || packet->payload == NULL || packet->seq_num != 0U || packet->ack_num != 0U)
    {
        goto out_unlock;
    }
    /* DTLS の epoch 0 の ClientHello は、KEY 未受信の受信者による再試行も表します。 */
    if (!is_client(ctx) && state->session != NULL && packet->payload_len > 13U &&
        packet->payload[0] == 22U && packet->payload[3] == 0U && packet->payload[4] == 0U &&
        packet->payload[13] == 1U && now_ms - state->retry_ms >= retry_interval(ctx))
    {
        potr_internal_certificate_state_dispose(state);
    }
    if (state->failed)
    {
        if (now_ms - state->retry_ms < retry_interval(ctx))
        {
            goto out_unlock;
        }
        potr_internal_certificate_state_dispose(state);
    }
    if (state->session == NULL && start_session(ctx, state, is_client(ctx), now_ms) != POTR_OK)
    {
        goto out_unlock;
    }
    if (peer != NULL && !state->peer_known)
    {
        state->local_identifier = peer->session_id;
        state->local_timestamp = peer->session_ts;
    }
    if (state->peer_known && !matches_identity(state, packet))
    {
        /* クライアントは周期的な再試行で相手セッションの変更を採用します。 */
        goto out_unlock;
    }
    identity_bytes(state->peer_identity, packet->session_tv_sec, packet->session_id, packet->session_tv_nsec);
    state->peer_known = 1;
    new_path = endpoint_is_unset(&state->destination[path_idx]);
    state->destination[path_idx] = *sender;
    ret = advance_session(state, packet->payload, packet->payload_len);
    (void)flush_output(ctx, state, CPLAT_INVALID_SOCKET, &produced);
    if (ret == POTR_OK && new_path && !produced)
    {
        struct potr_internal_certificate_flight *record;
        /* 遅れて観測した経路にも同じバイト列を送り、DTLS に重複処理を任せます。 */
        for (record = state->flight; record != NULL; record = record->next)
        {
            (void)send_record(ctx, state, record->bytes, record->length, POTR_FLAG_HANDSHAKE, CPLAT_INVALID_SOCKET);
        }
    }
    if (ret != POTR_OK || !state->established)
    {
        goto out_unlock;
    }
    if (ctx->role == POTR_ROLE_SENDER && uses_group_key(ctx))
    {
        (void)send_record(ctx, state, ctx->group_key, sizeof(ctx->group_key), POTR_FLAG_KEY, CPLAT_INVALID_SOCKET);
    }
    if (ctx->is_multi_peer && peer != NULL)
    {
        peer->session_id = state->local_identifier;
        peer->session_ts = state->local_timestamp;
    }
    if (ctx->is_multi_peer && peer == NULL)
    {
        /* ピアの通信状態は相互認証が完了してから生成します。 */
        cplat_local_lock_unlock(ctx->callback_mutex);
        peer = potr_internal_peer_create(ctx, sender, path_idx);
        cplat_local_lock_lock(ctx->callback_mutex, CPLAT_SYNC_WAIT_FOREVER);
        if (peer != NULL)
        {
            peer->session_id = state->local_identifier;
            peer->session_ts = state->local_timestamp;
            peer->tx_session = state->transmit;
            peer->peer_session_id = packet->session_id;
            session_ts_from_hdr(packet->session_tv_sec, packet->session_tv_nsec, &peer->peer_session_ts);
            peer->peer_session_known = 1;
            peer->certificate = *state;
            memset(state, 0, sizeof(*state));
        }
    }
out_unlock:
    cplat_local_lock_unlock(ctx->callback_mutex);
    cplat_local_lock_unlock(ctx->certificate_mutex);
    if (ctx->is_multi_peer)
    {
        cplat_local_lock_unlock(ctx->peers_mutex);
    }
    return POTR_OK;
}

static void tick_session(potr_context *ctx, potr_internal_certificate_state *state)
{
    potr_internal_auth_state result;
    uint32_t timeout_ms;
    int has_timeout;

    if (state->session == NULL || state->failed)
    {
        return;
    }
    if (potr_internal_auth_session_get_timeout_ms(state->session, &has_timeout, &timeout_ms) == POTR_OK &&
        has_timeout && timeout_ms == 0U)
    {
        state->retransmitted = 1;
        (void)potr_internal_auth_session_handle_timeout(state->session, &result);
        if (result == POTR_INTERNAL_AUTH_STATE_FAILED)
        {
            state->failed = 1;
            state->established = 0;
            POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING, "certificate: authentication failed: %s",
                potr_internal_auth_session_failure_reason(state->session));
            potr_internal_auth_session_dispose(state->session);
            state->session = NULL;
            dispose_flight(state);
            cplat_secure_zero(state->send_key, sizeof(state->send_key));
            cplat_secure_zero(state->receive_key, sizeof(state->receive_key));
        }
        (void)flush_output(ctx, state, CPLAT_INVALID_SOCKET, NULL);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_certificate_tick(potr_context *ctx, uint64_t now_ms)
{
    potr_internal_certificate_state *state;
    potr_internal_receiver *receiver;
    int i;

    if (ctx == NULL || ctx->service.auth != POTR_AUTH_CERT || ctx->certificate_mutex == NULL || is_tcp_type(ctx->service.type))
    {
        return;
    }
    state = &ctx->certificate;
    if (ctx->is_multi_peer)
    {
        cplat_local_lock_lock(ctx->peers_mutex, CPLAT_SYNC_WAIT_FOREVER);
    }
    cplat_local_lock_lock(ctx->certificate_mutex, CPLAT_SYNC_WAIT_FOREVER);
    cplat_local_lock_lock(ctx->callback_mutex, CPLAT_SYNC_WAIT_FOREVER);
    if (is_client(ctx))
    {
        tick_session(ctx, state);
        if (((!state->established && (state->session == NULL || state->failed || state->retransmitted)) ||
             (uses_group_key(ctx) && state->established && !ctx->group_key_known) ||
             (ctx->health_timeout_ms != 0U && now_ms - state->retry_ms >= ctx->health_timeout_ms &&
              cplat_atomic_load_i32(&ctx->health_alive, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)) &&
            ((state->session == NULL && !state->failed) || now_ms - state->retry_ms >= retry_interval(ctx)))
        {
            potr_internal_certificate_state_dispose(state);
            if (uses_group_key(ctx))
            {
                cplat_secure_zero(ctx->group_key, sizeof(ctx->group_key));
                ctx->group_key_known = 0;
            }
            if (start_session(ctx, state, 1, now_ms) == POTR_OK)
            {
                for (i = 0; i < ctx->n_path; i++)
                {
                    if (is_oneway_udp_type(ctx->service.type))
                    {
                        state->destination[i].address = ctx->src_addr_resolved[i];
                        state->destination[i].port = cplat_hton16(ctx->service.src_port);
                        if (state->destination[i].address == 0U)
                        {
                            state->destination[i].address = ctx->peer_address[i];
                        }
                        if (state->destination[i].port == 0U)
                        {
                            state->destination[i].port = ctx->peer_port[i];
                        }
                    }
                    else
                    {
                        state->destination[i] = ctx->dest_addr[i];
                    }
                }
                (void)advance_session(state, NULL, 0U);
                (void)flush_output(ctx, state, CPLAT_INVALID_SOCKET, NULL);
            }
        }
    }
    else
    {
        tick_session(ctx, state);
    }
    for (receiver = ctx->receivers; receiver != NULL; receiver = receiver->next)
    {
        tick_session(ctx, &receiver->certificate);
    }
    if (ctx->is_multi_peer)
    {
        for (i = 0; i < ctx->max_peers; i++)
        {
            if (ctx->peers[i].active)
            {
                tick_session(ctx, &ctx->peers[i].certificate);
            }
        }
    }
    cplat_local_lock_unlock(ctx->callback_mutex);
    cplat_local_lock_unlock(ctx->certificate_mutex);
    if (ctx->is_multi_peer)
    {
        for (i = 0; i < ctx->max_peers; i++)
        {
            if (ctx->peers[i].active && ctx->health_timeout_ms != 0U &&
                cplat_atomic_load_i32(&ctx->peers[i].health_alive, CPLAT_MEMORY_ORDER_ACQUIRE) == 0 &&
                now_ms - ctx->peers[i].certificate.retry_ms >= ctx->health_timeout_ms)
            {
                /* 相互認証の後に通常パケットが届かないピアも上限から外します。 */
                potr_internal_peer_free(ctx, &ctx->peers[i]);
            }
        }
        cplat_local_lock_unlock(ctx->peers_mutex);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_certificate_key(potr_context *ctx, const potr_internal_tx_session *session,
    const potr_packet *packet, int sending, uint8_t key[POTR_CRYPTO_KEY_SIZE])
{
    potr_internal_certificate_state *state;
    potr_internal_receiver *receiver;
    uint16_t flags;
    int ret = POTR_ERR_DISCONNECTED;
    int i;

    if (ctx == NULL || packet == NULL || key == NULL || (sending && session == NULL) ||
        ctx->service.auth != POTR_AUTH_CERT || ctx->certificate_mutex == NULL || ctx->callback_mutex == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    state = &ctx->certificate;
    flags = packet->flags;
    if (sending)
    {
        flags = cplat_ntoh16(flags);
    }
    cplat_local_lock_lock(ctx->certificate_mutex, CPLAT_SYNC_WAIT_FOREVER);
    cplat_local_lock_lock(ctx->callback_mutex, CPLAT_SYNC_WAIT_FOREVER);
    if (ctx->is_multi_peer)
    {
        state = NULL;
        for (i = 0; i < ctx->max_peers; i++)
        {
            if (ctx->peers[i].active && ((sending && session == &ctx->peers[i].tx_session) ||
                (!sending && matches_identity(&ctx->peers[i].certificate, packet))))
            {
                state = &ctx->peers[i].certificate;
                break;
            }
        }
    }
    else if (ctx->role == POTR_ROLE_SENDER && is_oneway_udp_type(ctx->service.type))
    {
        state = NULL;
        for (receiver = ctx->receivers; receiver != NULL; receiver = receiver->next)
        {
            if (receiver->certificate.established && (sending || matches_identity(&receiver->certificate, packet)))
            {
                state = &receiver->certificate;
                break;
            }
        }
    }
    if (uses_group_key(ctx) && (flags & (POTR_FLAG_DATA | POTR_FLAG_PING | POTR_FLAG_REJECT | POTR_FLAG_FIN)) != 0U &&
        ((sending && ctx->role == POTR_ROLE_SENDER) || (!sending && ctx->role == POTR_ROLE_RECEIVER)))
    {
        if (ctx->group_key_known && state != NULL && state->established && (sending || matches_identity(state, packet)))
        {
            memcpy(key, ctx->group_key, POTR_CRYPTO_KEY_SIZE);
            ret = POTR_OK;
        }
    }
    else if (state != NULL && state->established && (sending || matches_identity(state, packet)))
    {
        if (sending)
        {
            memcpy(key, state->send_key, POTR_CRYPTO_KEY_SIZE);
        }
        else
        {
            memcpy(key, state->receive_key, POTR_CRYPTO_KEY_SIZE);
        }
        ret = POTR_OK;
    }
    cplat_local_lock_unlock(ctx->callback_mutex);
    cplat_local_lock_unlock(ctx->certificate_mutex);
    return ret;
}

static int read_stream_bytes(potr_context *ctx, cplat_socket socket, uint8_t *bytes, size_t length, uint64_t deadline)
{
    size_t used = 0U;

    while (used < length)
    {
        uint64_t now_ms;
        int ready;
        size_t received;

        if (cplat_atomic_load_i32(&ctx->close_requested, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
        {
            return POTR_ERR_CANCELED;
        }
        if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
        {
            return POTR_ERR_IO;
        }
        if (now_ms >= deadline)
        {
            return POTR_ERR_TIMEOUT;
        }
        if (cplat_socket_wait_readable(socket, 100, &ready, NULL) != CPLAT_OK)
        {
            return POTR_ERR_IO;
        }
        if (!ready)
        {
            continue;
        }
        if (cplat_socket_recv(socket, bytes + used, length - used, &received, NULL) != CPLAT_OK || received == 0U)
        {
            return POTR_ERR_IO;
        }
        used += received;
    }
    return POTR_OK;
}

static int read_stream_record(potr_context *ctx, cplat_socket socket, uint8_t *wire, potr_packet *packet)
{
    uint16_t length;
    uint64_t now_ms;
    uint64_t deadline;
    uint32_t timeout = ctx->health_timeout_ms;
    int ret;

    if (timeout == 0U)
    {
        timeout = POTR_DEFAULT_TCP_HEALTH_TIMEOUT_MS;
    }
    if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
    {
        return POTR_ERR_IO;
    }
    deadline = now_ms + timeout;
    ret = read_stream_bytes(ctx, socket, wire, POTR_PACKET_HEADER_SIZE, deadline);
    if (ret != POTR_OK)
    {
        return ret;
    }
    memcpy(&length, wire + POTR_PACKET_PAYLOAD_LEN_OFFSET, sizeof(length));
    length = cplat_ntoh16(length);
    if (length > ctx->global.max_payload)
    {
        return POTR_ERR_PROTOCOL;
    }
    ret = read_stream_bytes(ctx, socket, wire + POTR_PACKET_HEADER_SIZE, length, deadline);
    if (ret != POTR_OK)
    {
        return ret;
    }
    return potr_internal_packet_parse(packet, wire, POTR_PACKET_HEADER_SIZE + length);
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_certificate_connect(potr_context *ctx, cplat_socket socket, const potr_packet *first)
{
    potr_internal_certificate_state *state;
    potr_packet packet;
    const potr_packet *input = first;
    uint8_t *wire;
    uint64_t now_ms;
    int ret = POTR_OK;

    if (ctx == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    state = &ctx->certificate;
    if (ctx->service.auth != POTR_AUTH_CERT)
    {
        return POTR_OK;
    }
    wire = (uint8_t *)cplat_malloc(POTR_PACKET_HEADER_SIZE + ctx->global.max_payload);
    if (wire == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }
    cplat_local_lock_lock(ctx->certificate_mutex, CPLAT_SYNC_WAIT_FOREVER);
    if (state->established)
    {
        if (first == NULL)
        {
            goto out;
        }
        if (cplat_atomic_load_i32(&ctx->tcp_active_paths, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
        {
            ret = POTR_ERR_PROTOCOL;
            goto out;
        }
        /* 接続を公開する前に切断した相手も、新しい ClientHello から再認証します。 */
    }
    potr_internal_certificate_state_dispose(state);
    if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
    {
        ret = POTR_ERR_IO;
        goto out;
    }
    ret = start_session(ctx, state, is_client(ctx), now_ms);
    if (ret != POTR_OK)
    {
        goto out;
    }
    if (state->client)
    {
        ret = advance_session(state, NULL, 0U);
        if (ret == POTR_OK)
        {
            ret = flush_output(ctx, state, socket, NULL);
        }
    }
    while (ret == POTR_OK && !state->established)
    {
        if (input == NULL)
        {
            ret = read_stream_record(ctx, socket, wire, &packet);
            if (ret != POTR_OK)
            {
                break;
            }
            input = &packet;
        }
        if (input->service_id != ctx->service.service_id || input->flags != POTR_FLAG_HANDSHAKE ||
            input->payload_len == 0U || input->seq_num != 0U || input->ack_num != 0U ||
            (state->peer_known && !matches_identity(state, input)))
        {
            ret = POTR_ERR_PROTOCOL;
            break;
        }
        identity_bytes(state->peer_identity, input->session_tv_sec, input->session_id, input->session_tv_nsec);
        state->peer_known = 1;
        ret = advance_session(state, input->payload, input->payload_len);
        if (ret == POTR_OK)
        {
            ret = flush_output(ctx, state, socket, NULL);
        }
        input = NULL;
    }
    if (ret != POTR_OK)
    {
        potr_internal_certificate_state_dispose(state);
    }
out:
    cplat_local_lock_unlock(ctx->certificate_mutex);
    cplat_secure_zero(wire, POTR_PACKET_HEADER_SIZE + ctx->global.max_payload);
    cplat_free(wire);
    return ret;
}
