#ifndef TCPSERVER_WINDOWS_INJECT_H
#define TCPSERVER_WINDOWS_INJECT_H

#ifdef __cplusplus
extern "C"
{
#endif
    void tcpserver_trace_test_emit(void);

    /* worker_loop() を呼び出す。 */
    void tcpserver_test_worker_loop(const char *pipe_name, int conns_per_worker);

    /* 1 ワーカーが空いている状態で find_available_worker() を呼び出し、その戻り値を返す。 */
    int tcpserver_test_find_available_worker(void);

    /* 受領通知 (ACK) を 1 件書いた親側のパイプで worker_monitor_thread() を呼び出す。準備に失敗した場合は 0 を返す。 */
    int tcpserver_test_worker_monitor_ack(const char *pipe_name);
#ifdef __cplusplus
}
#endif

#endif
