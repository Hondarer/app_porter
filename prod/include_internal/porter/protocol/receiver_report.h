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
    uint64_t flow_send_limit; /**< 受理した送出上限の状態と通番の組 (POTR_FLOW_SEND_LIMIT_*)。multicast と broadcast では 0 = 上限なし。 */
    uint64_t flow_limit_received_ms; /**< 最後にフロー制御パケットを受理した時刻 [ms]。 */
    uint64_t flow_last_packet_ms; /**< 最後に何らかのパケット (REPORT、NACK、フロー制御) を受理した時刻 [ms]。 */
    uint64_t flow_limit_stalled_ms; /**< 送出が送出上限に達して止まった時刻 [ms]。0 = 止まっていない。 */
    uint64_t flow_stall_accum_units; /**< multicast と broadcast で、この受信者が止める状態で送信者が送出を止めていた時間の積算 [1/9 ms] (POTR_FLOW_ACCUM_DECAY_DIVISOR 倍)。 */
    uint64_t flow_unexclude_since_ms; /**< 除外を解く条件を満たし始めた時刻 [ms]。flow_unexclude_pending が 1 の間だけ有効。 */
    uint64_t flow_unmonitored_ms; /**< 監視対象でなくなった時刻 [ms]。flow_ever_monitored が 1 で非監視対象の間だけ有効。 */
    int64_t session_tv_sec; /**< 受信者のセッション開始秒。 */
    int32_t session_tv_nsec; /**< 受信者のセッション開始ナノ秒部。 */
    uint32_t session_id; /**< 受信者のセッション識別子。 */
    uint32_t flow_report_send_limit; /**< 受信報告に載った送出上限の絶対値。flow_report_limit_known が 1 の間だけ有効。 */
    uint32_t flow_report_max_allowance; /**< 受信報告に載った許可数の上限。0 = 送出上限なし (または未受信)。 */
    int flow_report_limit_known; /**< 受信報告に載った送出上限の記録があるか (1: ある)。 */
    uint8_t path_state[POTR_MAX_PATH]; /**< 受信者が報告した経路受信状態。 */
    int active; /**< 期限内に REPORT を受信したエントリ。 */
    int flow_monitored; /**< multicast と broadcast で、監視対象に選んでいるか (1: 監視対象)。 */
    int flow_ever_monitored; /**< multicast と broadcast で、一度でも監視対象になったことがあるか (1: ある、0: ない)。 */
    int32_t flow_margin; /**< 余裕 (受信報告に載った送出上限 − 受理した時点の次に割り当てる通番)。flow_margin_known が 1 の間だけ有効。 */
    int flow_margin_known; /**< 余裕の記録があるか (1: ある)。記録がない受信者は監視対象の候補にしない。 */
    int flow_excluded; /**< multicast と broadcast で、止める判定と監視対象から除外しているか (1: 除外中)。 */
    int flow_last_stopping; /**< 前回の判定時点で止める状態だったか。除外中も活動中なら記録します。callback_mutex で保護。 */
    int flow_unexclude_pending; /**< 除外を解く条件を満たし始めた時刻を記録中か (1: 記録中)。 */
    int _pad; /**< パディング (構造体サイズを 8 [byte] の倍数に揃える)。 */
} potr_internal_receiver;

/** 除外を解く条件 (受信報告の送出上限が次の通番より先にある状態) を維持する時間の倍率 (猶予時間の 10 倍)。 */
#define POTR_FLOW_UNEXCLUDE_GRACE_MULTIPLIER 10U

static_assert(POTR_FLOW_UNEXCLUDE_GRACE_MULTIPLIER == 10U, "porter: POTR_FLOW_UNEXCLUDE_GRACE_MULTIPLIER must be 10");

/** 監視対象の受信者が、許可数の上限の半分を受信するたびに送出上限を更新する回数 (1 窓あたり 2 回)。 */
#define POTR_FLOW_MONITOR_UPDATES_PER_WINDOW 2U

/** 通知の見込み (2 ÷ 許可数の上限) を整数で積算するためのスケール (100 [%] を 1,000,000,000 として表す)。 */
#define POTR_FLOW_MONITOR_BUDGET_SCALE 1000000000ULL

/** 予算 (flow_control_feedback_percent) 1 [%] あたりのスケール値。 */
#define POTR_FLOW_MONITOR_BUDGET_PER_PERCENT (POTR_FLOW_MONITOR_BUDGET_SCALE / (uint64_t)POTR_PERCENT_MAX)

/** 1 台あたりの通知の見込みを求める際の分子 (2 × 1,000,000,000)。 */
#define POTR_FLOW_MONITOR_UNIT_NUMERATOR \
    ((uint64_t)POTR_FLOW_MONITOR_UPDATES_PER_WINDOW * POTR_FLOW_MONITOR_BUDGET_SCALE)

static_assert(POTR_FLOW_MONITOR_UPDATES_PER_WINDOW == 2U, "porter: POTR_FLOW_MONITOR_UPDATES_PER_WINDOW must be 2");
static_assert(POTR_FLOW_MONITOR_BUDGET_SCALE % (uint64_t)POTR_PERCENT_MAX == 0ULL &&
                  POTR_FLOW_MONITOR_BUDGET_PER_PERCENT > 0ULL,
              "porter: POTR_FLOW_MONITOR_BUDGET_SCALE must be a positive multiple of POTR_PERCENT_MAX");
static_assert(POTR_FLOW_MONITOR_UNIT_NUMERATOR / POTR_FLOW_MONITOR_BUDGET_SCALE ==
                  (uint64_t)POTR_FLOW_MONITOR_UPDATES_PER_WINDOW,
              "porter: POTR_FLOW_MONITOR_UNIT_NUMERATOR must not overflow");

static_assert(POTR_FLOW_MONITOR_UNIT_NUMERATOR <= UINT64_MAX / POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX,
              "porter: monitor cost sum must fit in uint64_t");

/**
 *  @brief          受信者 1 台の通知の見込み (2 ÷ 許可数の上限) をスケール済み整数で求めます。
 *
 *  割り算は切り捨てます。実際の合計が予算を超える誤差は、監視する数 ÷ 1,000,000,000 未満です。
 *
 *  @param[in]      max_allowance   受信者の許可数の上限 (1 以上)。0 の場合は UINT64_MAX を返します。
 *  @return         POTR_FLOW_MONITOR_BUDGET_SCALE 倍した通知の見込み。失敗モードのない計算のため結果コードの対象外です。
 */
static inline uint64_t flow_monitor_receiver_cost(uint32_t max_allowance)
{
    if (max_allowance == 0U)
    {
        return UINT64_MAX;
    }

    return POTR_FLOW_MONITOR_UNIT_NUMERATOR / (uint64_t)max_allowance;
}

/**
 *  @brief          multicast と broadcast の送信者が監視する受信者の数を求めます。
 *
 *  監視する数は、予算 (flow_control_feedback_percent) から求めた数 (budget_count) と
 *  monitored_count (監視する数の下限) の大きいほうとします。
 *  ただし、「監視する数の上限」(POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX と「(max_payload − 2) ÷ 16」の小さいほう) と、
 *  候補の受信者数を超えません。
 *
 *  @param[in]      candidates          余裕と許可数の上限 (1 以上) の記録がある活動中の受信者数 (候補の数)。
 *  @param[in]      budget_count        予算 (flow_control_feedback_percent) の条件を満たす最大の数。
 *  @param[in]      monitored_count     監視する受信者の台数の下限。0 = 下限なし。
 *  @param[in]      max_payload         送信者の実効 max_payload [byte]。
 *  @return         監視する数。失敗モードのない計算のため結果コードの対象外です。
 */
static inline size_t flow_monitor_count(size_t candidates, size_t budget_count, uint32_t monitored_count,
                                        uint32_t max_payload)
{
    size_t count = (budget_count > (size_t)monitored_count) ? budget_count : (size_t)monitored_count;
    size_t limit = (size_t)POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX;

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
 *  監視対象でない受信者 (除外している受信者を除く) から NACK または抑制の要求を受けた場合は、
 *  その受信者の余裕を 0 として記録し、監視対象を選び直します (次の受信報告で上書きします)。\n
 *  受信報告では、seq_num を許可数の上限として保持し、seq_num が 0 でない場合に ack_num を送出上限として扱います。
 *  unicast では、未通知または相手なしの受信者の最初の通知として送出上限を保持し、保持している送出上限がある場合は
 *  より先へ進んでいる場合だけ更新します。
 *  multicast と broadcast では、その時点の次に割り当てる DATA の通番との循環を考慮した差を余裕として記録し、
 *  seq_num が 0 の場合は余裕の記録を消します。余裕の記録を更新するたびに監視対象を選び直し、
 *  監視対象から外れた受信者の送出上限は破棄します。監視対象が送出上限を保持していない場合は、
 *  受信報告の送出上限を保持し、保持している場合はより先へ進んでいる場合だけ更新します。\n
 *  unicast で新しい受信者の項目を作る時点で、ハンドル側が未通知だった場合はハンドル側の基準通番を引き継ぎます。\n
 *  ペイロードが POTR_FLOW_CONTROL_PAYLOAD_SIZE バイトでないパケットと、状態が不正なパケットは POTR_ERR_PROTOCOL とします。
 *  受信報告で、seq_num が 0 なのに ack_num が 0 でない場合も POTR_ERR_PROTOCOL とします。
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
 *  unicast では、「止める状態」の受信者が 1 台でもあれば抑制します。\n
 *  multicast と broadcast では、除外していない受信者のうち 1 台でも「止める状態」であれば抑制します。
 *  受信者ごとに、その受信者が止める状態で送信者が送出を止めていた時間を積算し、止めていない間は経過時間 ÷ 9 だけ
 *  減算します (0 を下回らない)。経過区間には前回の判定時点の停止状態を使います。
 *  前回の止める状態の数 (活動中の除外受信者を含む) が除外上限を超える区間は、全員の積算を据え置きます。
 *  全体が遅い間は新しく除外せず、積算に基づく待機上限も適用しません。積算が猶予時間 (grace_ms) に達した受信者は、除外している数が
 *  flow_excluded_limit() の上限に達していない場合に除外し、監視対象からも外します。
 *  除外した受信者は、受信報告に載った送出上限が next_seq より先にある状態が猶予時間の 10 倍の間続いた時点で
 *  除外を解き、積算を 0 に戻します。活動中でなくなった除外受信者も、除外、積算、解除用の計時を戻します。\n
 *  multicast と broadcast で受信者ごとに保持する送出上限は、監視対象の受信者のものだけです (potr_internal_receiver_accept())。\n
 *  送出上限に達した時点から有効期限が経過しても、相手から何らかのパケット (フロー制御パケット、受信報告、NACK) が
 *  届いている間は破棄しません。何も届かずに有効期限が経過した受信者は、送出上限を破棄し、
 *  上限がないものとして扱います (unicast では相手なしへ遷移します)。
 *
 *  @param[in,out]  ctx 送信者。NULL は許可しません。
 *  @param[in]      now_ms 単調増加時刻 [ms]。
 *  @param[in]      next_seq 次に割り当てる DATA の通番。
 *  @param[out]     tracked 有効な要求、送出上限、0 でない積算、または除外が残っている受信者数の格納先。NULL は許可しません。
 *                  0 以外の間は、送出のたびに判定し直す必要があります。
 *  @param[out]     until_ms 「止める状態」の受信者の判定が変わりうる最も早い時刻 [ms] の格納先
 *                  (要求の期限、送出上限の破棄の時刻、または積算が猶予時間に達しうる時刻)。該当がなければ 0。NULL は許可しません。
 *                  呼び出し元はこの時刻に判定し直します。
 *  @return         抑制する場合は 1、しない場合と引数が NULL の場合は 0。失敗モードのない述語のため結果コードの対象外です。
 *  @note           callback_mutex を内部で取得します。所有権は移動しません。
 */
int potr_internal_receiver_flow_suppressed(potr_context *ctx, uint64_t now_ms, uint32_t next_seq, size_t *tracked,
                                           uint64_t *until_ms);
/**
 *  @brief          multicast と broadcast の送信者が、監視対象を選び直し、その一覧を返します。
 *
 *  余裕と許可数の上限 (1 以上) の記録がある、除外していない活動中の受信者を候補とします。
 *  候補を余裕が小さい順 (同じ場合は現在の監視対象、次に監視対象でなくなってからの時間が長い順、
 *  それも同じ場合は管理表の順) に並べ、先頭から「2 ÷ 許可数の上限」の合計が予算
 *  (flow_control_feedback_percent ÷ 100) 以下である最大の数を求め、flow_monitor_count() で
 *  下限・上限・候補数に収めた数の受信者を監視対象にします。
 *  活動中の受信者は、受信報告で活動中と判定している受信者と、有効な抑制の要求を送ってきている受信者です。
 *  監視対象から外れた受信者の送出上限は破棄し、外れた時刻を記録します。\n
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
