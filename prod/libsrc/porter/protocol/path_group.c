/**
 *******************************************************************************
 *  @file           path_group.c
 *  @brief          優先グループの純粋な状態遷移を実装します。
 *
 *******************************************************************************
 */
#include <string.h>
#include <porter/protocol/path_group.h>

/* 経路集合を返す値関数のため、結果コードの対象外です。 */
static uint8_t group_paths(const potr_internal_path_group_input *input, uint8_t group)
{
    uint8_t paths = 0U;
    size_t i;

    for (i = 0U; i < POTR_MAX_PATH; i++)
    {
        if (input->available[i] && (group == 0U || input->group[i] == group))
        {
            paths |= (uint8_t)(1U << i);
        }
    }
    return paths;
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_path_group_evaluate(const potr_internal_path_group_input *input,
    const potr_internal_path_group_state *before, potr_internal_path_group_state *after,
    potr_internal_path_group_result *result)
{
    potr_internal_path_group_state previous;

    if (input == NULL || before == NULL || after == NULL || result == NULL ||
        before->selected > POTR_MAX_PATH_GROUP || before->candidate > POTR_MAX_PATH_GROUP)
    {
        return;
    }
    previous = *before;
    uint8_t reachable[POTR_MAX_PATH_GROUP + 1U] = {0};
    uint8_t first = 0U;
    uint8_t best = 0U;
    uint8_t group;
    size_t i;
    int tcp = input->type == POTR_TYPE_TCP || input->type == POTR_TYPE_TCP_BIDIR;
    int bidir = tcp || input->type == POTR_TYPE_UNICAST_BIDIR || input->type == POTR_TYPE_UNICAST_BIDIR_N1;
    int raw = input->type == POTR_TYPE_UNICAST_RAW || input->type == POTR_TYPE_MULTICAST_RAW ||
              input->type == POTR_TYPE_BROADCAST_RAW;

    before = &previous;
    *after = previous;
    memset(result, 0, sizeof(*result));
    result->previous_group = before->selected;

    for (i = 0U; i < POTR_MAX_PATH; i++)
    {
        group = input->group[i];
        if (!input->available[i] || group == 0U || group > POTR_MAX_PATH_GROUP)
        {
            continue;
        }
        if (first == 0U || group < first)
        {
            first = group;
        }
        if (bidir)
        {
            reachable[group] |= input->reachable[i];
        }
        else
        {
            reachable[group] = (uint8_t)(((unsigned int)input->reported_groups >> (group - 1U)) & 1U);
        }
    }
    for (group = 1U; group <= POTR_MAX_PATH_GROUP; group++)
    {
        if (reachable[group] && group_paths(input, group) != 0U)
        {
            best = group;
            break;
        }
    }

    if (raw || input->interval_ms == 0U || input->timeout_ms == 0U ||
        (!bidir && !input->report_received && input->now_ms - input->started_ms >= input->timeout_ms))
    {
        after->candidate = 0U;
        result->data_paths = group_paths(input, 0U);
        result->selected_paths = result->data_paths;
        result->next_group = after->selected;
        return;
    }

    if (!bidir && !input->report_received)
    {
        after->selected = first;
        after->candidate = 0U;
        result->data_paths = group_paths(input, first);
        result->selected_paths = result->data_paths;
        result->next_group = first;
        return;
    }

    if (!after->confirmed)
    {
        if (best != 0U)
        {
            after->selected = best;
        }
        after->confirmed = (uint8_t)(!bidir || best != 0U);
    }
    if (best == 0U)
    {
        after->candidate = 0U;
        result->disconnected = (uint8_t)bidir;
        if (!bidir)
        {
            result->data_paths = group_paths(input, 0U);
            result->selected_paths = result->data_paths;
        }
        result->next_group = after->selected;
        return;
    }

    if (after->selected == 0U || !reachable[after->selected] || group_paths(input, after->selected) == 0U)
    {
        if (tcp && before->confirmed && before->selected != 0U && before->selected != best)
        {
            after->resend_group = best;
        }
        after->selected = best;
        after->candidate = 0U;
    }
    else if (best < after->selected)
    {
        uint32_t wait_ms = input->failback_ms;

        if (wait_ms == 0U)
        {
            wait_ms = input->timeout_ms;
        }
        if (after->candidate != best)
        {
            after->candidate = best;
            after->candidate_since_ms = input->now_ms;
        }
        if (input->now_ms - after->candidate_since_ms >= wait_ms)
        {
            after->selected = best;
            after->candidate = 0U;
            after->resend_group = 0U;
        }
    }
    else
    {
        after->candidate = 0U;
    }

    result->selected_paths = group_paths(input, after->selected);
    result->data_paths = result->selected_paths;
    if (after->candidate != 0U)
    {
        result->data_paths |= group_paths(input, after->candidate);
    }
    result->next_group = after->selected;
    result->changed = (uint8_t)(before->selected != 0U && before->selected != after->selected);
    result->tcp_resend = (uint8_t)(after->resend_group != 0U);
}
