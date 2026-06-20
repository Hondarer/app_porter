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
     *  @brief          通番を初期化します。
     *  @param[out]     seq     初期化する通番へのポインター。
     *  @param[in]      initial 初期値。
     */
    extern void seqnum_init(uint32_t *seq, uint32_t initial);

    /**
     *  @brief          通番を 1 進めて次の値を返します。
     *  @param[in,out]  seq     通番へのポインター。
     *  @return         インクリメント後の通番。seq が NULL の場合は 0 を返します。
     *
     *  uint32_t の最大値に達した場合は 0 に折り返します。
     */
    extern uint32_t seqnum_next(uint32_t *seq);

    /**
     *  @brief          通番 a が通番 b より新しいかどうかを判定します。
     *  @param[in]      a   比較対象の通番 A。
     *  @param[in]      b   比較対象の通番 B。
     *  @return         a が b より新しい場合は 1、そうでない場合は 0 を返します。
     *
     *  uint32_t 折り返しを考慮した比較を行います。\n
     *  差が UINT32_MAX / 2 以下の場合に「a が新しい」と判定します。
     */
    extern int seqnum_is_newer(uint32_t a, uint32_t b);

    /**
     *  @brief          通番 seq がウィンドウ [base, base + window_size) に含まれるか判定します。
     *  @param[in]      seq         判定する通番。
     *  @param[in]      base        ウィンドウ先頭の通番。
     *  @param[in]      window_size ウィンドウ サイズ (パケット数)。
     *  @return         ウィンドウ内の場合は 1、ウィンドウ外の場合は 0 を返します。
     */
    extern int seqnum_in_window(uint32_t seq, uint32_t base, uint16_t window_size);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SEQNUM_H */
