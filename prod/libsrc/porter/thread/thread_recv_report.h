/**
 *******************************************************************************
 *  @file           thread_recv_report.h
 *  @brief          UDP 受信スレッドの受信報告の周期処理を定義します。
 *
 *******************************************************************************
 */
#ifndef THREAD_RECV_REPORT_PRIVATE_H
#define THREAD_RECV_REPORT_PRIVATE_H
#include <assert.h>
#include <cplat/clock/timespec.h>
#include <porter/potr_context.h>
#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/** 1 秒あたりのナノ秒数 [ns]。 */
#define POTR_NSEC_PER_SEC 1000000000LL

/**
 * 受信ソケットの待機が「待たずに返った (ソケットが空でなかった)」と判定する所要時間の上限 [ns]。
 * Linux の poll() が既に読めるソケットに対して即座に戻る所要時間は数マイクロ秒以下であり、
 * 実際にパケットの到着を待った場合と区別するため 30 [us] とする。
 */
#define POTR_RECV_POLL_IMMEDIATE_THRESHOLD_NS 30000LL

static_assert(POTR_RECV_POLL_IMMEDIATE_THRESHOLD_NS > 0LL &&
                  POTR_RECV_POLL_IMMEDIATE_THRESHOLD_NS < POTR_NSEC_PER_SEC,
              "porter: POTR_RECV_POLL_IMMEDIATE_THRESHOLD_NS out of range");

/**
 *  @brief          multicast または broadcast で受信報告を送出する受信者かどうかを返します。
 *  @param[in]      ctx サービス。NULL は許可しません。
 *  @return         対象の場合は 1、それ以外は 0 を返します。
 *                  失敗モードのない述語のため共通結果コードの適用対象外。
 */
static inline int thread_recv_report_tracks_lag(const potr_context *ctx)
{
    return ctx->role == POTR_ROLE_RECEIVER &&
           (ctx->service.type == POTR_TYPE_MULTICAST || ctx->service.type == POTR_TYPE_BROADCAST) &&
           ctx->health_interval_ms != 0U;
}

/**
 *  @brief          待機の前後の単調増加時刻から、待機が待たずに返ったかを判定します。
 *  @param[in]      before  待機前の単調増加時刻。NULL の場合は 0 を返します。
 *  @param[in]      after   待機後の単調増加時刻。NULL の場合は 0 を返します。
 *  @return         所要時間が POTR_RECV_POLL_IMMEDIATE_THRESHOLD_NS 未満であれば 1、それ以外は 0 を返します。
 *                  失敗モードのない純関数のため共通結果コードの適用対象外。
 */
static inline int thread_recv_report_wait_was_immediate(const cplat_timespec *before, const cplat_timespec *after)
{
    int64_t diff_sec;
    int64_t diff_nsec;

    if (before == NULL || after == NULL)
    {
        return 0;
    }

    diff_sec = (int64_t)after->tv_sec - (int64_t)before->tv_sec;
    if (diff_sec < 0)
    {
        return 1;
    }

    if (diff_sec > 1)
    {
        return 0;
    }

    diff_nsec = diff_sec * POTR_NSEC_PER_SEC + (after->tv_nsec - before->tv_nsec);

    return diff_nsec < POTR_RECV_POLL_IMMEDIATE_THRESHOLD_NS;
}

/**
 *  @brief          ソケットが空になったため、連続して処理した DATA の数を 0 に戻します。
 *  @param[in,out]  ctx サービス。NULL は許可しません。
 *  @note           UDP 受信スレッドから呼び出します。所有権は移動しません。
 */
static inline void thread_recv_report_reset_consecutive(potr_context *ctx)
{
    ctx->recv_report_consecutive_data = 0U;
}

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
 *                  unicast、multicast、broadcast の受信者は、その時点の自身の送出上限と許可数の上限
 *                  (potr_internal_flow_control_report_limit()) を、seq_num (許可数の上限) と ack_num (送出上限) に載せます。
 *                  ソケットの送出失敗は次の報告周期で再試行します。所有権は移動しません。
 */
int thread_recv_report_tick(potr_context *ctx, uint64_t now_ms);
/**
 *  @brief          新しい送信者のセッションを採用した時点で、臨時の受信報告を 1 回送出します。
 *  @param[in,out]  ctx サービス。NULL は許可しません。
 *  @return         成功時 POTR_OK、封緘失敗時は封緘関数の結果。
 *  @note           multicast と broadcast の受信者のうち、受信報告を送出する構成で監視対象でないものだけが送出します。
 *                  連続して処理した DATA の数を 0 に戻します。
 *                  周期の受信報告の時刻 (next_report_ms) および読み出しの遅れによる臨時の受信報告の前回時刻は変えません。
 *                  UDP 受信スレッドから呼び出します。所有権は移動しません。
 */
int thread_recv_report_on_adopted(potr_context *ctx);
/**
 *  @brief          採用している送信者の DATA を 1 個処理したことを記録し、読み出しが遅れていれば臨時の受信報告を送出します。
 *  @param[in,out]  ctx サービス。NULL は許可しません。
 *  @param[in]      now_ms 単調増加時刻 [ms]。
 *  @return         成功時 POTR_OK、封緘失敗時は封緘関数の結果。
 *  @note           multicast と broadcast の受信者のうち、受信報告を送出する構成を対象とします。
 *                  監視対象でない受信者で受信バッファーの幅があり、ソケットが空にならないまま「幅 ÷ 2」個以上の DATA を
 *                  続けて処理した時点で検出回数を増やし、前回の臨時の受信報告から flow_control_request_interval_ms 以上
 *                  経過していれば受信報告を 1 回送出します。周期の受信報告の時刻 (next_report_ms) は変えません。
 *                  UDP 受信スレッドから呼び出します。所有権は移動しません。
 */
int thread_recv_report_on_data_accepted(potr_context *ctx, uint64_t now_ms);
#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* THREAD_RECV_REPORT_PRIVATE_H */
