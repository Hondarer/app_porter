#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_packet_unpack_next, const potr_packet *container, size_t *offset,
               potr_packet *elem_out)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_packet_unpack_next(container, offset, elem_out);
    }
    return mock_ret;
}
