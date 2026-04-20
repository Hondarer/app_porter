#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_potrPeerTable.h>

Mock_potrPeerTable *_mock_potrPeerTable = nullptr;

Mock_potrPeerTable::Mock_potrPeerTable()
{
    ON_CALL(*this, peer_find_by_id(_, _))
        .WillByDefault(Return(nullptr));
    ON_CALL(*this, peer_send_fin(_, _))
        .WillByDefault(Return());
    ON_CALL(*this, peer_free(_, _))
        .WillByDefault(Return());

    _mock_potrPeerTable = this;
}

Mock_potrPeerTable::~Mock_potrPeerTable()
{
    _mock_potrPeerTable = nullptr;
}
