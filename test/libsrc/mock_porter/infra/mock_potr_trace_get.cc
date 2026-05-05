#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>
#include <porter/infra/potrTrace.h>

MOCK_WEAK_IMPL(com_util_tracer_t *, potr_trace_get, void)
{
    com_util_tracer_t *rtc = nullptr;

    if (_mock_porter != nullptr)
    {
        rtc = _mock_porter->potr_trace_get();
    }

    if (getTraceLevel() > TRACE_NONE)
    {
        printf("  > %s", __func__);
        if (getTraceLevel() >= TRACE_DETAIL)
        {
            printf(" -> 0x%p\n", (void *)rtc);
        }
        else
        {
            printf("\n");
        }
    }

    return rtc;
}
