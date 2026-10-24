#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_packet_build_ping, potr_packet *packet, const potr_internal_packet_session_hdr *shdr,
               uint32_t seq_num, const uint8_t *health_payload, uint16_t health_payload_len)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret =
            _mock_porter->potr_internal_packet_build_ping(packet, shdr, seq_num, health_payload, health_payload_len);
    }
    return mock_ret;
}
