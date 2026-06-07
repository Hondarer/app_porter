/**
 *******************************************************************************
 *  @file           potrPeerTable.h
 *  @brief          N:1 モード用ピア テーブル管理モジュールの内部ヘッダー。
 *  @author         Tetsuo Honda
 *  @date           2026/03/23
 *  @version        1.0.0
 *
 *  POTR_TYPE_UNICAST_BIDIR の N:1 モード (src 情報省略) において、
 *  複数クライアント ピアの状態を管理するユーティリティです。\n
 *  is_multi_peer == 1 のときのみ有効。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/* NOTE: このヘッダーは多数のソース ファイルから参照されるため、            */
/*       @hideincludedbygraph によって "Included by" グラフを無効にします。 */

#ifndef POTR_PEER_TABLE_H
#define POTR_PEER_TABLE_H

#include <porter/porter_type.h>

#include <porter/potrContext.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief  ピアの全パスへ FIN パケットを送信する。
     *
     *  ピアの dest_addr[] に対して FIN パケットを直接 sendto する。\\n
     *  呼び出し元は peers_mutex を取得してから呼び出すこと。
     *
     *  @param[in,out]  ctx     セッション コンテキスト。
     *  @param[in]      peer    FIN を送信するピア コンテキスト。
     */
    extern void peer_send_fin(PotrContext *ctx, PotrPeerContext *peer);

    /**
     *  @brief  ピア テーブルを初期化する。
     *
     *  ctx->peers を max_peers 分確保し、peers_mutex を初期化する。\n
     *  ctx->max_peers, ctx->n_peers, ctx->next_peer_id を設定する。
     *
     *  @param[in,out]  ctx         セッション コンテキスト。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR。
     */
    extern int peer_table_init(PotrContext *ctx);

    /**
     *  @brief  ピア テーブルを破棄する。
     *
     *  全アクティブ ピアに FIN を送信し、リソースを解放する。\n
     *  peers_mutex を解放する。
     *
     *  @param[in,out]  ctx         セッション コンテキスト。
     */
    extern void peer_table_dispose(PotrContext *ctx);

    /**
     *  @brief  session_triplet でピアを検索する。
     *
     *  ピア テーブルを線形探索し、session_id / session_tv_sec / session_tv_nsec が
     *  一致するアクティブなエントリを返す。\n
     *  呼び出し元は peers_mutex を取得してから呼び出すこと。
     *
     *  @param[in]  ctx             セッション コンテキスト。
     *  @param[in]  session_id      ピアのセッション識別子。
     *  @param[in]  session_tv_sec  ピアのセッション開始時刻 秒部。
     *  @param[in]  session_tv_nsec ピアのセッション開始時刻 ナノ秒部。
     *  @return     見つかった場合はピア コンテキストへのポインター、見つからない場合は NULL。
     */
    extern PotrPeerContext *peer_find_by_session(const PotrContext *ctx, uint32_t session_id, int64_t session_tv_sec,
                                                 int32_t session_tv_nsec);

    /**
     *  @brief  peer_id でピアを検索する。
     *
     *  呼び出し元は peers_mutex を取得してから呼び出すこと。
     *
     *  @param[in]  ctx     セッション コンテキスト。
     *  @param[in]  peer_id 検索するピア ID。
     *  @return     見つかった場合はピア コンテキストへのポインター、見つからない場合は NULL。
     */
    extern PotrPeerContext *peer_find_by_id(const PotrContext *ctx, PotrPeerId peer_id);

    /**
     *  @brief  新規ピアを作成する。
     *
     *  空きスロットを確保し、session_id/session_tv を生成してウィンドウを初期化する。\n
     *  送信元アドレスを dest_addr[path_idx] に記録し、n_paths を 1 に設定する。\n
     *  dest_addr[] のインデックスは ctx->sock[] / src_addr[] と直接対応する。\n
     *  path_last_recv_sec[path_idx] は後続の n1_update_path_recv() 呼び出しで設定される。\n
     *  max_peers 超過時はログ エラー後 NULL を返す。\n
     *  呼び出し元は peers_mutex を取得してから呼び出すこと。
     *
     *  @param[in,out]  ctx         セッション コンテキスト。
     *  @param[in]      sender_addr ピアの送信元アドレス (recvfrom で取得したアドレス)。
     *  @param[in]      path_idx    パケットを受信したサーバー ソケットのインデックス (ctx->sock[] の添字)。
     *  @return         成功時はピア コンテキストへのポインター、失敗時は NULL。
     */
    extern PotrPeerContext *peer_create(PotrContext *ctx, const struct sockaddr_in *sender_addr, int path_idx);

    /**
     *  @brief  ピアの特定パスをクリアしてスロットを未使用に戻す。
     *
     *  dest_addr[path_idx] をゼロ クリアし、path_last_recv_sec/nsec をリセット後 n_paths を減算する。\n
     *  タイムアウト・TCP FIN/RST などパス断の発生源によらず共通して呼び出す。\n
     *  呼び出し元は peers_mutex を取得してから呼び出すこと。
     *
     *  @param[in]      ctx         セッション コンテキスト。
     *  @param[in,out]  peer        対象ピア コンテキスト。
     *  @param[in]      path_idx    クリアするパスのインデックス (ctx->sock[] の添字)。
     */
    extern void peer_path_clear(const PotrContext *ctx, PotrPeerContext *peer, int path_idx);

    /**
     *  @brief  ピア リソースを解放してスロットをクリアする。
     *
     *  ウィンドウ破棄・frag_buf 解放・send_window_mutex 解放・スロット クリアを行う。\n
     *  呼び出し元は peers_mutex を取得してから呼び出すこと。\n
     *  FIN の送信は呼び出し元が行うこと (本関数は送信しない)。
     *
     *  @param[in,out]  ctx     セッション コンテキスト。
     *  @param[in,out]  peer    解放するピア コンテキスト。
     */
    extern void peer_free(PotrContext *ctx, PotrPeerContext *peer);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_PEER_TABLE_H */
