/**
 *******************************************************************************
 *  @file           potr_socket_io.h
 *  @brief          非ブロッキング ソケット通信と TCP 接続世代の制御ヘルパーを宣言します。
 *  @author         Tetsuo Honda
 *  @date           2026/11/24
 *  @version        1.0.0
 *
 *  cplat の解除できるソケット待機 (`cplat_socket_watch`、`cplat_socket_wait_cancel`) を用いた
 *  UDP/TCP 共通送信ヘルパー、TCP フレーム読み取り状態機械、TCP close の絶対期限管理、
 *  および TCP 接続世代の閉じる要求・解放処理を提供します。\n
 *  有効な解除オブジェクトへの `cplat_socket_wait_cancel_signal()` は常に `CPLAT_OK` を返し、
 *  OS 通知が失敗しても待機側の最大 50 [ms] の内部間隔で解除を検出します。\n
 *  Windows の登録 (`cplat_socket_watch`) は登録ごとに 3 つの手動リセット イベントとロックを持ち、
 *  内部スレッドを生成しません。\n
 *  送信可能の待機 (`cplat_socket_watch_wait_writable()`) が `CPLAT_OK` を返しても、
 *  切断やエラーの場合があるため送信成功とは扱わず、必ず実際の送信を再試行して結果を判定します。
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

#ifndef POTR_INFRA_SOCKET_IO_H
#define POTR_INFRA_SOCKET_IO_H

#include <stddef.h>
#include <stdint.h>

#include <cplat/base/error.h>
#include <cplat/net/endpoint.h>
#include <cplat/net/socket.h>
#include <porter/potr_context.h>

/** UDP 送信可能待機の絶対期限 [ms]。再試行しても延長しません。 */
#define POTR_UDP_SEND_TIMEOUT_MS 100U

/** TCP 送信可能待機の 1 回の待機上限 [ms]。後から公開される close 期限を検出するために用います。 */
#define POTR_TCP_SEND_WAIT_SLICE_MS 100U

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          TCP フレーム読み取りの中間状態を保持します。
     *
     *  非ブロッキング受信でヘッダーまたはペイロードの一部だけを受信した位置を保持し、
     *  `CPLAT_ERR_WOULD_BLOCK` で待機へ戻ってもフレームを途切れさせません。\n
     *  ヘッダーの完了直後にペイロード長の上限を検証します。
     */
    typedef struct potr_internal_tcp_frame_reader
    {
        size_t received;      /**< 現在のフレームで読み取り済みの総バイト数。 */
        size_t expected;      /**< 現在の段階の目標総バイト数 (ヘッダー段階はヘッダー長、本文段階はフレーム全長)。 */
        uint16_t payload_len; /**< ヘッダー完了後に抽出したペイロード長 [byte]。 */
        uint16_t header_done; /**< ヘッダー読み取り完了フラグ (1: 完了, 0: 未完了)。 */
        uint32_t _pad;        /**< パディング。 */
    } potr_internal_tcp_frame_reader;

    /**
     *  @brief          TCP close の絶対期限を公開します (未公開の場合のみ)。
     *  @param[in,out]  ctx セッション コンテキスト。NULL は無処理。
     *
     *  単調時刻に `tcp_close_timeout_ms` を加えた絶対期限を一度だけ計算して公開します。\n
     *  すでに公開済みの場合は上書きせず、各段階へ同じ期限を引き継ぎます。
     */
    extern void potr_internal_tcp_close_deadline_publish(potr_context *ctx);

    /**
     *  @brief          TCP close の絶対期限までの残り時間 [ms] を取得します。
     *  @param[in]      ctx              セッション コンテキスト。NULL は不可。
     *  @param[out]     has_deadline_out 期限が公開済みなら 1、未公開なら 0。NULL 可。
     *  @param[out]     remaining_ms_out 残り時間 [ms]。未公開時は CPLAT_WAIT_FOREVER、期限超過時は 0。NULL 可。
     *  @retval         POTR_OK          期限未公開、または期限内です。
     *  @retval         POTR_ERR_TIMEOUT 期限を超過しました。
     *  @retval         POTR_ERR_IO      単調時刻の取得に失敗しました。
     */
    extern int potr_internal_tcp_close_remaining_ms(const potr_context *ctx, int *has_deadline_out,
                                                    int *remaining_ms_out);

    /**
     *  @brief          指定経路の TCP 接続世代へ閉じる要求を発行します。
     *  @param[in,out]  ctx        セッション コンテキスト。NULL は無処理。
     *  @param[in]      path_idx   パス インデックス。範囲外は無処理。
     *  @param[in]      generation 対象の世代番号。0 を指定した場合は公開中の任意の世代を対象とします。
     *
     *  `tcp_lifecycle_mutex[path_idx]` の下で対象世代を確認し、閉じる途中の状態と `running[path_idx] = 0`
     *  を設定してから、その世代の `tcp_conn_cancel[path_idx]` へ解除を通知します。\n
     *  `tcp_send_mutex[path_idx]` を取得せず、ソケットの shutdown / detach / close を行いません。\n
     *  重複要求や古い世代への要求は無害に無視します。
     *
     *  @par            スレッド セーフ
     *  受信、送信、ヘルス チェック、サービス終了のどのスレッドからも呼び出せます。
     */
    extern void potr_internal_tcp_conn_request_close(potr_context *ctx, int path_idx, uint64_t generation);

    /**
     *  @brief          全経路の公開中 TCP 接続世代へ閉じる要求を発行します。
     *  @param[in,out]  ctx セッション コンテキスト。NULL は無処理。
     */
    extern void potr_internal_tcp_conn_request_close_all(potr_context *ctx);

    /**
     *  @brief          指定経路の TCP 接続世代を公開解除し、watch の detach、socket の close、cancel の破棄を行います。
     *  @param[in,out]  ctx        セッション コンテキスト。NULL は無処理。
     *  @param[in]      path_idx   パス インデックス。範囲外は無処理。
     *  @param[in]      generation 解放する世代番号。0 は無処理。
     *
     *  `tcp_send_mutex[path_idx]`、`tcp_lifecycle_mutex[path_idx]` の順で取得して送信側の使用完了を確定し、
     *  公開ポインターを無効化してから detach、close、cancel の破棄を行います。
     *
     *  @pre            経路 `path_idx` の接続管理スレッドだけが、当該世代の受信スレッドとヘルス チェック スレッドの
     *                  終了待ち完了後に呼び出します。
     */
    extern void potr_internal_tcp_conn_release(potr_context *ctx, int path_idx, uint64_t generation);

    /**
     *  @brief          登録済み UDP ソケットへデータグラムを全量送信します。
     *  @param[in,out]  ctx        セッション コンテキスト。
     *  @param[in]      path_idx   送信経路インデックス。
     *  @param[in]      buf        送信データ。
     *  @param[in]      len        送信データのバイト数。
     *  @param[in]      endpoint   送信先エンドポイント。
     *  @param[out]     detail_out 失敗時の詳細エラー格納先。NULL 可。
     *  @retval         POTR_OK                 全量の送信に成功しました。
     *  @retval         POTR_ERR_TIMEOUT        100 [ms] の絶対期限内に送信できませんでした。
     *  @retval         POTR_ERR_CANCELED       `recv_cancel` により待機が解除されました。
     *  @retval         POTR_ERR_IO             短い送信、または OS の送信失敗が発生しました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT 引数が不正です。
     *
     *  `udp_send_mutex[path_idx]` で送信と送信可能待機を直列化します。\n
     *  `CPLAT_ERR_WOULD_BLOCK` の場合は最初の試行から 100 [ms] の絶対期限内で待機と再試行を繰り返し、
     *  期限を延長しません。短い送信は `POTR_ERR_IO` とし、残りを別データグラムとして送りません。
     */
    extern int potr_internal_udp_sendto(potr_context *ctx, int path_idx, const void *buf, size_t len,
                                        const cplat_ipv4_endpoint *endpoint, cplat_error *detail_out);

    /**
     *  @brief          送信用ロック保持下で、指定経路の TCP 接続へ全量送信します。
     *  @param[in,out]  ctx        セッション コンテキスト。
     *  @param[in]      path_idx   送信経路インデックス。
     *  @param[in]      buf        送信データ。
     *  @param[in]      len        送信データのバイト数。
     *  @param[out]     sent_out   送信済みバイト数の格納先 (失敗時も途中送信数を格納)。NULL 可。
     *  @param[out]     detail_out 失敗時の詳細エラー格納先。NULL 可。
     *  @retval         POTR_OK               全量を送信しました。
     *  @retval         POTR_ERR_DISCONNECTED 未接続または閉じる途中です。
     *  @retval         POTR_ERR_CANCELED     世代の解除通知を受けました。
     *  @retval         POTR_ERR_TIMEOUT      TCP close の絶対期限を超過しました。
     *  @retval         POTR_ERR_IO           OS の送信失敗が発生しました。
     *
     *  各反復で世代の停止状態と close の絶対期限を確認し、1 回の送信可能待機は最大 100 [ms] とします。\n
     *  送信失敗・解除・期限超過時は、対象世代へ `potr_internal_tcp_conn_request_close()` を発行します。
     *
     *  @pre            呼び出し元が `tcp_send_mutex[path_idx]` を保持していること。
     */
    extern int potr_internal_tcp_send_all_locked(potr_context *ctx, int path_idx, const void *buf, size_t len,
                                                 size_t *sent_out, cplat_error *detail_out);

    /**
     *  @brief          `tcp_send_mutex[path_idx]` を取得して指定経路の TCP 接続へ全量送信します。
     *  @param[in,out]  ctx        セッション コンテキスト。
     *  @param[in]      path_idx   送信経路インデックス。
     *  @param[in]      buf        送信データ。
     *  @param[in]      len        送信データのバイト数。
     *  @param[out]     sent_out   送信済みバイト数の格納先。NULL 可。
     *  @param[out]     detail_out 失敗時の詳細エラー格納先。NULL 可。
     *  @return         `potr_internal_tcp_send_all_locked()` と同じ結果コードを返します。
     */
    extern int potr_internal_tcp_send_all(potr_context *ctx, int path_idx, const void *buf, size_t len,
                                          size_t *sent_out, cplat_error *detail_out);

    /**
     *  @brief          指定経路の TCP 接続に送信バッファーの空きがあるかを無待機で確認します。
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx 確認する経路インデックス。
     *  @retval         POTR_OK               送信可能 (または続く送信で結果を判定すべき状態) です。
     *  @retval         POTR_ERR_TIMEOUT      送信バッファーに空きがありません。
     *  @retval         POTR_ERR_CANCELED     解除が通知されています。
     *  @retval         POTR_ERR_DISCONNECTED 未接続または閉じる途中です。
     *
     *  `tcp_send_mutex[path_idx]` の下で `cplat_socket_watch_wait_writable(..., CPLAT_NO_WAIT, ...)` を呼び出します。
     */
    extern int potr_internal_tcp_check_writable(potr_context *ctx, int path_idx);

    /**
     *  @brief          TCP フレーム読み取り状態を初期化します。
     *  @param[out]     reader 初期化する読み取り状態。NULL は無処理。
     */
    extern void potr_internal_tcp_frame_reader_reset(potr_internal_tcp_frame_reader *reader);

    /**
     *  @brief          登録済み TCP ソケットから単発受信を行い、フレーム読み取り状態を 1 段階進めます。
     *  @param[in,out]  reader       フレーム読み取り状態。NULL は不可。
     *  @param[in]      fd           対象の接続済みソケット。
     *  @param[in,out]  buf          フレーム格納バッファー (ヘッダー + ペイロード)。NULL は不可。
     *  @param[in]      max_buf      `buf` の容量 [byte]。`POTR_PACKET_HEADER_SIZE` 以上であること。
     *  @param[out]     complete_out 1 フレーム読み取り完了なら 1、継続中なら 0。NULL は不可。
     *  @param[out]     detail_out   失敗時の詳細エラー格納先。NULL 可。
     *  @retval         POTR_OK               受信を進めたか、`CPLAT_ERR_WOULD_BLOCK` で待機へ戻る状態です。
     *  @retval         POTR_ERR_EOF          相手が切断しました。
     *  @retval         POTR_ERR_PROTOCOL     ペイロード長が `max_buf` を超えています。
     *  @retval         POTR_ERR_IO           OS の受信失敗が発生しました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT 引数が不正です。
     */
    extern int potr_internal_tcp_frame_reader_step(potr_internal_tcp_frame_reader *reader, cplat_socket fd,
                                                   uint8_t *buf, size_t max_buf, int *complete_out,
                                                   cplat_error *detail_out);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_INFRA_SOCKET_IO_H */
