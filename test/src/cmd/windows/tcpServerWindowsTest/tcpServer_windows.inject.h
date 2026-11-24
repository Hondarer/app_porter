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

    /* Windows の子プロセス起動で使う内部処理を呼び出す。 */
    DWORD tcpserver_test_get_native_executable_path(char **path_out);
    DWORD tcpserver_test_build_command_line(const char *executable_path, const char *arguments,
                                            char **command_line_out);
    BOOL tcpserver_test_start_child_process(const char *executable_path, const char *arguments,
                                            BOOL inherit_handles, LPPROCESS_INFORMATION process_info,
                                            DWORD *error_out);
#ifdef __cplusplus
}
#endif

#endif
