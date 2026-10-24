#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(cplat_socket, thread_connect_with_timeout, potr_context *ctx, int path_idx)
{
    cplat_socket mock_ret = CPLAT_INVALID_SOCKET;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->thread_connect_with_timeout(ctx, path_idx);
    }
    return mock_ret;
}
