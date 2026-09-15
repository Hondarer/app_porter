#ifndef API_OPEN_PATHS_PRIVATE_H
#define API_OPEN_PATHS_PRIVATE_H

#include <porter/potr_context.h>

/**
 * @brief 通信種別に応じてソケットと経路を準備します。
 * @param[in,out] ctx 設定を格納済みのコンテキスト。NULL は許可しません。
 * @param[in] role 検証済みの送受信役割。
 * @return 成功時は POTR_OK、失敗時は POTR_ERR_* を返します。
 * @note スレッド起動前に呼びます。途中まで確保したソケットも ctx が所有し、
 *       呼び出し側がサービス開始失敗時に解放します。
 */
int api_open_paths_by_type(potr_context *ctx, potr_role role);

/**
 * @brief 準備済み経路から送信先アドレスを設定します。
 * @param[in,out] ctx 経路を準備済みのコンテキスト。NULL は許可しません。
 * @param[in] role 検証済みの送受信役割。
 * @return 成功時は POTR_OK、不正なアドレスには POTR_ERR_INVALID_ARGUMENT を返します。
 * @note スレッド起動前に呼びます。ctx の所有権は移動しません。
 */
int api_setup_dest_addr(potr_context *ctx, potr_role role);

#endif
