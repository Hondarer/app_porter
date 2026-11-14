/**
 *******************************************************************************
 *  @file           thread_recv_validate.c
 *  @brief          受信パケットの暗号認証と UDP 送信元の照合を行います。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <string.h>
#include <cplat/runtime/memory_lock.h>
#include <inttypes.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/packet_seal.h>
#include <porter/infra/potr_trace.h>
#include <cplat/net/byteorder.h>

#include "thread_recv_validate.h"

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_authenticate_packet(potr_context *ctx, potr_packet *pkt, const uint8_t *wire_hdr,
                                    const char *log_prefix, int path_idx)
{
    uint8_t dummy[1];
    uint8_t *plaintext = ctx->recv_crypto_buf;
    size_t capacity = ctx->crypto_buf_size;
    const uint8_t *key = NULL;
    uint8_t derived_key[POTR_CRYPTO_KEY_SIZE] = {0};
    int ret;

    if (pkt->flags & (POTR_FLAG_HANDSHAKE | POTR_FLAG_KEY))
    {
        return POTR_ERR_PROTOCOL;
    }
    if (ctx->service.auth == POTR_AUTH_CERT)
    {
        if (!(pkt->flags & POTR_FLAG_ENCRYPTED))
        {
            return POTR_ERR_PROTOCOL;
        }
        ret = potr_internal_certificate_key(ctx, NULL, pkt, 0, derived_key);
        if (ret != POTR_OK)
        {
            cplat_secure_zero(derived_key, sizeof(derived_key));
            return ret;
        }
        key = derived_key;
    }
    else if (ctx->service.encrypt_enabled || (pkt->flags & POTR_FLAG_ENCRYPTED))
    {
        key = ctx->service.encrypt_key;
    }
    if (!(pkt->flags & (POTR_FLAG_DATA | POTR_FLAG_PING | POTR_FLAG_REPORT)))
    {
        plaintext = dummy;
        capacity = sizeof(dummy);
    }
    ret = potr_internal_packet_open(pkt, wire_hdr, key, plaintext, capacity);
    cplat_secure_zero(derived_key, sizeof(derived_key));
    if (ret != POTR_OK)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                   "%s[service_id=%" PRId64 " path=%d]: authentication failed result=%d",
                   log_prefix, ctx->service.service_id, path_idx, ret);
    }
    return ret;
}

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_replay_packet(potr_internal_replay_window *window, const potr_packet *packet,
                               const uint8_t *wire_header)
{
    uint16_t flags;
    int ret;

    memcpy(&flags, wire_header + 32U, sizeof(flags));
    if (!(cplat_ntoh16(flags) & POTR_FLAG_ENCRYPTED))
    {
        return POTR_OK;
    }
    ret = potr_internal_replay_window_accept(window, packet);
    if (ret != POTR_OK)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                   "replay: dropping session=%u counter=%" PRIu64,
                   (unsigned)packet->session_id, packet->tx_counter);
    }
    return ret;
}

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_check_src_addr(const potr_context *ctx, const cplat_ipv4_endpoint *sender)
{
    int i;

    /*
     * N:1 モードでは、src_port 指定時はポートのみでフィルタリングし、未指定時は全許可する。
     * 正本は app/porter/docs/config.md の「送信元フィルタリング」節である。
     */
    if (ctx->is_multi_peer)
    {
        if (ctx->service.src_port != 0)
        {
            /* sender->port はネットワーク バイト オーダーである。 */
            if (cplat_ntoh16(sender->port) == ctx->service.src_port)
            {
                return 1;
            }

            return 0;
        }

        return 1;
    }

    if (ctx->role == POTR_ROLE_SENDER && is_oneway_udp_type(ctx->service.type) &&
        (!is_raw_type(ctx->service.type) || ctx->service.auth == POTR_AUTH_CERT))
    {
        if (raw_base_type(ctx->service.type) != POTR_TYPE_UNICAST)
        {
            return 1;
        }
        for (i = 0; i < ctx->n_path; i++)
        {
            if (sender->address == ctx->dst_addr_resolved[i])
            {
                return 1;
            }
        }
        return 0;
    }

    /* 1:1 双方向では、役割に応じた相手アドレスを照合する。 */
    if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
    {
        if (ctx->role == POTR_ROLE_SENDER)
        {
            /*
             * SENDER が受け取るパケットは、RECEIVER (dst_addr) から来る。
             * dst_addr の先頭が空なら未指定としてすべて許可する。
             */
            if (ctx->service.dst_addr[0][0] == '\0')
            {
                return 1;
            }

            for (i = 0; i < ctx->n_path; i++)
            {
                if (sender->address == ctx->dst_addr_resolved[i])
                {
                    return 1;
                }
            }
        }
        else
        {
            /*
             * RECEIVER が受け取るパケットは、SENDER (src_addr) から来る。
             * src_addr の先頭が空なら未指定としてすべて許可する。
             */
            if (ctx->service.src_addr[0][0] == '\0')
            {
                return 1;
            }

            for (i = 0; i < ctx->n_path; i++)
            {
                if (sender->address == ctx->src_addr_resolved[i])
                {
                    return 1;
                }
            }
        }

        return 0;
    }

    /*
     * 上記以外の通信種別では、設定された src_addr と送信元アドレスを照合する。
     * src_addr の先頭が空なら未指定としてすべて許可する。
     * 正本は app/porter/docs/config.md の「送信元フィルタリング」節である。
     */
    if (ctx->service.src_addr[0][0] == '\0')
    {
        return 1;
    }

    for (i = 0; i < ctx->n_path; i++)
    {
        if (sender->address == ctx->src_addr_resolved[i])
        {
            return 1;
        }
    }

    return 0;
}
