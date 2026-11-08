#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#if defined(PLATFORM_WINDOWS)
    #include <cplat/base/windows_sdk.h>
    #include <cplat/crt/stdlib.h>
    #include <cplat/crt/stdio.h>
    #include <cplat/win32/win32.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <chrono>
    #include <cstdio>
    #include <cstdlib>
    #include <cstring>
    #include <thread>
    #include <vector>
    #pragma comment(lib, "ws2_32.lib")

namespace
{
struct WinsockSession
{
    WSADATA data = {};
    int result = WSAStartup(MAKEWORD(2, 2), &data);
    ~WinsockSession()
    {
        if (result == 0)
            WSACleanup();
    }
};
struct SocketOwner
{
    SOCKET value = INVALID_SOCKET;
    ~SocketOwner()
    {
        if (value != INVALID_SOCKET)
            closesocket(value);
    }
};
struct ClientSockets
{
    std::vector<SOCKET> values;
    ~ClientSockets()
    {
        for (SOCKET value : values)
            closesocket(value);
    }
};
struct ServerProcess
{
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    PROCESS_INFORMATION process = {};
    ~ServerProcess()
    {
        if (job != nullptr)
        {
            TerminateJobObject(job, 0);
            CloseHandle(job);
        }
        if (process.hProcess != nullptr)
        {
            TerminateProcess(process.hProcess, 0);
            WaitForSingleObject(process.hProcess, INFINITE);
            CloseHandle(process.hProcess);
        }
        if (process.hThread != nullptr)
            CloseHandle(process.hThread);
    }
};

// [サブ手順 名前=tcpServerPreforkTest.exercise]
int exercise(const int capacity)
{
    WinsockSession winsock;
    if (winsock.result != 0)
        return -1;
    SocketOwner probe;
    probe.value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (probe.value == INVALID_SOCKET)
        return -1;
    sockaddr_in endpoint = {};
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(probe.value, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint)) != 0)
        return -1;
    int address_size = sizeof(endpoint);
    if (getsockname(probe.value, reinterpret_cast<sockaddr *>(&endpoint), &address_size) != 0)
        return -1;
    closesocket(probe.value);
    probe.value = INVALID_SOCKET;
    char executable_path[PLATFORM_PATH_MAX];
    int exists = 0;
    if (cplat_getenv("POTR_TCPSERVER_TEST_EXE", executable_path, sizeof(executable_path), &exists, nullptr) != CPLAT_OK)
        return -1;
    const char *executable = exists ? executable_path : "../../../../prod/cbin/tcpServer.exe";
    char command[4096];
    if (cplat_snprintf(command, sizeof(command), "\"%s\" --mode prefork --port %u --workers 2 --conns-per-worker %d",
                       executable, static_cast<unsigned>(ntohs(endpoint.sin_port)), capacity) != CPLAT_OK)
        return -1;
    ServerProcess server;
    if (server.job == nullptr)
        return -1;
    STARTUPINFOW info = {};
    info.cb = sizeof(info);
    if (!CreateProcessU(nullptr, command, nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr, &info,
                        &server.process))
        return -1;
    if (!AssignProcessToJobObject(server.job, server.process.hProcess))
        return -1;
    ResumeThread(server.process.hThread);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    SocketOwner ready;
    while (std::chrono::steady_clock::now() < deadline)
    {
        ready.value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (connect(ready.value, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint)) == 0)
            break;
        closesocket(ready.value);
        ready.value = INVALID_SOCKET;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (ready.value == INVALID_SOCKET)
        return -1;
    closesocket(ready.value);
    ready.value = INVALID_SOCKET;
    int completed = 0;
    // 全接続枠を同時に使用し、DONE 後の再割り当ても繰り返す。
    for (int round = 0; round < 8; ++round)
    {
        ClientSockets clients;
        for (int i = 0; i < capacity * 2; ++i)
        {
            SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (client == INVALID_SOCKET)
                return -1;
            clients.values.push_back(client);
            if (connect(client, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint)) != 0)
                return -1;
        }
        for (SOCKET client : clients.values)
        {
            // テストの受信待機だけを制限する。サーバーの受け渡し契約には期限を追加しない。
            const DWORD timeout_ms = 10000;
            if (setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout_ms),
                           sizeof(timeout_ms)) != 0)
                return -1;
            const char message[] = "prefork-test";
            int sent = 0;
            int count = 0;
            char received[sizeof(message)] = {};
            while (sent < static_cast<int>(sizeof(message)))
            {
                int part = send(client, message + sent, static_cast<int>(sizeof(message)) - sent, 0);
                if (part <= 0)
                    return -1;
                sent += part;
            }
            while (count < static_cast<int>(sizeof(received)))
            {
                int part = recv(client, received + count, static_cast<int>(sizeof(received)) - count, 0);
                if (part <= 0)
                    return -1;
                count += part;
            }
            if (std::memcmp(message, received, sizeof(message)) != 0)
                return -1;
            ++completed; // [手順] - 親側参照を閉じた後の往復通信を数える。
        }
    }
    return completed;
}
// [サブ手順終了]
} // namespace
#endif

TEST(tcpServerPreforkTest, transfers_sockets_to_sequential_workers)
{
#if defined(PLATFORM_WINDOWS)
    // Arrange
    // Pre-Assert
    // Act
    // [サブ手順参照 名前=tcpServerPreforkTest.exercise]
    int completed = exercise(1);
    // Assert
    EXPECT_EQ(16, completed); // [確認_正常系] - 2 ワーカーで 8 回の往復通信が完了すること。
#else
    GTEST_SKIP() << "Windows 実機で実行するテストです。";
#endif
}
TEST(tcpServerPreforkTest, transfers_sockets_to_multiplex_workers)
{
#if defined(PLATFORM_WINDOWS)
    // Arrange
    // Pre-Assert
    // Act
    // [サブ手順参照 名前=tcpServerPreforkTest.exercise]
    int completed = exercise(3);
    // Assert
    EXPECT_EQ(48, completed); // [確認_正常系] - 6 接続枠で 8 回の往復通信が完了すること。
#else
    GTEST_SKIP() << "Windows 実機で実行するテストです。";
#endif
}
