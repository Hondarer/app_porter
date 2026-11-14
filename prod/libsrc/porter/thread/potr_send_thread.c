/**
 *******************************************************************************
 *  @file           potr_send_thread.c
 *  @brief          データを非同期送信するスレッドを実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/08
 *  @version        1.0.0
 *
 *  送信キュー (potr_internal_send_queue) からペイロード エレメントを取り出して
 *  外側パケット (POTR_FLAG_DATA) を構築し sendto を呼び出す送信スレッド。\n
 *  potr_service_open (POTR_ROLE_SENDER) 時に起動し、potr_service_close 時に停止します。\n
 *  potr_service_send の flags 引数の値によらず常に起動しており、
 *  非ブロッキング送信 (POTR_SEND_BLOCKING なし) 時のみキューが使用されます。
 *
 *  @par            通番管理
 *  すべてのデータ パケットはパック コンテナー形式で送受信します。\n
 *  再送・順序整列の単位は外側パケット (UDP datagram) であり、\n
 *  通番は本スレッドが外側パケットを構築する際に付与します。\n
 *  送信ウィンドウ (ctx->send_window) への登録も本スレッドが行います。
 *
 *  @par            パッキング機能
 *  POTR_MAX_PAYLOAD - POTR_PAYLOAD_ELEM_HDR_SIZE 以下のフラグメントが
 *  複数キューに滞留している場合、送信スレッドが 1 つの外側パケットに
 *  まとめて送信します。\n
 *  以下の場合は単体 (ペイロード エレメント 1 件) のコンテナーとして送信します。
 *  - MORE_FRAG フラグが付いているエントリ (フラグメント化メッセージの途中)
 *  - キューに追加エントリが存在しない場合
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/base/platform.h>

#include <string.h>
#include <inttypes.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>

#include <porter/potr_context.h>
#include <porter/potr_peer_table.h>
#include <porter/infra/potr_send_queue.h>
#include <porter/thread/potr_send_thread.h>
#include <porter/thread/potr_health_thread.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/packet_seal.h>
#include <porter/protocol/window.h>
#include <porter/infra/potr_trace.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/socket.h>

#include "thread_recv_session.h"

/*
 * 有効データの送信時刻を記録するかを判定する。
 * 1:1 かつ片方向 UDP の場合は 1、それ以外は 0 を返す。
 */
static int should_track_valid_data_send_time(const potr_context *ctx)
{
    return ctx != NULL && !ctx->is_multi_peer && is_oneway_udp_type(ctx->service.type);
}

/* ペイロード エレメントを packed_buf に追記する。 */
static void append_payload_elem(uint8_t *packed_buf, size_t *packed_len, const potr_internal_payload_elem *entry)
{
    uint16_t flags_nbo = cplat_hton16(entry->flags);
    uint32_t plen_nbo = cplat_hton32((uint32_t)entry->payload_len);

    memcpy(packed_buf + *packed_len, &flags_nbo, 2);
    *packed_len += 2;
    memcpy(packed_buf + *packed_len, &plen_nbo, 4);
    *packed_len += 4;
    memcpy(packed_buf + *packed_len, entry->payload, entry->payload_len);
    *packed_len += entry->payload_len;
}

/*
 * TCP の指定 path へパケットを封緘して書き込む。
 * 相手端は CONNECTED になる前に受信した DATA を破棄するため、未通知の経路状態があれば先に PING を書き込む。
 * 書き込めなかった path はストリームの途中が欠けるため、ソケットを停止して受信スレッドに切断を検知させる。
 * 成功時は POTR_OK、path が未接続または書き込みに失敗した場合は POTR_ERR_DISCONNECTED を返す。
 * 呼び出し元は send_window_mutex を保持する。
 */
static int tcp_write_path(potr_context *ctx, int path_idx, const potr_packet *packet)
{
    int result = POTR_ERR_DISCONNECTED;
    size_t wire_len = ctx->crypto_buf_size;

    cplat_local_lock_lock(ctx->tcp_send_mutex[path_idx], CPLAT_SYNC_WAIT_FOREVER);

    if (ctx->tcp_conn_fd[path_idx] != CPLAT_INVALID_SOCKET)
    {
        cplat_error detail;
        int announce_result = potr_internal_tcp_announce_ping_state_locked(ctx, path_idx);

        if (announce_result != POTR_OK)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "send_thread[service_id=%" PRId64
                       "]: path[%d] PING before DATA failed (result=%d), disconnecting",
                       ctx->service.service_id, path_idx, announce_result);
            cplat_socket_shutdown(ctx->tcp_conn_fd[path_idx]);
        }
        else if (potr_internal_packet_seal_for_context(ctx, &ctx->tx_session, packet, ctx->crypto_buf, &wire_len) != POTR_OK)
        {
            cplat_socket_shutdown(ctx->tcp_conn_fd[path_idx]);
        }
        else if (cplat_socket_send_all(ctx->tcp_conn_fd[path_idx], ctx->crypto_buf, wire_len, &detail) == CPLAT_OK)
        {
            result = POTR_OK;
        }
        else
        {
            POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_ERROR, &detail,
                                      "send_thread[service_id=%" PRId64 "]: path[%d] send failed, disconnecting",
                                      ctx->service.service_id, path_idx);
            cplat_socket_shutdown(ctx->tcp_conn_fd[path_idx]);
        }
    }

    cplat_local_lock_unlock(ctx->tcp_send_mutex[path_idx]);

    return result;
}

/*
 * TCP の指定された経路集合へ同じパケットを書き込む。
 * 送信バッファーに空きがない path は、他の path が書き込めた場合に限り今回のパケットを飛ばす。
 * TCP 受信側は欠けた通番を補えないため、どの path にも書き込めないままパケットを捨ててはならない。
 * いずれかの path に書き込めた場合は POTR_OK、どの path にも書き込めない場合は POTR_ERR_DISCONNECTED を返す。
 * 呼び出し元は send_window_mutex を保持する。
 */
static int tcp_write_all_paths(potr_context *ctx, const potr_packet *packet, uint8_t paths)
{
    int skipped[POTR_MAX_PATH];
    int sent_any = 0;
    int i;

    if (cplat_atomic_load_i32(&ctx->tcp_active_paths, CPLAT_MEMORY_ORDER_ACQUIRE) <= 0)
    {
        return POTR_ERR_DISCONNECTED;
    }

    for (i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        skipped[i] = 0;
    }

    for (i = 0; i < ctx->n_path; i++)
    {
        int writable = 0;

        if ((paths & (uint8_t)(1U << i)) == 0U || ctx->tcp_conn_fd[i] == CPLAT_INVALID_SOCKET)
        {
            continue;
        }

        /* 送信バッファーの空きを確認する (非ブロッキング)。確認できない場合は書き込みの結果で判定する。 */
        if (cplat_socket_wait_writable(ctx->tcp_conn_fd[i], CPLAT_SOCKET_NO_WAIT, &writable, NULL) != CPLAT_OK)
        {
            writable = 1;
        }

        if (writable)
        {
            if (ctx->buf_full_suppress_cnt[i] > 0)
            {
                ++ctx->buf_full_suppress_cnt[i];
                if (ctx->buf_full_suppress_cnt[i] > 10)
                {
                    ctx->buf_full_suppress_cnt[i] = 0;
                }
            }

            if (tcp_write_path(ctx, i, packet) == POTR_OK)
            {
                sent_any = 1;
            }
        }
        else
        {
            skipped[i] = 1;
        }
    }

    for (i = 0; i < ctx->n_path; i++)
    {
        if (!skipped[i])
        {
            continue;
        }

        if (!sent_any)
        {
            /* どの path にも書き込めていないため、空きを待って書き込む。 */
            if (tcp_write_path(ctx, i, packet) == POTR_OK)
            {
                sent_any = 1;
            }
        }
        else if (ctx->buf_full_suppress_cnt[i] == 0)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                       "send_thread[service_id=%" PRId64 "]: path[%d]"
                       " send buffer full, packet sent on other path only",
                       ctx->service.service_id, i);
            ctx->buf_full_suppress_cnt[i] = 1;
        }
    }

    if (sent_any)
    {
        return POTR_OK;
    }

    return POTR_ERR_DISCONNECTED;
}

/*
 * send_wire_buf の [POTR_PACKET_HEADER_SIZE .. POTR_PACKET_HEADER_SIZE+packed_len-1] に
 * 詰め済みのペイロードから外側コンテナーを構築して送信する。
 * seq_num を付与し、送信済み DATA を send_window に登録する。
 * send_wire_buf は平文ペイロード、crypto_buf は送出ごとの封緘先として使用する。
 * 成功時は POTR_OK を返す。パケットを構築できない場合と、TCP でどの path にも書き込めない場合は
 * エラー コードを返す。UDP の送信失敗は再送で回復するため、成功として扱う。
 */
static int flush_packed(potr_context *ctx, size_t packed_len)
{
    potr_packet outer_pkt;
    potr_internal_packet_session_hdr shdr;
    uint32_t seq;
    size_t wire_len;
    uint8_t *packed_buf = ctx->send_wire_buf + POTR_PACKET_HEADER_SIZE;
    int is_tcp = is_tcp_type(ctx->service.type);
    int build_result;
    int result = POTR_OK;
    potr_internal_path_group_result selection;

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = ctx->session_id;
    session_ts_to_hdr(&ctx->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);

    /* send_window へのアクセスを排他制御する (送信スレッド・ヘルス チェック スレッド・受信スレッドが競合)。 */
    cplat_local_lock_lock(ctx->send_window_mutex, CPLAT_SYNC_WAIT_FOREVER);

    result = potr_internal_path_group_select(ctx, NULL, &selection);
    if (result != POTR_OK || selection.disconnected)
    {
        cplat_local_lock_unlock(ctx->send_window_mutex);
        if (result != POTR_OK)
        {
            return result;
        }
        return POTR_ERR_DISCONNECTED;
    }

    seq = ctx->send_window.next_seq;

    build_result = potr_internal_packet_build_packed(&outer_pkt, &shdr, seq, packed_buf, packed_len);
    if (build_result != POTR_OK)
    {
        cplat_local_lock_unlock(ctx->send_window_mutex);
        return build_result;
    }

    if (is_tcp)
    {
        if (selection.tcp_resend)
        {
            uint32_t retained = ctx->send_window.next_seq - ctx->send_window.base_seq;
            uint32_t offset;

            /* 通番が周回しても、基底からの距離の順に送り直します。新規 DATA はこの後に書き込みます。 */
            for (offset = 0U; offset < retained; offset++)
            {
                potr_packet retained_packet;

                result = potr_internal_window_send_get(&ctx->send_window, ctx->send_window.base_seq + offset,
                                                        &retained_packet);
                if (result == POTR_OK)
                {
                    result = tcp_write_all_paths(ctx, &retained_packet, selection.selected_paths);
                }
                if (result != POTR_OK)
                {
                    cplat_local_lock_unlock(ctx->send_window_mutex);
                    return result;
                }
            }
            cplat_local_lock_lock(ctx->callback_mutex, CPLAT_SYNC_WAIT_FOREVER);
            if (ctx->path_group_state.resend_group == selection.next_group)
            {
                ctx->path_group_state.resend_group = 0U;
            }
            cplat_local_lock_unlock(ctx->callback_mutex);
        }
        result = tcp_write_all_paths(ctx, &outer_pkt, selection.selected_paths);
        if (result == POTR_OK && selection.data_paths != selection.selected_paths)
        {
            (void)tcp_write_all_paths(ctx, &outer_pkt, (uint8_t)(selection.data_paths & ~selection.selected_paths));
        }
        if (result == POTR_OK)
        {
            potr_internal_window_send_push(&ctx->send_window, &outer_pkt);
            ctx->send_has_data = 1;
        }

        cplat_local_lock_unlock(ctx->send_window_mutex);
    }
    else
    {
        int i;
        int sent_any = 0;
        int window_pending = 1;

        for (i = 0; i < ctx->n_path; i++)
        {
            size_t sent = 0;
            int send_ret;

            if ((selection.data_paths & (uint8_t)(1U << i)) == 0U)
            {
                continue;
            }
            wire_len = ctx->crypto_buf_size;
            result = potr_internal_packet_seal_for_context(ctx, &ctx->tx_session, &outer_pkt, ctx->crypto_buf, &wire_len);
            if (result != POTR_OK)
            {
                cplat_local_lock_unlock(ctx->send_window_mutex);
                return result;
            }
            send_ret = cplat_socket_sendto(ctx->sock[i], ctx->crypto_buf, wire_len, &ctx->dest_addr[i], &sent, NULL);
            if (window_pending)
            {
                /* 封緘に成功した平文を保持する。封緘と送出の間にはウィンドウ操作を挟まない。 */
                potr_internal_window_send_push(&ctx->send_window, &outer_pkt);
                ctx->send_has_data = 1;
                window_pending = 0;
            }
            if (send_ret == CPLAT_OK)
            {
                sent_any = 1;
                if (should_track_valid_data_send_time(ctx))
                {
                    uint64_t path_sent_ms;

                    if (cplat_clock_get_monotonic_ms(&path_sent_ms) == CPLAT_OK)
                    {
                        cplat_atomic_store_u64(&ctx->last_valid_data_send_ms[i], path_sent_ms,
                                               CPLAT_MEMORY_ORDER_RELAXED);
                    }
                    else
                    {
                        cplat_local_lock_unlock(ctx->send_window_mutex);
                        cplat_atomic_store_i32(&ctx->fatal_error, POTR_ERR_IO, CPLAT_MEMORY_ORDER_RELEASE);
                        potr_internal_send_queue_abort(&ctx->send_queue, POTR_ERR_IO);
                        return POTR_ERR_IO;
                    }
                }
            }
        }

        cplat_local_lock_unlock(ctx->send_window_mutex);
        if (sent_any && should_track_valid_data_send_time(ctx))
        {
            potr_internal_health_thread_wake(ctx);
        }
    }

    return result;
}

/*
 * N:1 モード専用: ピアの send_window を使ってパック コンテナーを構築して sendto する。
 * 呼び出し元は peers_mutex を保持し、peer が解放されないことを保証する。
 */
static void flush_packed_peer(potr_context *ctx, potr_internal_peer_context *peer, size_t packed_len)
{
    potr_packet outer_pkt;
    potr_internal_packet_session_hdr shdr;
    uint32_t seq;
    size_t wire_len;
    int window_pending = 1;
    potr_internal_path_group_result selection;
    uint8_t *packed_buf = ctx->send_wire_buf + POTR_PACKET_HEADER_SIZE;

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = peer->session_id;
    session_ts_to_hdr(&peer->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);

    cplat_local_lock_lock(peer->send_window_mutex, CPLAT_SYNC_WAIT_FOREVER);

    if (potr_internal_path_group_select(ctx, peer, &selection) != POTR_OK || selection.disconnected)
    {
        cplat_local_lock_unlock(peer->send_window_mutex);
        return;
    }
    seq = peer->send_window.next_seq;

    if (potr_internal_packet_build_packed(&outer_pkt, &shdr, seq, packed_buf, packed_len) != POTR_OK)
    {
        cplat_local_lock_unlock(peer->send_window_mutex);
        return;
    }

    /* N:1 はソケットのインデックスに対応する選択グループの経路へ送信する。 */
    {
        int k;

        for (k = 0; k < (int)POTR_MAX_PATH; k++)
        {
            size_t sent = 0;

            if ((selection.data_paths & (uint8_t)(1U << k)) == 0U || endpoint_is_unset(&peer->dest_addr[k]))
            {
                continue;
            }
            wire_len = ctx->crypto_buf_size;
            if (potr_internal_packet_seal_for_context(ctx, &peer->tx_session, &outer_pkt, ctx->crypto_buf, &wire_len) != POTR_OK)
            {
                if (window_pending)
                {
                    cplat_local_lock_unlock(peer->send_window_mutex);
                }
                return;
            }
            (void)cplat_socket_sendto(ctx->sock[k], ctx->crypto_buf, wire_len, &peer->dest_addr[k], &sent, NULL);
            if (window_pending)
            {
                potr_internal_window_send_push(&peer->send_window, &outer_pkt);
                peer->send_has_data = 1;
                cplat_local_lock_unlock(peer->send_window_mutex);
                window_pending = 0;
            }
        }
    }
    if (window_pending)
    {
        cplat_local_lock_unlock(peer->send_window_mutex);
    }
}

/* N:1 モード専用: キューからエントリを取り出してピアへパッキング送信する。 */
static void send_packed_peer_mode(potr_context *ctx, potr_internal_payload_elem *first)
{
    potr_peer_id target_peer_id = first->peer_id;
    potr_internal_peer_context *peer = NULL;
    uint8_t *packed_buf = ctx->send_wire_buf + POTR_PACKET_HEADER_SIZE;
    size_t packed_len = 0;
    int n_dequeued = 1;
    int peer_exists;

    /*
     * 宛先の存在だけを先に判定する。ここで得たピアのポインターは、ロック解放後に使わない。
     * 受信スレッド、ヘルス チェック スレッド、potr_peer_disconnect がピアを解放すると、
     * 同じスロットのウィンドウと mutex は破棄または別ピアへ再利用されるためである。
     */
    cplat_local_lock_lock(ctx->peers_mutex, CPLAT_SYNC_WAIT_FOREVER);
    peer_exists = (potr_internal_peer_find_by_id(ctx, target_peer_id) != NULL);
    cplat_local_lock_unlock(ctx->peers_mutex);

    if (!peer_exists)
    {
        /* 切断済みピア宛エントリ: 破棄 */
        potr_internal_send_queue_complete(&ctx->send_queue);
        return;
    }

    append_payload_elem(packed_buf, &packed_len, first);

    /* 同一ピア宛の追加エントリを即時パッキングする (pack_wait なし)。 */
    if (!(first->flags & POTR_FLAG_MORE_FRAG))
    {
        potr_internal_payload_elem next;

        while (potr_internal_send_queue_peek(&ctx->send_queue, &next) == POTR_OK)
        {
            size_t elem_size;
            size_t crypto_tag_overhead;

            if (next.peer_id != target_peer_id)
            {
                break;
            }
            if (next.flags & POTR_FLAG_MORE_FRAG)
            {
                break;
            }

            elem_size = POTR_PAYLOAD_ELEM_HDR_SIZE + (size_t)next.payload_len;

            if (ctx->service.encrypt_enabled)
            {
                crypto_tag_overhead = POTR_CRYPTO_TAG_SIZE;
            }
            else
            {
                crypto_tag_overhead = 0U;
            }

            if (packed_len + elem_size > (size_t)ctx->global.max_payload - crypto_tag_overhead)
            {
                break;
            }

            if (potr_internal_send_queue_try_pop(&ctx->send_queue, &next) != POTR_OK)
            {
                break;
            }

            append_payload_elem(packed_buf, &packed_len, &next);
            n_dequeued++;
        }
    }

    /*
     * ピアを ID で再検索し、sendto の完了まで peers_mutex を保持する。
     * ヘルス チェック スレッドの N:1 PING 送信と同じく、ロック順は
     * peers_mutex -> peer->send_window_mutex とする。
     */
    cplat_local_lock_lock(ctx->peers_mutex, CPLAT_SYNC_WAIT_FOREVER);
    peer = potr_internal_peer_find_by_id(ctx, target_peer_id);
    if (peer != NULL)
    {
        flush_packed_peer(ctx, peer, packed_len);
        if (cplat_atomic_load_i32(&peer->tx_session.exhausted, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
        {
            thread_recv_disconnect_peer_all_paths(ctx, peer);
            potr_internal_peer_free(ctx, peer);
        }
    }
    else
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                   "sender[service_id=%" PRId64 "]: peer=%u disconnected before flush, %d elem(s) dropped",
                   ctx->service.service_id, (unsigned)target_peer_id, n_dequeued);
    }

    cplat_local_lock_unlock(ctx->peers_mutex);

    {
        int i;

        for (i = 0; i < n_dequeued; i++)
        {
            potr_internal_send_queue_complete(&ctx->send_queue);
        }
    }
}

/* 送信スレッド本体 */
static void send_thread_func(void *arg)
{
    potr_context *ctx = (potr_context *)arg;
    potr_internal_payload_elem first;

    while (cplat_atomic_load_i32(&ctx->fatal_error, CPLAT_MEMORY_ORDER_ACQUIRE) == POTR_OK)
    {
        /* キューからエントリを取り出す (ブロッキング)。 */
        if (potr_internal_send_queue_pop(&ctx->send_queue, &first, &ctx->send_thread_running) != POTR_OK)
        {
            break;
        }

        /* N:1 モード: peer_id でルーティングして送信する。 */
        if (ctx->is_multi_peer)
        {
            send_packed_peer_mode(ctx, &first);
            continue;
        }

        /* パッキングを試行する。 */
        {
            /* packed_buf は send_wire_buf のヘッダー直後領域を直接使用する (ゼロ コピー)。 */
            uint8_t *packed_buf = ctx->send_wire_buf + POTR_PACKET_HEADER_SIZE;
            size_t packed_len = 0;
            int n_dequeued = 1;
            int flush_result = POTR_OK;

            append_payload_elem(packed_buf, &packed_len, &first);

            /* MORE_FRAG エントリはパッキング不可のため、そのまま単体コンテナーとして送信する。 */
            if (!(first.flags & POTR_FLAG_MORE_FRAG))
            {
                uint32_t pack_wait_ms = ctx->service.pack_wait_ms;

                if (pack_wait_ms > 0)
                {
                    /* パッキング待ちあり: タイムアウトまで追加エントリを待ち合わせる。 */
                    uint64_t deadline;

                    if (cplat_clock_get_monotonic_ms(&deadline) != CPLAT_OK)
                    {
                        cplat_atomic_store_i32(&ctx->fatal_error, POTR_ERR_IO, CPLAT_MEMORY_ORDER_RELEASE);
                        potr_internal_send_queue_abort(&ctx->send_queue, POTR_ERR_IO);
                        goto complete_packed;
                    }

                    deadline += pack_wait_ms;
                    potr_internal_payload_elem next;

                    for (;;)
                    {
                        uint64_t now;

                        if (cplat_clock_get_monotonic_ms(&now) != CPLAT_OK)
                        {
                            cplat_atomic_store_i32(&ctx->fatal_error, POTR_ERR_IO, CPLAT_MEMORY_ORDER_RELEASE);
                            potr_internal_send_queue_abort(&ctx->send_queue, POTR_ERR_IO);
                            goto complete_packed;
                        }

                        uint32_t remaining;
                        size_t elem_size;
                        size_t crypto_tag_overhead;

                        if (now >= deadline)
                        {
                            break; /* タイムアウト */
                        }

                        remaining = (uint32_t)(deadline - now);

                        /* remaining は deadline - now [ms] であり pack_wait_ms 以下で INT_MAX に収まる。 */
                        if (potr_internal_send_queue_peek_timed(&ctx->send_queue, &next, (int)remaining) != POTR_OK)
                        {
                            break; /* タイムアウト (エントリなし) */
                        }

                        if (next.flags & POTR_FLAG_MORE_FRAG)
                        {
                            break; /* MORE_FRAG はパッキング不可 */
                        }

                        elem_size = POTR_PAYLOAD_ELEM_HDR_SIZE + (size_t)next.payload_len;

                        if (ctx->service.encrypt_enabled)
                        {
                            crypto_tag_overhead = POTR_CRYPTO_TAG_SIZE;
                        }
                        else
                        {
                            crypto_tag_overhead = 0U;
                        }

                        if (packed_len + elem_size > (size_t)ctx->global.max_payload - crypto_tag_overhead)
                        {
                            break; /* 容量満杯: 即時送信してタイマー リセット */
                        }

                        if (potr_internal_send_queue_try_pop(&ctx->send_queue, &next) != POTR_OK)
                        {
                            break; /* 競合防止 (通常発生しない) */
                        }

                        append_payload_elem(packed_buf, &packed_len, &next);
                        n_dequeued++;
                    }
                }
                else
                {
                    /* パッキング待ちなし: キューにあるエントリを即時まとめる。 */
                    potr_internal_payload_elem next;

                    while (potr_internal_send_queue_peek(&ctx->send_queue, &next) == POTR_OK)
                    {
                        size_t elem_size;
                        size_t crypto_tag_overhead;

                        if (next.flags & POTR_FLAG_MORE_FRAG)
                        {
                            break;
                        }

                        elem_size = POTR_PAYLOAD_ELEM_HDR_SIZE + (size_t)next.payload_len;

                        if (ctx->service.encrypt_enabled)
                        {
                            crypto_tag_overhead = POTR_CRYPTO_TAG_SIZE;
                        }
                        else
                        {
                            crypto_tag_overhead = 0U;
                        }

                        if (packed_len + elem_size > (size_t)ctx->global.max_payload - crypto_tag_overhead)
                        {
                            break;
                        }

                        if (potr_internal_send_queue_try_pop(&ctx->send_queue, &next) != POTR_OK)
                        {
                            break;
                        }

                        append_payload_elem(packed_buf, &packed_len, &next);
                        n_dequeued++;
                    }
                }
            }

            /* 外側パケットを構築して送信する。 */
            flush_result = flush_packed(ctx, packed_len);

        complete_packed:
            /* デキューした全エントリ分の inflight を減算する。送信できなかった場合は失敗を記録する。 */
            {
                int i;

                for (i = 0; i < n_dequeued; i++)
                {
                    if (flush_result == POTR_OK)
                    {
                        potr_internal_send_queue_complete(&ctx->send_queue);
                    }
                    else
                    {
                        potr_internal_send_queue_complete_failed(&ctx->send_queue, flush_result);
                    }
                }
            }
        }
    }

    cplat_atomic_store_i32(&ctx->send_thread_running, 0, CPLAT_MEMORY_ORDER_RELEASE);

    return;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_send_thread_start(potr_context *ctx)
{
    if (ctx == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    cplat_atomic_store_i32(&ctx->send_thread_running, 1, CPLAT_MEMORY_ORDER_RELEASE);

    if (cplat_local_lock_create(&ctx->send_window_mutex) != CPLAT_OK)
    {
        cplat_atomic_store_i32(&ctx->send_thread_running, 0, CPLAT_MEMORY_ORDER_RELEASE);

        /* 同期資源生成 API は詳細原因を提供しない。 */
        return POTR_ERR_UNKNOWN;
    }

    if (cplat_thread_create(&ctx->send_thread, send_thread_func, ctx) != CPLAT_OK)
    {
        cplat_atomic_store_i32(&ctx->send_thread_running, 0, CPLAT_MEMORY_ORDER_RELEASE);
        cplat_local_lock_dispose(ctx->send_window_mutex);
        ctx->send_window_mutex = NULL;

        /* cplat のスレッド生成失敗には、porter の分類へ変換できる詳細コードがない。 */
        return POTR_ERR_UNKNOWN;
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_send_thread_stop(potr_context *ctx)
{
    if (ctx->send_thread == NULL)
    {
        return;
    }

    cplat_atomic_store_i32(&ctx->send_thread_running, 0, CPLAT_MEMORY_ORDER_RELEASE);
    potr_internal_send_queue_shutdown(&ctx->send_queue);

    if (cplat_thread_join(ctx->send_thread, CPLAT_SYNC_WAIT_FOREVER) != CPLAT_OK)
    {
        return;
    }

    ctx->send_thread = NULL;
    cplat_local_lock_dispose(ctx->send_window_mutex);
    ctx->send_window_mutex = NULL;
}
