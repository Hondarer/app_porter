#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_potrPeerTable.h>
#include <potrContext.h>
#include <potrPeerTable.h>

WEAK_ATR void peer_free(struct PotrContext_ *ctx, PotrPeerContext *peer)
{
    if (_mock_potrPeerTable != nullptr)
    {
        _mock_potrPeerTable->peer_free(ctx, peer);
    }

    if (getTraceLevel() > TRACE_NONE)
    {
        printf("  > %s 0x%p, 0x%p\n", __func__, (void *)ctx, (void *)peer);
    }
}
