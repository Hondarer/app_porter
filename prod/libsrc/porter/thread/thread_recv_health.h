/**
 *******************************************************************************
 *  @file           thread_recv_health.h
 *  @brief          受信タイムアウトの監視と PING の起床判断を定義します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef THREAD_RECV_HEALTH_PRIVATE_H
#define THREAD_RECV_HEALTH_PRIVATE_H

#include "thread_recv_slot.h"

/**
 *  @brief          N:1 のパス・ピアの受信期限を監視します。
 *  @param[in,out]  ctx             ピア テーブルを構成済みのコンテキスト。NULL は許可しません。
 *  @note           期限到達時はパスを解除し、ピアの期限到達時は切断して解放します。
 *  @par            スレッド セーフ
 *                  既存の受信スレッドから呼びます。内部で peers_mutex を保持して状態を更新し、
 *                  解放後に必要な health 起床を行います。呼び出し元は peers_mutex を保持しません。
 */
void thread_recv_health_check_peers(potr_context *ctx);

/**
 *  @brief          1:1 のパス・サービスの受信期限を監視します。
 *  @param[in,out]  slot            コンテキストの受信状態ビュー。NULL は許可しません。
 *  @note           パスの期限到達時は送信元学習を解除し、サービスの期限到達時はセッション・FIN・
 *                  受信ウィンドウを初期化します。ビューとコンテキストの所有権は移動しません。
 *  @par            スレッド セーフ
 *                  同じコンテキストの既存受信スレッドから直列に呼びます。
 */
void thread_recv_health_check_service(thread_recv_slot *slot);

/**
 *  @brief          TCP の受信状態変化に応じて PING 送信を促します。
 *  @param[in,out]  ctx             稼働中のコンテキスト。NULL は許可しません。
 *  @param[in]      path_idx        有効なパス番号。
 *  @param[in]      state_changed   受信状態が変化した場合は非 0。
 *  @note           TCP の状態変化時は、health 稼働中なら全パスを起床し、それ以外は即時送信します。
 *  @par            スレッド セーフ
 *                  状態更新後に、既存の受信スレッドから呼びます。
 */
void thread_recv_health_wake_tcp(potr_context *ctx, int path_idx, int state_changed);

#endif
