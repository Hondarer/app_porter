#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_auth_session_create, const potr_internal_auth_credentials *credentials, const potr_internal_auth_role role, const size_t max_record_size, potr_internal_auth_session **session_out)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_auth_session_create(credentials, role, max_record_size, session_out);
    }
    return mock_ret;
}
