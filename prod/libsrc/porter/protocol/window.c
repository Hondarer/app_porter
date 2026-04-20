/**
 *******************************************************************************
 *  @file           window.c
 *  @brief          スライディングウィンドウ管理モジュール。
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

#include <porter_const.h>

#include "seqnum.h"
#include "window.h"

/**
 *******************************************************************************
 *  @brief          ウィンドウを初期化します。
 *  @param[in,out]  win         初期化するウィンドウ構造体へのポインタ。
 *  @param[in]      initial_seq 初期通番。
 *  @param[in]      window_size ウィンドウサイズ (パケット数)。
 *  @param[in]      max_payload エントリごとのペイロード最大長 (バイト)。
 *  @return         成功時は POTR_SUCCESS、malloc 失敗時は POTR_ERROR を返します。
 *
 *  @details
 *  サイズが既存と同一の場合は状態をリセットするのみで再確保は行いません。\n
 *  異なるサイズの場合は既存バッファを解放して再確保します。
 *******************************************************************************
 */
int window_init(PotrWindow *win, uint32_t initial_seq,
                uint16_t window_size, uint16_t max_payload)
{
    uint16_t i;

    if (win == NULL)
    {
        return POTR_ERROR;
    }

    /* 既存バッファがあり、サイズが一致する場合は状態リセットのみ */
    if (win->packets != NULL
        && win->window_size == window_size
        && win->max_payload == max_payload)
    {
        memset(win->valid, 0, window_size);
        win->base_seq = initial_seq;
        win->next_seq = initial_seq;
        return POTR_SUCCESS;
    }

    /* 既存バッファを解放 */
    free(win->packets);
    free(win->valid);
    free(win->payload_pool);
    win->packets      = NULL;
    win->valid        = NULL;
    win->payload_pool = NULL;

    /* 新規確保 */
    win->packets      = (PotrPacket *)malloc((size_t)window_size * sizeof(PotrPacket));
    win->valid        = (uint8_t *)malloc((size_t)window_size);
    win->payload_pool = (uint8_t *)malloc((size_t)window_size * (size_t)max_payload);

    if (win->packets == NULL || win->valid == NULL || win->payload_pool == NULL)
    {
        free(win->packets);
        free(win->valid);
        free(win->payload_pool);
        win->packets      = NULL;
        win->valid        = NULL;
        win->payload_pool = NULL;
        return POTR_ERROR;
    }

    memset(win->valid, 0, (size_t)window_size);

    /* 各エントリの payload ポインタをプールスロットへ設定 */
    for (i = 0; i < window_size; i++)
    {
        memset(&win->packets[i], 0, sizeof(PotrPacket));
        win->packets[i].payload = win->payload_pool + (size_t)i * (size_t)max_payload;
    }

    win->base_seq    = initial_seq;
    win->next_seq    = initial_seq;
    win->window_size = window_size;
    win->max_payload = max_payload;

    return POTR_SUCCESS;
}

/**
 *******************************************************************************
 *  @brief          ウィンドウが保持する動的確保バッファを解放します。
 *  @param[in,out]  win  解放するウィンドウ構造体へのポインタ。
 *******************************************************************************
 */
void window_destroy(PotrWindow *win)
{
    if (win == NULL)
    {
        return;
    }

    free(win->packets);
    free(win->valid);
    free(win->payload_pool);
    win->packets      = NULL;
    win->valid        = NULL;
    win->payload_pool = NULL;
}

/* ウィンドウ内インデックス計算 */
static uint16_t win_index(const PotrWindow *win, uint32_t seq)
{
    return (uint16_t)((seq - win->base_seq) % win->window_size);
}

/* ---------- 送信側 ---------- */

/**
 *******************************************************************************
 *  @brief          送信ウィンドウにパケットを積みます。
 *  @param[in,out]  win     送信ウィンドウ構造体へのポインタ。
 *  @param[in]      packet  積むパケットへのポインタ。
 *  @return         成功時は POTR_SUCCESS、ウィンドウ満杯の場合は POTR_ERROR を返します。
 *******************************************************************************
 */
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
        idx               = win_index(win, win->base_seq);
        win->valid[idx]   = 0;
        win->base_seq++;
    }

    idx = win_index(win, win->next_seq);

    /* プールスロットへ深コピー (packet->payload_len は NBO: packet_build_packed が設定) */
    {
        /* プールスロットアドレスをインデックスから直接計算することで const 除去キャストを回避する */
        uint8_t *slot     = win->payload_pool + idx * (size_t)win->max_payload;
        win->packets[idx] = *packet;                   /* 構造体コピー */
        win->packets[idx].payload = slot;              /* プールスロットを設定 */
        memcpy(slot, packet->payload, (size_t)ntohs(packet->payload_len));
    }

    win->valid[idx] = 1;
    win->next_seq++;

    return POTR_SUCCESS;
}


/**
 *******************************************************************************
 *  @brief          送信ウィンドウが満杯かどうかを返します。
 *  @param[in]      win     送信ウィンドウ構造体へのポインタ。
 *  @return         満杯の場合は 1、空きがある場合は 0 を返します。
 *******************************************************************************
 */
int window_send_full(const PotrWindow *win)
{
    if (win == NULL)
    {
        return 1;
    }
    return (uint32_t)(win->next_seq - win->base_seq) >= (uint32_t)win->window_size;
}

/**
 *******************************************************************************
 *  @brief          送信ウィンドウから指定通番のパケットを取得します (再送用)。
 *  @param[in]      win         送信ウィンドウ構造体へのポインタ。
 *  @param[in]      seq_num     取得する通番。
 *  @param[out]     packet_out  取得したパケットを格納する構造体へのポインタ。
 *  @return         成功時は POTR_SUCCESS、エントリが存在しない場合は POTR_ERROR を返します。
 *******************************************************************************
 */
int window_send_get(const PotrWindow *win, uint32_t seq_num, PotrPacket *packet_out)
{
    uint16_t idx;

    if (win == NULL || packet_out == NULL)
    {
        return POTR_ERROR;
    }

    /* 通番がウィンドウ範囲外 */
    if (!seqnum_in_window(seq_num, win->base_seq,
                          (uint16_t)(win->next_seq - win->base_seq)))
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

/**
 *******************************************************************************
 *  @brief          受信ウィンドウにパケットを格納します。
 *  @param[in,out]  win     受信ウィンドウ構造体へのポインタ。
 *  @param[in]      packet  格納するパケットへのポインタ。
 *  @return         成功時は POTR_SUCCESS、ウィンドウ外の場合は POTR_ERROR を返します。
 *
 *  @details
 *  通番が受信ウィンドウ内であればバッファリングします。\n
 *  追い越し (順序外到着) にも対応します。
 *******************************************************************************
 */
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
        /* プールスロットへ深コピー (packet->payload_len はホストバイトオーダー: packet_parse が変換済み) */
        /* プールスロットアドレスをインデックスから直接計算することで const 除去キャストを回避する */
        uint8_t *slot     = win->payload_pool + idx * (size_t)win->max_payload;
        win->packets[idx] = *packet;
        win->packets[idx].payload = slot;
        memcpy(slot, packet->payload, (size_t)packet->payload_len);
        win->valid[idx] = 1;
    }

    return POTR_SUCCESS;
}

/**
 *******************************************************************************
 *  @brief          受信ウィンドウから順序整列済みパケットを取り出します。
 *  @param[in,out]  win     受信ウィンドウ構造体へのポインタ。
 *  @param[out]     packet  取り出したパケットを格納する構造体へのポインタ。
 *  @return         取り出せた場合は POTR_SUCCESS、次のパケットが未着の場合は POTR_ERROR を返します。
 *******************************************************************************
 */
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

    *packet         = win->packets[idx];
    win->valid[idx] = 0;
    win->base_seq++;
    win->next_seq++;

    return POTR_SUCCESS;
}

/**
 *******************************************************************************
 *  @brief          受信ウィンドウで指定通番をスキップして次の通番へ前進させます。
 *  @param[in,out]  win     受信ウィンドウ構造体へのポインタ。
 *  @param[in]      seq_num スキップする通番。next_seq と一致する場合のみ動作します。
 *
 *  @details
 *  REJECT 受信時に欠落パケットを「配信済み」として扱い、後続パケットの配信を
 *  継続するために使用します。seq_num が next_seq と一致しない場合は何もしません。
 *******************************************************************************
 */
void window_recv_skip(PotrWindow *win, uint32_t seq_num)
{
    uint16_t idx;

    if (win == NULL || win->next_seq != seq_num)
    {
        return;
    }

    idx               = win_index(win, seq_num);
    win->valid[idx]   = 0; /* 万一セットされていても無効化 */
    win->base_seq++;
    win->next_seq++;
}

/**
 *******************************************************************************
 *  @brief          受信ウィンドウで欠番が発生しているか確認し、NACK 番号を返します。
 *  @param[in]      win         受信ウィンドウ構造体へのポインタ。
 *  @param[out]     nack_num    欠番の通番を格納するポインタ。
 *  @return         欠番がある場合は 1、ない場合は 0 を返します。
 *******************************************************************************
 */
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

/**
 *******************************************************************************
 *  @brief          受信ウィンドウを新しい基点通番でリセットします。
 *  @param[in,out]  win          受信ウィンドウ構造体へのポインタ。
 *  @param[in]      new_base_seq リセット後の基点通番 (次に期待する通番)。
 *
 *  @details
 *  全スロットを無効化し、base_seq / next_seq を new_base_seq に設定します。\n
 *  バッファの再確保は行いません。\n
 *  RAW モードでギャップを検出してセッションをリセットする際に使用します。
 *******************************************************************************
 */
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
