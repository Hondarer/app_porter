#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_receiver_flow_suppressed, potr_context *ctx, uint64_t now_ms, uint32_t next_seq, size_t *tracked, uint64_t *until_ms)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_receiver_flow_suppressed(ctx, now_ms, next_seq, tracked, until_ms);
    }
    return mock_ret;
}
