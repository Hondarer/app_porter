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

/* N:1: poll タイムアウト時にヘルスチェック タイムアウトを確認する */
static void n1_check_health_timeout(potr_context *ctx)
{
    cplat_timespec now_ts;
    int i;

    cplat_get_monotonic(&now_ts);
    int k;
    int should_wake_health = 0;

    if (ctx->health_timeout_ms == 0)
        return;

    cplat_local_lock_lock(ctx->peers_mutex, CPLAT_SYNC_WAIT_FOREVER);

    for (i = 0; i < ctx->max_peers; i++)
    {
        int64_t elapsed_ms;
        int path_state_changed = 0;

        if (!ctx->peers[i].active)
            continue;
        if (!ctx->peers[i].health_alive)
            continue;

        /* パス単位のタイムアウト: 不通パスを dest_addr から削除する */
        for (k = 0; k < (int)POTR_MAX_PATH; k++)
        {
            int64_t path_elapsed;

            if (potr_endpoint_is_unset(&ctx->peers[i].dest_addr[k]))
                continue; /* 未使用 */
            if (ctx->peers[i].path_last_recv_ts[k].tv_sec == 0)
                continue; /* 初回受信前 */

            path_elapsed = cplat_timespec_diff_ms(&now_ts, &ctx->peers[i].path_last_recv_ts[k]);

            if (path_elapsed >= (int64_t)ctx->health_timeout_ms)
            {
                int state_changed = thread_recv_set_path_ping_state(&ctx->peers[i].path_ping_state[k], POTR_PING_STATE_ABNORMAL);
                should_wake_health |= state_changed;
                path_state_changed |= state_changed;
                potr_internal_peer_path_clear(ctx, &ctx->peers[i], k);
            }
        }

        if (path_state_changed)
        {
            thread_recv_slot peer_slot;

            thread_recv_slot_init_peer(&peer_slot, ctx, &ctx->peers[i]);
            thread_recv_sync_path_state(&peer_slot);
        }

        /* ピア単位のタイムアウト: 全パス消滅、または最終受信から切断判定 */
        if (ctx->peers[i].last_recv_ts.tv_sec == 0)
            continue;

        elapsed_ms = cplat_timespec_diff_ms(&now_ts, &ctx->peers[i].last_recv_ts);

        if (elapsed_ms >= (int64_t)ctx->health_timeout_ms)
        {
            potr_peer_id dead_id = ctx->peers[i].peer_id;

            POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                       "recv[service_id=%" PRId64 "]: peer=%u DISCONNECTED (timeout %lldms)", ctx->service.service_id,
                       (unsigned)dead_id, (long long)elapsed_ms);

            memset((void *)ctx->peers[i].remote_path_ping_state, 0, sizeof(ctx->peers[i].remote_path_ping_state));
            thread_recv_disconnect_peer_all_paths(ctx, &ctx->peers[i]);
            potr_internal_peer_free(ctx, &ctx->peers[i]);
        }
    }

    cplat_local_lock_unlock(ctx->peers_mutex);

    if (should_wake_health)
    {
        potr_internal_health_thread_wake(ctx);
    }
}

/* ================================================================
 * N:1 モード専用ここまで
 * ================================================================ */

/* PING ペイロード (相手端のパス受信状態ベクトル) を remote_path_ping_state[] に取り込む。
 * 入力バイトが POTR_PING_STATE_UNDEFINED の場合は当該スロットを更新しない。
 * これは bootstrap PING や送信競合で stale な UNDEFINED が後着した場合に、確立済みの
 * NORMAL を後退させて瞬間 DISCONNECTED を起こさないための防御。
 * NORMAL / ABNORMAL への遷移のみ反映する。 */
static void apply_remote_path_ping_state_payload(uint8_t *dst, const uint8_t *src, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        if (src[i] != POTR_PING_STATE_UNDEFINED)
        {
            dst[i] = src[i];
        }
    }
}

static void wake_udp_interrupt_ping_if_needed(potr_context *ctx, int state_changed)
{
    if (state_changed && ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
    {
        potr_internal_health_thread_wake(ctx);
    }
}

static void wake_tcp_interrupt_ping_if_needed(potr_context *ctx, int path_idx, int state_changed)
{
    if (state_changed && potr_is_tcp_type(ctx->service.type))
    {
        if (ctx->health_interval_ms > 0 && ctx->health_running[path_idx])
        {
            /*
             * TCP は path ごとに health スレッドを持つが、PING ペイロードは全 path の
             * 受信状態ベクトルを運ぶ。変化した path 自身が送れない場合でも別 path から
             * 更新済みベクトルを伝播できるよう、全 health スレッドを即時起床させる。
             */
            potr_internal_tcp_health_thread_wake_all(ctx);
        }
        else
        {
            (void)potr_internal_tcp_send_ping_now(ctx, path_idx);
        }
    }
}

/* タイムアウト時に経過時間を確認し、必要なら peer_port クリアと DISCONNECTED イベントを発火する */
static void check_health_timeout(thread_recv_slot *slot)
{
    potr_context *ctx = slot->ctx;
    cplat_timespec now_ts;
    int i;
    int should_wake_health = 0;
    int path_state_changed = 0;

    if (ctx->health_timeout_ms == 0)
        return;

    cplat_get_monotonic(&now_ts);

    /* パスごとのタイムアウト: peer_port をクリア */
    for (i = 0; i < ctx->n_path; i++)
    {
        int64_t elapsed_ms;

        if (ctx->path_last_recv_ts[i].tv_sec == 0)
            continue;

        elapsed_ms = cplat_timespec_diff_ms(&now_ts, &ctx->path_last_recv_ts[i]);

        if (elapsed_ms >= (int64_t)ctx->health_timeout_ms)
        {
            int state_changed = thread_recv_set_path_ping_state(&ctx->path_ping_state[i], POTR_PING_STATE_ABNORMAL);
            should_wake_health |= state_changed;
            path_state_changed |= state_changed;
            ctx->peer_port[i] = 0;
            ctx->path_last_recv_ts[i].tv_sec = 0;
            /* unicast_bidir で動的学習したアドレス・ポートをリセットする (再接続を許可) */
            if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
            {
                if (ctx->service.src_addr[0][0] == '\0')
                {
                    /* 動的 1:1 RECEIVER: 学習した送信元 IP をリセット */
                    ctx->dest_addr[i].address = 0;
                }
                if (ctx->service.src_port == 0)
                {
                    /* エフェメラル ポート動的学習: ポートをリセット */
                    ctx->dest_addr[i].port = 0;
                }
            }
        }
    }

    if (path_state_changed)
    {
        thread_recv_sync_path_state(slot);
    }

    /* 全体の health_alive 判定 */
    if (!ctx->health_alive || ctx->last_recv_ts.tv_sec == 0)
        return;

    {
        int64_t elapsed_ms;
        elapsed_ms = cplat_timespec_diff_ms(&now_ts, &ctx->last_recv_ts);

        if (elapsed_ms >= (int64_t)ctx->health_timeout_ms)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                       "recv[service_id=%" PRId64 "]: DISCONNECTED (timeout %lldms >= %ums)", ctx->service.service_id,
                       (long long)elapsed_ms, (unsigned)ctx->health_timeout_ms);
            /* FIN と同様にセッション状態をリセットして次の接続を受け入れ可能にする。
               peer_session_known をクリアすることで、送信者が同一セッションのまま
               復帰した場合でも potr_internal_window_init を経由して受信ウィンドウを初期化し、
               前セッションの next_seq が gap スキャンに影響しないようにする。 */
            ctx->pending_fin = 0;
            ctx->fin_target_seq = 0;
            ctx->peer_session_known = 0;
            ctx->reorder_pending = 0;
            ctx->last_recv_ts.tv_sec = 0;
            /* 次の接続到来まで受信状態を不定に戻す */
            should_wake_health |=
                thread_recv_set_all_path_ping_states(ctx->path_ping_state, POTR_MAX_PATH, POTR_PING_STATE_UNDEFINED);
            memset((void *)ctx->remote_path_ping_state, 0, sizeof(ctx->remote_path_ping_state));
            thread_recv_disconnect_service_all_paths(ctx);
            potr_internal_window_init(&ctx->recv_window, 0, ctx->global.window_size, ctx->global.max_payload);
        }
    }

    wake_udp_interrupt_ping_if_needed(ctx, should_wake_health);
}

/* N:1 モード: 受信パケットをピアごとにディスパッチして処理する。
   wire_hdr は認証検証用の受信 wire 先頭 (ヘッダー部)。 */
static void n1_handle_packet(potr_context *ctx, potr_packet *pkt, const uint8_t *wire_hdr,
                             const cplat_ipv4_endpoint *sender_addr, int path_idx)
{
    potr_internal_peer_context *peer = NULL;
    int is_new_peer = 0;
    thread_recv_slot peer_slot;

    if (thread_recv_authenticate_packet(ctx, pkt, wire_hdr, "recv", -1) != POTR_OK)
    {
        return;
    }

    cplat_local_lock_lock(ctx->peers_mutex, CPLAT_SYNC_WAIT_FOREVER);

    /* session_triplet でピアを検索 */
    cplat_timespec pkt_session_ts;
    potr_session_ts_from_hdr(pkt->session_tv_sec, pkt->session_tv_nsec, &pkt_session_ts);
    peer = potr_internal_peer_find_by_session(ctx, pkt->session_id, &pkt_session_ts);

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
                potr_internal_window_init(&peer->recv_window, pkt->seq_num, ctx->global.window_size, ctx->global.max_payload);
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

    thread_recv_slot_init_peer(&peer_slot, ctx, peer);

    if (is_new_peer)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                   "recv[service_id=%" PRId64 "]: new peer=%u from %u.%u.%u.%u:%u (CONNECTED pending PING+NORMAL)",
                   ctx->service.service_id, (unsigned)peer->peer_id,
                   (unsigned)((cplat_ntoh32(sender_addr->address) >> 24) & 0xFF),
                   (unsigned)((cplat_ntoh32(sender_addr->address) >> 16) & 0xFF),
                   (unsigned)((cplat_ntoh32(sender_addr->address) >> 8) & 0xFF),
                   (unsigned)(cplat_ntoh32(sender_addr->address) & 0xFF),
                   (unsigned)cplat_ntoh16(sender_addr->port));
    }

    /* FIN: ピアの正常終了通知 */
    if (pkt->flags & POTR_FLAG_FIN)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                   "recv[service_id=%" PRId64 "]: peer=%u FIN received"
                   " (fin_target_seq=%u recv_next=%u)",
                   ctx->service.service_id, (unsigned)peer->peer_id, (unsigned)pkt->ack_num,
                   (unsigned)peer->recv_window.next_seq);

        {
            uint32_t fin_target_seq = 0U;
            int fin_action = thread_recv_fin_on_packet(&peer_slot, pkt, &fin_target_seq);

            if (fin_action == THREAD_RECV_FIN_PENDING)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                           "recv[service_id=%" PRId64 "]: peer=%u FIN pending (waiting for seq=%u)",
                           ctx->service.service_id, (unsigned)peer->peer_id, (unsigned)pkt->ack_num);
                cplat_local_lock_unlock(ctx->peers_mutex);
                return;
            }

            /* 即時: no-data FIN またはウィンドウ到達済み。 */
            if (fin_action == THREAD_RECV_FIN_FIRE)
            {
                thread_recv_fin_fire(&peer_slot, fin_target_seq);
            }
            cplat_local_lock_unlock(ctx->peers_mutex);
            return;
        }
    }

    if ((pkt->flags & POTR_FLAG_NACK) != 0)
    {
        thread_recv_window_on_nack(&peer_slot, pkt);
        cplat_local_lock_unlock(ctx->peers_mutex);
        return;
    }

    if ((pkt->flags & POTR_FLAG_REJECT) != 0)
    {
        thread_recv_window_on_reject(&peer_slot, pkt, path_idx, sender_addr);
        cplat_local_lock_unlock(ctx->peers_mutex);
        return;
    }

    /* DATA / PING */
    if (!(pkt->flags & (POTR_FLAG_DATA | POTR_FLAG_PING)))
    {
        cplat_local_lock_unlock(ctx->peers_mutex);
        return;
    }

    if (!thread_recv_session_adopt(&peer_slot, pkt))
    {
        cplat_local_lock_unlock(ctx->peers_mutex);
        return;
    }

    thread_recv_learn_sender_path(&peer_slot, path_idx, sender_addr);

    {
        const char *pkt_kind_str;
        if ((pkt->flags & POTR_FLAG_PING) != 0)
        {
            pkt_kind_str = "PING";
        }
        else
        {
            pkt_kind_str = "DATA";
        }
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: peer=%u %s seq=%u",
                   ctx->service.service_id, (unsigned)peer->peer_id, pkt_kind_str, (unsigned)pkt->seq_num);
    }

    if ((pkt->flags & POTR_FLAG_PING) != 0)
    {
        int ping_state_changed = thread_recv_update_path_health(&peer_slot, path_idx);

        if (pkt->payload_len >= POTR_MAX_PATH && pkt->payload != NULL)
        {
            apply_remote_path_ping_state_payload(peer->remote_path_ping_state, pkt->payload, POTR_MAX_PATH);
        }

        thread_recv_sync_path_state(&peer_slot);
        thread_recv_window_scan_ping_gap(&peer_slot, pkt);
        cplat_local_lock_unlock(ctx->peers_mutex);
        if (ping_state_changed)
        {
            potr_internal_health_thread_wake(ctx);
        }
        return;
    }

    thread_recv_window_accept_outer(&peer_slot, pkt, path_idx);
    cplat_local_lock_unlock(ctx->peers_mutex);
}

/* 送信者ロール: NACK のみ処理する。
   戻り値 1: パケットを消費した (呼び出し元は次のパケットへ)。
   戻り値 0: UNICAST_BIDIR SENDER のため受信者処理へフォール スルーする。 */
static int sender_handle_packet(thread_recv_slot *slot, const potr_packet *pkt)
{
    potr_context *ctx = slot->ctx;

    if (potr_is_raw_type(ctx->service.type))
    {
        return 1;
    }

    thread_recv_window_on_nack(slot, pkt);

    if (ctx->service.type != POTR_TYPE_UNICAST_BIDIR)
    {
        return 1;
    }

    return 0;
}

/* 受信者ロール: FIN / REJECT / DATA / PING を処理する (1:1 モード) */
static void receiver_handle_packet(thread_recv_slot *svc_slot, potr_packet *pkt, const cplat_ipv4_endpoint *sender_addr,
                                   int path_idx)
{
    potr_context *ctx = svc_slot->ctx;

    /* FIN: 送信者からの正常終了通知 */
    if (pkt->flags & POTR_FLAG_FIN)
    {
        if (!thread_recv_session_adopt(svc_slot, pkt))
        {
            return; /* 旧セッションの FIN → 無視 */
        }

        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                   "recv[service_id=%" PRId64 "]: FIN received (fin_target_seq=%u recv_next=%u)",
                   ctx->service.service_id, (unsigned)pkt->ack_num, (unsigned)ctx->recv_window.next_seq);

        /* target 付き FIN かつ recv_window.next_seq が目標値に未到達: FIN をペンディング。
         * 後着の DATA がウィンドウを満たした時点で thread_recv_fin_fire() が呼び出される。
         * セッション リセットを遅延することで後着 DATA を引き続き受け入れ可能にする。 */
        {
            uint32_t fin_target_seq = 0U;
            int fin_action = thread_recv_fin_on_packet(svc_slot, pkt, &fin_target_seq);

            if (fin_action == THREAD_RECV_FIN_PENDING)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "recv[service_id=%" PRId64 "]: FIN pending (waiting for seq=%u)",
                           ctx->service.service_id, (unsigned)pkt->ack_num);
                return;
            }

            /* 即時: no-data FIN またはウィンドウ到達済み。 */
            if (fin_action == THREAD_RECV_FIN_FIRE)
            {
                thread_recv_fin_fire(svc_slot, fin_target_seq);
            }
            return;
        }
    }

    if ((pkt->flags & POTR_FLAG_REJECT) != 0)
    {
        thread_recv_window_on_reject(svc_slot, pkt, path_idx, sender_addr);
        return;
    }

    if ((pkt->flags & (POTR_FLAG_DATA | POTR_FLAG_PING)) == 0)
    {
        return;
    }

    if (thread_recv_session_adopt(svc_slot, pkt) == 0)
    {
        return;
    }

    thread_recv_learn_sender_path(svc_slot, path_idx, sender_addr);

    {
        const char *pkt_kind_str;
        if ((pkt->flags & POTR_FLAG_PING) != 0)
        {
            pkt_kind_str = "PING";
        }
        else
        {
            pkt_kind_str = "DATA";
        }
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: %s seq=%u path=%d",
                   ctx->service.service_id, pkt_kind_str, (unsigned)pkt->seq_num, path_idx);
    }

    if ((pkt->flags & POTR_FLAG_PING) != 0)
    {
        int ping_state_changed = thread_recv_update_path_health(svc_slot, path_idx);

        if (pkt->payload_len >= POTR_MAX_PATH && pkt->payload != NULL)
        {
            apply_remote_path_ping_state_payload(ctx->remote_path_ping_state, pkt->payload, POTR_MAX_PATH);
        }

        if (potr_is_raw_type(ctx->service.type))
        {
            thread_recv_window_scan_ping_gap(svc_slot, pkt);
            thread_recv_sync_path_state(svc_slot);
        }
        else
        {
            if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
            {
                wake_udp_interrupt_ping_if_needed(ctx, ping_state_changed);
            }
            thread_recv_sync_path_state(svc_slot);
            thread_recv_window_scan_ping_gap(svc_slot, pkt);
        }
    }
    else
    {
        thread_recv_window_accept_outer(svc_slot, pkt, path_idx);
    }
}

/* 受信スレッド本体 */
static void recv_thread_func(void *arg)
{
    potr_context *ctx = (potr_context *)arg;
    uint8_t *buf = ctx->recv_buf; /* PACKET_HEADER_SIZE + max_payload バイト */
    potr_packet pkt;
    cplat_ipv4_endpoint sender_addr;
    uint32_t poll_ms;
    thread_recv_slot svc_slot; /* 1:1 モード用スロット (フィールド位置は不変のため 1 回だけ構成) */

    thread_recv_slot_init_ctx(&svc_slot, ctx);

    if ((ctx->role == POTR_ROLE_RECEIVER || ctx->service.type == POTR_TYPE_UNICAST_BIDIR) &&
        ctx->health_timeout_ms > 0U)
    {
        poll_ms = ctx->health_timeout_ms / 3U;
        if (poll_ms < 100U)
            poll_ms = 100U;
    }
    else
    {
        poll_ms = 500U;
    }
    /* reorder_timeout_ms が有効な場合は poll 間隔を短縮してタイムアウト精度を確保する */
    if ((ctx->role == POTR_ROLE_RECEIVER || ctx->service.type == POTR_TYPE_UNICAST_BIDIR) &&
        ctx->global.reorder_timeout_ms > 0U)
    {
        if (ctx->global.reorder_timeout_ms < poll_ms)
        {
            poll_ms = ctx->global.reorder_timeout_ms;
        }
    }

    while (ctx->running[0])
    {
        unsigned char ready[POTR_MAX_PATH];
        cplat_error detail;
        int poll_result;
        int i;

        poll_result = cplat_socket_wait_readable_multi(ctx->sock, (size_t)ctx->n_path, (int)poll_ms, ready, &detail);
        if (poll_result != CPLAT_OK)
        {
            if (!ctx->running[0])
                break;
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "recv[service_id=%" PRId64 "]: socket poll failed: rc=%d",
                       ctx->service.service_id, poll_result);
            break;
        }

        int has_ready = 0;
        for (i = 0; i < ctx->n_path; ++i)
        {
            if (ready[i] != 0U)
            {
                has_ready = 1;
                break;
            }
        }
        if (!has_ready)
        {
            if (ctx->is_multi_peer)
            {
                n1_check_health_timeout(ctx);
            }
            else if (ctx->role == POTR_ROLE_RECEIVER || ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
            {
                check_health_timeout(&svc_slot);
                if (ctx->global.reorder_timeout_ms > 0U)
                {
                    thread_recv_window_check_reorder_timeout(&svc_slot);
                }
            }
            continue;
        }

        for (i = 0; i < ctx->n_path; i++)
        {
            size_t recv_len = 0;
            int recv_result;

            if (ctx->sock[i] == CPLAT_INVALID_SOCKET)
                continue;
            if (ready[i] == 0U)
                continue;

            memset(&sender_addr, 0, sizeof(sender_addr));

            recv_result = cplat_socket_recvfrom(ctx->sock[i], buf, PACKET_HEADER_SIZE + ctx->global.max_payload,
                                                   &sender_addr, &recv_len, NULL);
            if (recv_result != CPLAT_OK || recv_len == 0U)
            {
                if (!ctx->running[0])
                    break; /* 正常終了: ソケット クローズによる割り込み */
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
                continue;

            /* ── N:1 モード: ピアごとにディスパッチ ── */
            if (ctx->is_multi_peer)
            {
                n1_handle_packet(ctx, &pkt, buf, &sender_addr, i);
                continue;
            }

            if (thread_recv_authenticate_packet(ctx, &pkt, buf, "recv", -1) != POTR_OK)
                continue;

            /* ── 送信者ロール: NACK のみ処理 ── */
            if (ctx->role == POTR_ROLE_SENDER)
            {
                if (sender_handle_packet(&svc_slot, &pkt))
                {
                    continue;
                }
                /* UNICAST_BIDIR SENDER: フォール スルーして受信者処理 (FIN/REJECT/DATA/PING) へ */
            }

            /* ── 受信者ロール: FIN / REJECT / DATA / PING を処理 ── */
            receiver_handle_packet(&svc_slot, &pkt, &sender_addr, i);
        }
    }

    return;
}

/* ================================================================
 * TCP ストリーム受信スレッド
 * ================================================================ */

/* TCP ソケットが読み取り可能になるまで最大 wait_ms ミリ秒待機する。
 * 戻り値: 1 = データあり、0 = タイムアウト、-1 = エラー。 */
static int tcp_wait_readable(cplat_socket fd, int wait_ms)
{
    int ready = 0;

    if (cplat_socket_wait_readable(fd, wait_ms, &ready, NULL) != CPLAT_OK)
    {
        return -1;
    }
    return ready;
}

/* TCP ソケットから正確に n バイト読み取る。
 * 戻り値: 成功時は POTR_OK、切断時 (recv が 0 を返した) は POTR_ERR_EOF、エラー時は POTR_ERR_IO。 */
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

/* 呼び出し元は tcp_recv_mutex を保持し、配信完了まで受信状態を直列化する。
 * 破棄するパケットは POTR_OK、ストリームを切断する解析失敗は POTR_ERR_PROTOCOL。 */
static int tcp_handle_packet(potr_context *ctx, thread_recv_slot *svc_slot, const uint8_t *buf,
                             uint16_t wire_payload_len, int path_idx)
{
    potr_packet pkt;

    /* 5. パケット解析 */
    if (potr_internal_packet_parse(&pkt, buf, PACKET_HEADER_SIZE + wire_payload_len) != POTR_OK)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "tcp_recv[service_id=%" PRId64 "]: potr_internal_packet_parse failed",
                   ctx->service.service_id);
        return POTR_ERR_PROTOCOL;
    }

    /* 6. service_id チェック */
    if (pkt.service_id != ctx->service.service_id)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                   "tcp_recv[service_id=%" PRId64 "]: service_id mismatch (%" PRId64 ")", ctx->service.service_id,
                   pkt.service_id);
        return POTR_OK;
    }

    if (thread_recv_authenticate_packet(ctx, &pkt, buf, "tcp_recv", path_idx) != POTR_OK)
        return POTR_OK;

    /* accept で採用したセッション以外は PING を含めて状態へ反映しない。
     * ウィンドウの開始通番は引き続き最初の DATA から決定する。 */
    if (ctx->role == POTR_ROLE_RECEIVER && ctx->tcp_accepted_session_known)
    {
        cplat_timespec packet_session_ts;
        potr_session_ts_from_hdr(pkt.session_tv_sec, pkt.session_tv_nsec, &packet_session_ts);
        if (pkt.session_id != ctx->tcp_accepted_session_id ||
            cplat_timespec_cmp(&packet_session_ts, &ctx->tcp_accepted_session_ts) != 0)
        {
            return POTR_OK;
        }
    }

    /* 8. パケット種別処理 */
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

        cplat_local_lock_lock(ctx->recv_window_mutex, CPLAT_SYNC_WAIT_FOREVER);
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
                   ctx->service.service_id, path_idx, (unsigned)fin_target_seq,
                   (unsigned)ctx->recv_window.next_seq);

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
            thread_recv_fin_fire(svc_slot, fin_target_seq);
        }
        return POTR_OK;
    }
    else if (pkt.flags & POTR_FLAG_PING)
    {
        int ping_state_changed;

        /* PING 受信: 最終受信時刻と受信状態を更新する。*/
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "tcp_recv[service_id=%" PRId64 " path=%d]: PING seq=%u",
                   ctx->service.service_id, path_idx, (unsigned)pkt.seq_num);
        cplat_local_lock_lock(ctx->tcp_state_mutex, CPLAT_SYNC_WAIT_FOREVER);
        ctx->tcp_last_ping_recv_ms[path_idx] = cplat_get_monotonic_ms();
        ping_state_changed = thread_recv_set_path_ping_state(&ctx->path_ping_state[path_idx], POTR_PING_STATE_NORMAL);
        if (pkt.payload_len >= POTR_MAX_PATH && pkt.payload != NULL)
        {
            apply_remote_path_ping_state_payload(ctx->remote_path_ping_state, pkt.payload, POTR_MAX_PATH);
        }
        thread_recv_sync_path_state(svc_slot);
        cplat_local_lock_unlock(ctx->tcp_state_mutex);
        wake_tcp_interrupt_ping_if_needed(ctx, path_idx, ping_state_changed);
    }
    else if (pkt.flags & POTR_FLAG_DATA)
    {
        /* セッション照合 + recv_window への投入 (recv_window_mutex で保護) */
        {
            uint32_t fin_target_seq = 0U;
            int should_fire_fin = 0;
            int pushed;

            cplat_local_lock_lock(ctx->recv_window_mutex, CPLAT_SYNC_WAIT_FOREVER);
            if (!thread_recv_session_adopt(svc_slot, &pkt))
            {
                cplat_local_lock_unlock(ctx->recv_window_mutex);
                POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                           "tcp_recv[service_id=%" PRId64 " path=%d]: DATA session mismatch, ignored",
                           ctx->service.service_id, path_idx);
                return POTR_OK;
            }
            pushed = potr_internal_window_recv_push(&ctx->recv_window, &pkt);
            cplat_local_lock_unlock(ctx->recv_window_mutex);

            if (pushed != POTR_OK)
            {
                /* 重複パケット → スキップ */
                POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                           "tcp_recv[service_id=%" PRId64 " path=%d]: DATA seq=%u duplicate, skipped",
                           ctx->service.service_id, path_idx, (unsigned)pkt.seq_num);
                return POTR_OK;
            }

            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                       "tcp_recv[service_id=%" PRId64 " path=%d]: DATA seq=%u payload=%u", ctx->service.service_id,
                       path_idx, (unsigned)pkt.seq_num, (unsigned)pkt.payload_len);

            /* 順序整列済みパケットをポップして配信 */
            cplat_local_lock_lock(ctx->recv_window_mutex, CPLAT_SYNC_WAIT_FOREVER);
            {
                potr_packet out;
                while (potr_internal_window_recv_pop(&ctx->recv_window, &out) == POTR_OK)
                {
                    size_t offset = 0;
                    potr_packet elem;
                    cplat_local_lock_unlock(ctx->recv_window_mutex);
                    while (potr_internal_packet_unpack_next(&out, &offset, &elem) == POTR_OK)
                    {
                        thread_recv_slot_deliver_payload_elem(svc_slot, &elem);
                    }
                    cplat_local_lock_lock(ctx->recv_window_mutex, CPLAT_SYNC_WAIT_FOREVER);
                }

                if (thread_recv_fin_pending_reached(svc_slot, &fin_target_seq))
                {
                    should_fire_fin = 1;
                }
            }
            cplat_local_lock_unlock(ctx->recv_window_mutex);

            if (should_fire_fin)
            {
                thread_recv_fin_fire(svc_slot, fin_target_seq);
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
    uint8_t *buf = ctx->tcp_recv_buf[path_idx]; /* PACKET_HEADER_SIZE + max_payload バイト */
    cplat_socket fd;
    thread_recv_slot svc_slot; /* TCP は 1:1 モードのみ */

    thread_recv_slot_init_ctx(&svc_slot, ctx);

    /* PING 受信タイムアウト監視を使用するか判定する。
     * TCP は bootstrap PING 往復だけでも CONNECTED できるが、受信タイムアウト監視は
     * 定周期 PING を送信する構成でのみ有効とする。 */
    int use_recv_timeout = (ctx->health_interval_ms > 0 && ctx->health_timeout_ms > 0);
    /* ポーリング間隔: 1 秒単位でチェックし、health_timeout_ms を超えないようにする。 */
    uint32_t poll_ms;
    const char *recv_timeout_label;

    if (use_recv_timeout)
    {
        if (ctx->health_timeout_ms < 1000U)
        {
            poll_ms = ctx->health_timeout_ms;
        }
        else
        {
            poll_ms = 1000U;
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

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "tcp_recv[service_id=%" PRId64 " path=%d]: starting (recv_timeout=%s)",
               ctx->service.service_id, path_idx, recv_timeout_label);

    while (ctx->running[path_idx])
    {
        uint16_t wire_payload_len;
        int r;

        fd = ctx->tcp_conn_fd[path_idx];
        if (fd == CPLAT_INVALID_SOCKET)
        {
            break;
        }

        /* ── 先読みバッファー処理 ──
         * accept スレッドが session 判定のために読み取ったパケットが残っている場合、
         * ソケットからの読み取りをスキップしてバッファーの内容をそのまま使用する。
         * accept スレッドは recv スレッド起動前に書き込みを完了しているため mutex 不要。 */
        if (ctx->tcp_first_pkt_len[path_idx] > 0)
        {
            /* accept スレッドの先読みバッファーを recv バッファーにコピーする */
            size_t first_len = ctx->tcp_first_pkt_len[path_idx];
            memcpy(buf, ctx->tcp_first_pkt_buf[path_idx], first_len);
            ctx->tcp_first_pkt_len[path_idx] = 0; /* 先読みバッファーをクリア */
            {
                uint16_t wpl;
                memcpy(&wpl, buf + 34, sizeof(wpl));
                wire_payload_len = cplat_ntoh16(wpl);
            }
        }
        else
        {
            /* タイムアウト付きポーリングで PING 受信を監視する。
             * データが到着するまで poll_ms 待機し、タイムアウト時は PING 受信時刻を確認する。 */
            if (use_recv_timeout)
            {
                /* poll_ms は health_timeout から算出したポーリング間隔。INT_MAX 以下 */
                int readable = tcp_wait_readable(fd, (int)poll_ms);
                if (!ctx->running[path_idx])
                    break;
                if (readable < 0)
                    break; /* エラー */
                if (readable == 0)
                {
                    /* ポーリング タイムアウト: PING 受信時刻を確認する */
                    uint64_t last = ctx->tcp_last_ping_recv_ms[path_idx];
                    uint64_t elapsed = cplat_get_monotonic_ms() - last;
                    if (last > 0 && elapsed > (uint64_t)ctx->health_timeout_ms)
                    {
                        int ping_state_changed;

                        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                                   "tcp_recv[service_id=%" PRId64 " path=%d]: PING timeout"
                                   " (%llu ms), disconnecting",
                                   ctx->service.service_id, path_idx, (unsigned long long)elapsed);
                        cplat_local_lock_lock(ctx->tcp_state_mutex, CPLAT_SYNC_WAIT_FOREVER);
                        ping_state_changed =
                            thread_recv_set_path_ping_state(&ctx->path_ping_state[path_idx], POTR_PING_STATE_ABNORMAL);
                        thread_recv_sync_path_state(&svc_slot);
                        cplat_local_lock_unlock(ctx->tcp_state_mutex);
                        wake_tcp_interrupt_ping_if_needed(ctx, path_idx, ping_state_changed);
                        break;
                    }
                    continue;
                }
                /* readable == 1: データあり。以降の tcp_read_all へ進む。 */
            }

            /* 1. 固定長ヘッダー読み取り */
            r = tcp_read_all(fd, buf, PACKET_HEADER_SIZE);
            if (r != POTR_OK)
            {
                break; /* 切断 or エラー */
            }

            /* 2. payload_len をヘッダー末尾の offset 34 から抽出する */
            {
                uint16_t wpl;
                memcpy(&wpl, buf + 34, sizeof(wpl));
                wire_payload_len = cplat_ntoh16(wpl);
            }

            /* 3. ペイロード長バリデーション */
            if ((size_t)wire_payload_len > ctx->global.max_payload)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                           "tcp_recv[service_id=%" PRId64 "]: oversized payload %u > max %u,"
                           " disconnecting",
                           ctx->service.service_id, (unsigned)wire_payload_len, (unsigned)ctx->global.max_payload);
                break;
            }

            /* 4. ペイロード読み取り */
            if (wire_payload_len > 0)
            {
                r = tcp_read_all(fd, buf + PACKET_HEADER_SIZE, wire_payload_len);
                if (r != POTR_OK)
                {
                    break;
                }
            }
        } /* else (先読みバッファーなし) ここまで */

        cplat_local_lock_lock(ctx->tcp_recv_mutex, CPLAT_SYNC_WAIT_FOREVER);
        if (ctx->running[path_idx])
        {
            r = tcp_handle_packet(ctx, &svc_slot, buf, wire_payload_len, path_idx);
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

    /* 接続断処理: DISCONNECTED イベントは connect スレッドが tcp_active_paths == 0 時に発火する */
    ctx->running[path_idx] = 0;

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "tcp_recv[service_id=%" PRId64 " path=%d]: exited",
               ctx->service.service_id, path_idx);

    return;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_comm_recv_thread_start(potr_context *ctx)
{
    if (ctx == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    ctx->running[0] = 1;

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv_thread[service_id=%" PRId64 "]: starting", ctx->service.service_id);

    if (cplat_thread_create(&ctx->recv_thread[0], recv_thread_func, ctx) != CPLAT_OK)
    {
        ctx->running[0] = 0;
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "recv_thread[service_id=%" PRId64 "]: thread create failed",
                   ctx->service.service_id);
        /* cplat のスレッド生成失敗には、porter の分類へ変換できる詳細コードがありません。 */
        return POTR_ERR_UNKNOWN;
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_comm_recv_thread_stop(potr_context *ctx)
{
    if (ctx == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    ctx->running[0] = 0;

    {
        int i;
        for (i = 0; i < ctx->n_path; i++)
        {
            if (ctx->sock[i] != CPLAT_INVALID_SOCKET)
            {
                (void)cplat_socket_shutdown_receive(&ctx->sock[i], NULL);
            }
        }
    }

    cplat_thread_join(ctx->recv_thread[0], CPLAT_SYNC_WAIT_FOREVER);

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_tcp_recv_thread_start(potr_context *ctx, int path_idx)
{
    if (ctx == NULL || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    ctx->running[path_idx] = 1;

    ctx->tcp_recv_args[path_idx].ctx = ctx;
    ctx->tcp_recv_args[path_idx].path_idx = path_idx;

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "tcp_recv_thread[service_id=%" PRId64 " path=%d]: starting",
               ctx->service.service_id, path_idx);

    if (cplat_thread_create(&ctx->recv_thread[path_idx], tcp_recv_thread_func, &ctx->tcp_recv_args[path_idx]) !=
        CPLAT_OK)
    {
        ctx->running[path_idx] = 0;
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "tcp_recv_thread[service_id=%" PRId64 " path=%d]: thread create failed",
                   ctx->service.service_id, path_idx);
        /* cplat のスレッド生成失敗には、porter の分類へ変換できる詳細コードがありません。 */
        return POTR_ERR_UNKNOWN;
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_tcp_recv_thread_stop(potr_context *ctx, int path_idx)
{
    if (ctx == NULL || path_idx < 0 || path_idx >= (int)POTR_MAX_PATH)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    ctx->running[path_idx] = 0;

    cplat_thread_join(ctx->recv_thread[path_idx], CPLAT_SYNC_WAIT_FOREVER);

    return POTR_OK;
}
