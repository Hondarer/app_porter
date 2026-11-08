#ifndef _IN_TEST_SRC
    #include "thread_recv_window.c"
#endif
#include "thread_recv_window.inject.h"
int test_reorder_gap_ready(thread_recv_slot *slot, const uint32_t nack_num)
{
    return reorder_gap_ready(slot, nack_num);
}
