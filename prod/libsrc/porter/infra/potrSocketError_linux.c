/**
 *******************************************************************************
 *  @file           potrSocketError_linux.c
 *  @brief          Linux のソケット エラーを捕捉、分類します。
 *******************************************************************************
 */

#include <com_util/base/platform.h>

#if defined(PLATFORM_LINUX)
    #include <errno.h>
    #include <porter/infra/potrSocketErrorPlatform.h>

void potr_socket_error_capture(com_util_error *error)
{
    const int error_code = errno;
    com_util_error_capture_errno(error, error_code);
}

void potr_socket_error_capture_code(com_util_error *error, const unsigned long code)
{
    com_util_error_capture_errno(error, (int)code);
}

void potr_socket_error_capture_invalid_argument(com_util_error *error)
{
    com_util_error_capture_errno(error, EINVAL);
}

potr_socket_cause_t potr_socket_cause_from_errno(const int error_code)
{
    potr_socket_cause_t cause;

    switch (error_code)
    {
    case 0:
        cause = POTR_SOCKET_CAUSE_NONE;
        break;
    case EAGAIN:
        cause = POTR_SOCKET_CAUSE_WOULD_BLOCK;
        break;
    #if defined(EWOULDBLOCK) && (EAGAIN != EWOULDBLOCK)
    case EWOULDBLOCK:
        cause = POTR_SOCKET_CAUSE_WOULD_BLOCK;
        break;
    #endif
    case EINPROGRESS:
        cause = POTR_SOCKET_CAUSE_IN_PROGRESS;
        break;
    case EINTR:
        cause = POTR_SOCKET_CAUSE_INTERRUPTED;
        break;
    case ECONNREFUSED:
        cause = POTR_SOCKET_CAUSE_CONNECTION_REFUSED;
        break;
    case ECONNRESET:
        cause = POTR_SOCKET_CAUSE_CONNECTION_RESET;
        break;
    case ECONNABORTED:
        cause = POTR_SOCKET_CAUSE_CONNECTION_ABORTED;
        break;
    case ENOTCONN:
        cause = POTR_SOCKET_CAUSE_NOT_CONNECTED;
        break;
    case ETIMEDOUT:
        cause = POTR_SOCKET_CAUSE_TIMED_OUT;
        break;
    case EHOSTUNREACH:
        cause = POTR_SOCKET_CAUSE_HOST_UNREACHABLE;
        break;
    case ENETUNREACH:
        cause = POTR_SOCKET_CAUSE_NET_UNREACHABLE;
        break;
    case ENETDOWN:
        cause = POTR_SOCKET_CAUSE_NET_DOWN;
        break;
    case EADDRINUSE:
        cause = POTR_SOCKET_CAUSE_ADDRESS_IN_USE;
        break;
    case EADDRNOTAVAIL:
        cause = POTR_SOCKET_CAUSE_ADDRESS_NOT_AVAILABLE;
        break;
    case EMSGSIZE:
        cause = POTR_SOCKET_CAUSE_MESSAGE_SIZE;
        break;
    case EACCES:
    case EPERM:
        cause = POTR_SOCKET_CAUSE_ACCESS_DENIED;
        break;
    case ENOMEM:
    case ENOBUFS:
        cause = POTR_SOCKET_CAUSE_OUT_OF_MEMORY;
        break;
    case EMFILE:
    case ENFILE:
        cause = POTR_SOCKET_CAUSE_TOO_MANY_OPEN_FILES;
        break;
    case EINVAL:
    case EFAULT:
        cause = POTR_SOCKET_CAUSE_INVALID_ARGUMENT;
        break;
    case EAFNOSUPPORT:
    case EPROTONOSUPPORT:
    case EOPNOTSUPP:
        cause = POTR_SOCKET_CAUSE_UNSUPPORTED;
        break;
    #if defined(ESHUTDOWN)
    case ESHUTDOWN:
        cause = POTR_SOCKET_CAUSE_SHUTDOWN;
        break;
    #endif
    default:
        cause = POTR_SOCKET_CAUSE_OTHER;
        break;
    }
    return cause;
}

#elif defined(PLATFORM_WINDOWS) && defined(COMPILER_MSVC)
    #pragma warning(disable : 4206)
#endif
