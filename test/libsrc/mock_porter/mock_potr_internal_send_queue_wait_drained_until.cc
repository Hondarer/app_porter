#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>
#include <porter/porter_result.h>

MOCK_WEAK_IMPL(int, potr_internal_send_queue_wait_drained_until, potr_internal_send_queue *q, uint64_t deadline_ms)
{
    int mock_ret = POTR_OK;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_send_queue_wait_drained_until(q, deadline_ms);
    }
    return mock_ret;
}
