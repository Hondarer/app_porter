#include <cplat/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>
#include <porter.h>

MOCK_WEAK_IMPL(cplat_tracer *, potr_tracer_get, void)
{
    cplat_tracer *mock_ret = nullptr;

    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_tracer_get();
    }

    if (getTraceLevel() > TRACE_NONE)
    {
        printf("  > %s", __func__);
        if (getTraceLevel() >= TRACE_DETAIL)
        {
            printf(" -> 0x%p\n", (void *)mock_ret);
        }
        else
        {
            printf("\n");
        }
    }

    return mock_ret;
}
