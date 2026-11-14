#ifndef POTR_RECV_THREAD_INJECT_H
#define POTR_RECV_THREAD_INJECT_H
#include "thread_recv_slot.h"
#ifdef __cplusplus
extern "C"
{
#endif
int test_tcp_handle_packet(potr_context *ctx, thread_recv_slot *slot, const uint8_t *wire, uint16_t payload_len);
int test_check_service_replay(potr_context *ctx, const potr_packet *packet, const uint8_t *wire);
#ifdef __cplusplus
}
#endif
#endif
