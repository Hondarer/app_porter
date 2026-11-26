/**
 *******************************************************************************
 *  @file           potr_flow_control.c
 *  @brief          受信側の通知キューの占有量に基づくフロー制御を管理します。
 *******************************************************************************
 */
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <cplat/crt/stdlib.h>
#include <cplat/crypto/random.h>
#include <cplat/clock/clock.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/socket.h>
#include <cplat/sync/sync.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/potr_context.h>
#include <porter/infra/potr_socket_io.h>
#include <porter/potr_flow_control.h>
#include <porter/potr_peer_table.h>
#include <porter/infra/potr_trace.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/packet_seal.h>
#include <porter/protocol/receiver_report.h>

/** フロー制御パケットのワイヤー長の上限 [byte] (ヘッダー、状態と送出上限、認証タグ)。 */
#define FLOW_CONTROL_WIRE_SIZE (POTR_PACKET_HEADER_SIZE + POTR_FLOW_CONTROL_PAYLOAD_SIZE + POTR_CRYPTO_TAG_SIZE)

static_assert(FLOW_CONTROL_WIRE_SIZE <= POTR_PACKET_HEADER_SIZE + POTR_MAX_PAYLOAD + POTR_CRYPTO_TAG_SIZE,
              "porter: FLOW_CONTROL_WIRE_SIZE exceeds the maximum wire size");
static_assert(POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX >= 1U && POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX <= UINT8_MAX,
              "porter: POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX must fit in the 1-byte count");
static_assert(POTR_DEFAULT_FLOW_CONTROL_SUPPRESS_PERCENT <= POTR_PERCENT_MAX &&
                  POTR_DEFAULT_FLOW_CONTROL_RESUME_PERCENT <= POTR_PERCENT_MAX &&
                  POTR_DEFAULT_FLOW_CONTROL_EXCLUDED_RECEIVER_PERCENT <= POTR_PERCENT_MAX &&
                  POTR_DEFAULT_FLOW_CONTROL_FEEDBACK_PERCENT >= 1U &&
                  POTR_DEFAULT_FLOW_CONTROL_FEEDBACK_PERCENT <= POTR_PERCENT_MAX &&
                  POTR_DEFAULT_FLOW_CONTROL_RECEIVE_BUFFER_PERCENT <= POTR_PERCENT_MAX,
              "porter: default flow control percent out of range");
static_assert(POTR_DEFAULT_FLOW_CONTROL_GRACE_MS >= 1U && POTR_DEFAULT_FLOW_CONTROL_GRACE_MS <= POTR_WAIT_MS_MAX,
              "porter: default grace_ms out of range");
static_assert(POTR_DEFAULT_FLOW_CONTROL_RESUME_PERCENT < POTR_DEFAULT_FLOW_CONTROL_SUPPRESS_PERCENT,
              "porter: default resume percent must be below suppress percent");

/** 監視対象の一覧のパケットのワイヤー長の上限 [byte] (ヘッダー、一覧、認証タグ)。 */
#define FLOW_CONTROL_MONITOR_WIRE_SIZE \
    (POTR_PACKET_HEADER_SIZE + POTR_FLOW_CONTROL_MONITOR_PAYLOAD_MAX + POTR_CRYPTO_TAG_SIZE)

/** 拒否記録の保持数の下限。除外上限が 0 の場合も、呼び出し済みの記録を 1 件は保持する。 */
#define FLOW_SLOW_PEER_RETAIN_MIN 1U

static_assert(FLOW_SLOW_PEER_RETAIN_MIN >= 1U && FLOW_SLOW_PEER_RETAIN_MIN <= POTR_FLOW_SLOW_PEER_REJECT_MAX,
              "porter: slow peer retention minimum must fit in the table");

/**
 *  相手ごとの占有量と、抑制の要求の状態。
 *  N:1 モードのピアが切断されて解放された後も、そのピアの通知がすべて消費されるまで保持する。
 */
typedef struct flow_account
{
    struct flow_account *next; /**< 次の相手 (N:1 モードのみ)。 */
    uint64_t occupancy;        /**< コールバックから復帰していない DATA のバイト数。 */
    uint64_t max_packet_size;  /**< 現在のセッションで受信した DATA パケット 1 個の展開後の大きさの最大値 [byte]。 */
    uint64_t next_request_ms;  /**< 抑制の要求を次に再送する時刻 [ms]。requesting が 1 の間だけ有効。 */
    uint64_t last_data_ms;     /**< 相手から最後に DATA を受理した時刻 [ms]。0 = 未受理。 */
    uint64_t last_limit_advance_ms; /**< 送出上限を進める通知の送出に最後に成功した時刻 [ms]。0 = 未記録。 */
    uint64_t last_active_ms;   /**< 相手から最後に DATA または問い合わせを受理した時刻 [ms]。0 = 未受理。 */
    uint64_t last_grant_seq;   /**< 最後に許可数 1 以上の送出上限を知らせた順序番号。0 = 一度も許可を受けていない。 */
    uint64_t last_try_epoch;   /**< 順番の許可の走査で送出を試した世代番号。同一走査での再試行を防ぐ。 */
    cplat_timespec session_ts; /**< 相手のセッション開始時刻。 */
    uint32_t session_id;       /**< 相手のセッション識別子。 */
    potr_peer_id peer_id;      /**< 相手のピア識別子 (N:1 モード以外では POTR_PEER_NA)。 */
    int requesting;            /**< 抑制を要求中か (1: 要求中, 0: 要求なし)。 */
    int refused;               /**< 容量超過で切断し、受信を再開しない状態か (1: 再開しない)。 */
    uint32_t next_seq;         /**< 次に受信する通番。has_next_seq が 1 の間だけ有効。 */
    uint32_t notified_base;    /**< 前回フロー制御パケットまたは DATA 相乗りで送出上限を知らせた時点の「次に受信する通番」。notified が 1 の間だけ有効。 */
    uint32_t notified_limit;   /**< 前回送出した送出上限 (フロー制御パケットと DATA 相乗りの両方を含む)。notified が 1 の間だけ有効。 */
    int has_next_seq;          /**< 次に受信する通番を受け取ったか (1: 受け取った)。 */
    int notified;              /**< 現在の通信で、相手へ送出上限を知らせたか (1: 知らせた)。 */
    uint32_t reserved_limit; /**< 送出準備中の DATA に相乗りする送出上限。reserved が 1 の間だけ有効。 */
    int reserved;             /**< 相乗りする上限を送出結果が確定するまで合計に予約しているか。 */
    int sending_active;        /**< N:1 モードの送信中のピア数 (active_sender_count) に数えているか (1: 送信中)。 */
} flow_account;

/**
 *  受信側のフロー制御の資源。mutex で保護する。
 *  ロック順は peers_mutex (または tcp_recv_mutex) -> send_window_mutex (または peer->send_window_mutex)
 *  -> fc->mutex -> (tcp_send_mutex または certificate_mutex -> callback_mutex) とする。
 *  fc->mutex の内側で peers_mutex および send_window_mutex を取得してはならない。
 */
struct potr_internal_flow_control
{
    cplat_local_lock *mutex;   /**< 占有量と状態の遷移、およびフロー制御パケットの送出を一組として保護する。 */
    flow_account *accounts;    /**< N:1 モードの相手の一覧 (登録順)。 */
    flow_account service;      /**< N:1 モード以外の相手。 */
    uint64_t capacity;         /**< 容量 [byte]。 */
    uint64_t suppress_bytes;   /**< 抑制を要求するしきい値 [byte]。 */
    uint64_t resume_bytes;     /**< 解除するしきい値 [byte]。 */
    uint64_t max_payload;      /**< 受信側の実効 max_payload [byte]。送出上限の許可数の計算に使う。 */
    uint64_t receive_buffer_width; /**< 受信バッファーの幅 [パケット] (N:1 モードのピア数による分割と下限の適用の前)。 */
    uint64_t monitor_received_ms; /**< multicast と broadcast の受信者が、監視対象の一覧を最後に受け取った時刻 [ms]。 */
    uint64_t grant_seq_counter; /**< 許可を与えた順序を記録する単調増加カウンター。 */
    uint64_t grant_try_epoch;  /**< 順番の許可の走査ごとに増やす世代番号。 */
    int turn_grant_retry;     /**< 順番の許可の送出失敗で、空き枠の再割り当てが必要か。 */
    int receive_buffer_used;   /**< OS の受信バッファーに基づく送出上限を使うか (1: 使う)。 */
    int one_to_many_receiver;  /**< multicast と broadcast の受信者か (1: 監視対象である間だけ送出上限を知らせる)。 */
    int monitored;             /**< multicast と broadcast の受信者で、監視対象に指定されているか (1: 監視対象)。 */
    uint32_t active_sender_count; /**< N:1 モードで、直近の request_interval_ms の間に DATA または問い合わせを受けたピアの数。 */
    uint32_t _pad;             /**< パディング (構造体サイズを 8 [byte] の倍数に揃える)。 */
    cplat_ipv4_endpoint reply[POTR_MAX_PATH]; /**< UDP の返送先 (N:1 モード以外)。受信スレッドが更新する。 */
};

static int get_receive_buffer_size(cplat_socket receive_socket, uint64_t *size);
static int query_receive_buffer_size(const potr_context *ctx, uint64_t *size);
static int is_unlimited_receiver(const potr_context *ctx);
static int is_unlimited_mode(const struct potr_internal_flow_control *fc);
static void clear_sending_active(struct potr_internal_flow_control *fc, flow_account *account);
static flow_account *find_account(const potr_context *ctx, struct potr_internal_flow_control *fc,
                                  potr_peer_id peer_id, int create);
static void remove_account(struct potr_internal_flow_control *fc, flow_account *target);
static void refresh_reply(const potr_context *ctx, struct potr_internal_flow_control *fc);
static int send_state(potr_context *ctx, struct potr_internal_flow_control *fc, const flow_account *account,
                      uint8_t state, uint32_t send_limit);
static uint64_t total_buffer_width(const struct potr_internal_flow_control *fc);
static uint64_t multi_peer_width(const struct potr_internal_flow_control *fc);
static int is_turn_mode(const potr_context *ctx, const struct potr_internal_flow_control *fc);
static int account_has_grant(const flow_account *account);
static uint32_t account_remaining_grant(const flow_account *account);
static int account_grant_expired(const potr_context *ctx, const flow_account *account, uint64_t now_ms, int has_now);
static uint32_t account_effective_remaining_grant(const potr_context *ctx, const flow_account *account,
                                                  uint64_t now_ms, int has_now);
static uint32_t occupancy_allowance(const struct potr_internal_flow_control *fc, const flow_account *account);
static uint32_t buffer_allowance(const potr_context *ctx, const struct potr_internal_flow_control *fc,
                                 const flow_account *account);
static uint32_t send_allowance(const potr_context *ctx, const struct potr_internal_flow_control *fc,
                               const flow_account *account);
static void record_notified(struct potr_internal_flow_control *fc, flow_account *account, uint32_t send_limit);
static uint8_t current_state(const flow_account *account);
static int mark_sending_active(struct potr_internal_flow_control *fc, flow_account *account, uint64_t now_ms);
static uint32_t turn_grant_allowance(const struct potr_internal_flow_control *fc, const flow_account *account);
static int dispatch_turn_grants(potr_context *ctx, struct potr_internal_flow_control *fc, potr_peer_id target_peer_id,
                                uint64_t now_ms, int has_now);
static int may_notify_limit(const struct potr_internal_flow_control *fc);
static int below_resume(const struct potr_internal_flow_control *fc, const flow_account *account);
static void notify_state(potr_context *ctx, struct potr_internal_flow_control *fc, flow_account *account,
                         uint8_t state);
static void notify_unlimited(potr_context *ctx, struct potr_internal_flow_control *fc, flow_account *account);
static void adopt_session(potr_context *ctx, struct potr_internal_flow_control *fc, flow_account *account,
                          uint32_t session_id, const cplat_timespec *session_ts);
static uint64_t next_request_time(const potr_context *ctx, uint64_t now_ms);
static void start_request(potr_context *ctx, struct potr_internal_flow_control *fc, flow_account *account,
                          uint64_t now_ms);
static void judge_progress(potr_context *ctx, struct potr_internal_flow_control *fc, flow_account *account);
static int uses_query_udp(potr_type type);
static void tick_account(potr_context *ctx, struct potr_internal_flow_control *fc, flow_account *account,
                         uint64_t now_ms);
static void on_monitor_list(potr_context *ctx, const potr_packet *pkt);
static void on_query(potr_context *ctx, struct potr_internal_peer_context *peer, const potr_packet *pkt);
static int recheck_wait_ms(uint64_t now_ms, uint64_t until_ms);
static uint32_t read_next_seq(const potr_internal_window *window, cplat_local_lock *window_mutex);
static uint64_t earlier_time(uint64_t current_ms, uint64_t candidate_ms);
static uint64_t later_time(uint64_t first_ms, uint64_t second_ms);
static void send_monitor_list(potr_context *ctx, const potr_internal_packet_session_hdr *list, size_t count);
static void send_monitor_list_on_stall(potr_context *ctx, uint64_t now_ms);
static int query_timer_due(uint64_t *next_ms, uint64_t *interval_ms, uint64_t max_interval_ms, uint64_t now_ms);
static uint64_t advance_query_timer(potr_context *ctx, struct potr_internal_peer_context *peer, uint64_t now_ms);
static int finish_limit_expiry(potr_context *ctx, uint64_t send_limit, uint64_t received_ms, uint64_t last_packet_ms);
static int expire_limit_if_unchanged(potr_context *ctx, uint64_t send_limit, uint64_t received_ms,
                                     uint64_t last_packet_ms);
static int peer_state_locked(potr_context *ctx, potr_internal_peer_context *peer, uint64_t now_ms, int allow_query,
                             uint32_t *allowance_out);
static uint64_t slow_peer_reject_duration_ms(const potr_context *ctx);
static int slow_peer_reject_expired(const potr_internal_slow_peer_reject_entry *entry, uint64_t now_ms,
                                    uint64_t duration_ms);
static void slow_peer_reject_clear(potr_context *ctx, potr_internal_slow_peer_reject_entry *entry);
static uint32_t max_occupancy_allowance(const struct potr_internal_flow_control *fc, const flow_account *account);
static uint32_t report_max_allowance(const struct potr_internal_flow_control *fc, const flow_account *account);

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_flow_control_resolve(const potr_service_def *service, const potr_global_config *global,
                                       potr_role role, uint32_t health_interval_ms,
                                       potr_internal_flow_control_config *config)
{
    uint32_t disabled_err = 0U;
    int ret = potr_internal_flow_control_resolve_values(service, global, role, health_interval_ms, config,
                                                        &disabled_err);
    if (ret != POTR_OK)
    {
        return ret;
    }

    if (is_raw_type(service->type))
    {
        return POTR_OK;
    }

    if (disabled_err != 0U)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " POTR_FLOW_CONTROL_DISABLED is not allowed for this field",
                   service->service_id);
        return POTR_ERR_INVALID_CONFIG;
    }

    if (config->suppress_percent < 1U || config->suppress_percent > POTR_PERCENT_MAX || config->resume_percent < 1U ||
        config->resume_percent >= config->suppress_percent)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " invalid thresholds suppress_percent=%u resume_percent=%u"
                   " (1 <= resume < suppress <= 100)",
                   service->service_id, (unsigned)config->suppress_percent, (unsigned)config->resume_percent);
        return POTR_ERR_INVALID_CONFIG;
    }

    if (config->request_interval_ms == 0U || config->request_lifetime_ms <= config->request_interval_ms)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " invalid request_interval_ms=%u request_lifetime_ms=%u"
                   " (0 < interval < lifetime)",
                   service->service_id, (unsigned)config->request_interval_ms,
                   (unsigned)config->request_lifetime_ms);
        return POTR_ERR_INVALID_CONFIG;
    }

    /* 再送周期、有効期限、猶予時間は待機の時間に使うため、待機の関数へ int で渡せる上限を超える値は受け付けない。 */
    if (config->request_interval_ms > POTR_WAIT_MS_MAX || config->request_lifetime_ms > POTR_WAIT_MS_MAX)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " invalid request_interval_ms=%u request_lifetime_ms=%u"
                   " (max: %u)",
                   service->service_id, (unsigned)config->request_interval_ms,
                   (unsigned)config->request_lifetime_ms, (unsigned)POTR_WAIT_MS_MAX);
        return POTR_ERR_INVALID_CONFIG;
    }

    if (config->grace_ms == 0U || config->grace_ms > POTR_WAIT_MS_MAX)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " invalid grace_ms=%u (1 <= grace_ms <= %u)",
                   service->service_id, (unsigned)config->grace_ms, (unsigned)POTR_WAIT_MS_MAX);
        return POTR_ERR_INVALID_CONFIG;
    }

    if (config->excluded_receiver_percent > POTR_PERCENT_MAX)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " invalid excluded_receiver_percent=%u (max: 100)",
                   service->service_id, (unsigned)config->excluded_receiver_percent);
        return POTR_ERR_INVALID_CONFIG;
    }

    if (config->feedback_percent < 1U || config->feedback_percent > POTR_PERCENT_MAX)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " invalid feedback_percent=%u (1 <= percent <= 100)",
                   service->service_id, (unsigned)config->feedback_percent);
        return POTR_ERR_INVALID_CONFIG;
    }

    if (config->monitored_receiver_count > POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " invalid monitored_receiver_count=%u (max: %u)",
                   service->service_id, (unsigned)config->monitored_receiver_count,
                   (unsigned)POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX);
        return POTR_ERR_INVALID_CONFIG;
    }

    if (config->receive_buffer_percent > POTR_PERCENT_MAX)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " invalid receive_buffer_percent=%u (1 <= percent <= 100)",
                   service->service_id, (unsigned)config->receive_buffer_percent);
        return POTR_ERR_INVALID_CONFIG;
    }

    if (config->capacity == 0U)
    {
        return POTR_OK;
    }

    /*
     * 送出上限で止まった後に、フラグメントの途中にあった最大長のメッセージ 1 件が完成するだけで容量を超える構成は、
     * 切断になりうるため受け付けない。要求のしきい値から容量までの余裕を、桁あふれしない 64 ビットで求める。
     */
    {
        uint64_t margin = (uint64_t)config->capacity - (uint64_t)config->capacity * config->suppress_percent / 100U;

        if (margin < (uint64_t)global->max_message_size)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "flow_control: service_id=%" PRId64 " margin above suppress threshold=%" PRIu64
                       " (capacity=%u suppress_percent=%u) is smaller than max_message_size=%u",
                       service->service_id, margin, (unsigned)config->capacity,
                       (unsigned)config->suppress_percent, (unsigned)global->max_message_size);
            return POTR_ERR_INVALID_CONFIG;
        }
    }

    /* 一対多の送信側は、受信報告で受信者数を把握する。受信報告を送らない受信側の構成は受け付けない。 */
    if ((service->type == POTR_TYPE_MULTICAST || service->type == POTR_TYPE_BROADCAST) && role == POTR_ROLE_RECEIVER &&
        health_interval_ms == 0U)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " multicast/broadcast receiver requires health_interval_ms > 0",
                   service->service_id);
        return POTR_ERR_INVALID_CONFIG;
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_flow_control_start(potr_context *ctx)
{
    struct potr_internal_flow_control *fc;
    uint64_t buffer_size = 0U;
    int buffer_used = 0;

    if (ctx == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (ctx->flow_control_config.capacity == 0U && ctx->flow_control_config.receive_buffer_percent == 0U &&
        !is_unlimited_receiver(ctx))
    {
        return POTR_OK;
    }

    /* 送出上限の許可数を max_payload で求めるため、送出上限を使う構成では 0 を受け付けない (サービスを開く処理が 64 以上を検査済み)。 */
    if ((ctx->flow_control_config.capacity != 0U || ctx->flow_control_config.receive_buffer_percent != 0U) &&
        ctx->global.max_payload == 0U)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    /* 大きさを取得できなかった場合は、警告を記録し、このハンドルでは OS の受信バッファーに基づく送出上限を使わない。 */
    if (ctx->flow_control_config.receive_buffer_percent != 0U && query_receive_buffer_size(ctx, &buffer_size) == POTR_OK)
    {
        buffer_used = 1;
    }

    /* 容量が 0 で、OS の受信バッファーに基づく送出上限も使わず、上限なしの通知も行わない場合は資源を生成しない。 */
    if (ctx->flow_control_config.capacity == 0U && buffer_used == 0 && !is_unlimited_receiver(ctx))
    {
        return POTR_OK;
    }

    fc = (struct potr_internal_flow_control *)cplat_calloc(1U, sizeof(*fc));
    if (fc == NULL)
    {
        return POTR_ERR_OUT_OF_MEMORY;
    }

    if (cplat_local_lock_create(&fc->mutex) != CPLAT_OK)
    {
        cplat_free(fc);
        return POTR_ERR_UNKNOWN;
    }

    fc->capacity = ctx->flow_control_config.capacity;
    fc->suppress_bytes = fc->capacity * ctx->flow_control_config.suppress_percent / 100U;
    fc->resume_bytes = fc->capacity * ctx->flow_control_config.resume_percent / 100U;
    fc->max_payload = ctx->global.max_payload;
    fc->receive_buffer_used = buffer_used;
    ctx->flow_receive_buffer_width = 0U;
    ctx->flow_monitored = 0;
    if (buffer_used != 0)
    {
        uint64_t effective_width;

        fc->receive_buffer_width = flow_receive_buffer_width(buffer_size, ctx->flow_control_config.receive_buffer_percent,
                                                             (uint32_t)ctx->global.max_payload);
        effective_width = fc->receive_buffer_width;
        if (effective_width < (uint64_t)POTR_FLOW_RECEIVE_BUFFER_WIDTH_MIN)
        {
            effective_width = (uint64_t)POTR_FLOW_RECEIVE_BUFFER_WIDTH_MIN;
        }
        ctx->flow_receive_buffer_width = (uint32_t)effective_width;
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                   "flow_control[service_id=%" PRId64 "]: OS receive buffer=%" PRIu64 " width=%" PRIu64 " packets",
                   ctx->service.service_id, buffer_size, fc->receive_buffer_width);
    }
    fc->one_to_many_receiver = ctx->role == POTR_ROLE_RECEIVER &&
                               (ctx->service.type == POTR_TYPE_MULTICAST || ctx->service.type == POTR_TYPE_BROADCAST);
    fc->service.peer_id = POTR_PEER_NA;
    ctx->flow_control = fc;

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_flow_control_stop(potr_context *ctx)
{
    struct potr_internal_flow_control *fc;
    flow_account *account;

    if (ctx == NULL || ctx->flow_control == NULL)
    {
        return;
    }

    fc = ctx->flow_control;
    ctx->flow_control = NULL;
    ctx->flow_receive_buffer_width = 0U;
    ctx->flow_monitored = 0;
    account = fc->accounts;

    while (account != NULL)
    {
        flow_account *next = account->next;
        cplat_free(account);
        account = next;
    }

    cplat_local_lock_dispose(fc->mutex);
    cplat_free(fc);
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_flow_control_admit(potr_context *ctx, potr_peer_id peer_id, uint32_t session_id,
                                     const cplat_timespec *session_ts, size_t len)
{
    struct potr_internal_flow_control *fc;
    flow_account *account;
    uint64_t now_ms = 0U;
    int result = POTR_OK;

    if (ctx == NULL || ctx->flow_control == NULL || session_ts == NULL)
    {
        return POTR_OK;
    }

    fc = ctx->flow_control;

    /* 容量が 0 の受信側 (OS の受信バッファーに基づく送出上限だけを使う受信側) は、占有量を数えない。容量は開始後は不変である。 */
    if (fc->capacity == 0U)
    {
        return POTR_OK;
    }

    /*
     * 時刻は要求の再送の予定にだけ使い、失敗時は now_ms が 0 のままで次の周期処理の再送が早まるだけのため、
     * 戻り値は明示的に破棄する。
     */
    (void)cplat_clock_get_monotonic_ms(&now_ms);

    cplat_local_lock_lock(fc->mutex);
    refresh_reply(ctx, fc);
    account = find_account(ctx, fc, peer_id, 1);

    if (account == NULL)
    {
        /* 記録を確保できない場合は、計数せずに受け入れる。受信を止めるより、通知を続けることを優先する。 */
        cplat_local_lock_unlock(fc->mutex);
        return POTR_OK;
    }

    adopt_session(ctx, fc, account, session_id, session_ts);

    if (account->refused != 0 || (uint64_t)len > fc->capacity - account->occupancy)
    {
        /*
         * 容量を超える DATA は受け入れない。呼び出し元がこの相手を切断する。
         * 片方向 UDP の送信者は受信者の切断を知らずに送出を続けるため、抑制の要求を続ける。
         * そのほかの通信種別は、切断によって相手の送出が止まるため、要求をやめる。
         */
        if (account->refused == 0)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                       "flow_control[service_id=%" PRId64 "]: peer=%u capacity exceeded"
                       " (occupancy=%" PRIu64 " len=%zu capacity=%" PRIu64 "), disconnecting",
                       ctx->service.service_id, (unsigned)peer_id, account->occupancy, len, fc->capacity);
        }

        account->refused = 1;
        clear_sending_active(fc, account);

        if (is_oneway_udp_type(ctx->service.type))
        {
            if (account->requesting == 0)
            {
                start_request(ctx, fc, account, now_ms);
            }
        }
        else
        {
            account->requesting = 0;
        }

        result = POTR_ERR_FULL;
    }
    else
    {
        account->occupancy += (uint64_t)len;

        if (account->requesting == 0 && account->occupancy > fc->suppress_bytes)
        {
            start_request(ctx, fc, account, now_ms);
            POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                       "flow_control[service_id=%" PRId64 "]: peer=%u requested suppression (occupancy=%" PRIu64 ")",
                       ctx->service.service_id, (unsigned)peer_id, account->occupancy);
        }
    }

    cplat_local_lock_unlock(fc->mutex);

    return result;
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_flow_control_on_received(potr_context *ctx, potr_peer_id peer_id, uint32_t session_id,
                                            const cplat_timespec *session_ts, uint32_t next_seq, int data_packet,
                                            size_t expanded_size)
{
    struct potr_internal_flow_control *fc;
    flow_account *account;
    int idle_gap = 0;
    uint32_t prev_remaining;
    int became_active = 0;
    int has_now = 0;
    uint64_t now_ms = 0U;

    if (ctx == NULL || ctx->flow_control == NULL || session_ts == NULL)
    {
        return;
    }

    fc = ctx->flow_control;
    cplat_local_lock_lock(fc->mutex);
    refresh_reply(ctx, fc);
    account = find_account(ctx, fc, peer_id, 1);

    /* 記録を確保できない相手と、容量超過で受信を再開しない相手には、送出上限を知らせない。 */
    if (account == NULL || account->refused != 0)
    {
        cplat_local_lock_unlock(fc->mutex);
        return;
    }

    adopt_session(ctx, fc, account, session_id, session_ts);
    prev_remaining = account_remaining_grant(account);

    /*
     * 圧縮した DATA パケットの展開後の大きさの最大値を、既存の fc->mutex の下でセッションごとに記録する。
     * 占有量による許可数の計算で max_payload との大きいほうを使い、展開後の容量超過を防ぐ。
     */
    if (data_packet != 0 && (uint64_t)expanded_size > account->max_packet_size)
    {
        account->max_packet_size = (uint64_t)expanded_size;
    }

    /* 送出上限を使わない受信側は、最初に送出できる時点で「上限なし」を 1 回だけ送出する。 */
    if (is_unlimited_mode(fc))
    {
        account->next_seq = next_seq;
        account->has_next_seq = 1;
        if (account->notified == 0)
        {
            notify_unlimited(ctx, fc, account);
        }

        cplat_local_lock_unlock(fc->mutex);
        return;
    }

    /*
     * 次に受信する通番をまだ受け取っていなかった場合 (抑制の要求だけを先に送出した場合を含む) と、
     * 前回知らせた時点より前へ戻った場合 (受信ウィンドウの初期化) は、送出上限を知らせ直す。
     */
    if (account->has_next_seq == 0 || (account->notified != 0 && (int32_t)(next_seq - account->notified_base) < 0))
    {
        account->notified = 0;
        prev_remaining = 0U;
    }

    /*
     * UDP (multicast と broadcast を除く) では、最後の DATA の受理から有効期限の半分以上途切れた後の DATA の受理で、
     * 送出上限の更新を送出する (送信側が未使用の許可を初期の幅へ縮めた後の回復を早めるため)。
     * N:1 モードで OS の受信バッファーに基づく送出上限を使う場合は、同じ現在時刻で送信中のピアも記録する。
     */
    if (data_packet != 0 && uses_query_udp(ctx->service.type))
    {
        if (cplat_clock_get_monotonic_ms(&now_ms) == CPLAT_OK)
        {
            uint64_t half_lifetime_ms = (uint64_t)(ctx->flow_control_config.request_lifetime_ms / 2U);

            has_now = 1;

            if (account->last_data_ms != 0U && half_lifetime_ms > 0U &&
                now_ms >= account->last_data_ms + half_lifetime_ms)
            {
                idle_gap = 1;
            }

            account->last_data_ms = now_ms;

            if (ctx->is_multi_peer && fc->receive_buffer_used != 0)
            {
                became_active = mark_sending_active(fc, account, now_ms);
            }
        }
    }

    account->next_seq = next_seq;
    account->has_next_seq = 1;

    if (is_turn_mode(ctx, fc))
    {
        int granted_self = 0;

        if (became_active != 0 || fc->turn_grant_retry != 0)
        {
            if (has_now == 0 && cplat_clock_get_monotonic_ms(&now_ms) == CPLAT_OK)
            {
                has_now = 1;
            }

            granted_self = dispatch_turn_grants(ctx, fc, account->peer_id, now_ms, has_now);
        }

        if (account->notified == 0)
        {
            notify_state(ctx, fc, account, current_state(account));
        }
        else if (data_packet != 0 && account->requesting == 0)
        {
            uint32_t remaining = account_remaining_grant(account);
            int exhausted = prev_remaining > 0U && remaining == 0U;
            int crossed_refill = prev_remaining > (uint32_t)POTR_FLOW_TURN_REFILL_TRIGGER_REMAINING &&
                                 remaining <= (uint32_t)POTR_FLOW_TURN_REFILL_TRIGGER_REMAINING;

            if (exhausted != 0 || crossed_refill != 0)
            {
                /* 許可を使い終えた時点で占有量が高ければ抑制を要求する。 */
                if (exhausted != 0 && !below_resume(fc, account))
                {
                    start_request(ctx, fc, account, now_ms);
                    POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                               "flow_control[service_id=%" PRId64 "]: peer=%u requested suppression at send limit"
                               " (occupancy=%" PRIu64 ")",
                               ctx->service.service_id, (unsigned)account->peer_id, account->occupancy);
                }

                (void)dispatch_turn_grants(ctx, fc, POTR_PEER_NA, now_ms, has_now);
            }
            else if (idle_gap != 0 && granted_self == 0)
            {
                notify_state(ctx, fc, account, (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME);
            }
        }
    }
    else if (account->notified == 0)
    {
        /*
         * 相手にまだ送出上限を知らせていないため、最初に送出できる時点で知らせる。
         * multicast と broadcast の受信者は、監視対象でない間は知らせず、送り終えた時点の判定も行わない。
         */
        if (may_notify_limit(fc))
        {
            notify_state(ctx, fc, account, current_state(account));
        }
    }
    else if (data_packet != 0 && account->requesting == 0)
    {
        if (idle_gap != 0)
        {
            notify_state(ctx, fc, account, (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME);
        }
        else
        {
            judge_progress(ctx, fc, account);
        }
    }

    cplat_local_lock_unlock(fc->mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_flow_control_release(potr_context *ctx, potr_peer_id peer_id, size_t len)
{
    struct potr_internal_flow_control *fc;
    flow_account *account;

    if (ctx == NULL || ctx->flow_control == NULL)
    {
        return;
    }

    fc = ctx->flow_control;

    /* 容量が 0 の受信側は占有量を数えないため、減算と解除の判定を行わない。容量は開始後は不変である。 */
    if (fc->capacity == 0U)
    {
        return;
    }

    /* N:1 モードでは、ピアの宛先を参照して送出するため、peers_mutex、fc->mutex の順に取得する。 */
    if (ctx->is_multi_peer)
    {
        cplat_local_lock_lock(ctx->peers_mutex);
    }

    cplat_local_lock_lock(fc->mutex);
    account = find_account(ctx, fc, peer_id, 0);

    if (account != NULL)
    {
        if ((uint64_t)len > account->occupancy)
        {
            account->occupancy = 0U;
        }
        else
        {
            account->occupancy -= (uint64_t)len;
        }

        if (account->occupancy < fc->resume_bytes)
        {
            if (account->requesting != 0)
            {
                /* 解除は、下回ったことを検出した時点で、新しい送出上限を載せて即時に 1 回送出する。 */
                account->requesting = 0;
                if (is_turn_mode(ctx, fc))
                {
                    uint64_t now_ms = 0U;
                    int has_now = cplat_clock_get_monotonic_ms(&now_ms) == CPLAT_OK;
                    int granted_self = dispatch_turn_grants(ctx, fc, account->peer_id, now_ms, has_now);

                    if (granted_self == 0)
                    {
                        notify_state(ctx, fc, account, (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME);
                    }
                }
                else
                {
                    notify_state(ctx, fc, account, (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME);
                }
                POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                           "flow_control[service_id=%" PRId64 "]: peer=%u resumed (occupancy=%" PRIu64 ")",
                           ctx->service.service_id, (unsigned)peer_id, account->occupancy);
            }

            if (account->refused != 0)
            {
                account->refused = 0;
                POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                           "flow_control[service_id=%" PRId64 "]: peer=%u accepts reception again",
                           ctx->service.service_id, (unsigned)peer_id);
            }
        }

        /*
         * N:1 モードでは、占有量が 0 になり、ピアが解放済みの相手の記録を解放する。
         * 接続中のピアの記録は、知らせた送出上限を保つため残す (解放すると次の DATA で知らせ直すことになる)。
         */
        if (ctx->is_multi_peer && account->occupancy == 0U && account->refused == 0 &&
            potr_internal_peer_find_by_id(ctx, peer_id) == NULL)
        {
            remove_account(fc, account);
        }
    }

    cplat_local_lock_unlock(fc->mutex);

    if (ctx->is_multi_peer)
    {
        cplat_local_lock_unlock(ctx->peers_mutex);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_flow_control_tick(potr_context *ctx, uint64_t now_ms)
{
    struct potr_internal_flow_control *fc;

    if (ctx == NULL || ctx->flow_control == NULL)
    {
        return;
    }

    fc = ctx->flow_control;

    if (ctx->is_multi_peer)
    {
        cplat_local_lock_lock(ctx->peers_mutex);
    }

    cplat_local_lock_lock(fc->mutex);

    if (ctx->is_multi_peer)
    {
        flow_account **link = &fc->accounts;
        uint64_t interval_ms = (uint64_t)ctx->flow_control_config.request_interval_ms;
        int slot_freed = 0;

        while (*link != NULL)
        {
            flow_account *account = *link;

            /*
             * 占有量が 0 で、ピアが解放された相手の記録を解放する。
             * PING だけを受けたピアの記録は、通知の消費 (release) を経ないため、ここで解放する。
             */
            if (account->occupancy == 0U && account->refused == 0 &&
                potr_internal_peer_find_by_id(ctx, account->peer_id) == NULL)
            {
                if (account_has_grant(account))
                {
                    slot_freed = 1;
                }

                clear_sending_active(fc, account);
                *link = account->next;
                cplat_free(account);
                continue;
            }

            /*
             * 直近の request_interval_ms の間に DATA も問い合わせもないピアを送信中のピアから外す。
             * 送信中でなくなっても、すでに知らせた未使用の許可は有効期限までは全体の幅に数え続ける。
             */
            if (fc->receive_buffer_used != 0 && account->sending_active != 0 && interval_ms > 0U &&
                now_ms >= account->last_active_ms + interval_ms)
            {
                clear_sending_active(fc, account);
            }

            if (fc->receive_buffer_used != 0 && account_has_grant(account) &&
                account_grant_expired(ctx, account, now_ms, 1) != 0)
            {
                slot_freed = 1;
            }

            tick_account(ctx, fc, account, now_ms);
            link = &account->next;
        }

        if (fc->receive_buffer_used != 0)
        {
            if (is_turn_mode(ctx, fc))
            {
                if (slot_freed != 0 || fc->turn_grant_retry != 0)
                {
                    (void)dispatch_turn_grants(ctx, fc, POTR_PEER_NA, now_ms, 1);
                }
            }
            else
            {
                flow_account *account;

                /*
                 * 順番の許可から通常の配分へ戻った後、許可数 0 のまま待っていた送信中のピアへ
                 * 通常の配分の値を知らせて再開させる。
                 */
                for (account = fc->accounts; account != NULL; account = account->next)
                {
                    if (account->sending_active != 0 && account->notified != 0 && account->has_next_seq != 0 &&
                        account->refused == 0 && account->requesting == 0 && !account_has_grant(account) &&
                        below_resume(fc, account))
                    {
                        notify_state(ctx, fc, account, (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME);
                    }
                }
            }
        }
    }
    else
    {
        refresh_reply(ctx, fc);

        /*
         * multicast と broadcast の受信者は、監視対象の一覧を有効期限の間受け取らなかった場合、監視対象でなくなったものとして扱う。
         * 送信者の停止と一覧の喪失が続いた場合に、送出上限を送り続けないためである。
         */
        if (fc->monitored != 0 &&
            now_ms >= fc->monitor_received_ms + (uint64_t)ctx->flow_control_config.request_lifetime_ms)
        {
            fc->monitored = 0;
            ctx->flow_monitored = 0;
            fc->service.notified = 0;
            POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                       "flow_control[service_id=%" PRId64 "]: monitored receiver list expired, no longer monitored",
                       ctx->service.service_id);
        }

        tick_account(ctx, fc, &fc->service, now_ms);
    }

    cplat_local_lock_unlock(fc->mutex);

    if (ctx->is_multi_peer)
    {
        cplat_local_lock_unlock(ctx->peers_mutex);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_flow_control_on_peer_freed(potr_context *ctx, potr_peer_id peer_id)
{
    struct potr_internal_flow_control *fc;
    flow_account *account;
    int was_grant = 0;

    if (ctx == NULL || ctx->flow_control == NULL || !ctx->is_multi_peer)
    {
        return;
    }

    fc = ctx->flow_control;
    cplat_local_lock_lock(fc->mutex);
    account = find_account(ctx, fc, peer_id, 0);

    if (account != NULL)
    {
        if (account_has_grant(account))
        {
            was_grant = 1;
        }

        clear_sending_active(fc, account);

        if (account->occupancy == 0U && account->refused == 0)
        {
            remove_account(fc, account);
        }

        if (was_grant != 0 && is_turn_mode(ctx, fc))
        {
            uint64_t now_ms = 0U;
            int has_now = cplat_clock_get_monotonic_ms(&now_ms) == CPLAT_OK;

            (void)dispatch_turn_grants(ctx, fc, POTR_PEER_NA, now_ms, has_now);
        }
    }

    cplat_local_lock_unlock(fc->mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_flow_control_refused(potr_context *ctx, potr_peer_id peer_id, uint32_t session_id,
                                       const cplat_timespec *session_ts)
{
    struct potr_internal_flow_control *fc;
    flow_account *account;
    int refused = 0;

    if (ctx == NULL || ctx->flow_control == NULL || session_ts == NULL)
    {
        return 0;
    }

    fc = ctx->flow_control;
    /* 容量 0 では受け入れ拒否へ遷移しないため、記録の探索とロックを省く。容量は開始後は不変である。 */
    if (fc->capacity == 0U)
    {
        return 0;
    }

    cplat_local_lock_lock(fc->mutex);

    if (ctx->is_multi_peer)
    {
        for (account = fc->accounts; account != NULL; account = account->next)
        {
            if (account->refused == 0)
            {
                continue;
            }

            if ((peer_id != POTR_PEER_NA && account->peer_id == peer_id) ||
                (account->session_id == session_id && account->session_ts.tv_sec == session_ts->tv_sec &&
                 account->session_ts.tv_nsec == session_ts->tv_nsec))
            {
                refused = 1;
                break;
            }
        }
    }
    else
    {
        refused = fc->service.refused;
    }

    cplat_local_lock_unlock(fc->mutex);

    return refused;
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_flow_control_on_packet(potr_context *ctx, struct potr_internal_peer_context *peer,
                                          const potr_packet *pkt)
{
    uint8_t state = 0U;
    uint32_t send_limit = 0U;
    uint64_t now_ms;
    uint64_t expire_ms = 0U;

    if (ctx == NULL || pkt == NULL)
    {
        return;
    }

    /* 監視対象の一覧は、multicast と broadcast の受信者だけが受理する。その他の通信種別では破棄する。 */
    if (pkt->payload != NULL && pkt->payload_len >= 1U && pkt->payload[0] == POTR_FLOW_CONTROL_STATE_MONITOR)
    {
        if (peer == NULL && !ctx->is_multi_peer)
        {
            on_monitor_list(ctx, pkt);
        }

        return;
    }

    if (flow_control_payload_decode(pkt, &state, &send_limit) != POTR_OK)
    {
        return;
    }

    if (state == POTR_FLOW_CONTROL_STATE_QUERY)
    {
        on_query(ctx, peer, pkt);
        return;
    }

    /*
     * 時刻を取得できない場合は、要求の期限と送出上限を破棄する時刻を決められないため、このパケットを反映しない。
     * 相手は、要求を再送周期ごとに再送し、送出上限を次の契機で送出する。
     */
    if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
    {
        return;
    }

    if (state == POTR_FLOW_CONTROL_STATE_UNLIMITED)
    {
        if (peer != NULL)
        {
            peer->flow_request_expire_ms = 0U;
            peer->flow_limit_received_ms = now_ms;
            peer->flow_send_limit = POTR_FLOW_SEND_LIMIT_UNLIMITED;
            peer->flow_limit_stalled_ms = 0U;
            peer->flow_query_next_ms = 0U;
            peer->flow_query_interval_ms = 0U;
            potr_internal_flow_control_peer_sync(ctx, peer);
        }
        else
        {
            cplat_local_lock_lock(ctx->flow_limit_mutex);
            cplat_atomic_store_u64(&ctx->flow_request_expire_ms, 0U, CPLAT_MEMORY_ORDER_RELEASE);
            cplat_atomic_store_u64(&ctx->flow_limit_received_ms, now_ms, CPLAT_MEMORY_ORDER_RELEASE);
            cplat_atomic_store_u64(&ctx->flow_send_limit, POTR_FLOW_SEND_LIMIT_UNLIMITED, CPLAT_MEMORY_ORDER_RELEASE);
            cplat_local_lock_unlock(ctx->flow_limit_mutex);
        }

        potr_internal_send_queue_wake_flow(&ctx->send_queue);
        return;
    }

    if (state == POTR_FLOW_CONTROL_STATE_SUPPRESS)
    {
        expire_ms = now_ms + ctx->flow_control_config.request_lifetime_ms;
    }

    /* 送出上限は、状態の値によらず上書きする。通番で表すため、重複して届いても判定は変わらない。 */
    if (peer != NULL)
    {
        uint64_t old_state = peer->flow_send_limit & POTR_FLOW_SEND_LIMIT_STATE_MASK;

        if (old_state == POTR_FLOW_SEND_LIMIT_UNNOTIFIED || old_state == POTR_FLOW_SEND_LIMIT_NO_PEER)
        {
            peer->flow_first_limit_ms = now_ms;
        }
        peer->flow_request_expire_ms = expire_ms;
        peer->flow_limit_received_ms = now_ms;
        peer->flow_send_limit = POTR_FLOW_SEND_LIMIT_HELD | (uint64_t)send_limit;
        potr_internal_flow_control_peer_sync(ctx, peer);
    }
    else
    {
        cplat_local_lock_lock(ctx->flow_limit_mutex);
        cplat_atomic_store_u64(&ctx->flow_request_expire_ms, expire_ms, CPLAT_MEMORY_ORDER_RELEASE);

        uint64_t old_state = cplat_atomic_load_u64(&ctx->flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE) &
                             POTR_FLOW_SEND_LIMIT_STATE_MASK;

        if (old_state == POTR_FLOW_SEND_LIMIT_UNNOTIFIED || old_state == POTR_FLOW_SEND_LIMIT_NO_PEER)
        {
            cplat_atomic_store_u64(&ctx->flow_first_limit_ms, now_ms, CPLAT_MEMORY_ORDER_RELEASE);
        }

        /* 受けた時刻を先に書く。送出上限を読んだ送信スレッドが、対応する時刻を必ず読めるようにするためである。 */
        cplat_atomic_store_u64(&ctx->flow_limit_received_ms, now_ms, CPLAT_MEMORY_ORDER_RELEASE);
        cplat_atomic_store_u64(&ctx->flow_send_limit, POTR_FLOW_SEND_LIMIT_HELD | (uint64_t)send_limit,
                               CPLAT_MEMORY_ORDER_RELEASE);
        cplat_local_lock_unlock(ctx->flow_limit_mutex);
    }

    potr_internal_send_queue_wake_flow(&ctx->send_queue);
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_flow_control_send_query(potr_context *ctx, struct potr_internal_peer_context *peer)
{
    potr_packet packet;
    potr_internal_packet_session_hdr shdr;
    uint8_t payload[POTR_FLOW_CONTROL_PAYLOAD_SIZE];
    uint8_t wire[FLOW_CONTROL_WIRE_SIZE];
    potr_internal_tx_session *session;
    const cplat_ipv4_endpoint *dest;
    int i;

    if (ctx == NULL || !uses_query_udp(ctx->service.type))
    {
        return;
    }

    shdr.service_id = ctx->service.service_id;
    if (peer != NULL)
    {
        shdr.session_id = peer->session_id;
        session_ts_to_hdr(&peer->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);
        session = &peer->tx_session;
        dest = peer->dest_addr;
    }
    else
    {
        shdr.session_id = ctx->session_id;
        session_ts_to_hdr(&ctx->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);
        session = &ctx->tx_session;
        dest = ctx->dest_addr;
    }

    if (potr_internal_packet_build_flow_control(&packet, &shdr, payload, (uint8_t)POTR_FLOW_CONTROL_STATE_QUERY, 0U) !=
        POTR_OK)
    {
        return;
    }

    for (i = 0; i < ctx->n_path; i++)
    {
        size_t wire_len = sizeof(wire);

        if (ctx->sock[i] == CPLAT_INVALID_SOCKET || endpoint_is_unset(&dest[i]))
        {
            continue;
        }

        if (potr_internal_packet_seal_for_context(ctx, session, &packet, wire, &wire_len) != POTR_OK)
        {
            continue;
        }

        /* 問い合わせは止まっている間に周期を延ばして送出し直すため、戻り値は明示的に破棄する。 */
        (void)potr_internal_udp_sendto(ctx, i, wire, wire_len, &dest[i], NULL);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_flow_control_on_peer_packet(potr_context *ctx, struct potr_internal_peer_context *peer,
                                               const potr_packet *pkt)
{
    uint64_t now_ms;
    int is_limit_notification = 0;
    int is_piggyback_limit = 0;

    if (ctx == NULL || pkt == NULL)
    {
        return;
    }

    if (ctx->service.type != POTR_TYPE_UNICAST_BIDIR && ctx->service.type != POTR_TYPE_UNICAST_BIDIR_N1)
    {
        return;
    }

    if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
    {
        return;
    }

    /* 問い合わせ以外のフロー制御パケットは、直後の on_packet() が送出上限を更新するため問い合わせを重ねない。 */
    if ((pkt->flags & POTR_FLAG_FLOW_CONTROL) != 0U && pkt->payload != NULL && pkt->payload_len >= 1U &&
        pkt->payload[0] != POTR_FLOW_CONTROL_STATE_QUERY)
    {
        is_limit_notification = 1;
    }
    else if ((pkt->flags & POTR_FLAG_DATA) != 0U && (pkt->flags & POTR_FLAG_SEND_LIMIT) != 0U &&
             (pkt->flags &
              (POTR_FLAG_FIN | POTR_FLAG_NACK | POTR_FLAG_REJECT | POTR_FLAG_FLOW_CONTROL | POTR_FLAG_PING)) == 0U)
    {
        /* 送出上限を相乗りした DATA パケット。この関数内で送出上限を更新するため、問い合わせを重ねない。 */
        is_piggyback_limit = 1;
        is_limit_notification = 1;
    }

    if (peer != NULL)
    {
        peer->flow_last_packet_ms = now_ms;
        if (is_piggyback_limit != 0)
        {
            uint64_t state = peer->flow_send_limit & POTR_FLOW_SEND_LIMIT_STATE_MASK;
            uint32_t send_limit = pkt->ack_num;

            /*
             * 保持している送出上限より先へ進んでいる場合 (または未通知・相手なしの場合) だけ更新する。
             * 再送された DATA に載っている古い値は、循環を考慮した差の比較で無視する。
             */
            if (state != POTR_FLOW_SEND_LIMIT_UNLIMITED &&
                (state != POTR_FLOW_SEND_LIMIT_HELD ||
                 (int32_t)(send_limit - (uint32_t)(peer->flow_send_limit & 0xFFFFFFFFU)) > 0))
            {
                if (state == POTR_FLOW_SEND_LIMIT_UNNOTIFIED || state == POTR_FLOW_SEND_LIMIT_NO_PEER)
                {
                    peer->flow_first_limit_ms = now_ms;
                }
                peer->flow_limit_received_ms = now_ms;
                peer->flow_send_limit = POTR_FLOW_SEND_LIMIT_HELD | (uint64_t)send_limit;
                potr_internal_flow_control_peer_sync(ctx, peer);
                if (peer->flow_limit_stalled_ms != 0U || peer->flow_deferred_entries != 0)
                {
                    potr_internal_send_queue_wake_flow(&ctx->send_queue);
                }
            }
        }
        else if (is_limit_notification == 0 &&
                 (peer->flow_send_limit & POTR_FLOW_SEND_LIMIT_STATE_MASK) == POTR_FLOW_SEND_LIMIT_NO_PEER)
        {
            uint32_t next_seq = read_next_seq(&peer->send_window, peer->send_window_mutex);

            peer->flow_send_limit = POTR_FLOW_SEND_LIMIT_UNNOTIFIED | (uint64_t)next_seq;
            peer->flow_limit_received_ms = now_ms;
            peer->flow_limit_stalled_ms = 0U;
            peer->flow_query_next_ms = 0U;
            peer->flow_query_interval_ms = 0U;
            potr_internal_flow_control_peer_sync(ctx, peer);
            potr_internal_flow_control_send_query(ctx, peer);
        }

        return;
    }

    /*
     * 時刻を書いてから状態を読む。この順序と期限切れ側の再確認を逐次一貫にして、
     * 期限切れ側が新時刻を見なければ、受信側が「相手なし」を見て復帰することを保証する。
     * 通知を受けるロックは、進んだ相乗り上限の反映と「相手なし」からの復帰にだけ使う。
     */
    cplat_atomic_store_u64(&ctx->flow_last_packet_ms, now_ms, CPLAT_MEMORY_ORDER_SEQ_CST);
    if (is_piggyback_limit != 0)
    {
        uint32_t send_limit = pkt->ack_num;
        uint64_t current = cplat_atomic_load_u64(&ctx->flow_send_limit, CPLAT_MEMORY_ORDER_SEQ_CST);

        if ((current & POTR_FLOW_SEND_LIMIT_STATE_MASK) != POTR_FLOW_SEND_LIMIT_UNLIMITED &&
            ((current & POTR_FLOW_SEND_LIMIT_STATE_MASK) != POTR_FLOW_SEND_LIMIT_HELD ||
             (int32_t)(send_limit - (uint32_t)current) > 0))
        {
            int updated = 0;

            cplat_local_lock_lock(ctx->flow_limit_mutex);
            current = cplat_atomic_load_u64(&ctx->flow_send_limit, CPLAT_MEMORY_ORDER_SEQ_CST);
            for (;;)
            {
                uint64_t state = current & POTR_FLOW_SEND_LIMIT_STATE_MASK;

                if (state == POTR_FLOW_SEND_LIMIT_UNLIMITED ||
                    (state == POTR_FLOW_SEND_LIMIT_HELD && (int32_t)(send_limit - (uint32_t)current) <= 0))
                {
                    break;
                }

                if (state == POTR_FLOW_SEND_LIMIT_UNNOTIFIED || state == POTR_FLOW_SEND_LIMIT_NO_PEER)
                {
                    cplat_atomic_store_u64(&ctx->flow_first_limit_ms, now_ms, CPLAT_MEMORY_ORDER_RELEASE);
                }
                cplat_atomic_store_u64(&ctx->flow_limit_received_ms, now_ms, CPLAT_MEMORY_ORDER_RELEASE);
                if (cplat_atomic_compare_exchange_u64(&ctx->flow_send_limit, &current,
                                                      POTR_FLOW_SEND_LIMIT_HELD | (uint64_t)send_limit,
                                                      CPLAT_MEMORY_ORDER_SEQ_CST) != 0)
                {
                    updated = 1;
                    break;
                }
            }
            cplat_local_lock_unlock(ctx->flow_limit_mutex);
            if (updated != 0)
            {
                potr_internal_send_queue_wake_flow(&ctx->send_queue);
            }
        }

        return;
    }

    /*
     * 受信のたびに send_window_mutex を取得して送信スレッドと競合しないよう、
     * 「相手なし」の場合だけ通番を読む。読んだ後に状態が変わった場合は、比較交換が失敗して何もしない。
     */
    if (is_limit_notification == 0 &&
        (cplat_atomic_load_u64(&ctx->flow_send_limit, CPLAT_MEMORY_ORDER_SEQ_CST) & POTR_FLOW_SEND_LIMIT_STATE_MASK) ==
            POTR_FLOW_SEND_LIMIT_NO_PEER)
    {
        uint64_t expected = POTR_FLOW_SEND_LIMIT_NO_PEER;
        uint32_t next_seq = read_next_seq(&ctx->send_window, ctx->send_window_mutex);

        int updated;

        /* send_window_mutex は解放済み。flow_limit_mutex の内側でほかのロックを取得しない。 */
        cplat_local_lock_lock(ctx->flow_limit_mutex);
        updated = cplat_atomic_compare_exchange_u64(&ctx->flow_send_limit, &expected,
                                                     POTR_FLOW_SEND_LIMIT_UNNOTIFIED | (uint64_t)next_seq,
                                                     CPLAT_MEMORY_ORDER_SEQ_CST);
        if (updated != 0)
        {
            cplat_atomic_store_u64(&ctx->flow_limit_received_ms, now_ms, CPLAT_MEMORY_ORDER_RELEASE);
        }
        cplat_local_lock_unlock(ctx->flow_limit_mutex);
        if (updated != 0)
        {
            potr_internal_flow_control_send_query(ctx, NULL);
        }
    }
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_flow_control_piggyback_limit(potr_context *ctx, potr_peer_id peer_id, uint32_t *send_limit)
{
    struct potr_internal_flow_control *fc;
    flow_account *account;
    int held = 0;

    if (send_limit == NULL)
    {
        return 0;
    }

    *send_limit = 0U;

    /* 相乗りは unicast_bidir と unicast_bidir_n1 で送出上限を使う受信側だけが行う。対象外ではロックを取らない。 */
    if (ctx == NULL || ctx->flow_control == NULL ||
        (ctx->service.type != POTR_TYPE_UNICAST_BIDIR && ctx->service.type != POTR_TYPE_UNICAST_BIDIR_N1) ||
        is_unlimited_mode(ctx->flow_control))
    {
        return 0;
    }

    fc = ctx->flow_control;
    cplat_local_lock_lock(fc->mutex);
    account = find_account(ctx, fc, peer_id, 0);

    /* 送出上限をまだ計算していない相手と、容量超過で受信を再開しない相手には相乗りしない。 */
    if (account != NULL && account->has_next_seq != 0 && account->refused == 0)
    {
        uint32_t limit = account->next_seq + send_allowance(ctx, fc, account);

        *send_limit = limit;
        account->reserved_limit = limit;
        account->reserved = 1;
        held = 1;
    }

    cplat_local_lock_unlock(fc->mutex);

    return held;
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_flow_control_piggyback_complete(potr_context *ctx, potr_peer_id peer_id, uint32_t send_limit,
                                                  int sent)
{
    struct potr_internal_flow_control *fc = ctx->flow_control;
    flow_account *account;

    if (fc == NULL)
    {
        return;
    }
    cplat_local_lock_lock(fc->mutex);
    account = find_account(ctx, fc, peer_id, 0);
    if (account != NULL && account->reserved != 0 && account->reserved_limit == send_limit)
    {
        account->reserved = 0;
        if (sent == 0)
        {
            fc->turn_grant_retry = 1;
        }
        if (sent != 0 && account->has_next_seq != 0 && account->refused == 0 &&
            (account->notified == 0 || (int32_t)(send_limit - account->notified_limit) >= 0))
        {
            /* 送出の間に、受信スレッドがより先の上限を知らせていた場合は、その記録を保つ。 */
            record_notified(fc, account, send_limit);
        }
    }
    cplat_local_lock_unlock(fc->mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_flow_control_suppressed(potr_context *ctx, uint64_t now_ms, int *wait_ms, uint32_t *allowance)
{
    uint64_t until_ms = 0U;
    uint32_t next_seq;

    if (ctx == NULL || wait_ms == NULL || allowance == NULL)
    {
        return 0;
    }

    *allowance = UINT32_MAX;

    if (ctx->is_multi_peer || is_raw_type(ctx->service.type))
    {
        return 0;
    }

    /* close の開始後は抑制を無視し、送信キューに残っている DATA を送出する。送出上限も同じく無視する。 */
    if (cplat_atomic_load_i32(&ctx->close_requested, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
    {
        ctx->flow_query_next_ms = 0U;
        ctx->flow_query_interval_ms = 0U;
        return 0;
    }

    if (is_oneway_udp_type(ctx->service.type))
    {
        size_t tracked = 0U;
        int suppressed;

        if (ctx->service.type == POTR_TYPE_UNICAST)
        {
            uint64_t handle_limit = cplat_atomic_load_u64(&ctx->flow_send_limit, CPLAT_MEMORY_ORDER_SEQ_CST);
            uint64_t handle_state = handle_limit & POTR_FLOW_SEND_LIMIT_STATE_MASK;
            int pending = cplat_atomic_load_i32(&ctx->flow_request_pending, CPLAT_MEMORY_ORDER_ACQUIRE);

            if (pending == 0 &&
                (handle_state == POTR_FLOW_SEND_LIMIT_UNLIMITED || handle_state == POTR_FLOW_SEND_LIMIT_NO_PEER))
            {
                ctx->flow_query_next_ms = 0U;
                ctx->flow_query_interval_ms = 0U;
                return 0;
            }

            if (pending != 0)
            {
                cplat_atomic_store_i32(&ctx->flow_request_pending, 0, CPLAT_MEMORY_ORDER_RELEASE);
                next_seq = read_next_seq(&ctx->send_window, ctx->send_window_mutex);
                ctx->flow_receiver_limit_stalled = 0;
                suppressed = potr_internal_receiver_flow_suppressed(ctx, now_ms, next_seq, &tracked, &until_ms);
                if (tracked != 0U)
                {
                    cplat_atomic_store_i32(&ctx->flow_request_pending, 1, CPLAT_MEMORY_ORDER_RELEASE);
                }

                if (suppressed != 0 && ctx->flow_receiver_limit_stalled != 0)
                {
                    until_ms = earlier_time(until_ms, advance_query_timer(ctx, NULL, now_ms));
                }
                else
                {
                    ctx->flow_query_next_ms = 0U;
                    ctx->flow_query_interval_ms = 0U;
                }

                if (suppressed == 0)
                {
                    return 0;
                }
            }
            else
            {
                /* 受信者からまだ 1 パケットも受けていない間の初期の幅を判定する。 */
                uint64_t received_ms = cplat_atomic_load_u64(&ctx->flow_limit_received_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
                uint64_t last_pkt_ms = cplat_atomic_load_u64(&ctx->flow_last_packet_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
                uint64_t activity_ms = 0U;
                uint64_t limit_until_ms = 0U;
                potr_internal_send_limit_state limit_state;

                if (handle_state != POTR_FLOW_SEND_LIMIT_UNNOTIFIED)
                {
                    activity_ms = later_time(received_ms, last_pkt_ms);
                }
                next_seq = read_next_seq(&ctx->send_window, ctx->send_window_mutex);
                limit_state = flow_send_limit_judge(handle_limit, activity_ms, &ctx->flow_limit_stalled_ms, next_seq,
                                                    now_ms, ctx->flow_control_config.request_lifetime_ms,
                                                    &limit_until_ms, allowance);
                if (limit_state == POTR_SEND_LIMIT_EXPIRED)
                {
                    if (!expire_limit_if_unchanged(ctx, handle_limit, received_ms, last_pkt_ms))
                    {
                        *wait_ms = POTR_INTERNAL_FLOW_RECHECK_MS;
                        return 1;
                    }
                    ctx->flow_limit_stalled_ms = 0U;
                    ctx->flow_query_next_ms = 0U;
                    ctx->flow_query_interval_ms = 0U;
                    *allowance = UINT32_MAX;
                    return 0;
                }

                if (limit_state != POTR_SEND_LIMIT_REACHED)
                {
                    ctx->flow_query_next_ms = 0U;
                    ctx->flow_query_interval_ms = 0U;
                    return 0;
                }

                until_ms = earlier_time(limit_until_ms, advance_query_timer(ctx, NULL, now_ms));
            }
        }
        else
        {
            /*
             * multicast と broadcast: 初期の幅は適用しない。
             * 要求、送出上限、積算、除外のいずれも残っていない間は、受信者の管理表を走査しない。
             */
            if (cplat_atomic_load_i32(&ctx->flow_request_pending, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
            {
                ctx->flow_query_next_ms = 0U;
                ctx->flow_query_interval_ms = 0U;
                return 0;
            }

            cplat_atomic_store_i32(&ctx->flow_request_pending, 0, CPLAT_MEMORY_ORDER_RELEASE);

            /* send_window_mutex と callback_mutex を重ねて保持しないよう、通番を先に読む。 */
            next_seq = read_next_seq(&ctx->send_window, ctx->send_window_mutex);
            ctx->flow_receiver_limit_stalled = 0;
            suppressed = potr_internal_receiver_flow_suppressed(ctx, now_ms, next_seq, &tracked, &until_ms);
            if (tracked != 0U)
            {
                cplat_atomic_store_i32(&ctx->flow_request_pending, 1, CPLAT_MEMORY_ORDER_RELEASE);
            }

            if (suppressed != 0 && ctx->flow_receiver_limit_stalled != 0)
            {
                until_ms = earlier_time(until_ms, advance_query_timer(ctx, NULL, now_ms));
            }
            else
            {
                ctx->flow_query_next_ms = 0U;
                ctx->flow_query_interval_ms = 0U;
            }

            if (suppressed == 0)
            {
                return 0;
            }
        }
    }
    else
    {
        uint64_t expire_ms = cplat_atomic_load_u64(&ctx->flow_request_expire_ms, CPLAT_MEMORY_ORDER_ACQUIRE);

        /* 送出上限を先に読む。受信スレッドは時刻を先に書くため、読んだ送出上限に対応する時刻以降の値を読める。 */
        uint64_t send_limit = cplat_atomic_load_u64(&ctx->flow_send_limit, CPLAT_MEMORY_ORDER_SEQ_CST);
        uint64_t received_ms = cplat_atomic_load_u64(&ctx->flow_limit_received_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
        uint64_t activity_ms = received_ms;
        uint64_t last_pkt_ms = 0U;
        uint64_t limit_until_ms = 0U;
        potr_internal_send_limit_state limit_state;
        int request_valid = expire_ms > now_ms;
        int udp_query = uses_query_udp(ctx->service.type);

        if (udp_query)
        {
            last_pkt_ms = cplat_atomic_load_u64(&ctx->flow_last_packet_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
            activity_ms = later_time(received_ms, last_pkt_ms);
        }

        next_seq = read_next_seq(&ctx->send_window, ctx->send_window_mutex);

        /*
         * UDP では、最後の DATA 送出から有効期限以上途切れた場合、保持中の送出上限を「次の通番 + 初期の幅」との
         * 小さいほうへ縮める (上限ありの状態のときだけ)。DATA 未送出では最初の上限受理を起点にする。
         */
        uint64_t idle_since_ms = ctx->flow_last_data_send_ms;

        if (idle_since_ms == 0U)
        {
            idle_since_ms = cplat_atomic_load_u64(&ctx->flow_first_limit_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
        }
        if (udp_query && idle_since_ms != 0U &&
            now_ms >= idle_since_ms + (uint64_t)ctx->flow_control_config.request_lifetime_ms)
        {
            cplat_local_lock_lock(ctx->flow_limit_mutex);
            send_limit = cplat_atomic_load_u64(&ctx->flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
            /* ロック待ちにセッションが変わった場合は、新しい起点から判定し直す。 */
            if (ctx->flow_last_data_send_ms == 0U)
            {
                idle_since_ms = cplat_atomic_load_u64(&ctx->flow_first_limit_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
            }
            if (idle_since_ms != 0U &&
                now_ms >= idle_since_ms + (uint64_t)ctx->flow_control_config.request_lifetime_ms)
            {
                send_limit = flow_send_limit_clamp_initial(send_limit, next_seq);
                cplat_atomic_store_u64(&ctx->flow_send_limit, send_limit, CPLAT_MEMORY_ORDER_RELEASE);
            }
            cplat_local_lock_unlock(ctx->flow_limit_mutex);
        }

        if (udp_query && (send_limit & POTR_FLOW_SEND_LIMIT_STATE_MASK) == POTR_FLOW_SEND_LIMIT_UNNOTIFIED)
        {
            /* 初期の幅での停止は、最初の通知以外のパケットによって期限を延ばさない。 */
            activity_ms = 0U;
        }

        limit_state = flow_send_limit_judge(send_limit, activity_ms, &ctx->flow_limit_stalled_ms, next_seq, now_ms,
                                            ctx->flow_control_config.request_lifetime_ms, &limit_until_ms,
                                            allowance);

        /*
         * UDP で有効期限の間、相手から何も届かなかった場合は「相手なし」へ遷移し、制限しない。
         * TCP は従来の破棄条件を維持し、送出上限の値は書き換えない。
         */
        if (limit_state == POTR_SEND_LIMIT_EXPIRED && udp_query)
        {
            if (!expire_limit_if_unchanged(ctx, send_limit, received_ms, last_pkt_ms))
            {
                *wait_ms = POTR_INTERNAL_FLOW_RECHECK_MS;
                return 1;
            }
            ctx->flow_limit_stalled_ms = 0U;
        }

        if (limit_state != POTR_SEND_LIMIT_REACHED)
        {
            ctx->flow_query_next_ms = 0U;
            ctx->flow_query_interval_ms = 0U;
        }

        if (request_valid == 0 && limit_state != POTR_SEND_LIMIT_REACHED)
        {
            return 0;
        }

        /* 相手を切断と判定した場合は、その相手の要求と送出上限を取り消す。 */
        if (cplat_atomic_load_i32(&ctx->health_alive, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
        {
            cplat_local_lock_lock(ctx->flow_limit_mutex);
            cplat_atomic_store_u64(&ctx->flow_request_expire_ms, 0U, CPLAT_MEMORY_ORDER_RELEASE);
            cplat_atomic_store_u64(&ctx->flow_send_limit, POTR_FLOW_SEND_LIMIT_NO_PEER, CPLAT_MEMORY_ORDER_RELEASE);
            cplat_local_lock_unlock(ctx->flow_limit_mutex);
            ctx->flow_limit_stalled_ms = 0U;
            ctx->flow_query_next_ms = 0U;
            ctx->flow_query_interval_ms = 0U;
            *allowance = UINT32_MAX;
            return 0;
        }

        if (request_valid != 0)
        {
            until_ms = expire_ms;
        }

        if (limit_state == POTR_SEND_LIMIT_REACHED)
        {
            until_ms = earlier_time(until_ms, limit_until_ms);
            if (udp_query)
            {
                until_ms = earlier_time(until_ms, advance_query_timer(ctx, NULL, now_ms));
            }
        }
    }

    *wait_ms = recheck_wait_ms(now_ms, until_ms);

    return 1;
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_flow_control_peer_state(potr_context *ctx, potr_peer_id peer_id, uint64_t now_ms, int *deferred,
                                          uint32_t *allowance)
{
    potr_internal_peer_context *peer;
    int suppressed = 0;

    if (allowance != NULL)
    {
        *allowance = UINT32_MAX;
    }

    if (ctx == NULL || deferred == NULL)
    {
        return 0;
    }

    *deferred = 0;

    if (!ctx->is_multi_peer)
    {
        return 0;
    }

    /*
     * ピア識別子と期限が一致する記録だけを使う。衝突、切断、抑制、保留、上限到達、期限切れでは
     * 記録が無効になるため、従来のロック下の判定へ戻る。ピアのポインターはロック外へ持ち出さない。
     * 記録は送出上限までの残りの数を持たないため、送出できる数を求める場合は使わない。
     */
    if (allowance == NULL)
    {
        uint64_t epoch = cplat_atomic_load_u64(&ctx->flow_peer_ready_epoch, CPLAT_MEMORY_ORDER_ACQUIRE);
        uint64_t ready = cplat_atomic_load_u64(
            &ctx->flow_peer_ready[peer_id & (POTR_FLOW_PEER_READY_CACHE_SIZE - 1U)], CPLAT_MEMORY_ORDER_ACQUIRE);
        if (epoch == (now_ms >> 32U) && (potr_peer_id)(ready >> 32U) == peer_id && ready != 0U &&
            (uint32_t)ready > (uint32_t)now_ms)
        {
            return 0;
        }
    }

    cplat_local_lock_lock(ctx->peers_mutex);
    peer = potr_internal_peer_find_by_id(ctx, peer_id);

    /* close の開始後は、抑制の要求と送出上限を無視する。 */
    if (peer != NULL && cplat_atomic_load_i32(&ctx->close_requested, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
    {
        suppressed = peer_state_locked(ctx, peer, now_ms, 1, allowance);
    }

    if (peer != NULL)
    {
        *deferred = peer->flow_deferred_entries;
    }

    cplat_local_lock_unlock(ctx->peers_mutex);

    return suppressed;
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_flow_control_peer_mark_blocking(potr_context *ctx, potr_peer_id peer_id, uint64_t now_ms)
{
    potr_internal_peer_context *peer;

    if (ctx == NULL || !ctx->is_multi_peer || ctx->peers_mutex == NULL)
    {
        return;
    }

    cplat_local_lock_lock(ctx->peers_mutex);
    peer = potr_internal_peer_find_by_id(ctx, peer_id);
    if (peer != NULL && peer->active &&
        cplat_atomic_load_i32(&peer->health_alive, CPLAT_MEMORY_ORDER_ACQUIRE) != 0 && peer->flow_blocking == 0)
    {
        if (cplat_atomic_load_i32(&ctx->flow_blocking_peers, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
        {
            int i;

            /* 最初の妨げの時点を、積算区間の開始状態として全ピアについて記録する。 */
            for (i = 0; i < ctx->max_peers; i++)
            {
                potr_internal_peer_context *other = &ctx->peers[i];

                other->flow_last_stopping = other->active &&
                    cplat_atomic_load_i32(&other->health_alive, CPLAT_MEMORY_ORDER_ACQUIRE) != 0 &&
                    peer_state_locked(ctx, other, now_ms, 0, NULL);
            }
        }
        peer->flow_blocking = 1;
        peer->flow_stall_accum_units = 0U;
        peer->flow_stall_last_ms = now_ms;
        /* 加算前の値は使わないため、戻り値は明示的に破棄する。 */
        (void)cplat_atomic_fetch_add_i32(&ctx->flow_blocking_peers, 1, CPLAT_MEMORY_ORDER_RELEASE);
    }
    cplat_local_lock_unlock(ctx->peers_mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_flow_control_note_queue_full(potr_context *ctx, uint64_t now_ms)
{
    uint64_t expected = 0U;

    if (ctx == NULL || !ctx->is_multi_peer || now_ms == 0U)
    {
        return;
    }

    /* 最初の満杯発生時刻を保持する。すでに記録済みの場合は上書きしない。戻り値は使わない。 */
    (void)cplat_atomic_compare_exchange_u64(&ctx->flow_queue_full_ms, &expected, now_ms, CPLAT_MEMORY_ORDER_ACQ_REL);
}

/* Doxygen コメントは、ヘッダーに記載 */
size_t potr_internal_flow_control_collect_slow_peers_locked(potr_context *ctx, uint64_t now_ms, potr_peer_id *out_peers,
                                                            uint64_t *out_accum_ms, size_t max_out,
                                                            uint64_t *next_check_ms)
{
    uint64_t full_ms;
    uint32_t grace_ms;
    uint64_t grace_units;
    size_t connected_count = 0U;
    size_t stopping_count = 0U;
    size_t previous_stopping_count = 0U;
    size_t limit;
    size_t count = 0U;
    uint64_t earliest = 0U;
    int i;

    if (next_check_ms != NULL)
    {
        *next_check_ms = 0U;
    }

    if (ctx == NULL || !ctx->is_multi_peer || ctx->peers == NULL ||
        cplat_atomic_load_i32(&ctx->close_requested, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
    {
        return 0U;
    }

    full_ms = cplat_atomic_exchange_u64(&ctx->flow_queue_full_ms, 0U, CPLAT_MEMORY_ORDER_ACQ_REL);
    if (full_ms == 0U && cplat_atomic_load_i32(&ctx->flow_blocking_peers, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
    {
        return 0U;
    }

    grace_ms = ctx->flow_control_config.grace_ms;
    if (grace_ms == 0U)
    {
        grace_ms = (uint32_t)POTR_DEFAULT_FLOW_CONTROL_GRACE_MS;
    }
    grace_units = (uint64_t)grace_ms * (uint64_t)POTR_FLOW_ACCUM_DECAY_DIVISOR;

    /* 前回と今回の停止数を先に確定し、全体が遅かった区間の積算を据え置く。 */
    for (i = 0; i < ctx->max_peers; i++)
    {
        potr_internal_peer_context *peer = &ctx->peers[i];

        if (!peer->active || cplat_atomic_load_i32(&peer->health_alive, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
        {
            continue;
        }
        connected_count++;
        if (peer->flow_last_stopping)
        {
            previous_stopping_count++;
        }
        if (peer_state_locked(ctx, peer, now_ms, 0, NULL))
        {
            stopping_count++;
        }
    }
    limit = flow_excluded_limit(connected_count, ctx->flow_control_config.excluded_receiver_count,
                                ctx->flow_control_config.excluded_receiver_percent);

    /* 第 1 走査: 接続中ピア数・同時停止ピア数の集計、キュー満杯による妨げの反映、積算の更新。 */
    for (i = 0; i < ctx->max_peers; i++)
    {
        potr_internal_peer_context *peer = &ctx->peers[i];
        int stopping;

        if (!peer->active || cplat_atomic_load_i32(&peer->health_alive, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
        {
            continue;
        }

        stopping = peer_state_locked(ctx, peer, now_ms, 0, NULL);

        /* 送信キュー満杯の記録があり、保留エントリを持つ止める状態のピアには妨げを記録する。 */
        if (full_ms != 0U && peer->flow_deferred_entries > 0 && stopping != 0 && peer->flow_blocking == 0)
        {
            uint64_t since_ms = later_time(full_ms, peer->flow_deferred_since_ms);

            if (since_ms > now_ms)
            {
                since_ms = now_ms;
            }
            peer->flow_blocking = 1;
            peer->flow_stall_accum_units = 0U;
            peer->flow_stall_last_ms = since_ms;
            peer->flow_last_stopping = stopping;
            /* 加算前の値は使わないため、戻り値は明示的に破棄する。 */
            (void)cplat_atomic_fetch_add_i32(&ctx->flow_blocking_peers, 1, CPLAT_MEMORY_ORDER_RELEASE);
        }

        if (peer->flow_blocking != 0)
        {
            uint64_t elapsed_ms = 0U;

            if (peer->flow_stall_last_ms != 0U && now_ms > peer->flow_stall_last_ms)
            {
                elapsed_ms = now_ms - peer->flow_stall_last_ms;
            }

            /* 解除で起床しても、直前に止めていた区間は前回の状態で加算する。 */
            if (previous_stopping_count <= limit)
            {
                peer->flow_stall_accum_units = flow_stall_accum_update(
                    peer->flow_stall_accum_units, elapsed_ms, peer->flow_last_stopping);
            }
            peer->flow_stall_last_ms = now_ms;

            /* 止める状態が解けて積算が 0 に戻った時点で妨げの記録を消す。 */
            if (stopping == 0 && peer->flow_stall_accum_units == 0U)
            {
                potr_internal_flow_control_peer_clear_blocking(ctx, peer);
            }
        }
        peer->flow_last_stopping = stopping;
    }

    /* 第 2 走査: 上限判定、積算の上限丸め、切断対象の収集、次の判定時刻の算出。 */
    for (i = 0; i < ctx->max_peers; i++)
    {
        potr_internal_peer_context *peer = &ctx->peers[i];
        int stopping;

        if (!peer->active || peer->flow_blocking == 0 ||
            cplat_atomic_load_i32(&peer->health_alive, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
        {
            continue;
        }

        if (previous_stopping_count > limit || stopping_count > limit)
        {
            /* 全体が遅い区間は積算を据え置き、新しい切断もしない。 */
            if (peer->flow_stall_accum_units > grace_units)
            {
                peer->flow_stall_accum_units = grace_units;
            }
            earliest = earlier_time(earliest, now_ms + (uint64_t)POTR_INTERNAL_FLOW_RECHECK_MS);
            continue;
        }

        stopping = peer_state_locked(ctx, peer, now_ms, 0, NULL);
        if (stopping != 0 && peer->flow_stall_accum_units >= grace_units)
        {
            if (out_peers != NULL && count < max_out)
            {
                out_peers[count] = peer->peer_id;
                if (out_accum_ms != NULL)
                {
                    out_accum_ms[count] = peer->flow_stall_accum_units / (uint64_t)POTR_FLOW_ACCUM_DECAY_DIVISOR;
                }
                count++;
            }
            continue;
        }

        if (stopping != 0)
        {
            uint64_t rem_units = grace_units - peer->flow_stall_accum_units;
            uint64_t rem_ms = (rem_units + (uint64_t)POTR_FLOW_ACCUM_DECAY_DIVISOR - 1U) /
                              (uint64_t)POTR_FLOW_ACCUM_DECAY_DIVISOR;

            earliest = earlier_time(earliest, now_ms + rem_ms);
        }
        else
        {
            earliest = earlier_time(earliest, now_ms + (uint64_t)POTR_INTERNAL_FLOW_RECHECK_MS);
        }
    }

    if (next_check_ms != NULL)
    {
        *next_check_ms = earliest;
    }

    return count;
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_flow_control_slow_peer_record_locked(potr_context *ctx, uint32_t session_id,
                                                        const cplat_timespec *session_ts, uint64_t now_ms)
{
    uint64_t duration_ms;
    potr_internal_slow_peer_reject_entry *same_slot = NULL;
    potr_internal_slow_peer_reject_entry *free_slot = NULL;
    potr_internal_slow_peer_reject_entry *oldest_slot = NULL;
    potr_internal_slow_peer_reject_entry *target;
    size_t i;
    size_t retain_limit;
    size_t connected_count = 0U;
    int peer_index;

    if (ctx == NULL || session_ts == NULL)
    {
        return;
    }

    duration_ms = slow_peer_reject_duration_ms(ctx);
    for (peer_index = 0; peer_index < ctx->max_peers && ctx->peers != NULL; peer_index++)
    {
        const potr_internal_peer_context *peer = &ctx->peers[peer_index];

        if (peer->active != 0 && cplat_atomic_load_i32(&peer->health_alive, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
        {
            connected_count++;
        }
    }
    retain_limit = flow_excluded_limit(connected_count, ctx->flow_control_config.excluded_receiver_count,
                                       ctx->flow_control_config.excluded_receiver_percent);
    if (retain_limit < FLOW_SLOW_PEER_RETAIN_MIN)
    {
        retain_limit = FLOW_SLOW_PEER_RETAIN_MIN;
    }
    if (retain_limit > POTR_FLOW_SLOW_PEER_REJECT_MAX)
    {
        retain_limit = POTR_FLOW_SLOW_PEER_REJECT_MAX;
    }

    for (i = 0U; i < POTR_FLOW_SLOW_PEER_REJECT_MAX; i++)
    {
        potr_internal_slow_peer_reject_entry *entry = &ctx->flow_slow_rejected[i];

        if (entry->active == 0U)
        {
            if (free_slot == NULL)
            {
                free_slot = entry;
            }
            continue;
        }

        if (slow_peer_reject_expired(entry, now_ms, duration_ms))
        {
            slow_peer_reject_clear(ctx, entry);
            if (free_slot == NULL)
            {
                free_slot = entry;
            }
            continue;
        }

        if (entry->session_id == session_id && cplat_timespec_cmp(&entry->session_ts, session_ts) == 0)
        {
            same_slot = entry;
        }

        if (oldest_slot == NULL || entry->disconnected_ms < oldest_slot->disconnected_ms ||
            (entry->disconnected_ms == oldest_slot->disconnected_ms && entry->record_seq < oldest_slot->record_seq))
        {
            oldest_slot = entry;
        }
    }

    if (same_slot != NULL)
    {
        target = same_slot;
    }
    else if (free_slot != NULL)
    {
        target = free_slot;
        ctx->flow_slow_rejected_count++;
    }
    else
    {
        target = oldest_slot;
    }

    target->session_id = session_id;
    target->session_ts = *session_ts;
    target->disconnected_ms = now_ms;
    target->record_seq = ++ctx->flow_slow_rejected_seq;
    target->active = 1U;
    target->drop_logged = 0U;

    while (ctx->flow_slow_rejected_count > retain_limit)
    {
        oldest_slot = NULL;
        for (i = 0U; i < POTR_FLOW_SLOW_PEER_REJECT_MAX; i++)
        {
            potr_internal_slow_peer_reject_entry *entry = &ctx->flow_slow_rejected[i];

            if (entry->active != 0U &&
                (oldest_slot == NULL || entry->disconnected_ms < oldest_slot->disconnected_ms ||
                 (entry->disconnected_ms == oldest_slot->disconnected_ms && entry->record_seq < oldest_slot->record_seq)))
            {
                oldest_slot = entry;
            }
        }

        slow_peer_reject_clear(ctx, oldest_slot);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_flow_control_slow_peer_rejected_locked(potr_context *ctx, uint32_t session_id,
                                                         const cplat_timespec *session_ts, uint64_t now_ms)
{
    uint64_t duration_ms;
    size_t i;

    if (ctx == NULL || session_ts == NULL || ctx->flow_slow_rejected_count == 0U)
    {
        return 0;
    }

    duration_ms = slow_peer_reject_duration_ms(ctx);

    for (i = 0U; i < POTR_FLOW_SLOW_PEER_REJECT_MAX; i++)
    {
        potr_internal_slow_peer_reject_entry *entry = &ctx->flow_slow_rejected[i];

        if (entry->active == 0U)
        {
            continue;
        }

        if (slow_peer_reject_expired(entry, now_ms, duration_ms))
        {
            slow_peer_reject_clear(ctx, entry);
            continue;
        }

        if (entry->session_id == session_id && cplat_timespec_cmp(&entry->session_ts, session_ts) == 0)
        {
            if (entry->drop_logged == 0U)
            {
                uint64_t remaining_ms =
                    (now_ms >= entry->disconnected_ms) ? (duration_ms - (now_ms - entry->disconnected_ms)) : duration_ms;

                entry->drop_logged = 1U;
                POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                           "recv[service_id=%" PRId64 "]: ignoring packets from disconnected slow peer session=%u "
                           "(remaining=%" PRIu64 "ms)",
                           ctx->service.service_id, session_id, remaining_ms);
            }
            return 1;
        }
    }

    return 0;
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_flow_control_report_limit(potr_context *ctx, uint32_t *send_limit, uint32_t *max_allowance)
{
    struct potr_internal_flow_control *fc;
    flow_account *account;
    int held = 0;

    if (send_limit != NULL)
    {
        *send_limit = 0U;
    }

    if (max_allowance != NULL)
    {
        *max_allowance = 0U;
    }

    if (send_limit == NULL || max_allowance == NULL)
    {
        return 0;
    }

    /* 送出上限を使わない受信側は、送出上限を持たない。 */
    if (ctx == NULL || ctx->flow_control == NULL || ctx->is_multi_peer || is_unlimited_mode(ctx->flow_control))
    {
        return 0;
    }

    fc = ctx->flow_control;
    cplat_local_lock_lock(fc->mutex);
    account = &fc->service;

    /* 送信者をまだ採用していない受信側と、容量超過で受信を再開しない受信側は、送出上限を持たない。 */
    if (account->has_next_seq != 0 && account->refused == 0)
    {
        *send_limit = account->next_seq + send_allowance(ctx, fc, account);
        *max_allowance = report_max_allowance(fc, account);
        held = 1;
    }

    cplat_local_lock_unlock(fc->mutex);

    return held;
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_flow_control_monitor_tick(potr_context *ctx, uint64_t now_ms)
{
    potr_internal_packet_session_hdr list[POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX];
    size_t count;
    int changed = 0;
    int retry_due = 0;
    int periodic_due;

    if (ctx == NULL || ctx->role != POTR_ROLE_SENDER ||
        (ctx->service.type != POTR_TYPE_MULTICAST && ctx->service.type != POTR_TYPE_BROADCAST))
    {
        return;
    }

    count = potr_internal_receiver_monitor_list(ctx, now_ms, list, POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX, &changed);

    /* 監視対象が 0 台の間は送出しない。受信者は、一覧の有効期限で監視対象でなくなる。 */
    if (count == 0U)
    {
        ctx->flow_monitor_next_ms = 0U;
        ctx->flow_monitor_periodic_next_ms = 0U;
        ctx->flow_monitor_retry_next_ms = 0U;
        ctx->flow_monitor_retry_interval_ms = 0U;
        return;
    }

    /* 周期送出と未通知中の再送は受信スレッドで管理し、停止中の問い合わせと間隔の計算を共用する。 */
    if (changed != 0)
    {
        ctx->flow_monitor_retry_next_ms = 0U;
        ctx->flow_monitor_retry_interval_ms = 0U;
    }
    if (cplat_atomic_load_i32(&ctx->flow_monitor_unnotified, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
    {
        retry_due = query_timer_due(&ctx->flow_monitor_retry_next_ms, &ctx->flow_monitor_retry_interval_ms,
                                    ctx->flow_control_config.request_interval_ms, now_ms);
    }
    else
    {
        ctx->flow_monitor_retry_next_ms = 0U;
        ctx->flow_monitor_retry_interval_ms = 0U;
    }

    periodic_due = changed != 0 || ctx->flow_monitor_periodic_next_ms == 0U ||
                   now_ms >= ctx->flow_monitor_periodic_next_ms;
    if (periodic_due != 0 || retry_due != 0)
    {
        send_monitor_list(ctx, list, count);
    }
    if (periodic_due != 0)
    {
        ctx->flow_monitor_periodic_next_ms = now_ms + (uint64_t)ctx->flow_control_config.request_interval_ms;
    }
    ctx->flow_monitor_next_ms = earlier_time(ctx->flow_monitor_periodic_next_ms, ctx->flow_monitor_retry_next_ms);
}

/*
 * ソケットから OS の受信バッファーの大きさを取得する。取得できない場合と、値が 0 の場合は POTR_ERR_IO を返す。
 * Linux の取得値は、管理情報の分を含み、受信したパケットを確保済みの領域の大きさで数える。
 * 取得値は cplat_socket_get_receive_buffer_size() が補正せずに返す。
 */
static int get_receive_buffer_size(cplat_socket receive_socket, uint64_t *size)
{
    size_t value = 0U;

    if (cplat_socket_get_receive_buffer_size(receive_socket, &value, NULL) != CPLAT_OK || value == 0U)
    {
        return POTR_ERR_IO;
    }

    *size = (uint64_t)value;

    return POTR_OK;
}

/*
 * 受信に使う全経路のソケットから OS の受信バッファーの大きさを取得し、最も小さい値を返す。
 * いずれかの経路で取得できなかった場合と、有効なソケットがない場合は、警告を記録して POTR_ERR_IO を返す。
 */
static int query_receive_buffer_size(const potr_context *ctx, uint64_t *size)
{
    uint64_t smallest = 0U;
    int found = 0;
    int i;

    for (i = 0; i < ctx->n_path; i++)
    {
        uint64_t path_size = 0U;

        if (ctx->sock[i] == CPLAT_INVALID_SOCKET)
        {
            continue;
        }

        if (get_receive_buffer_size(ctx->sock[i], &path_size) != POTR_OK)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                       "flow_control[service_id=%" PRId64 "]: path=%d cannot get SO_RCVBUF,"
                       " send limit by OS receive buffer is not used",
                       ctx->service.service_id, i);
            return POTR_ERR_IO;
        }

        if (!found || path_size < smallest)
        {
            smallest = path_size;
            found = 1;
        }
    }

    if (!found)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                   "flow_control[service_id=%" PRId64 "]: no socket to get SO_RCVBUF,"
                   " send limit by OS receive buffer is not used",
                   ctx->service.service_id);
        return POTR_ERR_IO;
    }

    *size = smallest;

    return POTR_OK;
}

/*
 * 送出上限を使わない場合 (容量が 0 で、OS の受信バッファーに基づく送出上限も使わない) でも、
 * 状態が「上限なし」のフロー制御パケットを送出する受信側かどうかを返す。
 * multicast と broadcast の受信者は送信者が初期の幅を適用しないため送出せず、RAW 系と片方向送信者も対象外とする。
 */
static int is_unlimited_receiver(const potr_context *ctx)
{
    if (is_raw_type(ctx->service.type) || ctx->service.type == POTR_TYPE_MULTICAST ||
        ctx->service.type == POTR_TYPE_BROADCAST)
    {
        return 0;
    }

    if (ctx->service.type == POTR_TYPE_UNICAST || ctx->service.type == POTR_TYPE_TCP)
    {
        return ctx->role == POTR_ROLE_RECEIVER;
    }

    return ctx->service.type == POTR_TYPE_UNICAST_BIDIR || ctx->service.type == POTR_TYPE_UNICAST_BIDIR_N1 ||
           ctx->service.type == POTR_TYPE_TCP_BIDIR;
}

/* 資源が「上限なし」を知らせるだけの受信側かどうかを返す。capacity と receive_buffer_used は開始後は不変である。 */
static int is_unlimited_mode(const struct potr_internal_flow_control *fc)
{
    return fc->capacity == 0U && fc->receive_buffer_used == 0;
}

/* 送信中のピア数から外す。fc->mutex を保持して呼ぶ。 */
static void clear_sending_active(struct potr_internal_flow_control *fc, flow_account *account)
{
    if (account->sending_active != 0)
    {
        account->sending_active = 0;
        if (fc->active_sender_count > 0U)
        {
            fc->active_sender_count--;
        }
    }
}

/* 相手の占有量の記録を探す。N:1 モードで見つからず create が非 0 の場合は末尾へ作成する (管理表の順を保つため)。fc->mutex を保持して呼ぶ。 */
static flow_account *find_account(const potr_context *ctx, struct potr_internal_flow_control *fc,
                                  potr_peer_id peer_id, int create)
{
    flow_account **link;
    flow_account *account;

    if (!ctx->is_multi_peer)
    {
        return &fc->service;
    }

    link = &fc->accounts;
    while (*link != NULL)
    {
        if ((*link)->peer_id == peer_id)
        {
            return *link;
        }

        link = &(*link)->next;
    }

    if (create == 0)
    {
        return NULL;
    }

    account = (flow_account *)cplat_calloc(1U, sizeof(*account));
    if (account != NULL)
    {
        account->peer_id = peer_id;
        *link = account;
    }

    return account;
}

/* 占有量が 0 になった N:1 モードの相手の記録を解放する。fc->mutex を保持して呼ぶ。 */
static void remove_account(struct potr_internal_flow_control *fc, flow_account *target)
{
    flow_account **link = &fc->accounts;

    while (*link != NULL)
    {
        if (*link == target)
        {
            clear_sending_active(fc, target);
            *link = target->next;
            cplat_free(target);
            return;
        }

        link = &(*link)->next;
    }
}

/*
 * N:1 モード以外の UDP の返送先を、受信スレッドが学習した値から更新する。
 * 学習した値は受信スレッドが所有するため、通知スレッドは fc->reply の写しだけを使う。
 * 受信スレッドから、fc->mutex を保持して呼ぶ。
 */
static void refresh_reply(const potr_context *ctx, struct potr_internal_flow_control *fc)
{
    int i;

    if (ctx->is_multi_peer || is_tcp_type(ctx->service.type))
    {
        return;
    }

    for (i = 0; i < ctx->n_path; i++)
    {
        if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
        {
            fc->reply[i] = ctx->dest_addr[i];
            continue;
        }

        /* 片方向の受信者は、NACK・受信報告と同じ返送先を使う。 */
        endpoint_clear(&fc->reply[i]);
        fc->reply[i].address = ctx->src_addr_resolved[i];
        if (fc->reply[i].address == 0U)
        {
            fc->reply[i].address = ctx->peer_address[i];
        }

        if (ctx->service.src_port != 0U)
        {
            fc->reply[i].port = cplat_hton16(ctx->service.src_port);
        }
        else
        {
            fc->reply[i].port = ctx->peer_port[i];
        }

        if (fc->reply[i].address == 0U || fc->reply[i].port == 0U)
        {
            endpoint_clear(&fc->reply[i]);
        }
    }
}

/*
 * フロー制御パケット (状態と送出上限) を相手へ送出する。fc->mutex を保持して呼ぶ。
 * N:1 モードでは、呼び出し元が peers_mutex も保持していること。
 * いずれかの経路へ送出できた場合は 1、どの経路へも送出できなかった場合は 0 を返す。
 * 送出の失敗は呼び出し元が記録だけに使う。要求は再送周期ごとに再送し、送出上限は次の契機で送出し直し、
 * 解除は送信側の要求の有効期限で補うためである。
 */
static int send_state(potr_context *ctx, struct potr_internal_flow_control *fc, const flow_account *account,
                      uint8_t state, uint32_t send_limit)
{
    potr_packet packet;
    potr_internal_packet_session_hdr shdr;
    uint8_t payload[POTR_FLOW_CONTROL_PAYLOAD_SIZE];
    uint8_t wire[FLOW_CONTROL_WIRE_SIZE];
    potr_internal_tx_session *session = &ctx->tx_session;
    const cplat_ipv4_endpoint *dest = fc->reply;
    int sent_any = 0;
    int i;

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = ctx->session_id;
    session_ts_to_hdr(&ctx->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);

    if (ctx->is_multi_peer)
    {
        potr_internal_peer_context *peer = potr_internal_peer_find_by_id(ctx, account->peer_id);

        /* 切断して解放済みのピアには、宛先と鍵が残っていないため送出しない。 */
        if (peer == NULL)
        {
            return 0;
        }

        shdr.session_id = peer->session_id;
        session_ts_to_hdr(&peer->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);
        session = &peer->tx_session;
        dest = peer->dest_addr;
    }

    if (potr_internal_packet_build_flow_control(&packet, &shdr, payload, state, send_limit) != POTR_OK)
    {
        return 0;
    }

    for (i = 0; i < ctx->n_path; i++)
    {
        size_t wire_len = sizeof(wire);

        if (is_tcp_type(ctx->service.type))
        {
            if (ctx->tcp_conn_fd[i] == CPLAT_INVALID_SOCKET)
            {
                continue;
            }

            /* 書き込めない経路は、受信スレッドが切断を検知する。送出できた経路の有無だけを記録する。 */
            cplat_local_lock_lock(ctx->tcp_send_mutex[i]);
            if (potr_internal_packet_seal_for_context(ctx, session, &packet, wire, &wire_len) == POTR_OK &&
                potr_internal_tcp_send_all_locked(ctx, i, wire, wire_len, NULL, NULL) == POTR_OK)
            {
                sent_any = 1;
            }
            cplat_local_lock_unlock(ctx->tcp_send_mutex[i]);
            continue;
        }

        if (ctx->sock[i] == CPLAT_INVALID_SOCKET || endpoint_is_unset(&dest[i]))
        {
            continue;
        }

        /* 証明書方式でハンドシェイクが完了していない経路は、封緘に失敗するため送出しない。 */
        if (potr_internal_packet_seal_for_context(ctx, session, &packet, wire, &wire_len) == POTR_OK)
        {
            if (potr_internal_udp_sendto(ctx, i, wire, wire_len, &dest[i], NULL) == POTR_OK)
            {
                sent_any = 1;
            }
        }
    }

    return sent_any;
}

/*
 * OS の受信バッファーに基づく全体の幅を返す。
 * 下限 (POTR_FLOW_RECEIVE_BUFFER_WIDTH_MIN) は全体の幅に適用し、N:1 モードのピアごとの幅には適用しない。
 * これにより、順番の許可で同時に許可を持てるピア数 (全体の幅 ÷ POTR_FLOW_TURN_WIDTH) が常に 1 以上になる。
 */
static uint64_t total_buffer_width(const struct potr_internal_flow_control *fc)
{
    uint64_t width = fc->receive_buffer_width;

    if (width < (uint64_t)POTR_FLOW_RECEIVE_BUFFER_WIDTH_MIN)
    {
        width = (uint64_t)POTR_FLOW_RECEIVE_BUFFER_WIDTH_MIN;
    }

    return width;
}

/*
 * N:1 モードでのピアごとの幅 (全体の幅 ÷ 送信中のピア数、切り捨て) を返す。
 * 送信中のピアがない場合は 1 として計算する。下限は適用しない。fc->mutex を保持して呼ぶ。
 */
static uint64_t multi_peer_width(const struct potr_internal_flow_control *fc)
{
    uint64_t divisor = 1U;

    if (fc->active_sender_count > 0U)
    {
        divisor = (uint64_t)fc->active_sender_count;
    }

    return total_buffer_width(fc) / divisor;
}

/* N:1 モードで順番の許可 (ピアごとの幅が POTR_FLOW_TURN_WIDTH 未満) が有効かを返す。fc->mutex を保持して呼ぶ。 */
static int is_turn_mode(const potr_context *ctx, const struct potr_internal_flow_control *fc)
{
    if (!ctx->is_multi_peer || fc->receive_buffer_used == 0)
    {
        return 0;
    }

    return multi_peer_width(fc) < (uint64_t)POTR_FLOW_TURN_WIDTH;
}

/* 相手へ知らせた送出上限が、次に受信する通番より先にある (許可を持つ) かを返す。fc->mutex を保持して呼ぶ。 */
static int account_has_grant(const flow_account *account)
{
    return account->notified != 0 && account->has_next_seq != 0 &&
           (int32_t)(account->notified_limit - account->next_seq) > 0;
}

/* 相手が保持している許可の残りパケット数 (送出上限 − 次に受信する通番) を返す。許可を持たない場合は 0。 */
static uint32_t account_remaining_grant(const flow_account *account)
{
    if (!account_has_grant(account))
    {
        return 0U;
    }

    return account->notified_limit - account->next_seq;
}

/*
 * 順番の許可で、ピアの未使用の許可が有効期限を過ぎて送信側で縮められたとみなせるかを返す。
 * 最後の DATA 受理と、送出上限を進める通知の送出成功の、新しいほうから有効期限を数える。
 * 時刻を取得できなかった場合 (has_now == 0) は安全側として期限切れとみなさない。
 * fc->mutex を保持して呼ぶ。
 */
static int account_grant_expired(const potr_context *ctx, const flow_account *account, uint64_t now_ms, int has_now)
{
    uint64_t lifetime_ms = (uint64_t)ctx->flow_control_config.request_lifetime_ms;
    uint64_t since_ms = account->last_limit_advance_ms;

    if (has_now == 0 || since_ms == 0U || lifetime_ms == 0U)
    {
        return 0;
    }

    if (account->last_data_ms > since_ms)
    {
        since_ms = account->last_data_ms;
    }

    return now_ms >= since_ms + lifetime_ms;
}

/* 順番の許可で合計と補充の計算に使う未使用の許可数を返す。期限切れの許可は 0 とする。fc->mutex を保持して呼ぶ。 */
static uint32_t account_effective_remaining_grant(const potr_context *ctx, const flow_account *account,
                                                  uint64_t now_ms, int has_now)
{
    uint32_t remaining = 0U;

    if (account_grant_expired(ctx, account, now_ms, has_now) == 0)
    {
        remaining = account_remaining_grant(account);
    }
    /* 送出準備と送出完了の間に別の問い合わせが来ても、予約した許可は重ねて配らない。 */
    if (account->reserved != 0 && (int32_t)(account->reserved_limit - account->next_seq) > 0)
    {
        uint32_t reserved = account->reserved_limit - account->next_seq;

        if (reserved > remaining)
        {
            remaining = reserved;
        }
    }
    return remaining;
}

/*
 * 占有量による許可数を返す。容量が 0 の受信側では UINT32_MAX を返す。
 * 占有量が要求のしきい値を超えている場合は 0、それ以外は
 * (要求のしきい値 − 占有量) ÷ 1 パケットの大きさ (切り捨て) + 1 とする。
 * 1 パケットの大きさは、max_payload と「そのセッションで受信した DATA パケット 1 個の展開後の大きさの最大値」の
 * 大きいほうとする。fc->mutex を保持して呼ぶ。
 */
static uint32_t occupancy_allowance(const struct potr_internal_flow_control *fc, const flow_account *account)
{
    uint64_t packet_size;

    if (fc->capacity == 0U)
    {
        return UINT32_MAX;
    }

    if (account->occupancy > fc->suppress_bytes)
    {
        return 0U;
    }

    packet_size = fc->max_payload;
    if (account->max_packet_size > packet_size)
    {
        packet_size = account->max_packet_size;
    }

    /* 要求のしきい値は容量 (uint32_t) 以下、packet_size は 1 以上のため、商は uint32_t に収まる。 */
    return (uint32_t)((fc->suppress_bytes - account->occupancy) / packet_size) + 1U;
}

/*
 * 受信バッファーの幅による許可数を返す。
 * N:1 モード以外では全体の幅 (下限 POTR_FLOW_RECEIVE_BUFFER_WIDTH_MIN 以上) を返す。
 * N:1 モードでは全体の幅を送信中のピア数で割ったピアごとの幅を使う。
 * ピアごとの幅が POTR_FLOW_TURN_WIDTH 未満 (順番の許可) の場合は、許可を持つピアには残りパケット数、
 * それ以外のピア (待っているピアと送信中でないピア) には 0 を返す。
 * ピアごとの幅が POTR_FLOW_TURN_WIDTH 以上 (通常の配分) の場合は、すでに知らせた送出上限を戻さないよう、
 * ピアごとの幅と受けた許可の残りの大きいほうを返す。
 * fc->mutex を保持して呼ぶ (N:1 モードでは peers_mutex も)。
 */
static uint32_t buffer_allowance(const potr_context *ctx, const struct potr_internal_flow_control *fc,
                                 const flow_account *account)
{
    uint64_t width;
    uint32_t remaining;

    if (!ctx->is_multi_peer)
    {
        /* 幅は SO_RCVBUF (int) を 1 パケットの見積もり (2,000 byte 以上) で割った値以下のため、uint32_t に収まる。 */
        return (uint32_t)total_buffer_width(fc);
    }

    remaining = account_remaining_grant(account);
    width = multi_peer_width(fc);

    if (width < (uint64_t)POTR_FLOW_TURN_WIDTH)
    {
        return remaining;
    }

    if (width < (uint64_t)remaining)
    {
        return remaining;
    }

    return (uint32_t)width;
}

/*
 * 許可数を返す。占有量による許可数と、受信バッファーの幅による許可数の小さいほうとする。
 * 容量が 0 の受信側では占有量による許可数を使わない。
 * OS の受信バッファーに基づく送出上限を使わない受信側では、受信バッファーの幅による許可数を使わない。
 * 資源は、どちらかを使う受信側にだけ生成する。fc->mutex を保持して呼ぶ (N:1 モードでは peers_mutex も)。
 */
static uint32_t send_allowance(const potr_context *ctx, const struct potr_internal_flow_control *fc,
                               const flow_account *account)
{
    uint32_t allowance = occupancy_allowance(fc, account);

    if (fc->receive_buffer_used != 0)
    {
        uint32_t width = buffer_allowance(ctx, fc, account);

        if (width < allowance)
        {
            allowance = width;
        }
    }

    return allowance;
}

/*
 * 送出上限を知らせた記録を更新する。許可数が 1 以上 (send_limit > next_seq) の場合は、
 * 順番の許可の公平性判定に使う許可順序番号 (last_grant_seq) も更新する。fc->mutex を保持して呼ぶ。
 */
static void record_notified(struct potr_internal_flow_control *fc, flow_account *account, uint32_t send_limit)
{
    int advanced = account->notified == 0 || (int32_t)(send_limit - account->notified_limit) > 0;

    account->notified = 1;
    account->notified_base = account->next_seq;
    account->notified_limit = send_limit;
    if (advanced != 0)
    {
        uint64_t now_ms = 0U;

        /* 時計の取得に失敗した新しい許可は、時刻未記録として期限切れにしない。 */
        account->last_limit_advance_ms = 0U;
        if (cplat_clock_get_monotonic_ms(&now_ms) == CPLAT_OK)
        {
            account->last_limit_advance_ms = now_ms;
        }
    }
    if (advanced != 0 && (int32_t)(send_limit - account->next_seq) > 0)
    {
        fc->grant_seq_counter++;
        account->last_grant_seq = fc->grant_seq_counter;
    }
}

/* 現在の状態の値を返す。要求中なら抑制の要求、それ以外は解除とする。 */
static uint8_t current_state(const flow_account *account)
{
    if (account->requesting != 0)
    {
        return (uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS;
    }

    return (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME;
}

/*
 * 送信中のピアとして記録する。直近の request_interval_ms に DATA または問い合わせを受けた時刻を更新し、
 * 送信中でなかった場合は active_sender_count を 1 増やして 1 を返す。すでに送信中だった場合は 0 を返す。
 * fc->mutex を保持して呼ぶ。
 */
static int mark_sending_active(struct potr_internal_flow_control *fc, flow_account *account, uint64_t now_ms)
{
    account->last_active_ms = now_ms;
    if (account->sending_active == 0)
    {
        account->sending_active = 1;
        fc->active_sender_count++;
        return 1;
    }

    return 0;
}

/*
 * 順番の許可で 1 回に割り当てる許可数を返す。
 * 容量を設定している場合は POTR_FLOW_TURN_WIDTH (4) と占有量による許可数の小さいほうとする。
 * fc->mutex を保持して呼ぶ。
 */
static uint32_t turn_grant_allowance(const struct potr_internal_flow_control *fc, const flow_account *account)
{
    uint32_t grant = (uint32_t)POTR_FLOW_TURN_WIDTH;
    uint32_t occ = occupancy_allowance(fc, account);

    if (occ < grant)
    {
        grant = occ;
    }

    return grant;
}

/*
 * 順番の許可では、活動の有無によらず、全ピアの未使用の許可の合計で全体の幅を守る。
 * 最後の DATA 受理と、送出上限を進める通知の送出成功の、新しいほうから有効期限以上経過した許可は数えない。
 * 待機中または半分以上使ったピアへ、最後の許可から最も長く待った順に補充する。
 * 1 回の許可は POTR_FLOW_TURN_WIDTH までで、残りの枠が少なければその分だけ出す。
 * 通常配分からの切り替え直後に旧許可が幅を超えている間は、新しい許可を出さない。
 * target_peer_id に許可を送出できた場合は 1、それ以外は 0 を返す。
 * fc->mutex と peers_mutex を保持して呼ぶ。
 */
static int dispatch_turn_grants(potr_context *ctx, struct potr_internal_flow_control *fc, potr_peer_id target_peer_id,
                                uint64_t now_ms, int has_now)
{
    uint64_t width;
    uint64_t used = 0U;
    flow_account *account;
    int granted_target = 0;

    fc->turn_grant_retry = 0;

    if (!is_turn_mode(ctx, fc))
    {
        return 0;
    }

    width = total_buffer_width(fc);

    for (account = fc->accounts; account != NULL; account = account->next)
    {
        used += (uint64_t)account_effective_remaining_grant(ctx, account, now_ms, has_now);
    }

    if (used >= width)
    {
        return 0;
    }

    fc->grant_try_epoch++;

    while (used < width)
    {
        flow_account *best = NULL;
        uint32_t grant;
        uint32_t remaining;
        uint32_t send_limit;
        int expired;

        for (account = fc->accounts; account != NULL; account = account->next)
        {
            remaining = account_effective_remaining_grant(ctx, account, now_ms, has_now);

            if (account->sending_active == 0 || account->has_next_seq == 0 || account->refused != 0 ||
                account->requesting != 0 || remaining > (uint32_t)POTR_FLOW_TURN_REFILL_CANDIDATE_REMAINING ||
                account->last_try_epoch == fc->grant_try_epoch || turn_grant_allowance(fc, account) <= remaining)
            {
                continue;
            }

            if (best == NULL || account->last_grant_seq < best->last_grant_seq)
            {
                best = account;
            }
        }

        if (best == NULL)
        {
            break;
        }

        best->last_try_epoch = fc->grant_try_epoch;
        expired = account_grant_expired(ctx, best, now_ms, has_now);
        remaining = (expired != 0) ? 0U : account_remaining_grant(best);
        grant = turn_grant_allowance(fc, best) - remaining;

        if ((uint64_t)grant > width - used)
        {
            grant = (uint32_t)(width - used);
        }

        send_limit = best->next_seq + remaining + grant;

        /* 同値以下では新しい許可を渡さず、期限も延ばさない。既存の初期の幅だけを持つピアは飛ばす。 */
        if (expired != 0 && best->notified != 0 && (int32_t)(send_limit - best->notified_limit) <= 0)
        {
            continue;
        }

        if (send_state(ctx, fc, best, current_state(best), send_limit) != 0)
        {
            record_notified(fc, best, send_limit);
            used += (uint64_t)grant;

            if (target_peer_id != POTR_PEER_NA && best->peer_id == target_peer_id)
            {
                granted_target = 1;
            }
        }
        else
        {
            fc->turn_grant_retry = 1;
        }
    }

    if (used >= width)
    {
        fc->turn_grant_retry = 0;
    }

    return granted_target;
}

/*
 * 送出上限を知らせてよいかを返す。multicast と broadcast の受信者は、送信者が選んだ監視対象である間だけ、
 * 最初の送出と送出上限の更新を行う。受信者の全員が知らせると、送信者が受けるフロー制御パケットの数が
 * 受信者の台数に比例するためである。fc->mutex を保持して呼ぶ。
 */
static int may_notify_limit(const struct potr_internal_flow_control *fc)
{
    return fc->one_to_many_receiver == 0 || fc->monitored != 0;
}

/*
 * 占有量が解除のしきい値を下回っているかを返す。容量が 0 の受信側は、占有量の条件を常に満たすものとして扱う。
 * fc->mutex を保持して呼ぶ。
 */
static int below_resume(const struct potr_internal_flow_control *fc, const flow_account *account)
{
    return fc->capacity == 0U || account->occupancy < fc->resume_bytes;
}

/*
 * 状態と送出上限 (次に受信する通番 + 許可数) を送出する。fc->mutex を保持して呼ぶ (N:1 モードでは peers_mutex も)。
 * 送出できた場合は、知らせた範囲を記録する。送出できなかった場合は記録を変えず、次の契機で送出し直す。
 * multicast と broadcast の受信者で監視対象でない間は、抑制の要求と解除にも送出上限を載せるが、
 * 送信者はその送出上限を使わないため、知らせたものとして記録しない。
 */
static void notify_state(potr_context *ctx, struct potr_internal_flow_control *fc, flow_account *account,
                         uint8_t state)
{
    uint32_t send_limit = account->next_seq + send_allowance(ctx, fc, account);

    if (send_state(ctx, fc, account, state, send_limit) != 0 && may_notify_limit(fc))
    {
        record_notified(fc, account, send_limit);
    }
}

/*
 * 送出上限を使わない受信側が、「上限なし」(送出上限の欄は 0) を 1 回送出する。
 * fc->mutex を保持して呼ぶ (N:1 モードでは peers_mutex も)。
 * 送出できた場合は知らせたことを記録し、できなかった場合は次の契機で送出し直す。
 */
static void notify_unlimited(potr_context *ctx, struct potr_internal_flow_control *fc, flow_account *account)
{
    if (send_state(ctx, fc, account, (uint8_t)POTR_FLOW_CONTROL_STATE_UNLIMITED, 0U) != 0)
    {
        account->notified = 1;
        account->notified_base = account->next_seq;
        account->notified_limit = 0U;
    }
}

/*
 * 相手のセッションを記録する。セッションが変わった場合は新しい通信として、送出上限をまだ知らせていない状態に戻す。
 * multicast と broadcast の受信者は、送信者のセッションが変わった場合、監視対象の一覧を受け取っていない状態に戻す。
 * fc->mutex を保持して呼ぶ。
 */
static void adopt_session(potr_context *ctx, struct potr_internal_flow_control *fc, flow_account *account,
                          uint32_t session_id, const cplat_timespec *session_ts)
{
    if (account->session_id != session_id || account->session_ts.tv_sec != session_ts->tv_sec ||
        account->session_ts.tv_nsec != session_ts->tv_nsec)
    {
        clear_sending_active(fc, account);
        account->notified = 0;
        account->has_next_seq = 0;
        account->last_data_ms = 0U;
        account->last_limit_advance_ms = 0U;
        account->reserved = 0;
        account->last_active_ms = 0U;
        account->last_grant_seq = 0U;
        account->max_packet_size = 0U;
        fc->monitored = 0;
        ctx->flow_monitored = 0;
    }

    account->session_id = session_id;
    account->session_ts = *session_ts;
}

/* 抑制の要求を次に再送する時刻を返す。一対多の受信者は、再送の間隔を 100% から 150% の範囲でばらつかせる。 */
static uint64_t next_request_time(const potr_context *ctx, uint64_t now_ms)
{
    uint64_t interval_ms = ctx->flow_control_config.request_interval_ms;

    if (ctx->service.type == POTR_TYPE_MULTICAST || ctx->service.type == POTR_TYPE_BROADCAST)
    {
        uint32_t random_value = 0U;

        /* 乱数を取得できない場合は、ばらつきなしの再送周期を使う。 */
        if (cplat_random_bytes(&random_value, sizeof(random_value)) == CPLAT_OK)
        {
            interval_ms += (uint64_t)random_value % (interval_ms / 2U + 1U);
        }
    }

    return now_ms + interval_ms;
}

/* 要求なしから要求中へ遷移し、抑制の要求を送出する。fc->mutex を保持して呼ぶ (N:1 モードでは peers_mutex も)。 */
static void start_request(potr_context *ctx, struct potr_internal_flow_control *fc, flow_account *account,
                          uint64_t now_ms)
{
    account->requesting = 1;
    account->next_request_ms = next_request_time(ctx, now_ms);
    notify_state(ctx, fc, account, (uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS);
}

/*
 * 順序どおりに受信した DATA パケットを 1 個処理した後の判定を行う。要求なしの状態で、送出上限を知らせた後に呼ぶ。
 * fc->mutex を保持して呼ぶ。
 * 相手が送出上限まで送り終えた場合は、占有量が解除のしきい値以上なら抑制の要求へ遷移し、下回っていれば更新を送出する。
 * 相手が上限で止まっている間に、受信側が何も送出しない状態を作らないためである。
 * 送り終えていない場合は、前回知らせた範囲の半分以上を受信し、占有量が解除のしきい値を下回っていれば更新を送出する。
 * 容量が 0 の受信側は、占有量の条件を常に満たすものとして扱い、抑制の要求へ遷移しない。
 */
static void judge_progress(potr_context *ctx, struct potr_internal_flow_control *fc, flow_account *account)
{
    uint64_t received = (uint64_t)(uint32_t)(account->next_seq - account->notified_base);
    uint64_t range = (uint64_t)(uint32_t)(account->notified_limit - account->notified_base);

    /* 通番は循環するため、差を符号付きで見て、送出上限に達したかを判定する。 */
    if ((int32_t)(account->next_seq - account->notified_limit) >= 0)
    {
        if (!below_resume(fc, account))
        {
            uint64_t now_ms = 0U;

            /*
             * 時刻は要求の再送の予定にだけ使い、失敗時は now_ms が 0 のままで次の周期処理の再送が早まるだけのため、
             * 戻り値は明示的に破棄する。
             */
            (void)cplat_clock_get_monotonic_ms(&now_ms);

            start_request(ctx, fc, account, now_ms);
            POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                       "flow_control[service_id=%" PRId64 "]: peer=%u requested suppression at send limit"
                       " (occupancy=%" PRIu64 ")",
                       ctx->service.service_id, (unsigned)account->peer_id, account->occupancy);
        }
        else
        {
            notify_state(ctx, fc, account, (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME);
        }

        return;
    }

    if (below_resume(fc, account) && received * 2U >= range)
    {
        /*
         * N:1 モードの通常の配分では、送信中のピア数が増えてピアごとの幅が小さくなった場合でも
         * すでに知らせた送出上限を戻さない。新しい送出上限が前回と同じ間は重複通知を送出しない。
         */
        if (ctx->is_multi_peer && fc->receive_buffer_used != 0)
        {
            uint32_t candidate = account->next_seq + send_allowance(ctx, fc, account);

            if ((int32_t)(candidate - account->notified_limit) <= 0)
            {
                return;
            }
        }

        notify_state(ctx, fc, account, (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME);
    }
}

/* 問い合わせおよび送信途絶後の許可を適用する UDP の通信種別 (unicast、unicast_bidir、unicast_bidir_n1) かを返す。 */
static int uses_query_udp(potr_type type)
{
    return type == POTR_TYPE_UNICAST || type == POTR_TYPE_UNICAST_BIDIR || type == POTR_TYPE_UNICAST_BIDIR_N1;
}

/*
 * 周期処理で、1 つの相手について次を行う。fc->mutex を保持して呼ぶ (N:1 モードでは peers_mutex も)。
 * - 再送周期が経過した抑制の要求を再送する。
 * - まだ知らせていない送出上限を送出し直す (証明書方式のハンドシェイクの完了待ちなどで送出できなかった場合)。
 */
static void tick_account(potr_context *ctx, struct potr_internal_flow_control *fc, flow_account *account,
                         uint64_t now_ms)
{
    if (account->requesting != 0 && now_ms >= account->next_request_ms)
    {
        account->next_request_ms = next_request_time(ctx, now_ms);
        notify_state(ctx, fc, account, (uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS);
        return;
    }

    if (account->notified == 0 && account->has_next_seq != 0 && account->refused == 0 && may_notify_limit(fc))
    {
        if (is_unlimited_mode(fc))
        {
            notify_unlimited(ctx, fc, account);
        }
        else
        {
            notify_state(ctx, fc, account, current_state(account));
        }
    }
}

/*
 * multicast と broadcast の受信者が、採用している送信者から受けた監視対象の一覧を反映する。
 * 一覧に自身のセッション識別子が含まれる間は監視対象として動作し、送出上限をまだ知らせていない状態から始める。
 * 含まれない一覧を受けた場合は、監視対象でないものとして、送出上限を知らせていない状態に戻す。
 * 採用している送信者 (ctx->peer_session_*) のセッションと一致しない一覧は破棄する。
 */
static void on_monitor_list(potr_context *ctx, const potr_packet *pkt)
{
    struct potr_internal_flow_control *fc = ctx->flow_control;
    flow_account *account;
    potr_internal_packet_session_hdr own;
    cplat_timespec sender_ts;
    uint64_t now_ms;
    size_t count = 0U;
    size_t i;
    int included = 0;

    /* その他の通信種別と、送信者が受信した一覧は破棄する。フロー制御パケットを送出しない受信者も使わない。 */
    if (fc == NULL || fc->one_to_many_receiver == 0 || flow_control_monitor_decode(pkt, &count) != POTR_OK)
    {
        return;
    }

    /* 採用している送信者のセッション識別子と一致しない一覧は破棄する。 */
    session_ts_from_hdr(pkt->session_tv_sec, pkt->session_tv_nsec, &sender_ts);
    if (ctx->peer_session_known == 0 || pkt->session_id != ctx->peer_session_id ||
        sender_ts.tv_sec != ctx->peer_session_ts.tv_sec || sender_ts.tv_nsec != ctx->peer_session_ts.tv_nsec)
    {
        return;
    }

    /* 時刻を取得できない場合は、一覧の期限を決められないため、この一覧を反映しない。送信者は周期ごとに送出する。 */
    if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
    {
        return;
    }

    own.service_id = ctx->service.service_id;
    session_ts_to_hdr(&ctx->session_ts, &own.session_tv_sec, &own.session_tv_nsec);
    own.session_id = ctx->session_id;

    for (i = 0U; i < count && included == 0; i++)
    {
        potr_internal_packet_session_hdr entry = own;

        flow_control_monitor_entry(pkt, i, &entry);
        if (entry.session_id == own.session_id && entry.session_tv_sec == own.session_tv_sec &&
            entry.session_tv_nsec == own.session_tv_nsec)
        {
            included = 1;
        }
    }

    cplat_local_lock_lock(fc->mutex);
    refresh_reply(ctx, fc);
    account = &fc->service;

    /* 送信者のセッションが変わった場合は、一覧を受け取っていない状態から反映する。 */
    adopt_session(ctx, fc, account, pkt->session_id, &sender_ts);
    fc->monitor_received_ms = now_ms;

    if (included != 0)
    {
        if (fc->monitored == 0)
        {
            fc->monitored = 1;
            ctx->flow_monitored = 1;
            account->notified = 0;
            POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "flow_control[service_id=%" PRId64 "]: selected as monitored receiver",
                       ctx->service.service_id);
        }

        /*
         * 自身を含む一覧を受けるたびに、その時点の状態と送出上限を 1 回送出する。
         * 次に受信する通番がまだ分からない場合は、次の DATA か PING の受理で行う。
         */
        if (account->has_next_seq != 0 && account->refused == 0)
        {
            notify_state(ctx, fc, account, current_state(account));
        }
    }
    else if (fc->monitored != 0)
    {
        fc->monitored = 0;
        ctx->flow_monitored = 0;
        account->notified = 0;
        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "flow_control[service_id=%" PRId64 "]: no longer monitored receiver",
                   ctx->service.service_id);
    }

    cplat_local_lock_unlock(fc->mutex);
}

/*
 * 採用している相手のセッションから受けた問い合わせに対し、その時点の状態と送出上限を 1 回送出する。
 * 抑制の要求中なら抑制の要求、そうでなければ解除、送出上限を使わない受信側なら上限なし (送出上限の欄は 0) とする。
 * multicast と broadcast の受信者と、採用していないセッションからの問い合わせは破棄する。
 */
static void on_query(potr_context *ctx, struct potr_internal_peer_context *peer, const potr_packet *pkt)
{
    struct potr_internal_flow_control *fc = ctx->flow_control;
    flow_account *account;
    cplat_timespec sender_ts;
    potr_peer_id peer_id;

    if (fc == NULL || fc->one_to_many_receiver != 0)
    {
        return;
    }

    session_ts_from_hdr(pkt->session_tv_sec, pkt->session_tv_nsec, &sender_ts);

    if (ctx->is_multi_peer)
    {
        if (peer == NULL || peer->peer_session_known == 0 || pkt->session_id != peer->peer_session_id ||
            sender_ts.tv_sec != peer->peer_session_ts.tv_sec ||
            sender_ts.tv_nsec != peer->peer_session_ts.tv_nsec)
        {
            return;
        }

        peer_id = peer->peer_id;
    }
    else
    {
        if (peer != NULL || ctx->peer_session_known == 0 || pkt->session_id != ctx->peer_session_id ||
            sender_ts.tv_sec != ctx->peer_session_ts.tv_sec ||
            sender_ts.tv_nsec != ctx->peer_session_ts.tv_nsec)
        {
            return;
        }

        peer_id = POTR_PEER_NA;
    }

    cplat_local_lock_lock(fc->mutex);
    refresh_reply(ctx, fc);
    account = find_account(ctx, fc, peer_id, 1);
    if (account != NULL)
    {
        adopt_session(ctx, fc, account, pkt->session_id, &sender_ts);
        if (is_unlimited_mode(fc))
        {
            notify_unlimited(ctx, fc, account);
        }
        else if (account->has_next_seq != 0 && account->refused == 0)
        {
            int granted_self = 0;

            if (ctx->is_multi_peer && fc->receive_buffer_used != 0)
            {
                uint64_t now_ms = 0U;

                if (cplat_clock_get_monotonic_ms(&now_ms) == CPLAT_OK)
                {
                    (void)mark_sending_active(fc, account, now_ms);

                    if (is_turn_mode(ctx, fc))
                    {
                        granted_self = dispatch_turn_grants(ctx, fc, account->peer_id, now_ms, 1);
                    }
                }
            }

            if (granted_self == 0)
            {
                notify_state(ctx, fc, account, current_state(account));
            }
        }
    }

    cplat_local_lock_unlock(fc->mutex);
}

/* 残り時間 [ms] を、確認し直すまでの待機の上限に丸める。 */
static int recheck_wait_ms(uint64_t now_ms, uint64_t until_ms)
{
    if (until_ms > now_ms && until_ms - now_ms < (uint64_t)POTR_INTERNAL_FLOW_RECHECK_MS)
    {
        /* 差は POTR_INTERNAL_FLOW_RECHECK_MS 未満であり、int に収まる。 */
        return (int)(until_ms - now_ms);
    }

    return POTR_INTERNAL_FLOW_RECHECK_MS;
}

/*
 * 次に割り当てる DATA の通番を読む。send_window は送信スレッドと接続スレッド (TCP の全経路の切断時の初期化) が
 * 更新するため、send_window_mutex の下で読む。send_window_mutex は送信スレッドの起動時に生成し、
 * 生成前は並行して更新するスレッドがないため、NULL の場合はそのまま読む。
 */
static uint32_t read_next_seq(const potr_internal_window *window, cplat_local_lock *window_mutex)
{
    uint32_t next_seq;

    if (window_mutex == NULL)
    {
        return window->next_seq;
    }

    cplat_local_lock_lock(window_mutex);
    next_seq = window->next_seq;
    cplat_local_lock_unlock(window_mutex);

    return next_seq;
}

/* 早い方の時刻を返す。0 は「時刻なし」として扱う。 */
static uint64_t earlier_time(uint64_t current_ms, uint64_t candidate_ms)
{
    if (current_ms == 0U || (candidate_ms != 0U && candidate_ms < current_ms))
    {
        return candidate_ms;
    }

    return current_ms;
}

/* 2 つの時刻のうち新しいほうを返す。 */
static uint64_t later_time(uint64_t first_ms, uint64_t second_ms)
{
    if (second_ms > first_ms)
    {
        return second_ms;
    }

    return first_ms;
}

/*
 * 監視対象の一覧のパケットを組み立て、DATA と同じ宛先へ全経路から送出する。
 */
static void send_monitor_list(potr_context *ctx, const potr_internal_packet_session_hdr *list, size_t count)
{
    potr_internal_packet_session_hdr shdr;
    uint8_t payload[POTR_FLOW_CONTROL_MONITOR_PAYLOAD_MAX];
    potr_packet packet;
    int i;

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = ctx->session_id;
    session_ts_to_hdr(&ctx->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);

    if (potr_internal_packet_build_flow_control_list(&packet, &shdr, payload, list, count) != POTR_OK)
    {
        return;
    }

    /* 宛先は DATA と同じ (マルチキャスト グループまたはブロードキャスト) とし、全経路へ送出する。 */
    for (i = 0; i < ctx->n_path; i++)
    {
        uint8_t wire[FLOW_CONTROL_MONITOR_WIRE_SIZE];
        size_t wire_len = sizeof(wire);

        if (ctx->sock[i] == CPLAT_INVALID_SOCKET || endpoint_is_unset(&ctx->dest_addr[i]))
        {
            continue;
        }

        /* 証明書方式でハンドシェイクが完了していない間は、封緘に失敗するため送出しない。次の周期で送出し直す。 */
        if (potr_internal_packet_seal_for_context(ctx, &ctx->tx_session, &packet, wire, &wire_len) != POTR_OK)
        {
            continue;
        }

        /* 一覧は周期ごとに送出し直し、受信者は期限内の最新の一覧で動作するため、戻り値は明示的に破棄する。 */
        (void)potr_internal_udp_sendto(ctx, i, wire, wire_len, &ctx->dest_addr[i], NULL);
    }
}

/*
 * multicast と broadcast の送信者が監視対象の送出上限で止まっている間に、最新の監視対象の一覧を 1 回送出し直す。
 * 受信スレッドが管理する周期送出の時刻 (flow_monitor_next_ms) は変更しない。
 */
static void send_monitor_list_on_stall(potr_context *ctx, uint64_t now_ms)
{
    potr_internal_packet_session_hdr list[POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX];
    size_t count;
    int changed = 0;

    count = potr_internal_receiver_monitor_list(ctx, now_ms, list, POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX, &changed);
    if (count == 0U)
    {
        return;
    }

    send_monitor_list(ctx, list, count);
}

/*
 * 問い合わせまたは一覧の再送時刻を進め、送出する期限に達した場合は 1 を返す。
 * 初回は 10 ms 後、以後は 2 倍ずつ延ばし、指定された再送周期を上限とする。
 */
static int query_timer_due(uint64_t *next_ms, uint64_t *interval_ms, uint64_t max_interval_ms, uint64_t now_ms)
{
    uint64_t initial_ms = POTR_FLOW_QUERY_INITIAL_INTERVAL_MS;
    uint64_t next_interval;

    if (max_interval_ms == 0U)
    {
        max_interval_ms = POTR_DEFAULT_FLOW_CONTROL_REQUEST_INTERVAL_MS;
    }
    if (initial_ms > max_interval_ms)
    {
        initial_ms = max_interval_ms;
    }
    if (*next_ms == 0U)
    {
        *interval_ms = initial_ms;
        *next_ms = now_ms + initial_ms;
        return 0;
    }
    if (now_ms < *next_ms)
    {
        return 0;
    }

    next_interval = initial_ms;
    if (*interval_ms > 0U)
    {
        next_interval = *interval_ms * POTR_FLOW_QUERY_BACKOFF_MULTIPLIER;
    }
    if (next_interval > max_interval_ms)
    {
        next_interval = max_interval_ms;
    }
    *interval_ms = next_interval;
    *next_ms = now_ms + next_interval;
    return 1;
}

/* 停止中の問い合わせまたは一覧の送出し直しを進め、次の時刻 [ms] を返す。 */
static uint64_t advance_query_timer(potr_context *ctx, struct potr_internal_peer_context *peer, uint64_t now_ms)
{
    uint64_t *next_ms = &ctx->flow_query_next_ms;
    uint64_t *interval_ms = &ctx->flow_query_interval_ms;

    if (peer != NULL)
    {
        next_ms = &peer->flow_query_next_ms;
        interval_ms = &peer->flow_query_interval_ms;
    }
    if (query_timer_due(next_ms, interval_ms, ctx->flow_control_config.request_interval_ms, now_ms))
    {
        if (ctx->service.type == POTR_TYPE_MULTICAST || ctx->service.type == POTR_TYPE_BROADCAST)
        {
            send_monitor_list_on_stall(ctx, now_ms);
        }
        else
        {
            potr_internal_flow_control_send_query(ctx, peer);
        }
    }

    return *next_ms;
}

/*
 * 「相手なし」への書き換え後に受信時刻を再確認する。flow_limit_mutex を保持して呼ぶ。
 * 非通知パケットの時刻更新はこのロックを取らないため、古い時刻で遷移した場合は元の値へ戻す。
 * 受信側が先に「相手なし」を見た場合も、復帰側は同じロックの下で比較交換して状態を再確認する。
 */
static int finish_limit_expiry(potr_context *ctx, uint64_t send_limit, uint64_t received_ms, uint64_t last_packet_ms)
{
    if (cplat_atomic_load_u64(&ctx->flow_last_packet_ms, CPLAT_MEMORY_ORDER_SEQ_CST) != last_packet_ms ||
        cplat_atomic_load_u64(&ctx->flow_limit_received_ms, CPLAT_MEMORY_ORDER_ACQUIRE) != received_ms)
    {
        uint64_t expected = POTR_FLOW_SEND_LIMIT_NO_PEER;

        /* 新しいセッションの未通知や通知済みの上限は、古い値で上書きしない。 */
        (void)cplat_atomic_compare_exchange_u64(&ctx->flow_send_limit, &expected, send_limit,
                                                CPLAT_MEMORY_ORDER_SEQ_CST);
        return 0;
    }
    return 1;
}

/*
 * 通知の受理と期限切れ遷移を flow_limit_mutex で直列化する。
 * ロックを待つ間の通知は時刻の読み直しで検出し、変化があれば制限を外さない。
 * このロックの内側では、ほかのロックを取得しない (ロック順序の末端)。
 */
static int expire_limit_if_unchanged(potr_context *ctx, uint64_t send_limit, uint64_t received_ms,
                                     uint64_t last_packet_ms)
{
    uint64_t expected = send_limit;
    int expired = 0;

    cplat_local_lock_lock(ctx->flow_limit_mutex);
    if (cplat_atomic_load_u64(&ctx->flow_limit_received_ms, CPLAT_MEMORY_ORDER_ACQUIRE) != received_ms ||
        cplat_atomic_load_u64(&ctx->flow_last_packet_ms, CPLAT_MEMORY_ORDER_ACQUIRE) != last_packet_ms)
    {
        cplat_local_lock_unlock(ctx->flow_limit_mutex);
        return 0;
    }

    expired = cplat_atomic_compare_exchange_u64(&ctx->flow_send_limit, &expected, POTR_FLOW_SEND_LIMIT_NO_PEER,
                                                CPLAT_MEMORY_ORDER_SEQ_CST);
    if (expired != 0)
    {
        expired = finish_limit_expiry(ctx, send_limit, received_ms, last_packet_ms);
    }
    cplat_local_lock_unlock(ctx->flow_limit_mutex);
    return expired;
}

/*
 * N:1 モードのピア 1 件について、「止める状態」(有効な抑制の要求がある、または送出上限に達している) かを判定する。
 * 呼び出し元が peers_mutex を保持していること。
 * allow_query が非 0 の場合 (送信要求や送信スレッドからの呼び出し) は、送出上限に達している間に問い合わせを進める。
 * 周期処理の積算判定など、送出する DATA がないピアを含めて判定する場合は allow_query を 0 とする。
 * allowance_out が NULL でない場合は、送出上限に達するまでに送出できる DATA のパケット数を格納する
 * (送出上限を保持していない場合と、止める状態の場合は UINT32_MAX)。
 */
static int peer_state_locked(potr_context *ctx, potr_internal_peer_context *peer, uint64_t now_ms, int allow_query,
                             uint32_t *allowance_out)
{
    uint64_t limit_until_ms = 0U;
    uint32_t allowance = 0U;
    uint32_t next_seq = read_next_seq(&peer->send_window, peer->send_window_mutex);
    uint64_t activity_ms = later_time(peer->flow_limit_received_ms, peer->flow_last_packet_ms);
    potr_internal_send_limit_state limit_state;
    int suppressed = 0;

    uint64_t idle_since_ms = peer->flow_last_data_send_ms;

    /* DATA 未送出では最初の上限の受理を起点とし、後続通知では延長しない。 */
    if (idle_since_ms == 0U)
    {
        idle_since_ms = peer->flow_first_limit_ms;
    }
    if (idle_since_ms != 0U &&
        now_ms >= idle_since_ms + (uint64_t)ctx->flow_control_config.request_lifetime_ms)
    {
        peer->flow_send_limit = flow_send_limit_clamp_initial(peer->flow_send_limit, next_seq);
    }

    if ((peer->flow_send_limit & POTR_FLOW_SEND_LIMIT_STATE_MASK) == POTR_FLOW_SEND_LIMIT_UNNOTIFIED)
    {
        activity_ms = 0U;
    }

    limit_state = flow_send_limit_judge(peer->flow_send_limit, activity_ms, &peer->flow_limit_stalled_ms, next_seq,
                                        now_ms, ctx->flow_control_config.request_lifetime_ms, &limit_until_ms,
                                        &allowance);

    /* 有効期限の間、相手から何も届かなかった場合は「相手なし」へ遷移する。 */
    if (limit_state == POTR_SEND_LIMIT_EXPIRED)
    {
        peer->flow_send_limit = POTR_FLOW_SEND_LIMIT_NO_PEER;
        peer->flow_limit_stalled_ms = 0U;
        peer->flow_query_next_ms = 0U;
        peer->flow_query_interval_ms = 0U;
    }
    else if (limit_state != POTR_SEND_LIMIT_REACHED)
    {
        peer->flow_query_next_ms = 0U;
        peer->flow_query_interval_ms = 0U;
    }

    /*
     * 期限が経過した抑制の要求は、要求なし (0) へ戻す。判定は期限との比較だけで行うため結果は変わらず、
     * 要求と送出上限をどちらも持たないピアを、ピアの数 (flow_tracked_peers) から外せるようにするためである。
     */
    if (peer->flow_request_expire_ms != 0U && peer->flow_request_expire_ms <= now_ms)
    {
        peer->flow_request_expire_ms = 0U;
    }

    if (peer->flow_request_expire_ms != 0U || limit_state == POTR_SEND_LIMIT_REACHED)
    {
        /* 相手を切断と判定した場合は、その相手の要求と送出上限を取り消す。 */
        if (cplat_atomic_load_i32(&peer->health_alive, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
        {
            peer->flow_request_expire_ms = 0U;
            peer->flow_send_limit = POTR_FLOW_SEND_LIMIT_NO_PEER;
            peer->flow_limit_stalled_ms = 0U;
            peer->flow_query_next_ms = 0U;
            peer->flow_query_interval_ms = 0U;
        }
        else
        {
            suppressed = 1;
            if (allow_query != 0 && limit_state == POTR_SEND_LIMIT_REACHED)
            {
                /* 次の問い合わせ時刻の戻り値は、N:1 の送信スレッドと送信 API が短い間隔で再確認するため使わない。 */
                (void)advance_query_timer(ctx, peer, now_ms);
            }
        }
    }

    potr_internal_flow_control_peer_sync(ctx, peer);

    if (allowance_out != NULL)
    {
        *allowance_out = (suppressed != 0) ? UINT32_MAX : allowance;
    }

    return suppressed;
}

/* 遅いピアとして切断したセッションを受け入れない期間 [ms] を返す。 */
static uint64_t slow_peer_reject_duration_ms(const potr_context *ctx)
{
    uint32_t grace_ms = ctx->flow_control_config.grace_ms;

    if (grace_ms == 0U)
    {
        grace_ms = (uint32_t)POTR_DEFAULT_FLOW_CONTROL_GRACE_MS;
    }

    return (uint64_t)grace_ms * (uint64_t)POTR_FLOW_UNEXCLUDE_GRACE_MULTIPLIER;
}

/* 受け入れ拒否記録が期限切れか判定する。 */
static int slow_peer_reject_expired(const potr_internal_slow_peer_reject_entry *entry, uint64_t now_ms,
                                    uint64_t duration_ms)
{
    return now_ms >= entry->disconnected_ms && now_ms - entry->disconnected_ms >= duration_ms;
}

/* 受け入れ拒否記録の 1 エントリを消去し、有効件数を減算する。 */
static void slow_peer_reject_clear(potr_context *ctx, potr_internal_slow_peer_reject_entry *entry)
{
    memset(entry, 0, sizeof(*entry));
    if (ctx->flow_slow_rejected_count > 0U)
    {
        ctx->flow_slow_rejected_count--;
    }
}

/*
 * 占有量が 0 のときの、占有量による許可数 (容量から決まる最大の許可数) を返す。
 * 容量が 0 の受信側では UINT32_MAX を返す。
 * 1 パケットの大きさは、occupancy_allowance() と同じく max_payload と展開後の大きさの最大値の大きいほうを使う。
 * fc->mutex を保持して呼ぶ。
 */
static uint32_t max_occupancy_allowance(const struct potr_internal_flow_control *fc, const flow_account *account)
{
    uint64_t packet_size;

    if (fc->capacity == 0U)
    {
        return UINT32_MAX;
    }

    packet_size = fc->max_payload;
    if (account->max_packet_size > packet_size)
    {
        packet_size = account->max_packet_size;
    }

    /* 要求のしきい値は容量 (uint32_t) 以下、packet_size は 1 以上のため、商は uint32_t に収まる。 */
    return (uint32_t)(fc->suppress_bytes / packet_size) + 1U;
}

/*
 * 受信報告の seq_num に載せる許可数の上限を返す。
 * 受信バッファーの幅 (OS の受信バッファーに基づく送出上限を使う場合) と、
 * 容量から決まる最大の許可数 (容量を設定している場合) の小さいほうとする。
 * どちらか一方だけを使う場合はその値となり、送出上限を使う受信側では必ず 1 以上になる。
 * fc->mutex を保持して呼ぶ (片方向 UDP 受信者のみ対象)。
 */
static uint32_t report_max_allowance(const struct potr_internal_flow_control *fc, const flow_account *account)
{
    uint32_t allowance = max_occupancy_allowance(fc, account);

    if (fc->receive_buffer_used != 0)
    {
        uint32_t width = (uint32_t)total_buffer_width(fc);

        if (width < allowance)
        {
            allowance = width;
        }
    }

    return allowance;
}
