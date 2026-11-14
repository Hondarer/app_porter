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
#include <cplat/base/platform.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>

#include <cplat/net/byteorder.h>
#include <cplat/net/socket.h>
#include <porter/infra/potr_result.h>
#include <porter/infra/potr_tcp_control.h>
#include <porter/potr_context.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/packet_seal.h>

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_tcp_send_control_packet(potr_context *ctx, const potr_packet *pkt)
{
    uint8_t wire_buf[POTR_PACKET_HEADER_SIZE + POTR_CRYPTO_TAG_SIZE];
    size_t wire_len;
    int attempted = 0;
    int sent_any = 0;
    int i;

    if (ctx == NULL || pkt == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    /* すべての有効な TCP パスに対して制御パケットの送信を試行する。 */
    for (i = 0; i < ctx->n_path; i++)
    {
        if (ctx->tcp_conn_fd[i] == CPLAT_INVALID_SOCKET)
        {
            continue;
        }

        attempted = 1;

        cplat_local_lock_lock(ctx->tcp_send_mutex[i], CPLAT_SYNC_WAIT_FOREVER);
        wire_len = sizeof(wire_buf);
        int ret = potr_internal_packet_seal_for_context(ctx, &ctx->tx_session, pkt, wire_buf, &wire_len);
        if (ret == POTR_OK)
        {
            if (cplat_socket_send_all(ctx->tcp_conn_fd[i], wire_buf, wire_len, NULL) == CPLAT_OK)
            {
                sent_any = 1;
            }
        }
        cplat_local_lock_unlock(ctx->tcp_send_mutex[i]);
        if (ret != POTR_OK)
        {
            return ret;
        }
    }

    /* 1 本以上のパスで送信成功した場合は POTR_OK、有効なパスがなければ切断、全パス失敗なら I/O エラーとする。 */
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
