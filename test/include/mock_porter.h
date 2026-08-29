#ifndef MOCK_PORTER_H
#define MOCK_PORTER_H

#include <cplat/base/platform.h>
#include <testfw.h>
#include <porter.h>
#include <porter/potr_context.h>
#include <cplat/trace/tracer.h>

#if defined(COMPILER_MSVC)
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_peer_find_by_id")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_peer_send_fin")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_peer_free")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_trace_get")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_tracer_get")
#endif /* COMPILER_MSVC */

class Mock_porter
{
  public:
    // Peer table mock methods
    MOCK_METHOD(potr_internal_peer_context *, potr_internal_peer_find_by_id, (const potr_context *, potr_peer_id));
    MOCK_METHOD(void, potr_internal_peer_send_fin, (potr_context *, potr_internal_peer_context *));
    MOCK_METHOD(void, potr_internal_peer_free, (potr_context *, potr_internal_peer_context *));

    // Trace mock methods
    MOCK_METHOD(cplat_tracer *, potr_internal_trace_get, ());
    MOCK_METHOD(cplat_tracer *, potr_tracer_get, ());

    Mock_porter();
    ~Mock_porter();
};

extern Mock_porter *_mock_porter;

#endif /* MOCK_PORTER_H */
