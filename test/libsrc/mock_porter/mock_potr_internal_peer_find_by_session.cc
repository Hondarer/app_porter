#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(potr_internal_peer_context *, potr_internal_peer_find_by_session, const potr_context *ctx,
               uint32_t session_id, const cplat_timespec *session_ts)
{
    potr_internal_peer_context *mock_ret = nullptr;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_peer_find_by_session(ctx, session_id, session_ts);
    }
    return mock_ret;
}
