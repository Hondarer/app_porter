#include <cplat/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_porter.h>

Mock_porter *_mock_porter = nullptr;

Mock_porter::Mock_porter()
{
    ON_CALL(*this, api_open_paths_unicast(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, api_open_paths_multicast(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, api_open_paths_broadcast(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, api_open_paths_unicast_bidir(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, api_open_paths_unicast_bidir_n1(_)).WillByDefault(Return(0));
    ON_CALL(*this, api_open_paths_tcp(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, api_paths_resolve_ipv4(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, api_paths_parse_ipv4(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, thread_connect_read_first_packet(_, _, _, _, _)).WillByDefault(Return(0));
    ON_CALL(*this, thread_connect_session_compare(_, _)).WillByDefault(Return(TCP_SESSION_NEW));
    ON_CALL(*this, thread_connect_with_timeout(_, _)).WillByDefault(Return(CPLAT_INVALID_SOCKET));
    ON_CALL(*this, thread_connect_peer_allowed(_, _, _)).WillByDefault(Return(1));
    ON_CALL(*this, thread_recv_health_check_peers(_)).WillByDefault(Return());
    ON_CALL(*this, thread_recv_health_check_service(_)).WillByDefault(Return());
    ON_CALL(*this, thread_recv_health_wake_tcp(_, _, _)).WillByDefault(Return());
    // peer table
    ON_CALL(*this, potr_internal_peer_find_by_id(_, _)).WillByDefault(Return(nullptr));
    ON_CALL(*this, potr_internal_peer_send_fin(_, _)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_peer_free(_, _)).WillByDefault(Return());

    // trace
    ON_CALL(*this, potr_internal_trace_get()).WillByDefault(Return(nullptr));
    ON_CALL(*this, potr_tracer_get()).WillByDefault(Return(nullptr));

    ON_CALL(*this, api_context_dispose_resources(_)).WillByDefault(Return());
    ON_CALL(*this, api_context_dispose_tcp_sync(_)).WillByDefault(Return());
    ON_CALL(*this, api_context_has_threads(_)).WillByDefault(Return(0));
    ON_CALL(*this, api_open_paths_by_type(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, api_setup_dest_addr(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_callback_mutex_dispose(_)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_callback_mutex_init(_)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_comm_recv_thread_start(_)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_comm_recv_thread_stop(_)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_connect_thread_start(_)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_connect_thread_stop(_)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_copy_tcp_path_states(_, _)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_emit_service_path_events_locked(_, _)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_health_thread_start(_)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_health_thread_stop(_)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_health_thread_wake(_)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_packet_build_fin(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_packet_build_packed(_, _, _, _, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_packet_build_ping(_, _, _, _, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_packet_parse(_, _, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_packet_unpack_next(_, _, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_packet_wire_size(_)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_peer_create(_, _, _)).WillByDefault(Return(nullptr));
    ON_CALL(*this, potr_internal_peer_find_by_session(_, _, _)).WillByDefault(Return(nullptr));
    ON_CALL(*this, potr_internal_peer_path_clear(_, _, _)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_peer_table_dispose(_)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_peer_table_init(_)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_peer_table_send_fin(_)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_result_from_error(_)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_result_from_socket_result(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_send_queue_complete(_)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_send_queue_dispose(_)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_send_queue_init(_, _, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_send_queue_peek(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_send_queue_peek_timed(_, _, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_send_queue_pop(_, _, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_send_queue_shutdown(_)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_send_queue_try_pop(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_send_queue_wait_drained(_)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_send_thread_start(_)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_send_thread_stop(_)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_start_connected_threads(_, _, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_sync_service_path_state_locked(_, _, _)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_tcp_health_thread_start(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_tcp_health_thread_stop(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_tcp_health_thread_wake(_, _)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_tcp_health_thread_wake_all(_)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_tcp_recv_thread_start(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_tcp_recv_thread_stop(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_tcp_send_control_packet(_, _, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_tcp_send_ping_now(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_window_dispose(_)).WillByDefault(Return());
    ON_CALL(*this, potr_internal_window_init(_, _, _, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_window_recv_pop(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_window_recv_push(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, potr_internal_window_send_push(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, thread_recv_apply_remote_path_ping_state(_, _, _)).WillByDefault(Return());
    ON_CALL(*this, thread_recv_authenticate_packet(_, _, _, _, _)).WillByDefault(Return(0));
    ON_CALL(*this, thread_recv_check_src_addr(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, thread_recv_disconnect_peer_all_paths(_, _)).WillByDefault(Return());
    ON_CALL(*this, thread_recv_disconnect_service_all_paths(_)).WillByDefault(Return());
    ON_CALL(*this, thread_recv_dispatch_packet(_, _, _, _)).WillByDefault(Return(0));
    ON_CALL(*this, thread_recv_fin_fire(_, _)).WillByDefault(Return());
    ON_CALL(*this, thread_recv_fin_on_ack(_, _)).WillByDefault(Return());
    ON_CALL(*this, thread_recv_fin_on_packet(_, _, _)).WillByDefault(Return(0));
    ON_CALL(*this, thread_recv_fin_pending_reached(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, thread_recv_session_adopt(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, thread_recv_set_all_path_ping_states(_, _, _)).WillByDefault(Return(0));
    ON_CALL(*this, thread_recv_set_path_ping_state(_, _)).WillByDefault(Return(0));
    ON_CALL(*this, thread_recv_slot_deliver_payload_elem(_, _)).WillByDefault(Return());
    ON_CALL(*this, thread_recv_slot_init_ctx(_, _)).WillByDefault(Return());
    ON_CALL(*this, thread_recv_slot_init_peer(_, _, _)).WillByDefault(Return());
    ON_CALL(*this, thread_recv_sync_path_state(_)).WillByDefault(Return());
    ON_CALL(*this, thread_recv_wake_udp_interrupt_ping(_, _)).WillByDefault(Return());
    ON_CALL(*this, thread_recv_window_check_reorder_timeout(_)).WillByDefault(Return());
    ON_CALL(*this, thread_recv_window_on_nack(_, _)).WillByDefault(Return());

    ON_CALL(*this, potr_internal_trace_socket_failure_at(_, _, _, _, _)).WillByDefault(Return());

    TESTFW_REGISTER_MOCK_INSTANCE(_mock_porter);
}

Mock_porter::~Mock_porter()
{
    TESTFW_UNREGISTER_MOCK_INSTANCE(_mock_porter);
}
