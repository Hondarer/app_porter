#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>

#include <config.h>
#include <porter_const.h>
#include <porter_test_helper.h>

#include <cstdlib>

TEST(configFileLoadTest, loadsGlobalServiceAndServiceIdsFromRealFile)
{
    PorterConfigBuilder builder;
    PotrGlobalConfig    global  = {};
    PotrServiceDef      service = {};
    int64_t            *ids     = nullptr;
    int                 count   = 0;

    std::string config_path = builder
        .setUdpHealthIntervalMs(111U)
        .setUdpHealthTimeoutMs(222U)
        .setTcpHealthIntervalMs(333U)
        .setTcpHealthTimeoutMs(444U)
        .setTcpCloseTimeoutMs(555U)
        .addUnicastService(1001, 5001, "127.0.0.1")
        .addTcpBidirService(2002, 6002, "127.0.0.1")
        .build();

    // Arrange
    ASSERT_FALSE(config_path.empty()); // [状態] - 実ファイルへ [global] と 2 つの service section を書き出す。

    // Pre-Assert

    // Act
    int rtc_global = config_load_global(config_path.c_str(), &global);              // [手順] - 実ファイルから [global] を読み込む。
    int rtc_service = config_load_service(config_path.c_str(), 2002, &service);     // [手順] - 実ファイルから service_id 2002 を読み込む。
    int rtc_ids = config_list_service_ids(config_path.c_str(), &ids, &count);       // [手順] - 実ファイルから service ID 一覧を列挙する。

    // Assert
    ASSERT_EQ(POTR_SUCCESS, rtc_global);                     // [確認_正常系] - [global] の読込に成功すること。
    ASSERT_EQ(POTR_SUCCESS, rtc_service);                    // [確認_正常系] - service の読込に成功すること。
    ASSERT_EQ(POTR_SUCCESS, rtc_ids);                        // [確認_正常系] - service ID 列挙に成功すること。
    EXPECT_EQ(16U, global.window_size);                      // [確認_正常系] - Builder が出力した window_size を読み込むこと。
    EXPECT_EQ(1400U, global.max_payload);                    // [確認_正常系] - Builder が出力した max_payload を読み込むこと。
    EXPECT_EQ(111U, global.udp_health_interval_ms);          // [確認_正常系] - udp_health_interval_ms を実ファイルから読み込むこと。
    EXPECT_EQ(222U, global.udp_health_timeout_ms);           // [確認_正常系] - udp_health_timeout_ms を実ファイルから読み込むこと。
    EXPECT_EQ(333U, global.tcp_health_interval_ms);          // [確認_正常系] - tcp_health_interval_ms を実ファイルから読み込むこと。
    EXPECT_EQ(444U, global.tcp_health_timeout_ms);           // [確認_正常系] - tcp_health_timeout_ms を実ファイルから読み込むこと。
    EXPECT_EQ(555U, global.tcp_close_timeout_ms);            // [確認_正常系] - tcp_close_timeout_ms を実ファイルから読み込むこと。
    EXPECT_EQ(2002, service.service_id);                     // [確認_正常系] - 指定 service_id の service を読み込むこと。
    EXPECT_EQ(POTR_TYPE_TCP_BIDIR, service.type);            // [確認_正常系] - type を実ファイルから読み込むこと。
    EXPECT_EQ(6002U, service.dst_port);                      // [確認_正常系] - dst_port を実ファイルから読み込むこと。
    ASSERT_NE(nullptr, ids);                                 // [確認_正常系] - service ID 配列が確保されること。
    ASSERT_EQ(2, count);                                     // [確認_正常系] - 2 件の service ID を列挙すること。
    EXPECT_EQ(1001, ids[0]);                                 // [確認_正常系] - 1 件目の service ID を保持すること。
    EXPECT_EQ(2002, ids[1]);                                 // [確認_正常系] - 2 件目の service ID を保持すること。

    free(ids);
}
