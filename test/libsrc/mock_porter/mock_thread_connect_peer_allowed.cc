#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, thread_connect_peer_allowed, const potr_context *ctx, int path_idx,
               const cplat_ipv4_endpoint *peer_addr)
{
    int mock_ret = 1;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->thread_connect_peer_allowed(ctx, path_idx, peer_addr);
    }
    return mock_ret;
}
