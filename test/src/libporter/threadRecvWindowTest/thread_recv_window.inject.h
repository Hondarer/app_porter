#ifndef THREAD_RECV_WINDOW_INJECT_H
#define THREAD_RECV_WINDOW_INJECT_H

#include "thread_recv_slot.h"
#ifdef __cplusplus
extern "C"
{
#endif
    int test_reorder_gap_ready(thread_recv_slot *slot, uint32_t nack_num);
#ifdef __cplusplus
}
#endif

#endif /* THREAD_RECV_WINDOW_INJECT_H */
