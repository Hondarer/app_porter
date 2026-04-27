/**
 *******************************************************************************
 *  @file           potrRecvThread.c
 *  @brief          受信スレッドモジュール。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>

#if defined(PLATFORM_LINUX)
    #include <arpa/inet.h>
    #include <sys/select.h>
#endif /* PLATFORM_LINUX */

#include <porter_const.h>

#include "../protocol/packet.h"
#include "../protocol/seqnum.h"
#include "../protocol/window.h"
#include "../potrContext.h"
#include "../potrPathEvent.h"
#include "../potrPeerTable.h"
#include "potrHealthThread.h"
#include "potrRecvThread.h"
#include <com_util/compress/compress.h>
#include <com_util/crypto/crypto.h>
#include "../infra/potrTrace.h"
#include "../infra/potrPlatform.h"

/* 前方宣言: 後で定義される関数 */
static void recv_deliver(struct PotrContext_ *ctx, const uint8_t *payload,
                         size_t payload_len, int compressed);
static void send_nack(struct PotrContext_ *ctx, uint32_t nack_seq);
static void raw_session_disconnect(struct PotrContext_ *ctx);
static int set_path_ping_state(volatile uint8_t *state, uint8_t next_state);
static int set_all_path_ping_states(volatile uint8_t *states,
                                    size_t            count,
                                    uint8_t           next_state);
static void wake_udp_interrupt_ping_if_needed(struct PotrContext_ *ctx, int state_changed);
static void wake_tcp_interrupt_ping_if_needed(struct PotrContext_ *ctx,
                                              int                  path_idx,
                                              int                  state_changed);
static int send_tcp_fin_ack(struct PotrContext_ *ctx, uint32_t fin_target_seq);

static void sync_service_path_state(struct PotrContext_ *ctx)
{
    int                    next_states[POTR_MAX_PATH];
    PotrPreparedPathEvents prepared;

    if (potr_is_tcp_type(ctx->service.type))
    {
        potr_copy_tcp_path_states(ctx, next_states);
    }
    else if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
    {
        potr_copy_bidir_udp_path_states(ctx, next_states);
    }
    else
    {
        potr_copy_oneway_path_states(ctx, next_states);
    }

    COM_UTIL_MUTEX_LOCK(&ctx->callback_mutex);
    potr_sync_service_path_state_locked(ctx, next_states, &prepared);
    potr_emit_service_path_events_locked(ctx, &prepared);
    COM_UTIL_MUTEX_UNLOCK(&ctx->callback_mutex);
}

static void disconnect_service_all_paths(struct PotrContext_ *ctx)
{
    int                    next_states[POTR_MAX_PATH];
    PotrPreparedPathEvents prepared;

    potr_zero_path_states(next_states);
    COM_UTIL_MUTEX_LOCK(&ctx->callback_mutex);
    potr_sync_service_path_state_locked(ctx, next_states, &prepared);
    potr_emit_service_path_events_locked(ctx, &prepared);
    COM_UTIL_MUTEX_UNLOCK(&ctx->callback_mutex);
}

static void sync_peer_path_state(struct PotrContext_ *ctx, PotrPeerContext *peer)
{
    int                    next_states[POTR_MAX_PATH];
    PotrPreparedPathEvents prepared;

    potr_copy_bidir_n1_path_states(peer, next_states);
    COM_UTIL_MUTEX_LOCK(&ctx->callback_mutex);
    potr_sync_peer_path_state_locked(peer, next_states, &prepared);
    potr_emit_peer_path_events_locked(ctx, peer, &prepared);
    COM_UTIL_MUTEX_UNLOCK(&ctx->callback_mutex);
}

static void disconnect_peer_all_paths(struct PotrContext_ *ctx, PotrPeerContext *peer)
{
    int                    next_states[POTR_MAX_PATH];
    PotrPreparedPathEvents prepared;

    potr_zero_path_states(next_states);
    COM_UTIL_MUTEX_LOCK(&ctx->callback_mutex);
    potr_sync_peer_path_state_locked(peer, next_states, &prepared);
    potr_emit_peer_path_events_locked(ctx, peer, &prepared);
    COM_UTIL_MUTEX_UNLOCK(&ctx->callback_mutex);
}

static int tcp_send_all_with_mutex(PotrSocket fd, com_util_mutex_t *mtx,
                                   const uint8_t *buf, size_t len)
{
    int ret;
    COM_UTIL_MUTEX_LOCK(mtx);
    ret = potr_tcp_send(fd, buf, len);
    COM_UTIL_MUTEX_UNLOCK(mtx);
    return ret;
}

static int send_tcp_control_packet(struct PotrContext_ *ctx, PotrPacket *pkt,
                                   uint32_t nonce_val)
{
    uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
    size_t  wire_len;
    int     sent_any = 0;
    int     i;

    if (ctx == NULL || pkt == NULL)
    {
        return POTR_ERROR;
    }

    if (ctx->service.encrypt_enabled)
    {
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t  enc_out = POTR_CRYPTO_TAG_SIZE;
        uint32_t nonce_nbo = htonl(nonce_val);

        pkt->flags      |= htons(POTR_FLAG_ENCRYPTED);
        pkt->payload_len = htons((uint16_t)POTR_CRYPTO_TAG_SIZE);

        memcpy(nonce,      &pkt->session_id, 4);
        memcpy(nonce + 4,  &pkt->flags,      2);
        memcpy(nonce + 6,  &nonce_nbo,       4);
        memset(nonce + 10, 0,                2);

        memcpy(wire_buf, pkt, PACKET_HEADER_SIZE);
        if (com_util_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out,
                         NULL, 0,
                         ctx->service.encrypt_key,
                         nonce,
                         wire_buf, PACKET_HEADER_SIZE) != 0)
        {
            return POTR_ERROR;
        }
        wire_len = PACKET_HEADER_SIZE + enc_out;
    }
    else
    {
        memcpy(wire_buf, pkt, PACKET_HEADER_SIZE);
        wire_len = PACKET_HEADER_SIZE;
    }

    for (i = 0; i < ctx->n_path; i++)
    {
        if (ctx->tcp_conn_fd[i] == POTR_INVALID_SOCKET)
        {
            continue;
        }
        if (tcp_send_all_with_mutex(ctx->tcp_conn_fd[i], &ctx->tcp_send_mutex[i],
                                    wire_buf, wire_len) == POTR_SUCCESS)
        {
            sent_any = 1;
        }
    }

    return sent_any ? POTR_SUCCESS : POTR_ERROR;
}

static void notify_tcp_close_ack_received(struct PotrContext_ *ctx, uint32_t fin_target_seq)
{
    if (ctx == NULL)
    {
        return;
    }

    COM_UTIL_MUTEX_LOCK(&ctx->tcp_close_mutex);
    if (ctx->tcp_close_waiting_ack
        && ctx->tcp_close_wait_target_seq == fin_target_seq
        && !ctx->tcp_close_ack_received)
    {
        ctx->tcp_close_ack_received = 1;
        ctx->tcp_close_ack_seq      = fin_target_seq;
        COM_UTIL_COND_SIGNAL(&ctx->tcp_close_cv);
    }
    COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_close_mutex);
}

static int send_tcp_fin_ack(struct PotrContext_ *ctx, uint32_t fin_target_seq)
{
    PotrPacket           fin_ack_pkt;
    PotrPacketSessionHdr shdr;

    shdr.service_id      = ctx->service.service_id;
    shdr.session_id      = ctx->session_id;
    shdr.session_tv_sec  = ctx->session_tv_sec;
    shdr.session_tv_nsec = ctx->session_tv_nsec;

    if (packet_build_fin_ack(&fin_ack_pkt, &shdr, fin_target_seq) != POTR_SUCCESS)
    {
        return POTR_ERROR;
    }

    return send_tcp_control_packet(ctx, &fin_ack_pkt, fin_target_seq);
}

/* ================================================================
 * N:1 モード専用: ピアコンテキストを使ったパケット処理関数群
 * ================================================================ */

static void n1_send_nack(struct PotrContext_ *ctx, PotrPeerContext *peer,
                         uint32_t nack_seq)
{
    PotrPacket           nack_pkt;
    PotrPacketSessionHdr shdr;
    size_t               wire_len;
    int                  k;

    shdr.service_id      = ctx->service.service_id;
    shdr.session_id      = peer->session_id;
    shdr.session_tv_sec  = peer->session_tv_sec;
    shdr.session_tv_nsec = peer->session_tv_nsec;

    if (packet_build_nack(&nack_pkt, &shdr, nack_seq) != POTR_SUCCESS) return;

    if (ctx->service.encrypt_enabled)
    {
        uint8_t  wire_buf[PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
        uint8_t  nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t   enc_out = POTR_CRYPTO_TAG_SIZE;

        nack_pkt.flags      |= htons(POTR_FLAG_ENCRYPTED);
        nack_pkt.payload_len = htons((uint16_t)POTR_CRYPTO_TAG_SIZE);

        /* ノンス: session_id(4B) + flags(2B, NACK|ENCRYPTED NBO) + ack_num(4B NBO) + padding(2B) */
        memcpy(nonce,      &nack_pkt.session_id, 4);
        memcpy(nonce + 4,  &nack_pkt.flags,      2);
        memcpy(nonce + 6,  &nack_pkt.ack_num,    4);
        memset(nonce + 10, 0,                    2);

        memcpy(wire_buf, &nack_pkt, PACKET_HEADER_SIZE);
        if (com_util_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out,
                         NULL, 0,
                         ctx->service.encrypt_key,
                         nonce,
                         wire_buf, PACKET_HEADER_SIZE) != 0)
        {
            return;
        }
        wire_len = PACKET_HEADER_SIZE + enc_out;

        for (k = 0; k < (int)POTR_MAX_PATH; k++)
        {
            if (peer->dest_addr[k].sin_family == 0) continue;
            potr_sendto(ctx->sock[k], wire_buf, wire_len, 0,
                        (const struct sockaddr *)&peer->dest_addr[k],
                        (int)sizeof(peer->dest_addr[k]));
        }
    }
    else
    {
        wire_len = packet_wire_size(&nack_pkt);
        for (k = 0; k < (int)POTR_MAX_PATH; k++)
        {
            if (peer->dest_addr[k].sin_family == 0) continue;
            potr_sendto(ctx->sock[k], (const uint8_t *)&nack_pkt, wire_len, 0,
                        (const struct sockaddr *)&peer->dest_addr[k],
                        (int)sizeof(peer->dest_addr[k]));
        }
    }
}

static void n1_send_reject(struct PotrContext_ *ctx, PotrPeerContext *peer,
                            uint32_t seq_num)
{
    PotrPacket           reject_pkt;
    PotrPacketSessionHdr shdr;
    size_t               wire_len;
    int                  k;

    shdr.service_id      = ctx->service.service_id;
    shdr.session_id      = peer->session_id;
    shdr.session_tv_sec  = peer->session_tv_sec;
    shdr.session_tv_nsec = peer->session_tv_nsec;

    if (packet_build_reject(&reject_pkt, &shdr, seq_num) != POTR_SUCCESS) return;

    if (ctx->service.encrypt_enabled)
    {
        uint8_t  wire_buf[PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
        uint8_t  nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t   enc_out = POTR_CRYPTO_TAG_SIZE;

        reject_pkt.flags      |= htons(POTR_FLAG_ENCRYPTED);
        reject_pkt.payload_len = htons((uint16_t)POTR_CRYPTO_TAG_SIZE);

        /* ノンス: session_id(4B) + flags(2B, REJECT|ENCRYPTED NBO) + ack_num(4B NBO) + padding(2B) */
        memcpy(nonce,      &reject_pkt.session_id, 4);
        memcpy(nonce + 4,  &reject_pkt.flags,      2);
        memcpy(nonce + 6,  &reject_pkt.ack_num,    4);
        memset(nonce + 10, 0,                      2);

        memcpy(wire_buf, &reject_pkt, PACKET_HEADER_SIZE);
        if (com_util_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out,
                         NULL, 0,
                         ctx->service.encrypt_key,
                         nonce,
                         wire_buf, PACKET_HEADER_SIZE) != 0)
        {
            return;
        }
        wire_len = PACKET_HEADER_SIZE + enc_out;

        for (k = 0; k < (int)POTR_MAX_PATH; k++)
        {
            if (peer->dest_addr[k].sin_family == 0) continue;
            potr_sendto(ctx->sock[k], wire_buf, wire_len, 0,
                        (const struct sockaddr *)&peer->dest_addr[k],
                        (int)sizeof(peer->dest_addr[k]));
        }
    }
    else
    {
        wire_len = packet_wire_size(&reject_pkt);
        for (k = 0; k < (int)POTR_MAX_PATH; k++)
        {
            if (peer->dest_addr[k].sin_family == 0) continue;
            potr_sendto(ctx->sock[k], (const uint8_t *)&reject_pkt, wire_len, 0,
                        (const struct sockaddr *)&peer->dest_addr[k],
                        (int)sizeof(peer->dest_addr[k]));
        }
    }
}

/* N:1: リオーダー待機判定 (ピアごとの reorder 状態を使用) */
static int n1_reorder_gap_ready(PotrPeerContext *peer, uint32_t nack_num)
{
    int64_t  now_sec;
    int32_t  now_nsec;
    uint32_t ms = 0; /* peer には global.reorder_timeout_ms へのアクセスがないため ctx から渡さない */

    /* シンプルな実装: 常に即時 (待機なし) */
    (void)peer;
    (void)nack_num;
    (void)now_sec;
    (void)now_nsec;
    (void)ms;
    return 1;
}

/* N:1: データを解凍してコールバックに渡す */
static void n1_recv_deliver(struct PotrContext_ *ctx, PotrPeerContext *peer,
                             const uint8_t *payload, size_t payload_len,
                             int compressed)
{
    if (compressed)
    {
        size_t dec_len = ctx->compress_buf_size;

        if (com_util_decompress(ctx->compress_buf, &dec_len,
                            payload, payload_len) == 0)
        {
            potr_callback_emit(ctx, peer->peer_id,
                               POTR_EVENT_DATA, ctx->compress_buf, dec_len);
        }
        else
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                     "recv[service_id=%" PRId64 "]: peer=%u decompress failed",
                     ctx->service.service_id, (unsigned)peer->peer_id);
        }
    }
    else
    {
        potr_callback_emit(ctx, peer->peer_id,
                           POTR_EVENT_DATA, payload, payload_len);
    }
}

/* N:1: ペイロードエレメントをフラグメント結合してコールバックに渡す */
static void n1_deliver_payload_elem(struct PotrContext_ *ctx, PotrPeerContext *peer,
                                     const PotrPacket *elem)
{
    /* 未接続ピアの DATA は破棄する */
    if (!peer->health_alive) return;

    if (elem->flags & POTR_FLAG_MORE_FRAG)
    {
        if (peer->frag_buf_len + elem->payload_len <= ctx->global.max_message_size)
        {
            if (peer->frag_buf_len == 0)
            {
                peer->frag_compressed = (elem->flags & POTR_FLAG_COMPRESSED) ? 1 : 0;
            }
            memcpy(peer->frag_buf + peer->frag_buf_len,
                   elem->payload, elem->payload_len);
            peer->frag_buf_len += elem->payload_len;
        }
        else
        {
            peer->frag_buf_len    = 0;
            peer->frag_compressed = 0;
        }
    }
    else if (peer->frag_buf_len > 0)
    {
        if (peer->frag_buf_len + elem->payload_len <= ctx->global.max_message_size)
        {
            memcpy(peer->frag_buf + peer->frag_buf_len,
                   elem->payload, elem->payload_len);
            peer->frag_buf_len += elem->payload_len;

            if (ctx->callback != NULL)
            {
                n1_recv_deliver(ctx, peer, peer->frag_buf,
                                peer->frag_buf_len, peer->frag_compressed);
            }
        }
        peer->frag_buf_len    = 0;
        peer->frag_compressed = 0;
    }
    else
    {
        if (ctx->callback != NULL)
        {
            n1_recv_deliver(ctx, peer, elem->payload, (size_t)elem->payload_len,
                            (elem->flags & POTR_FLAG_COMPRESSED) ? 1 : 0);
        }
    }
}

static int recv_window_reached_fin_target(uint32_t next_seq, uint32_t fin_target_seq)
{
    return next_seq == fin_target_seq;
}

static int fin_packet_has_target(const PotrPacket *pkt)
{
    return (pkt->flags & POTR_FLAG_FIN_TARGET_VALID) != 0;
}

static void clear_pending_fin_peer(PotrPeerContext *peer)
{
    peer->pending_fin    = 0;
    peer->fin_target_seq = 0;
}

static void clear_pending_fin_ctx(struct PotrContext_ *ctx)
{
    ctx->pending_fin    = 0;
    ctx->fin_target_seq = 0;
}

/* N:1: FIN による DISCONNECTED 発火とピア解放を行う。peers_mutex 保護下で呼ぶこと。 */
static void n1_fire_disconnected_by_fin(struct PotrContext_ *ctx, PotrPeerContext *peer)
{
    disconnect_peer_all_paths(ctx, peer);
    clear_pending_fin_peer(peer);
    peer_free(ctx, peer);
}

/* N:1: 受信ウィンドウからパケットを取り出して配信する */
static void n1_drain_recv_window(struct PotrContext_ *ctx, PotrPeerContext *peer)
{
    PotrPacket pop_pkt;

    while (window_recv_pop(&peer->recv_window, &pop_pkt) == POTR_SUCCESS)
    {
        if (pop_pkt.flags & POTR_FLAG_PING) continue;

        {
            size_t     offset = 0;
            PotrPacket elem;
            while (packet_unpack_next(&pop_pkt, &offset, &elem) == POTR_SUCCESS)
            {
                n1_deliver_payload_elem(ctx, peer, &elem);
            }
        }
    }

    if (peer->pending_fin
        && recv_window_reached_fin_target(peer->recv_window.next_seq, peer->fin_target_seq))
    {
        n1_fire_disconnected_by_fin(ctx, peer);
    }
}

/* N:1: DATA/PING を recv_window に投入して NACK 処理を行う */
static void n1_process_outer_pkt(struct PotrContext_ *ctx, PotrPeerContext *peer,
                                   const PotrPacket *pkt)
{
    uint32_t nack_num;
    uint32_t stretch;

    if (window_recv_push(&peer->recv_window, pkt) != POTR_SUCCESS)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "recv[service_id=%" PRId64 "]: peer=%u recv_window full, dropping seq=%u",
                 ctx->service.service_id, (unsigned)peer->peer_id,
                 (unsigned)pkt->seq_num);
        return;
    }

    stretch = (uint32_t)(pkt->seq_num - peer->recv_window.base_seq) + 1U;
    if (stretch * 10U >= (uint32_t)peer->recv_window.window_size * 8U)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_WARNING,
                 "recv[service_id=%" PRId64 "]: peer=%u recv_window utilization high (%u/%u)",
                 ctx->service.service_id, (unsigned)peer->peer_id,
                 (unsigned)stretch, (unsigned)peer->recv_window.window_size);
    }

    if (window_recv_needs_nack(&peer->recv_window, &nack_num))
    {
        if (n1_reorder_gap_ready(peer, nack_num))
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                     "recv[service_id=%" PRId64 "]: peer=%u NACK seq=%u",
                     ctx->service.service_id, (unsigned)peer->peer_id,
                     (unsigned)nack_num);
            n1_send_nack(ctx, peer, nack_num);
        }
    }
    else
    {
        peer->reorder_pending = 0;
    }

    n1_drain_recv_window(ctx, peer);

    if (window_recv_needs_nack(&peer->recv_window, &nack_num))
    {
        if (n1_reorder_gap_ready(peer, nack_num))
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                     "recv[service_id=%" PRId64 "]: peer=%u NACK seq=%u (post-drain)",
                     ctx->service.service_id, (unsigned)peer->peer_id,
                     (unsigned)nack_num);
            n1_send_nack(ctx, peer, nack_num);
        }
    }
    else
    {
        peer->reorder_pending = 0;
    }
}

/* N:1: セッション照合・更新 (ピアコンテキスト版) */
static int n1_check_and_update_session(struct PotrContext_ *ctx,
                                        PotrPeerContext     *peer,
                                        const PotrPacket    *pkt)
{
    if (!peer->peer_session_known)
    {
        peer->peer_session_id      = pkt->session_id;
        peer->peer_session_tv_sec  = pkt->session_tv_sec;
        peer->peer_session_tv_nsec = pkt->session_tv_nsec;
        peer->peer_session_known   = 1;
        peer->reorder_pending      = 0;
        clear_pending_fin_peer(peer);
        window_init(&peer->recv_window, pkt->seq_num,
                    ctx->global.window_size, ctx->global.max_payload);
        return 1;
    }

    if (pkt->session_tv_sec > peer->peer_session_tv_sec)
    {
        /* 新セッション: フォールスルーして採用 */
    }
    else if (pkt->session_tv_sec < peer->peer_session_tv_sec)
    {
        return 0;
    }
    else if (pkt->session_tv_nsec > peer->peer_session_tv_nsec)
    {
        /* 新セッション: フォールスルーして採用 */
    }
    else if (pkt->session_tv_nsec < peer->peer_session_tv_nsec)
    {
        return 0;
    }
    else if (pkt->session_id > peer->peer_session_id)
    {
        /* 新セッション: フォールスルーして採用 */
    }
    else
    {
        return (pkt->session_id == peer->peer_session_id) ? 1 : 0;
    }

    peer->peer_session_id      = pkt->session_id;
    peer->peer_session_tv_sec  = pkt->session_tv_sec;
    peer->peer_session_tv_nsec = pkt->session_tv_nsec;
    peer->reorder_pending      = 0;
    clear_pending_fin_peer(peer);
    window_init(&peer->recv_window, pkt->seq_num,
                ctx->global.window_size, ctx->global.max_payload);
    return 1;
}

/* N:1: 送信元アドレスを記録し、未知のパスを学習する */
static void n1_update_path_recv(PotrPeerContext          *peer,
                                 const struct sockaddr_in *sender_addr,
                                 int                       path_idx)
{
    if (peer->dest_addr[path_idx].sin_family == AF_INET)
    {
        /* 既知パス: ポートを更新 */
        peer->dest_addr[path_idx].sin_port = sender_addr->sin_port;
    }
    else
    {
        /* 新規パス: インデックス path_idx のスロットに直接記録 */
        peer->dest_addr[path_idx] = *sender_addr;
        peer->n_paths++;
        POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,
                 "n1_update_path_recv: peer=%u path %d learned",
                 (unsigned)peer->peer_id, path_idx);
    }
}

/* N:1: パスごとのヘルスチェック受信時刻と受信状態を更新する。
   type 8 では PING のみをヘルス信号として扱うため、PING 受信時のみ呼ぶこと。 */
static int n1_update_path_health(PotrPeerContext *peer, int path_idx)
{
    int64_t s;
    int32_t ns;

    com_util_get_monotonic(&s, &ns);
    peer->last_recv_tv_sec               = s;
    peer->last_recv_tv_nsec              = ns;
    peer->path_last_recv_sec[path_idx]   = s;
    peer->path_last_recv_nsec[path_idx]  = ns;
    return set_path_ping_state(&peer->path_ping_state[path_idx],
                               POTR_PING_STATE_NORMAL);
}

/* N:1: select() タイムアウト時にヘルスタイムアウトを確認する */
static void n1_check_health_timeout(struct PotrContext_ *ctx)
{
    int64_t now_sec;
    int32_t now_nsec;
    int i;

    com_util_get_monotonic(&now_sec, &now_nsec);
    int k;
    int should_wake_health = 0;

    if (ctx->health_timeout_ms == 0) return;

    COM_UTIL_MUTEX_LOCK(&ctx->peers_mutex);

    for (i = 0; i < ctx->max_peers; i++)
    {
        int64_t elapsed_ms;
        int     path_state_changed = 0;

        if (!ctx->peers[i].active) continue;
        if (!ctx->peers[i].health_alive) continue;

        /* パス単位のタイムアウト: 不通パスを dest_addr から削除する */
        for (k = 0; k < (int)POTR_MAX_PATH; k++)
        {
            int64_t path_elapsed;

            if (ctx->peers[i].dest_addr[k].sin_family == 0) continue; /* 未使用 */
            if (ctx->peers[i].path_last_recv_sec[k]   == 0) continue; /* 初回受信前 */

            path_elapsed = (now_sec  - ctx->peers[i].path_last_recv_sec[k])  * 1000LL
                         + (now_nsec - ctx->peers[i].path_last_recv_nsec[k]) / 1000000L;

            if (path_elapsed >= (int64_t)ctx->health_timeout_ms)
            {
                int state_changed =
                    set_path_ping_state(&ctx->peers[i].path_ping_state[k],
                                        POTR_PING_STATE_ABNORMAL);
                should_wake_health |= state_changed;
                path_state_changed |= state_changed;
                peer_path_clear(ctx, &ctx->peers[i], k);
            }
        }

        if (path_state_changed)
        {
            sync_peer_path_state(ctx, &ctx->peers[i]);
        }

        /* ピア単位のタイムアウト: 全パス消滅、または最終受信から切断判定 */
        if (ctx->peers[i].last_recv_tv_sec == 0) continue;

        elapsed_ms = (now_sec  - ctx->peers[i].last_recv_tv_sec)  * 1000LL
                   + (now_nsec - ctx->peers[i].last_recv_tv_nsec) / 1000000L;

        if (elapsed_ms >= (int64_t)ctx->health_timeout_ms)
        {
            PotrPeerId dead_id = ctx->peers[i].peer_id;

            POTR_LOG(COM_UTIL_LOG_LEVEL_WARNING,
                     "recv[service_id=%" PRId64 "]: peer=%u DISCONNECTED (timeout %lldms)",
                     ctx->service.service_id, (unsigned)dead_id,
                     (long long)elapsed_ms);

            memset((void *)ctx->peers[i].remote_path_ping_state, 0,
                   sizeof(ctx->peers[i].remote_path_ping_state));
            disconnect_peer_all_paths(ctx, &ctx->peers[i]);
            peer_free(ctx, &ctx->peers[i]);
        }
    }

    COM_UTIL_MUTEX_UNLOCK(&ctx->peers_mutex);

    if (should_wake_health)
    {
        potr_health_thread_wake(ctx);
    }
}

/* ================================================================
 * N:1 モード専用ここまで
 * ================================================================ */

/* 受信パケットの暗号化要件と GCM 認証を検証する。
   encrypt_enabled 時は ENCRYPTED フラグを必須とし、成功時のみ後続処理へ進める。 */
static int recv_authenticate_packet(struct PotrContext_ *ctx,
                                    PotrPacket          *pkt,
                                    const uint8_t       *wire_hdr,
                                    const char          *log_prefix,
                                    int                  path_idx)
{
    if (!(pkt->flags & POTR_FLAG_ENCRYPTED))
    {
        if (!ctx->service.encrypt_enabled)
        {
            return 1;
        }

        if (path_idx >= 0)
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                     "%s[service_id=%" PRId64 " path=%d]: missing ENCRYPTED flag, dropping flags=0x%04x",
                     log_prefix, ctx->service.service_id, path_idx,
                     (unsigned)pkt->flags);
        }
        else
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                     "%s[service_id=%" PRId64 "]: missing ENCRYPTED flag, dropping flags=0x%04x",
                     log_prefix, ctx->service.service_id, (unsigned)pkt->flags);
        }
        return 0;
    }

    if ((pkt->flags & POTR_FLAG_ENCRYPTED)
        && (pkt->flags & (POTR_FLAG_DATA | POTR_FLAG_PING)))
    {
        uint8_t  nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t   dec_len = ctx->crypto_buf_size;
        uint32_t sid_nbo   = htonl(pkt->session_id);
        uint16_t flags_nbo = htons((uint16_t)pkt->flags);
        uint32_t seq_nbo   = htonl(pkt->seq_num);

        memcpy(nonce,      &sid_nbo,   4);
        memcpy(nonce + 4,  &flags_nbo, 2);
        memcpy(nonce + 6,  &seq_nbo,   4);
        memset(nonce + 10, 0,          2);

        if (com_util_decrypt(ctx->crypto_buf, &dec_len,
                         pkt->payload, pkt->payload_len,
                         ctx->service.encrypt_key,
                         nonce,
                         wire_hdr, PACKET_HEADER_SIZE) != 0)
        {
            if (path_idx >= 0)
            {
                POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                         "%s[service_id=%" PRId64 " path=%d]: decrypt failed (auth) seq=%u",
                         log_prefix, ctx->service.service_id, path_idx,
                         (unsigned)pkt->seq_num);
            }
            else
            {
                POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                         "%s[service_id=%" PRId64 "]: decrypt failed (auth) seq=%u",
                         log_prefix, ctx->service.service_id, (unsigned)pkt->seq_num);
            }
            return 0;
        }

        pkt->payload     = ctx->crypto_buf;
        pkt->payload_len = (uint16_t)dec_len;
        pkt->flags       = (uint16_t)(pkt->flags & ~POTR_FLAG_ENCRYPTED);
        return 1;
    }

    if (pkt->payload_len != POTR_CRYPTO_TAG_SIZE)
    {
        if (path_idx >= 0)
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                     "%s[service_id=%" PRId64 " path=%d]: encrypted control pkt bad len=%u flags=0x%04x",
                     log_prefix, ctx->service.service_id, path_idx,
                     (unsigned)pkt->payload_len, (unsigned)pkt->flags);
        }
        else
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                     "%s[service_id=%" PRId64 "]: encrypted control pkt bad len=%u flags=0x%04x",
                     log_prefix, ctx->service.service_id,
                     (unsigned)pkt->payload_len, (unsigned)pkt->flags);
        }
        return 0;
    }

    {
        uint8_t  nonce[POTR_CRYPTO_NONCE_SIZE];
        uint8_t  dummy[1];
        size_t   dummy_len = sizeof(dummy);
        uint32_t val;
        uint32_t sid_nbo   = htonl(pkt->session_id);
        uint16_t flags_nbo = htons((uint16_t)pkt->flags);
        uint32_t val_nbo;

        val = (pkt->flags & (POTR_FLAG_NACK | POTR_FLAG_REJECT | POTR_FLAG_FIN_ACK))
              ? pkt->ack_num : pkt->seq_num;
        val_nbo = htonl(val);

        memcpy(nonce,      &sid_nbo,   4);
        memcpy(nonce + 4,  &flags_nbo, 2);
        memcpy(nonce + 6,  &val_nbo,   4);
        memset(nonce + 10, 0,          2);

        if (com_util_decrypt(dummy, &dummy_len,
                         pkt->payload, POTR_CRYPTO_TAG_SIZE,
                         ctx->service.encrypt_key,
                         nonce,
                         wire_hdr, PACKET_HEADER_SIZE) != 0)
        {
            if (path_idx >= 0)
            {
                POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                         "%s[service_id=%" PRId64 " path=%d]: tag verify failed flags=0x%04x",
                         log_prefix, ctx->service.service_id, path_idx,
                         (unsigned)pkt->flags);
            }
            else
            {
                POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                         "%s[service_id=%" PRId64 "]: tag verify failed flags=0x%04x",
                         log_prefix, ctx->service.service_id, (unsigned)pkt->flags);
            }
            return 0;
        }
    }

    pkt->flags       = (uint16_t)(pkt->flags & ~POTR_FLAG_ENCRYPTED);
    pkt->payload_len = 0;
    pkt->payload     = NULL;
    return 1;
}

/* 送信元 IP が期待アドレスのいずれかと一致するか確認する。
   N:1 モード: src_port 指定時は送信元ポートのみでフィルタリング。未指定時は全許可。
   UNICAST_BIDIR SENDER:   受信パケットの送信元は RECEIVER (dst_addr_resolved) と照合する。
   UNICAST_BIDIR RECEIVER: 受信パケットの送信元は SENDER   (src_addr_resolved) と照合する。
   その他: src_addr_resolved と照合する。src_addr が未設定の場合は常に 1 (合格) を返す。 */
static int check_src_addr(const struct PotrContext_ *ctx,
                           const struct sockaddr_in  *sender)
{
    int i;

    /* N:1 モード: src_port 指定時はポートのみでフィルタリング、未指定時は全許可 */
    if (ctx->is_multi_peer)
    {
        if (ctx->service.src_port != 0)
            return (ntohs(sender->sin_port) == ctx->service.src_port) ? 1 : 0;
        return 1;
    }

    if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
    {
        if (ctx->role == POTR_ROLE_SENDER)
        {
            /* SENDER が受け取るパケット: RECEIVER (dst_addr) から来る */
            if (ctx->service.dst_addr[0][0] == '\0') return 1;
            for (i = 0; i < ctx->n_path; i++)
            {
                if (sender->sin_addr.s_addr == ctx->dst_addr_resolved[i].s_addr)
                    return 1;
            }
        }
        else
        {
            /* RECEIVER が受け取るパケット: SENDER (src_addr) から来る */
            if (ctx->service.src_addr[0][0] == '\0') return 1;
            for (i = 0; i < ctx->n_path; i++)
            {
                if (sender->sin_addr.s_addr == ctx->src_addr_resolved[i].s_addr)
                    return 1;
            }
        }
        return 0;
    }

    if (ctx->service.src_addr[0][0] == '\0')
    {
        return 1;
    }
    for (i = 0; i < ctx->n_path; i++)
    {
        if (sender->sin_addr.s_addr == ctx->src_addr_resolved[i].s_addr)
        {
            return 1;
        }
    }
    return 0;
}

/* セッションの採用判定を行い、必要であればコンテキストの相手セッション情報を更新する。
   採用すべきセッションなら 1、破棄すべき旧セッションなら 0 を返す。 */
static int check_and_update_session(struct PotrContext_ *ctx,
                                    const PotrPacket    *pkt)
{
    if (!ctx->peer_session_known)
    {
        /* 初回受信 (または FIN/タイムアウト後の再接続): セッション採用 + ウィンドウをリセット。
           pkt->seq_num で初期化することで、送信者の現在位置に直接同期し
           NACK/REJECT サイクルなしに再加入できる。
             DATA 着信時: window_init(DATA.seq_num) → push → pop → 即時 CONNECTED
             PING 着信時: window_init(PING.seq_num) → gap スキャン範囲がゼロ → NACK なし
           FIN/タイムアウト後は送信者が同一セッションのまま任意の seq から
           再開する可能性があるため pkt->seq_num を使用する。 */
        ctx->peer_session_id      = pkt->session_id;
        ctx->peer_session_tv_sec  = pkt->session_tv_sec;
        ctx->peer_session_tv_nsec = pkt->session_tv_nsec;
        ctx->peer_session_known   = 1;
        ctx->reorder_pending      = 0;
        clear_pending_fin_ctx(ctx);
        POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                 "recv[service_id=%" PRId64 "]: new session (first contact), new_id=%u seq=%u",
                 ctx->service.service_id,
                 pkt->session_id, (unsigned)pkt->seq_num);
        window_init(&ctx->recv_window, pkt->seq_num,
                    ctx->global.window_size, ctx->global.max_payload);
        return 1;
    }

    /* (session_tv_sec, session_tv_nsec, session_id) の辞書順で新旧を判定する。
       - pkt > 既知セッション: 新セッション。return せずにフォールスルーし、
         関数末尾の「新セッション採用」ブロックで採用処理を行う。
       - pkt < 既知セッション: 旧セッション。即 return 0 で破棄する。
       - pkt == 既知セッション: 同一セッション。即 return 1 で通常受信を継続する。
       新セッションと判定された分岐は LOG のみで return しないため、
       if-else チェーンを抜けた後に必ず末尾の採用ブロックに到達する。 */
    if (pkt->session_tv_sec > ctx->peer_session_tv_sec)
    {
        /* 新セッション (tv_sec が大): フォールスルーして採用 */
        POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                 "recv[service_id=%" PRId64 "]: new session (tv_sec %lld > %lld)"
                 ", old_id=%u new_id=%u",
                 ctx->service.service_id,
                 (long long)pkt->session_tv_sec, (long long)ctx->peer_session_tv_sec,
                 ctx->peer_session_id, pkt->session_id);
    }
    else if (pkt->session_tv_sec < ctx->peer_session_tv_sec)
    {
        return 0; /* 旧セッション (tv_sec が小): 破棄 */
    }
    else if (pkt->session_tv_nsec > ctx->peer_session_tv_nsec)
    {
        /* 新セッション (tv_sec 同一・tv_nsec が大): フォールスルーして採用 */
        POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                 "recv[service_id=%" PRId64 "]: new session (tv_nsec %d > %d)"
                 ", old_id=%u new_id=%u",
                 ctx->service.service_id,
                 pkt->session_tv_nsec, ctx->peer_session_tv_nsec,
                 ctx->peer_session_id, pkt->session_id);
    }
    else if (pkt->session_tv_nsec < ctx->peer_session_tv_nsec)
    {
        return 0; /* 旧セッション (tv_sec 同一・tv_nsec が小): 破棄 */
    }
    else if (pkt->session_id > ctx->peer_session_id)
    {
        /* 新セッション (タイムスタンプ完全一致・session_id が大): フォールスルーして採用 */
        POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                 "recv[service_id=%" PRId64 "]: new session (id tiebreak %u > %u)",
                 ctx->service.service_id,
                 pkt->session_id, ctx->peer_session_id);
    }
    else
    {
        /* ここに到達するのは tv_sec == tv_sec かつ tv_nsec == tv_nsec かつ
           session_id <= peer_session_id の場合のみ。
           新セッション分岐はこの else には入らない。 */
        if (pkt->session_id == ctx->peer_session_id)
        {
            return 1; /* 同一セッション: 採用済みのため再初期化不要 */
        }
        else
        {
            return 0; /* 旧セッション (タイムスタンプ完全一致・session_id が小): 破棄 */
        }
    }

    /* 新セッション採用: コンテキストを更新しウィンドウ・リオーダー状態をリセットする。
       最初に受信したパケットの seq_num で初期化することで、送信者が先行して
       送信済みの seq に直接同期し、不要な NACK/REJECT サイクルを発生させない。
       送信者を先に起動して受信者が後から参加した場合も同様に機能する。 */
    ctx->peer_session_id      = pkt->session_id;
    ctx->peer_session_tv_sec  = pkt->session_tv_sec;
    ctx->peer_session_tv_nsec = pkt->session_tv_nsec;
    ctx->reorder_pending      = 0;
    clear_pending_fin_ctx(ctx);
    window_init(&ctx->recv_window, pkt->seq_num,
                ctx->global.window_size, ctx->global.max_payload);
    return 1;
}

/** NACK 重複抑制の時間窓 (ミリ秒)。この時間内の同一 ack_num の NACK は破棄する。 */
#define POTR_NACK_DEDUP_MS 200U

static int set_path_ping_state(volatile uint8_t *state, uint8_t next_state)
{
    if (*state == next_state)
    {
        return 0;
    }

    *state = next_state;
    return 1;
}

static int set_all_path_ping_states(volatile uint8_t *states,
                                    size_t            count,
                                    uint8_t           next_state)
{
    size_t i;
    int    changed = 0;

    for (i = 0; i < count; i++)
    {
        if (states[i] != next_state)
        {
            states[i] = next_state;
            changed   = 1;
        }
    }

    return changed;
}

static void wake_udp_interrupt_ping_if_needed(struct PotrContext_ *ctx, int state_changed)
{
    if (state_changed && ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
    {
        potr_health_thread_wake(ctx);
    }
}

static void wake_tcp_interrupt_ping_if_needed(struct PotrContext_ *ctx,
                                              int                  path_idx,
                                              int                  state_changed)
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
            potr_tcp_health_thread_wake_all(ctx);
        }
        else
        {
            (void)potr_tcp_send_ping_now(ctx, path_idx);
        }
    }
}

/* パスごとの peer_port と送信元アドレスを更新する */
static void update_path_recv(struct PotrContext_      *ctx,
                              int                       path_idx,
                              const struct sockaddr_in *sender)
{
    ctx->peer_port[path_idx] = sender->sin_port; /* NBO のまま格納 */

    /* unicast_bidir で送信先アドレスが未確定の場合は受信パケットの送信元から動的学習する。
       - src_addr 省略 (動的 1:1 RECEIVER): IP アドレスが 0 → 送信元 IP で更新
       - src_port=0 (エフェメラルポート動的学習): ポートが 0 → 送信元ポートで更新 */
    if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
    {
        if (ctx->service.src_addr[0][0] == '\0'
            && ctx->dest_addr[path_idx].sin_addr.s_addr == 0)
        {
            ctx->dest_addr[path_idx].sin_addr = sender->sin_addr; /* NBO */
        }
        if (ctx->dest_addr[path_idx].sin_port == 0)
        {
            ctx->dest_addr[path_idx].sin_port = sender->sin_port; /* NBO */
        }
    }
}

/* パスごとのヘルスチェック受信時刻と受信状態を更新する。
   片方向 type 1-6 では PING / 有効 DATA、双方向 type 7 では PING 受信時のみ呼ぶこと。 */
static int update_path_health(struct PotrContext_ *ctx, int path_idx)
{
    com_util_get_monotonic(&ctx->last_recv_tv_sec, &ctx->last_recv_tv_nsec);
    com_util_get_monotonic(&ctx->path_last_recv_sec[path_idx],
                  &ctx->path_last_recv_nsec[path_idx]);
    return set_path_ping_state(&ctx->path_ping_state[path_idx],
                               POTR_PING_STATE_NORMAL);
}

/* タイムアウト時に経過時間を確認し、必要なら peer_port クリアと DISCONNECTED イベントを発火する */
static void check_health_timeout(struct PotrContext_ *ctx)
{
    int64_t now_sec;
    int32_t now_nsec;
    int     i;
    int     should_wake_health = 0;
    int     path_state_changed = 0;

    if (ctx->health_timeout_ms == 0) return;

    com_util_get_monotonic(&now_sec, &now_nsec);

    /* パスごとのタイムアウト: peer_port をクリア */
    for (i = 0; i < ctx->n_path; i++)
    {
        int64_t elapsed_ms;

        if (ctx->path_last_recv_sec[i] == 0) continue;

        elapsed_ms = (now_sec  - ctx->path_last_recv_sec[i])  * 1000LL
                   + (now_nsec - ctx->path_last_recv_nsec[i]) / 1000000L;

        if (elapsed_ms >= (int64_t)ctx->health_timeout_ms)
        {
            int state_changed = set_path_ping_state(&ctx->path_ping_state[i],
                                                    POTR_PING_STATE_ABNORMAL);
            should_wake_health |= state_changed;
            path_state_changed |= state_changed;
            ctx->peer_port[i]          = 0;
            ctx->path_last_recv_sec[i] = 0;
            /* unicast_bidir で動的学習したアドレス・ポートをリセットする (再接続を許可) */
            if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
            {
                if (ctx->service.src_addr[0][0] == '\0')
                {
                    /* 動的 1:1 RECEIVER: 学習した送信元 IP をリセット */
                    ctx->dest_addr[i].sin_addr.s_addr = 0;
                }
                if (ctx->service.src_port == 0)
                {
                    /* エフェメラルポート動的学習: ポートをリセット */
                    ctx->dest_addr[i].sin_port = 0;
                }
            }
        }
    }

    if (path_state_changed)
    {
        sync_service_path_state(ctx);
    }

    /* 全体の health_alive 判定 */
    if (!ctx->health_alive || ctx->last_recv_tv_sec == 0) return;

    {
        int64_t elapsed_ms;
        elapsed_ms = (now_sec  - ctx->last_recv_tv_sec)  * 1000LL
                   + (now_nsec - ctx->last_recv_tv_nsec) / 1000000L;

        if (elapsed_ms >= (int64_t)ctx->health_timeout_ms)
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_WARNING,
                     "recv[service_id=%" PRId64 "]: DISCONNECTED (timeout %lldms >= %ums)",
                     ctx->service.service_id,
                     (long long)elapsed_ms,
                     (unsigned)ctx->health_timeout_ms);
            /* FIN と同様にセッション状態をリセットして次の接続を受け入れ可能にする。
               peer_session_known をクリアすることで、送信者が同一セッションのまま
               復帰した場合でも window_init を経由して受信ウィンドウを初期化し、
               前セッションの next_seq が gap スキャンに影響しないようにする。 */
            clear_pending_fin_ctx(ctx);
            ctx->peer_session_known = 0;
            ctx->reorder_pending    = 0;
            ctx->last_recv_tv_sec   = 0;
            /* 次の接続到来まで受信状態を不定に戻す */
            should_wake_health |= set_all_path_ping_states(ctx->path_ping_state,
                                                           POTR_MAX_PATH,
                                                           POTR_PING_STATE_UNDEFINED);
            memset((void *)ctx->remote_path_ping_state, 0, sizeof(ctx->remote_path_ping_state));
            disconnect_service_all_paths(ctx);
            window_init(&ctx->recv_window, 0,
                        ctx->global.window_size, ctx->global.max_payload);
        }
    }

    wake_udp_interrupt_ping_if_needed(ctx, should_wake_health);
}

/* 欠番 nack_num に対してリオーダー待機が完了しているか確認する。
   返値: 1 = 処理進行 (NACK/DISCONNECT を発行すべき)、0 = まだ待機中。
   reorder_timeout_ms == 0 の場合は常に 1 を返す (即時)。
   新しいギャップまたは欠番通番が変わった場合はタイマーをリセットして 0 を返す。
   同一欠番でタイムアウト経過後は reorder_pending を 0 にクリアして 1 を返す。 */
static int reorder_gap_ready(struct PotrContext_ *ctx, uint32_t nack_num)
{
    int64_t  now_sec;
    int32_t  now_nsec;
    uint32_t ms;

    if (ctx->global.reorder_timeout_ms == 0U) return 1;

    ms = ctx->global.reorder_timeout_ms;

    /* 新しいギャップ、または欠番通番が変わった: タイマーをリセットして待機開始 */
    if (!ctx->reorder_pending || ctx->reorder_nack_num != nack_num)
    {
        uint32_t effective_ms;
        com_util_get_monotonic(&now_sec, &now_nsec);

        /* マルチキャスト/ブロードキャスト通常モードでは NACK 送出タイミングを分散させる。
           複数受信者が同一欠番を同時に NACK すると送信者側で輻輳が発生するため、
           タイマー値を reorder_timeout_ms の 100%〜200% の範囲でジッタを付加する。
           ジッタ源: now_nsec の下位ビット (外部 RNG 不要・移植性高)。
           RAW 系は POTR_TYPE_MULTICAST_RAW / BROADCAST_RAW であり条件に該当しないため対象外。 */
        effective_ms = ms;
        if (ctx->service.type == POTR_TYPE_MULTICAST
            || ctx->service.type == POTR_TYPE_BROADCAST)
        {
            effective_ms = ms + (uint32_t)((uint32_t)now_nsec % ms);
        }

        ctx->reorder_pending       = 1;
        ctx->reorder_nack_num      = nack_num;
        ctx->reorder_deadline_sec  = now_sec  + (int64_t)(effective_ms / 1000U);
        ctx->reorder_deadline_nsec = now_nsec + (int32_t)((effective_ms % 1000U) * 1000000U);
        if (ctx->reorder_deadline_nsec >= 1000000000L)
        {
            ctx->reorder_deadline_sec++;
            ctx->reorder_deadline_nsec -= 1000000000L;
        }
        return 0; /* 待機開始 */
    }

    /* 同一欠番: タイムアウト確認 */
    com_util_get_monotonic(&now_sec, &now_nsec);
    if (now_sec > ctx->reorder_deadline_sec
        || (now_sec == ctx->reorder_deadline_sec
            && now_nsec >= ctx->reorder_deadline_nsec))
    {
        ctx->reorder_pending = 0;
        return 1; /* タイムアウト: 処理進行 */
    }

    return 0; /* まだ待機中 */
}

/* select() タイムアウト時に、リオーダー待機中の欠番がタイムアウトしていれば処理する。
   通常モード: NACK を送出する。
   RAW モード: DISCONNECTED を発行してセッションをリセットし、次のパケットで再同期する。 */
static void check_reorder_timeout(struct PotrContext_ *ctx)
{
    if (!ctx->reorder_pending) return;
    if (!reorder_gap_ready(ctx, ctx->reorder_nack_num)) return;

    /* reorder_gap_ready が 1 を返した時点で reorder_pending はすでにクリア済み */
    if (potr_is_raw_type(ctx->service.type))
    {
        /* RAW モード: DISCONNECTED を発行してセッション状態をリセットする。
           次のパケット受信時に check_and_update_session で window_init が呼ばれ
           自然に再同期する。 */
        raw_session_disconnect(ctx);
        ctx->peer_session_known = 0;
        ctx->last_recv_tv_sec   = 0;
        window_init(&ctx->recv_window, 0,
                    ctx->global.window_size, ctx->global.max_payload);
    }
    else
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                 "recv[service_id=%" PRId64 "]: NACK seq=%u (reorder timeout)",
                 ctx->service.service_id, (unsigned)ctx->reorder_nack_num);
        send_nack(ctx, ctx->reorder_nack_num);
    }
}

/* NACK パケットを全パスへ送信する */
static void send_nack(struct PotrContext_ *ctx, uint32_t nack_seq)
{
    PotrPacket           nack_pkt;
    PotrPacketSessionHdr shdr;
    size_t               wire_len;
    int                  i;

    shdr.service_id      = ctx->service.service_id;
    shdr.session_id      = ctx->session_id;
    shdr.session_tv_sec  = ctx->session_tv_sec;
    shdr.session_tv_nsec = ctx->session_tv_nsec;

    if (packet_build_nack(&nack_pkt, &shdr, nack_seq) != POTR_SUCCESS) return;

    if (ctx->service.encrypt_enabled)
    {
        uint8_t  wire_buf[PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
        uint8_t  nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t   enc_out = POTR_CRYPTO_TAG_SIZE;

        nack_pkt.flags      |= htons(POTR_FLAG_ENCRYPTED);
        nack_pkt.payload_len = htons((uint16_t)POTR_CRYPTO_TAG_SIZE);

        /* ノンス: session_id(4B) + flags(2B, NACK|ENCRYPTED NBO) + ack_num(4B NBO) + padding(2B) */
        memcpy(nonce,      &nack_pkt.session_id, 4);
        memcpy(nonce + 4,  &nack_pkt.flags,      2);
        memcpy(nonce + 6,  &nack_pkt.ack_num,    4);
        memset(nonce + 10, 0,                    2);

        memcpy(wire_buf, &nack_pkt, PACKET_HEADER_SIZE);
        if (com_util_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out,
                         NULL, 0,
                         ctx->service.encrypt_key,
                         nonce,
                         wire_buf, PACKET_HEADER_SIZE) != 0)
        {
            return;
        }
        wire_len = PACKET_HEADER_SIZE + enc_out;

        for (i = 0; i < ctx->n_path; i++)
        {
            if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
            {
                potr_sendto(ctx->sock[i], wire_buf, wire_len, 0,
                            (const struct sockaddr *)&ctx->dest_addr[i],
                            (int)sizeof(ctx->dest_addr[i]));
            }
            else
            {
                struct sockaddr_in dest;
                uint16_t           port;

                if (ctx->service.src_port != 0)
                    port = htons(ctx->service.src_port);
                else
                    port = ctx->peer_port[i];

                if (port == 0) continue;

                memset(&dest, 0, sizeof(dest));
                dest.sin_family = AF_INET;
                dest.sin_addr   = ctx->src_addr_resolved[i];
                dest.sin_port   = port;
                potr_sendto(ctx->sock[i], wire_buf, wire_len, 0,
                            (const struct sockaddr *)&dest, (int)sizeof(dest));
            }
        }
    }
    else
    {
        wire_len = packet_wire_size(&nack_pkt);

        for (i = 0; i < ctx->n_path; i++)
        {
            /* UNICAST_BIDIR: dest_addr[i] (dst_addr:dst_port) へ直接送信する。
               通常 unicast: src_addr_resolved[i]:src_port または peer_port へ送信する。 */
            if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
            {
                potr_sendto(ctx->sock[i], (const uint8_t *)&nack_pkt, wire_len, 0,
                            (const struct sockaddr *)&ctx->dest_addr[i],
                            (int)sizeof(ctx->dest_addr[i]));
            }
            else
            {
                struct sockaddr_in dest;
                uint16_t           port;

                if (ctx->service.src_port != 0)
                {
                    port = htons(ctx->service.src_port);
                }
                else
                {
                    port = ctx->peer_port[i]; /* NBO */
                }

                if (port == 0) continue; /* ポート未観測のパスは送れない */

                memset(&dest, 0, sizeof(dest));
                dest.sin_family = AF_INET;
                dest.sin_addr   = ctx->src_addr_resolved[i];
                dest.sin_port   = port;

                potr_sendto(ctx->sock[i], (const uint8_t *)&nack_pkt, wire_len, 0,
                            (const struct sockaddr *)&dest, (int)sizeof(dest));
            }
        }
    }
}

/* REJECT パケットを全パスへ送信する */
static void send_reject(struct PotrContext_ *ctx, uint32_t seq_num)
{
    PotrPacket           reject_pkt;
    PotrPacketSessionHdr shdr;
    size_t               wire_len;
    int                  i;

    shdr.service_id      = ctx->service.service_id;
    shdr.session_id      = ctx->session_id;
    shdr.session_tv_sec  = ctx->session_tv_sec;
    shdr.session_tv_nsec = ctx->session_tv_nsec;

    if (packet_build_reject(&reject_pkt, &shdr, seq_num) != POTR_SUCCESS) return;

    if (ctx->service.encrypt_enabled)
    {
        uint8_t  wire_buf[PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
        uint8_t  nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t   enc_out = POTR_CRYPTO_TAG_SIZE;

        reject_pkt.flags      |= htons(POTR_FLAG_ENCRYPTED);
        reject_pkt.payload_len = htons((uint16_t)POTR_CRYPTO_TAG_SIZE);

        /* ノンス: session_id(4B) + flags(2B, REJECT|ENCRYPTED NBO) + ack_num(4B NBO) + padding(2B) */
        memcpy(nonce,      &reject_pkt.session_id, 4);
        memcpy(nonce + 4,  &reject_pkt.flags,      2);
        memcpy(nonce + 6,  &reject_pkt.ack_num,    4);
        memset(nonce + 10, 0,                      2);

        memcpy(wire_buf, &reject_pkt, PACKET_HEADER_SIZE);
        if (com_util_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out,
                         NULL, 0,
                         ctx->service.encrypt_key,
                         nonce,
                         wire_buf, PACKET_HEADER_SIZE) != 0)
        {
            return;
        }
        wire_len = PACKET_HEADER_SIZE + enc_out;

        for (i = 0; i < ctx->n_path; i++)
        {
            potr_sendto(ctx->sock[i], wire_buf, wire_len, 0,
                        (const struct sockaddr *)&ctx->dest_addr[i],
                        (int)sizeof(ctx->dest_addr[i]));
        }
    }
    else
    {
        wire_len = packet_wire_size(&reject_pkt);

        for (i = 0; i < ctx->n_path; i++)
        {
            potr_sendto(ctx->sock[i], (const uint8_t *)&reject_pkt, wire_len, 0,
                        (const struct sockaddr *)&ctx->dest_addr[i],
                        (int)sizeof(ctx->dest_addr[i]));
        }
    }
}

/* 受信データを解凍してコールバックに渡す */
static void recv_deliver(struct PotrContext_ *ctx,
                         const uint8_t      *payload,
                         size_t              payload_len,
                         int                 compressed)
{
    if (compressed)
    {
        size_t dec_len = ctx->compress_buf_size;

        if (com_util_decompress(ctx->compress_buf, &dec_len,
                            payload, payload_len) == 0)
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                     "recv[service_id=%" PRId64 "]: decompress %zu -> %zu bytes",
                     ctx->service.service_id, payload_len, dec_len);
            potr_callback_emit(ctx, POTR_PEER_NA,
                               POTR_EVENT_DATA,
                               ctx->compress_buf,
                               dec_len);
        }
        else
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                     "recv[service_id=%" PRId64 "]: decompress failed (src_len=%zu)",
                     ctx->service.service_id, payload_len);
        }
    }
    else
    {
        potr_callback_emit(ctx, POTR_PEER_NA, POTR_EVENT_DATA,
                           payload, payload_len);
    }
}

/* ペイロードエレメント 1 件のフラグメント結合・解凍・コールバック処理。
   window_recv_pop で取り出した外側パケットを packet_unpack_next で展開した
   各ペイロードエレメントに対して呼び出す。 */
static void deliver_payload_elem(struct PotrContext_ *ctx, const PotrPacket *elem)
{
    /* 未接続状態では DATA を破棄する。接続確立前/DISCONNECTED 後の DATA が
       アプリに届かないようにする。CONNECTED 発火は health_alive=1 への遷移で行う。 */
    if (!ctx->health_alive)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                 "recv[service_id=%" PRId64 "]: drop DATA elem while health_alive=0",
                 ctx->service.service_id);
        return;
    }

    if (elem->flags & POTR_FLAG_MORE_FRAG)
    {
        /* 中間フラグメント: バッファに追記 */
        if (ctx->frag_buf_len + elem->payload_len <= ctx->global.max_message_size)
        {
            if (ctx->frag_buf_len == 0)
            {
                if (elem->flags & POTR_FLAG_COMPRESSED)
                {
                    ctx->frag_compressed = 1;
                }
                else
                {
                    ctx->frag_compressed = 0;
                }
            }
            memcpy(ctx->frag_buf + ctx->frag_buf_len,
                   elem->payload, elem->payload_len);
            ctx->frag_buf_len += elem->payload_len;
        }
        else
        {
            ctx->frag_buf_len    = 0;
            ctx->frag_compressed = 0;
        }
    }
    else if (ctx->frag_buf_len > 0)
    {
        /* 最終フラグメント: バッファに追記してコールバック */
        if (ctx->frag_buf_len + elem->payload_len <= ctx->global.max_message_size)
        {
            memcpy(ctx->frag_buf + ctx->frag_buf_len,
                   elem->payload, elem->payload_len);
            ctx->frag_buf_len += elem->payload_len;

            if (ctx->callback != NULL)
            {
                recv_deliver(ctx, ctx->frag_buf,
                             ctx->frag_buf_len, ctx->frag_compressed);
            }
        }
        ctx->frag_buf_len    = 0;
        ctx->frag_compressed = 0;
    }
    else
    {
        /* フラグメントなし: 直接コールバック */
        if (ctx->callback != NULL)
        {
            int is_compressed;
            if (elem->flags & POTR_FLAG_COMPRESSED)
            {
                is_compressed = 1;
            }
            else
            {
                is_compressed = 0;
            }
            recv_deliver(ctx,
                         elem->payload,
                         (size_t)elem->payload_len,
                         is_compressed);
        }
    }
}

/* FIN 受信時の DISCONNECTED 発火とセッションリセットを行う。
   pending_fin の即時解消パスと drain_recv_window() 経由の遅延解消パスの両方から呼ぶ。 */
static void fire_disconnected_by_fin(struct PotrContext_ *ctx, uint32_t fin_target_seq)
{
    if (ctx->service.type == POTR_TYPE_TCP
        || ctx->service.type == POTR_TYPE_TCP_BIDIR)
    {
        if (send_tcp_fin_ack(ctx, fin_target_seq) == POTR_SUCCESS)
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,
                     "tcp_recv[service_id=%" PRId64 "]: FIN_ACK sent ack=%u",
                     ctx->service.service_id, (unsigned)fin_target_seq);
        }
        else
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_WARNING,
                     "tcp_recv[service_id=%" PRId64 "]: FIN_ACK send failed ack=%u",
                     ctx->service.service_id, (unsigned)fin_target_seq);
        }
    }

    if (potr_is_tcp_type(ctx->service.type))
    {
        COM_UTIL_MUTEX_LOCK(&ctx->tcp_state_mutex);
        set_all_path_ping_states(ctx->path_ping_state, POTR_MAX_PATH, POTR_PING_STATE_UNDEFINED);
        memset((void *)ctx->remote_path_ping_state, 0, sizeof(ctx->remote_path_ping_state));
        disconnect_service_all_paths(ctx);
        COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_state_mutex);
    }
    else
    {
        set_all_path_ping_states(ctx->path_ping_state, POTR_MAX_PATH, POTR_PING_STATE_UNDEFINED);
        memset((void *)ctx->remote_path_ping_state, 0, sizeof(ctx->remote_path_ping_state));
        disconnect_service_all_paths(ctx);
    }
    clear_pending_fin_ctx(ctx);
    ctx->peer_session_known = 0;
    ctx->reorder_pending    = 0;
    ctx->last_recv_tv_sec   = 0;
    COM_UTIL_MUTEX_LOCK(&ctx->recv_window_mutex);
    window_init(&ctx->recv_window, 0,
                ctx->global.window_size, ctx->global.max_payload);
    COM_UTIL_MUTEX_UNLOCK(&ctx->recv_window_mutex);
}

/* recv_window から順序整列済みの外側パケットを取り出してペイロードエレメントを配信する。
   REJECT 処理後と通常受信処理の両方から呼び出す。 */
static void drain_recv_window(struct PotrContext_ *ctx)
{
    PotrPacket pop_pkt;

    while (window_recv_pop(&ctx->recv_window, &pop_pkt) == POTR_SUCCESS)
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
        POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                 "recv[service_id=%" PRId64 "]: pop seq=%u %s",
                 ctx->service.service_id,
                 (unsigned)pop_pkt.seq_num,
                 pkt_type_str);

        if (pop_pkt.flags & POTR_FLAG_PING)
        {
            continue; /* PING: 生存確認のみ、ペイロードエレメント展開不要 */
        }

        /* DATA: ペイロードエレメントを順に展開して配信 */
        {
            size_t     offset = 0;
            PotrPacket elem;

            while (packet_unpack_next(&pop_pkt, &offset, &elem) == POTR_SUCCESS)
            {
                deliver_payload_elem(ctx, &elem);
            }
        }
    }

    /* pending FIN: recv_window.next_seq が FIN の目標値に到達したら DISCONNECTED を発火する。 */
    if (ctx->pending_fin
        && recv_window_reached_fin_target(ctx->recv_window.next_seq, ctx->fin_target_seq))
    {
        fire_disconnected_by_fin(ctx, ctx->fin_target_seq);
    }
}

/* RAW モード用: DISCONNECTED イベントを発行してセッション状態を部分的にリセットする。
   ウィンドウリセットは呼び出し元が行う (新しい基点通番が確定してから呼ぶため)。
   フラグメント組み立てバッファも破棄する。 */
static void raw_session_disconnect(struct PotrContext_ *ctx)
{
    set_all_path_ping_states(ctx->path_ping_state, POTR_MAX_PATH, POTR_PING_STATE_UNDEFINED);
    memset((void *)ctx->remote_path_ping_state, 0, sizeof(ctx->remote_path_ping_state));
    disconnect_service_all_paths(ctx);

    /* フラグメント組み立て途中状態と pending FIN を破棄 */
    clear_pending_fin_ctx(ctx);
    ctx->frag_buf_len    = 0;
    ctx->frag_compressed = 0;
}

/* 外側パケット (DATA / PING) を受信ウィンドウに投入し、順序整列済みパケットを配信する。
   再送・順序整列の単位は外側パケットであり、NACK も外側パケットの seq_num を指定する。
   RAW モードでは NACK を送信せず、ギャップ検出時は DISCONNECTED を発行してウィンドウを
   新しい基点通番でリセットする。 */
static void process_outer_pkt(struct PotrContext_ *ctx,
                              const PotrPacket    *pkt,
                              int                  path_idx)
{
    uint32_t nack_num;
    uint32_t stretch;
    int      is_raw = potr_is_raw_type(ctx->service.type);

    if (window_recv_push(&ctx->recv_window, pkt) != POTR_SUCCESS)
    {
        if (is_raw)
        {
            /* ウィンドウ満杯: DISCONNECTED を発行し、受信したパケットの通番でリセットしてから
               再投入する。再投入は必ず成功する (空ウィンドウの先頭スロット)。 */
            POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                     "recv[service_id=%" PRId64 "]: RAW recv_window full, resetting to seq=%u",
                     ctx->service.service_id, (unsigned)pkt->seq_num);
            raw_session_disconnect(ctx);
            window_recv_reset(&ctx->recv_window, pkt->seq_num);
            if (window_recv_push(&ctx->recv_window, pkt) != POTR_SUCCESS)
            {
                /* リセット直後の再投入失敗は想定外 */
                POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                         "recv[service_id=%" PRId64 "]: RAW window re-push failed seq=%u (bug)",
                         ctx->service.service_id, (unsigned)pkt->seq_num);
                return;
            }
        }
        else
        {
            /* 通番がウィンドウ範囲外のためドロップ (受信ウィンドウ満杯、または古い重複パケット)。
               受信者は next_seq を待ち続けるが、ヘルスチェックや後続パケット到着時に NACK が送られる。 */
            POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                     "recv[service_id=%" PRId64 "]: recv_window full (100%%), dropping seq=%u"
                     " (base_seq=%u window_size=%u)",
                     ctx->service.service_id, (unsigned)pkt->seq_num,
                     (unsigned)ctx->recv_window.base_seq,
                     (unsigned)ctx->recv_window.window_size);
            return;
        }
    }

    /* ウィンドウ利用率チェック: 今回のパケットが占める先頭からの距離で推定する。
       stretch = pkt->seq_num - base_seq + 1 。push 成功後は [1, window_size] の範囲。 */
    stretch = (uint32_t)(pkt->seq_num - ctx->recv_window.base_seq) + 1U;
    if (stretch * 10U >= (uint32_t)ctx->recv_window.window_size * 8U)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_WARNING,
                 "recv[service_id=%" PRId64 "]: recv_window utilization high (%u/%u >= 80%%)"
                 " seq=%u base_seq=%u",
                 ctx->service.service_id,
                 (unsigned)stretch, (unsigned)ctx->recv_window.window_size,
                 (unsigned)pkt->seq_num, (unsigned)ctx->recv_window.base_seq);
    }

    if (window_recv_needs_nack(&ctx->recv_window, &nack_num))
    {
        if (is_raw)
        {
            /* ギャップ検出: リオーダー待機を確認してから DISCONNECTED を発行する。
               reorder_timeout_ms > 0 のとき、タイムアウト前はウィンドウに今のパケットを
               残したまま待機する。タイムアウト後または即時モードでは reset + 再 push。
               skip ループは push 済みパケットのスロットマッピングを壊すため使用しない。 */
            if (reorder_gap_ready(ctx, nack_num))
            {
                raw_session_disconnect(ctx);
                window_recv_reset(&ctx->recv_window, pkt->seq_num);
                if (window_recv_push(&ctx->recv_window, pkt) != POTR_SUCCESS)
                {
                    /* リセット直後の再投入失敗は想定外 */
                    POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                             "recv[service_id=%" PRId64 "]: RAW gap re-push failed seq=%u (bug)",
                             ctx->service.service_id, (unsigned)pkt->seq_num);
                    return;
                }
            }
            /* else: リオーダー待機中。パケットはウィンドウに push 済み。 */
        }
        else
        {
            if (reorder_gap_ready(ctx, nack_num))
            {
                POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                         "recv[service_id=%" PRId64 "]: NACK seq=%u",
                         ctx->service.service_id, (unsigned)nack_num);
                send_nack(ctx, nack_num);
            }
            /* else: リオーダー待機中。NACK 送出を保留。 */
        }
    }
    else
    {
        /* 欠番なし: 待機中の欠番が埋まった (または元から欠番なし) */
        ctx->reorder_pending = 0;
    }

    /* 片方向 type 1-6 では、有効な DATA を PING と同等のヘルス信号として扱う。
       push 成功後に更新することで、重複・範囲外 DATA は health に反映しない。 */
    if ((pkt->flags & POTR_FLAG_DATA)
        && ctx->service.type != POTR_TYPE_UNICAST_BIDIR)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                 "recv[service_id=%" PRId64 "]: DATA seq=%u updates health on path=%d",
                 ctx->service.service_id, (unsigned)pkt->seq_num, path_idx);
        update_path_health(ctx, path_idx);
        sync_service_path_state(ctx);
    }

    drain_recv_window(ctx);

    /* drain 後に next_seq が前進した結果、新たな欠番が先頭に現れる場合は NACK を送る。
       例: seq=3,4 欠落・seq=5 着時、drain 前は NACK(3)、seq=3 再送着→ drain で pop 後
       next_seq=4 が欠番になるが次のパケット到着まで NACK(4) が遅延するのを防ぐ。
       RAW モードで reorder_gap_ready が 1 を返した場合は reset + 再 push によりウィンドウに
       パケット 1 つのみ残るため drain 後は空となりここに到達しない。
       リオーダー待機中 (RAW/通常) は drain で前進しないため post-drain 欠番も保留のまま。 */
    if (window_recv_needs_nack(&ctx->recv_window, &nack_num))
    {
        if (!is_raw && reorder_gap_ready(ctx, nack_num))
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                     "recv[service_id=%" PRId64 "]: NACK seq=%u (post-drain)",
                     ctx->service.service_id, (unsigned)nack_num);
            send_nack(ctx, nack_num);
        }
        /* RAW モードでは reset + 再 push 後は到達しない。リオーダー待機中は
           check_reorder_timeout がタイムアウト処理を担う。 */
    }
    else
    {
        ctx->reorder_pending = 0;
    }
}

/* 受信スレッド本体 */
COM_UTIL_THREAD_FUNC(recv_thread_func)
{
    struct PotrContext_ *ctx = (struct PotrContext_ *)arg;
    uint8_t            *buf = ctx->recv_buf; /* PACKET_HEADER_SIZE + max_payload バイト */
    PotrPacket          pkt;
    struct sockaddr_in  sender_addr;
    uint32_t            poll_ms;

    if ((ctx->role == POTR_ROLE_RECEIVER || ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
        && ctx->health_timeout_ms > 0U)
    {
        poll_ms = ctx->health_timeout_ms / 3U;
        if (poll_ms < 100U) poll_ms = 100U;
    }
    else
    {
        poll_ms = 500U;
    }
    /* reorder_timeout_ms が有効な場合は poll 間隔を短縮してタイムアウト精度を確保する */
    if ((ctx->role == POTR_ROLE_RECEIVER || ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
        && ctx->global.reorder_timeout_ms > 0U)
    {
        if (ctx->global.reorder_timeout_ms < poll_ms)
        {
            poll_ms = ctx->global.reorder_timeout_ms;
        }
    }

    while (ctx->running[0])
    {
        fd_set         readfds;
        struct timeval tv;
        int            ret;
        int            maxfd = -1;
        int            i;

        FD_ZERO(&readfds);
        for (i = 0; i < ctx->n_path; i++)
        {
            if (ctx->sock[i] == POTR_INVALID_SOCKET) continue;
#if defined(PLATFORM_LINUX)
            FD_SET(ctx->sock[i], &readfds);
            if (ctx->sock[i] > maxfd) maxfd = ctx->sock[i];
#elif defined(PLATFORM_WINDOWS)
            FD_SET(ctx->sock[i], &readfds);
            /* Windows では SOCKET は UINT_PTR。maxfd の代わりに n_path を使う */
#endif /* PLATFORM_ */
        }

#if defined(PLATFORM_WINDOWS)
        /* Windows: select の第1引数は無視されるが 0 でなく n_path を渡す */
        maxfd = ctx->n_path;
#endif /* PLATFORM_WINDOWS */

        if (maxfd < 0) break;

        tv.tv_sec  = (long)(poll_ms / 1000U);
        tv.tv_usec = (long)((poll_ms % 1000U) * 1000U);

        ret = select(maxfd + 1, &readfds, NULL, NULL, &tv);

        if (ret < 0)
        {
            if (!ctx->running[0]) break;
            continue;
        }

        if (ret == 0)
        {
            if (ctx->is_multi_peer)
            {
                n1_check_health_timeout(ctx);
            }
            else if (ctx->role == POTR_ROLE_RECEIVER
                     || ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
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
            int recv_len;
            int sender_len;

            if (ctx->sock[i] == POTR_INVALID_SOCKET) continue;
            if (!FD_ISSET(ctx->sock[i], &readfds)) continue;

            memset(&sender_addr, 0, sizeof(sender_addr));
            sender_len = (int)sizeof(sender_addr);

            recv_len = potr_recvfrom(ctx->sock[i], buf,
                                     PACKET_HEADER_SIZE + ctx->global.max_payload,
                                     0, (struct sockaddr *)&sender_addr,
                                     &sender_len);
            if (recv_len <= 0)
            {
                if (!ctx->running[0]) break; /* 正常終了: ソケットクローズによる割り込み */
                POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                         "recv[service_id=%" PRId64 "]: recvfrom returned %d",
                         ctx->service.service_id, recv_len);
                continue;
            }

            if (packet_parse(&pkt, buf, (size_t)recv_len) != POTR_SUCCESS)
            {
                POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                         "recv[service_id=%" PRId64 "]: packet parse failed (len=%d)",
                         ctx->service.service_id, recv_len);
                continue;
            }
            if (pkt.service_id != ctx->service.service_id)
            {
                POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                         "recv[service_id=%" PRId64 "]: ignored packet for service_id=%" PRId64 "",
                         ctx->service.service_id, pkt.service_id);
                continue;
            }
            if (!check_src_addr(ctx, &sender_addr)) continue;

            /* ── N:1 モード: ピアごとにディスパッチ ── */
            if (ctx->is_multi_peer)
            {
                PotrPeerContext *peer = NULL;
                int              is_new_peer = 0;

                if (!recv_authenticate_packet(ctx, &pkt, buf, "recv", -1))
                {
                    continue;
                }

                COM_UTIL_MUTEX_LOCK(&ctx->peers_mutex);

                /* session_triplet でピアを検索 */
                peer = peer_find_by_session(ctx,
                                            pkt.session_id,
                                            pkt.session_tv_sec,
                                            pkt.session_tv_nsec);

                if (peer == NULL)
                {
                    if (pkt.flags & POTR_FLAG_PING)
                    {
                        peer = peer_create(ctx, &sender_addr, i);
                        if (peer != NULL)
                        {
                            peer->peer_session_id      = pkt.session_id;
                            peer->peer_session_tv_sec  = pkt.session_tv_sec;
                            peer->peer_session_tv_nsec = pkt.session_tv_nsec;
                            peer->peer_session_known   = 1;
                            window_init(&peer->recv_window, pkt.seq_num,
                                        ctx->global.window_size,
                                        ctx->global.max_payload);
                            is_new_peer = 1;
                        }
                    }
                    else if (pkt.flags & POTR_FLAG_DATA)
                    {
                        POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                                 "recv[service_id=%" PRId64 "]: drop initial DATA"
                                 " from unknown peer session=%u",
                                 ctx->service.service_id, pkt.session_id);
                    }
                }

                if (peer == NULL)
                {
                    COM_UTIL_MUTEX_UNLOCK(&ctx->peers_mutex);
                    continue; /* max_peers 超過または初回受理対象外 */
                }

                if (is_new_peer)
                {
                    POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,
                             "recv[service_id=%" PRId64 "]: new peer=%u from %u.%u.%u.%u:%u (CONNECTED pending PING+NORMAL)",
                             ctx->service.service_id, (unsigned)peer->peer_id,
                             (unsigned)((ntohl(sender_addr.sin_addr.s_addr) >> 24) & 0xFF),
                             (unsigned)((ntohl(sender_addr.sin_addr.s_addr) >> 16) & 0xFF),
                             (unsigned)((ntohl(sender_addr.sin_addr.s_addr) >>  8) & 0xFF),
                             (unsigned)( ntohl(sender_addr.sin_addr.s_addr)        & 0xFF),
                             (unsigned)ntohs(sender_addr.sin_port));
                }

                /* FIN: ピアの正常終了通知 */
                if (pkt.flags & POTR_FLAG_FIN)
                {
                    POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,
                             "recv[service_id=%" PRId64 "]: peer=%u FIN received"
                             " (fin_target_seq=%u recv_next=%u)",
                             ctx->service.service_id, (unsigned)peer->peer_id,
                             (unsigned)pkt.ack_num, (unsigned)peer->recv_window.next_seq);

                    /* target 付き FIN かつ recv_window.next_seq が未到達: FIN をペンディング。 */
                    if (fin_packet_has_target(&pkt)
                        && !recv_window_reached_fin_target(peer->recv_window.next_seq, pkt.ack_num))
                    {
                        peer->pending_fin   = 1;
                        peer->fin_target_seq = pkt.ack_num;
                        POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,
                                 "recv[service_id=%" PRId64 "]: peer=%u FIN pending (waiting for seq=%u)",
                                 ctx->service.service_id, (unsigned)peer->peer_id,
                                 (unsigned)pkt.ack_num);
                        COM_UTIL_MUTEX_UNLOCK(&ctx->peers_mutex);
                        continue;
                    }

                    /* 即時: no-data FIN またはウィンドウ追い付き済み。 */
                    n1_fire_disconnected_by_fin(ctx, peer);
                    COM_UTIL_MUTEX_UNLOCK(&ctx->peers_mutex);
                    continue;
                }

                /* NACK: 送信ウィンドウから再送する */
                if (pkt.flags & POTR_FLAG_NACK)
                {
                    PotrPacket resend_pkt;
                    size_t     wire_len = 0;
                    int        get_result;
                    int        j;

                    COM_UTIL_MUTEX_LOCK(&peer->send_window_mutex);
                    get_result = window_send_get(&peer->send_window,
                                                 pkt.ack_num, &resend_pkt);
                    if (get_result == POTR_SUCCESS)
                    {
                        wire_len = packet_wire_size(&resend_pkt);
                        memcpy(ctx->recv_buf, &resend_pkt, PACKET_HEADER_SIZE);
                        memcpy(ctx->recv_buf + PACKET_HEADER_SIZE,
                               resend_pkt.payload,
                               wire_len - PACKET_HEADER_SIZE);
                    }
                    COM_UTIL_MUTEX_UNLOCK(&peer->send_window_mutex);

                    if (get_result == POTR_SUCCESS)
                    {
                        POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                                 "recv[service_id=%" PRId64 "]: peer=%u NACK seq=%u -> retransmit",
                                 ctx->service.service_id, (unsigned)peer->peer_id,
                                 (unsigned)pkt.ack_num);
                        for (j = 0; j < (int)POTR_MAX_PATH; j++)
                        {
                            if (peer->dest_addr[j].sin_family == 0) continue;
                            potr_sendto(ctx->sock[j], ctx->recv_buf, wire_len, 0,
                                        (const struct sockaddr *)&peer->dest_addr[j],
                                        (int)sizeof(peer->dest_addr[j]));
                        }
                    }
                    else
                    {
                        POTR_LOG(COM_UTIL_LOG_LEVEL_WARNING,
                                 "recv[service_id=%" PRId64 "]: peer=%u NACK seq=%u not in window -> REJECT",
                                 ctx->service.service_id, (unsigned)peer->peer_id,
                                 (unsigned)pkt.ack_num);
                        n1_send_reject(ctx, peer, pkt.ack_num);
                    }

                    COM_UTIL_MUTEX_UNLOCK(&ctx->peers_mutex);
                    continue;
                }

                /* REJECT */
                if (pkt.flags & POTR_FLAG_REJECT)
                {
                    if (!n1_check_and_update_session(ctx, peer, &pkt))
                    {
                        COM_UTIL_MUTEX_UNLOCK(&ctx->peers_mutex);
                        continue;
                    }
                    n1_update_path_recv(peer, &sender_addr, i);

                    set_all_path_ping_states(peer->path_ping_state,
                                             POTR_MAX_PATH,
                                             POTR_PING_STATE_UNDEFINED);
                    memset((void *)peer->remote_path_ping_state, 0,
                           sizeof(peer->remote_path_ping_state));
                    disconnect_peer_all_paths(ctx, peer);
                    peer->reorder_pending = 0;
                    window_recv_skip(&peer->recv_window, pkt.ack_num);
                    n1_drain_recv_window(ctx, peer);
                    COM_UTIL_MUTEX_UNLOCK(&ctx->peers_mutex);
                    continue;
                }

                /* DATA / PING */
                if (!(pkt.flags & (POTR_FLAG_DATA | POTR_FLAG_PING)))
                {
                    COM_UTIL_MUTEX_UNLOCK(&ctx->peers_mutex);
                    continue;
                }

                if (!n1_check_and_update_session(ctx, peer, &pkt))
                {
                    COM_UTIL_MUTEX_UNLOCK(&ctx->peers_mutex);
                    continue;
                }

                n1_update_path_recv(peer, &sender_addr, i);

                POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                         "recv[service_id=%" PRId64 "]: peer=%u %s seq=%u",
                         ctx->service.service_id, (unsigned)peer->peer_id,
                         (pkt.flags & POTR_FLAG_PING) ? "PING" : "DATA",
                         (unsigned)pkt.seq_num);

                if (pkt.flags & POTR_FLAG_PING)
                {
                    int ping_state_changed;

                    /* ヘルスチェックタイムアウト用受信時刻と受信状態を更新する (PING 限定) */
                    ping_state_changed = n1_update_path_health(peer, i);

                    /* PING ペイロード (相手端のパス受信状態) を格納する */
                    if (pkt.payload_len >= POTR_MAX_PATH && pkt.payload != NULL)
                    {
                        memcpy(peer->remote_path_ping_state, pkt.payload, POTR_MAX_PATH);
                    }

                    sync_peer_path_state(ctx, peer);

                    if (pkt.seq_num != peer->recv_window.next_seq
                        && seqnum_in_window(pkt.seq_num,
                                            peer->recv_window.next_seq + 1U,
                                            peer->recv_window.window_size))
                    {
                        if (n1_reorder_gap_ready(peer, peer->recv_window.next_seq))
                        {
                            n1_send_nack(ctx, peer, peer->recv_window.next_seq);
                        }
                    }
                    COM_UTIL_MUTEX_UNLOCK(&ctx->peers_mutex);
                    if (ping_state_changed)
                    {
                        potr_health_thread_wake(ctx);
                    }
                    continue;
                }

                n1_process_outer_pkt(ctx, peer, &pkt);
                COM_UTIL_MUTEX_UNLOCK(&ctx->peers_mutex);
                continue;
            }

            if (!recv_authenticate_packet(ctx, &pkt, buf, "recv", -1)) continue;

            /* ── 送信者ロール: NACK のみ処理 ── */
            if (ctx->role == POTR_ROLE_SENDER)
            {
                /* RAW モードは再送バッファを持たないため NACK を無視する */
                if (potr_is_raw_type(ctx->service.type))
                {
                    continue;
                }

                if (pkt.flags & POTR_FLAG_NACK)
                {
                    int j;

                    /* 同一 ack_num の NACK が POTR_NACK_DEDUP_MS 以内に届いた場合は破棄 */
                    {
                        uint64_t now_ms    = com_util_get_monotonic_ms();
                        int      dedup_idx;
                        int      is_dup    = 0;

                        for (dedup_idx = 0;
                             dedup_idx < (int)POTR_NACK_DEDUP_SLOTS;
                             dedup_idx++)
                        {
                            const PotrNackDedupEntry *e =
                                &ctx->nack_dedup_buf[dedup_idx];
                            if (e->time_ms != 0
                                && e->ack_num == pkt.ack_num
                                && (now_ms - e->time_ms)
                                   < (uint64_t)POTR_NACK_DEDUP_MS)
                            {
                                is_dup = 1;
                                break;
                            }
                        }

                        if (is_dup)
                        {
                            continue;
                        }

                        ctx->nack_dedup_buf[ctx->nack_dedup_next].ack_num =
                            pkt.ack_num;
                        ctx->nack_dedup_buf[ctx->nack_dedup_next].time_ms =
                            now_ms;
                        ctx->nack_dedup_next =
                            (uint8_t)((ctx->nack_dedup_next + 1U)
                                      % POTR_NACK_DEDUP_SLOTS);
                    }

                    {
                        PotrPacket resend_pkt;
                        int        get_result;
                        size_t     wire_len = 0;

                        /* send_window へのアクセスを排他制御する (送信スレッド・ヘルスチェックスレッドと競合)。
                           ミューテックス保持中に recv_buf へ wire データを組み立て、
                           プールスロットが上書きされる前にコピーを完了させる。 */
                        COM_UTIL_MUTEX_LOCK(&ctx->send_window_mutex);
                        get_result = window_send_get(&ctx->send_window,
                                                     pkt.ack_num,
                                                     &resend_pkt);

                        if (get_result == POTR_SUCCESS)
                        {
                            /* [NBO ヘッダー 36B][ペイロード] を recv_buf に組み立てる */
                            wire_len = packet_wire_size(&resend_pkt);
                            memcpy(ctx->recv_buf, &resend_pkt, PACKET_HEADER_SIZE);
                            memcpy(ctx->recv_buf + PACKET_HEADER_SIZE,
                                   resend_pkt.payload,
                                   wire_len - PACKET_HEADER_SIZE);
                        }

                        COM_UTIL_MUTEX_UNLOCK(&ctx->send_window_mutex);

                        if (get_result == POTR_SUCCESS)
                        {
                            POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                                     "sender[service_id=%" PRId64 "]: NACK received seq=%u"
                                     " -> retransmit",
                                     ctx->service.service_id,
                                     (unsigned)pkt.ack_num);
                            for (j = 0; j < ctx->n_path; j++)
                            {
                                potr_sendto(ctx->sock[j], ctx->recv_buf, wire_len, 0,
                                            (const struct sockaddr *)&ctx->dest_addr[j],
                                            (int)sizeof(ctx->dest_addr[j]));
                            }
                        }
                        else
                        {
                            POTR_LOG(COM_UTIL_LOG_LEVEL_WARNING,
                                     "sender[service_id=%" PRId64 "]: NACK seq=%u not in window"
                                     " -> REJECT",
                                     ctx->service.service_id,
                                     (unsigned)pkt.ack_num);
                            send_reject(ctx, pkt.ack_num);
                        }
                    }
                }
                /* ACK・その他 (DATA/PING の反射など) は無視 */
                if (ctx->service.type != POTR_TYPE_UNICAST_BIDIR)
                {
                    continue;
                }
                /* UNICAST_BIDIR SENDER: フォールスルーして受信者処理 (FIN/REJECT/DATA/PING) へ */
            }

            /* ── 受信者ロール: FIN / REJECT / DATA / PING を処理 ── */

            /* FIN: 送信者からの正常終了通知 */
            if (pkt.flags & POTR_FLAG_FIN)
            {
                if (!check_and_update_session(ctx, &pkt))
                {
                    continue; /* 旧セッションの FIN → 無視 */
                }

                POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,
                         "recv[service_id=%" PRId64 "]: FIN received (fin_target_seq=%u recv_next=%u)",
                         ctx->service.service_id,
                         (unsigned)pkt.ack_num, (unsigned)ctx->recv_window.next_seq);

                /* target 付き FIN かつ recv_window.next_seq が目標値に未到達: FIN をペンディング。
                 * 後着の DATA がウィンドウを満たした時点で fire_disconnected_by_fin() が呼ばれる。
                 * セッションリセットを遅延することで後着 DATA を引き続き受け入れ可能にする。 */
                if (fin_packet_has_target(&pkt)
                    && !recv_window_reached_fin_target(ctx->recv_window.next_seq, pkt.ack_num))
                {
                    ctx->pending_fin    = 1;
                    ctx->fin_target_seq = pkt.ack_num;
                    POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,
                             "recv[service_id=%" PRId64 "]: FIN pending (waiting for seq=%u)",
                             ctx->service.service_id, (unsigned)pkt.ack_num);
                    continue;
                }

                /* 即時: no-data FIN またはウィンドウ追い付き済み。 */
                fire_disconnected_by_fin(ctx, pkt.ack_num);
                continue;
            }

            /* REJECT: 欠落外側パケットをスキップして後続パケットを配信する */
            if (pkt.flags & POTR_FLAG_REJECT)
            {
                /* RAW モードは REJECT を発生させない (念のため無視) */
                if (potr_is_raw_type(ctx->service.type))
                {
                    continue;
                }

                if (!check_and_update_session(ctx, &pkt))
                {
                    continue;
                }

                /* 送信元から受信できている = 生存確認としてタイムアウトをリセットする */
                update_path_recv(ctx, i, &sender_addr);

                POTR_LOG(COM_UTIL_LOG_LEVEL_WARNING,
                         "recv[service_id=%" PRId64 "]: REJECT received seq=%u"
                         " (packet unrecoverable)",
                         ctx->service.service_id, (unsigned)pkt.ack_num);

                set_all_path_ping_states(ctx->path_ping_state,
                                         POTR_MAX_PATH,
                                         POTR_PING_STATE_UNDEFINED);
                memset((void *)ctx->remote_path_ping_state, 0, sizeof(ctx->remote_path_ping_state));
                disconnect_service_all_paths(ctx);

                /* 欠落外側パケットをスキップして recv_window を前進させる */
                ctx->reorder_pending = 0;
                window_recv_skip(&ctx->recv_window, pkt.ack_num);

                /* 後続パケットを配信 (ウィンドウに溜まっていれば最初の pop で CONNECTED を発火) */
                drain_recv_window(ctx);
                continue;
            }

            /* NACK / ACK は受信者では無視 */
            if (!(pkt.flags & (POTR_FLAG_DATA | POTR_FLAG_PING)))
            {
                continue;
            }

            /* セッション照合 */
            if (!check_and_update_session(ctx, &pkt))
            {
                continue;
            }

            /* peer_port と送信元アドレスを更新 */
            update_path_recv(ctx, i, &sender_addr);

            {
                const char *pkt_kind_str;
                if (pkt.flags & POTR_FLAG_PING)
                {
                    pkt_kind_str = "PING";
                }
                else
                {
                    pkt_kind_str = "DATA";
                }
                POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                         "recv[service_id=%" PRId64 "]: %s seq=%u path=%d",
                         ctx->service.service_id,
                         pkt_kind_str,
                         (unsigned)pkt.seq_num, i);
            }

            if (pkt.flags & POTR_FLAG_PING)
            {
                int ping_state_changed;

                /* ヘルスチェックタイムアウト用受信時刻と受信状態を更新する (PING 限定) */
                ping_state_changed = update_path_health(ctx, i);

                /* PING ペイロード (相手端のパス受信状態) を格納する */
                if (pkt.payload_len >= POTR_MAX_PATH && pkt.payload != NULL)
                {
                    memcpy(ctx->remote_path_ping_state, pkt.payload, POTR_MAX_PATH);
                }

                /* PING はウィンドウ外: NACK・再送の対象外。
                   seq_num は送信側の next_seq (消費前) を示す。
                   通常モード: [recv_window.next_seq, seq_num) の範囲を全スキャンして欠番を一括 NACK する。
                   RAW モード: ギャップがあれば DISCONNECTED を発行してウィンドウを新基点にリセットする。 */
                if (potr_is_raw_type(ctx->service.type))
                {
                    /* pkt.seq_num が next_seq より前方にある (= ギャップあり) か判定する。
                       seqnum_in_window で window_size 以内の前方範囲のみを対象とし、
                       古い PING (next_seq より後方) は無視する。
                       reorder_timeout_ms > 0 のとき、タイムアウト前は DISCONNECTED を保留する。 */
                    if (pkt.seq_num != ctx->recv_window.next_seq
                        && seqnum_in_window(pkt.seq_num,
                                            ctx->recv_window.next_seq + 1U,
                                            ctx->recv_window.window_size))
                    {
                        if (reorder_gap_ready(ctx, ctx->recv_window.next_seq))
                        {
                            raw_session_disconnect(ctx);
                            window_recv_reset(&ctx->recv_window, pkt.seq_num);
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
                        while (scan_seq != pkt.seq_num
                               && seqnum_in_window(scan_seq, ctx->recv_window.base_seq,
                                                   ctx->recv_window.window_size))
                        {
                            uint16_t idx = (uint16_t)((scan_seq - ctx->recv_window.base_seq)
                                                      % ctx->recv_window.window_size);
                            if (!ctx->recv_window.valid[idx])
                            {
                                if (reorder_gap_ready(ctx, scan_seq))
                                {
                                    POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
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
                        if (scan_seq == pkt.seq_num)
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
                process_outer_pkt(ctx, &pkt, i);
            }
        }
    }

    COM_UTIL_THREAD_RETURN;
}

/* ================================================================
 * TCP ストリーム受信スレッド
 * ================================================================ */

/* TCP ソケットが読み取り可能になるまで最大 wait_ms ミリ秒待機する。
 * 戻り値: 1 = データあり、0 = タイムアウト、-1 = エラー。 */
static int tcp_wait_readable(PotrSocket fd, uint32_t wait_ms)
{
    return potr_poll_readable(fd, (int)wait_ms);
}

/* TCP ソケットから正確に n バイト読み取る。
 * 戻り値: 1 = 成功、0 = 切断 (recv が 0 を返した)、-1 = エラー。 */
static int tcp_read_all(PotrSocket fd, uint8_t *buf, size_t n)
{
    return potr_tcp_recv_all(fd, buf, n);
}

/* TCP 受信スレッドに渡す引数 (path ごと) */
typedef struct
{
    struct PotrContext_ *ctx;
    int                  path_idx;
    int                  _pad;
} TcpRecvArg;

static TcpRecvArg s_tcp_recv_args[POTR_MAX_PATH];

/* TCP ストリーム受信スレッド本体 (path ごと) */
COM_UTIL_THREAD_FUNC(tcp_recv_thread_func)
{
    TcpRecvArg          *rarg     = (TcpRecvArg *)arg;
    struct PotrContext_ *ctx      = rarg->ctx;
    int                  path_idx = rarg->path_idx;
    uint8_t             *buf      = ctx->recv_buf; /* PACKET_HEADER_SIZE + max_payload バイト */
    PotrSocket           fd;

    /* PING 受信タイムアウト監視を使用するか判定する。
     * TCP は bootstrap PING 往復だけでも CONNECTED できるが、受信タイムアウト監視は
     * 定周期 PING を送る構成でのみ有効とする。 */
    int      use_recv_timeout = (ctx->health_interval_ms > 0 && ctx->health_timeout_ms > 0);
    /* ポーリング間隔: 1 秒単位でチェックし、health_timeout_ms を超えないようにする。 */
    uint32_t poll_ms = use_recv_timeout
                       ? (ctx->health_timeout_ms < 1000U
                          ? ctx->health_timeout_ms
                          : 1000U)
                       : 0U;

    POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
             "tcp_recv[service_id=%" PRId64 " path=%d]: starting (recv_timeout=%s)",
             ctx->service.service_id, path_idx,
             use_recv_timeout ? "enabled" : "disabled");

    while (ctx->running[path_idx])
    {
        PotrPacket pkt;
        uint16_t   wire_payload_len;
        int        r;

        fd = ctx->tcp_conn_fd[path_idx];
        if (fd == POTR_INVALID_SOCKET)
        {
            break;
        }

        /* ── 先読みバッファ処理 ──
         * accept スレッドが session 判定のために読み取ったパケットが残っている場合、
         * ソケットからの読み取りをスキップしてバッファの内容をそのまま使用する。
         * accept スレッドは recv スレッド起動前に書き込みを完了しているため mutex 不要。 */
        if (ctx->tcp_first_pkt_len[path_idx] > 0)
        {
            /* accept スレッドの先読みバッファを recv バッファにコピーする */
            size_t first_len = ctx->tcp_first_pkt_len[path_idx];
            memcpy(buf, ctx->tcp_first_pkt_buf[path_idx], first_len);
            ctx->tcp_first_pkt_len[path_idx] = 0; /* 先読みバッファをクリア */
            {
                uint16_t wpl;
                memcpy(&wpl, buf + 34, sizeof(wpl));
                wire_payload_len = ntohs(wpl);
            }
        }
        else
        {
        /* タイムアウト付きポーリングで PING 受信を監視する。
         * データが届くまで poll_ms 待機し、タイムアウト時は PING 受信時刻を確認する。 */
        if (use_recv_timeout)
        {
            int readable = tcp_wait_readable(fd, poll_ms);
            if (!ctx->running[path_idx]) break;
            if (readable < 0) break; /* エラー */
            if (readable == 0)
            {
                /* ポーリングタイムアウト: PING 受信時刻を確認する */
                uint64_t last    = ctx->tcp_last_ping_recv_ms[path_idx];
                uint64_t elapsed = com_util_get_monotonic_ms() - last;
                if (last > 0 && elapsed > (uint64_t)ctx->health_timeout_ms)
                {
                    int ping_state_changed;

                    POTR_LOG(COM_UTIL_LOG_LEVEL_WARNING,
                             "tcp_recv[service_id=%" PRId64 " path=%d]: PING timeout"
                             " (%llu ms), disconnecting",
                             ctx->service.service_id, path_idx,
                             (unsigned long long)elapsed);
                    COM_UTIL_MUTEX_LOCK(&ctx->tcp_state_mutex);
                    ping_state_changed = set_path_ping_state(&ctx->path_ping_state[path_idx],
                                                             POTR_PING_STATE_ABNORMAL);
                    sync_service_path_state(ctx);
                    COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_state_mutex);
                    wake_tcp_interrupt_ping_if_needed(ctx, path_idx, ping_state_changed);
                    break;
                }
                continue;
            }
            /* readable == 1: データあり。以降の tcp_read_all へ進む。 */
        }

        /* 1. 固定長ヘッダー読み取り */
        r = tcp_read_all(fd, buf, PACKET_HEADER_SIZE);
        if (r <= 0)
        {
            break; /* 切断 or エラー */
        }

        /* 2. payload_len をヘッダー末尾の offset 34 から抽出する */
        {
            uint16_t wpl;
            memcpy(&wpl, buf + 34, sizeof(wpl));
            wire_payload_len = ntohs(wpl);
        }

        /* 3. ペイロード長バリデーション */
        if ((size_t)wire_payload_len > ctx->global.max_payload)
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_WARNING,
                     "tcp_recv[service_id=%" PRId64 "]: oversized payload %u > max %u,"
                     " disconnecting",
                     ctx->service.service_id,
                     (unsigned)wire_payload_len,
                     (unsigned)ctx->global.max_payload);
            break;
        }

        /* 4. ペイロード読み取り */
        if (wire_payload_len > 0)
        {
            r = tcp_read_all(fd, buf + PACKET_HEADER_SIZE, wire_payload_len);
            if (r <= 0)
            {
                break;
            }
        }
        } /* else (先読みバッファなし) ここまで */

        /* 5. パケット解析 */
        if (packet_parse(&pkt, buf,
                         PACKET_HEADER_SIZE + wire_payload_len) != POTR_SUCCESS)
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                     "tcp_recv[service_id=%" PRId64 "]: packet_parse failed",
                     ctx->service.service_id);
            break;
        }

        /* 6. service_id チェック */
        if (pkt.service_id != ctx->service.service_id)
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                     "tcp_recv[service_id=%" PRId64 "]: service_id mismatch (%" PRId64 ")",
                     ctx->service.service_id, pkt.service_id);
            continue;
        }

        if (!recv_authenticate_packet(ctx, &pkt, buf, "tcp_recv", path_idx)) continue;

        /* 8. パケット種別処理 */
        if (pkt.flags & POTR_FLAG_FIN_ACK)
        {
            POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,
                     "tcp_recv[service_id=%" PRId64 " path=%d]: FIN_ACK ack=%u",
                     ctx->service.service_id, path_idx, (unsigned)pkt.ack_num);
            notify_tcp_close_ack_received(ctx, pkt.ack_num);
            continue;
        }
        else if (pkt.flags & POTR_FLAG_FIN)
        {
            uint32_t fin_target_seq = pkt.ack_num;
            int      should_fire_fin = 0;

            COM_UTIL_MUTEX_LOCK(&ctx->recv_window_mutex);
            if (!check_and_update_session(ctx, &pkt))
            {
                COM_UTIL_MUTEX_UNLOCK(&ctx->recv_window_mutex);
                POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                         "tcp_recv[service_id=%" PRId64 " path=%d]: FIN session mismatch, ignored",
                         ctx->service.service_id, path_idx);
                continue;
            }

            POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,
                     "tcp_recv[service_id=%" PRId64 " path=%d]: FIN received (fin_target_seq=%u recv_next=%u)",
                     ctx->service.service_id, path_idx,
                     (unsigned)fin_target_seq, (unsigned)ctx->recv_window.next_seq);

            if (fin_packet_has_target(&pkt)
                && !recv_window_reached_fin_target(ctx->recv_window.next_seq, fin_target_seq))
            {
                ctx->pending_fin    = 1;
                ctx->fin_target_seq = fin_target_seq;
                COM_UTIL_MUTEX_UNLOCK(&ctx->recv_window_mutex);
                POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,
                         "tcp_recv[service_id=%" PRId64 " path=%d]: FIN pending (waiting for seq=%u)",
                         ctx->service.service_id, path_idx, (unsigned)fin_target_seq);
                continue;
            }

            should_fire_fin = 1;
            COM_UTIL_MUTEX_UNLOCK(&ctx->recv_window_mutex);

            if (should_fire_fin)
            {
                fire_disconnected_by_fin(ctx, fin_target_seq);
            }
            continue;
        }
        else if (pkt.flags & POTR_FLAG_PING)
        {
            int ping_state_changed;

            /* PING 受信: 最終受信時刻と受信状態を更新する。*/
            POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                     "tcp_recv[service_id=%" PRId64 " path=%d]: PING seq=%u",
                     ctx->service.service_id, path_idx, (unsigned)pkt.seq_num);
            COM_UTIL_MUTEX_LOCK(&ctx->tcp_state_mutex);
            ctx->tcp_last_ping_recv_ms[path_idx] = com_util_get_monotonic_ms();
            ping_state_changed = set_path_ping_state(&ctx->path_ping_state[path_idx],
                                                     POTR_PING_STATE_NORMAL);
            if (pkt.payload_len >= POTR_MAX_PATH && pkt.payload != NULL)
            {
                memcpy(ctx->remote_path_ping_state, pkt.payload, POTR_MAX_PATH);
            }
            sync_service_path_state(ctx);
            COM_UTIL_MUTEX_UNLOCK(&ctx->tcp_state_mutex);
            wake_tcp_interrupt_ping_if_needed(ctx, path_idx, ping_state_changed);
        }
        else if (pkt.flags & POTR_FLAG_DATA)
        {
            /* セッション照合 + recv_window への投入 (recv_window_mutex で保護) */
            {
                uint32_t fin_target_seq = 0U;
                int      should_fire_fin = 0;
                int pushed;

                COM_UTIL_MUTEX_LOCK(&ctx->recv_window_mutex);
                if (!check_and_update_session(ctx, &pkt))
                {
                    COM_UTIL_MUTEX_UNLOCK(&ctx->recv_window_mutex);
                    POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                             "tcp_recv[service_id=%" PRId64 " path=%d]: DATA session mismatch, ignored",
                             ctx->service.service_id, path_idx);
                    continue;
                }
                pushed = window_recv_push(&ctx->recv_window, &pkt);
                COM_UTIL_MUTEX_UNLOCK(&ctx->recv_window_mutex);

                if (pushed != POTR_SUCCESS)
                {
                    /* 重複パケット → スキップ */
                    POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                             "tcp_recv[service_id=%" PRId64 " path=%d]: DATA seq=%u duplicate, skipped",
                             ctx->service.service_id, path_idx, (unsigned)pkt.seq_num);
                    continue;
                }

                POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
                         "tcp_recv[service_id=%" PRId64 " path=%d]: DATA seq=%u payload=%u",
                         ctx->service.service_id, path_idx,
                         (unsigned)pkt.seq_num, (unsigned)pkt.payload_len);

                /* 順序整列済みパケットをポップして配信 */
                COM_UTIL_MUTEX_LOCK(&ctx->recv_window_mutex);
                {
                    PotrPacket out;
                    while (window_recv_pop(&ctx->recv_window, &out) == POTR_SUCCESS)
                    {
                        size_t     offset = 0;
                        PotrPacket elem;
                        COM_UTIL_MUTEX_UNLOCK(&ctx->recv_window_mutex);
                        while (packet_unpack_next(&out, &offset, &elem) == POTR_SUCCESS)
                        {
                            deliver_payload_elem(ctx, &elem);
                        }
                        COM_UTIL_MUTEX_LOCK(&ctx->recv_window_mutex);
                    }

                    if (ctx->pending_fin
                        && recv_window_reached_fin_target(ctx->recv_window.next_seq,
                                                          ctx->fin_target_seq))
                    {
                        fin_target_seq  = ctx->fin_target_seq;
                        should_fire_fin = 1;
                    }
                }
                COM_UTIL_MUTEX_UNLOCK(&ctx->recv_window_mutex);

                if (should_fire_fin)
                {
                    fire_disconnected_by_fin(ctx, fin_target_seq);
                }
            }
        }
    }

    /* 接続断処理: DISCONNECTED イベントは connect スレッドが tcp_active_paths == 0 時に発火する */
    ctx->running[path_idx] = 0;

    POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
             "tcp_recv[service_id=%" PRId64 " path=%d]: exited",
             ctx->service.service_id, path_idx);

    COM_UTIL_THREAD_RETURN;
}

/**
 *******************************************************************************
 *  @brief          非 TCP 受信スレッドを起動します。
 *  @param[in,out]  ctx  セッションコンテキストへのポインタ。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *******************************************************************************
 */
int comm_recv_thread_start(struct PotrContext_ *ctx)
{
    if (ctx == NULL)
    {
        return POTR_ERROR;
    }

    ctx->running[0] = 1;

    POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
             "recv_thread[service_id=%" PRId64 "]: starting",
             ctx->service.service_id);

    if (com_util_thread_create(&ctx->recv_thread[0], recv_thread_func, ctx) != 0)
    {
        ctx->running[0] = 0;
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "recv_thread[service_id=%" PRId64 "]: thread create failed",
                 ctx->service.service_id);
        return POTR_ERROR;
    }

    return POTR_SUCCESS;
}

/**
 *******************************************************************************
 *  @brief          非 TCP 受信スレッドを停止します。
 *  @param[in,out]  ctx  セッションコンテキストへのポインタ。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *******************************************************************************
 */
int comm_recv_thread_stop(struct PotrContext_ *ctx)
{
    if (ctx == NULL)
    {
        return POTR_ERROR;
    }

    ctx->running[0] = 0;

    {
        int i;
        for (i = 0; i < ctx->n_path; i++)
        {
            if (ctx->sock[i] != POTR_INVALID_SOCKET)
            {
#if defined(PLATFORM_LINUX)
                shutdown(ctx->sock[i], SHUT_RD);
#elif defined(PLATFORM_WINDOWS)
                potr_close_socket(ctx->sock[i]);
                ctx->sock[i] = POTR_INVALID_SOCKET;
#endif /* PLATFORM_ */
            }
        }
    }

    com_util_thread_join(&ctx->recv_thread[0]);

    return POTR_SUCCESS;
}

/**
 *******************************************************************************
 *  @brief          TCP 受信スレッドを path ごとに起動します。
 *  @param[in,out]  ctx       セッションコンテキストへのポインタ。
 *  @param[in]      path_idx  パスインデックス (0 ～ n_path-1)。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *******************************************************************************
 */
int tcp_recv_thread_start(struct PotrContext_ *ctx, int path_idx)
{
    if (ctx == NULL) { return POTR_ERROR; }

    ctx->running[path_idx] = 1;

    s_tcp_recv_args[path_idx].ctx      = ctx;
    s_tcp_recv_args[path_idx].path_idx = path_idx;

    POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
             "tcp_recv_thread[service_id=%" PRId64 " path=%d]: starting",
             ctx->service.service_id, path_idx);

    if (com_util_thread_create(&ctx->recv_thread[path_idx], tcp_recv_thread_func,
                           &s_tcp_recv_args[path_idx]) != 0)
    {
        ctx->running[path_idx] = 0;
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "tcp_recv_thread[service_id=%" PRId64 " path=%d]: thread create failed",
                 ctx->service.service_id, path_idx);
        return POTR_ERROR;
    }

    return POTR_SUCCESS;
}

/**
 *******************************************************************************
 *  @brief          TCP 受信スレッドの終了を待機します。
 *  @details        スレッドの停止はソケットクローズ (connect スレッド側) で行います。
 *                  本関数は join のみを担当します。
 *  @param[in,out]  ctx       セッションコンテキストへのポインタ。
 *  @param[in]      path_idx  パスインデックス (0 ～ n_path-1)。
 *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
 *******************************************************************************
 */
int tcp_recv_thread_stop(struct PotrContext_ *ctx, int path_idx)
{
    if (ctx == NULL) { return POTR_ERROR; }

    ctx->running[path_idx] = 0;

    com_util_thread_join(&ctx->recv_thread[path_idx]);

    return POTR_SUCCESS;
}
