#ifndef _IN_TEST_SRC
    #include "potr_flow_control.c"
#endif
#include "potr_flow_control.inject.h"

/* 期限切れの比較交換と受信時刻の再確認の間の順序を、スレッドの実行時機に依存せず検証する。 */
int potr_test_finish_limit_expiry(potr_context *ctx, uint64_t send_limit, uint64_t received_ms,
                                  uint64_t last_packet_ms)
{
    int expired;

    cplat_local_lock_lock(ctx->flow_limit_mutex);
    expired = finish_limit_expiry(ctx, send_limit, received_ms, last_packet_ms);
    cplat_local_lock_unlock(ctx->flow_limit_mutex);
    return expired;
}
