#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(size_t, potr_internal_flow_control_collect_slow_peers_locked, potr_context *ctx, uint64_t now_ms,
               potr_peer_id *out_peers, uint64_t *out_accum_ms, size_t max_out, uint64_t *next_check_ms)
{
    if (_mock_porter != nullptr)
    {
        return _mock_porter->potr_internal_flow_control_collect_slow_peers_locked(ctx, now_ms, out_peers, out_accum_ms,
                                                                                max_out, next_check_ms);
    }
    return 0U;
}
