/**
 *******************************************************************************
 *  @file           receiver_report.h
 *  @brief          受信報告と返送元ごとのリプレイ記録を定義します。
 *
 *******************************************************************************
 */
#ifndef POTR_PROTOCOL_RECEIVER_REPORT_H
#define POTR_PROTOCOL_RECEIVER_REPORT_H
#include <stddef.h>
#include <stdint.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/packet_seal.h>
#include <porter/porter_const.h>
#include <porter/potr_flow_control.h>
#include <porter/security/certificate.h>

/** 片方向 UDP の返送元セッション。離脱後も再利用までリプレイ記録を保持します。 */
typedef struct potr_internal_receiver
{
    potr_internal_certificate_state certificate; /**< 認証前から受信者ごとに保持する相互認証。 */
    potr_internal_replay_window replay_window; /**< NACK と REPORT に共通の受信記録。 */
    struct potr_internal_receiver *next; /**< 次の管理エントリ。 */
    uint64_t last_report_ms; /**< 最後に受理した REPORT の到着時刻 [ms]。 */
    uint64_t retired_ms; /**< 離脱時刻 [ms]。NACK だけのエントリは登録時刻。再利用順の基準。 */
    uint64_t flow_request_expire_ms; /**< 受理した抑制の要求の期限 [ms]。0 = 要求なし。 */
    uint64_t flow_send_limit; /**< 受理した送出上限 (POTR_FLOW_SEND_LIMIT_HELD と通番の組)。0 = 上限なし。 */
    uint64_t flow_limit_received_ms; /**< 最後にフロー制御パケットを受理した時刻 [ms]。 */
    uint64_t flow_limit_stalled_ms; /**< 送出が送出上限に達して止まった時刻 [ms]。0 = 止まっていない。 */
    int64_t session_tv_sec; /**< 受信者のセッション開始秒。 */
    int32_t session_tv_nsec; /**< 受信者のセッション開始ナノ秒部。 */
    uint32_t session_id; /**< 受信者のセッション識別子。 */
    uint8_t path_state[POTR_MAX_PATH]; /**< 受信者が報告した経路受信状態。 */
    int active; /**< 期限内に REPORT を受信したエントリ。 */
    int flow_monitored; /**< multicast と broadcast で、監視対象に選んでいるか (1: 監視対象)。 */
    int32_t flow_margin; /**< 余裕 (受信報告に載った送出上限 − 受理した時点の次に割り当てる通番)。flow_margin_known が 1 の間だけ有効。 */
    int flow_margin_known; /**< 余裕の記録があるか (1: ある)。記録がない受信者は監視対象の候補にしない。 */
} potr_internal_receiver;

/**
 *  @brief          multicast と broadcast の送信者が監視する受信者の数を求めます。
 *
 *  しきい値は、「1 対 N の判定」が成り立つ最小の「止める状態」の受信者数です。台数の条件では
 *  receiver_count と活動中の受信者数の小さいほう、割合の条件では「活動中の受信者数 × receiver_percent ÷ 100」の切り上げです。
 *  有効な条件のうち小さいほうとし、両方が無効の場合は 0 です。\n
 *  監視する数は、monitored_count、「活動中の受信者数 × monitored_percent ÷ 100」の切り上げ、しきい値のうち最も大きい値です。
 *  ただし、「監視する数の上限」(POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX と「(max_payload − 2) ÷ 16」の小さいほう) と、
 *  候補の受信者数を超えません。しきい値が 0 の場合は、送信者は送出上限で止まらないため 0 とします。\n
 *  しきい値より少ない数を設定した場合にしきい値の台数を監視するのは、監視対象がしきい値より少ないと、
 *  止める条件が成り立たないためです。
 *
 *  @param[in]      active              活動中の受信者数。
 *  @param[in]      candidates          余裕の記録がある活動中の受信者数 (候補の数)。
 *  @param[in]      receiver_percent    1 対 N の割合の条件 [%]。0 = 無効。
 *  @param[in]      receiver_count      1 対 N の台数の条件。0 = 無効。
 *  @param[in]      monitored_percent   監視する受信者の割合 [%]。0 = 使わない。
 *  @param[in]      monitored_count     監視する受信者の台数。0 = 使わない。
 *  @param[in]      max_payload         送信者の実効 max_payload [byte]。
 *  @return         監視する数。失敗モードのない計算のため結果コードの対象外です。
 */
static inline size_t flow_monitor_count(size_t active, size_t candidates, uint32_t receiver_percent,
                                        uint32_t receiver_count, uint32_t monitored_percent, uint32_t monitored_count,
                                        uint32_t max_payload)
{
    size_t threshold = 0U;
    size_t count = (size_t)monitored_count;
    size_t limit = (size_t)POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX;

    if (receiver_count != 0U)
    {
        threshold = (size_t)receiver_count;
        if (threshold > active)
        {
            threshold = active;
        }
    }

    /* 受信者数は max_peers 以下、割合は 100 以下のため、乗算は桁あふれしない。 */
    if (receiver_percent != 0U)
    {
        size_t by_percent = (active * (size_t)receiver_percent + POTR_PERCENT_CEIL_OFFSET) / POTR_PERCENT_MAX;

        if (receiver_count == 0U || by_percent < threshold)
        {
            threshold = by_percent;
        }
    }

    if (threshold == 0U)
    {
        return 0U;
    }

    if (monitored_percent != 0U)
    {
        size_t by_percent = (active * (size_t)monitored_percent + POTR_PERCENT_CEIL_OFFSET) / POTR_PERCENT_MAX;

        if (by_percent > count)
        {
            count = by_percent;
        }
    }

    if (threshold > count)
    {
        count = threshold;
    }

    /* 監視対象の一覧を 1 個のパケットで知らせるための上限。max_payload は検査済み (64 以上) で、2 を引いても負にならない。 */
    if (max_payload >= POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE &&
        (size_t)((max_payload - POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE) / POTR_FLOW_CONTROL_MONITOR_ENTRY_SIZE) < limit)
    {
        limit = (size_t)((max_payload - POTR_FLOW_CONTROL_MONITOR_HEADER_SIZE) / POTR_FLOW_CONTROL_MONITOR_ENTRY_SIZE);
    }

    if (count > limit)
    {
        count = limit;
    }

    if (count > candidates)
    {
        count = candidates;
    }

    return count;
}
#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/**
 *  @brief          認証後の REPORT、NACK またはフロー制御パケットを返送元ごとの記録で検査します。
 *
 *  フロー制御パケットを受理した場合は、返送元の抑制の要求の期限を更新します。
 *  抑制の要求では「到着時刻 + 有効期限」とし、解除では要求なしへ戻します。
 *  状態の値によらず、返送元の送出上限を上書きし、受理した時刻を記録します。
 *  ただし multicast と broadcast では、送出上限を保持するのは監視対象の受信者だけです。
 *  監視対象でない受信者から抑制の要求を受けた場合は、その受信者の余裕を 0 として記録し、監視対象を選び直します。\n
 *  受信報告では、seq_num が 1 の場合に ack_num を送出上限として、その時点の次に割り当てる DATA の通番との
 *  循環を考慮した差を余裕として記録し、seq_num が 0 の場合は余裕の記録を消します。multicast と broadcast では、
 *  余裕の記録を更新するたびに監視対象を選び直します。監視対象から外れた受信者の送出上限は破棄します。\n
 *  ペイロードが POTR_FLOW_CONTROL_PAYLOAD_SIZE バイトでないパケットと、状態が不正なパケットは POTR_ERR_PROTOCOL とします。
 *  受信報告で、seq_num が 0 と 1 以外の場合と、seq_num が 0 で ack_num が 0 でない場合も POTR_ERR_PROTOCOL とします。
 *  @param[in,out]  ctx 送信者。NULL は許可しません。
 *  @param[in]      packet 開封済みパケット。NULL は許可しません。
 *  @param[in]      wire_header 受信ヘッダー。NULL は許可しません。
 *  @return         受理時 POTR_OK、上限時 POTR_ERR_FULL、形式・リプレイ不正時 POTR_ERR_PROTOCOL、
 *                  確保失敗時 POTR_ERR_OUT_OF_MEMORY、時計取得失敗時 POTR_ERR_IO、NULL 時 POTR_ERR_INVALID_ARGUMENT。
 *  @note           send_window_mutex を取得して次に割り当てる通番を読んだ後、重ねずに callback_mutex を取得します。
 *                  所有権は移動しません。
 */
int potr_internal_receiver_accept(potr_context *ctx, const potr_packet *packet, const uint8_t *wire_header);
/**
 *  @brief          期限切れを除いた全受信者に届くグループの集合を返します。
 *  @param[in,out]  ctx 送信者。NULL は許可しません。
 *  @param[in]      now_ms 単調増加時刻 [ms]。
 *  @return         ビット 0 がグループ 1 を表すマスク。報告中の受信者がなければ 0。
 *  @note           NULL は 0 を返します。集合を返す値関数のため結果コードの対象外です。
 *                  呼び出し側が callback_mutex を保持します。所有権は移動しません。
 */
uint8_t potr_internal_receiver_groups_locked(potr_context *ctx, uint64_t now_ms);
/**
 *  @brief          片方向 UDP の送信者が、受信者からの抑制の要求と送出上限によって送出を止めるべきかを判定します。
 *
 *  受信者が「止める状態」であるとは、有効な (期限内の) 要求があるか、受理した送出上限に達している
 *  (next_seq が送出上限以上である) ことです。通番は循環を考慮した差で比較します。\n
 *  unicast は、「止める状態」の受信者が 1 つでもあれば抑制します。\n
 *  multicast と broadcast は、「止める状態」の受信者の割合または台数が実効設定の条件を満たす場合に抑制します。
 *  multicast と broadcast で受信者ごとに保持する送出上限は、監視対象の受信者のものだけです (potr_internal_receiver_accept())。
 *  活動中の受信者は、受信報告で活動中と判定している受信者と、「止める状態」の受信者です。
 *  活動中の受信者数が台数の設定値を下回る場合は、活動中の受信者数を台数のしきい値として使います。\n
 *  送出上限に達した時点から有効期限が経過しても次のフロー制御パケットを受理していない受信者は、
 *  その送出上限を破棄し、上限がないものとして扱います。
 *
 *  @param[in,out]  ctx 送信者。NULL は許可しません。
 *  @param[in]      now_ms 単調増加時刻 [ms]。
 *  @param[in]      next_seq 次に割り当てる DATA の通番。
 *  @param[out]     tracked 有効な要求または送出上限を保持している受信者数の格納先。NULL は許可しません。
 *                  0 以外の間は、送出のたびに判定し直す必要があります。
 *  @param[out]     until_ms 「止める状態」の受信者の判定が変わりうる最も早い時刻 [ms] の格納先
 *                  (要求の期限、または送出上限の破棄の時刻)。該当がなければ 0。NULL は許可しません。
 *                  呼び出し元はこの時刻に判定し直します。
 *  @return         抑制する場合は 1、しない場合と引数が NULL の場合は 0。失敗モードのない述語のため結果コードの対象外です。
 *  @note           callback_mutex を内部で取得します。所有権は移動しません。
 */
int potr_internal_receiver_flow_suppressed(potr_context *ctx, uint64_t now_ms, uint32_t next_seq, size_t *tracked,
                                           uint64_t *until_ms);
/**
 *  @brief          multicast と broadcast の送信者が、監視対象を選び直し、その一覧を返します。
 *
 *  余裕の記録がある活動中の受信者のうち、余裕が小さいほうから flow_monitor_count() の数の受信者を監視対象にします。
 *  活動中の受信者は、受信報告で活動中と判定している受信者と、有効な抑制の要求を送ってきている受信者です。
 *  監視対象から外れた受信者の送出上限は破棄します。\n
 *  順位付けに使う余裕は、受信報告から求めた値だけです。フロー制御パケットの送出上限は、更新の直後の値になり、
 *  受信報告から求めた値より常に大きくなるため使いません。
 *
 *  @param[in,out]  ctx         送信者。NULL は許可しません。
 *  @param[in]      now_ms      単調増加時刻 [ms]。
 *  @param[out]     list        監視対象の受信者のセッション識別子の格納先 (service_id は設定しません)。NULL は許可しません。
 *  @param[in]      capacity    list に格納できる数。
 *  @param[out]     changed     前回この関数を呼んだ後に監視対象が変わった場合は 1、変わっていない場合は 0 の格納先。
 *                              NULL は許可しません。
 *  @return         監視対象の台数 (capacity 以下)。multicast と broadcast の送信者以外と引数が NULL の場合は 0。
 *                  台数を返す値関数のため結果コードの対象外です。
 *  @note           callback_mutex を内部で取得します。所有権は移動しません。
 */
size_t potr_internal_receiver_monitor_list(potr_context *ctx, uint64_t now_ms, potr_internal_packet_session_hdr *list,
                                           size_t capacity, int *changed);
#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* POTR_PROTOCOL_RECEIVER_REPORT_H */
