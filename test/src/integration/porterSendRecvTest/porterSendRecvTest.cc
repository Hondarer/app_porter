#include <com_util/base/platform.h>
#include <porter/porter_const.h>
#include <porter/porter_type.h>
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
#endif /* PLATFORM_ */

/**
 * テスト用の一時バイナリ ファイルを作成するヘルパー。
 * デストラクターでファイルを削除する。
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

static vector<uint8_t> make_plain_data_packet(int64_t service_id, uint32_t session_id, int64_t session_tv_sec,
                                              int32_t session_tv_nsec, uint32_t seq_num, const string &payload)
{
    size_t packed_len = POTR_PAYLOAD_ELEM_HDR_SIZE + payload.size();
    vector<uint8_t> packet(kPacketHeaderSize + packed_len, 0);

    write_i64_be(packet, 0, service_id);
    write_i64_be(packet, 8, session_tv_sec);
    write_u32_be(packet, 16, session_id);
    write_u32_be(packet, 20, (uint32_t)session_tv_nsec);
    write_u32_be(packet, 24, seq_num);
    write_u32_be(packet, 28, 0U);
    write_u16_be(packet, 32, POTR_FLAG_DATA);
    write_u16_be(packet, 34, (uint16_t)packed_len);
    write_u32_be(packet, 36, POTR_PROTOCOL_VERSION);
    write_u16_be(packet, kPacketHeaderSize, 0U);
    write_u32_be(packet, kPacketHeaderSize + 2U, (uint32_t)payload.size());
    memcpy(packet.data() + kPacketHeaderSize + POTR_PAYLOAD_ELEM_HDR_SIZE, payload.data(), payload.size());
    return packet;
}

static vector<uint8_t> make_plain_ping_packet(int64_t service_id, uint32_t session_id, int64_t session_tv_sec,
                                              int32_t session_tv_nsec, uint32_t seq_num, uint8_t ping_state)
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
    write_u32_be(packet, 36, POTR_PROTOCOL_VERSION);
    return packet;
}

static vector<uint8_t> make_plain_fin_packet(int64_t service_id, uint32_t session_id, int64_t session_tv_sec,
                                             int32_t session_tv_nsec, uint32_t ack_num, bool fin_target_valid)
{
    vector<uint8_t> packet(kPacketHeaderSize, 0);
    uint16_t flags = POTR_FLAG_FIN;

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
    write_u32_be(packet, 36, POTR_PROTOCOL_VERSION);
    return packet;
}

static vector<uint8_t> make_invalid_encrypted_ping_packet(int64_t service_id, uint32_t session_id,
                                                          int64_t session_tv_sec, int32_t session_tv_nsec,
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
    write_u32_be(packet, 36, POTR_PROTOCOL_VERSION);
    return packet;
}

static int send_udp_packet(const vector<uint8_t> &packet, int port)
{
#if defined(PLATFORM_LINUX)
    int sockfd;
    struct sockaddr_in addr;
    ssize_t sent;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    sent = sendto(sockfd, packet.data(), packet.size(), 0, (const struct sockaddr *)&addr, sizeof(addr));
    close(sockfd);
    if (sent == (ssize_t)packet.size())
    {
        return 0;
    }
    return -1;
#elif defined(PLATFORM_WINDOWS)
    WSADATA wsa;
    SOCKET sockfd;
    struct sockaddr_in addr;
    int sent;

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
    addr.sin_port = htons((uint16_t)port);
    InetPtonA(AF_INET, "127.0.0.1", &addr.sin_addr);

    sent = sendto(sockfd, (const char *)packet.data(), (int)packet.size(), 0, (const struct sockaddr *)&addr,
                  sizeof(addr));
    closesocket(sockfd);
    WSACleanup();
    if (sent == (int)packet.size())
    {
        return 0;
    }
    return -1;
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
    size_t pos = 0;

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

    // TearDown でのクリーンアップ用。テスト失敗時もプロセス リークを防ぐ。
    AsyncProcessHandle recv_h_, send_h_;

    void SetUp() override
    {
        string ws = findWorkspaceRoot();
        ASSERT_FALSE(ws.empty());
#if defined(PLATFORM_LINUX)
        recv_path = ws + "/app/porter/prod/cbin/porter-test";
        send_path = ws + "/app/porter/prod/cbin/porter-test";
        lib_path = ws + "/app/porter/prod/lib" + ":" + ws + "/app/com_util/prod/lib";
#elif defined(PLATFORM_WINDOWS)
        recv_path = ws + "\\app\\porter\\prod\\cbin\\porter-test.exe";
        send_path = ws + "\\app\\porter\\prod\\cbin\\porter-test.exe";
        lib_path = ws + "\\app\\porter\\prod\\lib" + ";" + ws + "\\app\\com_util\\prod\\lib";
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
    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "10"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。
    // [確認_正常系] - RECIEVER が "受信待機中" を出力すること。

    // SENDER を起動して最初のプロンプトを待つ
    send_h_ = startProcessAsync(send_path, {"sender", config_path, "10"}, makeOpts()); // [手順] - SENDER を起動する。
    ASSERT_NE(nullptr, send_h_); // [確認_正常系] - SENDER が起動すること。
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 5000));

    // Pre-Assert

    // Act
    ASSERT_TRUE(writeLineStdin(send_h_, "send Hello Porter"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));

    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "Hello Porter", 3000)); // [手順] - RECIEVER が "Hello Porter" を出力するまで待機する。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信 (12 バイト)", 3000)); // [手順] - RECIEVER が受信バイト数を出力するまで待機する。

    writeLineStdin(send_h_, "exit");

    int send_exit = waitForExit(send_h_, 5000); // [手順] - SENDER が終了するまで待機する。

    // RECIEVER を停止して出力を回収する
    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    EXPECT_EQ(0, send_exit); // [確認_正常系] - waitForExit の戻り値として、SENDER の終了コードが 0 であること。
    EXPECT_NE(
        string::npos,
        getStdout(recv_h_).find("Hello Porter")); // [確認_正常系] - RECIEVER が "Hello Porter" を受信していること。
    EXPECT_NE(
        string::npos,
        getStdout(recv_h_).find(
            "受信 (12 バイト)")); // [確認_正常系] - RECIEVER の受信バイト数が 12 バイト ("Hello Porter" の文字数) であること。
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
    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "10"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。
    // [確認_正常系] - RECIEVER が "受信待機中" を出力すること。

    // SENDER を起動して最初のプロンプトを待つ
    send_h_ = startProcessAsync(send_path, {"sender", config_path, "10"}, makeOpts()); // [手順] - SENDER を起動する。
    ASSERT_NE(nullptr, send_h_); // [確認_正常系] - SENDER が起動すること。

    // Pre-Assert

    // Act
    // msg1 / msg2 / msg3 を順次送り込む
    const vector<string> messages = {"msg1", "msg2", "msg3"};
    // [手順] - プロンプト待機、送信、受信待機を msg1、msg2、msg3 の 3 回繰り返す。
    // [確認_正常系] - msg1、msg2、msg3 のそれぞれが送信のたびに RECIEVER で受信されること。
    for (size_t i = 0; i < messages.size(); i++)
    {
        ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));
        ASSERT_TRUE(writeLineStdin(send_h_, string("send ") + messages[i]));
        ASSERT_NO_THROW(waitForOutput(recv_h_, messages[i], 3000));
    }

    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "exit"));

    int send_exit = waitForExit(send_h_, 5000); // [手順] - SENDER が終了するまで待機する。

    // RECIEVER を停止して出力を回収する
    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    string recv_out = getStdout(recv_h_);
    EXPECT_EQ(0, send_exit); // [確認_正常系] - waitForExit の戻り値として、SENDER の終了コードが 0 であること。
    EXPECT_NE(string::npos, recv_out.find("msg1")); // [確認_正常系] - RECIEVER が "msg1" を受信していること。
    EXPECT_NE(string::npos, recv_out.find("msg2")); // [確認_正常系] - RECIEVER が "msg2" を受信していること。
    EXPECT_NE(string::npos, recv_out.find("msg3")); // [確認_正常系] - RECIEVER が "msg3" を受信していること。
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
    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "10"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。
    // [確認_正常系] - RECIEVER が "受信待機中" を出力すること。

    // Pre-Assert

    // Act
    interruptProcess(recv_h_);                  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    int recv_exit = waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    EXPECT_EQ(0, recv_exit); // [確認_正常系] - waitForExit の戻り値として、RECIEVER の終了コードが 0 であること。
    EXPECT_NE(
        string::npos,
        getStdout(recv_h_).find("終了しました")); // [確認_正常系] - RECIEVER が "終了しました" を出力していること。
}

// 片方向 unicast で PING 無効でも初回 DATA 受信で CONNECTED と DATA 配信が成立することを確認する
TEST_F(porterSendRecvTest, unicast_initial_data_establishes_connected_without_ping)
{
    // Arrange
    PorterConfigBuilder cfg;
    string config_path = cfg.setUdpHealthIntervalMs(0)
                             .setUdpHealthTimeoutMs(1200)
                             .addUnicastService(11, 19016)
                             .build(); // [状態] - PING 無効 (interval 0) の unicast サービスをポート 19016 で定義する。

    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "11"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。

    // Pre-Assert

    // Act
    ASSERT_EQ(0, send_udp_packet(make_plain_data_packet(11, 0x4101U, 1200, 3400, 0U, "data-connect-ok"),
                                 19016)); // [手順] - 初回 DATA パケット "data-connect-ok" を UDP で直接送信する。

    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "接続確立", 3000)); // [手順] - RECIEVER が "接続確立" を出力するまで待機する。
    ASSERT_NO_THROW(waitForOutput(recv_h_, "data-connect-ok",
                                  3000)); // [手順] - RECIEVER が "data-connect-ok" を出力するまで待機する。

    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    {
        string recv_out = getStdout(recv_h_);
        EXPECT_NE(string::npos,
                  recv_out.find("接続確立")); // [確認_正常系] - PING なしでも初回 DATA で "接続確立" が出力されること。
        EXPECT_NE(string::npos,
                  recv_out.find("data-connect-ok")); // [確認_正常系] - 初回 DATA "data-connect-ok" が配信されること。
    }
}

// 片方向 unicast 送信者 open 直後は immediate PING を送らず、receiver が即 CONNECTED しないことを確認する
TEST_F(porterSendRecvTest, unicast_sender_open_does_not_trigger_immediate_ping)
{
    // Arrange
    PorterConfigBuilder cfg;
    string config_path = cfg.setUdpHealthIntervalMs(1000)
                             .setUdpHealthTimeoutMs(1500)
                             .addUnicastService(13, 19018)
                             .build(); // [状態] - PING 周期 1000 ms の unicast サービスをポート 19018 で定義する。

    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "13"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。

    // Pre-Assert

    // Act
    send_h_ = startProcessAsync(send_path, {"sender", config_path, "13"},
                                makeOpts()); // [手順] - SENDER を起動する (open 直後の状態を作る)。
    ASSERT_NE(nullptr, send_h_);             // [確認_正常系] - SENDER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(send_h_, "porter-test[sender:", 5000)); // [手順] - SENDER がプロンプトを出力するまで待機する。

    sleep_ms(250); // [手順] - PING 周期 (1000 ms) より短い 250 ms だけ待機する。
    EXPECT_EQ(string::npos,
              getStdout(recv_h_).find("接続確立")); // [確認_正常系] - open 直後の時点で "接続確立" が出力されないこと。

    interruptProcess(send_h_);                // [手順] - SENDER に SIGINT (Ctrl + C) を入力する。
    EXPECT_EQ(0, waitForExit(send_h_,
                             5000)); // [確認_正常系] - waitForExit の戻り値として、SENDER の終了コードが 0 であること。

    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    EXPECT_EQ(string::npos,
              getStdout(recv_h_).find(
                  "接続確立")); // [確認_正常系] - 最後まで immediate PING による "接続確立" が発生しないこと。
}

// 片方向 unicast で PING 無効時も有効 DATA の継続受信で health timeout が延長されることを確認する
TEST_F(porterSendRecvTest, unicast_data_resets_health_timeout_without_ping)
{
    // Arrange
    PorterConfigBuilder cfg;
    string config_path =
        cfg.setUdpHealthIntervalMs(0)
            .setUdpHealthTimeoutMs(900)
            .addUnicastService(12, 19017)
            .build(); // [状態] - PING 無効・health timeout 900 ms の unicast サービスをポート 19017 で定義する。

    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "12"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。

    // Pre-Assert

    // Act
    ASSERT_EQ(0, send_udp_packet(make_plain_data_packet(12, 0x4102U, 2200, 4500, 0U, "timeout-reset-1"),
                                 19017)); // [手順] - 1 通目の DATA "timeout-reset-1" を UDP で直接送信する。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "timeout-reset-1", 3000)); // [手順] - RECIEVER が 1 通目の受信を出力するまで待機する。

    sleep_ms(450); // [手順] - timeout (900 ms) の半分の 450 ms 待機する。
    EXPECT_EQ(string::npos,
              getStdout(recv_h_).find("切断検知")); // [確認_正常系] - timeout 前は "切断検知" が出力されないこと。

    ASSERT_EQ(0, send_udp_packet(make_plain_data_packet(12, 0x4102U, 2200, 4500, 1U, "timeout-reset-2"),
                                 19017)); // [手順] - 2 通目の DATA "timeout-reset-2" を送信して timeout を延長させる。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "timeout-reset-2", 3000)); // [手順] - RECIEVER が 2 通目の受信を出力するまで待機する。

    sleep_ms(550); // [手順] - 1 通目基準なら timeout する 550 ms 待機する。
    EXPECT_EQ(string::npos,
              getStdout(recv_h_).find("切断検知")); // [確認_正常系] - 2 通目の DATA で timeout が延長されていること。

    ASSERT_NO_THROW(waitForOutput(recv_h_, "切断検知",
                                  2000)); // [手順] - 最終 DATA 基準の timeout で "切断検知" が出力されるまで待機する。

    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    {
        string recv_out = getStdout(recv_h_);
        EXPECT_NE(string::npos, recv_out.find("接続確立")); // [確認_正常系] - "接続確立" が出力されていること。
        EXPECT_NE(string::npos,
                  recv_out.find("timeout-reset-1")); // [確認_正常系] - 1 通目の DATA が配信されていること。
        EXPECT_NE(string::npos,
                  recv_out.find("timeout-reset-2")); // [確認_正常系] - 2 通目の DATA が配信されていること。
        EXPECT_NE(string::npos,
                  recv_out.find("切断検知")); // [確認_正常系] - 最終的に timeout で "切断検知" が出力されること。
    }
}

// 単発送信の直後に close しても、最終 DATA が切断前に配信されることを確認する
TEST_F(porterSendRecvTest, unicast_close_after_single_send_delivers_before_disconnect)
{
    // Arrange
    const string payload = "fin-pending-ok"; // [状態] - 送信メッセージを "fin-pending-ok" とする。
    PorterConfigBuilder cfg;
    string config_path =
        cfg.addUnicastService(53, 19053).build(); // [状態] - unicast サービスをポート 19053 で定義する。

    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "53"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。

    send_h_ = startProcessAsync(send_path, {"sender", config_path, "53"}, makeOpts()); // [手順] - SENDER を起動する。
    ASSERT_NE(nullptr, send_h_); // [確認_正常系] - SENDER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(send_h_, "porter-test[sender:", 5000)); // [手順] - SENDER がプロンプトを出力するまで待機する。

    // Pre-Assert

    // Act
    ASSERT_TRUE(writeLineStdin(send_h_, string("send ") + payload)); // [手順] - 単発メッセージを送信する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));

    /* sender 側は追加送信せず即 close する。 */
    ASSERT_TRUE(writeLineStdin(send_h_, "exit")); // [手順] - 送信直後に "exit" で SENDER を close する。
    EXPECT_EQ(0, waitForExit(send_h_,
                             5000)); // [確認_正常系] - waitForExit の戻り値として、SENDER の終了コードが 0 であること。

    ASSERT_NO_THROW(waitForOutput(recv_h_, payload, 3000)); // [手順] - RECIEVER が最終 DATA を出力するまで待機する。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "切断検知", 3000)); // [手順] - RECIEVER が "切断検知" を出力するまで待機する。

    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    {
        string recv_out = getStdout(recv_h_);
        size_t data_pos = recv_out.find(payload);
        size_t disc_pos = recv_out.find("切断検知");
        EXPECT_NE(string::npos, data_pos); // [確認_正常系] - 最終 DATA "fin-pending-ok" が配信されていること。
        EXPECT_NE(string::npos, disc_pos); // [確認_正常系] - "切断検知" が出力されていること。
        EXPECT_LT(data_pos, disc_pos);     // [確認_正常系] - 最終 DATA の配信が切断検知より先であること。
    }
}

// no-data FIN は FIN target フラグなしで即時切断されることを確認する
TEST_F(porterSendRecvTest, fin_without_target_flag_disconnects_immediately)
{
    // Arrange
    PorterConfigBuilder cfg;
    string config_path =
        cfg.addUnicastService(56, 19056).build(); // [状態] - unicast サービスをポート 19056 で定義する。

    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "56"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。

    // Pre-Assert

    // Act
    ASSERT_EQ(0, send_udp_packet(make_plain_ping_packet(56, 0x5601U, 4234, 8678, 0U, POTR_PING_STATE_UNDEFINED),
                                 19056)); // [手順] - PING パケットを送信して接続を確立させる。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "接続確立", 3000)); // [手順] - RECIEVER が "接続確立" を出力するまで待機する。

    ASSERT_EQ(0, send_udp_packet(make_plain_fin_packet(56, 0x5601U, 4234, 8678, 0U, false),
                                 19056)); // [手順] - target フラグなしの FIN パケットを送信する。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "切断検知", 3000)); // [手順] - RECIEVER が "切断検知" を出力するまで待機する。

    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    {
        string recv_out = getStdout(recv_h_);
        EXPECT_EQ((size_t)1,
                  count_occurrences(recv_out, "接続確立")); // [確認_正常系] - "接続確立" が 1 回だけ出力されること。
        EXPECT_EQ((size_t)1,
                  count_occurrences(recv_out,
                                    "切断検知")); // [確認_正常系] - FIN により "切断検知" が即時に 1 回出力されること。
    }
}

// FIN target が 0 に wrap する場合でも、flag により pending FIN が正しく解消されることを確認する
TEST_F(porterSendRecvTest, fin_target_zero_wrap_is_handled_by_flag)
{
    // Arrange
    PorterConfigBuilder cfg;
    const string payload = "wrap-fin-target-zero"; // [状態] - 最終 DATA のメッセージを "wrap-fin-target-zero" とする。
    string config_path =
        cfg.addUnicastService(57, 19057).build(); // [状態] - unicast サービスをポート 19057 で定義する。

    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "57"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。

    // Pre-Assert

    // Act
    ASSERT_EQ(0, send_udp_packet(make_plain_ping_packet(57, 0x5701U, 5234, 9678, UINT32_MAX, POTR_PING_STATE_UNDEFINED),
                                 19057)); // [手順] - 通番 UINT32_MAX の PING を送信して接続を確立させる。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "接続確立", 3000)); // [手順] - RECIEVER が "接続確立" を出力するまで待機する。

    ASSERT_EQ(0, send_udp_packet(make_plain_fin_packet(57, 0x5701U, 5234, 9678, 0U, true),
                                 19057)); // [手順] - target が 0 に wrap した FIN (target フラグ付き) を送信する。
    sleep_ms(150);                        // [手順] - 150 ms 待機する。
    EXPECT_EQ(string::npos, getStdout(recv_h_).find(
                                "切断検知")); // [確認_正常系] - 最終 DATA 到着前は切断されないこと (pending FIN)。

    ASSERT_EQ(0, send_udp_packet(make_plain_data_packet(57, 0x5701U, 5234, 9678, UINT32_MAX, payload),
                                 19057));                   // [手順] - 通番 UINT32_MAX の最終 DATA を送信する。
    ASSERT_NO_THROW(waitForOutput(recv_h_, payload, 3000)); // [手順] - RECIEVER が最終 DATA を出力するまで待機する。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "切断検知", 3000)); // [手順] - RECIEVER が "切断検知" を出力するまで待機する。

    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    {
        string recv_out = getStdout(recv_h_);
        size_t data_pos = recv_out.find(payload);
        size_t disc_pos = recv_out.find("切断検知");
        EXPECT_NE(string::npos, data_pos); // [確認_正常系] - 最終 DATA "wrap-fin-target-zero" が配信されていること。
        EXPECT_NE(string::npos, disc_pos); // [確認_正常系] - pending FIN が解消され "切断検知" が出力されること。
        EXPECT_LT(data_pos, disc_pos);     // [確認_正常系] - 最終 DATA の配信が切断検知より先であること。
    }
}

// N:1 で単発送信の直後に close しても、最終 DATA が peer 解放前に配信されることを確認する
TEST_F(porterSendRecvTest, n1_close_after_single_send_delivers_before_disconnect)
{
    // Arrange
    const string payload = "n1-fin-pending-ok"; // [状態] - 送信メッセージを "n1-fin-pending-ok" とする。
    PorterConfigBuilder server_cfg;
    PorterConfigBuilder client_cfg;
    string server_config_path = server_cfg.addUnicastBidirN1Service(54, 19054, 1, "0.0.0.0")
                                    .build(); // [状態] - N:1 サーバー側サービスをポート 19054 で定義する。
    string client_config_path = client_cfg.addUnicastBidirService(54, 19054)
                                    .build(); // [状態] - クライアント側の unicast_bidir サービスを定義する。

    recv_h_ = startProcessAsync(recv_path, {"receiver", server_config_path, "54"},
                                makeOpts()); // [手順] - RECIEVER (N:1 サーバー) を起動する。
    ASSERT_NE(nullptr, recv_h_);             // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。

    send_h_ = startProcessAsync(send_path, {"sender", client_config_path, "54"},
                                makeOpts()); // [手順] - SENDER (クライアント) を起動する。
    ASSERT_NE(nullptr, send_h_);             // [確認_正常系] - SENDER が起動すること。
    ASSERT_NO_THROW(waitForOutput(send_h_, "双方向モード", 5000));
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 3000)); // [手順] - 双方が "接続確立" を出力するまで待機する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "接続確立", 3000));

    // Pre-Assert

    // Act
    ASSERT_TRUE(writeLineStdin(send_h_, string("send ") + payload)); // [手順] - 単発メッセージを送信する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));

    ASSERT_TRUE(writeLineStdin(send_h_, "exit"));           // [手順] - 送信直後に "exit" で SENDER を close する。
    EXPECT_EQ(0, waitForExit(send_h_,
                             5000)); // [確認_正常系] - waitForExit の戻り値として、SENDER の終了コードが 0 であること。
    ASSERT_NO_THROW(waitForOutput(recv_h_, payload, 3000)); // [手順] - RECIEVER が最終 DATA を出力するまで待機する。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "切断検知", 3000)); // [手順] - RECIEVER が "切断検知" を出力するまで待機する。

    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    {
        string recv_out = getStdout(recv_h_);
        size_t data_pos = recv_out.find(payload);
        size_t disc_pos = recv_out.find("切断検知");
        EXPECT_NE(string::npos, data_pos); // [確認_正常系] - 最終 DATA "n1-fin-pending-ok" が配信されていること。
        EXPECT_NE(string::npos, disc_pos); // [確認_正常系] - "切断検知" が出力されていること。
        EXPECT_LT(data_pos, disc_pos);     // [確認_正常系] - 最終 DATA の配信が peer 解放より先であること。
    }
}

// pending FIN のまま health timeout した後、新セッション受理で stale 状態が再発しないことを確認する
TEST_F(porterSendRecvTest, health_timeout_clears_pending_fin_before_new_session)
{
    // Arrange
    PorterConfigBuilder cfg;
    const string payload = "after-timeout-ok";
    const string first_payload = "before-timeout-pending"; // [状態] - timeout 前後のメッセージを 2 種類用意する。
    string config_path = cfg.setUdpHealthTimeoutMs(300)
                             .addUnicastService(55, 19055)
                             .build(); // [状態] - health timeout 300 ms の unicast サービスをポート 19055 で定義する。

    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "55"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。

    // Pre-Assert

    // Act
    ASSERT_EQ(0, send_udp_packet(make_plain_data_packet(55, 0x5501U, 3234, 7678, 0U, first_payload),
                                 19055));                         // [手順] - 1 つ目のセッションで DATA を送信する。
    ASSERT_NO_THROW(waitForOutput(recv_h_, first_payload, 3000)); // [手順] - RECIEVER が受信を出力するまで待機する。

    ASSERT_EQ(0,
              send_udp_packet(make_plain_fin_packet(55, 0x5501U, 3234, 7678, 2U, true),
                              19055)); // [手順] - 未到達の target (通番 2) を指す FIN を送信し pending FIN 状態にする。
    sleep_ms(150);                     // [手順] - 150 ms 待機する。
    EXPECT_EQ(string::npos,
              getStdout(recv_h_).find("切断検知")); // [確認_正常系] - pending FIN の時点では切断されないこと。
    ASSERT_NO_THROW(waitForOutput(recv_h_, "切断検知",
                                  2000)); // [手順] - health timeout により "切断検知" が出力されるまで待機する。

    ASSERT_EQ(0, send_udp_packet(make_plain_data_packet(55, 0x5502U, 3235, 7679, 0U, payload),
                                 19055)); // [手順] - 新しいセッション ID で DATA を送信する。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, payload, 3000)); // [手順] - RECIEVER が新セッションの受信を出力するまで待機する。
    sleep_ms(150);                              // [手順] - stale な pending FIN による誤切断がないか 150 ms 観察する。

    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    {
        string recv_out = getStdout(recv_h_);
        EXPECT_NE(string::npos,
                  recv_out.find(first_payload));         // [確認_正常系] - timeout 前の DATA が配信されていること。
        EXPECT_NE(string::npos, recv_out.find(payload)); // [確認_正常系] - 新セッションの DATA が配信されていること。
        EXPECT_EQ(
            (size_t)1,
            count_occurrences(
                recv_out,
                "切断検知")); // [確認_正常系] - "切断検知" が timeout の 1 回だけであること (stale FIN が再発しないこと)。
    }
}

// 片方向 unicast で recent DATA により periodic PING が抑止され、最後の DATA 基準で再開することを確認する
TEST_F(porterSendRecvTest, unicast_recent_data_defers_ping_until_last_data_interval)
{
    // Arrange
    PorterConfigBuilder cfg;
    string config_path = cfg.setUdpHealthIntervalMs(500)
                             .setUdpHealthTimeoutMs(1500)
                             .addUnicastService(14, 19019)
                             .build(); // [状態] - PING 周期 500 ms の unicast サービスをポート 19019 で定義する。

    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "14"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。

    send_h_ = startProcessAsync(send_path, {"sender", "-l", "VERBOSE", config_path, "14"},
                                makeOpts()); // [手順] - SENDER を VERBOSE トレース付きで起動する。
    ASSERT_NE(nullptr, send_h_);             // [確認_正常系] - SENDER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(send_h_, "porter-test[sender:", 5000)); // [手順] - SENDER がプロンプトを出力するまで待機する。

    // Pre-Assert

    // Act
    ASSERT_TRUE(writeLineStdin(send_h_, "send ping-delay-1")); // [手順] - 1 通目 "ping-delay-1" を送信する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "ping-delay-1", 3000));

    sleep_ms(250); // [手順] - PING 周期の半分の 250 ms 待機する。
    ASSERT_TRUE(writeLineStdin(
        send_h_, "send ping-delay-2")); // [手順] - 2 通目 "ping-delay-2" を送信し recent DATA を更新する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "ping-delay-2", 3000));

    sleep_ms(250); // [手順] - 1 通目基準では PING 周期を超える 250 ms 待機する。
    {
        string send_err = getStderr(send_h_);
        EXPECT_NE(
            string::npos,
            send_err.find(
                "suppress PING due to recent DATA")); // [確認_正常系] - recent DATA による PING 抑止トレースが出力されること。
        EXPECT_EQ(
            string::npos,
            send_err.find(
                "health[service_id=14]: PING seq=")); // [確認_正常系] - この時点では periodic PING が送信されないこと。
    }

    sleep_ms(400); // [手順] - 最後の DATA から PING 周期を超えるまで待機する。
    EXPECT_NE(
        string::npos,
        getStderr(send_h_).find(
            "health[service_id=14]: PING seq=")); // [確認_正常系] - 最後の DATA 基準で periodic PING が再開されること。

    ASSERT_TRUE(writeLineStdin(send_h_, "exit")); // [手順] - "exit" で SENDER を終了させる。
    EXPECT_EQ(0, waitForExit(send_h_,
                             5000)); // [確認_正常系] - waitForExit の戻り値として、SENDER の終了コードが 0 であること。

    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    {
        string recv_out = getStdout(recv_h_);
        EXPECT_NE(string::npos, recv_out.find("接続確立"));     // [確認_正常系] - "接続確立" が出力されていること。
        EXPECT_NE(string::npos, recv_out.find("ping-delay-1")); // [確認_正常系] - 1 通目が配信されていること。
        EXPECT_NE(string::npos, recv_out.find("ping-delay-2")); // [確認_正常系] - 2 通目が配信されていること。
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
    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "20"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。
    // [確認_正常系] - RECIEVER が "受信待機中" を出力すること。

    // SENDER を起動して最初のプロンプトを待つ
    send_h_ = startProcessAsync(send_path, {"sender", config_path, "20"}, makeOpts()); // [手順] - SENDER を起動する。
    ASSERT_NE(nullptr, send_h_); // [確認_正常系] - SENDER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(send_h_, "双方向モード", 5000)); // [手順] - SENDER が "双方向モード" を出力するまで待機する。
    // [確認_正常系] - SENDER が "双方向モード" を出力すること。
    ASSERT_NO_THROW(waitForOutput(
        send_h_, "porter-test[sender:", 3000)); // [手順] - SENDER が送信方法選択プロンプトを出力するまで待機する。
    // [確認_正常系] - SENDER が "porter-test[sender:" を出力すること。
    /* TCP は接続確立の完了後に最初の PING 周期へ入るため、UDP より少し余裕を持たせる。 */
    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 2800));

    // Pre-Assert

    // Act
    ASSERT_TRUE(writeLineStdin(send_h_, "send bidir-test"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "exit"));

    int send_exit = waitForExit(send_h_, 5000); // [手順] - SENDER が終了するまで待機する。

    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "bidir-test", 3000)); // [手順] - RECIEVER が "bidir-test" を出力するまで待機する。

    // RECIEVER を停止して出力を回収する
    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    EXPECT_EQ(0, send_exit); // [確認_正常系] - waitForExit の戻り値として、SENDER の終了コードが 0 であること。
    EXPECT_NE(string::npos,
              getStdout(recv_h_).find("bidir-test")); // [確認_正常系] - RECIEVER が "bidir-test" を受信していること。
}

// 暗号化有効時、平文の UDP DATA パケットが破棄されることを確認する
TEST_F(porterSendRecvTest, encrypted_unicast_drops_plain_udp_packet)
{
    // Arrange
    PorterConfigBuilder cfg;
    string config_path =
        cfg.addUnicastService(30, 19030, "127.0.0.1", "mysecretphrase")
            .build(); // [状態] - パスフレーズ "mysecretphrase" で暗号化した unicast サービスをポート 19030 で定義する。

    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "30"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。

    // Pre-Assert

    // Act
    ASSERT_EQ(0, send_udp_packet(make_plain_data_packet(30, 0x1001U, 1234, 5678, 0U, "plain-should-drop"),
                                 19030)); // [手順] - 平文の DATA パケット "plain-should-drop" を UDP で直接送信する。
    sleep_ms(300);                        // [手順] - 破棄処理のため 300 ms 待機する。

    send_h_ = startProcessAsync(send_path, {"sender", config_path, "30"},
                                makeOpts()); // [手順] - 暗号化設定の SENDER を起動する。
    ASSERT_NE(nullptr, send_h_);             // [確認_正常系] - SENDER が起動すること。
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 5000));

    ASSERT_TRUE(writeLineStdin(send_h_, "send encrypted-ok")); // [手順] - 暗号化経路で "encrypted-ok" を送信する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "exit")); // [手順] - "exit" で SENDER を終了させる。

    EXPECT_EQ(0, waitForExit(send_h_,
                             5000)); // [確認_正常系] - waitForExit の戻り値として、SENDER の終了コードが 0 であること。

    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "encrypted-ok", 3000)); // [手順] - RECIEVER が暗号化経路の受信を出力するまで待機する。

    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    {
        string recv_out = getStdout(recv_h_);
        EXPECT_EQ(string::npos,
                  recv_out.find("plain-should-drop")); // [確認_異常系] - 平文パケットが破棄され配信されないこと。
        EXPECT_NE(string::npos,
                  recv_out.find("encrypted-ok")); // [確認_正常系] - 暗号化経路のメッセージは配信されること。
    }
}

// 暗号化有効 N:1 でタグ不正の初回パケットが peer slot を消費しないことを確認する
TEST_F(porterSendRecvTest, encrypted_n1_bad_tag_does_not_consume_peer_slot)
{
    // Arrange
    PorterConfigBuilder server_cfg;
    PorterConfigBuilder client_cfg;
    /*
     * 実運用では、関連ノードに同一の定義ファイルを配布し、同じ service_id の定義を用いて
     * unicast_bidir_n1 サーバーと unicast_bidir クライアントが通信する。
     * 本テストでは PorterConfigBuilder が 1 ファイル内に同じ service_id の複数定義を持てないため、
     * server/client 用に別ファイルを生成して同一 service_id の構成を模擬する。
     */
    string server_config_path = server_cfg.addUnicastBidirN1Service(50, 19050, 1, "0.0.0.0", "mysecretphrase")
                                    .build(); // [状態] - 暗号化 N:1 サーバー (peer 上限 1) をポート 19050 で定義する。
    string client_config_path = client_cfg.addUnicastBidirService(50, 19050, "127.0.0.1", "mysecretphrase")
                                    .build(); // [状態] - 同じパスフレーズのクライアント側サービスを定義する。

    recv_h_ = startProcessAsync(recv_path, {"receiver", server_config_path, "50"},
                                makeOpts()); // [手順] - RECIEVER (N:1 サーバー) を起動する。
    ASSERT_NE(nullptr, recv_h_);             // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。

    // Pre-Assert

    // Act
    ASSERT_EQ(0, send_udp_packet(make_invalid_encrypted_ping_packet(50, 0x2001U, 2234, 6678, 0U),
                                 19050)); // [手順] - 認証タグ不正の暗号化 PING パケットを UDP で直接送信する。
    sleep_ms(300);                        // [手順] - 破棄処理のため 300 ms 待機する。

    EXPECT_EQ(string::npos,
              getStdout(recv_h_).find("接続確立")); // [確認_異常系] - タグ不正のパケットでは "接続確立" しないこと。

    send_h_ = startProcessAsync(send_path, {"sender", client_config_path, "50"},
                                makeOpts()); // [手順] - 正規のクライアントを起動する。
    ASSERT_NE(nullptr, send_h_);             // [確認_正常系] - SENDER が起動すること。
    ASSERT_NO_THROW(waitForOutput(send_h_, "双方向モード", 5000));
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));

    /* 状態変化時の割り込み PING により、双方向 CONNECTED が 2 周期未満で成立することを確認する。 */
    /* TCP は接続確立の完了後に最初の PING 周期へ入るため、UDP より少し余裕を持たせる。 */
    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 2800)); // [手順] - 双方が "接続確立" を出力するまで待機する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "接続確立", 2800));

    ASSERT_TRUE(
        writeLineStdin(send_h_, "send n1-secure-ok")); // [手順] - 正規クライアントから "n1-secure-ok" を送信する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "exit")); // [手順] - "exit" で SENDER を終了させる。

    EXPECT_EQ(0, waitForExit(send_h_,
                             5000)); // [確認_正常系] - waitForExit の戻り値として、SENDER の終了コードが 0 であること。

    ASSERT_NO_THROW(waitForOutput(recv_h_, "n1-secure-ok", 3000)); // [手順] - RECIEVER が受信を出力するまで待機する。

    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    {
        string recv_out = getStdout(recv_h_);
        EXPECT_NE(
            string::npos,
            recv_out.find("接続確立")); // [確認_正常系] - peer slot が消費されず正規クライアントが接続できること。
        EXPECT_NE(string::npos,
                  recv_out.find("n1-secure-ok")); // [確認_正常系] - 正規クライアントのメッセージが配信されること。
    }
}

// N:1 で未知 peer の初回 DATA が peer slot を消費せず破棄されることを確認する
TEST_F(porterSendRecvTest, n1_initial_plain_data_does_not_consume_peer_slot)
{
    // Arrange
    PorterConfigBuilder server_cfg;
    PorterConfigBuilder client_cfg;
    string server_config_path = server_cfg.addUnicastBidirN1Service(52, 19052, 1, "0.0.0.0")
                                    .build(); // [状態] - N:1 サーバー (peer 上限 1) をポート 19052 で定義する。
    string client_config_path = client_cfg.addUnicastBidirService(52, 19052)
                                    .build(); // [状態] - クライアント側の unicast_bidir サービスを定義する。

    recv_h_ = startProcessAsync(recv_path, {"receiver", server_config_path, "52"},
                                makeOpts()); // [手順] - RECIEVER (N:1 サーバー) を起動する。
    ASSERT_NE(nullptr, recv_h_);             // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。

    // Pre-Assert

    // Act
    ASSERT_EQ(0, send_udp_packet(make_plain_data_packet(52, 0x3001U, 3234, 7678, 0U, "plain-n1-drop"),
                                 19052)); // [手順] - 未知 peer からの初回 DATA "plain-n1-drop" を UDP で直接送信する。
    sleep_ms(300);                        // [手順] - 破棄処理のため 300 ms 待機する。

    EXPECT_EQ(string::npos,
              getStdout(recv_h_).find("接続確立")); // [確認_異常系] - 初回 DATA では "接続確立" しないこと。
    EXPECT_EQ(string::npos,
              getStdout(recv_h_).find("plain-n1-drop")); // [確認_異常系] - 初回 DATA が配信されず破棄されること。

    send_h_ = startProcessAsync(send_path, {"sender", client_config_path, "52"},
                                makeOpts()); // [手順] - 正規のクライアントを起動する。
    ASSERT_NE(nullptr, send_h_);             // [確認_正常系] - SENDER が起動すること。
    ASSERT_NO_THROW(waitForOutput(send_h_, "双方向モード", 5000));
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 5000)); // [手順] - 双方が "接続確立" を出力するまで待機する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "接続確立", 5000));

    ASSERT_TRUE(writeLineStdin(
        send_h_, "send n1-after-ping-ok")); // [手順] - 正規クライアントから "n1-after-ping-ok" を送信する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "exit")); // [手順] - "exit" で SENDER を終了させる。

    EXPECT_EQ(0, waitForExit(send_h_,
                             5000)); // [確認_正常系] - waitForExit の戻り値として、SENDER の終了コードが 0 であること。

    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "n1-after-ping-ok", 3000)); // [手順] - RECIEVER が受信を出力するまで待機する。

    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    {
        string recv_out = getStdout(recv_h_);
        EXPECT_EQ(string::npos,
                  recv_out.find("plain-n1-drop")); // [確認_異常系] - 未知 peer の初回 DATA が最後まで配信されないこと。
        EXPECT_NE(string::npos,
                  recv_out.find(
                      "n1-after-ping-ok")); // [確認_正常系] - peer slot が消費されず正規クライアントが通信できること。
    }
}

// 暗号化有効 N:1 双方向通信でクライアント側も CONNECTED になってから送信できることを確認する
TEST_F(porterSendRecvTest, encrypted_n1_client_reaches_connected_before_send)
{
    // Arrange
    PorterConfigBuilder server_cfg;
    PorterConfigBuilder client_cfg;
    string server_config_path = server_cfg.addUnicastBidirN1Service(51, 19051, 1, "0.0.0.0", "mysecretphrase")
                                    .build(); // [状態] - 暗号化 N:1 サーバーをポート 19051 で定義する。
    string client_config_path = client_cfg.addUnicastBidirService(51, 19051, "127.0.0.1", "mysecretphrase")
                                    .build(); // [状態] - 同じパスフレーズのクライアント側サービスを定義する。

    recv_h_ = startProcessAsync(recv_path, {"receiver", server_config_path, "51"},
                                makeOpts()); // [手順] - RECIEVER (N:1 サーバー) を起動する。
    ASSERT_NE(nullptr, recv_h_);             // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。

    send_h_ = startProcessAsync(send_path, {"sender", client_config_path, "51"},
                                makeOpts()); // [手順] - SENDER (クライアント) を起動する。
    ASSERT_NE(nullptr, send_h_);             // [確認_正常系] - SENDER が起動すること。
    ASSERT_NO_THROW(waitForOutput(send_h_, "双方向モード", 5000));
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));

    // Pre-Assert

    // Act
    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 2800)); // [手順] - サーバー側の "接続確立" を待機する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "接続確立", 2800)); // [手順] - クライアント側の "接続確立" を待機する。

    ASSERT_TRUE(
        writeLineStdin(send_h_, "send n1-connected-ok")); // [手順] - CONNECTED 後に "n1-connected-ok" を送信する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "exit")); // [手順] - "exit" で SENDER を終了させる。

    EXPECT_EQ(0, waitForExit(send_h_,
                             5000)); // [確認_正常系] - waitForExit の戻り値として、SENDER の終了コードが 0 であること。

    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "n1-connected-ok", 3000)); // [手順] - RECIEVER が受信を出力するまで待機する。

    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    {
        string recv_out = getStdout(recv_h_);
        EXPECT_NE(string::npos, recv_out.find("接続確立")); // [確認_正常系] - "接続確立" が出力されていること。
        EXPECT_NE(string::npos,
                  recv_out.find("n1-connected-ok")); // [確認_正常系] - CONNECTED 後の送信が配信されること。
    }
}

// 暗号化 tcp_bidir で一定時間のヘルスチェック後も送受信できることを確認する
TEST_F(porterSendRecvTest, encrypted_tcp_bidir_stays_healthy_and_receives)
{
    // Arrange
    PorterConfigBuilder cfg;
    string config_path =
        cfg.addTcpBidirService(60, 19060, "127.0.0.1", "mysecretphrase")
            .build(); // [状態] - パスフレーズ "mysecretphrase" で暗号化した tcp_bidir サービスをポート 19060 で定義する。

    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "60"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。

    send_h_ = startProcessAsync(send_path, {"sender", "-l", "VERBOSE", config_path, "60"},
                                makeOpts()); // [手順] - SENDER を VERBOSE トレース付きで起動する。
    ASSERT_NE(nullptr, send_h_);             // [確認_正常系] - SENDER が起動すること。
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 5000));

    // Pre-Assert

    // Act
    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 2800)); // [手順] - 双方が "接続確立" を出力するまで待機する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "接続確立", 2800));

    ASSERT_TRUE(writeLineStdin(
        send_h_, "send tcp-encrypted-ok")); // [手順] - ヘルスチェック経過後に "tcp-encrypted-ok" を送信する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));
    // send 側プロンプト復帰後でも recv 側 stdout への反映には遅延が出るため、
    // exit/interrupt の前に recv 側で受信文字列の出力を確認する。CPU 高負荷下
    // (テスト並列実行下) では recv 側の出力反映よりも interrupt が先行し、
    // recv_out に "tcp-encrypted-ok" が残らないことがある。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "tcp-encrypted-ok", 5000)); // [手順] - RECIEVER が受信を出力するまで待機する。
    ASSERT_TRUE(writeLineStdin(send_h_, "exit"));          // [手順] - "exit" で SENDER を終了させる。

    EXPECT_EQ(0, waitForExit(send_h_,
                             5000)); // [確認_正常系] - waitForExit の戻り値として、SENDER の終了コードが 0 であること。

    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    {
        string recv_out = getStdout(recv_h_);
        EXPECT_NE(string::npos, recv_out.find("接続確立")); // [確認_正常系] - "接続確立" が出力されていること。
        EXPECT_NE(
            string::npos,
            recv_out.find("tcp-encrypted-ok")); // [確認_正常系] - ヘルスチェック経過後も暗号化 TCP で受信できること。
    }
}

// tcp_bidir は定周期 health PING 無効でも bootstrap PING だけで接続確立できることを確認する
TEST_F(porterSendRecvTest, tcp_bidir_connects_without_periodic_health_ping)
{
    // Arrange
    PorterConfigBuilder cfg;
    string config_path =
        cfg.setTcpHealthIntervalMs(0)
            .setTcpHealthTimeoutMs(0)
            .addTcpBidirService(61, 19061)
            .build(); // [状態] - 定周期 health PING 無効 (interval 0) の tcp_bidir サービスをポート 19061 で定義する。

    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "61"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。

    // Pre-Assert

    // Act
    send_h_ = startProcessAsync(send_path, {"sender", "-l", "VERBOSE", config_path, "61"},
                                makeOpts()); // [手順] - SENDER を VERBOSE トレース付きで起動する。
    ASSERT_NE(nullptr, send_h_);             // [確認_正常系] - SENDER が起動すること。
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 5000));
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "接続確立", 3000)); // [手順] - bootstrap PING だけで双方の "接続確立" を待機する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "接続確立", 3000));

    ASSERT_TRUE(writeLineStdin(send_h_, "send tcp-before-connected")); // [手順] - "tcp-before-connected" を送信する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "exit")); // [手順] - "exit" で SENDER を終了させる。

    EXPECT_EQ(0, waitForExit(send_h_,
                             5000)); // [確認_正常系] - waitForExit の戻り値として、SENDER の終了コードが 0 であること。

    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "tcp-before-connected", 3000)); // [手順] - RECIEVER が受信を出力するまで待機する。

    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    EXPECT_EQ(string::npos,
              getStderr(send_h_).find("未接続のため送信できません")); // [確認_正常系] - 未接続エラーが発生しないこと。
    EXPECT_NE(string::npos,
              getStdout(recv_h_).find("接続確立")); // [確認_正常系] - bootstrap PING だけで "接続確立" できること。
    EXPECT_NE(string::npos,
              getStdout(recv_h_).find("tcp-before-connected")); // [確認_正常系] - メッセージが配信されること。
}

// tcp_bidir で定周期 health PING 無効時は tcp_health_timeout_ms を無視して接続維持できることを確認する
TEST_F(porterSendRecvTest, tcp_bidir_without_periodic_health_ping_ignores_timeout)
{
    // Arrange
    PorterConfigBuilder cfg;
    string config_path =
        cfg.setTcpHealthIntervalMs(0)
            .setTcpHealthTimeoutMs(500)
            .addTcpBidirService(62, 19062)
            .build(); // [状態] - PING 無効かつ timeout 500 ms の tcp_bidir サービスをポート 19062 で定義する。

    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "62"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。

    send_h_ = startProcessAsync(send_path, {"sender", "-l", "VERBOSE", config_path, "62"},
                                makeOpts()); // [手順] - SENDER を VERBOSE トレース付きで起動する。
    ASSERT_NE(nullptr, send_h_);             // [確認_正常系] - SENDER が起動すること。
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 5000));
    ASSERT_NO_THROW(waitForOutput(recv_h_, "接続確立", 3000)); // [手順] - 双方が "接続確立" を出力するまで待機する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "接続確立", 3000));

    // Pre-Assert

    // Act
    sleep_ms(1200); // [手順] - tcp_health_timeout_ms (500 ms) を大きく超える 1200 ms 待機する。

    ASSERT_TRUE(writeLineStdin(
        send_h_, "send tcp-timeout-ignored")); // [手順] - timeout 経過後に "tcp-timeout-ignored" を送信する。
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));
    ASSERT_TRUE(writeLineStdin(send_h_, "exit")); // [手順] - "exit" で SENDER を終了させる。

    EXPECT_EQ(0, waitForExit(send_h_,
                             5000)); // [確認_正常系] - waitForExit の戻り値として、SENDER の終了コードが 0 であること。

    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "tcp-timeout-ignored", 3000)); // [手順] - RECIEVER が受信を出力するまで待機する。

    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    EXPECT_EQ(string::npos,
              getStderr(send_h_).find("PING timeout")); // [確認_正常系] - SENDER 側で PING timeout が発生しないこと。
    EXPECT_EQ(string::npos,
              getStderr(recv_h_).find("PING timeout")); // [確認_正常系] - RECIEVER 側で PING timeout が発生しないこと。
    EXPECT_NE(string::npos,
              getStdout(recv_h_).find(
                  "tcp-timeout-ignored")); // [確認_正常系] - timeout 経過後も接続が維持され配信されること。
}

// バイナリ ファイル送信テスト: 受信側で一時ファイルに保存されることを確認する
TEST_F(porterSendRecvTest, send_binary_file_and_recv_saves)
{
    // Arrange
    PorterConfigBuilder cfg;
    string config_path =
        cfg.addUnicastService(10, 19013)
            .build(); // [状態] - 127.0.0.1 で ポート 19013 を送受信に利用する unicast サービスを定義する。

    // バイナリ ファイルを作成する (NUL バイトを含むデータ)
    TempBinaryFile bin_file;
    vector<uint8_t> bin_content = {0x00, 0x01, 0x02, 0x03, 0xFF, 0xFE, 0x80, 0x7F,
                                   0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70};
    string bin_path =
        bin_file.create(bin_content); // [状態] - NUL バイトを含む 16 バイトのバイナリ ファイルを作成する。
    ASSERT_FALSE(bin_path.empty());   // [確認_正常系] - バイナリ ファイルが作成されること。

    // RECIEVER を先に起動してリスナー確立を待つ
    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "10"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。
    // [確認_正常系] - RECIEVER が "受信待機中" を出力すること。

    // SENDER を起動してプロンプトを待つ
    send_h_ = startProcessAsync(send_path, {"sender", config_path, "10"}, makeOpts()); // [手順] - SENDER を起動する。
    ASSERT_NE(nullptr, send_h_); // [確認_正常系] - SENDER が起動すること。
    ASSERT_NO_THROW(waitForOutput(
        send_h_, "porter-test[sender:", 5000)); // [手順] - SENDER が送信方法選択プロンプトを出力するまで待機する。
    // [確認_正常系] - SENDER が "porter-test[sender:" を出力すること。

    // Pre-Assert

    // Act
    ASSERT_TRUE(writeLineStdin(send_h_, string("file ") + bin_path));
    ASSERT_NO_THROW(waitForOutput(send_h_, "ファイル送信完了",
                                  3000)); // [手順] - SENDER が "ファイル送信完了" を出力するまで待機する。
    // [確認_正常系] - SENDER が "ファイル送信完了" を出力すること。

    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));

    ASSERT_NO_THROW(waitForOutput(recv_h_, "バイナリ データを保存しました",
                                  3000)); // [手順] - RECIEVER が保存メッセージを出力するまで待機する。

    writeLineStdin(send_h_, "exit");

    int send_exit = waitForExit(send_h_, 5000); // [手順] - SENDER が終了するまで待機する。

    // RECIEVER を停止して出力を回収する
    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    string recv_out = getStdout(recv_h_);
    EXPECT_EQ(0, send_exit); // [確認_正常系] - waitForExit の戻り値として、SENDER の終了コードが 0 であること。
    EXPECT_NE(
        string::npos,
        recv_out.find(
            "バイナリ データを保存しました")); // [確認_正常系] - RECIEVER がバイナリ データを一時ファイルに保存したこと。
    EXPECT_NE(string::npos,
              recv_out.find("受信 (16 バイト)")); // [確認_正常系] - RECIEVER の受信バイト数が 16 バイトであること。
}

// テキスト メッセージ送信テスト: recv.c の変更後もテキストが正しく表示されることを確認する
TEST_F(porterSendRecvTest, send_text_still_displays_as_text)
{
    // Arrange
    PorterConfigBuilder cfg;
    string config_path =
        cfg.addUnicastService(10, 19014)
            .build(); // [状態] - 127.0.0.1 で ポート 19014 を送受信に利用する unicast サービスを定義する。

    // RECIEVER を先に起動してリスナー確立を待つ
    recv_h_ =
        startProcessAsync(recv_path, {"receiver", config_path, "10"}, makeOpts()); // [手順] - RECIEVER を起動する。
    ASSERT_NE(nullptr, recv_h_); // [確認_正常系] - RECIEVER が起動すること。
    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "受信待機中", 5000)); // [手順] - RECIEVER が "受信待機中" を出力するまで待機する。
    // [確認_正常系] - RECIEVER が "受信待機中" を出力すること。

    // SENDER を起動してプロンプトを待つ
    send_h_ = startProcessAsync(send_path, {"sender", config_path, "10"}, makeOpts()); // [手順] - SENDER を起動する。
    ASSERT_NE(nullptr, send_h_); // [確認_正常系] - SENDER が起動すること。
    ASSERT_NO_THROW(waitForOutput(
        send_h_, "porter-test[sender:", 5000)); // [手順] - SENDER が送信方法選択プロンプトを出力するまで待機する。
    // [確認_正常系] - SENDER が "porter-test[sender:" を出力すること。

    // Pre-Assert

    // Act
    ASSERT_TRUE(writeLineStdin(send_h_, "send Hello Text"));
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));

    ASSERT_NO_THROW(
        waitForOutput(recv_h_, "Hello Text", 3000)); // [手順] - RECIEVER が "Hello Text" を出力するまで待機する。

    writeLineStdin(send_h_, "exit");

    int send_exit = waitForExit(send_h_, 5000); // [手順] - SENDER が終了するまで待機する。

    // RECIEVER を停止して出力を回収する
    interruptProcess(recv_h_);  // [手順] - RECIEVER に SIGINT (Ctrl + C) を入力する。
    waitForExit(recv_h_, 3000); // [手順] - RECIEVER が終了するまで待機する。

    // Assert
    string recv_out = getStdout(recv_h_);
    EXPECT_EQ(0, send_exit); // [確認_正常系] - waitForExit の戻り値として、SENDER の終了コードが 0 であること。
    EXPECT_NE(
        string::npos,
        recv_out.find("Hello Text")); // [確認_正常系] - RECIEVER が "Hello Text" をテキストとして表示していること。
    EXPECT_EQ(
        string::npos,
        recv_out.find(
            "バイナリ データを保存しました")); // [確認_正常系] - RECIEVER がバイナリ保存メッセージを出力していないこと。
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
    ASSERT_FALSE(large_path.empty()); // [確認_正常系] - ファイルが作成されること。

    // SENDER を起動してプロンプトを待つ (RECIEVER は不要: 送信されないため)
    send_h_ = startProcessAsync(send_path, {"sender", config_path, "10"}, makeOpts()); // [手順] - SENDER を起動する。
    ASSERT_NE(nullptr, send_h_); // [確認_正常系] - SENDER が起動すること。
    ASSERT_NO_THROW(waitForOutput(
        send_h_, "porter-test[sender:", 5000)); // [手順] - SENDER が送信方法選択プロンプトを出力するまで待機する。

    // Pre-Assert

    // Act
    ASSERT_TRUE(writeLineStdin(send_h_, string("file ") + large_path));
    ASSERT_NO_THROW(waitForOutput(send_h_, "porter-test[sender:", 3000));
    // [確認_異常系] - SENDER がエラー後も対話を継続していること。

    writeLineStdin(send_h_, "exit");

    int send_exit = waitForExit(send_h_, 5000); // [手順] - SENDER が終了するまで待機する。

    // Assert
    string send_err = getStderr(send_h_);
    EXPECT_EQ(
        0,
        send_exit); // [確認_正常系] - waitForExit の戻り値として、SENDER の終了コードが 0 であり、エラーがループ内で処理されること。
    EXPECT_NE(
        string::npos,
        send_err.find("最大送信サイズ")); // [確認_異常系] - SENDER の stderr にサイズ超過エラーが出力されていること。
}
