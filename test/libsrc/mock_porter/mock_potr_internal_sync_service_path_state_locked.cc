#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(void, potr_internal_sync_service_path_state_locked, potr_context *ctx, const int *next_states,
               potr_internal_prepared_path_events *prepared)
{
    if (_mock_porter != nullptr)
    {
        _mock_porter->potr_internal_sync_service_path_state_locked(ctx, next_states, prepared);
    }
}
