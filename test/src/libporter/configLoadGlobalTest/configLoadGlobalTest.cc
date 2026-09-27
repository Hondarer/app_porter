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

using namespace testing;

// 必須引数が NULL の場合に POTR_ERR_INVALID_ARGUMENT を返すことの確認
TEST(configLoadGlobalTest, returnsInvalidArgumentWhenParameterIsNull)
{
    // Arrange
    potr_global_config global = {};

    // Pre-Assert

    // Act
    int actual_ret_null_path = potr_internal_config_load_global(nullptr, &global);      // [手順] - config_path を NULL にして呼び出す。
    int actual_ret_null_out = potr_internal_config_load_global("config.conf", nullptr); // [手順] - 出力先を NULL にして呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_INVALID_ARGUMENT,
        actual_ret_null_path); // [確認_異常系] - config_path が NULL の場合に potr_internal_config_load_global の戻り値が POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(
        POTR_ERR_INVALID_ARGUMENT,
        actual_ret_null_out); // [確認_異常系] - 出力先が NULL の場合に potr_internal_config_load_global の戻り値が POTR_ERR_INVALID_ARGUMENT であること。
}

// 設定ファイルの open に失敗した場合に POTR_ERR_IO を返すことの確認
TEST(configLoadGlobalTest, returnsIoErrorWhenFileCannotBeOpened)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_global_config global = {};

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("missing.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(nullptr)); // [Pre-Assert確認_異常系] - 存在しない設定ファイルの open が 1 回試行されること。

    // Act
    int actual_ret_open_fail =
        potr_internal_config_load_global("missing.conf", &global); // [手順] - open に失敗する設定ファイルを指定して呼び出す。

    // Assert
    EXPECT_EQ(
        POTR_ERR_IO,
        actual_ret_open_fail); // [確認_異常系] - open に失敗する設定ファイルを指定した場合に potr_internal_config_load_global の戻り値が POTR_ERR_IO であること。
}

// 設定ファイルから global オブジェクトの設定値が正しく読み込まれ、他キーや未知のキーが無視されることの確認
TEST(configLoadGlobalTest, loadsGlobalOverridesAndIgnoresOtherSections)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({
        R"({"services":{"10":{"udp_health_interval_ms":999,},},"global":{"window_size":32,"max_payload":1200,"udp_health_interval_ms":111,"udp_health_timeout_ms":222,"tcp_health_interval_ms":333,"tcp_health_timeout_ms":444,"tcp_close_timeout_ms":555,"reorder_timeout_ms":12,"max_message_size":4096,"send_queue_depth":48,"unknown_key":999,},"unused":["a,b",],})"
    });
    potr_global_config global = {};
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, const int, const char *, char *buf, int size, FILE *stream) -> char *
            {
                return lines.read(buf, size, stream);
            })); // [状態] - fgets が呼び出された際に global 定義を含む JSONC を返すようにモックを設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("config.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(
            ConfigLineStream::handle())); // [Pre-Assert確認_正常系] - 設定ファイル open が 1 回呼び出されること。
    EXPECT_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle()))
        .WillOnce(Return(0)); // [Pre-Assert確認_正常系] - 読み込み完了時に fclose が 1 回呼び出されること。

    // Act
    int actual_ret = potr_internal_config_load_global("config.conf", &global); // [手順] - global 定義を含む設定を読み込む。

    // Assert
    EXPECT_EQ(POTR_OK,
              actual_ret); // [確認_正常系] - potr_internal_config_load_global の戻り値から、読み込みに成功したと判断できること。
    EXPECT_EQ(32U, global.window_size);             // [確認_正常系] - window_size を設定値で上書きすること。
    EXPECT_EQ(1200U, global.max_payload);           // [確認_正常系] - max_payload を設定値で上書きすること。
    EXPECT_EQ(111U, global.udp_health_interval_ms); // [確認_正常系] - global の UDP interval を読み込むこと。
    EXPECT_EQ(222U, global.udp_health_timeout_ms);  // [確認_正常系] - global の UDP timeout を読み込むこと。
    EXPECT_EQ(333U, global.tcp_health_interval_ms); // [確認_正常系] - global の TCP interval を読み込むこと。
    EXPECT_EQ(444U, global.tcp_health_timeout_ms);  // [確認_正常系] - global の TCP timeout を読み込むこと。
    EXPECT_EQ(555U, global.tcp_close_timeout_ms);   // [確認_正常系] - tcp_close_timeout_ms を読み込むこと。
    EXPECT_EQ(12U, global.reorder_timeout_ms);      // [確認_正常系] - reorder_timeout_ms を読み込むこと。
    EXPECT_EQ(4096U, global.max_message_size);      // [確認_正常系] - max_message_size を読み込むこと。
    EXPECT_EQ(48U, global.send_queue_depth);        // [確認_正常系] - send_queue_depth を読み込むこと。
}

// 設定ファイルに global オブジェクトが存在しない場合に全項目で既定値が維持されることの確認
TEST(configLoadGlobalTest, keepsDefaultsWhenGlobalSectionIsMissing)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({
        R"({"services":{"10":{"dst_port":5001,"type":"unicast"}}})"
    });
    potr_global_config global = {};
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, const int, const char *, char *buf, int size, FILE *stream) -> char *
            {
                return lines.read(buf, size, stream);
            })); // [状態] - fgets が呼び出された際に services のみを含む JSONC を返すようにモックを設定する。

    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("service-only.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(
            ConfigLineStream::handle())); // [Pre-Assert確認_正常系] - 設定ファイル open が 1 回呼び出されること。
    EXPECT_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle()))
        .WillOnce(Return(0)); // [Pre-Assert確認_正常系] - 読み込み完了時に fclose が呼び出されること。

    // Act
    int actual_ret = potr_internal_config_load_global("service-only.conf", &global); // [手順] - global を含まない設定を読み込む。

    // Assert
    EXPECT_EQ(POTR_OK,
              actual_ret); // [確認_正常系] - potr_internal_config_load_global の戻り値から、[global] が無くても成功したと判断できること。
    EXPECT_EQ(POTR_DEFAULT_WINDOW_SIZE, global.window_size); // [確認_正常系] - window_size が既定値のままであること。
    EXPECT_EQ(POTR_DEFAULT_MAX_PAYLOAD, global.max_payload); // [確認_正常系] - max_payload が既定値のままであること。
    EXPECT_EQ(0U, global.reorder_timeout_ms); // [確認_正常系] - reorder_timeout_ms が既定値のままであること。
    EXPECT_EQ(POTR_MAX_MESSAGE_SIZE,
              global.max_message_size); // [確認_正常系] - max_message_size が既定値のままであること。
    EXPECT_EQ(POTR_SEND_QUEUE_DEPTH,
              global.send_queue_depth); // [確認_正常系] - send_queue_depth が既定値のままであること。
    EXPECT_EQ(POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS,
              global.udp_health_interval_ms); // [確認_正常系] - UDP interval が既定値のままであること。
    EXPECT_EQ(POTR_DEFAULT_UDP_HEALTH_TIMEOUT_MS,
              global.udp_health_timeout_ms); // [確認_正常系] - UDP timeout が既定値のままであること。
    EXPECT_EQ(POTR_DEFAULT_TCP_HEALTH_INTERVAL_MS,
              global.tcp_health_interval_ms); // [確認_正常系] - TCP interval が既定値のままであること。
    EXPECT_EQ(POTR_DEFAULT_TCP_HEALTH_TIMEOUT_MS,
              global.tcp_health_timeout_ms); // [確認_正常系] - TCP timeout が既定値のままであること。
    EXPECT_EQ(POTR_DEFAULT_TCP_CLOSE_TIMEOUT_MS,
              global.tcp_close_timeout_ms); // [確認_正常系] - TCP close timeout が既定値のままであること。
}

// 空のオブジェクトに置かれたカンマは末尾カンマとして受け付けないことの確認
TEST(configLoadGlobalTest, rejectsCommaWithoutPrecedingMember)
{
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_stdio> mock_stdio;
    ConfigLineStream lines({R"({"global":{,}})"});
    potr_global_config global = {};
    ON_CALL(mock_stdio, fgets(_, _, _, _, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke(
            [&](const char *, const int, const char *, char *buf, int size, FILE *stream) -> char *
            {
                return lines.read(buf, size, stream);
            }));

    EXPECT_CALL(mock_cplat, cplat_fopen(StrEq("invalid.json"), StrEq("r"), nullptr))
        .WillOnce(Return(ConfigLineStream::handle()));
    EXPECT_CALL(mock_stdio, fclose(_, _, _, ConfigLineStream::handle())).WillOnce(Return(0));

    int actual_ret = potr_internal_config_load_global("invalid.json", &global); // [手順] - 不正なカンマを含む定義を読む。

    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT,
              actual_ret); // [確認_異常系] - 項目を伴わないカンマを構文エラーとして返すこと。
}
