/**
 *******************************************************************************
 *  @file           seqnum.h
 *  @brief          通番を管理する内部 API を提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/* NOTE: このヘッダーは多数のソース ファイルから参照されるため、            */
/*       @hideincludedbygraph によって "Included by" グラフを無効にします。 */

#ifndef SEQNUM_H
#define SEQNUM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          通番 seq がウィンドウ [base, base + window_size) に含まれるか判定します。
     *  @param[in]      seq         判定する通番。
     *  @param[in]      base        ウィンドウ先頭の通番。
     *  @param[in]      window_size ウィンドウ サイズ (パケット数)。
     *  @return         ウィンドウ内の場合は 1、ウィンドウ外の場合は 0 を返します。
     *                  失敗モードのない述語のため、共通結果コード (POTR_RESULT) の適用対象外です。
     */
    extern int potr_internal_seqnum_in_window(uint32_t seq, uint32_t base, uint16_t window_size);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SEQNUM_H */
