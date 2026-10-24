#ifndef MOCK_PORTER_H
#define MOCK_PORTER_H

#include <cplat/base/platform.h>
#include <testfw.h>
#include <porter.h>
#include <porter/potr_context.h>
#include <cplat/trace/tracer.h>
extern "C"
{
#include <porter/potr_path_event.h>
#include <porter/potr_peer_table.h>
#include <porter/infra/potr_result.h>
#include <porter/infra/potr_send_queue.h>
#include <porter/infra/potr_tcp_control.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/window.h>
#include <porter/thread/potr_connected_threads.h>
#include <porter/thread/potr_connect_thread.h>
#include <porter/thread/potr_health_thread.h>
#include <porter/thread/potr_recv_thread.h>
#include <porter/thread/potr_send_thread.h>
#include "../../prod/libsrc/porter/api/api_context_cleanup.h"
#include "../../prod/libsrc/porter/api/api_open_paths.h"
#include "../../prod/libsrc/porter/thread/thread_recv_dispatch.h"
#include "../../prod/libsrc/porter/thread/thread_recv_fin.h"
#include "../../prod/libsrc/porter/thread/thread_recv_session.h"
#include "../../prod/libsrc/porter/thread/thread_recv_slot.h"
#include "../../prod/libsrc/porter/thread/thread_recv_validate.h"
#include "../../prod/libsrc/porter/thread/thread_recv_window.h"
}

#if defined(COMPILER_MSVC)
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_peer_find_by_id")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_peer_send_fin")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_peer_free")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_trace_get")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_tracer_get")
    #pragma comment(linker, "/INCLUDE:_mock_impl_api_context_dispose_resources")
    #pragma comment(linker, "/INCLUDE:_mock_impl_api_context_dispose_tcp_sync")
    #pragma comment(linker, "/INCLUDE:_mock_impl_api_context_has_threads")
    #pragma comment(linker, "/INCLUDE:_mock_impl_api_open_paths_by_type")
    #pragma comment(linker, "/INCLUDE:_mock_impl_api_setup_dest_addr")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_callback_mutex_dispose")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_callback_mutex_init")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_comm_recv_thread_start")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_comm_recv_thread_stop")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_connect_thread_start")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_connect_thread_stop")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_copy_tcp_path_states")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_emit_service_path_events_locked")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_health_thread_start")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_health_thread_stop")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_health_thread_wake")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_packet_build_fin")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_packet_build_packed")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_packet_build_ping")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_packet_parse")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_packet_unpack_next")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_packet_wire_size")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_peer_create")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_peer_find_by_session")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_peer_path_clear")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_peer_table_dispose")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_peer_table_init")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_peer_table_send_fin")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_result_from_error")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_result_from_socket_result")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_send_queue_complete")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_send_queue_dispose")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_send_queue_init")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_send_queue_peek")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_send_queue_peek_timed")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_send_queue_pop")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_send_queue_shutdown")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_send_queue_try_pop")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_send_queue_wait_drained")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_send_thread_start")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_send_thread_stop")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_start_connected_threads")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_sync_service_path_state_locked")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_tcp_health_thread_start")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_tcp_health_thread_stop")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_tcp_health_thread_wake")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_tcp_health_thread_wake_all")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_tcp_recv_thread_start")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_tcp_recv_thread_stop")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_tcp_send_control_packet")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_tcp_send_ping_now")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_window_dispose")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_window_init")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_window_recv_pop")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_window_recv_push")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_window_send_push")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_apply_remote_path_ping_state")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_authenticate_packet")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_check_src_addr")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_disconnect_peer_all_paths")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_disconnect_service_all_paths")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_dispatch_packet")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_fin_fire")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_fin_on_ack")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_fin_on_packet")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_fin_pending_reached")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_session_adopt")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_set_all_path_ping_states")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_set_path_ping_state")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_slot_deliver_payload_elem")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_slot_init_ctx")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_slot_init_peer")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_sync_path_state")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_wake_udp_interrupt_ping")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_window_check_reorder_timeout")
    #pragma comment(linker, "/INCLUDE:_mock_impl_thread_recv_window_on_nack")
    #pragma comment(linker, "/INCLUDE:_mock_impl_potr_internal_trace_socket_failure_at")
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

    MOCK_METHOD(void, api_context_dispose_resources, (potr_context * ctx));
    MOCK_METHOD(void, api_context_dispose_tcp_sync, (potr_context * ctx));
    MOCK_METHOD(int, api_context_has_threads, (const potr_context *ctx));
    MOCK_METHOD(int, api_open_paths_by_type, (potr_context * ctx, potr_role role));
    MOCK_METHOD(int, api_setup_dest_addr, (potr_context * ctx, potr_role role));
    MOCK_METHOD(void, potr_internal_callback_mutex_dispose, (potr_context * ctx));
    MOCK_METHOD(int, potr_internal_callback_mutex_init, (potr_context * ctx));
    MOCK_METHOD(int, potr_internal_comm_recv_thread_start, (potr_context * ctx));
    MOCK_METHOD(int, potr_internal_comm_recv_thread_stop, (potr_context * ctx));
    MOCK_METHOD(int, potr_internal_connect_thread_start, (potr_context * ctx));
    MOCK_METHOD(void, potr_internal_connect_thread_stop, (potr_context * ctx));
    MOCK_METHOD(void, potr_internal_copy_tcp_path_states, (const potr_context *ctx, int *states));
    MOCK_METHOD(void, potr_internal_emit_service_path_events_locked,
                (potr_context * ctx, const potr_internal_prepared_path_events *prepared));
    MOCK_METHOD(int, potr_internal_health_thread_start, (potr_context * ctx));
    MOCK_METHOD(int, potr_internal_health_thread_stop, (potr_context * ctx));
    MOCK_METHOD(void, potr_internal_health_thread_wake, (potr_context * ctx));
    MOCK_METHOD(int, potr_internal_packet_build_fin,
                (potr_packet * packet, const potr_internal_packet_session_hdr *shdr));
    MOCK_METHOD(int, potr_internal_packet_build_packed,
                (potr_packet * out, const potr_internal_packet_session_hdr *shdr, uint32_t seq_num,
                 const void *packed_payload, size_t payload_len));
    MOCK_METHOD(int, potr_internal_packet_build_ping,
                (potr_packet * packet, const potr_internal_packet_session_hdr *shdr, uint32_t seq_num,
                 const uint8_t *health_payload, uint16_t health_payload_len));
    MOCK_METHOD(int, potr_internal_packet_parse, (potr_packet * packet, const void *buf, size_t buf_len));
    MOCK_METHOD(int, potr_internal_packet_unpack_next,
                (const potr_packet *container, size_t *offset, potr_packet *elem_out));
    MOCK_METHOD(size_t, potr_internal_packet_wire_size, (const potr_packet *packet));
    MOCK_METHOD(potr_internal_peer_context *, potr_internal_peer_create,
                (potr_context * ctx, const cplat_ipv4_endpoint *sender_addr, int path_idx));
    MOCK_METHOD(potr_internal_peer_context *, potr_internal_peer_find_by_session,
                (const potr_context *ctx, uint32_t session_id, const cplat_timespec *session_ts));
    MOCK_METHOD(void, potr_internal_peer_path_clear,
                (const potr_context *ctx, potr_internal_peer_context *peer, int path_idx));
    MOCK_METHOD(void, potr_internal_peer_table_dispose, (potr_context * ctx));
    MOCK_METHOD(int, potr_internal_peer_table_init, (potr_context * ctx));
    MOCK_METHOD(void, potr_internal_peer_table_send_fin, (potr_context * ctx));
    MOCK_METHOD(int, potr_internal_result_from_error, (const cplat_error *detail));
    MOCK_METHOD(int, potr_internal_result_from_socket_result, (int cplat_result, const cplat_error *detail));
    MOCK_METHOD(void, potr_internal_send_queue_complete, (potr_internal_send_queue * q));
    MOCK_METHOD(void, potr_internal_send_queue_dispose, (potr_internal_send_queue * q));
    MOCK_METHOD(int, potr_internal_send_queue_init, (potr_internal_send_queue * q, size_t depth, uint16_t max_payload));
    MOCK_METHOD(int, potr_internal_send_queue_peek, (potr_internal_send_queue * q, potr_internal_payload_elem *out));
    MOCK_METHOD(int, potr_internal_send_queue_peek_timed,
                (potr_internal_send_queue * q, potr_internal_payload_elem *out, int timeout_ms));
    MOCK_METHOD(int, potr_internal_send_queue_pop,
                (potr_internal_send_queue * q, potr_internal_payload_elem *out, cplat_atomic_i32 *running));
    MOCK_METHOD(void, potr_internal_send_queue_shutdown, (potr_internal_send_queue * q));
    MOCK_METHOD(int, potr_internal_send_queue_try_pop, (potr_internal_send_queue * q, potr_internal_payload_elem *out));
    MOCK_METHOD(void, potr_internal_send_queue_wait_drained, (potr_internal_send_queue * q));
    MOCK_METHOD(int, potr_internal_send_thread_start, (potr_context * ctx));
    MOCK_METHOD(void, potr_internal_send_thread_stop, (potr_context * ctx));
    MOCK_METHOD(int, potr_internal_start_connected_threads,
                (potr_context * ctx, int path_idx, const potr_internal_connected_threads_ops *ops));
    MOCK_METHOD(void, potr_internal_sync_service_path_state_locked,
                (potr_context * ctx, const int *next_states, potr_internal_prepared_path_events *prepared));
    MOCK_METHOD(int, potr_internal_tcp_health_thread_start, (potr_context * ctx, int path_idx));
    MOCK_METHOD(int, potr_internal_tcp_health_thread_stop, (potr_context * ctx, int path_idx));
    MOCK_METHOD(void, potr_internal_tcp_health_thread_wake, (potr_context * ctx, int path_idx));
    MOCK_METHOD(void, potr_internal_tcp_health_thread_wake_all, (potr_context * ctx));
    MOCK_METHOD(int, potr_internal_tcp_recv_thread_start, (potr_context * ctx, int path_idx));
    MOCK_METHOD(int, potr_internal_tcp_recv_thread_stop, (potr_context * ctx, int path_idx));
    MOCK_METHOD(int, potr_internal_tcp_send_control_packet,
                (const potr_context *ctx, potr_packet *pkt, uint32_t nonce_val));
    MOCK_METHOD(int, potr_internal_tcp_send_ping_now, (potr_context * ctx, int path_idx));
    MOCK_METHOD(void, potr_internal_window_dispose, (potr_internal_window * win));
    MOCK_METHOD(int, potr_internal_window_init,
                (potr_internal_window * win, uint32_t initial_seq, uint16_t window_size, uint16_t max_payload));
    MOCK_METHOD(int, potr_internal_window_recv_pop, (potr_internal_window * win, potr_packet *packet));
    MOCK_METHOD(int, potr_internal_window_recv_push, (potr_internal_window * win, const potr_packet *packet));
    MOCK_METHOD(int, potr_internal_window_send_push, (potr_internal_window * win, const potr_packet *packet));
    MOCK_METHOD(void, thread_recv_apply_remote_path_ping_state, (uint8_t *dst, const uint8_t *src, size_t count));
    MOCK_METHOD(int, thread_recv_authenticate_packet,
                (potr_context * ctx, potr_packet *pkt, const uint8_t *wire_hdr, const char *log_prefix, int path_idx));
    MOCK_METHOD(int, thread_recv_check_src_addr, (const potr_context *ctx, const cplat_ipv4_endpoint *sender));
    MOCK_METHOD(void, thread_recv_disconnect_peer_all_paths, (potr_context * ctx, potr_internal_peer_context *peer));
    MOCK_METHOD(void, thread_recv_disconnect_service_all_paths, (potr_context * ctx));
    MOCK_METHOD(int, thread_recv_dispatch_packet,
                (thread_recv_slot * slot, potr_packet *pkt, int path_idx, const cplat_ipv4_endpoint *sender));
    MOCK_METHOD(void, thread_recv_fin_fire, (thread_recv_slot * slot, uint32_t fin_target_seq));
    MOCK_METHOD(void, thread_recv_fin_on_ack, (potr_context * ctx, uint32_t ack_num));
    MOCK_METHOD(int, thread_recv_fin_on_packet,
                (thread_recv_slot * slot, const potr_packet *pkt, uint32_t *fin_target_seq_out));
    MOCK_METHOD(int, thread_recv_fin_pending_reached, (const thread_recv_slot *slot, uint32_t *fin_target_seq_out));
    MOCK_METHOD(int, thread_recv_session_adopt, (thread_recv_slot * slot, const potr_packet *pkt));
    MOCK_METHOD(int, thread_recv_set_all_path_ping_states,
                (cplat_atomic_u8 * states, size_t count, uint8_t next_state));
    MOCK_METHOD(int, thread_recv_set_path_ping_state, (cplat_atomic_u8 * state, uint8_t next_state));
    MOCK_METHOD(void, thread_recv_slot_deliver_payload_elem, (thread_recv_slot * slot, const potr_packet *elem));
    MOCK_METHOD(void, thread_recv_slot_init_ctx, (thread_recv_slot * slot, potr_context *ctx));
    MOCK_METHOD(void, thread_recv_slot_init_peer,
                (thread_recv_slot * slot, potr_context *ctx, potr_internal_peer_context *peer));
    MOCK_METHOD(void, thread_recv_sync_path_state, (thread_recv_slot * slot));
    MOCK_METHOD(void, thread_recv_wake_udp_interrupt_ping, (potr_context * ctx, int state_changed));
    MOCK_METHOD(void, thread_recv_window_check_reorder_timeout, (thread_recv_slot * slot));
    MOCK_METHOD(void, thread_recv_window_on_nack, (thread_recv_slot * slot, const potr_packet *pkt));

    MOCK_METHOD(void, potr_internal_trace_socket_failure_at,
                (const char *, int, cplat_trace_level, const cplat_error *, const char *));

    Mock_porter();
    virtual ~Mock_porter();
};

extern Mock_porter *_mock_porter;

#endif /* MOCK_PORTER_H */
