/** @file thread_recv_validate.c
 * @brief 受信パケットの暗号認証と UDP 送信元の照合を行います。
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

    if ((pkt->flags & POTR_FLAG_ENCRYPTED) && (pkt->flags & (POTR_FLAG_DATA | POTR_FLAG_PING)))
    {
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t dec_len = ctx->crypto_buf_size;
        uint32_t sid_nbo = cplat_hton32(pkt->session_id);
        uint16_t flags_nbo = cplat_hton16((uint16_t)pkt->flags);
        uint32_t seq_nbo = cplat_hton32(pkt->seq_num);

        memcpy(nonce, &sid_nbo, 4);
        memcpy(nonce + 4, &flags_nbo, 2);
        memcpy(nonce + 6, &seq_nbo, 4);
        memset(nonce + 10, 0, 2);

        if (cplat_decrypt(ctx->crypto_buf, &dec_len, pkt->payload, pkt->payload_len, ctx->service.encrypt_key, nonce,
                          wire_hdr, PACKET_HEADER_SIZE) != CPLAT_OK)
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

        pkt->payload = ctx->crypto_buf;
        pkt->payload_len = (uint16_t)dec_len;
        pkt->flags = (uint16_t)(pkt->flags & ~POTR_FLAG_ENCRYPTED);
        return POTR_OK;
    }

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
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
        uint8_t dummy[1];
        size_t dummy_len = sizeof(dummy);
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

        if (cplat_decrypt(dummy, &dummy_len, pkt->payload, POTR_CRYPTO_TAG_SIZE, ctx->service.encrypt_key, nonce,
                          wire_hdr, PACKET_HEADER_SIZE) != CPLAT_OK)
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

    pkt->flags = (uint16_t)(pkt->flags & ~POTR_FLAG_ENCRYPTED);
    pkt->payload_len = 0;
    pkt->payload = NULL;
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_check_src_addr(const potr_context *ctx, const cplat_ipv4_endpoint *sender)
{
    int i;

    /* N:1 モード: src_port 指定時はポートのみでフィルタリング、未指定時は全許可 */
    if (ctx->is_multi_peer)
    {
        if (ctx->service.src_port != 0)
        {
            if (cplat_ntoh16(sender->port) == ctx->service.src_port)
            {
                return 1;
            }
            return 0;
        }
        return 1;
    }

    if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR)
    {
        if (ctx->role == POTR_ROLE_SENDER)
        {
            /* SENDER が受け取るパケット: RECEIVER (dst_addr) から来る */
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
            /* RECEIVER が受け取るパケット: SENDER (src_addr) から来る */
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
