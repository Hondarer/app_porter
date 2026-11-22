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
 *  相手は、N:1 モードではピアごと、それ以外ではハンドルの通信相手 1 つです。\n
 *  再送を行う UDP の通信種別の受信側は、容量の設定によらず、OS の受信バッファーに収まると見積もった
 *  DATA パケットの数 (受信バッファーの幅) で送出上限の許可数を抑えます。\n
 *  multicast と broadcast では、送出上限を知らせる受信者を、送信者が選んだ監視対象に限ります。
 *******************************************************************************
 */
#ifndef POTR_FLOW_CONTROL_H
#define POTR_FLOW_CONTROL_H
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <cplat/clock/clock.h>
#include <porter/porter_const.h>
#include <porter/porter_type.h>

/**
 *  送信側が保持する送出上限の状態を表す上位 32 ビットのマスクと値。
 *  1 つの 64 ビット値にまとめるのは、受信スレッドの書き込みと送信スレッドの読み取りをアトミックに 1 回で行うためである。
 *  - 未通知 (0): 下位 32 ビットは基準通番 (最初に割り当てた通番)。有効な上限は「基準通番 + POTR_FLOW_INITIAL_WIDTH」。
 *  - 上限あり (1): 下位 32 ビットは相手から受けた送出上限。
 *  - 上限なし (2): 相手から「上限なし」を受けた。相手のセッションが変わるまで制限しない。
 *  - 相手なし (3): 止まったまま有効期限の間、相手から何も届かなかった。制限しないが、パケットが届いたら未通知へ戻す。
 */
#define POTR_FLOW_SEND_LIMIT_STATE_MASK 0xFFFFFFFF00000000ULL
#define POTR_FLOW_SEND_LIMIT_UNNOTIFIED 0x0000000000000000ULL
#define POTR_FLOW_SEND_LIMIT_HELD       0x0000000100000000ULL
#define POTR_FLOW_SEND_LIMIT_UNLIMITED  0x0000000200000000ULL
#define POTR_FLOW_SEND_LIMIT_NO_PEER    0x0000000300000000ULL

/** 送信側が相手から最初の通知を受けるまでの初期の幅 [パケット]。POTR_FLOW_RECEIVE_BUFFER_WIDTH_MIN と同じ値だが、意味が異なるため別定数とする。 */
#define POTR_FLOW_INITIAL_WIDTH 4U

/** 送出上限または初期の幅で止まり始めてから、最初の問い合わせを送出するまでの時間 [ms]。 */
#define POTR_FLOW_QUERY_INITIAL_INTERVAL_MS 10U

static_assert((POTR_FLOW_SEND_LIMIT_HELD & (uint64_t)UINT32_MAX) == 0ULL &&
                  POTR_FLOW_SEND_LIMIT_HELD > (uint64_t)UINT32_MAX,
              "porter: POTR_FLOW_SEND_LIMIT_HELD must not overlap the 32-bit sequence number");

/** 送信側の送出上限の判定結果。 */
typedef enum potr_internal_send_limit_state
{
    POTR_SEND_LIMIT_OPEN = 0,    /**< 送出上限を保持していない、または送出上限に達していない。 */
    POTR_SEND_LIMIT_REACHED = 1, /**< 送出上限に達しており、止まった時点から有効期限が経過していない。 */
    POTR_SEND_LIMIT_EXPIRED = 2  /**< 送出上限に達して止まった時点から、次のパケットを受けずに有効期限が経過した。 */
} potr_internal_send_limit_state;

/**
 *  @brief          送信側が、相手から受けた送出上限 (または未通知時の初期の幅) に達しているかを判定します。
 *
 *  通番は循環を考慮した差で比較します。送出上限は、同じ通知の重複と古い通知の喪失で判定が変わらないよう、
 *  バイト数ではなく通番で表します。\n
 *  状態が「上限なし」(POTR_FLOW_SEND_LIMIT_UNLIMITED) または「相手なし」(POTR_FLOW_SEND_LIMIT_NO_PEER) の場合は
 *  制限せず POTR_SEND_LIMIT_OPEN を返します。\n
 *  状態が「未通知」(POTR_FLOW_SEND_LIMIT_UNNOTIFIED) の場合は、「下位 32 ビット + POTR_FLOW_INITIAL_WIDTH」を上限とします。\n
 *  状態が「上限あり」(POTR_FLOW_SEND_LIMIT_HELD) の場合は、下位 32 ビットを上限とします。\n
 *  止まった時点 (stalled_ms) と最後にパケットを受けた時刻 (received_ms) の遅い方から
 *  有効期限が経過した場合は POTR_SEND_LIMIT_EXPIRED を返します。
 *
 *  @param[in]      send_limit  保持している送出上限 (上位 32 ビットの状態と下位 32 ビットの通番の組)。
 *  @param[in]      received_ms 最後に判定対象パケットを受けた時刻 [ms]。
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
    uint64_t state = send_limit & POTR_FLOW_SEND_LIMIT_STATE_MASK;
    uint32_t limit;
    uint64_t since_ms;

    *allowance = UINT32_MAX;

    if (state == POTR_FLOW_SEND_LIMIT_UNLIMITED || state == POTR_FLOW_SEND_LIMIT_NO_PEER)
    {
        *stalled_ms = 0U;
        return POTR_SEND_LIMIT_OPEN;
    }

    if (state == POTR_FLOW_SEND_LIMIT_UNNOTIFIED)
    {
        limit = (uint32_t)(send_limit & 0xFFFFFFFFU) + (uint32_t)POTR_FLOW_INITIAL_WIDTH;
    }
    else
    {
        limit = (uint32_t)(send_limit & 0xFFFFFFFFU);
    }

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

/**
 *  @brief          上限あり (POTR_FLOW_SEND_LIMIT_HELD) の送出上限を、「next_seq + 初期の幅」との小さいほうへ縮めます。
 *
 *  送信が途切れた後に未使用の許可を減らすために使います。上限あり以外の状態 (未通知、上限なし、相手なし) は
 *  変更せずそのまま返します。通番は循環を考慮した差で比較します。
 *
 *  @param[in]      send_limit  現在の送出上限の状態と通番の組。
 *  @param[in]      next_seq    次に割り当てる DATA の通番。
 *  @return         縮めた後の送出上限の状態と通番の組。失敗モードのない計算のため共通結果コードの適用対象外。
 */
static inline uint64_t flow_send_limit_clamp_initial(uint64_t send_limit, uint32_t next_seq)
{
    uint32_t held;
    uint32_t cap;

    if ((send_limit & POTR_FLOW_SEND_LIMIT_STATE_MASK) != POTR_FLOW_SEND_LIMIT_HELD)
    {
        return send_limit;
    }

    held = (uint32_t)(send_limit & 0xFFFFFFFFU);
    cap = next_seq + (uint32_t)POTR_FLOW_INITIAL_WIDTH;

    /* 通番は循環するため、差を符号付きで見て小さいほうを選ぶ。 */
    if ((int32_t)(cap - held) < 0)
    {
        return POTR_FLOW_SEND_LIMIT_HELD | (uint64_t)cap;
    }

    return send_limit;
}

/** 受信バッファーの幅の下限 [パケット]。 */
#define POTR_FLOW_RECEIVE_BUFFER_WIDTH_MIN 4U

/** N:1 モードで順番の許可へ切り替えるピアごとの幅のしきい値、および 1 回の順番の許可のパケット数 [パケット]。 */
#define POTR_FLOW_TURN_WIDTH 4U

/** 受信バッファーの幅の計算で、1 パケットの見積もりに加える管理情報の量 [byte]。 */
#define POTR_FLOW_RECEIVE_BUFFER_OVERHEAD 2048U

static_assert(POTR_FLOW_RECEIVE_BUFFER_WIDTH_MIN >= 1U && POTR_FLOW_RECEIVE_BUFFER_OVERHEAD > 0U,
              "porter: receive buffer width parameters must be positive");
static_assert(POTR_FLOW_TURN_WIDTH >= 1U && POTR_FLOW_TURN_WIDTH <= POTR_FLOW_RECEIVE_BUFFER_WIDTH_MIN,
              "porter: POTR_FLOW_TURN_WIDTH must be between 1 and POTR_FLOW_RECEIVE_BUFFER_WIDTH_MIN");

/**
 * 抑制中の状態を確認し直すまでの待機の上限 [ms]。
 * 送信スレッドは、抑制の要求で止まっている間、抑制の状態と close の開始を、この時間以内に確認し直す
 * (potr_internal_flow_control_suppressed() が返す待機時間の上限)。
 */
#define POTR_INTERNAL_FLOW_RECHECK_MS 50

static_assert(POTR_INTERNAL_FLOW_RECHECK_MS > 0 && POTR_INTERNAL_FLOW_RECHECK_MS < INT_MAX,
              "porter: POTR_INTERNAL_FLOW_RECHECK_MS out of range");

/**
 * N:1 モードで、止める状態のピアを待っている間に、状態を確認し直すまでの待機の上限 [ms]。
 * 送信の呼び出し (potr_service_send) と、保留中のエントリがある送信スレッドが使う。
 * 相手の送出上限または初期の幅で止まっている間の問い合わせを、最初の問い合わせまでの時間
 * (POTR_FLOW_QUERY_INITIAL_INTERVAL_MS) の精度で送出するため、同じ値にする。
 */
#define POTR_INTERNAL_FLOW_LIMIT_RECHECK_MS ((int)POTR_FLOW_QUERY_INITIAL_INTERVAL_MS)

static_assert(POTR_INTERNAL_FLOW_LIMIT_RECHECK_MS > 0 &&
                  POTR_INTERNAL_FLOW_LIMIT_RECHECK_MS <= POTR_INTERNAL_FLOW_RECHECK_MS,
              "porter: POTR_INTERNAL_FLOW_LIMIT_RECHECK_MS out of range");

/** 百分率 [%] の上限。 */
#define POTR_PERCENT_MAX 100U

/** 百分率で求めた値を切り上げるために、割る前に加える値。 */
#define POTR_PERCENT_CEIL_OFFSET (POTR_PERCENT_MAX - 1U)

static_assert(POTR_PERCENT_MAX == 100U, "porter: POTR_PERCENT_MAX must be 100");

/**
 * 止めていない間の積算の減算率の分母 (経過時間 ÷ 9)。
 * 積算 (flow_stall_accum_units) はこの定数倍 (1/9 [ms] 単位) で保持し、止めていた間は「経過時間 × 9」を加算、
 * 止めていない間は「経過時間」をそのまま減算することで、1 [ms] 刻みの判定でも端数による誤差なく減算する。
 */
#define POTR_FLOW_ACCUM_DECAY_DIVISOR 9U

/** N:1 モードで 1 回の周期判定で切断対象として収集するピア数の上限。 */
#define POTR_FLOW_SLOW_PEER_BATCH_MAX 64U

/**
 * N:1 モードで、全体を妨げている遅いピアとして切断したセッションの受け入れ拒否記録の上限数。
 * 除外する数の上限は接続中のピア数で変わるため固定長とし、既定の max_peers (POTR_DEFAULT_MAX_PEERS = 1024) と
 * 既定の割合 (10% -> 102) を収めつつ、コンテキスト内の固定配列と線形探索を小さく保つ 128 とする。
 */
#define POTR_FLOW_SLOW_PEER_REJECT_MAX 128U

static_assert(POTR_FLOW_ACCUM_DECAY_DIVISOR == 9U, "porter: POTR_FLOW_ACCUM_DECAY_DIVISOR must be 9");
static_assert(POTR_FLOW_SLOW_PEER_BATCH_MAX >= 1U && POTR_FLOW_SLOW_PEER_BATCH_MAX <= 256U,
              "porter: POTR_FLOW_SLOW_PEER_BATCH_MAX out of range");
static_assert(POTR_FLOW_SLOW_PEER_REJECT_MAX >= POTR_FLOW_SLOW_PEER_BATCH_MAX &&
                  POTR_FLOW_SLOW_PEER_REJECT_MAX <= 1024U,
              "porter: POTR_FLOW_SLOW_PEER_REJECT_MAX out of range");

/** N:1 モードで遅いピアとして切断したセッションの受け入れ拒否記録の 1 エントリ。 */
typedef struct potr_internal_slow_peer_reject_entry
{
    cplat_timespec session_ts; /**< 切断したピアのセッション開始時刻。 */
    uint64_t disconnected_ms;  /**< 切断した単調増加時刻 [ms]。 */
    uint64_t record_seq;       /**< 記録順の連番 (同時刻の古い順判定に使用)。 */
    uint32_t session_id;       /**< 切断したピアのセッション識別子。 */
    uint8_t active;            /**< 1: 有効な記録, 0: 未使用。 */
    uint8_t drop_logged;       /**< 1: 期間内のパケット破棄を INFO へ記録済み, 0: 未記録。 */
    uint8_t _pad[2];           /**< パディング (構造体を 8 [byte] 境界に揃える)。 */
} potr_internal_slow_peer_reject_entry;

/**
 *  @brief          除外または切断できる相手 (受信者またはピア) の数の上限を求めます。
 *
 *  上限は「excluded_count」と「active × excluded_percent ÷ 100 (切り捨て)」の大きいほうとします。
 *  ただし、「active − 1」を超えません (active が 0 または 1 の場合は 0)。
 *  無効 (0) の項目は 0 として計算し、両方が 0 の場合は 0 を返します。
 *
 *  @param[in]      active              活動中の受信者数 (1 対 N) または接続中のピア数 (N 対 1)。
 *  @param[in]      excluded_count      除外または切断する数の上限 [台]。0 = 無効。
 *  @param[in]      excluded_percent    除外または切断する数の上限 [%]。0 = 無効。
 *  @return         除外または切断する数の上限 [台]。失敗モードのない計算のため結果コードの対象外です。
 */
static inline size_t flow_excluded_limit(size_t active, uint32_t excluded_count, uint32_t excluded_percent)
{
    size_t by_count = (size_t)excluded_count;
    size_t by_percent;
    size_t limit;

    if (active <= 1U)
    {
        return 0U;
    }

    by_percent = (size_t)(((uint64_t)active * (uint64_t)excluded_percent) / (uint64_t)POTR_PERCENT_MAX);
    limit = (by_count > by_percent) ? by_count : by_percent;

    if (limit > active - 1U)
    {
        limit = active - 1U;
    }

    return limit;
}

/**
 *  @brief          止める状態の積算値 [1/9 ms] を、経過時間 [ms] に応じて更新します。
 *
 *  全体の送出時間のうち約 10% までの停止を一時的な遅れとして受け入れ、それを超える遅い相手だけを
 *  除外または切断するための積算式です。
 *  - stalled が非 0 (止める状態): 積算 + 経過時間 × POTR_FLOW_ACCUM_DECAY_DIVISOR
 *  - stalled が 0 (止めていない): 積算 − 経過時間 (0 を下回らない)
 *
 *  @param[in]      accum_units 現在の積算値 [1/9 ms]。
 *  @param[in]      elapsed_ms  前回の判定からの経過時間 [ms]。
 *  @param[in]      stalled     止める状態として加算する場合は非 0、減算する場合は 0。
 *  @return         更新後の積算値 [1/9 ms]。失敗モードのない計算のため結果コードの対象外です。
 */
static inline uint64_t flow_stall_accum_update(uint64_t accum_units, uint64_t elapsed_ms, int stalled)
{
    if (elapsed_ms == 0U)
    {
        return accum_units;
    }

    if (stalled != 0)
    {
        return accum_units + elapsed_ms * (uint64_t)POTR_FLOW_ACCUM_DECAY_DIVISOR;
    }

    if (elapsed_ms >= accum_units)
    {
        return 0U;
    }

    return accum_units - elapsed_ms;
}

/**
 *  @brief          OS の受信バッファーの大きさから、受信バッファーの幅 (収まると見積もった DATA パケットの数) を求めます。
 *
 *  パケット長 = ヘッダー長 + max_payload + 認証タグ長 (暗号化の有無によらず加える)、
 *  1 パケットの見積もり = 2 × パケット長 + POTR_FLOW_RECEIVE_BUFFER_OVERHEAD、
 *  幅 = OS の受信バッファーの大きさ × percent ÷ 100 ÷ 1 パケットの見積もり (切り捨て) です。\n
 *  Linux は、受信したパケットを、データの長さではなく、管理情報を含む確保済みの領域の大きさで数えます。
 *  領域は 2 のべき乗の大きさで確保されるため、最大でデータの長さの約 2 倍に管理情報を加えた量になります。
 *  見積もりは、この数え方に対する安全側の値です。
 *  see: https://man7.org/linux/man-pages/man7/socket.7.html \n
 *  N:1 モードのピア数による分割と、下限 (POTR_FLOW_RECEIVE_BUFFER_WIDTH_MIN) の適用は、呼び出し元が行います。
 *
 *  @param[in]      buffer_size OS の受信バッファーの大きさ [byte] (SO_RCVBUF で取得した値)。
 *  @param[in]      percent     幅の計算に使う割合 [%] (1〜100)。
 *  @param[in]      max_payload 受信側の実効 max_payload [byte]。
 *  @return         受信バッファーの幅 [パケット]。失敗モードのない計算のため共通結果コードの適用対象外。
 */
static inline uint64_t flow_receive_buffer_width(uint64_t buffer_size, uint32_t percent, uint32_t max_payload)
{
    uint64_t packet_size = (uint64_t)POTR_PACKET_HEADER_WIRE_SIZE + (uint64_t)max_payload + (uint64_t)POTR_CRYPTO_TAG_SIZE;
    uint64_t estimate = 2U * packet_size + (uint64_t)POTR_FLOW_RECEIVE_BUFFER_OVERHEAD;

    /* buffer_size は int の範囲 (SO_RCVBUF)、percent は 100 以下のため、乗算は 64 ビットで桁あふれしない。 */
    return buffer_size * (uint64_t)percent / 100U / estimate;
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
    uint32_t excluded_receiver_count;    /**< 除外または切断する数の上限 [台]。0 = 無効。 */
    uint32_t excluded_receiver_percent;  /**< 除外または切断する数の上限 [%]。0 = 無効。 */
    uint32_t grace_ms;                   /**< 遅い相手が全体を止めてよい猶予時間 [ms]。 */
    uint32_t feedback_percent;           /**< 監視対象から受けるフロー制御パケットの数の予算 (DATA 100 パケットあたりの数)。 */
    uint32_t monitored_receiver_count;   /**< multicast と broadcast の送信側が監視する受信者の台数の下限 [台]。0 = 下限なし。 */
    uint32_t receive_buffer_percent; /**< 受信バッファーの幅の計算に使う割合 [%]。0 = OS の受信バッファーに基づく送出上限を使わない (無効、または対象外の通信種別と役割)。 */
    uint32_t _pad; /**< パディング (構造体サイズを 8 [byte] の倍数に揃える)。 */
} potr_internal_flow_control_config;

#ifdef __cplusplus
extern "C"
{
#endif
    /**
     *  @brief          フロー制御の実効設定を決定し、検査します。
     *
     *  サービス側の 0 はグローバル設定の値を使用します。サービス側の POTR_FLOW_CONTROL_DISABLED は、
     *  容量、監視する受信者の台数の下限、除外する数の上限 (台数と割合)、受信バッファーの幅の割合に限り
     *  「無効」(0) として扱います。除外する数の上限 (台数と割合) と受信バッファーの幅の割合は、
     *  グローバル設定の POTR_FLOW_CONTROL_DISABLED も「無効」(0) として扱い、グローバル設定の 0 は既定値とします。\n
     *  除外する数の上限の割合は 100 以下、予算 (feedback_percent) は 1 から 100、監視する受信者の台数の下限は
     *  POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX 以下、受信バッファーの幅の割合は無効でなければ 1 から 100 であることを検査します。\n
     *  受信バッファーの幅の割合は、OS の受信バッファーに基づく送出上限を使わない場合に 0 とします。
     *  使う受信側は、unicast、multicast、broadcast の受信者 (multicast と broadcast は health_interval_ms が 0 でない場合だけ)、
     *  unicast_bidir と unicast_bidir_n1 の両方の役割です。\n
     *  再送周期、有効期限、猶予時間は、待機時間の設定値の上限 (POTR_WAIT_MS_MAX) 以下であることを検査します。
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
     *  @note           OS の受信バッファーに基づく送出上限を使う受信側 (実効設定の receive_buffer_percent が 0 でない) は、
     *                  受信に使う全経路のソケットから OS の受信バッファーの大きさ (SO_RCVBUF) を取得し、最も小さい値から
     *                  受信バッファーの幅を求めます。取得できなかった場合は、警告を診断出力に記録し、
     *                  このハンドルでは OS の受信バッファーに基づく送出上限を使いません。\n
     *                  容量が 0 で、OS の受信バッファーに基づく送出上限も使わない受信側 (multicast と broadcast の受信者、
     *                  RAW 系、片方向送信者を除く) は、状態が「上限なし」の通知と問い合わせへの応答を行うために資源を生成します。
     *                  それ以外の構成では何も生成せず、以後の各関数は何もしません。
     *                  UDP では経路のソケットを開いた後、受信スレッドと通知スレッドの起動前に呼びます。
     *  @param[in,out]  ctx     実効設定 (flow_control_config) と global.max_payload を設定済みのコンテキスト。
     *  @return         成功時は POTR_OK、ctx が NULL の場合と、送出上限を使う構成で max_payload が 0 の場合は
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
     *  呼び出し元は DATA を破棄し、その相手との通信を切断として扱います。\n
     *  容量が 0 の場合 (OS の受信バッファーに基づく送出上限だけを使う場合) は、占有量を数えずに POTR_OK を返します。
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
     *  「受信を再開しない」状態を取り消します。容量が 0 の場合は何もしません。
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
     *  送出上限は「次に受信する通番 + 許可数」です。許可数は、「占有量による許可数」と「受信バッファーの幅」の
     *  小さいほうです。占有量による許可数は、占有量が「容量 × 要求のしきい値」を超えている場合は 0、
     *  それ以外は (「容量 × 要求のしきい値」 − 占有量) ÷ 1 パケットの大きさ (切り捨て) + 1 です。
     *  1 パケットの大きさは、max_payload と「そのセッションで受信した DATA パケット 1 個の展開後の大きさの最大値」の
     *  大きいほうを使います (セッションが変わると最大値は 0 に戻ります)。
     *  容量が 0 の受信側は受信バッファーの幅だけを、OS の受信バッファーに基づく送出上限を使わない受信側は
     *  占有量による許可数だけを使います。受信バッファーの全体の幅は POTR_FLOW_RECEIVE_BUFFER_WIDTH_MIN 以上とします。
     *  N:1 モードでは、すべてのピアが 1 つのソケットを共有するため、全体の幅を送信中のピア (直近の
     *  flow_control_request_interval_ms の間に DATA または問い合わせを受けたピア) の数で割ります (送信中のピアがない場合は 1)。
     *  ピアごとの幅が POTR_FLOW_TURN_WIDTH 以上の場合は全員にその幅で許可し、すでに知らせた送出上限は戻しません。
     *  POTR_FLOW_TURN_WIDTH 未満の場合は順番の許可へ切り替え、同時に許可を持つピアの数を
     *  「全体の幅 ÷ POTR_FLOW_TURN_WIDTH (切り捨て)」まで、1 回の許可を POTR_FLOW_TURN_WIDTH パケットとします。\n
     *  容量が 0 の受信側は、下の最初の送出と更新だけを行い、占有量の条件は常に満たすものとして扱います。\n
     *  multicast と broadcast の受信者は、送信者から受けた監視対象の一覧に自身が含まれている間だけ、
     *  最初の送出と更新を行います。監視対象でない間は、送出上限を知らせていないものとして扱い、
     *  送り終えた時点の判定も行いません。\n
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
     *  @param[in]      expanded_size   data_packet が非 0 の場合に、その DATA パケット 1 個の展開後の大きさ [byte]
     *                                  (非圧縮または中間フラグメントなどで max_payload 以下の場合は 0 でも可)。
     *                                  data_packet が 0 の場合は無視します。
     *
     *  @par            スレッド セーフ
     *  受信スレッド (TCP では tcp_recv_mutex を保持した受信スレッド) から呼びます。
     *  N:1 モードでは、呼び出し元が peers_mutex を保持していること。
     */
    extern void potr_internal_flow_control_on_received(potr_context *ctx, potr_peer_id peer_id, uint32_t session_id,
                                                       const cplat_timespec *session_ts, uint32_t next_seq,
                                                       int data_packet, size_t expanded_size);

    /**
     *  @brief          再送周期が経過した抑制の要求を再送し、まだ知らせていない送出上限を送出し直します。
     *
     *  N:1 モードでは、占有量が 0 になり、ピアが解放された相手の記録も解放します。また、直近の
     *  flow_control_request_interval_ms の間に DATA も問い合わせもないピアを送信中のピアの数から外し、
     *  順番の許可で枠が空いた場合は待っているピアへ許可を送出します。\n
     *  multicast と broadcast の受信者は、監視対象の一覧を flow_control_request_lifetime_ms の間受け取っていない場合、
     *  監視対象でなくなったものとして扱い、送出上限を知らせていない状態に戻します。
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
     *  @brief          N:1 モードでピアが解放されたことを受信側のフロー制御へ反映します。
     *
     *  解放されたピアが送信中のピアに数えられていれば数から外します。順番の許可で許可を持っていたピアが
     *  解放された場合は、空いた枠を待っている次のピアへ割り当てます。占有量が 0 で再開拒否中でない記録は
     *  その場で解放します。\n
     *  フロー制御が無効の場合、N:1 モード以外の場合、および ctx が NULL の場合は何もしません。
     *
     *  @param[in,out]  ctx     コンテキスト。
     *  @param[in]      peer_id 解放されたピアの識別子。
     *
     *  @par            スレッド セーフ
     *  呼び出し元が peers_mutex を保持し、fc->mutex を保持していないこと (内部で fc->mutex を取得します)。
     */
    extern void potr_internal_flow_control_on_peer_freed(potr_context *ctx, potr_peer_id peer_id);

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
     *  @brief          相手から受信したフロー制御パケットを処理します。
     *
     *  双方向 UDP と TCP、片方向 UDP の受信者 (監視対象の一覧および問い合わせ) で使います。\n
     *  状態が監視対象の一覧 (POTR_FLOW_CONTROL_STATE_MONITOR) のパケットは、multicast と broadcast の受信者だけが受理し、
     *  一覧に自身のセッション識別子が含まれていれば監視対象として、含まれていなければ監視対象でないものとして動作します。
     *  採用している送信者のセッションから自身を含む一覧を受けるたびに、次に受信する通番が分かっていれば、
     *  その時点の状態 (抑制の要求中は抑制の要求、そうでなければ解除) と送出上限をフロー制御パケットで 1 回送出します。
     *  そのほかの通信種別と役割、形式が不正な一覧は破棄します。\n
     *  状態が問い合わせ (POTR_FLOW_CONTROL_STATE_QUERY) のパケットは、採用している相手のセッションからのものだけを受理し、
     *  その時点の状態 (抑制の要求中は抑制の要求、そうでなければ解除、送出上限を使わないなら上限なし) と送出上限を
     *  フロー制御パケットで 1 回送出します。\n
     *  状態が上限なし (POTR_FLOW_CONTROL_STATE_UNLIMITED) のパケットでは、抑制の要求と送出上限を解除し、
     *  送信スレッドへ通知します。\n
     *  抑制の要求と解除では、抑制の要求では期限を「現在時刻 + 有効期限」へ更新し、
     *  解除では要求なしへ戻します。状態の値によらず、その相手の送出上限を上書きし、受けた時刻を記録します。
     *  N:1 モードでは、要求または送出上限を保持しているピアの数 (potr_context::flow_tracked_peers) へ反映します。
     *  そのうえで、送信スレッドへ通知します。\n
     *  形式が不正なパケットと、現在時刻を取得できない場合は無視します。\n
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
     *  @brief          UDP の送信側が相手へ「問い合わせ」のフロー制御パケットを 1 回送出します。
     *
     *  multicast、broadcast、TCP、RAW 系では何もしません。
     *
     *  @param[in,out]  ctx     コンテキスト。
     *  @param[in,out]  peer    N:1 モードの相手のピア。それ以外では NULL。
     *
     *  @par            スレッド セーフ
     *  N:1 モードでは、呼び出し元が peers_mutex を保持していること。
     */
    extern void potr_internal_flow_control_send_query(potr_context *ctx, struct potr_internal_peer_context *peer);

    /**
     *  @brief          双方向 UDP (1:1 および N:1) で相手から何らかのパケットを受けたことを記録します。
     *
     *  最後にパケットを受けた時刻を更新します。状態が「相手なし」(POTR_FLOW_SEND_LIMIT_NO_PEER) の場合は、
     *  その時点で次に割り当てる通番を基準とする「未通知」へ戻し、問い合わせを 1 回送出します
     *  (ただし、受けたパケット自体が問い合わせ以外のフロー制御パケット、または送出上限を相乗りした DATA パケットの場合は、
     *  問い合わせを重ねません)。\n
     *  受けたパケットが POTR_FLAG_DATA と POTR_FLAG_SEND_LIMIT の両方を持つ DATA パケットの場合は、
     *  ack_num に載っている送出上限が保持している値より先へ進んでいる場合 (または「未通知」「相手なし」の場合) だけ
     *  送出上限を更新し、受けた時刻を記録して送信スレッドへ通知します。「上限なし」の場合は更新しません。
     *  POTR_FLAG_SEND_LIMIT が立っていない DATA の ack_num は無視します。
     *
     *  @param[in,out]  ctx     コンテキスト。
     *  @param[in,out]  peer    N:1 モードの相手のピア。それ以外では NULL。
     *  @param[in]      pkt     受信したパケット。呼び出し元が、採用している相手のセッションのものであることを確認すること。
     *
     *  @par            スレッド セーフ
     *  受信スレッドから呼びます。N:1 モードでは、呼び出し元が peers_mutex を保持していること。
     */
    extern void potr_internal_flow_control_on_peer_packet(potr_context *ctx, struct potr_internal_peer_context *peer,
                                                          const potr_packet *pkt);

    /**
     *  @brief          逆方向の DATA パケットへ相乗りする送出上限を取得し、「前回知らせた範囲」を更新します。
     *
     *  unicast_bidir と unicast_bidir_n1 で、送出上限を使う側 (容量が 0 でない、または OS の受信バッファーに基づく
     *  送出上限を使う) が DATA を送出するときに呼びます。\n
     *  送出上限をまだ計算していない場合 (相手からまだ通番を受け取っていない場合)、容量超過で受信を再開しない状態の場合、
     *  送出上限を使わない場合、および対象外の通信種別では 0 を返し、*send_limit に 0 を格納します。\n
     *  送出上限を返した場合は、「前回知らせた範囲」(notified、notified_base、notified_limit) をその時点の
     *  次に受信する通番と送出上限で更新します。DATA で知らせた分は、更新のフロー制御パケットを送出しません。
     *  抑制の要求と解除の送出には影響しません。
     *
     *  @param[in,out]  ctx         コンテキスト。
     *  @param[in]      peer_id     相手のピア識別子 (N:1 モード以外では POTR_PEER_NA)。
     *  @param[out]     send_limit  送出上限の格納先。送出上限を載せない場合は 0 を格納します。NULL は許可しません。
     *  @return         送出上限を載せる場合は 1、載せない場合と引数が NULL の場合は 0。
     *                  失敗モードのない述語のため共通結果コードの適用対象外。
     *
     *  @par            スレッド セーフ
     *  送信スレッドから呼びます。ロック順は peers_mutex -> send_window_mutex (または peer->send_window_mutex)
     *  -> fc->mutex とします (N:1 モードでは呼び出し元が peers_mutex を保持していること)。
     *  相乗りしない通信種別と送出上限を使わない構成では、fc->mutex を取得せずに 0 を返します。
     */
    extern int potr_internal_flow_control_piggyback_limit(potr_context *ctx, potr_peer_id peer_id,
                                                          uint32_t *send_limit);

    /**
     *  @brief          受信報告に載せる、その時点の自身の送出上限と許可数の上限を返します。
     *
     *  unicast、multicast、broadcast の受信者は、受信報告の seq_num に許可数の上限、ack_num に送出上限を載せます。
     *  許可数の上限は、「受信バッファーの幅」(OS の受信バッファーに基づく送出上限を使う場合) と、
     *  「容量から決まる最大の許可数」(容量を設定している場合、占有量が 0 のときの占有量による許可数) の小さいほうです。
     *  どちらか一方だけを使う場合はその値となり、送出上限がある場合は必ず 1 以上になります。
     *  送信者は、unicast と監視対象の送出上限の回復、および multicast と broadcast の監視対象を選ぶための
     *  順位付けと監視する数の計算に使います。\n
     *  送出上限がないのは、送出上限を使わない受信側 (資源が未生成、または上限なしモード)、送信者をまだ採用していない
     *  受信側 (次に受信する通番を受け取っていない)、容量超過で受信を再開しない状態の受信側です。
     *
     *  @param[in,out]  ctx             コンテキスト。
     *  @param[out]     send_limit      送出上限の格納先。送出上限がない場合は 0 を格納します。NULL は許可しません。
     *  @param[out]     max_allowance   許可数の上限の格納先。送出上限がある場合は 1 以上、ない場合は 0 を格納します。
     *                                  NULL は許可しません。
     *  @return         送出上限がある場合は 1、ない場合と引数が NULL の場合は 0。失敗モードのない述語のため
     *                  共通結果コードの適用対象外。
     *
     *  @par            スレッド セーフ
     *  受信スレッドから呼びます。呼び出し元はロックを保持しないこと。
     */
    extern int potr_internal_flow_control_report_limit(potr_context *ctx, uint32_t *send_limit,
                                                       uint32_t *max_allowance);

    /**
     *  @brief          multicast と broadcast の送信者が、監視対象の一覧を受信者へ送出します。
     *
     *  監視対象を選び直し (potr_internal_receiver_monitor_list())、監視対象が変わった時点と、
     *  flow_control_request_interval_ms の周期で、監視対象の一覧のパケット (状態が POTR_FLOW_CONTROL_STATE_MONITOR) を
     *  DATA と同じ宛先へ全経路から送出します。監視対象が 0 台の間は送出しません。\n
     *  multicast と broadcast の送信者以外では何もしません。
     *
     *  @param[in,out]  ctx     コンテキスト。
     *  @param[in]      now_ms  現在の単調増加時刻 [ms]。
     *
     *  @par            スレッド セーフ
     *  受信スレッドから呼びます。呼び出し元はロックを保持しないこと (内部で callback_mutex を取得します)。
     */
    extern void potr_internal_flow_control_monitor_tick(potr_context *ctx, uint64_t now_ms);

    /**
     *  @brief          送信側が、相手が「止める状態」であるために新しい DATA の送出を止めるべきかを返します。
     *
     *  相手が「止める状態」であるとは、有効な (期限内の) 抑制の要求があるか、受けた送出上限に達している
     *  (次に割り当てる DATA の通番が送出上限以上である) ことです。片方向 UDP の multicast と broadcast は、
     *  「止める状態」の受信者を potr_internal_receiver_flow_suppressed() の条件で数え、監視対象の送出上限で
     *  止まっている間は、問い合わせと同じ間隔 (10 ms 後から 2 倍ずつ、上限は flow_control_request_interval_ms) で
     *  監視対象の一覧を送出し直します。\n
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

    /**
     *  @brief          N:1 モードで、指定したピアがほかのピアへの送信を妨げた時点を記録します。
     *
     *  全ピア宛 (POTR_PEER_ALL) の送信が、そのピアが止める状態であるために受け付けられなかった、
     *  または待たされたときに呼びます。すでに妨げが記録されている場合は上書きしません。\n
     *  記録だけを行い、ピアの切断は行いません。
     *
     *  @param[in,out]  ctx     コンテキスト。
     *  @param[in]      peer_id 妨げたピアの識別子。
     *  @param[in]      now_ms  妨げが起きた時刻 [ms]。
     *
     *  @par            スレッド セーフ
     *  送信要求を受け付けたスレッドから呼びます。内部で peers_mutex を取得するため、呼び出し元は保持しないこと。
     */
    extern void potr_internal_flow_control_peer_mark_blocking(potr_context *ctx, potr_peer_id peer_id, uint64_t now_ms);

    /**
     *  @brief          N:1 モードで、保留エントリがある状態で送信キューが満杯になった最初の時刻を記録します。
     *
     *  送信キューが満杯で、いずれかのピア宛のエントリが送信キューに保留されている状態で、
     *  送信要求が受け付けられなかった、または待たされたときに呼びます。\n
     *  アトミック操作で時刻を記録するだけで、ロックの取得、全ピアの走査、および切断は行いません。
     *  記録された時刻は、周期処理 (potr_internal_flow_control_collect_slow_peers_locked()) が
     *  保留エントリを持つ止める状態のピアへ反映します。
     *
     *  @param[in,out]  ctx     コンテキスト。
     *  @param[in]      now_ms  満杯を観測した時刻 [ms] (0 は無視)。
     *
     *  @par            スレッド セーフ
     *  任意のスレッドから呼べます。ロックを取得しません。
     */
    extern void potr_internal_flow_control_note_queue_full(potr_context *ctx, uint64_t now_ms);

    /**
     *  @brief          N:1 モードで、全体を妨げている遅いピアの積算を更新し、切断対象のピアを収集します。
     *
     *  妨げが記録されているピア、または保留エントリがある状態でキュー満杯が記録されたピアについて、
     *  止める状態 (potr_internal_flow_control_peer_state() と同じ判定) である時間を積算します。
     *  止める状態でない間は経過時間 ÷ 9 を減算し、積算が 0 に戻った時点で妨げの記録を消します。\n
     *  同時に止める状態にあるピアの数が flow_excluded_limit() (接続中のピア数に対する上限) を
     *  超えている間は切断対象にせず、積算を猶予時間 (grace_ms) で上限丸めします。\n
     *  上限以下であれば、積算が猶予時間に達している妨げ中のピアを out_peers へ格納します。\n
     *  妨げが記録されているピアがなく、キュー満杯の記録もない場合は、全ピアを走査せず即座に 0 を返します。
     *
     *  @param[in,out]  ctx             コンテキスト。
     *  @param[in]      now_ms          現在の単調増加時刻 [ms]。
     *  @param[out]     out_peers       切断対象のピア識別子の格納先。NULL は許可しません (max_out が 0 でない場合)。
     *  @param[out]     out_accum_ms    各切断対象の積算時間 [ms] の格納先。不要なら NULL 可。
     *  @param[in]      max_out         out_peers に格納できる最大件数。
     *  @param[out]     next_check_ms   妨げ中のピアが残っている場合の、次の判定予定時刻 [ms] の格納先。
     *                                  該当がない場合は 0。不要なら NULL 可。
     *  @return         out_peers へ格納した切断対象の件数。件数を返す値関数のため共通結果コードの適用対象外。
     *
     *  @par            スレッド セーフ
     *  受信スレッドの周期処理から呼びます。呼び出し元が peers_mutex を保持していること。
     */
    extern size_t potr_internal_flow_control_collect_slow_peers_locked(potr_context *ctx, uint64_t now_ms,
                                                                       potr_peer_id *out_peers, uint64_t *out_accum_ms,
                                                                       size_t max_out, uint64_t *next_check_ms);

    /**
     *  @brief          N:1 モードで全体を妨げている遅いピアとして切断したセッションを記録します。
     *
     *  記録したセッションからのパケットは、切断時刻 (now_ms) から猶予時間 (grace_ms) ×
     *  POTR_FLOW_UNEXCLUDE_GRACE_MULTIPLIER の間、受け入れを拒否します。\n
     *  記録の表 (POTR_FLOW_SLOW_PEER_REJECT_MAX 件) が満杯の場合は、期限切れの記録を消したうえで、
     *  なお空きがなければ最も古い記録を上書きします。同じセッションがすでに記録されている場合は上書きします。\n
     *  ヘルス チェックのタイムアウト、FIN、REJECT による切断からは呼び出しません。
     *
     *  @param[in,out]  ctx         コンテキスト。
     *  @param[in]      session_id  切断したピアのセッション識別子。
     *  @param[in]      session_ts  切断したピアのセッション開始時刻。NULL は許可しません。
     *  @param[in]      now_ms      切断した単調増加時刻 [ms]。
     *
     *  @par            スレッド セーフ
     *  呼び出し元が peers_mutex を保持していること。失敗モードのない記録のため共通結果コードの適用対象外。
     */
    extern void potr_internal_flow_control_slow_peer_record_locked(potr_context *ctx, uint32_t session_id,
                                                                   const cplat_timespec *session_ts, uint64_t now_ms);

    /**
     *  @brief          未知のピアのセッションが、遅いピアとして切断された後の受け入れ拒否期間内かを判定します。
     *
     *  ピア検索で見つからなかったパケットに対して、新しいピアを生成しうる経路の手前で呼び出します。\n
     *  受け入れ拒否期間 (grace_ms × POTR_FLOW_UNEXCLUDE_GRACE_MULTIPLIER) が過ぎた記録は、照合の時点で消去します。\n
     *  期間内に一致した場合は、そのセッションの最初の 1 回だけ INFO へ診断出力し、1 を返します。
     *
     *  @param[in,out]  ctx         コンテキスト。
     *  @param[in]      session_id  パケットのセッション識別子。
     *  @param[in]      session_ts  パケットのセッション開始時刻。NULL は許可しません。
     *  @param[in]      now_ms      現在の単調増加時刻 [ms]。
     *  @return         受け入れを拒否する期間内であれば 1、そうでなければ 0。失敗モードのない述語のため
     *                  共通結果コードの適用対象外。
     *
     *  @par            スレッド セーフ
     *  呼び出し元が peers_mutex を保持していること。
     */
    extern int potr_internal_flow_control_slow_peer_rejected_locked(potr_context *ctx, uint32_t session_id,
                                                                    const cplat_timespec *session_ts, uint64_t now_ms);
#ifdef __cplusplus
}
#endif
#endif /* POTR_FLOW_CONTROL_H */
