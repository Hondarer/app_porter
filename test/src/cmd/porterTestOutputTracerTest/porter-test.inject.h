#ifndef PORTER_TEST_INJECT_H
#define PORTER_TEST_INJECT_H

#include <porter/porter_type.h>

#ifdef __cplusplus
extern "C"
{
#endif
    void test_ensure_tracer_started(void);
    int test_get_tracer_started(void);
    void test_reset_tracer_started(void);
    void test_reset_tracer_screen(void);
#ifdef __cplusplus
}
#endif

#endif /* PORTER_TEST_INJECT_H */
