/**
 *******************************************************************************
 *  @file           configLoadServiceTest.cc
 *  @brief          サービス設定読み込み処理の単体テストを定義します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#include <cplat/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>

#include <mock_porter.h>
#include <porter/protocol/config.h>
#include <config_test_helper.h>
#include <mock_cplat.h>
#include <mock_stdio.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>

#include <array>
#include <cstring>
#include <ostream>

using namespace testing;

namespace
{
struct service_configuration : potr_internal_config_service
{
    service_configuration() : potr_internal_config_service{} {}
    ~service_configuration() { potr_internal_config_service_dispose(this); }
};
}


// 必須引数が NULL の場合に POTR_ERR_INVALID_ARGUMENT を返すことの確認
TEST(configLoadServiceTest, returnsInvalidArgumentWhenParameterIsNull)
{
    // Arrange
    service_configuration owned;

    // Pre-Assert

    // Act
    int actual_ret_null_path = potr_internal_config_load_service(
        nullptr, 10,
        &owned); // [手順] - config_path に nullptr を指定して potr_internal_config_load_service() を呼び出す。
    int actual_ret_null_out = potr_internal_config_load_service(
        "config.conf", 10,
        nullptr); // [手順] - 出力先に nullptr を指定して potr_internal_config_load_service() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_INVALID_ARGUMENT,
        actual_ret_null_path); // [確認_異常系] - config_path が nullptr の場合に potr_internal_config_load_service() の戻り値が POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(
        POTR_ERR_INVALID_ARGUMENT,
        actual_ret_null_out); // [確認_異常系] - 出力先が nullptr の場合に potr_internal_config_load_service() の戻り値が POTR_ERR_INVALID_ARGUMENT であること。
}

// 設定ファイルの open に失敗した場合に POTR_ERR_IO を返すことの確認
TEST(configLoadServiceTest, returnsIoErrorWhenFileCannotBeOpened)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat; // [状態] - cplat モックを用意する。
    service_configuration owned;

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("missing.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(
            nullptr)); // [Pre-Assert確認_異常系] - cplat_fopen() が "missing.conf" と等しい文字列、"r" と等しい文字列、nullptr を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - cplat_fopen() から nullptr を返す。

    // Act
    int actual_ret_open_fail = potr_internal_config_load_service(
        "missing.conf", 10,
        &owned); // [手順] - open に失敗する設定ファイルを指定して potr_internal_config_load_service() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_IO,
        actual_ret_open_fail); // [確認_異常系] - open に失敗する設定ファイルを指定した場合に potr_internal_config_load_service() の戻り値が POTR_ERR_IO であること。
}

// 指定した service ID の設定項目が正しく読み込まれ、未指定項目では service 単位の既定値が維持されることの確認
TEST(configLoadServiceTest, loadsRequestedServiceAndKeepsPerServiceDefaults)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines(
        {R"({/* JSONC block comment */"services":{"10":{"type":"unicast","dst_port":4000},"42":{"type":"tcp_bidir","dst_port":5001,"src_port":6001,"ttl":3,"pack_wait_ms":7,"src_addr1":"10.0.0.1","src_addr2":"10.0.0.2","dst_addr1":"10.0.1.1","dst_addr2":"10.0.1.2","broadcast_addr":"tcp://host,}","health_interval_ms":111,"health_timeout_ms":222,"reconnect_interval_ms":333,"connect_timeout_ms":-1,"max_peers":16,"encrypt_key":"00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFF",},"77":{"dst_port":9000}}})"});
    service_configuration owned;
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, const int, const char *, char *buf, int size, FILE *stream) -> char *
            {
                return lines.read(buf, size, stream);
            })); // [状態] - fgets() から、fgets() が呼び出された際に複数 service 定義を含む JSONC を出力バッファーに設定してそのポインターを返すように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("config.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(
            ConfigLineStream::
                handle())); // [Pre-Assert確認_正常系] - cplat_fopen() が "config.conf" と等しい文字列、"r" と等しい文字列、nullptr を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - cplat_fopen() から ConfigLineStream::handle() を返す。
    EXPECT_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle()))
        .WillOnce(Return(
            0)); // [Pre-Assert確認_正常系] - fclose() が ConfigLineStream::handle() の戻り値を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - fclose() から 0 を返す。
    EXPECT_CALL(mock_cplat, cplat_passphrase_to_key(_, _, _))
        .Times(
            0); // [Pre-Assert確認_正常系] - cplat_passphrase_to_key() が呼び出されないこと。(64 桁 hex の encrypt_key では passphrase 変換をしないことの確認)

    // Act
    int actual_ret = potr_internal_config_load_service(
        "config.conf", 42, &owned); // [手順] - service_id 42 を指定して potr_internal_config_load_service() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_OK,
              actual_ret); // [確認_正常系] - potr_internal_config_load_service() の戻り値が POTR_OK であること。
    EXPECT_EQ(
        42,
        owned.definition.service_id); // [確認_正常系] - owned.definition.service_id が 42 であること。(section 名から service_id を設定したことの確認)
    EXPECT_EQ(POTR_TYPE_TCP_BIDIR, owned.definition.type);  // [確認_正常系] - owned.definition.type が POTR_TYPE_TCP_BIDIR であること。
    EXPECT_EQ(5001U, owned.definition.dst_port);            // [確認_正常系] - owned.definition.dst_port が 5001U であること。
    EXPECT_EQ(6001U, owned.definition.src_port);            // [確認_正常系] - owned.definition.src_port が 6001U であること。
    EXPECT_EQ(3U, owned.definition.ttl);                    // [確認_正常系] - owned.definition.ttl が 3U であること。
    EXPECT_EQ(7U, owned.definition.pack_wait_ms);           // [確認_正常系] - owned.definition.pack_wait_ms が 7U であること。
    EXPECT_STREQ("10.0.0.1", owned.definition.src_addr[0]); // [確認_正常系] - owned.definition.src_addr[0] が "10.0.0.1" であること。
    EXPECT_STREQ("10.0.0.2", owned.definition.src_addr[1]); // [確認_正常系] - owned.definition.src_addr[1] が "10.0.0.2" であること。
    EXPECT_STREQ("10.0.1.1", owned.definition.dst_addr[0]); // [確認_正常系] - owned.definition.dst_addr[0] が "10.0.1.1" であること。
    EXPECT_STREQ("10.0.1.2", owned.definition.dst_addr[1]); // [確認_正常系] - owned.definition.dst_addr[1] が "10.0.1.2" であること。
    EXPECT_STREQ(
        "tcp://host,}",
        owned.definition.broadcast_addr); // [確認_正常系] - owned.definition.broadcast_addr が "tcp://host,}" であること。(文字列内の // をコメントとして扱っていないことの確認)
    EXPECT_EQ(16U, owned.definition.max_peers);              // [確認_正常系] - owned.definition.max_peers が 16U であること。
    EXPECT_EQ(111U, owned.definition.health_interval_ms);    // [確認_正常系] - owned.definition.health_interval_ms が 111U であること。
    EXPECT_EQ(222U, owned.definition.health_timeout_ms);     // [確認_正常系] - owned.definition.health_timeout_ms が 222U であること。
    EXPECT_EQ(333U, owned.definition.reconnect_interval_ms); // [確認_正常系] - owned.definition.reconnect_interval_ms が 333U であること。
    EXPECT_EQ(
        POTR_DEFAULT_CONNECT_TIMEOUT_MS,
        owned.definition.connect_timeout_ms); // [確認_正常系] - owned.definition.connect_timeout_ms が POTR_DEFAULT_CONNECT_TIMEOUT_MS であること。(負の connect_timeout_ms では既定値を維持したことの確認)
    EXPECT_EQ(
        1,
        owned.definition.encrypt_enabled); // [確認_正常系] - owned.definition.encrypt_enabled が 1 であること。(hex 形式の encrypt_key で暗号化を有効化したことの確認)
    EXPECT_EQ(
        0x00U,
        owned.definition.encrypt_key
            [0]); // [確認_正常系] - owned.definition.encrypt_key[0] が 0x00U であること。(hex 文字列の先頭バイトを変換したことの確認)
    EXPECT_EQ(
        0x11U,
        owned.definition.encrypt_key
            [1]); // [確認_正常系] - owned.definition.encrypt_key[1] が 0x11U であること。(hex 文字列の 2 バイト目を変換したことの確認)
    EXPECT_EQ(
        0x22U,
        owned.definition.encrypt_key
            [2]); // [確認_正常系] - owned.definition.encrypt_key[2] が 0x22U であること。(hex 文字列の 3 バイト目を変換したことの確認)
    EXPECT_EQ(
        0x33U,
        owned.definition.encrypt_key
            [3]); // [確認_正常系] - owned.definition.encrypt_key[3] が 0x33U であること。(hex 文字列の 4 バイト目を変換したことの確認)
}

// encrypt_key が hex 文字列でない場合にパスフレーズとして鍵導出 (ハッシュ化) されることの確認
TEST(configLoadServiceTest, hashesPassphraseWhenEncryptKeyIsNotHex)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines(
        {R"({"services":{"55":{"type":"unicast_bidir","dst_port":5001,"encrypt_key":"secret passphrase"}}})"});
    service_configuration owned;
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, const int, const char *, char *buf, int size, FILE *stream) -> char *
            {
                return lines.read(buf, size, stream);
            })); // [状態] - fgets() から、passphrase 形式の encrypt_key を含む行列を出力バッファーに設定してそのポインターを返すように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("config.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(
            ConfigLineStream::
                handle())); // [Pre-Assert確認_正常系] - cplat_fopen() が "config.conf" と等しい文字列、"r" と等しい文字列、nullptr を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - cplat_fopen() から ConfigLineStream::handle() を返す。
    EXPECT_CALL(mock_cplat, cplat_passphrase_to_key(_, _, strlen("secret passphrase")))
        .WillOnce(
            [](uint8_t *key, const uint8_t *passphrase, size_t len)
            {
                (void)passphrase;
                (void)len;
                memset(key, 0x5A, POTR_CRYPTO_KEY_SIZE);
                return 0;
            }); // [Pre-Assert確認_正常系] - cplat_passphrase_to_key() が "secret passphrase" に対する strlen() の戻り値を指定して 1 回呼び出されること。(非 hex の encrypt_key では passphrase 変換を行うことの確認)
    // [Pre-Assert手順] - cplat_passphrase_to_key() から、key に 0x5A を POTR_CRYPTO_KEY_SIZE バイト設定して 0 を返す。
    EXPECT_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle()))
        .WillOnce(Return(
            0)); // [Pre-Assert確認_正常系] - fclose() が ConfigLineStream::handle() の戻り値を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - fclose() から 0 を返す。

    // Act
    int actual_ret = potr_internal_config_load_service(
        "config.conf", 55,
        &owned); // [手順] - passphrase 形式の encrypt_key を含む service の service_id 55 を指定して potr_internal_config_load_service() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_OK,
              actual_ret); // [確認_正常系] - potr_internal_config_load_service() の戻り値が POTR_OK であること。
    EXPECT_EQ(
        1,
        owned.definition.encrypt_enabled); // [確認_正常系] - owned.definition.encrypt_enabled が 1 であること。(passphrase から鍵導出できた場合に暗号化を有効化したことの確認)
    EXPECT_EQ(
        0x5A,
        owned.definition.encrypt_key
            [0]); // [確認_正常系] - owned.definition.encrypt_key[0] が 0x5A であること。(導出した鍵を構造体へ格納したことの確認)
    EXPECT_EQ(
        0x5A,
        owned.definition.encrypt_key
            [31]); // [確認_正常系] - owned.definition.encrypt_key[31] が 0x5A であること。(導出した鍵を末尾まで保持したことの確認)
}

// パスフレーズからの鍵導出に失敗した場合に暗号化が無効化され、鍵領域がゼロクリアされることの確認
TEST(configLoadServiceTest, clearsKeyWhenPassphraseHashingFails)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"56":{"type":"tcp","dst_port":5002,"encrypt_key":"not-a-hex-secret"}}})"});
    service_configuration owned;
    memset(&owned.definition, 0xA5, sizeof(owned.definition));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, const int, const char *, char *buf, int size, FILE *stream) -> char *
            {
                return lines.read(buf, size, stream);
            })); // [状態] - fgets() から、hash 失敗を確認する service 行列を出力バッファーに設定してそのポインターを返すように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("config.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(
            ConfigLineStream::
                handle())); // [Pre-Assert確認_正常系] - cplat_fopen() が "config.conf" と等しい文字列、"r" と等しい文字列、nullptr を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - cplat_fopen() から ConfigLineStream::handle() を返す。
    EXPECT_CALL(mock_cplat, cplat_passphrase_to_key(_, _, strlen("not-a-hex-secret")))
        .WillOnce(Return(
            -1)); // [Pre-Assert確認_異常系] - cplat_passphrase_to_key() が "not-a-hex-secret" に対する strlen() の戻り値を指定して 1 回呼び出されること。
                  // [Pre-Assert手順] - cplat_passphrase_to_key() から -1 を返す。
    EXPECT_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle()))
        .WillOnce(Return(
            0)); // [Pre-Assert確認_正常系] - fclose() が ConfigLineStream::handle() の戻り値を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - fclose() から 0 を返す。

    // Act
    int actual_ret = potr_internal_config_load_service(
        "config.conf", 56,
        &owned); // [手順] - hash 失敗を起こす service の service_id 56 を指定して potr_internal_config_load_service() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_OK,
        actual_ret); // [確認_正常系] - potr_internal_config_load_service() の戻り値が POTR_OK であること。(鍵導出に失敗しても service の読込自体は成功したことの確認)
    EXPECT_EQ(
        0,
        owned.definition.encrypt_enabled); // [確認_異常系] - owned.definition.encrypt_enabled が 0 であること。(hash 失敗時に暗号化を無効として扱うことの確認)
    EXPECT_EQ(
        0,
        memcmp(
            owned.definition.encrypt_key, std::array<uint8_t, POTR_CRYPTO_KEY_SIZE>{}.data(),
            POTR_CRYPTO_KEY_SIZE)); // [確認_異常系] - memcmp() の戻り値が 0 であること。(hash 失敗時に鍵をゼロ クリアしたことの確認)
}

// 設定ファイルに対象の service ID が存在しない場合に POTR_ERR_NOT_FOUND を返すことの確認
TEST(configLoadServiceTest, returnsErrorWhenRequestedServiceDoesNotExist)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"10":{"dst_port":4000}}})"});
    service_configuration owned;
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, const int, const char *, char *buf, int size, FILE *stream) -> char *
            {
                return lines.read(buf, size, stream);
            })); // [状態] - fgets() から、fgets() が呼び出された際に対象外 service のみを含む JSONC を出力バッファーに設定してそのポインターを返すように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("config.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(
            ConfigLineStream::
                handle())); // [Pre-Assert確認_正常系] - cplat_fopen() が "config.conf" と等しい文字列、"r" と等しい文字列、nullptr を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - cplat_fopen() から ConfigLineStream::handle() を返す。
    EXPECT_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle()))
        .WillOnce(Return(
            0)); // [Pre-Assert確認_正常系] - fclose() が ConfigLineStream::handle() の戻り値を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - fclose() から 0 を返す。

    // Act
    int actual_ret = potr_internal_config_load_service(
        "config.conf", 42,
        &owned); // [手順] - 存在しない service_id を指定して potr_internal_config_load_service() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_NOT_FOUND,
        actual_ret); // [確認_異常系] - 対象 service が存在しない場合に potr_internal_config_load_service() の戻り値が POTR_ERR_NOT_FOUND であること。
}

// 負号付き文字列を採用せず既定値を維持することの確認
TEST(configLoadServiceTest, negative_zero_strings_preserve_defaults)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines(
        {R"({"services":{"10":{"ttl":"-0","connect_timeout_ms":"-0","dst_port":"-x","src_port":-0}}})"}); // [状態] - 整数設定の境界値または不正値を用意する。
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(StrEq("integer-values.jsonc"), StrEq("r"), nullptr))
        .WillByDefault(Return(
            ConfigLineStream::handle())); // [状態] - cplat_fopen() から設定用ストリームを返すように既定動作を設定する。
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, const int, const char *, char *buf, int size, FILE *stream) -> char *
            {
                return lines.read(buf, size, stream);
            })); // [状態] - fgets() から、設定の内容を順に出力バッファーに設定してそのポインターを返すように既定動作を設定する。
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle()))
        .WillByDefault(Return(0)); // [状態] - fclose() から 0 を返すように既定動作を設定する。

    // Pre-Assert

    // Act
    int actual_ret = potr_internal_config_load_service(
        "integer-values.jsonc", 10,
        &owned); // [手順] - 整数設定を含む設定ファイルを指定して potr_internal_config_load_service() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);       // [確認_正常系] - 設定の読み込みが成功すること。
    EXPECT_EQ(POTR_DEFAULT_TTL, owned.definition.ttl); // [確認_正常系] - owned.definition.ttl が POTR_DEFAULT_TTL であること。
    EXPECT_EQ(
        POTR_DEFAULT_CONNECT_TIMEOUT_MS,
        owned.definition.connect_timeout_ms); // [確認_正常系] - owned.definition.connect_timeout_ms が POTR_DEFAULT_CONNECT_TIMEOUT_MS であること。
    EXPECT_EQ(0U, owned.definition.dst_port); // [確認_正常系] - owned.definition.dst_port が 0U であること。
    EXPECT_EQ(0U, owned.definition.src_port); // [確認_正常系] - owned.definition.src_port が 0U であること。
}

// 整数型の上限値を採用することの確認
TEST(configLoadServiceTest, accepts_integer_type_upper_bounds)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines(
        {R"({"services":{"10":{"ttl":"255","dst_port":"65535","pack_wait_ms":"4294967295"}}})"}); // [状態] - 整数設定の境界値または不正値を用意する。
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(StrEq("integer-values.jsonc"), StrEq("r"), nullptr))
        .WillByDefault(Return(
            ConfigLineStream::handle())); // [状態] - cplat_fopen() から設定用ストリームを返すように既定動作を設定する。
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, const int, const char *, char *buf, int size, FILE *stream) -> char *
            {
                return lines.read(buf, size, stream);
            })); // [状態] - fgets() から、設定の内容を順に出力バッファーに設定してそのポインターを返すように既定動作を設定する。
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle()))
        .WillByDefault(Return(0)); // [状態] - fclose() から 0 を返すように既定動作を設定する。

    // Pre-Assert

    // Act
    int actual_ret = potr_internal_config_load_service(
        "integer-values.jsonc", 10,
        &owned); // [手順] - 整数設定を含む設定ファイルを指定して potr_internal_config_load_service() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);          // [確認_正常系] - 設定の読み込みが成功すること。
    EXPECT_EQ(UINT8_MAX, owned.definition.ttl);           // [確認_正常系] - owned.definition.ttl が UINT8_MAX であること。
    EXPECT_EQ(UINT16_MAX, owned.definition.dst_port);     // [確認_正常系] - owned.definition.dst_port が UINT16_MAX であること。
    EXPECT_EQ(UINT32_MAX, owned.definition.pack_wait_ms); // [確認_正常系] - owned.definition.pack_wait_ms が UINT32_MAX であること。
}

// 値域外の整数を採用せず既定値を維持することの確認
TEST(configLoadServiceTest, out_of_range_values_preserve_defaults)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines(
        {R"({"services":{"10":{"ttl":"256","dst_port":"65536","connect_timeout_ms":"4294967296"}}})"}); // [状態] - 整数設定の境界値または不正値を用意する。
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(StrEq("integer-values.jsonc"), StrEq("r"), nullptr))
        .WillByDefault(Return(
            ConfigLineStream::handle())); // [状態] - cplat_fopen() から設定用ストリームを返すように既定動作を設定する。
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, const int, const char *, char *buf, int size, FILE *stream) -> char *
            {
                return lines.read(buf, size, stream);
            })); // [状態] - fgets() から、設定の内容を順に出力バッファーに設定してそのポインターを返すように既定動作を設定する。
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle()))
        .WillByDefault(Return(0)); // [状態] - fclose() から 0 を返すように既定動作を設定する。

    // Pre-Assert

    // Act
    int actual_ret = potr_internal_config_load_service(
        "integer-values.jsonc", 10,
        &owned); // [手順] - 整数設定を含む設定ファイルを指定して potr_internal_config_load_service() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);       // [確認_正常系] - 設定の読み込みが成功すること。
    EXPECT_EQ(POTR_DEFAULT_TTL, owned.definition.ttl); // [確認_正常系] - owned.definition.ttl が POTR_DEFAULT_TTL であること。
    EXPECT_EQ(0U, owned.definition.dst_port);          // [確認_正常系] - owned.definition.dst_port が 0U であること。
    EXPECT_EQ(
        POTR_DEFAULT_CONNECT_TIMEOUT_MS,
        owned.definition.connect_timeout_ms); // [確認_正常系] - owned.definition.connect_timeout_ms が POTR_DEFAULT_CONNECT_TIMEOUT_MS であること。
}

// JSON 数値の負のゼロを 0 として採用することの確認
TEST(configLoadServiceTest, numeric_negative_zero_is_zero)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines(
        {R"({"services":{"10":{"ttl":-0,"connect_timeout_ms":-0}}})"}); // [状態] - 整数設定の境界値または不正値を用意する。
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(StrEq("integer-values.jsonc"), StrEq("r"), nullptr))
        .WillByDefault(Return(
            ConfigLineStream::handle())); // [状態] - cplat_fopen() から設定用ストリームを返すように既定動作を設定する。
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, const int, const char *, char *buf, int size, FILE *stream) -> char *
            {
                return lines.read(buf, size, stream);
            })); // [状態] - fgets() から、設定の内容を順に出力バッファーに設定してそのポインターを返すように既定動作を設定する。
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle()))
        .WillByDefault(Return(0)); // [状態] - fclose() から 0 を返すように既定動作を設定する。

    // Pre-Assert

    // Act
    int actual_ret = potr_internal_config_load_service(
        "integer-values.jsonc", 10,
        &owned); // [手順] - 整数設定を含む設定ファイルを指定して potr_internal_config_load_service() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);        // [確認_正常系] - 設定の読み込みが成功すること。
    EXPECT_EQ(0U, owned.definition.ttl);                // [確認_正常系] - owned.definition.ttl が 0U であること。
    EXPECT_EQ(0U, owned.definition.connect_timeout_ms); // [確認_正常系] - owned.definition.connect_timeout_ms が 0U であること。
}

// 全経路の優先グループと切り戻し時間の既定値を設定することの確認
TEST(configLoadServiceTest, defaults_path_groups)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    ASSERT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 戻り値が POTR_OK であること。
    for (size_t i = 0; i < POTR_MAX_PATH; i++)
    {
        EXPECT_EQ(1U, owned.definition.path_group[i]); // [確認_正常系 回数=8] - 各経路のグループが 1 であること。
    }
    EXPECT_EQ(0U, owned.definition.group_failback_ms); // [確認_正常系] - 切り戻し時間が 0 であること。
}

// 8 経路と欠番のあるグループを受理することの確認
TEST(configLoadServiceTest, accepts_eight_paths_and_sparse_groups)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"src_addr1":"127.0.0.1","dst_addr1":"127.0.1.1","path_group1":1,"src_addr2":"127.0.0.2","dst_addr2":"127.0.1.2","path_group2":1,"src_addr3":"127.0.0.3","dst_addr3":"127.0.1.3","path_group3":1,"src_addr4":"127.0.0.4","dst_addr4":"127.0.1.4","path_group4":1,"src_addr5":"127.0.0.5","dst_addr5":"127.0.1.5","path_group5":3,"src_addr6":"127.0.0.6","dst_addr6":"127.0.1.6","path_group6":3,"src_addr7":"127.0.0.7","dst_addr7":"127.0.1.7","path_group7":3,"src_addr8":"127.0.0.8","dst_addr8":"127.0.1.8","path_group8":3,"group_failback_ms":1234}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    ASSERT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 戻り値が POTR_OK であること。
    EXPECT_STREQ("127.0.0.1", owned.definition.src_addr[0]); // [確認_正常系] - 経路 1 の src_addr が復元されること。
    EXPECT_STREQ("127.0.1.1", owned.definition.dst_addr[0]); // [確認_正常系] - 経路 1 の dst_addr が復元されること。
    EXPECT_STREQ("127.0.0.2", owned.definition.src_addr[1]); // [確認_正常系] - 経路 2 の src_addr が復元されること。
    EXPECT_STREQ("127.0.1.2", owned.definition.dst_addr[1]); // [確認_正常系] - 経路 2 の dst_addr が復元されること。
    EXPECT_STREQ("127.0.0.3", owned.definition.src_addr[2]); // [確認_正常系] - 経路 3 の src_addr が復元されること。
    EXPECT_STREQ("127.0.1.3", owned.definition.dst_addr[2]); // [確認_正常系] - 経路 3 の dst_addr が復元されること。
    EXPECT_STREQ("127.0.0.4", owned.definition.src_addr[3]); // [確認_正常系] - 経路 4 の src_addr が復元されること。
    EXPECT_STREQ("127.0.1.4", owned.definition.dst_addr[3]); // [確認_正常系] - 経路 4 の dst_addr が復元されること。
    EXPECT_STREQ("127.0.0.5", owned.definition.src_addr[4]); // [確認_正常系] - 経路 5 の src_addr が復元されること。
    EXPECT_STREQ("127.0.1.5", owned.definition.dst_addr[4]); // [確認_正常系] - 経路 5 の dst_addr が復元されること。
    EXPECT_STREQ("127.0.0.6", owned.definition.src_addr[5]); // [確認_正常系] - 経路 6 の src_addr が復元されること。
    EXPECT_STREQ("127.0.1.6", owned.definition.dst_addr[5]); // [確認_正常系] - 経路 6 の dst_addr が復元されること。
    EXPECT_STREQ("127.0.0.7", owned.definition.src_addr[6]); // [確認_正常系] - 経路 7 の src_addr が復元されること。
    EXPECT_STREQ("127.0.1.7", owned.definition.dst_addr[6]); // [確認_正常系] - 経路 7 の dst_addr が復元されること。
    EXPECT_STREQ("127.0.0.8", owned.definition.src_addr[7]); // [確認_正常系] - 経路 8 の src_addr が復元されること。
    EXPECT_STREQ("127.0.1.8", owned.definition.dst_addr[7]); // [確認_正常系] - 経路 8 の dst_addr が復元されること。
    EXPECT_EQ(1U, owned.definition.path_group[0]); // [確認_正常系] - グループ 1 が保持されること。
    EXPECT_EQ(3U, owned.definition.path_group[7]); // [確認_正常系] - グループ 2 が欠番でもグループ 3 を受理すること。
    EXPECT_EQ(1234U, owned.definition.group_failback_ms); // [確認_正常系] - 切り戻し時間が 1234 であること。
}

// アドレス未設定の経路のグループを保持することの確認
TEST(configLoadServiceTest, retains_group_without_address)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"path_group8":4}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    ASSERT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 戻り値が POTR_OK であること。
    EXPECT_STREQ("", owned.definition.src_addr[7]); // [確認_正常系] - 経路 8 のアドレスが未設定であること。
    EXPECT_EQ(4U, owned.definition.path_group[7]); // [確認_正常系] - 経路 8 のグループが 4 であること。
}

// 上限を超える経路キー src_addr9 を拒否することの確認
TEST(configLoadServiceTest, rejects_src_addr9)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"src_addr9":null}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 経路番号が上限を超えると設定エラーであること。
}

// 上限を超える経路キー dst_addr9 を拒否することの確認
TEST(configLoadServiceTest, rejects_dst_addr9)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"dst_addr9":null}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 経路番号が上限を超えると設定エラーであること。
}

// 上限を超える経路キー path_group9 を拒否することの確認
TEST(configLoadServiceTest, rejects_path_group9)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"path_group9":null}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 経路番号が上限を超えると設定エラーであること。
}

// 上限を超える経路キー src_addr10 を拒否することの確認
TEST(configLoadServiceTest, rejects_src_addr10)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"src_addr10":null}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 経路番号が上限を超えると設定エラーであること。
}

// 上限を超える経路キー dst_addr100 を拒否することの確認
TEST(configLoadServiceTest, rejects_dst_addr100)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"dst_addr100":null}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 経路番号が上限を超えると設定エラーであること。
}

// 上限を超える経路キー path_group4294967296 を拒否することの確認
TEST(configLoadServiceTest, rejects_path_group4294967296)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"path_group4294967296":null}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 経路番号が上限を超えると設定エラーであること。
}

// 範囲外または不正な優先グループを拒否することの確認
TEST(configLoadServiceTest, rejects_path_group_zero)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"path_group8":0}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - アドレス未設定でも不正なグループは設定エラーであること。
}

// 範囲外または不正な優先グループを拒否することの確認
TEST(configLoadServiceTest, rejects_path_group_five)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"path_group8":5}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - アドレス未設定でも不正なグループは設定エラーであること。
}

// 範囲外または不正な優先グループを拒否することの確認
TEST(configLoadServiceTest, rejects_path_group_overflow)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"path_group8":256}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - アドレス未設定でも不正なグループは設定エラーであること。
}

// 範囲外または不正な優先グループを拒否することの確認
TEST(configLoadServiceTest, rejects_path_group_negative)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"path_group8":-1}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - アドレス未設定でも不正なグループは設定エラーであること。
}

// 範囲外または不正な優先グループを拒否することの確認
TEST(configLoadServiceTest, rejects_path_group_fraction)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"path_group8":1.5}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - アドレス未設定でも不正なグループは設定エラーであること。
}

// 範囲外または不正な優先グループを拒否することの確認
TEST(configLoadServiceTest, rejects_path_group_null)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"path_group8":null}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - アドレス未設定でも不正なグループは設定エラーであること。
}

// 範囲外または不正な優先グループを拒否することの確認
TEST(configLoadServiceTest, rejects_path_group_text)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"path_group8":"invalid"}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - アドレス未設定でも不正なグループは設定エラーであること。
}

// 不正な切り戻し時間を拒否することの確認
TEST(configLoadServiceTest, rejects_failback_negative)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"group_failback_ms":-1}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 不正な切り戻し時間は設定エラーであること。
}

// 不正な切り戻し時間を拒否することの確認
TEST(configLoadServiceTest, rejects_failback_overflow)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"group_failback_ms":4294967296}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 不正な切り戻し時間は設定エラーであること。
}

// uint32 最大の切り戻し時間を受理することの確認
TEST(configLoadServiceTest, accepts_failback_uint32_max)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"group_failback_ms":4294967295}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    ASSERT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 戻り値が POTR_OK であること。
    EXPECT_EQ(UINT32_MAX, owned.definition.group_failback_ms); // [確認_正常系] - 切り戻し時間が uint32 最大値であること。
}

// RAW モードで明示したグループについて警告することの確認
TEST(configLoadServiceTest, warns_explicit_group_in_unicast_raw)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"path_group8":1,"type":"unicast_raw"}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_WARNING, nullptr,
                                                 HasSubstr("path_group is ignored in RAW mode"))).Times(1);
    // [Pre-Assert確認_正常系] - WARNING と path_group の無視を示す文字列でログが 1 回出力されること。
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - RAW モードでも設定の読み込みが成功すること。
    for (size_t path_idx = 0U; path_idx < POTR_MAX_PATH; path_idx++)
    {
        EXPECT_EQ(0U, owned.definition.path_group[path_idx]); // [確認_正常系 回数=8] - 優先グループが未指定値にリセットされること。
    }
}

// RAW モードで明示したグループについて警告することの確認
TEST(configLoadServiceTest, warns_explicit_group_in_multicast_raw)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"path_group8":1,"type":"multicast_raw"}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_WARNING, nullptr,
                                                 HasSubstr("path_group is ignored in RAW mode"))).Times(1);
    // [Pre-Assert確認_正常系] - WARNING と path_group の無視を示す文字列でログが 1 回出力されること。
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - RAW モードでも設定の読み込みが成功すること。
    for (size_t path_idx = 0U; path_idx < POTR_MAX_PATH; path_idx++)
    {
        EXPECT_EQ(0U, owned.definition.path_group[path_idx]); // [確認_正常系 回数=8] - 優先グループが未指定値にリセットされること。
    }
}

// RAW モードで明示したグループについて警告することの確認
TEST(configLoadServiceTest, warns_explicit_group_in_broadcast_raw)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"path_group8":1,"type":"broadcast_raw"}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
            {
                return lines.read(buffer, size, stream);
            }));
    // [状態] - JSON のサービス設定を読み込むモックを用意する。
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_WARNING, nullptr,
                                                 HasSubstr("path_group is ignored in RAW mode"))).Times(1);
    // [Pre-Assert確認_正常系] - WARNING と path_group の無視を示す文字列でログが 1 回出力されること。
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned);
    // [手順] - サービス 42 の設定を読み込む。
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - RAW モードでも設定の読み込みが成功すること。
    for (size_t path_idx = 0U; path_idx < POTR_MAX_PATH; path_idx++)
    {
        EXPECT_EQ(0U, owned.definition.path_group[path_idx]); // [確認_正常系 回数=8] - 優先グループが未指定値にリセットされること。
    }
}

// 証明書設定の文字列と配列を所有し、解放時に参照を消去することの確認
TEST(configLoadServiceTest, owns_certificate_strings_and_arrays)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"42":{"auth":"cert","cert_file":"self.pem","key_file":"self-key.pem","ca_file":"ca.pem","crl_file":"crl.pem","pinned_certs":["first.pem","second.pem"],"peer_names":["peer.test","urn:peer"]}}})"});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke([&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
        {
            return lines.read(buffer, size, stream);
        }));

    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned); // [手順] - 証明書方式の全設定項目を読み込む。

    // Assert
    ASSERT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 全設定の読み込みが成功すること。
    EXPECT_EQ(POTR_AUTH_CERT, owned.definition.auth); // [確認_正常系] - 証明書方式を選択すること。
    EXPECT_STREQ("self.pem", owned.definition.cert_file); // [確認_正常系] - 証明書パスを保持すること。
    EXPECT_STREQ("self-key.pem", owned.definition.key_file); // [確認_正常系] - 秘密鍵パスを保持すること。
    EXPECT_STREQ("ca.pem", owned.definition.ca_file); // [確認_正常系] - CA パスを保持すること。
    EXPECT_STREQ("crl.pem", owned.definition.crl_file); // [確認_正常系] - CRL パスを保持すること。
    ASSERT_EQ(2U, owned.definition.pinned_cert_count); // [確認_正常系] - 直接登録の件数が 2 件であること。
    EXPECT_STREQ("second.pem", owned.definition.pinned_certs[1]); // [確認_正常系] - 直接登録の第 2 要素を保持すること。
    ASSERT_EQ(2U, owned.definition.peer_name_count); // [確認_正常系] - 相手名の件数が 2 件であること。
    EXPECT_STREQ("urn:peer", owned.definition.peer_names[1]); // [確認_正常系] - URI の相手名を保持すること。

    // Cleanup
    potr_internal_config_service_dispose(&owned);
}

namespace
{
struct certificate_config_case
{
    const char *entry;
};

void PrintTo(const certificate_config_case &value, std::ostream *output)
{
    *output << "entry: \"" << value.entry << "\"";
}
}

class certificate_config_rejection_test : public TestWithParam<certificate_config_case> {};

// 認証設定の型や方式が不正な場合に読み込みを失敗させることの確認
TEST_P(certificate_config_rejection_test, rejects_invalid_authentication_setting)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    std::string json = std::string("{\"services\":{\"42\":{\"cert_file\":\"self.pem\",") + GetParam().entry + "}}}";
    ConfigLineStream lines({json.c_str()});
    service_configuration owned;
    ON_CALL(mock_cplat, cplat_fopen(_, _, _)).WillByDefault(Return(ConfigLineStream::handle()));
    ON_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillByDefault(Return(0));
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke([&](const char *, int, const char *, char *buffer, int size, FILE *stream) -> char *
        {
            return lines.read(buffer, size, stream);
        }));

    // Pre-Assert
    // Act
    int actual_ret = potr_internal_config_load_service("config.conf", 42, &owned); // [手順] - 型や認証方式が不正な設定を読み込む。

    // Assert
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, actual_ret); // [確認_異常系] - 設定の不正を引数エラーとして返すこと。
    EXPECT_EQ(nullptr, owned.document); // [確認_異常系] - 途中まで読み込んだ文書を解放すること。
    EXPECT_EQ(nullptr, owned.definition.cert_file); // [確認_異常系] - 解放した文字列の参照を残さないこと。
}

INSTANTIATE_TEST_SUITE_P(invalid_values, certificate_config_rejection_test, Values(
    certificate_config_case{"\"auth\":\"unknown\""}, certificate_config_case{"\"auth\":1"},
    certificate_config_case{"\"auth\":null"}, certificate_config_case{"\"key_file\":3"},
    certificate_config_case{"\"ca_file\":\"\""}, certificate_config_case{"\"crl_file\":false"},
    certificate_config_case{"\"pinned_certs\":\"peer.pem\""}, certificate_config_case{"\"pinned_certs\":[\"peer.pem\",2]"},
    certificate_config_case{"\"peer_names\":{}"}, certificate_config_case{"\"peer_names\":[null]"}));
