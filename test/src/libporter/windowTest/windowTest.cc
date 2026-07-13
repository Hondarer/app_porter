#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>

#include <porter/porter_const.h>
#include <porter/porter_type.h>
#include <porter/protocol/window.h>

#if defined(PLATFORM_LINUX)
    #include <arpa/inet.h>
#elif defined(PLATFORM_WINDOWS)
    #include <com_util/base/windows_sdk.h>
#endif /* PLATFORM_ */

#include <string.h>

using namespace testing;

class windowTest : public Test
{
  protected:
    PotrWindow win;

    void SetUp() override
    {
        memset(&win, 0, sizeof(win));
    }

    void TearDown() override
    {
        window_dispose(&win);
    }

    /* 受信ウィンドウ用パケットを組み立てる (payload_len はホスト バイト オーダー) */
    static PotrPacket make_recv_packet(uint32_t seq_num, const uint8_t *payload, uint16_t payload_len)
    {
        PotrPacket pkt;

        memset(&pkt, 0, sizeof(pkt));
        pkt.seq_num = seq_num;
        pkt.payload = payload;
        pkt.payload_len = payload_len;
        return pkt;
    }

    /* 送信ウィンドウ用パケットを組み立てる (payload_len は NBO: packet_build_packed 相当) */
    static PotrPacket make_send_packet(uint32_t seq_num, const uint8_t *payload, uint16_t payload_len)
    {
        PotrPacket pkt;

        memset(&pkt, 0, sizeof(pkt));
        pkt.seq_num = seq_num;
        pkt.payload = payload;
        pkt.payload_len = htons(payload_len);
        return pkt;
    }
};

// 初期化と同一サイズ再初期化でバッファーが再確保されず状態がリセットされることの確認
TEST_F(windowTest, initInitializesStateAndReusesBuffersOnSameSize)
{
    // Arrange
    PotrPacket *first_packets; // [状態] - 初回確保時のバッファー アドレスを記録する。

    // Pre-Assert

    // Act
    int rtc_null = window_init(NULL, 0U, 8U, 128U);    // [手順] - win に NULL を与えて呼び出す。
    int rtc_first = window_init(&win, 100U, 8U, 128U); // [手順] - initial_seq=100 で初期化する。
    first_packets = win.packets;
    int rtc_second = window_init(&win, 200U, 8U, 128U); // [手順] - 同一サイズ・initial_seq=200 で再初期化する。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc_null);       // [確認_異常系] - win が NULL の場合に POTR_ERROR を返すこと。
    EXPECT_EQ(POTR_SUCCESS, rtc_first);    // [確認_正常系] - 初期化が成功すること。
    EXPECT_EQ(POTR_SUCCESS, rtc_second);   // [確認_正常系] - 同一サイズの再初期化が成功すること。
    EXPECT_EQ(200U, win.base_seq);         // [確認_正常系] - base_seq が再初期化時の initial_seq になること。
    EXPECT_EQ(200U, win.next_seq);         // [確認_正常系] - next_seq が再初期化時の initial_seq になること。
    EXPECT_EQ(first_packets, win.packets); // [確認_正常系] - 同一サイズの再初期化でバッファーが再確保されないこと。
    EXPECT_EQ(8U, win.window_size);        // [確認_正常系] - window_size が保持されること。
    EXPECT_EQ(128U, win.max_payload);      // [確認_正常系] - max_payload が保持されること。
}

// - 送信ウィンドウが満杯のとき push で最古エントリが evict されること。
// - evict された通番の send_get が POTR_ERROR を返すこと。
TEST_F(windowTest, sendPushEvictsOldestEntryWhenFull)
{
    // Arrange
    uint8_t payload[4] = {0xAA, 0xBB, 0xCC, 0xDD}; // [状態] - 4 バイトのペイロードを持つパケットを積む。
    PotrPacket out;
    uint32_t seq;

    ASSERT_EQ(POTR_SUCCESS, window_init(&win, 0U, 4U, 16U)); // [状態] - window_size=4 で初期化する。

    // Pre-Assert

    // Act
    for (seq = 0U; seq < 4U; seq++)
    {
        PotrPacket pkt = make_send_packet(seq, payload, sizeof(payload));
        ASSERT_EQ(POTR_SUCCESS, window_send_push(&win, &pkt));
    }
    int full_after_4 = window_send_full(&win); // [手順] - 4 件 push した直後の満杯判定を取得する。
    {
        PotrPacket pkt = make_send_packet(4U, payload, sizeof(payload));
        int rtc_push = window_send_push(&win, &pkt); // [手順] - 満杯状態で 5 件目を push する。
        ASSERT_EQ(POTR_SUCCESS, rtc_push);           // [確認_正常系] - 5 件目の push が成功すること。
    }
    int rtc_evicted = window_send_get(&win, 0U, &out); // [手順] - evict された通番 0 を取得する。
    int rtc_latest = window_send_get(&win, 4U, &out);  // [手順] - 最新の通番 4 を取得する。

    // Assert
    EXPECT_EQ(1, full_after_4);          // [確認_正常系] - window_size 件 push で満杯になること。
    EXPECT_EQ(POTR_ERROR, rtc_evicted);  // [確認_異常系] - evict された通番の取得が POTR_ERROR になること。
    EXPECT_EQ(POTR_SUCCESS, rtc_latest); // [確認_正常系] - 最新エントリの取得が成功すること。
    EXPECT_EQ(1U, win.base_seq);         // [確認_正常系] - evict により base_seq が前進すること。
}

// - send_get がプール スロットへディープ コピーされたペイロードを返すこと。
// - ウィンドウ範囲外の通番に POTR_ERROR を返すこと。
TEST_F(windowTest, sendGetReturnsDeepCopiedPayload)
{
    // Arrange
    uint8_t payload[3] = {0x01, 0x02, 0x03}; // [状態] - ディープ コピー検証用のペイロード 3 バイトを用意する。
    PotrPacket out;

    ASSERT_EQ(POTR_SUCCESS, window_init(&win, 0U, 4U, 16U)); // [状態] - window_size=4 で初期化する。

    // Pre-Assert

    // Act
    {
        PotrPacket pkt = make_send_packet(0U, payload, sizeof(payload));
        ASSERT_EQ(POTR_SUCCESS, window_send_push(&win, &pkt));
    }
    payload[0] = 0xFF; // [手順] - push 後に元バッファーを書き換え、ディープ コピーであることを確認する。
    int rtc_get = window_send_get(&win, 0U, &out);
    int rtc_out_of_range = window_send_get(&win, 99U, &out); // [手順] - 範囲外の通番で取得する。

    // Assert
    EXPECT_EQ(POTR_SUCCESS, rtc_get); // [確認_正常系] - 保持中の通番の取得が成功すること。
    EXPECT_EQ(0x01, out.payload[0]);  // [確認_正常系] - push 時点のペイロードがディープ コピーで保持されること。
    EXPECT_EQ(POTR_ERROR, rtc_out_of_range); // [確認_異常系] - 範囲外の通番に POTR_ERROR を返すこと。
}

// - 順序どおりに push したパケットが pop で順に取り出せること。
// - 空ウィンドウの pop が POTR_ERROR を返すこと。
TEST_F(windowTest, recvPushAndPopDeliversInOrder)
{
    // Arrange
    uint8_t payload[2] = {0x10, 0x20}; // [状態] - 受信パケットのペイロード 2 バイトを用意する。
    PotrPacket out;

    ASSERT_EQ(POTR_SUCCESS, window_init(&win, 0U, 4U, 16U)); // [状態] - base_seq=0 で初期化する。

    // Pre-Assert

    // Act
    {
        PotrPacket pkt0 = make_recv_packet(0U, payload, sizeof(payload));
        PotrPacket pkt1 = make_recv_packet(1U, payload, sizeof(payload));
        int rtc_push0 = window_recv_push(&win, &pkt0); // [手順] - seq=0, 1 を順に push する。
        ASSERT_EQ(POTR_SUCCESS, rtc_push0);            // [確認_正常系] - seq=0 の push が成功すること。
        int rtc_push1 = window_recv_push(&win, &pkt1);
        ASSERT_EQ(POTR_SUCCESS, rtc_push1); // [確認_正常系] - seq=1 の push が成功すること。
    }
    int rtc_pop0 = window_recv_pop(&win, &out);
    uint32_t seq0 = out.seq_num;
    int rtc_pop1 = window_recv_pop(&win, &out);
    uint32_t seq1 = out.seq_num;
    int rtc_pop_empty = window_recv_pop(&win, &out); // [手順] - 空になったウィンドウから pop する。

    // Assert
    EXPECT_EQ(POTR_SUCCESS, rtc_pop0);    // [確認_正常系] - 1 件目の pop が成功すること。
    EXPECT_EQ(0U, seq0);                  // [確認_正常系] - 1 件目が通番 0 で取り出されること。
    EXPECT_EQ(POTR_SUCCESS, rtc_pop1);    // [確認_正常系] - 2 件目の pop が成功すること。
    EXPECT_EQ(1U, seq1);                  // [確認_正常系] - 2 件目が通番 1 で取り出され、通番順が保たれること。
    EXPECT_EQ(POTR_ERROR, rtc_pop_empty); // [確認_異常系] - 未着の通番の pop が POTR_ERROR になること。
}

// - 先行パケットのみ到着した状態で欠番 (next_seq) が NACK 対象になること。
// - 欠番が埋まったあと needs_nack が 0 を返し、順に pop できること。
TEST_F(windowTest, recvOutOfOrderDetectsGapAndRecovers)
{
    // Arrange
    uint8_t payload[1] = {0x77}; // [状態] - 受信パケットのペイロード 1 バイトを用意する。
    PotrPacket out;
    uint32_t nack_num = 999U;

    ASSERT_EQ(POTR_SUCCESS, window_init(&win, 0U, 8U, 16U)); // [状態] - base_seq=0 で初期化する。

    // Pre-Assert

    // Act
    {
        PotrPacket pkt2 = make_recv_packet(2U, payload, sizeof(payload));
        int rtc_push2 = window_recv_push(&win, &pkt2); // [手順] - seq=2 を先行して push する。
        ASSERT_EQ(POTR_SUCCESS, rtc_push2);            // [確認_正常系] - seq=2 の push が成功すること。
    }
    int rtc_gap = window_recv_needs_nack(&win, &nack_num); // [手順] - 欠番判定を行う。
    int rtc_pop_blocked = window_recv_pop(&win, &out);     // [手順] - 欠番未解消のまま pop する。
    {
        PotrPacket pkt0 = make_recv_packet(0U, payload, sizeof(payload));
        PotrPacket pkt1 = make_recv_packet(1U, payload, sizeof(payload));
        int rtc_push0 = window_recv_push(&win, &pkt0); // [手順] - 欠番 seq=0, 1 を埋める。
        ASSERT_EQ(POTR_SUCCESS, rtc_push0);            // [確認_正常系] - seq=0 の push が成功すること。
        int rtc_push1 = window_recv_push(&win, &pkt1);
        ASSERT_EQ(POTR_SUCCESS, rtc_push1); // [確認_正常系] - seq=1 の push が成功すること。
    }
    int rtc_no_gap = window_recv_needs_nack(&win, &nack_num);
    int pop_count = 0;
    while (window_recv_pop(&win, &out) == POTR_SUCCESS)
    {
        pop_count++;
    }

    // Assert
    EXPECT_EQ(1, rtc_gap);                  // [確認_正常系] - 先行パケット到着時に欠番が検出されること。
    EXPECT_EQ(0U, nack_num);                // [確認_正常系] - NACK 対象が next_seq (0) であること。
    EXPECT_EQ(POTR_ERROR, rtc_pop_blocked); // [確認_異常系] - 欠番未解消のまま pop できないこと。
    EXPECT_EQ(0, rtc_no_gap);               // [確認_正常系] - 欠番解消後は NACK 不要になること。
    EXPECT_EQ(3, pop_count);                // [確認_正常系] - 3 件すべて順に取り出せること。
}

// - ウィンドウ範囲外の通番の push が POTR_ERROR を返すこと。
// - 同一通番の重複 push が成功扱い (冪等) になること。
TEST_F(windowTest, recvPushRejectsOutOfWindowAndAcceptsDuplicate)
{
    // Arrange
    uint8_t payload[1] = {0x55}; // [状態] - 受信パケットのペイロード 1 バイトを用意する。

    ASSERT_EQ(POTR_SUCCESS, window_init(&win, 0U, 4U, 16U)); // [状態] - window_size=4 で初期化する。

    // Pre-Assert

    // Act
    PotrPacket pkt_out = make_recv_packet(4U, payload, sizeof(payload));
    int rtc_out = window_recv_push(&win, &pkt_out); // [手順] - base_seq + window_size の通番を push する。
    PotrPacket pkt0 = make_recv_packet(0U, payload, sizeof(payload));
    int rtc_first = window_recv_push(&win, &pkt0);
    int rtc_dup = window_recv_push(&win, &pkt0); // [手順] - 同一通番を重複 push する。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc_out);     // [確認_異常系] - ウィンドウ範囲外の push が POTR_ERROR になること。
    EXPECT_EQ(POTR_SUCCESS, rtc_first); // [確認_正常系] - 範囲内の push が成功すること。
    EXPECT_EQ(POTR_SUCCESS, rtc_dup);   // [確認_正常系] - 重複 push が成功扱いになること。
}

// - skip が next_seq と一致する通番のときのみウィンドウを前進させること。
TEST_F(windowTest, recvSkipAdvancesOnlyOnNextSeq)
{
    // Arrange
    ASSERT_EQ(POTR_SUCCESS, window_init(&win, 10U, 4U, 16U)); // [状態] - base_seq=10 で初期化する。

    // Pre-Assert

    // Act
    window_recv_skip(&win, 99U); // [手順] - next_seq と一致しない通番で skip する。
    uint32_t next_after_mismatch = win.next_seq;
    window_recv_skip(&win, 10U); // [手順] - next_seq と一致する通番で skip する。

    // Assert
    EXPECT_EQ(10U, next_after_mismatch); // [確認_正常系] - 不一致の skip では前進しないこと。
    EXPECT_EQ(11U, win.next_seq);        // [確認_正常系] - 一致した skip で next_seq が前進すること。
    EXPECT_EQ(11U, win.base_seq);        // [確認_正常系] - 一致した skip で base_seq が前進すること。
}

// - reset が全スロットを無効化し base_seq / next_seq を新基点に設定すること。
TEST_F(windowTest, recvResetClearsSlotsAndSetsNewBase)
{
    // Arrange
    uint8_t payload[1] = {0x99}; // [状態] - 受信パケットのペイロード 1 バイトを用意する。
    PotrPacket out;

    ASSERT_EQ(POTR_SUCCESS, window_init(&win, 0U, 4U, 16U)); // [状態] - base_seq=0 で初期化する。
    {
        PotrPacket pkt = make_recv_packet(0U, payload, sizeof(payload));
        ASSERT_EQ(POTR_SUCCESS, window_recv_push(&win, &pkt)); // [状態] - スロットにパケットを格納しておく。
    }

    // Pre-Assert

    // Act
    window_recv_reset(&win, 500U); // [手順] - 新基点 500 でリセットする。
    int rtc_pop = window_recv_pop(&win, &out);

    // Assert
    EXPECT_EQ(500U, win.base_seq);  // [確認_正常系] - base_seq が新基点になること。
    EXPECT_EQ(500U, win.next_seq);  // [確認_正常系] - next_seq が新基点になること。
    EXPECT_EQ(POTR_ERROR, rtc_pop); // [確認_正常系] - リセットにより格納済みスロットが無効化されること。
}
