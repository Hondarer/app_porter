#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_auth_session_take_output, potr_internal_auth_session *session, uint8_t *out, const size_t out_capacity, size_t *out_len_out)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_auth_session_take_output(session, out, out_capacity, out_len_out);
    }
    return mock_ret;
}
