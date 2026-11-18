#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, potr_internal_tcp_recv_thread_stop, potr_context *ctx, int path_idx)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->potr_internal_tcp_recv_thread_stop(ctx, path_idx);
    }
}
