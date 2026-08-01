/**
 *******************************************************************************
 *  @file           seqnum.c
 *  @brief          通番を管理する機能を提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <stdint.h>

#include <porter/protocol/seqnum.h>

/* Doxygen コメントは、ヘッダーに記載 */

int seqnum_in_window(uint32_t seq, uint32_t base, uint16_t window_size)
{
    return (uint32_t)(seq - base) < (uint32_t)window_size;
}
