#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, thread_recv_slot_init_ctx, thread_recv_slot *slot, potr_context *ctx)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->thread_recv_slot_init_ctx(slot, ctx);
    }
}
