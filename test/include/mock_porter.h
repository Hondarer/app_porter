#ifndef MOCK_PORTER_H
#define MOCK_PORTER_H

#include <com_util/base/platform.h>
#include <testfw.h>
#include <porter.h>
#include <porter/potrContext.h>
#include <porter/infra/potrPlatform.h>
#include <com_util/trace/tracer.h>

#if defined(COMPILER_MSVC)
    #pragma comment(linker, "/INCLUDE:_mock_impl_peer_find_by_id")
    #pragma comment(linker, "/INCLUDE:_mock_impl_peer_send_fin")
    #pragma comment(linker, "/INCLUDE:_mock_impl_peer_free")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_trace_get")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potrGetTracer")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_setsockopt")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_sendto")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_recvfrom")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_tcp_send")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_tcp_recv_all")
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

    // Platform socket mock methods
    MOCK_METHOD(int, potr_setsockopt, (PotrSocket, int, int, const void *, int, com_util_error *));
    MOCK_METHOD(int, potr_sendto,
                (PotrSocket, const uint8_t *, size_t, int, const struct sockaddr *, int, com_util_error *));
    MOCK_METHOD(int, potr_recvfrom, (PotrSocket, uint8_t *, size_t, int, struct sockaddr *, int *, com_util_error *));
    MOCK_METHOD(int, potr_tcp_send, (PotrSocket, const uint8_t *, size_t, com_util_error *));
    MOCK_METHOD(int, potr_tcp_recv_all, (PotrSocket, uint8_t *, size_t, com_util_error *));

    Mock_porter();
    ~Mock_porter();
};

extern Mock_porter *_mock_porter;

#endif /* MOCK_PORTER_H */
