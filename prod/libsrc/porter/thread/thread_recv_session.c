/** @file thread_recv_session.c
 * @brief 受信セッションの採用判定と経路切断を行います。
 */

#include <inttypes.h>

#include <cplat/base/platform.h>
#include <porter/porter_const.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/window.h>
#include <porter/potr_path_event.h>
#include <porter/infra/potr_trace.h>
#include <cplat/clock/timespec.h>
#include <cplat/net/endpoint.h>
#include <cplat/sync/sync.h>

#include "thread_recv_session.h"

static void slot_clear_pending_fin(thread_recv_slot *slot)
{
    *slot->pending_fin = 0;
    *slot->fin_target_seq = 0;
}

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_session_adopt(thread_recv_slot *slot, const potr_packet *pkt)
{
    potr_context *ctx = slot->ctx;

    if (!*slot->peer_session_known)
    {
        /* 初回受信 (または FIN/タイムアウト後の再接続): セッション採用およびウィンドウのリセット。
           pkt->seq_num で初期化することで、送信者の現在位置に直接同期し
           NACK/REJECT サイクルを経ずに再加入できる。
             DATA 着信時: potr_internal_window_init(DATA.seq_num) → push → pop → 即時 CONNECTED
             PING 着信時: potr_internal_window_init(PING.seq_num) → gap スキャン範囲がゼロ → NACK なし
           FIN/タイムアウト後は送信者が同一セッションのまま任意の seq から
           再開する可能性があるため pkt->seq_num を使用する。 */
        *slot->peer_session_id = pkt->session_id;
        potr_session_ts_from_hdr(pkt->session_tv_sec, pkt->session_tv_nsec, slot->peer_session_ts);
        *slot->peer_session_known = 1;
        *slot->reorder_pending = 0;
        slot_clear_pending_fin(slot);
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                   "recv[service_id=%" PRId64 "]: new session (first contact), new_id=%u seq=%u",
                   ctx->service.service_id, pkt->session_id, (unsigned)pkt->seq_num);
        potr_internal_window_init(slot->recv_window, pkt->seq_num, ctx->global.window_size, ctx->global.max_payload);
        return 1;
    }

    /* (セッション開始時刻, session_id) の辞書順で新旧を判定する。
       - pkt > 既知セッション: 新セッション。return せずにフォール スルーし、
         関数末尾の「新セッション採用」ブロックで採用処理を行う。
       - pkt < 既知セッション: 旧セッション。直ちに return 0 で破棄する。
       - pkt == 既知セッション: 同一セッション。直ちに return 1 で通常受信を継続する。
       新セッションと判定された分岐は LOG のみで return しないため、
       if-else チェーンを抜けた後に必ず末尾の採用ブロックに到達する。 */
    cplat_timespec pkt_session_ts;
    potr_session_ts_from_hdr(pkt->session_tv_sec, pkt->session_tv_nsec, &pkt_session_ts);
    int ts_cmp = cplat_timespec_cmp(&pkt_session_ts, slot->peer_session_ts);

    if (ts_cmp > 0)
    {
        /* 新セッション (セッション開始時刻が大): フォール スルーして採用 */
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                   "recv[service_id=%" PRId64 "]: new session (ts %lld.%09lld > %lld.%09lld)"
                   ", old_id=%u new_id=%u",
                   ctx->service.service_id, (long long)pkt_session_ts.tv_sec, (long long)pkt_session_ts.tv_nsec,
                   (long long)slot->peer_session_ts->tv_sec, (long long)slot->peer_session_ts->tv_nsec,
                   *slot->peer_session_id, pkt->session_id);
    }
    else if (ts_cmp < 0)
    {
        return 0; /* 旧セッション (セッション開始時刻が小): 破棄 */
    }
    else if (pkt->session_id > *slot->peer_session_id)
    {
        /* 新セッション (タイムスタンプ完全一致・session_id が大): フォール スルーして採用 */
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: new session (id tiebreak %u > %u)",
                   ctx->service.service_id, pkt->session_id, *slot->peer_session_id);
    }
    else
    {
        /* ここに到達するのはセッション開始時刻が完全一致かつ
           session_id <= peer_session_id の場合のみ。
           新セッション分岐はこの else には入らない。 */
        if (pkt->session_id == *slot->peer_session_id)
        {
            return 1; /* 同一セッション: 採用済みのため再初期化不要 */
        }
        else
        {
            return 0; /* 旧セッション (タイムスタンプ完全一致・session_id が小): 破棄 */
        }
    }

    /* 新セッション採用: スロットを更新し、ウィンドウおよびリオーダー状態をリセットする。
       最初に受信したパケットの seq_num で初期化することで、送信者が先行して
       送信済みの seq に直接同期し、不要な NACK/REJECT サイクルを発生させない。
       送信者を先に起動して受信者が後から参加した場合も同様に機能する。 */
    *slot->peer_session_id = pkt->session_id;
    *slot->peer_session_ts = pkt_session_ts;
    *slot->reorder_pending = 0;
    slot_clear_pending_fin(slot);
    potr_internal_window_init(slot->recv_window, pkt->seq_num, ctx->global.window_size, ctx->global.max_payload);
    return 1;
}

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_set_path_ping_state(volatile uint8_t *state, uint8_t next_state)
{
    if (*state == next_state)
    {
        return 0;
    }

    *state = next_state;
    return 1;
}

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_set_all_path_ping_states(volatile uint8_t *states, size_t count, uint8_t next_state)
{
    size_t i;
    int changed = 0;

    for (i = 0; i < count; i++)
    {
        if (states[i] != next_state)
        {
            states[i] = next_state;
            changed = 1;
        }
    }

    return changed;
}

/* Doxygen コメントは、ヘッダーに記載 */

void thread_recv_disconnect_service_all_paths(potr_context *ctx)
{
    int next_states[POTR_MAX_PATH];
    potr_internal_prepared_path_events prepared;

    potr_internal_zero_path_states(next_states);
    cplat_local_lock_lock(ctx->callback_mutex, CPLAT_SYNC_WAIT_FOREVER);
    potr_internal_sync_service_path_state_locked(ctx, next_states, &prepared);
    potr_internal_emit_service_path_events_locked(ctx, &prepared);
    cplat_local_lock_unlock(ctx->callback_mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */

void thread_recv_disconnect_peer_all_paths(potr_context *ctx, potr_internal_peer_context *peer)
{
    int next_states[POTR_MAX_PATH];
    potr_internal_prepared_path_events prepared;

    potr_internal_zero_path_states(next_states);
    cplat_local_lock_lock(ctx->callback_mutex, CPLAT_SYNC_WAIT_FOREVER);
    potr_internal_sync_peer_path_state_locked(peer, next_states, &prepared);
    potr_internal_emit_peer_path_events_locked(ctx, peer, &prepared);
    cplat_local_lock_unlock(ctx->callback_mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_update_path_health(thread_recv_slot *slot, int path_idx)
{
    cplat_timespec now_ts;

    cplat_get_monotonic(&now_ts);
    *slot->last_recv_ts = now_ts;
    slot->path_last_recv_ts[path_idx] = now_ts;
    return thread_recv_set_path_ping_state(&slot->path_ping_state[path_idx], POTR_PING_STATE_NORMAL);
}

/* Doxygen コメントは、ヘッダーに記載 */

void thread_recv_sync_path_state(thread_recv_slot *slot)
{
    potr_context *ctx = slot->ctx;
    int next_states[POTR_MAX_PATH];
    potr_internal_prepared_path_events prepared;

    if (slot->peer != NULL)
    {
        potr_internal_copy_bidir_n1_path_states(slot->peer, next_states);
        cplat_local_lock_lock(ctx->callback_mutex, CPLAT_SYNC_WAIT_FOREVER);
        potr_internal_sync_peer_path_state_locked(slot->peer, next_states, &prepared);
        potr_internal_emit_peer_path_events_locked(ctx, slot->peer, &prepared);
        cplat_local_lock_unlock(ctx->callback_mutex);
        return;
    }

    if (potr_is_tcp_type(ctx->service.type))
    {
        potr_internal_copy_tcp_path_states(ctx, next_states);
    }
    else if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
    {
        potr_internal_copy_bidir_udp_path_states(ctx, next_states);
    }
    else
    {
        potr_internal_copy_oneway_path_states(ctx, next_states);
    }

    cplat_local_lock_lock(ctx->callback_mutex, CPLAT_SYNC_WAIT_FOREVER);
    potr_internal_sync_service_path_state_locked(ctx, next_states, &prepared);
    potr_internal_emit_service_path_events_locked(ctx, &prepared);
    cplat_local_lock_unlock(ctx->callback_mutex);
}

/* Doxygen コメントは、ヘッダーに記載 */

void thread_recv_learn_sender_path(thread_recv_slot *slot, int path_idx, const cplat_ipv4_endpoint *sender)
{
    potr_context *ctx = slot->ctx;

    if (slot->peer != NULL)
    {
        if (!potr_endpoint_is_unset(&slot->dest_addr[path_idx]))
        {
            slot->dest_addr[path_idx].port = sender->port;
        }
        else
        {
            slot->dest_addr[path_idx] = *sender;
            slot->peer->n_paths++;
            POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "n1_update_path_recv: peer=%u path %d learned",
                       (unsigned)slot->peer->peer_id, path_idx);
        }
        return;
    }

    ctx->peer_port[path_idx] = sender->port;

    if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
    {
        if (ctx->service.src_addr[0][0] == '\0' && slot->dest_addr[path_idx].address == 0)
        {
            slot->dest_addr[path_idx].address = sender->address;
        }
        if (slot->dest_addr[path_idx].port == 0)
        {
            slot->dest_addr[path_idx].port = sender->port;
        }
    }
}
