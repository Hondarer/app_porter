/**
 *******************************************************************************
 *  @file           api_context_cleanup.h
 *  @brief          停止済みコンテキストの資源を解放する内部関数を定義します。
 *  @author         Tetsuo Honda
 *  @date           2026/10/24
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef API_CONTEXT_CLEANUP_PRIVATE_H
#define API_CONTEXT_CLEANUP_PRIVATE_H

#include <porter/potr_context.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     *  @brief          終了待ちが必要なスレッド ハンドルの有無を返します。
     *  @param[in]      ctx  有効なコンテキスト。NULL は許可しません。
     *  @return         ハンドルが残るとき 1、それ以外は 0 を返します。
     *  @note           成否を返さない述語です。停止を管理するスレッドから呼びます。
     */
    int api_context_has_threads(const potr_context *ctx);

    /**
     *  @brief          停止済みコンテキストの共通資源を解放します。
     *  @param[in,out]  ctx  有効なコンテキスト。NULL は許可しません。
     *  @note           使用する全スレッドが未起動または終了待ち済みであることが必要です。
     *                  ソケットは INVALID、ポインターは NULL へ戻します。
     *                  本体、送信キューと TCP 同期資源の所有権は呼び出し側に残ります。
     */
    void api_context_dispose_resources(potr_context *ctx);

    /**
     *  @brief          生成済みの TCP サービス用同期資源を破棄します。
     *  @param[in,out]  ctx  有効なコンテキスト。NULL は許可しません。
     *  @note           使用する全スレッドが未起動または終了待ち済みであることが必要です。
     *                  部分初期化を許可し、破棄後のメンバーを NULL へ戻します。
     */
    void api_context_dispose_tcp_sync(potr_context *ctx);

#ifdef __cplusplus
}
#endif

#endif
