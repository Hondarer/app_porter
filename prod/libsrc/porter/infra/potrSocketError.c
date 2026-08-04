/**
 *******************************************************************************
 *  @file           potrSocketError.c
 *  @brief          ソケット エラーの共通処理を実装します。
 *******************************************************************************
 */

#include <com_util/base/platform.h>
#include <porter/infra/potrSocketError.h>
#include <porter/infra/potrSocketErrorPlatform.h>
#include <porter/porter_result.h>
#include <stddef.h>

static void potr_socket_error_publish(com_util_error *detail_out, const com_util_error *detail)
{
    if (detail_out != NULL)
    {
        *detail_out = *detail;
    }
    com_util_error_set_last(detail);
}

void potr_socket_error_clear(com_util_error *detail_out)
{
    com_util_error detail;

    com_util_error_clear(&detail);
    potr_socket_error_publish(detail_out, &detail);
}

int potr_socket_error_report(com_util_error *detail_out)
{
    com_util_error detail;

    potr_socket_error_capture(&detail);
    potr_socket_error_publish(detail_out, &detail);
    const int result = potr_result_from_socket_error(&detail);
    if (result == POTR_OK)
    {
        return POTR_ERR_UNKNOWN;
    }
    return result;
}

int potr_socket_error_report_code(com_util_error *detail_out, const unsigned long code)
{
    com_util_error detail;

    potr_socket_error_capture_code(&detail, code);
    potr_socket_error_publish(detail_out, &detail);
    const int result = potr_result_from_socket_error(&detail);
    if (result == POTR_OK)
    {
        return POTR_ERR_UNKNOWN;
    }
    return result;
}

int potr_socket_error_report_invalid_argument(com_util_error *detail_out)
{
    com_util_error detail;

    potr_socket_error_capture_invalid_argument(&detail);
    potr_socket_error_publish(detail_out, &detail);
    return POTR_ERR_INVALID_ARGUMENT;
}

potr_socket_cause_t potr_socket_error_get_cause(const com_util_error *error)
{
    if ((error == NULL) || (error->domain == COM_UTIL_ERROR_DOMAIN_NONE))
    {
        return POTR_SOCKET_CAUSE_NONE;
    }

#if defined(PLATFORM_LINUX)
    if (error->domain == COM_UTIL_ERROR_DOMAIN_ERRNO)
    {
        return potr_socket_cause_from_errno((int)error->code);
    }
#elif defined(PLATFORM_WINDOWS)
    if (error->domain == COM_UTIL_ERROR_DOMAIN_WINDOWS)
    {
        return potr_socket_cause_from_windows_error(error->code);
    }
#endif /* PLATFORM_ */
    return POTR_SOCKET_CAUSE_OTHER;
}

int potr_socket_error_is(const com_util_error *error, const potr_socket_cause_t cause)
{
    return potr_socket_error_get_cause(error) == cause;
}

int potr_socket_error_is_connect_in_progress(const com_util_error *error)
{
    const potr_socket_cause_t cause = potr_socket_error_get_cause(error);
    return (cause == POTR_SOCKET_CAUSE_IN_PROGRESS) || (cause == POTR_SOCKET_CAUSE_WOULD_BLOCK);
}

int potr_result_from_socket_error(const com_util_error *error)
{
    int result;

    switch (potr_socket_error_get_cause(error))
    {
    case POTR_SOCKET_CAUSE_NONE:
        result = POTR_OK;
        break;
    case POTR_SOCKET_CAUSE_TIMED_OUT:
        result = POTR_ERR_TIMEOUT;
        break;
    case POTR_SOCKET_CAUSE_CONNECTION_REFUSED:
    case POTR_SOCKET_CAUSE_CONNECTION_RESET:
    case POTR_SOCKET_CAUSE_CONNECTION_ABORTED:
    case POTR_SOCKET_CAUSE_NOT_CONNECTED:
    case POTR_SOCKET_CAUSE_SHUTDOWN:
        result = POTR_ERR_DISCONNECTED;
        break;
    case POTR_SOCKET_CAUSE_OUT_OF_MEMORY:
        result = POTR_ERR_OUT_OF_MEMORY;
        break;
    case POTR_SOCKET_CAUSE_INVALID_ARGUMENT:
        result = POTR_ERR_INVALID_ARGUMENT;
        break;
    case POTR_SOCKET_CAUSE_UNSUPPORTED:
        result = POTR_ERR_UNSUPPORTED;
        break;
    case POTR_SOCKET_CAUSE_OTHER:
    case POTR_SOCKET_CAUSE_WOULD_BLOCK:
    case POTR_SOCKET_CAUSE_IN_PROGRESS:
    case POTR_SOCKET_CAUSE_INTERRUPTED:
    case POTR_SOCKET_CAUSE_HOST_UNREACHABLE:
    case POTR_SOCKET_CAUSE_NET_UNREACHABLE:
    case POTR_SOCKET_CAUSE_NET_DOWN:
    case POTR_SOCKET_CAUSE_ADDRESS_IN_USE:
    case POTR_SOCKET_CAUSE_ADDRESS_NOT_AVAILABLE:
    case POTR_SOCKET_CAUSE_MESSAGE_SIZE:
    case POTR_SOCKET_CAUSE_ACCESS_DENIED:
    case POTR_SOCKET_CAUSE_TOO_MANY_OPEN_FILES:
    case POTR_SOCKET_CAUSE_NOT_INITIALIZED:
        result = POTR_ERR_IO;
        break;
    default:
        result = POTR_ERR_IO;
        break;
    }
    return result;
}
