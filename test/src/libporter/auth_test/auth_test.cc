#include <testfw.h>
#include <mock_openssl.h>
#include <mock_cplat.h>
#include <mock_stdio.h>
#include <mock_string.h>
#include <porter/porter_result.h>
#include <porter/security/potr_auth.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <ostream>
#include <vector>

namespace
{
const char *const k_peer_names[] = {"porter.test", "urn:porter:peer"};

std::string data_path(const std::string &name)
{
    return (std::filesystem::path(__FILE__).parent_path() / "data" / name).string();
}

struct auth_configuration
{
    std::string certificate;
    std::string key;
    std::string authority = data_path("ca.pem");
    std::string pinned;
    const char *pin_paths[1] = {};
    potr_internal_auth_config config = {};

    auth_configuration(const char *identity, int mode, const char *peer_identity)
    {
        certificate = data_path(std::string(identity) + ".pem");
        key = data_path(std::string(identity) + "-key.pem");
        pinned = data_path(std::string(peer_identity) + ".pem");
        config.cert_file = certificate.c_str();
        config.key_file = key.c_str();
        if (mode != 1)
        {
            config.ca_file = authority.c_str();
            config.peer_names = k_peer_names;
            config.peer_name_count = 2;
        }
        if (mode != 0)
        {
            pin_paths[0] = pinned.c_str();
            config.pinned_certs = pin_paths;
            config.pinned_cert_count = 1;
        }
    }
};

struct exchange_report
{
    size_t largest_output = 0;
    size_t delivered = 0;
    int result = POTR_OK;
    potr_internal_auth_state client_state = POTR_INTERNAL_AUTH_STATE_IN_PROGRESS;
    potr_internal_auth_state server_state = POTR_INTERNAL_AUTH_STATE_IN_PROGRESS;
    int padding = 0;
};

struct auth_pair
{
    potr_internal_auth_credentials *client_credentials = nullptr;
    potr_internal_auth_credentials *server_credentials = nullptr;
    potr_internal_auth_session *client = nullptr;
    potr_internal_auth_session *server = nullptr;
    exchange_report report;

    // [サブ手順 名前=auth_pair.open]
    int open(const potr_internal_auth_config &client_config, const potr_internal_auth_config &server_config,
             potr_internal_auth_transport transport, size_t maximum = 512)
    {
        // [状態] - 両端の認証情報を読み込み、クライアントとサーバーの session を生成する。
        int actual_ret = potr_internal_auth_credentials_create(&client_config, transport, &client_credentials);
        if (actual_ret == POTR_OK)
        {
            actual_ret = potr_internal_auth_credentials_create(&server_config, transport, &server_credentials);
        }
        if (actual_ret == POTR_OK)
        {
            actual_ret = potr_internal_auth_session_create(client_credentials, POTR_INTERNAL_AUTH_ROLE_CLIENT, maximum, &client);
        }
        if (actual_ret == POTR_OK)
        {
            actual_ret = potr_internal_auth_session_create(server_credentials, POTR_INTERNAL_AUTH_ROLE_SERVER, maximum, &server);
        }
        return actual_ret;
    }

    // [サブ手順終了]

    // [サブ手順 名前=auth_pair.exchange]
    void transfer(potr_internal_auth_session *from, potr_internal_auth_session *to, potr_internal_auth_state &state)
    {
        std::array<uint8_t, 65536> output = {};
        for (size_t message = 0; message < 1000; ++message)
        {
            size_t length = 0;
            report.result = potr_internal_auth_session_take_output(from, output.data(), output.size(), &length);
            if (report.result != POTR_OK || length == 0)
            {
                return;
            }
            // windows.h の関数形式マクロ max に展開されないよう、関数名を括弧で囲む。
            report.largest_output = (std::max)(report.largest_output, length);
            ++report.delivered;
            report.result = potr_internal_auth_session_receive(to, output.data(), length, &state);
            if (report.result != POTR_OK || state == POTR_INTERNAL_AUTH_STATE_FAILED)
            {
                return;
            }
        }
        report.result = POTR_ERR_FULL;
    }

    void exchange()
    {
        // [手順] - 出力がなくなるまで相手に渡し、両端の確立または失敗まで相互認証を進める。
        report.result = potr_internal_auth_session_receive(client, nullptr, 0, &report.client_state);
        if (report.result != POTR_OK)
        {
            return;
        }
        for (size_t round = 0; round < 100; ++round)
        {
            size_t previous = report.delivered;
            transfer(client, server, report.server_state);
            if (report.result != POTR_OK || report.server_state == POTR_INTERNAL_AUTH_STATE_FAILED)
            {
                return;
            }
            transfer(server, client, report.client_state);
            if (report.result != POTR_OK || report.client_state == POTR_INTERNAL_AUTH_STATE_FAILED
                || previous == report.delivered
                || (report.client_state == POTR_INTERNAL_AUTH_STATE_ESTABLISHED
                    && report.server_state == POTR_INTERNAL_AUTH_STATE_ESTABLISHED))
            {
                return;
            }
        }
        report.result = POTR_ERR_TIMEOUT;
    }

    // [サブ手順終了]

    // [サブ手順 名前=auth_pair.cleanup]
    ~auth_pair()
    {
        // Cleanup: session が借用する credentials より先に session を解放する。
        potr_internal_auth_session_dispose(client);
        potr_internal_auth_session_dispose(server);
        potr_internal_auth_credentials_dispose(client_credentials);
        potr_internal_auth_credentials_dispose(server_credentials);
    }
    // [サブ手順終了]
};

struct auth_success_parameter
{
    potr_internal_auth_transport transport;
    int mode;
};

void PrintTo(const auth_success_parameter &parameter, std::ostream *stream);
void PrintTo(const auth_success_parameter &parameter, std::ostream *stream)
{
    *stream << "transport: " << parameter.transport << ", mode: " << parameter.mode;
}

std::vector<auth_success_parameter> success_parameters()
{
    std::vector<auth_success_parameter> parameters;
    for (auto transport : {POTR_INTERNAL_AUTH_TRANSPORT_DATAGRAM, POTR_INTERNAL_AUTH_TRANSPORT_STREAM})
    {
        for (int mode : {0, 1, 2})
        {
            parameters.push_back({transport, mode});
        }
    }
    return parameters;
}
class auth_success_test : public TestWithParam<auth_success_parameter> {};

// CA、直接登録、その両方の構成で DTLS / TLS の相互認証と鍵導出を確認する。
TEST_P(auth_success_test, mutual_authentication_and_export)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    auto [transport, mode] = GetParam();
    auth_configuration client("client", mode, "server");
    auth_configuration server("server", mode, "client");
    auth_pair pair;
    int open_ret = pair.open(client.config, server.config, transport); // [サブ手順参照 名前=auth_pair.open 回数=PARAM]
    ASSERT_EQ(POTR_OK, open_ret); // [状態確認] - pair.open() の戻り値が POTR_OK であること。
    std::array<uint8_t, POTR_CRYPTO_KEY_SIZE> client_send = {}, client_receive = {}, server_send = {}, server_receive = {};
    std::array<uint8_t, POTR_CRYPTO_KEY_SIZE> other_send = {}, other_receive = {};
    const uint8_t context[] = {1, 2, 3};
    const uint8_t other_context[] = {1, 2, 4};

    // Pre-Assert
    // Act
    pair.exchange(); // [サブ手順参照 名前=auth_pair.exchange 回数=PARAM]

    // Assert
    ASSERT_EQ(POTR_OK, pair.report.result); // [確認_正常系 回数=PARAM] - pair.report.result が POTR_OK であること。
    ASSERT_EQ(POTR_INTERNAL_AUTH_STATE_ESTABLISHED, pair.report.client_state); // [確認_正常系 回数=PARAM] - クライアントの状態が ESTABLISHED であること。
    ASSERT_EQ(POTR_INTERNAL_AUTH_STATE_ESTABLISHED, pair.report.server_state); // [確認_正常系 回数=PARAM] - サーバーの状態が ESTABLISHED であること。
    EXPECT_LE(pair.report.largest_output, 512U); // [確認_正常系 回数=PARAM] - 最大出力長が 512 以下であること。
    EXPECT_EQ(POTR_OK, potr_internal_auth_session_export_keys(pair.client, context, sizeof(context), client_send.data(), client_receive.data())); // [確認_正常系 回数=PARAM] - クライアントの export_keys() の戻り値が POTR_OK であること。
    EXPECT_EQ(POTR_OK, potr_internal_auth_session_export_keys(pair.server, context, sizeof(context), server_send.data(), server_receive.data())); // [確認_正常系 回数=PARAM] - サーバーの export_keys() の戻り値が POTR_OK であること。
    EXPECT_EQ(client_send, server_send); // [確認_正常系 回数=PARAM] - クライアントからサーバー方向の鍵が両端で一致すること。
    EXPECT_EQ(client_receive, server_receive); // [確認_正常系 回数=PARAM] - サーバーからクライアント方向の鍵が両端で一致すること。
    EXPECT_NE(client_send, client_receive); // [確認_正常系 回数=PARAM] - 2 方向の鍵が異なること。
    EXPECT_EQ(POTR_OK, potr_internal_auth_session_export_keys(pair.client, other_context, sizeof(other_context), other_send.data(), other_receive.data())); // [確認_正常系 回数=PARAM] - 異なる文脈の export_keys() の戻り値が POTR_OK であること。
    EXPECT_NE(client_send, other_send); // [確認_正常系 回数=PARAM] - 文脈を変えるとクライアントからサーバー方向の鍵が変わること。
    EXPECT_NE(client_receive, other_receive); // [確認_正常系 回数=PARAM] - 文脈を変えるとサーバーからクライアント方向の鍵が変わること。
    EXPECT_STREQ("", potr_internal_auth_session_failure_reason(pair.client)); // [確認_正常系 回数=PARAM] - 確立したクライアントの失敗理由が空文字列であること。
}
// [サブ手順参照 名前=auth_pair.cleanup 回数=PARAM]

INSTANTIATE_TEST_SUITE_P(transports_and_trust, auth_success_test,
    ValuesIn(success_parameters()));

struct rejection_case
{
    const char *identity;
    const char *name;
    const char *reason;
    int mode;
    bool use_crl;
    uint8_t padding[3] = {};

    rejection_case(const char *id, int m, const char *n, bool crl, const char *r)
        : identity(id), name(n), reason(r), mode(m), use_crl(crl)
    {
    }
};
struct rejection_parameter
{
    rejection_case rejection;
    potr_internal_auth_transport transport;
    bool reject_client;
    uint8_t padding[3] = {};

    rejection_parameter(potr_internal_auth_transport t, const rejection_case &r, bool rc)
        : rejection(r), transport(t), reject_client(rc)
    {
    }
};

void PrintTo(const rejection_parameter &parameter, std::ostream *stream);
void PrintTo(const rejection_parameter &parameter, std::ostream *stream)
{
    const rejection_case &rejection = parameter.rejection;
    *stream << "transport: " << parameter.transport << ", rejection: {identity: \"" << rejection.identity
            << "\", mode: " << rejection.mode << ", name: ";
    if (rejection.name == nullptr)
    {
        *stream << "nullptr";
    }
    else
    {
        *stream << '"' << rejection.name << '"';
    }
    *stream << ", use_crl: " << rejection.use_crl << ", reason: \"" << rejection.reason
            << "\"}, reject_client: " << parameter.reject_client;
}

std::vector<rejection_parameter> rejection_parameters()
{
    const rejection_case cases[] = {
        {"foreign", 0, nullptr, false, "untrusted CA"},
        {"server", 0, "unmatched", false, "name mismatch"},
        {"expired", 0, nullptr, false, "expired"},
        {"expired", 1, nullptr, false, "expired"},
        {"revoked", 0, nullptr, true, "revoked"},
        {"unregistered", 1, nullptr, false, "not registered"},
        {"same_key", 1, nullptr, false, "not registered"},
        {"wildcard", 0, "child.porter.test", false, "name mismatch"},
        {"common_name", 0, "porter.test", false, "name mismatch"},
        {"client", 0, "PORTER.TEST", false, "name mismatch"},
        {"server", 0, "urn:porter", false, "name mismatch"}
    };
    std::vector<rejection_parameter> parameters;
    for (auto transport : {POTR_INTERNAL_AUTH_TRANSPORT_DATAGRAM, POTR_INTERNAL_AUTH_TRANSPORT_STREAM})
    {
        for (const auto &rejection : cases)
        {
            for (bool reject_client : {false, true})
            {
                parameters.push_back({transport, rejection, reject_client});
            }
        }
    }
    return parameters;
}
class auth_rejection_test : public TestWithParam<rejection_parameter> {};

// 同じ拒否条件をクライアント証明書とサーバー証明書の双方へ適用する。
TEST_P(auth_rejection_test, rejects_certificate_with_reason)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    const auto &parameter = GetParam();
    const potr_internal_auth_transport transport = parameter.transport;
    const rejection_case &rejection = parameter.rejection;
    const bool reject_client = parameter.reject_client;
    const char *client_identity = "client";
    const char *server_identity = rejection.identity;
    if (reject_client)
    {
        client_identity = rejection.identity;
        server_identity = "server";
    }
    auth_configuration client(client_identity, rejection.mode, "server");
    auth_configuration server(server_identity, rejection.mode, "client");
    potr_internal_auth_config *verifier = &client.config;
    if (reject_client)
    {
        verifier = &server.config;
    }
    const char *names[] = {rejection.name};
    if (rejection.name != nullptr)
    {
        verifier->peer_names = names;
        verifier->peer_name_count = 1;
    }
    std::string crl = data_path("crl.pem");
    if (rejection.use_crl)
    {
        verifier->crl_file = crl.c_str();
    }
    auth_pair pair;
    int open_ret = pair.open(client.config, server.config, transport); // [サブ手順参照 名前=auth_pair.open 回数=PARAM]
    ASSERT_EQ(POTR_OK, open_ret); // [状態確認] - pair.open() の戻り値が POTR_OK であること。

    // Pre-Assert
    // Act
    pair.exchange(); // [サブ手順参照 名前=auth_pair.exchange 回数=PARAM]
    potr_internal_auth_state state = pair.report.client_state;
    potr_internal_auth_session *session = pair.client;
    if (reject_client)
    {
        state = pair.report.server_state;
        session = pair.server;
    }

    // Assert
    EXPECT_EQ(POTR_OK, pair.report.result); // [確認_異常系 回数=PARAM] - 認証の拒否時も pair.report.result が POTR_OK であること。
    EXPECT_EQ(POTR_INTERNAL_AUTH_STATE_FAILED, state); // [確認_異常系 回数=PARAM] - 証明書を検証した側の状態が FAILED であること。
    EXPECT_THAT(potr_internal_auth_session_failure_reason(session), HasSubstr(rejection.reason)); // [確認_異常系 回数=PARAM] - 失敗理由が rejection.reason を含むこと。
}
// [サブ手順参照 名前=auth_pair.cleanup 回数=PARAM]

INSTANTIATE_TEST_SUITE_P(transports_and_reasons, auth_rejection_test,
    ValuesIn(rejection_parameters()));

struct transport_parameter
{
    potr_internal_auth_transport transport;
};

void PrintTo(const transport_parameter &parameter, std::ostream *stream);
void PrintTo(const transport_parameter &parameter, std::ostream *stream)
{
    *stream << "transport: " << parameter.transport;
}

class auth_transport_test : public TestWithParam<transport_parameter> {};

TEST_P(auth_transport_test, pin_accepts_independently_of_ca_name_and_revocation)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    auth_configuration client("client", 2, "revoked");
    auth_configuration server("revoked", 2, "client");
    std::string crl = data_path("crl.pem");
    const char *names[] = {"unmatched"};
    client.config.crl_file = crl.c_str();
    client.config.peer_names = names;
    client.config.peer_name_count = 1;
    auth_pair pair;
    int open_ret = pair.open(client.config, server.config, GetParam().transport); // [サブ手順参照 名前=auth_pair.open 回数=PARAM]
    ASSERT_EQ(POTR_OK, open_ret); // [状態確認] - pair.open() の戻り値が POTR_OK であること。

    // Pre-Assert
    // Act
    pair.exchange(); // [サブ手順参照 名前=auth_pair.exchange 回数=PARAM]

    // Assert
    EXPECT_EQ(POTR_OK, pair.report.result); // [確認_正常系 回数=PARAM] - pair.report.result が POTR_OK であること。
    EXPECT_EQ(POTR_INTERNAL_AUTH_STATE_ESTABLISHED, pair.report.client_state); // [確認_正常系 回数=PARAM] - クライアントの状態が ESTABLISHED であること。
    EXPECT_EQ(POTR_INTERNAL_AUTH_STATE_ESTABLISHED, pair.report.server_state); // [確認_正常系 回数=PARAM] - サーバーの状態が ESTABLISHED であること。
}
// [サブ手順参照 名前=auth_pair.cleanup 回数=PARAM]

TEST_P(auth_transport_test, ca_accepts_when_direct_registration_does_not_match)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    auth_configuration client("client", 2, "unregistered");
    auth_configuration server("server", 2, "unregistered");
    auth_pair pair;
    int open_ret = pair.open(client.config, server.config, GetParam().transport); // [サブ手順参照 名前=auth_pair.open 回数=PARAM]
    ASSERT_EQ(POTR_OK, open_ret); // [状態確認] - pair.open() の戻り値が POTR_OK であること。

    // Pre-Assert
    // Act
    pair.exchange(); // [サブ手順参照 名前=auth_pair.exchange 回数=PARAM]

    // Assert
    EXPECT_EQ(POTR_OK, pair.report.result); // [確認_正常系 回数=PARAM] - pair.report.result が POTR_OK であること。
    EXPECT_EQ(POTR_INTERNAL_AUTH_STATE_ESTABLISHED, pair.report.client_state); // [確認_正常系 回数=PARAM] - クライアントの状態が ESTABLISHED であること。
    EXPECT_EQ(POTR_INTERNAL_AUTH_STATE_ESTABLISHED, pair.report.server_state); // [確認_正常系 回数=PARAM] - サーバーの状態が ESTABLISHED であること。
}
// [サブ手順参照 名前=auth_pair.cleanup 回数=PARAM]

TEST_P(auth_transport_test, expired_certificate_is_rejected_even_when_registered)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    auth_configuration client("client", 1, "expired");
    auth_configuration server("expired", 1, "client");
    auth_pair pair;
    int open_ret = pair.open(client.config, server.config, GetParam().transport); // [サブ手順参照 名前=auth_pair.open 回数=PARAM]
    ASSERT_EQ(POTR_OK, open_ret); // [状態確認] - pair.open() の戻り値が POTR_OK であること。

    // Pre-Assert
    // Act
    pair.exchange(); // [サブ手順参照 名前=auth_pair.exchange 回数=PARAM]

    // Assert
    EXPECT_EQ(POTR_OK, pair.report.result); // [確認_異常系 回数=PARAM] - pair.report.result が POTR_OK であること。
    EXPECT_EQ(POTR_INTERNAL_AUTH_STATE_FAILED, pair.report.client_state); // [確認_異常系 回数=PARAM] - クライアントの状態が FAILED であること。
    EXPECT_THAT(potr_internal_auth_session_failure_reason(pair.client), HasSubstr("expired")); // [確認_異常系 回数=PARAM] - 失敗理由が expired を含むこと。
}
// [サブ手順参照 名前=auth_pair.cleanup 回数=PARAM]

TEST_P(auth_transport_test, requires_peer_certificate)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    auth_configuration client("client", 0, "server");
    auth_configuration server("server", 0, "client");
    auth_pair pair;
    // [状態] - クライアントの読み込みだけを省き、証明書を持たない SSL_CTX を作る。
    ON_CALL(mock_openssl, SSL_CTX_use_certificate(_, _, _, _, _)).WillByDefault(Return(1));
    ON_CALL(mock_openssl, SSL_CTX_use_PrivateKey(_, _, _, _, _)).WillByDefault(Return(1));
    ON_CALL(mock_openssl, SSL_CTX_check_private_key(_, _, _, _)).WillByDefault(Return(1));
    ASSERT_EQ(POTR_OK, potr_internal_auth_credentials_create(&client.config, GetParam().transport, &pair.client_credentials)); // [状態確認] - 証明書なしの credentials_create() の戻り値が POTR_OK であること。
    ON_CALL(mock_openssl, SSL_CTX_use_certificate(_, _, _, _, _)).WillByDefault(Invoke(delegate_real_SSL_CTX_use_certificate));
    ON_CALL(mock_openssl, SSL_CTX_use_PrivateKey(_, _, _, _, _)).WillByDefault(Invoke(delegate_real_SSL_CTX_use_PrivateKey));
    ON_CALL(mock_openssl, SSL_CTX_check_private_key(_, _, _, _)).WillByDefault(Invoke(delegate_real_SSL_CTX_check_private_key));
    ASSERT_EQ(POTR_OK, potr_internal_auth_credentials_create(&server.config, GetParam().transport, &pair.server_credentials)); // [状態確認] - サーバーの credentials_create() の戻り値が POTR_OK であること。
    ASSERT_EQ(POTR_OK, potr_internal_auth_session_create(pair.client_credentials, POTR_INTERNAL_AUTH_ROLE_CLIENT, 512, &pair.client)); // [状態確認] - クライアントの session_create() の戻り値が POTR_OK であること。
    ASSERT_EQ(POTR_OK, potr_internal_auth_session_create(pair.server_credentials, POTR_INTERNAL_AUTH_ROLE_SERVER, 512, &pair.server)); // [状態確認] - サーバーの session_create() の戻り値が POTR_OK であること。

    // Pre-Assert
    // Act
    pair.exchange(); // [サブ手順参照 名前=auth_pair.exchange 回数=PARAM]

    // Assert
    EXPECT_EQ(POTR_INTERNAL_AUTH_STATE_FAILED, pair.report.server_state); // [確認_異常系 回数=PARAM] - サーバーの状態が FAILED であること。
    EXPECT_THAT(potr_internal_auth_session_failure_reason(pair.server), HasSubstr("certificate missing")); // [確認_異常系 回数=PARAM] - サーバーの失敗理由が certificate missing を含むこと。
}
// [サブ手順参照 名前=auth_pair.cleanup 回数=PARAM]

TEST_P(auth_transport_test, export_requires_established_and_preserves_output_on_failure)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    auth_configuration client("client", 0, "server");
    auth_configuration server("server", 0, "client");
    auth_pair pair;
    int open_ret = pair.open(client.config, server.config, GetParam().transport); // [サブ手順参照 名前=auth_pair.open 回数=PARAM]
    ASSERT_EQ(POTR_OK, open_ret); // [状態確認] - pair.open() の戻り値が POTR_OK であること。
    std::array<uint8_t, POTR_CRYPTO_KEY_SIZE> first, second;
    first.fill(0xa5);
    second.fill(0x5a);
    auto original_first = first;
    auto original_second = second;

    // Pre-Assert
    // Act
    int actual_ret = potr_internal_auth_session_export_keys(pair.client, nullptr, 0, first.data(), second.data()); // [手順] - 確立前に鍵を導出する。

    // Assert
    EXPECT_EQ(POTR_ERR_UNSUPPORTED, actual_ret); // [確認_異常系 回数=PARAM] - 確立前の export_keys() の戻り値が POTR_ERR_UNSUPPORTED であること。
    EXPECT_EQ(original_first, first); // [確認_異常系 回数=PARAM] - 第 1 出力が変わらないこと。
    EXPECT_EQ(original_second, second); // [確認_異常系 回数=PARAM] - 第 2 出力が変わらないこと。

    // Act_2
    pair.exchange(); // [サブ手順参照 名前=auth_pair.exchange 回数=PARAM]
    ASSERT_EQ(POTR_INTERNAL_AUTH_STATE_ESTABLISHED, pair.report.client_state); // [状態確認] - クライアントの状態が ESTABLISHED であること。
    ON_CALL(mock_openssl, SSL_export_keying_material(_, _, _, _, _, _, _, _, _, _, _)).WillByDefault(DoAll(SetArrayArgument<4>(original_first.begin(), original_first.end()), Return(0))); // [状態] - 導出途中で失敗させる。
    actual_ret = potr_internal_auth_session_export_keys(pair.client, nullptr, 0, first.data(), second.data()); // [手順] - 鍵の導出を失敗させる。

    // Assert_2
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual_ret); // [確認_異常系 回数=PARAM] - 導出失敗時の export_keys() の戻り値が POTR_ERR_UNKNOWN であること。
    EXPECT_EQ(original_first, first); // [確認_異常系 回数=PARAM] - 導出失敗時も第 1 出力が変わらないこと。
    EXPECT_EQ(original_second, second); // [確認_異常系 回数=PARAM] - 導出失敗時も第 2 出力が変わらないこと。
}
// [サブ手順参照 名前=auth_pair.cleanup 回数=PARAM]

// 相手に証明書を拒否された側の失敗理由が、汎用の理由に OpenSSL の理由の文字列を付けたものになることの確認
TEST_P(auth_transport_test, rejected_side_failure_reason_includes_openssl_reason)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    auth_configuration client("client", 0, "server");
    auth_configuration server("foreign", 0, "client");
    // [状態] - サーバーが信頼されない CA の証明書を使い、クライアントがサーバー証明書を拒否する構成を用意する。
    auth_pair pair;
    int open_ret = pair.open(client.config, server.config, GetParam().transport); // [サブ手順参照 名前=auth_pair.open 回数=PARAM]
    ASSERT_EQ(POTR_OK, open_ret); // [状態確認] - pair.open() の戻り値が POTR_OK であること。
    pair.exchange(); // [サブ手順参照 名前=auth_pair.exchange 回数=PARAM]
    ASSERT_EQ(POTR_INTERNAL_AUTH_STATE_FAILED, pair.report.client_state); // [状態確認] - クライアントの状態が FAILED であること。
    potr_internal_auth_state server_state = pair.report.server_state;

    // Pre-Assert
    // Act
    pair.transfer(pair.client, pair.server, server_state); // [手順] - クライアントの拒否の通知 (アラート) をサーバーへ渡す。

    // Assert
    EXPECT_EQ(POTR_INTERNAL_AUTH_STATE_FAILED, server_state); // [確認_異常系 回数=PARAM] - サーバーの状態が FAILED であること。
    EXPECT_THAT(potr_internal_auth_session_failure_reason(pair.server),
                HasSubstr("handshake failed or peer rejected certificate (OpenSSL: ")); // [確認_異常系 回数=PARAM] - サーバーの失敗理由が汎用の理由と OpenSSL の理由の文字列を含むこと。
}
// [サブ手順参照 名前=auth_pair.cleanup 回数=PARAM]

// OpenSSL の理由の文字列が得られない場合は、失敗理由に OpenSSL のエラー コードを付けることの確認
TEST_P(auth_transport_test, rejected_side_failure_reason_includes_openssl_code_without_reason_text)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    auth_configuration client("client", 0, "server");
    auth_configuration server("foreign", 0, "client");
    // [状態] - サーバーが信頼されない CA の証明書を使い、クライアントがサーバー証明書を拒否する構成を用意する。
    ON_CALL(mock_openssl, ERR_reason_error_string(_, _, _, _)).WillByDefault(Return(nullptr));
    // [状態] - ERR_reason_error_string() から nullptr (未登録のコード) を返すように既定動作を設定する。
    auth_pair pair;
    int open_ret = pair.open(client.config, server.config, GetParam().transport); // [サブ手順参照 名前=auth_pair.open 回数=PARAM]
    ASSERT_EQ(POTR_OK, open_ret); // [状態確認] - pair.open() の戻り値が POTR_OK であること。
    pair.exchange(); // [サブ手順参照 名前=auth_pair.exchange 回数=PARAM]
    ASSERT_EQ(POTR_INTERNAL_AUTH_STATE_FAILED, pair.report.client_state); // [状態確認] - クライアントの状態が FAILED であること。
    potr_internal_auth_state server_state = pair.report.server_state;

    // Pre-Assert
    // Act
    pair.transfer(pair.client, pair.server, server_state); // [手順] - クライアントの拒否の通知 (アラート) をサーバーへ渡す。

    // Assert
    EXPECT_EQ(POTR_INTERNAL_AUTH_STATE_FAILED, server_state); // [確認_異常系 回数=PARAM] - サーバーの状態が FAILED であること。
    EXPECT_THAT(potr_internal_auth_session_failure_reason(pair.server),
                HasSubstr("handshake failed or peer rejected certificate (OpenSSL error 0x")); // [確認_異常系 回数=PARAM] - サーバーの失敗理由が汎用の理由と OpenSSL のエラー コードを含むこと。
}
// [サブ手順参照 名前=auth_pair.cleanup 回数=PARAM]

INSTANTIATE_TEST_SUITE_P(transports, auth_transport_test,
    Values(transport_parameter{POTR_INTERNAL_AUTH_TRANSPORT_DATAGRAM}, transport_parameter{POTR_INTERNAL_AUTH_TRANSPORT_STREAM}));

TEST(auth_test, datagram_loss_retransmits_after_real_timeout)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    auth_configuration client("client", 0, "server");
    auth_configuration server("server", 0, "client");
    auth_pair pair;
    int open_ret = pair.open(client.config, server.config, POTR_INTERNAL_AUTH_TRANSPORT_DATAGRAM, 256); // [サブ手順参照 名前=auth_pair.open]
    ASSERT_EQ(POTR_OK, open_ret); // [状態確認] - pair.open() の戻り値が POTR_OK であること。
    std::array<uint8_t, 65536> output = {};
    size_t length = 0;
    int has_timeout = 0;
    uint32_t timeout_ms = 0;
    potr_internal_auth_state state;
    ASSERT_EQ(POTR_OK, potr_internal_auth_session_receive(pair.client, nullptr, 0, &state)); // [状態確認] - 開始時の receive() の戻り値が POTR_OK であること。
    ASSERT_EQ(POTR_OK, potr_internal_auth_session_take_output(pair.client, output.data(), output.size(), &length)); // [状態確認] - 最初の take_output() の戻り値が POTR_OK であること。
    ASSERT_GT(length, 0U); // [状態確認] - 最初の出力長が 0 より大きいこと。
    ASSERT_EQ(POTR_OK, potr_internal_auth_session_get_timeout_ms(pair.client, &has_timeout, &timeout_ms)); // [状態確認] - get_timeout_ms() の戻り値が POTR_OK であること。
    ASSERT_EQ(1, has_timeout); // [状態確認] - 再送期限があること。
    ASSERT_LT(timeout_ms, 5000U); // [状態確認] - 初回の再送期限が 5000 ms より短いこと。

    // Pre-Assert
    // Act
    // [手順] - 最初のデータグラムを渡さず、OpenSSL の実時間の期限まで待機する。
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms + 10U));
    int actual_ret = potr_internal_auth_session_handle_timeout(pair.client, &state); // [手順] - 期限に到達した再送を処理する。
    size_t retransmitted_length = 0;
    int output_ret = potr_internal_auth_session_take_output(pair.client, output.data(), output.size(), &retransmitted_length); // [手順] - 再送データグラムを取り出す。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - handle_timeout() の戻り値が POTR_OK であること。
    ASSERT_EQ(POTR_OK, output_ret); // [確認_正常系] - 再送の take_output() の戻り値が POTR_OK であること。
    ASSERT_GT(retransmitted_length, 0U); // [確認_正常系] - 再送の出力長が 0 より大きいこと。
    EXPECT_LE(retransmitted_length, 256U); // [確認_正常系] - 再送の出力長が 256 以下であること。

    // Act_2
    ASSERT_EQ(POTR_OK, potr_internal_auth_session_receive(pair.server, output.data(), retransmitted_length, &state)); // [状態確認] - 再送を入力する receive() の戻り値が POTR_OK であること。
    pair.exchange(); // [サブ手順参照 名前=auth_pair.exchange]

    // Assert_2
    EXPECT_EQ(POTR_OK, pair.report.result); // [確認_正常系] - pair.report.result が POTR_OK であること。
    EXPECT_EQ(POTR_INTERNAL_AUTH_STATE_ESTABLISHED, pair.report.client_state); // [確認_正常系] - クライアントの状態が ESTABLISHED であること。
    EXPECT_EQ(POTR_INTERNAL_AUTH_STATE_ESTABLISHED, pair.report.server_state); // [確認_正常系] - サーバーの状態が ESTABLISHED であること。
    EXPECT_LE(pair.report.largest_output, 256U); // [確認_正常系] - 全出力の最大長が 256 以下であること。
}
// [サブ手順参照 名前=auth_pair.cleanup]

TEST(auth_test, stream_accepts_arbitrary_fragments_and_has_no_timer)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    auth_configuration client("client", 0, "server");
    auth_configuration server("server", 0, "client");
    auth_pair pair;
    int open_ret = pair.open(client.config, server.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, 17); // [サブ手順参照 名前=auth_pair.open]
    ASSERT_EQ(POTR_OK, open_ret); // [状態確認] - pair.open() の戻り値が POTR_OK であること。
    int has_timeout = 1;
    uint32_t timeout_ms = 1;
    potr_internal_auth_state state;

    // Pre-Assert
    // Act
    pair.exchange(); // [サブ手順参照 名前=auth_pair.exchange]
    int timeout_ret = potr_internal_auth_session_get_timeout_ms(pair.client, &has_timeout, &timeout_ms); // [手順] - ストリームのタイマーを取得する。
    int handle_ret = potr_internal_auth_session_handle_timeout(pair.client, &state); // [手順] - ストリームの期限処理を呼ぶ。

    // Assert
    EXPECT_EQ(POTR_INTERNAL_AUTH_STATE_ESTABLISHED, pair.report.client_state); // [確認_正常系] - クライアントの状態が ESTABLISHED であること。
    EXPECT_EQ(POTR_INTERNAL_AUTH_STATE_ESTABLISHED, pair.report.server_state); // [確認_正常系] - サーバーの状態が ESTABLISHED であること。
    EXPECT_LE(pair.report.largest_output, 17U); // [確認_正常系] - 最大出力長が 17 以下であること。
    EXPECT_EQ(POTR_OK, timeout_ret); // [確認_正常系] - get_timeout_ms() の戻り値が POTR_OK であること。
    EXPECT_EQ(0, has_timeout); // [確認_正常系] - has_timeout が 0 であること。
    EXPECT_EQ(0U, timeout_ms); // [確認_正常系] - timeout_ms が 0 であること。
    EXPECT_EQ(POTR_OK, handle_ret); // [確認_正常系] - handle_timeout() の戻り値が POTR_OK であること。
    EXPECT_EQ(POTR_INTERNAL_AUTH_STATE_ESTABLISHED, state); // [確認_正常系] - 期限処理後も状態が ESTABLISHED であること。
}
// [サブ手順参照 名前=auth_pair.cleanup]

TEST(auth_test, datagram_capacity_error_preserves_whole_output)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    auth_configuration client("client", 0, "server");
    auth_configuration server("server", 0, "client");
    auth_pair pair;
    int open_ret = pair.open(client.config, server.config, POTR_INTERNAL_AUTH_TRANSPORT_DATAGRAM); // [サブ手順参照 名前=auth_pair.open]
    ASSERT_EQ(POTR_OK, open_ret); // [状態確認] - pair.open() の戻り値が POTR_OK であること。
    potr_internal_auth_state state;
    ASSERT_EQ(POTR_OK, potr_internal_auth_session_receive(pair.client, nullptr, 0, &state)); // [状態確認] - 開始時の receive() の戻り値が POTR_OK であること。
    std::array<uint8_t, 1024> buffer = {};
    size_t length = 99;

    // Pre-Assert
    // Act
    int actual_ret = potr_internal_auth_session_take_output(pair.client, buffer.data(), 1, &length); // [手順] - 容量 1 byte でデータグラムを取り出す。

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 容量不足の take_output() の戻り値が POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(0U, length); // [確認_異常系] - 容量不足時の出力長が 0 であること。

    // Act_2
    actual_ret = potr_internal_auth_session_take_output(pair.client, buffer.data(), buffer.size(), &length); // [手順] - 十分な容量で同じ出力を取り出す。

    // Assert_2
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 容量拡大後の take_output() の戻り値が POTR_OK であること。
    EXPECT_GT(length, 1U); // [確認_正常系] - データグラムの出力長が 1 より大きいこと。
}
// [サブ手順参照 名前=auth_pair.cleanup]

TEST(auth_test, credentials_validate_configuration)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    auth_configuration source("client", 0, "server");
    std::array<potr_internal_auth_config, 10> invalid;
    invalid.fill(source.config);
    invalid[0].cert_file = nullptr;
    invalid[1].key_file = nullptr;
    invalid[2].ca_file = nullptr;
    invalid[3].peer_name_count = 0;
    invalid[4].peer_names = nullptr;
    invalid[5].ca_file = "";
    invalid[6].cert_file = "";
    invalid[7].pinned_cert_count = 1;
    invalid[8].crl_file = "";
    const char *empty_name[] = {""};
    invalid[9].peer_names = empty_name;
    invalid[9].peer_name_count = 1;

    for (const auto &config : invalid)
    {
        potr_internal_auth_credentials *credentials = nullptr;
        // Pre-Assert
        // Act
        int actual_ret = potr_internal_auth_credentials_create(&config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, &credentials); // [手順] - 不正な設定で認証情報を作る。
        // Assert
        EXPECT_EQ(POTR_ERR_INVALID_CONFIG, actual_ret); // [確認_異常系 回数=10] - 不正な設定の credentials_create() の戻り値が POTR_ERR_INVALID_CONFIG であること。
        EXPECT_EQ(nullptr, credentials); // [確認_異常系 回数=10] - 不正な設定の出力が nullptr であること。
        potr_internal_auth_credentials_dispose(credentials);
    }
}

TEST(auth_test, credentials_reject_missing_files_and_mismatched_key)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    auth_configuration missing("absent", 0, "server");
    auth_configuration mismatch("client", 0, "server");
    std::string other_key = data_path("server-key.pem");
    mismatch.config.key_file = other_key.c_str();
    potr_internal_auth_credentials *credentials = nullptr;

    // Pre-Assert
    // Act
    int missing_ret = potr_internal_auth_credentials_create(&missing.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, &credentials); // [手順] - 存在しないファイルを読み込む。
    int mismatch_ret = potr_internal_auth_credentials_create(&mismatch.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, &credentials); // [手順] - 証明書と対応しない秘密鍵を読み込む。

    // Assert
    EXPECT_EQ(POTR_ERR_IO, missing_ret); // [確認_異常系] - 不在ファイルの credentials_create() の戻り値が POTR_ERR_IO であること。
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, mismatch_ret); // [確認_異常系] - 鍵が対応しない credentials_create() の戻り値が POTR_ERR_INVALID_CONFIG であること。
    EXPECT_EQ(nullptr, credentials); // [確認_異常系] - 失敗時の出力が nullptr であること。
}

TEST(auth_test, injects_context_and_certificate_loading_failures)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    auth_configuration client("client", 0, "server");
    potr_internal_auth_credentials *credentials = nullptr;
    ON_CALL(mock_openssl, SSL_CTX_new(_, _, _, _)).WillByDefault(Return(nullptr)); // [状態] - SSL_CTX の生成を失敗させる。

    // Pre-Assert
    // Act
    int context_ret = potr_internal_auth_credentials_create(&client.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, &credentials); // [手順] - SSL_CTX の生成失敗を注入する。
    ON_CALL(mock_openssl, SSL_CTX_new(_, _, _, _)).WillByDefault(Invoke(delegate_real_SSL_CTX_new));
    ON_CALL(mock_openssl, SSL_CTX_use_certificate(_, _, _, _, _)).WillByDefault(Return(0)); // [状態] - エラー キューが空のまま自端の証明書の登録を失敗させる。
    int certificate_ret = potr_internal_auth_credentials_create(&client.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, &credentials); // [手順] - 自端の証明書の登録失敗を注入する。
    ON_CALL(mock_openssl, SSL_CTX_use_certificate(_, _, _, _, _)).WillByDefault(Invoke(delegate_real_SSL_CTX_use_certificate));
    auth_configuration pinned("client", 1, "server");
    ON_CALL(mock_openssl, PEM_read_bio_X509(_, _, _, _, _, _, _)).WillByDefault(Return(nullptr)); // [状態] - エラー キューが空のまま直接登録する証明書の解析を失敗させる。
    int pin_ret = potr_internal_auth_credentials_create(&pinned.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, &credentials); // [手順] - 直接登録の解析失敗を注入する。

    // Assert
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, context_ret); // [確認_異常系] - SSL_CTX の生成失敗時の戻り値が POTR_ERR_OUT_OF_MEMORY であること。
    EXPECT_EQ(POTR_ERR_UNKNOWN, certificate_ret); // [確認_異常系] - 分類情報のない証明書登録失敗時の戻り値が POTR_ERR_UNKNOWN であること。
    EXPECT_EQ(POTR_ERR_UNKNOWN, pin_ret); // [確認_異常系] - 分類情報のない直接登録の解析失敗時の戻り値が POTR_ERR_UNKNOWN であること。
    EXPECT_EQ(nullptr, credentials); // [確認_異常系] - 失敗時の出力が nullptr であること。
}

TEST(auth_test, injects_session_creation_failure)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    auth_configuration client("client", 0, "server");
    auth_pair pair;
    ASSERT_EQ(POTR_OK, potr_internal_auth_credentials_create(&client.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, &pair.client_credentials)); // [状態確認] - credentials_create() の戻り値が POTR_OK であること。
    ON_CALL(mock_openssl, SSL_new(_, _, _, _)).WillByDefault(Return(nullptr)); // [状態] - SSL の生成を失敗させる。

    // Pre-Assert
    // Act
    int actual_ret = potr_internal_auth_session_create(pair.client_credentials, POTR_INTERNAL_AUTH_ROLE_CLIENT, 512, &pair.client); // [手順] - SSL の生成失敗を注入する。

    // Assert
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, actual_ret); // [確認_異常系] - session_create() の戻り値が POTR_ERR_OUT_OF_MEMORY であること。
    EXPECT_EQ(nullptr, pair.client); // [確認_異常系] - session の出力が nullptr であること。
}
// [サブ手順参照 名前=auth_pair.cleanup]

} // namespace

// PEM 内の複数の CRL をすべて信頼ストアへ登録することの確認
TEST(auth_test, loads_every_revocation_list_in_pem)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    auth_configuration client("client", 0, "server");
    const auto path = std::filesystem::temp_directory_path() /
        ("porter-crls-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".pem");
    std::ifstream source(data_path("crl.pem"), std::ios::binary);
    const std::string pem((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
    std::ofstream output(path, std::ios::binary);
    output << pem << pem;
    output.close();
    std::string revocations = path.string();
    client.config.crl_file = revocations.c_str();
    potr_internal_auth_credentials *credentials = nullptr;

    // Pre-Assert
    EXPECT_CALL(mock_openssl, X509_STORE_add_crl(_, _, _, _, _)).Times(2)
        .WillRepeatedly(Invoke(delegate_real_X509_STORE_add_crl)); // [Pre-Assert確認_正常系] - 2 件の CRL を信頼ストアへ登録すること。

    // Act
    int actual_ret = potr_internal_auth_credentials_create(&client.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM,
        &credentials); // [手順] - 2 件の CRL を格納した PEM を読み込む。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 複数 CRL の読み込みが成功すること。

    // Cleanup
    potr_internal_auth_credentials_dispose(credentials);
    std::filesystem::remove(path);
}

namespace
{

std::string read_binary_file(const std::string &path)
{
    std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

void write_binary_file_utf8(const std::string &utf8_path, const std::string &content)
{
    std::ofstream output(std::filesystem::u8path(utf8_path), std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
}

std::string make_temp_test_path(const std::string &stem, const std::string &ext)
{
    auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() / (stem + "-" + std::to_string(tick) + ext)).string();
}

} // namespace

// 非 ASCII の UTF-8 パスから全認証ファイルを読み込めることの確認
TEST(auth_test, credentials_load_non_ascii_utf8_paths)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string dir_utf8 = std::filesystem::temp_directory_path().u8string() + "/porter-認証確認-" + std::to_string(tick);
    std::filesystem::create_directories(std::filesystem::u8path(dir_utf8));
    std::string cert_utf8 = dir_utf8 + "/自端証明書.pem";
    std::string key_utf8 = dir_utf8 + "/秘密鍵.pem";
    std::string ca_utf8 = dir_utf8 + "/認証局.pem";
    std::string crl_utf8 = dir_utf8 + "/失効一覧.pem";
    std::string pin_utf8 = dir_utf8 + "/相手証明書.pem";
    write_binary_file_utf8(cert_utf8, read_binary_file(data_path("client.pem")));
    write_binary_file_utf8(key_utf8, read_binary_file(data_path("client-key.pem")));
    write_binary_file_utf8(ca_utf8, read_binary_file(data_path("ca.pem")));
    write_binary_file_utf8(crl_utf8, read_binary_file(data_path("crl.pem")));
    write_binary_file_utf8(pin_utf8, read_binary_file(data_path("server.pem")));

    const char *pin_list[1] = {pin_utf8.c_str()};
    potr_internal_auth_config config = {};
    config.cert_file = cert_utf8.c_str();
    config.key_file = key_utf8.c_str();
    config.ca_file = ca_utf8.c_str();
    config.crl_file = crl_utf8.c_str();
    config.pinned_certs = pin_list;
    config.pinned_cert_count = 1U;
    config.peer_names = k_peer_names;
    config.peer_name_count = 2U;
    potr_internal_auth_credentials *credentials = nullptr;
    // [状態] - 日本語文字を含む UTF-8 パスへ全認証ファイルを配置する。

    // Pre-Assert

    // Act
    int actual_ret = potr_internal_auth_credentials_create(&config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM,
                                                           &credentials); // [手順] - 非 ASCII の UTF-8 パスから認証情報を読み込む。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 非 ASCII の UTF-8 パスからの読み込みが成功すること。
    EXPECT_NE(nullptr, credentials); // [確認_正常系] - 認証情報のハンドルが生成されること。

    // Cleanup
    potr_internal_auth_credentials_dispose(credentials);
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::u8path(dir_utf8), ec);
}

// 認証ファイルの各役割で不在パスがすべて POTR_ERR_IO になることの確認
TEST(auth_test, credentials_reject_missing_all_file_roles)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    std::string absent_cert = data_path("absent-cert.pem");
    std::string absent_key = data_path("absent-key.pem");
    std::string absent_ca = data_path("absent-ca.pem");
    std::string absent_crl = data_path("absent-crl.pem");
    std::string absent_pin = data_path("absent-pin.pem");
    const char *absent_pin_list[1] = {absent_pin.c_str()};

    auth_configuration missing_cert("client", 0, "server");
    missing_cert.config.cert_file = absent_cert.c_str();
    auth_configuration missing_key("client", 0, "server");
    missing_key.config.key_file = absent_key.c_str();
    auth_configuration missing_ca("client", 0, "server");
    missing_ca.config.ca_file = absent_ca.c_str();
    auth_configuration missing_crl("client", 0, "server");
    missing_crl.config.crl_file = absent_crl.c_str();
    auth_configuration missing_pin("client", 1, "server");
    missing_pin.config.pinned_certs = absent_pin_list;
    // [状態] - 証明書・秘密鍵・CA・CRL・直接登録の各パスだけを不在ファイルに設定する。

    // Pre-Assert

    // Act & Assert
    const potr_internal_auth_config *configs[] = {&missing_cert.config, &missing_key.config, &missing_ca.config,
                                                  &missing_crl.config, &missing_pin.config};
    for (const potr_internal_auth_config *config : configs)
    {
        potr_internal_auth_credentials *credentials = nullptr;
        int ret = potr_internal_auth_credentials_create(config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM,
                                                        &credentials); // [手順 回数=5] - 不在の認証ファイルを指定して認証情報を作る。
        EXPECT_EQ(POTR_ERR_IO, ret); // [確認_異常系 回数=5] - 不在ファイルの戻り値が POTR_ERR_IO であること。
        EXPECT_EQ(nullptr, credentials); // [確認_異常系 回数=5] - 失敗時の出力が nullptr であること。
    }
}

// setvbuf() 失敗と fread() 中の ferror() 発生時に解析へ進まず POTR_ERR_IO を返し消去と解放を行うことの確認
TEST(auth_test, credentials_reject_setvbuf_and_ferror_before_parsing)
{
    // Arrange
    auth_configuration client("client", 0, "server");
    const std::string cert_bytes = read_binary_file(data_path("client.pem"));

    // setvbuf() 失敗の検証
    {
        NiceMock<Mock_cplat> mock_cplat;
        NiceMock<Mock_stdio> mock_stdio;
        NiceMock<Mock_openssl> mock_openssl;
        potr_internal_auth_credentials *credentials = nullptr;
        EXPECT_CALL(mock_stdio, setvbuf(_, _, _, _, _, _, _)).WillOnce(Return(-1));
        EXPECT_CALL(mock_openssl, BIO_new(_, _, _, _)).Times(0);
        EXPECT_CALL(mock_openssl, PEM_read_bio_X509(_, _, _, _, _, _, _)).Times(0);

        int setvbuf_ret = potr_internal_auth_credentials_create(&client.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM,
                                                                &credentials);
        EXPECT_EQ(POTR_ERR_IO, setvbuf_ret);
        EXPECT_EQ(nullptr, credentials);
    }

    // 初回 fread() 直後の ferror() 検証
    {
        NiceMock<Mock_cplat> mock_cplat;
        NiceMock<Mock_stdio> mock_stdio;
        NiceMock<Mock_openssl> mock_openssl;
        potr_internal_auth_credentials *credentials = nullptr;
        ON_CALL(mock_stdio, ferror(_, _, _, _)).WillByDefault(Return(1));
        EXPECT_CALL(mock_openssl, BIO_write(_, _, _, _, _, _)).Times(0);
        EXPECT_CALL(mock_openssl, PEM_read_bio_X509(_, _, _, _, _, _, _)).Times(0);
        EXPECT_CALL(mock_openssl, BIO_free(_, _, _, _)).Times(AtLeast(1)).WillRepeatedly(Invoke(delegate_real_BIO_free));
        EXPECT_CALL(mock_cplat, cplat_secure_zero(_, _)).Times(AtLeast(1)).WillRepeatedly(Invoke(delegate_real_cplat_secure_zero));

        int first_ferror_ret = potr_internal_auth_credentials_create(&client.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM,
                                                                     &credentials);
        EXPECT_EQ(POTR_ERR_IO, first_ferror_ret);
        EXPECT_EQ(nullptr, credentials);
    }

    // 有効な PEM 全量読み取り直後の ferror() 検証 (EOF を確定させない)
    {
        NiceMock<Mock_cplat> mock_cplat;
        NiceMock<Mock_stdio> mock_stdio;
        NiceMock<Mock_openssl> mock_openssl;
        potr_internal_auth_credentials *credentials = nullptr;
        size_t fread_call = 0U;
        ON_CALL(mock_stdio, fread(_, _, _, _, _, _, _))
            .WillByDefault([&](const char *, const int, const char *, void *buffer, size_t size, size_t count, FILE *) -> size_t {
                ++fread_call;
                if (fread_call == 1U)
                {
                    size_t cap = size * count;
                    size_t copy_len = (std::min)(cap, cert_bytes.size());
                    std::memcpy(buffer, cert_bytes.data(), copy_len);
                    return copy_len;
                }
                return 0U;
            });
        ON_CALL(mock_stdio, ferror(_, _, _, _))
            .WillByDefault([&](const char *, const int, const char *, FILE *) -> int {
                return fread_call >= 2U ? 1 : 0;
            });
        EXPECT_CALL(mock_openssl, BIO_write(_, _, _, _, _, _)).Times(1).WillOnce(Invoke(delegate_real_BIO_write));
        EXPECT_CALL(mock_openssl, PEM_read_bio_X509(_, _, _, _, _, _, _)).Times(0);
        EXPECT_CALL(mock_openssl, BIO_free(_, _, _, _)).Times(AtLeast(1)).WillRepeatedly(Invoke(delegate_real_BIO_free));
        EXPECT_CALL(mock_cplat, cplat_secure_zero(_, _)).Times(AtLeast(2)).WillRepeatedly(Invoke(delegate_real_cplat_secure_zero));

        int mid_ferror_ret = potr_internal_auth_credentials_create(&client.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM,
                                                                   &credentials);
        EXPECT_EQ(POTR_ERR_IO, mid_ferror_ret);
        EXPECT_EQ(nullptr, credentials);
    }
}

// オープン後にファイルを差し替えても再オープンせず、同一ハンドルの内容をメモリ BIO で解析することの確認
TEST(auth_test, credentials_read_opened_handle_without_reopening_path)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    NiceMock<Mock_openssl> mock_openssl;
    const std::string temp_cert = make_temp_test_path("porter-reopen", ".pem");
    write_binary_file_utf8(temp_cert, read_binary_file(data_path("client.pem")));

    auth_configuration client("client", 0, "server");
    client.config.cert_file = temp_cert.c_str();
    potr_internal_auth_credentials *credentials = nullptr;
    FILE *cert_fp = nullptr;
    bool replaced = false;
    // [状態] - 一時証明書ファイルを配置し、オープン後差し替えと単一オープンを監視する設定を用意する。

    // Pre-Assert
    EXPECT_CALL(mock_openssl, BIO_new_file(_, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_openssl, SSL_CTX_use_certificate_chain_file(_, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_openssl, SSL_CTX_use_PrivateKey_file(_, _, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_openssl, SSL_CTX_load_verify_locations(_, _, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_openssl, BIO_s_secmem(_, _, _)).Times(AtLeast(3)).WillRepeatedly(Invoke(delegate_real_BIO_s_secmem));
    EXPECT_CALL(mock_openssl, BIO_ctrl(_, _, _, _, BIO_C_SET_BUF_MEM_EOF_RETURN, 0L, nullptr))
        .Times(AtLeast(3))
        .WillRepeatedly(Invoke(delegate_real_BIO_ctrl));
    EXPECT_CALL(mock_cplat, cplat_fopen(_, StrEq("rb"), nullptr))
        .Times(3)
        .WillRepeatedly([&](const char *path, const char *mode, cplat_error *detail) -> FILE * {
            FILE *fp = delegate_real_cplat_fopen(path, mode, detail);
            if (path == temp_cert)
            {
                cert_fp = fp;
            }
            return fp;
        });
    ON_CALL(mock_stdio, fclose(_, _, _, _))
        .WillByDefault([&](const char *file, const int line, const char *func, FILE *fp) -> int {
            int ret = delegate_real_fclose(file, line, func, fp);
            if (fp == cert_fp && !replaced)
            {
                replaced = true;
                write_binary_file_utf8(temp_cert, "CORRUPTED_AFTER_CLOSE\n");
            }
            return ret;
        });
    ON_CALL(mock_stdio, fread(_, _, _, _, _, _, _))
        .WillByDefault([&](const char *file, const int line, const char *func, void *buf, size_t size, size_t count,
                           FILE *fp) -> size_t {
            size_t read_ret = delegate_real_fread(file, line, func, buf, size, count, fp);
            if (fp == cert_fp && !replaced)
            {
                replaced = true;
                write_binary_file_utf8(temp_cert, "CORRUPTED_BEFORE_EOF\n");
            }
            return read_ret;
        });

    // Act
    int actual_ret = potr_internal_auth_credentials_create(&client.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM,
                                                           &credentials); // [手順] - 読み取り開始直後に実ファイルを破損させながら認証情報を生成する。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 再オープンせず開いたハンドル由来のメモリ BIO で成功すること。
    EXPECT_NE(nullptr, credentials); // [確認_正常系] - 認証情報が生成されること。
    EXPECT_TRUE(replaced); // [確認_正常系] - 読み取り途中の実ファイル差し替えが実行されたこと。

    // Cleanup
    potr_internal_auth_credentials_dispose(credentials);
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(temp_cert), ec);
}

// 空・NUL 含み・未閉鎖・壊れた後続ブロック・不正ラベル・過剰ブロックを拒否することの確認
TEST(auth_test, credentials_reject_invalid_pem_files)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    const std::string valid_cert = read_binary_file(data_path("client.pem"));
    const std::string valid_key = read_binary_file(data_path("client-key.pem"));
    const std::string temp_file = make_temp_test_path("porter-bad-pem", ".pem");
    const std::string nul_content = valid_cert + std::string("\0", 1) + "\n";

    struct bad_pem_case
    {
        const char *label;
        std::string content;
        int role; // 0: cert_file, 1: key_file, 2: ca_file, 3: crl_file, 4: pinned_certs
        int pad = 0; /**< 明示的アラインメントです。0 を指定します。 */

        bad_pem_case(const char *case_label, int case_role, const std::string &case_content)
            : label(case_label), content(case_content), role(case_role)
        {
        }
    };
    const bad_pem_case cases[] = {
        {"empty_cert", 0, ""},
        {"empty_crl", 3, ""},
        {"nul_in_cert", 0, nul_content},
        {"non_pem_text", 0, "not a certificate\n"},
        {"unclosed_trailing_block", 0, valid_cert + "\n-----BEGIN CERTIFICATE-----\nAAAA\n"},
        {"broken_second_block", 0,
         valid_cert + "\n-----BEGIN CERTIFICATE-----\n!!!not-base64!!!\n-----END CERTIFICATE-----\n"},
        {"empty_pem_body", 0, "-----BEGIN CERTIFICATE-----\n\n-----END CERTIFICATE-----\n"},
        {"mismatched_end_label", 0,
         "-----BEGIN CERTIFICATE-----\nAAAA\n-----END X509 CERTIFICATE-----\n"},
        {"wrong_label_trailing_key_in_cert", 0, valid_cert + "\n" + valid_key},
        {"extra_key_block", 1, valid_key + "\n" + valid_key},
        {"extra_pinned_cert_block", 4, valid_cert + "\n" + valid_cert},
        {"wrong_label_cert_in_ca", 2, valid_key},
    };
    // [状態] - 異常 PEM 入力の各パターンを用意する。

    // Pre-Assert

    // Act & Assert
    for (const auto &test_case : cases)
    {
        SCOPED_TRACE(test_case.label);
        write_binary_file_utf8(temp_file, test_case.content);
        auth_configuration cfg("client", test_case.role == 4 ? 1 : 0, "server");
        const char *pin_list[1] = {temp_file.c_str()};
        if (test_case.role == 0)
        {
            cfg.config.cert_file = temp_file.c_str();
        }
        else if (test_case.role == 1)
        {
            cfg.config.key_file = temp_file.c_str();
        }
        else if (test_case.role == 2)
        {
            cfg.config.ca_file = temp_file.c_str();
        }
        else if (test_case.role == 3)
        {
            cfg.config.crl_file = temp_file.c_str();
        }
        else
        {
            cfg.config.pinned_certs = pin_list;
        }
        potr_internal_auth_credentials *credentials = nullptr;
        int ret = potr_internal_auth_credentials_create(&cfg.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, &credentials);
        EXPECT_EQ(POTR_ERR_INVALID_CONFIG, ret); // [確認_異常系 回数=12] - 壊れた・空・過剰な PEM ファイルが POTR_ERR_INVALID_CONFIG になること。
        EXPECT_EQ(nullptr, credentials); // [確認_異常系 回数=12] - 失敗時の出力が nullptr であること。
    }

    // Cleanup
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(temp_file), ec);
}

// 暗号化秘密鍵に対して reject_password を渡し、対話入力を行わず拒否することの確認
TEST(auth_test, credentials_reject_encrypted_private_key_without_prompt)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    const std::string encrypted_key =
        "-----BEGIN ENCRYPTED PRIVATE KEY-----\n"
        "MIH0MF8GCSqGSIb3DQEFDTBSMDEGCSqGSIb3DQEFDDAkBBBtjh0QS1iJbcun+0qY\n"
        "5N7UAgIIADAMBggqhkiG9w0CCQUAMB0GCWCGSAFlAwQBKgQQuthp36pLCEihXsN/\n"
        "n03xGASBkEd8eFs/VFvPIShJKG2lBgRula4WoQHyzAL+h85LVxbwChrMMhM1MUNc\n"
        "6950HCMe+P/+Tgpx36+xX1lp4hxpbyLuga0/uvxGnXPlFmIrNxlAGHmEBquTJePH\n"
        "BLySf+zziNSIJ8tcKasmQ1jpCFS+Q7benAh+HHttljSSIiRhRLKourCSd2PjIdHP\n"
        "wA0/Zv6QZw==\n"
        "-----END ENCRYPTED PRIVATE KEY-----\n";
    const std::string temp_key = make_temp_test_path("porter-enc-key", ".pem");
    write_binary_file_utf8(temp_key, encrypted_key);
    auth_configuration client("client", 0, "server");
    client.config.key_file = temp_key.c_str();
    potr_internal_auth_credentials *credentials = nullptr;
    // [状態] - 暗号化された PKCS#8 秘密鍵ファイルを用意する。

    // Pre-Assert
    EXPECT_CALL(mock_openssl, PEM_read_bio_PrivateKey(_, _, _, _, _, Ne(nullptr), nullptr))
        .Times(1)
        .WillOnce(Invoke(delegate_real_PEM_read_bio_PrivateKey)); // [Pre-Assert確認_正常系] - reject_password コールバックが明示指定されること。

    // Act
    int actual_ret = potr_internal_auth_credentials_create(&client.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM,
                                                           &credentials); // [手順] - 暗号化秘密鍵を読み込む。

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_CONFIG, actual_ret); // [確認_異常系] - 暗号化秘密鍵が POTR_ERR_INVALID_CONFIG で拒否されること。
    EXPECT_EQ(nullptr, credentials); // [確認_異常系] - 失敗時の出力が nullptr であること。

    // Cleanup
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(temp_key), ec);
}

// 証明書チェーンの先頭と複数の中間証明書、および複数の CA 証明書を登録できることの確認
TEST(auth_test, credentials_load_cert_chain_and_multiple_ca_certs)
{
    // Arrange
    NiceMock<Mock_openssl> mock_openssl;
    const std::string client_pem = read_binary_file(data_path("client.pem"));
    const std::string ca_pem = read_binary_file(data_path("ca.pem"));
    const std::string server_pem = read_binary_file(data_path("server.pem"));
    const std::string chain_file = make_temp_test_path("porter-chain", ".pem");
    const std::string multi_ca_file = make_temp_test_path("porter-multi-ca", ".pem");
    write_binary_file_utf8(chain_file, client_pem + "\n" + ca_pem + "\n" + server_pem);
    write_binary_file_utf8(multi_ca_file, ca_pem + "\n" + server_pem);

    auth_configuration client("client", 0, "server");
    client.config.cert_file = chain_file.c_str();
    client.config.ca_file = multi_ca_file.c_str();
    potr_internal_auth_credentials *credentials = nullptr;
    // [状態] - 自端証明書 + 2 個の後続チェーン証明書と、2 個の CA 証明書を含む PEM ファイルを用意する。

    // Pre-Assert
    EXPECT_CALL(mock_openssl, SSL_CTX_ctrl(_, _, _, _, _, _, _))
        .Times(AnyNumber());
    EXPECT_CALL(mock_openssl, SSL_CTX_use_certificate(_, _, _, _, _))
        .Times(1)
        .WillOnce(Invoke(delegate_real_SSL_CTX_use_certificate));
    EXPECT_CALL(mock_openssl, SSL_CTX_ctrl(_, _, _, _, SSL_CTRL_EXTRA_CHAIN_CERT, 0L, _))
        .Times(2)
        .WillRepeatedly(Invoke(delegate_real_SSL_CTX_ctrl));
    EXPECT_CALL(mock_openssl, X509_STORE_add_cert(_, _, _, _, _))
        .Times(2)
        .WillRepeatedly(Invoke(delegate_real_X509_STORE_add_cert));

    // Act
    int actual_ret = potr_internal_auth_credentials_create(&client.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM,
                                                           &credentials); // [手順] - 証明書チェーンと複数 CA 証明書を読み込む。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - チェーン証明書と複数 CA 証明書の登録が成功すること。
    EXPECT_NE(nullptr, credentials); // [確認_正常系] - 認証情報のハンドルが生成されること。

    // Cleanup
    potr_internal_auth_credentials_dispose(credentials);
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(chain_file), ec);
    std::filesystem::remove(std::filesystem::u8path(multi_ca_file), ec);
}

// OpenSSL 確保失敗・複数エラーキュー・未分類内部エラーの分類とローカル参照・BIO 解放の確認
TEST(auth_test, credentials_classify_openssl_oom_and_queue_and_free_resources)
{
    // Arrange
    auth_configuration client("client", 0, "server");
    const std::string chain_file = make_temp_test_path("porter-chain-oom", ".pem");
    const std::string crl_file = data_path("crl.pem");
    write_binary_file_utf8(chain_file, read_binary_file(data_path("client.pem")) + "\n" + read_binary_file(data_path("ca.pem")));
    // [状態] - 証明書チェーンと CRL のテスト用入力を用意する。

    auto setup_fake_errors = [](Mock_openssl &mock_openssl, std::vector<unsigned long> &fake_errors, bool &use_fake_queue) {
        ON_CALL(mock_openssl, ERR_clear_error(_, _, _))
            .WillByDefault([&](const char *file, const int line, const char *func) {
                fake_errors.clear();
                use_fake_queue = false;
                delegate_real_ERR_clear_error(file, line, func);
            });
        ON_CALL(mock_openssl, ERR_get_error(_, _, _))
            .WillByDefault([&](const char *file, const int line, const char *func) -> unsigned long {
                if (!use_fake_queue)
                {
                    return delegate_real_ERR_get_error(file, line, func);
                }
                if (fake_errors.empty())
                {
                    return 0UL;
                }
                unsigned long next = fake_errors.front();
                fake_errors.erase(fake_errors.begin());
                return next;
            });
    };

    // 1. BIO_new() 確保失敗 -> POTR_ERR_OUT_OF_MEMORY と fclose() 実行
    {
        NiceMock<Mock_stdio> mock_stdio;
        NiceMock<Mock_openssl> mock_openssl;
        potr_internal_auth_credentials *credentials = nullptr;
        EXPECT_CALL(mock_openssl, BIO_new(_, _, _, _)).WillOnce(Return(nullptr));
        EXPECT_CALL(mock_stdio, fclose(_, _, _, _)).Times(AtLeast(1)).WillRepeatedly(Invoke(delegate_real_fclose));
        int ret = potr_internal_auth_credentials_create(&client.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, &credentials);
        EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, ret);
        EXPECT_EQ(nullptr, credentials);
    }

    // 2. BIO_write() 拡張失敗 (ERR_R_MALLOC_FAILURE -> OOM) と BIO_free()
    {
        NiceMock<Mock_openssl> mock_openssl;
        potr_internal_auth_credentials *credentials = nullptr;
        std::vector<unsigned long> fake_errors;
        bool use_fake_queue = false;
        setup_fake_errors(mock_openssl, fake_errors, use_fake_queue);
        EXPECT_CALL(mock_openssl, BIO_write(_, _, _, _, _, _))
            .WillOnce([&](const char *, const int, const char *, BIO *, const void *, int) -> int {
                fake_errors.push_back(ERR_PACK(ERR_LIB_CRYPTO, 0, ERR_R_MALLOC_FAILURE));
                use_fake_queue = true;
                return -1;
            });
        EXPECT_CALL(mock_openssl, BIO_free(_, _, _, _)).Times(AtLeast(1)).WillRepeatedly(Invoke(delegate_real_BIO_free));
        EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY,
                  potr_internal_auth_credentials_create(&client.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, &credentials));
    }

    // 3. BIO_write() 失敗 (空キュー -> UNKNOWN) と BIO_free()
    {
        NiceMock<Mock_openssl> mock_openssl;
        potr_internal_auth_credentials *credentials = nullptr;
        std::vector<unsigned long> fake_errors;
        bool use_fake_queue = false;
        setup_fake_errors(mock_openssl, fake_errors, use_fake_queue);
        EXPECT_CALL(mock_openssl, BIO_write(_, _, _, _, _, _))
            .WillOnce([&](const char *, const int, const char *, BIO *, const void *, int) -> int {
                use_fake_queue = true;
                return 0;
            });
        EXPECT_CALL(mock_openssl, BIO_free(_, _, _, _)).Times(AtLeast(1)).WillRepeatedly(Invoke(delegate_real_BIO_free));
        EXPECT_EQ(POTR_ERR_UNKNOWN,
                  potr_internal_auth_credentials_create(&client.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, &credentials));
    }

    // 4. エラー キューの末尾が別エラーでも先頭の ERR_R_MALLOC_FAILURE を優先すること
    {
        NiceMock<Mock_openssl> mock_openssl;
        potr_internal_auth_credentials *credentials = nullptr;
        std::vector<unsigned long> fake_errors;
        bool use_fake_queue = false;
        setup_fake_errors(mock_openssl, fake_errors, use_fake_queue);
        EXPECT_CALL(mock_openssl, PEM_read_bio_PrivateKey(_, _, _, _, _, _, _))
            .WillOnce([&](const char *, const int, const char *, BIO *, EVP_PKEY **, pem_password_cb *, void *) -> EVP_PKEY * {
                fake_errors.push_back(ERR_PACK(ERR_LIB_CRYPTO, 0, ERR_R_MALLOC_FAILURE));
                fake_errors.push_back(ERR_PACK(ERR_LIB_PEM, 0, PEM_R_NO_START_LINE));
                use_fake_queue = true;
                return nullptr;
            });
        int ret = potr_internal_auth_credentials_create(&client.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, &credentials);
        EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, ret);
        EXPECT_TRUE(fake_errors.empty());
    }

    // 5. SSL_CTX_use_certificate() 確保失敗時の X509_free() と BIO_free()
    {
        NiceMock<Mock_openssl> mock_openssl;
        potr_internal_auth_credentials *credentials = nullptr;
        std::vector<unsigned long> fake_errors;
        bool use_fake_queue = false;
        setup_fake_errors(mock_openssl, fake_errors, use_fake_queue);
        EXPECT_CALL(mock_openssl, SSL_CTX_use_certificate(_, _, _, _, _))
            .WillOnce([&](const char *, const int, const char *, SSL_CTX *, X509 *) -> int {
                fake_errors.push_back(ERR_PACK(ERR_LIB_CRYPTO, 0, ERR_R_MALLOC_FAILURE));
                use_fake_queue = true;
                return 0;
            });
        EXPECT_CALL(mock_openssl, X509_free(_, _, _, _)).Times(AtLeast(1)).WillRepeatedly(Invoke(delegate_real_X509_free));
        EXPECT_CALL(mock_openssl, BIO_free(_, _, _, _)).Times(AtLeast(1)).WillRepeatedly(Invoke(delegate_real_BIO_free));
        EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY,
                  potr_internal_auth_credentials_create(&client.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, &credentials));
    }

    // 6. SSL_CTX_add_extra_chain_cert() 失敗時の X509_free() と BIO_free()
    {
        NiceMock<Mock_openssl> mock_openssl;
        potr_internal_auth_credentials *credentials = nullptr;
        std::vector<unsigned long> fake_errors;
        bool use_fake_queue = false;
        setup_fake_errors(mock_openssl, fake_errors, use_fake_queue);
        auth_configuration chain_cfg("client", 0, "server");
        chain_cfg.config.cert_file = chain_file.c_str();
        EXPECT_CALL(mock_openssl, SSL_CTX_ctrl(_, _, _, _, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(mock_openssl, SSL_CTX_ctrl(_, _, _, _, SSL_CTRL_EXTRA_CHAIN_CERT, 0L, _))
            .WillOnce([&](const char *, const int, const char *, SSL_CTX *, int, long, void *) -> long {
                fake_errors.push_back(ERR_PACK(ERR_LIB_CRYPTO, 0, ERR_R_MALLOC_FAILURE));
                use_fake_queue = true;
                return 0L;
            });
        EXPECT_CALL(mock_openssl, X509_free(_, _, _, _)).Times(AtLeast(2)).WillRepeatedly(Invoke(delegate_real_X509_free));
        EXPECT_CALL(mock_openssl, BIO_free(_, _, _, _)).Times(AtLeast(1)).WillRepeatedly(Invoke(delegate_real_BIO_free));
        EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY,
                  potr_internal_auth_credentials_create(&chain_cfg.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, &credentials));
    }

    // 7. SSL_CTX_use_PrivateKey() 確保失敗時の EVP_PKEY_free()
    {
        NiceMock<Mock_openssl> mock_openssl;
        potr_internal_auth_credentials *credentials = nullptr;
        std::vector<unsigned long> fake_errors;
        bool use_fake_queue = false;
        setup_fake_errors(mock_openssl, fake_errors, use_fake_queue);
        EXPECT_CALL(mock_openssl, SSL_CTX_use_PrivateKey(_, _, _, _, _))
            .WillOnce([&](const char *, const int, const char *, SSL_CTX *, EVP_PKEY *) -> int {
                fake_errors.push_back(ERR_PACK(ERR_LIB_CRYPTO, 0, ERR_R_MALLOC_FAILURE));
                use_fake_queue = true;
                return 0;
            });
        EXPECT_CALL(mock_openssl, EVP_PKEY_free(_, _, _, _)).Times(AtLeast(1)).WillRepeatedly(Invoke(delegate_real_EVP_PKEY_free));
        EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY,
                  potr_internal_auth_credentials_create(&client.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, &credentials));
    }

    // 8. X509_STORE_add_cert() 確保失敗時の X509_free()
    {
        NiceMock<Mock_openssl> mock_openssl;
        potr_internal_auth_credentials *credentials = nullptr;
        std::vector<unsigned long> fake_errors;
        bool use_fake_queue = false;
        setup_fake_errors(mock_openssl, fake_errors, use_fake_queue);
        EXPECT_CALL(mock_openssl, X509_STORE_add_cert(_, _, _, _, _))
            .WillOnce([&](const char *, const int, const char *, X509_STORE *, X509 *) -> int {
                fake_errors.push_back(ERR_PACK(ERR_LIB_CRYPTO, 0, ERR_R_MALLOC_FAILURE));
                use_fake_queue = true;
                return 0;
            });
        EXPECT_CALL(mock_openssl, X509_free(_, _, _, _)).Times(AtLeast(1)).WillRepeatedly(Invoke(delegate_real_X509_free));
        EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY,
                  potr_internal_auth_credentials_create(&client.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, &credentials));
    }

    // 9. X509_STORE_add_crl() 内部エラー時の UNKNOWN 分類と X509_CRL_free()・BIO_free()
    {
        NiceMock<Mock_openssl> mock_openssl;
        potr_internal_auth_credentials *credentials = nullptr;
        std::vector<unsigned long> fake_errors;
        bool use_fake_queue = false;
        setup_fake_errors(mock_openssl, fake_errors, use_fake_queue);
        auth_configuration crl_cfg("client", 0, "server");
        crl_cfg.config.crl_file = crl_file.c_str();
        EXPECT_CALL(mock_openssl, X509_STORE_add_crl(_, _, _, _, _))
            .WillOnce([&](const char *, const int, const char *, X509_STORE *, X509_CRL *) -> int {
                fake_errors.push_back(ERR_PACK(ERR_LIB_CRYPTO, 0, ERR_R_INTERNAL_ERROR));
                use_fake_queue = true;
                return 0;
            });
        EXPECT_CALL(mock_openssl, X509_CRL_free(_, _, _, _)).Times(AtLeast(1)).WillRepeatedly(Invoke(delegate_real_X509_CRL_free));
        EXPECT_CALL(mock_openssl, BIO_free(_, _, _, _)).Times(AtLeast(1)).WillRepeatedly(Invoke(delegate_real_BIO_free));
        EXPECT_EQ(POTR_ERR_UNKNOWN,
                  potr_internal_auth_credentials_create(&crl_cfg.config, POTR_INTERNAL_AUTH_TRANSPORT_STREAM, &credentials));
    }

    // Cleanup
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(chain_file), ec);
}
