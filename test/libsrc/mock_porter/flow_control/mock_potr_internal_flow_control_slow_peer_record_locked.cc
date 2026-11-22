#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, potr_internal_flow_control_slow_peer_record_locked, potr_context *ctx, uint32_t session_id,
               const cplat_timespec *session_ts, uint64_t now_ms)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->potr_internal_flow_control_slow_peer_record_locked(ctx, session_id, session_ts, now_ms);
    }
}
