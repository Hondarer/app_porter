#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>
#include <porter.h>

int delegate_real_potr_service_close(potr_context *handle)
{
    static auto real_fn = reinterpret_cast<decltype(&potr_service_close)>(
        resolveSharedSymbolOrExit(kLibPorterName, "potr_service_close"));

    return real_fn(handle);
}

MOCK_WEAK_IMPL(int, potr_service_close, potr_context *handle)
{
    int mock_ret = 0;

    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_service_close(handle);
    }
    else
    {
        mock_ret = delegate_real_potr_service_close(handle);
    }

    if (getTraceLevel() > TRACE_NONE)
    {
        printf("  > %s 0x%p", __func__, (void *)handle);
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
