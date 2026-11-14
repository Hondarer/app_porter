#ifndef _IN_TEST_SRC
    #include "potr_recv_thread.c"
#endif
#include "potr_recv_thread.inject.h"
int test_tcp_handle_packet(potr_context *ctx, thread_recv_slot *slot, const uint8_t *wire, uint16_t payload_len)
{
    return tcp_handle_packet(ctx, slot, wire, payload_len, 0, 0);
}
int test_check_service_replay(potr_context *ctx, const potr_packet *packet, const uint8_t *wire)
{
    return check_service_replay(ctx, packet, wire);
}
