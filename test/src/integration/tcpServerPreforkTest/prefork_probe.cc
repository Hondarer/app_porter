#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
    #include "prefork_probe.h"
    #include <testfw.h>
    #include <cplat/console/console.h>
    #include <cstdio>
    #include <cstring>

namespace
{
prefork_probe_state *s_state = nullptr;
cplat_tracer *s_tracer = nullptr;
HANDLE s_release = nullptr;
SOCKET s_leaked_socket = INVALID_SOCKET;

// フックは記録だけを行い、製品スレッドをテスト指示待ちにしない。
// ready の公開・取得にはプロセス間共有メモリーで使用できる Interlocked を使う。
// see: https://learn.microsoft.com/en-us/windows/win32/sync/interlocked-variable-access
void record_trace(cplat_tracer_hook_entry *, cplat_tracer *, cplat_trace_level level, const cplat_timespec *,
                  const char *message, void *)
{
    LONG index = InterlockedIncrement(&s_state->record_count) - 1;
    if (index < PREFORK_PROBE_RECORDS)
    {
        prefork_probe_record &record = s_state->records[index];
        record.process_id = GetCurrentProcessId();
        record.level = static_cast<int>(level);
        strcpy_s(record.message, message);
        InterlockedExchange(&record.ready, 1);
    }
}

bool claim(const int scenario)
{
    if (s_state == nullptr || s_state->scenario != scenario || InterlockedCompareExchange(&s_state->armed, 0, 0) == 0)
    {
        return false;
    }
    if (InterlockedCompareExchange(&s_state->claimed, 1, 0) != 0)
    {
        return false;
    }
    InterlockedExchange(&s_state->target_pid, static_cast<LONG>(GetCurrentProcessId()));
    return true;
}

void hold(void)
{
    InterlockedExchange(&s_state->hook_ready, 1);
    // 期限と回収はテスト側の Job Object が担当する。
    if (WaitForSingleObject(s_release, INFINITE) != WAIT_OBJECT_0)
    {
        ExitProcess(91);
    }
}
} // namespace

extern "C" void prefork_probe_initialize(void)
{
    wchar_t key[128];
    DWORD count = GetEnvironmentVariableW(L"POTR_PREFORK_PROBE_KEY", key, 128);
    if (count == 0 || count >= 128)
    {
        ExitProcess(92);
    }
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, key);
    if (mapping == nullptr)
    {
        ExitProcess(93);
    }
    s_state = static_cast<prefork_probe_state *>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*s_state)));
    CloseHandle(mapping);
    wchar_t event_name[160];
    swprintf_s(event_name, L"%s.release", key);
    s_release = OpenEventW(SYNCHRONIZE, FALSE, event_name);
    if (s_state == nullptr || s_release == nullptr)
    {
        ExitProcess(94);
    }
    s_tracer = cplat_tracer_create(CPLAT_TRACER_CONCURRENCY_TRACER_MANAGED);
    if (s_tracer == nullptr || cplat_tracer_set_hook(s_tracer, record_trace, nullptr) == nullptr ||
        cplat_tracer_start(s_tracer) != CPLAT_OK)
    {
        ExitProcess(95);
    }
}

extern "C" cplat_tracer *prefork_probe_tracer(void)
{
    return s_tracer;
}

extern "C" void prefork_probe_worker_started(const int index, const DWORD process_id)
{
    if (index < 2)
    {
        InterlockedExchange(&s_state->worker_pids[index], static_cast<LONG>(process_id));
    }
}

extern "C" SOCKET prefork_probe_restore(WSAPROTOCOL_INFOW *information)
{
    SOCKET restored =
        WSASocketW(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, information, 0, WSA_FLAG_OVERLAPPED);
    if (claim(PREFORK_PROBE_RESTORE_FAIL))
    {
        // 複製情報を消費した実ソケットを回収してから戻り値だけを異常にする。
        if (restored != INVALID_SOCKET)
        {
            InterlockedExchange(&s_state->restored_socket_created, 1);
            closesocket(restored);
        }
        InterlockedExchange(&s_state->hook_ready, 1);
        WSASetLastError(WSAEINVAL);
        return INVALID_SOCKET;
    }
    return restored;
}

extern "C" void prefork_probe_before_ack(const HANDLE pipe, const SOCKET socket_value)
{
    if (claim(PREFORK_PROBE_WORKER_EXIT) || claim(PREFORK_PROBE_HOLD_ACK))
    {
        hold();
    }
    else if (claim(PREFORK_PROBE_PIPE_BREAK))
    {
        hold();
        if (socket_value != INVALID_SOCKET)
        {
            closesocket(socket_value);
        }
        // クライアント側パイプを閉じ、ワーカーのプロセス ハンドルはシグナルにしない。
        CloseHandle(pipe);
        InterlockedExchange(&s_state->pipe_broken, 1);
        HANDLE parked = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (parked == nullptr)
        {
            ExitProcess(96);
        }
        WaitForSingleObject(parked, INFINITE);
        ExitProcess(97);
    }
}

extern "C" void prefork_probe_before_done(void)
{
    if (claim(PREFORK_PROBE_HOLD_DONE))
    {
        hold();
    }
}

extern "C" int prefork_probe_skip_close(const SOCKET socket_value)
{
    if (claim(PREFORK_PROBE_LEAK))
    {
        s_leaked_socket = socket_value;
        InterlockedExchange(&s_state->hook_ready, 1);
        return 1;
    }
    return 0;
}

extern "C" void prefork_probe_before_wait(void)
{
    if (claim(PREFORK_PROBE_EARLY_ACK))
    {
        hold();
    }
}

extern "C" void prefork_probe_accepted(void)
{
    if (s_leaked_socket != INVALID_SOCKET && WaitForSingleObject(s_release, 0) == WAIT_OBJECT_0)
    {
        closesocket(s_leaked_socket);
        s_leaked_socket = INVALID_SOCKET;
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--probe") == 0)
    {
        return tcpserver_command_main(argc - 1, argv + 1);
    }
    if (argc > 1 && std::strcmp(argv[1], "--worker") == 0)
    {
        return tcpserver_command_main(argc, argv);
    }
    cplat_console_init();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
