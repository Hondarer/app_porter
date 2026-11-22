#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, thread_connect_read_first_packet, cplat_socket fd, uint8_t *buf, size_t max_buf, size_t *len_out,
               int timeout_ms)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->thread_connect_read_first_packet(fd, buf, max_buf, len_out, timeout_ms);
    }
    return mock_ret;
}
