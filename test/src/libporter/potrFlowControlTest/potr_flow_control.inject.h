#ifndef POTR_FLOW_CONTROL_INJECT_H
#define POTR_FLOW_CONTROL_INJECT_H
#include <porter/potr_context.h>
#ifdef __cplusplus
extern "C" {
#endif
int potr_test_finish_limit_expiry(potr_context *ctx, uint64_t send_limit, uint64_t received_ms,
                                  uint64_t last_packet_ms);
#ifdef __cplusplus
}
#endif
#endif
