/**
 *******************************************************************************
 *  @file           api_open_internal.h
 *  @brief          検証済み設定からサービスを開く内部エントリを定義します。
 *******************************************************************************
 */

#ifndef POTR_API_OPEN_INTERNAL_H
#define POTR_API_OPEN_INTERNAL_H

#include <porter/porter_type.h>

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          検証済みのグローバル設定とサービス定義からサービスを開きます。
     *  @param[in]      global      検証済みのグローバル設定。NULL は不可。
     *  @param[in]      service     検証済みのサービス定義。NULL は不可。
     *  @param[in]      role        開く役割 (POTR_ROLE_SENDER または POTR_ROLE_RECEIVER)。
     *  @param[in]      callback    受信コールバック。
     *  @param[out]     handle      成功時にハンドルを格納するポインター。NULL は不可。
     *  @return         POTR_OK または POTR_ERR_*。
     *
     *  設定ファイル読み込み側 (段階 1..4 済み) と potr_service_open() (構造体検査後に呼び出し) が共用します。
     */
    extern int api_service_open_validated(const potr_global_config *global, const potr_service_def *service,
                                          potr_role role, potr_recv_fn callback, potr_context **handle);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* POTR_API_OPEN_INTERNAL_H */
