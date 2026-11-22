#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(uint64_t, thread_recv_health_check_slow_peers, potr_context *ctx, uint64_t now_ms)
{
    if (_mock_porter != nullptr)
    {
        return _mock_porter->thread_recv_health_check_slow_peers(ctx, now_ms);
    }
    return 0U;
}
