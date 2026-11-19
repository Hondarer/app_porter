#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, potr_internal_flow_control_tick, potr_context *ctx, uint64_t now_ms)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->potr_internal_flow_control_tick(ctx, now_ms);
    }
}
