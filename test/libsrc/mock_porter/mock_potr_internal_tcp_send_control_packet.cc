#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_tcp_send_control_packet, const potr_context *ctx, potr_packet *pkt,
               uint32_t nonce_val)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_tcp_send_control_packet(ctx, pkt, nonce_val);
    }
    return mock_ret;
}
