#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>
#include <porter/porter_result.h>

MOCK_WEAK_IMPL(int, potr_internal_certificate_receive, potr_context *ctx, potr_packet *packet, const uint8_t *wire_header, const cplat_ipv4_endpoint *sender, int path_idx)
{
    int mock_ret = POTR_ERR_NOT_FOUND;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_certificate_receive(ctx, packet, wire_header, sender, path_idx);
    }
    return mock_ret;
}
