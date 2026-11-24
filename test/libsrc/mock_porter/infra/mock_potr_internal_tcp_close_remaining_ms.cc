#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>
#include <porter/porter_result.h>

MOCK_WEAK_IMPL(int, potr_internal_tcp_close_remaining_ms, const potr_context *ctx, int *has_deadline_out,
               int *remaining_ms_out)
{
    int mock_ret = POTR_OK;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_tcp_close_remaining_ms(ctx, has_deadline_out, remaining_ms_out);
    }
    return mock_ret;
}
