/**
 *******************************************************************************
 *  @file           packet_seal.c
 *  @brief          パケットの封緘、開封とリプレイ検査を行います。
 *
 *******************************************************************************
 */
#include <cplat/base/platform.h>
#include <string.h>
#include <cplat/clock/clock.h>
#include <cplat/crypto/crypto.h>
#include <cplat/runtime/memory_lock.h>
#include <cplat/net/byteorder.h>
#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include <porter/potr_context.h>
#include <porter/infra/potr_trace.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/packet_seal.h>

static uint64_t to_network_u64(uint64_t value)
{
    return ((uint64_t)cplat_hton32((uint32_t)value) << 32) | cplat_hton32((uint32_t)(value >> 32));
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_tx_session_init(potr_internal_tx_session *session)
{
    if (session == NULL)
    {
        return;
    }
    cplat_atomic_store_u64(&session->next_counter, 0U, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&session->exhausted, 0, CPLAT_MEMORY_ORDER_RELAXED);
    session->start_mono_ns = 0U;
    session->start_mono_valid =
        (cplat_clock_get_monotonic_high_resolution_ns(&session->start_mono_ns) == CPLAT_OK);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_packet_seal(potr_internal_tx_session *session, const potr_packet *packet,
                              const uint8_t *key, uint8_t *wire, size_t *wire_len)
{
    potr_packet header;
    cplat_timespec now;
    uint64_t mono_ns;
    uint64_t counter;
    uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
    size_t payload_len;
    size_t output_len;

    if (session == NULL || packet == NULL || wire == NULL || wire_len == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    payload_len = cplat_ntoh16(packet->payload_len);
    output_len = payload_len;
    if (key != NULL)
    {
        output_len += POTR_CRYPTO_TAG_SIZE;
    }
    if ((payload_len != 0U && packet->payload == NULL) || output_len > POTR_MAX_PAYLOAD ||
        *wire_len < POTR_PACKET_HEADER_SIZE + output_len)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    counter = cplat_atomic_load_u64(&session->next_counter, CPLAT_MEMORY_ORDER_RELAXED);
    for (;;)
    {
        if (counter == UINT64_MAX)
        {
            return POTR_ERR_DISCONNECTED;
        }
        if (cplat_atomic_compare_exchange_u64(&session->next_counter, &counter, counter + 1U,
                                              CPLAT_MEMORY_ORDER_RELAXED))
        {
            break;
        }
    }

    header = *packet;
    header.tx_counter = to_network_u64(counter);
    header.tx_tv_sec = 0;
    header.tx_tv_nsec = (int32_t)cplat_hton32(UINT32_MAX);
    header.tx_mono_ns = UINT64_MAX;
    header.reserved = 0U;
    if (cplat_clock_get_realtime(&now) == CPLAT_OK)
    {
        header.tx_tv_sec = (int64_t)to_network_u64((uint64_t)now.tv_sec);
        header.tx_tv_nsec = (int32_t)cplat_hton32((uint32_t)now.tv_nsec);
    }
    if (cplat_clock_get_monotonic_high_resolution_ns(&mono_ns) == CPLAT_OK &&
        session->start_mono_valid && mono_ns >= session->start_mono_ns)
    {
        header.tx_mono_ns = to_network_u64(mono_ns - session->start_mono_ns);
    }
    header.flags &= (uint16_t)~cplat_hton16(POTR_FLAG_ENCRYPTED);
    if (key != NULL)
    {
        header.flags |= cplat_hton16(POTR_FLAG_ENCRYPTED);
    }
    header.payload_len = cplat_hton16((uint16_t)output_len);
    memcpy(wire, &header, POTR_PACKET_HEADER_SIZE);

    if (key != NULL)
    {
        memcpy(nonce, &header.session_id, 4U);
        memcpy(nonce + 4U, &header.tx_counter, 8U);
        if (cplat_crypto_encrypt(wire + POTR_PACKET_HEADER_SIZE, &output_len, packet->payload, payload_len,
                                  key, nonce, wire, POTR_PACKET_HEADER_SIZE) != CPLAT_OK)
        {
            return POTR_ERR_UNKNOWN;
        }
    }
    else if (payload_len != 0U)
    {
        memcpy(wire + POTR_PACKET_HEADER_SIZE, packet->payload, payload_len);
    }
    *wire_len = POTR_PACKET_HEADER_SIZE + output_len;
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_packet_seal_for_context(potr_context *ctx, potr_internal_tx_session *session,
                                          const potr_packet *packet, uint8_t *wire, size_t *wire_len)
{
    const uint8_t *key = NULL;
    uint8_t derived_key[POTR_CRYPTO_KEY_SIZE] = {0};
    int ret;

    if (ctx == NULL || session == NULL || packet == NULL || wire == NULL || wire_len == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (ctx->service.auth == POTR_AUTH_CERT)
    {
        if (cplat_ntoh16(packet->flags) != POTR_FLAG_HANDSHAKE)
        {
            ret = potr_internal_certificate_key(ctx, session, packet, 1, derived_key);
            if (ret != POTR_OK)
            {
                cplat_secure_zero(derived_key, sizeof(derived_key));
                return ret;
            }
            key = derived_key;
        }
    }
    else if (ctx->service.encrypt_enabled)
    {
        key = ctx->service.encrypt_key;
    }
    ret = potr_internal_packet_seal(session, packet, key, wire, wire_len);
    cplat_secure_zero(derived_key, sizeof(derived_key));
    if (ret == POTR_ERR_DISCONNECTED)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "packet seal: transmit counter exhausted, closing session");
        if (ctx->is_multi_peer && session != &ctx->tx_session)
        {
            cplat_atomic_store_i32(&session->exhausted, 1, CPLAT_MEMORY_ORDER_RELEASE);
        }
        else
        {
            cplat_atomic_store_i32(&ctx->fatal_error, ret, CPLAT_MEMORY_ORDER_RELEASE);
            potr_internal_send_queue_abort(&ctx->send_queue, ret);
        }
    }
    return ret;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_packet_open(potr_packet *packet, const uint8_t *wire_header, const uint8_t *key,
                              uint8_t *plaintext, size_t capacity)
{
    uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
    size_t plaintext_len = capacity;
    int ret;

    if (packet == NULL || wire_header == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    if (!(packet->flags & POTR_FLAG_ENCRYPTED))
    {
        if (key != NULL)
        {
            return POTR_ERR_PROTOCOL;
        }
        return POTR_OK;
    }
    if (packet->payload_len < POTR_CRYPTO_TAG_SIZE ||
        (!(packet->flags & (POTR_FLAG_DATA | POTR_FLAG_PING | POTR_FLAG_REPORT | POTR_FLAG_KEY)) && packet->payload_len != POTR_CRYPTO_TAG_SIZE) ||
        key == NULL)
    {
        return POTR_ERR_PROTOCOL;
    }
    if (plaintext == NULL || packet->payload == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    memcpy(nonce, wire_header + 16U, 4U);
    memcpy(nonce + 4U, wire_header + 40U, 8U);
    ret = cplat_crypto_decrypt(plaintext, &plaintext_len, packet->payload, packet->payload_len,
                                key, nonce, wire_header, POTR_PACKET_HEADER_SIZE);
    if (ret != CPLAT_OK)
    {
        if (ret == CPLAT_ERR_AUTHENTICATION_FAILED)
        {
            return POTR_ERR_PROTOCOL;
        }
        if (ret == CPLAT_ERR_OUT_OF_MEMORY)
        {
            return POTR_ERR_OUT_OF_MEMORY;
        }
        if (ret == CPLAT_ERR_INVALID_ARGUMENT || ret == CPLAT_ERR_BUFFER_TOO_SMALL || ret == CPLAT_ERR_OUT_OF_RANGE)
        {
            return POTR_ERR_INVALID_ARGUMENT;
        }
        return POTR_ERR_UNKNOWN;
    }
    packet->payload = NULL;
    if (plaintext_len != 0U)
    {
        packet->payload = plaintext;
    }
    packet->payload_len = (uint16_t)plaintext_len;
    packet->flags &= (uint16_t)~POTR_FLAG_ENCRYPTED;
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_replay_window_prepare(potr_internal_replay_window *window, const potr_packet *packet)
{
    if (window == NULL || packet == NULL)
    {
        return;
    }
    if (window->session_id != packet->session_id || window->session_tv_sec != packet->session_tv_sec ||
        window->session_tv_nsec != packet->session_tv_nsec)
    {
        memset(window, 0, sizeof(*window));
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_replay_window_accept(potr_internal_replay_window *window, const potr_packet *packet)
{
    uint64_t distance;
    size_t word;
    uint64_t bit;
    int same_session;

    if (window == NULL || packet == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    same_session = window->known && window->session_id == packet->session_id &&
        window->session_tv_sec == packet->session_tv_sec && window->session_tv_nsec == packet->session_tv_nsec;
    if (!same_session)
    {
        if (window->known &&
            (packet->session_tv_sec < window->session_tv_sec ||
             (packet->session_tv_sec == window->session_tv_sec && packet->session_tv_nsec < window->session_tv_nsec) ||
             (packet->session_tv_sec == window->session_tv_sec && packet->session_tv_nsec == window->session_tv_nsec &&
              packet->session_id < window->session_id)))
        {
            return POTR_ERR_PROTOCOL;
        }
        memset(window, 0, sizeof(*window));
        window->known = 1;
        window->session_id = packet->session_id;
        window->session_tv_sec = packet->session_tv_sec;
        window->session_tv_nsec = packet->session_tv_nsec;
        window->highest = packet->tx_counter;
    }
    if (packet->tx_counter > window->highest)
    {
        uint64_t shifted[4] = {0};
        size_t i;

        distance = packet->tx_counter - window->highest;
        if (distance < 256U)
        {
            /* 右辺のシフト数を 64 未満に保ち、ビットマップの記録を距離分だけ移動する。 */
            for (i = 0U; i < 256U - (size_t)distance; i++)
            {
                if ((window->bitmap[i / 64U] & (UINT64_C(1) << (i % 64U))) != 0U)
                {
                    size_t target = i + (size_t)distance;
                    shifted[target / 64U] |= UINT64_C(1) << (target % 64U);
                }
            }
        }
        memcpy(window->bitmap, shifted, sizeof(shifted));
        window->highest = packet->tx_counter;
    }
    distance = window->highest - packet->tx_counter;
    if (distance >= 256U)
    {
        return POTR_ERR_PROTOCOL;
    }
    word = (size_t)(distance / 64U);
    bit = UINT64_C(1) << (distance % 64U);
    if ((window->bitmap[word] & bit) != 0U)
    {
        return POTR_ERR_PROTOCOL;
    }
    window->bitmap[word] |= bit;
    return POTR_OK;
}
