#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, api_context_dispose_tcp_sync, potr_context *ctx)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->api_context_dispose_tcp_sync(ctx);
    }
}
