#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_send_queue_pop, potr_internal_send_queue *q, potr_internal_payload_elem *out,
               cplat_atomic_i32 *running)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_send_queue_pop(q, out, running);
    }
    return mock_ret;
}
