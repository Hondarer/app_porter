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
#include <porter/potr_flow_control.h>
#include <porter/potr_peer_table.h>
#include <porter/infra/potr_trace.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/packet_seal.h>
#include <porter/protocol/receiver_report.h>

/** 送信スレッドが抑制の状態を確認し直すまでの待機の上限 [ms]。close の開始を、この時間以内に検出する。 */
#define FLOW_CONTROL_RECHECK_MS 50

/** フロー制御パケットのワイヤー長の上限 [byte] (ヘッダー、状態、認証タグ)。 */
#define FLOW_CONTROL_WIRE_SIZE (POTR_PACKET_HEADER_SIZE + POTR_FLOW_CONTROL_PAYLOAD_SIZE + POTR_CRYPTO_TAG_SIZE)

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
                      &config->receiver_count) != POTR_OK)
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

    if (config->receiver_percent > 100U)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "flow_control: service_id=%" PRId64 " invalid receiver_percent=%u (max: 100)",
                   service->service_id, (unsigned)config->receiver_percent);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (config->capacity == 0U)
    {
        return POTR_OK;
    }

    /* 最大長のメッセージ 1 件で容量を超える構成は、必ず切断になるため受け付けない。 */
    if (config->capacity < global->max_message_size)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "flow_control: service_id=%" PRId64 " capacity=%u is smaller than max_message_size=%u",
                   service->service_id, (unsigned)config->capacity, (unsigned)global->max_message_size);
        return POTR_ERR_INVALID_ARGUMENT;
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

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_flow_control_start(potr_context *ctx)
{
    struct potr_internal_flow_control *fc;

    if (ctx == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (ctx->flow_control_config.capacity == 0U)
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
 * フロー制御パケットを相手へ送出する。fc->mutex を保持して呼ぶ。
 * N:1 モードでは、呼び出し元が peers_mutex も保持していること。
 * 送出の失敗は呼び出し元へ返さない。要求は再送周期ごとに再送し、解除は送信側の要求の有効期限で補うためである。
 */
static void send_state(potr_context *ctx, struct potr_internal_flow_control *fc, const flow_account *account,
                       uint8_t state)
{
    potr_packet packet;
    potr_internal_packet_session_hdr shdr;
    uint8_t wire[FLOW_CONTROL_WIRE_SIZE];
    potr_internal_tx_session *session = &ctx->tx_session;
    const cplat_ipv4_endpoint *dest = fc->reply;
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
            return;
        }

        shdr.session_id = peer->session_id;
        session_ts_to_hdr(&peer->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);
        session = &peer->tx_session;
        dest = peer->dest_addr;
    }

    if (potr_internal_packet_build_flow_control(&packet, &shdr, &state) != POTR_OK)
    {
        return;
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

            cplat_local_lock_lock(ctx->tcp_send_mutex[i]);
            if (potr_internal_packet_seal_for_context(ctx, session, &packet, wire, &wire_len) == POTR_OK)
            {
                (void)cplat_socket_send_all(ctx->tcp_conn_fd[i], wire, wire_len,
                                            NULL); /* 書き込めない経路は受信スレッドが切断を検知するため、戻り値は明示的に破棄する */
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
            (void)cplat_socket_sendto(ctx->sock[i], wire, wire_len, &dest[i], &sent,
                                      NULL); /* 要求は再送し、解除は有効期限で補うため、戻り値は明示的に破棄する */
        }
    }
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

    /* 時刻は要求の再送の予定にだけ使う。取得できない場合は 0 とし、次の周期処理で再送する。 */
    (void)cplat_clock_get_monotonic_ms(
        &now_ms); /* 失敗時は now_ms が 0 のままで、再送が早まるだけのため、戻り値は明示的に破棄する */

    cplat_local_lock_lock(fc->mutex);
    refresh_reply(ctx, fc);
    account = find_account(ctx, fc, peer_id, 1);

    if (account == NULL)
    {
        /* 記録を確保できない場合は、計数せずに受け入れる。受信を止めるより、通知を続けることを優先する。 */
        cplat_local_lock_unlock(fc->mutex);
        return POTR_OK;
    }

    account->session_id = session_id;
    account->session_ts = *session_ts;

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
                account->requesting = 1;
                account->next_request_ms = next_request_time(ctx, now_ms);
                send_state(ctx, fc, account, (uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS);
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
            account->requesting = 1;
            account->next_request_ms = next_request_time(ctx, now_ms);
            send_state(ctx, fc, account, (uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS);
            POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                       "flow_control[service_id=%" PRId64 "]: peer=%u requested suppression (occupancy=%" PRIu64 ")",
                       ctx->service.service_id, (unsigned)peer_id, account->occupancy);
        }
    }

    cplat_local_lock_unlock(fc->mutex);

    return result;
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
                /* 解除は、下回ったことを検出した時点で即時に 1 回送出する。 */
                account->requesting = 0;
                send_state(ctx, fc, account, (uint8_t)POTR_FLOW_CONTROL_STATE_RESUME);
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

        if (ctx->is_multi_peer && account->occupancy == 0U)
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

/* 再送周期が経過した相手へ、抑制の要求を再送する。fc->mutex を保持して呼ぶ。 */
static void resend_request(potr_context *ctx, struct potr_internal_flow_control *fc, flow_account *account,
                           uint64_t now_ms)
{
    if (account->requesting != 0 && now_ms >= account->next_request_ms)
    {
        account->next_request_ms = next_request_time(ctx, now_ms);
        send_state(ctx, fc, account, (uint8_t)POTR_FLOW_CONTROL_STATE_SUPPRESS);
    }
}

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_flow_control_tick(potr_context *ctx, uint64_t now_ms)
{
    struct potr_internal_flow_control *fc;
    flow_account *account;

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
        for (account = fc->accounts; account != NULL; account = account->next)
        {
            resend_request(ctx, fc, account, now_ms);
        }
    }
    else
    {
        refresh_reply(ctx, fc);
        resend_request(ctx, fc, &fc->service, now_ms);
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

/* Doxygen コメントは、ヘッダーに記載 */
void potr_internal_flow_control_on_packet(potr_context *ctx, struct potr_internal_peer_context *peer,
                                          const potr_packet *pkt)
{
    uint64_t now_ms;
    uint64_t expire_ms = 0U;

    if (ctx == NULL || pkt == NULL || pkt->payload == NULL || pkt->payload_len != POTR_FLOW_CONTROL_PAYLOAD_SIZE ||
        pkt->seq_num != 0U || pkt->ack_num != 0U)
    {
        return;
    }

    if (pkt->payload[0] == POTR_FLOW_CONTROL_STATE_SUPPRESS)
    {
        /* 時刻を取得できない場合は期限を決められないため、この要求を反映しない。相手が再送周期ごとに再送する。 */
        if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
        {
            return;
        }

        expire_ms = now_ms + ctx->flow_control_config.request_lifetime_ms;
    }
    else if (pkt->payload[0] != POTR_FLOW_CONTROL_STATE_RESUME)
    {
        return;
    }

    if (peer != NULL)
    {
        peer->flow_request_expire_ms = expire_ms;
    }
    else
    {
        cplat_atomic_store_u64(&ctx->flow_request_expire_ms, expire_ms, CPLAT_MEMORY_ORDER_RELEASE);
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

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_flow_control_suppressed(potr_context *ctx, uint64_t now_ms, int *wait_ms)
{
    uint64_t until_ms;

    if (ctx == NULL || wait_ms == NULL || ctx->is_multi_peer || is_raw_type(ctx->service.type))
    {
        return 0;
    }

    /* close の開始後は抑制を無視し、送信キューに残っている DATA を送出する。 */
    if (cplat_atomic_load_i32(&ctx->close_requested, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
    {
        return 0;
    }

    if (is_oneway_udp_type(ctx->service.type))
    {
        size_t requesting = 0U;
        int suppressed;

        /*
         * 要求を 1 つも受けていない間は、受信者の管理表を走査しない。
         * 先に 0 へ戻してから走査し、要求が残っていれば 1 へ戻す。走査の後に届いた要求は、
         * 受信スレッドが 1 を設定するため、取りこぼさない。
         */
        if (cplat_atomic_load_i32(&ctx->flow_request_pending, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
        {
            return 0;
        }

        cplat_atomic_store_i32(&ctx->flow_request_pending, 0, CPLAT_MEMORY_ORDER_RELEASE);

        suppressed = potr_internal_receiver_flow_suppressed(ctx, now_ms, &requesting, &until_ms);
        if (requesting != 0U)
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
        until_ms = cplat_atomic_load_u64(&ctx->flow_request_expire_ms, CPLAT_MEMORY_ORDER_ACQUIRE);
        if (until_ms <= now_ms)
        {
            return 0;
        }

        /* 相手を切断と判定した場合は、その相手の要求を取り消す。 */
        if (cplat_atomic_load_i32(&ctx->health_alive, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
        {
            cplat_atomic_store_u64(&ctx->flow_request_expire_ms, 0U, CPLAT_MEMORY_ORDER_RELEASE);
            return 0;
        }
    }

    *wait_ms = recheck_wait_ms(now_ms, until_ms);

    return 1;
}
