#include <cplat/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>

#include <mock_porter.h>
#include <porter/protocol/config.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include <porter_test_helper.h>

#include <cstdlib>

// 実ファイルから global、service、service ID 一覧が読み込めることの確認
TEST(configFileLoadTest, loadsGlobalServiceAndServiceIdsFromRealFile)
{
    // Arrange
    PorterConfigBuilder builder;
    potr_global_config global = {};
    potr_service_def service = {};
    int64_t *ids = nullptr;
    int count = 0;

    std::string config_path = builder.setUdpHealthIntervalMs(111U)
                                  .setUdpHealthTimeoutMs(222U)
                                  .setTcpHealthIntervalMs(333U)
                                  .setTcpHealthTimeoutMs(444U)
                                  .setTcpCloseTimeoutMs(555U)
                                  .addUnicastService(1001, 5001, "127.0.0.1")
                                  .addUnicastService(INT64_MAX, 5003, "127.0.0.1")
                                  .addTcpBidirService(2002, 6002, "127.0.0.1")
                                  .build();

    ASSERT_FALSE(config_path.empty()); // [状態] - 実ファイルへ global と 3 つの service オブジェクトを書き出す。
                                       // [状態確認] - config_path.empty() の戻り値が false であること。

    // Pre-Assert

    // Act
    int actual_ret_global =
        potr_internal_config_load_global(config_path.c_str(), &global); // [手順] - 実ファイルから global を読み込む。
    int actual_ret_service =
        potr_internal_config_load_service(config_path.c_str(), 2002,
                                          &service); // [手順] - 実ファイルから service_id 2002 を読み込む。
    int actual_ret_ids =
        potr_internal_config_list_service_ids(config_path.c_str(), &ids,
                                              &count); // [手順] - 実ファイルから service ID 一覧を列挙する。

    // Assert
    ASSERT_EQ(POTR_OK,
              actual_ret_global); // [確認_正常系] - potr_internal_config_load_global() の戻り値が POTR_OK であること。
    ASSERT_EQ(
        POTR_OK,
        actual_ret_service); // [確認_正常系] - potr_internal_config_load_service() の戻り値が POTR_OK であること。
    ASSERT_EQ(
        POTR_OK,
        actual_ret_ids); // [確認_正常系] - potr_internal_config_list_service_ids() の戻り値が POTR_OK であること。
    EXPECT_EQ(
        16U,
        global
            .window_size); // [確認_正常系] - global.window_size が 16U であること。(Builder が出力した window_size を読み込んだことの確認)
    EXPECT_EQ(
        1400U,
        global
            .max_payload); // [確認_正常系] - global.max_payload が 1400U であること。(Builder が出力した max_payload を読み込んだことの確認)
    EXPECT_EQ(
        111U,
        global
            .udp_health_interval_ms); // [確認_正常系] - global.udp_health_interval_ms が 111U であること。(実ファイルへ書いた udp_health_interval_ms を読み込んだことの確認)
    EXPECT_EQ(
        222U,
        global
            .udp_health_timeout_ms); // [確認_正常系] - global.udp_health_timeout_ms が 222U であること。(実ファイルへ書いた udp_health_timeout_ms を読み込んだことの確認)
    EXPECT_EQ(
        333U,
        global
            .tcp_health_interval_ms); // [確認_正常系] - global.tcp_health_interval_ms が 333U であること。(実ファイルへ書いた tcp_health_interval_ms を読み込んだことの確認)
    EXPECT_EQ(
        444U,
        global
            .tcp_health_timeout_ms); // [確認_正常系] - global.tcp_health_timeout_ms が 444U であること。(実ファイルへ書いた tcp_health_timeout_ms を読み込んだことの確認)
    EXPECT_EQ(
        555U,
        global
            .tcp_close_timeout_ms); // [確認_正常系] - global.tcp_close_timeout_ms が 555U であること。(実ファイルへ書いた tcp_close_timeout_ms を読み込んだことの確認)
    EXPECT_EQ(
        2002,
        service
            .service_id); // [確認_正常系] - service.service_id が 2002 であること。(指定した service_id の service を読み込んだことの確認)
    EXPECT_EQ(POTR_TYPE_TCP_BIDIR, service.type); // [確認_正常系] - service.type が POTR_TYPE_TCP_BIDIR であること。
    EXPECT_EQ(
        6002U,
        service
            .dst_port); // [確認_正常系] - service.dst_port が 6002U であること。(実ファイルへ書いた dst_port を読み込んだことの確認)
    ASSERT_NE(nullptr, ids); // [確認_正常系] - ids が nullptr 以外であること。(service ID 配列が確保されたことの確認)
    ASSERT_EQ(3, count);     // [確認_正常系] - count が 3 であること。(3 件の service ID を列挙したことの確認)
    EXPECT_EQ(1001,
              ids[0]); // [確認_正常系] - ids[0] が 1001 であること。(1 件目の service ID を保持していることの確認)
    EXPECT_EQ(
        INT64_MAX,
        ids[1]); // [確認_正常系] - ids[1] が INT64_MAX であること。(64 ビット最大値の ID を保持していることの確認)
    EXPECT_EQ(2002,
              ids[2]); // [確認_正常系] - ids[2] が 2002 であること。(3 件目の service ID を保持していることの確認)

    // Cleanup
    free(ids);
}
