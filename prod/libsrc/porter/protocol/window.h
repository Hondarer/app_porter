/**
 *******************************************************************************
 *  @file           window.h
 *  @brief          スライディングウィンドウ管理モジュールの内部ヘッダー。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>

#include <porter_const.h>
#include <porter_type.h>

/**
 *  @brief  スライディングウィンドウ管理構造体。
 *
 *  @details
 *  パケットバッファ・有効フラグ・ペイロードプールは動的確保する。\n
 *  window_init() で確保し、window_destroy() で解放すること。
 */
typedef struct
{
    PotrPacket *packets;      /**< パケットバッファ (動的確保。window_size 要素)。 */
    uint8_t    *valid;        /**< バッファ有効フラグ配列 (動的確保。window_size バイト)。 */
    uint8_t    *payload_pool; /**< ペイロードプール (動的確保。window_size × max_payload バイト)。 */
    uint32_t    base_seq;     /**< ウィンドウ先頭の通番。 */
    uint32_t    next_seq;     /**< 送信側: 次に割り当てる通番。受信側: 次に期待する通番。 */
    uint16_t    window_size;  /**< ウィンドウサイズ (パケット数)。 */
    uint16_t    max_payload;  /**< エントリごとのペイロード最大長 (バイト)。 */
    uint32_t    _pad;         /**< パディング (構造体サイズを 8 バイト境界に揃える)。 */
} PotrWindow;

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    extern int  window_init(PotrWindow *win, uint32_t initial_seq,
                            uint16_t window_size, uint16_t max_payload);
    extern void window_destroy(PotrWindow *win);
    extern int  window_send_push(PotrWindow *win, const PotrPacket *packet);
    extern int  window_send_full(const PotrWindow *win);
    extern int  window_send_get(const PotrWindow *win, uint32_t seq_num,
                                PotrPacket *packet_out);
    extern int  window_recv_push(PotrWindow *win, const PotrPacket *packet);
    extern int  window_recv_pop(PotrWindow *win, PotrPacket *packet);
    extern int  window_recv_needs_nack(const PotrWindow *win, uint32_t *nack_num);
    extern void window_recv_skip(PotrWindow *win, uint32_t seq_num);
    extern void window_recv_reset(PotrWindow *win, uint32_t new_base_seq);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* WINDOW_H */
