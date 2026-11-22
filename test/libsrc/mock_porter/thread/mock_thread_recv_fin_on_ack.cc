#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, thread_recv_fin_on_ack, potr_context *ctx, uint32_t ack_num)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->thread_recv_fin_on_ack(ctx, ack_num);
    }
}
