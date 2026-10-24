#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, thread_recv_session_adopt, thread_recv_slot *slot, const potr_packet *pkt)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->thread_recv_session_adopt(slot, pkt);
    }
    return mock_ret;
}
