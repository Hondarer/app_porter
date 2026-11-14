#ifndef _IN_TEST_SRC
    #include "tcpServer_windows.c"
#endif
#include "tcpServer_windows.inject.h"

void tcpserver_trace_test_emit(void)
{
    prefork_trace("parent_close", 42, (SOCKET)17, 0);
}
