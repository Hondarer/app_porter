#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, thread_recv_health_check_peers, potr_context *ctx)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->thread_recv_health_check_peers(ctx);
    }
}
