/**
 *******************************************************************************
 *  @file           potrTcpControl.h
 *  @brief          TCP 制御パケットの送信ヘルパーを定義します。
 *  @author         Tetsuo Honda
 *  @date           2026/07/07
 *  @version        1.0.0
 *
 *  ペイロードなし制御パケット (FIN / FIN_ACK など) を TCP 接続の全パスへ
 *  送信する内部ヘルパーを提供します。\n
 *  ライブラリ外部には公開しません。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef POTR_TCP_CONTROL_H
#define POTR_TCP_CONTROL_H

#include <stdint.h>

#include <porter/porter_type.h>
#include <porter/protocol/packet.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          TCP 制御パケットをアクティブな全パスへ送信します。
     *  @param[in]      ctx        セッション コンテキスト。
     *  @param[in,out]  pkt        送信する制御パケット。暗号化有効時は flags と payload_len を書き換えます。
     *  @param[in]      nonce_val  暗号化ノンスの seq_or_ack_num 部に使用する値 (ホスト バイト オーダー)。
     *  @retval         POTR_SUCCESS  1 パス以上への送信に成功。
     *  @retval         POTR_ERROR    引数不正、暗号化失敗、または全パスで送信失敗。
     *
     *  制御パケットをヘッダーのみの wire 形式に直列化し、tcp_conn_fd が有効な全パスへ
     *  path 単位の送信ミューテックスを取得して送信します。\n
     *  暗号化有効時はヘッダー全体を AAD とする GCM 認証タグのみを付加します。
     */
    int potr_tcp_send_control_packet(const PotrContext *ctx, PotrPacket *pkt, uint32_t nonce_val);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_TCP_CONTROL_H */
