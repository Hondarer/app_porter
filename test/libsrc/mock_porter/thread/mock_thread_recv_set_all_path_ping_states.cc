#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, thread_recv_set_all_path_ping_states, cplat_atomic_u8 *states, size_t count, uint8_t next_state)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->thread_recv_set_all_path_ping_states(states, count, next_state);
    }
    return mock_ret;
}
