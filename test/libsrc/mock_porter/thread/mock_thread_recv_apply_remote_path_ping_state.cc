#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, thread_recv_apply_remote_path_ping_state, uint8_t *dst, const uint8_t *src, size_t count)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->thread_recv_apply_remote_path_ping_state(dst, src, count);
    }
}
