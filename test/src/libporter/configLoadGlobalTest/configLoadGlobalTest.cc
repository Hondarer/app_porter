/**
 *******************************************************************************
 *  @file           configLoadGlobalTest.cc
 *  @brief          グローバル設定読み込み処理の単体テストを定義します。
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

#include <cstring>
#include <ostream>
#include <string>

using namespace testing;

namespace
{
/** 解釈できない global の設定値と、維持される既定値の組。 */
struct unparsed_global_case
{
    const char *name;
    const char *key;
    const char *value;
    uint32_t expected;
};

void PrintTo(const unparsed_global_case &, std::ostream *);
void PrintTo(const unparsed_global_case &value, std::ostream *output)
{
    *output << "name: \"" << value.name << "\", key: \"" << value.key << "\", value: \"" << value.value
            << "\", expected: " << value.expected;
}

/** 設定キーに対応する global 設定構造体の値を返す。 */
uint32_t global_field(const potr_global_config &global, const char *key)
{
    if (strcmp(key, "window_size") == 0)
    {
        return global.window_size;
    }
    if (strcmp(key, "max_payload") == 0)
    {
        return global.max_payload;
    }
    if (strcmp(key, "udp_health_interval_ms") == 0)
    {
        return global.udp_health_interval_ms;
    }
    if (strcmp(key, "udp_health_timeout_ms") == 0)
    {
        return global.udp_health_timeout_ms;
    }
    if (strcmp(key, "tcp_health_interval_ms") == 0)
    {
        return global.tcp_health_interval_ms;
    }
    if (strcmp(key, "tcp_health_timeout_ms") == 0)
    {
        return global.tcp_health_timeout_ms;
    }
    if (strcmp(key, "tcp_close_timeout_ms") == 0)
    {
        return global.tcp_close_timeout_ms;
    }
    if (strcmp(key, "reorder_timeout_ms") == 0)
    {
        return global.reorder_timeout_ms;
    }
    if (strcmp(key, "max_message_size") == 0)
    {
        return global.max_message_size;
    }
    return global.send_queue_depth;
}
} // namespace

class configLoadGlobalUnparsedTest : public TestWithParam<unparsed_global_case>
{
};

// 必須引数が NULL の場合に POTR_ERR_INVALID_ARGUMENT を返すことの確認
TEST(configLoadGlobalTest, returnsInvalidArgumentWhenParameterIsNull)
{
    // Arrange
    potr_global_config global = {}; // [状態] - 出力用 global 設定構造体をゼロ初期化する。

    // Pre-Assert

    // Act
    int actual_ret_null_path = potr_internal_config_load_global(
        nullptr, &global); // [手順] - config_path に nullptr を指定して potr_internal_config_load_global() を呼び出す。
    int actual_ret_null_out = potr_internal_config_load_global(
        "config.conf", nullptr); // [手順] - 出力先に nullptr を指定して potr_internal_config_load_global() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_INVALID_ARGUMENT,
        actual_ret_null_path); // [確認_異常系] - config_path が nullptr の場合に potr_internal_config_load_global() の戻り値が POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(
        POTR_ERR_INVALID_ARGUMENT,
        actual_ret_null_out); // [確認_異常系] - 出力先が nullptr の場合に potr_internal_config_load_global() の戻り値が POTR_ERR_INVALID_ARGUMENT であること。
}

// 設定ファイルの open に失敗した場合に POTR_ERR_IO を返すことの確認
TEST(configLoadGlobalTest, returnsIoErrorWhenFileCannotBeOpened)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat; // [状態] - cplat モックを用意する。
    potr_global_config global = {};  // [状態] - 出力用 global 設定構造体をゼロ初期化する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("missing.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(
            nullptr)); // [Pre-Assert確認_異常系] - cplat_fopen() が "missing.conf" と等しい文字列、"r" と等しい文字列、nullptr を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - cplat_fopen() から nullptr を返す。

    // Act
    int actual_ret_open_fail = potr_internal_config_load_global(
        "missing.conf",
        &global); // [手順] - open に失敗する設定ファイルを指定して potr_internal_config_load_global() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_IO,
        actual_ret_open_fail); // [確認_異常系] - open に失敗する設定ファイルを指定した場合に potr_internal_config_load_global() の戻り値が POTR_ERR_IO であること。
}

// 設定ファイルから global オブジェクトの設定値が正しく読み込まれ、他キーや未知のキーが無視されることの確認
TEST(configLoadGlobalTest, loadsGlobalOverridesAndIgnoresOtherSections)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines(
        {R"({"services":{"10":{"udp_health_interval_ms":999,},},"global":{"window_size":32,"max_payload":1200,"udp_health_interval_ms":111,"udp_health_timeout_ms":222,"tcp_health_interval_ms":333,"tcp_health_timeout_ms":444,"tcp_close_timeout_ms":555,"reorder_timeout_ms":12,"max_message_size":4096,"send_queue_depth":48,"unknown_key":999,},"unused":["a,b",],})"});
    potr_global_config global = {};
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, const int, const char *, char *buf, int size, FILE *stream) -> char *
            {
                return lines.read(buf, size, stream);
            })); // [状態] - fgets() から、global 定義を含む JSONC を出力バッファーに設定してそのポインターを返すように既定動作を設定する。

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
    int actual_ret = potr_internal_config_load_global(
        "config.conf",
        &global); // [手順] - global 定義を含む設定ファイルを指定して potr_internal_config_load_global() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_OK,
              actual_ret); // [確認_正常系] - potr_internal_config_load_global() の戻り値が POTR_OK であること。
    EXPECT_EQ(32U, global.window_size);   // [確認_正常系] - global.window_size が 32U であること。
    EXPECT_EQ(1200U, global.max_payload); // [確認_正常系] - global.max_payload が 1200U であること。
    EXPECT_EQ(111U,
              global.udp_health_interval_ms);      // [確認_正常系] - global.udp_health_interval_ms が 111U であること。
    EXPECT_EQ(222U, global.udp_health_timeout_ms); // [確認_正常系] - global.udp_health_timeout_ms が 222U であること。
    EXPECT_EQ(333U,
              global.tcp_health_interval_ms);      // [確認_正常系] - global.tcp_health_interval_ms が 333U であること。
    EXPECT_EQ(444U, global.tcp_health_timeout_ms); // [確認_正常系] - global.tcp_health_timeout_ms が 444U であること。
    EXPECT_EQ(555U, global.tcp_close_timeout_ms);  // [確認_正常系] - global.tcp_close_timeout_ms が 555U であること。
    EXPECT_EQ(12U, global.reorder_timeout_ms);     // [確認_正常系] - global.reorder_timeout_ms が 12U であること。
    EXPECT_EQ(4096U, global.max_message_size);     // [確認_正常系] - global.max_message_size が 4096U であること。
    EXPECT_EQ(48U, global.send_queue_depth);       // [確認_正常系] - global.send_queue_depth が 48U であること。
}

// 設定ファイルに global オブジェクトが存在しない場合に全項目で既定値が維持されることの確認
TEST(configLoadGlobalTest, keepsDefaultsWhenGlobalSectionIsMissing)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"services":{"10":{"dst_port":5001,"type":"unicast"}}})"});
    potr_global_config global = {};
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, const int, const char *, char *buf, int size, FILE *stream) -> char *
            {
                return lines.read(buf, size, stream);
            })); // [状態] - fgets() から、services のみを含む JSONC を出力バッファーに設定してそのポインターを返すように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("service-only.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(
            ConfigLineStream::
                handle())); // [Pre-Assert確認_正常系] - cplat_fopen() が "service-only.conf" と等しい文字列、"r" と等しい文字列、nullptr を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - cplat_fopen() から ConfigLineStream::handle() を返す。
    EXPECT_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle()))
        .WillOnce(Return(
            0)); // [Pre-Assert確認_正常系] - fclose() が ConfigLineStream::handle() の戻り値を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - fclose() から 0 を返す。

    // Act
    int actual_ret = potr_internal_config_load_global(
        "service-only.conf",
        &global); // [手順] - global を含まない設定ファイルを指定して potr_internal_config_load_global() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_OK,
        actual_ret); // [確認_正常系] - global が存在しない場合に potr_internal_config_load_global() の戻り値が POTR_OK であること。
    EXPECT_EQ(POTR_DEFAULT_WINDOW_SIZE,
              global.window_size); // [確認_正常系] - global.window_size が POTR_DEFAULT_WINDOW_SIZE であること。
    EXPECT_EQ(POTR_DEFAULT_MAX_PAYLOAD,
              global.max_payload); // [確認_正常系] - global.max_payload が POTR_DEFAULT_MAX_PAYLOAD であること。
    EXPECT_EQ(0U, global.reorder_timeout_ms); // [確認_正常系] - global.reorder_timeout_ms が 0U であること。
    EXPECT_EQ(POTR_MAX_MESSAGE_SIZE,
              global.max_message_size); // [確認_正常系] - global.max_message_size が POTR_MAX_MESSAGE_SIZE であること。
    EXPECT_EQ(POTR_SEND_QUEUE_DEPTH,
              global.send_queue_depth); // [確認_正常系] - global.send_queue_depth が POTR_SEND_QUEUE_DEPTH であること。
    EXPECT_EQ(
        POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS,
        global
            .udp_health_interval_ms); // [確認_正常系] - global.udp_health_interval_ms が POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS であること。
    EXPECT_EQ(
        POTR_DEFAULT_UDP_HEALTH_TIMEOUT_MS,
        global
            .udp_health_timeout_ms); // [確認_正常系] - global.udp_health_timeout_ms が POTR_DEFAULT_UDP_HEALTH_TIMEOUT_MS であること。
    EXPECT_EQ(
        POTR_DEFAULT_TCP_HEALTH_INTERVAL_MS,
        global
            .tcp_health_interval_ms); // [確認_正常系] - global.tcp_health_interval_ms が POTR_DEFAULT_TCP_HEALTH_INTERVAL_MS であること。
    EXPECT_EQ(
        POTR_DEFAULT_TCP_HEALTH_TIMEOUT_MS,
        global
            .tcp_health_timeout_ms); // [確認_正常系] - global.tcp_health_timeout_ms が POTR_DEFAULT_TCP_HEALTH_TIMEOUT_MS であること。
    EXPECT_EQ(
        POTR_DEFAULT_TCP_CLOSE_TIMEOUT_MS,
        global
            .tcp_close_timeout_ms); // [確認_正常系] - global.tcp_close_timeout_ms が POTR_DEFAULT_TCP_CLOSE_TIMEOUT_MS であること。
}

// 空のオブジェクトに置かれたカンマは末尾カンマとして受け付けないことの確認
TEST(configLoadGlobalTest, rejectsCommaWithoutPrecedingMember)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat; // [状態] - cplat モックを用意する。
    NiceMock<Mock_stdio> mock_stdio; // [状態] - stdio モックを用意する。
    ConfigLineStream lines({R"({"global":{,}})"});
    potr_global_config global = {}; // [状態] - 出力用 global 設定構造体をゼロ初期化する。
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, const int, const char *, char *buf, int size, FILE *stream) -> char *
            {
                return lines.read(buf, size, stream);
            })); // [状態] - fgets() から、不正なカンマ配置を含む JSONC を出力バッファーに設定してそのポインターを返すように既定動作を設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("invalid.json"), StrEq("r"), nullptr))
        .WillOnce(Return(
            ConfigLineStream::
                handle())); // [Pre-Assert確認_異常系] - cplat_fopen() が "invalid.json" と等しい文字列、"r" と等しい文字列、nullptr を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - cplat_fopen() から ConfigLineStream::handle() を返す。
    EXPECT_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle()))
        .WillOnce(Return(
            0)); // [Pre-Assert確認_異常系] - fclose() が ConfigLineStream::handle() の戻り値を指定して 1 回呼び出されること。
    // [Pre-Assert手順] - fclose() から 0 を返す。

    // Act
    int actual_ret = potr_internal_config_load_global(
        "invalid.json",
        &global); // [手順] - 不正なカンマを含む定義を指定して potr_internal_config_load_global() を呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_INVALID_ARGUMENT,
        actual_ret); // [確認_異常系] - 項目を伴わないカンマに対する potr_internal_config_load_global() の戻り値が POTR_ERR_INVALID_ARGUMENT であること。
}

// 負号付き文字列を採用せず既定値を維持することの確認
TEST(configLoadGlobalTest, negative_zero_strings_preserve_defaults)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines(
        {R"({"global":{"window_size":"-0","udp_health_interval_ms":"-0","tcp_close_timeout_ms":"-x","send_queue_depth":-0}})"}); // [状態] - 整数設定の境界値または不正値を用意する。
    potr_global_config global = {};
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
    int actual_ret = potr_internal_config_load_global(
        "integer-values.jsonc",
        &global); // [手順] - 整数設定を含む設定ファイルを指定して potr_internal_config_load_global() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 設定の読み込みが成功すること。
    EXPECT_EQ(POTR_DEFAULT_WINDOW_SIZE,
              global.window_size); // [確認_正常系] - global.window_size が POTR_DEFAULT_WINDOW_SIZE であること。
    EXPECT_EQ(
        POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS,
        global
            .udp_health_interval_ms); // [確認_正常系] - global.udp_health_interval_ms が POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS であること。
    EXPECT_EQ(
        POTR_DEFAULT_TCP_CLOSE_TIMEOUT_MS,
        global
            .tcp_close_timeout_ms); // [確認_正常系] - global.tcp_close_timeout_ms が POTR_DEFAULT_TCP_CLOSE_TIMEOUT_MS であること。
    EXPECT_EQ(0U, global.send_queue_depth); // [確認_正常系] - global.send_queue_depth が 0U であること。
}

// 整数型の上限値を採用することの確認
TEST(configLoadGlobalTest, accepts_integer_type_upper_bounds)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines(
        {R"({"global":{"window_size":"65535","send_queue_depth":"4294967295"}})"}); // [状態] - 整数設定の境界値または不正値を用意する。
    potr_global_config global = {};
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
    int actual_ret = potr_internal_config_load_global(
        "integer-values.jsonc",
        &global); // [手順] - 整数設定を含む設定ファイルを指定して potr_internal_config_load_global() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret);            // [確認_正常系] - 設定の読み込みが成功すること。
    EXPECT_EQ(UINT16_MAX, global.window_size); // [確認_正常系] - global.window_size が UINT16_MAX であること。
    EXPECT_EQ(UINT32_MAX,
              global.send_queue_depth); // [確認_正常系] - global.send_queue_depth が UINT32_MAX であること。
}

// 値域外の整数を採用せず既定値を維持することの確認
TEST(configLoadGlobalTest, out_of_range_values_preserve_defaults)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines(
        {R"({"global":{"window_size":"65536","send_queue_depth":"4294967296"}})"}); // [状態] - 整数設定の境界値または不正値を用意する。
    potr_global_config global = {};
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
    int actual_ret = potr_internal_config_load_global(
        "integer-values.jsonc",
        &global); // [手順] - 整数設定を含む設定ファイルを指定して potr_internal_config_load_global() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 設定の読み込みが成功すること。
    EXPECT_EQ(POTR_DEFAULT_WINDOW_SIZE,
              global.window_size); // [確認_正常系] - global.window_size が POTR_DEFAULT_WINDOW_SIZE であること。
    EXPECT_EQ(POTR_SEND_QUEUE_DEPTH,
              global.send_queue_depth); // [確認_正常系] - global.send_queue_depth が POTR_SEND_QUEUE_DEPTH であること。
}

// 解釈できない global の設定値で WARNING を出し、既定値を維持することの確認
TEST_P(configLoadGlobalUnparsedTest, warns_and_keeps_default_when_value_cannot_be_parsed)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    const unparsed_global_case &param = GetParam();
    std::string json = std::string("{\"global\":{\"") + param.key + "\":\"" + param.value + "\"}}";
    ConfigLineStream lines({json.c_str()}); // [状態] - 1 項目だけに解釈できない値を持つ global 定義を用意する。
    potr_global_config global = {};
    ON_CALL(mock_cplat, cplat_fopen(StrEq("unparsed.jsonc"), StrEq("r"), nullptr))
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
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, _, _, _))
        .Times(AnyNumber()); // [Pre-Assert確認_正常系 回数=PARAM] - cplat_tracer_writef_at() が任意の回数呼び出されること。
    EXPECT_CALL(mock_cplat, cplat_tracer_writef_at(_, CPLAT_TRACE_LEVEL_WARNING, nullptr,
                                                   AllOf(HasSubstr(param.key), HasSubstr(param.value))))
        .Times(1); // [Pre-Assert確認_異常系 回数=PARAM] - cplat_tracer_writef_at() が CPLAT_TRACE_LEVEL_WARNING、nullptr、項目名と値を含む文字列を指定して 1 回呼び出されること。

    // Act
    int actual_ret = potr_internal_config_load_global(
        "unparsed.jsonc",
        &global); // [手順] - 解釈できない値を含む設定ファイルを指定して potr_internal_config_load_global() を呼び出す。

    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系 回数=PARAM] - 設定の読み込みが成功すること。
    EXPECT_EQ(param.expected,
              global_field(global, param.key)); // [確認_異常系 回数=PARAM] - 解釈できない値を与えた項目が既定値のままであること。
}

INSTANTIATE_TEST_SUITE_P(
    , configLoadGlobalUnparsedTest,
    Values(unparsed_global_case{"window_size_text", "window_size", "abc", POTR_DEFAULT_WINDOW_SIZE},
           unparsed_global_case{"max_payload_out_of_range", "max_payload", "65536", POTR_DEFAULT_MAX_PAYLOAD},
           unparsed_global_case{"udp_health_interval_ms_negative", "udp_health_interval_ms", "-1",
                                POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS},
           unparsed_global_case{"udp_health_timeout_ms_out_of_range", "udp_health_timeout_ms", "4294967296",
                                POTR_DEFAULT_UDP_HEALTH_TIMEOUT_MS},
           unparsed_global_case{"tcp_health_interval_ms_text", "tcp_health_interval_ms", "abc",
                                POTR_DEFAULT_TCP_HEALTH_INTERVAL_MS},
           unparsed_global_case{"tcp_health_timeout_ms_negative", "tcp_health_timeout_ms", "-1",
                                POTR_DEFAULT_TCP_HEALTH_TIMEOUT_MS},
           unparsed_global_case{"tcp_close_timeout_ms_out_of_range", "tcp_close_timeout_ms", "4294967296",
                                POTR_DEFAULT_TCP_CLOSE_TIMEOUT_MS},
           unparsed_global_case{"reorder_timeout_ms_text", "reorder_timeout_ms", "abc", 0U},
           unparsed_global_case{"max_message_size_negative", "max_message_size", "-1", POTR_MAX_MESSAGE_SIZE},
           unparsed_global_case{"send_queue_depth_out_of_range", "send_queue_depth", "4294967296",
                                POTR_SEND_QUEUE_DEPTH}),
    [](const TestParamInfo<unparsed_global_case> &param_info) { return std::string(param_info.param.name); });