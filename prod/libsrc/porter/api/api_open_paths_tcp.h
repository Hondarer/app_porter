/**
 *******************************************************************************
 *  @file           api_open_paths_tcp.h
 *  @brief          TCP の listen ソケットと接続先アドレスを準備します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef API_OPEN_PATHS_TCP_PRIVATE_H
#define API_OPEN_PATHS_TCP_PRIVATE_H

#include <porter/potr_context.h>

/**
 *  @brief          tcp のソケットと経路を準備します。
 *  @param[in,out]  ctx             設定済みコンテキスト。NULL は許可しません。
 *  @param[in]      role            検証済みの送受信役割。
 *  @return         成功時 POTR_OK、失敗時 POTR_ERR_*。
 *  @note           格納済みソケットは途中失敗時も ctx が所有し、呼び出し側が回収します。
 *  @par            スレッド セーフ
 *                  起動前に、同じ ctx へのアクセスを直列化して呼びます。
 */
int api_open_paths_tcp(potr_context *ctx, potr_role role);

#endif
