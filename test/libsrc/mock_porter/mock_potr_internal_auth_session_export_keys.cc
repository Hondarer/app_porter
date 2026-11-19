#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_auth_session_export_keys, potr_internal_auth_session *session, const uint8_t *context, const size_t context_len, uint8_t client_to_server_key[POTR_CRYPTO_KEY_SIZE], uint8_t server_to_client_key[POTR_CRYPTO_KEY_SIZE])
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_auth_session_export_keys(session, context, context_len, client_to_server_key, server_to_client_key);
    }
    return mock_ret;
}
