/**
 *******************************************************************************
 *  @file           window.c
 *  @brief          スライディング ウィンドウを管理する機能を提供します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <com_util/base/platform.h>
#include <stdlib.h>
#include <string.h>

#if defined(PLATFORM_LINUX)
    #include <arpa/inet.h>
#elif defined(PLATFORM_WINDOWS)
    #include <com_util/base/windows_sdk.h>
#endif /* PLATFORM_ */

#include <porter/porter_const.h>

#include <porter/protocol/seqnum.h>
#include <porter/protocol/window.h>

/* Doxygen コメントは、ヘッダーに記載 */

int window_init(PotrWindow *win, uint32_t initial_seq, uint16_t window_size, uint16_t max_payload)
{
    uint16_t i;

    if (win == NULL)
    {
        return POTR_ERROR;
    }

    /* 既存バッファーがあり、サイズが一致する場合は状態リセットのみ */
    if (win->packets != NULL && win->window_size == window_size && win->max_payload == max_payload)
    {
        memset(win->valid, 0, window_size);
        win->base_seq = initial_seq;
        win->next_seq = initial_seq;
        return POTR_SUCCESS;
    }

    /* 既存バッファーを解放 */
    free(win->packets);
    free(win->valid);
    free(win->payload_pool);
    win->packets = NULL;
    win->valid = NULL;
    win->payload_pool = NULL;

    /* 新規確保 */
    win->packets = (PotrPacket *)malloc((size_t)window_size * sizeof(PotrPacket));
    win->valid = (uint8_t *)malloc((size_t)window_size);
    win->payload_pool = (uint8_t *)malloc((size_t)window_size * (size_t)max_payload);

    if (win->packets == NULL || win->valid == NULL || win->payload_pool == NULL)
    {
        free(win->packets);
        free(win->valid);
        free(win->payload_pool);
        win->packets = NULL;
        win->valid = NULL;
        win->payload_pool = NULL;
        return POTR_ERROR;
    }

    memset(win->valid, 0, (size_t)window_size);

    /* 各エントリの payload ポインターをプール スロットへ設定 */
    for (i = 0; i < window_size; i++)
    {
        memset(&win->packets[i], 0, sizeof(PotrPacket));
        win->packets[i].payload = win->payload_pool + (size_t)i * (size_t)max_payload;
    }

    win->base_seq = initial_seq;
    win->next_seq = initial_seq;
    win->window_size = window_size;
    win->max_payload = max_payload;

    return POTR_SUCCESS;
}

/* Doxygen コメントは、ヘッダーに記載 */

void window_dispose(PotrWindow *win)
{
    if (win == NULL)
    {
        return;
    }

    free(win->packets);
    free(win->valid);
    free(win->payload_pool);
    win->packets = NULL;
    win->valid = NULL;
    win->payload_pool = NULL;
}

/**
 *  @brief          通番に対応するウィンドウ内インデックスを計算します。
 *  @param[in]      win  ウィンドウ構造体へのポインター。
 *  @param[in]      seq  インデックスを求める通番。
 *  @return         base_seq からの相対位置を window_size で剰余したインデックス。
 */
static uint16_t win_index(const PotrWindow *win, uint32_t seq)
{
    return (uint16_t)((seq - win->base_seq) % win->window_size);
}

/* ---------- 送信側 ---------- */

/* Doxygen コメントは、ヘッダーに記載 */

int window_send_push(PotrWindow *win, const PotrPacket *packet)
{
    uint16_t idx;

    if (win == NULL || packet == NULL)
    {
        return POTR_ERROR;
    }

    /* ACK なし設計のため、満杯の場合は最古エントリを evict して循環利用する。
       evict されたエントリへの NACK は REJECT で応答する。 */
    if (window_send_full(win))
    {
        idx = win_index(win, win->base_seq);
        win->valid[idx] = 0;
        win->base_seq++;
    }

    idx = win_index(win, win->next_seq);

    /* プール スロットへディープ コピー (packet->payload_len は NBO: packet_build_packed が設定) */
    {
        /* プール スロット アドレスをインデックスから直接計算することで const 除去キャストを回避する */
        uint8_t *slot = win->payload_pool + idx * (size_t)win->max_payload;
        win->packets[idx] = *packet;      /* 構造体コピー */
        win->packets[idx].payload = slot; /* プール スロットを設定 */
        memcpy(slot, packet->payload, (size_t)ntohs(packet->payload_len));
    }

    win->valid[idx] = 1;
    win->next_seq++;

    return POTR_SUCCESS;
}

/* Doxygen コメントは、ヘッダーに記載 */

int window_send_full(const PotrWindow *win)
{
    if (win == NULL)
    {
        return 1;
    }
    return (uint32_t)(win->next_seq - win->base_seq) >= (uint32_t)win->window_size;
}

/* Doxygen コメントは、ヘッダーに記載 */

int window_send_get(const PotrWindow *win, uint32_t seq_num, PotrPacket *packet_out)
{
    uint16_t idx;

    if (win == NULL || packet_out == NULL)
    {
        return POTR_ERROR;
    }

    /* 通番がウィンドウ範囲外 */
    if (!seqnum_in_window(seq_num, win->base_seq, (uint16_t)(win->next_seq - win->base_seq)))
    {
        return POTR_ERROR;
    }

    idx = win_index(win, seq_num);
    if (!win->valid[idx])
    {
        return POTR_ERROR;
    }

    *packet_out = win->packets[idx];
    return POTR_SUCCESS;
}

/* ---------- 受信側 ---------- */

/* Doxygen コメントは、ヘッダーに記載 */

int window_recv_push(PotrWindow *win, const PotrPacket *packet)
{
    uint16_t idx;

    if (win == NULL || packet == NULL)
    {
        return POTR_ERROR;
    }

    if (!seqnum_in_window(packet->seq_num, win->base_seq, win->window_size))
    {
        return POTR_ERROR;
    }

    idx = win_index(win, packet->seq_num);
    if (!win->valid[idx])
    {
        /* プール スロットへディープ コピー (packet->payload_len はホスト バイト オーダー: packet_parse が変換済み) */
        /* プール スロット アドレスをインデックスから直接計算することで const 除去キャストを回避する */
        uint8_t *slot = win->payload_pool + idx * (size_t)win->max_payload;
        win->packets[idx] = *packet;
        win->packets[idx].payload = slot;
        memcpy(slot, packet->payload, (size_t)packet->payload_len);
        win->valid[idx] = 1;
    }

    return POTR_SUCCESS;
}

/* Doxygen コメントは、ヘッダーに記載 */

int window_recv_pop(PotrWindow *win, PotrPacket *packet)
{
    uint16_t idx;

    if (win == NULL || packet == NULL)
    {
        return POTR_ERROR;
    }

    idx = win_index(win, win->next_seq);
    if (!win->valid[idx])
    {
        return POTR_ERROR;
    }

    *packet = win->packets[idx];
    win->valid[idx] = 0;
    win->base_seq++;
    win->next_seq++;

    return POTR_SUCCESS;
}

/* Doxygen コメントは、ヘッダーに記載 */

void window_recv_skip(PotrWindow *win, uint32_t seq_num)
{
    uint16_t idx;

    if (win == NULL || win->next_seq != seq_num)
    {
        return;
    }

    idx = win_index(win, seq_num);
    win->valid[idx] = 0; /* 万一セットされていても無効化 */
    win->base_seq++;
    win->next_seq++;
}

/* Doxygen コメントは、ヘッダーに記載 */

int window_recv_needs_nack(const PotrWindow *win, uint32_t *nack_num)
{
    uint16_t i;
    uint16_t idx;

    if (win == NULL || nack_num == NULL)
    {
        return 0;
    }

    idx = win_index(win, win->next_seq);
    if (win->valid[idx])
    {
        return 0; /* next_seq が既着 = 欠番なし */
    }

    /* next_seq が未着のとき、ウィンドウ内に先行して到着したパケットがあれば欠番 */
    for (i = 1U; i < win->window_size; i++)
    {
        uint16_t look_idx = win_index(win, win->next_seq + i);
        if (win->valid[look_idx])
        {
            *nack_num = win->next_seq;
            return 1;
        }
    }

    return 0; /* ウィンドウが空 = 欠番なし */
}

/* Doxygen コメントは、ヘッダーに記載 */

void window_recv_reset(PotrWindow *win, uint32_t new_base_seq)
{
    if (win == NULL || win->valid == NULL)
    {
        return;
    }

    memset(win->valid, 0, (size_t)win->window_size);
    win->base_seq = new_base_seq;
    win->next_seq = new_base_seq;
}
