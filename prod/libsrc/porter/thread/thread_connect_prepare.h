/**
 *******************************************************************************
 *  @file           thread_connect_prepare.h
 *  @brief          TCP 接続成立前の読み取り・照合と接続待機を実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/23
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef THREAD_CONNECT_PREPARE_PRIVATE_H
#define THREAD_CONNECT_PREPARE_PRIVATE_H

#include <porter/potr_context.h>
#include <porter/protocol/packet.h>

/* 成否ではなくセッションの新旧を分類する比較結果。共通結果コードの適用対象外。 */
#define TCP_SESSION_NEW  (1)
#define TCP_SESSION_SAME (0)
#define TCP_SESSION_OLD  (-1)

/**
 *  @brief          最初の TCP パケットを読み取ります。
 *  @param[in]      fd              接続済みソケット。所有権は移動しません。
 *  @param[out]     buf             ヘッダー以上の容量を持つバッファー。NULL は許可しません。
 *  @param[in]      max_buf         バッファー容量。ヘッダー以上を指定します。
 *  @param[out]     len_out         成功時の受信長。NULL は許可しません。失敗時は変更しません。
 *  @param[in]      timeout_ms      既存の接続管理が計算した読み取り待機時間。
 *  @return         成功時 POTR_OK、待機期限は POTR_ERR_TIMEOUT、長さ超過は POTR_ERR_PROTOCOL。
 *                  EOF・I/O 失敗は既存のソケット結果変換に従います。
 *  @par            スレッド セーフ
 *                  同じソケットとバッファーへの並行操作を避けて呼びます。
 */
int thread_connect_read_first_packet(cplat_socket fd, uint8_t *buf, size_t max_buf, size_t *len_out, int timeout_ms);

/**
 *  @brief          受信セッションの新旧を比較します。
 *  @param[in]      ctx             採用済みセッション。NULL は許可しません。
 *  @param[in]      pkt             解析・認証済みパケット。NULL は許可しません。
 *  @return         TCP_SESSION_NEW、TCP_SESSION_SAME または TCP_SESSION_OLD。
 *  @note           成否ではなく分類を返すため、共通結果コードの適用対象外です。
 *  @par            スレッド セーフ
 *                  呼び出し元が session_establish_mutex と tcp_recv_mutex をこの順で取得済みとします。
 *                  共有状態を更新しません。
 */
int thread_connect_session_compare(const potr_context *ctx, const potr_packet *pkt);

/**
 *  @brief          接続期限と停止要求を監視して TCP 接続します。
 *  @param[in,out]  ctx             設定と停止フラグ。NULL は許可しません。接続は登録しません。
 *  @param[in]      path_idx        有効なパス番号。
 *  @return         成功時は呼び出し元が所有する接続ソケット、失敗時は CPLAT_INVALID_SOCKET。
 *  @note           失敗時は新規ソケットを閉じます。ハンドルを返すため共通結果コードの適用対象外です。
 *  @par            スレッド セーフ
 *                  設定を固定したコンテキストの接続管理から呼びます。停止フラグは atomic で参照します。
 */
cplat_socket thread_connect_with_timeout(potr_context *ctx, int path_idx);

/**
 *  @brief          接続元アドレスとポートを照合します。
 *  @param[in]      ctx             フィルター設定。NULL は許可しません。
 *  @param[in]      path_idx        有効なパス番号。
 *  @param[in]      peer_addr       接続元。NULL は許可しません。
 *  @return         採用可能なら 1、不一致なら 0。成否ではない述語のため共通結果コードの適用対象外です。
 *  @par            スレッド セーフ
 *                  設定と接続元を変更しない呼び出しは並行実行できます。
 */
int thread_connect_peer_allowed(const potr_context *ctx, int path_idx, const cplat_ipv4_endpoint *peer_addr);

#endif
