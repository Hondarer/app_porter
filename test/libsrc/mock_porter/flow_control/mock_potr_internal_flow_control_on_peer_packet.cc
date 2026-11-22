#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, potr_internal_flow_control_on_peer_packet, potr_context *ctx,
               struct potr_internal_peer_context *peer, const potr_packet *pkt)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->potr_internal_flow_control_on_peer_packet(ctx, peer, pkt);
    }
}
