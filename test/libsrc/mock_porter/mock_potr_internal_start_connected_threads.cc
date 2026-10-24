#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_start_connected_threads, potr_context *ctx, int path_idx,
               const potr_internal_connected_threads_ops *ops)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_start_connected_threads(ctx, path_idx, ops);
    }
    return mock_ret;
}
