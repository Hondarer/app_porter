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

void potr_socket_error_capture(com_util_error *error)
{
    const int error_code = WSAGetLastError();
    com_util_error_capture_windows_error(error, (unsigned long)error_code);
}

void potr_socket_error_capture_code(com_util_error *error, const unsigned long code)
{
    com_util_error_capture_windows_error(error, code);
}

void potr_socket_error_capture_invalid_argument(com_util_error *error)
{
    com_util_error_capture_windows_error(error, WSAEINVAL);
}

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
