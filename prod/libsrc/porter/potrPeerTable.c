/**
 *******************************************************************************
 *  @file           potrPeerTable.c
 *  @brief          N:1 モード用ピアテーブル管理の実装。
 *  @author         Tetsuo Honda
 *  @date           2026/03/23
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#if defined(PLATFORM_LINUX)
    #include <arpa/inet.h>
#endif /* PLATFORM_LINUX */

#include <porter_const.h>
#include <porter.h>

#include "potrContext.h"
#include "potrPeerTable.h"
#include "protocol/packet.h"
#include "protocol/window.h"
#include "infra/potrTrace.h"
#include "infra/potrPlatform.h"
#include <com_util/crypto/crypto.h>

/* ピアのセッション識別子・開始時刻を生成して peer に格納する */
static void peer_generate_session(PotrPeerContext *peer)
{
    srand((unsigned int)com_util_get_monotonic_ms());
    peer->session_id = (uint32_t)rand();
    com_util_get_realtime(&peer->session_tv_sec, &peer->session_tv_nsec);
}

/* 使用中でない peer_id を単調増加カウンタから生成する (peers_mutex 取得済みの文脈で呼ぶ) */
static PotrPeerId allocate_peer_id(struct PotrContext_ *ctx)
{
    PotrPeerId candidate = ctx->next_peer_id;
    int        i;
    int        in_use;

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

/* doxygen コメントは、ヘッダに記載 */
void peer_send_fin(struct PotrContext_ *ctx, PotrPeerContext *peer)
{
    PotrPacket           fin_pkt;
    PotrPacketSessionHdr shdr;
    uint32_t             wire_target_seq = 0U;
    int                  has_data = 0;
    size_t               wire_len;
    int                  i;

    shdr.service_id      = ctx->service.service_id;
    shdr.session_id      = peer->session_id;
    shdr.session_tv_sec  = peer->session_tv_sec;
    shdr.session_tv_nsec = peer->session_tv_nsec;

    if (packet_build_fin(&fin_pkt, &shdr) != POTR_SUCCESS)
    {
        return;
    }

    /* 現セッションで DATA を送っている場合のみ FIN target を有効化する。 */
    COM_UTIL_MUTEX_LOCK(&peer->send_window_mutex);
    wire_target_seq = peer->send_window.next_seq;
    has_data        = peer->send_has_data;
    COM_UTIL_MUTEX_UNLOCK(&peer->send_window_mutex);

    if (has_data)
    {
        fin_pkt.flags  |= htons(POTR_FLAG_FIN_TARGET_VALID);
        fin_pkt.ack_num = htonl(wire_target_seq);
    }

    if (ctx->service.encrypt_enabled)
    {
        uint8_t  wire_buf[PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
        uint8_t  nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t   enc_out = POTR_CRYPTO_TAG_SIZE;

        fin_pkt.flags      |= htons(POTR_FLAG_ENCRYPTED);
        fin_pkt.payload_len = htons((uint16_t)POTR_CRYPTO_TAG_SIZE);

        /* ノンス: session_id(4B) + flags(2B, FIN|ENCRYPTED NBO) + 0(4B) + padding(2B) */
        memcpy(nonce,      &fin_pkt.session_id, 4);
        memcpy(nonce + 4,  &fin_pkt.flags,      2);
        memset(nonce + 6,  0,                   4);
        memset(nonce + 10, 0,                   2);

        memcpy(wire_buf, &fin_pkt, PACKET_HEADER_SIZE);
        if (com_util_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out,
                         NULL, 0,
                         ctx->service.encrypt_key,
                         nonce,
                         wire_buf, PACKET_HEADER_SIZE) != 0)
        {
            return;
        }
        wire_len = PACKET_HEADER_SIZE + enc_out;

        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            if (peer->dest_addr[i].sin_family == 0) continue;
            if (ctx->sock[i] == POTR_INVALID_SOCKET) continue;
            potr_sendto(ctx->sock[i], wire_buf, wire_len, 0,
                        (const struct sockaddr *)&peer->dest_addr[i],
                        (int)sizeof(peer->dest_addr[i]));
        }
    }
    else
    {
        wire_len = packet_wire_size(&fin_pkt);

        for (i = 0; i < (int)POTR_MAX_PATH; i++)
        {
            if (peer->dest_addr[i].sin_family == 0) continue;
            if (ctx->sock[i] == POTR_INVALID_SOCKET) continue;
            potr_sendto(ctx->sock[i], (const uint8_t *)&fin_pkt, wire_len, 0,
                        (const struct sockaddr *)&peer->dest_addr[i],
                        (int)sizeof(peer->dest_addr[i]));
        }
    }
}

/* doxygen コメントは、ヘッダに記載 */
int peer_table_init(struct PotrContext_ *ctx)
{
    int i;

    ctx->peers = (PotrPeerContext *)calloc((size_t)ctx->max_peers,
                                           sizeof(PotrPeerContext));
    if (ctx->peers == NULL)
    {
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "peer_table_init: service_id=%" PRId64 " calloc failed (max_peers=%d)",
                 ctx->service.service_id, ctx->max_peers);
        return POTR_ERROR;
    }

    for (i = 0; i < ctx->max_peers; i++)
    {
        ctx->peers[i].active = 0;
    }

    COM_UTIL_MUTEX_INIT(&ctx->peers_mutex);
    ctx->n_peers      = 0;
    ctx->next_peer_id = 1U;

    POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
             "peer_table_init: service_id=%" PRId64 " max_peers=%d",
             ctx->service.service_id, ctx->max_peers);

    return POTR_SUCCESS;
}

/* doxygen コメントは、ヘッダに記載 */
void peer_table_destroy(struct PotrContext_ *ctx)
{
    int i;

    if (ctx->peers == NULL)
    {
        return;
    }

    POTR_LOG(COM_UTIL_LOG_LEVEL_VERBOSE,
             "peer_table_destroy: service_id=%" PRId64 " n_peers=%d",
             ctx->service.service_id, ctx->n_peers);

    for (i = 0; i < ctx->max_peers; i++)
    {
        if (!ctx->peers[i].active)
        {
            continue;
        }

        /* 各ピアへ FIN を送信 */
        peer_send_fin(ctx, &ctx->peers[i]);

        /* リソース解放 */
        window_destroy(&ctx->peers[i].send_window);
        window_destroy(&ctx->peers[i].recv_window);
        COM_UTIL_MUTEX_DESTROY(&ctx->peers[i].send_window_mutex);
        free(ctx->peers[i].frag_buf);
        ctx->peers[i].frag_buf = NULL;
        ctx->peers[i].active   = 0;
    }

    COM_UTIL_MUTEX_DESTROY(&ctx->peers_mutex);

    free(ctx->peers);
    ctx->peers   = NULL;
    ctx->n_peers = 0;
}

/* doxygen コメントは、ヘッダに記載 */
PotrPeerContext *peer_find_by_session(struct PotrContext_ *ctx,
                                      uint32_t session_id,
                                      int64_t  session_tv_sec,
                                      int32_t  session_tv_nsec)
{
    int i;

    for (i = 0; i < ctx->max_peers; i++)
    {
        if (!ctx->peers[i].active)
        {
            continue;
        }
        if (ctx->peers[i].peer_session_id      == session_id      &&
            ctx->peers[i].peer_session_tv_sec  == session_tv_sec  &&
            ctx->peers[i].peer_session_tv_nsec == session_tv_nsec)
        {
            return &ctx->peers[i];
        }
    }
    return NULL;
}

/* doxygen コメントは、ヘッダに記載 */
PotrPeerContext *peer_find_by_id(struct PotrContext_ *ctx, PotrPeerId peer_id)
{
    int i;

    for (i = 0; i < ctx->max_peers; i++)
    {
        if (ctx->peers[i].active && ctx->peers[i].peer_id == peer_id)
        {
            return &ctx->peers[i];
        }
    }
    return NULL;
}

/* doxygen コメントは、ヘッダに記載 */
PotrPeerContext *peer_create(struct PotrContext_       *ctx,
                              const struct sockaddr_in *sender_addr,
                              int                       path_idx)
{
    int              i;
    PotrPeerContext *peer = NULL;

    /* max_peers 超過チェック */
    if (ctx->n_peers >= ctx->max_peers)
    {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender_addr->sin_addr, ip_str, sizeof(ip_str));
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "peer_create: service_id=%" PRId64 " max_peers=%d reached, "
                 "rejecting new connection from %s:%u",
                 ctx->service.service_id, ctx->max_peers,
                 ip_str, (unsigned)ntohs(sender_addr->sin_port));
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
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "peer_create: service_id=%" PRId64 " no free slot (internal error)",
                 ctx->service.service_id);
        return NULL;
    }

    /* スロットを初期化 */
    memset(peer, 0, sizeof(*peer));

    peer->peer_id = allocate_peer_id(ctx);
    peer->active  = 1;

    /* 自セッション生成 */
    peer_generate_session(peer);
    peer->send_has_data = 0;

    /* ウィンドウ初期化 */
    if (window_init(&peer->send_window, 0,
                    ctx->global.window_size, ctx->global.max_payload) != POTR_SUCCESS)
    {
        peer->active = 0;
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "peer_create: service_id=%" PRId64 " send_window init failed",
                 ctx->service.service_id);
        return NULL;
    }

    if (window_init(&peer->recv_window, 0,
                    ctx->global.window_size, ctx->global.max_payload) != POTR_SUCCESS)
    {
        window_destroy(&peer->send_window);
        peer->active = 0;
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "peer_create: service_id=%" PRId64 " recv_window init failed",
                 ctx->service.service_id);
        return NULL;
    }

    COM_UTIL_MUTEX_INIT(&peer->send_window_mutex);

    /* フラグメント結合バッファ確保 */
    peer->frag_buf = (uint8_t *)malloc(ctx->global.max_message_size);
    if (peer->frag_buf == NULL)
    {
        window_destroy(&peer->recv_window);
        window_destroy(&peer->send_window);
        COM_UTIL_MUTEX_DESTROY(&peer->send_window_mutex);
        peer->active = 0;
        POTR_LOG(COM_UTIL_LOG_LEVEL_ERROR,
                 "peer_create: service_id=%" PRId64 " frag_buf alloc failed",
                 ctx->service.service_id);
        return NULL;
    }
    peer->frag_buf_len    = 0;
    peer->frag_compressed = 0;

    /* 送信元アドレスを最初のパスとして記録 (インデックス = path_idx = ctx->sock[] の添字) */
    peer->dest_addr[path_idx]           = *sender_addr;
    peer->path_last_recv_sec[path_idx]  = 0; /* n1_update_path_recv() で更新される */
    peer->n_paths                       = 1;

    ctx->n_peers++;

    POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,
             "peer_create: service_id=%" PRId64 " peer_id=%u created (n_peers=%d)",
             ctx->service.service_id, (unsigned)peer->peer_id, ctx->n_peers);

    return peer;
}

/* doxygen コメントは、ヘッダに記載 */
void peer_path_clear(struct PotrContext_ *ctx, PotrPeerContext *peer, int path_idx)
{
    if (peer->dest_addr[path_idx].sin_family == 0)
    {
        return; /* すでに未使用スロット */
    }

    POTR_LOG(COM_UTIL_LOG_LEVEL_WARNING,
             "peer_path_clear: service_id=%" PRId64 " peer=%u path %d cleared",
             ctx->service.service_id, (unsigned)peer->peer_id, path_idx);

    memset(&peer->dest_addr[path_idx], 0, sizeof(peer->dest_addr[path_idx]));
    peer->path_last_recv_sec[path_idx]  = 0;
    peer->path_last_recv_nsec[path_idx] = 0;
    peer->n_paths--;
}

/* doxygen コメントは、ヘッダに記載 */
void peer_free(struct PotrContext_ *ctx, PotrPeerContext *peer)
{
    if (peer == NULL || !peer->active)
    {
        return;
    }

    POTR_LOG(COM_UTIL_LOG_LEVEL_INFO,
             "peer_free: service_id=%" PRId64 " peer_id=%u freed",
             ctx->service.service_id, (unsigned)peer->peer_id);

    window_destroy(&peer->send_window);
    window_destroy(&peer->recv_window);
    COM_UTIL_MUTEX_DESTROY(&peer->send_window_mutex);

    free(peer->frag_buf);
    peer->frag_buf = NULL;

    peer->active = 0;
    ctx->n_peers--;
}
