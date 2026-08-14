/**
 *******************************************************************************
 *  @file           potr_tcp_control.c
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

#include <porter/porter_result.h>
#include <porter/porter_const.h>

#include <com_util/crypto/crypto.h>
#include <com_util/net/byteorder.h>
#include <com_util/net/socket.h>
#include <porter/infra/potr_result.h>
#include <porter/infra/potr_tcp_control.h>
#include <porter/potr_context.h>
#include <porter/protocol/packet.h>

/* path 単位の送信ミューテックスを取得して全バイト送信する */
static int tcp_send_all_locked(com_util_socket fd, com_util_local_lock *mtx, const uint8_t *buf, size_t len)
{
    int result;
    com_util_error detail;

    com_util_local_lock_lock(mtx, COM_UTIL_SYNC_WAIT_FOREVER);
    result = com_util_socket_send_all(fd, buf, len, &detail);
    com_util_local_lock_unlock(mtx);

    if (result != COM_UTIL_OK)
    {
        return potr_internal_result_from_error(&detail);
    }
    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_tcp_send_control_packet(const potr_context *ctx, potr_packet *pkt, uint32_t nonce_val)
{
    uint8_t wire_buf[PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
    size_t wire_len;
    int attempted = 0;
    int sent_any = 0;
    int i;

    if (ctx == NULL || pkt == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    if (ctx->service.encrypt_enabled)
    {
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t enc_out = POTR_CRYPTO_TAG_SIZE;
        uint32_t nonce_nbo = com_util_hton32(nonce_val);

        pkt->flags |= com_util_hton16(POTR_FLAG_ENCRYPTED);
        pkt->payload_len = com_util_hton16((uint16_t)POTR_CRYPTO_TAG_SIZE);

        memcpy(nonce, &pkt->session_id, 4);
        memcpy(nonce + 4, &pkt->flags, 2);
        memcpy(nonce + 6, &nonce_nbo, 4);
        memset(nonce + 10, 0, 2);

        memcpy(wire_buf, pkt, PACKET_HEADER_SIZE);
        if (com_util_encrypt(wire_buf + PACKET_HEADER_SIZE, &enc_out, NULL, 0, ctx->service.encrypt_key, nonce,
                             wire_buf, PACKET_HEADER_SIZE) != COM_UTIL_OK)
        {
            /* 暗号化失敗は入力データ起因と断定できないため、分類不能として扱う。 */
            return POTR_ERR_UNKNOWN;
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
        if (ctx->tcp_conn_fd[i] == COM_UTIL_INVALID_SOCKET)
        {
            continue;
        }
        attempted = 1;
        if (tcp_send_all_locked(ctx->tcp_conn_fd[i], ctx->tcp_send_mutex[i], wire_buf, wire_len) == POTR_OK)
        {
            sent_any = 1;
        }
    }

    if (sent_any)
    {
        return POTR_OK;
    }

    if (!attempted)
    {
        return POTR_ERR_DISCONNECTED;
    }

    return POTR_ERR_IO;
}
