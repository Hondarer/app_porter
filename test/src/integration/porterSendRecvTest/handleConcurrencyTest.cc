#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <porter.h>
#include <cplat/crt/string.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <future>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

using namespace testing;

namespace
{
using bytes = std::vector<uint8_t>;

std::mutex s_mutex;
std::condition_variable s_cv;

/* 受信側の記録 */
std::vector<bytes> s_received;
std::set<potr_peer_id> s_connected_peers;
bool s_client_connected = false;

/* コールバック内からの API 呼び出しの記録 */
potr_context *s_server = nullptr;
bool s_reentry_done = false;
int s_reentry_send = POTR_OK;
int s_reentry_disconnect = POTR_OK;
int s_reentry_close = POTR_OK;

constexpr int send_threads = 4;
constexpr int messages_per_thread = 10;
constexpr size_t message_size = 5000U; /* max_payload (1400) を超え、分割して送信される大きさ */

bool wait_for(const std::function<bool()> &condition, int seconds = 10)
{
    std::unique_lock<std::mutex> lock(s_mutex);
    return s_cv.wait_for(lock, std::chrono::seconds(seconds), condition);
}

/* スレッド番号とメッセージ番号から、内容を検証できるメッセージを作る。
 * 先頭 2 バイトにスレッド番号とメッセージ番号を入れ、以降は両者から決まる値で埋める。 */
bytes make_message(int thread_no, int index)
{
    bytes msg(message_size);
    msg[0] = static_cast<uint8_t>(thread_no);
    msg[1] = static_cast<uint8_t>(index);
    for (size_t i = 2; i < msg.size(); ++i)
    {
        msg[i] = static_cast<uint8_t>((thread_no * 31 + index * 7 + static_cast<int>(i / 16)) & 0xFF);
    }
    return msg;
}

void record_callback(int64_t, potr_peer_id peer_id, potr_event event, const void *data, size_t len)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (event == POTR_EVENT_DATA)
    {
        const auto *ptr = static_cast<const uint8_t *>(data);
        s_received.emplace_back(ptr, ptr + len);
    }
    else if (event == POTR_EVENT_CONNECTED)
    {
        s_connected_peers.insert(peer_id);
    }
    else if (event == POTR_EVENT_DISCONNECTED)
    {
        s_connected_peers.erase(peer_id);
    }
    s_cv.notify_all();
}

void client_callback(int64_t, potr_peer_id, potr_event event, const void *, size_t)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (event == POTR_EVENT_CONNECTED)
    {
        s_client_connected = true;
    }
    else if (event == POTR_EVENT_DISCONNECTED)
    {
        s_client_connected = false;
    }
    s_cv.notify_all();
}

// [サブ手順 名前=handleConcurrencyTest.reentry_callback]
void reentry_callback(int64_t, potr_peer_id peer_id, potr_event event, const void *data, size_t len)
{
    if (event == POTR_EVENT_DATA && s_server != nullptr)
    {
        uint8_t reply = 'r';
        int send_ret = potr_service_send(s_server, peer_id, &reply, sizeof(reply), 0);
        // [手順] - DATA のコールバック内から、同一サービスへ potr_service_send() を呼び出す。
        int disconnect_ret = potr_peer_disconnect(s_server, peer_id);
        // [手順] - DATA のコールバック内から、同一サービスへ potr_peer_disconnect() を呼び出す。
        int close_ret = potr_service_close(s_server);
        // [手順] - DATA のコールバック内から、同一サービスへ potr_service_close() を呼び出す。
        std::lock_guard<std::mutex> lock(s_mutex);
        s_reentry_send = send_ret;
        s_reentry_disconnect = disconnect_ret;
        s_reentry_close = close_ret;
        s_reentry_done = true;
        s_cv.notify_all();
        return;
    }
    record_callback(0, peer_id, event, data, len);
}
// [サブ手順終了]

potr_global_config make_global()
{
    potr_global_config global = {};
    global.window_size = 16;
    global.max_payload = 1400;
    global.max_message_size = 65535;
    global.send_queue_depth = 64;
    global.udp_health_interval_ms = 100;
    global.udp_health_timeout_ms = 3000;
    global.tcp_health_interval_ms = 100;
    global.tcp_health_timeout_ms = 3000;
    global.tcp_close_timeout_ms = 3000;
    return global;
}

potr_service_def make_tcp_service(uint16_t port)
{
    potr_service_def service = {};
    service.service_id = 8100;
    service.type = POTR_TYPE_TCP_BIDIR;
    service.dst_port = port;
    cplat_strcpy(service.src_addr[0], POTR_MAX_ADDR_LEN, "127.0.0.1");
    cplat_strcpy(service.dst_addr[0], POTR_MAX_ADDR_LEN, "127.0.0.1");
    return service;
}

potr_service_def make_n1_server(uint16_t port)
{
    potr_service_def service = {};
    service.service_id = 8200;
    service.type = POTR_TYPE_UNICAST_BIDIR_N1;
    service.dst_port = port;
    service.max_peers = 8;
    cplat_strcpy(service.dst_addr[0], POTR_MAX_ADDR_LEN, "127.0.0.1");
    return service;
}

potr_service_def make_n1_client(uint16_t port)
{
    potr_service_def service = {};
    service.service_id = 8200;
    service.type = POTR_TYPE_UNICAST_BIDIR;
    service.dst_port = port;
    cplat_strcpy(service.src_addr[0], POTR_MAX_ADDR_LEN, "127.0.0.1");
    cplat_strcpy(service.dst_addr[0], POTR_MAX_ADDR_LEN, "127.0.0.1");
    return service;
}
} // namespace

class handleConcurrencyTest : public Test
{
  protected:
    potr_context *server = nullptr;
    std::vector<potr_context *> clients;

    // [サブ手順 名前=handleConcurrencyTest.SetUp]
    void SetUp() override
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_received.clear();
        s_connected_peers.clear();
        s_client_connected = false;
        s_server = nullptr;
        s_reentry_done = false;
        s_reentry_send = POTR_OK;
        s_reentry_disconnect = POTR_OK;
        s_reentry_close = POTR_OK;
    }
    // [サブ手順終了]

    // [サブ手順 名前=handleConcurrencyTest.TearDown]
    void TearDown() override
    {
        for (potr_context *client : clients)
        {
            (void)potr_service_close(client);
        }
        clients.clear();
        if (server != nullptr)
        {
            (void)potr_service_close(server);
            server = nullptr;
        }
        s_server = nullptr;
    }
    // [サブ手順終了]

    // [サブ手順 名前=handleConcurrencyTest.open_tcp_pair]
    void open_tcp_pair(uint16_t port)
    {
        potr_global_config global = make_global();
        potr_service_def service = make_tcp_service(port);
        potr_context *client = nullptr;
        ASSERT_EQ(POTR_OK, potr_service_open(&global, &service, POTR_ROLE_RECEIVER, record_callback, &server));
        // [状態確認] - 受信側の potr_service_open() の戻り値が POTR_OK であること。
        ASSERT_EQ(POTR_OK, potr_service_open(&global, &service, POTR_ROLE_SENDER, client_callback, &client));
        // [状態確認] - 送信側の potr_service_open() の戻り値が POTR_OK であること。
        clients.push_back(client);
        ASSERT_TRUE(wait_for([] { return s_client_connected; }));
        // [状態確認] - 送信側が CONNECTED になるまで待つ wait_for() の戻り値が true であること。
    }
    // [サブ手順終了]

    // [サブ手順 名前=handleConcurrencyTest.open_n1]
    void open_n1(uint16_t port, int n_clients, potr_recv_fn server_callback)
    {
        potr_global_config global = make_global();
        potr_service_def server_def = make_n1_server(port);
        potr_service_def client_def = make_n1_client(port);
        ASSERT_EQ(POTR_OK, potr_service_open(&global, &server_def, POTR_ROLE_RECEIVER, server_callback, &server));
        // [状態確認] - N:1 サーバーの potr_service_open() の戻り値が POTR_OK であること。
        s_server = server;
        for (int i = 0; i < n_clients; ++i)
        {
            potr_context *client = nullptr;
            ASSERT_EQ(POTR_OK, potr_service_open(&global, &client_def, POTR_ROLE_SENDER, client_callback, &client));
            // [状態確認] - クライアントの potr_service_open() の戻り値が POTR_OK であること。
            clients.push_back(client);
        }
        ASSERT_TRUE(wait_for([n_clients] { return (int)s_connected_peers.size() == n_clients; }));
        // [状態確認] - サーバーで全クライアントが CONNECTED になるまで待つ wait_for() の戻り値が true であること。
    }
    // [サブ手順終了]
};

// 同一ハンドルの複数スレッドから、分割と圧縮を伴うメッセージを同時に送っても、内容が壊れずに届くことの確認
// [サブ手順参照 名前=handleConcurrencyTest.SetUp]
TEST_F(handleConcurrencyTest, concurrent_fragmented_compressed_sends_arrive_intact)
{
    // Arrange
    // [サブ手順参照 名前=handleConcurrencyTest.open_tcp_pair]
    ASSERT_NO_FATAL_FAILURE(open_tcp_pair(19931));
    potr_context *client = clients[0];
    std::atomic<int> failures(0);

    // Pre-Assert

    // Act
    std::vector<std::thread> threads;
    for (int t = 0; t < send_threads; ++t)
    {
        threads.emplace_back(
            [client, t, &failures]()
            {
                for (int i = 0; i < messages_per_thread; ++i)
                {
                    bytes msg = make_message(t, i);
                    int flags = POTR_SEND_BLOCKING;
                    if ((i % 2) == 1)
                    {
                        flags |= POTR_SEND_COMPRESS;
                    }
                    if (potr_service_send(client, POTR_PEER_NA, msg.data(), msg.size(), flags) != POTR_OK)
                    {
                        failures.fetch_add(1);
                    }
                }
            }); // [手順] - 4 スレッドから同一ハンドルへ、分割されるメッセージを交互に圧縮して 10 件ずつ送信する。
    }
    for (std::thread &th : threads)
    {
        th.join();
    }
    bool all_arrived =
        wait_for([] { return s_received.size() == (size_t)(send_threads * messages_per_thread); });
    // [手順] - 全メッセージの到着を待つ。

    // Assert
    EXPECT_EQ(0, failures.load()); // [確認_正常系] - すべての potr_service_send() が POTR_OK を返すこと。
    EXPECT_TRUE(all_arrived);      // [確認_正常系] - 送信した 40 件がすべて届くこと。
    std::lock_guard<std::mutex> lock(s_mutex);
    int next_index[send_threads] = {};
    int corrupted = 0;
    int out_of_order = 0;
    for (const bytes &msg : s_received)
    {
        if (msg.size() != message_size || msg[0] >= send_threads)
        {
            ++corrupted;
            continue;
        }
        int t = msg[0];
        int i = msg[1];
        if (msg != make_message(t, i))
        {
            ++corrupted;
        }
        if (i != next_index[t])
        {
            ++out_of_order;
        }
        next_index[t] = i + 1;
    }
    EXPECT_EQ(0, corrupted);    // [確認_正常系] - 届いたメッセージに、他のメッセージと混ざったものがないこと。
    EXPECT_EQ(0, out_of_order); // [確認_正常系] - 各スレッドの送信順序が保たれていること。
}
// [サブ手順参照 名前=handleConcurrencyTest.TearDown]

// 非ブロッキング送信を流し続ける間も、別スレッドのブロッキング送信が復帰することの確認
// [サブ手順参照 名前=handleConcurrencyTest.SetUp]
TEST_F(handleConcurrencyTest, blocking_send_returns_while_others_keep_sending)
{
    // Arrange
    // [サブ手順参照 名前=handleConcurrencyTest.open_tcp_pair]
    ASSERT_NO_FATAL_FAILURE(open_tcp_pair(19932));
    potr_context *client = clients[0];
    std::atomic<bool> stop(false);
    std::vector<std::thread> flooders;
    for (int t = 0; t < 3; ++t)
    {
        flooders.emplace_back(
            [client, &stop]()
            {
                uint8_t payload[64] = {};
                while (!stop.load())
                {
                    (void)potr_service_send(client, POTR_PEER_NA, payload, sizeof(payload), 0);
                }
            }); // [状態] - 3 スレッドから非ブロッキング送信を流し続ける。
    }

    // Pre-Assert

    // Act
    auto blocking = std::async(std::launch::async,
                               [client]()
                               {
                                   uint8_t payload[32] = {};
                                   int result = POTR_OK;
                                   for (int i = 0; i < 20 && result == POTR_OK; ++i)
                                   {
                                       result = potr_service_send(client, POTR_PEER_NA, payload, sizeof(payload),
                                                                  POTR_SEND_BLOCKING);
                                   }
                                   return result;
                               }); // [手順] - 別スレッドでブロッキング送信を 20 回行う。
    std::future_status status = blocking.wait_for(std::chrono::seconds(10));
    // [手順] - ブロッキング送信の完了を最大 10 秒待つ。
    stop.store(true);
    for (std::thread &th : flooders)
    {
        th.join();
    }
    int blocking_ret = blocking.get();

    // Assert
    EXPECT_EQ(std::future_status::ready, status);
    // [確認_正常系] - 非ブロッキング送信が続いている間に、ブロッキング送信が 10 秒以内に復帰すること。
    EXPECT_EQ(POTR_OK, blocking_ret); // [確認_正常系] - ブロッキング送信の戻り値が POTR_OK であること。
}
// [サブ手順参照 名前=handleConcurrencyTest.TearDown]

// N:1 で全ピア宛の送信を続ける間にピアの切断を繰り返しても、異常終了しないことの確認
// [サブ手順参照 名前=handleConcurrencyTest.SetUp]
TEST_F(handleConcurrencyTest, n1_disconnect_peers_while_sending)
{
    // Arrange
    // [サブ手順参照 名前=handleConcurrencyTest.open_n1]
    ASSERT_NO_FATAL_FAILURE(open_n1(19933, 4, record_callback));
    std::vector<potr_peer_id> peers;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        peers.assign(s_connected_peers.begin(), s_connected_peers.end());
    }
    potr_context *srv = server;
    std::atomic<bool> stop(false);
    std::vector<std::thread> senders;
    for (int t = 0; t < 2; ++t)
    {
        senders.emplace_back(
            [srv, &stop]()
            {
                bytes payload(3000, 'n');
                while (!stop.load())
                {
                    (void)potr_service_send(srv, POTR_PEER_ALL, payload.data(), payload.size(), 0);
                }
            }); // [状態] - 2 スレッドから全ピア宛の送信を流し続ける。
    }

    // Pre-Assert

    // Act
    std::vector<int> disconnect_rets;
    for (potr_peer_id peer : peers)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        disconnect_rets.push_back(potr_peer_disconnect(srv, peer));
        // [手順] - 送信を続けたまま、ピアを 1 つずつ potr_peer_disconnect() で切断する。
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true);
    for (std::thread &th : senders)
    {
        th.join();
    }
    int close_ret = potr_service_close(srv); // [手順] - サーバーを閉じる。
    server = nullptr;
    s_server = nullptr;

    // Assert
    ASSERT_EQ((size_t)4, disconnect_rets.size()); // [確認_正常系] - 4 ピアを切断したこと。
    for (int ret : disconnect_rets)
    {
        EXPECT_EQ(POTR_OK, ret); // [確認_正常系 回数=4] - potr_peer_disconnect() の戻り値が POTR_OK であること。
    }
    EXPECT_EQ(POTR_OK, close_ret); // [確認_正常系] - 切断と送信の後もサーバーを正常に閉じられること。
}
// [サブ手順参照 名前=handleConcurrencyTest.TearDown]

// N:1 の受信コールバック内から送信、切断、終了を呼ぶと、停止せずに POTR_ERR_UNSUPPORTED を返すことの確認
// [サブ手順参照 名前=handleConcurrencyTest.SetUp]
TEST_F(handleConcurrencyTest, n1_api_calls_from_own_callback_return_unsupported)
{
    // Arrange
    // [サブ手順参照 名前=handleConcurrencyTest.open_n1]
    // [サブ手順参照 名前=handleConcurrencyTest.reentry_callback]
    ASSERT_NO_FATAL_FAILURE(open_n1(19934, 1, reentry_callback));
    potr_context *client = clients[0];
    ASSERT_TRUE(wait_for([] { return s_client_connected; }));
    // [状態確認] - クライアントが CONNECTED になるまで待つ wait_for() の戻り値が true であること。
    uint8_t payload[8] = {'c', 'a', 'l', 'l', 'b', 'a', 'c', 'k'};

    // Pre-Assert

    // Act
    int send_ret = potr_service_send(client, POTR_PEER_NA, payload, sizeof(payload), POTR_SEND_BLOCKING);
    // [手順] - クライアントからサーバーへ 1 件送信し、サーバーのコールバックを発火させる。
    bool done = wait_for([] { return s_reentry_done; });
    // [手順] - コールバック内の呼び出しが終わるまで待つ。

    // Assert
    EXPECT_EQ(POTR_OK, send_ret); // [確認_正常系] - クライアントの送信が成功すること。
    EXPECT_TRUE(done);            // [確認_異常系] - コールバックが停止せずに復帰すること。
    std::lock_guard<std::mutex> lock(s_mutex);
    EXPECT_EQ(POTR_ERR_UNSUPPORTED, s_reentry_send);
    // [確認_異常系] - コールバック内の potr_service_send() の戻り値が POTR_ERR_UNSUPPORTED であること。
    EXPECT_EQ(POTR_ERR_UNSUPPORTED, s_reentry_disconnect);
    // [確認_異常系] - コールバック内の potr_peer_disconnect() の戻り値が POTR_ERR_UNSUPPORTED であること。
    EXPECT_EQ(POTR_ERR_UNSUPPORTED, s_reentry_close);
    // [確認_異常系] - コールバック内の potr_service_close() の戻り値が POTR_ERR_UNSUPPORTED であること。
    // (サーバーは閉じられずに残り、TearDown で正常に閉じる)
}
// [サブ手順参照 名前=handleConcurrencyTest.TearDown]
