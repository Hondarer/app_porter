#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>
#include <porter/porter_result.h>

MOCK_WEAK_IMPL(int, potr_internal_udp_sendto_multi, potr_context *ctx, int path_idx,
               const cplat_socket_datagram *datagrams, size_t count, int *results_out)
{
    int mock_ret = POTR_OK;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_udp_sendto_multi(ctx, path_idx, datagrams, count, results_out);
    }
    else if (results_out != nullptr)
    {
        for (size_t i = 0U; i < count; i++)
        {
            results_out[i] = POTR_OK;
        }
    }
    return mock_ret;
}
