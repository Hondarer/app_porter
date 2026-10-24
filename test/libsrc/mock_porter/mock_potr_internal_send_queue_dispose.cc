#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, potr_internal_send_queue_dispose, potr_internal_send_queue *q)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->potr_internal_send_queue_dispose(q);
    }
}
