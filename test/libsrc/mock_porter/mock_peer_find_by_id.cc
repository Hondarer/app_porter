#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>
#include <porter/potrContext.h>
#include <porter/potrPeerTable.h>

MOCK_WEAK_IMPL(PotrPeerContext *, peer_find_by_id, const PotrContext *ctx, PotrPeerId peer_id)
{
    PotrPeerContext *rtc = nullptr;

    if (_mock_porter != nullptr)
    {
        rtc = _mock_porter->peer_find_by_id(ctx, peer_id);
    }

    if (getTraceLevel() > TRACE_NONE)
    {
        printf("  > %s 0x%p, %d", __func__, (const void *)ctx, (int)peer_id);
        if (getTraceLevel() >= TRACE_DETAIL)
        {
            printf(" -> 0x%p\n", (void *)rtc);
        }
        else
        {
            printf("\n");
        }
    }

    return rtc;
}
