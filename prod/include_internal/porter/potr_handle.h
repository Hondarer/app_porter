/**
 *******************************************************************************
 *  @file           potr_handle.h
 *  @brief          公開ハンドルと通信コンテキストの寿命を管理します。
 *******************************************************************************
 */
#ifndef POTR_HANDLE_H
#define POTR_HANDLE_H

#include <porter/porter_type.h>

#ifdef __cplusplus
extern "C"
{
#endif
    /**
     *  @brief          ハンドルを準備します。
     *  @param[in,out]  ctx 通信コンテキスト。
     *  @return         POTR_OK、POTR_ERR_INVALID_ARGUMENT、POTR_ERR_OUT_OF_MEMORY または POTR_ERR_UNKNOWN。
     */
    int potr_internal_handle_prepare(potr_context *ctx);

    /**
     *  @brief          公開する値を返します。
     *  @param[in]      ctx 通信コンテキスト。
     *  @return         不透明ハンドル。
     */
    potr_context *potr_internal_handle_public(const potr_context *ctx);

    /**
     *  @brief          未公開ハンドルを回収します。
     *  @param[in,out]  ctx 停止済みコンテキスト。
     */
    void potr_internal_handle_discard(potr_context *ctx);

    /**
     *  @brief          API の参照を取得します。
     *  @param[in]      handle 公開ハンドル。
     *  @param[out]     ctx 通信コンテキスト。
     *  @return         POTR_OK、POTR_ERR_INVALID_ARGUMENT、POTR_ERR_INVALID_HANDLE、POTR_ERR_CANCELED または POTR_ERR_OUT_OF_MEMORY。
     */
    int potr_internal_handle_acquire(potr_context *handle, potr_context **ctx);

    /**
     *  @brief          API の参照を返却します。
     *  @param[in,out]  ctx 通信コンテキスト。
     */
    void potr_internal_handle_release(potr_context *ctx);

    /**
     *  @brief          受付を停止して先行 API を待ちます。
     *  @param[in]      handle 公開ハンドル。
     *  @param[out]     ctx 通信コンテキスト。
     *  @return         POTR_OK、POTR_ERR_INVALID_ARGUMENT、POTR_ERR_INVALID_HANDLE、POTR_ERR_CANCELED または POTR_ERR_UNSUPPORTED。
     */
    int potr_internal_handle_begin_close(potr_context *handle, potr_context **ctx);

    /**
     *  @brief          資源回収済みとして終了を記録します。
     *  @note           以後、ハンドルによる操作は POTR_ERR_INVALID_HANDLE を返します。
     *                  終了処理は資源を保持したまま失敗しないため、終了失敗の状態はありません。
     *  @param[in,out]  ctx 通信コンテキスト。
     */
    void potr_internal_handle_end_close(potr_context *ctx);
#ifdef __cplusplus
}
#endif
#endif
