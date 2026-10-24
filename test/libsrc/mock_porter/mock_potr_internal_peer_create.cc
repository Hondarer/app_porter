#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(potr_internal_peer_context *, potr_internal_peer_create, potr_context *ctx,
               const cplat_ipv4_endpoint *sender_addr, int path_idx)
{
    potr_internal_peer_context *mock_ret = nullptr;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_peer_create(ctx, sender_addr, path_idx);
    }
    return mock_ret;
}
