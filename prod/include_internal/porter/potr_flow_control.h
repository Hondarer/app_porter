/**
 *******************************************************************************
 *  @file           potr_flow_control.h
 *  @brief          受信側の通知キューの占有量に基づくフロー制御を管理します。
 *
 *  受信側は、相手ごとに通知キューの占有量 (コールバックから復帰していない DATA のバイト数) を数えます。
 *  占有量が要求のしきい値を超えたら抑制の要求を、解除のしきい値を下回ったら解除を、
 *  フロー制御パケットで送信側へ伝えます。\n
 *  すべてのフロー制御パケットには、「この通番まで送ってよい」という送出上限を載せます。
 *  送信側は送出上限に達したら自分で止まるため、抑制の要求が届くまでの遅れや喪失があっても、
 *  占有量が要求のしきい値の付近で止まります。\n
 *  相手は、N:1 モードではピアごと、それ以外ではハンドルの通信相手 1 つです。
 *******************************************************************************
 */
#ifndef POTR_FLOW_CONTROL_H
#define POTR_FLOW_CONTROL_H
#include <stdint.h>
#include <stddef.h>
#include <cplat/clock/clock.h>
#include <porter/porter_type.h>

/**
 *  送信側が保持する送出上限で、上限を保持していることを表すビット。
 *  保持値は、このビットと下位 32 ビットの通番の組とし、0 を「上限なし」とする。
 *  1 つの 64 ビット値にまとめるのは、受信スレッドの書き込みと送信スレッドの読み取りをアトミックに 1 回で行うためである。
 */
#define POTR_FLOW_SEND_LIMIT_HELD 0x100000000ULL

/** 送信側の送出上限の判定結果。 */
typedef enum potr_internal_send_limit_state
{
    POTR_SEND_LIMIT_OPEN = 0,    /**< 送出上限を保持していない、または送出上限に達していない。 */
    POTR_SEND_LIMIT_REACHED = 1, /**< 送出上限に達しており、止まった時点から有効期限が経過していない。 */
    POTR_SEND_LIMIT_EXPIRED = 2  /**< 送出上限に達して止まった時点から、次のフロー制御パケットを受けずに有効期限が経過した。 */
} potr_internal_send_limit_state;

/**
 *  @brief          送信側が、相手から受けた送出上限に達しているかを判定します。
 *
 *  通番は循環を考慮した差で比較します。送出上限は、同じ通知の重複と古い通知の喪失で判定が変わらないよう、
 *  バイト数ではなく通番で表します。\n
 *  止まった時点 (stalled_ms) と最後にフロー制御パケットを受けた時刻 (received_ms) の遅い方から
 *  有効期限が経過した場合は POTR_SEND_LIMIT_EXPIRED を返します。呼び出し元は、その送出上限を破棄し、
 *  上限がないものとして扱います。送出上限の通知が失われた場合と、受信側が停止した場合に、
 *  送出が止まったままになることを防ぐためです。
 *
 *  @param[in]      send_limit  保持している送出上限 (POTR_FLOW_SEND_LIMIT_HELD と通番の組。0 は上限なし)。
 *  @param[in]      received_ms 最後にフロー制御パケットを受けた時刻 [ms]。
 *  @param[in,out]  stalled_ms  送出上限に達して止まった時刻 [ms] (0 は止まっていない)。
 *                              送出上限に達した最初の判定で now_ms を設定し、達していなければ 0 へ戻します。NULL は許可しません。
 *  @param[in]      next_seq    次に割り当てる DATA の通番。
 *  @param[in]      now_ms      現在の単調増加時刻 [ms]。
 *  @param[in]      lifetime_ms 有効期限 [ms]。
 *  @param[out]     until_ms    POTR_SEND_LIMIT_REACHED の場合に、送出上限を破棄する時刻 [ms] の格納先。
 *                              それ以外の場合は変更しません。NULL は許可しません。
 *  @param[out]     allowance   POTR_SEND_LIMIT_OPEN の場合に、送出上限に達するまでに送出できる DATA のパケット数の格納先。
 *                              上限を保持していない場合とそれ以外の結果では UINT32_MAX を格納します。NULL は許可しません。
 *  @return         判定結果。失敗モードのない判定のため共通結果コードの適用対象外。
 *
 *  @par            スレッド セーフ
 *  値だけを扱う関数です。stalled_ms の排他は呼び出し元が行います。
 */
static inline potr_internal_send_limit_state flow_send_limit_judge(uint64_t send_limit, uint64_t received_ms,
                                                                   uint64_t *stalled_ms, uint32_t next_seq,
                                                                   uint64_t now_ms, uint64_t lifetime_ms,
                                                                   uint64_t *until_ms, uint32_t *allowance)
{
    uint32_t limit;
    uint64_t since_ms;

    *allowance = UINT32_MAX;

    if ((send_limit & POTR_FLOW_SEND_LIMIT_HELD) == 0U)
    {
        *stalled_ms = 0U;
        return POTR_SEND_LIMIT_OPEN;
    }

    limit = (uint32_t)(send_limit & 0xFFFFFFFFU);

    /* 通番は循環するため、差を符号付きで見て、送出上限が次の通番より先にあるかを判定する。 */
    if ((int32_t)(limit - next_seq) > 0)
    {
        *stalled_ms = 0U;
        *allowance = limit - next_seq;
        return POTR_SEND_LIMIT_OPEN;
    }

    if (*stalled_ms == 0U)
    {
        *stalled_ms = now_ms;
    }

    since_ms = *stalled_ms;
    if (received_ms > since_ms)
    {
        since_ms = received_ms;
    }

    if (since_ms + lifetime_ms > now_ms)
    {
        *until_ms = since_ms + lifetime_ms;
        return POTR_SEND_LIMIT_REACHED;
    }

    return POTR_SEND_LIMIT_EXPIRED;
}

struct potr_internal_peer_context;

/**
 *  @brief  サービス定義とグローバル設定を重ねた、フロー制御の実効設定。
 */
typedef struct potr_internal_flow_control_config
{
    uint32_t capacity;            /**< 通知キューの容量 [byte]。0 = 受信側のフロー制御は無効。 */
    uint32_t suppress_percent;    /**< 抑制を要求するしきい値 [%]。 */
    uint32_t resume_percent;      /**< 解除するしきい値 [%]。 */
    uint32_t request_interval_ms; /**< 抑制の要求の再送周期 [ms]。 */
    uint32_t request_lifetime_ms; /**< 抑制の要求の有効期限 [ms]。 */
    uint32_t receiver_percent;    /**< 一対多の割合の条件 [%]。0 = 無効。 */
    uint32_t receiver_count;      /**< 一対多の台数の条件。0 = 無効。 */
    uint32_t _pad;                /**< パディング (構造体サイズを 8 [byte] の倍数に揃える)。 */
} potr_internal_flow_control_config;

#ifdef __cplusplus
extern "C"
{
#endif
    /**
     *  @brief          フロー制御の実効設定を決定し、検査します。
     *
     *  サービス側の 0 はグローバル設定の値を使用します。サービス側の POTR_FLOW_CONTROL_DISABLED は、
     *  容量、一対多の割合、一対多の台数に限り「無効」(0) として扱います。\n
     *  再送周期と有効期限は、待機時間の設定値の上限 (POTR_WAIT_MS_MAX) 以下であることを検査します。
     *  容量が 0 でない場合は、要求のしきい値から容量までの余裕 (容量 − 容量 × 要求のしきい値 ÷ 100) が
     *  max_message_size 以上であることを検査します。\n
     *  RAW 系の通信種別では、設定を検査せず、容量を 0 (無効) とします。
     *
     *  @param[in]      service             サービス定義。
     *  @param[in]      global              グローバル設定。
     *  @param[in]      role                役割。
     *  @param[in]      health_interval_ms  通信種別とサービス上書きを解決した実効 PING 送信間隔 [ms]。
     *  @param[out]     config              実効設定の格納先。失敗時の内容は不定です。
     *  @return         成功時は POTR_OK、引数が NULL の場合と設定値が許容範囲にない場合は
     *                  POTR_ERR_INVALID_ARGUMENT を返します。
     */
    extern int potr_internal_flow_control_resolve(const potr_service_def *service, const potr_global_config *global,
                                                  potr_role role, uint32_t health_interval_ms,
                                                  potr_internal_flow_control_config *config);

    /**
     *  @brief          受信側のフロー制御の資源を生成します。
     *  @note           実効設定の容量が 0 の場合は何も生成せず、以後の各関数は何もしません。
     *                  受信スレッドと通知スレッドの起動前に呼びます。
     *  @param[in,out]  ctx     実効設定 (flow_control_config) と global.max_payload を設定済みのコンテキスト。
     *  @return         成功時は POTR_OK、ctx が NULL の場合と、容量が 0 でなく max_payload が 0 の場合は
     *                  POTR_ERR_INVALID_ARGUMENT、
     *                  メモリ確保失敗時は POTR_ERR_OUT_OF_MEMORY、同期資源の生成失敗時は POTR_ERR_UNKNOWN を返します。
     *                  失敗時は生成済みの資源を回収します。
     */
    extern int potr_internal_flow_control_start(potr_context *ctx);

    /**
     *  @brief          受信側のフロー制御の資源を解放します。
     *  @note           受信スレッドと通知スレッドの終了待ち後に呼びます。未生成の場合と ctx が NULL の場合は何もしません。
     *  @param[in,out]  ctx     コンテキスト。
     */
    extern void potr_internal_flow_control_stop(potr_context *ctx);

    /**
     *  @brief          通知キューへ登録する DATA を占有量へ加算します。
     *
     *  加算後の占有量が要求のしきい値を超えた場合は、抑制の要求を即時に送出します。
     *  抑制の要求には、その時点の送出上限 (potr_internal_flow_control_on_received() の説明を参照) を載せます。\n
     *  加算すると容量を超える場合は加算せず、その相手を「受信を再開しない」状態にして POTR_ERR_FULL を返します。
     *  呼び出し元は DATA を破棄し、その相手との通信を切断として扱います。
     *
     *  @param[in,out]  ctx             コンテキスト。
     *  @param[in]      peer_id         相手のピア識別子 (N:1 モード以外では POTR_PEER_NA)。
     *  @param[in]      session_id      相手のセッション識別子。
     *  @param[in]      session_ts      相手のセッション開始時刻。NULL は許可しません。
     *  @param[in]      len             DATA のデータ長 [byte]。
     *  @return         加算した場合と、フロー制御が無効の場合は POTR_OK、容量を超える場合は POTR_ERR_FULL を返します。
     *
     *  @par            スレッド セーフ
     *  受信スレッドから呼びます。N:1 モードでは、呼び出し元が peers_mutex を保持していること。
     */
    extern int potr_internal_flow_control_admit(potr_context *ctx, potr_peer_id peer_id, uint32_t session_id,
                                                const cplat_timespec *session_ts, size_t len);

    /**
     *  @brief          コールバックから復帰した DATA を占有量から減算します。
     *
     *  減算後の占有量が解除のしきい値を下回った場合は、解除 (新しい送出上限を載せる) を即時に 1 回送出し、
     *  「受信を再開しない」状態を取り消します。
     *
     *  @param[in,out]  ctx             コンテキスト。
     *  @param[in]      peer_id         通知に付与したピア識別子。
     *  @param[in]      len             DATA のデータ長 [byte]。
     *
     *  @par            スレッド セーフ
     *  通知スレッドから呼びます。呼び出し元はロックを保持しないこと (N:1 モードでは内部で peers_mutex を取得します)。
     */
    extern void potr_internal_flow_control_release(potr_context *ctx, potr_peer_id peer_id, size_t len);

    /**
     *  @brief          受信を終えた位置を反映し、必要な場合に送出上限を送信側へ知らせます。
     *
     *  送出上限は「次に受信する通番 + 許可数」です。許可数は、占有量が「容量 × 要求のしきい値」を
     *  超えている場合は 0、それ以外は (「容量 × 要求のしきい値」 − 占有量) ÷ max_payload (切り捨て) + 1 です。\n
     *  次の時点でフロー制御パケットを送出します。
     *  - 相手にまだ送出上限を知らせていない場合 (相手を新しい通信として採用した後、または次に受信する通番が
     *    前回知らせた時点より前へ戻った後)。状態の値は、要求中なら抑制の要求、それ以外は解除とします。
     *    送出できない間 (証明書方式でハンドシェイクが完了していない間など) は、
     *    potr_internal_flow_control_tick() が周期ごとに送出し直します。
     *  - data_packet が非 0 で、要求なしの状態の場合:
     *    - 相手が送出上限まで送り終えた時点で、占有量が解除のしきい値以上であれば、抑制の要求を送出して要求中へ遷移します。
     *      解除のしきい値を下回っていれば、送出上限の更新 (状態の値は解除) を送出します。
     *      相手が上限で止まっている間に、受信側が何も送出しない状態を作らないためです。
     *    - 前回知らせた範囲 (前回の送出の時点の「次に受信する通番」から送出上限まで) の半分以上を受信し、
     *      占有量が解除のしきい値を下回っていれば、送出上限の更新を送出します。
     *
     *  状態の遷移とパケットの送出は、相手ごとの排他の下で一組として行います。\n
     *  フロー制御が無効の場合と、容量超過で受信を再開しない状態の相手には、何もしません。
     *
     *  @param[in,out]  ctx             コンテキスト。
     *  @param[in]      peer_id         相手のピア識別子 (N:1 モード以外では POTR_PEER_NA)。
     *  @param[in]      session_id      相手のセッション識別子。
     *  @param[in]      session_ts      相手のセッション開始時刻。NULL は許可しません。
     *  @param[in]      next_seq        順序どおりに受信を終えた DATA パケットの次の通番 (次に受信する通番)。
     *  @param[in]      data_packet     順序どおりに受信した DATA パケット (フラグメントの途中のパケットを含む) を
     *                                  1 個処理した直後の呼び出しでは非 0。PING の受理など、受信の位置を
     *                                  知らせるだけの呼び出しでは 0 とし、最初の送出だけを行います。
     *
     *  @par            スレッド セーフ
     *  受信スレッド (TCP では tcp_recv_mutex を保持した受信スレッド) から呼びます。
     *  N:1 モードでは、呼び出し元が peers_mutex を保持していること。
     */
    extern void potr_internal_flow_control_on_received(potr_context *ctx, potr_peer_id peer_id, uint32_t session_id,
                                                       const cplat_timespec *session_ts, uint32_t next_seq,
                                                       int data_packet);

    /**
     *  @brief          再送周期が経過した抑制の要求を再送し、まだ知らせていない送出上限を送出し直します。
     *
     *  N:1 モードでは、占有量が 0 になり、ピアが解放された相手の記録も解放します。
     *
     *  @param[in,out]  ctx     コンテキスト。
     *  @param[in]      now_ms  現在の単調増加時刻 [ms]。
     *
     *  @par            スレッド セーフ
     *  受信スレッドの周期処理から呼びます。呼び出し元はロックを保持しないこと
     *  (N:1 モードでは内部で peers_mutex を取得します)。
     */
    extern void potr_internal_flow_control_tick(potr_context *ctx, uint64_t now_ms);

    /**
     *  @brief          相手からの受信を再開しない状態かどうかを返します。
     *
     *  容量超過で切断した相手は、占有量が解除のしきい値を下回るまで、新しい通信として採用しません。
     *  切断と接続の通知が繰り返されることを避けるためです。
     *
     *  N:1 モードでは、ピア識別子またはセッション識別子が一致する相手を対象にします。
     *  ピアが監視のタイムアウトで解放された後に、同じセッションが新しいピアとして登録されることを防ぐためです。
     *
     *  @param[in,out]  ctx             コンテキスト。
     *  @param[in]      peer_id         相手のピア識別子 (N:1 モード以外と未登録の相手では POTR_PEER_NA)。
     *  @param[in]      session_id      相手のセッション識別子 (N:1 モードの照合に使用)。
     *  @param[in]      session_ts      相手のセッション開始時刻 (N:1 モードの照合に使用)。NULL は許可しません。
     *  @return         受信を再開しない状態の場合は 1、それ以外とフロー制御が無効の場合は 0 を返します。
     *                  失敗モードのない述語のため共通結果コードの適用対象外。
     *
     *  @par            スレッド セーフ
     *  任意のスレッドから呼べます。
     */
    extern int potr_internal_flow_control_refused(potr_context *ctx, potr_peer_id peer_id, uint32_t session_id,
                                                  const cplat_timespec *session_ts);

    /**
     *  @brief          相手から受信したフロー制御パケットを、送信側の状態へ反映します。
     *
     *  双方向 UDP と TCP で使います。抑制の要求では期限を「現在時刻 + 有効期限」へ更新し、
     *  解除では要求なしへ戻します。状態の値によらず、その相手の送出上限を上書きし、受けた時刻を記録します。
     *  N:1 モードでは、要求または送出上限を保持しているピアの数 (potr_context::flow_tracked_peers) へ反映します。
     *  そのうえで、送信スレッドへ通知します。\n
     *  形式が不正なパケット (ペイロードが POTR_FLOW_CONTROL_PAYLOAD_SIZE バイトでない、状態が不正など) と、
     *  現在時刻を取得できない場合は無視します。\n
     *  片方向 UDP の送信者は、受信者の管理表 (potr_internal_receiver_accept()) で期限と送出上限を管理します。
     *
     *  @param[in,out]  ctx     コンテキスト。
     *  @param[in,out]  peer    N:1 モードの相手のピア。それ以外では NULL。
     *  @param[in]      pkt     開封済みのフロー制御パケット。呼び出し元が、追跡中の相手セッションのものであることを確認すること。
     *
     *  @par            スレッド セーフ
     *  受信スレッドから呼びます。N:1 モードでは、呼び出し元が peers_mutex を保持していること。
     */
    extern void potr_internal_flow_control_on_packet(potr_context *ctx, struct potr_internal_peer_context *peer,
                                                     const potr_packet *pkt);

    /**
     *  @brief          送信側が、相手が「止める状態」であるために新しい DATA の送出を止めるべきかを返します。
     *
     *  相手が「止める状態」であるとは、有効な (期限内の) 抑制の要求があるか、受けた送出上限に達している
     *  (次に割り当てる DATA の通番が送出上限以上である) ことです。片方向 UDP の multicast と broadcast は、
     *  「止める状態」の受信者を potr_internal_receiver_flow_suppressed() の条件で数えます。\n
     *  送出上限に達した時点から有効期限が経過しても次のフロー制御パケットが届かない場合は、送出上限を破棄して再開します。
     *  相手を切断と判定した場合は、その相手の要求と送出上限を取り消します。\n
     *  N:1 モード以外で使います。close の開始後は、抑制を無視するため常に 0 を返します。
     *
     *  @param[in,out]  ctx         コンテキスト。
     *  @param[in]      now_ms      現在の単調増加時刻 [ms]。
     *  @param[out]     wait_ms     抑制する場合に、次に判定し直すまでの待機の上限 [ms] の格納先。NULL は許可しません。
     *  @param[out]     allowance   抑制しない場合に、送出上限に達するまでに送出できる DATA のパケット数の格納先。
     *                              送出上限を保持していない場合と片方向 UDP では UINT32_MAX を格納します。NULL は許可しません。
     *  @return         抑制する場合は 1、しない場合は 0。失敗モードのない述語のため共通結果コードの適用対象外。
     *
     *  @par            スレッド セーフ
     *  送信スレッドから呼びます。呼び出し元はロックを保持しないこと
     *  (内部で send_window_mutex と callback_mutex を順に、重ねずに取得します)。
     */
    extern int potr_internal_flow_control_suppressed(potr_context *ctx, uint64_t now_ms, int *wait_ms,
                                                     uint32_t *allowance);

    /**
     *  @brief          N:1 モードのピアが「止める状態」(有効な抑制の要求がある、または送出上限に達している) かを返します。
     *
     *  送出上限に達した時点から有効期限が経過しても次のフロー制御パケットが届かない場合は、送出上限を破棄します。
     *  期限が経過した要求は、要求なしへ戻します。\n
     *  切断と判定したピアは、要求と送出上限を取り消して 0 を返します。
     *  要求と送出上限の変化は、要求または送出上限を保持しているピアの数 (potr_context::flow_tracked_peers) へ反映します。\n
     *  close の開始後は、抑制を無視するため常に 0 を返します。存在しないピアは 0 を返します。
     *
     *  @param[in,out]  ctx         コンテキスト。
     *  @param[in]      peer_id     ピア識別子。
     *  @param[in]      now_ms      現在の単調増加時刻 [ms]。
     *  @param[out]     deferred    送信スレッドが保留している、このピア宛のエントリ数の格納先。NULL は許可しません。
     *  @return         抑制する場合は 1、しない場合は 0。失敗モードのない述語のため共通結果コードの適用対象外。
     *
     *  @par            スレッド セーフ
     *  任意のスレッドから呼べます。内部で peers_mutex、ピアの send_window_mutex の順に取得するため、
     *  呼び出し元はどちらも保持しないこと。
     */
    extern int potr_internal_flow_control_peer_state(potr_context *ctx, potr_peer_id peer_id, uint64_t now_ms,
                                                     int *deferred);
#ifdef __cplusplus
}
#endif
#endif /* POTR_FLOW_CONTROL_H */
