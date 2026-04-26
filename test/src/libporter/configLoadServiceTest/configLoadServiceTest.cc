#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>

#include <config.h>
#include <config_test_helper.h>
#include <mock_com_util.h>
#include <porter_const.h>

#include <array>
#include <cstring>

using namespace testing;

TEST(configLoadServiceTest, returnsErrorWhenArgumentIsInvalidOrFileCannotBeOpened)
{
    NiceMock<Mock_com_util> mock_com_util;
    PotrServiceDef          def = {};

    // Arrange

    // Pre-Assert
    EXPECT_CALL(mock_com_util, com_util_fopen(StrEq("missing.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(nullptr)); // [Pre-Assert確認_異常系] - 存在しない設定ファイルの open が 1 回試行されること。

    // Act
    int rtc_null_path = config_load_service(nullptr, 10, &def);           // [手順] - config_path を NULL にして呼び出す。
    int rtc_null_out  = config_load_service("config.conf", 10, nullptr);   // [手順] - 出力先を NULL にして呼び出す。
    int rtc_open_fail = config_load_service("missing.conf", 10, &def);     // [手順] - open に失敗する設定ファイルを指定して呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc_null_path); // [確認_異常系] - config_path が NULL の場合に POTR_ERROR を返すこと。
    EXPECT_EQ(POTR_ERROR, rtc_null_out);  // [確認_異常系] - 出力先が NULL の場合に POTR_ERROR を返すこと。
    EXPECT_EQ(POTR_ERROR, rtc_open_fail); // [確認_異常系] - open 失敗時に POTR_ERROR を返すこと。
}

TEST(configLoadServiceTest, loadsRequestedServiceAndKeepsPerServiceDefaults)
{
    NiceMock<Mock_com_util> mock_com_util;
    ConfigLineStream        lines({
        "[service.10]\n",
        "type = unicast\n",
        "dst_port = 4000\n",
        "[service.42]\n",
        "type = tcp_bidir\n",
        "dst_port = 5001\n",
        "src_port = 6001\n",
        "ttl = 3\n",
        "pack_wait_ms = 7\n",
        "src_addr1 = 10.0.0.1\n",
        "src_addr2 = 10.0.0.2\n",
        "dst_addr1 = 10.0.1.1\n",
        "dst_addr2 = 10.0.1.2\n",
        "health_interval_ms = 111\n",
        "health_timeout_ms = 222\n",
        "reconnect_interval_ms = 333\n",
        "connect_timeout_ms = -1\n",
        "max_peers = 16\n",
        "encrypt_key = 00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFF\n",
        "[service.77]\n",
        "dst_port = 9000\n",
    });
    PotrServiceDef def = {};

    // Arrange

    // Pre-Assert
    EXPECT_CALL(mock_com_util, com_util_fopen(StrEq("config.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(ConfigLineStream::handle())); // [Pre-Assert確認_正常系] - 設定ファイル open が 1 回呼び出されること。
    ON_CALL(mock_com_util, com_util_fgets(_, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke([&](char *buf, int size, FILE *stream) -> char * {
            return lines.read(buf, size, stream);
        })); // [Pre-Assert手順] - 複数 service section を含む行列を順に返す。
    EXPECT_CALL(mock_com_util, com_util_fclose(ConfigLineStream::handle()))
        .WillOnce(Return(0)); // [Pre-Assert確認_正常系] - 読み込み完了時に fclose が 1 回呼び出されること。
    EXPECT_CALL(mock_com_util, com_util_passphrase_to_key(_, _, _))
        .Times(0); // [Pre-Assert確認_正常系] - 64 桁 hex の encrypt_key では passphrase 変換を呼ばないこと。

    // Act
    int rtc = config_load_service("config.conf", 42, &def); // [手順] - service_id 42 の設定を読み込む。

    // Assert
    EXPECT_EQ(POTR_SUCCESS, rtc);                                      // [確認_正常系] - 対象 service の読み込みに成功すること。
    EXPECT_EQ(42, def.service_id);                                     // [確認_正常系] - service_id を section 名から設定すること。
    EXPECT_EQ(POTR_TYPE_TCP_BIDIR, def.type);                          // [確認_正常系] - type を読み込むこと。
    EXPECT_EQ(5001U, def.dst_port);                                    // [確認_正常系] - dst_port を読み込むこと。
    EXPECT_EQ(6001U, def.src_port);                                    // [確認_正常系] - src_port を読み込むこと。
    EXPECT_EQ(3U, def.ttl);                                            // [確認_正常系] - ttl を読み込むこと。
    EXPECT_EQ(7U, def.pack_wait_ms);                                   // [確認_正常系] - pack_wait_ms を読み込むこと。
    EXPECT_STREQ("10.0.0.1", def.src_addr[0]);                         // [確認_正常系] - src_addr1 を読み込むこと。
    EXPECT_STREQ("10.0.0.2", def.src_addr[1]);                         // [確認_正常系] - src_addr2 を読み込むこと。
    EXPECT_STREQ("10.0.1.1", def.dst_addr[0]);                         // [確認_正常系] - dst_addr1 を読み込むこと。
    EXPECT_STREQ("10.0.1.2", def.dst_addr[1]);                         // [確認_正常系] - dst_addr2 を読み込むこと。
    EXPECT_EQ(16U, def.max_peers);                                     // [確認_正常系] - max_peers を読み込むこと。
    EXPECT_EQ(111U, def.health_interval_ms);                           // [確認_正常系] - health_interval_ms を読み込むこと。
    EXPECT_EQ(222U, def.health_timeout_ms);                            // [確認_正常系] - health_timeout_ms を読み込むこと。
    EXPECT_EQ(333U, def.reconnect_interval_ms);                        // [確認_正常系] - reconnect_interval_ms を読み込むこと。
    EXPECT_EQ(POTR_DEFAULT_CONNECT_TIMEOUT_MS, def.connect_timeout_ms); // [確認_正常系] - 負の connect_timeout_ms は既定値を維持すること。
    EXPECT_EQ(1, def.encrypt_enabled);                                 // [確認_正常系] - hex 形式の encrypt_key で暗号化を有効化すること。
    EXPECT_EQ(0x00U, def.encrypt_key[0]);                              // [確認_正常系] - hex 文字列を 32 バイト鍵へ変換すること。
    EXPECT_EQ(0x11U, def.encrypt_key[1]);                              // [確認_正常系] - hex 文字列の 2 バイト目を正しく変換すること。
    EXPECT_EQ(0x22U, def.encrypt_key[2]);                              // [確認_正常系] - hex 文字列の 3 バイト目を正しく変換すること。
    EXPECT_EQ(0x33U, def.encrypt_key[3]);                              // [確認_正常系] - hex 文字列の 4 バイト目を正しく変換すること。
}

TEST(configLoadServiceTest, hashesPassphraseWhenEncryptKeyIsNotHex)
{
    NiceMock<Mock_com_util> mock_com_util;
    ConfigLineStream        lines({
        "[service.55]\n",
        "type = unicast_bidir\n",
        "dst_port = 5001\n",
        "encrypt_key = secret passphrase\n",
    });
    PotrServiceDef def = {};

    // Arrange

    // Pre-Assert
    EXPECT_CALL(mock_com_util, com_util_fopen(StrEq("config.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(ConfigLineStream::handle())); // [Pre-Assert確認_正常系] - 設定ファイル open が 1 回呼び出されること。
    ON_CALL(mock_com_util, com_util_fgets(_, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke([&](char *buf, int size, FILE *stream) -> char * {
            return lines.read(buf, size, stream);
        })); // [Pre-Assert手順] - passphrase 形式の encrypt_key を含む行列を順に返す。
    EXPECT_CALL(mock_com_util, com_util_passphrase_to_key(_, _, strlen("secret passphrase")))
        .WillOnce([](uint8_t *key, const uint8_t *passphrase, size_t len) {
            (void)passphrase;
            (void)len;
            memset(key, 0x5A, POTR_CRYPTO_KEY_SIZE);
            return 0;
        }); // [Pre-Assert確認_正常系] - 非 hex の encrypt_key では passphrase 変換が 1 回呼び出されること。
    EXPECT_CALL(mock_com_util, com_util_fclose(ConfigLineStream::handle()))
        .WillOnce(Return(0)); // [Pre-Assert確認_正常系] - 読み込み完了時に fclose が 1 回呼び出されること。

    // Act
    int rtc = config_load_service("config.conf", 55, &def); // [手順] - passphrase 形式の encrypt_key を含む service を読み込む。

    // Assert
    EXPECT_EQ(POTR_SUCCESS, rtc);              // [確認_正常系] - 読み込みに成功すること。
    EXPECT_EQ(1, def.encrypt_enabled);         // [確認_正常系] - passphrase から鍵導出できた場合に暗号化を有効化すること。
    EXPECT_EQ(0x5A, def.encrypt_key[0]);       // [確認_正常系] - 導出した鍵を構造体へ格納すること。
    EXPECT_EQ(0x5A, def.encrypt_key[31]);      // [確認_正常系] - 導出した鍵を末尾まで保持すること。
}

TEST(configLoadServiceTest, clearsKeyWhenPassphraseHashingFails)
{
    NiceMock<Mock_com_util> mock_com_util;
    ConfigLineStream        lines({
        "[service.56]\n",
        "type = tcp\n",
        "dst_port = 5002\n",
        "encrypt_key = not-a-hex-secret\n",
    });
    PotrServiceDef def = {};
    memset(&def, 0xA5, sizeof(def));

    // Arrange

    // Pre-Assert
    EXPECT_CALL(mock_com_util, com_util_fopen(StrEq("config.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(ConfigLineStream::handle())); // [Pre-Assert確認_正常系] - 設定ファイル open が 1 回呼び出されること。
    ON_CALL(mock_com_util, com_util_fgets(_, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke([&](char *buf, int size, FILE *stream) -> char * {
            return lines.read(buf, size, stream);
        })); // [Pre-Assert手順] - hash 失敗を確認する service 行列を順に返す。
    EXPECT_CALL(mock_com_util, com_util_passphrase_to_key(_, _, strlen("not-a-hex-secret")))
        .WillOnce(Return(-1)); // [Pre-Assert確認_異常系] - passphrase 変換失敗を 1 回返すこと。
    EXPECT_CALL(mock_com_util, com_util_fclose(ConfigLineStream::handle()))
        .WillOnce(Return(0)); // [Pre-Assert確認_正常系] - 読み込み完了時に fclose が 1 回呼び出されること。

    // Act
    int rtc = config_load_service("config.conf", 56, &def); // [手順] - hash 失敗を起こす service を読み込む。

    // Assert
    EXPECT_EQ(POTR_SUCCESS, rtc);                      // [確認_正常系] - service の読込自体は成功すること。
    EXPECT_EQ(0, def.encrypt_enabled);                 // [確認_異常系] - hash 失敗時に暗号化を無効として扱うこと。
    EXPECT_EQ(0, memcmp(def.encrypt_key,
                        std::array<uint8_t, POTR_CRYPTO_KEY_SIZE>{}.data(),
                        POTR_CRYPTO_KEY_SIZE));        // [確認_異常系] - hash 失敗時に鍵をゼロクリアすること。
}

TEST(configLoadServiceTest, returnsErrorWhenRequestedServiceDoesNotExist)
{
    NiceMock<Mock_com_util> mock_com_util;
    ConfigLineStream        lines({
        "[service.10]\n",
        "dst_port = 4000\n",
    });
    PotrServiceDef def = {};

    // Arrange

    // Pre-Assert
    EXPECT_CALL(mock_com_util, com_util_fopen(StrEq("config.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(ConfigLineStream::handle())); // [Pre-Assert確認_正常系] - 設定ファイル open が 1 回呼び出されること。
    ON_CALL(mock_com_util, com_util_fgets(_, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke([&](char *buf, int size, FILE *stream) -> char * {
            return lines.read(buf, size, stream);
        })); // [Pre-Assert手順] - 対象外 service のみを含む行列を順に返す。
    EXPECT_CALL(mock_com_util, com_util_fclose(ConfigLineStream::handle()))
        .WillOnce(Return(0)); // [Pre-Assert確認_正常系] - 読み込み完了時に fclose が呼び出されること。

    // Act
    int rtc = config_load_service("config.conf", 42, &def); // [手順] - 存在しない service_id を指定して読み込む。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc); // [確認_異常系] - 対象 service が無い場合に POTR_ERROR を返すこと。
}
