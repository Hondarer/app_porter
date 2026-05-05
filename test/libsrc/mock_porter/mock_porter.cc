#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>

Mock_porter *_mock_porter = nullptr;

Mock_porter::Mock_porter()
{
    // peer table
    ON_CALL(*this, peer_find_by_id(_, _))
        .WillByDefault(Return(nullptr));
    ON_CALL(*this, peer_send_fin(_, _))
        .WillByDefault(Return());
    ON_CALL(*this, peer_free(_, _))
        .WillByDefault(Return());

    // trace
    ON_CALL(*this, potr_trace_get())
        .WillByDefault(Return(nullptr));
    ON_CALL(*this, potrGetTracer())
        .WillByDefault(Return(nullptr));

    _mock_porter = this;
}

Mock_porter::~Mock_porter()
{
    _mock_porter = nullptr;
}
