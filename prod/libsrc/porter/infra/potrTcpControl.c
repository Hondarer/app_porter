/**
 *******************************************************************************
 *  @file           potrTcpControl.c
 *  @brief          TCP 制御パケットの送信ヘルパーを提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/07/07
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <string.h>
#include <com_util/base/platform.h>

#include <porter/porter_const.h>

#include <com_util/crypto/crypto.h>
#include <porter/infra/potrPlatform.h>
#include <porter/infra/potrTcpControl.h>
#include <porter/potrContext.h>
#include <porter/protocol/packet.h>

/* path 単位の送信ミューテックスを取得して全バイト送信する */
static int tcp_send_all_locked(PotrSocket fd, com_util_local_lock *mtx, const uint8_t *buf, size_t len)
{
    int result;

    com_util_local_lock_lock(mtx, COM_UTIL_SYNC_WAIT_FOREVER);
    result = potr_tcp_send(fd, buf, len);
    com_util_local_lock_unlock(mtx);

    if (result == 0)
    {
        return POTR_SUCCESS;
    }

    return POTR_ERROR;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_tcp_send_control_packet(const PotrContext *ctx, PotrPacket *pkt, uint32_t nonce_val)
{
    uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
    size_t wire_len;
    int sent_any = 0;
    int i;

    if (ctx == NULL || pkt == NULL)
    {
        return POTR_ERROR;
    }

    if (ctx->service.encrypt_enabled)
    {
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t enc_out = POTR_CRYPTO_TAG_SIZE;
        uint32_t nonce_nbo = htonl(nonce_val);

        pkt->flags |= htons(POTR_FLAG_ENCRYPTED);
        pkt->payload_len = htons((uint16_t)POTR_CRYPTO_TAG_SIZE);

        memcpy(nonce, &pkt->session_id, 4);
        memcpy(nonce + 4, &pkt->flags, 2);
        memcpy(nonce + 6, &nonce_nbo, 4);
        memset(nonce + 10, 0, 2);

        memcpy(wire_buf, pkt, PACKET_HEADER_SIZE);
        if (com_util_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out, NULL, 0, ctx->service.encrypt_key, nonce,
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
        if (tcp_send_all_locked(ctx->tcp_conn_fd[i], ctx->tcp_send_mutex[i], wire_buf, wire_len) == POTR_SUCCESS)
        {
            sent_any = 1;
        }
    }

    if (sent_any)
    {
        return POTR_SUCCESS;
    }

    return POTR_ERROR;
}
