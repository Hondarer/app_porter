#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, thread_recv_dispatch_packet, thread_recv_slot *slot, potr_packet *pkt, int path_idx,
               const cplat_ipv4_endpoint *sender)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->thread_recv_dispatch_packet(slot, pkt, path_idx, sender);
    }
    return mock_ret;
}
