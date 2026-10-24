#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>
#include <porter/infra/potr_trace.h>
#include <stdarg.h>
#include <stdio.h>

MOCK_WEAK_IMPL(void, potr_internal_trace_socket_failure_at, const char *file, int line, cplat_trace_level level,
               const cplat_error *detail, const char *format, ...)
{
    if (_mock_porter != nullptr)
    {
        char message[1024];
        va_list args;
        va_start(args, format);
        (void)vsnprintf(message, sizeof(message), format, args);
        va_end(args);
        _mock_porter->potr_internal_trace_socket_failure_at(file, line, level, detail, message);
    }
}
