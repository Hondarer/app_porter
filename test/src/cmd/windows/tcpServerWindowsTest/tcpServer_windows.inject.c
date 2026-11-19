#ifndef _IN_TEST_SRC
    #include "tcpServer_windows.c"
#endif
#include "tcpServer_windows.inject.h"

void tcpserver_trace_test_emit(void)
{
    prefork_trace("parent_close", 42, (SOCKET)17, 0);
}

void tcpserver_test_worker_loop(const char *pipe_name, int conns_per_worker)
{
    worker_loop(pipe_name, conns_per_worker);
}

int tcpserver_test_find_available_worker(void)
{
    WorkerInfo worker = {0};
    HANDLE events[1];
    int worker_id;

    /* 1 ワーカーが空いている状態で、空きワーカーの予約を行う。 */
    worker.alive = 1;
    worker.ack_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    events[0] = CreateEventW(NULL, TRUE, TRUE, NULL);
    InitializeCriticalSection(&worker.state_lock);
    worker_id = find_available_worker(&worker, events, 1, 2);
    DeleteCriticalSection(&worker.state_lock);
    (void)CloseHandle(events[0]);
    (void)CloseHandle(worker.ack_event);
    return worker_id;
}

int tcpserver_test_worker_monitor_ack(const char *pipe_name)
{
    WorkerInfo worker = {0};
    HANDLE events[1];
    WorkerMonitorArg arg;
    WorkerReply reply = {WORKER_ACK, 1};
    OVERLAPPED connection = {0};
    DWORD written = 0;
    HANDLE client;

    /* 監視スレッドが読む親側のパイプと、受領通知を書き込むワーカー側のパイプを用意する。 */
    worker.pipe = CreateNamedPipeA(pipe_name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, NULL);
    if (worker.pipe == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    client = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (client == INVALID_HANDLE_VALUE)
    {
        (void)CloseHandle(worker.pipe);
        return 0;
    }

    connection.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ConnectNamedPipe(worker.pipe, &connection) && GetLastError() != ERROR_PIPE_CONNECTED)
    {
        (void)CloseHandle(connection.hEvent);
        (void)CloseHandle(client);
        (void)CloseHandle(worker.pipe);
        return 0;
    }
    (void)CloseHandle(connection.hEvent);

    if (!WriteFile(client, &reply, sizeof(reply), &written, NULL) || written != sizeof(reply))
    {
        (void)CloseHandle(client);
        (void)CloseHandle(worker.pipe);
        return 0;
    }

    /* 受領通知を書いた後にワーカー側を閉じ、監視スレッドの次の読み込みを失敗させて終える。 */
    (void)CloseHandle(client);

    worker.alive = 1;
    worker.pending = 1;
    worker.ack_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    events[0] = CreateEventW(NULL, TRUE, TRUE, NULL);
    InitializeCriticalSection(&worker.state_lock);
    arg.id = 0;
    arg.workers = &worker;
    arg.events = events;
    arg.conns_per_worker = 2;
    worker_monitor_thread(&arg);
    DeleteCriticalSection(&worker.state_lock);
    (void)CloseHandle(events[0]);
    (void)CloseHandle(worker.ack_event);
    (void)CloseHandle(worker.pipe);
    return 1;
}
