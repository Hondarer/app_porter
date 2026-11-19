#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(const char *, potr_internal_auth_session_failure_reason, const potr_internal_auth_session *session)
{
    const char *mock_ret = "";
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_auth_session_failure_reason(session);
    }
    return mock_ret;
}
