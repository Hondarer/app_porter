#include <cplat/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>
#include <porter/potr_context.h>
#include <porter/potr_path_event.h>

MOCK_WEAK_IMPL(int, potr_internal_in_own_callback, const potr_context *ctx)
{
    int mock_ret = 0;

    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_in_own_callback(ctx);
    }

    if (getTraceLevel() > TRACE_NONE)
    {
        printf("  > %s 0x%p", __func__, (const void *)ctx);
        if (getTraceLevel() >= TRACE_DETAIL)
        {
            printf(" -> %d\n", mock_ret);
        }
        else
        {
            printf("\n");
        }
    }

    return mock_ret;
}
