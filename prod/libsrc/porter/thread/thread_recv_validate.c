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
#include <inttypes.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/protocol/packet.h>
#include <porter/infra/potr_trace.h>
#include <cplat/crypto/crypto.h>
#include <cplat/net/byteorder.h>

#include "thread_recv_validate.h"

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_authenticate_packet(potr_context *ctx, potr_packet *pkt, const uint8_t *wire_hdr,
                                    const char *log_prefix, int path_idx)
{
    /*
     * 暗号化を要求するサービスは、ENCRYPTED のないパケットを破棄する。
     * 暗号化を要求しないサービスは、平文のパケットを受理する。
     * 正本は app/porter/docs/config.md の「encrypt_key の詳細」節である。
     */
    if (!(pkt->flags & POTR_FLAG_ENCRYPTED))
    {
        if (!ctx->service.encrypt_enabled)
        {
            return POTR_OK;
        }

        if (path_idx >= 0)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                       "%s[service_id=%" PRId64 " path=%d]: missing ENCRYPTED flag, dropping flags=0x%04x", log_prefix,
                       ctx->service.service_id, path_idx, (unsigned)pkt->flags);
        }
        else
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                       "%s[service_id=%" PRId64 "]: missing ENCRYPTED flag, dropping flags=0x%04x", log_prefix,
                       ctx->service.service_id, (unsigned)pkt->flags);
        }

        return POTR_ERR_PROTOCOL;
    }

    /*
     * DATA と PING は暗号文のペイロードを復号する。
     * AAD には受信ヘッダー wire_hdr を使い、ヘッダーの改ざんを検出する。
     * 正本は app/porter/docs/config.md の「encrypt_key の詳細」節である。
     * 解析済みフィールドはホスト バイト オーダーなので、ノンスへ詰める前にネットワーク バイト オーダーへ戻す。
     * ノンスは session_id、flags、seq_num、2 [byte] の 0 の順で、flags は ENCRYPTED を含んだ送信時の値である。
     * 正本は app/porter/docs/config.md の「ノンス構成」節である。
     */
    if ((pkt->flags & POTR_FLAG_ENCRYPTED) && (pkt->flags & (POTR_FLAG_DATA | POTR_FLAG_PING)))
    {
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t dec_len = ctx->crypto_buf_size; /* 呼び出し時は出力先の容量、復帰後は平文のバイト数 */
        uint32_t sid_nbo = cplat_hton32(pkt->session_id);
        uint16_t flags_nbo = cplat_hton16((uint16_t)pkt->flags);
        uint32_t seq_nbo = cplat_hton32(pkt->seq_num);

        memcpy(nonce, &sid_nbo, 4);
        memcpy(nonce + 4, &flags_nbo, 2);
        memcpy(nonce + 6, &seq_nbo, 4);
        memset(nonce + 10, 0, 2);

        if (cplat_crypto_decrypt(ctx->recv_crypto_buf, &dec_len, pkt->payload, pkt->payload_len,
                                 ctx->service.encrypt_key, nonce, wire_hdr, POTR_PACKET_HEADER_SIZE) != CPLAT_OK)
        {
            if (path_idx >= 0)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                           "%s[service_id=%" PRId64 " path=%d]: decrypt failed (auth) seq=%u", log_prefix,
                           ctx->service.service_id, path_idx, (unsigned)pkt->seq_num);
            }
            else
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "%s[service_id=%" PRId64 "]: decrypt failed (auth) seq=%u",
                           log_prefix, ctx->service.service_id, (unsigned)pkt->seq_num);
            }

            return POTR_ERR_PROTOCOL;
        }

        /*
         * 解析直後の payload はワイヤ上の暗号文を指す。
         * 復号した平文は認証タグを含まず、ctx の復号バッファーにあるので payload を付け替える。
         */
        pkt->payload = ctx->recv_crypto_buf;
        pkt->payload_len = (uint16_t)dec_len;
        pkt->flags = (uint16_t)(pkt->flags & ~POTR_FLAG_ENCRYPTED);
        return POTR_OK;
    }

    /*
     * DATA と PING 以外の暗号化パケットは、ペイロードが認証タグだけである。
     * 長さが違えば破棄する。
     */
    if (pkt->payload_len != POTR_CRYPTO_TAG_SIZE)
    {
        if (path_idx >= 0)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                       "%s[service_id=%" PRId64 " path=%d]: encrypted control pkt bad len=%u flags=0x%04x", log_prefix,
                       ctx->service.service_id, path_idx, (unsigned)pkt->payload_len, (unsigned)pkt->flags);
        }
        else
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                       "%s[service_id=%" PRId64 "]: encrypted control pkt bad len=%u flags=0x%04x", log_prefix,
                       ctx->service.service_id, (unsigned)pkt->payload_len, (unsigned)pkt->flags);
        }

        return POTR_ERR_PROTOCOL;
    }

    {
        /*
         * 平文長が 0 でも cplat_crypto_decrypt() は出力先に NULL を受け付けないため、dummy を渡す。
         * AAD には受信ヘッダー wire_hdr を使い、ヘッダーの改ざんを検出する。
         * 正本は app/porter/docs/config.md の「encrypt_key の詳細」節である。
         * NACK、REJECT、FIN_ACK は対象通番を ack_num に持つ。それ以外は seq_num をノンスに使う。
         * 解析済みフィールドはホスト バイト オーダーなので、詰める前にネットワーク バイト オーダーへ戻す。
         * ノンスは session_id、flags、通番、2 [byte] の 0 の順で、flags は ENCRYPTED を含んだ送信時の値である。
         * 正本は app/porter/docs/config.md の「ノンス構成」節である。
         */
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
        uint8_t dummy[1];
        size_t dummy_len = sizeof(dummy); /* 呼び出し時は出力先の容量、復帰後は平文のバイト数 */
        uint32_t val;
        uint32_t sid_nbo = cplat_hton32(pkt->session_id);
        uint16_t flags_nbo = cplat_hton16((uint16_t)pkt->flags);
        uint32_t val_nbo;

        if ((pkt->flags & (POTR_FLAG_NACK | POTR_FLAG_REJECT | POTR_FLAG_FIN_ACK)) != 0)
        {
            val = pkt->ack_num;
        }
        else
        {
            val = pkt->seq_num;
        }

        val_nbo = cplat_hton32(val);

        memcpy(nonce, &sid_nbo, 4);
        memcpy(nonce + 4, &flags_nbo, 2);
        memcpy(nonce + 6, &val_nbo, 4);
        memset(nonce + 10, 0, 2);

        if (cplat_crypto_decrypt(dummy, &dummy_len, pkt->payload, POTR_CRYPTO_TAG_SIZE, ctx->service.encrypt_key, nonce,
                                 wire_hdr, POTR_PACKET_HEADER_SIZE) != CPLAT_OK)
        {
            if (path_idx >= 0)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                           "%s[service_id=%" PRId64 " path=%d]: tag verify failed flags=0x%04x", log_prefix,
                           ctx->service.service_id, path_idx, (unsigned)pkt->flags);
            }
            else
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "%s[service_id=%" PRId64 "]: tag verify failed flags=0x%04x",
                           log_prefix, ctx->service.service_id, (unsigned)pkt->flags);
            }

            return POTR_ERR_PROTOCOL;
        }
    }

    /*
     * 制御パケットのワイヤ上のペイロードは認証タグだけであり、平文はない。
     * 検証後は payload を外す。
     */
    pkt->flags = (uint16_t)(pkt->flags & ~POTR_FLAG_ENCRYPTED);
    pkt->payload_len = 0;
    pkt->payload = NULL;

    return POTR_OK;
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
                return 1;

            for (i = 0; i < ctx->n_path; i++)
            {
                if (sender->address == ctx->dst_addr_resolved[i])
                    return 1;
            }
        }
        else
        {
            /*
             * RECEIVER が受け取るパケットは、SENDER (src_addr) から来る。
             * src_addr の先頭が空なら未指定としてすべて許可する。
             */
            if (ctx->service.src_addr[0][0] == '\0')
                return 1;

            for (i = 0; i < ctx->n_path; i++)
            {
                if (sender->address == ctx->src_addr_resolved[i])
                    return 1;
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
