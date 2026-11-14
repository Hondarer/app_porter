#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_window_send_get, const potr_internal_window *win, uint32_t seq_num,
               potr_packet *packet_out)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_window_send_get(win, seq_num, packet_out);
    }
    return mock_ret;
}
