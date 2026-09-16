#include <testfw.h>
#include <porter.h>
#include <cplat/net/socket.h>
#include <cplat/net/byteorder.h>
#include <cplat/compress/compress.h>
#include <cplat/crypto/crypto.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <cstring>
#include <algorithm>
#include <functional>

#if defined(PLATFORM_LINUX)
    #include <dlfcn.h>

namespace
{
using bytes = std::vector<uint8_t>;
std::mutex s_mutex;
std::condition_variable s_cv;
bool s_hold_header = false;
bool s_header_held = false;
bool s_release = false;
bool s_hold_callback = false;
bool s_callback_held = false;
bool s_payload_stable = true;
potr_context *s_reply_handle = nullptr;
int s_reply_result = POTR_OK;
unsigned s_paths = 0;
unsigned s_live_paths = 0;
unsigned s_disconnects = 0;
unsigned s_headers = 0;
unsigned s_decompressions = 0;
std::vector<bytes> s_received;
constexpr size_t header_size = offsetof(potr_packet, payload);
uint32_t s_held_seq = 1;

bool wait_for(const std::function<bool()> &condition)
{
    std::unique_lock<std::mutex> lock(s_mutex);
    return s_cv.wait_for(lock, std::chrono::seconds(5), condition);
}

void put(bytes &out, size_t offset, uint64_t value, size_t width)
{
    for (size_t i = 0; i < width; ++i)
    {
        out[offset + width - i - 1] = static_cast<uint8_t>(value & 255U);
        value >>= 8;
    }
}

bytes packet(uint32_t seq, uint16_t flags, const bytes &payload, bool encrypted)
{
    bytes out(header_size, 0);
    put(out, 0, 7123, 8);
    put(out, 8, 1000, 8);
    put(out, 16, 123, 4);
    put(out, 24, seq, 4);
    uint16_t wire_flags = flags;
    size_t wire_size = payload.size();
    if (encrypted)
    {
        wire_flags |= POTR_FLAG_ENCRYPTED;
        wire_size += POTR_CRYPTO_TAG_SIZE;
    }
    put(out, 32, wire_flags, 2);
    put(out, 34, wire_size, 2);
    put(out, 36, POTR_PROTOCOL_VERSION, 4);
    if (encrypted)
    {
        uint8_t nonce[POTR_CRYPTO_NONCE_SIZE] = {};
        uint8_t key[POTR_CRYPTO_KEY_SIZE] = {};
        memcpy(nonce, out.data() + 16, 4);
        memcpy(nonce + 4, out.data() + 32, 2);
        memcpy(nonce + 6, out.data() + 24, 4);
        bytes cipher(payload.size() + POTR_CRYPTO_TAG_SIZE);
        size_t len = cipher.size();
        EXPECT_EQ(CPLAT_OK, cplat_encrypt(cipher.data(), &len, payload.data(), payload.size(), key, nonce, out.data(),
                                          header_size));
        out.insert(out.end(), cipher.begin(), cipher.end());
    }
    else
    {
        out.insert(out.end(), payload.begin(), payload.end());
    }
    return out;
}

bytes data_packet(uint32_t seq, const bytes &payload, bool encrypted, uint16_t flags = 0)
{
    bytes elem(POTR_PAYLOAD_ELEM_HDR_SIZE, 0);
    put(elem, 0, flags, 2);
    put(elem, 2, payload.size(), 4);
    elem.insert(elem.end(), payload.begin(), payload.end());
    return packet(seq, POTR_FLAG_DATA, elem, encrypted);
}

void receive(int64_t, potr_peer_id, potr_event event, const void *data, size_t len)
{
    std::unique_lock<std::mutex> lock(s_mutex);
    if (event == POTR_EVENT_DATA)
    {
        const auto *ptr = static_cast<const uint8_t *>(data);
        bytes before(ptr, ptr + len);
        if (s_reply_handle != nullptr && s_received.size() == 1)
        {
            bytes reply(400, 'R');
            s_reply_result = potr_send(s_reply_handle, POTR_PEER_NA, reply.data(), reply.size(), POTR_SEND_COMPRESS);
            s_payload_stable = before == bytes(ptr, ptr + len);
        }
        if (s_hold_callback && s_received.size() == 1)
        {
            s_callback_held = true;
            s_cv.notify_all();
            s_cv.wait_for(lock, std::chrono::seconds(5), [] { return s_release; });
            s_payload_stable = before == bytes(ptr, ptr + len);
        }
        s_received.push_back(before);
    }
    if (event == POTR_EVENT_CONNECTED || event == POTR_EVENT_PATH_CONNECTED)
    {
        ++s_paths;
    }
    if (event == POTR_EVENT_PATH_CONNECTED || event == POTR_EVENT_PATH_DISCONNECTED)
    {
        const auto *states = static_cast<const int *>(data);
        s_live_paths = static_cast<unsigned>(states[0] != 0) | (static_cast<unsigned>(states[1] != 0) << 1);
    }
    if (event == POTR_EVENT_DISCONNECTED)
        ++s_disconnects;
    s_cv.notify_all();
}
} // namespace

/* 実ソケットの読み取り完了後に停止し、解析前の上書きを再現します。
 * Linux の動的シンボル解決を使用するため、この試験だけ Linux を対象とします。 */
extern "C" int cplat_socket_recv_all(cplat_socket sock, void *buf, size_t len, cplat_error *detail)
{
    using recv_fn = int (*)(cplat_socket, void *, size_t, cplat_error *);
    static auto real_recv = reinterpret_cast<recv_fn>(dlsym(RTLD_NEXT, "cplat_socket_recv_all"));
    int ret = real_recv(sock, buf, len, detail);
    if (ret == CPLAT_OK && len == header_size)
    {
        auto *p = static_cast<uint8_t *>(buf);
        if (p[6] == 0x1b && p[7] == 0xd3 && (p[33] & POTR_FLAG_DATA))
        {
            std::unique_lock<std::mutex> lock(s_mutex);
            ++s_headers;
            if (s_hold_header && p[27] == s_held_seq)
            {
                s_header_held = true;
                s_cv.notify_all();
                s_cv.wait_for(lock, std::chrono::seconds(5), [] { return s_release; });
            }
            s_cv.notify_all();
        }
    }
    return ret;
}

extern "C" int cplat_decompress(uint8_t *dest, size_t *dest_len, const uint8_t *src, size_t src_len)
{
    using decompress_fn = int (*)(uint8_t *, size_t *, const uint8_t *, size_t);
    static auto real_decompress = reinterpret_cast<decompress_fn>(dlsym(RTLD_NEXT, "cplat_decompress"));
    int ret = real_decompress(dest, dest_len, src, src_len);
    std::lock_guard<std::mutex> lock(s_mutex);
    ++s_decompressions;
    s_cv.notify_all();
    return ret;
}

class tcpMultipathTest : public Test
{
  protected:
    potr_context *handle = nullptr;
    cplat_socket sockets[2] = {CPLAT_INVALID_SOCKET, CPLAT_INVALID_SOCKET};

    void SetUp() override
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_hold_header = s_header_held = s_release = s_hold_callback = s_callback_held = false;
        s_payload_stable = true;
        s_reply_handle = nullptr;
        s_reply_result = POTR_OK;
        s_held_seq = 1;
        s_paths = s_headers = 0;
        s_live_paths = s_disconnects = 0;
        s_decompressions = 0;
        s_received.clear();
    }

    void open_receiver(bool crypto = false, bool bidirectional = false, bool seed_first_path = true)
    {
        potr_global_config global = {};
        global.window_size = 16;
        global.max_payload = 512;
        global.max_message_size = 8192;
        global.send_queue_depth = 16;
        potr_service_def service = {};
        service.service_id = 7123;
        service.type = POTR_TYPE_TCP;
        if (bidirectional)
            service.type = POTR_TYPE_TCP_BIDIR;
        service.dst_port = 19723;
        service.encrypt_enabled = crypto;
        strcpy(service.src_addr[0], "127.0.0.1");
        strcpy(service.src_addr[1], "127.0.0.1");
        strcpy(service.dst_addr[0], "127.0.0.1");
        strcpy(service.dst_addr[1], "127.0.0.2");
        ASSERT_EQ(POTR_OK, potr_service_open(&global, &service, POTR_ROLE_RECEIVER, receive, &handle));
        for (int i = 0; i < 2; ++i)
        {
            cplat_ipv4_endpoint endpoint = {};
            ASSERT_EQ(CPLAT_OK, cplat_ipv4_parse(service.dst_addr[i], &endpoint.address));
            endpoint.port = cplat_hton16(service.dst_port);
            ASSERT_EQ(CPLAT_OK, cplat_socket_open(CPLAT_SOCKET_TCP, &sockets[i], nullptr));
            ASSERT_EQ(CPLAT_OK, cplat_socket_connect(sockets[i], &endpoint, nullptr));
            send_packet(i, packet(0, POTR_FLAG_PING, bytes(POTR_MAX_PATH, 1), crypto));
            if (i == 0 && seed_first_path)
            {
                send_packet(0, data_packet(0, bytes{'s'}, crypto));
                ASSERT_TRUE(wait_for([] { return s_received.size() == 1; }));
            }
        }
        // PING に対する応答の受信で、両経路の開始を待ちます。
        int ready = 0;
        ASSERT_EQ(CPLAT_OK, cplat_socket_wait_readable(sockets[1], 5000, &ready, nullptr));
        ASSERT_EQ(1, ready);
        // 応答 PING は accept スレッドの bootstrap 送信でも発生するため、読み取り可能なだけでは
        // 受信スレッドが先読み PING を処理し終えた証拠になりません。PATH_CONNECTED の到達で、
        // 各経路の受信スレッドが次のヘッダー読み取りへ進むことを保証します。
        ASSERT_TRUE(wait_for([] { return s_live_paths == 3; }));
        if (!seed_first_path)
        {
            send_packet(0, data_packet(0, bytes{'s'}, crypto));
            ASSERT_TRUE(wait_for([] { return s_received.size() == 1; }));
        }
    }

    void send_packet(int path, const bytes &wire)
    {
        ASSERT_EQ(CPLAT_OK, cplat_socket_send_all(sockets[path], wire.data(), wire.size(), nullptr));
    }

    void TearDown() override
    {
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_release = true;
            s_cv.notify_all();
        }
        if (handle != nullptr)
        {
            EXPECT_EQ(POTR_OK, potr_service_close(handle));
        }
        for (auto socket : sockets)
        {
            cplat_socket_close(socket);
        }
    }
};

// 一方のヘッダー読み取り停止中に、他方の受信が内容を上書きしないことを確認します。
TEST_F(tcpMultipathTest, interleaved_headers_preserve_packets)
{
    // Arrange
    ASSERT_NO_FATAL_FAILURE(open_receiver());
    bytes first(50, 'A');
    bytes second(90, 'B');
    // Pre-Assert
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_hold_header = true;
    }
    // Act
    send_packet(0, data_packet(1, first, false)); // [手順] - 第 1 経路をヘッダー読み取り直後に停止する。
    ASSERT_TRUE(wait_for([] { return s_header_held; }));
    send_packet(1, data_packet(2, second, false)); // [手順] - 第 2 経路へ異なる長さのパケットを送信する。
    ASSERT_TRUE(wait_for([] { return s_headers >= 3; }));
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_release = true;
        s_cv.notify_all();
    }
    // Assert
    ASSERT_TRUE(wait_for([] { return s_received.size() == 3; })); // [確認_正常系] - 両パケットを配信すること。
    std::lock_guard<std::mutex> lock(s_mutex);
    EXPECT_EQ(first, s_received[1]);  // [確認_正常系] - 第 1 パケットの内容が一致すること。
    EXPECT_EQ(second, s_received[2]); // [確認_正常系] - 第 2 パケットの内容が一致すること。
}

// 復号・展開したフラグメントを複数経路で順序どおりに結合することを確認します。
TEST_F(tcpMultipathTest, encrypted_compressed_fragments)
{
    // Arrange
    ASSERT_NO_FATAL_FAILURE(open_receiver(true));
    bytes expected(4096);
    uint32_t seed = 42;
    for (size_t i = 0; i < 2048; ++i)
    {
        seed = seed * 1664525U + 1013904223U;
        expected[i] = static_cast<uint8_t>(seed >> 24);
    }
    memcpy(expected.data() + 2048, expected.data(), 2048);
    bytes compressed(8192);
    size_t length = compressed.size();
    ASSERT_EQ(CPLAT_OK, cplat_compress(compressed.data(), &length, expected.data(), expected.size()));
    compressed.resize(length);
    // Pre-Assert
    ASSERT_GT(length, 400U);
    ASSERT_LT(length, expected.size());
    // Act
    uint32_t seq = 1;
    for (size_t offset = 0; offset < length; offset += 400)
    {
        size_t end = std::min(length, offset + 400);
        uint16_t flags = POTR_FLAG_COMPRESSED;
        if (end < length)
            flags |= POTR_FLAG_MORE_FRAG;
        send_packet(static_cast<int>(seq % 2), data_packet(seq,
                                                           bytes(compressed.begin() + static_cast<ptrdiff_t>(offset),
                                                                 compressed.begin() + static_cast<ptrdiff_t>(end)),
                                                           true, flags));
        ++seq;
    }
    // Assert
    ASSERT_TRUE(wait_for([] { return s_received.size() == 2; })); // [確認_正常系] - 結合したメッセージを配信すること。
    std::lock_guard<std::mutex> lock(s_mutex);
    EXPECT_EQ(expected, s_received[1]); // [確認_正常系] - 復号・展開した全バイトが一致すること。
}

// 他経路の読み取り待機が、到着済み DATA の処理を停止させないことを確認します。
TEST_F(tcpMultipathTest, blocked_path_does_not_stop_other_path)
{
    // Arrange
    ASSERT_NO_FATAL_FAILURE(open_receiver());
    bytes first(40, 'A');
    bytes second(60, 'B');
    // Pre-Assert
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_hold_header = true;
        s_held_seq = 2;
    }
    // Act
    send_packet(0, data_packet(2, second, false));
    ASSERT_TRUE(wait_for([] { return s_header_held; }));
    send_packet(1, data_packet(1, first, false)); // [手順] - 他経路のヘッダー待機中に先行 DATA を送信する。
    // Assert
    ASSERT_TRUE(
        wait_for([] { return s_received.size() == 2; })); // [確認_正常系] - 待機経路を解除する前に配信できること。
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_release = true;
        s_cv.notify_all();
    }
    ASSERT_TRUE(wait_for([] { return s_received.size() == 3; }));
    std::lock_guard<std::mutex> lock(s_mutex);
    EXPECT_EQ(first, s_received[1]);  // [確認_正常系] - 先行 DATA が一致すること。
    EXPECT_EQ(second, s_received[2]); // [確認_正常系] - 待機を解除した DATA が一致すること。
}

// 圧縮した返信が、コールバック中の展開済みデータを変更しないことを確認します。
TEST_F(tcpMultipathTest, encrypted_compressed_reply_preserves_callback_data)
{
    // Arrange
    ASSERT_NO_FATAL_FAILURE(open_receiver(true, true));
    bytes expected(400, 'Q');
    bytes compressed(1024);
    size_t length = compressed.size();
    ASSERT_EQ(CPLAT_OK, cplat_compress(compressed.data(), &length, expected.data(), expected.size()));
    compressed.resize(length);
    // Pre-Assert
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_reply_handle = handle;
    }
    // Act
    send_packet(0, data_packet(1, compressed, true,
                               POTR_FLAG_COMPRESSED)); // [手順] - 復号・展開後のコールバックから圧縮返信する。
    // Assert
    ASSERT_TRUE(wait_for([] { return s_received.size() == 2; }));
    std::lock_guard<std::mutex> lock(s_mutex);
    EXPECT_EQ(POTR_OK, s_reply_result); // [確認_正常系] - 返信を受け付けること。
    EXPECT_TRUE(s_payload_stable);      // [確認_正常系] - 返信の前後で受信データが変化しないこと。
    EXPECT_EQ(expected, s_received[1]); // [確認_正常系] - 展開した全バイトが一致すること。
}

// 配信中に別経路から受信しても、展開済みデータの寿命と順序を維持することを確認します。
TEST_F(tcpMultipathTest, callback_lifetime_across_paths)
{
    // Arrange
    ASSERT_NO_FATAL_FAILURE(open_receiver(true));
    bytes first(400, 'A');
    bytes second(400, 'B');
    bytes first_compressed(1024);
    bytes second_compressed(1024);
    size_t first_size = first_compressed.size();
    size_t second_size = second_compressed.size();
    ASSERT_EQ(CPLAT_OK, cplat_compress(first_compressed.data(), &first_size, first.data(), first.size()));
    ASSERT_EQ(CPLAT_OK, cplat_compress(second_compressed.data(), &second_size, second.data(), second.size()));
    first_compressed.resize(first_size);
    second_compressed.resize(second_size);
    // Pre-Assert
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_hold_callback = true;
    }
    // Act
    send_packet(0, data_packet(1, first_compressed, true, POTR_FLAG_COMPRESSED));
    ASSERT_TRUE(wait_for([] { return s_callback_held; })); // [手順] - 最初の DATA コールバックを停止する。
    send_packet(1, data_packet(2, second_compressed, true, POTR_FLAG_COMPRESSED));
    ASSERT_TRUE(wait_for([] { return s_headers >= 3; })); // [手順] - 別経路が後続ヘッダーを読み取るまで待機する。
    {
        std::unique_lock<std::mutex> lock(s_mutex);
        // 修正前は後続の展開が先行し、修正後はコールバック復帰まで待機します。
        s_cv.wait_for(lock, std::chrono::milliseconds(250), [] { return s_decompressions >= 2; });
        s_release = true;
        s_cv.notify_all();
    }
    // Assert
    ASSERT_TRUE(wait_for([] { return s_received.size() == 3; }));
    std::lock_guard<std::mutex> lock(s_mutex);
    EXPECT_TRUE(s_payload_stable);    // [確認_正常系] - コールバック中の受信内容を維持すること。
    EXPECT_EQ(first, s_received[1]);  // [確認_正常系] - 先行 DATA が最初に配信されること。
    EXPECT_EQ(second, s_received[2]); // [確認_正常系] - 後続 DATA が次に配信されること。
}

// DATA より先に両経路の暗号化 PING だけで同一セッションを採用できることを確認します。
TEST_F(tcpMultipathTest, bootstrap_both_paths_before_data)
{
    // Arrange
    // Pre-Assert
    // Act
    ASSERT_NO_FATAL_FAILURE(
        open_receiver(true, true, false)); // [手順] - 両経路を PING で確立した後に DATA を送信する。
    // Assert
    std::lock_guard<std::mutex> lock(s_mutex);
    EXPECT_EQ(3U, s_live_paths);  // [確認_正常系] - 両経路の接続を維持すること。
    EXPECT_EQ(0U, s_disconnects); // [確認_正常系] - 同一セッションの経路を切断しないこと。
}

// 部分再接続をまたいで DATA の順序と FIN の完了条件を維持することを確認します。
TEST_F(tcpMultipathTest, reconnect_path_and_finish_in_order)
{
    // Arrange
    ASSERT_NO_FATAL_FAILURE(open_receiver(false, true));
    bytes first(40, 'A');
    bytes second(60, 'B');
    // Pre-Assert
    ASSERT_TRUE(wait_for([] { return s_live_paths == 3; }));
    // Act
    cplat_socket_close(sockets[0]);
    sockets[0] = CPLAT_INVALID_SOCKET;
    ASSERT_TRUE(wait_for([] { return s_live_paths == 2; }));
    send_packet(1, data_packet(1, first, false)); // [手順] - 一方の経路を切断した状態で送受信を継続する。
    ASSERT_TRUE(wait_for([] { return s_received.size() == 2; }));
    cplat_ipv4_endpoint endpoint = {};
    ASSERT_EQ(CPLAT_OK, cplat_ipv4_parse("127.0.0.1", &endpoint.address));
    endpoint.port = cplat_hton16(19723);
    ASSERT_EQ(CPLAT_OK, cplat_socket_open(CPLAT_SOCKET_TCP, &sockets[0], nullptr));
    ASSERT_EQ(CPLAT_OK, cplat_socket_connect(sockets[0], &endpoint, nullptr));
    send_packet(0, packet(2, POTR_FLAG_PING, bytes(POTR_MAX_PATH, 1), false));
    ASSERT_TRUE(wait_for([] { return s_live_paths == 3; }));
    bytes fin = packet(0, POTR_FLAG_FIN | POTR_FLAG_FIN_TARGET_VALID, {}, false);
    put(fin, 28, 3, 4);
    send_packet(1, fin); // [手順] - 最後の DATA より先に FIN を受信させる。
    send_packet(0, data_packet(2, second, false));
    // Assert
    ASSERT_TRUE(wait_for(
        []
        {
            return s_received.size() == 3 && s_disconnects == 1;
        })); // [確認_正常系] - 最終 DATA の配信後に切断すること。
    std::lock_guard<std::mutex> lock(s_mutex);
    EXPECT_EQ(first, s_received[1]);  // [確認_正常系] - 残存経路の DATA が一致すること。
    EXPECT_EQ(second, s_received[2]); // [確認_正常系] - 再接続経路の DATA が一致すること。
}
#endif
