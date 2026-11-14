#ifndef POTR_SEND_THREAD_INJECT_H
#define POTR_SEND_THREAD_INJECT_H
#include <porter/potr_context.h>
#ifdef __cplusplus
extern "C"
{
#endif
int test_flush_packed(potr_context *ctx, size_t packed_len);
#ifdef __cplusplus
}
#endif
#endif
