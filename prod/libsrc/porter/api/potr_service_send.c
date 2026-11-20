/**
 *******************************************************************************
 *  @file           potr_service_send.c
 *  @brief          データを送信する potr_service_send 関数を提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/base/platform.h>
#include <cplat/crt/stdlib.h>
#include <stdlib.h>
#include <inttypes.h>

#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter/porter_spec.h>

#include <porter/potr_context.h>
#include <porter/potr_handle.h>
#include <porter/potr_path_event.h>
#include <porter/potr_peer_table.h>
#include <porter/infra/potr_send_queue.h>
#include <cplat/compress/compress.h>
#include <cplat/runtime/memory_lock.h>
#include <cplat/net/byteorder.h>
#include <porter/infra/potr_trace.h>

/*
 * 1 宛先へ 1 メッセージ分の断片を登録する (peers_mutex の取得は不要で、呼び出し元で検索済みである)。
 * 同一ハンドルへの並行送信で断片が他のメッセージと交互に並ばないよう、登録の間は push_mutex を保持する。
 * ブロッキング送信では、登録した最後の断片の番号まで送信済みになるのを待つ。
 * 送信スレッドは取り出した順に完了させるため、それ以前に登録された送信も送信済みになる。
 * 成功時は POTR_OK を返す。断片の登録に失敗したときは、その結果を返す。
 * ブロッキング送信で断片を送信できなかったときは、送信スレッドが記録した結果を返す。
 */
/* 1 エレメントに格納できるデータ長 [byte] を返す。エレメント ヘッダーと認証タグの分を除く。 */
static size_t elem_payload_capacity(const potr_context *ctx)
{
    size_t capacity = (size_t)ctx->global.max_payload - POTR_PAYLOAD_ELEM_HDR_SIZE;

    if (ctx->service.encrypt_enabled)
    {
        capacity -= POTR_CRYPTO_TAG_SIZE;
    }

    return capacity;
}

/* len [byte] のメッセージが占有する送信キューのエントリ数 (断片数) を返す。len は 1 以上である。 */
static size_t message_entry_count(const potr_context *ctx, size_t len)
{
    size_t capacity = elem_payload_capacity(ctx);

    return (len + capacity - 1U) / capacity;
}

/*
 * 1 メッセージ分の断片を送信キューへ登録する。呼び出し元は登録用ロックを保持する。
 * wait が 0 の場合、呼び出し元は全断片分の空きを確認済みである。登録用ロックの保持中は
 * 他の登録元が空きを減らさないため、確認後の登録は満杯で失敗しない。
 */
static int push_message(potr_context *ctx, potr_peer_id peer_id, const uint8_t *ptr, size_t len, uint16_t base_flags,
                        int wait, uint64_t *first_ticket, uint64_t *last_ticket)
{
    size_t remaining = len;
    size_t capacity = elem_payload_capacity(ctx);
    int result = POTR_OK;

    *first_ticket = 0U;
    *last_ticket = 0U;

    while (remaining > 0)
    {
        size_t chunk = remaining;
        uint16_t elem_flags = base_flags;

        if (chunk > capacity)
        {
            chunk = capacity;
            elem_flags |= POTR_FLAG_MORE_FRAG;
        }

        if (wait != 0)
        {
            result = potr_internal_send_queue_push_wait(&ctx->send_queue, peer_id, elem_flags, ptr, (uint16_t)chunk,
                                                        &ctx->send_thread_running, last_ticket);
        }
        else
        {
            result = potr_internal_send_queue_push(&ctx->send_queue, peer_id, elem_flags, ptr, (uint16_t)chunk,
                                                   last_ticket);
        }

        if (result != POTR_OK)
        {
            break;
        }

        if (*first_ticket == 0U)
        {
            *first_ticket = *last_ticket;
        }

        ptr += chunk;
        remaining -= chunk;
    }

    return result;
}

/** 抑制中のピアの状態を確認し直すまでの待機の上限 [ms]。 */
#define SEND_FLOW_RECHECK_MS 50

/*
 * N:1 モードで、宛先のピアが抑制中 (有効な抑制の要求がある、または受けた送出上限に達している) かどうかを返す。
 * deferred には、送信スレッドが保留しているエントリ数を返す。
 * 時刻を取得できない場合は期限を判定できないため、抑制中でないものとして扱う。
 */
static int peer_is_suppressed(potr_context *ctx, potr_peer_id peer_id, int *deferred)
{
    uint64_t now_ms;

    *deferred = 0;

    if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
    {
        return 0;
    }

    return potr_internal_flow_control_peer_state(ctx, peer_id, now_ms, deferred);
}

/*
 * N:1 モードで、抑制中のピア宛の送信要求を扱う。
 * 待ち合わせを指定しない送信は、送信キューの空きの有無にかかわらず POTR_ERR_FULL を返す。
 * 保留が送信キューを占有して、ほかのピア宛の送信を止めないようにするためである。
 * 待ち合わせを指定した送信は、抑制の解除、ピアの切断、close の開始のいずれかまで待つ。
 * wait_deferred が非 0 の場合は、そのピア宛の保留エントリがなくなるまで待つ (送出の完了待ち)。
 * 抑制の要求または送出上限を保持しているピアがない場合 (wait_deferred が 0) と、送信スレッドが保留している
 * エントリがない場合 (wait_deferred が非 0) は、時刻とロックを取得せずに戻る。
 */
static int wait_peer_sendable(potr_context *ctx, potr_peer_id peer_id, int blocking, int wait_deferred)
{
    uint64_t generation = 0U;

    if (wait_deferred != 0)
    {
        /* 送信スレッドは、完了待ちの番号を進める前に保留の総数へ加えるため、完了待ちの後に 0 なら保留はない。 */
        if (cplat_atomic_load_i32(&ctx->flow_deferred_total, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
        {
            return POTR_OK;
        }
    }
    else if (potr_internal_flow_control_peers_idle(ctx) != 0)
    {
        return POTR_OK;
    }

    for (;;)
    {
        int deferred;
        int suppressed = peer_is_suppressed(ctx, peer_id, &deferred);

        if (wait_deferred != 0)
        {
            if (deferred == 0)
            {
                return POTR_OK;
            }
        }
        else if (suppressed == 0)
        {
            return POTR_OK;
        }
        else if (blocking == 0)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                       "potr_service_send: service_id=%" PRId64 " peer_id=%u suppressed by flow control",
                       ctx->service.service_id, (unsigned)peer_id);
            return POTR_ERR_FULL;
        }

        if (cplat_atomic_load_i32(&ctx->send_thread_running, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
        {
            return POTR_ERR_CANCELED;
        }

        {
            int fatal = (int)cplat_atomic_load_i32(&ctx->fatal_error, CPLAT_MEMORY_ORDER_ACQUIRE);

            if (fatal != POTR_OK)
            {
                return fatal;
            }
        }

        potr_internal_send_queue_wait_flow(&ctx->send_queue, &generation, SEND_FLOW_RECHECK_MS);
    }
}

/*
 * 1 つの宛先へメッセージを送信する。
 * 待ち合わせを指定しない送信は、全断片分の空きがなければ 1 つも登録せずに POTR_ERR_FULL を返す。
 * 待ち合わせを指定した送信は、空きを待って登録し、最後の断片の送出の完了まで待つ。
 */
static int send_to_peer(potr_context *ctx, potr_peer_id peer_id, const uint8_t *ptr, size_t len, int flags,
                        uint16_t base_flags)
{
    uint64_t first_ticket = 0U;
    uint64_t last_ticket = 0U;
    int result;
    /* flags はビット フラグであり、POTR_SEND_* 定数は int に収まる。 */
    int blocking = (((unsigned)flags & POTR_SEND_BLOCKING) != 0U);

    if (ctx->is_multi_peer)
    {
        result = wait_peer_sendable(ctx, peer_id, blocking, 0);
        if (result != POTR_OK)
        {
            return result;
        }
    }

    potr_internal_send_queue_lock_push(&ctx->send_queue);

    /*
     * 待ち合わせを指定しない送信は、全断片分の空きを先に確かめる。断片が 1 つの場合は、
     * potr_internal_send_queue_push() が空きの確認と登録を 1 回の排他で行うため、確認を省く。
     */
    if (blocking != 0 || len <= elem_payload_capacity(ctx))
    {
        result = POTR_OK;
    }
    else
    {
        result = potr_internal_send_queue_check_space(&ctx->send_queue, message_entry_count(ctx, len));
    }

    if (result == POTR_OK)
    {
        result = push_message(ctx, peer_id, ptr, len, base_flags, blocking, &first_ticket, &last_ticket);
    }

    potr_internal_send_queue_unlock_push(&ctx->send_queue);

    if (result != POTR_OK)
    {
        return result;
    }

    if (blocking != 0)
    {
        potr_internal_send_queue_wait_ticket(&ctx->send_queue, last_ticket);
        result = (int)cplat_atomic_load_i32(&ctx->fatal_error, CPLAT_MEMORY_ORDER_ACQUIRE);
        if (result != POTR_OK)
        {
            return result;
        }

        result = potr_internal_send_queue_ticket_result(&ctx->send_queue, first_ticket);

        /* 送信スレッドが保留したエントリは、完了待ちの番号だけが先に進む。送出されるまで待つ。 */
        if (result == POTR_OK && ctx->is_multi_peer)
        {
            result = wait_peer_sendable(ctx, peer_id, blocking, 1);
        }

        return result;
    }

    return POTR_OK;
}

/*
 * 待ち合わせを指定しない全ピア宛の送信を行う。
 * 全ピア分の断片を登録できる空きがなければ、どのピア宛も登録せずに POTR_ERR_FULL を返す。
 */
static int send_to_peers_nonblocking(potr_context *ctx, const potr_peer_id *ids, int n_ids, const uint8_t *ptr,
                                     size_t len, uint16_t base_flags)
{
    uint64_t first_ticket;
    uint64_t last_ticket;
    int result;
    int i;

    /* 抑制中のピアが 1 つでもあれば、どのピア宛も登録しない。 */
    for (i = 0; i < n_ids; i++)
    {
        result = wait_peer_sendable(ctx, ids[i], 0, 0);
        if (result != POTR_OK)
        {
            return result;
        }
    }

    potr_internal_send_queue_lock_push(&ctx->send_queue);

    /* n_ids は max_peers 以下の正の値であり、size_t に収まる。 */
    result = potr_internal_send_queue_check_space(&ctx->send_queue, message_entry_count(ctx, len) * (size_t)n_ids);

    for (i = 0; result == POTR_OK && i < n_ids; i++)
    {
        result = push_message(ctx, ids[i], ptr, len, base_flags, 0, &first_ticket, &last_ticket);
    }

    potr_internal_send_queue_unlock_push(&ctx->send_queue);

    return result;
}

static int route_and_send(potr_context *ctx, potr_peer_id peer_id, const uint8_t *ptr, size_t len, int flags,
                          uint16_t base_flags);

static int send_impl(potr_context *handle, potr_peer_id peer_id, const void *data, size_t len, int flags)
{
    potr_context *ctx = (potr_context *)handle;
    const uint8_t *ptr = (const uint8_t *)data;
    uint16_t base_flags = 0;
    unsigned max_message_size;
    uint8_t *compress_buf = NULL;
    int result;

    if (ctx == NULL || data == NULL || len == 0 || len > (size_t)ctx->global.max_message_size)
    {
        if (ctx != NULL)
        {
            max_message_size = (unsigned)ctx->global.max_message_size;
        }
        else
        {
            max_message_size = 0U;
        }

        POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_service_send: invalid argument (handle=%p data=%p len=%zu max=%u)",
                   (const void *)handle, data, len, max_message_size);
        return POTR_ERR_INVALID_ARGUMENT;
    }

    POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "potr_service_send: service_id=%" PRId64 " peer_id=%u len=%zu flags=0x%x",
               ctx->service.service_id, (unsigned)peer_id, len, (unsigned)flags);

    if (cplat_atomic_load_i32(&ctx->close_requested, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                   "potr_service_send: service_id=%" PRId64 " rejected because close is in progress",
                   ctx->service.service_id);
        return POTR_ERR_CANCELED;
    }

    /* 片方向の受信専用サービスは送信キューを持たない。 */
    if (ctx->role == POTR_ROLE_RECEIVER && ctx->service.type != POTR_TYPE_UNICAST_BIDIR &&
        ctx->service.type != POTR_TYPE_UNICAST_BIDIR_N1 && ctx->service.type != POTR_TYPE_TCP_BIDIR)
    {
        return POTR_ERR_UNSUPPORTED;
    }

    if (ctx->service.auth == POTR_AUTH_CERT && !ctx->is_multi_peer)
    {
        potr_packet packet = {0};
        uint8_t key[POTR_CRYPTO_KEY_SIZE] = {0};
        int ready;

        packet.flags = cplat_hton16(POTR_FLAG_DATA);
        ready = potr_internal_certificate_key(ctx, &ctx->tx_session, &packet, 1, key);
        cplat_secure_zero(key, sizeof(key));
        if (ready != POTR_OK)
        {
            return POTR_ERR_DISCONNECTED;
        }
    }

    /*
     * TCP: 物理 path 未接続、または PING 交換による論理 CONNECTED 前は
     * POTR_ERR_DISCONNECTED を返す。
     */
    if (is_tcp_type(ctx->service.type) &&
        (cplat_atomic_load_i32(&ctx->tcp_active_paths, CPLAT_MEMORY_ORDER_ACQUIRE) == 0 ||
         cplat_atomic_load_i32(&ctx->health_alive, CPLAT_MEMORY_ORDER_ACQUIRE) == 0))
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                   "potr_service_send: service_id=%" PRId64 " TCP not connected"
                   " (active_paths=%d health_alive=%d)",
                   ctx->service.service_id,
                   (int)cplat_atomic_load_i32(&ctx->tcp_active_paths, CPLAT_MEMORY_ORDER_RELAXED),
                   (int)cplat_atomic_load_i32(&ctx->health_alive, CPLAT_MEMORY_ORDER_RELAXED));
        return POTR_ERR_DISCONNECTED;
    }

    /* UDP の 1:1 双方向では、PING 交換による接続確立前は POTR_ERR_DISCONNECTED を返す。 */
    if (ctx->service.type == POTR_TYPE_UNICAST_BIDIR &&
        cplat_atomic_load_i32(&ctx->health_alive, CPLAT_MEMORY_ORDER_ACQUIRE) == 0)
    {
        POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "potr_service_send: service_id=%" PRId64 " UDP bidir not connected",
                   ctx->service.service_id);
        return POTR_ERR_DISCONNECTED;
    }

    /* RAW モードでは常にブロッキング送信を行う。 */
    if (is_raw_type(ctx->service.type))
    {
        flags |= (int)POTR_SEND_BLOCKING; /* POTR_SEND_BLOCKING は int に収まる */
    }

    /*
     * 圧縮が要求された場合はペイロード全体を圧縮する。
     * 同一ハンドルへの並行送信で圧縮結果が混ざらないよう、圧縮先は呼び出しごとに確保する。
     * 圧縮結果は登録時にキューへ複製されるため、登録の完了後に消去して解放できる。
     */
    if (((unsigned)flags & POTR_SEND_COMPRESS) != 0U)
    {
        size_t cmp_len = ctx->compress_buf_size;
        int cmp_ret;

        compress_buf = (uint8_t *)cplat_malloc(ctx->compress_buf_size);
        if (compress_buf == NULL)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "potr_service_send: service_id=%" PRId64 " compress buffer malloc failed (size=%zu)",
                       ctx->service.service_id, ctx->compress_buf_size);
            return POTR_ERR_OUT_OF_MEMORY;
        }

        cmp_ret = cplat_compress(compress_buf, &cmp_len, (const uint8_t *)data, len);
        if (cmp_ret == CPLAT_OK && cmp_len < len)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE, "potr_service_send: service_id=%" PRId64 " compress %zu -> %zu bytes",
                       ctx->service.service_id, len, cmp_len);
            ptr = compress_buf;
            len = cmp_len;
            base_flags = POTR_FLAG_COMPRESSED;
        }
        else if (cmp_ret == CPLAT_OK || cmp_ret == CPLAT_ERR_BUFFER_TOO_SMALL)
        {
            /*
             * 入口で len <= max_message_size を検証済みで compress_buf_size == max_message_size のため、
             * CPLAT_ERR_BUFFER_TOO_SMALL は圧縮後サイズが元データ長以上であることを意味する。
             * 圧縮が有利でないため、元のデータと長さのまま非圧縮で送る。
             */
            POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                       "potr_service_send: service_id=%" PRId64 " compression skipped"
                       " (result=%d compressed=%zu original=%zu bytes), sending uncompressed",
                       ctx->service.service_id, cmp_ret, cmp_len, len);
        }
        else
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "potr_service_send: service_id=%" PRId64 " compression failed (len=%zu result=%d)",
                       ctx->service.service_id, len, cmp_ret);
            cplat_secure_zero(compress_buf, ctx->compress_buf_size);
            cplat_free(compress_buf);
            return POTR_ERR_UNKNOWN; /* 圧縮失敗は入力データ起因と断定できないため、分類不能として扱う */
        }
    }

    result = route_and_send(ctx, peer_id, ptr, len, flags, base_flags);

    if (compress_buf != NULL)
    {
        cplat_secure_zero(compress_buf, ctx->compress_buf_size);
        cplat_free(compress_buf);
    }

    return result;
}

/* 宛先を決めて送信キューへ登録する。N:1 では peer_id でピアを選び、1:1 では peer_id を無視する。 */
static int route_and_send(potr_context *ctx, potr_peer_id peer_id, const uint8_t *ptr, size_t len, int flags,
                          uint16_t base_flags)
{
    /* N:1 モードでは、peer_id に基づいてルーティングする。 */
    if (ctx->is_multi_peer)
    {
        if (peer_id == POTR_PEER_NA)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                       "potr_service_send: service_id=%" PRId64 " N:1 mode requires valid peer_id (got POTR_PEER_NA)",
                       ctx->service.service_id);
            return POTR_ERR_INVALID_ARGUMENT;
        }

        if (peer_id == POTR_PEER_ALL)
        {
            /*
             * 全アクティブ ピアへ送信する。
             * peers_mutex を保持しない状態で送信するため、先に peer_id のリストを収集してからキューへ格納する。
             */
            potr_peer_id *ids;
            int n_ids = 0;
            int i;
            int result = POTR_OK;

            ids = (potr_peer_id *)cplat_malloc((size_t)ctx->max_peers * sizeof(potr_peer_id));
            if (ids == NULL)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_service_send: service_id=%" PRId64 " PEER_ALL malloc failed",
                           ctx->service.service_id);
                return POTR_ERR_OUT_OF_MEMORY;
            }

            cplat_local_lock_lock(ctx->peers_mutex);

            for (i = 0; i < ctx->max_peers; i++)
            {
                if (ctx->peers[i].active &&
                    cplat_atomic_load_i32(&ctx->peers[i].health_alive, CPLAT_MEMORY_ORDER_ACQUIRE) != 0)
                {
                    ids[n_ids++] = ctx->peers[i].peer_id;
                }
            }

            cplat_local_lock_unlock(ctx->peers_mutex);

            if (n_ids == 0)
            {
                cplat_free(ids);
                POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                           "potr_service_send: service_id=%" PRId64 " PEER_ALL not connected", ctx->service.service_id);
                return POTR_ERR_DISCONNECTED;
            }

            /* flags はビット フラグであり、POTR_SEND_* 定数は int に収まる。 */
            if (((unsigned)flags & POTR_SEND_BLOCKING) == 0U)
            {
                result = send_to_peers_nonblocking(ctx, ids, n_ids, ptr, len, base_flags);
                cplat_free(ids);
                return result;
            }

            for (i = 0; i < n_ids; i++)
            {
                int send_result = send_to_peer(ctx, ids[i], ptr, len, flags, base_flags);

                /* 最初の失敗を結果に残し、残りのピアへの登録は続ける。 */
                if (result == POTR_OK && send_result != POTR_OK)
                {
                    result = send_result;
                }
            }

            cplat_free(ids);
            return result;
        }
        else
        {
            /* 指定ピアへ送信する。存在確認と接続確認だけを mutex で保護し、送信は mutex の外で行う。 */
            int peer_alive;
            cplat_local_lock_lock(ctx->peers_mutex);

            {
                potr_internal_peer_context *peer = potr_internal_peer_find_by_id(ctx, peer_id);
                if (peer == NULL)
                {
                    cplat_local_lock_unlock(ctx->peers_mutex);
                    POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR,
                               "potr_service_send: service_id=%" PRId64 " peer_id=%u not found",
                               ctx->service.service_id, (unsigned)peer_id);
                    return POTR_ERR_NOT_FOUND;
                }

                peer_alive = cplat_atomic_load_i32(&peer->health_alive, CPLAT_MEMORY_ORDER_ACQUIRE);
            }

            cplat_local_lock_unlock(ctx->peers_mutex);

            if (!peer_alive)
            {
                POTR_TRACE(CPLAT_TRACE_LEVEL_VERBOSE,
                           "potr_service_send: service_id=%" PRId64 " peer_id=%u N:1 not connected",
                           ctx->service.service_id, (unsigned)peer_id);
                return POTR_ERR_DISCONNECTED;
            }

            return send_to_peer(ctx, peer_id, ptr, len, flags, base_flags);
        }
    }

    /* 1:1 モードでは、peer_id を無視する。 */
    return send_to_peer(ctx, POTR_PEER_NA, ptr, len, flags, base_flags);
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_service_send(potr_context *handle, potr_peer_id peer_id, const void *data, size_t len, int flags)
{
    potr_context *ctx = NULL;
    int ret;

    if (handle == NULL)
    {
        return send_impl(NULL, peer_id, data, len, flags);
    }

    ret = potr_internal_handle_acquire(handle, &ctx);
    if (ret != POTR_OK)
    {
        return ret;
    }

    ret = send_impl(ctx, peer_id, data, len, flags);
    potr_internal_handle_release(ctx);

    return ret;
}
