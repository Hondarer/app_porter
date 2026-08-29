/**
 *******************************************************************************
 *  @file           potr_peer_table.c
 *  @brief          N:1 モード用ピア テーブルを管理する機能を実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/23
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/base/platform.h>
#include <cplat/crt/stdlib.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/porter_spec.h>

#include <porter/potr_context.h>
#include <porter/potr_peer_table.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/window.h>
#include <porter/infra/potr_result.h>
#include <porter/infra/potr_trace.h>
#include <cplat/crypto/crypto.h>
#include <cplat/crypto/random.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/endpoint.h>
#include <cplat/net/socket.h>

/* ピアのセッション識別子・開始時刻を生成して peer に格納する
 * session_id は AES-256-GCM nonce の非決定要素であり、衝突または推測は
 * (key, nonce) の再利用を招く。暗号論的乱数源から取得する。 */
static int peer_generate_session(potr_internal_peer_context *peer)
{
    int ret;

    ret = cplat_random_bytes(&peer->session_id, sizeof(peer->session_id));
    if (ret != CPLAT_OK)
    {
        return potr_internal_result_from_socket_result(ret, NULL);
    }
    cplat_get_realtime(&peer->session_ts);

    return POTR_OK;
}

/* 使用中でない peer_id を単調増加カウンターから生成する (peers_mutex 取得済みの文脈で呼ぶ) */
static potr_peer_id allocate_peer_id(potr_context *ctx)
{
    potr_peer_id candidate = ctx->next_peer_id;
    int i;
    int in_use;

    for (;;)
    {
        /* 予約値をスキップ */
        if (candidate == 0 || candidate == POTR_PEER_ALL)
        {
            candidate++;
            continue;
        }

        /* 現在接続中ピアとの衝突チェック */
        in_use = 0;
        for (i = 0; i < ctx->max_peers; i++)
        {
            if (ctx->peers[i].active && ctx->peers[i].peer_id == candidate)
            {
                in_use = 1;
                break;
            }
        }

        if (!in_use)
        {
            break;
        }
        candidate++;
    }

    ctx->next_peer_id = candidate + 1;
    return candidate;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_peer_send_fin(potr_context *ctx, potr_internal_peer_context *peer)
{
    potr_packet fin_pkt;
    potr_internal_packet_session_hdr shdr;
    uint32_t wire_target_seq = 0U;
    int has_data = 0;
    size_t wire_len;
    int i;

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = peer->session_id;
    potr_session_ts_to_hdr(&peer->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);

    if (potr_internal_packet_build_fin(&fin_pkt, &shdr) != POTR_OK)
    {
        return;
    }

    /* 現セッションで DATA を送っている場合のみ FIN target を有効化する。 */
    cplat_local_lock_lock(peer->send_window_mutex, CPLAT_SYNC_WAIT_FOREVER);
    wire_target_seq = peer->send_window.next_seq;
    has_data = peer->send_has_data;
    cplat_local_lock_unlock(peer->send_window_mutex);

    if (has_data)
    {
        fin_pkt.flags |= cplat_hton16(POTR_FLAG_FIN_TARGET_VALID);
        fin_pkt.ack_num = cplat_hton32(wire_target_seq);
    }

    if (ctx->service.encrypt_enabled)
    {
        uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t enc_out = POTR_CRYPTO_TAG_SIZE;

        fin_pkt.flags |= cplat_hton16(POTR_FLAG_ENCRYPTED);
        fin_pkt.payload_len = cplat_hton16((uint16_t)POTR_CRYPTO_TAG_SIZE);

        /* ノンス: session_id(4B) + flags(2B, FIN|ENCRYPTED NBO) + 0(4B) + padding(2B) */
        memcpy(nonce, &fin_pkt.session_id, 4);
        memcpy(nonce + 4, &fin_pkt.flags, 2);
        memset(nonce + 6, 0, 4);
        memset(nonce + 10, 0, 2);

        memcpy(wire_buf, &fin_pkt, PACKET_HEADER_SIZE);
        if (cplat_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out, NULL, 0, ctx->service.encrypt_key, nonce,
                             wire_buf, PACKET_HEADER_SIZE) != CPLAT_OK)
        {
            return;
        }
        wire_len = PACKET_HEADER_SIZE + enc_out;

        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            size_t sent = 0;

            if (potr_endpoint_is_unset(&peer->dest_addr[i]))
                continue;
            if (ctx->sock[i] == CPLAT_INVALID_SOCKET)
                continue;
            (void)cplat_socket_sendto(ctx->sock[i], wire_buf, wire_len, &peer->dest_addr[i], &sent, NULL);
        }
    }
    else
    {
        wire_len = potr_internal_packet_wire_size(&fin_pkt);

        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            size_t sent = 0;

            if (potr_endpoint_is_unset(&peer->dest_addr[i]))
                continue;
            if (ctx->sock[i] == CPLAT_INVALID_SOCKET)
                continue;
            (void)cplat_socket_sendto(ctx->sock[i], (const uint8_t *)&fin_pkt, wire_len, &peer->dest_addr[i], &sent,
                                         NULL);
        }
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_peer_table_init(potr_context *ctx)
{
    int i;

    ctx->peers = (potr_internal_peer_context *)cplat_calloc((size_t)ctx->max_peers, sizeof(potr_internal_peer_context));
    if (ctx->peers == NULL)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_internal_peer_table_init: service_id=%" PRId64 " calloc failed (max_peers=%d)",
                   ctx->service.service_id, ctx->max_peers);
        return POTR_ERR_OUT_OF_MEMORY;
    }

    for (i = 0; i < ctx->max_peers; i++)
    {
        ctx->peers[i].active = 0;
    }

    cplat_local_lock_create(&ctx->peers_mutex);
    ctx->n_peers = 0;
    ctx->next_peer_id = 1U;

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "potr_internal_peer_table_init: service_id=%" PRId64 " max_peers=%d",
               ctx->service.service_id, ctx->max_peers);

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_peer_table_dispose(potr_context *ctx)
{
    int i;

    if (ctx->peers == NULL)
    {
        return;
    }

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "potr_internal_peer_table_dispose: service_id=%" PRId64 " n_peers=%d",
               ctx->service.service_id, ctx->n_peers);

    for (i = 0; i < ctx->max_peers; i++)
    {
        if (!ctx->peers[i].active)
        {
            continue;
        }

        /* 各ピアへ FIN を送信 */
        potr_internal_peer_send_fin(ctx, &ctx->peers[i]);

        /* リソース解放 */
        potr_internal_window_dispose(&ctx->peers[i].send_window);
        potr_internal_window_dispose(&ctx->peers[i].recv_window);
        cplat_local_lock_dispose(ctx->peers[i].send_window_mutex);
        cplat_free(ctx->peers[i].frag_buf);
        ctx->peers[i].frag_buf = NULL;
        ctx->peers[i].active = 0;
    }

    cplat_local_lock_dispose(ctx->peers_mutex);

    cplat_free(ctx->peers);
    ctx->peers = NULL;
    ctx->n_peers = 0;
}

/* Doxygen コメントは、ヘッダーに記載 */

potr_internal_peer_context *potr_internal_peer_find_by_session(const potr_context *ctx, uint32_t session_id, const cplat_timespec *session_ts)
{
    int i;

    for (i = 0; i < ctx->max_peers; i++)
    {
        if (!ctx->peers[i].active)
        {
            continue;
        }
        if (ctx->peers[i].peer_session_id == session_id &&
            cplat_timespec_cmp(&ctx->peers[i].peer_session_ts, session_ts) == 0)
        {
            return (potr_internal_peer_context *)&ctx->peers[i];
        }
    }
    return NULL;
}

/* Doxygen コメントは、ヘッダーに記載 */

potr_internal_peer_context *potr_internal_peer_find_by_id(const potr_context *ctx, potr_peer_id peer_id)
{
    int i;

    for (i = 0; i < ctx->max_peers; i++)
    {
        if (ctx->peers[i].active && ctx->peers[i].peer_id == peer_id)
        {
            return (potr_internal_peer_context *)&ctx->peers[i];
        }
    }
    return NULL;
}

/* Doxygen コメントは、ヘッダーに記載 */

potr_internal_peer_context *potr_internal_peer_create(potr_context *ctx, const cplat_ipv4_endpoint *sender_addr, int path_idx)
{
    int i;
    potr_internal_peer_context *peer = NULL;

    /* max_peers 超過チェック */
    if (ctx->n_peers >= ctx->max_peers)
    {
        char ip_str[CPLAT_IPV4_ADDR_STRLEN];
        (void)cplat_ipv4_to_string(sender_addr->address, ip_str, sizeof(ip_str), NULL);
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "potr_internal_peer_create: service_id=%" PRId64 " max_peers=%d reached, "
                   "rejecting new connection from %s:%u",
                   ctx->service.service_id, ctx->max_peers, ip_str, (unsigned)cplat_ntoh16(sender_addr->port));
        return NULL;
    }

    /* 空きスロットを確保 */
    for (i = 0; i < ctx->max_peers; i++)
    {
        if (!ctx->peers[i].active)
        {
            peer = &ctx->peers[i];
            break;
        }
    }

    if (peer == NULL)
    {
        /* n_peers < max_peers のはずなのにスロットが見つからない (内部整合性エラー) */
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_internal_peer_create: service_id=%" PRId64 " no free slot (internal error)",
                   ctx->service.service_id);
        return NULL;
    }

    /* スロットを初期化 */
    memset(peer, 0, sizeof(*peer));

    peer->peer_id = allocate_peer_id(ctx);
    peer->active = 1;

    /* 自セッション生成 */
    if (peer_generate_session(peer) != POTR_OK)
    {
        peer->active = 0;
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_internal_peer_create: service_id=%" PRId64 " session id generation failed",
                   ctx->service.service_id);
        return NULL;
    }
    peer->send_has_data = 0;

    /* ウィンドウ初期化 */
    if (potr_internal_window_init(&peer->send_window, 0, ctx->global.window_size, ctx->global.max_payload) != POTR_OK)
    {
        peer->active = 0;
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_internal_peer_create: service_id=%" PRId64 " send_window init failed",
                   ctx->service.service_id);
        return NULL;
    }

    if (potr_internal_window_init(&peer->recv_window, 0, ctx->global.window_size, ctx->global.max_payload) != POTR_OK)
    {
        potr_internal_window_dispose(&peer->send_window);
        peer->active = 0;
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_internal_peer_create: service_id=%" PRId64 " recv_window init failed",
                   ctx->service.service_id);
        return NULL;
    }

    cplat_local_lock_create(&peer->send_window_mutex);

    /* フラグメント結合バッファー確保 */
    peer->frag_buf = (uint8_t *)cplat_malloc(ctx->global.max_message_size);
    if (peer->frag_buf == NULL)
    {
        potr_internal_window_dispose(&peer->recv_window);
        potr_internal_window_dispose(&peer->send_window);
        cplat_local_lock_dispose(peer->send_window_mutex);
        peer->active = 0;
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_internal_peer_create: service_id=%" PRId64 " frag_buf alloc failed",
                   ctx->service.service_id);
        return NULL;
    }
    peer->frag_buf_len = 0;
    peer->frag_compressed = 0;

    /* 送信元アドレスを最初のパスとして記録 (インデックス = path_idx = ctx->sock[] の添字) */
    peer->dest_addr[path_idx] = *sender_addr;
    peer->path_last_recv_ts[path_idx].tv_sec = 0; /* n1_update_path_recv() で更新される */
    peer->n_paths = 1;

    ctx->n_peers++;

    POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "potr_internal_peer_create: service_id=%" PRId64 " peer_id=%u created (n_peers=%d)",
               ctx->service.service_id, (unsigned)peer->peer_id, ctx->n_peers);

    return peer;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_peer_path_clear(const potr_context *ctx, potr_internal_peer_context *peer, int path_idx)
{
    if (potr_endpoint_is_unset(&peer->dest_addr[path_idx]))
    {
        return; /* すでに未使用スロット */
    }

    POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING, "potr_internal_peer_path_clear: service_id=%" PRId64 " peer=%u path %d cleared",
               ctx->service.service_id, (unsigned)peer->peer_id, path_idx);

    potr_endpoint_clear(&peer->dest_addr[path_idx]);
    peer->path_last_recv_ts[path_idx].tv_sec = 0;
    peer->path_last_recv_ts[path_idx].tv_nsec = 0;
    peer->n_paths--;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_peer_free(potr_context *ctx, potr_internal_peer_context *peer)
{
    if (peer == NULL || !peer->active)
    {
        return;
    }

    POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "potr_internal_peer_free: service_id=%" PRId64 " peer_id=%u freed", ctx->service.service_id,
               (unsigned)peer->peer_id);

    potr_internal_window_dispose(&peer->send_window);
    potr_internal_window_dispose(&peer->recv_window);
    cplat_local_lock_dispose(peer->send_window_mutex);

    cplat_free(peer->frag_buf);
    peer->frag_buf = NULL;

    peer->active = 0;
    ctx->n_peers--;
}
