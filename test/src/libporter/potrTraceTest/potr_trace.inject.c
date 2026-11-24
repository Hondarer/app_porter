#ifndef _IN_TEST_SRC
    #include "potr_trace.c"
#endif

#include "potr_trace.inject.h"

void potr_trace_test_reset_state(void)
{
    cplat_atomic_store_ptr(&s_trace, NULL, CPLAT_MEMORY_ORDER_RELAXED);
    cplat_atomic_store_i32(&s_trace_state, 0, CPLAT_MEMORY_ORDER_RELAXED);
}

cplat_tracer *potr_trace_test_get_published(void)
{
    return (cplat_tracer *)cplat_atomic_load_ptr(&s_trace, CPLAT_MEMORY_ORDER_ACQUIRE);
}
