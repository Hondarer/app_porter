#ifndef _IN_TEST_SRC
    #include "thread_recv_window.c"
#endif
#include "thread_recv_window.inject.h"
void test_slot_send_packet(thread_recv_slot *slot, const potr_packet *packet)
{
    slot_send_packet(slot, packet);
}
void test_slot_retransmit(thread_recv_slot *slot, uint32_t sequence)
{
    slot_retransmit_or_reject(slot, sequence);
}
