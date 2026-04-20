/**
 *******************************************************************************
 *  @file           potrContext.h
 *  @brief          セッションコンテキスト内部定義ヘッダー。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @details
 *  PotrHandle の実体定義。ライブラリ外部には公開しない。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef POTR_CONTEXT_H
#define POTR_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

#include <com_util/base/platform.h>
#include <porter_type.h>

#include "protocol/window.h"
#include <com_util/compress/compress.h>
#include <com_util/crypto/crypto.h>
#include "infra/potrSendQueue.h"
#include "infra/potrPlatform.h"

/** TCP 通信種別 (POTR_TYPE_TCP / POTR_TYPE_TCP_BIDIR) か判定する。 */
static inline int potr_is_tcp_type(PotrType t)
{
    return t == POTR_TYPE_TCP || t == POTR_TYPE_TCP_BIDIR;
}

/** RAW 系通信種別 (POTR_TYPE_*_RAW) か判定する。 */
static inline int potr_is_raw_type(PotrType t)
{
    return t == POTR_TYPE_UNICAST_RAW   ||
           t == POTR_TYPE_MULTICAST_RAW ||
           t == POTR_TYPE_BROADCAST_RAW;
}

/** RAW 系通信種別をベース通信種別に変換する (非 RAW 型はそのまま返す)。 */
static inline PotrType potr_raw_base_type(PotrType t)
{
    switch (t)
    {
        case POTR_TYPE_UNICAST_RAW:   return POTR_TYPE_UNICAST;
        case POTR_TYPE_MULTICAST_RAW: return POTR_TYPE_MULTICAST;
        case POTR_TYPE_BROADCAST_RAW: return POTR_TYPE_BROADCAST;
        case POTR_TYPE_UNICAST:
        case POTR_TYPE_MULTICAST:
        case POTR_TYPE_BROADCAST:
        case POTR_TYPE_UNICAST_BIDIR:
        case POTR_TYPE_UNICAST_BIDIR_N1:
        case POTR_TYPE_TCP:
        case POTR_TYPE_TCP_BIDIR:
        default:                      return t;
    }
}

/** 片方向 UDP 系通信種別 (type 1-6) か判定する。 */
static inline int potr_is_oneway_udp_type(PotrType t)
{
    PotrType base = potr_raw_base_type(t);

    return base == POTR_TYPE_UNICAST
        || base == POTR_TYPE_MULTICAST
        || base == POTR_TYPE_BROADCAST;
}

/** open 直後の即時 PING を使う通信種別か判定する。 */
static inline int potr_type_uses_immediate_health_ping(PotrType t)
{
    return !potr_is_oneway_udp_type(t);
}

/** volatile な path_ping_state 配列を通常配列へコピーする。 */
static inline void potr_copy_path_ping_state(uint8_t *dst,
                                             const volatile uint8_t *src,
                                             size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        dst[i] = src[i];
    }
}

/** volatile な path_ping_state 配列を同一値で初期化する。 */
static inline void potr_fill_path_ping_state(volatile uint8_t *dst,
                                             uint8_t value,
                                             size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        dst[i] = value;
    }
}

/** NACK 重複抑制リングバッファのスロット数 (POTR_MAX_PATH × 2)。 */
#define POTR_NACK_DEDUP_SLOTS 8U

/** NACK 重複抑制バッファの 1 エントリ。 */
typedef struct
{
    uint32_t ack_num; /**< 再送または REJECT した ack_num。 */
    uint32_t _pad;    /**< パディング (time_ms を 8 バイト境界に揃える)。 */
    uint64_t time_ms; /**< 処理時刻 (ms、単調増加)。0 = 未使用スロット。 */
} PotrNackDedupEntry;

/**
 *  @brief  N:1 モードにおける個別ピアのコンテキスト。
 *
 *  @details
 *  is_multi_peer == 1 のとき有効。ピアごとに独立した送受信状態を保持する。\n
 *  ピアテーブル (PotrContext_::peers[]) に配置される。
 */
typedef struct PotrPeerContext_
{
    PotrPeerId  peer_id; /**< 外部公開用ピア識別子 (単調増加カウンタから付与)。 */
    int         active;  /**< 1: 有効スロット, 0: 空き。 */

    /* 自セッション (このピア宛の送信に使用) */
    uint32_t session_id;        /**< 自セッション識別子 (乱数)。 */
    uint32_t _pad_session;      /**< パディング (session_tv_sec を 8 バイト境界に揃える)。 */
    int64_t  session_tv_sec;    /**< 自セッション開始時刻 秒部。 */
    int32_t  session_tv_nsec;   /**< 自セッション開始時刻 ナノ秒部。 */

    /* ピアセッション追跡 */
    uint32_t peer_session_id;       /**< 追跡中のピアセッション識別子。 */
    int64_t  peer_session_tv_sec;   /**< 追跡中のピアセッション開始時刻 秒部。 */
    int32_t  peer_session_tv_nsec;  /**< 追跡中のピアセッション開始時刻 ナノ秒部。 */
    int      peer_session_known;    /**< ピアセッションが初期化済みか (0: 未初期化)。 */

    /* 送受信ウィンドウ (ピアごと独立) */
    PotrWindow send_window;        /**< 送信ウィンドウ (NACK 再送用)。 */
    com_util_mutex_t  send_window_mutex;  /**< send_window 保護 (送信・受信・ヘルスチェックスレッド競合)。 */
    PotrWindow recv_window;        /**< 受信ウィンドウ (順序整列)。 */
    int        send_has_data;      /**< 現セッションで DATA を 1 件以上送信済みか (1: 送信済み, 0: 未送信)。 */
    uint32_t   _pad_send_has_data; /**< パディング。 */

    /* フラグメント結合 (ピアごと独立) */
    uint8_t *frag_buf;       /**< フラグメント結合バッファ (動的確保)。 */
    size_t   frag_buf_len;   /**< 現在のデータ長。 */
    int      frag_compressed;/**< 圧縮フラグ (非 0: 圧縮あり)。 */

    /* ヘルスチェック */
    volatile int     health_alive;                         /**< 疎通状態 (1: alive, 0: dead/未接続)。 */
    int              path_logical_alive[POTR_MAX_PATH];   /**< パスごとの論理接続状態 (1: connected, 0: disconnected)。 */
    volatile uint8_t path_ping_state[POTR_MAX_PATH];       /**< 自端の各パス PING 受信状態 (POTR_PING_STATE_*)。受信スレッドが更新、ヘルススレッドが読む。 */
    uint8_t          remote_path_ping_state[POTR_MAX_PATH];/**< 相手端から PING ペイロードで受信した各パス受信状態 (POTR_PING_STATE_*)。 */
    int64_t      last_recv_tv_sec;  /**< 最終受信時刻 秒部 (CLOCK_MONOTONIC)。0 = 未受信。 */
    int32_t      last_recv_tv_nsec; /**< 最終受信時刻 ナノ秒部。 */
    uint32_t     _pad_nack_dedup;   /**< パディング (nack_dedup_buf を 8 バイト境界に揃える)。 */

    /* NACK 重複抑制 */
    PotrNackDedupEntry nack_dedup_buf[POTR_NACK_DEDUP_SLOTS]; /**< NACK 重複抑制バッファ。 */
    uint8_t            nack_dedup_next;                        /**< 次に書き込むスロット。 */
    uint8_t            _pad_reorder[3];                        /**< パディング (reorder_pending を 4 バイト境界に揃える)。 */

    /* リオーダーバッファタイムアウト管理 */
    int      reorder_pending;       /**< リオーダー待機中 (1: 待機中, 0: 待機なし)。 */
    uint32_t reorder_nack_num;      /**< 待機中の欠番通番。 */
    uint32_t _pad_reorder_dl;       /**< パディング (reorder_deadline_sec を 8 バイト境界に揃える)。 */
    int64_t  reorder_deadline_sec;  /**< タイムアウト期限 秒部 (CLOCK_MONOTONIC)。 */
    int32_t  reorder_deadline_nsec; /**< タイムアウト期限 ナノ秒部。 */

    /* pending FIN 管理 (FIN を先受信したが受信ウィンドウが未追い付きの場合) */
    int      pending_fin;    /**< FIN 受信ペンディング中 (1: 待機中, 0: なし)。 */
    uint32_t fin_target_seq; /**< FIN の ack_num に埋め込まれた受信完了目標 next_seq。 */

    /* マルチパス: ピアごとの送信先 (recvfrom で学習)
     * インデックスは ctx->sock[] / src_addr[] と直接対応する。
     * 未使用スロットは dest_addr[k].sin_family == 0 (AF_UNSPEC) で判定する。 */
    struct sockaddr_in dest_addr[POTR_MAX_PATH];          /**< 送信先ソケットアドレス (インデックス = ctx->sock[] の添字)。未使用スロットは sin_family == 0。 */
    int                n_paths;                           /**< アクティブパス数。ループ境界には使わず管理カウンタとして使用する。 */
    int64_t            path_last_recv_sec[POTR_MAX_PATH]; /**< パスごとの最終受信時刻 秒部。未使用スロットは 0。 */
    int32_t            path_last_recv_nsec[POTR_MAX_PATH];/**< パスごとの最終受信時刻 ナノ秒部。 */
} PotrPeerContext;

/**
 *  @brief  セッションコンテキスト構造体。PotrHandle の実体。
 */
struct PotrContext_
{
    PotrRecvCallback callback;                         /**< 受信コールバック。 */
    com_util_mutex_t        callback_mutex;                   /**< コールバック直列化用ミューテックス。 */
    com_util_thread_t       recv_thread[POTR_MAX_PATH];       /**< 受信スレッドハンドル (path ごと)。 */
    com_util_thread_t       health_thread[POTR_MAX_PATH];     /**< ヘルスチェックスレッドハンドル (path ごと、TCP: 全ロール)。 */
    com_util_mutex_t        health_mutex[POTR_MAX_PATH];      /**< ヘルスチェックスレッド停止用ミューテックス (path ごと)。 */
    com_util_condvar_t      health_wakeup[POTR_MAX_PATH];     /**< ヘルスチェックスレッドを即時起床させる条件変数 (path ごと)。 */
    PotrServiceDef   service;      /**< サービス定義。 */
    PotrGlobalConfig global;       /**< プロトコル別のグローバル既定値。 */
    uint32_t         health_interval_ms; /**< 通信種別とサービス上書きを解決した実効 PING 送信間隔。 */
    PotrWindow       send_window;       /**< 送信バッファ (過去 N パケット保持。NACK 再送・REJECT 判定に使用)。 */
    com_util_mutex_t        send_window_mutex; /**< send_window 保護用ミューテックス (送信スレッド・ヘルスチェックスレッド・受信スレッドが競合するため)。 */
    PotrWindow       recv_window;       /**< 受信ウィンドウ (順序整列・欠番検出)。 */
    uint32_t         health_timeout_ms;  /**< 通信種別とサービス上書きを解決した実効受信タイムアウト。 */

    /* マルチパス: ソケット配列 */
    PotrSocket         sock[POTR_MAX_PATH];              /**< 各パスの UDP ソケット。 */
    int                n_path;                               /**< 有効パス数。 */

    volatile int     running[POTR_MAX_PATH];        /**< 受信スレッド実行フラグ (1: 実行中, 0: 停止)。path ごと。 */
    volatile int     health_running[POTR_MAX_PATH]; /**< ヘルスチェックスレッド実行フラグ (1: 実行中, 0: 停止)。path ごと。 */
    volatile int     health_send_immediate[POTR_MAX_PATH]; /**< オープン時割り込み PING フラグ。health_sleep() 冒頭でチェック・クリア。 */
    volatile int     health_alive;                         /**< 疎通状態 (1: alive, 0: dead/未接続)。UDP 用。受信者が管理。 */
    int              path_logical_alive[POTR_MAX_PATH];    /**< パスごとの論理接続状態 (1: connected, 0: disconnected)。 */
    volatile uint8_t path_ping_state[POTR_MAX_PATH];       /**< 自端の各パス PING 受信状態 (POTR_PING_STATE_*)。受信スレッドが更新、ヘルススレッドが読む。 */
    volatile uint64_t last_ping_send_ms;                  /**< 送信側 health 用 PING 最終送信時刻 (ms, CLOCK_MONOTONIC)。type 1-6 のみ使用。0 = 未送信。 */
    volatile uint64_t last_valid_data_send_ms;            /**< 送信側 health 用有効 DATA 最終送信時刻 (ms, CLOCK_MONOTONIC)。type 1-6 のみ使用。0 = 未送信。 */
    uint8_t          remote_path_ping_state[POTR_MAX_PATH];/**< 相手端から PING ペイロードで受信した各パス受信状態 (POTR_PING_STATE_*)。 */
    PotrRole         role;            /**< 役割 (POTR_ROLE_SENDER / POTR_ROLE_RECEIVER)。 */

    /* 解決済みアドレス (各パス分) */
    struct in_addr     src_addr_resolved[POTR_MAX_PATH]; /**< 解決済み送信元 IPv4 アドレス。 */
    struct in_addr     dst_addr_resolved[POTR_MAX_PATH]; /**< 解決済み宛先 IPv4 アドレス (unicast のみ)。 */
    struct sockaddr_in dest_addr[POTR_MAX_PATH];         /**< 送信先ソケットアドレス (送信者が sendto に使用)。 */

    /* 自セッション識別子 (potrOpenService 時に決定) */
    int64_t          session_tv_sec;    /**< 自セッション開始時刻 秒部。 */
    uint32_t         session_id;        /**< 自セッション識別子 (乱数)。 */
    int32_t          session_tv_nsec;   /**< 自セッション開始時刻 ナノ秒部。 */

    /* 相手セッション追跡 (受信者が使用) */
    int64_t          peer_session_tv_sec;  /**< 追跡中の相手セッション開始時刻 秒部。 */
    uint32_t         peer_session_id;      /**< 追跡中の相手セッション識別子。 */
    int32_t          peer_session_tv_nsec; /**< 追跡中の相手セッション開始時刻 ナノ秒部。 */
    int              peer_session_known;   /**< 相手セッションが初期化済みか (0: 未初期化)。 */

    /* 受信者: パスごとの送信者ポートキャッシュ (src_port=0 対応) */
    uint16_t           peer_port[POTR_MAX_PATH]; /**< 各パスで観測した送信者ポート (NBO)。0 = 未観測。 */

    /* ヘルスチェック: 最終受信時刻 (受信者が使用。CLOCK_MONOTONIC 基準)。 */
    int32_t          last_recv_tv_nsec;   /**< 最終受信時刻 ナノ秒部。 */
    int64_t          last_recv_tv_sec;    /**< 最終受信時刻 秒部。0 = 未受信。 */

    /* 受信者: パスごとの最終受信時刻 (パス単位の peer_port クリア用。CLOCK_MONOTONIC 基準)。 */
    int64_t            path_last_recv_sec[POTR_MAX_PATH];  /**< パスごとの最終受信時刻 秒部。0 = 未受信。 */
    int32_t            path_last_recv_nsec[POTR_MAX_PATH]; /**< パスごとの最終受信時刻 ナノ秒部。 */

    size_t           frag_buf_len;       /**< フラグメント結合バッファの現在のデータ長 (バイト)。 */
    int              frag_compressed;   /**< フラグメント受信中の圧縮フラグ (非 0: 圧縮あり)。 */
    uint32_t         _pad_frag;         /**< パディング (frag_buf を 8 バイト境界に揃える)。 */
    uint8_t         *frag_buf;          /**< フラグメント結合バッファ (動的確保。max_message_size バイト)。 */
    uint8_t         *compress_buf;      /**< 圧縮・解凍用一時バッファ (動的確保)。 */
    size_t           compress_buf_size; /**< compress_buf のサイズ (バイト)。 */
    uint8_t         *crypto_buf;        /**< 暗号化・復号用一時バッファ (動的確保)。 */
    size_t           crypto_buf_size;   /**< crypto_buf のサイズ (バイト)。 */
    uint8_t         *recv_buf;          /**< 受信バッファ / 再送 wire 組立バッファ (動的確保。PACKET_HEADER_SIZE + max_payload バイト)。 */
    uint8_t         *send_wire_buf;     /**< 送信 wire 組立バッファ (動的確保。PACKET_HEADER_SIZE + max_payload バイト)。送信スレッドのみ使用。 */

    /* 非同期送信 (POTR_ROLE_SENDER のみ使用) */
    com_util_thread_t        send_thread;          /**< 送信スレッドハンドル。 */
    volatile int      send_thread_running;  /**< 送信スレッド実行フラグ (1: 実行中, 0: 停止)。 */
    uint32_t          _pad_send_thread;     /**< パディング (nack_dedup_buf を 8 バイト境界に揃える)。 */

    /* 送信者: NACK 重複抑制リングバッファ */
    PotrNackDedupEntry nack_dedup_buf[POTR_NACK_DEDUP_SLOTS]; /**< NACK 重複抑制エントリ配列。 */
    uint8_t            nack_dedup_next;                        /**< 次に書き込むスロットインデックス。 */
    uint8_t            _pad_nack_dedup[7];                     /**< パディング (reorder フィールドを 4 バイト境界に揃える)。 */
    int                send_has_data;                          /**< 現セッションで DATA を 1 件以上送信済みか (1: 送信済み, 0: 未送信)。 */
    volatile int       close_requested;                       /**< potrCloseService 開始後の新規送信禁止フラグ。 */
    volatile int       tcp_close_waiting_ack;                 /**< TCP close が FIN_ACK 待機中か。 */
    volatile int       tcp_close_ack_received;                /**< 期待する FIN_ACK を受信済みか。 */
    uint32_t           tcp_close_wait_target_seq;             /**< 待機中の FIN target 通番。 */
    uint32_t           tcp_close_ack_seq;                     /**< 受信済み FIN_ACK の ack_num。 */

    /* 受信者: リオーダーバッファタイムアウト管理 (reorder_timeout_ms > 0 のときのみ使用) */
    int      reorder_pending;        /**< リオーダー待機中か (1: 待機中、0: 待機なし)。 */
    uint32_t reorder_nack_num;       /**< 待機中の欠番通番。 */
    int64_t  reorder_deadline_sec;   /**< タイムアウト期限 秒部 (CLOCK_MONOTONIC)。 */
    int32_t  reorder_deadline_nsec;  /**< タイムアウト期限 ナノ秒部。 */

    /* pending FIN 管理 (FIN を先受信したが受信ウィンドウが未追い付きの場合) */
    int      pending_fin;    /**< FIN 受信ペンディング中 (1: 待機中, 0: なし)。 */
    uint32_t fin_target_seq; /**< FIN の ack_num に埋め込まれた受信完了目標 next_seq。 */

    uint32_t _pad_reorder;           /**< パディング (send_queue を 8 バイト境界に揃える)。 */

    PotrSendQueue  send_queue;          /**< 非同期送信キュー。 */

    /* N:1 マルチピアモード専用フィールド (is_multi_peer == 1 のときのみ有効) */
    int              is_multi_peer;    /**< 1: N:1 モード (src_addr/src_port 省略), 0: 1:1 モード。 */
    uint32_t         _pad_multi_peer; /**< パディング (peers を 8 バイト境界に揃える)。 */
    PotrPeerContext *peers;            /**< ピアテーブル (動的確保。max_peers エントリ)。 */
    int              max_peers;      /**< ピアテーブルサイズ (service.max_peers から取得)。 */
    int              n_peers;        /**< 現在の接続ピア数。 */
    com_util_mutex_t        peers_mutex;    /**< ピアテーブル保護用ミューテックス。 */
    uint32_t         next_peer_id;   /**< 次に発行するピア ID (単調増加、初期値 1)。 */

    /* --- TCP 接続管理 (POTR_TYPE_TCP / POTR_TYPE_TCP_BIDIR のみ有効) --- */
    PotrSocket         tcp_listen_sock[POTR_MAX_PATH]; /**< RECEIVER: listen ソケット (path ごと)。 */
    PotrSocket         tcp_conn_fd[POTR_MAX_PATH];     /**< アクティブ TCP 接続 fd (path ごと)。 */
    volatile int       tcp_active_paths;               /**< アクティブ TCP path 数 (0 = 全切断)。 */
    uint32_t           _pad_tcp_connected[2];          /**< パディング (tcp_send_mutex を 8 バイト境界に揃える。8 バイト確保)。 */
    com_util_mutex_t          tcp_send_mutex[POTR_MAX_PATH];  /**< TCP send() 排他制御 (path ごと)。送信スレッド・ヘルスチェックスレッド・recv スレッド競合防止。 */

    /* recv_window 保護 (TCP v2: 複数 recv スレッドが同一 recv_window にアクセスするため) */
    com_util_mutex_t          recv_window_mutex;              /**< recv_window 保護用ミューテックス。 */

    /* connect/accept スレッド */
    com_util_thread_t         connect_thread[POTR_MAX_PATH];         /**< SENDER: connect スレッド。RECEIVER: accept スレッド。path ごと。 */
    volatile int       connect_thread_running[POTR_MAX_PATH]; /**< connect スレッド実行フラグ (1: 実行中, 0: 停止)。path ごと。 */

    /* 切断通知 (recv/health スレッド → connect スレッドへの通知) */
    com_util_mutex_t          tcp_state_mutex;            /**< tcp_state_cv 保護用ミューテックス。tcp_active_paths のカウンタ更新も保護。 */
    com_util_condvar_t        tcp_state_cv;               /**< 切断通知・reconnect sleep の中断用条件変数。 */
    com_util_mutex_t          tcp_close_mutex;            /**< tcp_close_cv 保護用ミューテックス。 */
    com_util_condvar_t        tcp_close_cv;               /**< FIN_ACK 待機解除用条件変数。 */

    /* PING 受信追跡 (TCP recv スレッドが参照・更新。両端 PING 受信タイムアウト監視に使用) */
    volatile uint64_t  tcp_last_ping_recv_ms[POTR_MAX_PATH]; /**< TCP PING 最終受信時刻 (ms, CLOCK_MONOTONIC 基準)。path ごと。接続確立時に現在時刻で初期化。受信タイムアウト判定に使用。 */

    /* 送信バッファ満杯ログ抑制 (TCP v2 送信スレッド用) */
    int                buf_full_suppress_cnt[POTR_MAX_PATH];     /**< path ごとの送信バッファ満杯ログ抑制カウンタ (0: 抑制なし、1-10: 抑制中)。 */

    /* TCP セッション確立排他制御 (RECEIVER TCP のみ使用)。
     * 複数 path の accept スレッドが並行して session_id 判定を行う際の競合を防ぐ。
     * potr_connect_thread_start で初期化、potr_connect_thread_stop で破棄する。 */
    com_util_mutex_t          session_establish_mutex;

    /* TCP 先読みバッファ (path ごと)。
     * accept スレッドが session 判定のために読み取った最初の 1 パケット分のバイト列を
     * recv スレッドに引き渡す。accept スレッドが書き込み後に recv スレッドを起動するため
     * 書き込み→起動→読み出しの順序が保証され、mutex は不要。
     * recv スレッドは起動直後に先読みパケットを処理し tcp_first_pkt_len[i] を 0 に戻す。 */
    uint8_t           *tcp_first_pkt_buf[POTR_MAX_PATH]; /**< 先読みパケットバッファ (動的確保、PACKET_HEADER_SIZE + max_payload バイト)。 */
    size_t             tcp_first_pkt_len[POTR_MAX_PATH];  /**< 先読みパケットのバイト数 (0: 先読みなし)。 */
};

#endif /* POTR_CONTEXT_H */
