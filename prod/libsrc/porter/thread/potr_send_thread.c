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
 *  potr_send の flags 引数の値によらず常に起動しており、
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
#include <porter/protocol/window.h>
#include <porter/infra/potr_trace.h>
#include <cplat/crypto/crypto.h>
#include <cplat/net/byteorder.h>
#include <cplat/net/socket.h>

static int should_track_valid_data_send_time(const potr_context *ctx)
{
    return ctx != NULL && !ctx->is_multi_peer && potr_is_oneway_udp_type(ctx->service.type);
}

/* ペイロード エレメントを packed_buf に追記する */
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

/* send_wire_buf の [PACKET_HEADER_SIZE .. PACKET_HEADER_SIZE+packed_len-1] に
   詰め済みのペイロードから外側コンテナーを構築して送信する。
   seq_num を付与する。UDP では再送バッファー (send_window) にも登録する。
   send_wire_buf = [NBO ヘッダー 32B][packed_payload packed_len B] として組み立てる。 */
static void flush_packed(potr_context *ctx, size_t packed_len)
{
    potr_packet outer_pkt;
    potr_internal_packet_session_hdr shdr;
    uint32_t seq;
    size_t wire_len;
    uint8_t *packed_buf = ctx->send_wire_buf + PACKET_HEADER_SIZE;
    int is_tcp = potr_is_tcp_type(ctx->service.type);

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = ctx->session_id;
    potr_session_ts_to_hdr(&ctx->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);

    /* send_window へのアクセスを排他制御する (送信スレッド・ヘルスチェック スレッド・受信スレッドが競合) */
    cplat_local_lock_lock(ctx->send_window_mutex, CPLAT_SYNC_WAIT_FOREVER);

    seq = ctx->send_window.next_seq;

    if (potr_internal_packet_build_packed(&outer_pkt, &shdr, seq, packed_buf, packed_len) != POTR_OK)
    {
        cplat_local_lock_unlock(ctx->send_window_mutex);
        return;
    }

    if (ctx->service.encrypt_enabled)
    {
        /* 暗号化パス:
         *   1. ENCRYPTED フラグを OR (outer_pkt.flags はすでに NBO)
         *   2. payload_len を packed_len + TAG_SIZE に更新
         *   3. nonce = session_id(4B NBO) + flags(2B NBO) + seq_num(4B NBO) + padding(2B)
         *   4. AAD  = outer_pkt ヘッダー 32B (NBO)
         *   5. packed_buf → ctx->crypto_buf に暗号化
         *   6. UDP: potr_internal_window_send_push に暗号化済みペイロードを登録
         *      TCP: ウィンドウ登録不要 (再送は TCP 層が担保); next_seq のみインクリメント
         *   7. send_wire_buf に暗号化済みデータを組立て
         */
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t enc_len = ctx->crypto_buf_size;

        outer_pkt.flags |= cplat_hton16(POTR_FLAG_ENCRYPTED);
        outer_pkt.payload_len = cplat_hton16((uint16_t)(packed_len + POTR_CRYPTO_TAG_SIZE));

        /* ノンス: session_id(4B NBO) + flags(2B NBO) + seq_num(4B NBO) + padding(2B)
         * outer_pkt の各フィールドはすでに NBO */
        memcpy(nonce, &outer_pkt.session_id, 4);
        memcpy(nonce + 4, &outer_pkt.flags, 2);
        memcpy(nonce + 6, &outer_pkt.seq_num, 4);
        memset(nonce + 10, 0, 2);

        if (cplat_encrypt(ctx->crypto_buf, &enc_len, packed_buf, packed_len, ctx->service.encrypt_key, nonce,
                             (const uint8_t *)&outer_pkt, PACKET_HEADER_SIZE) != CPLAT_OK)
        {
            cplat_local_lock_unlock(ctx->send_window_mutex);
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "sender[service_id=%" PRId64 "]: encrypt failed seq=%u",
                       ctx->service.service_id, (unsigned)seq);
            return;
        }

        if (is_tcp)
        {
            /* TCP: ウィンドウ登録不要。next_seq をインクリメントして mutex を解放 */
            ctx->send_window.next_seq++;
            ctx->send_has_data = 1;
            cplat_local_lock_unlock(ctx->send_window_mutex);
        }
        else
        {
            /* window には暗号化済みペイロードを格納して NACK 再送時に再暗号化不要にする */
            outer_pkt.payload = ctx->crypto_buf;
            potr_internal_window_send_push(&ctx->send_window, &outer_pkt);
            ctx->send_has_data = 1;
            cplat_local_lock_unlock(ctx->send_window_mutex);
        }

        /* wire 組立: NBO ヘッダー + 暗号文 + タグ */
        memcpy(ctx->send_wire_buf, &outer_pkt, PACKET_HEADER_SIZE);
        memcpy(ctx->send_wire_buf + PACKET_HEADER_SIZE, ctx->crypto_buf, enc_len);
        wire_len = PACKET_HEADER_SIZE + enc_len;

        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                   "sender[service_id=%" PRId64 "]: DATA(enc) seq=%u packed_len=%zu enc_len=%zu",
                   ctx->service.service_id, (unsigned)seq, packed_len, enc_len);
    }
    else
    {
        if (is_tcp)
        {
            /* TCP: ウィンドウ登録不要。next_seq をインクリメントして mutex を解放 */
            ctx->send_window.next_seq++;
            ctx->send_has_data = 1;
            cplat_local_lock_unlock(ctx->send_window_mutex);
        }
        else
        {
            potr_internal_window_send_push(&ctx->send_window, &outer_pkt);
            ctx->send_has_data = 1;
            cplat_local_lock_unlock(ctx->send_window_mutex);
        }

        /* NBO ヘッダー (32B) を send_wire_buf 先頭に書き込む (ペイロードはすでに直後に配置済み) */
        memcpy(ctx->send_wire_buf, &outer_pkt, PACKET_HEADER_SIZE);
        wire_len = PACKET_HEADER_SIZE + packed_len;

        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "sender[service_id=%" PRId64 "]: DATA seq=%u packed_len=%zu",
                   ctx->service.service_id, (unsigned)seq, packed_len);
    }

    if (is_tcp)
    {
        /* TCP v2: アクティブな全 path にループ送信する */
        if (ctx->tcp_active_paths > 0)
        {
            int i;
            for (i = 0; i < ctx->n_path; i++)
            {
                int writable = 0;

                if (ctx->tcp_conn_fd[i] == CPLAT_INVALID_SOCKET)
                    continue;

                /* 送信バッファーの空きを確認 (非ブロッキング) */
                (void)cplat_socket_wait_writable(ctx->tcp_conn_fd[i], CPLAT_SOCKET_NO_WAIT, &writable, NULL);
                if (writable)
                {
                    if (ctx->buf_full_suppress_cnt[i] > 0 && ++ctx->buf_full_suppress_cnt[i] > 10)
                    {
                        ctx->buf_full_suppress_cnt[i] = 0;
                    }
                    cplat_local_lock_lock(ctx->tcp_send_mutex[i], CPLAT_SYNC_WAIT_FOREVER);
                    (void)cplat_socket_send_all(ctx->tcp_conn_fd[i], ctx->send_wire_buf, wire_len, NULL);
                    cplat_local_lock_unlock(ctx->tcp_send_mutex[i]);
                }
                else
                {
                    if (ctx->buf_full_suppress_cnt[i] == 0)
                    {
                        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                                   "send_thread[service_id=%" PRId64 "]: path[%d]"
                                   " send buffer full, packet skipped",
                                   ctx->service.service_id, i);
                        ctx->buf_full_suppress_cnt[i] = 1;
                    }
                }
            }
        }
    }
    else
    {
        int i;
        int sent_any = 0;
        for (i = 0; i < ctx->n_path; i++)
        {
            size_t sent = 0;
            int send_ret;

            send_ret =
                cplat_socket_sendto(ctx->sock[i], ctx->send_wire_buf, wire_len, &ctx->dest_addr[i], &sent, NULL);
            if (send_ret == CPLAT_OK)
            {
                sent_any = 1;
            }
        }

        if (sent_any && should_track_valid_data_send_time(ctx))
        {
            ctx->last_valid_data_send_ms = cplat_get_monotonic_ms();
            potr_internal_health_thread_wake(ctx);
        }
    }
}

/* N:1 モード専用: ピアの send_window を使ってパック コンテナーを構築して sendto する */
static void flush_packed_peer(potr_context *ctx, potr_internal_peer_context *peer, size_t packed_len)
{
    potr_packet outer_pkt;
    potr_internal_packet_session_hdr shdr;
    uint32_t seq;
    size_t wire_len;
    uint8_t *packed_buf = ctx->send_wire_buf + PACKET_HEADER_SIZE;

    shdr.service_id = ctx->service.service_id;
    shdr.session_id = peer->session_id;
    potr_session_ts_to_hdr(&peer->session_ts, &shdr.session_tv_sec, &shdr.session_tv_nsec);

    cplat_local_lock_lock(peer->send_window_mutex, CPLAT_SYNC_WAIT_FOREVER);

    seq = peer->send_window.next_seq;

    if (potr_internal_packet_build_packed(&outer_pkt, &shdr, seq, packed_buf, packed_len) != POTR_OK)
    {
        cplat_local_lock_unlock(peer->send_window_mutex);
        return;
    }

    if (ctx->service.encrypt_enabled)
    {
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE];
        size_t enc_len = ctx->crypto_buf_size;

        outer_pkt.flags |= cplat_hton16(POTR_FLAG_ENCRYPTED);
        outer_pkt.payload_len = cplat_hton16((uint16_t)(packed_len + POTR_CRYPTO_TAG_SIZE));

        /* ノンス: session_id(4B NBO) + flags(2B NBO) + seq_num(4B NBO) + padding(2B)
         * outer_pkt の各フィールドはすでに NBO */
        memcpy(nonce, &outer_pkt.session_id, 4);
        memcpy(nonce + 4, &outer_pkt.flags, 2);
        memcpy(nonce + 6, &outer_pkt.seq_num, 4);
        memset(nonce + 10, 0, 2);

        if (cplat_encrypt(ctx->crypto_buf, &enc_len, packed_buf, packed_len, ctx->service.encrypt_key, nonce,
                             (const uint8_t *)&outer_pkt, PACKET_HEADER_SIZE) != CPLAT_OK)
        {
            cplat_local_lock_unlock(peer->send_window_mutex);
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "sender[service_id=%" PRId64 "]: peer=%u encrypt failed seq=%u",
                       ctx->service.service_id, (unsigned)peer->peer_id, (unsigned)seq);
            return;
        }

        outer_pkt.payload = ctx->crypto_buf;
        potr_internal_window_send_push(&peer->send_window, &outer_pkt);
        peer->send_has_data = 1;

        cplat_local_lock_unlock(peer->send_window_mutex);

        memcpy(ctx->send_wire_buf, &outer_pkt, PACKET_HEADER_SIZE);
        memcpy(ctx->send_wire_buf + PACKET_HEADER_SIZE, ctx->crypto_buf, enc_len);
        wire_len = PACKET_HEADER_SIZE + enc_len;

        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                   "sender[service_id=%" PRId64 "]: peer=%u DATA(enc) seq=%u packed_len=%zu", ctx->service.service_id,
                   (unsigned)peer->peer_id, (unsigned)seq, packed_len);
    }
    else
    {
        potr_internal_window_send_push(&peer->send_window, &outer_pkt);
        peer->send_has_data = 1;

        cplat_local_lock_unlock(peer->send_window_mutex);

        memcpy(ctx->send_wire_buf, &outer_pkt, PACKET_HEADER_SIZE);
        wire_len = PACKET_HEADER_SIZE + packed_len;

        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "sender[service_id=%" PRId64 "]: peer=%u DATA seq=%u packed_len=%zu",
                   ctx->service.service_id, (unsigned)peer->peer_id, (unsigned)seq, packed_len);
    }

    /* N:1 はインデックス = ctx->sock[] の添字として全パスへ送信する */
    {
        int k;
        for (k = 0; k < (int)POTR_MAX_PATH; k++)
        {
            size_t sent = 0;

            if (potr_endpoint_is_unset(&peer->dest_addr[k]))
                continue;
            (void)cplat_socket_sendto(ctx->sock[k], ctx->send_wire_buf, wire_len, &peer->dest_addr[k], &sent, NULL);
        }
    }
}

/* N:1 モード専用: キューからエントリを取り出してピアへパッキング送信する */
static void send_packed_peer_mode(potr_context *ctx, potr_internal_payload_elem *first)
{
    potr_peer_id target_peer_id = first->peer_id;
    potr_internal_peer_context *peer = NULL;
    uint8_t *packed_buf = ctx->send_wire_buf + PACKET_HEADER_SIZE;
    size_t packed_len = 0;
    int n_dequeued = 1;

    /* ピアを検索 (peers_mutex は lookup だけ保護、送信中は解放する) */
    cplat_local_lock_lock(ctx->peers_mutex, CPLAT_SYNC_WAIT_FOREVER);
    peer = potr_internal_peer_find_by_id(ctx, target_peer_id);
    cplat_local_lock_unlock(ctx->peers_mutex);

    if (peer == NULL)
    {
        /* 切断済みピア宛エントリ: 破棄 */
        potr_internal_send_queue_complete(&ctx->send_queue);
        return;
    }

    append_payload_elem(packed_buf, &packed_len, first);

    /* 同一ピア宛の追加エントリを即時パッキング (pack_wait なし) */
    if (!(first->flags & POTR_FLAG_MORE_FRAG))
    {
        potr_internal_payload_elem next;

        while (potr_internal_send_queue_peek(&ctx->send_queue, &next) == POTR_OK)
        {
            size_t elem_size;
            size_t crypto_tag_overhead;

            if (next.peer_id != target_peer_id)
                break;
            if (next.flags & POTR_FLAG_MORE_FRAG)
                break;

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

    flush_packed_peer(ctx, peer, packed_len);

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

    for (;;)
    {
        /* キューからエントリを取り出す (ブロッキング) */
        if (potr_internal_send_queue_pop(&ctx->send_queue, &first, &ctx->send_thread_running) != POTR_OK)
        {
            break;
        }

        /* N:1 モード: peer_id でルーティングして送信 */
        if (ctx->is_multi_peer)
        {
            send_packed_peer_mode(ctx, &first);
            continue;
        }

        /* パッキング試行 */
        {
            /* packed_buf は send_wire_buf のヘッダー直後領域を直接使用 (ゼロ コピー) */
            uint8_t *packed_buf = ctx->send_wire_buf + PACKET_HEADER_SIZE;
            size_t packed_len = 0;
            int n_dequeued = 1;

            append_payload_elem(packed_buf, &packed_len, &first);

            /* MORE_FRAG エントリはパッキング不可。そのまま単体コンテナーとして送信。 */
            if (!(first.flags & POTR_FLAG_MORE_FRAG))
            {
                uint32_t pack_wait_ms = ctx->service.pack_wait_ms;

                if (pack_wait_ms > 0)
                {
                    /* パッキング待ちあり: タイムアウトまで追加エントリを待ち合わせる */
                    uint64_t deadline = cplat_get_monotonic_ms() + pack_wait_ms;
                    potr_internal_payload_elem next;

                    for (;;)
                    {
                        uint64_t now = cplat_get_monotonic_ms();
                        uint32_t remaining;
                        size_t elem_size;
                        size_t crypto_tag_overhead;

                        if (now >= deadline)
                        {
                            break; /* タイムアウト */
                        }

                        remaining = (uint32_t)(deadline - now);

                        /* remaining は deadline - now (ms) であり pack_wait_ms 以下で INT_MAX に収まる */
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
                    /* パッキング待ちなし: キューにあるエントリを即時まとめる */
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

            /* 外側パケットを構築して送信 */
            flush_packed(ctx, packed_len);

            /* デキューした全エントリ分の inflight を減算 */
            {
                int i;
                for (i = 0; i < n_dequeued; i++)
                {
                    potr_internal_send_queue_complete(&ctx->send_queue);
                }
            }
        }
    }

    return;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_send_thread_start(potr_context *ctx)
{
    if (ctx == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    ctx->send_thread_running = 1;

    cplat_local_lock_create(&ctx->send_window_mutex);
    if (cplat_thread_create(&ctx->send_thread, send_thread_func, ctx) != CPLAT_OK)
    {
        ctx->send_thread_running = 0;
        cplat_local_lock_dispose(ctx->send_window_mutex);
        /* cplat のスレッド生成失敗には、porter の分類へ変換できる詳細コードがありません。 */
        return POTR_ERR_UNKNOWN;
    }

    return POTR_OK;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_send_thread_stop(potr_context *ctx)
{
    ctx->send_thread_running = 0;
    potr_internal_send_queue_shutdown(&ctx->send_queue);

    cplat_thread_join(ctx->send_thread, CPLAT_SYNC_WAIT_FOREVER);
    cplat_local_lock_dispose(ctx->send_window_mutex);
}
