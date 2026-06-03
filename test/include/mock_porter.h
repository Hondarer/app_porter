#ifndef MOCK_PORTER_H
#define MOCK_PORTER_H

#include <com_util/base/platform.h>
#include <testfw.h>
#include <porter.h>
#include <porter/potrContext.h>
#include <com_util/trace/tracer.h>

#if defined(COMPILER_MSVC)
    #pragma comment(linker, "/INCLUDE:_mock_impl_peer_find_by_id")
    #pragma comment(linker, "/INCLUDE:_mock_impl_peer_send_fin")
    #pragma comment(linker, "/INCLUDE:_mock_impl_peer_free")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_trace_get")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potrGetTracer")
#endif /* COMPILER_MSVC */

class Mock_porter
{
  public:
    // Peer table mock methods
    MOCK_METHOD(PotrPeerContext *, peer_find_by_id, (const PotrContext *, PotrPeerId));
    MOCK_METHOD(void, peer_send_fin, (PotrContext *, PotrPeerContext *));
    MOCK_METHOD(void, peer_free, (PotrContext *, PotrPeerContext *));

    // Trace mock methods
    MOCK_METHOD(com_util_tracer *, potr_trace_get, ());
    MOCK_METHOD(com_util_tracer *, potrGetTracer, ());

    Mock_porter();
    ~Mock_porter();
};

extern Mock_porter *_mock_porter;

#endif /* MOCK_PORTER_H */
