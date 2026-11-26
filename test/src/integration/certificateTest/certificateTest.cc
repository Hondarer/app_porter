#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
    #include <cplat/base/windows_sdk.h>
#elif defined(PLATFORM_LINUX)
    #include <netinet/in.h>
    #include <sys/socket.h>
#endif
#include <testfw.h>
#include <mock_openssl.h>
#include <porter.h>
#include <porter_test_trace.h>
#include <porter/potr_context.h>
#include <porter/potr_flow_control.h>
#include <porter/potr_handle.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/packet_seal.h>
#include <cplat/crt/string.h>
#include <cplat/runtime/memory_lock.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include <ostream>

namespace
{
struct certificate_case
{
    potr_type type;
    int trust;
};

void PrintTo(const certificate_case &value, std::ostream *output)
{
    *output << "type: " << (int)value.type << ", trust: " << value.trust;
}

std::vector<certificate_case> certificate_cases()
{
    std::vector<certificate_case> cases;
    for (potr_type type : {POTR_TYPE_UNICAST_BIDIR, POTR_TYPE_UNICAST_BIDIR_N1,
        POTR_TYPE_TCP, POTR_TYPE_TCP_BIDIR, POTR_TYPE_UNICAST, POTR_TYPE_MULTICAST,
        POTR_TYPE_UNICAST_RAW, POTR_TYPE_MULTICAST_RAW})
    {
        for (int trust : {0, 1, 2})
        {
            cases.push_back({type, trust});
        }
    }
    return cases;
}

std::atomic<unsigned> delivered{0U};
std::atomic<potr_peer_id> connected_peer{POTR_PEER_NA};

void receive_data(int64_t, potr_peer_id peer, potr_event event, const void *data, size_t length)
{
    if (event == POTR_EVENT_CONNECTED)
    {
        connected_peer.store(peer);
    }
    if (event == POTR_EVENT_DATA && length == 4U && std::memcmp(data, "cert", 4U) == 0)
    {
        delivered.fetch_add(1U);
    }
}

struct service_handle
{
    potr_context *value = nullptr;
    ~service_handle()
    {
        if (value != nullptr)
        {
            (void)potr_service_close(value);
        }
    }
};

struct certificate_configuration
{
    std::string certificate;
    std::string key;
    std::string authority;
    std::string pin;
    std::array<const char *, 1> pins{};
    std::array<const char *, 2> names{"porter.test", "urn:porter:peer"};
    potr_service_def definition{};

    certificate_configuration(potr_type type, int mode, const char *identity, const char *other, uint16_t dst_port,
                              uint16_t src_port)
    {
        auto source_directory = std::filesystem::path(__FILE__).parent_path();
        if (source_directory.empty())
        {
            source_directory = std::filesystem::current_path();
        }
        auto directory = source_directory.parent_path().parent_path() / "libporter" / "auth_test" / "data";
        certificate = (directory / (std::string(identity) + ".pem")).string();
        key = (directory / (std::string(identity) + "-key.pem")).string();
        authority = (directory / "ca.pem").string();
        pin = (directory / (std::string(other) + ".pem")).string();
        definition.auth = POTR_AUTH_CERT;
        definition.type = type;
        definition.service_id = 9361;
        definition.dst_port = dst_port;
        definition.src_port = src_port;
        definition.ttl = 1U;
        definition.max_peers = 4U;
        definition.reconnect_interval_ms = 100U;
        definition.connect_timeout_ms = 1000U;
        cplat_strcpy(definition.src_addr[0], sizeof(definition.src_addr[0]), "127.0.0.1");
        cplat_strcpy(definition.dst_addr[0], sizeof(definition.dst_addr[0]), "127.0.0.1");
        cplat_strcpy(definition.multicast_group, sizeof(definition.multicast_group), "239.255.86.72");
        definition.cert_file = certificate.c_str();
        definition.key_file = key.c_str();
        if (mode != 1)
        {
            definition.ca_file = authority.c_str();
            definition.peer_names = names.data();
            definition.peer_name_count = names.size();
        }
        if (mode != 0)
        {
            pins[0] = pin.c_str();
            definition.pinned_certs = pins.data();
            definition.pinned_cert_count = pins.size();
        }
    }
};

potr_global_config global_configuration()
{
    potr_global_config global{};
    global.window_size = 16U;
    global.max_payload = 1400U;
    global.max_message_size = 65536U;
    global.send_queue_depth = 16U;
    global.udp_health_interval_ms = 500U;
    global.udp_health_timeout_ms = 3000U;
    global.tcp_health_interval_ms = 100U;
    global.tcp_health_timeout_ms = 3000U;
    global.tcp_close_timeout_ms = 1000U;
    return global;
}

bool send_until_delivered(potr_context *handle, unsigned target, std::chrono::milliseconds duration)
{
    auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline)
    {
        (void)potr_service_send(handle, POTR_PEER_NA, "cert", 4U, 0);
        if (delivered.load() >= target)
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return delivered.load() >= target;
}
}

class certificateTest : public testing::TestWithParam<certificate_case> {};

// 通信種別と信頼設定の各組み合わせで相互認証後に DATA を配送することの確認
TEST_P(certificateTest, sends_data_after_mutual_authentication)
{
    // Arrange
    testing::NiceMock<Mock_openssl> mock_openssl;
    const auto type = GetParam().type;
    const auto mode = GetParam().trust;
    auto global = global_configuration();
    std::vector<uint16_t> ports;
    ASSERT_TRUE(allocateTestPorts(2U, ports)); // [状態] - 宛先と送信元のポートを allocateTestPorts() で 2 個払い出す。
    // [状態確認] - allocateTestPorts() の戻り値が true であること。
    ASSERT_TRUE(porter_test_start_error_trace()); // [状態] - porter のトレーサーの標準エラー出力を ERROR で開始する。
    // [状態確認] - porter_test_start_error_trace() の戻り値が true であること。
    certificate_configuration sender(type, mode, "server", "client", ports[0], ports[1]);
    certificate_configuration receiver(type, mode, "client", "server", ports[0], ports[1]);
    if (type == POTR_TYPE_UNICAST_BIDIR_N1)
    {
        sender.definition.type = POTR_TYPE_UNICAST_BIDIR;
    }
    service_handle receiving;
    service_handle sending;
    delivered.store(0U);
    connected_peer.store(POTR_PEER_NA);
    ASSERT_EQ(POTR_OK, potr_service_open(&global, &receiver.definition, POTR_ROLE_RECEIVER, receive_data,
        &receiving.value)); // [状態確認] - 受信側のサービスが開くこと。
    potr_recv_fn sender_callback = nullptr;
    if (sender.definition.type == POTR_TYPE_UNICAST_BIDIR || sender.definition.type == POTR_TYPE_TCP_BIDIR)
    {
        sender_callback = receive_data;
    }
    ASSERT_EQ(POTR_OK, potr_service_open(&global, &sender.definition, POTR_ROLE_SENDER, sender_callback,
        &sending.value)); // [状態確認] - 送信側のサービスが開くこと。

    // Pre-Assert
    // Act
    bool actual_delivered = send_until_delivered(sending.value, 1U, std::chrono::seconds(10)); // [手順] - 認証と経路の確立を待ちながら DATA を送る。

    // Assert
    EXPECT_TRUE(actual_delivered); // [確認_正常系] - 証明書方式の DATA が受信コールバックに届くこと。
}

INSTANTIATE_TEST_SUITE_P(transports_and_trust, certificateTest, testing::ValuesIn(certificate_cases()));

// 信頼されない証明書を持つ相手とは通信を確立しないことの確認
TEST(certificate_rejection_test, rejects_untrusted_authority)
{
    // Arrange
    testing::NiceMock<Mock_openssl> mock_openssl;
    auto global = global_configuration();
    std::vector<uint16_t> ports;
    ASSERT_TRUE(allocateTestPorts(2U, ports)); // [状態] - 宛先と送信元のポートを allocateTestPorts() で 2 個払い出す。
    // [状態確認] - allocateTestPorts() の戻り値が true であること。
    ASSERT_TRUE(porter_test_start_error_trace()); // [状態] - porter のトレーサーの標準エラー出力を ERROR で開始する。
    // [状態確認] - porter_test_start_error_trace() の戻り値が true であること。
    certificate_configuration sender(POTR_TYPE_UNICAST_BIDIR, 0, "foreign", "client", ports[0], ports[1]);
    certificate_configuration receiver(POTR_TYPE_UNICAST_BIDIR, 0, "client", "foreign", ports[0], ports[1]);
    service_handle receiving;
    service_handle sending;
    delivered.store(0U);
    ASSERT_EQ(POTR_OK, potr_service_open(&global, &receiver.definition, POTR_ROLE_RECEIVER, receive_data,
        &receiving.value)); // [状態確認] - 受信側のサービスが開くこと。
    ASSERT_EQ(POTR_OK, potr_service_open(&global, &sender.definition, POTR_ROLE_SENDER, receive_data,
        &sending.value)); // [状態確認] - 信頼しない CA の証明書を持つサービスが開くこと。

    // Pre-Assert
    // Act
    bool actual_delivered = send_until_delivered(sending.value, 1U, std::chrono::seconds(3)); // [手順] - 認証を再試行する間も DATA を送信できないことを調べる。

    // Assert
    EXPECT_FALSE(actual_delivered); // [確認_異常系] - 相互認証に失敗した相手の DATA が届かないこと。
}

// 認証設定、片方向の送信元ポートと証明書ファイルを open で検査することの確認
TEST(certificate_open_test, validates_credentials_and_oneway_source_port)
{
    // Arrange
    testing::NiceMock<Mock_openssl> mock_openssl;
    auto global = global_configuration();
    std::vector<uint16_t> ports;
    ASSERT_TRUE(allocateTestPorts(8U, ports)); // [状態] - 4 つの設定の宛先と送信元のポートを allocateTestPorts() で 8 個払い出す。
    // [状態確認] - allocateTestPorts() の戻り値が true であること。
    ASSERT_TRUE(porter_test_start_error_trace()); // [状態] - porter のトレーサーの標準エラー出力を ERROR で開始する。
    // [状態確認] - porter_test_start_error_trace() の戻り値が true であること。
    certificate_configuration configuration(POTR_TYPE_MULTICAST, 0, "server", "client", ports[0], ports[1]);
    certificate_configuration unicast_receiver(POTR_TYPE_UNICAST, 0, "client", "server", ports[2], ports[3]);
    certificate_configuration unicast_raw_receiver(POTR_TYPE_UNICAST_RAW, 0, "client", "server", ports[4], ports[5]);
    std::array<potr_service_def, 10> invalid{};
    invalid.fill(configuration.definition);
    int invalid_auth = 7;
    std::memcpy(&invalid[0].auth, &invalid_auth, sizeof(invalid[0].auth));
    invalid[1].cert_file = nullptr;
    invalid[2].key_file = nullptr;
    invalid[3].ca_file = nullptr;
    invalid[4].peer_names = nullptr;
    invalid[5].peer_name_count = 0U;
    invalid[6].src_port = 0U;
    invalid[7].pinned_cert_count = 1U;
    invalid[8].type = POTR_TYPE_UNICAST;
    invalid[8].src_port = 0U;
    invalid[9].type = POTR_TYPE_UNICAST_RAW;
    invalid[9].src_port = 0U;
    unicast_receiver.definition.src_port = 0U;
    unicast_raw_receiver.definition.src_port = 0U;
    std::array<int, 10> results{};
    std::string absent = configuration.certificate + ".absent";
    certificate_configuration mismatched_key_cfg(POTR_TYPE_UNICAST, 0, "server", "client", ports[6], ports[7]);
    mismatched_key_cfg.definition.key_file = unicast_receiver.key.c_str();
    service_handle missing;
    service_handle mismatch;
    service_handle receiving_unicast;
    service_handle receiving_unicast_raw;
    configuration.definition.cert_file = absent.c_str();

    // Pre-Assert
    // Act
    for (size_t i = 0U; i < invalid.size(); i++)
    {
        service_handle handle;
        results[i] = potr_service_open(&global, &invalid[i], POTR_ROLE_SENDER, nullptr,
            &handle.value); // [手順 回数=10] - 不正な認証方式または組み合わせを open へ渡す。
    }
    int missing_result = potr_service_open(&global, &configuration.definition, POTR_ROLE_SENDER, nullptr,
        &missing.value); // [手順] - 存在しない証明書ファイルを渡す。
    int mismatch_result = potr_service_open(&global, &mismatched_key_cfg.definition, POTR_ROLE_SENDER, nullptr,
        &mismatch.value); // [手順] - 証明書と対応しない秘密鍵を open へ渡す。
    int unicast_receiver_result = potr_service_open(&global, &unicast_receiver.definition, POTR_ROLE_RECEIVER,
        receive_data, &receiving_unicast.value); // [手順] - src_port が 0 の片方向 unicast 受信者を open へ渡す。
    int unicast_raw_receiver_result = potr_service_open(&global, &unicast_raw_receiver.definition,
        POTR_ROLE_RECEIVER, receive_data,
        &receiving_unicast_raw.value); // [手順] - src_port が 0 の片方向 unicast_raw 受信者を open へ渡す。

    // Assert
    for (int result : results)
    {
        EXPECT_EQ(POTR_ERR_INVALID_CONFIG, result); // [確認_異常系 回数=10] - 不正な設定を設定エラーで拒否すること。
    }
    EXPECT_EQ(POTR_ERR_IO, missing_result); // [確認_異常系] - 証明書の読み込み失敗を open で返すこと。
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, mismatch_result); // [確認_異常系] - 証明書と秘密鍵の不一致を POTR_ERR_INVALID_CONFIG で返すこと。
    EXPECT_EQ(POTR_OK, unicast_receiver_result); // [確認_正常系] - src_port が 0 の片方向 unicast 受信者を開けること。
    EXPECT_EQ(POTR_OK, unicast_raw_receiver_result); // [確認_正常系] - src_port が 0 の片方向 unicast_raw 受信者を開けること。
}

// 公開定義のポインターを保持せずに認証情報を open 中に読み込むことの確認
TEST(certificate_open_test, retains_credentials_without_configuration_pointers)
{
    // Arrange
    testing::NiceMock<Mock_openssl> mock_openssl;
    auto global = global_configuration();
    std::vector<uint16_t> ports;
    ASSERT_TRUE(allocateTestPorts(2U, ports)); // [状態] - 宛先と送信元のポートを allocateTestPorts() で 2 個払い出す。
    // [状態確認] - allocateTestPorts() の戻り値が true であること。
    ASSERT_TRUE(porter_test_start_error_trace()); // [状態] - porter のトレーサーの標準エラー出力を ERROR で開始する。
    // [状態確認] - porter_test_start_error_trace() の戻り値が true であること。
    certificate_configuration configuration(POTR_TYPE_UNICAST, 2, "server", "client", ports[0], ports[1]);
    service_handle service;
    potr_context *context = nullptr;
    ASSERT_EQ(POTR_OK, potr_service_open(&global, &configuration.definition, POTR_ROLE_SENDER, nullptr,
        &service.value)); // [状態確認] - 証明書方式でサービスが開くこと。

    // Pre-Assert
    // Act
    int actual_ret = potr_internal_handle_acquire(service.value, &context); // [手順] - 保持された定義と認証情報を参照する。

    // Assert
    ASSERT_EQ(POTR_OK, actual_ret); // [確認_正常系] - コンテキストを参照できること。
    EXPECT_NE(nullptr, context->credentials); // [確認_正常系] - 読み込んだ認証情報を保持すること。
    EXPECT_EQ(nullptr, context->service.cert_file); // [確認_正常系] - 証明書パスを保持しないこと。
    EXPECT_EQ(nullptr, context->service.key_file); // [確認_正常系] - 秘密鍵パスを保持しないこと。
    EXPECT_EQ(nullptr, context->service.ca_file); // [確認_正常系] - CA パスを保持しないこと。
    EXPECT_EQ(nullptr, context->service.crl_file); // [確認_正常系] - CRL パスを保持しないこと。
    EXPECT_EQ(nullptr, context->service.pinned_certs); // [確認_正常系] - 登録証明書の配列を保持しないこと。
    EXPECT_EQ(nullptr, context->service.peer_names); // [確認_正常系] - 名前の配列を保持しないこと。

    // Cleanup
    potr_internal_handle_release(context);
}

// 認証に失敗した受信者が存在しても他の受信者へ配送できることの確認
TEST(certificate_group_test, isolates_receiver_authentication_failure)
{
    // Arrange
    testing::NiceMock<Mock_openssl> mock_openssl;
    auto global = global_configuration();
    std::vector<uint16_t> ports;
    ASSERT_TRUE(allocateTestPorts(3U, ports)); // [状態] - 宛先、送信元、信頼しない受信者の返送用のポートを allocateTestPorts() で 3 個払い出す。
    // [状態確認] - allocateTestPorts() の戻り値が true であること。
    ASSERT_TRUE(porter_test_start_error_trace()); // [状態] - porter のトレーサーの標準エラー出力を ERROR で開始する。
    // [状態確認] - porter_test_start_error_trace() の戻り値が true であること。
    certificate_configuration sender(POTR_TYPE_MULTICAST, 0, "server", "client", ports[0], ports[1]);
    certificate_configuration receiver(POTR_TYPE_MULTICAST, 0, "client", "server", ports[0], ports[1]);
    certificate_configuration untrusted(POTR_TYPE_MULTICAST, 0, "foreign", "server", ports[0], ports[1]);
    untrusted.definition.dst_port = ports[2];
    service_handle receiving;
    service_handle rejecting;
    service_handle sending;
    potr_context *rejected_context = nullptr;
    delivered.store(0U);
    ASSERT_EQ(POTR_OK, potr_service_open(&global, &receiver.definition, POTR_ROLE_RECEIVER, receive_data,
        &receiving.value)); // [状態確認] - 正規の受信者を開始する。
    ASSERT_EQ(POTR_OK, potr_service_open(&global, &untrusted.definition, POTR_ROLE_RECEIVER, receive_data,
        &rejecting.value)); // [状態確認] - 別の返送用ポートから信頼しない証明書で認証する受信者を開始する。
    ASSERT_EQ(POTR_OK, potr_service_open(&global, &sender.definition, POTR_ROLE_SENDER, nullptr,
        &sending.value)); // [状態確認] - 両受信者が認証を開始する送信者を開く。

    // Pre-Assert
    // Act
    bool actual_delivered = send_until_delivered(sending.value, 1U, std::chrono::seconds(10)); // [手順] - 不正な受信者の認証と並行してグループ DATA を送信する。
    ASSERT_EQ(POTR_OK, potr_internal_handle_acquire(rejecting.value,
        &rejected_context)); // [状態確認] - 不正な受信者の鍵の状態を参照する。
    cplat_local_lock_lock(rejected_context->certificate_mutex);
    int rejected_established = rejected_context->certificate.established;
    int rejected_group_key = rejected_context->group_key_known;
    cplat_local_lock_unlock(rejected_context->certificate_mutex);
    potr_internal_handle_release(rejected_context);

    // Assert
    EXPECT_TRUE(actual_delivered); // [確認_正常系] - 正規の受信者がグループ DATA を復号して受信すること。
    EXPECT_EQ(0, rejected_established); // [確認_異常系] - 不正な受信者の認証が確立しないこと。
    EXPECT_EQ(0, rejected_group_key); // [確認_異常系] - 不正な受信者がグループ鍵を取得しないこと。
}

extern "C"
{
#include "../../../../prod/libsrc/porter/thread/thread_recv_validate.h"
}

// 相互認証の前には通常パケットと KEY が通信状態を変更しないことの確認
TEST(certificate_packet_test, preserves_state_before_authentication)
{
    // Arrange
    testing::NiceMock<Mock_openssl> mock_openssl;
    std::vector<uint16_t> ports;
    ASSERT_TRUE(allocateTestPorts(2U, ports)); // [状態] - 宛先と送信元のポートを allocateTestPorts() で 2 個払い出す。
    // [状態確認] - allocateTestPorts() の戻り値が true であること。
    certificate_configuration configuration(POTR_TYPE_MULTICAST, 0, "client", "server", ports[0], ports[1]);
    potr_context context{};
    context.service = configuration.definition;
    context.global = global_configuration();
    context.role = POTR_ROLE_RECEIVER;
    context.n_path = 1;
    ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&context.callback_mutex)); // [状態確認] - 通信状態の保護用ロックを用意する。
    ASSERT_EQ(POTR_OK, potr_internal_certificate_initialize(&context, &configuration.definition)); // [状態確認] - 認証情報を読み込み、ハンドシェイクは開始しない。
    std::array<uint8_t, POTR_PACKET_HEADER_SIZE> wire{};
    std::array<uint16_t, 7> flags{POTR_FLAG_DATA, POTR_FLAG_PING, POTR_FLAG_REPORT,
        POTR_FLAG_NACK, POTR_FLAG_REJECT, POTR_FLAG_FIN, POTR_FLAG_FIN_ACK};
    std::array<int, 7> results{};
    potr_packet packet{};
    cplat_ipv4_endpoint sender{};
    context.recv_window.next_seq = 81U;
    context.replay_window.highest = 91U;
    context.path_logical_alive[0] = 1;

    // Pre-Assert
    // Act
    for (size_t i = 0U; i < flags.size(); i++)
    {
        packet.flags = (uint16_t)(flags[i] | POTR_FLAG_ENCRYPTED);
        results[i] = thread_recv_authenticate_packet(&context, &packet, wire.data(), "test", 0); // [手順 回数=7] - 相互認証の前に暗号化フラグ付きの通常パケットを入力する。
    }
    packet.flags = POTR_FLAG_KEY | POTR_FLAG_ENCRYPTED;
    (void)potr_internal_certificate_receive(&context, &packet, wire.data(), &sender, 0); // [手順] - ハンドシェイク前に KEY を入力する。
    packet.flags = POTR_FLAG_DATA;
    int plaintext_result = thread_recv_authenticate_packet(&context, &packet, wire.data(), "test", 0); // [手順] - 平文の DATA を入力する。

    // Assert
    for (int result : results)
    {
        EXPECT_EQ(POTR_ERR_DISCONNECTED, result); // [確認_異常系 回数=7] - 未確立のパケットを拒否すること。
    }
    EXPECT_EQ(POTR_ERR_PROTOCOL, plaintext_result); // [確認_異常系] - 証明書方式で平文を拒否すること。
    EXPECT_EQ(81U, context.recv_window.next_seq); // [確認_異常系] - 受信ウィンドウを変更しないこと。
    EXPECT_EQ(91U, context.replay_window.highest); // [確認_異常系] - リプレイ窓を変更しないこと。
    EXPECT_EQ(1, context.path_logical_alive[0]); // [確認_異常系] - 経路状態を変更しないこと。
    EXPECT_EQ(0, context.peer_session_known); // [確認_異常系] - 相手セッションを採用しないこと。
    EXPECT_EQ(0, context.group_key_known); // [確認_異常系] - KEY を採用しないこと。

    // Cleanup
    potr_internal_certificate_dispose(&context);
    cplat_local_lock_dispose(context.callback_mutex);
}

// 事前共有鍵方式では証明書方式の制御パケットを処理しないことの確認
TEST(certificate_packet_test, pre_shared_key_rejects_certificate_control_packets)
{
    // Arrange
    potr_context context{};
    context.n_path = 1;
    potr_packet packet{};
    cplat_ipv4_endpoint sender{};
    std::array<uint8_t, POTR_PACKET_HEADER_SIZE> wire{};

    // Pre-Assert
    // Act
    packet.flags = POTR_FLAG_HANDSHAKE;
    int handshake_result = potr_internal_certificate_receive(&context, &packet, wire.data(), &sender, 0); // [手順] - 事前共有鍵方式へ HANDSHAKE を入力する。
    packet.flags = POTR_FLAG_KEY | POTR_FLAG_ENCRYPTED;
    int key_result = potr_internal_certificate_receive(&context, &packet, wire.data(), &sender, 0); // [手順] - 事前共有鍵方式へ KEY を入力する。

    // Assert
    EXPECT_EQ(POTR_OK, handshake_result); // [確認_異常系] - HANDSHAKE を通常受信へ渡さず消費すること。
    EXPECT_EQ(POTR_OK, key_result); // [確認_異常系] - KEY を通常受信へ渡さず消費すること。
    EXPECT_EQ(nullptr, context.certificate.session); // [確認_異常系] - 認証セッションを作らないこと。
    EXPECT_EQ(0, context.peer_session_known); // [確認_異常系] - 相手セッションを変更しないこと。
}

// グループ鍵を持たない受信者がハンドシェイクを再試行して KEY を取得することの確認
TEST(certificate_group_test, retries_handshake_until_group_key_is_available)
{
    // Arrange
    testing::NiceMock<Mock_openssl> mock_openssl;
    auto global = global_configuration();
    global.window_size = 128U;
    std::vector<uint16_t> ports;
    ASSERT_TRUE(allocateTestPorts(2U, ports)); // [状態] - 宛先と送信元のポートを allocateTestPorts() で 2 個払い出す。
    // [状態確認] - allocateTestPorts() の戻り値が true であること。
    ASSERT_TRUE(porter_test_start_error_trace()); // [状態] - porter のトレーサーの標準エラー出力を ERROR で開始する。
    // [状態確認] - porter_test_start_error_trace() の戻り値が true であること。
    certificate_configuration sender(POTR_TYPE_MULTICAST, 0, "server", "client", ports[0], ports[1]);
    certificate_configuration receiver(POTR_TYPE_MULTICAST, 0, "client", "server", ports[0], ports[1]);
    service_handle receiving;
    service_handle sending;
    potr_context *context = nullptr;
    delivered.store(0U);
    ASSERT_EQ(POTR_OK, potr_service_open(&global, &receiver.definition, POTR_ROLE_RECEIVER, receive_data,
        &receiving.value)); // [状態確認] - 受信者を開始する。
    ASSERT_EQ(POTR_OK, potr_service_open(&global, &sender.definition, POTR_ROLE_SENDER, nullptr,
        &sending.value)); // [状態確認] - 送信者を開始する。
    ASSERT_TRUE(send_until_delivered(sending.value, 1U, std::chrono::seconds(10))); // [状態確認] - グループ鍵を取得して DATA を受信する。
    ASSERT_EQ(POTR_OK, potr_internal_handle_acquire(receiving.value, &context)); // [状態確認] - 受信者のグループ鍵を参照する。
    cplat_local_lock_lock(context->certificate_mutex);
    context->group_key_known = 0;
    cplat_secure_zero(context->group_key, sizeof(context->group_key)); // [状態] - KEY が届かずグループ鍵を持たない状態にする。
    cplat_local_lock_unlock(context->certificate_mutex);
    potr_internal_handle_release(context);
    unsigned target = delivered.load() + 1U;

    // Pre-Assert
    // Act
    bool actual_delivered = send_until_delivered(sending.value, target, std::chrono::seconds(10)); // [手順] - 周期的なハンドシェイクの再試行と KEY の再送を待つ。

    // Assert
    EXPECT_TRUE(actual_delivered); // [確認_正常系] - KEY を再取得して DATA の復号を再開すること。
}

// 証明書方式の multicast で監視対象の一覧をグループ鍵で封緘・検証して受理し、別のグループ鍵の一覧を破棄することの確認
TEST(certificate_packet_test, monitor_list_sealed_with_group_key_is_verified_and_wrong_key_is_rejected)
{
    // Arrange
    testing::NiceMock<Mock_openssl> mock_openssl;
    potr_context sender_ctx{};
    potr_context wrong_sender_ctx{};
    potr_context receiver_ctx{};
    potr_internal_receiver sender_receiver{};
    potr_internal_receiver wrong_sender_receiver{};
    std::array<uint8_t, POTR_CRYPTO_KEY_SIZE> group_key{};
    std::array<uint8_t, POTR_CRYPTO_KEY_SIZE> wrong_group_key{};
    group_key.fill(0x5AU);
    wrong_group_key.fill(0xA5U);
    constexpr int64_t kServiceId = 41;
    constexpr int64_t kSenderSec = 500;
    constexpr int32_t kSenderNsec = 100;
    constexpr uint32_t kSenderSessionId = 1001U;
    constexpr int64_t kReceiverSec = 600;
    constexpr int32_t kReceiverNsec = 200;
    constexpr uint32_t kReceiverSessionId = 2002U;
    sender_ctx.service.type = POTR_TYPE_MULTICAST;
    sender_ctx.service.auth = POTR_AUTH_CERT;
    sender_ctx.service.service_id = kServiceId;
    sender_ctx.role = POTR_ROLE_SENDER;
    sender_ctx.n_path = 1;
    sender_ctx.session_id = kSenderSessionId;
    sender_ctx.session_ts.tv_sec = kSenderSec;
    sender_ctx.session_ts.tv_nsec = kSenderNsec;
    sender_ctx.group_key_known = 1;
    std::memcpy(sender_ctx.group_key, group_key.data(), POTR_CRYPTO_KEY_SIZE);
    sender_receiver.certificate.established = 1;
    sender_ctx.receivers = &sender_receiver;
    potr_internal_tx_session_init(&sender_ctx.tx_session);
    ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&sender_ctx.certificate_mutex));
    // [状態確認] - 正しい送信者の証明書保護ロックを生成できること。
    ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&sender_ctx.callback_mutex));
    // [状態確認] - 正しい送信者の状態保護ロックを生成できること。

    wrong_sender_ctx.service.type = POTR_TYPE_MULTICAST;
    wrong_sender_ctx.service.auth = POTR_AUTH_CERT;
    wrong_sender_ctx.service.service_id = kServiceId;
    wrong_sender_ctx.role = POTR_ROLE_SENDER;
    wrong_sender_ctx.n_path = 1;
    wrong_sender_ctx.session_id = kSenderSessionId;
    wrong_sender_ctx.session_ts.tv_sec = kSenderSec;
    wrong_sender_ctx.session_ts.tv_nsec = kSenderNsec;
    wrong_sender_ctx.group_key_known = 1;
    std::memcpy(wrong_sender_ctx.group_key, wrong_group_key.data(), POTR_CRYPTO_KEY_SIZE);
    wrong_sender_receiver.certificate.established = 1;
    wrong_sender_ctx.receivers = &wrong_sender_receiver;
    potr_internal_tx_session_init(&wrong_sender_ctx.tx_session);
    ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&wrong_sender_ctx.certificate_mutex));
    // [状態確認] - 別鍵の送信者の証明書保護ロックを生成できること。
    ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&wrong_sender_ctx.callback_mutex));
    // [状態確認] - 別鍵の送信者の状態保護ロックを生成できること。

    std::array<uint8_t, POTR_FLOW_CONTROL_MONITOR_PAYLOAD_MAX + POTR_CRYPTO_TAG_SIZE> recv_crypto_buf{};
    receiver_ctx.service.type = POTR_TYPE_MULTICAST;
    receiver_ctx.service.auth = POTR_AUTH_CERT;
    receiver_ctx.service.service_id = kServiceId;
    receiver_ctx.role = POTR_ROLE_RECEIVER;
    receiver_ctx.n_path = 1;
    receiver_ctx.session_id = kReceiverSessionId;
    receiver_ctx.session_ts.tv_sec = kReceiverSec;
    receiver_ctx.session_ts.tv_nsec = kReceiverNsec;
    receiver_ctx.peer_session_known = 1;
    receiver_ctx.peer_session_id = kSenderSessionId;
    receiver_ctx.peer_session_ts.tv_sec = kSenderSec;
    receiver_ctx.peer_session_ts.tv_nsec = kSenderNsec;
    receiver_ctx.group_key_known = 1;
    std::memcpy(receiver_ctx.group_key, group_key.data(), POTR_CRYPTO_KEY_SIZE);
    receiver_ctx.certificate.established = 1;
    receiver_ctx.certificate.peer_known = 1;
    {
        uint64_t sec_val = (uint64_t)kSenderSec;
        for (size_t i = 0U; i < 8U; i++)
        {
            receiver_ctx.certificate.peer_identity[i] = (uint8_t)(sec_val >> ((7U - i) * 8U));
        }
        uint32_t sid_be = cplat_hton32(kSenderSessionId);
        uint32_t nsec_be = cplat_hton32((uint32_t)kSenderNsec);
        std::memcpy(receiver_ctx.certificate.peer_identity + 8U, &sid_be, sizeof(sid_be));
        std::memcpy(receiver_ctx.certificate.peer_identity + 12U, &nsec_be, sizeof(nsec_be));
    }
    receiver_ctx.recv_crypto_buf = recv_crypto_buf.data();
    receiver_ctx.crypto_buf_size = recv_crypto_buf.size();
    receiver_ctx.global.max_payload = 100U;
    receiver_ctx.flow_control_config.capacity = 1000U;
    receiver_ctx.flow_control_config.suppress_percent = 80U;
    receiver_ctx.flow_control_config.resume_percent = 60U;
    receiver_ctx.flow_control_config.request_interval_ms = 200U;
    receiver_ctx.flow_control_config.request_lifetime_ms = 1000U;
    ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&receiver_ctx.certificate_mutex));
    // [状態確認] - 受信者の証明書保護ロックを生成できること。
    ASSERT_EQ(CPLAT_OK, cplat_local_lock_create(&receiver_ctx.callback_mutex));
    // [状態確認] - 受信者の状態保護ロックを生成できること。
    ASSERT_EQ(POTR_OK, potr_internal_flow_control_start(&receiver_ctx));
    // [状態確認] - 受信者のフロー制御資源を開始できること。

    potr_internal_packet_session_hdr sender_hdr{};
    sender_hdr.service_id = kServiceId;
    sender_hdr.session_tv_sec = kSenderSec;
    sender_hdr.session_tv_nsec = kSenderNsec;
    sender_hdr.session_id = kSenderSessionId;
    potr_internal_packet_session_hdr receiver_entry{};
    receiver_entry.service_id = kServiceId;
    receiver_entry.session_tv_sec = kReceiverSec;
    receiver_entry.session_tv_nsec = kReceiverNsec;
    receiver_entry.session_id = kReceiverSessionId;
    std::array<uint8_t, POTR_FLOW_CONTROL_MONITOR_PAYLOAD_MAX> payload_buf{};
    potr_packet list_pkt{};
    ASSERT_EQ(POTR_OK,
              potr_internal_packet_build_flow_control_list(&list_pkt, &sender_hdr, payload_buf.data(), &receiver_entry,
                                                           1U));
    // [状態確認] - 受信者自身を含む監視対象の一覧パケットを組み立てられること。
    std::array<uint8_t, POTR_PACKET_HEADER_SIZE + POTR_FLOW_CONTROL_MONITOR_PAYLOAD_MAX + POTR_CRYPTO_TAG_SIZE>
        wrong_wire{};
    std::array<uint8_t, POTR_PACKET_HEADER_SIZE + POTR_FLOW_CONTROL_MONITOR_PAYLOAD_MAX + POTR_CRYPTO_TAG_SIZE>
        valid_wire{};
    size_t wrong_wire_len = wrong_wire.size();
    size_t valid_wire_len = valid_wire.size();
    potr_packet parsed_wrong{};
    potr_packet parsed_valid{};
    // [状態] - 証明書方式 multicast で同じグループ鍵を持つ送信者・受信者と、別のグループ鍵を持つ送信者を用意する。

    // Pre-Assert
    // Act
    int actual_wrong_seal =
        potr_internal_packet_seal_for_context(&wrong_sender_ctx, &wrong_sender_ctx.tx_session, &list_pkt,
                                              wrong_wire.data(), &wrong_wire_len);
    ASSERT_EQ(POTR_OK, potr_internal_packet_parse(&parsed_wrong, wrong_wire.data(), wrong_wire_len));
    // [状態確認] - 別のグループ鍵で封緘したパケットを解析できること。
    int actual_wrong_auth =
        thread_recv_authenticate_packet(&receiver_ctx, &parsed_wrong, wrong_wire.data(), "test", 0);
    if (actual_wrong_auth == POTR_OK)
    {
        potr_internal_flow_control_on_packet(&receiver_ctx, nullptr, &parsed_wrong);
    }
    int monitored_after_wrong = receiver_ctx.flow_monitored;
    // [手順] - 別のグループ鍵で封緘した監視対象の一覧を受信者で検証する。

    int actual_valid_seal =
        potr_internal_packet_seal_for_context(&sender_ctx, &sender_ctx.tx_session, &list_pkt, valid_wire.data(),
                                              &valid_wire_len);
    ASSERT_EQ(POTR_OK, potr_internal_packet_parse(&parsed_valid, valid_wire.data(), valid_wire_len));
    // [状態確認] - 正しいグループ鍵で封緘したパケットを解析できること。
    int actual_valid_auth =
        thread_recv_authenticate_packet(&receiver_ctx, &parsed_valid, valid_wire.data(), "test", 0);
    if (actual_valid_auth == POTR_OK)
    {
        potr_internal_flow_control_on_packet(&receiver_ctx, nullptr, &parsed_valid);
    }
    int monitored_after_valid = receiver_ctx.flow_monitored;
    // [手順] - 正しいグループ鍵で封緘した監視対象の一覧を受信者で検証し、受理させる。

    // Assert
    EXPECT_EQ(POTR_OK, actual_wrong_seal); // [確認_正常系] - 別のグループ鍵でも封緘自体は成功すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual_wrong_auth);
    // [確認_異常系] - 別のグループ鍵で封緘した監視対象の一覧は検証に失敗して破棄されること。
    EXPECT_EQ(0, monitored_after_wrong);
    // [確認_異常系] - 検証失敗の一覧では監視対象として受理されないこと。
    EXPECT_EQ(POTR_OK, actual_valid_seal); // [確認_正常系] - 正しいグループ鍵で監視対象の一覧を封緘できること。
    EXPECT_EQ(POTR_OK, actual_valid_auth); // [確認_正常系] - 正しいグループ鍵で監視対象の一覧の検証に成功すること。
    EXPECT_EQ((uint16_t)POTR_FLAG_FLOW_CONTROL, parsed_valid.flags);
    // [確認_正常系] - 復号後に POTR_FLAG_ENCRYPTED が外れ、POTR_FLAG_FLOW_CONTROL が残ること。
    EXPECT_EQ(1, monitored_after_valid);
    // [確認_正常系] - 検証に成功した監視対象の一覧を受理し、監視対象として登録されること。

    // Cleanup
    potr_internal_flow_control_stop(&receiver_ctx);
    cplat_local_lock_dispose(receiver_ctx.callback_mutex);
    cplat_local_lock_dispose(receiver_ctx.certificate_mutex);
    cplat_local_lock_dispose(wrong_sender_ctx.callback_mutex);
    cplat_local_lock_dispose(wrong_sender_ctx.certificate_mutex);
    cplat_local_lock_dispose(sender_ctx.callback_mutex);
    cplat_local_lock_dispose(sender_ctx.certificate_mutex);
}

// 証明書方式 TCP のハンドシェイク途中でサービスを閉じても、待機が解除されて 1 秒以内に完了することの確認
TEST(certificateTest, tcp_stop_during_certificate_exchange_completes_quickly)
{
    testing::NiceMock<Mock_openssl> mock_openssl;
    potr_global_config global = global_configuration();
    global.tcp_close_timeout_ms = 500U;

    std::vector<uint16_t> ports;
    ASSERT_TRUE(allocateTestPorts(2U, ports)); // [状態確認] - allocateTestPorts() の戻り値が true であること。
    ASSERT_TRUE(porter_test_start_error_trace()); // [状態確認] - porter_test_start_error_trace() の戻り値が true であること。
    const uint16_t port = ports[0];
    cplat_error detail{};

    certificate_configuration receiver_cfg(POTR_TYPE_TCP, 0, "client", "server", port, ports[1]);
    potr_context *receiver = nullptr;
    ASSERT_EQ(POTR_OK, potr_service_open(&global, &receiver_cfg.definition, POTR_ROLE_RECEIVER, receive_data, &receiver));

    /* 相手側ソケットを接続したまま証明書ハンドシェイクを送らずに停止させる。 */
    cplat_socket client = CPLAT_INVALID_SOCKET;
    ASSERT_EQ(CPLAT_OK, cplat_socket_open(CPLAT_SOCKET_TCP, &client, &detail));
    cplat_ipv4_endpoint dst{};
    dst.address = cplat_hton32(0x7F000001U);
    dst.port = cplat_hton16(port);
    ASSERT_EQ(CPLAT_OK, cplat_socket_connect(client, &dst, &detail));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(POTR_OK, potr_service_close(receiver));
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    EXPECT_LT(elapsed_ms, 1000);
    cplat_socket_close(client);
}
