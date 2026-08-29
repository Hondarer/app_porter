#include <cplat/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>
#include <porter/potr_context.h>
#include <porter/potr_peer_table.h>

MOCK_WEAK_IMPL(void, potr_internal_peer_free, potr_context *ctx, potr_internal_peer_context *peer)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->potr_internal_peer_free(ctx, peer);
    }

    if (getTraceLevel() > TRACE_NONE)
    {
        printf("  > %s 0x%p, 0x%p\n", __func__, (void *)ctx, (void *)peer);
    }
}
