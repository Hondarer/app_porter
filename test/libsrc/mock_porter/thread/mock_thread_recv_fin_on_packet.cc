#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(thread_recv_fin_result, thread_recv_fin_on_packet, thread_recv_slot *slot, const potr_packet *pkt,
               uint32_t *fin_target_seq_out)
{
    thread_recv_fin_result mock_ret = THREAD_RECV_FIN_IGNORED;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->thread_recv_fin_on_packet(slot, pkt, fin_target_seq_out);
    }
    return mock_ret;
}
