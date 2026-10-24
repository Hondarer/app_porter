#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, potr_internal_health_thread_wake, potr_context *ctx)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->potr_internal_health_thread_wake(ctx);
    }
}
