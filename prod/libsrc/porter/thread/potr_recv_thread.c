/**
 *******************************************************************************
 *  @file           potr_recv_thread.c
 *  @brief          データを受信するスレッドを実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/base/platform.h>
#include <cplat/crt/stdlib.h>
#include <porter/infra/potr_send_queue.h>
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <inttypes.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>

#include <porter/protocol/packet.h>
#include <porter/protocol/window.h>
#include <porter/potr_context.h>
#include <porter/potr_peer_table.h>
#include <porter/thread/potr_health_thread.h>
#include <porter/thread/potr_recv_thread.h>
#include <porter/infra/potr_trace.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/endpoint.h>
#include <cplat/net/socket.h>

#include "thread_recv_validate.h"
#include "thread_recv_slot.h"
#include "thread_recv_session.h"
#include "thread_recv_fin.h"
#include "thread_recv_window.h"
#include "thread_recv_dispatch.h"
#include "thread_recv_health.h"
#include "thread_recv_report.h"

/**
 * 受信期限を監視する場合の、待機の上限を求める除数。待機の上限は health_timeout_ms をこの値で割った値とする。
 * 受信期限は待機から戻るたびに確認するため、待機による無受信の検出の遅れは、待機の上限を超えない。
 */
#define POTR_RECV_POLL_HEALTH_TIMEOUT_DIVISOR 3U

/**
 * 受信期限を監視する場合の、待機の上限の下限 [ms]。
 * health_timeout_ms が 300 [ms] 未満の場合は、待機の上限がこの値になり、health_timeout_ms の 3 分の 1 より長くなる。
 */
#define POTR_RECV_POLL_MIN_MS 100U

/**
 * 受信期限を監視しない場合の、待機の上限 [ms]。
 * 停止の指示は待機から戻るたびに読み直すため、待機による停止の遅れは、この値を超えない。
 */
#define POTR_RECV_POLL_UNMONITORED_MS 500U

static_assert(POTR_RECV_POLL_HEALTH_TIMEOUT_DIVISOR >= 1U, "receive poll divisor must be positive");
static_assert(POTR_RECV_POLL_MIN_MS > 0U, "receive poll minimum must be positive");
static_assert(POTR_RECV_POLL_MIN_MS <= POTR_RECV_POLL_UNMONITORED_MS,
              "receive poll minimum must not exceed the unmonitored poll interval");
static_assert(POTR_RECV_POLL_UNMONITORED_MS <= (uint32_t)INT_MAX, "unmonitored poll interval must fit in int");

/**
 *  証明書で認証する場合の、待機の上限 [ms] です。受信期限の監視の有無によらず適用します。
 *  相互認証の開始、DTLS の再送、周期的な再試行は、待機から戻るたびに処理するため、
 *  待機によるこれらの処理の遅れは、この値を超えません。
 *  制約の記載は app/porter/docs/health-check.md の「タイムアウト検出 (受信スレッド)」を参照してください。
 */
#define POTR_RECV_POLL_CERT_MS 100U

static_assert(POTR_RECV_POLL_CERT_MS > 0U, "cert poll interval must be positive");
static_assert(POTR_RECV_POLL_CERT_MS <= (uint32_t)INT_MAX, "cert poll interval must fit in int");

/** 32 ビットの通番の空間の半分。通番の差がこの値未満の場合を「先」、以上の場合を「前」と判定する。 */
#define POTR_SEQNUM_HALF_SPACE 0x80000000U

static_assert(POTR_SEQNUM_HALF_SPACE == (1UL << 31), "porter: POTR_SEQNUM_HALF_SPACE must be half of 2^32");

/**
 * TCP の受信スレッドで受信期限を監視する場合の、ポーリングの間隔の上限 [ms]。
 * health_timeout_ms がこの値未満の場合は、health_timeout_ms を間隔とする。
 */
#define RECV_TCP_POLL_INTERVAL_MS 1000U

static_assert(RECV_TCP_POLL_INTERVAL_MS > 0U && RECV_TCP_POLL_INTERVAL_MS <= POTR_WAIT_MS_MAX,
              "porter: RECV_TCP_POLL_INTERVAL_MS out of range");

/** tcp_wait_readable() の結果。 */
typedef enum tcp_wait_status
{
    TCP_WAIT_ERROR = -1,  /**< 待機に失敗した。 */
    TCP_WAIT_TIMEOUT = 0, /**< 読み取り可能になる前に待機の時間が過ぎた。 */
    TCP_WAIT_READY = 1    /**< 読み取り可能になった。 */
} tcp_wait_status;

/*
 * 片方向 UDP の REPORT と NACK は複数の返送元から届く。
 * 認証済みの送信元セッションごとに記録を保持し、返送元同士の通番とセッションを混同しない。
 * 単一の UDP 受信スレッドからだけ呼び出す。
 */
static int check_service_replay(potr_context *ctx, const potr_packet *packet, const uint8_t *wire_header)
{
    if (ctx->role == POTR_ROLE_SENDER && is_oneway_udp_type(ctx->service.type))
    {
        if (is_raw_type(ctx->service.type))
        {
            return POTR_OK;
        }
        potr_internal_path_group_result selection;
        int ret = potr_internal_path_group_select(ctx, NULL, &selection);

        if (ret != POTR_OK)
        {
            return ret;
        }
        /* 到着前に期限切れを反映し、途絶を挟んだ回復を連続到達として数えません。 */
        return potr_internal_receiver_accept(ctx, packet, wire_header);
    }
    if (ctx->service.encrypt_enabled && !ctx->peer_session_known)
    {
        potr_internal_replay_window_prepare(&ctx->replay_window, packet);
    }
    return thread_recv_replay_packet(&ctx->replay_window, packet, wire_header);
}

/*
 * N:1 モード: 受信パケットをピアごとにディスパッチして処理する。
 * wire_hdr は認証検証用の受信 wire 先頭 (ヘッダー部)。
 */
static void n1_handle_packet(potr_context *ctx, potr_packet *pkt, const uint8_t *wire_hdr,
                             const cplat_ipv4_endpoint *sender_addr, int path_idx)
{
    potr_internal_peer_context *peer = NULL;
    int is_new_peer = 0;
    thread_recv_slot peer_slot;

    cplat_local_lock_lock(ctx->peers_mutex);
    if (thread_recv_authenticate_packet(ctx, pkt, wire_hdr, "recv", -1) != POTR_OK)
    {
        cplat_local_lock_unlock(ctx->peers_mutex);
        return;
    }

    /* session_triplet でピアを検索する。 */
    cplat_timespec pkt_session_ts;
    session_ts_from_hdr(pkt->session_tv_sec, pkt->session_tv_nsec, &pkt_session_ts);
    peer = potr_internal_peer_find_by_session(ctx, pkt->session_id, &pkt_session_ts);

    /* 通知キューの容量超過で切断した相手は、占有量が解除のしきい値を下回るまで、新しいピアとして登録しない。 */
    if (peer == NULL && potr_internal_flow_control_refused(ctx, POTR_PEER_NA, pkt->session_id, &pkt_session_ts) != 0)
    {
        cplat_local_lock_unlock(ctx->peers_mutex);
        return;
    }

    if (peer == NULL)
    {
        if (pkt->flags & POTR_FLAG_PING)
        {
            peer = potr_internal_peer_create(ctx, sender_addr, path_idx);
            if (peer != NULL)
            {
                peer->peer_session_id = pkt->session_id;
                peer->peer_session_ts = pkt_session_ts;
                peer->peer_session_known = 1;
                if (ctx->service.encrypt_enabled)
                {
                    potr_internal_replay_window_prepare(&peer->replay_window, pkt);
                }
                /*
                 * potr_internal_peer_create が同じ window_size と max_payload で確保したウィンドウの状態を
                 * リセットするだけで再確保が起きないため、戻り値は明示的に破棄する。
                 */
                (void)potr_internal_window_init(&peer->recv_window, pkt->seq_num, ctx->global.window_size,
                                                ctx->global.max_payload);
                is_new_peer = 1;
            }
        }
        else if (pkt->flags & POTR_FLAG_DATA)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                       "recv[service_id=%" PRId64 "]: drop initial DATA"
                       " from unknown peer session=%u",
                       ctx->service.service_id, pkt->session_id);
        }
    }

    if (peer == NULL)
    {
        cplat_local_lock_unlock(ctx->peers_mutex);
        return; /* max_peers 超過または初回受理対象外 */
    }

    if (thread_recv_replay_packet(&peer->replay_window, pkt, wire_hdr) != POTR_OK)
    {
        cplat_local_lock_unlock(ctx->peers_mutex);
        return;
    }
    thread_recv_slot_init_peer(&peer_slot, ctx, peer);

    if (is_new_peer)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                   "recv[service_id=%" PRId64 "]: new peer=%u from %u.%u.%u.%u:%u (CONNECTED pending PING+NORMAL)",
                   ctx->service.service_id, (unsigned)peer->peer_id,
                   (unsigned)((cplat_ntoh32(sender_addr->address) >> 24) & 0xFF),
                   (unsigned)((cplat_ntoh32(sender_addr->address) >> 16) & 0xFF),
                   (unsigned)((cplat_ntoh32(sender_addr->address) >> 8) & 0xFF),
                   (unsigned)(cplat_ntoh32(sender_addr->address) & 0xFF), (unsigned)cplat_ntoh16(sender_addr->port));
    }

    {
        int should_wake_health = thread_recv_dispatch_packet(&peer_slot, pkt, path_idx, sender_addr);

        if (peer->active && cplat_atomic_load_i32(&peer->tx_session.exhausted, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
        {
            thread_recv_disconnect_peer_all_paths(ctx, peer);
            potr_internal_peer_free(ctx, peer);
        }
        cplat_local_lock_unlock(ctx->peers_mutex);

        if (should_wake_health != 0)
        {
            potr_internal_health_thread_wake(ctx);
        }
    }
}

/*
 * 送信者ロール: REPORT と NACK を処理する。
 * 戻り値 1: パケットを消費した (呼び出し元は次のパケットへ)。
 * 戻り値 0: UNICAST_BIDIR SENDER のため受信者処理へフォール スルーする。
 */
static int sender_handle_packet(thread_recv_slot *slot, const potr_packet *pkt)
{
    potr_context *ctx = slot->ctx;

    if (is_raw_type(ctx->service.type))
    {
        return 1;
    }

    if (pkt->flags == POTR_FLAG_REPORT)
    {
        potr_internal_path_group_result selection;
        /*
         * REPORT の反映でグループの状態を更新する目的だけで呼び、失敗 (時計の取得の失敗) は次の送信の経路選択が
         * 検出するため、戻り値は明示的に破棄する。
         */
        (void)potr_internal_path_group_select(ctx, NULL, &selection);

        /* 活動中の受信者数が変わると、一対多の抑制の判定が変わりうるため、送信スレッドへ判定し直させる。 */
        potr_internal_send_queue_wake_flow(&ctx->send_queue);
        return 1;
    }

    /*
     * 片方向 UDP の送信者は、check_service_replay() が受信者の管理表へ抑制の要求の期限を反映済みである。
     * 送信スレッドへ、抑制の判定をし直させる。
     */
    if (pkt->flags == POTR_FLAG_FLOW_CONTROL && is_oneway_udp_type(ctx->service.type))
    {
        cplat_atomic_store_i32(&ctx->flow_request_pending, 1, CPLAT_MEMORY_ORDER_RELEASE);
        potr_internal_send_queue_wake_flow(&ctx->send_queue);
        return 1;
    }
    if (pkt->flags == POTR_FLAG_NACK && ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
    {
        potr_internal_flow_control_on_peer_packet(ctx, NULL, pkt);
    }

    thread_recv_window_on_nack(slot, pkt);

    if (ctx->service.type != POTR_TYPE_UNICAST_BIDIR)
    {
        return 1;
    }

    return 0;
}

/* 受信スレッド本体 */
static void recv_thread_func(void *arg)
{
    potr_context *ctx = (potr_context *)arg;
    uint8_t *buf = ctx->recv_buf; /* POTR_PACKET_HEADER_SIZE + max_payload [byte] */
    potr_packet pkt;
    uint32_t poll_ms;
    thread_recv_slot svc_slot; /* 1:1 モード用スロット (フィールド位置は不変のため 1 回だけ構成) */

    thread_recv_slot_init_ctx(&svc_slot, ctx);

    if ((ctx->role == POTR_ROLE_RECEIVER || ctx->service.type == POTR_TYPE_UNICAST_BIDIR) &&
        ctx->health_timeout_ms > 0U)
    {
        poll_ms = ctx->health_timeout_ms / POTR_RECV_POLL_HEALTH_TIMEOUT_DIVISOR;
        if (poll_ms < POTR_RECV_POLL_MIN_MS)
        {
            poll_ms = POTR_RECV_POLL_MIN_MS;
        }
    }
    else
    {
        poll_ms = POTR_RECV_POLL_UNMONITORED_MS;
    }

    /* reorder_timeout_ms が有効な場合は poll 間隔を短縮してタイムアウト精度を確保する。 */
    if ((ctx->role == POTR_ROLE_RECEIVER || ctx->service.type == POTR_TYPE_UNICAST_BIDIR) &&
        ctx->global.reorder_timeout_ms > 0U)
    {
        if (ctx->global.reorder_timeout_ms < poll_ms)
        {
            poll_ms = ctx->global.reorder_timeout_ms;
        }
    }

    while (cplat_atomic_load_i32(&ctx->running[0], CPLAT_MEMORY_ORDER_ACQUIRE) != 0 &&
           cplat_atomic_load_i32(&ctx->fatal_error, CPLAT_MEMORY_ORDER_ACQUIRE) == POTR_OK)
    {
        unsigned char ready[POTR_MAX_PATH];
        cplat_error detail;
        int poll_result;
        int i;

        uint64_t now_ms;
        uint32_t wait_ms = poll_ms;

        if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
        {
            cplat_atomic_store_i32(&ctx->fatal_error, POTR_ERR_IO, CPLAT_MEMORY_ORDER_RELEASE);
            potr_internal_send_queue_abort(&ctx->send_queue, POTR_ERR_IO);
            break;
        }
        if (ctx->is_multi_peer)
        {
            thread_recv_health_check_peers(ctx);
        }
        else if (ctx->role == POTR_ROLE_RECEIVER || ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
        {
            thread_recv_health_check_service(&svc_slot);
        }
        potr_internal_certificate_tick(ctx, now_ms);
        if (ctx->service.auth == POTR_AUTH_CERT && wait_ms > POTR_RECV_POLL_CERT_MS)
        {
            wait_ms = POTR_RECV_POLL_CERT_MS;
        }
        int report_ret = thread_recv_report_tick(ctx, now_ms);
        if (report_ret != POTR_OK)
        {
            cplat_atomic_store_i32(&ctx->fatal_error, report_ret, CPLAT_MEMORY_ORDER_RELEASE);
            potr_internal_send_queue_abort(&ctx->send_queue, report_ret);
            break;
        }
        if (ctx->next_report_ms > now_ms && ctx->next_report_ms - now_ms < wait_ms)
        {
            wait_ms = (uint32_t)(ctx->next_report_ms - now_ms);
        }

        /*
         * multicast と broadcast の送信者は、監視対象が変わった時点と周期で、監視対象の一覧を受信者へ送出する。
         * 受信報告の処理で監視対象が変わった場合は、受信の処理を終えて次の周回に入ったこの時点で送出する。
         */
        if (ctx->role == POTR_ROLE_SENDER &&
            (ctx->service.type == POTR_TYPE_MULTICAST || ctx->service.type == POTR_TYPE_BROADCAST))
        {
            potr_internal_flow_control_monitor_tick(ctx, now_ms);
            if (ctx->flow_monitor_next_ms > now_ms && ctx->flow_monitor_next_ms - now_ms < wait_ms)
            {
                wait_ms = (uint32_t)(ctx->flow_monitor_next_ms - now_ms);
            }
        }

        /* 抑制の要求を再送周期ごとに再送するため、フロー制御が有効な受信側は待ち時間の上限を再送周期にする。 */
        if (ctx->flow_control != NULL)
        {
            potr_internal_flow_control_tick(ctx, now_ms);
            if (ctx->flow_control_config.request_interval_ms < wait_ms)
            {
                wait_ms = ctx->flow_control_config.request_interval_ms;
            }
        }
        poll_result =
            cplat_socket_timed_wait_readable_multi(ctx->sock, (size_t)ctx->n_path, (int)wait_ms, ready, &detail);
        if (poll_result == CPLAT_ERR_TIMEOUT)
        {
            if (!ctx->is_multi_peer && ctx->global.reorder_timeout_ms > 0U &&
                (ctx->role == POTR_ROLE_RECEIVER || ctx->service.type == POTR_TYPE_UNICAST_BIDIR))
            {
                thread_recv_window_check_reorder_timeout(&svc_slot);
            }

            continue;
        }
        if (poll_result != CPLAT_OK)
        {
            if (cplat_atomic_load_i32(&ctx->running[0], CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
            {
                break;
            }
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "recv[service_id=%" PRId64 "]: socket poll failed: rc=%d",
                       ctx->service.service_id, poll_result);
            break;
        }

        for (i = 0; i < ctx->n_path; i++)
        {
            size_t recv_len = 0;
            int recv_result;

            if (ctx->sock[i] == CPLAT_INVALID_SOCKET)
            {
                continue;
            }
            if (ready[i] == 0U)
            {
                continue;
            }

            cplat_ipv4_endpoint sender_addr = {0};

            recv_result = cplat_socket_recvfrom(ctx->sock[i], buf, POTR_PACKET_HEADER_SIZE + ctx->global.max_payload,
                                                &sender_addr, &recv_len, NULL);
            if (recv_result != CPLAT_OK || recv_len == 0U)
            {
                if (cplat_atomic_load_i32(&ctx->running[0], CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
                {
                    break; /* 正常終了: ソケット クローズによる割り込み */
                }
                POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: recvfrom failed (rc=%d)",
                           ctx->service.service_id, recv_result);
                continue;
            }

            if (potr_internal_packet_parse(&pkt, buf, recv_len) != POTR_OK)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: packet parse failed (len=%zu)",
                           ctx->service.service_id, recv_len);
                continue;
            }

            if (pkt.service_id != ctx->service.service_id)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                           "recv[service_id=%" PRId64 "]: ignored packet for service_id=%" PRId64 "",
                           ctx->service.service_id, pkt.service_id);
                continue;
            }

            if (!thread_recv_check_src_addr(ctx, &sender_addr))
            {
                continue;
            }

            if (potr_internal_certificate_receive(ctx, &pkt, buf, &sender_addr, i) == POTR_OK)
            {
                continue;
            }

            /* ── N:1 モード: ピアごとにディスパッチ ── */
            if (ctx->is_multi_peer)
            {
                n1_handle_packet(ctx, &pkt, buf, &sender_addr, i);
                continue;
            }

            if (thread_recv_authenticate_packet(ctx, &pkt, buf, "recv", -1) != POTR_OK)
            {
                continue;
            }

            if (check_service_replay(ctx, &pkt, buf) != POTR_OK)
            {
                continue;
            }

            /* ── 送信者ロール: REPORT と NACK を処理 ── */
            if (ctx->role == POTR_ROLE_SENDER)
            {
                if (sender_handle_packet(&svc_slot, &pkt))
                {
                    continue;
                }

                /* UNICAST_BIDIR SENDER: フォール スルーして受信者処理 (FIN/REJECT/DATA/PING) へ進む。 */
            }

            /*
             * ── 受信者ロール: FIN / REJECT / DATA / PING を処理 ──
             * 戻り値はヘルス チェック スレッドを起こす要否 (N:1 のときだけ 1) であり、1:1 では常に 0 のため、
             * 戻り値は明示的に破棄する。
             */
            (void)thread_recv_dispatch_packet(&svc_slot, &pkt, i, &sender_addr);
        }
    }

    return;
}

/*
 * ================================================================
 * TCP ストリーム受信スレッド
 * ================================================================
 */

/*
 * TCP ソケットが読み取り可能になるまで最大 wait_ms [ms] 待機する。
 * 戻り値: TCP_WAIT_READY = データあり、TCP_WAIT_TIMEOUT = タイムアウト、TCP_WAIT_ERROR = エラー。
 */
static tcp_wait_status tcp_wait_readable(cplat_socket fd, int wait_ms)
{
    int rc = cplat_socket_timed_wait_readable(fd, wait_ms, NULL);
    if (rc == CPLAT_OK)
    {
        return TCP_WAIT_READY;
    }
    if (rc == CPLAT_ERR_TIMEOUT)
    {
        return TCP_WAIT_TIMEOUT;
    }

    return TCP_WAIT_ERROR;
}

/*
 * TCP ソケットから正確に n [byte] 読み取る。
 * 戻り値: 成功時は POTR_OK、切断時 (recv が 0 を返した) は POTR_ERR_EOF、エラー時は POTR_ERR_IO。
 */
static int tcp_read_all(cplat_socket fd, uint8_t *buf, size_t n)
{
    cplat_error detail;
    int result = cplat_socket_recv_all(fd, buf, n, &detail);

    if (result == CPLAT_OK)
    {
        return POTR_OK;
    }

    if (result == CPLAT_ERR_EOF)
    {
        return POTR_ERR_EOF;
    }

    return POTR_ERR_IO;
}

/*
 * 受信した TCP パケットを解析し、パケット種別に応じた処理を行う。
 * 呼び出し元は tcp_recv_mutex を保持し、配信完了まで受信状態を直列化する。
 * 破棄するパケットは POTR_OK、ストリームを切断する解析失敗は POTR_ERR_PROTOCOL を返す。
 */
static int tcp_handle_packet(potr_context *ctx, thread_recv_slot *svc_slot, const uint8_t *buf,
                             uint16_t wire_payload_len, int path_idx, int replay_checked)
{
    potr_packet pkt;

    /* パケットを解析する。 */
    if (potr_internal_packet_parse(&pkt, buf, POTR_PACKET_HEADER_SIZE + wire_payload_len) != POTR_OK)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "tcp_recv[service_id=%" PRId64 "]: potr_internal_packet_parse failed",
                   ctx->service.service_id);
        return POTR_ERR_PROTOCOL;
    }

    /* service_id を確認する。 */
    if (pkt.service_id != ctx->service.service_id)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "tcp_recv[service_id=%" PRId64 "]: service_id mismatch (%" PRId64 ")",
                   ctx->service.service_id, pkt.service_id);
        return POTR_OK;
    }

    if (thread_recv_authenticate_packet(ctx, &pkt, buf, "tcp_recv", path_idx) != POTR_OK)
    {
        return POTR_OK;
    }

    /*
     * accept で採用したセッション以外は PING を含めて状態へ反映しない。
     * ウィンドウの開始通番は引き続き最初の DATA から決定する。
     */
    if (ctx->role == POTR_ROLE_RECEIVER && ctx->tcp_accepted_session_known)
    {
        cplat_timespec packet_session_ts;
        session_ts_from_hdr(pkt.session_tv_sec, pkt.session_tv_nsec, &packet_session_ts);

        if (pkt.session_id != ctx->tcp_accepted_session_id ||
            cplat_timespec_cmp(&packet_session_ts, &ctx->tcp_accepted_session_ts) != 0)
        {
            return POTR_OK;
        }
    }

    if (!replay_checked && thread_recv_replay_packet(&ctx->replay_window, &pkt, buf) != POTR_OK)
    {
        return POTR_OK;
    }

    /* パケット種別に応じた処理を行う。 */
    if (pkt.flags & POTR_FLAG_FLOW_CONTROL)
    {
        /* 相手からの抑制の要求または解除を、送信側の状態へ反映する。 */
        potr_internal_flow_control_on_packet(ctx, NULL, &pkt);
        return POTR_OK;
    }

    if (pkt.flags & POTR_FLAG_FIN_ACK)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "tcp_recv[service_id=%" PRId64 " path=%d]: FIN_ACK ack=%u",
                   ctx->service.service_id, path_idx, (unsigned)pkt.ack_num);
        thread_recv_fin_on_ack(ctx, pkt.ack_num);
        return POTR_OK;
    }
    else if (pkt.flags & POTR_FLAG_FIN)
    {
        uint32_t fin_target_seq = pkt.ack_num;
        int fin_action;

        cplat_local_lock_lock(ctx->recv_window_mutex);

        if (!thread_recv_session_adopt(svc_slot, &pkt))
        {
            cplat_local_lock_unlock(ctx->recv_window_mutex);
            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                       "tcp_recv[service_id=%" PRId64 " path=%d]: FIN session mismatch, ignored",
                       ctx->service.service_id, path_idx);
            return POTR_OK;
        }

        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                   "tcp_recv[service_id=%" PRId64 " path=%d]: FIN received (fin_target_seq=%u recv_next=%u)",
                   ctx->service.service_id, path_idx, (unsigned)fin_target_seq, (unsigned)ctx->recv_window.next_seq);

        fin_action = thread_recv_fin_on_packet(svc_slot, &pkt, &fin_target_seq);
        if (fin_action == THREAD_RECV_FIN_PENDING)
        {
            cplat_local_lock_unlock(ctx->recv_window_mutex);
            POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                       "tcp_recv[service_id=%" PRId64 " path=%d]: FIN pending (waiting for seq=%u)",
                       ctx->service.service_id, path_idx, (unsigned)fin_target_seq);
            return POTR_OK;
        }

        cplat_local_lock_unlock(ctx->recv_window_mutex);

        if (fin_action == THREAD_RECV_FIN_FIRE)
        {
            /*
             * 1:1 TCP の受信ループ終了に伴い相手の正常 FIN 受理を記録し、
             * FIN_ACK 返信後に受信スレッドを終了して接続管理スレッドへ経路後処理を委ねる。
             */
            cplat_atomic_store_i32(&ctx->tcp_peer_fin_received, 1, CPLAT_MEMORY_ORDER_RELEASE);
            thread_recv_fin_fire(svc_slot, fin_target_seq);
            return POTR_ERR_EOF;
        }

        return POTR_OK;
    }
    else if (pkt.flags & POTR_FLAG_PING)
    {
        int ping_state_changed;

        /* PING 受信: 最終受信時刻と受信状態を更新する。 */
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "tcp_recv[service_id=%" PRId64 " path=%d]: PING seq=%u",
                   ctx->service.service_id, path_idx, (unsigned)pkt.seq_num);
        uint64_t ping_time;

        if (cplat_clock_get_monotonic_ms(&ping_time) != CPLAT_OK)
        {
            cplat_atomic_store_i32(&ctx->fatal_error, POTR_ERR_IO, CPLAT_MEMORY_ORDER_RELEASE);
            potr_internal_send_queue_abort(&ctx->send_queue, POTR_ERR_IO);
            return POTR_ERR_IO;
        }

        cplat_local_lock_lock(ctx->tcp_state_mutex);
        cplat_atomic_store_u64(&ctx->tcp_last_ping_recv_ms[path_idx], ping_time, CPLAT_MEMORY_ORDER_RELAXED);
        ping_state_changed = thread_recv_set_path_ping_state(&ctx->path_ping_state[path_idx], POTR_PING_STATE_NORMAL);

        if (pkt.payload_len >= POTR_MAX_PATH && pkt.payload != NULL)
        {
            thread_recv_apply_remote_path_ping_state(ctx->remote_path_ping_state, pkt.payload, POTR_MAX_PATH);
        }

        thread_recv_sync_path_state(svc_slot);
        cplat_local_lock_unlock(ctx->tcp_state_mutex);
        thread_recv_health_wake_tcp(ctx, path_idx, ping_state_changed);
    }
    else if (pkt.flags & POTR_FLAG_DATA)
    {
        /* セッション照合と recv_window への投入を行う (recv_window_mutex で保護)。 */
        {
            uint32_t fin_target_seq = 0U;
            int should_fire_fin = 0;
            int pushed;

            cplat_local_lock_lock(ctx->recv_window_mutex);

            if (!thread_recv_session_adopt(svc_slot, &pkt))
            {
                cplat_local_lock_unlock(ctx->recv_window_mutex);
                POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                           "tcp_recv[service_id=%" PRId64 " path=%d]: DATA session mismatch, ignored",
                           ctx->service.service_id, path_idx);
                return POTR_OK;
            }

            pushed = potr_internal_window_recv_push(&ctx->recv_window, &pkt);

            /*
             * 通番の差を符号なしで求め、半周以上先を「ウィンドウ先頭より前」と判定する。
             * 先頭より前は、他の path で受信済みの重複である。
             */
            if (pushed != POTR_OK && (uint32_t)(pkt.seq_num - ctx->recv_window.base_seq) < POTR_SEQNUM_HALF_SPACE)
            {
                uint32_t recv_base = ctx->recv_window.base_seq;

                cplat_local_lock_unlock(ctx->recv_window_mutex);

                /*
                 * ウィンドウより先の通番は、受信していない通番があることを示す。
                 * TCP は欠番を再送で補えないため、黙って破棄すると以降の受信が止まる。
                 * ストリームを切断し、再接続で受信状態を回復させる。
                 */
                POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                           "tcp_recv[service_id=%" PRId64
                           " path=%d]: DATA seq=%u beyond window (base=%u), disconnecting",
                           ctx->service.service_id, path_idx, (unsigned)pkt.seq_num, (unsigned)recv_base);
                return POTR_ERR_PROTOCOL;
            }

            cplat_local_lock_unlock(ctx->recv_window_mutex);

            if (pushed != POTR_OK)
            {
                /* 他の path で受信済みの重複パケットのためスキップする。 */
                POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                           "tcp_recv[service_id=%" PRId64 " path=%d]: DATA seq=%u duplicate, skipped",
                           ctx->service.service_id, path_idx, (unsigned)pkt.seq_num);
                return POTR_OK;
            }

            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "tcp_recv[service_id=%" PRId64 " path=%d]: DATA seq=%u payload=%u",
                       ctx->service.service_id, path_idx, (unsigned)pkt.seq_num, (unsigned)pkt.payload_len);

            /* 順序整列済みパケットをポップして配信する。 */
            cplat_local_lock_lock(ctx->recv_window_mutex);

            {
                potr_packet out;

                while (potr_internal_window_recv_pop(&ctx->recv_window, &out) == POTR_OK)
                {
                    size_t offset = 0;
                    potr_packet elem;
                    cplat_local_lock_unlock(ctx->recv_window_mutex);

                    svc_slot->packet_expanded_size = 0U;
                    while (potr_internal_packet_unpack_next(&out, &offset, &elem) == POTR_OK)
                    {
                        thread_recv_slot_deliver_payload_elem(svc_slot, &elem);
                    }

                    /*
                     * 通知キューの容量を超えた場合は、DATA を破棄したため、ストリームを切断する。
                     * TCP は破棄した DATA を再送で補えない。ほかの path も、次の DATA の配信で同じ判定になり切断する。
                     */
                    if (svc_slot->flow_overflow != 0)
                    {
                        svc_slot->flow_overflow = 0;
                        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                                   "tcp_recv[service_id=%" PRId64 " path=%d]: notification capacity exceeded, disconnecting",
                                   ctx->service.service_id, path_idx);
                        return POTR_ERR_PROTOCOL;
                    }

                    /*
                     * 順序どおりに受信した DATA パケットを 1 個処理するたびに、送出上限の更新と、
                     * 送出上限まで送り終えた相手への通知を判定する。呼び出し元が tcp_recv_mutex を保持している。
                     */
                    potr_internal_flow_control_on_received(ctx, POTR_PEER_NA, *svc_slot->peer_session_id,
                                                           svc_slot->peer_session_ts, out.seq_num + 1U, 1,
                                                           svc_slot->packet_expanded_size);

                    cplat_local_lock_lock(ctx->recv_window_mutex);
                }

                if (thread_recv_fin_pending_reached(svc_slot, &fin_target_seq))
                {
                    should_fire_fin = 1;
                }
            }

            cplat_local_lock_unlock(ctx->recv_window_mutex);

            if (should_fire_fin)
            {
                /* 保留していた FIN を発動したため、相手 FIN 受理を記録して受信ループを終了する。 */
                cplat_atomic_store_i32(&ctx->tcp_peer_fin_received, 1, CPLAT_MEMORY_ORDER_RELEASE);
                thread_recv_fin_fire(svc_slot, fin_target_seq);
                return POTR_ERR_EOF;
            }
        }
    }

    return POTR_OK;
}

/* TCP ストリーム受信スレッド本体 (path ごと) */
static void tcp_recv_thread_func(void *arg)
{
    potr_internal_path_thread_arg *rarg = (potr_internal_path_thread_arg *)arg;
    potr_context *ctx = rarg->ctx;
    int path_idx = rarg->path_idx;
    uint8_t *buf = ctx->tcp_recv_buf[path_idx]; /* POTR_PACKET_HEADER_SIZE + max_payload [byte] */
    cplat_socket fd;
    thread_recv_slot svc_slot; /* TCP は 1:1 モードのみ */

    thread_recv_slot_init_ctx(&svc_slot, ctx);

    /*
     * PING 受信タイムアウト監視を使用するか判定する。
     * TCP は bootstrap PING 往復だけでも CONNECTED できるが、受信タイムアウト監視は
     * 定周期 PING を送信する構成でのみ有効とする。
     */
    int use_recv_timeout = (ctx->health_interval_ms > 0 && ctx->health_timeout_ms > 0);

    /* フロー制御が有効な受信側は、抑制の要求を再送周期ごとに再送するため、受信をポーリングで待つ。 */
    int use_flow_tick = (ctx->flow_control != NULL);

    /* ポーリング間隔: 1 [s] 単位でチェックし、health_timeout_ms を超えないようにする。 */
    uint32_t poll_ms;
    const char *recv_timeout_label;

    if (use_recv_timeout)
    {
        if (ctx->health_timeout_ms < RECV_TCP_POLL_INTERVAL_MS)
        {
            poll_ms = ctx->health_timeout_ms;
        }
        else
        {
            poll_ms = RECV_TCP_POLL_INTERVAL_MS;
        }
    }
    else
    {
        poll_ms = 0U;
    }

    if (use_recv_timeout)
    {
        recv_timeout_label = "enabled";
    }
    else
    {
        recv_timeout_label = "disabled";
    }

    if (use_flow_tick != 0 && (use_recv_timeout == 0 || ctx->flow_control_config.request_interval_ms < poll_ms))
    {
        poll_ms = ctx->flow_control_config.request_interval_ms;
    }

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "tcp_recv[service_id=%" PRId64 " path=%d]: starting (recv_timeout=%s)",
               ctx->service.service_id, path_idx, recv_timeout_label);

    while (cplat_atomic_load_i32(&ctx->running[path_idx], CPLAT_MEMORY_ORDER_ACQUIRE) != 0 &&
           cplat_atomic_load_i32(&ctx->fatal_error, CPLAT_MEMORY_ORDER_ACQUIRE) == POTR_OK)
    {
        uint16_t wire_payload_len;
        int r;

        fd = ctx->tcp_conn_fd[path_idx];
        if (fd == CPLAT_INVALID_SOCKET)
        {
            break;
        }

        /*
         * ── 先読みバッファー処理 ──
         * accept スレッドが session 判定のために読み取ったパケットが残っている場合、
         * ソケットからの読み取りをスキップしてバッファーの内容をそのまま使用する。
         * accept スレッドは recv スレッド起動前に書き込みを完了しているため mutex 不要である。
         */
        int replay_checked = 0;

        if (ctx->tcp_first_pkt_len[path_idx] > 0)
        {
            /* accept スレッドの先読みバッファーを recv バッファーにコピーする。 */
            size_t first_len = ctx->tcp_first_pkt_len[path_idx];
            replay_checked = 1;
            memcpy(buf, ctx->tcp_first_pkt_buf[path_idx], first_len);
            ctx->tcp_first_pkt_len[path_idx] = 0; /* 先読みバッファーをクリア */

            {
                uint16_t wpl;
                memcpy(&wpl, buf + POTR_PACKET_PAYLOAD_LEN_OFFSET, sizeof(wpl));
                wire_payload_len = cplat_ntoh16(wpl);
            }
        }
        else
        {
            /*
             * タイムアウト付きポーリングで PING 受信を監視する。
             * データが到着するまで poll_ms 待機し、タイムアウト時は PING 受信時刻を確認する。
             */
            if (use_recv_timeout != 0 || use_flow_tick != 0)
            {
                /* poll_ms は health_timeout または抑制の要求の再送周期から算出したポーリング間隔。INT_MAX 以下。 */
                int readable = tcp_wait_readable(fd, (int)poll_ms);
                if (cplat_atomic_load_i32(&ctx->running[path_idx], CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
                {
                    break;
                }
                if (readable < 0)
                {
                    break; /* エラー */
                }

                if (readable == 0)
                {
                    /* ポーリング タイムアウト: PING 受信時刻を確認する。 */
                    uint64_t last =
                        cplat_atomic_load_u64(&ctx->tcp_last_ping_recv_ms[path_idx], CPLAT_MEMORY_ORDER_RELAXED);
                    uint64_t now;

                    if (cplat_clock_get_monotonic_ms(&now) != CPLAT_OK)
                    {
                        cplat_atomic_store_i32(&ctx->fatal_error, POTR_ERR_IO, CPLAT_MEMORY_ORDER_RELEASE);
                        potr_internal_send_queue_abort(&ctx->send_queue, POTR_ERR_IO);
                        break;
                    }

                    if (use_flow_tick != 0)
                    {
                        potr_internal_flow_control_tick(ctx, now);
                    }

                    /* 受信タイムアウト監視を使わない構成では、抑制の要求の再送だけを行う。 */
                    if (use_recv_timeout == 0)
                    {
                        continue;
                    }

                    uint64_t elapsed = now - last;
                    if (last > 0 && elapsed > (uint64_t)ctx->health_timeout_ms)
                    {
                        int ping_state_changed;

                        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                                   "tcp_recv[service_id=%" PRId64 " path=%d]: PING timeout"
                                   " (%llu ms), disconnecting",
                                   ctx->service.service_id, path_idx, (unsigned long long)elapsed);
                        cplat_local_lock_lock(ctx->tcp_state_mutex);
                        ping_state_changed =
                            thread_recv_set_path_ping_state(&ctx->path_ping_state[path_idx], POTR_PING_STATE_ABNORMAL);
                        thread_recv_sync_path_state(&svc_slot);
                        cplat_local_lock_unlock(ctx->tcp_state_mutex);
                        thread_recv_health_wake_tcp(ctx, path_idx, ping_state_changed);
                        break;
                    }

                    continue;
                }

                /* readable == 1: データあり。以降の tcp_read_all へ進む。 */
            }

            /* 固定長ヘッダーを読み取る。 */
            r = tcp_read_all(fd, buf, POTR_PACKET_HEADER_SIZE);
            if (r != POTR_OK)
            {
                break; /* 切断 or エラー */
            }

            /* payload_len をヘッダー末尾の offset 34 から抽出する。 */
            {
                uint16_t wpl;
                memcpy(&wpl, buf + POTR_PACKET_PAYLOAD_LEN_OFFSET, sizeof(wpl));
                wire_payload_len = cplat_ntoh16(wpl);
            }

            /* ペイロード長を検証する。 */
            if ((size_t)wire_payload_len > ctx->global.max_payload)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                           "tcp_recv[service_id=%" PRId64 "]: oversized payload %u > max %u,"
                           " disconnecting",
                           ctx->service.service_id, (unsigned)wire_payload_len, (unsigned)ctx->global.max_payload);
                break;
            }

            /* ペイロードを読み取る。 */
            if (wire_payload_len > 0)
            {
                r = tcp_read_all(fd, buf + POTR_PACKET_HEADER_SIZE, wire_payload_len);
                if (r != POTR_OK)
                {
                    break;
                }
            }
        } /* else (先読みバッファーなし) ここまで */

        cplat_local_lock_lock(ctx->tcp_recv_mutex);

        if (cplat_atomic_load_i32(&ctx->running[path_idx], CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
        {
            r = tcp_handle_packet(ctx, &svc_slot, buf, wire_payload_len, path_idx, replay_checked);
        }
        else
        {
            r = POTR_ERR_CANCELED;
        }

        cplat_local_lock_unlock(ctx->tcp_recv_mutex);

        if (r != POTR_OK)
        {
            break;
        }
    }

    /* 接続断処理: DISCONNECTED イベントは connect スレッドが tcp_active_paths == 0 時に発火する。 */
    cplat_atomic_store_i32(&ctx->running[path_idx], 0, CPLAT_MEMORY_ORDER_RELEASE);

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "tcp_recv[service_id=%" PRId64 " path=%d]: exited", ctx->service.service_id,
               path_idx);

    return;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_comm_recv_thread_start(potr_context *ctx)
{
    if (ctx == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    cplat_atomic_store_i32(&ctx->running[0], 1, CPLAT_MEMORY_ORDER_RELEASE);

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv_thread[service_id=%" PRId64 "]: starting", ctx->service.service_id);

    if (cplat_thread_create(&ctx->recv_thread[0], recv_thread_func, ctx) != CPLAT_OK)
    {
        cplat_atomic_store_i32(&ctx->running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "recv_thread[service_id=%" PRId64 "]: thread create failed",
                   ctx->service.service_id);

        /* cplat のスレッド生成失敗には、porter の分類へ変換できる詳細コードがない。 */
        return POTR_ERR_UNKNOWN;
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_comm_recv_thread_stop(potr_context *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    cplat_atomic_store_i32(&ctx->running[0], 0, CPLAT_MEMORY_ORDER_RELEASE);

    {
        int i;

        for (i = 0; i < ctx->n_path; i++)
        {
            if (ctx->sock[i] != CPLAT_INVALID_SOCKET)
            {
                /*
                 * 停止の処理であり、未接続の UDP ソケットでは失敗を返すが、受信スレッドは待機の上限ごとに停止の指示を
                 * 読み直し、失敗しても停止は有限の時間で終わるため、戻り値は明示的に破棄する。
                 */
                (void)cplat_socket_shutdown_receive(&ctx->sock[i], NULL);
            }
        }
    }

    if (ctx->recv_thread[0] != NULL)
    {
        cplat_thread_join(ctx->recv_thread[0]);

        ctx->recv_thread[0] = NULL;
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_tcp_recv_thread_start(potr_context *ctx, int path_idx)
{
    if (ctx == NULL || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    cplat_atomic_store_i32(&ctx->running[path_idx], 1, CPLAT_MEMORY_ORDER_RELEASE);

    ctx->tcp_recv_args[path_idx].ctx = ctx;
    ctx->tcp_recv_args[path_idx].path_idx = path_idx;

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "tcp_recv_thread[service_id=%" PRId64 " path=%d]: starting",
               ctx->service.service_id, path_idx);

    if (cplat_thread_create(&ctx->recv_thread[path_idx], tcp_recv_thread_func, &ctx->tcp_recv_args[path_idx]) !=
        CPLAT_OK)
    {
        cplat_atomic_store_i32(&ctx->running[path_idx], 0, CPLAT_MEMORY_ORDER_RELEASE);
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "tcp_recv_thread[service_id=%" PRId64 " path=%d]: thread create failed",
                   ctx->service.service_id, path_idx);

        /* cplat のスレッド生成失敗には、porter の分類へ変換できる詳細コードがない。 */
        return POTR_ERR_UNKNOWN;
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_tcp_recv_thread_stop(potr_context *ctx, int path_idx)
{
    if (ctx == NULL || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH)
    {
        return;
    }

    cplat_atomic_store_i32(&ctx->running[path_idx], 0, CPLAT_MEMORY_ORDER_RELEASE);

    if (ctx->recv_thread[path_idx] != NULL)
    {
        cplat_thread_join(ctx->recv_thread[path_idx]);

        ctx->recv_thread[path_idx] = NULL;
    }
}
