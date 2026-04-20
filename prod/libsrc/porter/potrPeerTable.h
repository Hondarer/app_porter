/**
 *******************************************************************************
 *  @file           potrPeerTable.h
 *  @brief          N:1 モード用ピアテーブル管理モジュールの内部ヘッダー。
 *  @author         Tetsuo Honda
 *  @date           2026/03/23
 *  @version        1.0.0
 *
 *  @details
 *  POTR_TYPE_UNICAST_BIDIR の N:1 モード (src 情報省略) において、
 *  複数クライアントピアの状態を管理するユーティリティです。\n
 *  is_multi_peer == 1 のときのみ有効。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef POTR_PEER_TABLE_H
#define POTR_PEER_TABLE_H

#include <porter_type.h>

#include "potrContext.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief  ピアの全パスへ FIN パケットを送信する。
     *
     *  @details
     *  ピアの dest_addr[] に対して FIN パケットを直接 sendto する。\\n
     *  呼び出し元は peers_mutex を取得してから呼び出すこと。
     *
     *  @param[in,out]  ctx     セッションコンテキスト。
     *  @param[in]      peer    FIN を送信するピアコンテキスト。
     */
    extern void peer_send_fin(struct PotrContext_ *ctx, PotrPeerContext *peer);

    /**
     *  @brief  ピアテーブルを初期化する。
     *
     *  @details
     *  ctx->peers を max_peers 分確保し、peers_mutex を初期化する。\n
     *  ctx->max_peers, ctx->n_peers, ctx->next_peer_id を設定する。
     *
     *  @param[in,out]  ctx         セッションコンテキスト。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int peer_table_init(struct PotrContext_ *ctx);

    /**
     *  @brief  ピアテーブルを破棄する。
     *
     *  @details
     *  全アクティブピアに FIN を送信し、リソースを解放する。\n
     *  peers_mutex を解放する。
     *
     *  @param[in,out]  ctx         セッションコンテキスト。
     */
    extern void peer_table_destroy(struct PotrContext_ *ctx);

    /**
     *  @brief  session_triplet でピアを検索する。
     *
     *  @details
     *  ピアテーブルを線形探索し、session_id / session_tv_sec / session_tv_nsec が
     *  一致するアクティブなエントリを返す。\n
     *  呼び出し元は peers_mutex を取得してから呼び出すこと。
     *
     *  @param[in]  ctx             セッションコンテキスト。
     *  @param[in]  session_id      ピアのセッション識別子。
     *  @param[in]  session_tv_sec  ピアのセッション開始時刻 秒部。
     *  @param[in]  session_tv_nsec ピアのセッション開始時刻 ナノ秒部。
     *  @return     見つかった場合はピアコンテキストへのポインタ、見つからない場合は NULL。
     */
    extern PotrPeerContext *peer_find_by_session(struct PotrContext_ *ctx,
                                                 uint32_t session_id,
                                                 int64_t  session_tv_sec,
                                                 int32_t  session_tv_nsec);

    /**
     *  @brief  peer_id でピアを検索する。
     *
     *  @details
     *  呼び出し元は peers_mutex を取得してから呼び出すこと。
     *
     *  @param[in]  ctx     セッションコンテキスト。
     *  @param[in]  peer_id 検索するピア ID。
     *  @return     見つかった場合はピアコンテキストへのポインタ、見つからない場合は NULL。
     */
    extern PotrPeerContext *peer_find_by_id(struct PotrContext_ *ctx,
                                            PotrPeerId peer_id);

    /**
     *  @brief  新規ピアを作成する。
     *
     *  @details
     *  空きスロットを確保し、session_id/session_tv を生成してウィンドウを初期化する。\n
     *  送信元アドレスを dest_addr[path_idx] に記録し、n_paths を 1 に設定する。\n
     *  dest_addr[] のインデックスは ctx->sock[] / src_addr[] と直接対応する。\n
     *  path_last_recv_sec[path_idx] は後続の n1_update_path_recv() 呼び出しで設定される。\n
     *  max_peers 超過時はログエラー後 NULL を返す。\n
     *  呼び出し元は peers_mutex を取得してから呼び出すこと。
     *
     *  @param[in,out]  ctx         セッションコンテキスト。
     *  @param[in]      sender_addr ピアの送信元アドレス (recvfrom で取得したアドレス)。
     *  @param[in]      path_idx    パケットを受信したサーバソケットのインデックス (ctx->sock[] の添字)。
     *  @return         成功時はピアコンテキストへのポインタ、失敗時は NULL。
     */
    extern PotrPeerContext *peer_create(struct PotrContext_ *ctx,
                                        const struct sockaddr_in *sender_addr,
                                        int path_idx);

    /**
     *  @brief  ピアの特定パスをクリアしてスロットを未使用に戻す。
     *
     *  @details
     *  dest_addr[path_idx] をゼロクリアし、path_last_recv_sec/nsec をリセット後 n_paths を減算する。\n
     *  タイムアウト・TCP FIN/RST などパス断の発生源によらず共通して呼び出す。\n
     *  呼び出し元は peers_mutex を取得してから呼び出すこと。
     *
     *  @param[in,out]  ctx         セッションコンテキスト。
     *  @param[in,out]  peer        対象ピアコンテキスト。
     *  @param[in]      path_idx    クリアするパスのインデックス (ctx->sock[] の添字)。
     */
    extern void peer_path_clear(struct PotrContext_ *ctx, PotrPeerContext *peer, int path_idx);

    /**
     *  @brief  ピアリソースを解放してスロットをクリアする。
     *
     *  @details
     *  ウィンドウ破棄・frag_buf 解放・send_window_mutex 解放・スロットクリアを行う。\n
     *  呼び出し元は peers_mutex を取得してから呼び出すこと。\n
     *  FIN の送信は呼び出し元が行うこと (本関数は送信しない)。
     *
     *  @param[in,out]  ctx     セッションコンテキスト。
     *  @param[in,out]  peer    解放するピアコンテキスト。
     */
    extern void peer_free(struct PotrContext_ *ctx, PotrPeerContext *peer);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_PEER_TABLE_H */
