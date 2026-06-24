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

#include <com_util/base/platform.h>
#include <stddef.h>
#include <string.h>

#if defined(PLATFORM_LINUX)
    #include <arpa/inet.h>
#elif defined(PLATFORM_WINDOWS)
    #include <com_util/base/windows_sdk.h>
#endif /* PLATFORM_ */

#include <porter/porter_const.h>
#include <porter/porter_type.h>

#include <porter/protocol/packet.h>

/* packet.h で公開した PACKET_HEADER_SIZE をそのまま使用する */

/**
 *  @brief          64 ビット値をホスト バイト オーダーからネットワーク バイト オーダーへ変換します。
 *  @param[in]      v   変換するホスト バイト オーダーの値。
 *  @return         ネットワーク バイト オーダーに変換した値。
 *
 *  htonl() を上位 32 ビットと下位 32 ビットに適用し、ワードを入れ替えて連結します。
 */
static uint64_t hton64(uint64_t v)
{
    uint32_t hi = htonl((uint32_t)(v >> 32));
    uint32_t lo = htonl((uint32_t)(v & 0xFFFFFFFFUL));
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
static void fill_session_hdr(PotrPacket *packet, const PotrPacketSessionHdr *shdr)
{
    packet->service_id = (int64_t)hton64((uint64_t)shdr->service_id);
    packet->session_tv_sec = (int64_t)hton64((uint64_t)shdr->session_tv_sec);
    packet->session_id = htonl(shdr->session_id);
    packet->session_tv_nsec = htonl((uint32_t)shdr->session_tv_nsec);
    packet->protocol_version = htonl(POTR_PROTOCOL_VERSION);
}

/* Doxygen コメントは、ヘッダーに記載 */

int packet_build_nack(PotrPacket *packet, const PotrPacketSessionHdr *shdr, uint32_t nack_num)
{
    if (packet == NULL || shdr == NULL)
    {
        return POTR_ERROR;
    }

    memset(packet, 0, PACKET_HEADER_SIZE);
    packet->payload = NULL;
    fill_session_hdr(packet, shdr);
    packet->seq_num = 0;
    packet->ack_num = htonl(nack_num);
    packet->flags = htons(POTR_FLAG_NACK);
    packet->payload_len = 0;

    return POTR_SUCCESS;
}

/* Doxygen コメントは、ヘッダーに記載 */
/**
 *  @details
 *  暗号化時はヘルスチェック スレッドが wire_buf にコピー後に com_util_encrypt を適用します。
 */
int packet_build_ping(PotrPacket *packet, const PotrPacketSessionHdr *shdr, uint32_t seq_num,
                      const uint8_t *health_payload, uint16_t health_payload_len)
{
    if (packet == NULL || shdr == NULL)
    {
        return POTR_ERROR;
    }

    memset(packet, 0, PACKET_HEADER_SIZE);
    fill_session_hdr(packet, shdr);
    packet->seq_num = htonl(seq_num);
    packet->ack_num = 0;
    packet->flags = htons(POTR_FLAG_PING);

    if (health_payload != NULL)
    {
        packet->payload = health_payload;
        packet->payload_len = htons(health_payload_len);
    }
    else
    {
        packet->payload = NULL;
        packet->payload_len = 0;
    }

    return POTR_SUCCESS;
}

/* Doxygen コメントは、ヘッダーに記載 */

int packet_build_reject(PotrPacket *packet, const PotrPacketSessionHdr *shdr, uint32_t seq_num)
{
    if (packet == NULL || shdr == NULL)
    {
        return POTR_ERROR;
    }

    memset(packet, 0, PACKET_HEADER_SIZE);
    packet->payload = NULL;
    fill_session_hdr(packet, shdr);
    packet->seq_num = 0;
    packet->ack_num = htonl(seq_num);
    packet->flags = htons(POTR_FLAG_REJECT);
    packet->payload_len = 0;

    return POTR_SUCCESS;
}

/* Doxygen コメントは、ヘッダーに記載 */

int packet_build_fin(PotrPacket *packet, const PotrPacketSessionHdr *shdr)
{
    if (packet == NULL || shdr == NULL)
    {
        return POTR_ERROR;
    }

    memset(packet, 0, PACKET_HEADER_SIZE);
    packet->payload = NULL;
    fill_session_hdr(packet, shdr);
    packet->seq_num = 0;
    packet->ack_num = 0;
    packet->flags = htons(POTR_FLAG_FIN);
    packet->payload_len = 0;

    return POTR_SUCCESS;
}

/* Doxygen コメントは、ヘッダーに記載 */

int packet_build_fin_ack(PotrPacket *packet, const PotrPacketSessionHdr *shdr, uint32_t fin_target_seq)
{
    if (packet == NULL || shdr == NULL)
    {
        return POTR_ERROR;
    }

    memset(packet, 0, PACKET_HEADER_SIZE);
    packet->payload = NULL;
    fill_session_hdr(packet, shdr);
    packet->seq_num = 0;
    packet->ack_num = htonl(fin_target_seq);
    packet->flags = htons(POTR_FLAG_FIN_ACK);
    packet->payload_len = 0;

    return POTR_SUCCESS;
}

/* Doxygen コメントは、ヘッダーに記載 */

int packet_build_packed(PotrPacket *out, const PotrPacketSessionHdr *shdr, uint32_t seq_num, const void *packed_payload,
                        size_t payload_len)
{
    if (out == NULL || shdr == NULL || packed_payload == NULL || payload_len == 0 || payload_len > POTR_MAX_PAYLOAD)
    {
        return POTR_ERROR;
    }

    memset(out, 0, PACKET_HEADER_SIZE);
    fill_session_hdr(out, shdr);
    out->seq_num = htonl(seq_num);
    out->ack_num = 0;
    out->flags = htons(POTR_FLAG_DATA);
    out->payload_len = htons((uint16_t)payload_len);
    /* ゼロ コピー: 呼び出し元バッファーを直接指す */
    out->payload = (const uint8_t *)packed_payload;

    return POTR_SUCCESS;
}

/* Doxygen コメントは、ヘッダーに記載 */

int packet_unpack_next(const PotrPacket *container, size_t *offset, PotrPacket *elem_out)
{
    const uint8_t *p;
    uint16_t flags_nbo;
    uint32_t plen_nbo;
    uint32_t payload_len;

    if (container == NULL || offset == NULL || elem_out == NULL)
    {
        return POTR_ERROR;
    }

    /* 末尾チェック (container->payload_len はホスト バイト オーダー) */
    if (*offset + POTR_PAYLOAD_ELEM_HDR_SIZE > (size_t)container->payload_len)
    {
        return POTR_ERROR;
    }

    p = container->payload + *offset;

    memcpy(&flags_nbo, p, 2);
    memcpy(&plen_nbo, p + 2, 4);

    payload_len = ntohl(plen_nbo);

    if (*offset + POTR_PAYLOAD_ELEM_HDR_SIZE + payload_len > (size_t)container->payload_len ||
        payload_len > POTR_MAX_PAYLOAD)
    {
        return POTR_ERROR;
    }

    memset(elem_out, 0, PACKET_HEADER_SIZE);
    elem_out->payload = NULL;
    elem_out->service_id = container->service_id;
    elem_out->session_id = container->session_id;
    elem_out->session_tv_sec = container->session_tv_sec;
    elem_out->session_tv_nsec = container->session_tv_nsec;
    elem_out->protocol_version = container->protocol_version;
    elem_out->ack_num = 0;
    elem_out->flags = ntohs(flags_nbo);
    elem_out->payload_len = (uint16_t)payload_len;
    /* ゼロ コピー: コンテナーのペイロード領域を直接指す
       コンテナーの生存期間中 (呼び出し元の処理完了まで) のみ有効 */
    elem_out->payload = p + POTR_PAYLOAD_ELEM_HDR_SIZE;

    *offset += POTR_PAYLOAD_ELEM_HDR_SIZE + payload_len;
    return POTR_SUCCESS;
}

/* Doxygen コメントは、ヘッダーに記載 */

int packet_parse(PotrPacket *packet, const void *buf, size_t buf_len)
{
    const uint8_t *b = (const uint8_t *)buf;
    uint32_t tmp32;
    uint64_t tmp64;
    uint16_t tmp16;

    if (packet == NULL || buf == NULL || buf_len < PACKET_HEADER_SIZE)
    {
        return POTR_ERROR;
    }

    memcpy(&tmp64, b + 0, 8);
    packet->service_id = (int64_t)ntoh64(tmp64);
    memcpy(&tmp64, b + 8, 8);
    packet->session_tv_sec = (int64_t)ntoh64(tmp64);
    memcpy(&tmp32, b + 16, 4);
    packet->session_id = ntohl(tmp32);
    memcpy(&tmp32, b + 20, 4);
    packet->session_tv_nsec = (int32_t)ntohl(tmp32);
    memcpy(&tmp32, b + 24, 4);
    packet->seq_num = ntohl(tmp32);
    memcpy(&tmp32, b + 28, 4);
    packet->ack_num = ntohl(tmp32);
    memcpy(&tmp16, b + 32, 2);
    packet->flags = ntohs(tmp16);
    memcpy(&tmp16, b + 34, 2);
    packet->payload_len = ntohs(tmp16);
    memcpy(&tmp32, b + 36, 4);
    packet->protocol_version = ntohl(tmp32);

    if (packet->protocol_version != POTR_PROTOCOL_VERSION || packet->payload_len > POTR_MAX_PAYLOAD ||
        (size_t)packet->payload_len + PACKET_HEADER_SIZE > buf_len)
    {
        return POTR_ERROR;
    }

    /* ゼロ コピー: 受信バッファー内のペイロード領域を直接指す
       呼び出し元バッファー (recv_buf) の生存期間中のみ有効 */
    packet->payload = b + PACKET_HEADER_SIZE;

    return POTR_SUCCESS;
}

/* Doxygen コメントは、ヘッダーに記載 */

size_t packet_wire_size(const PotrPacket *packet)
{
    if (packet == NULL)
    {
        return 0;
    }

    return PACKET_HEADER_SIZE + ntohs(packet->payload_len);
}
