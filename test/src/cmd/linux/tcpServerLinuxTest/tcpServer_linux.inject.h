#ifndef TCPSERVER_LINUX_INJECT_H
#define TCPSERVER_LINUX_INJECT_H

#ifdef __cplusplus
extern "C"
{
#endif
    /* worker_loop() を呼び出す。worker_loop() は最後に exit(0) でプロセスを終了する。 */
    void tcpserver_test_worker_loop(int server_fd, int worker_id, int conns_per_worker);

    /* worker_loop() の待機ループの継続フラグ (running) を 0 にする。 */
    void tcpserver_test_stop_running(void);
#ifdef __cplusplus
}
#endif

#endif
