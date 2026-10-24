#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_window_init, potr_internal_window *win, uint32_t initial_seq, uint16_t window_size,
               uint16_t max_payload)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_window_init(win, initial_seq, window_size, max_payload);
    }
    return mock_ret;
}
