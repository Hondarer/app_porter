/**
 *******************************************************************************
 *  @file           potr_health_thread.h
 *  @brief          ヘルス チェックを行うスレッドの内部インターフェイスを宣言します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/08
 *  @version        1.0.0
 *
 *  送信者が定周期で PING パケットを送信するスレッドの起動・停止 API。
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

#ifndef POTR_HEALTH_THREAD_H
#define POTR_HEALTH_THREAD_H

#include <porter/porter_result.h>
#include <porter/potr_context.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          非 TCP ヘルス チェック スレッドを起動します。
     *  @note           `health_mutex[0]` と `health_wakeup[0]` の所有権はサービス側にあり、
     *                  事前に生成された同期資源を使用して要求フラグを初期化してから起動します。
     *                  即時送信が必要な種別 (片方向 UDP 以外) だけ `health_send_pending[0]` を 1 にします。
     *                  起動に失敗した場合も共有の同期資源は破棄しません。
     *
     *  ctx->health_interval_ms が 0 の場合は起動しない (POTR_OK を返す)。
     *
     *  @param[in,out]  ctx セッション コンテキスト。
     *  @retval         POTR_OK                    起動に成功したか、起動が不要です。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx が NULL です。
     *  @retval         POTR_ERR_UNKNOWN           同期資源またはスレッドの生成に失敗しました。
     */
    extern int potr_internal_health_thread_start(potr_context *ctx);

    /**
     *  @brief          非 TCP ヘルス チェック スレッドを停止します。
     *  @note           `health_mutex[0]` の下で停止状態を設定して通知し、終了待ち後にハンドルを NULL へ戻します。
     *                  停止は保留中の送信要求 (`health_send_pending[0]`) より優先されます。
     *                  同期資源 (`health_mutex[0]`、`health_wakeup[0]`) は破棄せず、全起床側と全 health スレッドの
     *                  終了後に共通解放が破棄します。
     *                  終了待ちは失敗しないため、戻った時点でスレッドは終了しています。
     *                  未起動または停止済みの場合と、ctx が NULL の場合は何もしません。
     *  @param[in,out]  ctx セッション コンテキスト。
     */
    extern void potr_internal_health_thread_stop(potr_context *ctx);

    /**
     *  @brief          非 TCP ヘルス チェック スレッドへ PING 送信要求を記録して即時起床させます。
     *  @note           `health_mutex[0]` の下で運転中であることを確認し、`health_send_pending[0]` を 1 にして
     *                  条件変数へ通知します。待機前や送信中に届いた要求も保持され、次の送信へ反映されます。
     *  @param[in,out]  ctx セッション コンテキスト。
     */
    extern void potr_internal_health_thread_wake(potr_context *ctx);

    /**
     *  @brief          TCP PING パケットを即時送信します。
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス。
     *  @retval         POTR_OK                    送信に成功しました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx または path_idx が不正です。
     *  @retval         POTR_ERR_DISCONNECTED      送信可能なパスがありません。
     *  @retval         POTR_ERR_CANCELED          終了処理中です。
     *  @retval         POTR_ERR_IO                送信に失敗しました。
     *  @retval         POTR_ERR_UNKNOWN           暗号化に失敗しました。
     */
    extern int potr_internal_tcp_send_ping_now(potr_context *ctx, int path_idx);

    /**
     *  @brief          TCP の指定パスへ、未通知の経路状態があれば PING で書き込みます。
     *
     *  相手端は CONNECTED になる前に受信した DATA を破棄します。
     *  送信スレッドは DATA を書き込む前に本関数を呼び出し、同じストリームで DATA より先に
     *  最新の経路状態を相手端へ届けます。この接続で通知済みの経路状態が現在と同じ場合は書き込みません。
     *
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス。
     *  @retval         POTR_OK                    通知済み、または書き込みに成功しました。
     *  @retval         POTR_ERR_DISCONNECTED      パスが未接続です。
     *  @retval         POTR_ERR_IO                書き込みに失敗しました。
     *  @retval         POTR_ERR_UNKNOWN           暗号化に失敗しました。
     *  @pre            呼び出し元は send_window_mutex と tcp_send_mutex[path_idx] をこの順に保持します。
     */
    extern int potr_internal_tcp_announce_ping_state_locked(potr_context *ctx, int path_idx);

    /**
     *  @brief          TCP ヘルス チェック スレッドを path ごとに起動します。
     *
     *  ctx->health_interval_ms が 0 の場合は起動しない (POTR_OK を返す)。
     *
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス (0 〜 n_path-1)。
     *  @retval         POTR_OK                    起動に成功したか、起動が不要です。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  ctx または path_idx が不正です。
     *  @retval         POTR_ERR_CANCELED          終了処理中です。
     *  @retval         POTR_ERR_UNKNOWN           同期資源またはスレッドの生成に失敗しました。
     */
    extern int potr_internal_tcp_health_thread_start(potr_context *ctx, int path_idx);

    /**
     *  @brief          TCP ヘルス チェック スレッドを停止します。
     *  @note           終了待ち後にハンドルを NULL へ戻します。同期資源の所有権はサービスに残ります。
     *                  同じ path への起動・停止要求と同期し、並行する停止要求は終了待ちの完了を待ちます。
     *                  終了待ちは失敗しないため、戻った時点でスレッドは終了しています。
     *                  未起動または停止済みの場合と、ctx が NULL または path_idx が範囲外の場合は何もしません。
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス (0 〜 n_path-1)。
     */
    extern void potr_internal_tcp_health_thread_stop(potr_context *ctx, int path_idx);

    /**
     *  @brief          指定パスの TCP ヘルス チェック スレッドを即時起床させます。
     *  @param[in,out]  ctx      セッション コンテキスト。
     *  @param[in]      path_idx パス インデックス。
     */
    extern void potr_internal_tcp_health_thread_wake(potr_context *ctx, int path_idx);

    /**
     *  @brief          全パスの TCP ヘルス チェック スレッドを即時起床させます。
     *  @param[in,out]  ctx セッション コンテキスト。
     */
    extern void potr_internal_tcp_health_thread_wake_all(potr_context *ctx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_HEALTH_THREAD_H */
