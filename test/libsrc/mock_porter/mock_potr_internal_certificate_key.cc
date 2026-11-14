#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>
#include <porter/porter_result.h>

MOCK_WEAK_IMPL(int, potr_internal_certificate_key, potr_context *ctx, const potr_internal_tx_session *session, const potr_packet *packet, int sending, uint8_t *key)
{
    int mock_ret = POTR_ERR_DISCONNECTED;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_certificate_key(ctx, session, packet, sending, key);
    }
    return mock_ret;
}
