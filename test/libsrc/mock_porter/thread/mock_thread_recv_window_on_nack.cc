#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, thread_recv_window_on_nack, thread_recv_slot *slot, const potr_packet *pkt)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->thread_recv_window_on_nack(slot, pkt);
    }
}
