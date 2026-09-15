#ifndef THREAD_RECV_SLOT_PRIVATE_H
#define THREAD_RECV_SLOT_PRIVATE_H

#include <porter/potr_context.h>
#include <porter/potr_peer_table.h>

/* ================================================================
 * 受信セッション スロット
 *
 * 1:1 モード (potr_context が直接保持) と N:1 モード (potr_internal_peer_context が
 * ピアごとに保持) で同じ意味を持つ受信状態フィールド群を、ポインター経由で
 * 一元的に参照するためのビュー。1:1 を「ピア数 1 のスロット」として扱う
 * ことで、受信処理関数の二重実装 (n1_* 系と通常系のペア) を排除する。
 * ================================================================ */
typedef struct thread_recv_slot
{
    potr_context *ctx;                /* 所属コンテキスト */
    potr_internal_peer_context *peer; /* N:1 のピア。1:1 モードでは NULL */
    potr_peer_id peer_id;             /* コールバック用ピア識別子 (1:1 は POTR_PEER_NA) */
    int pad;                          /* パディング (recv_window をポインター境界に揃える) */
    potr_internal_window *recv_window;
    potr_internal_window *send_window;   /* NACK 再送元 */
    cplat_local_lock *send_window_mutex; /* send_window 保護 */
    uint32_t *session_id;                /* 自セッション識別子 (制御パケット構築) */
    cplat_timespec *session_ts;          /* 自セッション開始時刻 */
    cplat_ipv4_endpoint *dest_addr;      /* 送信先配列 (POTR_MAX_PATH) */
    potr_internal_nack_dedup_entry *nack_dedup_buf;
    uint8_t *nack_dedup_next;
    uint32_t *peer_session_id;
    cplat_timespec *peer_session_ts;
    int *peer_session_known;
    int *reorder_pending;
    uint32_t *reorder_nack_num;
    cplat_timespec *reorder_deadline_ts;
    int *pending_fin;
    uint32_t *fin_target_seq;
    uint8_t *frag_buf;
    size_t *frag_buf_len;
    int *frag_compressed;
    volatile int *health_alive;
    cplat_timespec *last_recv_ts;
    cplat_timespec *path_last_recv_ts;
    volatile uint8_t *path_ping_state;
    uint8_t *remote_path_ping_state; /* 相手端の経路受信状態 (POTR_MAX_PATH) */
} thread_recv_slot;

/**
 * @brief 1:1 の受信状態を参照するビューを構成します。
 * @param[out] slot 構成するビュー。NULL は許可しません。
 * @param[in,out] ctx 参照先のコンテキスト。NULL は許可しません。
 * @note 所有権は移動しません。ビューの使用中は ctx とそのバッファーを保持してください。
 *       ctx の配置が確定した後、受信処理の開始時に呼びます。
 */
void thread_recv_slot_init_ctx(thread_recv_slot *slot, potr_context *ctx);

/**
 * @brief N:1 のピアごとの受信状態を参照するビューを構成します。
 * @param[out] slot 構成するビュー。NULL は許可しません。
 * @param[in,out] ctx 所属コンテキスト。NULL は許可しません。
 * @param[in,out] peer 参照先のピア。NULL は許可しません。
 * @note 所有権は移動しません。ビューの使用中は ctx と peer を保持してください。
 *       ピアの作成後、受信処理の開始時に呼びます。
 */
void thread_recv_slot_init_peer(thread_recv_slot *slot, potr_context *ctx, potr_internal_peer_context *peer);

/**
 * @brief 順序整列済みエレメントを結合・展開して DATA コールバックへ渡します。
 * @param[in,out] slot 構成済みの受信状態ビュー。NULL は許可しません。
 * @param[in] elem 外側パケットから展開したエレメント。NULL は許可しません。
 * @note 呼び出し側は順序整列を済ませ、同じ受信状態と展開バッファーへの並行操作を避けます。
 *       所有権は移動しません。コールバックのデータは呼び出し中だけ有効です。
 *       未接続、結合途中、展開失敗の場合は DATA を通知しません。
 */
void thread_recv_slot_deliver_payload_elem(thread_recv_slot *slot, const potr_packet *elem);

#endif
