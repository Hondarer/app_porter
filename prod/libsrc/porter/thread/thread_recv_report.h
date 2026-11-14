/**
 *******************************************************************************
 *  @file           thread_recv_report.h
 *  @brief          UDP 受信スレッドの受信報告の周期処理を定義します。
 *
 *******************************************************************************
 */
#ifndef THREAD_RECV_REPORT_PRIVATE_H
#define THREAD_RECV_REPORT_PRIVATE_H
#include <porter/potr_context.h>
#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/**
 *  @brief          次の受信報告までの間隔を決定します。
 *  @param[in]      ctx サービス。NULL は許可しません。
 *  @param[out]     interval_ms 間隔 [ms]。NULL は許可しません。
 *  @return         成功時 POTR_OK、乱数取得失敗時 POTR_ERR_IO。
 *  @note           一対多は実効間隔の 100%〜150%、それ以外は実効間隔です。所有権は移動しません。
 */
int thread_recv_report_interval(const potr_context *ctx, uint64_t *interval_ms);
/**
 *  @brief          報告予定時刻に全経路から受信状態を返送します。
 *  @param[in,out]  ctx サービス。NULL は許可しません。
 *  @param[in]      now_ms 単調増加時刻 [ms]。
 *  @return         成功時 POTR_OK、封緘失敗時は封緘関数の結果、乱数取得失敗時 POTR_ERR_IO。
 *  @note           片方向の通常モードの受信者のみ送ります。UDP 受信スレッドから呼び出します。
 *                  ソケットの送出失敗は次の報告周期で再試行します。所有権は移動しません。
 */
int thread_recv_report_tick(potr_context *ctx, uint64_t now_ms);
#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* THREAD_RECV_REPORT_PRIVATE_H */
