#ifndef _IN_TEST_SRC
    #include "potr_health_thread.c"
#endif /* _IN_TEST_SRC */
#include "potr_health_thread.inject.h"
void test_health_thread_run(potr_context *ctx)
{
    health_thread_func(ctx);
}
