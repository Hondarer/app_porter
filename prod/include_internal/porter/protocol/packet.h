/**
 *******************************************************************************
 *  @file           packet.h
 *  @brief          パケットを構築および解析する内部 API を提供します。
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

/*
 * このヘッダーは多数のソース ファイルから参照されるため、
 * @hideincludedbygraph によって "Included by" グラフを無効にする。
 */

#ifndef POTR_PROTOCOL_PACKET_H
#define POTR_PROTOCOL_PACKET_H

#include <stddef.h>
#include <stdint.h>

#include <cplat/clock/timespec.h>
#include <porter/porter_const.h>
#include <porter/porter_result.h>
#include <porter/porter_type.h>

/** パケット ヘッダーの固定長 [byte]。payload の開始オフセット。POTR_PACKET_HEADER_WIRE_SIZE と一致します。 */
#define POTR_PACKET_HEADER_SIZE ((size_t)offsetof(potr_packet, payload))

/** potr_packet のワイヤー フォーマットで payload_len が始まる位置 [byte]。 */
#define POTR_PACKET_PAYLOAD_LEN_OFFSET 34U

/**
 * フロー制御パケット (抑制の要求、解除、上限なし) のペイロード長 [byte]。
 * 先頭 1 [byte] が状態 (POTR_FLOW_CONTROL_STATE_*)、続く 4 [byte] が送出上限 (uint32、ネットワーク バイト オーダー)。
 */
#define POTR_FLOW_CONTROL_PAYLOAD_SIZE 5U

/**
 * フロー制御パケット (問い合わせ: POTR_FLOW_CONTROL_STATE_QUERY) のペイロード長 [byte]。
 * 状態 1 [byte] のみ。
 */
#define POTR_FLOW_CONTROL_QUERY_PAYLOAD_SIZE 1U

/** 監視対象の一覧のペイロードで、受信者のセッション識別子の前に置く部分の長さ [byte] (状態 1 [byte] と台数 1 [byte])。 */
#define POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE 2U

/**
 * 監視対象の一覧のペイロードで、受信者 1 台分のセッション識別子の長さ [byte]。
 * ヘッダーと同じ並び (session_tv_sec 8 [byte]、session_id 4 [byte]、session_tv_nsec 4 [byte]、ネットワーク バイト オーダー)。
 */
#define POTR_FLOW_CONTROL_MONITOR_ENTRY_SIZE 16U

/** 監視対象の一覧のペイロード長の上限 [byte]。 */
#define POTR_FLOW_CONTROL_MONITOR_PAYLOAD_MAX \
    (POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE + POTR_FLOW_CONTROL_MONITOR_ENTRY_SIZE * POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX)

/**
 *  @brief  パケットに付与するセッション識別情報。
 *
 *  potr_service_open 時に決定し、全パケットのヘッダーに格納します。\n
 *  session_tv_sec / session_tv_nsec はワイヤ フォーマット (session header) の鏡像であり、
 *  幅が仕様で決まっているため固定幅型を維持します。\n
 *  cplat_timespec との変換には session_ts_to_hdr() / session_ts_from_hdr() を使用します。
 */
typedef struct potr_internal_packet_session_hdr
{
    int64_t service_id;      /**< サービス識別子。 */
    int64_t session_tv_sec;  /**< セッション開始時刻 秒部。 */
    uint32_t session_id;     /**< セッション識別子 (乱数)。 */
    int32_t session_tv_nsec; /**< セッション開始時刻 ナノ秒部。 */
} potr_internal_packet_session_hdr;

/**
 *  @brief          cplat_timespec をワイヤ用の固定幅セッション時刻フィールドへ変換します。
 *  @param[in]      ts       変換元のセッション開始時刻。
 *  @param[out]     tv_sec   ワイヤ フィールド (int64_t) の格納先。
 *  @param[out]     tv_nsec  ワイヤ フィールド (int32_t) の格納先。
 *
 *  ワイヤ フィールドは幅が仕様で固定されているため、縮小変換を本関数に集約します。\n
 *  正規化済みの tv_nsec (0 以上 999,999,999 以下) は int32_t の表現範囲内に収まります。
 */
static inline void session_ts_to_hdr(const cplat_timespec *ts, int64_t *tv_sec, int32_t *tv_nsec)
{
    *tv_sec = (int64_t)ts->tv_sec;
    *tv_nsec = (int32_t)ts->tv_nsec;
}

/**
 *  @brief          ワイヤ用の固定幅セッション時刻フィールドを cplat_timespec へ変換します。
 *  @param[in]      tv_sec   ワイヤ フィールド (int64_t) の値。
 *  @param[in]      tv_nsec  ワイヤ フィールド (int32_t) の値。
 *  @param[out]     ts       変換結果の格納先。
 */
static inline void session_ts_from_hdr(int64_t tv_sec, int32_t tv_nsec, cplat_timespec *ts)
{
    ts->tv_sec = (time_t)tv_sec;
    ts->tv_nsec = (int64_t)tv_nsec;
}

/**
 *  @brief          開封済みのフロー制御パケットから、状態と送出上限を取り出します。
 *  @param[in]      packet      開封済みのフロー制御パケット (payload_len はホスト バイト オーダー)。NULL は許可しません。
 *  @param[out]     state       状態 (POTR_FLOW_CONTROL_STATE_*) の格納先。NULL は許可しません。
 *  @param[out]     send_limit  送出上限の格納先。状態が問い合わせまたは上限なしの場合は 0 を格納します。NULL は許可しません。
 *  @return         形式が正しい場合は POTR_OK、ペイロード長が状態に合わない場合 (問い合わせは 1 [byte]、
 *                  抑制の要求・解除・上限なしは 5 [byte])、状態が不正な場合 (監視対象の一覧を含む)、
 *                  上限なしで送出上限の欄が 0 でない場合、seq_num または ack_num が 0 でない場合は
 *                  POTR_ERR_PROTOCOL を返します。失敗時の出力は不定です。
 *
 *  送出上限はネットワーク バイト オーダーの 4 [byte] を、バイトの並びから組み立てます。
 */
static inline int flow_control_payload_decode(const potr_packet *packet, uint8_t *state, uint32_t *send_limit)
{
    uint32_t limit;

    if (packet->payload == NULL || packet->payload_len == 0U || packet->seq_num != 0U || packet->ack_num != 0U)
    {
        return POTR_ERR_PROTOCOL;
    }

    *state = packet->payload[0];
    if (*state == POTR_FLOW_CONTROL_STATE_QUERY)
    {
        if (packet->payload_len != POTR_FLOW_CONTROL_QUERY_PAYLOAD_SIZE)
        {
            return POTR_ERR_PROTOCOL;
        }

        *send_limit = 0U;
        return POTR_OK;
    }

    if (packet->payload_len != POTR_FLOW_CONTROL_PAYLOAD_SIZE)
    {
        return POTR_ERR_PROTOCOL;
    }

    if (*state != POTR_FLOW_CONTROL_STATE_SUPPRESS && *state != POTR_FLOW_CONTROL_STATE_RESUME &&
        *state != POTR_FLOW_CONTROL_STATE_UNLIMITED)
    {
        return POTR_ERR_PROTOCOL;
    }

    limit = ((uint32_t)packet->payload[1] << 24) | ((uint32_t)packet->payload[2] << 16) |
            ((uint32_t)packet->payload[3] << 8) | (uint32_t)packet->payload[4];
    if (*state == POTR_FLOW_CONTROL_STATE_UNLIMITED && limit != 0U)
    {
        return POTR_ERR_PROTOCOL;
    }

    *send_limit = limit;

    return POTR_OK;
}

/**
 *  @brief          開封済みの監視対象の一覧のパケットを検査し、台数を取り出します。
 *  @param[in]      packet  開封済みのフロー制御パケット (payload_len はホスト バイト オーダー)。NULL は許可しません。
 *  @param[out]     count   台数の格納先。NULL は許可しません。
 *  @return         形式が正しい場合は POTR_OK、状態が POTR_FLOW_CONTROL_STATE_MONITOR でない場合、
 *                  台数が 1 から POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX の範囲にない場合、
 *                  ペイロード長が「2 + 16 × 台数」でない場合、seq_num または ack_num が 0 でない場合は
 *                  POTR_ERR_PROTOCOL を返します。失敗時の出力は不定です。
 */
static inline int flow_control_monitor_decode(const potr_packet *packet, size_t *count)
{
    if (packet->payload == NULL || packet->payload_len < POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE ||
        packet->seq_num != 0U || packet->ack_num != 0U || packet->payload[0] != POTR_FLOW_CONTROL_STATE_MONITOR)
    {
        return POTR_ERR_PROTOCOL;
    }

    *count = (size_t)packet->payload[1];
    if (*count < 1U || *count > POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX ||
        (size_t)packet->payload_len !=
            POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE + POTR_FLOW_CONTROL_MONITOR_ENTRY_SIZE * *count)
    {
        return POTR_ERR_PROTOCOL;
    }

    return POTR_OK;
}

/**
 *  @brief          検査済みの監視対象の一覧から、index 番目の受信者のセッション識別子を取り出します。
 *  @param[in]      packet      flow_control_monitor_decode() で検査済みのパケット。NULL は許可しません。
 *  @param[in]      index       取り出す受信者の位置 (0 以上、台数未満)。
 *  @param[out]     session     セッション識別子の格納先 (service_id は変更しません)。NULL は許可しません。
 *
 *  ネットワーク バイト オーダーの値を、バイトの並びから組み立てます。
 */
static inline void flow_control_monitor_entry(const potr_packet *packet, size_t index,
                                              potr_internal_packet_session_hdr *session)
{
    const uint8_t *entry =
        packet->payload + POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE + POTR_FLOW_CONTROL_MONITOR_ENTRY_SIZE * index;
    uint64_t tv_sec = 0U;
    uint32_t tv_nsec = 0U;
    uint32_t session_id = 0U;
    size_t i;

    for (i = 0U; i < 8U; i++)
    {
        tv_sec = (tv_sec << 8) | (uint64_t)entry[i];
    }

    for (i = 8U; i < 12U; i++)
    {
        session_id = (session_id << 8) | (uint32_t)entry[i];
    }

    for (i = 12U; i < 16U; i++)
    {
        tv_nsec = (tv_nsec << 8) | (uint32_t)entry[i];
    }

    session->session_tv_sec = (int64_t)tv_sec;
    session->session_id = session_id;
    session->session_tv_nsec = (int32_t)tv_nsec;
}

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          NACK パケットを構築します。
     *  @param[out]     packet      構築結果を格納するパケット構造体へのポインター。
     *  @param[in]      shdr        セッション識別ヘッダーへのポインター。
     *  @param[in]      nack_num    再送要求する通番。
     *  @return         成功時は POTR_OK、引数が不正な場合は POTR_ERR_INVALID_ARGUMENT を返します。
     */
    extern int potr_internal_packet_build_nack(potr_packet *packet, const potr_internal_packet_session_hdr *shdr,
                                               uint32_t nack_num);

    /**
     *  @brief          フロー制御パケットを構築します。
     *  @param[out]     packet      構築結果を格納するパケット構造体へのポインター。
     *  @param[in]      shdr        送出する側自身のセッション識別ヘッダーへのポインター。
     *  @param[out]     payload     ペイロードの格納先 (問い合わせは 1 [byte] 以上、それ以外は POTR_FLOW_CONTROL_PAYLOAD_SIZE [byte])。
     *                              packet->payload はこの領域を指すため、封緘が終わるまで有効であること。
     *  @param[in]      state       状態 (POTR_FLOW_CONTROL_STATE_SUPPRESS、POTR_FLOW_CONTROL_STATE_RESUME、
     *                              POTR_FLOW_CONTROL_STATE_QUERY、または POTR_FLOW_CONTROL_STATE_UNLIMITED)。
     *  @param[in]      send_limit  送出上限 (送信側が送出してよい DATA の通番の上限。この通番以上を送出しない)。
     *                              状態が問い合わせまたは上限なしの場合は 0 を指定すること。
     *  @return         成功時は POTR_OK、引数または状態の値が不正な場合、問い合わせまたは上限なしで send_limit が
     *                  0 でない場合は POTR_ERR_INVALID_ARGUMENT を返します。
     *
     *  通番を消費せず、ウィンドウに登録しません (NACK・再送の対象外)。seq_num と ack_num は常に 0 です。\n
     *  状態が問い合わせの場合、ペイロードは状態 1 [byte] のみです。それ以外の場合、ペイロードは先頭 1 [byte] が状態、
     *  続く 4 [byte] がネットワーク バイト オーダーの送出上限です。
     */
    extern int potr_internal_packet_build_flow_control(potr_packet *packet,
                                                       const potr_internal_packet_session_hdr *shdr,
                                                       uint8_t *payload, uint8_t state, uint32_t send_limit);

    /**
     *  @brief          監視対象の一覧のフロー制御パケットを構築します。
     *  @param[out]     packet      構築結果を格納するパケット構造体へのポインター。
     *  @param[in]      shdr        送出する側 (multicast と broadcast の送信者) 自身のセッション識別ヘッダーへのポインター。
     *  @param[out]     payload     ペイロードの格納先 (POTR_FLOW_CONTROL_MONITOR_PAYLOAD_MAX [byte] 以上)。
     *                              packet->payload はこの領域を指すため、封緘が終わるまで有効であること。
     *  @param[in]      receivers   監視対象の受信者のセッション識別子の配列 (service_id は使いません)。
     *  @param[in]      count       受信者の台数 (1 以上、POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX 以下)。
     *  @return         成功時は POTR_OK、引数が NULL の場合と台数が範囲にない場合は POTR_ERR_INVALID_ARGUMENT を返します。
     *
     *  ペイロードは、先頭 1 [byte] が状態 (POTR_FLOW_CONTROL_STATE_MONITOR)、次の 1 [byte] が台数、
     *  続いて台数分の受信者のセッション識別子 (1 台 16 [byte]、ヘッダーと同じ並びのネットワーク バイト オーダー) です。\n
     *  通番を消費せず、ウィンドウに登録しません (NACK・再送の対象外)。seq_num と ack_num は常に 0 です。
     */
    extern int potr_internal_packet_build_flow_control_list(potr_packet *packet,
                                                            const potr_internal_packet_session_hdr *shdr,
                                                            uint8_t *payload,
                                                            const potr_internal_packet_session_hdr *receivers,
                                                            size_t count);

    /**
     *  @brief          PING パケットを構築します。
     *  @param[out]     packet             構築結果を格納するパケット構造体へのポインター。
     *  @param[in]      shdr               セッション識別ヘッダーへのポインター。
     *  @param[in]      seq_num            通番 (ウィンドウ管理に使用)。
     *  @param[in]      health_payload     パス PING 受信状態ペイロードへのポインター (POTR_PING_STATE_* 値の配列)。
     *                                     NULL の場合はペイロードなし (payload_len=0)。
     *  @param[in]      health_payload_len ペイロード長 [byte]。通常 POTR_MAX_PATH。health_payload が NULL の場合は無視。
     *  @return         成功時は POTR_OK、引数が不正な場合は POTR_ERR_INVALID_ARGUMENT を返します。
     *
     *  ヘルス チェック パケットです。\n
     *  通番には送信側の next_seq (次に送出する DATA に割り当てる通番) を格納します。\n
     *  PING はウィンドウに登録されません (NACK・再送の対象外)。\n
     *  ack_num は常に 0。受信者は seq_num を上限として欠番を一括 NACK します。\n
     *  ペイロードには POTR_MAX_PATH バイトのパス受信状態 (POTR_PING_STATE_*) を格納します。
     */
    extern int potr_internal_packet_build_ping(potr_packet *packet, const potr_internal_packet_session_hdr *shdr,
                                               uint32_t seq_num, const uint8_t *health_payload,
                                               uint16_t health_payload_len);

    /**
     *  @brief          再送不能通知 (REJECT) パケットを構築します。
     *  @param[out]     packet      構築結果を格納するパケット構造体へのポインター。
     *  @param[in]      shdr        セッション識別ヘッダーへのポインター。
     *  @param[in]      seq_num     再送不能な通番。ack_num フィールドに格納します。
     *  @return         成功時は POTR_OK、引数が不正な場合は POTR_ERR_INVALID_ARGUMENT を返します。
     *
     *  受信者から NACK を受け取ったが、送信ウィンドウに該当パケットが存在しない場合に送信者が返すパケットです。\n
     *  受信者は通知された通番が自身の欠番に当たる場合に切断を通知して受信の状態を破棄し、次に届いたパケットから受信を再開します。\n
     *  FIN を保留中の場合は、即時に FIN を発火して終了します。
     */
    extern int potr_internal_packet_build_reject(potr_packet *packet, const potr_internal_packet_session_hdr *shdr,
                                                 uint32_t seq_num);

    /**
     *  @brief          正常終了通知 (FIN) パケットを構築します。
     *  @param[out]     packet      構築結果を格納するパケット構造体へのポインター。
     *  @param[in]      shdr        セッション識別ヘッダーへのポインター。
     *  @return         成功時は POTR_OK、引数が不正な場合は POTR_ERR_INVALID_ARGUMENT を返します。
     *
     *  送信者が potr_service_close 時に送出する終了通知パケットです。ペイロードなし。\n
     *  `POTR_FLAG_FIN_TARGET_VALID` と `ack_num` の設定は呼び出し側が行います。\n
     *  受信者は target 付き FIN の場合のみ `ack_num` を参照して
     *  受信ウィンドウ追い付き後まで DISCONNECTED を遅延できます。
     */
    extern int potr_internal_packet_build_fin(potr_packet *packet, const potr_internal_packet_session_hdr *shdr);

    /**
     *  @brief          FIN 完了応答 (FIN_ACK) パケットを構築します。
     *  @param[out]     packet          構築結果を格納するパケット構造体へのポインター。
     *  @param[in]      shdr            セッション識別ヘッダーへのポインター。
     *  @param[in]      fin_target_seq  完了した FIN target 通番。
     *  @return         成功時は POTR_OK、引数が不正な場合は POTR_ERR_INVALID_ARGUMENT を返します。
     */
    extern int potr_internal_packet_build_fin_ack(potr_packet *packet, const potr_internal_packet_session_hdr *shdr,
                                                  uint32_t fin_target_seq);

    /**
     *  @brief          データ パケット (パック コンテナー) を構築します。
     *  @param[out]     out             構築結果を格納するパケット構造体へのポインター。
     *  @param[in]      shdr            セッション識別ヘッダーへのポインター。
     *  @param[in]      seq_num         外側パケットの通番。再送・順序整列に使用します。
     *  @param[in]      packed_payload  送信スレッドが構築したペイロード エレメント列。
     *  @param[in]      payload_len     packed_payload のバイト数。
     *  @return         成功時は POTR_OK、引数が不正な場合は POTR_ERR_INVALID_ARGUMENT を返します。
     *
     *  すべてのデータ パケットはパック コンテナー形式で送受信します。\n
     *  ペイロード エレメントが 1 件のみの場合も同じ形式を使用します。\n
     *  再送・順序整列の単位は外側パケット (本関数が構築する UDP ペイロード) であり、
     *  通番は外側パケットの seq_num フィールドで管理します。\n
     *  ペイロード エレメントの形式は flags(2) + payload_len(4) + payload(N) です。\n
     *  受信者は POTR_FLAG_DATA を検出後 potr_internal_packet_unpack_next() でペイロード エレメントを展開します。
     */
    extern int potr_internal_packet_build_packed(potr_packet *out, const potr_internal_packet_session_hdr *shdr,
                                                 uint32_t seq_num, const void *packed_payload, size_t payload_len);

    /**
     *  @brief          データ パケットから次のペイロード エレメントを取り出します。
     *  @param[in]      container  potr_internal_packet_parse() 済みのデータ パケット (POTR_FLAG_DATA)。
     *  @param[in,out]  offset     コンテナー payload 内の読み取り位置。呼び出し毎に更新。
     *  @param[out]     elem_out   取り出したペイロード エレメントを格納する構造体へのポインター。
     *  @return         ペイロード エレメントを取り出せた場合は POTR_OK、引数が NULL の場合は
     *                  POTR_ERR_INVALID_ARGUMENT、末尾に達した場合は POTR_ERR_EOF、
     *                  エレメントが不正な場合は POTR_ERR_PROTOCOL を返します。
     *
     *  ペイロード エレメントの形式は flags(2) + payload_len(4) + payload(N) です。\n
     *  通番は外側パケットで管理するためペイロード エレメントには含まれません。\n
     *  container->payload_len はホスト バイト オーダー (potr_internal_packet_parse() 変換済み) で参照します。\n
     *  elem_out の session 情報は container から引き継ぎます。
     */
    extern int potr_internal_packet_unpack_next(const potr_packet *container, size_t *offset, potr_packet *elem_out);

    /**
     *  @brief          受信バイト列をパケット構造体に解析します。
     *  @param[out]     packet      解析結果を格納するパケット構造体へのポインター。
     *  @param[in]      buf         受信バイト列へのポインター。
     *  @param[in]      buf_len     受信バイト列の長さ。
     *  @return         成功時は POTR_OK、引数が NULL の場合は POTR_ERR_INVALID_ARGUMENT、
     *                  受信データが不正 (ヘッダー長未満、バージョン不一致、長さ不整合) な場合は
     *                  POTR_ERR_PROTOCOL を返します。
     *
     *  各フィールドをホスト バイト オーダーに変換して構造体に格納します。
     */
    extern int potr_internal_packet_parse(potr_packet *packet, const void *buf, size_t buf_len);

    /**
     *  @brief          パケットのヘッダー + ペイロードの合計バイト数を返します。
     *  @param[in]      packet  対象のパケット構造体へのポインター。
     *                          potr_internal_packet_build_*() で構築した NBO パケットを渡すこと。
     *                          potr_internal_packet_parse() 済み (ホスト バイト オーダー) のパケットを
     *                          渡すと payload_len の ntohs 変換が二重になり誤値を返します。
     *  @return         パケットの送信サイズ [byte]。packet が NULL の場合は 0。
     *
     *  UDP 送信時に sendto() へ渡すバイト数を求めるために使用します。\n
     *  内部で ntohs(packet->payload_len) を呼び出すため、引数は必ず NBO 状態で渡してください。
     */
    extern size_t potr_internal_packet_wire_size(const potr_packet *packet);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_PROTOCOL_PACKET_H */
