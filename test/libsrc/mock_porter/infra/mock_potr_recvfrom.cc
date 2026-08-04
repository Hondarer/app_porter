#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_recvfrom, PotrSocket sock, uint8_t *buf, size_t len, int flags, struct sockaddr *src,
               int *src_len, com_util_error *detail_out)
{
    int rtc = -1;

    if (_mock_porter != nullptr)
    {
        rtc = _mock_porter->potr_recvfrom(sock, buf, len, flags, src, src_len, detail_out);
    }
    if (getTraceLevel() > TRACE_NONE)
    {
        printf("  > %s -> %d\n", __func__, rtc);
    }
    return rtc;
}
