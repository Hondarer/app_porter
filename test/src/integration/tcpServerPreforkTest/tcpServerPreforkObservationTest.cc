#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
    #include "prefork_probe.h"
    #include <testfw.h>
    #include <chrono>
    #include <cstdio>
    #include <cstring>
    #include <functional>
    #include <stdexcept>
    #include <string>
    #include <thread>
    #include <vector>

namespace
{
void require(const bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(std::string(message) + " (Win32=" + std::to_string(GetLastError()) +
                                 ", Winsock=" + std::to_string(WSAGetLastError()) + ")");
    }
}

void wait_until(const std::function<bool()> &condition, const char *message)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!condition() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(condition(), message);
}

struct handle_owner
{
    HANDLE value = nullptr;
    ~handle_owner()
    {
        if (value != nullptr && value != INVALID_HANDLE_VALUE)
        {
            CloseHandle(value);
        }
    }
};

const char s_message[] = "prefork-observation";

class server_session
{
  public:
    handle_owner job;
    handle_owner process;
    handle_owner mapping;
    handle_owner release;
    prefork_probe_state *state = nullptr;
    sockaddr_in endpoint = {};
    std::vector<SOCKET> clients;
    bool winsock_started = false;
    DWORD handles_before = 0;

    ~server_session()
    {
        // 子プロセスを先に回収し、テスト失敗時も制御待機を残さない。
        if (job.value != nullptr)
        {
            TerminateJobObject(job.value, 0);
        }
        if (process.value != nullptr)
        {
            WaitForSingleObject(process.value, 10000);
        }
        for (SOCKET client : clients)
        {
            closesocket(client);
        }
        if (state != nullptr)
        {
            UnmapViewOfFile(state);
        }
        if (winsock_started)
        {
            WSACleanup();
        }
    }

    void start(const int capacity, const int scenario, const bool instrumented)
    {
        WSADATA data = {};
        require(WSAStartup(MAKEWORD(2, 2), &data) == 0, "WSAStartup");
        winsock_started = true;
        SOCKET port_probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        require(port_probe != INVALID_SOCKET, "port probe socket");
        clients.push_back(port_probe);
        endpoint.sin_family = AF_INET;
        endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        require(::bind(port_probe, reinterpret_cast<sockaddr *>(&endpoint), static_cast<int>(sizeof(endpoint))) == 0,
                "bind port probe");
        int size = sizeof(endpoint);
        require(getsockname(port_probe, reinterpret_cast<sockaddr *>(&endpoint), &size) == 0, "getsockname");
        closesocket(port_probe);
        clients.clear();

        wchar_t executable[4096] = {};
        std::wstring key;
        if (instrumented)
        {
            DWORD count = GetModuleFileNameW(nullptr, executable, 4096);
            require(count != 0 && count < 4096, "probe executable path");
            static LONG sequence = 0;
            key = L"Local\\porter_prefork_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
                  std::to_wstring(InterlockedIncrement(&sequence));
            mapping.value = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                               sizeof(prefork_probe_state), key.c_str());
            require(mapping.value != nullptr && GetLastError() != ERROR_ALREADY_EXISTS, "create control mapping");
            state = static_cast<prefork_probe_state *>(
                MapViewOfFile(mapping.value, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(prefork_probe_state)));
            require(state != nullptr, "map control state");
            std::memset(state, 0, sizeof(*state));
            state->scenario = scenario;
            release.value = CreateEventW(nullptr, TRUE, FALSE, (key + L".release").c_str());
            require(release.value != nullptr, "create release event");
        }
        else
        {
            DWORD count = GetEnvironmentVariableW(L"POTR_TCPSERVER_TEST_EXE", executable, 4096);
            require(count < 4096, "normal executable path length");
            if (count == 0)
            {
                wcscpy_s(executable, L"../../../../prod/cbin/tcpServer.exe");
            }
        }
        std::wstring command = L"\"" + std::wstring(executable) + L"\"" + (instrumented ? L" --probe" : L"") +
                               L" --mode prefork --port " + std::to_wstring(ntohs(endpoint.sin_port)) +
                               L" --workers 2 --conns-per-worker " + std::to_wstring(capacity);
        job.value = CreateJobObjectW(nullptr, nullptr);
        require(job.value != nullptr, "create job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
        // 子の起動前に Job へ登録し、ワーカーも同じ Job で回収する。
        // see: https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        require(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != 0,
                "set job cleanup");
        wchar_t previous_key[4096] = {};
        DWORD previous_count = GetEnvironmentVariableW(L"POTR_PREFORK_PROBE_KEY", previous_key, 4096);
        require(previous_count < 4096, "previous control key length");
        if (instrumented)
        {
            require(SetEnvironmentVariableW(L"POTR_PREFORK_PROBE_KEY", key.c_str()) != 0, "set control key");
        }
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION information = {};
        BOOL created = CreateProcessW(nullptr, &command[0], nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr,
                                      &startup, &information);
        DWORD create_error = GetLastError();
        if (instrumented)
        {
            SetEnvironmentVariableW(L"POTR_PREFORK_PROBE_KEY", previous_count == 0 ? nullptr : previous_key);
        }
        SetLastError(create_error);
        require(created != 0, "create server process");
        process.value = information.hProcess;
        handle_owner thread;
        thread.value = information.hThread;
        if (!AssignProcessToJobObject(job.value, process.value))
        {
            TerminateProcess(process.value, 98);
            require(false, "assign server job");
        }
        require(ResumeThread(thread.value) != static_cast<DWORD>(-1), "resume server");
        SOCKET ready = INVALID_SOCKET;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        do
        {
            SOCKET candidate = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            require(candidate != INVALID_SOCKET, "ready client socket");
            if (connect(candidate, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint)) == 0)
            {
                ready = candidate;
            }
            else
            {
                closesocket(candidate);
                require(WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT, "server exited during startup");
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } while (ready == INVALID_SOCKET && std::chrono::steady_clock::now() < deadline);
        require(ready != INVALID_SOCKET, "server startup deadline");
        clients.push_back(ready);
        set_timeout(ready, 3000);
        echo(ready);
        finish(ready);
        if (state != nullptr)
        {
            wait_until([&] { return count_trace("done") >= 1; }, "initial DONE");
            wait_until([&] { return count_trace("parent_close") >= 1; }, "initial parent close record");
        }
        GetProcessHandleCount(process.value, &handles_before);
    }

    void set_timeout(const SOCKET client, const DWORD timeout)
    {
        require(
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout)) == 0,
            "client receive deadline");
        require(
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout)) == 0,
            "client send deadline");
    }

    SOCKET open_client()
    {
        SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        require(client != INVALID_SOCKET, "new client socket");
        clients.push_back(client);
        set_timeout(client, 3000);
        require(connect(client, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint)) == 0, "client connect");
        return client;
    }

    void send_message(const SOCKET client)
    {
        int sent = 0;
        while (sent < static_cast<int>(sizeof(s_message)))
        {
            int part = send(client, s_message + sent, static_cast<int>(sizeof(s_message)) - sent, 0);
            require(part > 0, "send echo request");
            sent += part;
        }
    }

    void read_echo(const SOCKET client)
    {
        char received[sizeof(s_message)] = {};
        int count = 0;
        while (count < static_cast<int>(sizeof(received)))
        {
            int part = recv(client, received + count, static_cast<int>(sizeof(received)) - count, 0);
            require(part > 0, "receive complete echo");
            count += part;
        }
        require(std::memcmp(received, s_message, sizeof(received)) == 0, "echo payload equality");
    }

    void echo(const SOCKET client)
    {
        send_message(client);
        read_echo(client);
    }

    void no_reply(const SOCKET client)
    {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(client, &readable);
        timeval timeout = {0, 150000};
        require(select(0, &readable, nullptr, nullptr, &timeout) == 0, "request must remain pending");
    }

    void disconnected(const SOCKET client, const bool allow_reset)
    {
        char byte;
        int result = recv(client, &byte, 1, 0);
        int error = result == SOCKET_ERROR ? WSAGetLastError() : 0;
        require(result == 0 || (allow_reset && result == SOCKET_ERROR && error == WSAECONNRESET),
                "peer closure (timeout is a failure)");
    }

    void finish(const SOCKET client)
    {
        require(shutdown(client, SD_SEND) == 0, "client half-close");
        disconnected(client, false);
    }

    LONG count_trace(const char *stage, const DWORD worker_pid = 0, const char *result = nullptr)
    {
        LONG found = 0;
        std::string prefix = std::string(stage) + " ";
        std::string worker = "worker=" + std::to_string(worker_pid) + " ";
        for (int index = 0; index < PREFORK_PROBE_RECORDS; ++index)
        {
            prefork_probe_record &record = state->records[index];
            if (InterlockedCompareExchange(&record.ready, 0, 0) != 0 &&
                std::strncmp(record.message, prefix.c_str(), prefix.size()) == 0 &&
                (worker_pid == 0 || std::strstr(record.message, worker.c_str()) != nullptr) &&
                (result == nullptr || std::strstr(record.message, result) != nullptr))
            {
                require(record.level == CPLAT_TRACE_LEVEL_NONE, "trace level must be NONE");
                ++found;
            }
        }
        return found;
    }

    void wait_trace(const char *stage, const LONG minimum, const DWORD worker_pid = 0, const char *result = nullptr)
    {
        wait_until([&] { return count_trace(stage, worker_pid, result) >= minimum; }, stage);
    }

    DWORD target_pid()
    {
        return static_cast<DWORD>(InterlockedCompareExchange(&state->target_pid, 0, 0));
    }

    void arm()
    {
        InterlockedExchange(&state->armed, 1);
    }

    void wait_hook()
    {
        wait_until([&] { return InterlockedCompareExchange(&state->hook_ready, 0, 0) != 0; }, "fault hook reached");
    }

    void resume()
    {
        require(SetEvent(release.value) != 0, "release probe checkpoint");
    }

    void terminate_worker(const DWORD process_id)
    {
        handle_owner worker;
        worker.value = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, process_id);
        require(worker.value != nullptr, "open worker for termination");
        require(TerminateProcess(worker.value, 0) != 0, "terminate worker");
        require(WaitForSingleObject(worker.value, 10000) == WAIT_OBJECT_0, "worker termination deadline");
    }

    void worker_alive(const DWORD process_id)
    {
        handle_owner worker;
        worker.value = OpenProcess(SYNCHRONIZE, FALSE, process_id);
        require(worker.value != nullptr && WaitForSingleObject(worker.value, 0) == WAIT_TIMEOUT,
                "pipe failure must leave worker alive");
    }

    void close_and_verify()
    {
        require(TerminateJobObject(job.value, 0) != 0, "terminate complete server job");
        wait_until(
            [&]
            {
                JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting = {};
                require(QueryInformationJobObject(job.value, JobObjectBasicAccountingInformation, &accounting,
                                                  sizeof(accounting), nullptr) != 0,
                        "query server job cleanup");
                return accounting.ActiveProcesses == 0;
            },
            "parent and all workers must leave job");
    }

    void verify_records()
    {
        require(InterlockedCompareExchange(&state->record_count, 0, 0) < PREFORK_PROBE_RECORDS,
                "trace buffer overflow");
        require(WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT, "parent must remain alive");
        DWORD handles_after = 0;
        if (GetProcessHandleCount(process.value, &handles_after))
        {
            std::printf("reference: parent handles before=%lu after=%lu (not a pass criterion)\n", handles_before,
                        handles_after);
        }
    }
};

// [サブ手順 名前=tcpServerPreforkObservationTest.capacity_observation]
std::string capacity_observation(const int capacity)
{
    try
    {
        server_session server;
        server.start(capacity, PREFORK_PROBE_OBSERVE, false);
        std::vector<SOCKET> occupied;
        for (int index = 0; index < capacity * 2; ++index)
        {
            SOCKET client = server.open_client();
            occupied.push_back(client);
            server.echo(client);
        }
        SOCKET pending = server.open_client();
        server.send_message(pending);
        server.no_reply(pending);
        server.finish(occupied[0]);
        server.read_echo(pending);
        server.finish(pending);
        for (size_t index = 1; index < occupied.size(); ++index)
        {
            server.finish(occupied[index]);
        }
        server.close_and_verify();
        return {};
    }
    catch (const std::exception &error)
    {
        return error.what();
    }
}
// [サブ手順終了]

// [サブ手順 名前=tcpServerPreforkObservationTest.ack_fault_observation]
std::string ack_fault_observation(const int scenario)
{
    try
    {
        server_session server;
        server.start(1, scenario, true);
        LONG closed_before = server.count_trace("parent_close");
        LONG wait_before = server.count_trace("ack_wait");
        server.arm();
        SOCKET faulted = server.open_client();
        server.send_message(faulted);
        server.wait_hook();
        DWORD target = server.target_pid();
        if (scenario != PREFORK_PROBE_RESTORE_FAIL)
        {
            server.wait_trace("ack_wait", wait_before + 1);
            server.no_reply(faulted);
            require(server.count_trace("parent_close") == closed_before, "parent must retain socket until ACK");
        }
        if (scenario == PREFORK_PROBE_WORKER_EXIT)
        {
            server.terminate_worker(target);
        }
        else if (scenario == PREFORK_PROBE_PIPE_BREAK)
        {
            server.resume();
            wait_until([&] { return InterlockedCompareExchange(&server.state->pipe_broken, 0, 0) != 0; },
                       "pipe broken");
            server.worker_alive(target);
        }
        else if (scenario == PREFORK_PROBE_HOLD_ACK)
        {
            server.resume();
            server.read_echo(faulted);
            server.finish(faulted);
        }
        if (scenario != PREFORK_PROBE_HOLD_ACK)
        {
            server.disconnected(faulted, true);
        }
        server.wait_trace("parent_close", 1, target, "result=0");
        server.wait_trace("parent_close", closed_before + 1);
        if (scenario == PREFORK_PROBE_RESTORE_FAIL)
        {
            require(InterlockedCompareExchange(&server.state->restored_socket_created, 0, 0) == 1,
                    "restore fault must consume and close the duplicated socket");
            server.wait_trace("ack", 1, target, "result=0");
        }
        SOCKET next = server.open_client();
        server.echo(next);
        server.finish(next);
        if (scenario == PREFORK_PROBE_PIPE_BREAK)
        {
            server.worker_alive(target);
            server.wait_trace("monitor_dead", 1, target);
        }
        server.verify_records();
        server.close_and_verify();
        return {};
    }
    catch (const std::exception &error)
    {
        return error.what();
    }
}
// [サブ手順終了]

// [サブ手順 名前=tcpServerPreforkObservationTest.leak_sensitivity]
std::string leak_sensitivity()
{
    try
    {
        server_session server;
        server.start(1, PREFORK_PROBE_LEAK, true);
        server.arm();
        SOCKET leaked = server.open_client();
        server.echo(leaked);
        server.wait_hook();
        require(shutdown(leaked, SD_SEND) == 0, "half-close leak client");
        server.set_timeout(leaked, 200);
        char byte;
        int result = recv(leaked, &byte, 1, 0);
        require(result == SOCKET_ERROR && WSAGetLastError() == WSAETIMEDOUT, "omitted close must prevent EOF");
        server.wait_trace("parent_close_skipped", 1);
        server.resume();
        SOCKET next = server.open_client();
        server.echo(next);
        server.set_timeout(leaked, 3000);
        server.disconnected(leaked, false);
        server.finish(next);
        server.verify_records();
        server.close_and_verify();
        return {};
    }
    catch (const std::exception &error)
    {
        return error.what();
    }
}
// [サブ手順終了]

// [サブ手順 名前=tcpServerPreforkObservationTest.early_ack_observation]
std::string early_ack_observation()
{
    try
    {
        server_session server;
        server.start(1, PREFORK_PROBE_EARLY_ACK, true);
        LONG ack_before = server.count_trace("ack");
        LONG closed_before = server.count_trace("parent_close");
        server.arm();
        SOCKET client = server.open_client();
        server.send_message(client);
        server.wait_hook();
        server.wait_trace("ack", ack_before + 1);
        require(server.count_trace("parent_close") == closed_before, "parent must remain at pre-wait gate");
        server.resume();
        server.read_echo(client);
        server.finish(client);
        server.wait_trace("parent_close", closed_before + 1);
        server.verify_records();
        server.close_and_verify();
        return {};
    }
    catch (const std::exception &error)
    {
        return error.what();
    }
}
// [サブ手順終了]

// [サブ手順 名前=tcpServerPreforkObservationTest.done_sensitivity]
std::string done_sensitivity()
{
    try
    {
        server_session server;
        server.start(1, PREFORK_PROBE_HOLD_DONE, true);
        SOCKET first = server.open_client();
        server.echo(first);
        SOCKET second = server.open_client();
        server.echo(second);
        server.arm();
        server.finish(first);
        server.wait_hook();
        SOCKET pending = server.open_client();
        server.send_message(pending);
        server.wait_trace("capacity_wait", 1);
        server.no_reply(pending);
        server.resume();
        server.read_echo(pending);
        server.finish(pending);
        server.finish(second);
        server.wait_trace("done", 2);
        server.verify_records();
        server.close_and_verify();
        return {};
    }
    catch (const std::exception &error)
    {
        return error.what();
    }
}
// [サブ手順終了]

// [サブ手順 名前=tcpServerPreforkObservationTest.capacity_fault_observation]
std::string capacity_fault_observation(const bool lose_all)
{
    try
    {
        server_session server;
        server.start(1, PREFORK_PROBE_OBSERVE, true);
        SOCKET first = server.open_client();
        server.echo(first);
        SOCKET second = server.open_client();
        server.echo(second);
        SOCKET pending = server.open_client();
        server.send_message(pending);
        server.wait_trace("capacity_wait", 1);
        server.no_reply(pending);
        DWORD first_pid = static_cast<DWORD>(InterlockedCompareExchange(&server.state->worker_pids[0], 0, 0));
        DWORD second_pid = static_cast<DWORD>(InterlockedCompareExchange(&server.state->worker_pids[1], 0, 0));
        require(first_pid != 0 && second_pid != 0, "worker identities");
        server.terminate_worker(first_pid);
        server.wait_trace("monitor_dead", 1, first_pid);
        server.disconnected(first, true);
        if (lose_all)
        {
            server.terminate_worker(second_pid);
            server.disconnected(second, true);
            server.disconnected(pending, true);
            SOCKET rejected = server.open_client();
            server.disconnected(rejected, true);
            server.wait_trace("no_worker", 2);
        }
        else
        {
            server.finish(second);
            server.read_echo(pending);
            server.finish(pending);
        }
        server.verify_records();
        server.close_and_verify();
        return {};
    }
    catch (const std::exception &error)
    {
        return error.what();
    }
}
// [サブ手順終了]
} // namespace

TEST(tcpServerPreforkObservationTest, reuses_sequential_capacity_after_eof)
{
    // Arrange
    // Pre-Assert
    // Act
    // [サブ手順参照 名前=tcpServerPreforkObservationTest.capacity_observation]
    std::string error = capacity_observation(1);
    // Assert
    EXPECT_TRUE(error.empty()) << error; // [確認_正常系] - 全枠占有中の通信が 1 枠解放後に完了し、EOF を受信すること。
}

TEST(tcpServerPreforkObservationTest, reuses_multiplex_capacity_after_eof)
{
    // Arrange
    // Pre-Assert
    // Act
    // [サブ手順参照 名前=tcpServerPreforkObservationTest.capacity_observation]
    std::string error = capacity_observation(3);
    // Assert
    EXPECT_TRUE(error.empty()) << error; // [確認_正常系] - 多重処理の全枠占有後に 1 枠を再利用し、EOF を受信すること。
}

TEST(tcpServerPreforkObservationTest, releases_ack_wait_when_worker_exits)
{
    // Arrange
    // Pre-Assert
    // Act
    // [サブ手順参照 名前=tcpServerPreforkObservationTest.ack_fault_observation]
    std::string error = ack_fault_observation(PREFORK_PROBE_WORKER_EXIT);
    // Assert
    EXPECT_TRUE(error.empty())
        << error; // [確認_異常系] - ワーカー終了で ACK 待機を解除し、接続切断と後続通信が成立すること。
}

TEST(tcpServerPreforkObservationTest, releases_ack_wait_on_pipe_failure_with_worker_alive)
{
    // Arrange
    // Pre-Assert
    // Act
    // [サブ手順参照 名前=tcpServerPreforkObservationTest.ack_fault_observation]
    std::string error = ack_fault_observation(PREFORK_PROBE_PIPE_BREAK);
    // Assert
    EXPECT_TRUE(error.empty())
        << error; // [確認_異常系] - ワーカーを終了せずパイプ障害で ACK 待機を解除し、後続通信が成立すること。
}

TEST(tcpServerPreforkObservationTest, releases_ack_wait_when_socket_restore_fails)
{
    // Arrange
    // Pre-Assert
    // Act
    // [サブ手順参照 名前=tcpServerPreforkObservationTest.ack_fault_observation]
    std::string error = ack_fault_observation(PREFORK_PROBE_RESTORE_FAIL);
    // Assert
    EXPECT_TRUE(error.empty())
        << error; // [確認_異常系] - 復元戻り値の異常注入で ACK 不受理を確認し、後続通信が成立すること。
}

TEST(tcpServerPreforkObservationTest, retains_parent_socket_until_ack_is_released)
{
    // Arrange
    // Pre-Assert
    // Act
    // [サブ手順参照 名前=tcpServerPreforkObservationTest.ack_fault_observation]
    std::string error = ack_fault_observation(PREFORK_PROBE_HOLD_ACK);
    // Assert
    EXPECT_TRUE(error.empty())
        << error; // [確認_正常系] - ACK 保留中は親参照を保持し、解除後に通信と EOF が成立すること。
}

TEST(tcpServerPreforkObservationTest, accepts_ack_signaled_before_wait_begins)
{
    // Arrange
    // Pre-Assert
    // Act
    // [サブ手順参照 名前=tcpServerPreforkObservationTest.early_ack_observation]
    std::string error = early_ack_observation();
    // Assert
    EXPECT_TRUE(error.empty())
        << error; // [確認_正常系] - 待機開始前に到着した ACK を失わず、親参照の解放と EOF が成立すること。
}

TEST(tcpServerPreforkObservationTest, detects_omitted_parent_close_by_missing_eof)
{
    // Arrange
    // Pre-Assert
    // Act
    // [サブ手順参照 名前=tcpServerPreforkObservationTest.leak_sensitivity]
    std::string error = leak_sensitivity();
    // Assert
    EXPECT_TRUE(error.empty())
        << error; // [確認_異常系] - 親の閉鎖省略で EOF が期限切れになり、回収後に EOF を受信すること。
}

TEST(tcpServerPreforkObservationTest, retains_capacity_until_done_is_released)
{
    // Arrange
    // Pre-Assert
    // Act
    // [サブ手順参照 名前=tcpServerPreforkObservationTest.done_sensitivity]
    std::string error = done_sensitivity();
    // Assert
    EXPECT_TRUE(error.empty())
        << error; // [確認_正常系] - DONE 保留中は満杯のままで、解除後に追加接続の通信が完了すること。
}

TEST(tcpServerPreforkObservationTest, releases_capacity_wait_after_worker_loss)
{
    // Arrange
    // Pre-Assert
    // Act
    // [サブ手順参照 名前=tcpServerPreforkObservationTest.capacity_fault_observation]
    std::string error = capacity_fault_observation(false);
    // Assert
    EXPECT_TRUE(error.empty())
        << error; // [確認_異常系] - 空き待機中のワーカー終了後に、生存ワーカーの枠を再利用できること。
}

TEST(tcpServerPreforkObservationTest, rejects_connections_and_continues_after_all_workers_exit)
{
    // Arrange
    // Pre-Assert
    // Act
    // [サブ手順参照 名前=tcpServerPreforkObservationTest.capacity_fault_observation]
    std::string error = capacity_fault_observation(true);
    // Assert
    EXPECT_TRUE(error.empty())
        << error; // [確認_異常系] - 全ワーカー喪失で接続を閉じ、親が次の接続も受理して閉じること。
}
#endif
