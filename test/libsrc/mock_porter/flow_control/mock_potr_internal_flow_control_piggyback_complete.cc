#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, potr_internal_flow_control_piggyback_complete, potr_context *ctx, potr_peer_id peer_id,
               uint32_t send_limit, int sent)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->potr_internal_flow_control_piggyback_complete(ctx, peer_id, send_limit, sent);
    }
}
