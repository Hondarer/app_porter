#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, potr_internal_copy_tcp_path_states, const potr_context *ctx, int *states)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->potr_internal_copy_tcp_path_states(ctx, states);
    }
}
