/**
 *******************************************************************************
 *  @file           potr_flow_control.h
 *  @brief          受信側の通知キューの占有量に基づくフロー制御を管理します。
 *
 *  受信側は、相手ごとに通知キューの占有量 (コールバックから復帰していない DATA のバイト数) を数えます。
 *  占有量が要求のしきい値を超えたら抑制の要求を、解除のしきい値を下回ったら解除を、
 *  フロー制御パケットで送信側へ伝えます。\n
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
 *  @brief  サービス定義とグローバル設定を重ねた、フロー制御の実効設定。
 */
struct potr_internal_peer_context;

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
     *  @param[in,out]  ctx     実効設定 (flow_control_config) を設定済みのコンテキスト。
     *  @return         成功時は POTR_OK、ctx が NULL の場合は POTR_ERR_INVALID_ARGUMENT、
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
     *  加算後の占有量が要求のしきい値を超えた場合は、抑制の要求を即時に送出します。\n
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
     *  減算後の占有量が解除のしきい値を下回った場合は、解除を即時に 1 回送出し、
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
     *  @brief          再送周期が経過した抑制の要求を再送します。
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
     *  解除では要求なしへ戻して、送信スレッドへ通知します。形式が不正なパケットは無視します。\n
     *  片方向 UDP の送信者は、受信者の管理表 (potr_internal_receiver_accept()) で期限を管理します。
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
     *  @brief          送信側が、相手からの抑制の要求によって新しい DATA の送出を止めるべきかを返します。
     *
     *  N:1 モード以外で使います。close の開始後は、抑制を無視するため常に 0 を返します。
     *
     *  @param[in,out]  ctx         コンテキスト。
     *  @param[in]      now_ms      現在の単調増加時刻 [ms]。
     *  @param[out]     wait_ms     抑制する場合に、次に判定し直すまでの待機の上限 [ms] の格納先。NULL は許可しません。
     *  @return         抑制する場合は 1、しない場合は 0。失敗モードのない述語のため共通結果コードの適用対象外。
     *
     *  @par            スレッド セーフ
     *  送信スレッドから呼びます。
     */
    extern int potr_internal_flow_control_suppressed(potr_context *ctx, uint64_t now_ms, int *wait_ms);

    /**
     *  @brief          N:1 モードのピアが、抑制の要求によって送出を止めるべき状態かを返します。
     *
     *  close の開始後は、抑制を無視するため常に 0 を返します。存在しないピアと未接続のピアは 0 を返します。
     *
     *  @param[in,out]  ctx         コンテキスト。
     *  @param[in]      peer_id     ピア識別子。
     *  @param[in]      now_ms      現在の単調増加時刻 [ms]。
     *  @param[out]     deferred    送信スレッドが保留している、このピア宛のエントリ数の格納先。NULL は許可しません。
     *  @return         抑制する場合は 1、しない場合は 0。失敗モードのない述語のため共通結果コードの適用対象外。
     *
     *  @par            スレッド セーフ
     *  任意のスレッドから呼べます。内部で peers_mutex を取得するため、呼び出し元は peers_mutex を保持しないこと。
     */
    extern int potr_internal_flow_control_peer_state(potr_context *ctx, potr_peer_id peer_id, uint64_t now_ms,
                                                     int *deferred);
#ifdef __cplusplus
}
#endif
#endif /* POTR_FLOW_CONTROL_H */
