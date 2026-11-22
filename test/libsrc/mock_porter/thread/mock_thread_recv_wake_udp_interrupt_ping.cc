#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, thread_recv_wake_udp_interrupt_ping, potr_context *ctx, int state_changed)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->thread_recv_wake_udp_interrupt_ping(ctx, state_changed);
    }
}
