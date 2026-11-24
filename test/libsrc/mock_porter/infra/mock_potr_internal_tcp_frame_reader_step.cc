#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>
#include <porter/porter_result.h>

MOCK_WEAK_IMPL(int, potr_internal_tcp_frame_reader_step, potr_internal_tcp_frame_reader *reader, cplat_socket fd,
               uint8_t *buf, size_t max_buf, int *complete_out, cplat_error *detail_out)
{
    int mock_ret = POTR_OK;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_tcp_frame_reader_step(reader, fd, buf, max_buf, complete_out, detail_out);
    }
    return mock_ret;
}
