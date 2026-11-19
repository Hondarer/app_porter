#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_auth_session_get_timeout_ms, potr_internal_auth_session *session, int *has_timeout_out, uint32_t *timeout_ms_out)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_auth_session_get_timeout_ms(session, has_timeout_out, timeout_ms_out);
    }
    return mock_ret;
}
