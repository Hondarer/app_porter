#ifndef THREAD_RECV_VALIDATE_PRIVATE_H
#define THREAD_RECV_VALIDATE_PRIVATE_H

#include <porter/potr_context.h>
#include <cplat/net/endpoint.h>

/**
 * @brief 暗号化要件と GCM 認証を検証します。
 * @param[in,out] ctx 復号バッファーを所有するコンテキスト。NULL は許可しません。
 * @param[in,out] pkt 解析済みパケット。復号した DATA/PING は ctx の復号バッファーを参照します。
 * @param[in] wire_hdr PACKET_HEADER_SIZE バイト以上の受信ヘッダー。
 * @param[in] log_prefix ログの接頭辞。
 * @param[in] path_idx TCP の経路番号。UDP は -1 を指定します。
 * @return 成功時は POTR_OK、認証失敗時は POTR_ERR_PROTOCOL を返します。
 * @note ポインター引数に NULL は許可しません。所有権は移動しません。
 *       同じ ctx の復号バッファーを並行して使用しないでください。
 */
int thread_recv_authenticate_packet(potr_context *ctx, potr_packet *pkt, const uint8_t *wire_hdr,
                                    const char *log_prefix, int path_idx);

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
