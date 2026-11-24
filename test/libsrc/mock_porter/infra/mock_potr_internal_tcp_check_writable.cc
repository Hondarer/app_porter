#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>
#include <porter/porter_result.h>

MOCK_WEAK_IMPL(int, potr_internal_tcp_check_writable, potr_context *ctx, int path_idx)
{
    int mock_ret = POTR_OK;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_tcp_check_writable(ctx, path_idx);
    }
    return mock_ret;
}
