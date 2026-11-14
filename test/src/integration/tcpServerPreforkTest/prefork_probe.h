#ifndef PREFORK_PROBE_H
#define PREFORK_PROBE_H

#include "tcpServer.h"
#include <cplat/trace/tracer.h>

#ifdef __cplusplus
extern "C"
{
#endif
    int tcpserver_command_main(int argc, char **argv);
#ifdef __cplusplus
}
#endif

#if defined(PLATFORM_WINDOWS)
    #define PREFORK_PROBE_RECORDS      256
    #define PREFORK_PROBE_OBSERVE      0
    #define PREFORK_PROBE_WORKER_EXIT  1
    #define PREFORK_PROBE_PIPE_BREAK   2
    #define PREFORK_PROBE_RESTORE_FAIL 3
    #define PREFORK_PROBE_LEAK         4
    #define PREFORK_PROBE_HOLD_DONE    5
    #define PREFORK_PROBE_HOLD_ACK     6
    #define PREFORK_PROBE_EARLY_ACK    7

typedef struct prefork_probe_record
{
    volatile LONG ready;
    DWORD process_id;
    int level;
    char message[192];
} prefork_probe_record;

typedef struct prefork_probe_state
{
    LONG scenario;
    volatile LONG armed;
    volatile LONG claimed;
    volatile LONG hook_ready;
    volatile LONG pipe_broken;
    volatile LONG restored_socket_created;
    volatile LONG target_pid;
    volatile LONG worker_pids[2];
    volatile LONG record_count;
    prefork_probe_record records[PREFORK_PROBE_RECORDS];
} prefork_probe_state;

    #ifdef __cplusplus
extern "C"
{
    #endif
    void prefork_probe_initialize(void);
    cplat_tracer *prefork_probe_tracer(void);
    void prefork_probe_worker_started(const int index, const DWORD process_id);
    SOCKET prefork_probe_restore(WSAPROTOCOL_INFOW *information);
    void prefork_probe_before_ack(const HANDLE pipe, const SOCKET socket_value);
    void prefork_probe_before_done(void);
    void prefork_probe_before_wait(void);
    int prefork_probe_skip_close(const SOCKET socket_value);
    void prefork_probe_accepted(void);
    #ifdef __cplusplus
}
    #endif

#endif /* PLATFORM_WINDOWS */

#endif
