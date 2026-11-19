#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_packet_build_flow_control, potr_packet *packet, const potr_internal_packet_session_hdr *shdr, uint8_t *payload, uint8_t state, uint32_t send_limit)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_packet_build_flow_control(packet, shdr, payload, state, send_limit);
    }
    return mock_ret;
}
