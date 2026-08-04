#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_tcp_recv_all, PotrSocket sock, uint8_t *buf, size_t len, com_util_error *detail_out)
{
    int rtc = POTR_ERR_IO;

    if (_mock_porter != nullptr)
    {
        rtc = _mock_porter->potr_tcp_recv_all(sock, buf, len, detail_out);
    }
    if (getTraceLevel() > TRACE_NONE)
    {
        printf("  > %s -> %d\n", __func__, rtc);
    }
    return rtc;
}
