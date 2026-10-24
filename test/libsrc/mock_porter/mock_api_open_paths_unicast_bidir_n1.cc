#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, api_open_paths_unicast_bidir_n1, potr_context *ctx)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->api_open_paths_unicast_bidir_n1(ctx);
    }
    return mock_ret;
}
