#ifndef POTR_TRACE_INJECT_H
#define POTR_TRACE_INJECT_H

#include <cplat/trace/tracer.h>

#ifdef __cplusplus
extern "C"
{
#endif
    void potr_trace_test_reset_state(void);
    cplat_tracer *potr_trace_test_get_published(void);
#ifdef __cplusplus
}
#endif

#endif /* POTR_TRACE_INJECT_H */
