#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>
#include <porter/potr_context.h>
#include <porter/potr_peer_table.h>

MOCK_WEAK_IMPL(potr_internal_peer_context *, potr_internal_peer_find_by_id, const potr_context *ctx, potr_peer_id peer_id)
{
    potr_internal_peer_context *rtc = nullptr;

    if (_mock_porter != nullptr)
    {
        rtc = _mock_porter->potr_internal_peer_find_by_id(ctx, peer_id);
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
