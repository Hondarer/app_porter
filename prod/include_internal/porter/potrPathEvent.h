/**
 *******************************************************************************
 *  @file           potrPathEvent.h
 *  @brief          PATH イベントを処理する内部 helper を宣言します。
 *  @author         Tetsuo Honda
 *  @date           2026/04/19
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

#ifndef POTR_PATH_EVENT_H
#define POTR_PATH_EVENT_H

#include <stddef.h>

#include <porter/porter_type.h>

#include <porter/potrContext.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief  ロック保持中に計算した PATH イベントの確定結果。
     *
     *  ロック保持中に path 論理接続状態の差分を計算して本構造体に蓄え、
     *  ロック解放を伴わない emit 関数へ渡してコールバックを発火します。\n
     *  ロック保持区間とコールバック発火区間を同一ロック内に収め、状態とイベントの順序整合を保ちます。
     */
    typedef struct PotrPreparedPathEvents
    {
        int final_states[POTR_MAX_PATH];  /**< 各 path の確定論理接続状態 (1 = 接続、0 = 切断)。 */
        int changed_paths[POTR_MAX_PATH]; /**< 状態が変化した path のインデックス列。 */
        PotrEvent
            changed_events[POTR_MAX_PATH]; /**< changed_paths と対応する PATH イベント (CONNECTED / DISCONNECTED)。 */
        int changed_count;       /**< 状態が変化した path 数 (changed_paths / changed_events の有効要素数)。 */
        PotrEvent session_event; /**< セッション全体の接続状態変化イベント。変化なしの場合は 0。 */
    } PotrPreparedPathEvents;

    /**
     *  @brief          コールバック直列化用ミューテックスを生成します。
     *  @param[in,out]  ctx  対象のセッション コンテキストへのポインター。
     */
    extern void potr_callback_mutex_init(PotrContext *ctx);

    /**
     *  @brief          コールバック直列化用ミューテックスを破棄します。
     *  @param[in,out]  ctx  対象のセッション コンテキストへのポインター。
     */
    extern void potr_callback_mutex_dispose(PotrContext *ctx);

    /**
     *  @brief          受信コールバックを直列化して発火します。
     *  @param[in,out]  ctx     対象のセッション コンテキストへのポインター。
     *  @param[in]      peer_id 通知するピア識別子。
     *  @param[in]      event   通知するイベント種別。
     *  @param[in]      data    イベントに付随するデータ。付随しない場合は NULL。
     *  @param[in]      len     data の長さ、または PATH イベントの対象 path index。
     *
     *  callback_mutex を取得してからコールバックを呼び出し、呼び出し後に解放します。\n
     *  ctx または callback が NULL の場合は何もしません。
     */
    extern void potr_callback_emit(PotrContext *ctx, PotrPeerId peer_id, PotrEvent event, const void *data, size_t len);

    /**
     *  @brief          受信コールバックを発火します (ロック取得なし)。
     *  @param[in,out]  ctx     対象のセッション コンテキストへのポインター。
     *  @param[in]      peer_id 通知するピア識別子。
     *  @param[in]      event   通知するイベント種別。
     *  @param[in]      data    イベントに付随するデータ。付随しない場合は NULL。
     *  @param[in]      len     data の長さ、または PATH イベントの対象 path index。
     *
     *  呼び出し側で callback_mutex を保持している前提で、ロックを取得せずにコールバックを呼び出します。\n
     *  ctx または callback が NULL の場合は何もしません。
     */
    extern void potr_callback_emit_locked(PotrContext *ctx, PotrPeerId peer_id, PotrEvent event, const void *data,
                                          size_t len);

    /**
     *  @brief          path 論理接続状態配列を 0 で初期化します。
     *  @param[out]     states  POTR_MAX_PATH 要素の状態配列へのポインター。
     */
    extern void potr_zero_path_states(int *states);

    /**
     *  @brief          片方向通信の path 論理接続状態を算出します。
     *  @param[in]      ctx     対象のセッション コンテキストへのポインター。
     *  @param[out]     states  算出結果を格納する POTR_MAX_PATH 要素の配列。
     *
     *  各 path について、ローカルの PING 受信状態が NORMAL であれば 1、そうでなければ 0 を設定します。
     */
    extern void potr_copy_oneway_path_states(const PotrContext *ctx, int *states);

    /**
     *  @brief          双方向 UDP 通信 (1:1) の path 論理接続状態を算出します。
     *  @param[in]      ctx     対象のセッション コンテキストへのポインター。
     *  @param[out]     states  算出結果を格納する POTR_MAX_PATH 要素の配列。
     *
     *  各 path について、ローカルと相手の双方の PING 受信状態が NORMAL の場合のみ 1 を設定します。
     */
    extern void potr_copy_bidir_udp_path_states(const PotrContext *ctx, int *states);

    /**
     *  @brief          双方向 UDP 通信 (N:1) のピア単位 path 論理接続状態を算出します。
     *  @param[in]      peer    対象のピア コンテキストへのポインター。
     *  @param[out]     states  算出結果を格納する POTR_MAX_PATH 要素の配列。
     *
     *  各 path について、ピアのローカルと相手の双方の PING 受信状態が NORMAL の場合のみ 1 を設定します。
     */
    extern void potr_copy_bidir_n1_path_states(const PotrPeerContext *peer, int *states);

    /**
     *  @brief          TCP 通信の path 論理接続状態を算出します。
     *  @param[in]      ctx     対象のセッション コンテキストへのポインター。
     *  @param[out]     states  算出結果を格納する POTR_MAX_PATH 要素の配列。
     *
     *  各 path について、TCP ソケットが接続済みかつローカルと相手の双方の PING 受信状態が
     *  NORMAL の場合のみ 1 を設定します。
     */
    extern void potr_copy_tcp_path_states(const PotrContext *ctx, int *states);

    /**
     *  @brief          サービス単位の path 論理接続状態を更新し、発火すべきイベントを準備します。
     *  @param[in,out]  ctx         対象のセッション コンテキストへのポインター。
     *  @param[in]      next_states 新しい path 論理接続状態の配列 (POTR_MAX_PATH 要素)。
     *  @param[out]     prepared    準備したイベント情報の格納先。
     *
     *  現在の path_logical_alive と next_states の差分から PATH イベントを、論理接続本数の
     *  0 本との境界変化からセッション イベントを準備し、ctx の path_logical_alive と
     *  health_alive を更新します。\n
     *  状態更新とイベント発火の順序整合を保つため、呼び出し側で callback_mutex を保持した
     *  状態で呼び出します (_locked サフィックス)。
     */
    extern void potr_sync_service_path_state_locked(PotrContext *ctx, const int *next_states,
                                                    PotrPreparedPathEvents *prepared);

    /**
     *  @brief          ピア単位の path 論理接続状態を更新し、発火すべきイベントを準備します。
     *  @param[in,out]  peer        対象のピア コンテキストへのポインター。
     *  @param[in]      next_states 新しい path 論理接続状態の配列 (POTR_MAX_PATH 要素)。
     *  @param[out]     prepared    準備したイベント情報の格納先。
     *
     *  処理内容は potr_sync_service_path_state_locked() のピア版です。peer の path_logical_alive と
     *  health_alive を更新します。\n
     *  呼び出し側で callback_mutex を保持した状態で呼び出します (_locked サフィックス)。
     */
    extern void potr_sync_peer_path_state_locked(PotrPeerContext *peer, const int *next_states,
                                                 PotrPreparedPathEvents *prepared);

    /**
     *  @brief          準備済みのサービス単位 PATH / セッション イベントを発火します。
     *  @param[in,out]  ctx      対象のセッション コンテキストへのポインター。
     *  @param[in]      prepared potr_sync_service_path_state_locked() が準備したイベント情報。
     *
     *  変化した各 path の PATH イベントを発火し、最後にセッション イベント (あれば) を発火します。\n
     *  呼び出し側で callback_mutex を保持した状態で呼び出します (_locked サフィックス)。
     */
    extern void potr_emit_service_path_events_locked(PotrContext *ctx, const PotrPreparedPathEvents *prepared);

    /**
     *  @brief          準備済みのピア単位 PATH / セッション イベントを発火します。
     *  @param[in,out]  ctx      対象のセッション コンテキストへのポインター。
     *  @param[in]      peer     対象のピア コンテキストへのポインター。
     *  @param[in]      prepared potr_sync_peer_path_state_locked() が準備したイベント情報。
     *
     *  変化した各 path の PATH イベントを peer_id 付きで発火し、最後にセッション イベント
     *  (あれば) を発火します。\n
     *  呼び出し側で callback_mutex を保持した状態で呼び出します (_locked サフィックス)。
     */
    extern void potr_emit_peer_path_events_locked(PotrContext *ctx, const PotrPeerContext *peer,
                                                  const PotrPreparedPathEvents *prepared);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_PATH_EVENT_H */
