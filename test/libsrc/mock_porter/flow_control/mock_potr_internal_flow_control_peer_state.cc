#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_flow_control_peer_state, potr_context *ctx, potr_peer_id peer_id, uint64_t now_ms, int *deferred,
               uint32_t *allowance)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_flow_control_peer_state(ctx, peer_id, now_ms, deferred, allowance);
    }
    return mock_ret;
}
