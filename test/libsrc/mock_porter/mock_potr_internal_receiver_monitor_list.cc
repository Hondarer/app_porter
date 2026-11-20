#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(size_t, potr_internal_receiver_monitor_list, potr_context *ctx, uint64_t now_ms, potr_internal_packet_session_hdr *list, size_t capacity, int *changed)
{
    size_t mock_ret = 0U;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_receiver_monitor_list(ctx, now_ms, list, capacity, changed);
    }
    return mock_ret;
}
