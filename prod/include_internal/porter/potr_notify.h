/**
 *******************************************************************************
 *  @file           potr_notify.h
 *  @brief          通知専用スレッドへイベントを引き渡します。
 *******************************************************************************
 */
#ifndef POTR_NOTIFY_H
#define POTR_NOTIFY_H
#include <porter/porter_type.h>
#ifdef __cplusplus
extern "C"
{
#endif
    /**
     *  @brief          通知資源を起動します。
     *  @param[in,out]  ctx コンテキスト。
     *  @return         POTR_OK、POTR_ERR_INVALID_ARGUMENT、POTR_ERR_OUT_OF_MEMORY または POTR_ERR_UNKNOWN。
     */
    int potr_internal_notify_start(potr_context *ctx);
    /**
     *  @brief          通知をすべて実行して停止します。
     *  @param[in,out]  ctx 生成元が停止済みのコンテキスト。
     *  @return         POTR_OK、POTR_ERR_INVALID_ARGUMENT、POTR_ERR_OUT_OF_MEMORY または POTR_ERR_UNKNOWN。
     */
    int potr_internal_notify_stop(potr_context *ctx);
    /**
     *  @brief          通知を登録します。
     *  @param[in,out]  ctx コンテキスト。
     *  @param[in]      peer_id ピア。
     *  @param[in]      event イベント。
     *  @param[in]      data 内容。
     *  @param[in]      len 長さまたは PATH index。
     */
    void potr_internal_notify_enqueue(potr_context *ctx, potr_peer_id peer_id, potr_event event, const void *data,
                                      size_t len);
#ifdef __cplusplus
}
#endif
#endif
