#ifndef _IN_TEST_SRC
    #include "potr_send_thread.c"
#endif
#include "potr_send_thread.inject.h"
int test_flush_packed(potr_context *ctx, size_t packed_len)
{
    return flush_packed(ctx, packed_len);
}
