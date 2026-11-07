/**
 *******************************************************************************
 *  @file           potr_send_queue_abort.c
 *  @brief          送信待機を異常理由で解除します。
 *******************************************************************************
 */
#include <porter/infra/potr_send_queue.h>

/* Doxygen コメントは、ヘッダーに記載 */

void potr_internal_send_queue_abort(potr_internal_send_queue *q, int error)
{
    if (q == NULL || q->mutex == NULL)
    {
        return;
    }

    cplat_local_lock_lock(q->mutex, CPLAT_SYNC_WAIT_FOREVER);
    q->failure = error;
    cplat_condvar_broadcast(q->not_empty);
    cplat_condvar_broadcast(q->not_full);
    cplat_condvar_broadcast(q->progress);
    cplat_condvar_broadcast(q->drained);
    cplat_local_lock_unlock(q->mutex);
}
