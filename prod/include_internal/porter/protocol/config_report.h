/**
 *******************************************************************************
 *  @file           config_report.h
 *  @brief          設定の検証で見つかった誤りの報告先と JSON Pointer 構築を提供します。
 *******************************************************************************
 */

#ifndef POTR_PROTOCOL_CONFIG_REPORT_H
#define POTR_PROTOCOL_CONFIG_REPORT_H

#include <stddef.h>
#include <stdint.h>

#include <porter/porter_type.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /** 設定の誤りの報告先を保持します。 */
    typedef struct potr_internal_config_reporter
    {
        const char *config_path;      /**< トレース表示に使う設定ファイル パス。 */
        potr_config_issue_fn on_issue; /**< 誤り通知コールバック。NULL 可。 */
        void *user_data;              /**< コールバックへ渡す利用者データ。 */
        size_t issue_count;           /**< これまでに報告した誤りの累計件数。 */
        int trace_errors;             /**< 非 0 の場合、報告した誤りを ERROR トレースへ出力する。 */
        unsigned int pad;             /**< 明示的アラインメントです。0 を指定します。 */
    } potr_internal_config_reporter;

    /** RFC 6901 の JSON Pointer を保持する一時バッファーです。 */
    typedef struct potr_internal_config_path_buf
    {
        char inline_buf[256]; /**< 通常長キー向けのスタック領域。 */
        char *heap_buf;       /**< 256 byte を超える場合の動的領域。 */
        const char *ptr;      /**< 構築済みパス文字列 (常に非 NULL、失敗時は NULL)。 */
    } potr_internal_config_path_buf;

    /**
     *  @brief          設定の誤りの報告先を初期化します。
     *  @param[out]     reporter      初期化する報告先。NULL は不可です。
     *  @param[in]      config_path   設定ファイルのパス。NULL の場合は "(null)" として扱います。
     *  @param[in]      on_issue      誤りを受け取るコールバック。NULL 可。
     *  @param[in]      user_data     コールバックへ渡す値。
     *  @param[in]      trace_errors  ERROR トレースへ出力する場合は非 0、しない場合は 0。
     */
    extern void potr_internal_config_reporter_init(potr_internal_config_reporter *reporter, const char *config_path,
                                                   potr_config_issue_fn on_issue, void *user_data, int trace_errors);

    /**
     *  @brief          設定の誤り 1 件を報告先へ通知します。
     *  @param[in,out]  reporter        報告先。NULL の場合は何もしません。
     *  @param[in]      kind            誤りの種別。
     *  @param[in]      has_service_id  有効な service_id を伴う場合は非 0、それ以外は 0。
     *  @param[in]      service_id      has_service_id が非 0 の場合のサービス ID。
     *  @param[in]      key_path        RFC 6901 JSON Pointer 形式のパス。NULL は空文字列として扱います。
     *  @param[in]      message         英語 1 行の理由文。NULL は不可です。
     *  @param[in]      expected        期待する範囲または形式。NULL 可。
     *  @param[in]      line            構文誤りの行 (1 から)。構文誤り以外は 0。
     *  @param[in]      column          構文誤りの桁 (1 から、byte 単位)。構文誤り以外は 0。
     */
    extern void potr_internal_config_report_issue(potr_internal_config_reporter *reporter,
                                                    potr_config_issue_kind kind, int has_service_id,
                                                    int64_t service_id, const char *key_path, const char *message,
                                                    const char *expected, uint32_t line, uint32_t column);

    /**
     *  @brief          親の JSON Pointer にキー文字列を追加して RFC 6901 形式で構築します。
     *  @param[out]     buf      結果と記憶域を保持するバッファー。使用後に potr_internal_config_path_dispose() で解放します。
     *  @param[in]      parent   親の JSON Pointer。NULL または空文字列はルートとして扱います。
     *  @param[in]      segment  追加するキー文字列。'~' は "~0"、'/' は "~1" にエスケープします。
     *  @return         成功時は POTR_OK、引数が NULL の場合は POTR_ERR_INVALID_ARGUMENT、
     *                  メモリ確保失敗時は POTR_ERR_OUT_OF_MEMORY。
     */
    extern int potr_internal_config_path_push(potr_internal_config_path_buf *buf, const char *parent,
                                              const char *segment);

    /**
     *  @brief          親の JSON Pointer に配列要素の添字を追加して構築します。
     *  @param[out]     buf     結果と記憶域を保持するバッファー。
     *  @param[in]      parent  親の JSON Pointer。NULL または空文字列はルートとして扱います。
     *  @param[in]      index   0 から数えた配列要素の添字。
     *  @return         成功時は POTR_OK、引数が NULL の場合は POTR_ERR_INVALID_ARGUMENT、
     *                  メモリ確保失敗時は POTR_ERR_OUT_OF_MEMORY。
     */
    extern int potr_internal_config_path_push_index(potr_internal_config_path_buf *buf, const char *parent,
                                                    size_t index);

    /**
     *  @brief          potr_internal_config_path_buf が動的確保した領域があれば解放します。
     *  @param[in,out]  buf 解放対象のバッファー。NULL は何もしません。
     */
    extern void potr_internal_config_path_dispose(potr_internal_config_path_buf *buf);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_PROTOCOL_CONFIG_REPORT_H */
