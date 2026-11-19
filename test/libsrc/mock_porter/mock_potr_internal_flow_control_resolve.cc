#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_porter.h>

MOCK_WEAK_IMPL(int, potr_internal_flow_control_resolve, const potr_service_def *service, const potr_global_config *global, potr_role role, uint32_t health_interval_ms, potr_internal_flow_control_config *config)
{
    int mock_ret = 0;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_flow_control_resolve(service, global, role, health_interval_ms, config);
    }
    return mock_ret;
}
