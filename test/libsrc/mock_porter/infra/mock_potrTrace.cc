#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <com_util/trace/trace.h>
#include <porter.h>
#include "potrTrace.h"

WEAK_ATR trace_logger_t *potr_trace_get(void)
{
    return nullptr;
}

WEAK_ATR trace_logger_t * POTR_API potrGetLogger(void)
{
    return nullptr;
}
