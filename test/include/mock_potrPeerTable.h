#ifndef MOCK_POTR_PEER_TABLE_H
#define MOCK_POTR_PEER_TABLE_H

#include <testfw.h>
#include <porter_type.h>

/* 完全型不要のため前方宣言 */
struct PotrContext_;
typedef struct PotrPeerContext_ PotrPeerContext;

class Mock_potrPeerTable
{
public:
    MOCK_METHOD(PotrPeerContext *, peer_find_by_id,
                (struct PotrContext_ *, PotrPeerId));
    MOCK_METHOD(void, peer_send_fin,
                (struct PotrContext_ *, PotrPeerContext *));
    MOCK_METHOD(void, peer_free,
                (struct PotrContext_ *, PotrPeerContext *));

    Mock_potrPeerTable();
    ~Mock_potrPeerTable();
};

extern Mock_potrPeerTable *_mock_potrPeerTable;

#endif /* MOCK_POTR_PEER_TABLE_H */
