/**
 *******************************************************************************
 *  @file           seqnum.c
 *  @brief          通番管理モジュール。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <stddef.h>
#include <stdint.h>

#include <porter/protocol/seqnum.h>

/* Doxygen コメントは、ヘッダーに記載 */

void seqnum_init(uint32_t *seq, uint32_t initial)
{
    if (seq == NULL)
    {
        return;
    }
    *seq = initial;
}

/* Doxygen コメントは、ヘッダーに記載 */

uint32_t seqnum_next(uint32_t *seq)
{
    if (seq == NULL)
    {
        return 0;
    }
    return ++(*seq);
}

/* Doxygen コメントは、ヘッダーに記載 */

int seqnum_is_newer(uint32_t a, uint32_t b)
{
    return (uint32_t)(a - b) < (uint32_t)(UINT32_MAX / 2U);
}

/* Doxygen コメントは、ヘッダーに記載 */

int seqnum_in_window(uint32_t seq, uint32_t base, uint16_t window_size)
{
    return (uint32_t)(seq - base) < (uint32_t)window_size;
}
