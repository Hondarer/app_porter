#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>
#include <porter/porter_result.h>

MOCK_WEAK_IMPL(int, potr_internal_path_group_select, potr_context *ctx, potr_internal_peer_context *peer, potr_internal_path_group_result *selection)
{
    int mock_ret = POTR_OK;
    *selection = {};
    selection->data_paths = UINT8_MAX;
    selection->selected_paths = UINT8_MAX;
    if (_mock_porter != nullptr)
    {
        mock_ret = _mock_porter->potr_internal_path_group_select(ctx, peer, selection);
    }
    return mock_ret;
}
