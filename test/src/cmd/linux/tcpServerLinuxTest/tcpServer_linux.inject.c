#ifndef _IN_TEST_SRC
    #include "tcpServer_linux.c"
#endif
#include "tcpServer_linux.inject.h"

void tcpserver_test_worker_loop(int server_fd, int worker_id, int conns_per_worker)
{
    worker_loop(server_fd, worker_id, conns_per_worker);
}

void tcpserver_test_stop_running(void)
{
    running = 0;
}
