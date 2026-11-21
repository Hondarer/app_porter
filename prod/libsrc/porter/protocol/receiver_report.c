/**
 *******************************************************************************
 *  @file           receiver_report.c
 *  @brief          受信報告の期限と返送元の上限を管理します。
 *
 *******************************************************************************
 */
#include <string.h>
#include <inttypes.h>
#include <cplat/crt/stdlib.h>
#include <cplat/net/byteorder.h>
#include <porter/potr_context.h>
#include <porter/porter_result.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/receiver_report.h>
#include <porter/infra/potr_trace.h>

/* Doxygen コメントは、ヘッダーに記載 */

uint8_t potr_internal_receiver_groups_locked(potr_context *ctx, uint64_t now_ms)
{
    potr_internal_receiver *receiver;
    uint8_t groups = (uint8_t)((1U << POTR_MAX_PATH_GROUP) - 1U);
    int active = 0;

    if (ctx == NULL)
    {
        return 0U;
    }
    for (receiver = ctx->receivers; receiver != NULL; receiver = receiver->next)
    {
        uint8_t receiver_groups = 0U;
        int i;

        if (receiver->active && ctx->health_timeout_ms != 0U &&
            now_ms - receiver->last_report_ms >= ctx->health_timeout_ms)
        {
            receiver->active = 0;
            receiver->retired_ms = receiver->last_report_ms + ctx->health_timeout_ms;
            receiver->flow_unexclude_pending = 0;
            receiver->flow_unexclude_since_ms = 0U;
            if (receiver->flow_excluded)
            {
                /* 期限切れを検出した時点で解き、同一セッションの REPORT の再開時も除外を引き継がせない。 */
                receiver->flow_excluded = 0;
                receiver->flow_stall_accum_units = 0U;
                POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                           "flow_control[service_id=%" PRId64 "]: receiver session=%u unexcluded (report expired)",
                           ctx->service.service_id, (unsigned)receiver->session_id);
            }
        }
        if (!receiver->active)
        {
            continue;
        }
        active = 1;
        for (i = 0; i < ctx->n_path; i++)
        {
            uint8_t group = ctx->service.path_group[i];

            if (receiver->path_state[i] == POTR_PING_STATE_NORMAL && group >= 1U && group <= POTR_MAX_PATH_GROUP)
            {
                receiver_groups |= (uint8_t)(1U << (group - 1U));
            }
        }
        groups &= receiver_groups;
    }
    if (!active)
    {
        return 0U;
    }
    return groups;
}

/* multicast と broadcast の送信者かどうかを返す。監視対象を選ぶのは、この送信者だけである。 */
static int is_monitoring_sender(const potr_context *ctx)
{
    return ctx->role == POTR_ROLE_SENDER &&
           (ctx->service.type == POTR_TYPE_MULTICAST || ctx->service.type == POTR_TYPE_BROADCAST);
}

/*
 * 次に割り当てる DATA の通番を読む。send_window は送信スレッドが更新するため、send_window_mutex の下で読む。
 * send_window_mutex は送信スレッドの起動時に生成し、生成前は並行して更新するスレッドがないため、NULL の場合はそのまま読む。
 * callback_mutex を保持せずに呼ぶ (send_window_mutex と callback_mutex を重ねて保持しない)。
 */
static uint32_t read_send_next_seq(const potr_context *ctx)
{
    uint32_t next_seq;

    if (ctx->send_window_mutex == NULL)
    {
        return ctx->send_window.next_seq;
    }

    cplat_local_lock_lock(ctx->send_window_mutex);
    next_seq = ctx->send_window.next_seq;
    cplat_local_lock_unlock(ctx->send_window_mutex);

    return next_seq;
}

/*
 * フロー制御の判定で活動中として数える受信者かどうかを返す。受信報告によって活動中と判定している受信者と、
 * 有効な抑制の要求を送ってきている受信者である。callback_mutex を保持して呼ぶ。
 */
static int receiver_counted_active(const potr_internal_receiver *receiver, uint64_t now_ms)
{
    return receiver->active || receiver->flow_request_expire_ms > now_ms;
}

/*
 * 監視対象を選び直す。余裕の記録がある活動中の受信者のうち、余裕が小さいほうから監視する数の受信者を監視対象にする。
 * 遅い受信者から選ぶのは、送出を決めるのが、遅いほうから「しきい値」番目の受信者であるためである。
 * それより速い監視対象は、速さの順位が入れ替わったときに、次の「しきい値」番目の受信者の送出上限が
 * すでに届いているようにするための予備である。
 * 余裕が同じ受信者は、管理表の先頭に近いほうを先に選ぶ。監視対象から外れた受信者の送出上限は破棄する。
 * 監視対象が変わった場合は ctx->flow_monitor_changed を 1 にする。callback_mutex を保持して呼ぶ。
 */
static void monitor_select_locked(potr_context *ctx, uint64_t now_ms)
{
    potr_internal_receiver *receiver;
    size_t candidate_count = 0U;
    size_t target;
    size_t picked;
    size_t index;
    int32_t cut_margin = 0;
    size_t cut_index = 0U;
    int have_cut = 0;

    if (!is_monitoring_sender(ctx))
    {
        return;
    }

    for (receiver = ctx->receivers; receiver != NULL; receiver = receiver->next)
    {
        if (!receiver->flow_excluded && receiver_counted_active(receiver, now_ms) && receiver->flow_margin_known)
        {
            candidate_count++;
        }
    }

    target = flow_monitor_count(candidate_count, ctx->flow_control_config.monitored_receiver_count,
                                ctx->global.max_payload);

    /*
     * (余裕, 管理表の位置) の順で、小さいほうから target 番目の候補を求める。
     * 監視する数は POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX 以下のため、走査の回数は受信者数の 64 倍以下である。
     */
    for (picked = 0U; picked < target; picked++)
    {
        int found = 0;
        int32_t best_margin = 0;
        size_t best_index = 0U;

        for (receiver = ctx->receivers, index = 0U; receiver != NULL; receiver = receiver->next, index++)
        {
            if (receiver->flow_excluded || !receiver_counted_active(receiver, now_ms) || !receiver->flow_margin_known)
            {
                continue;
            }

            if (have_cut && (receiver->flow_margin < cut_margin ||
                             (receiver->flow_margin == cut_margin && index <= cut_index)))
            {
                continue;
            }

            if (!found || receiver->flow_margin < best_margin)
            {
                found = 1;
                best_margin = receiver->flow_margin;
                best_index = index;
            }
        }

        if (!found)
        {
            break;
        }

        cut_margin = best_margin;
        cut_index = best_index;
        have_cut = 1;
    }

    for (receiver = ctx->receivers, index = 0U; receiver != NULL; receiver = receiver->next, index++)
    {
        int selected = have_cut && !receiver->flow_excluded && receiver_counted_active(receiver, now_ms) &&
                       receiver->flow_margin_known &&
                       (receiver->flow_margin < cut_margin ||
                        (receiver->flow_margin == cut_margin && index <= cut_index));

        if (selected == receiver->flow_monitored)
        {
            continue;
        }

        receiver->flow_monitored = selected;
        ctx->flow_monitor_changed = 1;

        if (selected)
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                       "flow_control[service_id=%" PRId64 "]: receiver session=%u selected as monitored (margin=%d)",
                       ctx->service.service_id, (unsigned)receiver->session_id, (int)receiver->flow_margin);
        }
        else
        {
            /* 監視対象から外れた受信者の送出上限は、送信者が使わないため破棄する。 */
            receiver->flow_send_limit = 0U;
            receiver->flow_limit_stalled_ms = 0U;
            POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                       "flow_control[service_id=%" PRId64 "]: receiver session=%u is no longer monitored",
                       ctx->service.service_id, (unsigned)receiver->session_id);
        }
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_receiver_accept(potr_context *ctx, const potr_packet *packet, const uint8_t *wire_header)
{
    potr_internal_receiver *receiver;
    potr_internal_receiver *oldest = NULL;
    uint64_t now_ms;
    uint16_t wire_flags;
    int report;
    int flow;
    int monitoring;
    int is_new_receiver = 0;
    int need_query = 0;
    int ret = POTR_OK;
    uint32_t next_seq = 0U;
    uint8_t flow_state = 0U;
    uint32_t flow_send_limit = 0U;
    size_t count = 0U;
    size_t limit;
    size_t i;

    if (ctx == NULL || packet == NULL || wire_header == NULL)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }
    report = packet->flags == POTR_FLAG_REPORT;
    flow = packet->flags == POTR_FLAG_FLOW_CONTROL;
    limit = ctx->service.max_peers;
    if (limit == 0U)
    {
        limit = POTR_DEFAULT_MAX_PEERS;
    }
    if (!report && !flow && packet->flags != POTR_FLAG_NACK)
    {
        return POTR_ERR_PROTOCOL;
    }
    /* ペイロードの形式が不正なフロー制御パケットと、受信側向けの問い合わせは破棄する。 */
    if (flow && (flow_control_payload_decode(packet, &flow_state, &flow_send_limit) != POTR_OK ||
                 flow_state == POTR_FLOW_CONTROL_STATE_QUERY))
    {
        return POTR_ERR_PROTOCOL;
    }
    if (report)
    {
        /* seq_num は送出上限の有無 (1 = ack_num に送出上限がある、0 = ない)。ない場合の ack_num は 0 である。 */
        if (packet->payload == NULL || packet->payload_len != POTR_MAX_PATH || packet->seq_num > 1U ||
            (packet->seq_num == 0U && packet->ack_num != 0U))
        {
            return POTR_ERR_PROTOCOL;
        }
        for (i = 0U; i < POTR_MAX_PATH; i++)
        {
            if (packet->payload[i] != POTR_PING_STATE_UNDEFINED && packet->payload[i] != POTR_PING_STATE_NORMAL &&
                packet->payload[i] != POTR_PING_STATE_ABNORMAL)
            {
                return POTR_ERR_PROTOCOL;
            }
        }
    }
    if (cplat_clock_get_monotonic_ms(&now_ms) != CPLAT_OK)
    {
        return POTR_ERR_IO;
    }

    /* 余裕や未通知の基準に使う次に割り当てる DATA の通番を、callback_mutex を取得する前に読む。 */
    monitoring = is_monitoring_sender(ctx);
    if ((monitoring && report && packet->seq_num == 1U) || (!monitoring && ctx->service.type == POTR_TYPE_UNICAST))
    {
        next_seq = read_send_next_seq(ctx);
    }
    cplat_local_lock_lock(ctx->callback_mutex);
    /* 期限切れの受信者を無効にする副作用だけを使い、到達できるグループの集合はここでは使わないため、戻り値は明示的に破棄する。 */
    (void)potr_internal_receiver_groups_locked(ctx, now_ms);
    for (receiver = ctx->receivers; receiver != NULL; receiver = receiver->next)
    {
        if (receiver->session_id == packet->session_id && receiver->session_tv_sec == packet->session_tv_sec &&
            receiver->session_tv_nsec == packet->session_tv_nsec)
        {
            break;
        }
        if (!receiver->active && (oldest == NULL || receiver->retired_ms < oldest->retired_ms))
        {
            oldest = receiver;
        }
        count++;
    }
    if (ctx->service.auth == POTR_AUTH_CERT && (receiver == NULL || !receiver->certificate.established))
    {
        cplat_local_lock_unlock(ctx->callback_mutex);
        return POTR_ERR_PROTOCOL;
    }
    if (receiver == NULL)
    {
        if (count < limit)
        {
            receiver = (potr_internal_receiver *)cplat_calloc(1U, sizeof(*receiver));
            if (receiver != NULL)
            {
                receiver->next = ctx->receivers;
                ctx->receivers = receiver;
            }
        }
        else if (oldest != NULL)
        {
            potr_internal_receiver *next = oldest->next;

            /* 監視対象だった項目を再利用する場合は、監視対象が変わったものとして一覧を送出し直す。 */
            if (oldest->flow_monitored)
            {
                ctx->flow_monitor_changed = 1;
            }

            receiver = oldest;
            memset(receiver, 0, sizeof(*receiver));
            receiver->next = next;
        }
        else
        {
            POTR_TRACE(CPLAT_TRACE_LEVEL_WARNING,
                       "report[service_id=%" PRId64 "]: max_peers=%zu reached, dropping receiver session=%u",
                       ctx->service.service_id, limit, (unsigned)packet->session_id);
            cplat_local_lock_unlock(ctx->callback_mutex);
            return POTR_ERR_FULL;
        }
        if (receiver == NULL)
        {
            cplat_local_lock_unlock(ctx->callback_mutex);
            return POTR_ERR_OUT_OF_MEMORY;
        }
        receiver->retired_ms = now_ms;
        receiver->session_id = packet->session_id;
        receiver->session_tv_sec = packet->session_tv_sec;
        receiver->session_tv_nsec = packet->session_tv_nsec;
        is_new_receiver = 1;
    }
    memcpy(&wire_flags, wire_header + POTR_PACKET_FLAGS_OFFSET, sizeof(wire_flags));
    if ((cplat_ntoh16(wire_flags) & POTR_FLAG_ENCRYPTED) != 0U)
    {
        ret = potr_internal_replay_window_accept(&receiver->replay_window, packet);
    }
    if (ret == POTR_OK)
    {
        int handle_was_no_peer = 0;
        int handle_was_unnotified = 0;
        int report_applied_limit = 0;
        uint32_t handle_base_seq = 0U;

        receiver->flow_last_packet_ms = now_ms;
        cplat_atomic_store_u64(&ctx->flow_last_packet_ms, now_ms, CPLAT_MEMORY_ORDER_RELEASE);

        if (!monitoring && ctx->service.type == POTR_TYPE_UNICAST)
        {
            uint64_t prev_handle = cplat_atomic_load_u64(&ctx->flow_send_limit, CPLAT_MEMORY_ORDER_ACQUIRE);
            uint64_t prev_state = prev_handle & POTR_FLOW_SEND_LIMIT_STATE_MASK;

            handle_was_no_peer = prev_state == POTR_FLOW_SEND_LIMIT_NO_PEER;
            handle_was_unnotified = prev_state == POTR_FLOW_SEND_LIMIT_UNNOTIFIED;
            handle_base_seq = (uint32_t)(prev_handle & 0xFFFFFFFFU);

            /* 受信者の管理表で追跡を始めた後は、ハンドル側の初期の幅を解除する。 */
            cplat_atomic_store_u64(&ctx->flow_send_limit, POTR_FLOW_SEND_LIMIT_UNLIMITED, CPLAT_MEMORY_ORDER_RELEASE);
        }

        if (report)
        {
            memcpy(receiver->path_state, packet->payload, POTR_MAX_PATH);
            receiver->last_report_ms = now_ms;
            receiver->active = 1;
            receiver->retired_ms = 0U;
            ctx->report_received = 1;

            /*
             * multicast と broadcast では、受信報告の送出上限で余裕を更新して監視対象を選び直す。
             * 監視対象が保持している送出上限より先へ進んでいる場合は、受信報告による回復として更新する。
             * unicast では、未通知または相手なしの受信者の最初の通知として受理し、保持中なら先へ進んでいる場合だけ更新する。
             */
            if (monitoring)
            {
                if (packet->seq_num == 1U)
                {
                    /* 通番は循環するため、差を符号付きで見る。除外している受信者についても絶対値を更新する。 */
                    receiver->flow_margin = (int32_t)(packet->ack_num - next_seq);
                    receiver->flow_margin_known = 1;
                    receiver->flow_report_send_limit = packet->ack_num;
                    receiver->flow_report_limit_known = 1;
                    if (receiver->flow_margin <= 0)
                    {
                        receiver->flow_unexclude_pending = 0;
                        receiver->flow_unexclude_since_ms = 0U;
                    }
                }
                else
                {
                    receiver->flow_margin_known = 0;
                    receiver->flow_report_send_limit = 0U;
                    receiver->flow_report_limit_known = 0;
                    receiver->flow_unexclude_pending = 0;
                    receiver->flow_unexclude_since_ms = 0U;
                }

                monitor_select_locked(ctx, now_ms);

                if (packet->seq_num == 1U && receiver->flow_monitored != 0 &&
                    (receiver->flow_send_limit & POTR_FLOW_SEND_LIMIT_STATE_MASK) == POTR_FLOW_SEND_LIMIT_HELD)
                {
                    uint32_t held = (uint32_t)(receiver->flow_send_limit & 0xFFFFFFFFU);

                    if ((int32_t)(packet->ack_num - held) > 0)
                    {
                        receiver->flow_send_limit = POTR_FLOW_SEND_LIMIT_HELD | (uint64_t)packet->ack_num;
                        receiver->flow_limit_received_ms = now_ms;
                        cplat_atomic_store_i32(&ctx->flow_request_pending, 1, CPLAT_MEMORY_ORDER_RELEASE);
                    }
                }
            }
            else if (ctx->service.type == POTR_TYPE_UNICAST && packet->seq_num == 1U)
            {
                uint64_t recv_state = receiver->flow_send_limit & POTR_FLOW_SEND_LIMIT_STATE_MASK;

                if (is_new_receiver || recv_state == POTR_FLOW_SEND_LIMIT_UNNOTIFIED ||
                    recv_state == POTR_FLOW_SEND_LIMIT_NO_PEER)
                {
                    receiver->flow_send_limit = POTR_FLOW_SEND_LIMIT_HELD | (uint64_t)packet->ack_num;
                    receiver->flow_limit_received_ms = now_ms;
                    receiver->flow_limit_stalled_ms = 0U;
                    cplat_atomic_store_i32(&ctx->flow_request_pending, 1, CPLAT_MEMORY_ORDER_RELEASE);
                    report_applied_limit = 1;
                }
                else if (recv_state == POTR_FLOW_SEND_LIMIT_HELD)
                {
                    uint32_t held = (uint32_t)(receiver->flow_send_limit & 0xFFFFFFFFU);

                    if ((int32_t)(packet->ack_num - held) > 0)
                    {
                        receiver->flow_send_limit = POTR_FLOW_SEND_LIMIT_HELD | (uint64_t)packet->ack_num;
                        receiver->flow_limit_received_ms = now_ms;
                        cplat_atomic_store_i32(&ctx->flow_request_pending, 1, CPLAT_MEMORY_ORDER_RELEASE);
                    }
                }
            }
        }
        else if (flow)
        {
            int was_monitored = receiver->flow_monitored;

            if (flow_state == POTR_FLOW_CONTROL_STATE_UNLIMITED)
            {
                receiver->flow_request_expire_ms = 0U;
                receiver->flow_send_limit = monitoring ? 0U : POTR_FLOW_SEND_LIMIT_UNLIMITED;
                receiver->flow_limit_stalled_ms = 0U;
                receiver->flow_limit_received_ms = now_ms;
            }
            else
            {
                if (flow_state == POTR_FLOW_CONTROL_STATE_SUPPRESS)
                {
                    receiver->flow_request_expire_ms = now_ms + ctx->flow_control_config.request_lifetime_ms;
                }
                else
                {
                    receiver->flow_request_expire_ms = 0U;
                }

                /*
                 * 送出上限は、状態の値によらず上書きする。通番で表すため、重複して届いても判定は変わらない。
                 * multicast と broadcast では、監視対象の受信者の送出上限だけを保持する。
                 */
                if (!monitoring || was_monitored)
                {
                    receiver->flow_send_limit = POTR_FLOW_SEND_LIMIT_HELD | (uint64_t)flow_send_limit;
                    receiver->flow_limit_received_ms = now_ms;
                }

                /*
                 * 監視対象でない受信者から抑制の要求を受けた場合は、受信報告を待たずに候補にするため、余裕を 0 として記録する。
                 * 次の受信報告で上書きする。
                 */
                if (monitoring)
                {
                    if (!was_monitored && flow_state == POTR_FLOW_CONTROL_STATE_SUPPRESS)
                    {
                        receiver->flow_margin = 0;
                        receiver->flow_margin_known = 1;
                    }

                    monitor_select_locked(ctx, now_ms);
                }
            }
        }

        if (!monitoring && !flow && !report_applied_limit && ctx->service.type == POTR_TYPE_UNICAST)
        {
            uint64_t recv_state = receiver->flow_send_limit & POTR_FLOW_SEND_LIMIT_STATE_MASK;

            if (is_new_receiver || recv_state == POTR_FLOW_SEND_LIMIT_NO_PEER)
            {
                /*
                 * ハンドル側が未通知の間に最初のパケットを受けた場合は、セッションの最初の通番を引き継ぎ、
                 * 最初の通知までに初期の幅を超えないようにする。ハンドル側が相手なしだった場合と、
                 * 相手なしになった項目が戻る場合は、その時点で次に割り当てる通番を基準にする。
                 */
                uint32_t base_seq = (is_new_receiver && handle_was_unnotified) ? handle_base_seq : next_seq;

                receiver->flow_send_limit = POTR_FLOW_SEND_LIMIT_UNNOTIFIED | (uint64_t)base_seq;
                receiver->flow_limit_received_ms = now_ms;
                receiver->flow_limit_stalled_ms = 0U;
                cplat_atomic_store_i32(&ctx->flow_request_pending, 1, CPLAT_MEMORY_ORDER_RELEASE);
                if (!is_new_receiver || handle_was_no_peer)
                {
                    need_query = 1;
                }
            }
        }
    }
    cplat_local_lock_unlock(ctx->callback_mutex);
    if (need_query != 0)
    {
        potr_internal_flow_control_send_query(ctx, NULL);
    }
    return ret;
}

/*
 * multicast と broadcast の受信者 1 件について、保持中の送出上限を判定し、期限切れなら破棄する。
 * 同じ (now_ms, next_seq) に対して冪等である。callback_mutex を保持して呼ぶ。
 */
static potr_internal_send_limit_state monitoring_judge_receiver_limit(const potr_context *ctx,
                                                                      potr_internal_receiver *receiver,
                                                                      uint64_t now_ms, uint32_t next_seq,
                                                                      uint64_t *limit_until_ms)
{
    uint32_t allowance = 0U;
    potr_internal_send_limit_state limit_state;

    *limit_until_ms = 0U;
    if (receiver->flow_send_limit == 0U)
    {
        receiver->flow_limit_stalled_ms = 0U;
        return POTR_SEND_LIMIT_OPEN;
    }

    limit_state = flow_send_limit_judge(receiver->flow_send_limit, receiver->flow_limit_received_ms,
                                        &receiver->flow_limit_stalled_ms, next_seq, now_ms,
                                        ctx->flow_control_config.request_lifetime_ms, limit_until_ms, &allowance);

    /* multicast と broadcast の送出上限の破棄は従来どおりとする。 */
    if (limit_state == POTR_SEND_LIMIT_EXPIRED)
    {
        receiver->flow_send_limit = 0U;
        receiver->flow_limit_stalled_ms = 0U;
    }

    return limit_state;
}

/*
 * multicast と broadcast の送信者について、積算、除外、除外の解除、および止める判定を行う。
 * callback_mutex を保持して呼ぶ。
 */
static int monitoring_flow_suppressed_locked(potr_context *ctx, uint64_t now_ms, uint32_t next_seq, size_t *tracked,
                                             uint64_t *until_ms, int *limit_stalled)
{
    potr_internal_receiver *receiver;
    uint32_t grace_ms = ctx->flow_control_config.grace_ms;
    uint64_t grace_units;
    uint64_t unexclude_ms;
    uint64_t elapsed_ms = 0U;
    uint64_t earliest_ms = 0U;
    size_t active_count = 0U;
    size_t excluded_count = 0U;
    size_t excluded_limit;
    size_t stopping_count = 0U;
    size_t tracked_count = 0U;
    size_t limit_reached_count = 0U;
    int prev_stalled = 0;
    int exclusion_changed = 0;
    size_t previous_stopping_count = 0U;
    size_t current_stopping_count = 0U;
    int previous_all_slow;
    int current_all_slow;

    if (grace_ms == 0U)
    {
        grace_ms = (uint32_t)POTR_DEFAULT_FLOW_CONTROL_GRACE_MS;
    }

    grace_units = (uint64_t)grace_ms * (uint64_t)POTR_FLOW_ACCUM_DECAY_DIVISOR;
    unexclude_ms = (uint64_t)grace_ms * (uint64_t)POTR_FLOW_UNEXCLUDE_GRACE_MULTIPLIER;

    /*
     * wait_flow_resume() は、取り出し済み DATA を保持したまま次の判定まで待つ。
     * したがって前回「止める」を返した後の区間は、起床の遅れも含めて停止時間である。
     * close 開始後は呼び出し元がこの判定を通らず、スレッド終了・致命的エラーでは待機から抜ける。
     */
    if (ctx->flow_judge_has_last != 0 && now_ms > ctx->flow_judge_last_ms)
    {
        elapsed_ms = now_ms - ctx->flow_judge_last_ms;
        prev_stalled = ctx->flow_judge_last_suppressed;
    }

    /* 積算を変える前に前回と今回の停止数を求める。除外中の送出上限は REPORT の値で判定する。 */
    for (receiver = ctx->receivers; receiver != NULL; receiver = receiver->next)
    {
        uint64_t limit_until_ms = 0U;
        int stopping;

        if (receiver->active)
        {
            active_count++;
        }
        if (receiver->flow_last_stopping)
        {
            previous_stopping_count++;
        }
        if (receiver->flow_excluded)
        {
            stopping = receiver->active &&
                       (receiver->flow_request_expire_ms > now_ms ||
                        (receiver->flow_report_limit_known &&
                         (int32_t)(receiver->flow_report_send_limit - next_seq) <= 0));
        }
        else
        {
            potr_internal_send_limit_state limit_state =
                monitoring_judge_receiver_limit(ctx, receiver, now_ms, next_seq, &limit_until_ms);

            stopping = receiver->flow_request_expire_ms > now_ms || limit_state == POTR_SEND_LIMIT_REACHED;
        }
        if (stopping)
        {
            current_stopping_count++;
        }
    }
    excluded_limit = flow_excluded_limit(active_count, ctx->flow_control_config.excluded_receiver_count,
                                         ctx->flow_control_config.excluded_receiver_percent);
    previous_all_slow = previous_stopping_count > excluded_limit;
    current_all_slow = current_stopping_count > excluded_limit;

    /* 第 1 走査: 除外の解除と、直前の区間の積算。全体が遅い区間は加算も減算もしない。 */
    for (receiver = ctx->receivers; receiver != NULL; receiver = receiver->next)
    {
        if (receiver->flow_excluded)
        {
            int inactive = !receiver->active;
            int ahead = receiver->flow_report_limit_known &&
                        (int32_t)(receiver->flow_report_send_limit - next_seq) > 0;
            int recovered = 0;

            if (!inactive && ahead)
            {
                if (!receiver->flow_unexclude_pending)
                {
                    receiver->flow_unexclude_pending = 1;
                    receiver->flow_unexclude_since_ms = now_ms;
                }
                else if (now_ms >= receiver->flow_unexclude_since_ms &&
                         now_ms - receiver->flow_unexclude_since_ms >= unexclude_ms)
                {
                    recovered = 1;
                }
            }
            else
            {
                receiver->flow_unexclude_pending = 0;
                receiver->flow_unexclude_since_ms = 0U;
            }

            if (inactive || recovered)
            {
                receiver->flow_excluded = 0;
                receiver->flow_stall_accum_units = 0U;
                receiver->flow_unexclude_pending = 0;
                receiver->flow_unexclude_since_ms = 0U;
                exclusion_changed = 1;
                const char *reason = "report limit recovered";

                if (inactive)
                {
                    reason = "receiver inactive (report expired)";
                }
                POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                           "flow_control[service_id=%" PRId64 "]: receiver session=%u unexcluded (%s)",
                           ctx->service.service_id, (unsigned)receiver->session_id, reason);
            }
            else
            {
                excluded_count++;
            }

            /* 除外を解いた項目も直前は除外中なので、この区間は積算しない。 */
            continue;
        }

        if (!previous_all_slow)
        {
            /* 積算は 1/9 [ms] 単位。前回停止なら経過時間 × 9、それ以外なら経過時間を減算する。 */
            receiver->flow_stall_accum_units = flow_stall_accum_update(
                receiver->flow_stall_accum_units, elapsed_ms, prev_stalled && receiver->flow_last_stopping);
        }
    }

    /*
     * 第 2 走査: 猶予時間に達した受信者を、除外する数の上限に達するまで除外する。
     * 上限の適用前に積算を切り詰めずに比較することで、同じ判定で複数台が達した場合に、
     * 先に達した受信者 (積算が大きい順、同じなら管理表の順) から除外する。
     */
    excluded_limit = flow_excluded_limit(active_count, ctx->flow_control_config.excluded_receiver_count,
                                         ctx->flow_control_config.excluded_receiver_percent);

    while (!previous_all_slow && !current_all_slow && excluded_count < excluded_limit)
    {
        potr_internal_receiver *victim = NULL;

        for (receiver = ctx->receivers; receiver != NULL; receiver = receiver->next)
        {
            uint64_t limit_until_ms = 0U;
            potr_internal_send_limit_state limit_state;
            int stopping;

            if (receiver->flow_excluded || receiver->flow_stall_accum_units < grace_units)
            {
                continue;
            }

            limit_state = monitoring_judge_receiver_limit(ctx, receiver, now_ms, next_seq, &limit_until_ms);
            stopping = receiver->flow_request_expire_ms > now_ms || limit_state == POTR_SEND_LIMIT_REACHED;
            if (!stopping)
            {
                continue;
            }

            if (victim == NULL || receiver->flow_stall_accum_units > victim->flow_stall_accum_units)
            {
                victim = receiver;
            }
        }

        if (victim == NULL)
        {
            break;
        }

        victim->flow_excluded = 1;
        victim->flow_stall_accum_units = grace_units;
        victim->flow_unexclude_pending = 0;
        victim->flow_unexclude_since_ms = 0U;
        excluded_count++;
        exclusion_changed = 1;

        POTR_TRACE(CPLAT_TRACE_LEVEL_INFO,
                   "flow_control[service_id=%" PRId64 "]: receiver session=%u excluded (grace_ms=%u)",
                   ctx->service.service_id, (unsigned)victim->session_id, (unsigned)grace_ms);
    }

    if (exclusion_changed)
    {
        if (is_monitoring_sender(ctx))
        {
            monitor_select_locked(ctx, now_ms);
        }
        else
        {
            for (receiver = ctx->receivers; receiver != NULL; receiver = receiver->next)
            {
                if (receiver->flow_excluded && receiver->flow_monitored)
                {
                    receiver->flow_monitored = 0;
                    receiver->flow_send_limit = 0U;
                    receiver->flow_limit_stalled_ms = 0U;
                    ctx->flow_monitor_changed = 1;
                }
            }
        }
    }

    /* 第 3 走査: 積算の上限の丸め、止める状態の集計、追跡対象の集計、および次の判定時刻の算出。 */
    for (receiver = ctx->receivers; receiver != NULL; receiver = receiver->next)
    {
        uint64_t limit_until_ms = 0U;
        potr_internal_send_limit_state limit_state;
        int valid;
        int stopping;

        if (receiver->flow_excluded)
        {
            receiver->flow_last_stopping = receiver->active &&
                (receiver->flow_request_expire_ms > now_ms ||
                 (receiver->flow_report_limit_known && (int32_t)(receiver->flow_report_send_limit - next_seq) <= 0));
            /* 除外中の受信者は、除外を解く条件を判定し続けるため tracked に数える。 */
            tracked_count++;
            continue;
        }

        /* 除外の上限に達して除外されなかった受信者の積算は、猶予時間を上限として保持する。 */
        if (receiver->flow_stall_accum_units > grace_units)
        {
            receiver->flow_stall_accum_units = grace_units;
        }

        limit_state = monitoring_judge_receiver_limit(ctx, receiver, now_ms, next_seq, &limit_until_ms);
        valid = receiver->flow_request_expire_ms > now_ms;
        stopping = valid || limit_state == POTR_SEND_LIMIT_REACHED;
        receiver->flow_last_stopping = stopping;

        if (limit_state == POTR_SEND_LIMIT_REACHED)
        {
            limit_reached_count++;
        }

        /* 要求、送出上限、または 0 でない積算が残っている間は、送出のたびに判定し直す必要がある。 */
        if (valid || receiver->flow_send_limit != 0U || receiver->flow_stall_accum_units != 0U)
        {
            tracked_count++;
        }

        if (stopping)
        {
            stopping_count++;

            if (valid && (earliest_ms == 0U || receiver->flow_request_expire_ms < earliest_ms))
            {
                earliest_ms = receiver->flow_request_expire_ms;
            }

            if (limit_state == POTR_SEND_LIMIT_REACHED && (earliest_ms == 0U || limit_until_ms < earliest_ms))
            {
                earliest_ms = limit_until_ms;
            }

            /* まだ除外できる余地がある場合は、積算が猶予時間に達しうる時刻より後まで待たないようにする。 */
            if (!current_all_slow && excluded_count < excluded_limit)
            {
                uint64_t rem_units = (grace_units > receiver->flow_stall_accum_units)
                                         ? (grace_units - receiver->flow_stall_accum_units)
                                         : 0U;
                uint64_t rem_ms = (rem_units + (uint64_t)POTR_FLOW_ACCUM_DECAY_DIVISOR - 1U) /
                                  (uint64_t)POTR_FLOW_ACCUM_DECAY_DIVISOR;
                uint64_t exclude_at_ms = now_ms + rem_ms;

                if (earliest_ms == 0U || exclude_at_ms < earliest_ms)
                {
                    earliest_ms = exclude_at_ms;
                }
            }
        }
    }

    ctx->flow_judge_last_ms = now_ms;
    ctx->flow_judge_has_last = 1;
    ctx->flow_judge_last_suppressed = (stopping_count != 0U) ? 1 : 0;

    *limit_stalled = (limit_reached_count != 0U);
    *tracked = tracked_count;
    *until_ms = earliest_ms;

    return (stopping_count != 0U) ? 1 : 0;
}

/* Doxygen コメントは、ヘッダーに記載 */
int potr_internal_receiver_flow_suppressed(potr_context *ctx, uint64_t now_ms, uint32_t next_seq, size_t *tracked,
                                           uint64_t *until_ms)
{
    potr_internal_receiver *receiver;
    size_t stopping_count = 0U;
    size_t tracked_count = 0U;
    size_t limit_reached_count = 0U;
    uint64_t earliest_ms = 0U;
    int suppressed = 0;
    int monitoring;

    if (ctx == NULL || tracked == NULL || until_ms == NULL)
    {
        return 0;
    }

    monitoring = (ctx->service.type == POTR_TYPE_MULTICAST || ctx->service.type == POTR_TYPE_BROADCAST);

    cplat_local_lock_lock(ctx->callback_mutex);

    /*
     * 受信報告が途絶えた受信者を活動中の数から先に外す副作用だけを使い、到達できるグループの集合はここでは
     * 使わないため、戻り値は明示的に破棄する。
     */
    (void)potr_internal_receiver_groups_locked(ctx, now_ms);

    if (monitoring)
    {
        int limit_stalled = 0;

        suppressed = monitoring_flow_suppressed_locked(ctx, now_ms, next_seq, tracked, until_ms, &limit_stalled);
        cplat_local_lock_unlock(ctx->callback_mutex);
        ctx->flow_receiver_limit_stalled = limit_stalled;
        return suppressed;
    }

    for (receiver = ctx->receivers; receiver != NULL; receiver = receiver->next)
    {
        uint64_t limit_until_ms = 0U;
        uint32_t allowance = 0U;
        int valid = receiver->flow_request_expire_ms > now_ms;
        potr_internal_send_limit_state limit_state;
        int stopping;
        uint64_t activity_ms = receiver->flow_limit_received_ms;
        uint64_t state;

        /* 片方向 unicast で最後の DATA 送出から有効期限以上途切れた場合、保持中の送出上限を初期の幅へ縮める。 */
        if (ctx->flow_last_data_send_ms != 0U &&
            now_ms >= ctx->flow_last_data_send_ms + (uint64_t)ctx->flow_control_config.request_lifetime_ms)
        {
            receiver->flow_send_limit = flow_send_limit_clamp_initial(receiver->flow_send_limit, next_seq);
        }

        if (receiver->flow_last_packet_ms > activity_ms)
        {
            activity_ms = receiver->flow_last_packet_ms;
        }

        if ((receiver->flow_send_limit & POTR_FLOW_SEND_LIMIT_STATE_MASK) == POTR_FLOW_SEND_LIMIT_UNNOTIFIED)
        {
            /* 最初の通知を受けないまま止まる場合、REPORT 等で期限を延ばさない。 */
            activity_ms = 0U;
        }

        limit_state = flow_send_limit_judge(receiver->flow_send_limit, activity_ms, &receiver->flow_limit_stalled_ms,
                                            next_seq, now_ms, ctx->flow_control_config.request_lifetime_ms,
                                            &limit_until_ms, &allowance);

        /* unicast で有効期限の間、相手から何も届かなかった場合は「相手なし」へ遷移する。 */
        if (limit_state == POTR_SEND_LIMIT_EXPIRED)
        {
            receiver->flow_send_limit = POTR_FLOW_SEND_LIMIT_NO_PEER;
            receiver->flow_limit_stalled_ms = 0U;
        }

        stopping = valid || limit_state == POTR_SEND_LIMIT_REACHED;
        if (limit_state == POTR_SEND_LIMIT_REACHED)
        {
            limit_reached_count++;
        }

        /* 要求、未通知、または送出上限が残っている間は、送出のたびに判定し直す必要がある。 */
        state = receiver->flow_send_limit & POTR_FLOW_SEND_LIMIT_STATE_MASK;
        if (valid || state == POTR_FLOW_SEND_LIMIT_UNNOTIFIED || state == POTR_FLOW_SEND_LIMIT_HELD)
        {
            tracked_count++;
        }

        if (stopping)
        {
            stopping_count++;

            if (valid && (earliest_ms == 0U || receiver->flow_request_expire_ms < earliest_ms))
            {
                earliest_ms = receiver->flow_request_expire_ms;
            }

            if (limit_state == POTR_SEND_LIMIT_REACHED && (earliest_ms == 0U || limit_until_ms < earliest_ms))
            {
                earliest_ms = limit_until_ms;
            }
        }
    }

    cplat_local_lock_unlock(ctx->callback_mutex);
    ctx->flow_receiver_limit_stalled = (limit_reached_count != 0U);

    if (stopping_count != 0U)
    {
        suppressed = 1;
    }

    *tracked = tracked_count;
    *until_ms = earliest_ms;

    return suppressed;
}

/* Doxygen コメントは、ヘッダーに記載 */
size_t potr_internal_receiver_monitor_list(potr_context *ctx, uint64_t now_ms, potr_internal_packet_session_hdr *list,
                                           size_t capacity, int *changed)
{
    potr_internal_receiver *receiver;
    size_t count = 0U;

    if (ctx == NULL || list == NULL || changed == NULL)
    {
        return 0U;
    }

    *changed = 0;

    if (!is_monitoring_sender(ctx))
    {
        return 0U;
    }

    cplat_local_lock_lock(ctx->callback_mutex);

    /*
     * 受信報告が途絶えた受信者を活動中の数から先に外す副作用だけを使い、到達できるグループの集合はここでは
     * 使わないため、戻り値は明示的に破棄する。
     */
    (void)potr_internal_receiver_groups_locked(ctx, now_ms);

    /* 活動中の受信者の増減と、抑制の要求の期限の経過を反映するため、選び直す。 */
    monitor_select_locked(ctx, now_ms);

    for (receiver = ctx->receivers; receiver != NULL && count < capacity; receiver = receiver->next)
    {
        if (receiver->flow_monitored)
        {
            list[count].service_id = 0;
            list[count].session_id = receiver->session_id;
            list[count].session_tv_sec = receiver->session_tv_sec;
            list[count].session_tv_nsec = receiver->session_tv_nsec;
            count++;
        }
    }

    *changed = ctx->flow_monitor_changed;
    ctx->flow_monitor_changed = 0;

    cplat_local_lock_unlock(ctx->callback_mutex);

    return count;
}
