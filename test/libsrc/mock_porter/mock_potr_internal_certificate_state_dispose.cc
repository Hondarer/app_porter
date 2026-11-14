#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>
#include <porter/porter_result.h>

MOCK_WEAK_IMPL(void, potr_internal_certificate_state_dispose, potr_internal_certificate_state *state)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->potr_internal_certificate_state_dispose(state);
    }
}
