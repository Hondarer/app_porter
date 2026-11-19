#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, potr_internal_flow_control_on_received, potr_context *ctx, potr_peer_id peer_id, uint32_t session_id, const cplat_timespec *session_ts, uint32_t next_seq, int data_packet)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->potr_internal_flow_control_on_received(ctx, peer_id, session_id, session_ts, next_seq, data_packet);
    }
}
