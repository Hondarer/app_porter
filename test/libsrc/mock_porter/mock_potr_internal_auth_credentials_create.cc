#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_auth_credentials_create, const potr_internal_auth_config *config, const potr_internal_auth_transport transport, potr_internal_auth_credentials **credentials_out)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_auth_credentials_create(config, transport, credentials_out);
    }
    return mock_ret;
}
