#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>
#include <porter/potrContext.h>
#include <porter/potrPeerTable.h>

MOCK_WEAK_IMPL(void, peer_send_fin, PotrContext *ctx, PotrPeerContext *peer)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->peer_send_fin(ctx, peer);
    }

    if (getTraceLevel() > TRACE_NONE)
    {
        printf("  > %s 0x%p, 0x%p\n", __func__, (void *)ctx, (void *)peer);
    }
}
