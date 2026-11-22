#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, thread_recv_disconnect_peer_all_paths, potr_context *ctx, potr_internal_peer_context *peer)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->thread_recv_disconnect_peer_all_paths(ctx, peer);
    }
}
