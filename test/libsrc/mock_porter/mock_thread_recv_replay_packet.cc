#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, thread_recv_replay_packet, potr_internal_replay_window *window,
               const potr_packet *packet, const uint8_t *wire_header)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->thread_recv_replay_packet(window, packet, wire_header);
    }
    return mock_ret;
}
