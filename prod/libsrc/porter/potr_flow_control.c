/**
 *******************************************************************************
 *  @file           potr_flow_control.c
 *  @brief          受信側の通知キューの占有量に基づくフロー制御を管理します。
 *******************************************************************************
 */
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #include <cplat/base/windows_sdk.h>
#else /* PLATFORM_WINDOWS */
    #include <sys/socket.h>
#endif /* PLATFORM_WINDOWS */
#include <cplat/crt/stdlib.h>
#include <cplat/crypto/random.h>
#include <cplat/clock/clock.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/socket.h>
#include <cplat/sync/sync.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/potr_context.h>
#include <porter/potr_flow_control.h>
#include <porter/potr_peer_table.h>
#include <porter/infra/potr_trace.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/packet_seal.h>
#include <porter/protocol/receiver_report.h>

/** 送信スレッドが抑制の状態を確認し直すまでの待機の上限 [ms]。close の開始を、この時間以内に検出する。 */
#define FLOW_CONTROL_RECHECK_MS 50

/** フロー制御パケットのワイヤー長の上限 [byte] (ヘッダー、状態と送出上限、認証タグ)。 */
#define FLOW_CONTROL_WIRE_SIZE (POTR_PACKET_HEADER_SIZE + POTR_FLOW_CONTROL_PAYLOAD_SIZE + POTR_CRYPTO_TAG_SIZE)

/** 監視対象の一覧のパケットのワイヤー長の上限 [byte] (ヘッダー、一覧、認証タグ)。 */
#define FLOW_CONTROL_MONITOR_WIRE_SIZE \
    (POTR_PACKET_HEADER_SIZE + POTR_FLOW_CONTROL_MONITOR_PAYLOAD_MAX + POTR_CRYPTO_TAG_SIZE)

/**
 *  相手ごとの占有量と、抑制の要求の状態。
 *  N:1 モードのピアが切断されて解放された後も、そのピアの通知がすべて消費されるまで保持する。
 */
typedef struct flow_account
{
    struct flow_account *next; /**< 次の相手 (N:1 モードのみ)。 */
    uint64_t occupancy;        /**< コールバックから復帰していない DATA のバイト数。 */
    uint64_t next_request_ms;  /**< 抑制の要求を次に再送する時刻 [ms]。requesting が 1 の間だけ有効。 */
    cplat_timespec session_ts; /**< 相手のセッション開始時刻。 */
    uint32_t session_id;       /**< 相手のセッション識別子。 */
    potr_peer_id peer_id;      /**< 相手のピア識別子 (N:1 モード以外では POTR_PEER_NA)。 */
    int requesting;            /**< 抑制を要求中か (1: 要求中, 0: 要求なし)。 */
    int refused;               /**< 容量超過で切断し、受信を再開しない状態か (1: 再開しない)。 */
    uint32_t next_seq;         /**< 次に受信する通番。has_next_seq が 1 の間だけ有効。 */
    uint32_t notified_base;    /**< 前回フロー制御パケットを送出した時点の「次に受信する通番」。notified が 1 の間だけ有効。 */
    uint32_t notified_limit;   /**< 前回送出した送出上限。notified が 1 の間だけ有効。 */
    int has_next_seq;          /**< 次に受信する通番を受け取ったか (1: 受け取った)。 */
    int notified;              /**< 現在の通信で、相手へ送出上限を知らせたか (1: 知らせた)。 */
    int _pad;                  /**< パディング (構造体サイズを 8 [byte] の倍数に揃える)。 */
} flow_account;

/** 受信側のフロー制御の資源。mutex で保護する。 */
struct potr_internal_flow_control
{
    cplat_local_lock *mutex;   /**< 占有量と状態の遷移、およびフロー制御パケットの送出を一組として保護する。 */
    flow_account *accounts;    /**< N:1 モードの相手の一覧。 */
    flow_account service;      /**< N:1 モード以外の相手。 */
    uint64_t capacity;         /**< 容量 [byte]。 */
    uint64_t suppress_bytes;   /**< 抑制を要求するしきい値 [byte]。 */
    uint64_t resume_bytes;     /**< 解除するしきい値 [byte]。 */
    uint64_t max_payload;      /**< 受信側の実効 max_payload [byte]。送出上限の許可数の計算に使う。 */
    uint64_t receive_buffer_width; /**< 受信バッファーの幅 [パケット] (N:1 モードのピア数による分割と下限の適用の前)。 */
    uint64_t monitor_received_ms; /**< multicast と broadcast の受信者が、監視対象の一覧を最後に受け取った時刻 [ms]。 */
    int receive_buffer_used;   /**< OS の受信バッファーに基づく送出上限を使うか (1: 使う)。 */
    int one_to_many_receiver;  /**< multicast と broadcast の受信者か (1: 監視対象である間だけ送出上限を知らせる)。 */
    int monitored;             /**< multicast と broadcast の受信者で、監視対象に指定されているか (1: 監視対象)。 */
    int _pad;                  /**< パディング (reply を 8 [byte] 境界に揃える)。 */
    cplat_ipv4_endpoint reply[POTR_MAX_PATH]; /**< UDP の返送先 (N:1 モード以外)。受信スレッドが更新する。 */
};

/*
 * 実効値を決める。サービス側の 0 はグローバル値、allow_disabled が非 0 の項目の専用の値は 0 (無効) とする。
 * サービス側とグローバル値の両方が 0 の場合は default_value とする (0 が「無効」を表す項目では 0 を渡す)。
 */
static int resolve_value(uint32_t service_value, uint32_t global_value, int allow_disabled, uint32_t default_value,
                         uint32_t *value)
{
    if (global_value == POTR_FLOW_CONTROL_DISABLED)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (service_value == POTR_FLOW_CONTROL_DISABLED)
    {
        if (allow_disabled == 0)
        {
            return POTR_ERR_INVALID_ARGUMENT;
        }

        *value = 0U;
        return POTR_OK;
    }

    if (service_value != 0U)
    {
        *value = service_value;
    }
    else if (global_value != 0U)
    {
        *value = global_value;
    }
    else
    {
        /* 0 を取れない項目は、グローバル値も 0 (構造体を 0 で初期化した利用側) の場合に既定値を使う。 */
        *value = default_value;
    }

    return POTR_OK;
}

/*
 * OS の受信バッファーに基づく送出上限を使う受信側かどうかを返す。
 * unicast、multicast、broadcast の受信者と、unicast_bidir、unicast_bidir_n1 の両方の役割である。
 * multicast と broadcast の受信者は、受信報告を送る構成 (実効 health_interval_ms が 0 でない) に限る。
 * TCP は下位の通信が受信バッファーを守るため、RAW は再送を行わないため、対象外とする。
 */
static int uses_receive_buffer(potr_type type, potr_role role, uint32_t health_interval_ms)
{
    if (type == POTR_TYPE_UNICAST_BIDIR || type == POTR_TYPE_UNICAST_BIDIR_N1)
    {
        return 1;
    }

    if (role != POTR_ROLE_RECEIVER)
    {
        return 0;
    }

    if (type == POTR_TYPE_UNICAST)
    {
        return 1;
    }

    return (type == POTR_TYPE_MULTICAST || type == POTR_TYPE_BROADCAST) && health_interval_ms != 0U;
}

/*
 * 受信バッファーの幅の計算に使う割合の実効値を決め、検査する。
 * この項目は既定で有効で、0 が「既定値を使用」(サービス側では「グローバル値を使用」) を表すため、
 * グローバル設定とサービス定義の両方で POTR_FLOW_CONTROL_DISABLED (無効) を受け付ける。
 * 無効の場合と、OS の受信バッファーに基づく送出上限を使わない通信種別と役割では、0 を格納する。
 */
static int resolve_receive_buffer_percent(const potr_service_def *service, const potr_global_config *global,
                                          potr_role role, uint32_t health_interval_ms, uint32_t *value)
{
    uint32_t percent;

    if (service->flow_control_receive_buffer_percent == POTR_FLOW_CONTROL_DISABLED)
    {
        percent = 0U;
    }
    else if (service->flow_control_receive_buffer_percent != 0U)
    {
        percent = service->flow_control_receive_buffer_percent;
    }
    else if (global->flow_control_receive_buffer_percent == POTR_FLOW_CONTROL_DISABLED)
    {
        percent = 0U;
    }
    else if (global->flow_control_receive_buffer_percent != 0U)
    {
        percent = global->flow_control_receive_buffer_percent;
    }
    else
    {
        /* 構造体を 0 で初期化した利用側は、既定値を使う。 */
        percent = (uint32_t)POTR_DEFAULT_FLOW_CONTROL_RECEIVE_BUFFER_PERCENT;
    }

    if (percent > 100U)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " invalid receive_buffer_percent=%u (1 <= percent <= 100)",
                   service->service_id, (unsigned)percent);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (!uses_receive_buffer(service->type, role, health_interval_ms))
    {
        percent = 0U;
    }

    *value = percent;

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_flow_control_resolve(const potr_service_def *service, const potr_global_config *global,
                                       potr_role role, uint32_t health_interval_ms,
                                       potr_internal_flow_control_config *config)
{
    if (service == NULL || global == NULL || config == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    memset(config, 0, sizeof(*config));

    /* RAW 系の通信種別はフロー制御の対象外であり、設定を無視する。 */
    if (is_raw_type(service->type))
    {
        return POTR_OK;
    }

    if (resolve_value(service->flow_control_capacity, global->flow_control_capacity, 1, 0U, &config->capacity) !=
            POTR_OK ||
        resolve_value(service->flow_control_suppress_percent, global->flow_control_suppress_percent, 0,
                      (uint32_t)POTR_DEFAULT_FLOW_CONTROL_SUPPRESS_PERCENT,
                      &config->suppress_percent) != POTR_OK ||
        resolve_value(service->flow_control_resume_percent, global->flow_control_resume_percent, 0,
                      (uint32_t)POTR_DEFAULT_FLOW_CONTROL_RESUME_PERCENT,
                      &config->resume_percent) != POTR_OK ||
        resolve_value(service->flow_control_request_interval_ms, global->flow_control_request_interval_ms, 0,
                      (uint32_t)POTR_DEFAULT_FLOW_CONTROL_REQUEST_INTERVAL_MS,
                      &config->request_interval_ms) != POTR_OK ||
        resolve_value(service->flow_control_request_lifetime_ms, global->flow_control_request_lifetime_ms, 0,
                      (uint32_t)POTR_DEFAULT_FLOW_CONTROL_REQUEST_LIFETIME_MS,
                      &config->request_lifetime_ms) != POTR_OK ||
        resolve_value(service->flow_control_receiver_percent, global->flow_control_receiver_percent, 1, 0U,
                      &config->receiver_percent) != POTR_OK ||
        resolve_value(service->flow_control_receiver_count, global->flow_control_receiver_count, 1, 0U,
                      &config->receiver_count) != POTR_OK ||
        resolve_value(service->flow_control_monitored_receiver_percent, global->flow_control_monitored_receiver_percent,
                      1, 0U, &config->monitored_receiver_percent) != POTR_OK ||
        resolve_value(service->flow_control_monitored_receiver_count, global->flow_control_monitored_receiver_count, 1,
                      0U, &config->monitored_receiver_count) != POTR_OK)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " POTR_FLOW_CONTROL_DISABLED is not allowed for this field",
                   service->service_id);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (config->suppress_percent < 1U || config->suppress_percent > 100U || config->resume_percent < 1U ||
        config->resume_percent >= config->suppress_percent)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " invalid thresholds suppress_percent=%u resume_percent=%u"
                   " (1 <= resume < suppress <= 100)",
                   service->service_id, (unsigned)config->suppress_percent, (unsigned)config->resume_percent);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (config->request_interval_ms == 0U || config->request_lifetime_ms <= config->request_interval_ms)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " invalid request_interval_ms=%u request_lifetime_ms=%u"
                   " (0 < interval < lifetime)",
                   service->service_id, (unsigned)config->request_interval_ms,
                   (unsigned)config->request_lifetime_ms);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    /* 再送周期と有効期限は待機の時間に使うため、待機の関数へ int で渡せる上限を超える値は受け付けない。 */
    if (config->request_interval_ms > POTR_WAIT_MS_MAX || config->request_lifetime_ms > POTR_WAIT_MS_MAX)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " invalid request_interval_ms=%u request_lifetime_ms=%u"
                   " (max: %u)",
                   service->service_id, (unsigned)config->request_interval_ms,
                   (unsigned)config->request_lifetime_ms, (unsigned)POTR_WAIT_MS_MAX);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (config->receiver_percent > 100U)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "flow_control: service_id=%" PRId64 " invalid receiver_percent=%u (max: 100)",
                   service->service_id, (unsigned)config->receiver_percent);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (config->monitored_receiver_percent > 100U ||
        config->monitored_receiver_count > POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " invalid monitored_receiver_percent=%u (max: 100)"
                   " monitored_receiver_count=%u (max: %u)",
                   service->service_id, (unsigned)config->monitored_receiver_percent,
                   (unsigned)config->monitored_receiver_count, (unsigned)POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (resolve_receive_buffer_percent(service, global, role, health_interval_ms, &config->receive_buffer_percent) !=
        POTR_OK)
    {
        return POTR_ERR_INVALID_ARGUMENT;
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
            return POTR_ERR_INVALID_ARGUMENT;
        }
    }

    /* 一対多の送信側は、受信報告で受信者数を把握する。受信報告を送らない受信側の構成は受け付けない。 */
    if ((service->type == POTR_TYPE_MULTICAST || service->type == POTR_TYPE_BROADCAST) && role == POTR_ROLE_RECEIVER &&
        health_interval_ms == 0U)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " multicast/broadcast receiver requires health_interval_ms > 0",
                   service->service_id);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    return POTR_OK;
}

/*
 * ソケットから OS の受信バッファーの大きさ (SO_RCVBUF) を取得する。取得できない場合と、0 以下の値の場合は POTR_ERR_IO を返す。
 * Linux は、取得した値に管理情報の分を含め、受信したパケットを確保済みの領域の大きさで数える。
 * see: https://man7.org/linux/man-pages/man7/socket.7.html
 * Windows も同じ SOL_SOCKET の SO_RCVBUF で、受信に使う領域の大きさを取得できる。
 * see: https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-getsockopt
 * see: https://learn.microsoft.com/en-us/windows/win32/winsock/sol-socket-socket-options
 */
static int get_receive_buffer_size(cplat_socket receive_socket, uint64_t *size)
{
    int value = 0;

#if defined(PLATFORM_WINDOWS)
    int length = (int)sizeof(value);

    if (getsockopt((SOCKET)receive_socket, SOL_SOCKET, SO_RCVBUF, (char *)&value, &length) != 0 || value <= 0)
    {
        return POTR_ERR_IO;
    }
#else  /* PLATFORM_WINDOWS */
    socklen_t length = (socklen_t)sizeof(value);

    if (getsockopt((int)receive_socket, SOL_SOCKET, SO_RCVBUF, &value, &length) != 0 || value <= 0)
    {
        return POTR_ERR_IO;
    }
#endif /* PLATFORM_WINDOWS */

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

    if (ctx->flow_control_config.capacity == 0U && ctx->flow_control_config.receive_buffer_percent == 0U)
    {
        return POTR_OK;
    }

    /* 送出上限の許可数を max_payload で求めるため、0 は受け付けない (サービスを開く処理が 64 以上を検査済み)。 */
    if (ctx->global.max_payload == 0U)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    /* 大きさを取得できなかった場合は、警告を記録し、このハンドルでは OS の受信バッファーに基づく送出上限を使わない。 */
    if (ctx->flow_control_config.receive_buffer_percent != 0U && query_receive_buffer_size(ctx, &buffer_size) == POTR_OK)
    {
        buffer_used = 1;
    }

    /* 容量が 0 で、OS の受信バッファーに基づく送出上限も使わない受信側は、フロー制御パケットを送出しない。 */
    if (ctx->flow_control_config.capacity == 0U && buffer_used == 0)
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
    if (buffer_used != 0)
    {
        fc->receive_buffer_width = flow_receive_buffer_width(buffer_size, ctx->flow_control_config.receive_buffer_percent,
                                                             (uint32_t)ctx->global.max_payload);
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

/* 相手の占有量の記録を探す。N:1 モードで見つからず create が非 0 の場合は作成する。fc->mutex を保持して呼ぶ。 */
static flow_account *find_account(const potr_context *ctx, struct potr_internal_flow_control *fc,
                                  potr_peer_id peer_id, int create)
{
    flow_account *account;

    if (!ctx->is_multi_peer)
    {
        return &fc->service;
    }

    for (account = fc->accounts; account != NULL; account = account->next)
    {
        if (account->peer_id == peer_id)
        {
            return account;
        }
    }

    if (create == 0)
    {
        return NULL;
    }

    account = (flow_account *)cplat_calloc(1U, sizeof(*account));
    if (account != NULL)
    {
        account->peer_id = peer_id;
        account->next = fc->accounts;
        fc->accounts = account;
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
                cplat_socket_send_all(ctx->tcp_conn_fd[i], wire, wire_len, NULL) == CPLAT_OK)
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
            size_t sent = 0;

            if (cplat_socket_sendto(ctx->sock[i], wire, wire_len, &dest[i], &sent, NULL) == CPLAT_OK)
            {
                sent_any = 1;
            }
        }
    }

    return sent_any;
}

/*
 * 受信バッファーの幅による許可数を返す。N:1 モードでは、すべてのピアが 1 つのソケットを共有するため、
 * 幅を接続中のピア数で割る (切り捨て)。幅が下限 (POTR_FLOW_RECEIVE_BUFFER_WIDTH_MIN) 未満になる場合は下限とする。
 * fc->mutex を保持して呼ぶ (N:1 モードでは peers_mutex も)。
 */
static uint32_t buffer_allowance(const potr_context *ctx, const struct potr_internal_flow_control *fc)
{
    uint64_t width = fc->receive_buffer_width;

    if (ctx->is_multi_peer && ctx->n_peers > 1)
    {
        width /= (uint64_t)ctx->n_peers;
    }

    if (width < (uint64_t)POTR_FLOW_RECEIVE_BUFFER_WIDTH_MIN)
    {
        width = (uint64_t)POTR_FLOW_RECEIVE_BUFFER_WIDTH_MIN;
    }

    /* 幅は SO_RCVBUF (int) を 1 パケットの見積もり (2,000 byte 以上) で割った値以下のため、uint32_t に収まる。 */
    return (uint32_t)width;
}

/*
 * 許可数を返す。占有量による許可数と、受信バッファーの幅による許可数の小さいほうとする。
 * 占有量による許可数は、占有量が要求のしきい値を超えている場合は 0、それ以外は
 * (要求のしきい値 − 占有量) ÷ max_payload (切り捨て) + 1 とする。容量が 0 の受信側では使わない。
 * 受信バッファーの幅による許可数は、OS の受信バッファーに基づく送出上限を使わない受信側では使わない。
 * 資源は、どちらかを使う受信側にだけ生成する。fc->mutex を保持して呼ぶ (N:1 モードでは peers_mutex も)。
 */
static uint32_t send_allowance(const potr_context *ctx, const struct potr_internal_flow_control *fc,
                               const flow_account *account)
{
    uint32_t allowance = UINT32_MAX;

    if (fc->capacity != 0U)
    {
        if (account->occupancy > fc->suppress_bytes)
        {
            allowance = 0U;
        }
        else
        {
            /* 要求のしきい値は容量 (uint32_t) 以下、max_payload は 1 以上のため、商は uint32_t に収まる。 */
            allowance = (uint32_t)((fc->suppress_bytes - account->occupancy) / fc->max_payload) + 1U;
        }
    }

    if (fc->receive_buffer_used != 0)
    {
        uint32_t width = buffer_allowance(ctx, fc);

        if (width < allowance)
        {
            allowance = width;
        }
    }

    return allowance;
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
        account->notified = 1;
        account->notified_base = account->next_seq;
        account->notified_limit = send_limit;
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
 * 相手のセッションを記録する。セッションが変わった場合は新しい通信として、送出上限をまだ知らせていない状態に戻す。
 * multicast と broadcast の受信者は、送信者のセッションが変わった場合、監視対象の一覧を受け取っていない状態に戻す。
 * fc->mutex を保持して呼ぶ。
 */
static void adopt_session(struct potr_internal_flow_control *fc, flow_account *account, uint32_t session_id,
                          const cplat_timespec *session_ts)
{
    if (account->session_id != session_id || account->session_ts.tv_sec != session_ts->tv_sec ||
        account->session_ts.tv_nsec != session_ts->tv_nsec)
    {
        account->notified = 0;
        account->has_next_seq = 0;
        fc->monitored = 0;
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

    adopt_session(fc, account, session_id, session_ts);

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
        notify_state(ctx, fc, account, (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_flow_control_on_received(potr_context *ctx, potr_peer_id peer_id, uint32_t session_id,
                                            const cplat_timespec *session_ts, uint32_t next_seq, int data_packet)
{
    struct potr_internal_flow_control *fc;
    flow_account *account;

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

    adopt_session(fc, account, session_id, session_ts);

    /*
     * 次に受信する通番をまだ受け取っていなかった場合 (抑制の要求だけを先に送出した場合を含む) と、
     * 前回知らせた時点より前へ戻った場合 (受信ウィンドウの初期化) は、送出上限を知らせ直す。
     */
    if (account->has_next_seq == 0 || (account->notified != 0 && (int32_t)(next_seq - account->notified_base) < 0))
    {
        account->notified = 0;
    }

    account->next_seq = next_seq;
    account->has_next_seq = 1;

    if (account->notified == 0)
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
        judge_progress(ctx, fc, account);
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
                notify_state(ctx, fc, account, (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME);
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
        notify_state(ctx, fc, account, current_state(account));
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
                *link = account->next;
                cplat_free(account);
                continue;
            }

            tick_account(ctx, fc, account, now_ms);
            link = &account->next;
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
    adopt_session(fc, account, pkt->session_id, &sender_ts);
    fc->monitor_received_ms = now_ms;

    if (included != 0 && fc->monitored == 0)
    {
        fc->monitored = 1;
        account->notified = 0;
        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "flow_control[service_id=%" PRId64 "]: selected as monitored receiver",
                   ctx->service.service_id);

        /* 次に受信する通番が分かっていれば、最初の送出を行う。分からなければ、次の DATA か PING の受理で行う。 */
        if (account->has_next_seq != 0 && account->refused == 0)
        {
            notify_state(ctx, fc, account, current_state(account));
        }
    }
    else if (included == 0 && fc->monitored != 0)
    {
        fc->monitored = 0;
        account->notified = 0;
        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "flow_control[service_id=%" PRId64 "]: no longer monitored receiver",
                   ctx->service.service_id);
    }

    cplat_local_lock_unlock(fc->mutex);
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

    /*
     * 時刻を取得できない場合は、要求の期限と送出上限を破棄する時刻を決められないため、このパケットを反映しない。
     * 相手は、要求を再送周期ごとに再送し、送出上限を次の契機で送出する。
     */
    if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
    {
        return;
    }

    if (state == POTR_FLOW_CONTROL_STATE_SUPPRESS)
    {
        expire_ms = now_ms + ctx->flow_control_config.request_lifetime_ms;
    }

    /* 送出上限は、状態の値によらず上書きする。通番で表すため、重複して届いても判定は変わらない。 */
    if (peer != NULL)
    {
        peer->flow_request_expire_ms = expire_ms;
        peer->flow_limit_received_ms = now_ms;
        peer->flow_send_limit = POTR_FLOW_SEND_LIMIT_HELD | (uint64_t)send_limit;
        potr_internal_flow_control_peer_sync(ctx, peer);
    }
    else
    {
        cplat_atomic_store_u64(&ctx->flow_request_expire_ms, expire_ms, CPLAT_MEMORY_ORDER_RELEASE);

        /* 受けた時刻を先に書く。送出上限を読んだ送信スレッドが、対応する時刻を必ず読めるようにするためである。 */
        cplat_atomic_store_u64(&ctx->flow_limit_received_ms, now_ms, CPLAT_MEMORY_ORDER_RELEASE);
        cplat_atomic_store_u64(&ctx->flow_send_limit, POTR_FLOW_SEND_LIMIT_HELD | (uint64_t)send_limit,
                               CPLAT_MEMORY_ORDER_RELEASE);
    }

    potr_internal_send_queue_wake_flow(&ctx->send_queue);
}

/* 残り時間 [ms] を、確認し直すまでの待機の上限に丸める。 */
static int recheck_wait_ms(uint64_t now_ms, uint64_t until_ms)
{
    if (until_ms > now_ms && until_ms - now_ms < (uint64_t)FLOW_CONTROL_RECHECK_MS)
    {
        /* 差は FLOW_CONTROL_RECHECK_MS 未満であり、int に収まる。 */
        return (int)(until_ms - now_ms);
    }

    return FLOW_CONTROL_RECHECK_MS;
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
        return 0;
    }

    if (is_oneway_udp_type(ctx->service.type))
    {
        size_t tracked = 0U;
        int suppressed;

        /*
         * 要求と送出上限を 1 つも受けていない間は、受信者の管理表を走査しない。
         * 先に 0 へ戻してから走査し、要求か送出上限が残っていれば 1 へ戻す。走査の後に届いたフロー制御パケットは、
         * 受信スレッドが 1 を設定するため、取りこぼさない。
         */
        if (cplat_atomic_load_i32(&ctx->flow_request_pending, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
        {
            return 0;
        }

        cplat_atomic_store_i32(&ctx->flow_request_pending, 0, CPLAT_MEMORY_ORDER_RELEASE);

        /* send_window_mutex と callback_mutex を重ねて保持しないよう、通番を先に読む。 */
        next_seq = read_next_seq(&ctx->send_window, ctx->send_window_mutex);
        suppressed = potr_internal_receiver_flow_suppressed(ctx, now_ms, next_seq, &tracked, &until_ms);
        if (tracked != 0U)
        {
            cplat_atomic_store_i32(&ctx->flow_request_pending, 1, CPLAT_MEMORY_ORDER_RELEASE);
        }

        if (suppressed == 0)
        {
            return 0;
        }
    }
    else
    {
        uint64_t expire_ms = cplat_atomic_load_u64(&ctx->flow_request_expire_ms, CPLAT_MEMORY_ORDER_ACQUIRE);

        /* 送出上限を先に読む。受信スレッドは時刻を先に書くため、読んだ送出上限に対応する時刻以降の値を読める。 */
        uint64_t send_limit = cplat_atomic_load_u64(&ctx->flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
        uint64_t received_ms = cplat_atomic_load_u64(&ctx->flow_limit_received_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
        uint64_t limit_until_ms = 0U;
        potr_internal_send_limit_state limit_state;
        int request_valid = expire_ms > now_ms;

        next_seq = read_next_seq(&ctx->send_window, ctx->send_window_mutex);

        /*
         * 有効期限が経過した送出上限 (POTR_SEND_LIMIT_EXPIRED) は、上限がないものとして扱う。
         * 送出上限の値は書き換えず、次のフロー制御パケットを受けた時刻が止まった時刻より後になるまで、同じ判定を続ける。
         */
        limit_state = flow_send_limit_judge(send_limit, received_ms, &ctx->flow_limit_stalled_ms, next_seq, now_ms,
                                            ctx->flow_control_config.request_lifetime_ms, &limit_until_ms,
                                            allowance);

        if (request_valid == 0 && limit_state != POTR_SEND_LIMIT_REACHED)
        {
            return 0;
        }

        /* 相手を切断と判定した場合は、その相手の要求と送出上限を取り消す。 */
        if (cplat_atomic_load_i32(&ctx->health_alive, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
        {
            cplat_atomic_store_u64(&ctx->flow_request_expire_ms, 0U, CPLAT_MEMORY_ORDER_RELEASE);
            cplat_atomic_store_u64(&ctx->flow_send_limit, 0U, CPLAT_MEMORY_ORDER_RELEASE);
            ctx->flow_limit_stalled_ms = 0U;
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
        }
    }

    *wait_ms = recheck_wait_ms(now_ms, until_ms);

    return 1;
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_flow_control_peer_state(potr_context *ctx, potr_peer_id peer_id, uint64_t now_ms, int *deferred)
{
    potr_internal_peer_context *peer;
    int suppressed = 0;

    if (ctx == NULL || deferred == NULL)
    {
        return 0;
    }

    *deferred = 0;

    if (!ctx->is_multi_peer)
    {
        return 0;
    }

    cplat_local_lock_lock(ctx->peers_mutex);
    peer = potr_internal_peer_find_by_id(ctx, peer_id);

    /* close の開始後は、抑制の要求と送出上限を無視する。 */
    if (peer != NULL && cplat_atomic_load_i32(&ctx->close_requested, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
    {
        uint64_t limit_until_ms = 0U;
        uint32_t allowance = 0U;
        uint32_t next_seq = read_next_seq(&peer->send_window, peer->send_window_mutex);
        potr_internal_send_limit_state limit_state =
            flow_send_limit_judge(peer->flow_send_limit, peer->flow_limit_received_ms, &peer->flow_limit_stalled_ms,
                                  next_seq, now_ms, ctx->flow_control_config.request_lifetime_ms, &limit_until_ms,
                                  &allowance);

        /* 送出上限に達した時点から有効期限が経過しても次のフロー制御パケットが届かない場合は、送出上限を破棄する。 */
        if (limit_state == POTR_SEND_LIMIT_EXPIRED)
        {
            peer->flow_send_limit = 0U;
            peer->flow_limit_stalled_ms = 0U;
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
                peer->flow_send_limit = 0U;
                peer->flow_limit_stalled_ms = 0U;
            }
            else
            {
                suppressed = 1;
            }
        }

        potr_internal_flow_control_peer_sync(ctx, peer);
    }

    if (peer != NULL)
    {
        *deferred = peer->flow_deferred_entries;
    }

    cplat_local_lock_unlock(ctx->peers_mutex);

    return suppressed;
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_flow_control_report_limit(potr_context *ctx, uint32_t *send_limit)
{
    struct potr_internal_flow_control *fc;
    flow_account *account;
    int held = 0;

    if (send_limit == NULL)
    {
        return 0;
    }

    *send_limit = 0U;

    /* フロー制御パケットを送出しない受信側は、送出上限を持たない。 */
    if (ctx == NULL || ctx->flow_control == NULL || ctx->is_multi_peer)
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
        held = 1;
    }

    cplat_local_lock_unlock(fc->mutex);

    return held;
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_flow_control_monitor_tick(potr_context *ctx, uint64_t now_ms)
{
    potr_internal_packet_session_hdr list[POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX];
    potr_internal_packet_session_hdr shdr;
    uint8_t payload[POTR_FLOW_CONTROL_MONITOR_PAYLOAD_MAX];
    potr_packet packet;
    size_t count;
    int changed = 0;
    int i;

    if (ctx == NULL || ctx->role != POTR_ROLE_SENDER ||
        (ctx->service.type != POTR_TYPE_MULTICAST && ctx->service.type != POTR_TYPE_BROADCAST))
    {
        return;
    }

    count = potr_internal_receiver_monitor_list(ctx, now_ms, list, POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX, &changed);

    /* 監視対象が 0 台の間は送出しない。受信者は、一覧の有効期限で監視対象でなくなる。 */
    if (count == 0U)
    {
        return;
    }

    /* 監視対象が変わった時点と、flow_control_request_interval_ms の周期で送出する。 */
    if (changed == 0 && ctx->flow_monitor_next_ms != 0U && now_ms < ctx->flow_monitor_next_ms)
    {
        return;
    }

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
        size_t sent = 0U;

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
        (void)cplat_socket_sendto(ctx->sock[i], wire, wire_len, &ctx->dest_addr[i], &sent, NULL);
    }

    ctx->flow_monitor_next_ms = now_ms + (uint64_t)ctx->flow_control_config.request_interval_ms;
}
