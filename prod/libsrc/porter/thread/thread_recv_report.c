/**
 *******************************************************************************
 *  @file           thread_recv_report.c
 *  @brief          片方向 UDP 受信者の受信状態を周期的に報告します。
 *
 *******************************************************************************
 */
#include <cplat/crypto/random.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/socket.h>
#include <porter/porter_result.h>
#include <porter/protocol/packet.h>
#include "thread_recv_report.h"

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_report_interval(const potr_context *ctx, uint64_t *interval_ms)
{
    *interval_ms = ctx->health_interval_ms;
    if (ctx->service.type == POTR_TYPE_MULTICAST || ctx->service.type == POTR_TYPE_BROADCAST)
    {
        uint32_t random_value;
        uint64_t range = (uint64_t)ctx->health_interval_ms / 2U + 1U;

        if (cplat_random_bytes(&random_value, sizeof(random_value)) != CPLAT_OK)
        {
            return POTR_ERR_IO;
        }
        *interval_ms += (uint64_t)random_value % range;
    }
    return POTR_OK;
}

/* 受信報告パケットを組み立て、全経路へ送出する。 */
static int send_report(potr_context *ctx)
{
    potr_packet packet;
    potr_internal_packet_session_hdr shdr;
    uint8_t states[POTR_MAX_PATH];
    int ret;
    int i;

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = ctx->session_id;
    session_ts_to_hdr(&ctx->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);
    copy_path_ping_state(states, ctx->path_ping_state, POTR_MAX_PATH);
    ret = potr_internal_packet_build_ping(&packet, &shdr, 0U, states, (uint16_t)POTR_MAX_PATH);
    if (ret != POTR_OK)
    {
        return ret;
    }
    packet.flags = cplat_hton16(POTR_FLAG_REPORT);

    /*
     * unicast、multicast、broadcast の受信者は、受信報告のヘッダーに、その時点の自身の送出上限を載せる。
     * seq_num は有無 (1 = ack_num に送出上限がある)、ack_num は値である。パケットの数と大きさは変わらない。
     */
    {
        uint32_t send_limit = 0U;

        if (potr_internal_flow_control_report_limit(ctx, &send_limit) != 0)
        {
            packet.seq_num = cplat_hton32(1U);
            packet.ack_num = cplat_hton32(send_limit);
        }
    }

    for (i = 0; i < ctx->n_path; i++)
    {
        uint8_t wire[POTR_PACKET_HEADER_SIZE + POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE];
        size_t wire_len = sizeof(wire);
        size_t sent;
        cplat_ipv4_endpoint destination = {0};

        destination.address = ctx->src_addr_resolved[i];
        if (destination.address == 0U)
        {
            destination.address = ctx->peer_address[i];
        }
        destination.port = cplat_hton16(ctx->service.src_port);
        if (ctx->service.src_port == 0U)
        {
            destination.port = ctx->peer_port[i];
        }
        if (ctx->sock[i] == CPLAT_INVALID_SOCKET || destination.port == 0U || destination.address == 0U)
        {
            continue;
        }
        ret = potr_internal_packet_seal_for_context(ctx, &ctx->tx_session, &packet, wire, &wire_len);
        if (ret == POTR_ERR_DISCONNECTED && ctx->service.auth == POTR_AUTH_CERT)
        {
            continue;
        }
        if (ret != POTR_OK)
        {
            return ret;
        }
        /* REPORT は次の周期に送り直すため、戻り値は明示的に破棄する。 */
        (void)cplat_socket_sendto(ctx->sock[i], wire, wire_len, &destination, &sent, NULL);
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_report_tick(potr_context *ctx, uint64_t now_ms)
{
    uint64_t interval_ms;
    int ret;

    if (ctx->role != POTR_ROLE_RECEIVER || !is_oneway_udp_type(ctx->service.type) ||
        is_raw_type(ctx->service.type) || ctx->health_interval_ms == 0U)
    {
        return POTR_OK;
    }
    if (ctx->next_report_ms != 0U && now_ms < ctx->next_report_ms)
    {
        return POTR_OK;
    }
    ret = thread_recv_report_interval(ctx, &interval_ms);
    if (ret != POTR_OK)
    {
        return ret;
    }
    if (ctx->next_report_ms == 0U)
    {
        ctx->next_report_ms = now_ms + interval_ms;
        return POTR_OK;
    }
    ctx->next_report_ms = now_ms + interval_ms;

    return send_report(ctx);
}

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_report_on_adopted(potr_context *ctx)
{
    ctx->recv_report_consecutive_data = 0U;

    if (!thread_recv_report_tracks_lag(ctx) || ctx->flow_monitored != 0)
    {
        return POTR_OK;
    }

    /* 採用時の 1 回は間隔の制限の対象外とし、last_ad_hoc_report_ms と next_report_ms を変えない。 */
    return send_report(ctx);
}

/* Doxygen コメントは、ヘッダーに記載 */

int thread_recv_report_on_data_accepted(potr_context *ctx, uint64_t now_ms)
{
    uint32_t threshold;
    uint64_t interval_ms;
    int ret;

    if (!thread_recv_report_tracks_lag(ctx))
    {
        return POTR_OK;
    }

    ctx->recv_report_accepted_data++;

    /* 監視対象の受信者、および OS の受信バッファーの幅がない構成では、遅れの検出と臨時の送出を行わない。 */
    if (ctx->flow_monitored != 0 || ctx->flow_receive_buffer_width < 2U)
    {
        return POTR_OK;
    }

    ctx->recv_report_consecutive_data++;
    threshold = ctx->flow_receive_buffer_width / 2U;

    if (ctx->recv_report_consecutive_data < threshold)
    {
        return POTR_OK;
    }

    ctx->recv_report_consecutive_data = 0U;
    ctx->recv_report_lag_detected++;

    interval_ms = ctx->flow_control_config.request_interval_ms;
    if (interval_ms == 0U)
    {
        interval_ms = (uint64_t)POTR_DEFAULT_FLOW_CONTROL_REQUEST_INTERVAL_MS;
    }

    if (ctx->has_ad_hoc_report != 0 && now_ms < ctx->last_ad_hoc_report_ms + interval_ms)
    {
        return POTR_OK;
    }

    ret = send_report(ctx);
    if (ret != POTR_OK)
    {
        return ret;
    }

    ctx->has_ad_hoc_report = 1;
    ctx->last_ad_hoc_report_ms = now_ms;
    ctx->recv_report_lag_sent++;

    return POTR_OK;
}
