/**
 *******************************************************************************
 *  @file           porter_result.h
 *  @brief          porter ライブラリ共通の結果コードを定義します。
 *  @author         Tetsuo Honda
 *  @date           2026/07/30
 *  @version        1.0.0
 *
 *  porter の公開 API および内部関数が戻り値として使用する共通の結果コードです。\n
 *  成功は @ref POTR_OK (0) のみとし、非 0 はすべて
 *  「要求した操作が完遂されなかった」ことを表します。エラーは負値です。
 *
 *  @ref POTR_ERR_UNKNOWN (-1) は、分類済みコードに該当しない
 *  その他のエラーを表します。
 *
 *  判定は @c != @ref POTR_OK の名前比較を正とします。
 *  全エラーが負値のため @c < 0 判定も等価ですが、名前比較を推奨します。
 *  @c -1 などの数値リテラルとの比較は行いません。
 *
 *  定義は、引数・状態、リソース・コンテナー、通信・プロトコル、制御の帯に
 *  分けて並べ、各帯に将来の追加用の余白を設けています。
 *
 *  @warning        帯は定義の整理と追加位置を示すためのものであり、範囲判定に
 *                  よる分類は本 API の契約に含めません。
 *                  @c rc @c <= @c -20 のような範囲比較で種別を判定せず、
 *                  個々のコード名との比較を使用してください。
 *
 *  @attention      各コードの値を変更する場合は、porter の公開 API、内部関数、
 *                  テスト、利用側コードへの影響を調査してください。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/* NOTE: このヘッダーは多数のソース ファイルから参照されるため、            */
/*       @hideincludedbygraph によって "Included by" グラフを無効にします。 */

#ifndef PORTER_RESULT_H
#define PORTER_RESULT_H

/**
 *  @defgroup       POTR_RESULT 共通結果コード
 *  @ingroup        PORTER_PUBLIC_API
 *
 *  porter の公開 API および内部関数が戻り値として使用する共通の結果コードです。\n
 *  成功は @ref POTR_OK (0) のみで、エラーは負値です。
 */

/**
 *  @ingroup        POTR_RESULT
 *  @{
 */
#define POTR_OK 0 /**< 処理に成功しました。 */

/* 分類不能 */
#define POTR_ERR_UNKNOWN (-1) /**< 分類済みコードに該当しない、その他のエラーです。 */

/* 引数・状態: -2 〜 -9 */
#define POTR_ERR_INVALID_ARGUMENT (-2) /**< API 引数または設定値が不正です (NULL、サイズ 0 など)。 */
#define POTR_ERR_UNSUPPORTED      (-3) /**< 現在の通信種別または状態では操作がサポートされていません。 */
#define POTR_ERR_NOT_FOUND        (-4) /**< 対象のサービス、ピア、またはエントリが存在しません。 */

/* リソース・コンテナー: -10 〜 -19 */
#define POTR_ERR_OUT_OF_MEMORY (-10) /**< メモリを確保できません。 */
#define POTR_ERR_FULL          (-11) /**< キューまたはウィンドウが満杯です。 */
#define POTR_ERR_EMPTY         (-12) /**< キューが空、または順序整列済みパケットが未着です。 */
#define POTR_ERR_OUT_OF_WINDOW (-13) /**< 受信ウィンドウの範囲外です。 */

/* 通信・プロトコル: -20 〜 -39 */
#define POTR_ERR_DISCONNECTED (-20) /**< 送信先が論理 CONNECTED 前または切断中です。 */
#define POTR_ERR_TIMEOUT      (-21) /**< 通信または待機がタイムアウトしました。 */
#define POTR_ERR_EOF          (-22) /**< TCP 接続の終端に達しました。 */
#define POTR_ERR_IO           (-23) /**< ファイルまたはネットワークの I/O に失敗しました。 */
#define POTR_ERR_PROTOCOL     (-24) /**< 受信データがプロトコル要件を満たしていません。 */

/* 制御: -40 〜 */
#define POTR_ERR_CANCELED (-40) /**< シャットダウンにより処理または待機を中断しました。 */
/** @} */

#endif /* PORTER_RESULT_H */
