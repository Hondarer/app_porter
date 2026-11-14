#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, potr_internal_callback_emit, potr_context *ctx, potr_peer_id peer_id, potr_event event,
               const void *data, size_t len)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->potr_internal_callback_emit(ctx, peer_id, event, data, len);
    }
}
