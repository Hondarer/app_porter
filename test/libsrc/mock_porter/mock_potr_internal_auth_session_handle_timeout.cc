#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_auth_session_handle_timeout, potr_internal_auth_session *session, potr_internal_auth_state *state_out)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_auth_session_handle_timeout(session, state_out);
    }
    return mock_ret;
}
