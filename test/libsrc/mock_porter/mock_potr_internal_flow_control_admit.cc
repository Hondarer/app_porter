#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_flow_control_admit, potr_context *ctx, potr_peer_id peer_id, uint32_t session_id, const cplat_timespec *session_ts, size_t len)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_flow_control_admit(ctx, peer_id, session_id, session_ts, len);
    }
    return mock_ret;
}
