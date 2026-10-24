#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, potr_internal_peer_path_clear, const potr_context *ctx, potr_internal_peer_context *peer,
               int path_idx)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->potr_internal_peer_path_clear(ctx, peer, path_idx);
    }
}
