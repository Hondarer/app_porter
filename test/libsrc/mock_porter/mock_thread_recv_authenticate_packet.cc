#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, thread_recv_authenticate_packet, potr_context *ctx, potr_packet *pkt, const uint8_t *wire_hdr,
               const char *log_prefix, int path_idx)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->thread_recv_authenticate_packet(ctx, pkt, wire_hdr, log_prefix, path_idx);
    }
    return mock_ret;
}
