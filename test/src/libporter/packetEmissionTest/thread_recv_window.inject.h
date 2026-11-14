#ifndef THREAD_RECV_WINDOW_INJECT_H
#define THREAD_RECV_WINDOW_INJECT_H
#include "thread_recv_slot.h"
#ifdef __cplusplus
extern "C"
{
#endif
void test_slot_send_packet(thread_recv_slot *slot, const potr_packet *packet);
void test_slot_retransmit(thread_recv_slot *slot, uint32_t sequence);
#ifdef __cplusplus
}
#endif
#endif
