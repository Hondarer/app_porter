/**
 *******************************************************************************
 *  @file           path_group.h
 *  @brief          優先グループの選択状態と判定を定義します。
 *
 *******************************************************************************
 */
#ifndef POTR_PROTOCOL_PATH_GROUP_H
#define POTR_PROTOCOL_PATH_GROUP_H

#include <stdint.h>
#include <porter/porter_type.h>
#include <porter/porter_const.h>

/** グループ選択の継続状態。ゼロ初期化して使用します。 */
typedef struct potr_internal_path_group_state
{
    uint64_t candidate_since_ms; /**< 切り戻し候補の連続到達開始時刻 [ms]。 */
    uint8_t selected; /**< 選択中のグループ。0 は未選択。 */
    uint8_t candidate; /**< 切り戻し候補。0 は候補なし。 */
    uint8_t confirmed; /**< 到達状態を一度以上確認したか。 */
    uint8_t resend_group; /**< TCP の送り直しが完了していないグループ。 */
    uint32_t padding; /**< 8 [byte] 境界のパディング。 */
} potr_internal_path_group_state;

/** 現在時刻と経路状態のスナップショット。 */
typedef struct potr_internal_path_group_input
{
    uint64_t now_ms; /**< 単調増加時刻 [ms]。 */
    uint64_t started_ms; /**< サービス起動時刻 [ms]。 */
    uint32_t failback_ms; /**< 設定値。0 は timeout_ms を使用。 */
    uint32_t timeout_ms; /**< 実効受信タイムアウト [ms]。 */
    uint32_t interval_ms; /**< 実効監視間隔 [ms]。 */
    potr_type type; /**< 通信種別。 */
    uint8_t group[POTR_MAX_PATH]; /**< 経路のグループ番号 (1〜4)。 */
    uint8_t available[POTR_MAX_PATH]; /**< ソケットまたは接続のある経路。 */
    uint8_t reachable[POTR_MAX_PATH]; /**< 論理接続中の経路。 */
    uint8_t report_received; /**< 受信報告を一度以上受信したか。 */
    uint8_t reported_groups; /**< 全報告受信者に届くグループ。ビット 0 がグループ 1。 */
    uint8_t padding[6]; /**< 8 [byte] 境界のパディング。 */
} potr_internal_path_group_input;

/** 判定結果。経路マスクのビット 0 は経路 0 を表します。 */
typedef struct potr_internal_path_group_result
{
    uint8_t data_paths; /**< DATA の送出経路 (切り戻し待機中は 2 グループ)。 */
    uint8_t selected_paths; /**< 再送と REJECT の送出経路。 */
    uint8_t previous_group; /**< 選択変更前のグループ。 */
    uint8_t next_group; /**< 選択変更後のグループ。 */
    uint8_t changed; /**< 初回選択を除く選択変更。 */
    uint8_t tcp_resend; /**< 不達による切り替えに伴う送り直しが必要か。 */
    uint8_t disconnected; /**< 双方向または TCP の全グループ不達。 */
    uint8_t padding; /**< パディング。 */
} potr_internal_path_group_result;

struct potr_internal_peer_context;
#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/**
 *  @brief          入力と前状態だけから次状態と送出経路を決定します。
 *  @param[in]      input 判定入力。NULL は許可しません。
 *  @param[in]      before 前状態。NULL は許可しません。
 *  @param[out]     after 次状態。before と同じ領域も使用できます。NULL は許可しません。
 *  @param[out]     result 判定結果。NULL は許可しません。
 *  @note           NULL または範囲外の前状態では無処理で戻ります。
 *                  時計、ログ、共有状態にはアクセスしません。所有権は移動しません。
 *  @par            スレッド セーフ
 *  呼び出し側が入出力領域への並行更新を排他します。
 */
void potr_internal_path_group_evaluate(const potr_internal_path_group_input *input,
    const potr_internal_path_group_state *before, potr_internal_path_group_state *after,
    potr_internal_path_group_result *result);
/**
 *  @brief          既存の論理経路状態から送出経路を選択し、変更をログに記録します。
 *  @param[in,out]  ctx サービス。NULL は許可しません。
 *  @param[in,out]  peer N:1 の対象ピア。1:1 は NULL。
 *  @param[out]     result 選択結果。NULL は許可しません。
 *  @return         成功時 POTR_OK、時計取得失敗時 POTR_ERR_IO、NULL 時 POTR_ERR_INVALID_ARGUMENT。
 *  @note           グループ状態と報告表、論理経路状態は callback_mutex で保護します。
 *                  N:1 は呼び出し側が peers_mutex を保持します。所有権は移動しません。
 */
int potr_internal_path_group_select(potr_context *ctx, struct potr_internal_peer_context *peer,
    potr_internal_path_group_result *result);
#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* POTR_PROTOCOL_PATH_GROUP_H */
