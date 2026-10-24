#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, potr_internal_emit_service_path_events_locked, potr_context *ctx,
               const potr_internal_prepared_path_events *prepared)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->potr_internal_emit_service_path_events_locked(ctx, prepared);
    }
}
