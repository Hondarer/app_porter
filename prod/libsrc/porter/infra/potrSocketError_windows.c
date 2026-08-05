/**
 *******************************************************************************
 *  @file           potrSocketError_windows.c
 *  @brief          Windows のソケット エラーを捕捉、分類します。
 *******************************************************************************
 */

#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #include <com_util/base/windows_sdk.h>
    #include <porter/infra/potrSocketErrorPlatform.h>

/**
 *  @brief          Winsock エラー コードを、ソケット要因に基づく共通結果コードへ変換します。
 *  @param[in]      error_code Winsock エラー値。
 *  @return         対応する COM_UTIL_ERR_* を返します。
 *
 *  Winsock のエラー値は Win32 の GetLastError() 体系と番号空間が異なるため、
 *  com_util_result_from_windows_error() では正しく分類できません。\n
 *  そのため、ソケット要因の分類結果から共通結果コードへ変換します。
 */
static int potr_socket_error_result_from_windows_code(const unsigned long error_code)
{
    int result;

    switch (potr_socket_cause_from_windows_error(error_code))
    {
    case POTR_SOCKET_CAUSE_NONE:
        result = COM_UTIL_OK;
        break;
    case POTR_SOCKET_CAUSE_TIMED_OUT:
        result = COM_UTIL_ERR_TIMEOUT;
        break;
    case POTR_SOCKET_CAUSE_INVALID_ARGUMENT:
        result = COM_UTIL_ERR_INVALID_ARGUMENT;
        break;
    case POTR_SOCKET_CAUSE_ACCESS_DENIED:
        result = COM_UTIL_ERR_PERMISSION_DENIED;
        break;
    case POTR_SOCKET_CAUSE_OUT_OF_MEMORY:
        result = COM_UTIL_ERR_OUT_OF_MEMORY;
        break;
    case POTR_SOCKET_CAUSE_UNSUPPORTED:
        result = COM_UTIL_ERR_UNSUPPORTED;
        break;
    default:
        result = COM_UTIL_ERR_UNKNOWN;
        break;
    }
    return result;
}

/**
 *  @brief          Winsock エラー コードを詳細エラーへ格納します。
 *  @param[out]     error      格納先。NULL 可。
 *  @param[in]      error_code Winsock エラー値。0 の場合は空の値を格納します。
 */
static void potr_socket_error_store(com_util_error *error, const unsigned long error_code)
{
    if (error != NULL)
    {
        if (error_code == 0UL)
        {
            error->domain = COM_UTIL_ERROR_DOMAIN_NONE;
            error->result = COM_UTIL_OK;
            error->code = 0UL;
        }
        else
        {
            error->domain = COM_UTIL_ERROR_DOMAIN_WINDOWS;
            error->result = potr_socket_error_result_from_windows_code(error_code);
            error->code = error_code;
        }
    }
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_socket_error_capture(com_util_error *error)
{
    const int error_code = WSAGetLastError();
    potr_socket_error_store(error, (unsigned long)error_code);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_socket_error_capture_code(com_util_error *error, const unsigned long code)
{
    potr_socket_error_store(error, code);
}

/* Doxygen コメントは、ヘッダーに記載 */

void potr_socket_error_capture_invalid_argument(com_util_error *error)
{
    potr_socket_error_store(error, WSAEINVAL);
}

/* Doxygen コメントは、ヘッダーに記載 */

potr_socket_cause_t potr_socket_cause_from_windows_error(const unsigned long error_code)
{
    potr_socket_cause_t cause;

    switch (error_code)
    {
    case 0:
        cause = POTR_SOCKET_CAUSE_NONE;
        break;
    case WSAEWOULDBLOCK:
        cause = POTR_SOCKET_CAUSE_WOULD_BLOCK;
        break;
    case WSAEINPROGRESS:
        cause = POTR_SOCKET_CAUSE_IN_PROGRESS;
        break;
    case WSAEINTR:
        cause = POTR_SOCKET_CAUSE_INTERRUPTED;
        break;
    case WSAECONNREFUSED:
        cause = POTR_SOCKET_CAUSE_CONNECTION_REFUSED;
        break;
    case WSAECONNRESET:
        cause = POTR_SOCKET_CAUSE_CONNECTION_RESET;
        break;
    case WSAECONNABORTED:
        cause = POTR_SOCKET_CAUSE_CONNECTION_ABORTED;
        break;
    case WSAENOTCONN:
        cause = POTR_SOCKET_CAUSE_NOT_CONNECTED;
        break;
    case WSAETIMEDOUT:
        cause = POTR_SOCKET_CAUSE_TIMED_OUT;
        break;
    case WSAEHOSTUNREACH:
        cause = POTR_SOCKET_CAUSE_HOST_UNREACHABLE;
        break;
    case WSAENETUNREACH:
        cause = POTR_SOCKET_CAUSE_NET_UNREACHABLE;
        break;
    case WSAENETDOWN:
        cause = POTR_SOCKET_CAUSE_NET_DOWN;
        break;
    case WSAEADDRINUSE:
        cause = POTR_SOCKET_CAUSE_ADDRESS_IN_USE;
        break;
    case WSAEADDRNOTAVAIL:
        cause = POTR_SOCKET_CAUSE_ADDRESS_NOT_AVAILABLE;
        break;
    case WSAEMSGSIZE:
        cause = POTR_SOCKET_CAUSE_MESSAGE_SIZE;
        break;
    case WSAEACCES:
        cause = POTR_SOCKET_CAUSE_ACCESS_DENIED;
        break;
    case WSAENOBUFS:
        cause = POTR_SOCKET_CAUSE_OUT_OF_MEMORY;
        break;
    case WSAEMFILE:
        cause = POTR_SOCKET_CAUSE_TOO_MANY_OPEN_FILES;
        break;
    case WSAEINVAL:
    case WSAEFAULT:
        cause = POTR_SOCKET_CAUSE_INVALID_ARGUMENT;
        break;
    case WSAEAFNOSUPPORT:
    case WSAEPROTONOSUPPORT:
    case WSAEOPNOTSUPP:
        cause = POTR_SOCKET_CAUSE_UNSUPPORTED;
        break;
    case WSANOTINITIALISED:
        cause = POTR_SOCKET_CAUSE_NOT_INITIALIZED;
        break;
    case WSAESHUTDOWN:
        cause = POTR_SOCKET_CAUSE_SHUTDOWN;
        break;
    default:
        cause = POTR_SOCKET_CAUSE_OTHER;
        break;
    }
    return cause;
}

#endif /* PLATFORM_WINDOWS */
