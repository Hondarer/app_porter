#include <com_util/base/platform.h>
#include <porter_const.h>
#include <porter_type.h>
#include <porter_test_helper.h>
#include <testfw.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>

#if defined(PLATFORM_LINUX)
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#elif defined(PLATFORM_WINDOWS)
    #include <com_util/base/windows_sdk.h>
    #pragma comment(lib, "ws2_32.lib")
#endif /* PLATFORM_ */

/**
 * テスト用の一時バイナリファイルを作成するヘルパー。
 * デストラクタでファイルを削除する。
 */
class TempBinaryFile
{
  public:
    TempBinaryFile() = default;

    string create(const vector<uint8_t> &content)
    {
#if defined(PLATFORM_LINUX)
        char tmpl[] = "/tmp/porter_test_bin_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd == -1)
        {
            return "";
        }
        ssize_t w = write(fd, content.data(), content.size());
        close(fd);
        if (w < 0 || (size_t)w != content.size())
        {
            return "";
        }
        path_ = tmpl;
#elif defined(PLATFORM_WINDOWS)
        char tmp_dir[PLATFORM_PATH_MAX] = {};
        GetTempPathA(sizeof(tmp_dir), tmp_dir);
        char tmp_file[PLATFORM_PATH_MAX] = {};
        GetTempFileNameA(tmp_dir, "ptb", 0, tmp_file);
        path_ = tmp_file;
        FILE *fp = nullptr;
        fopen_s(&fp, path_.c_str(), "wb");
        if (!fp)
        {
            return "";
        }
        fwrite(content.data(), 1, content.size(), fp);
        fclose(fp);
#endif /* PLATFORM_ */
        return path_;
    }

    ~TempBinaryFile()
    {
        if (!path_.empty())
        {
#if defined(PLATFORM_LINUX)
            unlink(path_.c_str());
#elif defined(PLATFORM_WINDOWS)
            DeleteFileA(path_.c_str());
#endif /* PLATFORM_ */
        }
    }

    TempBinaryFile(const TempBinaryFile &) = delete;
    TempBinaryFile &operator=(const TempBinaryFile &) = delete;

  private:
    string path_;
};

static constexpr size_t kPacketHeaderSize = offsetof(PotrPacket, payload);

static uint64_t hton64_test(uint64_t v)
{
    uint32_t hi = htonl((uint32_t)(v >> 32));
    uint32_t lo = htonl((uint32_t)(v & 0xFFFFFFFFUL));
    return ((uint64_t)lo << 32) | (uint64_t)hi;
}

static void write_u16_be(vector<uint8_t> &buf, size_t offset, uint16_t value)
{
    uint16_t nbo = htons(value);
    memcpy(buf.data() + offset, &nbo, sizeof(nbo));
}

static void write_u32_be(vector<uint8_t> &buf, size_t offset, uint32_t value)
{
    uint32_t nbo = htonl(value);
    memcpy(buf.data() + offset, &nbo, sizeof(nbo));
}

static void write_i64_be(vector<uint8_t> &buf, size_t offset, int64_t value)
{
    uint64_t nbo = hton64_test((uint64_t)value);
    memcpy(buf.data() + offset, &nbo, sizeof(nbo));
}

static vector<uint8_t> make_plain_data_packet(int64_t service_id,
                                              uint32_t session_id,
                                              int64_t session_tv_sec,
                                              int32_t session_tv_nsec,
                                              uint32_t seq_num,
                                              const string &payload)
{
    size_t          packed_len = POTR_PAYLOAD_ELEM_HDR_SIZE + payload.size();
    vector<uint8_t> packet(kPacketHeaderSize + packed_len, 0);

    write_i64_be(packet, 0, service_id);
    write_i64_be(packet, 8, session_tv_sec);
    write_u32_be(packet, 16, session_id);
    write_u32_be(packet, 20, (uint32_t)session_tv_nsec);
    write_u32_be(packet, 24, seq_num);
    write_u32_be(packet, 28, 0U);
    write_u16_be(packet, 32, POTR_FLAG_DATA);
    write_u16_be(packet, 34, (uint16_t)packed_len);
    write_u16_be(packet, kPacketHeaderSize, 0U);
    write_u32_be(packet, kPacketHeaderSize + 2U, (uint32_t)payload.size());
    memcpy(packet.data() + kPacketHeaderSize + POTR_PAYLOAD_ELEM_HDR_SIZE,
           payload.data(), payload.size());
    return packet;
}

static vector<uint8_t> make_plain_ping_packet(int64_t service_id,
                                              uint32_t session_id,
                                              int64_t session_tv_sec,
                                              int32_t session_tv_nsec,
                                              uint32_t seq_num,
                                              uint8_t  ping_state)
{
    vector<uint8_t> packet(kPacketHeaderSize + POTR_MAX_PATH, ping_state);

    write_i64_be(packet, 0, service_id);
    write_i64_be(packet, 8, session_tv_sec);
    write_u32_be(packet, 16, session_id);
    write_u32_be(packet, 20, (uint32_t)session_tv_nsec);
    write_u32_be(packet, 24, seq_num);
    write_u32_be(packet, 28, 0U);
    write_u16_be(packet, 32, POTR_FLAG_PING);
    write_u16_be(packet, 34, POTR_MAX_PATH);
    return packet;
}

static vector<uint8_t> make_plain_fin_packet(int64_t service_id,
                                             uint32_t session_id,
                                             int64_t session_tv_sec,
                                             int32_t session_tv_nsec,
                                             uint32_t ack_num,
                                             bool     fin_target_valid)
{
    vector<uint8_t> packet(kPacketHeaderSize, 0);
    uint16_t        flags = POTR_FLAG_FIN;

    if (fin_target_valid)
    {
        flags = (uint16_t)(flags | POTR_FLAG_FIN_TARGET_VALID);
    }

    write_i64_be(packet, 0, service_id);
    write_i64_be(packet, 8, session_tv_sec);
    write_u32_be(packet, 16, session_id);
    write_u32_be(packet, 20, (uint32_t)session_tv_nsec);
    write_u32_be(packet, 24, 0U);
    write_u32_be(packet, 28, ack_num);
    write_u16_be(packet, 32, flags);
    write_u16_be(packet, 34, 0U);
    return packet;
}

static vector<uint8_t> make_invalid_encrypted_ping_packet(int64_t service_id,
                                                          uint32_t session_id,
                                                          int64_t session_tv_sec,
                                                          int32_t session_tv_nsec,
                                                          uint32_t seq_num)
{
    vector<uint8_t> packet(kPacketHeaderSize + POTR_CRYPTO_TAG_SIZE, 0xA5);

    write_i64_be(packet, 0, service_id);
    write_i64_be(packet, 8, session_tv_sec);
    write_u32_be(packet, 16, session_id);
    write_u32_be(packet, 20, (uint32_t)session_tv_nsec);
    write_u32_be(packet, 24, seq_num);
    write_u32_be(packet, 28, 0U);
    write_u16_be(packet, 32, (uint16_t)(POTR_FLAG_PING | POTR_FLAG_ENCRYPTED));
    write_u16_be(packet, 34, POTR_CRYPTO_TAG_SIZE);
    return packet;
}

static int send_udp_packet(const vector<uint8_t> &packet, int port)
{
#if defined(PLATFORM_LINUX)
    int                sockfd;
    struct sockaddr_in addr;
    ssize_t            sent;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    sent = sendto(sockfd, packet.data(), packet.size(), 0,
                  (const struct sockaddr *)&addr, sizeof(addr));
    close(sockfd);
    return (sent == (ssize_t)packet.size()) ? 0 : -1;
#elif defined(PLATFORM_WINDOWS)
    WSADATA            wsa;
    SOCKET             sockfd;
    struct sockaddr_in addr;
    int                sent;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        return -1;
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd == INVALID_SOCKET)
    {
        WSACleanup();
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    InetPtonA(AF_INET, "127.0.0.1", &addr.sin_addr);

    sent = sendto(sockfd, (const char *)packet.data(), (int)packet.size(), 0,
                  (const struct sockaddr *)&addr, sizeof(addr));
    closesocket(sockfd);
    WSACleanup();
    return (sent == (int)packet.size()) ? 0 : -1;
#endif /* PLATFORM_ */
}

static void sleep_ms(unsigned int ms)
{
#if defined(PLATFORM_LINUX)
    usleep(ms * 1000U);
#elif defined(PLATFORM_WINDOWS)
    Sleep(ms);
#endif /* PLATFORM_ */
}

static size_t count_occurrences(const string &text, const string &needle)
{
    size_t count = 0;
    size_t pos   = 0;

    while ((pos = text.find(needle, pos)) != string::npos)
    {
        count++;
        pos += needle.size();
    }
    return count;
}

class porterSendRecvTest : public Test
{
  protected:
    string recv_path, send_path, lib_path;

    // TearDown でのクリーンアップ用。テスト失敗時もプロセスリークを防ぐ。
    AsyncProcessHandle recv_h_, send_h_;

    void SetUp() override
    {
        string ws = findWorkspaceRoot();
        ASSERT_FALSE(ws.empty());
#if defined(PLATFORM_LINUX)
        recv_path = ws + "/app/porter/prod/bin/recv";
        send_path = ws + "/app/porter/prod/bin/send";
        lib_path = ws + "/app/porter/prod/lib"
                 + ":" + ws + "/app/com_util/prod/lib";
#elif defined(PLATFORM_WINDOWS)
        recv_path = ws + "\\app\\porter\\prod\\bin\\recv.exe";
        send_path = ws + "\\app\\porter\\prod\\bin\\send.exe";
        lib_path = ws + "\\app\\porter\\prod\\lib"
                 + ";" + ws + "\\app\\com_util\\prod\\lib";
#endif /* PLATFORM_ */
        resetTraceLevel();
        setTraceLevel("processController", TRACE_DETAIL);
    }

    void TearDown() override
    {
        // 以降の killProcess, waitForExit 呼び出しでトレースが出力されることを防ぐため、
        // processController のトレースを停止する
        setTraceLevel("processController", TRACE_NONE);

        // ASSERT マクロ等でテストが中断された場合でも確実に終了させる。
        if (send_h_)
        {
            killProcess(send_h_);
            waitForExit(send_h_, 1000);
        }
        if (recv_h_)
        {
            killProcess(recv_h_);
            waitForExit(recv_h_, 1000);
        }
    }

    ProcessOptions makeOpts()
    {
        ProcessOptions opts;
#if defined(PLATFORM_LINUX)
        opts.env_set["LD_LIBRARY_PATH"] = lib_path;
#elif defined(PLATFORM_WINDOWS)
        char cur[32768] = {0};
        GetEnvironmentVariableA("PATH", cur, sizeof(cur));
        opts.env_set["PATH"] = lib_path + ";" + string(cur);
#endif /* PLATFORM_ */
        return opts;
    }
};

// 単一メッセージ送受信テスト
TEST_F(porterSendRecvTest, send_single_message)
{
    // Arrange
    // 設定ファイルを動的生成 (ポート 19010 を使用)
    PorterConfigBuilder cfg;
    string config_path =
        cfg.addUnicastService(10, 19010)
            .build(); // [状態] - 127.0.0.1 で ポート 19010 を送受信に利用する unicast サービスを定義する。

    // RECIEVER を先に起動してリスナー確立を待つ
    recv_h_ = startProcessAsync(recv_path, {config_path, "10"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_);                                             // [確認] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。
    // [確認] - RECIEVER が "受信待機中" を出力すること。

    // SENDER を起動して最初のプロンプトを待つ
    send_h_ = startProcessAsync(send_path, {config_path, "10"}, makeOpts()); // [手順] - SENDER を起動する。
    ASSERT_NE(nullptr, send_h_);                                             // [確認] - SENDER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(send_h_, "送信方法を選択してください", 5000)); // [手順] - SENDER が送信方法選択プロンプトを出力するまで待機する。
    // [確認] - SENDER が "送信方法を選択してください" を出力すること。

    // Act
    // テキスト送信を選択する
    ASSERT_TRUE(writeLineStdin(send_h_, "T")); // [手順] - SENDER に "T" (テキスト) を入力する。
    ASSERT_NO_THROW(
        waitForOutput(send_h_, "メッセージ>", 3000)); // [手順] - SENDER が "メッセージ>" を出力するまで待機する。
    // [確認] - SENDER が "メッセージ>" を出力すること。

    // 対話入力を順次送り込む
    ASSERT_TRUE(writeLineStdin(send_h_, "Hello Porter")); // [手順] - SENDER に "Hello Porter" を入力する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "圧縮送信しますか",
                                  3000)); // [手順] - SENDER が "圧縮送信しますか" を出力するまで待機する。
    // [確認] - SENDER が "圧縮送信しますか" を出力すること。

    ASSERT_TRUE(writeLineStdin(send_h_, "N")); // [手順] - SENDER に "N" を入力する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "続けて送信しますか",
                                  3000)); // [手順] - SENDER が "続けて送信しますか" を出力するまで待機する。
    // [確認] - SENDER が "続けて送信しますか" を出力すること。

    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "Hello Porter", 3000)); // [手順] - RECIEVER が "Hello Porter" を出力するまで待機する。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信 (12 バイト)", 3000)); // [手順] - RECIEVER が受信バイト数を出力するまで待機する。

    writeLineStdin(send_h_, "N"); // [手順] - SENDER に "N" を入力する。

    int send_exit = waitForExit(send_h_, 5000); // [手順] - SENDER が終了するまで待機する。

    // RECIEVER を停止して出力を回収する
    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    EXPECT_EQ(0, send_exit); // [確認] - SENDER の終了コードが 0 であること。
    EXPECT_NE(string::npos,
              getStdout(recv_h_).find("Hello Porter")); // [確認] - RECIEVER が "Hello Porter" を受信していること。
    EXPECT_NE(
        string::npos,
        getStdout(recv_h_).find(
            "受信 (12 バイト)")); // [確認] - RECIEVER の受信バイト数が 12 バイト ("Hello Porter" の文字数) であること。
}

// 複数メッセージ連続送信テスト
TEST_F(porterSendRecvTest, send_multiple_messages)
{
    // Arrange
    PorterConfigBuilder cfg;
    string config_path =
        cfg.addUnicastService(10, 19011)
            .build(); // [状態] - 127.0.0.1 で ポート 19011 を送受信に利用する unicast サービスを定義する。

    // RECIEVER を先に起動してリスナー確立を待つ
    recv_h_ = startProcessAsync(recv_path, {config_path, "10"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_);                                             // [確認] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。
    // [確認] - RECIEVER が "受信待機中" を出力すること。

    // SENDER を起動して最初のプロンプトを待つ
    send_h_ = startProcessAsync(send_path, {config_path, "10"}, makeOpts()); // [手順] - SENDER を起動する。
    ASSERT_NE(nullptr, send_h_);                                             // [確認] - SENDER が起動すること。

    // Act
    // msg1 / msg2 / msg3 を順次送り込む
    const vector<string> messages = {"msg1", "msg2", "msg3"};
    // [手順] - 以下を 3 回繰り返す。
    // [確認] - 以下を 3 回繰り返す。
    for (size_t i = 0; i < messages.size(); i++)
    {
        ASSERT_NO_THROW(
            waitForOutput(send_h_, "送信方法を選択してください", 3000)); // [手順]   - SENDER が送信方法選択プロンプトを出力するまで待機する。
        // [確認]   - SENDER が "送信方法を選択してください" を出力すること。

        ASSERT_TRUE(writeLineStdin(send_h_, "T")); // [手順]   - SENDER に "T" (テキスト) を入力する。
        ASSERT_NO_THROW(
            waitForOutput(send_h_, "メッセージ>", 3000)); // [手順]   - SENDER が "メッセージ>" を出力するまで待機する。
        // [確認]   - SENDER が "メッセージ>" を出力すること。

        ASSERT_TRUE(writeLineStdin(send_h_, messages[i])); // [手順]   - SENDER に送信メッセージ "msg{1~3}" を入力する。
        ASSERT_NO_THROW(waitForOutput(send_h_, "圧縮送信しますか",
                                      3000)); // [手順]   - SENDER が "圧縮送信しますか" を出力するまで待機する。
        // [確認]   - SENDER が "圧縮送信しますか" を出力すること。

        ASSERT_TRUE(writeLineStdin(send_h_, "N")); // [手順]   - SENDER に "N" を入力する。
        ASSERT_NO_THROW(waitForOutput(send_h_, "続けて送信しますか",
                                      3000)); // [手順]   - SENDER が "続けて送信しますか" を出力するまで待機する。
        // [確認]   - SENDER が "続けて送信しますか" を出力すること。

        bool last = (i == messages.size() - 1);
        ASSERT_TRUE(writeLineStdin(
            send_h_, last ? "N" : "Y")); // [手順]   - SENDER に次の送信可否を入力する (1~2 は Y, 3 は N)。
    }

    int send_exit = waitForExit(send_h_, 5000); // [手順] - SENDER が終了するまで待機する。

    // RECIEVER を停止して出力を回収する
    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    string recv_out = getStdout(recv_h_);
    EXPECT_EQ(0, send_exit);                        // [確認] - SENDER の終了コードが 0 であること。
    EXPECT_NE(string::npos, recv_out.find("msg1")); // [確認] - RECIEVER が "msg1" を受信していること。
    EXPECT_NE(string::npos, recv_out.find("msg2")); // [確認] - RECIEVER が "msg2" を受信していること。
    EXPECT_NE(string::npos, recv_out.find("msg3")); // [確認] - RECIEVER が "msg3" を受信していること。
}

// RECIEVER の正常終了テスト
TEST_F(porterSendRecvTest, recv_exits_cleanly_on_sigint)
{
    // Arrange
    PorterConfigBuilder cfg;
    string config_path =
        cfg.addUnicastService(10, 19012)
            .build(); // [状態] - 127.0.0.1 で ポート 19012 を送受信に利用する unicast サービスを定義する。

    // RECIEVER を先に起動してリスナー確立を待つ
    recv_h_ = startProcessAsync(recv_path, {config_path, "10"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_);                                             // [確認] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。
    // [確認] - RECIEVER が "受信待機中" を出力すること。

    // Act
    interruptProcess(recv_h_);                  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    int recv_exit = waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    EXPECT_EQ(0, recv_exit); // [確認] - RECIEVER の終了コードが 0 であること。
    EXPECT_NE(string::npos,
              getStdout(recv_h_).find("終了しました")); // [確認] - RECIEVER が "終了しました" を出力していること。
}

// 片方向 unicast で PING 無効でも初回 DATA 受信で CONNECTED と DATA 配信が成立することを確認する
TEST_F(porterSendRecvTest, unicast_initial_data_establishes_connected_without_ping)
{
    PorterConfigBuilder cfg;
    string config_path =
        cfg.setUdpHealthIntervalMs(0)
            .setUdpHealthTimeoutMs(1200)
            .addUnicastService(11, 19016)
            .build();

    recv_h_ = startProcessAsync(recv_path, {config_path, "11"}, makeOpts());
    ASSERT_NE(nullptr, recv_h_);
    ASSERT_NO_THROW(waitForOutput(recv_h_, "受信待機中", 5000));

    ASSERT_EQ(0, send_udp_packet(
                     make_plain_data_packet(11, 0x4101U, 1200, 3400, 0U, "data-connect-ok"),
                     19016));

    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 3000));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "data-connect-ok", 3000));

    interruptProcess(recv_h_);
    waitForExit(recv_h_, 3000);

    {
        string recv_out = getStdout(recv_h_);
        EXPECT_NE(string::npos, recv_out.find("接続確立"));
        EXPECT_NE(string::npos, recv_out.find("data-connect-ok"));
    }
}

// 片方向 unicast 送信者 open 直後は immediate PING を送らず、receiver が即 CONNECTED しないことを確認する
TEST_F(porterSendRecvTest, unicast_sender_open_does_not_trigger_immediate_ping)
{
    PorterConfigBuilder cfg;
    string config_path =
        cfg.setUdpHealthIntervalMs(1000)
            .setUdpHealthTimeoutMs(1500)
            .addUnicastService(13, 19018)
            .build();

    recv_h_ = startProcessAsync(recv_path, {config_path, "13"}, makeOpts());
    ASSERT_NE(nullptr, recv_h_);
    ASSERT_NO_THROW(waitForOutput(recv_h_, "受信待機中", 5000));

    send_h_ = startProcessAsync(send_path, {config_path, "13"}, makeOpts());
    ASSERT_NE(nullptr, send_h_);
    ASSERT_NO_THROW(waitForOutput(send_h_, "送信方法を選択してください", 5000));

    sleep_ms(250);
    EXPECT_EQ(string::npos, getStdout(recv_h_).find("接続確立"));

    interruptProcess(send_h_);
    EXPECT_EQ(0, waitForExit(send_h_, 5000));

    interruptProcess(recv_h_);
    waitForExit(recv_h_, 3000);

    EXPECT_EQ(string::npos, getStdout(recv_h_).find("接続確立"));
}

// 片方向 unicast で PING 無効時も有効 DATA の継続受信で health timeout が延長されることを確認する
TEST_F(porterSendRecvTest, unicast_data_resets_health_timeout_without_ping)
{
    PorterConfigBuilder cfg;
    string config_path =
        cfg.setUdpHealthIntervalMs(0)
            .setUdpHealthTimeoutMs(900)
            .addUnicastService(12, 19017)
            .build();

    recv_h_ = startProcessAsync(recv_path, {config_path, "12"}, makeOpts());
    ASSERT_NE(nullptr, recv_h_);
    ASSERT_NO_THROW(waitForOutput(recv_h_, "受信待機中", 5000));

    ASSERT_EQ(0, send_udp_packet(
                     make_plain_data_packet(12, 0x4102U, 2200, 4500, 0U, "timeout-reset-1"),
                     19017));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "timeout-reset-1", 3000));

    sleep_ms(450);
    EXPECT_EQ(string::npos, getStdout(recv_h_).find("切断検知"));

    ASSERT_EQ(0, send_udp_packet(
                     make_plain_data_packet(12, 0x4102U, 2200, 4500, 1U, "timeout-reset-2"),
                     19017));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "timeout-reset-2", 3000));

    sleep_ms(550);
    EXPECT_EQ(string::npos, getStdout(recv_h_).find("切断検知"));

    ASSERT_NO_THROW(waitForOutput(recv_h_, "切断検知", 2000));

    interruptProcess(recv_h_);
    waitForExit(recv_h_, 3000);

    {
        string recv_out = getStdout(recv_h_);
        EXPECT_NE(string::npos, recv_out.find("接続確立"));
        EXPECT_NE(string::npos, recv_out.find("timeout-reset-1"));
        EXPECT_NE(string::npos, recv_out.find("timeout-reset-2"));
        EXPECT_NE(string::npos, recv_out.find("切断検知"));
    }
}

// 単発送信の直後に close しても、最終 DATA が切断前に配信されることを確認する
TEST_F(porterSendRecvTest, unicast_close_after_single_send_delivers_before_disconnect)
{
    const string payload = "fin-pending-ok";
    PorterConfigBuilder cfg;
    string config_path = cfg.addUnicastService(53, 19053).build();

    recv_h_ = startProcessAsync(recv_path, {config_path, "53"}, makeOpts());
    ASSERT_NE(nullptr, recv_h_);
    ASSERT_NO_THROW(waitForOutput(recv_h_, "受信待機中", 5000));

    send_h_ = startProcessAsync(send_path, {config_path, "53"}, makeOpts());
    ASSERT_NE(nullptr, send_h_);
    ASSERT_NO_THROW(waitForOutput(send_h_, "送信方法を選択してください", 5000));

    ASSERT_TRUE(writeLineStdin(send_h_, "T"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "メッセージ>", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, payload));
    ASSERT_NO_THROW(waitForOutput(send_h_, "圧縮送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "続けて送信しますか", 3000));

    /* sender 側は追加送信せず即 close する。 */
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));
    EXPECT_EQ(0, waitForExit(send_h_, 5000));

    ASSERT_NO_THROW(waitForOutput(recv_h_, payload, 3000));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "切断検知", 3000));

    interruptProcess(recv_h_);
    waitForExit(recv_h_, 3000);

    {
        string recv_out = getStdout(recv_h_);
        size_t data_pos = recv_out.find(payload);
        size_t disc_pos = recv_out.find("切断検知");
        EXPECT_NE(string::npos, data_pos);
        EXPECT_NE(string::npos, disc_pos);
        EXPECT_LT(data_pos, disc_pos);
    }
}

// no-data FIN は FIN target フラグなしで即時切断されることを確認する
TEST_F(porterSendRecvTest, fin_without_target_flag_disconnects_immediately)
{
    PorterConfigBuilder cfg;
    string config_path = cfg.addUnicastService(56, 19056).build();

    recv_h_ = startProcessAsync(recv_path, {config_path, "56"}, makeOpts());
    ASSERT_NE(nullptr, recv_h_);
    ASSERT_NO_THROW(waitForOutput(recv_h_, "受信待機中", 5000));

    ASSERT_EQ(0, send_udp_packet(
                     make_plain_ping_packet(56, 0x5601U, 4234, 8678, 0U,
                                            POTR_PING_STATE_UNDEFINED),
                     19056));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 3000));

    ASSERT_EQ(0, send_udp_packet(
                     make_plain_fin_packet(56, 0x5601U, 4234, 8678, 0U, false),
                     19056));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "切断検知", 3000));

    interruptProcess(recv_h_);
    waitForExit(recv_h_, 3000);

    {
        string recv_out = getStdout(recv_h_);
        EXPECT_EQ((size_t)1, count_occurrences(recv_out, "接続確立"));
        EXPECT_EQ((size_t)1, count_occurrences(recv_out, "切断検知"));
    }
}

// FIN target が 0 に wrap する場合でも、flag により pending FIN が正しく解消されることを確認する
TEST_F(porterSendRecvTest, fin_target_zero_wrap_is_handled_by_flag)
{
    PorterConfigBuilder cfg;
    const string payload = "wrap-fin-target-zero";
    string config_path = cfg.addUnicastService(57, 19057).build();

    recv_h_ = startProcessAsync(recv_path, {config_path, "57"}, makeOpts());
    ASSERT_NE(nullptr, recv_h_);
    ASSERT_NO_THROW(waitForOutput(recv_h_, "受信待機中", 5000));

    ASSERT_EQ(0, send_udp_packet(
                     make_plain_ping_packet(57, 0x5701U, 5234, 9678, UINT32_MAX,
                                            POTR_PING_STATE_UNDEFINED),
                     19057));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 3000));

    ASSERT_EQ(0, send_udp_packet(
                     make_plain_fin_packet(57, 0x5701U, 5234, 9678, 0U, true),
                     19057));
    sleep_ms(150);
    EXPECT_EQ(string::npos, getStdout(recv_h_).find("切断検知"));

    ASSERT_EQ(0, send_udp_packet(
                     make_plain_data_packet(57, 0x5701U, 5234, 9678, UINT32_MAX,
                                            payload),
                     19057));
    ASSERT_NO_THROW(waitForOutput(recv_h_, payload, 3000));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "切断検知", 3000));

    interruptProcess(recv_h_);
    waitForExit(recv_h_, 3000);

    {
        string recv_out = getStdout(recv_h_);
        size_t data_pos = recv_out.find(payload);
        size_t disc_pos = recv_out.find("切断検知");
        EXPECT_NE(string::npos, data_pos);
        EXPECT_NE(string::npos, disc_pos);
        EXPECT_LT(data_pos, disc_pos);
    }
}

// N:1 で単発送信の直後に close しても、最終 DATA が peer 解放前に配信されることを確認する
TEST_F(porterSendRecvTest, n1_close_after_single_send_delivers_before_disconnect)
{
    const string payload = "n1-fin-pending-ok";
    PorterConfigBuilder server_cfg;
    PorterConfigBuilder client_cfg;
    string server_config_path =
        server_cfg.addUnicastBidirN1Service(54, 19054, 1, "0.0.0.0").build();
    string client_config_path =
        client_cfg.addUnicastBidirService(54, 19054).build();

    recv_h_ = startProcessAsync(recv_path, {server_config_path, "54"}, makeOpts());
    ASSERT_NE(nullptr, recv_h_);
    ASSERT_NO_THROW(waitForOutput(recv_h_, "受信待機中", 5000));

    send_h_ = startProcessAsync(send_path, {client_config_path, "54"}, makeOpts());
    ASSERT_NE(nullptr, send_h_);
    ASSERT_NO_THROW(waitForOutput(send_h_, "双方向モード", 5000));
    ASSERT_NO_THROW(waitForOutput(send_h_, "送信方法を選択してください", 3000));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 3000));
    ASSERT_NO_THROW(waitForOutput(send_h_, "接続確立", 3000));

    ASSERT_TRUE(writeLineStdin(send_h_, "T"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "メッセージ>", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, payload));
    ASSERT_NO_THROW(waitForOutput(send_h_, "圧縮送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "続けて送信しますか", 3000));

    ASSERT_TRUE(writeLineStdin(send_h_, "N"));
    EXPECT_EQ(0, waitForExit(send_h_, 5000));
    ASSERT_NO_THROW(waitForOutput(recv_h_, payload, 3000));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "切断検知", 3000));

    interruptProcess(recv_h_);
    waitForExit(recv_h_, 3000);

    {
        string recv_out = getStdout(recv_h_);
        size_t data_pos = recv_out.find(payload);
        size_t disc_pos = recv_out.find("切断検知");
        EXPECT_NE(string::npos, data_pos);
        EXPECT_NE(string::npos, disc_pos);
        EXPECT_LT(data_pos, disc_pos);
    }
}

// pending FIN のまま health timeout した後、新セッション受理で stale 状態が再発しないことを確認する
TEST_F(porterSendRecvTest, health_timeout_clears_pending_fin_before_new_session)
{
    PorterConfigBuilder cfg;
    const string payload = "after-timeout-ok";
    const string first_payload = "before-timeout-pending";
    string config_path = cfg.setUdpHealthTimeoutMs(300)
                            .addUnicastService(55, 19055)
                            .build();

    recv_h_ = startProcessAsync(recv_path, {config_path, "55"}, makeOpts());
    ASSERT_NE(nullptr, recv_h_);
    ASSERT_NO_THROW(waitForOutput(recv_h_, "受信待機中", 5000));

    ASSERT_EQ(0, send_udp_packet(
                     make_plain_data_packet(55, 0x5501U, 3234, 7678, 0U,
                                            first_payload),
                     19055));
    ASSERT_NO_THROW(waitForOutput(recv_h_, first_payload, 3000));

    ASSERT_EQ(0, send_udp_packet(
                     make_plain_fin_packet(55, 0x5501U, 3234, 7678, 2U, true),
                     19055));
    sleep_ms(150);
    EXPECT_EQ(string::npos, getStdout(recv_h_).find("切断検知"));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "切断検知", 2000));

    ASSERT_EQ(0, send_udp_packet(
                     make_plain_data_packet(55, 0x5502U, 3235, 7679, 0U, payload),
                     19055));
    ASSERT_NO_THROW(waitForOutput(recv_h_, payload, 3000));
    sleep_ms(150);

    interruptProcess(recv_h_);
    waitForExit(recv_h_, 3000);

    {
        string recv_out = getStdout(recv_h_);
        EXPECT_NE(string::npos, recv_out.find(first_payload));
        EXPECT_NE(string::npos, recv_out.find(payload));
        EXPECT_EQ((size_t)1, count_occurrences(recv_out, "切断検知"));
    }
}

// 片方向 unicast で recent DATA により periodic PING が抑止され、最後の DATA 基準で再開することを確認する
TEST_F(porterSendRecvTest, unicast_recent_data_defers_ping_until_last_data_interval)
{
    PorterConfigBuilder cfg;
    string config_path =
        cfg.setUdpHealthIntervalMs(500)
            .setUdpHealthTimeoutMs(1500)
            .addUnicastService(14, 19019)
            .build();

    recv_h_ = startProcessAsync(recv_path, {config_path, "14"}, makeOpts());
    ASSERT_NE(nullptr, recv_h_);
    ASSERT_NO_THROW(waitForOutput(recv_h_, "受信待機中", 5000));

    send_h_ = startProcessAsync(send_path, {"-l", "VERBOSE", config_path, "14"}, makeOpts());
    ASSERT_NE(nullptr, send_h_);
    ASSERT_NO_THROW(waitForOutput(send_h_, "送信方法を選択してください", 5000));

    ASSERT_TRUE(writeLineStdin(send_h_, "T"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "メッセージ>", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "ping-delay-1"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "圧縮送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "続けて送信しますか", 3000));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "ping-delay-1", 3000));

    sleep_ms(250);
    ASSERT_TRUE(writeLineStdin(send_h_, "Y"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "送信方法を選択してください", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "T"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "メッセージ>", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "ping-delay-2"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "圧縮送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "続けて送信しますか", 3000));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "ping-delay-2", 3000));

    sleep_ms(250);
    {
        string send_err = getStderr(send_h_);
        EXPECT_NE(string::npos, send_err.find("suppress PING due to recent DATA"));
        EXPECT_EQ(string::npos, send_err.find("health[service_id=14]: PING seq="));
    }

    sleep_ms(400);
    EXPECT_NE(string::npos,
              getStderr(send_h_).find("health[service_id=14]: PING seq="));

    ASSERT_TRUE(writeLineStdin(send_h_, "N"));
    EXPECT_EQ(0, waitForExit(send_h_, 5000));

    interruptProcess(recv_h_);
    waitForExit(recv_h_, 3000);

    {
        string recv_out = getStdout(recv_h_);
        EXPECT_NE(string::npos, recv_out.find("接続確立"));
        EXPECT_NE(string::npos, recv_out.find("ping-delay-1"));
        EXPECT_NE(string::npos, recv_out.find("ping-delay-2"));
    }
}

// unicast_bidir 双方向通信テスト
TEST_F(porterSendRecvTest, bidir_echo)
{
    // Arrange
    PorterConfigBuilder cfg;
    string config_path =
        cfg.addUnicastBidirService(20, 19020)
            .build(); // [状態] - 127.0.0.1 で ポート 19020 を送受信に利用する unicast_bidir サービスを定義する。

    // RECIEVER を先に起動してリスナー確立を待つ
    recv_h_ = startProcessAsync(recv_path, {config_path, "20"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_);                                             // [確認] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。
    // [確認] - RECIEVER が "受信待機中" を出力すること。

    // SENDER を起動して最初のプロンプトを待つ
    send_h_ = startProcessAsync(send_path, {config_path, "20"}, makeOpts()); // [手順] - SENDER を起動する。
    ASSERT_NE(nullptr, send_h_);                                             // [確認] - SENDER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(send_h_, "双方向モード", 5000)); // [手順] - SENDER が "双方向モード" を出力するまで待機する。
    // [確認] - SENDER が "双方向モード" を出力すること。
    ASSERT_NO_THROW(
        waitForOutput(send_h_, "送信方法を選択してください", 3000)); // [手順] - SENDER が送信方法選択プロンプトを出力するまで待機する。
    // [確認] - SENDER が "送信方法を選択してください" を出力すること。
    /* TCP は接続確立の完了後に最初の PING 周期へ入るため、UDP より少し余裕を持たせる。 */
    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 2800));

    // Act
    // テキスト送信を選択する
    ASSERT_TRUE(writeLineStdin(send_h_, "T")); // [手順] - SENDER に "T" (テキスト) を入力する。
    ASSERT_NO_THROW(
        waitForOutput(send_h_, "メッセージ>", 3000)); // [手順] - SENDER が "メッセージ>" を出力するまで待機する。
    // [確認] - SENDER が "メッセージ>" を出力すること。

    // SENDER からメッセージを送り、RECIEVER が受信することを確認する
    ASSERT_TRUE(writeLineStdin(send_h_, "bidir-test")); // [手順] - SENDER に "bidir-test" を入力する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "圧縮送信しますか",
                                  3000)); // [手順] - SENDER が "圧縮送信しますか" を出力するまで待機する。
    // [確認] - SENDER が "圧縮送信しますか" を出力すること。

    ASSERT_TRUE(writeLineStdin(send_h_, "N")); // [手順] - SENDER に "N" を入力する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "続けて送信しますか",
                                  3000)); // [手順] - SENDER が "続けて送信しますか" を出力するまで待機する。
    // [確認] - SENDER が "続けて送信しますか" を出力すること。

    ASSERT_TRUE(writeLineStdin(send_h_, "N")); // [手順] - SENDER に "N" を入力する。

    int send_exit = waitForExit(send_h_, 5000); // [手順] - SENDER が終了するまで待機する。

    // RECIEVER を停止して出力を回収する
    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    EXPECT_EQ(0, send_exit); // [確認] - SENDER の終了コードが 0 であること。
    EXPECT_NE(string::npos,
              getStdout(recv_h_).find("bidir-test")); // [確認] - RECIEVER が "bidir-test" を受信していること。
}

// 暗号化有効時、平文の UDP DATA パケットが破棄されることを確認する
TEST_F(porterSendRecvTest, encrypted_unicast_drops_plain_udp_packet)
{
    PorterConfigBuilder cfg;
    string config_path =
        cfg.addUnicastService(30, 19030, "127.0.0.1", "mysecretphrase")
            .build();

    recv_h_ = startProcessAsync(recv_path, {config_path, "30"}, makeOpts());
    ASSERT_NE(nullptr, recv_h_);
    ASSERT_NO_THROW(waitForOutput(recv_h_, "受信待機中", 5000));

    ASSERT_EQ(0, send_udp_packet(
                     make_plain_data_packet(30, 0x1001U, 1234, 5678, 0U, "plain-should-drop"),
                     19030));
    sleep_ms(300);

    send_h_ = startProcessAsync(send_path, {config_path, "30"}, makeOpts());
    ASSERT_NE(nullptr, send_h_);
    ASSERT_NO_THROW(waitForOutput(send_h_, "送信方法を選択してください", 5000));

    ASSERT_TRUE(writeLineStdin(send_h_, "T"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "メッセージ>", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "encrypted-ok"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "圧縮送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "続けて送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));

    EXPECT_EQ(0, waitForExit(send_h_, 5000));

    ASSERT_NO_THROW(waitForOutput(recv_h_, "encrypted-ok", 3000));

    interruptProcess(recv_h_);
    waitForExit(recv_h_, 3000);

    {
        string recv_out = getStdout(recv_h_);
        EXPECT_EQ(string::npos, recv_out.find("plain-should-drop"));
        EXPECT_NE(string::npos, recv_out.find("encrypted-ok"));
    }
}

// 暗号化有効 N:1 でタグ不正の初回パケットが peer slot を消費しないことを確認する
TEST_F(porterSendRecvTest, encrypted_n1_bad_tag_does_not_consume_peer_slot)
{
    PorterConfigBuilder server_cfg;
    PorterConfigBuilder client_cfg;
    /*
     * 実運用では、関連ノードに同一の定義ファイルを配布し、同じ service_id の定義を用いて
     * unicast_bidir_n1 サーバと unicast_bidir クライアントが通信する。
     * 本テストでは PorterConfigBuilder が 1 ファイル内に同じ service_id の複数定義を持てないため、
     * server/client 用に別ファイルを生成して同一 service_id の構成を模擬する。
     */
    string server_config_path =
        server_cfg.addUnicastBidirN1Service(50, 19050, 1, "0.0.0.0", "mysecretphrase")
            .build();
    string client_config_path =
        client_cfg.addUnicastBidirService(50, 19050, "127.0.0.1", "mysecretphrase")
            .build();

    recv_h_ = startProcessAsync(recv_path, {server_config_path, "50"}, makeOpts());
    ASSERT_NE(nullptr, recv_h_);
    ASSERT_NO_THROW(waitForOutput(recv_h_, "受信待機中", 5000));

    ASSERT_EQ(0, send_udp_packet(
                     make_invalid_encrypted_ping_packet(50, 0x2001U, 2234, 6678, 0U),
                     19050));
    sleep_ms(300);

    EXPECT_EQ(string::npos, getStdout(recv_h_).find("接続確立"));

    send_h_ = startProcessAsync(send_path, {client_config_path, "50"}, makeOpts());
    ASSERT_NE(nullptr, send_h_);
    ASSERT_NO_THROW(waitForOutput(send_h_, "双方向モード", 5000));
    ASSERT_NO_THROW(waitForOutput(send_h_, "送信方法を選択してください", 3000));

    /* 状態変化時の割り込み PING により、双方向 CONNECTED が 2 周期未満で成立することを確認する。 */
    /* TCP は接続確立の完了後に最初の PING 周期へ入るため、UDP より少し余裕を持たせる。 */
    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 2800));
    ASSERT_NO_THROW(waitForOutput(send_h_, "接続確立", 2800));

    ASSERT_TRUE(writeLineStdin(send_h_, "T"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "メッセージ>", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "n1-secure-ok"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "圧縮送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "続けて送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));

    EXPECT_EQ(0, waitForExit(send_h_, 5000));

    interruptProcess(recv_h_);
    waitForExit(recv_h_, 3000);

    {
        string recv_out = getStdout(recv_h_);
        EXPECT_NE(string::npos, recv_out.find("接続確立"));
        EXPECT_NE(string::npos, recv_out.find("n1-secure-ok"));
    }
}

// N:1 で未知 peer の初回 DATA が peer slot を消費せず破棄されることを確認する
TEST_F(porterSendRecvTest, n1_initial_plain_data_does_not_consume_peer_slot)
{
    PorterConfigBuilder server_cfg;
    PorterConfigBuilder client_cfg;
    string server_config_path =
        server_cfg.addUnicastBidirN1Service(52, 19052, 1, "0.0.0.0")
            .build();
    string client_config_path =
        client_cfg.addUnicastBidirService(52, 19052)
            .build();

    recv_h_ = startProcessAsync(recv_path, {server_config_path, "52"}, makeOpts());
    ASSERT_NE(nullptr, recv_h_);
    ASSERT_NO_THROW(waitForOutput(recv_h_, "受信待機中", 5000));

    ASSERT_EQ(0, send_udp_packet(
                     make_plain_data_packet(52, 0x3001U, 3234, 7678, 0U, "plain-n1-drop"),
                     19052));
    sleep_ms(300);

    EXPECT_EQ(string::npos, getStdout(recv_h_).find("接続確立"));
    EXPECT_EQ(string::npos, getStdout(recv_h_).find("plain-n1-drop"));

    send_h_ = startProcessAsync(send_path, {client_config_path, "52"}, makeOpts());
    ASSERT_NE(nullptr, send_h_);
    ASSERT_NO_THROW(waitForOutput(send_h_, "双方向モード", 5000));
    ASSERT_NO_THROW(waitForOutput(send_h_, "送信方法を選択してください", 3000));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 5000));
    ASSERT_NO_THROW(waitForOutput(send_h_, "接続確立", 5000));

    ASSERT_TRUE(writeLineStdin(send_h_, "T"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "メッセージ>", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "n1-after-ping-ok"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "圧縮送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "続けて送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));

    EXPECT_EQ(0, waitForExit(send_h_, 5000));

    ASSERT_NO_THROW(waitForOutput(recv_h_, "n1-after-ping-ok", 3000));

    interruptProcess(recv_h_);
    waitForExit(recv_h_, 3000);

    {
        string recv_out = getStdout(recv_h_);
        EXPECT_EQ(string::npos, recv_out.find("plain-n1-drop"));
        EXPECT_NE(string::npos, recv_out.find("n1-after-ping-ok"));
    }
}

// 暗号化有効 N:1 双方向通信でクライアント側も CONNECTED になってから送信できることを確認する
TEST_F(porterSendRecvTest, encrypted_n1_client_reaches_connected_before_send)
{
    PorterConfigBuilder server_cfg;
    PorterConfigBuilder client_cfg;
    string server_config_path =
        server_cfg.addUnicastBidirN1Service(51, 19051, 1, "0.0.0.0", "mysecretphrase")
            .build();
    string client_config_path =
        client_cfg.addUnicastBidirService(51, 19051, "127.0.0.1", "mysecretphrase")
            .build();

    recv_h_ = startProcessAsync(recv_path, {server_config_path, "51"}, makeOpts());
    ASSERT_NE(nullptr, recv_h_);
    ASSERT_NO_THROW(waitForOutput(recv_h_, "受信待機中", 5000));

    send_h_ = startProcessAsync(send_path, {client_config_path, "51"}, makeOpts());
    ASSERT_NE(nullptr, send_h_);
    ASSERT_NO_THROW(waitForOutput(send_h_, "双方向モード", 5000));
    ASSERT_NO_THROW(waitForOutput(send_h_, "送信方法を選択してください", 3000));

    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 2800));
    ASSERT_NO_THROW(waitForOutput(send_h_, "接続確立", 2800));

    ASSERT_TRUE(writeLineStdin(send_h_, "T"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "メッセージ>", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "n1-connected-ok"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "圧縮送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "続けて送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));

    EXPECT_EQ(0, waitForExit(send_h_, 5000));

    interruptProcess(recv_h_);
    waitForExit(recv_h_, 3000);

    {
        string recv_out = getStdout(recv_h_);
        EXPECT_NE(string::npos, recv_out.find("接続確立"));
        EXPECT_NE(string::npos, recv_out.find("n1-connected-ok"));
    }
}

// 暗号化 tcp_bidir で一定時間のヘルスチェック後も送受信できることを確認する
TEST_F(porterSendRecvTest, encrypted_tcp_bidir_stays_healthy_and_receives)
{
    PorterConfigBuilder cfg;
    string config_path =
        cfg.addTcpBidirService(60, 19060, "127.0.0.1", "mysecretphrase")
            .build();

    recv_h_ = startProcessAsync(recv_path, {config_path, "60"}, makeOpts());
    ASSERT_NE(nullptr, recv_h_);
    ASSERT_NO_THROW(waitForOutput(recv_h_, "受信待機中", 5000));

    send_h_ = startProcessAsync(send_path, {"-l", "VERBOSE", config_path, "60"}, makeOpts());
    ASSERT_NE(nullptr, send_h_);
    ASSERT_NO_THROW(waitForOutput(send_h_, "送信方法を選択してください", 5000));

    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 2800));
    ASSERT_NO_THROW(waitForOutput(send_h_, "接続確立", 2800));

    ASSERT_TRUE(writeLineStdin(send_h_, "T"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "メッセージ>", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "tcp-encrypted-ok"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "圧縮送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "続けて送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));

    EXPECT_EQ(0, waitForExit(send_h_, 5000));

    interruptProcess(recv_h_);
    waitForExit(recv_h_, 3000);

    {
        string recv_out = getStdout(recv_h_);
        EXPECT_NE(string::npos, recv_out.find("接続確立"));
        EXPECT_NE(string::npos, recv_out.find("tcp-encrypted-ok"));
    }
}

// tcp_bidir は定周期 health PING 無効でも bootstrap PING だけで接続確立できることを確認する
TEST_F(porterSendRecvTest, tcp_bidir_connects_without_periodic_health_ping)
{
    PorterConfigBuilder cfg;
    string config_path =
        cfg.setTcpHealthIntervalMs(0)
            .setTcpHealthTimeoutMs(0)
            .addTcpBidirService(61, 19061)
            .build();

    recv_h_ = startProcessAsync(recv_path, {config_path, "61"}, makeOpts());
    ASSERT_NE(nullptr, recv_h_);
    ASSERT_NO_THROW(waitForOutput(recv_h_, "受信待機中", 5000));

    send_h_ = startProcessAsync(send_path, {"-l", "VERBOSE", config_path, "61"}, makeOpts());
    ASSERT_NE(nullptr, send_h_);
    ASSERT_NO_THROW(waitForOutput(send_h_, "送信方法を選択してください", 5000));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 3000));
    ASSERT_NO_THROW(waitForOutput(send_h_, "接続確立", 3000));

    ASSERT_TRUE(writeLineStdin(send_h_, "T"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "メッセージ>", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "tcp-before-connected"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "圧縮送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "続けて送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));

    EXPECT_EQ(0, waitForExit(send_h_, 5000));

    interruptProcess(recv_h_);
    waitForExit(recv_h_, 3000);

    EXPECT_EQ(string::npos, getStderr(send_h_).find("未接続のため送信できません"));
    EXPECT_NE(string::npos, getStdout(recv_h_).find("接続確立"));
    EXPECT_NE(string::npos, getStdout(recv_h_).find("tcp-before-connected"));
}

// tcp_bidir で定周期 health PING 無効時は tcp_health_timeout_ms を無視して接続維持できることを確認する
TEST_F(porterSendRecvTest, tcp_bidir_without_periodic_health_ping_ignores_timeout)
{
    PorterConfigBuilder cfg;
    string config_path =
        cfg.setTcpHealthIntervalMs(0)
            .setTcpHealthTimeoutMs(500)
            .addTcpBidirService(62, 19062)
            .build();

    recv_h_ = startProcessAsync(recv_path, {config_path, "62"}, makeOpts());
    ASSERT_NE(nullptr, recv_h_);
    ASSERT_NO_THROW(waitForOutput(recv_h_, "受信待機中", 5000));

    send_h_ = startProcessAsync(send_path, {"-l", "VERBOSE", config_path, "62"}, makeOpts());
    ASSERT_NE(nullptr, send_h_);
    ASSERT_NO_THROW(waitForOutput(send_h_, "送信方法を選択してください", 5000));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 3000));
    ASSERT_NO_THROW(waitForOutput(send_h_, "接続確立", 3000));

    sleep_ms(1200);

    ASSERT_TRUE(writeLineStdin(send_h_, "T"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "メッセージ>", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "tcp-timeout-ignored"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "圧縮送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "続けて送信しますか", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "N"));

    EXPECT_EQ(0, waitForExit(send_h_, 5000));

    interruptProcess(recv_h_);
    waitForExit(recv_h_, 3000);

    EXPECT_EQ(string::npos, getStderr(send_h_).find("PING timeout"));
    EXPECT_EQ(string::npos, getStderr(recv_h_).find("PING timeout"));
    EXPECT_NE(string::npos, getStdout(recv_h_).find("tcp-timeout-ignored"));
}

// バイナリファイル送信テスト: 受信側で一時ファイルに保存されることを確認する
TEST_F(porterSendRecvTest, send_binary_file_and_recv_saves)
{
    // Arrange
    PorterConfigBuilder cfg;
    string config_path =
        cfg.addUnicastService(10, 19013)
            .build(); // [状態] - 127.0.0.1 で ポート 19013 を送受信に利用する unicast サービスを定義する。

    // バイナリファイルを作成する (NUL バイトを含むデータ)
    TempBinaryFile bin_file;
    vector<uint8_t> bin_content = {0x00, 0x01, 0x02, 0x03, 0xFF, 0xFE, 0x80, 0x7F,
                                   0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70};
    string bin_path = bin_file.create(bin_content); // [状態] - NUL バイトを含む 16 バイトのバイナリファイルを作成する。
    ASSERT_FALSE(bin_path.empty());                 // [確認] - バイナリファイルが作成されること。

    // RECIEVER を先に起動してリスナー確立を待つ
    recv_h_ = startProcessAsync(recv_path, {config_path, "10"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_);                                             // [確認] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。
    // [確認] - RECIEVER が "受信待機中" を出力すること。

    // SENDER を起動してプロンプトを待つ
    send_h_ = startProcessAsync(send_path, {config_path, "10"}, makeOpts()); // [手順] - SENDER を起動する。
    ASSERT_NE(nullptr, send_h_);                                             // [確認] - SENDER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(send_h_, "送信方法を選択してください", 5000)); // [手順] - SENDER が送信方法選択プロンプトを出力するまで待機する。
    // [確認] - SENDER が "送信方法を選択してください" を出力すること。

    // Act
    // ファイル送信を選択する
    ASSERT_TRUE(writeLineStdin(send_h_, "f")); // [手順] - SENDER に "f" (ファイル) を入力する。
    ASSERT_NO_THROW(
        waitForOutput(send_h_, "ファイルパス>", 3000)); // [手順] - SENDER が "ファイルパス>" を出力するまで待機する。
    // [確認] - SENDER が "ファイルパス>" を出力すること。

    ASSERT_TRUE(writeLineStdin(send_h_, bin_path)); // [手順] - SENDER にバイナリファイルのパスを入力する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "圧縮送信しますか",
                                  3000)); // [手順] - SENDER が "圧縮送信しますか" を出力するまで待機する。
    // [確認] - SENDER が "圧縮送信しますか" を出力すること。

    ASSERT_TRUE(writeLineStdin(send_h_, "N")); // [手順] - SENDER に "N" を入力する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "ファイル送信完了",
                                  3000)); // [手順] - SENDER が "ファイル送信完了" を出力するまで待機する。
    // [確認] - SENDER が "ファイル送信完了" を出力すること。

    ASSERT_NO_THROW(waitForOutput(send_h_, "続けて送信しますか",
                                  3000)); // [手順] - SENDER が "続けて送信しますか" を出力するまで待機する。

    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "バイナリデータを保存しました", 3000)); // [手順] - RECIEVER が保存メッセージを出力するまで待機する。

    writeLineStdin(send_h_, "N"); // [手順] - SENDER に "N" を入力する。

    int send_exit = waitForExit(send_h_, 5000); // [手順] - SENDER が終了するまで待機する。

    // RECIEVER を停止して出力を回収する
    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    string recv_out = getStdout(recv_h_);
    EXPECT_EQ(0, send_exit);                        // [確認] - SENDER の終了コードが 0 であること。
    EXPECT_NE(string::npos,
              recv_out.find("バイナリデータを保存しました")); // [確認] - RECIEVER がバイナリデータを一時ファイルに保存したこと。
    EXPECT_NE(string::npos,
              recv_out.find("受信 (16 バイト)")); // [確認] - RECIEVER の受信バイト数が 16 バイトであること。
}

// テキストメッセージ送信テスト: recv.c の変更後もテキストが正しく表示されることを確認する
TEST_F(porterSendRecvTest, send_text_still_displays_as_text)
{
    // Arrange
    PorterConfigBuilder cfg;
    string config_path =
        cfg.addUnicastService(10, 19014)
            .build(); // [状態] - 127.0.0.1 で ポート 19014 を送受信に利用する unicast サービスを定義する。

    // RECIEVER を先に起動してリスナー確立を待つ
    recv_h_ = startProcessAsync(recv_path, {config_path, "10"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_);                                             // [確認] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。
    // [確認] - RECIEVER が "受信待機中" を出力すること。

    // SENDER を起動してプロンプトを待つ
    send_h_ = startProcessAsync(send_path, {config_path, "10"}, makeOpts()); // [手順] - SENDER を起動する。
    ASSERT_NE(nullptr, send_h_);                                             // [確認] - SENDER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(send_h_, "送信方法を選択してください", 5000)); // [手順] - SENDER が送信方法選択プロンプトを出力するまで待機する。
    // [確認] - SENDER が "送信方法を選択してください" を出力すること。

    // Act
    // テキスト送信を選択する
    ASSERT_TRUE(writeLineStdin(send_h_, "T")); // [手順] - SENDER に "T" (テキスト) を入力する。
    ASSERT_NO_THROW(
        waitForOutput(send_h_, "メッセージ>", 3000)); // [手順] - SENDER が "メッセージ>" を出力するまで待機する。
    // [確認] - SENDER が "メッセージ>" を出力すること。

    ASSERT_TRUE(writeLineStdin(send_h_, "Hello Text")); // [手順] - SENDER に "Hello Text" を入力する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "圧縮送信しますか",
                                  3000)); // [手順] - SENDER が "圧縮送信しますか" を出力するまで待機する。

    ASSERT_TRUE(writeLineStdin(send_h_, "N")); // [手順] - SENDER に "N" を入力する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "続けて送信しますか",
                                  3000)); // [手順] - SENDER が "続けて送信しますか" を出力するまで待機する。

    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "Hello Text", 3000)); // [手順] - RECIEVER が "Hello Text" を出力するまで待機する。

    writeLineStdin(send_h_, "N"); // [手順] - SENDER に "N" を入力する。

    int send_exit = waitForExit(send_h_, 5000); // [手順] - SENDER が終了するまで待機する。

    // RECIEVER を停止して出力を回収する
    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    string recv_out = getStdout(recv_h_);
    EXPECT_EQ(0, send_exit);                                       // [確認] - SENDER の終了コードが 0 であること。
    EXPECT_NE(string::npos, recv_out.find("Hello Text"));          // [確認] - RECIEVER が "Hello Text" をテキストとして表示していること。
    EXPECT_EQ(string::npos,
              recv_out.find("バイナリデータを保存しました")); // [確認] - RECIEVER がバイナリ保存メッセージを出力していないこと。
}

// サイズ超過ファイル送信テスト: 65535 バイトを超えるファイルの送信が拒否されることを確認する
TEST_F(porterSendRecvTest, send_file_too_large_fails)
{
    // Arrange
    PorterConfigBuilder cfg;
    string config_path =
        cfg.addUnicastService(10, 19015)
            .build(); // [状態] - 127.0.0.1 で ポート 19015 を送受信に利用する unicast サービスを定義する。

    // 65536 バイトのファイルを作成する (POTR_MAX_MESSAGE_SIZE = 65535 を超過)
    TempBinaryFile large_file;
    vector<uint8_t> large_content(65536, 0xAA); // [状態] - 65536 バイトのファイルを作成する。
    string large_path = large_file.create(large_content);
    ASSERT_FALSE(large_path.empty()); // [確認] - ファイルが作成されること。

    // SENDER を起動してプロンプトを待つ (RECIEVER は不要: 送信されないため)
    send_h_ = startProcessAsync(send_path, {config_path, "10"}, makeOpts()); // [手順] - SENDER を起動する。
    ASSERT_NE(nullptr, send_h_);                                             // [確認] - SENDER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(send_h_, "送信方法を選択してください", 5000)); // [手順] - SENDER が送信方法選択プロンプトを出力するまで待機する。

    // Act
    // ファイル送信を選択し、サイズ超過ファイルのパスを入力する
    ASSERT_TRUE(writeLineStdin(send_h_, "f")); // [手順] - SENDER に "f" (ファイル) を入力する。
    ASSERT_NO_THROW(
        waitForOutput(send_h_, "ファイルパス>", 3000)); // [手順] - SENDER が "ファイルパス>" を出力するまで待機する。

    ASSERT_TRUE(writeLineStdin(send_h_, large_path)); // [手順] - SENDER にサイズ超過ファイルのパスを入力する。

    // エラーメッセージ出力後に「続けて送信しますか」プロンプトが表示されるはず
    ASSERT_NO_THROW(waitForOutput(send_h_, "続けて送信しますか",
                                  3000)); // [手順] - SENDER が "続けて送信しますか" を出力するまで待機する。
    // [確認] - SENDER がエラー後も対話を継続していること。

    writeLineStdin(send_h_, "N"); // [手順] - SENDER に "N" を入力する。

    int send_exit = waitForExit(send_h_, 5000); // [手順] - SENDER が終了するまで待機する。

    // Assert
    string send_err = getStderr(send_h_);
    EXPECT_EQ(0, send_exit);                                                     // [確認] - SENDER の終了コードが 0 であること (エラーはループ内で処理される)。
    EXPECT_NE(string::npos, send_err.find("最大送信サイズ")); // [確認] - SENDER の stderr にサイズ超過エラーが出力されていること。
}
