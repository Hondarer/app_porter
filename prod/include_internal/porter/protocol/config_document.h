/**
 *******************************************************************************
 *  @file           config_document.h
 *  @brief          設定ファイルのバイナリ読み取りと JSONC 解析 (段階 1) を提供します。
 *******************************************************************************
 */

#ifndef POTR_PROTOCOL_CONFIG_DOCUMENT_H
#define POTR_PROTOCOL_CONFIG_DOCUMENT_H

#include <porter/protocol/config_report.h>

struct cJSON;

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          設定ファイルをバイナリ モードで読み取り、NUL 検査後に JSONC として解析します。
     *  @param[in]      config_path  設定ファイルのパス (UTF-8)。
     *  @param[in,out]  reporter     構文の誤りを通知する報告先。NULL 可 (誤り内容の破棄のみ)。
     *  @param[out]     root_out     成功時に解析した JSON ツリーを格納するポインター。
     *                               呼び出し元は cjson_app_cJSON_Delete() で解放する責務を持ちます。
     *                               失敗時は NULL を格納します。
     *  @return         POTR_OK、POTR_ERR_INVALID_ARGUMENT、POTR_ERR_INVALID_CONFIG、
     *                  POTR_ERR_IO、POTR_ERR_OUT_OF_MEMORY、または POTR_ERR_UNKNOWN。
     *
     *  Windows で 0x1A が終端扱いされることを避けるため "rb" で開き、setvbuf(_IONBF) 後に
     *  fread() で終端まで読み取って ferror() を確認します。\n
     *  生の NUL バイトと JSON 文字列内の "\u0000" エスケープを構文の誤りとして報告します。\n
     *  読み取りバッファー (拡張前の旧領域を含む) と作業チャンクは、すべての経路で容量全体を消去します。\n
     *  本関数は POTR_ERR_IO や POTR_ERR_OUT_OF_MEMORY のトレース出力を行いません。
     */
    extern int potr_internal_config_document_load(const char *config_path, potr_internal_config_reporter *reporter,
                                                  struct cJSON **root_out);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_PROTOCOL_CONFIG_DOCUMENT_H */
