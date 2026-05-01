/**
 *******************************************************************************
 *  @file           seqnum.h
 *  @brief          通番管理モジュールの内部ヘッダー。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef SEQNUM_H
#define SEQNUM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    extern void     seqnum_init(uint32_t *seq, uint32_t initial);
    extern uint32_t seqnum_next(uint32_t *seq);
    extern int      seqnum_is_newer(uint32_t a, uint32_t b);
    extern int      seqnum_in_window(uint32_t seq, uint32_t base, uint16_t window_size);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SEQNUM_H */
