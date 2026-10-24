#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, thread_connect_session_compare, const potr_context *ctx, const potr_packet *pkt)
{
    int mock_ret = TCP_SESSION_NEW;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->thread_connect_session_compare(ctx, pkt);
    }
    return mock_ret;
}
