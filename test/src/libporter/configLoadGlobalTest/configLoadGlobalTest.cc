#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>

#include <config.h>
#include <config_test_helper.h>
#include <mock_com_util.h>
#include <porter_const.h>

using namespace testing;

TEST(configLoadGlobalTest, returnsErrorWhenArgumentIsInvalidOrFileCannotBeOpened)
{
    NiceMock<Mock_com_util> mock_com_util;
    PotrGlobalConfig        global = {};

    // Arrange

    // Pre-Assert
    EXPECT_CALL(mock_com_util, com_util_fopen(StrEq("missing.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(nullptr)); // [Pre-Assert確認_異常系] - 存在しない設定ファイルの open が 1 回試行されること。

    // Act
    int rtc_null_path = config_load_global(nullptr, &global);         // [手順] - config_path を NULL にして呼び出す。
    int rtc_null_out  = config_load_global("config.conf", nullptr);   // [手順] - 出力先を NULL にして呼び出す。
    int rtc_open_fail = config_load_global("missing.conf", &global);  // [手順] - open に失敗する設定ファイルを指定して呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc_null_path); // [確認_異常系] - config_path が NULL の場合に POTR_ERROR を返すこと。
    EXPECT_EQ(POTR_ERROR, rtc_null_out);  // [確認_異常系] - 出力先が NULL の場合に POTR_ERROR を返すこと。
    EXPECT_EQ(POTR_ERROR, rtc_open_fail); // [確認_異常系] - open 失敗時に POTR_ERROR を返すこと。
}

TEST(configLoadGlobalTest, loadsGlobalOverridesAndIgnoresOtherSections)
{
    NiceMock<Mock_com_util> mock_com_util;
    ConfigLineStream        lines({
        "  # comment\n",
        "\n",
        "[service.10]\n",
        "udp_health_interval_ms = 999\n",
        "[global]\n",
        "window_size = 32\n",
        "max_payload = 1200\n",
        "udp_health_interval_ms = 111\n",
        "udp_health_timeout_ms = 222\n",
        "tcp_health_interval_ms = 333\n",
        "tcp_health_timeout_ms = 444\n",
        "tcp_close_timeout_ms = 555\n",
        "reorder_timeout_ms = 12\n",
        "max_message_size = 4096\n",
        "send_queue_depth = 48\n",
        "unknown_key = 999\n",
    });
    PotrGlobalConfig global = {};

    // Arrange

    // Pre-Assert
    EXPECT_CALL(mock_com_util, com_util_fopen(StrEq("config.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(ConfigLineStream::handle())); // [Pre-Assert確認_正常系] - 設定ファイル open が 1 回呼び出されること。
    ON_CALL(mock_com_util, com_util_fgets(_, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke([&](char *buf, int size, FILE *stream) -> char * {
            return lines.read(buf, size, stream);
        })); // [Pre-Assert手順] - com_util_fgets() がメモリ上の [global] 行列を順に返す。
    EXPECT_CALL(mock_com_util, com_util_fclose(ConfigLineStream::handle()))
        .WillOnce(Return(0)); // [Pre-Assert確認_正常系] - 読み込み完了時に fclose が 1 回呼び出されること。

    // Act
    int rtc = config_load_global("config.conf", &global); // [手順] - [global] を含む設定を読み込む。

    // Assert
    EXPECT_EQ(POTR_SUCCESS, rtc);                    // [確認_正常系] - 読み込みに成功すること。
    EXPECT_EQ(32U, global.window_size);              // [確認_正常系] - window_size を設定値で上書きすること。
    EXPECT_EQ(1200U, global.max_payload);            // [確認_正常系] - max_payload を設定値で上書きすること。
    EXPECT_EQ(111U, global.udp_health_interval_ms);  // [確認_正常系] - [global] の UDP interval を読み込むこと。
    EXPECT_EQ(222U, global.udp_health_timeout_ms);   // [確認_正常系] - [global] の UDP timeout を読み込むこと。
    EXPECT_EQ(333U, global.tcp_health_interval_ms);  // [確認_正常系] - [global] の TCP interval を読み込むこと。
    EXPECT_EQ(444U, global.tcp_health_timeout_ms);   // [確認_正常系] - [global] の TCP timeout を読み込むこと。
    EXPECT_EQ(555U, global.tcp_close_timeout_ms);    // [確認_正常系] - tcp_close_timeout_ms を読み込むこと。
    EXPECT_EQ(12U, global.reorder_timeout_ms);       // [確認_正常系] - reorder_timeout_ms を読み込むこと。
    EXPECT_EQ(4096U, global.max_message_size);       // [確認_正常系] - max_message_size を読み込むこと。
    EXPECT_EQ(48U, global.send_queue_depth);         // [確認_正常系] - send_queue_depth を読み込むこと。
}

TEST(configLoadGlobalTest, keepsDefaultsWhenGlobalSectionIsMissing)
{
    NiceMock<Mock_com_util> mock_com_util;
    ConfigLineStream        lines({
        "[service.10]\n",
        "dst_port = 5001\n",
        "type = unicast\n",
    });
    PotrGlobalConfig global = {};

    // Arrange

    // Pre-Assert
    EXPECT_CALL(mock_com_util, com_util_fopen(StrEq("service-only.conf"), StrEq("r"), nullptr))
        .WillOnce(Return(ConfigLineStream::handle())); // [Pre-Assert確認_正常系] - 設定ファイル open が 1 回呼び出されること。
    ON_CALL(mock_com_util, com_util_fgets(_, _, ConfigLineStream::handle()))
        .WillByDefault(Invoke([&](char *buf, int size, FILE *stream) -> char * {
            return lines.read(buf, size, stream);
        })); // [Pre-Assert手順] - [service] のみを含む行列を順に返す。
    EXPECT_CALL(mock_com_util, com_util_fclose(ConfigLineStream::handle()))
        .WillOnce(Return(0)); // [Pre-Assert確認_正常系] - 読み込み完了時に fclose が呼び出されること。

    // Act
    int rtc = config_load_global("service-only.conf", &global); // [手順] - [global] を含まない設定を読み込む。

    // Assert
    EXPECT_EQ(POTR_SUCCESS, rtc);                                  // [確認_正常系] - [global] が無くても成功すること。
    EXPECT_EQ(POTR_DEFAULT_WINDOW_SIZE, global.window_size);       // [確認_正常系] - window_size が既定値のままであること。
    EXPECT_EQ(POTR_DEFAULT_MAX_PAYLOAD, global.max_payload);       // [確認_正常系] - max_payload が既定値のままであること。
    EXPECT_EQ(0U, global.reorder_timeout_ms);                      // [確認_正常系] - reorder_timeout_ms が既定値のままであること。
    EXPECT_EQ(POTR_MAX_MESSAGE_SIZE, global.max_message_size);     // [確認_正常系] - max_message_size が既定値のままであること。
    EXPECT_EQ(POTR_SEND_QUEUE_DEPTH, global.send_queue_depth);     // [確認_正常系] - send_queue_depth が既定値のままであること。
    EXPECT_EQ(POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS,
              global.udp_health_interval_ms);                      // [確認_正常系] - UDP interval が既定値のままであること。
    EXPECT_EQ(POTR_DEFAULT_UDP_HEALTH_TIMEOUT_MS,
              global.udp_health_timeout_ms);                       // [確認_正常系] - UDP timeout が既定値のままであること。
    EXPECT_EQ(POTR_DEFAULT_TCP_HEALTH_INTERVAL_MS,
              global.tcp_health_interval_ms);                      // [確認_正常系] - TCP interval が既定値のままであること。
    EXPECT_EQ(POTR_DEFAULT_TCP_HEALTH_TIMEOUT_MS,
              global.tcp_health_timeout_ms);                       // [確認_正常系] - TCP timeout が既定値のままであること。
    EXPECT_EQ(POTR_DEFAULT_TCP_CLOSE_TIMEOUT_MS,
              global.tcp_close_timeout_ms);                        // [確認_正常系] - TCP close timeout が既定値のままであること。
}
