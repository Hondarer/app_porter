/**
 *******************************************************************************
 *  @file           packet.c
 *  @brief          パケット構築・解析モジュール。
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

#include <porter_const.h>
#include <porter_type.h>

#include "packet.h"

/* packet.h で公開した PACKET_HEADER_SIZE をそのまま使用する */

/* 64 ビット値をホストバイトオーダーからネットワークバイトオーダーへ変換する */
static uint64_t hton64(uint64_t v)
{
    uint32_t hi = htonl((uint32_t)(v >> 32));
    uint32_t lo = htonl((uint32_t)(v & 0xFFFFFFFFUL));
    return ((uint64_t)lo << 32) | (uint64_t)hi;
}

/* 64 ビット値をネットワークバイトオーダーからホストバイトオーダーへ変換する */
static uint64_t ntoh64(uint64_t v)
{
    return hton64(v); /* 対称変換 */
}

/* セッションヘッダーフィールドをパケットに NBO で書き込む */
static void fill_session_hdr(PotrPacket *packet, const PotrPacketSessionHdr *shdr)
{
    packet->service_id      = (int64_t)hton64((uint64_t)shdr->service_id);
    packet->session_tv_sec  = (int64_t)hton64((uint64_t)shdr->session_tv_sec);
    packet->session_id      = htonl(shdr->session_id);
    packet->session_tv_nsec = htonl((uint32_t)shdr->session_tv_nsec);
}

/**
 *******************************************************************************
 *  @brief          NACK パケットを構築します。
 *  @param[out]     packet      構築結果を格納するパケット構造体へのポインタ。
 *  @param[in]      shdr        セッション識別ヘッダーへのポインタ。
 *  @param[in]      nack_num    再送要求する通番。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *******************************************************************************
 */
int packet_build_nack(PotrPacket *packet, const PotrPacketSessionHdr *shdr,
                      uint32_t nack_num)
{
    if (packet == NULL || shdr == NULL)
    {
        return POTR_ERROR;
    }

    memset(packet, 0, PACKET_HEADER_SIZE);
    packet->payload     = NULL;
    fill_session_hdr(packet, shdr);
    packet->seq_num     = 0;
    packet->ack_num     = htonl(nack_num);
    packet->flags       = htons(POTR_FLAG_NACK);
    packet->payload_len = 0;

    return POTR_SUCCESS;
}

/**
 *******************************************************************************
 *  @brief          PING パケットを構築します。
 *  @param[out]     packet             構築結果を格納するパケット構造体へのポインタ。
 *  @param[in]      shdr               セッション識別ヘッダーへのポインタ。
 *  @param[in]      seq_num            通番 (ウィンドウ管理に使用)。
 *  @param[in]      health_payload     パス PING 受信状態ペイロードへのポインタ (POTR_PING_STATE_* 値の配列)。
 *                                     NULL の場合はペイロードなし (payload_len=0)。
 *  @param[in]      health_payload_len ペイロード長 (バイト)。通常 POTR_MAX_PATH。health_payload が NULL の場合は無視。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *
 *  @details
 *  ヘルスチェックパケットです。\n
 *  通番には送信側の next_seq（次に送出する DATA に割り当てる通番）を格納します。\n
 *  PING はウィンドウに登録されません（NACK・再送の対象外）。\n
 *  ack_num は常に 0。受信者は seq_num を上限として欠番を一括 NACK します。\n
 *  ペイロードには POTR_MAX_PATH バイトのパス受信状態 (POTR_PING_STATE_*) を格納します。\n
 *  暗号化時はヘルススレッドが wire_buf にコピー後に com_util_encrypt を適用します。
 *******************************************************************************
 */
int packet_build_ping(PotrPacket *packet, const PotrPacketSessionHdr *shdr,
                      uint32_t seq_num,
                      const uint8_t *health_payload, uint16_t health_payload_len)
{
    if (packet == NULL || shdr == NULL)
    {
        return POTR_ERROR;
    }

    memset(packet, 0, PACKET_HEADER_SIZE);
    fill_session_hdr(packet, shdr);
    packet->seq_num     = htonl(seq_num);
    packet->ack_num     = 0;
    packet->flags       = htons(POTR_FLAG_PING);

    if (health_payload != NULL)
    {
        packet->payload     = health_payload;
        packet->payload_len = htons(health_payload_len);
    }
    else
    {
        packet->payload     = NULL;
        packet->payload_len = 0;
    }

    return POTR_SUCCESS;
}

/**
 *******************************************************************************
 *  @brief          再送不能通知 (REJECT) パケットを構築します。
 *  @param[out]     packet      構築結果を格納するパケット構造体へのポインタ。
 *  @param[in]      shdr        セッション識別ヘッダーへのポインタ。
 *  @param[in]      seq_num     再送不能な通番。ack_num フィールドに格納します。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *
 *  @details
 *  受信者から NACK を受け取ったが、送信ウィンドウに該当パケットが存在しない場合に
 *  送信者が返すパケットです。受信者はこのパケットを受け取ると即時 DISCONNECTED を
 *  発火し、欠落通番をスキップして後続パケットの配信を継続します。
 *******************************************************************************
 */
int packet_build_reject(PotrPacket *packet, const PotrPacketSessionHdr *shdr,
                        uint32_t seq_num)
{
    if (packet == NULL || shdr == NULL)
    {
        return POTR_ERROR;
    }

    memset(packet, 0, PACKET_HEADER_SIZE);
    packet->payload     = NULL;
    fill_session_hdr(packet, shdr);
    packet->seq_num     = 0;
    packet->ack_num     = htonl(seq_num);
    packet->flags       = htons(POTR_FLAG_REJECT);
    packet->payload_len = 0;

    return POTR_SUCCESS;
}

/**
 *******************************************************************************
 *  @brief          正常終了通知 (FIN) パケットを構築します。
 *  @param[out]     packet      構築結果を格納するパケット構造体へのポインタ。
 *  @param[in]      shdr        セッション識別ヘッダーへのポインタ。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *
 *  @details
 *  送信者が potrCloseService 時に送出する終了通知パケットです。ペイロードなし。\n
 *  `POTR_FLAG_FIN_TARGET_VALID` と `ack_num` の設定は呼び出し側が行います。\n
 *  受信者は target 付き FIN の場合のみ `ack_num` を参照して
 *  受信ウィンドウ追い付き後まで DISCONNECTED を遅延できます。
 *******************************************************************************
 */
int packet_build_fin(PotrPacket *packet, const PotrPacketSessionHdr *shdr)
{
    if (packet == NULL || shdr == NULL)
    {
        return POTR_ERROR;
    }

    memset(packet, 0, PACKET_HEADER_SIZE);
    packet->payload     = NULL;
    fill_session_hdr(packet, shdr);
    packet->seq_num     = 0;
    packet->ack_num     = 0;
    packet->flags       = htons(POTR_FLAG_FIN);
    packet->payload_len = 0;

    return POTR_SUCCESS;
}

/**
 *******************************************************************************
 *  @brief          FIN 完了応答 (FIN_ACK) パケットを構築します。
 *  @param[out]     packet          構築結果を格納するパケット構造体へのポインタ。
 *  @param[in]      shdr            セッション識別ヘッダーへのポインタ。
 *  @param[in]      fin_target_seq  完了した FIN target 通番。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *******************************************************************************
 */
int packet_build_fin_ack(PotrPacket *packet, const PotrPacketSessionHdr *shdr,
                         uint32_t fin_target_seq)
{
    if (packet == NULL || shdr == NULL)
    {
        return POTR_ERROR;
    }

    memset(packet, 0, PACKET_HEADER_SIZE);
    packet->payload     = NULL;
    fill_session_hdr(packet, shdr);
    packet->seq_num     = 0;
    packet->ack_num     = htonl(fin_target_seq);
    packet->flags       = htons(POTR_FLAG_FIN_ACK);
    packet->payload_len = 0;

    return POTR_SUCCESS;
}

/**
 *******************************************************************************
 *  @brief          データパケット (パックコンテナ) を構築します。
 *  @param[out]     out             構築結果を格納するパケット構造体へのポインタ。
 *  @param[in]      shdr            セッション識別ヘッダーへのポインタ。
 *  @param[in]      seq_num         外側パケットの通番。再送・順序整列に使用する。
 *  @param[in]      packed_payload  送信スレッドが構築したペイロードエレメント列。
 *  @param[in]      payload_len     packed_payload のバイト数。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *
 *  @details
 *  すべてのデータパケットはパックコンテナ形式で送受信します。\n
 *  ペイロードエレメントが 1 件のみの場合も同じ形式を使用します。\n
 *  再送・順序整列の単位は外側パケット (本関数が構築する UDP ペイロード) であり、
 *  通番は外側パケットの seq_num フィールドで管理します。\n
 *  ペイロードエレメントの形式は flags(2) + payload_len(4) + payload(N) です。\n
 *  受信者は POTR_FLAG_DATA を検出後 packet_unpack_next() でペイロードエレメントを展開します。
 *******************************************************************************
 */
int packet_build_packed(PotrPacket *out, const PotrPacketSessionHdr *shdr,
                        uint32_t seq_num,
                        const void *packed_payload, size_t payload_len)
{
    if (out == NULL || shdr == NULL || packed_payload == NULL
        || payload_len == 0 || payload_len > POTR_MAX_PAYLOAD)
    {
        return POTR_ERROR;
    }

    memset(out, 0, PACKET_HEADER_SIZE);
    fill_session_hdr(out, shdr);
    out->seq_num     = htonl(seq_num);
    out->ack_num     = 0;
    out->flags       = htons(POTR_FLAG_DATA);
    out->payload_len = htons((uint16_t)payload_len);
    /* ゼロコピー: 呼び出し元バッファを直接指す。 */
    out->payload     = (const uint8_t *)packed_payload;

    return POTR_SUCCESS;
}

/**
 *******************************************************************************
 *  @brief          データパケットから次のペイロードエレメントを取り出します。
 *  @param[in]      container  packet_parse() 済みのデータパケット (POTR_FLAG_DATA)。
 *  @param[in,out]  offset     コンテナ payload 内の読み取り位置。呼び出し毎に更新。
 *  @param[out]     elem_out    取り出したペイロードエレメントを格納する構造体へのポインタ。
 *  @return         ペイロードエレメントを取り出せた場合は POTR_SUCCESS、
 *                  末尾に達した場合またはエラーの場合は POTR_ERROR を返します。
 *
 *  @details
 *  ペイロードエレメントの形式は flags(2) + payload_len(4) + payload(N) です。\n
 *  通番は外側パケットで管理するためペイロードエレメントには含まれません。\n
 *  container->payload_len はホストバイトオーダー (packet_parse() 変換済み) で参照します。\n
 *  elem_out の session 情報は container から引き継ぎます。
 *******************************************************************************
 */
int packet_unpack_next(const PotrPacket *container, size_t *offset,
                       PotrPacket *elem_out)
{
    const uint8_t *p;
    uint16_t       flags_nbo;
    uint32_t       plen_nbo;
    uint32_t       payload_len;

    if (container == NULL || offset == NULL || elem_out == NULL)
    {
        return POTR_ERROR;
    }

    /* 末尾チェック (container->payload_len はホストバイトオーダー) */
    if (*offset + POTR_PAYLOAD_ELEM_HDR_SIZE > (size_t)container->payload_len)
    {
        return POTR_ERROR;
    }

    p = container->payload + *offset;

    memcpy(&flags_nbo, p,     2);
    memcpy(&plen_nbo,  p + 2, 4);

    payload_len = ntohl(plen_nbo);

    if (*offset + POTR_PAYLOAD_ELEM_HDR_SIZE + payload_len > (size_t)container->payload_len
        || payload_len > POTR_MAX_PAYLOAD)
    {
        return POTR_ERROR;
    }

    memset(elem_out, 0, PACKET_HEADER_SIZE);
    elem_out->payload         = NULL;
    elem_out->service_id      = container->service_id;
    elem_out->session_id      = container->session_id;
    elem_out->session_tv_sec  = container->session_tv_sec;
    elem_out->session_tv_nsec = container->session_tv_nsec;
    elem_out->ack_num         = 0;
    elem_out->flags           = ntohs(flags_nbo);
    elem_out->payload_len     = (uint16_t)payload_len;
    /* ゼロコピー: コンテナのペイロード領域を直接指す。
       コンテナの生存期間中 (呼び出し元の処理完了まで) のみ有効。 */
    elem_out->payload = p + POTR_PAYLOAD_ELEM_HDR_SIZE;

    *offset += POTR_PAYLOAD_ELEM_HDR_SIZE + payload_len;
    return POTR_SUCCESS;
}

/**
 *******************************************************************************
 *  @brief          受信バイト列をパケット構造体に解析します。
 *  @param[out]     packet      解析結果を格納するパケット構造体へのポインタ。
 *  @param[in]      buf         受信バイト列へのポインタ。
 *  @param[in]      buf_len     受信バイト列の長さ。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *
 *  @details
 *  各フィールドをホストバイトオーダーに変換して構造体に格納します。
 *******************************************************************************
 */
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

    memcpy(&tmp64, b +  0, 8); packet->service_id      = (int64_t)ntoh64(tmp64);
    memcpy(&tmp64, b +  8, 8); packet->session_tv_sec  = (int64_t)ntoh64(tmp64);
    memcpy(&tmp32, b + 16, 4); packet->session_id      = ntohl(tmp32);
    memcpy(&tmp32, b + 20, 4); packet->session_tv_nsec = (int32_t)ntohl(tmp32);
    memcpy(&tmp32, b + 24, 4); packet->seq_num         = ntohl(tmp32);
    memcpy(&tmp32, b + 28, 4); packet->ack_num         = ntohl(tmp32);
    memcpy(&tmp16, b + 32, 2); packet->flags           = ntohs(tmp16);
    memcpy(&tmp16, b + 34, 2); packet->payload_len     = ntohs(tmp16);

    if (packet->payload_len > POTR_MAX_PAYLOAD
        || (size_t)packet->payload_len + PACKET_HEADER_SIZE > buf_len)
    {
        return POTR_ERROR;
    }

    /* ゼロコピー: 受信バッファ内のペイロード領域を直接指す。
       呼び出し元バッファ (recv_buf) の生存期間中のみ有効。 */
    packet->payload = b + PACKET_HEADER_SIZE;

    return POTR_SUCCESS;
}

/**
 *******************************************************************************
 *  @brief          パケットのヘッダー + ペイロードの合計バイト数を返します。
 *  @param[in]      packet  対象のパケット構造体へのポインタ。
 *                          packet_build_*() で構築した NBO パケットを渡すこと。
 *                          packet_parse() 済み (ホストバイトオーダー) のパケットを
 *                          渡すと payload_len の ntohs 変換が二重になり誤値を返す。
 *  @return         パケットの送信サイズ (バイト)。packet が NULL の場合は 0。
 *
 *  @details
 *  UDP 送信時に sendto() へ渡すバイト数を求めるために使用します。\n
 *  内部で ntohs(packet->payload_len) を呼ぶため、引数は必ず NBO 状態で渡してください。
 *******************************************************************************
 */
size_t packet_wire_size(const PotrPacket *packet)
{
    if (packet == NULL)
    {
        return 0;
    }

    return PACKET_HEADER_SIZE + ntohs(packet->payload_len);
}
