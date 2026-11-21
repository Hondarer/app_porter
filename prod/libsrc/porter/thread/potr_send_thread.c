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
 *  @par            まとめ送り (TCP 1:1)
 *  TCP_NODELAY を有効にした TCP では、送信呼び出しごとにセグメントが送り出されます。\n
 *  外側パケットごとに送信を呼び出すと、セグメント数と受信側の起床が増えて処理件数が下がるため、
 *  送信スレッドはキューにある後続のエントリも外側パケットに組み立て、メッセージの境界をまたいで
 *  最大 tcp_batch_cap 個を経路ごとに 1 つのバッファーへ封緘して、1 回の送信呼び出しで書き込みます。\n
 *  外側パケットの組み立ての規則と、各外側パケットの封緘結果は変えません。
 *  メッセージの境界では待たず、キューが空になった時点で集めた分を送ります。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/base/platform.h>

#include <assert.h>
#include <limits.h>
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
#include <cplat/crt/stdlib.h>
#include <cplat/runtime/memory_lock.h>
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

    memcpy(packed_buf + *packed_len, &flags_nbo, POTR_ELEM_HDR_FLAGS_SIZE);
    *packed_len += POTR_ELEM_HDR_FLAGS_SIZE;
    memcpy(packed_buf + *packed_len, &plen_nbo, POTR_ELEM_HDR_PLEN_SIZE);
    *packed_len += POTR_ELEM_HDR_PLEN_SIZE;
    memcpy(packed_buf + *packed_len, entry->payload, entry->payload_len);
    *packed_len += entry->payload_len;
}

/*
 * まとめ送りでは、経路ごとにパケット列を続けて封緘する。
 * 経路が複数の場合、送出通番 (tx_counter) の割り当て順は、パケットごとに経路を巡る順と異なり、
 * 1 回のまとめ送りの中で POTR_TCP_BATCH_MAX_PACKETS と経路数の積より小さい範囲でずれる。
 * 受信側の重複検出は直近 256 通番の範囲で順不同の到着を受け入れ、DATA の順序は通番 (seq_num) で整列するため、
 * ずれの最大値をこの範囲内に保つ。
 */
static_assert(POTR_TCP_BATCH_MAX_PACKETS * POTR_MAX_PATH <
                  sizeof(((potr_internal_replay_window *)0)->bitmap) * CHAR_BIT,
              "porter: TCP batch skew must stay within the replay window");
static_assert(POTR_TCP_BATCH_MAX_BYTES >=
                  POTR_PACKET_HEADER_WIRE_SIZE + POTR_DEFAULT_MAX_PAYLOAD + POTR_CRYPTO_TAG_SIZE,
              "porter: POTR_TCP_BATCH_MAX_BYTES must hold a packet of the default max payload");

/*
 * TCP の指定 path へ、packets の n_packets 個のパケットを順に封緘して連結し、1 回の送信呼び出しで書き込む。
 * 1 個の場合は crypto_buf、複数の場合は tcp_batch_wire_buf (tcp_batch_cap 個分) を封緘先とする。
 * 相手端は CONNECTED になる前に受信した DATA を破棄するため、未通知の経路状態があれば先に PING を書き込む。
 * 書き込めなかった path はストリームの途中が欠けるため、ソケットを停止して受信スレッドに切断を検知させる。
 * 成功時は POTR_OK、path が未接続、封緘または書き込みに失敗した場合は POTR_ERR_DISCONNECTED を返す。
 * 呼び出し元は send_window_mutex を保持する。
 */
static int tcp_write_path(potr_context *ctx, int path_idx, const potr_packet *packets, size_t n_packets)
{
    int result = POTR_ERR_DISCONNECTED;
    uint8_t *wire_buf = (n_packets > 1U) ? ctx->tcp_batch_wire_buf : ctx->crypto_buf;

    cplat_local_lock_lock(ctx->tcp_send_mutex[path_idx]);

    if (ctx->tcp_conn_fd[path_idx] != CPLAT_INVALID_SOCKET)
    {
        cplat_error detail;
        int announce_result = potr_internal_tcp_announce_ping_state_locked(ctx, path_idx);
        int sealed = (announce_result == POTR_OK);
        size_t total_len = 0U;
        size_t i;

        if (!sealed)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "send_thread[service_id=%" PRId64
                       "]: path[%d] PING before DATA failed (result=%d), disconnecting",
                       ctx->service.service_id, path_idx, announce_result);
        }

        /* 一部のパケットだけを書き込むとストリームの途中が欠けるため、すべて封緘できた場合に限り書き込む。 */
        for (i = 0U; sealed && i < n_packets; i++)
        {
            size_t wire_len = ctx->crypto_buf_size;

            if (potr_internal_packet_seal_for_context(ctx, &ctx->tx_session, &packets[i], wire_buf + total_len,
                                                      &wire_len) != POTR_OK)
            {
                sealed = 0;
            }
            else
            {
                total_len += wire_len;
            }
        }

        if (!sealed)
        {
            cplat_socket_shutdown(ctx->tcp_conn_fd[path_idx]);
        }
        else if (cplat_socket_send_all(ctx->tcp_conn_fd[path_idx], wire_buf, total_len, &detail) == CPLAT_OK)
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
 * TCP の指定された経路集合へ、同じパケット列 (packets の n_packets 個) を書き込む。
 * 送信バッファーに空きがない path は、他の path が書き込めた場合に限り今回のパケット列を飛ばす。
 * TCP 受信側は欠けた通番を補えないため、どの path にも書き込めないままパケットを捨ててはならない。
 * まとめたパケット列は、この判定を 1 つの単位として行う。
 * いずれかの path に書き込めた場合は POTR_OK、どの path にも書き込めない場合は POTR_ERR_DISCONNECTED を返す。
 * 呼び出し元は send_window_mutex を保持する。
 */
static int tcp_write_all_paths(potr_context *ctx, const potr_packet *packets, size_t n_packets, uint8_t paths)
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
        int writable = 1;

        if ((paths & (uint8_t)(1U << i)) == 0U || ctx->tcp_conn_fd[i] == CPLAT_INVALID_SOCKET)
        {
            continue;
        }

        /* 送信バッファーの空きを確認する (非ブロッキング)。確認できない場合は書き込みの結果で判定する。 */
        if (cplat_socket_timed_wait_writable(ctx->tcp_conn_fd[i], CPLAT_NO_WAIT, NULL) == CPLAT_ERR_TIMEOUT)
        {
            writable = 0;
        }

        if (writable)
        {
            /* 警告の後は、書き込み可能の判定を数え、POTR_TCP_BUF_FULL_SUPPRESS_COUNT 回で抑制を解く。 */
            if (ctx->buf_full_suppress_cnt[i] > 0)
            {
                ++ctx->buf_full_suppress_cnt[i];
                if (ctx->buf_full_suppress_cnt[i] > POTR_TCP_BUF_FULL_SUPPRESS_COUNT)
                {
                    ctx->buf_full_suppress_cnt[i] = 0;
                }
            }

            if (tcp_write_path(ctx, i, packets, n_packets) == POTR_OK)
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
            if (tcp_write_path(ctx, i, packets, n_packets) == POTR_OK)
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
        /*
         * DATA をソケットへ書き込んだ後に tcp_peer_fin_received を ACQUIRE で確認する。
         * 受信スレッドは tcp_peer_fin_received = 1 を RELEASE で設定した後に tcp_send_mutex[i] を取得して
         * FIN_ACK を書き込み、tcp_write_path() は同じ tcp_send_mutex[i] の下で DATA を書き込む。
         * このため、DATA 書き込み後の ACQUIRE 読み込みが 0 であれば、FIN_ACK はまだそのソケットへ書かれておらず、
         * 当該 DATA より後に書かれる (または書き込まれない) ことが保証される。
         */
        if (cplat_atomic_load_i32(&ctx->tcp_peer_fin_received, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
        {
            cplat_atomic_store_i32(&ctx->tcp_sent_after_peer_fin, 1, CPLAT_MEMORY_ORDER_RELEASE);
        }

        return POTR_OK;
    }

    return POTR_ERR_DISCONNECTED;
}

/*
 * TCP で、詰め済みのペイロード n_packets 個から連番の外側パケットを構築し、選択した経路へまとめて書き込む。
 * i 番目のペイロードは payload_base + i * payload_stride から始まる payload_lens[i] [byte] である。
 * 再送が必要な場合は、保持中の DATA を新しい DATA より先に 1 個ずつ送り直す。
 * どれかの path に書き込めた場合に限り、全パケットを send_window に登録して通番を進める。
 * どの path にも書き込めない場合は登録せず、次の送信で同じ通番を使う。
 * n_packets は 1 以上とし、2 以上の場合は tcp_batch_cap 以下とする。
 * 成功時は POTR_OK を返す。パケットを構築できない場合と、どの path にも書き込めない場合はエラー コードを返す。
 */
static int tcp_flush_packets(potr_context *ctx, const uint8_t *payload_base, size_t payload_stride,
                             const size_t *payload_lens, size_t n_packets)
{
    potr_packet packets[POTR_TCP_BATCH_MAX_PACKETS];
    potr_internal_packet_session_hdr shdr;
    potr_internal_path_group_result selection;
    uint32_t seq;
    size_t i;
    int result;

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = ctx->session_id;
    session_ts_to_hdr(&ctx->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);

    /* send_window へのアクセスを排他制御する (送信スレッド・ヘルス チェック スレッド・受信スレッドが競合)。 */
    cplat_local_lock_lock(ctx->send_window_mutex);

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
    for (i = 0U; i < n_packets; i++)
    {
        result = potr_internal_packet_build_packed(&packets[i], &shdr, seq + (uint32_t)i,
                                                   payload_base + i * payload_stride, payload_lens[i]);
        if (result != POTR_OK)
        {
            cplat_local_lock_unlock(ctx->send_window_mutex);
            return result;
        }
    }

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
                result = tcp_write_all_paths(ctx, &retained_packet, 1U, selection.selected_paths);
            }
            if (result != POTR_OK)
            {
                cplat_local_lock_unlock(ctx->send_window_mutex);
                return result;
            }
        }
        cplat_local_lock_lock(ctx->callback_mutex);
        if (ctx->path_group_state.resend_group == selection.next_group)
        {
            ctx->path_group_state.resend_group = 0U;
        }
        cplat_local_lock_unlock(ctx->callback_mutex);
    }

    result = tcp_write_all_paths(ctx, packets, n_packets, selection.selected_paths);
    if (result == POTR_OK && selection.data_paths != selection.selected_paths)
    {
        /*
         * 選択したグループの経路へは書き込み済みであり、ほかのグループへの複製の失敗で送信の結果は変わらず、
         * 書き込みの失敗は tcp_write_path がトレースするため、戻り値は明示的に破棄する。
         */
        (void)tcp_write_all_paths(ctx, packets, n_packets, (uint8_t)(selection.data_paths & ~selection.selected_paths));
    }
    if (result == POTR_OK)
    {
        for (i = 0U; i < n_packets; i++)
        {
            /* 満杯でも最古のエントリを追い出して格納し、NULL の場合だけ失敗する契約のため、戻り値は明示的に破棄する。 */
            (void)potr_internal_window_send_push(&ctx->send_window, &packets[i]);
        }
        ctx->send_has_data = 1;
    }

    cplat_local_lock_unlock(ctx->send_window_mutex);

    return result;
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
    int build_result;
    int result = POTR_OK;
    potr_internal_path_group_result selection;
    int i;
    int sent_any = 0;
    int window_pending = 1;

    if (is_tcp_type(ctx->service.type))
    {
        return tcp_flush_packets(ctx, packed_buf, 0U, &packed_len, 1U);
    }

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = ctx->session_id;
    session_ts_to_hdr(&ctx->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);

    /* send_window へのアクセスを排他制御する (送信スレッド・ヘルス チェック スレッド・受信スレッドが競合)。 */
    cplat_local_lock_lock(ctx->send_window_mutex);

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

    /*
     * unicast_bidir と unicast_bidir_n1 では、送出上限を使う場合に逆方向の DATA へ送出上限を相乗りさせる。
     * ロック順は send_window_mutex -> fc->mutex とする。相乗りしない通信種別ではロックを増やさない。
     */
    if ((ctx->service.type == POTR_TYPE_UNICAST_BIDIR || ctx->service.type == POTR_TYPE_UNICAST_BIDIR_N1) &&
        ctx->flow_control != NULL)
    {
        uint32_t send_limit = 0U;

        if (potr_internal_flow_control_piggyback_limit(ctx, POTR_PEER_NA, &send_limit) != 0)
        {
            outer_pkt.flags |= cplat_hton16(POTR_FLAG_SEND_LIMIT);
            outer_pkt.ack_num = cplat_hton32(send_limit);
        }
    }

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
            /*
             * 封緘に成功した平文を保持する。封緘と送出の間にはウィンドウ操作を挟まない。
             * 満杯でも最古のエントリを追い出して格納し、NULL の場合だけ失敗する契約のため、戻り値は明示的に破棄する。
             */
            (void)potr_internal_window_send_push(&ctx->send_window, &outer_pkt);
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
    if (!window_pending && (ctx->service.type == POTR_TYPE_UNICAST || ctx->service.type == POTR_TYPE_UNICAST_BIDIR))
    {
        uint64_t sent_ms = 0U;

        /* 送信が途切れた後の許可の短縮に使う時刻。取得失敗時は前回の時刻を保ち、戻り値は明示的に破棄する。 */
        if (cplat_clock_get_monotonic_ms(&sent_ms) == CPLAT_OK)
        {
            ctx->flow_last_data_send_ms = sent_ms;
        }
    }
    if (sent_any && should_track_valid_data_send_time(ctx))
    {
        potr_internal_health_thread_wake(ctx);
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

    cplat_local_lock_lock(peer->send_window_mutex);

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

    /*
     * N:1 サーバーでは、送出上限を使う場合に逆方向の DATA へ送出上限を相乗りさせる。
     * ロック順は peers_mutex -> peer->send_window_mutex -> fc->mutex とする。
     */
    if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR_N1 && ctx->flow_control != NULL)
    {
        uint32_t send_limit = 0U;

        if (potr_internal_flow_control_piggyback_limit(ctx, peer->peer_id, &send_limit) != 0)
        {
            outer_pkt.flags |= cplat_hton16(POTR_FLAG_SEND_LIMIT);
            outer_pkt.ack_num = cplat_hton32(send_limit);
        }
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
            /*
             * 送出したパケットは直後に送信ウィンドウへ保持し、届かなかった分は NACK による再送で回復するため、
             * 戻り値は明示的に破棄する。
             */
            (void)cplat_socket_sendto(ctx->sock[k], ctx->crypto_buf, wire_len, &peer->dest_addr[k], &sent, NULL);
            if (window_pending)
            {
                uint64_t sent_ms = 0U;

                /* 満杯でも最古のエントリを追い出して格納し、NULL の場合だけ失敗する契約のため、戻り値は明示的に破棄する。 */
                (void)potr_internal_window_send_push(&peer->send_window, &outer_pkt);
                peer->send_has_data = 1;
                if (cplat_clock_get_monotonic_ms(&sent_ms) == CPLAT_OK)
                {
                    peer->flow_last_data_send_ms = sent_ms;
                }
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
    cplat_local_lock_lock(ctx->peers_mutex);
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
    cplat_local_lock_lock(ctx->peers_mutex);
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

/*
 * パッキング待ちなしで、キューの先頭から続く単体エントリ (MORE_FRAG なし) を、外側パケットの上限まで
 * packed_buf へ詰める。暗号化時は認証タグの分を空けておく。詰めたエントリ数を *n_dequeued に加算する。
 */
static void pack_queued_entries(potr_context *ctx, uint8_t *packed_buf, size_t *packed_len, int *n_dequeued)
{
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

        if (*packed_len + elem_size > (size_t)ctx->global.max_payload - crypto_tag_overhead)
        {
            break;
        }

        if (potr_internal_send_queue_try_pop(&ctx->send_queue, &next) != POTR_OK)
        {
            break;
        }

        append_payload_elem(packed_buf, packed_len, &next);
        (*n_dequeued)++;
    }
}

/*
 * TCP 1:1 のまとめ送り。取り出し済みのエントリ first から始めて、キューにある後続のエントリも外側パケットに
 * 組み立て、最大 tcp_batch_cap 個を経路ごとに 1 回の送信呼び出しで書き込む。
 * TCP_NODELAY の下では送信呼び出しごとにセグメントが送り出されるため、外側パケットごとに送信を呼び出すと
 * セグメント数と受信側の起床が増え、処理件数が下がる。
 * 外側パケットの組み立ての規則は従来と同じである。
 * - MORE_FRAG 付きのエントリ (分割されたメッセージの途中の断片) は、1 件で 1 つの外側パケットにする。
 * - MORE_FRAG なしのエントリは、パッキング待ちなしの規則で後続の単体エントリを同じ外側パケットへ詰める。
 * 待ち時間は足さず、キューが空になった時点で集めた分を送る。分割されたメッセージの途中で次の断片がまだない
 * 場合も待たない。同じメッセージの断片は送信 API が push_mutex を保持したまま続けて登録するため、ほとんどの
 * 場合は取り出す時点でキューにそろっており、待ちを加えても処理件数は変わらないためである。
 * 残りの断片は、次の周回で同じ規則により送る。
 * 外側パケットの数は、相手から受けた送出上限までに送出できる数 (allowance) を超えない。
 * 送出上限は送出の前に 1 回だけ判定するため、まとめ送りで送出上限を超えて送り続けないようにするためである。
 * *n_dequeued には取り出したエントリ数を加算する。戻り値は tcp_flush_packets() の結果である。
 */
static int send_tcp_batch(potr_context *ctx, const potr_internal_payload_elem *first, int *n_dequeued,
                          uint32_t allowance)
{
    size_t payload_lens[POTR_TCP_BATCH_MAX_PACKETS];
    size_t stride = (size_t)ctx->global.max_payload;
    size_t n_packets = 0U;
    size_t batch_cap = ctx->tcp_batch_cap;
    potr_internal_payload_elem cur = *first;

    if ((size_t)allowance < batch_cap)
    {
        batch_cap = (size_t)allowance;
    }

    for (;;)
    {
        uint8_t *packed_buf = ctx->tcp_batch_plain_buf + n_packets * stride;
        size_t packed_len = 0U;

        append_payload_elem(packed_buf, &packed_len, &cur);
        if (!(cur.flags & POTR_FLAG_MORE_FRAG))
        {
            pack_queued_entries(ctx, packed_buf, &packed_len, n_dequeued);
        }
        payload_lens[n_packets++] = packed_len;

        if (n_packets >= batch_cap ||
            potr_internal_send_queue_try_pop(&ctx->send_queue, &cur) != POTR_OK)
        {
            break;
        }
        (*n_dequeued)++;
    }

    return tcp_flush_packets(ctx, ctx->tcp_batch_plain_buf, stride, payload_lens, n_packets);
}

/* 送信スレッド本体 */
/** 1 回の走査で送出を見送るピアの数の上限。超える分は次の走査で扱う。 */
#define FLOW_DEFERRED_SKIP_MAX 16U

static_assert(FLOW_DEFERRED_SKIP_MAX >= 1U && FLOW_DEFERRED_SKIP_MAX <= UINT32_MAX,
              "porter: FLOW_DEFERRED_SKIP_MAX out of range");

/**
 *  N:1 モードで、抑制中のピア宛のために送出を保留したエントリ。ペイロードは構造体の直後に置く。
 *  送信キューの登録順を保つ 1 本のリストで管理し、同じピア宛のエントリの順序を変えない。
 */
typedef struct potr_internal_deferred_entry
{
    struct potr_internal_deferred_entry *next; /**< 次の保留エントリ。 */
    potr_peer_id peer_id;                      /**< 宛先のピア識別子。 */
    uint16_t flags;                            /**< ペイロード エレメント フラグ。 */
    uint16_t payload_len;                      /**< ペイロード長 [byte]。 */
} potr_internal_deferred_entry;

/*
 * ピアが抑制中か、またはそのピア宛の保留エントリが残っているかを返す。どちらの場合も、新しいエントリを先に送出しない。
 * blocked には、ピアが抑制中かどうかを返す。
 */
static int peer_send_deferred(potr_context *ctx, potr_peer_id peer_id, int *blocked)
{
    uint64_t now_ms;
    int deferred = 0;

    /* 時刻を取得できない場合は期限を判定できないため、抑制せずに送出する。 */
    if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
    {
        *blocked = 0;
        return 0;
    }

    *blocked = potr_internal_flow_control_peer_state(ctx, peer_id, now_ms, &deferred);

    return *blocked != 0 || deferred != 0;
}

/*
 * 取り出したエントリの宛先が抑制中の場合、または同じ宛先の保留エントリが残っている場合に、エントリを保留する。
 * 保留した場合は 1、そのまま送出する場合は 0 を返す。
 * 保留したエントリは、完了待ちの番号だけを進める。送信キューの容量は、送出するまで占有し続ける。
 */
static int defer_entry_if_blocked(potr_context *ctx, const potr_internal_payload_elem *entry)
{
    potr_internal_deferred_entry *node;
    potr_internal_peer_context *peer;
    int blocked;

    /*
     * 保留中のエントリがなく、抑制の要求または送出上限を保持しているピアもない間は、どのピア宛も保留しない。
     * フロー制御を使わない構成で、エントリごとに時刻とロックを取得しないためである。
     */
    if (ctx->flow_deferred_head == NULL && potr_internal_flow_control_peers_idle(ctx) != 0)
    {
        return 0;
    }

    if (peer_send_deferred(ctx, entry->peer_id, &blocked) == 0)
    {
        return 0;
    }

    node = (potr_internal_deferred_entry *)cplat_malloc(sizeof(*node) + (size_t)entry->payload_len);
    if (node == NULL)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                   "sender[service_id=%" PRId64 "]: peer=%u failed to hold suppressed entry (out of memory)",
                   ctx->service.service_id, (unsigned)entry->peer_id);
        potr_internal_send_queue_complete_failed(&ctx->send_queue, POTR_ERR_OUT_OF_MEMORY);
        return 1;
    }

    node->next = NULL;
    node->peer_id = entry->peer_id;
    node->flags = entry->flags;
    node->payload_len = entry->payload_len;
    memcpy(node + 1, entry->payload, entry->payload_len);

    if (ctx->flow_deferred_tail != NULL)
    {
        ctx->flow_deferred_tail->next = node;
    }
    else
    {
        ctx->flow_deferred_head = node;
    }

    ctx->flow_deferred_tail = node;

    /*
     * 送信 API は、完了待ちの後に保留の総数を見て保留の有無を確かめる。完了待ちの番号を進める前に総数へ加える。
     * 加算前の値は使わないため、戻り値は明示的に破棄する。
     */
    (void)cplat_atomic_fetch_add_i32(&ctx->flow_deferred_total, 1, CPLAT_MEMORY_ORDER_RELEASE);

    cplat_local_lock_lock(ctx->peers_mutex);
    peer = potr_internal_peer_find_by_id(ctx, entry->peer_id);
    if (peer != NULL)
    {
        if (peer->flow_deferred_entries == 0)
        {
            uint64_t deferred_ms = 0U;

            if (cplat_clock_get_monotonic_ms(&deferred_ms) == CPLAT_OK)
            {
                peer->flow_deferred_since_ms = deferred_ms;
            }
        }
        peer->flow_deferred_entries++;
    }
    cplat_local_lock_unlock(ctx->peers_mutex);

    potr_internal_send_queue_defer(&ctx->send_queue);

    return 1;
}

/* 保留していたエントリを 1 つ送出する。宛先のピアが切断済みの場合は破棄する。 */
static void send_deferred_entry(potr_context *ctx, potr_internal_deferred_entry *node)
{
    potr_internal_payload_elem entry;
    potr_internal_peer_context *peer;
    uint8_t *packed_buf = ctx->send_wire_buf + POTR_PACKET_HEADER_SIZE;
    size_t packed_len = 0;

    entry.peer_id = node->peer_id;
    entry.flags = node->flags;
    entry.payload_len = node->payload_len;
    entry.payload = (uint8_t *)(node + 1);
    append_payload_elem(packed_buf, &packed_len, &entry);

    cplat_local_lock_lock(ctx->peers_mutex);
    peer = potr_internal_peer_find_by_id(ctx, node->peer_id);
    if (peer != NULL)
    {
        if (peer->flow_deferred_entries > 0)
        {
            peer->flow_deferred_entries--;
            if (peer->flow_deferred_entries == 0)
            {
                peer->flow_deferred_since_ms = 0U;
            }
        }

        flush_packed_peer(ctx, peer, packed_len);
        if (cplat_atomic_load_i32(&peer->tx_session.exhausted, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
        {
            thread_recv_disconnect_peer_all_paths(ctx, peer);
            potr_internal_peer_free(ctx, peer);
        }
    }
    cplat_local_lock_unlock(ctx->peers_mutex);

    potr_internal_send_queue_complete_deferred(&ctx->send_queue);
}

/*
 * 保留中のエントリのうち、宛先の抑制が解除されたものを登録順に送出する。
 * あるピア宛のエントリを見送った後は、同じ走査の中でそのピア宛の後続のエントリも見送り、順序を保つ。
 * drop_all が非 0 の場合は、送出せずにすべて破棄する (送信スレッドの終了時)。
 */
static void flush_deferred_entries(potr_context *ctx, int drop_all)
{
    potr_peer_id skipped[FLOW_DEFERRED_SKIP_MAX];
    size_t n_skipped = 0U;
    potr_internal_deferred_entry *prev = NULL;
    potr_internal_deferred_entry *node = ctx->flow_deferred_head;

    while (node != NULL)
    {
        potr_internal_deferred_entry *next = node->next;
        int skip = 0;
        size_t i;

        if (drop_all == 0)
        {
            for (i = 0U; i < n_skipped; i++)
            {
                if (skipped[i] == node->peer_id)
                {
                    skip = 1;
                }
            }

            if (skip == 0)
            {
                int blocked = 0;

                /* 抑制の要求または送出上限を保持しているピアがない間は、どのピアも抑制中でないため判定を省く。 */
                if (potr_internal_flow_control_peers_idle(ctx) == 0)
                {
                    /* 保留の有無ではなく抑制の状態 (blocked) だけを使うため、戻り値は明示的に破棄する。 */
                    (void)peer_send_deferred(ctx, node->peer_id, &blocked);
                }

                if (blocked != 0)
                {
                    /* 見送るピアの記録が上限に達した場合は、順序を保てないため、残りを次の走査へ回す。 */
                    if (n_skipped == FLOW_DEFERRED_SKIP_MAX)
                    {
                        return;
                    }

                    skipped[n_skipped++] = node->peer_id;
                    skip = 1;
                }
            }
        }

        if (skip != 0)
        {
            prev = node;
            node = next;
            continue;
        }

        if (prev != NULL)
        {
            prev->next = next;
        }
        else
        {
            ctx->flow_deferred_head = next;
        }

        if (ctx->flow_deferred_tail == node)
        {
            ctx->flow_deferred_tail = prev;
        }

        if (drop_all == 0)
        {
            send_deferred_entry(ctx, node);
        }
        else
        {
            potr_internal_send_queue_complete_deferred(&ctx->send_queue);
        }

        /* ピアごとの保留数を減らした後に総数を減らす。減算前の値は使わないため、戻り値は明示的に破棄する。 */
        (void)cplat_atomic_fetch_sub_i32(&ctx->flow_deferred_total, 1, CPLAT_MEMORY_ORDER_RELEASE);

        cplat_secure_zero(node + 1, node->payload_len);
        cplat_free(node);
        node = next;
    }
}

/*
 * N:1 モード専用: 保留中のエントリを先に送出してから、送信キューの次のエントリを取り出す。
 * 保留中のエントリが残っている間は、抑制の解除を確認し直すため、時間を区切って待つ。
 * 取り出した場合は POTR_OK、時間切れの場合は POTR_ERR_TIMEOUT、終了要求と異常の場合はその原因コードを返す。
 */
static int pop_peer_mode(potr_context *ctx, potr_internal_payload_elem *first)
{
    int result;

    flush_deferred_entries(ctx, 0);

    if (ctx->flow_deferred_head == NULL)
    {
        return potr_internal_send_queue_pop(&ctx->send_queue, first, &ctx->send_thread_running);
    }

    result = potr_internal_send_queue_peek_timed(&ctx->send_queue, first, POTR_INTERNAL_FLOW_LIMIT_RECHECK_MS);
    if (result == POTR_ERR_TIMEOUT)
    {
        if (cplat_atomic_load_i32(&ctx->send_thread_running, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
        {
            return POTR_ERR_CANCELED;
        }

        return POTR_ERR_TIMEOUT;
    }

    if (result != POTR_OK)
    {
        return result;
    }

    /* 取り出すのは送信スレッドだけであり、参照できたエントリは必ず取り出せる。 */
    if (potr_internal_send_queue_try_pop(&ctx->send_queue, first) != POTR_OK)
    {
        return POTR_ERR_TIMEOUT;
    }

    return POTR_OK;
}

/*
 * 相手から抑制の要求も送出上限も受けていないかを返す (N:1 モード以外)。受けていない場合は 1 を返す。
 * 1 を返す場合は potr_internal_flow_control_suppressed() が必ず 0 を返すため、時刻とロックの取得を省ける。
 * 片方向 UDP は、受信者の管理表に要求か送出上限が残っている可能性 (flow_request_pending) で判定する。
 * それ以外は、要求の期限と送出上限がどちらも 0 であることで判定する。このとき、送出上限で止まった時刻も
 * 0 へ戻す。送出上限を保持していない判定 (flow_send_limit_judge()) と同じ状態に保つためである。
 */
static int flow_control_sender_idle(potr_context *ctx)
{
    uint64_t limit_state;

    if (is_raw_type(ctx->service.type))
    {
        return 1;
    }

    if (ctx->service.type == POTR_TYPE_MULTICAST || ctx->service.type == POTR_TYPE_BROADCAST)
    {
        return cplat_atomic_load_i32(&ctx->flow_request_pending, CPLAT_MEMORY_ORDER_ACQUIRE) == 0;
    }

    if (ctx->service.type == POTR_TYPE_UNICAST)
    {
        if (cplat_atomic_load_i32(&ctx->flow_request_pending, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
        {
            return 0;
        }

        limit_state = cplat_atomic_load_u64(&ctx->flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE) &
                      POTR_FLOW_SEND_LIMIT_STATE_MASK;
        if (limit_state != POTR_FLOW_SEND_LIMIT_UNLIMITED && limit_state != POTR_FLOW_SEND_LIMIT_NO_PEER)
        {
            return 0;
        }

        ctx->flow_limit_stalled_ms = 0U;
        ctx->flow_query_next_ms = 0U;
        ctx->flow_query_interval_ms = 0U;
        return 1;
    }

    limit_state =
        cplat_atomic_load_u64(&ctx->flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE) & POTR_FLOW_SEND_LIMIT_STATE_MASK;
    if ((limit_state != POTR_FLOW_SEND_LIMIT_UNLIMITED && limit_state != POTR_FLOW_SEND_LIMIT_NO_PEER) ||
        cplat_atomic_load_u64(&ctx->flow_request_expire_ms, CPLAT_MEMORY_ORDER_ACQUIRE) != 0U)
    {
        return 0;
    }

    ctx->flow_limit_stalled_ms = 0U;
    ctx->flow_query_next_ms = 0U;
    ctx->flow_query_interval_ms = 0U;

    return 1;
}

/*
 * 相手が「止める状態」(有効な抑制の要求がある、または送出上限に達している) の間、新しい DATA の送出を待つ。
 * 取り出し済みの先頭エントリは、解除、要求の期限の経過、送出上限の更新または破棄、close の開始のいずれかの後に送出する。
 * 再送、PING、FIN などはほかのスレッドが送出するため、抑制中も継続する。
 * 戻り値は、送出上限に達するまでに送出できる DATA のパケット数である (上限がない場合は UINT32_MAX)。
 */
static uint32_t wait_flow_resume(potr_context *ctx)
{
    uint64_t generation = 0U;
    uint32_t allowance = UINT32_MAX;
    int announced = 0;

    /* フロー制御パケットを受けていない間は、時刻とロックを取得せずに送出する。 */
    if (flow_control_sender_idle(ctx) != 0)
    {
        return UINT32_MAX;
    }

    for (;;)
    {
        uint64_t now_ms;
        int wait_ms = 0;

        allowance = UINT32_MAX;

        /* 時刻を取得できない場合は期限を判定できないため、抑制せずに送出する。 */
        if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
        {
            break;
        }

        if (potr_internal_flow_control_suppressed(ctx, now_ms, &wait_ms, &allowance) == 0 ||
            cplat_atomic_load_i32(&ctx->send_thread_running, CPLAT_MEMORY_ORDER_ACQUIRE) == 0 ||
            cplat_atomic_load_i32(&ctx->fatal_error, CPLAT_MEMORY_ORDER_ACQUIRE) != POTR_OK)
        {
            break;
        }

        if (announced == 0)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "sender[service_id=%" PRId64 "]: flow control suppressed DATA",
                       ctx->service.service_id);
            announced = 1;
        }

        potr_internal_send_queue_wait_flow(&ctx->send_queue, &generation, wait_ms);
    }

    if (announced != 0)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO, "sender[service_id=%" PRId64 "]: flow control resumed DATA",
                   ctx->service.service_id);
    }

    return allowance;
}

static void send_thread_func(void *arg)
{
    potr_context *ctx = (potr_context *)arg;
    potr_internal_payload_elem first;
    uint32_t allowance = UINT32_MAX;

    while (cplat_atomic_load_i32(&ctx->fatal_error, CPLAT_MEMORY_ORDER_ACQUIRE) == POTR_OK)
    {
        /* N:1 モード: peer_id でルーティングして送信する。抑制中のピア宛は保留し、ほかのピア宛を先に送出する。 */
        if (ctx->is_multi_peer)
        {
            int pop_result = pop_peer_mode(ctx, &first);

            if (pop_result == POTR_ERR_TIMEOUT)
            {
                continue;
            }

            if (pop_result != POTR_OK)
            {
                break;
            }

            if (defer_entry_if_blocked(ctx, &first) == 0)
            {
                send_packed_peer_mode(ctx, &first);
            }

            continue;
        }

        /* キューからエントリを取り出す (ブロッキング)。 */
        if (potr_internal_send_queue_pop(&ctx->send_queue, &first, &ctx->send_thread_running) != POTR_OK)
        {
            break;
        }

        /* 相手が「止める状態」の間は、新しい DATA を送出せずに待つ。 */
        allowance = wait_flow_resume(ctx);

        /* パッキングを試行する。 */
        {
            /* packed_buf は send_wire_buf のヘッダー直後領域を直接使用する (ゼロ コピー)。 */
            uint8_t *packed_buf = ctx->send_wire_buf + POTR_PACKET_HEADER_SIZE;
            size_t packed_len = 0;
            int n_dequeued = 1;
            int flush_result = POTR_OK;

            /*
             * TCP 1:1 はまとめ送りで送る。パッキング待ちが設定され、先頭が分割されていないメッセージの場合は、
             * 待ち合わせを含む従来の処理で送る。
             */
            if (ctx->tcp_batch_cap >= POTR_TCP_BATCH_MIN_PACKETS &&
                ((first.flags & POTR_FLAG_MORE_FRAG) != 0U || ctx->service.pack_wait_ms == 0U))
            {
                flush_result = send_tcp_batch(ctx, &first, &n_dequeued, allowance);
                goto complete_packed;
            }

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
                    pack_queued_entries(ctx, packed_buf, &packed_len, &n_dequeued);
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

    /* 終了時に残っている保留エントリは、送出せずに破棄する。 */
    flush_deferred_entries(ctx, 1);

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

    cplat_thread_join(ctx->send_thread);

    ctx->send_thread = NULL;
    cplat_local_lock_dispose(ctx->send_window_mutex);
    ctx->send_window_mutex = NULL;
}
