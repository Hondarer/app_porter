/**
 *******************************************************************************
 *  @file           potr_result.c
 *  @brief          cplat の詳細エラーを porter の結果コードへ変換する機能を実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/08/12
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <porter/infra/potr_result.h>
#include <porter/porter_result.h>

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_result_from_error(const cplat_error *detail)
{
    int result;

    switch (cplat_error_get_cause(detail))
    {
    case CPLAT_CAUSE_NONE:
        result = POTR_OK;
        break;
    case CPLAT_CAUSE_TIMEOUT:
        result = POTR_ERR_TIMEOUT;
        break;
    case CPLAT_CAUSE_CONNECTION_REFUSED:
    case CPLAT_CAUSE_CONNECTION_RESET:
    case CPLAT_CAUSE_CONNECTION_ABORTED:
    case CPLAT_CAUSE_NOT_CONNECTED:
    case CPLAT_CAUSE_SHUTDOWN:
        result = POTR_ERR_DISCONNECTED;
        break;
    case CPLAT_CAUSE_OUT_OF_MEMORY:
        result = POTR_ERR_OUT_OF_MEMORY;
        break;
    case CPLAT_CAUSE_INVALID_ARGUMENT:
        result = POTR_ERR_INVALID_ARGUMENT;
        break;
    case CPLAT_CAUSE_UNSUPPORTED:
        result = POTR_ERR_UNSUPPORTED;
        break;
    case CPLAT_CAUSE_OTHER:
    case CPLAT_CAUSE_ACCESS_DENIED:
    case CPLAT_CAUSE_ALREADY_CONNECTED:
    case CPLAT_CAUSE_NOT_FOUND:
    case CPLAT_CAUSE_ALREADY_EXISTS:
    case CPLAT_CAUSE_SHARING_VIOLATION:
    case CPLAT_CAUSE_NOT_A_DIRECTORY:
    case CPLAT_CAUSE_IS_A_DIRECTORY:
    case CPLAT_CAUSE_DIRECTORY_NOT_EMPTY:
    case CPLAT_CAUSE_NAME_TOO_LONG:
    case CPLAT_CAUSE_DISK_FULL:
    case CPLAT_CAUSE_BUSY:
    /* cplat がシグナルによる中断を吸収するため、ソケット経路で本要因が返ることはない。
       Windows の I/O キャンセル由来では返りうるため case を残す。 */
    case CPLAT_CAUSE_INTERRUPTED:
    case CPLAT_CAUSE_BROKEN_PIPE:
    case CPLAT_CAUSE_TOO_MANY_OPEN_FILES:
    case CPLAT_CAUSE_READ_ONLY:
    case CPLAT_CAUSE_BUFFER_TOO_SMALL:
    case CPLAT_CAUSE_IO_ERROR:
    case CPLAT_CAUSE_WOULD_BLOCK:
    case CPLAT_CAUSE_IN_PROGRESS:
    case CPLAT_CAUSE_ADDRESS_IN_USE:
    case CPLAT_CAUSE_ADDRESS_NOT_AVAILABLE:
    case CPLAT_CAUSE_NETWORK_DOWN:
    case CPLAT_CAUSE_NETWORK_UNREACHABLE:
    case CPLAT_CAUSE_HOST_UNREACHABLE:
    case CPLAT_CAUSE_MESSAGE_SIZE:
    case CPLAT_CAUSE_NOT_INITIALIZED:
    default:
        result = POTR_ERR_IO;
        break;
    }

    return result;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_result_from_socket_result(int cplat_result, const cplat_error *detail)
{
    if (cplat_result == CPLAT_OK)
    {
        return POTR_OK;
    }
    if (cplat_result == CPLAT_ERR_EOF)
    {
        return POTR_ERR_EOF;
    }
    if (cplat_result == CPLAT_ERR_INVALID_ARGUMENT)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    return potr_internal_result_from_error(detail);
}
