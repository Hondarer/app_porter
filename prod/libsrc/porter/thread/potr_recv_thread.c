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
#include <porter/protocol/seqnum.h>
#include <porter/protocol/window.h>
#include <porter/potr_context.h>
#include <porter/potr_path_event.h>
#include <porter/potr_peer_table.h>
#include <porter/thread/potr_health_thread.h>
#include <porter/thread/potr_recv_thread.h>
#include <cplat/crypto/crypto.h>
#include <porter/infra/potr_trace.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/endpoint.h>
#include <cplat/net/socket.h>

#include "thread_recv_validate.h"
#include "thread_recv_slot.h"
#include "thread_recv_session.h"
#include "thread_recv_fin.h"

/* 前方宣言: 後で定義される関数 */
static void send_nack(potr_context *ctx, uint32_t nack_seq);
static void raw_session_disconnect(potr_context *ctx);
static void wake_udp_interrupt_ping_if_needed(potr_context *ctx, int state_changed);
static void wake_tcp_interrupt_ping_if_needed(potr_context *ctx, int path_idx, int state_changed);
static int reorder_gap_ready(potr_context *ctx, uint32_t nack_num);
static void n1_send_nack(potr_context *ctx, potr_internal_peer_context *peer, uint32_t nack_seq);
static void sync_service_path_state(potr_context *ctx);

/* 欠番 nack_num に対するリオーダー待機の完了判定。
   N:1 モードは現状リオーダー待機を持たないため常に 1 (即時処理) を返す。
   真偽値を返す述語のため共通結果コードの適用対象外。 */
static int slot_gap_ready(thread_recv_slot *slot, uint32_t nack_num)
{
    if (slot->peer != NULL)
    {
        return 1;
    }

    return reorder_gap_ready(slot->ctx, nack_num);
}

/* NACK 送信をモードに応じた宛先解決へディスパッチする */
static void slot_send_nack(thread_recv_slot *slot, uint32_t nack_seq)
{
    if (slot->peer != NULL)
    {
        n1_send_nack(slot->ctx, slot->peer, nack_seq);
    }
    else
    {
        send_nack(slot->ctx, nack_seq);
    }
}

/* NACK/REJECT 制御パケットを wire 形式へ直列化する。暗号化有効時はヘッダーを
   AAD とする GCM 認証タグを付加し、ノンスの seq_or_ack_num 部には ack_num を使う。
   wire_buf は PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE バイト以上であること。
   成功時は POTR_OK、暗号化失敗時は POTR_ERR_UNKNOWN を返す。 */
static int build_ctrl_pkt_wire(const potr_context *ctx, potr_packet *pkt, uint8_t *wire_buf, size_t *wire_len)
{
    if (ctx->service.encrypt_enabled)
    {
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t enc_out = POTR_CRYPTO_TAG_SIZE;

        pkt->flags |= cplat_hton16(POTR_FLAG_ENCRYPTED);
        pkt->payload_len = cplat_hton16((uint16_t)POTR_CRYPTO_TAG_SIZE);

        /* ノンス: session_id(4B) + flags(2B NBO) + ack_num(4B NBO) + padding(2B) */
        memcpy(nonce, &pkt->session_id, 4);
        memcpy(nonce + 4, &pkt->flags, 2);
        memcpy(nonce + 6, &pkt->ack_num, 4);
        memset(nonce + 10, 0, 2);

        memcpy(wire_buf, pkt, PACKET_HEADER_SIZE);
        if (cplat_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out, NULL, 0, ctx->service.encrypt_key, nonce,
                             wire_buf, PACKET_HEADER_SIZE) != CPLAT_OK)
        {
            /* cplat の暗号化失敗には、porter の分類へ変換できる詳細コードがありません。 */
            return POTR_ERR_UNKNOWN;
        }
        *wire_len = PACKET_HEADER_SIZE + enc_out;
        return POTR_OK;
    }

    memcpy(wire_buf, pkt, PACKET_HEADER_SIZE);
    *wire_len = potr_internal_packet_wire_size(pkt);
    return POTR_OK;
}

static void sync_service_path_state(potr_context *ctx)
{
    int next_states[POTR_MAX_PATH];
    potr_internal_prepared_path_events prepared;

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

static void sync_peer_path_state(potr_context *ctx, potr_internal_peer_context *peer)
{
    int next_states[POTR_MAX_PATH];
    potr_internal_prepared_path_events prepared;

    potr_internal_copy_bidir_n1_path_states(peer, next_states);
    cplat_local_lock_lock(ctx->callback_mutex, CPLAT_SYNC_WAIT_FOREVER);
    potr_internal_sync_peer_path_state_locked(peer, next_states, &prepared);
    potr_internal_emit_peer_path_events_locked(ctx, peer, &prepared);
    cplat_local_lock_unlock(ctx->callback_mutex);
}

/* ================================================================
 * N:1 モード専用: ピア コンテキストを使ったパケット処理関数群
 * ================================================================ */

/* N:1: wire 化した制御パケットをピアの有効な全パスへ送信する */
static void n1_send_ctrl_to_peer_paths(potr_context *ctx, potr_internal_peer_context *peer, potr_packet *pkt)
{
    uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
    size_t wire_len;
    int k;

    if (build_ctrl_pkt_wire(ctx, pkt, wire_buf, &wire_len) != POTR_OK)
        return;

    for (k = 0; k < (int)POTR_MAX_PATH; k++)
    {
        size_t sent = 0;

        if (potr_endpoint_is_unset(&peer->dest_addr[k]))
            continue;
        (void)cplat_socket_sendto(ctx->sock[k], wire_buf, wire_len, &peer->dest_addr[k], &sent, NULL);
    }
}

static void n1_send_nack(potr_context *ctx, potr_internal_peer_context *peer, uint32_t nack_seq)
{
    potr_packet nack_pkt;
    potr_internal_packet_session_hdr shdr;

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = peer->session_id;
    potr_session_ts_to_hdr(&peer->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);

    if (potr_internal_packet_build_nack(&nack_pkt, &shdr, nack_seq) != POTR_OK)
        return;

    n1_send_ctrl_to_peer_paths(ctx, peer, &nack_pkt);
}

static void n1_send_reject(potr_context *ctx, potr_internal_peer_context *peer, uint32_t seq_num)
{
    potr_packet reject_pkt;
    potr_internal_packet_session_hdr shdr;

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = peer->session_id;
    potr_session_ts_to_hdr(&peer->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);

    if (potr_internal_packet_build_reject(&reject_pkt, &shdr, seq_num) != POTR_OK)
        return;

    n1_send_ctrl_to_peer_paths(ctx, peer, &reject_pkt);
}

/* N:1: 送信元アドレスを記録し、未知のパスを学習する */
static void n1_update_path_recv(potr_internal_peer_context *peer, const cplat_ipv4_endpoint *sender_addr, int path_idx)
{
    if (!potr_endpoint_is_unset(&peer->dest_addr[path_idx]))
    {
        /* 既知パス: ポートを更新 */
        peer->dest_addr[path_idx].port = sender_addr->port;
    }
    else
    {
        /* 新規パス: インデックス path_idx のスロットに直接記録 */
        peer->dest_addr[path_idx] = *sender_addr;
        peer->n_paths++;
        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "n1_update_path_recv: peer=%u path %d learned", (unsigned)peer->peer_id,
                   path_idx);
    }
}

/* パスごとのヘルスチェック受信時刻と受信状態を更新する。
   片方向 type 1-6 では PING / 有効 DATA、双方向 type 7/8 では PING 受信時のみ呼び出すこと。 */
static int slot_update_path_health(thread_recv_slot *slot, int path_idx)
{
    cplat_timespec now_ts;

    cplat_get_monotonic(&now_ts);
    *slot->last_recv_ts = now_ts;
    slot->path_last_recv_ts[path_idx] = now_ts;
    return thread_recv_set_path_ping_state(&slot->path_ping_state[path_idx], POTR_PING_STATE_NORMAL);
}

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
            sync_peer_path_state(ctx, &ctx->peers[i]);
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

/** NACK 重複抑制の時間窓 (ミリ秒)。この時間内の同一 ack_num の NACK は破棄する。 */
#define POTR_NACK_DEDUP_MS 200U

/* PING ペイロード (相手端のパス受信状態ベクトル) を remote_path_ping_state[] に取り込む。
 * 入力バイトが POTR_PING_STATE_UNDEFINED の場合は当該スロットを更新しない。
 * これは bootstrap PING や送信競合で stale な UNDEFINED が後着した場合に、確立済みの
 * NORMAL を後退させて瞬間 DISCONNECTED を起こさないための防御。
 * NORMAL / ABNORMAL への遷移のみ反映する。
 * see: /home/user/.claude/plans/prompt-logs-76433196230-zip-ci-ci-fuzzy-moore.md */
static void apply_remote_path_ping_state_payload(volatile uint8_t *dst, const uint8_t *src, size_t count)
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

/* パスごとの peer_port と送信元アドレスを更新する */
static void update_path_recv(potr_context *ctx, int path_idx, const cplat_ipv4_endpoint *sender)
{
    ctx->peer_port[path_idx] = sender->port; /* NBO のまま格納 */

    /* unicast_bidir で送信先アドレスが未確定の場合は受信パケットの送信元から動的学習する。
       - src_addr 省略 (動的 1:1 RECEIVER): IP アドレスが 0 → 送信元 IP で更新
       - src_port=0 (エフェメラル ポート動的学習): ポートが 0 → 送信元ポートで更新 */
    if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
    {
        if (ctx->service.src_addr[0][0] == '\0' && ctx->dest_addr[path_idx].address == 0)
        {
            ctx->dest_addr[path_idx].address = sender->address; /* NBO */
        }
        if (ctx->dest_addr[path_idx].port == 0)
        {
            ctx->dest_addr[path_idx].port = sender->port; /* NBO */
        }
    }
}

/* タイムアウト時に経過時間を確認し、必要なら peer_port クリアと DISCONNECTED イベントを発火する */
static void check_health_timeout(potr_context *ctx)
{
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
        sync_service_path_state(ctx);
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

/* 欠番 nack_num に対してリオーダー待機が完了しているか確認する。
   返値: 1 = 処理進行 (NACK/DISCONNECT を発行すべき)、0 = まだ待機中。
   真偽値を返す述語のため共通結果コードの適用対象外。
   reorder_timeout_ms == 0 の場合は常に 1 を返す (即時)。
   新しいギャップまたは欠番通番が変わった場合はタイマーをリセットして 0 を返す。
   同一欠番でタイムアウト経過後は reorder_pending を 0 にクリアして 1 を返す。 */
static int reorder_gap_ready(potr_context *ctx, uint32_t nack_num)
{
    cplat_timespec now_ts;
    uint32_t ms;

    if (ctx->global.reorder_timeout_ms == 0U)
        return 1;

    ms = ctx->global.reorder_timeout_ms;

    /* 新しいギャップ、または欠番通番が変わった: タイマーをリセットして待機開始 */
    if (!ctx->reorder_pending || ctx->reorder_nack_num != nack_num)
    {
        uint32_t effective_ms;
        cplat_get_monotonic(&now_ts);

        /* マルチキャスト/ブロードキャスト通常モードでは NACK 送出タイミングを分散させる。
           複数受信者が同一欠番を同時に NACK すると送信者側で輻輳が発生するため、
           タイマー値を reorder_timeout_ms の 100%〜200% の範囲でジッタを付加する。
           ジッタ源: 現在時刻のナノ秒部の下位ビット (外部 RNG 不要・移植性高)。
           RAW 系は POTR_TYPE_MULTICAST_RAW / BROADCAST_RAW であり条件に該当しないため対象外。 */
        effective_ms = ms;
        if (ctx->service.type == POTR_TYPE_MULTICAST || ctx->service.type == POTR_TYPE_BROADCAST)
        {
            effective_ms = ms + (uint32_t)((uint64_t)now_ts.tv_nsec % ms);
        }

        ctx->reorder_pending = 1;
        ctx->reorder_nack_num = nack_num;
        cplat_timespec_add_ms(&now_ts, (uint64_t)effective_ms, &ctx->reorder_deadline_ts);
        return 0; /* 待機開始 */
    }

    /* 同一欠番: タイムアウト確認 */
    cplat_get_monotonic(&now_ts);
    if (cplat_timespec_cmp(&now_ts, &ctx->reorder_deadline_ts) >= 0)
    {
        ctx->reorder_pending = 0;
        return 1; /* タイムアウト: 処理進行 */
    }

    return 0; /* まだ待機中 */
}

/* poll タイムアウト時に、リオーダー待機中の欠番がタイムアウトしていれば処理する。
   通常モード: NACK を送出する。
   RAW モード: DISCONNECTED を発行してセッションをリセットし、次のパケットで再同期する。 */
static void check_reorder_timeout(potr_context *ctx)
{
    if (!ctx->reorder_pending)
        return;
    if (!reorder_gap_ready(ctx, ctx->reorder_nack_num))
        return;

    /* reorder_gap_ready が 1 を返した時点で reorder_pending はすでにクリア済み */
    if (potr_is_raw_type(ctx->service.type))
    {
        /* RAW モード: DISCONNECTED を発行してセッション状態をリセットする。
           次のパケット受信時に thread_recv_session_adopt で potr_internal_window_init が呼び出され
           自然に再同期する。 */
        raw_session_disconnect(ctx);
        ctx->peer_session_known = 0;
        ctx->last_recv_ts.tv_sec = 0;
        potr_internal_window_init(&ctx->recv_window, 0, ctx->global.window_size, ctx->global.max_payload);
    }
    else
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: NACK seq=%u (reorder timeout)",
                   ctx->service.service_id, (unsigned)ctx->reorder_nack_num);
        send_nack(ctx, ctx->reorder_nack_num);
    }
}

/* NACK パケットを全パスへ送信する */
static void send_nack(potr_context *ctx, uint32_t nack_seq)
{
    potr_packet nack_pkt;
    potr_internal_packet_session_hdr shdr;
    uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
    size_t wire_len;
    int i;

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = ctx->session_id;
    potr_session_ts_to_hdr(&ctx->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);

    if (potr_internal_packet_build_nack(&nack_pkt, &shdr, nack_seq) != POTR_OK)
        return;

    if (build_ctrl_pkt_wire(ctx, &nack_pkt, wire_buf, &wire_len) != POTR_OK)
        return;

    for (i = 0; i < ctx->n_path; i++)
    {
        size_t sent = 0;

        /* UNICAST_BIDIR: dest_addr[i] (dst_addr:dst_port) へ直接送信する。
           通常 unicast: src_addr_resolved[i]:src_port または peer_port へ送信する。 */
        if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
        {
            (void)cplat_socket_sendto(ctx->sock[i], wire_buf, wire_len, &ctx->dest_addr[i], &sent, NULL);
        }
        else
        {
            cplat_ipv4_endpoint dest = {0};
            uint16_t port;

            if (ctx->service.src_port != 0)
            {
                port = cplat_hton16(ctx->service.src_port);
            }
            else
            {
                port = ctx->peer_port[i]; /* NBO */
            }

            if (port == 0)
                continue; /* ポート未観測のパスは送れない */

            dest.address = ctx->src_addr_resolved[i];
            dest.port = port;

            (void)cplat_socket_sendto(ctx->sock[i], wire_buf, wire_len, &dest, &sent, NULL);
        }
    }
}

/* REJECT パケットを全パスへ送信する */
static void send_reject(potr_context *ctx, uint32_t seq_num)
{
    potr_packet reject_pkt;
    potr_internal_packet_session_hdr shdr;
    uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
    size_t wire_len;
    int i;

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = ctx->session_id;
    potr_session_ts_to_hdr(&ctx->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);

    if (potr_internal_packet_build_reject(&reject_pkt, &shdr, seq_num) != POTR_OK)
        return;

    if (build_ctrl_pkt_wire(ctx, &reject_pkt, wire_buf, &wire_len) != POTR_OK)
        return;

    for (i = 0; i < ctx->n_path; i++)
    {
        size_t sent = 0;

        (void)cplat_socket_sendto(ctx->sock[i], wire_buf, wire_len, &ctx->dest_addr[i], &sent, NULL);
    }
}

/* recv_window から順序整列済みの外側パケットを取り出してペイロード エレメントを配信する。
   REJECT 処理後と通常受信処理の両方から呼び出す。 */
static void slot_drain_recv_window(thread_recv_slot *slot)
{
    potr_context *ctx = slot->ctx;
    potr_packet pop_pkt;

    while (potr_internal_window_recv_pop(slot->recv_window, &pop_pkt) == POTR_OK)
    {
        const char *pkt_type_str;
        if (pop_pkt.flags & POTR_FLAG_PING)
        {
            pkt_type_str = "PING";
        }
        else
        {
            pkt_type_str = "DATA";
        }
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: pop seq=%u %s", ctx->service.service_id,
                   (unsigned)pop_pkt.seq_num, pkt_type_str);

        if (pop_pkt.flags & POTR_FLAG_PING)
        {
            continue; /* PING: 生存確認のみ、ペイロード エレメント展開不要 */
        }

        /* DATA: ペイロード エレメントを順に展開して配信 */
        {
            size_t offset = 0;
            potr_packet elem;

            while (potr_internal_packet_unpack_next(&pop_pkt, &offset, &elem) == POTR_OK)
            {
                thread_recv_slot_deliver_payload_elem(slot, &elem);
            }
        }
    }

    /* pending FIN: recv_window.next_seq が FIN の目標値に到達したら DISCONNECTED を発火する。 */
    {
        uint32_t fin_target_seq = 0U;

        if (thread_recv_fin_pending_reached(slot, &fin_target_seq))
        {
            thread_recv_fin_fire(slot, fin_target_seq);
        }
    }
}

/* RAW モード用: DISCONNECTED イベントを発行してセッション状態を部分的にリセットする。
   ウィンドウ リセットは呼び出し元が行う (新しい基点通番が確定してから呼び出すため)。
   フラグメント組み立てバッファーも破棄する。 */
static void raw_session_disconnect(potr_context *ctx)
{
    thread_recv_set_all_path_ping_states(ctx->path_ping_state, POTR_MAX_PATH, POTR_PING_STATE_UNDEFINED);
    memset((void *)ctx->remote_path_ping_state, 0, sizeof(ctx->remote_path_ping_state));
    thread_recv_disconnect_service_all_paths(ctx);

    /* フラグメント組み立て途中状態と pending FIN を破棄 */
    ctx->pending_fin = 0;
    ctx->fin_target_seq = 0;
    ctx->frag_buf_len = 0;
    ctx->frag_compressed = 0;
}

/* 外側パケット (DATA / PING) を受信ウィンドウに投入し、順序整列済みパケットを配信する。
   再送・順序整列の単位は外側パケットであり、NACK も外側パケットの seq_num を指定する。
   RAW モードでは NACK を送信せず、ギャップ検出時は DISCONNECTED を発行してウィンドウを
   新しい基点通番でリセットする。 */
static void slot_process_outer_pkt(thread_recv_slot *slot, const potr_packet *pkt, int path_idx)
{
    potr_context *ctx = slot->ctx;
    uint32_t nack_num;
    uint32_t stretch;
    /* RAW 系は 1:1 モード限定の通信種別 (N:1 の type は UNICAST_BIDIR_N1 のみ) */
    int is_raw = potr_is_raw_type(ctx->service.type);

    if (potr_internal_window_recv_push(slot->recv_window, pkt) != POTR_OK)
    {
        if (is_raw)
        {
            /* ウィンドウ満杯: DISCONNECTED を発行し、受信したパケットの通番でリセットしてから
               再投入する。再投入は必ず成功する (空ウィンドウの先頭スロット)。 */
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "recv[service_id=%" PRId64 "]: RAW recv_window full, resetting to seq=%u",
                       ctx->service.service_id, (unsigned)pkt->seq_num);
            raw_session_disconnect(ctx);
            potr_internal_window_recv_reset(slot->recv_window, pkt->seq_num);
            if (potr_internal_window_recv_push(slot->recv_window, pkt) != POTR_OK)
            {
                /* リセット直後の再投入失敗は想定外 */
                POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                           "recv[service_id=%" PRId64 "]: RAW window re-push failed seq=%u (bug)",
                           ctx->service.service_id, (unsigned)pkt->seq_num);
                return;
            }
        }
        else
        {
            /* 通番がウィンドウ範囲外のためドロップ (受信ウィンドウ満杯、または古い重複パケット)。
               受信者は next_seq を待ち続けるが、ヘルスチェックや後続パケット到着時に NACK が送信される。 */
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "recv[service_id=%" PRId64 "]: peer=%u recv_window full (100%%), dropping seq=%u"
                       " (base_seq=%u window_size=%u)",
                       ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)pkt->seq_num,
                       (unsigned)slot->recv_window->base_seq, (unsigned)slot->recv_window->window_size);
            return;
        }
    }

    /* ウィンドウ利用率チェック: 今回のパケットが占める先頭からの距離で推定する。
       stretch = pkt->seq_num - base_seq + 1。push 成功後は [1, window_size] の範囲。 */
    stretch = (uint32_t)(pkt->seq_num - slot->recv_window->base_seq) + 1U;
    if (stretch * 10U >= (uint32_t)slot->recv_window->window_size * 8U)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                   "recv[service_id=%" PRId64 "]: peer=%u recv_window utilization high (%u/%u >= 80%%)"
                   " seq=%u base_seq=%u",
                   ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)stretch,
                   (unsigned)slot->recv_window->window_size, (unsigned)pkt->seq_num,
                   (unsigned)slot->recv_window->base_seq);
    }

    if (potr_internal_window_recv_needs_nack(slot->recv_window, &nack_num))
    {
        if (is_raw)
        {
            /* ギャップ検出: リオーダー待機を確認してから DISCONNECTED を発行する。
               reorder_timeout_ms > 0 のとき、タイムアウト前はウィンドウに今のパケットを
               残したまま待機する。タイムアウト後または即時モードでは reset + 再 push。
               skip ループは push 済みパケットのスロット マッピングを壊すため使用しない。 */
            if (reorder_gap_ready(ctx, nack_num))
            {
                raw_session_disconnect(ctx);
                potr_internal_window_recv_reset(slot->recv_window, pkt->seq_num);
                if (potr_internal_window_recv_push(slot->recv_window, pkt) != POTR_OK)
                {
                    /* リセット直後の再投入失敗は想定外 */
                    POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                               "recv[service_id=%" PRId64 "]: RAW gap re-push failed seq=%u (bug)",
                               ctx->service.service_id, (unsigned)pkt->seq_num);
                    return;
                }
            }
            /* else: リオーダー待機中。パケットはウィンドウに push 済み。 */
        }
        else
        {
            if (slot_gap_ready(slot, nack_num))
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: peer=%u NACK seq=%u",
                           ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)nack_num);
                slot_send_nack(slot, nack_num);
            }
            /* else: リオーダー待機中。NACK 送出を保留。 */
        }
    }
    else
    {
        /* 欠番なし: 待機中の欠番が埋まった (または元から欠番なし) */
        *slot->reorder_pending = 0;
    }

    /* 片方向 type 1-6 では、有効な DATA を PING と同等のヘルス信号として扱う。
       push 成功後に更新することで、重複・範囲外 DATA は health に反映しない。
       N:1 モードは PING のみをヘルス信号として扱うため対象外。 */
    if (slot->peer == NULL && (pkt->flags & POTR_FLAG_DATA) && ctx->service.type != POTR_TYPE_UNICAST_BIDIR)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: DATA seq=%u updates health on path=%d",
                   ctx->service.service_id, (unsigned)pkt->seq_num, path_idx);
        slot_update_path_health(slot, path_idx);
        sync_service_path_state(ctx);
    }

    slot_drain_recv_window(slot);

    /* drain 後に next_seq が前進した結果、新たな欠番が先頭に現れる場合は NACK を送信する。
       例: seq=3,4 欠落・seq=5 着時、drain 前は NACK(3)、seq=3 再送着→ drain で pop 後
       next_seq=4 が欠番になるが次のパケット到着まで NACK(4) が遅延するのを防ぐ。
       RAW モードで reorder_gap_ready が 1 を返した場合は reset + 再 push によりウィンドウに
       パケット 1 つのみ残るため drain 後は空となりここに到達しない。
       リオーダー待機中 (RAW/通常) は drain で前進しないため post-drain 欠番も保留のまま。 */
    if (potr_internal_window_recv_needs_nack(slot->recv_window, &nack_num))
    {
        if (!is_raw && slot_gap_ready(slot, nack_num))
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: peer=%u NACK seq=%u (post-drain)",
                       ctx->service.service_id, (unsigned)slot->peer_id, (unsigned)nack_num);
            slot_send_nack(slot, nack_num);
        }
        /* RAW モードでは reset + 再 push 後は到達しない。リオーダー待機中は
           check_reorder_timeout がタイムアウト処理を担う。 */
    }
    else
    {
        *slot->reorder_pending = 0;
    }
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

    /* NACK: 送信ウィンドウから再送する */
    if (pkt->flags & POTR_FLAG_NACK)
    {
        potr_packet resend_pkt;
        size_t wire_len = 0;
        int get_result;
        int j;

        cplat_local_lock_lock(peer->send_window_mutex, CPLAT_SYNC_WAIT_FOREVER);
        get_result = potr_internal_window_send_get(&peer->send_window, pkt->ack_num, &resend_pkt);
        if (get_result == POTR_OK)
        {
            wire_len = potr_internal_packet_wire_size(&resend_pkt);
            memcpy(ctx->recv_buf, &resend_pkt, PACKET_HEADER_SIZE);
            memcpy(ctx->recv_buf + PACKET_HEADER_SIZE, resend_pkt.payload, wire_len - PACKET_HEADER_SIZE);
        }
        cplat_local_lock_unlock(peer->send_window_mutex);

        if (get_result == POTR_OK)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "recv[service_id=%" PRId64 "]: peer=%u NACK seq=%u -> retransmit",
                       ctx->service.service_id, (unsigned)peer->peer_id, (unsigned)pkt->ack_num);
            for (j = 0; j < (int)POTR_MAX_PATH; j++)
            {
                size_t sent = 0;

                if (potr_endpoint_is_unset(&peer->dest_addr[j]))
                    continue;
                (void)cplat_socket_sendto(ctx->sock[j], ctx->recv_buf, wire_len, &peer->dest_addr[j], &sent, NULL);
            }
        }
        else
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                       "recv[service_id=%" PRId64 "]: peer=%u NACK seq=%u not in window -> REJECT",
                       ctx->service.service_id, (unsigned)peer->peer_id, (unsigned)pkt->ack_num);
            n1_send_reject(ctx, peer, pkt->ack_num);
        }

        cplat_local_lock_unlock(ctx->peers_mutex);
        return;
    }

    /* REJECT */
    if (pkt->flags & POTR_FLAG_REJECT)
    {
        if (!thread_recv_session_adopt(&peer_slot, pkt))
        {
            cplat_local_lock_unlock(ctx->peers_mutex);
            return;
        }
        n1_update_path_recv(peer, sender_addr, path_idx);

        thread_recv_set_all_path_ping_states(peer->path_ping_state, POTR_MAX_PATH, POTR_PING_STATE_UNDEFINED);
        memset((void *)peer->remote_path_ping_state, 0, sizeof(peer->remote_path_ping_state));
        thread_recv_disconnect_peer_all_paths(ctx, peer);
        peer->reorder_pending = 0;
        potr_internal_window_recv_skip(&peer->recv_window, pkt->ack_num);
        slot_drain_recv_window(&peer_slot);
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

    n1_update_path_recv(peer, sender_addr, path_idx);

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

    if (pkt->flags & POTR_FLAG_PING)
    {
        int ping_state_changed;

        /* ヘルスチェック タイムアウト用受信時刻と受信状態を更新する (PING 限定) */
        ping_state_changed = slot_update_path_health(&peer_slot, path_idx);

        /* PING ペイロード (相手端のパス受信状態) を格納する */
        if (pkt->payload_len >= POTR_MAX_PATH && pkt->payload != NULL)
        {
            apply_remote_path_ping_state_payload(peer->remote_path_ping_state, pkt->payload, POTR_MAX_PATH);
        }

        sync_peer_path_state(ctx, peer);

        if (pkt->seq_num != peer->recv_window.next_seq &&
            potr_internal_seqnum_in_window(pkt->seq_num, peer->recv_window.next_seq + 1U, peer->recv_window.window_size))
        {
            if (slot_gap_ready(&peer_slot, peer->recv_window.next_seq))
            {
                n1_send_nack(ctx, peer, peer->recv_window.next_seq);
            }
        }
        cplat_local_lock_unlock(ctx->peers_mutex);
        if (ping_state_changed)
        {
            potr_internal_health_thread_wake(ctx);
        }
        return;
    }

    slot_process_outer_pkt(&peer_slot, pkt, path_idx);
    cplat_local_lock_unlock(ctx->peers_mutex);
}

/* 送信者ロール: NACK のみ処理する。
   戻り値 1: パケットを消費した (呼び出し元は次のパケットへ)。
   戻り値 0: UNICAST_BIDIR SENDER のため受信者処理へフォール スルーする。 */
static int sender_handle_packet(potr_context *ctx, const potr_packet *pkt)
{
    /* RAW モードは再送バッファーを持たないため NACK を無視する */
    if (potr_is_raw_type(ctx->service.type))
    {
        return 1;
    }

    if (pkt->flags & POTR_FLAG_NACK)
    {
        int j;

        /* 同一 ack_num の NACK が POTR_NACK_DEDUP_MS 以内に届いた場合は破棄 */
        {
            uint64_t now_ms = cplat_get_monotonic_ms();
            int dedup_idx;
            int is_dup = 0;

            for (dedup_idx = 0; dedup_idx < (int)POTR_NACK_DEDUP_SLOTS; dedup_idx++)
            {
                const potr_internal_nack_dedup_entry *e = &ctx->nack_dedup_buf[dedup_idx];
                if (e->time_ms != 0 && e->ack_num == pkt->ack_num &&
                    (now_ms - e->time_ms) < (uint64_t)POTR_NACK_DEDUP_MS)
                {
                    is_dup = 1;
                    break;
                }
            }

            if (is_dup)
            {
                return 1;
            }

            ctx->nack_dedup_buf[ctx->nack_dedup_next].ack_num = pkt->ack_num;
            ctx->nack_dedup_buf[ctx->nack_dedup_next].time_ms = now_ms;
            ctx->nack_dedup_next = (uint8_t)((ctx->nack_dedup_next + 1U) % POTR_NACK_DEDUP_SLOTS);
        }

        {
            potr_packet resend_pkt;
            int get_result;
            size_t wire_len = 0;

            /* send_window へのアクセスを排他制御する (送信スレッド・ヘルスチェック スレッドと競合)。
               ミューテックス保持中に recv_buf へ wire データを組み立て、
               プール スロットが上書きされる前にコピーを完了させる。 */
            cplat_local_lock_lock(ctx->send_window_mutex, CPLAT_SYNC_WAIT_FOREVER);
            get_result = potr_internal_window_send_get(&ctx->send_window, pkt->ack_num, &resend_pkt);

            if (get_result == POTR_OK)
            {
                /* [NBO ヘッダー][ペイロード] を recv_buf に組み立てる */
                wire_len = potr_internal_packet_wire_size(&resend_pkt);
                memcpy(ctx->recv_buf, &resend_pkt, PACKET_HEADER_SIZE);
                memcpy(ctx->recv_buf + PACKET_HEADER_SIZE, resend_pkt.payload, wire_len - PACKET_HEADER_SIZE);
            }

            cplat_local_lock_unlock(ctx->send_window_mutex);

            if (get_result == POTR_OK)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                           "sender[service_id=%" PRId64 "]: NACK received seq=%u"
                           " -> retransmit",
                           ctx->service.service_id, (unsigned)pkt->ack_num);
                for (j = 0; j < ctx->n_path; j++)
                {
                    size_t sent = 0;

                    (void)cplat_socket_sendto(ctx->sock[j], ctx->recv_buf, wire_len, &ctx->dest_addr[j], &sent,
                                                 NULL);
                }
            }
            else
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                           "sender[service_id=%" PRId64 "]: NACK seq=%u not in window"
                           " -> REJECT",
                           ctx->service.service_id, (unsigned)pkt->ack_num);
                send_reject(ctx, pkt->ack_num);
            }
        }
    }

    /* ACK・その他 (DATA/PING の反射など) は無視 */
    if (ctx->service.type != POTR_TYPE_UNICAST_BIDIR)
    {
        return 1;
    }

    /* UNICAST_BIDIR SENDER: 受信者処理 (FIN/REJECT/DATA/PING) へフォール スルー */
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

    /* REJECT: 欠落外側パケットをスキップして後続パケットを配信する */
    if (pkt->flags & POTR_FLAG_REJECT)
    {
        /* RAW モードは REJECT を発生させない (念のため無視) */
        if (potr_is_raw_type(ctx->service.type))
        {
            return;
        }

        if (!thread_recv_session_adopt(svc_slot, pkt))
        {
            return;
        }

        /* 送信元から受信できている = 生存確認としてタイムアウトをリセットする */
        update_path_recv(ctx, path_idx, sender_addr);

        POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                   "recv[service_id=%" PRId64 "]: REJECT received seq=%u"
                   " (packet unrecoverable)",
                   ctx->service.service_id, (unsigned)pkt->ack_num);

        thread_recv_set_all_path_ping_states(ctx->path_ping_state, POTR_MAX_PATH, POTR_PING_STATE_UNDEFINED);
        memset((void *)ctx->remote_path_ping_state, 0, sizeof(ctx->remote_path_ping_state));
        thread_recv_disconnect_service_all_paths(ctx);

        /* 欠落外側パケットをスキップして recv_window を前進させる */
        ctx->reorder_pending = 0;
        potr_internal_window_recv_skip(&ctx->recv_window, pkt->ack_num);

        /* 後続パケットを配信 (ウィンドウに溜まっていれば最初の pop で CONNECTED を発火) */
        slot_drain_recv_window(svc_slot);
        return;
    }

    /* NACK / ACK は受信者では無視 */
    if (!(pkt->flags & (POTR_FLAG_DATA | POTR_FLAG_PING)))
    {
        return;
    }

    /* セッション照合 */
    if (!thread_recv_session_adopt(svc_slot, pkt))
    {
        return;
    }

    /* peer_port と送信元アドレスを更新 */
    update_path_recv(ctx, path_idx, sender_addr);

    {
        const char *pkt_kind_str;
        if (pkt->flags & POTR_FLAG_PING)
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

    if (pkt->flags & POTR_FLAG_PING)
    {
        int ping_state_changed;

        /* ヘルスチェック タイムアウト用受信時刻と受信状態を更新する (PING 限定) */
        ping_state_changed = slot_update_path_health(svc_slot, path_idx);

        /* PING ペイロード (相手端のパス受信状態) を格納する */
        if (pkt->payload_len >= POTR_MAX_PATH && pkt->payload != NULL)
        {
            apply_remote_path_ping_state_payload(ctx->remote_path_ping_state, pkt->payload, POTR_MAX_PATH);
        }

        /* PING はウィンドウ外: NACK・再送の対象外。
           seq_num は送信側の next_seq (消費前) を示す。
           通常モード: [recv_window.next_seq, seq_num) の範囲を全スキャンして欠番を一括 NACK する。
           RAW モード: ギャップがあれば DISCONNECTED を発行してウィンドウを新基点にリセットする。 */
        if (potr_is_raw_type(ctx->service.type))
        {
            /* pkt->seq_num が next_seq より前方にある (= ギャップあり) か判定する。
               potr_internal_seqnum_in_window で window_size 以内の前方範囲のみを対象とし、
               古い PING (next_seq より後方) は無視する。
               reorder_timeout_ms > 0 のとき、タイムアウト前は DISCONNECTED を保留する。 */
            if (pkt->seq_num != ctx->recv_window.next_seq &&
                potr_internal_seqnum_in_window(pkt->seq_num, ctx->recv_window.next_seq + 1U, ctx->recv_window.window_size))
            {
                if (reorder_gap_ready(ctx, ctx->recv_window.next_seq))
                {
                    raw_session_disconnect(ctx);
                    potr_internal_window_recv_reset(&ctx->recv_window, pkt->seq_num);
                }
                /* else: リオーダー待機中。次の PING/DATA 到着時に再判定する。 */
            }
            else
            {
                ctx->reorder_pending = 0; /* ギャップなし */
            }
            sync_service_path_state(ctx);
        }
        else
        {
            if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
            {
                wake_udp_interrupt_ping_if_needed(ctx, ping_state_changed);
            }
            sync_service_path_state(ctx);

            {
                /* 先頭欠番のリオーダー待機を確認してから NACK スキャンを行う。
                   最初の欠番が待機中の場合はその後続の欠番も一括保留する。
                   スキャン完了 (全有効または全 NACK 送出済み) 時にリオーダー状態をクリアする。 */
                uint32_t scan_seq = ctx->recv_window.next_seq;
                while (scan_seq != pkt->seq_num &&
                       potr_internal_seqnum_in_window(scan_seq, ctx->recv_window.base_seq, ctx->recv_window.window_size))
                {
                    uint16_t idx = (uint16_t)((scan_seq - ctx->recv_window.base_seq) % ctx->recv_window.window_size);
                    if (!ctx->recv_window.valid[idx])
                    {
                        if (reorder_gap_ready(ctx, scan_seq))
                        {
                            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                                       "recv[service_id=%" PRId64 "]: NACK seq=%u (PING gap scan)",
                                       ctx->service.service_id, (unsigned)scan_seq);
                            send_nack(ctx, scan_seq);
                        }
                        else
                        {
                            break; /* リオーダー待機中: 後続欠番も保留 */
                        }
                    }
                    scan_seq++;
                }
                /* スキャン完了: リオーダー状態をクリア */
                if (scan_seq == pkt->seq_num)
                {
                    ctx->reorder_pending = 0;
                }
            }
        }
    }
    else
    {
        /* DATA: ウィンドウ経由で順序整列・配信 (RAW モードも同じ経路。
           RAW モードではギャップ検出時に NACK の代わりに DISCONNECTED を発行する)。 */
        slot_process_outer_pkt(svc_slot, pkt, path_idx);
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
                check_health_timeout(ctx);
                if (ctx->global.reorder_timeout_ms > 0U)
                {
                    check_reorder_timeout(ctx);
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
                if (sender_handle_packet(ctx, &pkt))
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
        sync_service_path_state(ctx);
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
                        sync_service_path_state(ctx);
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
