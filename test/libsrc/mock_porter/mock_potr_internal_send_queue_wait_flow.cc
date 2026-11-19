#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, potr_internal_send_queue_wait_flow, potr_internal_send_queue *q, uint64_t *generation, int timeout_ms)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->potr_internal_send_queue_wait_flow(q, generation, timeout_ms);
    }
}
