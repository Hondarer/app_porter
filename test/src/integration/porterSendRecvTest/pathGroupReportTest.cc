/**
 *******************************************************************************
 *  @file           pathGroupReportTest.cc
 *  @brief          実ソケットによる受信報告と優先グループの切り替えを検証します。
 *
 *******************************************************************************
 */
#include <testfw.h>
#include <porter.h>
#include <cplat/crt/string.h>
#include <cplat/net/socket.h>
#include <cplat/net/byteorder.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <vector>
#include <thread>

namespace
{
std::mutex report_test_mutex;
std::condition_variable report_test_condition;
std::vector<uint8_t> report_test_messages;

void report_test_receive(int64_t, potr_peer_id, potr_event event, const void *data, size_t len)
{
    if (event == POTR_EVENT_DATA && len == 1U)
    {
        std::lock_guard<std::mutex> lock(report_test_mutex);
        report_test_messages.push_back(*static_cast<const uint8_t *>(data));
        report_test_condition.notify_all();
    }
}

class pathGroupReportTest : public testing::Test
{
  protected:
    potr_context *sender = nullptr;
    potr_context *receiver = nullptr;
    cplat_socket observer = CPLAT_INVALID_SOCKET;

    // [サブ手順 名前=pathGroupReportTest.TearDown]
    void TearDown() override
    {
        if (sender != nullptr)
        {
            EXPECT_EQ(POTR_OK, potr_service_close(sender));
            // [状態確認] - 送信者を終了できること。
        }
        if (receiver != nullptr)
        {
            EXPECT_EQ(POTR_OK, potr_service_close(receiver));
            // [状態確認] - 受信者を終了できること。
        }
        if (observer != CPLAT_INVALID_SOCKET)
        {
            cplat_socket_close(observer);
        }
    }
    // [サブ手順終了]
};
}

// 受信者の報告により DATA が到達できる下位グループへ切り替わることの確認
TEST_F(pathGroupReportTest, receiver_reports_select_reachable_group)
{
    // Arrange
    potr_global_config global = {};
    potr_service_def source = {};
    potr_service_def destination = {};
    cplat_ipv4_endpoint observer_address = {};
    global.window_size = 16;
    global.max_payload = 512;
    global.max_message_size = 8192;
    global.send_queue_depth = 16;
    global.udp_health_interval_ms = 20;
    global.udp_health_timeout_ms = 1000;
    source.service_id = 7134;
    source.type = POTR_TYPE_UNICAST;
    source.src_port = 19734;
    source.dst_port = 19735;
    source.max_peers = 4;
    source.path_group[0] = 1;
    source.path_group[1] = 3;
    cplat_strcpy(source.src_addr[0], sizeof(source.src_addr[0]), "127.0.0.1");
    cplat_strcpy(source.src_addr[1], sizeof(source.src_addr[1]), "127.0.0.2");
    cplat_strcpy(source.dst_addr[0], sizeof(source.dst_addr[0]), "127.0.0.1");
    cplat_strcpy(source.dst_addr[1], sizeof(source.dst_addr[1]), "127.0.0.2");
    destination = source;
    cplat_strcpy(destination.dst_addr[0], sizeof(destination.dst_addr[0]), "127.0.0.3");
    {
        std::lock_guard<std::mutex> lock(report_test_mutex);
        report_test_messages.clear();
    }
    // [状態] - 最優先経路は観測ソケットだけに届き、porter 受信者は下位経路だけで受信する。
    ASSERT_EQ(CPLAT_OK, cplat_ipv4_parse("127.0.0.1", &observer_address.address));
    // [状態確認] - 観測アドレスを解決できること。
    observer_address.port = cplat_hton16(source.dst_port);
    ASSERT_EQ(CPLAT_OK, cplat_socket_open(CPLAT_SOCKET_UDP, &observer, nullptr));
    // [状態確認] - 最優先経路の観測ソケットを作成できること。
    ASSERT_EQ(CPLAT_OK, cplat_socket_bind(observer, &observer_address, nullptr));
    // [状態確認] - 最優先経路の宛先を観測できること。
    ASSERT_EQ(POTR_OK, potr_service_open(&global, &destination, POTR_ROLE_RECEIVER, report_test_receive, &receiver));
    // [状態確認] - 2 経路の受信者を起動できること。
    ASSERT_EQ(POTR_OK, potr_service_open(&global, &source, POTR_ROLE_SENDER, nullptr, &sender));
    // [状態確認] - 2 グループの送信者を起動できること。
    bool delivered = false;
    bool primary_received_data = false;
    int actual_ret = POTR_OK;
    // Pre-Assert
    // Act
    for (int attempt = 0; attempt < 100 && !delivered; attempt++)
    {
        const uint8_t marker = 1;
        actual_ret = potr_service_send(sender, POTR_PEER_NA, &marker, 1, POTR_SEND_BLOCKING);
        std::unique_lock<std::mutex> lock(report_test_mutex);
        delivered = report_test_condition.wait_for(lock, std::chrono::milliseconds(20), [] {
            return !report_test_messages.empty();
        });
    }
    // [手順] - 待機経路の PING と受信報告の往復後、下位経路で DATA が配信されるまで送る。
    /* DATA または PING の受信状態を載せた次の報告が送信者へ戻るまで待ちます。 */
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const uint8_t confirmed_marker = 2;
    actual_ret = potr_service_send(sender, POTR_PEER_NA, &confirmed_marker, 1, POTR_SEND_BLOCKING);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (std::chrono::steady_clock::now() < deadline)
    {
        int ready = 0;
        if (cplat_socket_wait_readable(observer, 10, &ready, nullptr) == CPLAT_OK && ready)
        {
            uint8_t wire[2048] = {};
            size_t received = 0;
            cplat_ipv4_endpoint from = {};
            if (cplat_socket_recvfrom(observer, wire, sizeof(wire), &from, &received, nullptr) == CPLAT_OK &&
                received >= POTR_PACKET_HEADER_WIRE_SIZE + POTR_PAYLOAD_ELEM_HDR_SIZE + 1U)
            {
                uint16_t flags;
                std::memcpy(&flags, wire + 32, sizeof(flags));
                if ((cplat_ntoh16(flags) & POTR_FLAG_DATA) != 0U &&
                    wire[POTR_PACKET_HEADER_WIRE_SIZE + POTR_PAYLOAD_ELEM_HDR_SIZE] == confirmed_marker)
                {
                    primary_received_data = true;
                }
            }
        }
    }
    // [手順] - 切り替え後の別 DATA が最優先経路へ複製されていないか観測する。
    bool confirmation_delivered;
    {
        std::unique_lock<std::mutex> lock(report_test_mutex);
        confirmation_delivered = report_test_condition.wait_for(lock, std::chrono::seconds(1), [] {
            for (uint8_t value : report_test_messages)
            {
                if (value == 2U)
                {
                    return true;
                }
            }
            return false;
        });
    }
    // Assert
    EXPECT_EQ(POTR_OK, actual_ret); // [確認_正常系] - 片方向の送信 API が成功すること。
    EXPECT_TRUE(delivered); // [確認_正常系] - REPORT で到達可能とされた下位経路で DATA を配信すること。
    EXPECT_TRUE(confirmation_delivered); // [確認_正常系] - 切り替え後の DATA を受信すること。
    EXPECT_FALSE(primary_received_data); // [確認_正常系] - 切り替え後は最優先グループへ DATA を送らないこと。
}
// [サブ手順参照 名前=pathGroupReportTest.TearDown]
