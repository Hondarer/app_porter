#ifndef _IN_TEST_SRC
    #include "porter-test.c"
#endif
#include "porter-test.inject.h"
void test_binary_receive(const void *data, const size_t length)
{
    on_recv(1, 0, POTR_EVENT_DATA, data, length);
}
