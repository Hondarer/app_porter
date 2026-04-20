/**
 *******************************************************************************
 *  @file           packet.h
 *  @brief          パケット構築・解析モジュールの内部ヘッダー。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef PACKET_H
#define PACKET_H

#include <stddef.h>
#include <stdint.h>

#include <porter_type.h>

/** パケットヘッダーの固定長 (バイト)。payload フィールドの開始オフセット。 */
#define PACKET_HEADER_SIZE ((size_t)offsetof(PotrPacket, payload))

/**
 *  @brief  パケットに付与するセッション識別情報。
 *  @details
 *  potrOpenService 時に決定し、全パケットのヘッダーに格納する。
 */
typedef struct
{
    int64_t  service_id;      /**< サービス識別子。 */
    int64_t  session_tv_sec;  /**< セッション開始時刻 秒部。 */
    uint32_t session_id;      /**< セッション識別子 (乱数)。 */
    int32_t  session_tv_nsec; /**< セッション開始時刻 ナノ秒部。 */
} PotrPacketSessionHdr;

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    extern int    packet_build_nack(PotrPacket *packet,
                                    const PotrPacketSessionHdr *shdr,
                                    uint32_t nack_num);
    extern int    packet_build_ping(PotrPacket *packet,
                                    const PotrPacketSessionHdr *shdr,
                                    uint32_t seq_num,
                                    const uint8_t *health_payload,
                                    uint16_t       health_payload_len);
    extern int    packet_build_reject(PotrPacket *packet,
                                      const PotrPacketSessionHdr *shdr,
                                      uint32_t seq_num);
    extern int    packet_build_fin(PotrPacket *packet,
                                   const PotrPacketSessionHdr *shdr);
    extern int    packet_build_fin_ack(PotrPacket *packet,
                                       const PotrPacketSessionHdr *shdr,
                                       uint32_t fin_target_seq);
    extern int    packet_build_packed(PotrPacket *out,
                                      const PotrPacketSessionHdr *shdr,
                                      uint32_t seq_num,
                                      const void *packed_payload,
                                      size_t payload_len);
    extern int    packet_unpack_next(const PotrPacket *container,
                                     size_t *offset,
                                     PotrPacket *elem_out);
    extern int    packet_parse(PotrPacket *packet, const void *buf, size_t buf_len);
    extern size_t packet_wire_size(const PotrPacket *packet);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* PACKET_H */
