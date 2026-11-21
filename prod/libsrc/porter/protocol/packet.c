/**
 *******************************************************************************
 *  @file           packet.c
 *  @brief          パケットの構築と解析を行う機能を提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/base/platform.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/porter_type.h>

#include <porter/protocol/packet.h>
#include <cplat/net/byteorder.h>

static_assert(POTR_PACKET_HEADER_SIZE == POTR_PACKET_HEADER_WIRE_SIZE, "porter: packet header wire size mismatch");
static_assert(offsetof(potr_packet, tx_counter) == 40U, "porter: tx_counter offset mismatch");
static_assert(offsetof(potr_packet, tx_tv_sec) == 48U, "porter: tx_tv_sec offset mismatch");
static_assert(offsetof(potr_packet, tx_tv_nsec) == 56U, "porter: tx_tv_nsec offset mismatch");
static_assert(offsetof(potr_packet, reserved) == 60U, "porter: reserved offset mismatch");
static_assert(offsetof(potr_packet, tx_mono_ns) == 64U, "porter: tx_mono_ns offset mismatch");
static_assert(POTR_PACKET_PAYLOAD_LEN_OFFSET == offsetof(potr_packet, payload_len),
              "porter: payload length offset mismatch");
static_assert(POTR_PACKET_FLAGS_OFFSET == offsetof(potr_packet, flags), "porter: flags offset mismatch");

/* ワイヤー ヘッダーの各項目の位置と幅が、potr_packet のメンバーと一致することを確かめる。 */
#define PACKET_FIELD_MATCHES(member, off, size) \
    (offsetof(potr_packet, member) == (off) && sizeof(((potr_packet *)0)->member) == (size))
static_assert(PACKET_FIELD_MATCHES(service_id, POTR_PKT_OFF_SERVICE_ID, POTR_PKT_SIZE_SERVICE_ID),
              "porter: service_id layout mismatch");
static_assert(PACKET_FIELD_MATCHES(session_tv_sec, POTR_PKT_OFF_SESSION_TV_SEC, POTR_PKT_SIZE_SESSION_TV_SEC),
              "porter: session_tv_sec layout mismatch");
static_assert(PACKET_FIELD_MATCHES(session_id, POTR_PKT_OFF_SESSION_ID, POTR_PKT_SIZE_SESSION_ID),
              "porter: session_id layout mismatch");
static_assert(PACKET_FIELD_MATCHES(session_tv_nsec, POTR_PKT_OFF_SESSION_TV_NSEC, POTR_PKT_SIZE_SESSION_TV_NSEC),
              "porter: session_tv_nsec layout mismatch");
static_assert(PACKET_FIELD_MATCHES(seq_num, POTR_PKT_OFF_SEQ_NUM, POTR_PKT_SIZE_SEQ_NUM),
              "porter: seq_num layout mismatch");
static_assert(PACKET_FIELD_MATCHES(ack_num, POTR_PKT_OFF_ACK_NUM, POTR_PKT_SIZE_ACK_NUM),
              "porter: ack_num layout mismatch");
static_assert(PACKET_FIELD_MATCHES(flags, POTR_PKT_OFF_FLAGS, POTR_PKT_SIZE_FLAGS), "porter: flags layout mismatch");
static_assert(PACKET_FIELD_MATCHES(payload_len, POTR_PKT_OFF_PAYLOAD_LEN, POTR_PKT_SIZE_PAYLOAD_LEN),
              "porter: payload_len layout mismatch");
static_assert(PACKET_FIELD_MATCHES(protocol_version, POTR_PKT_OFF_PROTOCOL_VERSION, POTR_PKT_SIZE_PROTOCOL_VERSION),
              "porter: protocol_version layout mismatch");
static_assert(PACKET_FIELD_MATCHES(tx_counter, POTR_PKT_OFF_TX_COUNTER, POTR_PKT_SIZE_TX_COUNTER),
              "porter: tx_counter layout mismatch");
static_assert(PACKET_FIELD_MATCHES(tx_tv_sec, POTR_PKT_OFF_TX_TV_SEC, POTR_PKT_SIZE_TX_TV_SEC),
              "porter: tx_tv_sec layout mismatch");
static_assert(PACKET_FIELD_MATCHES(tx_tv_nsec, POTR_PKT_OFF_TX_TV_NSEC, POTR_PKT_SIZE_TX_TV_NSEC),
              "porter: tx_tv_nsec layout mismatch");
static_assert(PACKET_FIELD_MATCHES(reserved, POTR_PKT_OFF_RESERVED, POTR_PKT_SIZE_RESERVED),
              "porter: reserved layout mismatch");
static_assert(PACKET_FIELD_MATCHES(tx_mono_ns, POTR_PKT_OFF_TX_MONO_NS, POTR_PKT_SIZE_TX_MONO_NS),
              "porter: tx_mono_ns layout mismatch");
static_assert(POTR_PKT_OFF_TX_MONO_NS + POTR_PKT_SIZE_TX_MONO_NS == POTR_PACKET_HEADER_WIRE_SIZE,
              "porter: last header field must end at the header size");

/* 外側パケットのフラグは 16 ビットの flags に収まり、各ビットが重ならないことを確かめる。 */
#define PACKET_OUTER_FLAGS_ALL \
    (POTR_FLAG_DATA | POTR_FLAG_NACK | POTR_FLAG_PING | POTR_FLAG_REJECT | POTR_FLAG_FIN | POTR_FLAG_ENCRYPTED | \
     POTR_FLAG_FIN_TARGET_VALID | POTR_FLAG_FIN_ACK | POTR_FLAG_REPORT | POTR_FLAG_HANDSHAKE | POTR_FLAG_KEY | \
     POTR_FLAG_FLOW_CONTROL)
#define PACKET_OUTER_FLAGS_SUM \
    (POTR_FLAG_DATA + POTR_FLAG_NACK + POTR_FLAG_PING + POTR_FLAG_REJECT + POTR_FLAG_FIN + POTR_FLAG_ENCRYPTED + \
     POTR_FLAG_FIN_TARGET_VALID + POTR_FLAG_FIN_ACK + POTR_FLAG_REPORT + POTR_FLAG_HANDSHAKE + POTR_FLAG_KEY + \
     POTR_FLAG_FLOW_CONTROL)
static_assert(PACKET_OUTER_FLAGS_ALL <= UINT16_MAX && PACKET_OUTER_FLAGS_ALL == PACKET_OUTER_FLAGS_SUM,
              "porter: outer packet flags must fit in uint16_t and must not overlap");
static_assert((POTR_FLAG_MORE_FRAG | POTR_FLAG_COMPRESSED) <= UINT16_MAX &&
                  (POTR_FLAG_MORE_FRAG & POTR_FLAG_COMPRESSED) == 0U,
              "porter: payload element flags must fit in uint16_t and must not overlap");

/* 大きさの上限が、ワイヤー形式の 16 ビットの長さとペイロード エレメントのヘッダーに収まることを確かめる。 */
static_assert(POTR_MAX_PAYLOAD <= UINT16_MAX && POTR_MAX_WINDOW_SIZE <= UINT16_MAX,
              "porter: payload and window limits must fit in uint16_t");
static_assert(POTR_PAYLOAD_ELEM_HDR_SIZE == 6U, "porter: payload element header must be 6 bytes");

/* フロー制御パケットのペイロードの形式を確かめる。 */
static_assert(POTR_FLOW_CONTROL_PAYLOAD_SIZE == 5U, "porter: flow control payload must be 5 bytes");
static_assert(POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE == 2U, "porter: monitor list header must be 2 bytes");
static_assert(POTR_FLOW_CONTROL_MONITOR_ENTRY_SIZE == POTR_IDENTITY_SIZE,
              "porter: monitor list entry must hold a session identity");
static_assert(POTR_FLOW_CONTROL_MONITOR_PAYLOAD_MAX <= POTR_DEFAULT_MAX_PAYLOAD,
              "porter: monitor list payload must fit in the default max payload");
/**
 *  @brief          64 ビット値をホスト バイト オーダーからネットワーク バイト オーダーへ変換します。
 *  @param[in]      v   変換するホスト バイト オーダーの値。
 *  @return         ネットワーク バイト オーダーに変換した値。
 *
 *  cplat_hton32() を上位 32 ビットと下位 32 ビットに適用し、ワードを入れ替えて連結します。
 */
static uint64_t hton64(uint64_t v)
{
    uint32_t hi = cplat_hton32((uint32_t)(v >> 32));
    uint32_t lo = cplat_hton32((uint32_t)(v & 0xFFFFFFFFUL));

    return ((uint64_t)lo << 32) | (uint64_t)hi;
}

/**
 *  @brief          64 ビット値をネットワーク バイト オーダーからホスト バイト オーダーへ変換します。
 *  @param[in]      v   変換するネットワーク バイト オーダーの値。
 *  @return         ホスト バイト オーダーに変換した値。
 *
 *  変換は対称であるため hton64() をそのまま再利用します。
 */
static uint64_t ntoh64(uint64_t v)
{
    return hton64(v); /* 対称変換 */
}

/**
 *  @brief          セッション識別フィールドをパケットにネットワーク バイト オーダーで書き込みます。
 *  @param[out]     packet  書き込み先パケット構造体へのポインター。
 *  @param[in]      shdr    セッション識別ヘッダーへのポインター。
 *
 *  service_id・session_tv_sec・session_id・session_tv_nsec を NBO に変換して格納し、
 *  protocol_version に POTR_PROTOCOL_VERSION を NBO で設定します。
 */
static void fill_session_hdr(potr_packet *packet, const potr_internal_packet_session_hdr *shdr)
{
    packet->service_id = (int64_t)hton64((uint64_t)shdr->service_id);
    packet->session_tv_sec = (int64_t)hton64((uint64_t)shdr->session_tv_sec);
    packet->session_id = cplat_hton32(shdr->session_id);

    /* session_tv_nsec は正規化済みナノ秒部 (0..999999999)。ワイヤのビット列を int32_t へ格納する。 */
    packet->session_tv_nsec = (int32_t)cplat_hton32((uint32_t)shdr->session_tv_nsec);
    packet->protocol_version = cplat_hton32(POTR_PROTOCOL_VERSION);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_packet_build_nack(potr_packet *packet, const potr_internal_packet_session_hdr *shdr,
                                    uint32_t nack_num)
{
    if (packet == NULL || shdr == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    memset(packet, 0, POTR_PACKET_HEADER_SIZE);
    packet->payload = NULL;
    fill_session_hdr(packet, shdr);
    packet->seq_num = 0;
    packet->ack_num = cplat_hton32(nack_num);
    packet->flags = cplat_hton16(POTR_FLAG_NACK);
    packet->payload_len = 0;

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_packet_build_flow_control(potr_packet *packet, const potr_internal_packet_session_hdr *shdr,
                                            uint8_t *payload, uint8_t state, uint32_t send_limit)
{
    uint16_t payload_len;

    if (packet == NULL || shdr == NULL || payload == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (state == POTR_FLOW_CONTROL_STATE_QUERY)
    {
        if (send_limit != 0U)
        {
            return POTR_ERR_INVALID_ARGUMENT;
        }

        payload[0] = state;
        payload_len = (uint16_t)POTR_FLOW_CONTROL_QUERY_PAYLOAD_SIZE;
    }
    else if (state == POTR_FLOW_CONTROL_STATE_SUPPRESS || state == POTR_FLOW_CONTROL_STATE_RESUME ||
             state == POTR_FLOW_CONTROL_STATE_UNLIMITED)
    {
        if (state == POTR_FLOW_CONTROL_STATE_UNLIMITED && send_limit != 0U)
        {
            return POTR_ERR_INVALID_ARGUMENT;
        }

        /* 送出上限はネットワーク バイト オーダーで格納する。解析側の flow_control_payload_decode() と対称にする。 */
        payload[0] = state;
        payload[1] = (uint8_t)(send_limit >> 24);
        payload[2] = (uint8_t)(send_limit >> 16);
        payload[3] = (uint8_t)(send_limit >> 8);
        payload[4] = (uint8_t)send_limit;
        payload_len = (uint16_t)POTR_FLOW_CONTROL_PAYLOAD_SIZE;
    }
    else
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    memset(packet, 0, POTR_PACKET_HEADER_SIZE);
    fill_session_hdr(packet, shdr);
    packet->seq_num = 0;
    packet->ack_num = 0;
    packet->flags = cplat_hton16(POTR_FLAG_FLOW_CONTROL);
    packet->payload = payload;
    packet->payload_len = cplat_hton16(payload_len);

    return POTR_OK;
}

/* 値を、上位のバイトから順に length [byte] 書き込む (ネットワーク バイト オーダー)。 */
static void put_network_order(uint8_t *destination, uint64_t value, size_t length)
{
    size_t i;

    for (i = 0U; i < length; i++)
    {
        destination[i] = (uint8_t)(value >> (8U * (length - 1U - i)));
    }
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_packet_build_flow_control_list(potr_packet *packet, const potr_internal_packet_session_hdr *shdr,
                                                 uint8_t *payload, const potr_internal_packet_session_hdr *receivers,
                                                 size_t count)
{
    size_t i;

    if (packet == NULL || shdr == NULL || payload == NULL || receivers == NULL || count < 1U ||
        count > POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    /* 受信者のセッション識別子は、ヘッダーと同じ並びとし、解析側の flow_control_monitor_entry() と対称にする。 */
    payload[0] = (uint8_t)POTR_FLOW_CONTROL_STATE_MONITOR;
    payload[1] = (uint8_t)count;

    for (i = 0U; i < count; i++)
    {
        uint8_t *entry = payload + POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE + POTR_FLOW_CONTROL_MONITOR_ENTRY_SIZE * i;

        put_network_order(entry + POTR_IDENTITY_TV_SEC_OFF, (uint64_t)receivers[i].session_tv_sec,
                          POTR_IDENTITY_TV_SEC_SIZE);
        put_network_order(entry + POTR_IDENTITY_ID_OFF, (uint64_t)receivers[i].session_id, POTR_IDENTITY_ID_SIZE);
        put_network_order(entry + POTR_IDENTITY_TV_NSEC_OFF, (uint64_t)(uint32_t)receivers[i].session_tv_nsec,
                          POTR_IDENTITY_TV_NSEC_SIZE);
    }

    memset(packet, 0, POTR_PACKET_HEADER_SIZE);
    fill_session_hdr(packet, shdr);
    packet->seq_num = 0;
    packet->ack_num = 0;
    packet->flags = cplat_hton16(POTR_FLAG_FLOW_CONTROL);
    packet->payload = payload;
    packet->payload_len =
        cplat_hton16((uint16_t)(POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE + POTR_FLOW_CONTROL_MONITOR_ENTRY_SIZE * count));

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_packet_build_ping(potr_packet *packet, const potr_internal_packet_session_hdr *shdr, uint32_t seq_num,
                                    const uint8_t *health_payload, uint16_t health_payload_len)
{
    if (packet == NULL || shdr == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    memset(packet, 0, POTR_PACKET_HEADER_SIZE);
    fill_session_hdr(packet, shdr);
    packet->seq_num = cplat_hton32(seq_num);
    packet->ack_num = 0;
    packet->flags = cplat_hton16(POTR_FLAG_PING);

    if (health_payload != NULL)
    {
        packet->payload = health_payload;
        packet->payload_len = cplat_hton16(health_payload_len);
    }
    else
    {
        packet->payload = NULL;
        packet->payload_len = 0;
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_packet_build_reject(potr_packet *packet, const potr_internal_packet_session_hdr *shdr,
                                      uint32_t seq_num)
{
    if (packet == NULL || shdr == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    memset(packet, 0, POTR_PACKET_HEADER_SIZE);
    packet->payload = NULL;
    fill_session_hdr(packet, shdr);
    packet->seq_num = 0;
    packet->ack_num = cplat_hton32(seq_num);
    packet->flags = cplat_hton16(POTR_FLAG_REJECT);
    packet->payload_len = 0;

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_packet_build_fin(potr_packet *packet, const potr_internal_packet_session_hdr *shdr)
{
    if (packet == NULL || shdr == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    memset(packet, 0, POTR_PACKET_HEADER_SIZE);
    packet->payload = NULL;
    fill_session_hdr(packet, shdr);
    packet->seq_num = 0;
    packet->ack_num = 0;
    packet->flags = cplat_hton16(POTR_FLAG_FIN);
    packet->payload_len = 0;

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_packet_build_fin_ack(potr_packet *packet, const potr_internal_packet_session_hdr *shdr,
                                       uint32_t fin_target_seq)
{
    if (packet == NULL || shdr == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    memset(packet, 0, POTR_PACKET_HEADER_SIZE);
    packet->payload = NULL;
    fill_session_hdr(packet, shdr);
    packet->seq_num = 0;
    packet->ack_num = cplat_hton32(fin_target_seq);
    packet->flags = cplat_hton16(POTR_FLAG_FIN_ACK);
    packet->payload_len = 0;

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_packet_build_packed(potr_packet *out, const potr_internal_packet_session_hdr *shdr, uint32_t seq_num,
                                      const void *packed_payload, size_t payload_len)
{
    if (out == NULL || shdr == NULL || packed_payload == NULL || payload_len == 0 || payload_len > POTR_MAX_PAYLOAD)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    memset(out, 0, POTR_PACKET_HEADER_SIZE);
    fill_session_hdr(out, shdr);
    out->seq_num = cplat_hton32(seq_num);
    out->ack_num = 0;
    out->flags = cplat_hton16(POTR_FLAG_DATA);
    out->payload_len = cplat_hton16((uint16_t)payload_len);

    /* ゼロ コピー: 呼び出し元バッファーを直接指す。 */
    out->payload = (const uint8_t *)packed_payload;

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_packet_unpack_next(const potr_packet *container, size_t *offset, potr_packet *elem_out)
{
    const uint8_t *p;
    uint16_t flags_nbo;
    uint32_t plen_nbo;
    uint32_t payload_len;

    if (container == NULL || offset == NULL || elem_out == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    /* 末尾チェック (container->payload_len はホスト バイト オーダー)。 */
    if (*offset + POTR_PAYLOAD_ELEM_HDR_SIZE > (size_t)container->payload_len)
    {
        return POTR_ERR_EOF;
    }

    p = container->payload + *offset;

    memcpy(&flags_nbo, p, POTR_ELEM_HDR_FLAGS_SIZE);
    memcpy(&plen_nbo, p + POTR_ELEM_HDR_PLEN_OFF, POTR_ELEM_HDR_PLEN_SIZE);

    payload_len = cplat_ntoh32(plen_nbo);

    if (*offset + POTR_PAYLOAD_ELEM_HDR_SIZE + payload_len > (size_t)container->payload_len ||
        payload_len > POTR_MAX_PAYLOAD)
    {
        return POTR_ERR_PROTOCOL;
    }

    memset(elem_out, 0, POTR_PACKET_HEADER_SIZE);
    elem_out->payload = NULL;
    elem_out->service_id = container->service_id;
    elem_out->session_id = container->session_id;
    elem_out->session_tv_sec = container->session_tv_sec;
    elem_out->session_tv_nsec = container->session_tv_nsec;
    elem_out->protocol_version = container->protocol_version;
    elem_out->ack_num = 0;
    elem_out->flags = cplat_ntoh16(flags_nbo);
    elem_out->payload_len = (uint16_t)payload_len;

    /*
     * ゼロ コピー: コンテナーのペイロード領域を直接指す。
     * コンテナーの生存期間中 (呼び出し元の処理完了まで) のみ有効である。
     */
    elem_out->payload = p + POTR_PAYLOAD_ELEM_HDR_SIZE;

    *offset += POTR_PAYLOAD_ELEM_HDR_SIZE + payload_len;

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_packet_parse(potr_packet *packet, const void *buf, size_t buf_len)
{
    const uint8_t *b = (const uint8_t *)buf;
    uint32_t tmp32;
    uint64_t tmp64;
    uint16_t tmp16;

    if (packet == NULL || buf == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    /* ヘッダー長未満の受信データは不正パケットとして扱う。 */
    if (buf_len < POTR_PACKET_HEADER_SIZE)
    {
        return POTR_ERR_PROTOCOL;
    }

    memcpy(&tmp64, b + POTR_PKT_OFF_SERVICE_ID, POTR_PKT_SIZE_SERVICE_ID);
    packet->service_id = (int64_t)ntoh64(tmp64);
    memcpy(&tmp64, b + POTR_PKT_OFF_SESSION_TV_SEC, POTR_PKT_SIZE_SESSION_TV_SEC);
    packet->session_tv_sec = (int64_t)ntoh64(tmp64);
    memcpy(&tmp32, b + POTR_PKT_OFF_SESSION_ID, POTR_PKT_SIZE_SESSION_ID);
    packet->session_id = cplat_ntoh32(tmp32);
    memcpy(&tmp32, b + POTR_PKT_OFF_SESSION_TV_NSEC, POTR_PKT_SIZE_SESSION_TV_NSEC);
    packet->session_tv_nsec = (int32_t)cplat_ntoh32(tmp32);
    memcpy(&tmp32, b + POTR_PKT_OFF_SEQ_NUM, POTR_PKT_SIZE_SEQ_NUM);
    packet->seq_num = cplat_ntoh32(tmp32);
    memcpy(&tmp32, b + POTR_PKT_OFF_ACK_NUM, POTR_PKT_SIZE_ACK_NUM);
    packet->ack_num = cplat_ntoh32(tmp32);
    memcpy(&tmp16, b + POTR_PKT_OFF_FLAGS, POTR_PKT_SIZE_FLAGS);
    packet->flags = cplat_ntoh16(tmp16);
    memcpy(&tmp16, b + POTR_PKT_OFF_PAYLOAD_LEN, POTR_PKT_SIZE_PAYLOAD_LEN);
    packet->payload_len = cplat_ntoh16(tmp16);
    memcpy(&tmp32, b + POTR_PKT_OFF_PROTOCOL_VERSION, POTR_PKT_SIZE_PROTOCOL_VERSION);
    packet->protocol_version = cplat_ntoh32(tmp32);
    memcpy(&tmp64, b + POTR_PKT_OFF_TX_COUNTER, POTR_PKT_SIZE_TX_COUNTER);
    packet->tx_counter = ntoh64(tmp64);
    memcpy(&tmp64, b + POTR_PKT_OFF_TX_TV_SEC, POTR_PKT_SIZE_TX_TV_SEC);
    packet->tx_tv_sec = (int64_t)ntoh64(tmp64);
    memcpy(&tmp32, b + POTR_PKT_OFF_TX_TV_NSEC, POTR_PKT_SIZE_TX_TV_NSEC);
    packet->tx_tv_nsec = (int32_t)cplat_ntoh32(tmp32);
    memcpy(&tmp32, b + POTR_PKT_OFF_RESERVED, POTR_PKT_SIZE_RESERVED);
    packet->reserved = cplat_ntoh32(tmp32);
    memcpy(&tmp64, b + POTR_PKT_OFF_TX_MONO_NS, POTR_PKT_SIZE_TX_MONO_NS);
    packet->tx_mono_ns = ntoh64(tmp64);

    if (packet->protocol_version != POTR_PROTOCOL_VERSION || packet->payload_len > POTR_MAX_PAYLOAD ||
        (size_t)packet->payload_len + POTR_PACKET_HEADER_SIZE > buf_len)
    {
        return POTR_ERR_PROTOCOL;
    }

    /*
     * ゼロ コピー: 受信バッファー内のペイロード領域を直接指す。
     * 呼び出し元バッファー (recv_buf) の生存期間中のみ有効である。
     */
    packet->payload = b + POTR_PACKET_HEADER_SIZE;

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

size_t potr_internal_packet_wire_size(const potr_packet *packet)
{
    if (packet == NULL)
    {
        return 0;
    }

    return POTR_PACKET_HEADER_SIZE + cplat_ntoh16(packet->payload_len);
}
