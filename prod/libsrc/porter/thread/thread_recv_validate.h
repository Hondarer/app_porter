/**
 *******************************************************************************
 *  @file           thread_recv_validate.h
 *  @brief          受信パケットの検証と事前チェック処理の内部関数を定義します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef THREAD_RECV_VALIDATE_PRIVATE_H
#define THREAD_RECV_VALIDATE_PRIVATE_H

#include <porter/potr_context.h>
#include <cplat/net/endpoint.h>

/**
 *  @brief          暗号化要件と GCM 認証を検証します。
 *  @param[in,out]  ctx             復号バッファーを所有するコンテキスト。NULL は許可しません。
 *  @param[in,out]  pkt             解析済みパケット。復号した DATA/PING は ctx の復号バッファーを参照します。
 *  @param[in]      wire_hdr        POTR_PACKET_HEADER_SIZE バイト以上の受信ヘッダー。
 *  @param[in]      log_prefix      ログの接頭辞。
 *  @param[in]      path_idx        TCP の経路番号。UDP は -1 を指定します。
 *  @return         成功時は POTR_OK、受信形式不正または認証失敗時は POTR_ERR_PROTOCOL を返します。
 *                  メモリ不足は POTR_ERR_OUT_OF_MEMORY、ローカルの引数・容量・範囲の不整合は
 *                  POTR_ERR_INVALID_ARGUMENT、その他の内部失敗は POTR_ERR_UNKNOWN です。
 *                  証明書方式で鍵が未確立、または相手セッションが一致しない場合は POTR_ERR_DISCONNECTED。
 *  @note           証明書方式は平文を拒否し、確立した受信鍵またはグループ鍵で開封します。
 *                  HANDSHAKE と KEY は専用の処理で消費し、本関数では拒否します。
 *                  ポインター引数に NULL は許可しません。所有権は移動しません。
 *                  同じ ctx の復号バッファーを並行して使用しないでください。
 */
int thread_recv_authenticate_packet(potr_context *ctx, potr_packet *pkt, const uint8_t *wire_hdr,
                                    const char *log_prefix, int path_idx);

/**
 *  @brief          認証済みパケットをリプレイ窓で検査します。
 *  @param[in,out]  window 相手セッションの受信記録。NULL は許可しません。
 *  @param[in]      packet 認証済みの解析結果。NULL は許可しません。
 *  @param[in]      wire_header 受信ヘッダー 72 [byte]。NULL は許可しません。
 *  @return         受理時は POTR_OK、リプレイまたは窓外のときは POTR_ERR_PROTOCOL です。
 *  @note           平文は検査・記録しません。所有権は移動しません。
 *                  TCP は tcp_recv_mutex、N:1 は peers_mutex、UDP は単一受信スレッドで排他します。
 */
int thread_recv_replay_packet(potr_internal_replay_window *window, const potr_packet *packet,
                               const uint8_t *wire_header);

/**
 * @brief UDP パケットの送信元を設定済みの相手アドレスと照合します。
 * @param[in] ctx アドレス解決済みのコンテキスト。NULL は許可しません。
 * @param[in] sender 受信した送信元アドレス。NULL は許可しません。
 * @return 許可する場合は 1、拒否する場合は 0 を返します。
 * @note 採用可否を返す述語のため共通結果コードの適用対象外です。
 *       N:1 は設定された送信元ポートだけを照合します。
 *       1:1 は役割に応じた相手アドレスを照合し、未指定なら許可します。
 *       設定を変更しない限り並行して呼び出せます。
 */
int thread_recv_check_src_addr(const potr_context *ctx, const cplat_ipv4_endpoint *sender);

#endif
