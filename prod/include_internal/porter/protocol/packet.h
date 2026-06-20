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
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/* NOTE: このヘッダーは多数のソース ファイルから参照されるため、            */
/*       @hideincludedbygraph によって "Included by" グラフを無効にします。 */

#ifndef PACKET_H
#define PACKET_H

#include <stddef.h>
#include <stdint.h>

#include <porter/porter_type.h>

/** パケット ヘッダーの固定長 (バイト)。payload フィールドの開始オフセット。 */
#define PACKET_HEADER_SIZE ((size_t)offsetof(PotrPacket, payload))

/**
 *  @brief  パケットに付与するセッション識別情報。
 *
 *  potrOpenService 時に決定し、全パケットのヘッダーに格納します。
 */
typedef struct PotrPacketSessionHdr
{
    int64_t service_id;      /**< サービス識別子。 */
    int64_t session_tv_sec;  /**< セッション開始時刻 秒部。 */
    uint32_t session_id;     /**< セッション識別子 (乱数)。 */
    int32_t session_tv_nsec; /**< セッション開始時刻 ナノ秒部。 */
} PotrPacketSessionHdr;

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          NACK パケットを構築します。
     *  @param[out]     packet      構築結果を格納するパケット構造体へのポインター。
     *  @param[in]      shdr        セッション識別ヘッダーへのポインター。
     *  @param[in]      nack_num    再送要求する通番。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
     */
    extern int packet_build_nack(PotrPacket *packet, const PotrPacketSessionHdr *shdr, uint32_t nack_num);

    /**
     *  @brief          PING パケットを構築します。
     *  @param[out]     packet             構築結果を格納するパケット構造体へのポインター。
     *  @param[in]      shdr               セッション識別ヘッダーへのポインター。
     *  @param[in]      seq_num            通番 (ウィンドウ管理に使用)。
     *  @param[in]      health_payload     パス PING 受信状態ペイロードへのポインター (POTR_PING_STATE_* 値の配列)。
     *                                     NULL の場合はペイロードなし (payload_len=0)。
     *  @param[in]      health_payload_len ペイロード長 (バイト)。通常 POTR_MAX_PATH。health_payload が NULL の場合は無視。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
     *
     *  ヘルスチェック パケットです。\n
     *  通番には送信側の next_seq (次に送出する DATA に割り当てる通番) を格納します。\n
     *  PING はウィンドウに登録されません (NACK・再送の対象外)。\n
     *  ack_num は常に 0。受信者は seq_num を上限として欠番を一括 NACK します。\n
     *  ペイロードには POTR_MAX_PATH バイトのパス受信状態 (POTR_PING_STATE_*) を格納します。
     */
    extern int packet_build_ping(PotrPacket *packet, const PotrPacketSessionHdr *shdr, uint32_t seq_num,
                                 const uint8_t *health_payload, uint16_t health_payload_len);

    /**
     *  @brief          再送不能通知 (REJECT) パケットを構築します。
     *  @param[out]     packet      構築結果を格納するパケット構造体へのポインター。
     *  @param[in]      shdr        セッション識別ヘッダーへのポインター。
     *  @param[in]      seq_num     再送不能な通番。ack_num フィールドに格納します。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
     *
     *  受信者から NACK を受け取ったが、送信ウィンドウに該当パケットが存在しない場合に
     *  送信者が返すパケットです。受信者はこのパケットを受け取ると即時 DISCONNECTED を
     *  発火し、欠落通番をスキップして後続パケットの配信を継続します。
     */
    extern int packet_build_reject(PotrPacket *packet, const PotrPacketSessionHdr *shdr, uint32_t seq_num);

    /**
     *  @brief          正常終了通知 (FIN) パケットを構築します。
     *  @param[out]     packet      構築結果を格納するパケット構造体へのポインター。
     *  @param[in]      shdr        セッション識別ヘッダーへのポインター。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
     *
     *  送信者が potrCloseService 時に送出する終了通知パケットです。ペイロードなし。\n
     *  `POTR_FLAG_FIN_TARGET_VALID` と `ack_num` の設定は呼び出し側が行います。\n
     *  受信者は target 付き FIN の場合のみ `ack_num` を参照して
     *  受信ウィンドウ追い付き後まで DISCONNECTED を遅延できます。
     */
    extern int packet_build_fin(PotrPacket *packet, const PotrPacketSessionHdr *shdr);

    /**
     *  @brief          FIN 完了応答 (FIN_ACK) パケットを構築します。
     *  @param[out]     packet          構築結果を格納するパケット構造体へのポインター。
     *  @param[in]      shdr            セッション識別ヘッダーへのポインター。
     *  @param[in]      fin_target_seq  完了した FIN target 通番。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
     */
    extern int packet_build_fin_ack(PotrPacket *packet, const PotrPacketSessionHdr *shdr, uint32_t fin_target_seq);

    /**
     *  @brief          データ パケット (パック コンテナー) を構築します。
     *  @param[out]     out             構築結果を格納するパケット構造体へのポインター。
     *  @param[in]      shdr            セッション識別ヘッダーへのポインター。
     *  @param[in]      seq_num         外側パケットの通番。再送・順序整列に使用します。
     *  @param[in]      packed_payload  送信スレッドが構築したペイロード エレメント列。
     *  @param[in]      payload_len     packed_payload のバイト数。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
     *
     *  すべてのデータ パケットはパック コンテナー形式で送受信します。\n
     *  ペイロード エレメントが 1 件のみの場合も同じ形式を使用します。\n
     *  再送・順序整列の単位は外側パケット (本関数が構築する UDP ペイロード) であり、
     *  通番は外側パケットの seq_num フィールドで管理します。\n
     *  ペイロード エレメントの形式は flags(2) + payload_len(4) + payload(N) です。\n
     *  受信者は POTR_FLAG_DATA を検出後 packet_unpack_next() でペイロード エレメントを展開します。
     */
    extern int packet_build_packed(PotrPacket *out, const PotrPacketSessionHdr *shdr, uint32_t seq_num,
                                   const void *packed_payload, size_t payload_len);

    /**
     *  @brief          データ パケットから次のペイロード エレメントを取り出します。
     *  @param[in]      container  packet_parse() 済みのデータ パケット (POTR_FLAG_DATA)。
     *  @param[in,out]  offset     コンテナー payload 内の読み取り位置。呼び出し毎に更新。
     *  @param[out]     elem_out   取り出したペイロード エレメントを格納する構造体へのポインター。
     *  @return         ペイロード エレメントを取り出せた場合は POTR_SUCCESS、
     *                  末尾に達した場合またはエラーの場合は POTR_ERROR を返します。
     *
     *  ペイロード エレメントの形式は flags(2) + payload_len(4) + payload(N) です。\n
     *  通番は外側パケットで管理するためペイロード エレメントには含まれません。\n
     *  container->payload_len はホスト バイト オーダー (packet_parse() 変換済み) で参照します。\n
     *  elem_out の session 情報は container から引き継ぎます。
     */
    extern int packet_unpack_next(const PotrPacket *container, size_t *offset, PotrPacket *elem_out);

    /**
     *  @brief          受信バイト列をパケット構造体に解析します。
     *  @param[out]     packet      解析結果を格納するパケット構造体へのポインター。
     *  @param[in]      buf         受信バイト列へのポインター。
     *  @param[in]      buf_len     受信バイト列の長さ。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
     *
     *  各フィールドをホスト バイト オーダーに変換して構造体に格納します。
     */
    extern int packet_parse(PotrPacket *packet, const void *buf, size_t buf_len);

    /**
     *  @brief          パケットのヘッダー + ペイロードの合計バイト数を返します。
     *  @param[in]      packet  対象のパケット構造体へのポインター。
     *                          packet_build_*() で構築した NBO パケットを渡すこと。
     *                          packet_parse() 済み (ホスト バイト オーダー) のパケットを
     *                          渡すと payload_len の ntohs 変換が二重になり誤値を返します。
     *  @return         パケットの送信サイズ (バイト)。packet が NULL の場合は 0。
     *
     *  UDP 送信時に sendto() へ渡すバイト数を求めるために使用します。\n
     *  内部で ntohs(packet->payload_len) を呼ぶため、引数は必ず NBO 状態で渡してください。
     */
    extern size_t packet_wire_size(const PotrPacket *packet);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* PACKET_H */
