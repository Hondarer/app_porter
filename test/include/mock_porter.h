#ifndef MOCK_PORTER_H
#define MOCK_PORTER_H

#include <testfw.h>
#include <porter_type.h>
#include <com_util/trace/tracer.h>

/* 完全型不要のため前方宣言 */
struct PotrContext_;
typedef struct PotrPeerContext_ PotrPeerContext;

// delegate_real_* 関数の前方宣言
extern com_util_tracer_t *delegate_real_potr_trace_get(void);
extern com_util_tracer_t *delegate_real_potrGetTracer(void);

class Mock_porter
{
public:
    // Peer table mock methods
    MOCK_METHOD(PotrPeerContext *, peer_find_by_id,
                (struct PotrContext_ *, PotrPeerId));
    MOCK_METHOD(void, peer_send_fin,
                (struct PotrContext_ *, PotrPeerContext *));
    MOCK_METHOD(void, peer_free,
                (struct PotrContext_ *, PotrPeerContext *));

    // Trace mock methods
    MOCK_METHOD(com_util_tracer_t *, potr_trace_get, ());
    MOCK_METHOD(com_util_tracer_t *, potrGetTracer, ());

    Mock_porter();
    ~Mock_porter();
};

extern Mock_porter *_mock_porter;

#endif /* MOCK_PORTER_H */
