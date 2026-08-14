/**
 *******************************************************************************
 *  @file           potr_result.c
 *  @brief          com_util の詳細エラーを porter の結果コードへ変換する機能を実装します。
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

int potr_internal_result_from_error(const com_util_error *detail)
{
    int result;

    switch (com_util_error_get_cause(detail))
    {
    case COM_UTIL_CAUSE_NONE:
        result = POTR_OK;
        break;
    case COM_UTIL_CAUSE_TIMEOUT:
        result = POTR_ERR_TIMEOUT;
        break;
    case COM_UTIL_CAUSE_CONNECTION_REFUSED:
    case COM_UTIL_CAUSE_CONNECTION_RESET:
    case COM_UTIL_CAUSE_CONNECTION_ABORTED:
    case COM_UTIL_CAUSE_NOT_CONNECTED:
    case COM_UTIL_CAUSE_SHUTDOWN:
        result = POTR_ERR_DISCONNECTED;
        break;
    case COM_UTIL_CAUSE_OUT_OF_MEMORY:
        result = POTR_ERR_OUT_OF_MEMORY;
        break;
    case COM_UTIL_CAUSE_INVALID_ARGUMENT:
        result = POTR_ERR_INVALID_ARGUMENT;
        break;
    case COM_UTIL_CAUSE_UNSUPPORTED:
        result = POTR_ERR_UNSUPPORTED;
        break;
    case COM_UTIL_CAUSE_OTHER:
    case COM_UTIL_CAUSE_ACCESS_DENIED:
    case COM_UTIL_CAUSE_ALREADY_CONNECTED:
    case COM_UTIL_CAUSE_NOT_FOUND:
    case COM_UTIL_CAUSE_ALREADY_EXISTS:
    case COM_UTIL_CAUSE_SHARING_VIOLATION:
    case COM_UTIL_CAUSE_NOT_A_DIRECTORY:
    case COM_UTIL_CAUSE_IS_A_DIRECTORY:
    case COM_UTIL_CAUSE_DIRECTORY_NOT_EMPTY:
    case COM_UTIL_CAUSE_NAME_TOO_LONG:
    case COM_UTIL_CAUSE_DISK_FULL:
    case COM_UTIL_CAUSE_BUSY:
    /* com_util がシグナルによる中断を吸収するため、ソケット経路で本要因が返ることはない。
       Windows の I/O キャンセル由来では返りうるため case を残す。 */
    case COM_UTIL_CAUSE_INTERRUPTED:
    case COM_UTIL_CAUSE_BROKEN_PIPE:
    case COM_UTIL_CAUSE_TOO_MANY_OPEN_FILES:
    case COM_UTIL_CAUSE_READ_ONLY:
    case COM_UTIL_CAUSE_BUFFER_TOO_SMALL:
    case COM_UTIL_CAUSE_IO_ERROR:
    case COM_UTIL_CAUSE_WOULD_BLOCK:
    case COM_UTIL_CAUSE_IN_PROGRESS:
    case COM_UTIL_CAUSE_ADDRESS_IN_USE:
    case COM_UTIL_CAUSE_ADDRESS_NOT_AVAILABLE:
    case COM_UTIL_CAUSE_NETWORK_DOWN:
    case COM_UTIL_CAUSE_NETWORK_UNREACHABLE:
    case COM_UTIL_CAUSE_HOST_UNREACHABLE:
    case COM_UTIL_CAUSE_MESSAGE_SIZE:
    case COM_UTIL_CAUSE_NOT_INITIALIZED:
    default:
        result = POTR_ERR_IO;
        break;
    }

    return result;
}

/* Doxygen コメントは、ヘッダーに記載 */

int potr_internal_result_from_socket_result(int com_util_result, const com_util_error *detail)
{
    if (com_util_result == COM_UTIL_OK)
    {
        return POTR_OK;
    }
    if (com_util_result == COM_UTIL_ERR_EOF)
    {
        return POTR_ERR_EOF;
    }
    if (com_util_result == COM_UTIL_ERR_INVALID_ARGUMENT)
    {
        return POTR_ERR_INVALID_ARGUMENT;
    }

    return potr_internal_result_from_error(detail);
}
