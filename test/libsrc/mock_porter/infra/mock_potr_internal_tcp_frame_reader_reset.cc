#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, potr_internal_tcp_frame_reader_reset, potr_internal_tcp_frame_reader *reader)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->potr_internal_tcp_frame_reader_reset(reader);
    }
}
