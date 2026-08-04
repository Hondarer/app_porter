#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_setsockopt, PotrSocket sock, int level, int optname, const void *optval, int optlen,
               com_util_error *detail_out)
{
    int rtc = POTR_ERR_IO;

    if (_mock_porter != nullptr)
    {
        rtc = _mock_porter->potr_setsockopt(sock, level, optname, optval, optlen, detail_out);
    }
    if (getTraceLevel() > TRACE_NONE)
    {
        printf("  > %s -> %d\n", __func__, rtc);
    }
    return rtc;
}
