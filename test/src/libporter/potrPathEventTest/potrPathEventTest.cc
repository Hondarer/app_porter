#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>

#include <mock_com_util.h>
#include <porter/porter_const.h>
#include <porter/potrContext.h>
#include <porter/potrPathEvent.h>

#include <string.h>

using namespace testing;

struct CapturedEvent
{
    PotrPeerId peer_id;
    PotrEvent event;
    size_t len;
    int path_states[POTR_MAX_PATH];
};

static CapturedEvent g_events[8];
static int g_event_count;

static void capture_callback(int64_t service_id, PotrPeerId peer_id, PotrEvent event, const void *data, size_t len)
{
    CapturedEvent *entry = &g_events[g_event_count++];

    EXPECT_EQ(42, service_id);
    entry->peer_id = peer_id;
    entry->event = event;
    entry->len = len;
    memset(entry->path_states, 0, sizeof(entry->path_states));
    if ((event == POTR_EVENT_PATH_CONNECTED || event == POTR_EVENT_PATH_DISCONNECTED) && data != nullptr)
    {
        memcpy(entry->path_states, data, sizeof(entry->path_states));
    }
}

class potrPathEventTest : public Test
{
  protected:
    void SetUp() override
    {
        memset(&ctx, 0, sizeof(ctx));
        memset(&peer, 0, sizeof(peer));
        memset(g_events, 0, sizeof(g_events));
        g_event_count = 0;

        ctx.service.service_id = 42;
        ctx.callback = capture_callback;
        peer.peer_id = 7;
    }

    PotrContext ctx;
    PotrPeerContext peer;
};

// service 接続時に PATH_CONNECTED が CONNECTED より先に通知されることの確認
TEST_F(potrPathEventTest, service_connect_emits_paths_before_connected)
{
    // Arrange
    PotrPreparedPathEvents prepared;
    int next_states[POTR_MAX_PATH] = {1, 0, 1, 0}; // [状態] - path 0 と 2 が接続へ遷移する次状態を用意する。

    // Pre-Assert

    // Act
    potr_sync_service_path_state_locked(&ctx, next_states,
                                        &prepared); // [手順] - service の path 状態を同期しイベントを準備する。

    // Assert
    EXPECT_EQ(2, prepared.changed_count);    // [確認_正常系] - 変化した path が 2 件であること。
    EXPECT_EQ(0, prepared.changed_paths[0]); // [確認_正常系] - 1 件目の変化が path 0 であること。
    EXPECT_EQ(POTR_EVENT_PATH_CONNECTED,
              prepared.changed_events[0]);   // [確認_正常系] - path 0 のイベントが PATH_CONNECTED であること。
    EXPECT_EQ(2, prepared.changed_paths[1]); // [確認_正常系] - 2 件目の変化が path 2 であること。
    EXPECT_EQ(POTR_EVENT_PATH_CONNECTED,
              prepared.changed_events[1]); // [確認_正常系] - path 2 のイベントが PATH_CONNECTED であること。
    EXPECT_EQ(POTR_EVENT_CONNECTED,
              prepared.session_event);       // [確認_正常系] - セッション イベントが CONNECTED であること。
    EXPECT_EQ(1, ctx.health_alive);          // [確認_正常系] - health_alive が 1 になること。
    EXPECT_EQ(1, ctx.path_logical_alive[0]); // [確認_正常系] - path 0 の論理状態が接続になること。
    EXPECT_EQ(1, ctx.path_logical_alive[2]); // [確認_正常系] - path 2 の論理状態が接続になること。

    potr_emit_service_path_events_locked(&ctx, &prepared); // [手順] - 準備済みイベントを callback へ発行する。

    EXPECT_EQ(3, g_event_count); // [確認_正常系] - callback が 3 回呼ばれること。
    EXPECT_EQ(POTR_PEER_NA,
              g_events[0].peer_id); // [確認_正常系] - service イベントの peer_id が POTR_PEER_NA であること。
    EXPECT_EQ(POTR_EVENT_PATH_CONNECTED,
              g_events[0].event);             // [確認_正常系] - 1 番目に path 0 の PATH_CONNECTED が通知されること。
    EXPECT_EQ(0U, g_events[0].len);           // [確認_正常系] - 1 番目の len が path 番号 0 であること。
    EXPECT_EQ(1, g_events[0].path_states[0]); // [確認_正常系] - 通知時点の path_states で path 0 が接続であること。
    EXPECT_EQ(1, g_events[0].path_states[2]); // [確認_正常系] - 通知時点の path_states で path 2 が接続であること。
    EXPECT_EQ(POTR_EVENT_PATH_CONNECTED,
              g_events[1].event);             // [確認_正常系] - 2 番目に path 2 の PATH_CONNECTED が通知されること。
    EXPECT_EQ(2U, g_events[1].len);           // [確認_正常系] - 2 番目の len が path 番号 2 であること。
    EXPECT_EQ(1, g_events[1].path_states[0]); // [確認_正常系] - 2 番目の path_states でも path 0 が接続であること。
    EXPECT_EQ(1, g_events[1].path_states[2]); // [確認_正常系] - 2 番目の path_states でも path 2 が接続であること。
    EXPECT_EQ(POTR_EVENT_CONNECTED, g_events[2].event); // [確認_正常系] - 最後に CONNECTED が通知されること。
}

// peer 切断時に全 path の PATH_DISCONNECTED が DISCONNECTED より先に通知されることの確認
TEST_F(potrPathEventTest, peer_disconnect_emits_all_paths_before_disconnected)
{
    // Arrange
    PotrPreparedPathEvents prepared;
    int next_states[POTR_MAX_PATH] = {0, 0, 0, 0}; // [状態] - 全 path が切断へ遷移する次状態を用意する。

    peer.health_alive = 1;
    peer.path_logical_alive[1] = 1;
    peer.path_logical_alive[3] = 1; // [状態] - peer 7 を path 1 と 3 が接続済みの状態とする。

    // Pre-Assert

    // Act
    potr_sync_peer_path_state_locked(&peer, next_states,
                                     &prepared); // [手順] - peer の path 状態を同期しイベントを準備する。

    // Assert
    EXPECT_EQ(2, prepared.changed_count);    // [確認_正常系] - 変化した path が 2 件であること。
    EXPECT_EQ(1, prepared.changed_paths[0]); // [確認_正常系] - 1 件目の変化が path 1 であること。
    EXPECT_EQ(POTR_EVENT_PATH_DISCONNECTED,
              prepared.changed_events[0]);   // [確認_正常系] - path 1 のイベントが PATH_DISCONNECTED であること。
    EXPECT_EQ(3, prepared.changed_paths[1]); // [確認_正常系] - 2 件目の変化が path 3 であること。
    EXPECT_EQ(POTR_EVENT_PATH_DISCONNECTED,
              prepared.changed_events[1]); // [確認_正常系] - path 3 のイベントが PATH_DISCONNECTED であること。
    EXPECT_EQ(POTR_EVENT_DISCONNECTED,
              prepared.session_event);        // [確認_正常系] - セッション イベントが DISCONNECTED であること。
    EXPECT_EQ(0, peer.health_alive);          // [確認_正常系] - health_alive が 0 になること。
    EXPECT_EQ(0, peer.path_logical_alive[1]); // [確認_正常系] - path 1 の論理状態が切断になること。
    EXPECT_EQ(0, peer.path_logical_alive[3]); // [確認_正常系] - path 3 の論理状態が切断になること。

    potr_emit_peer_path_events_locked(&ctx, &peer, &prepared); // [手順] - 準備済みイベントを callback へ発行する。

    EXPECT_EQ(3, g_event_count);                   // [確認_正常系] - callback が 3 回呼ばれること。
    EXPECT_EQ((PotrPeerId)7, g_events[0].peer_id); // [確認_正常系] - peer イベントの peer_id が 7 であること。
    EXPECT_EQ(POTR_EVENT_PATH_DISCONNECTED,
              g_events[0].event);   // [確認_正常系] - 1 番目に path 1 の PATH_DISCONNECTED が通知されること。
    EXPECT_EQ(1U, g_events[0].len); // [確認_正常系] - 1 番目の len が path 番号 1 であること。
    EXPECT_EQ(POTR_EVENT_PATH_DISCONNECTED,
              g_events[1].event);   // [確認_正常系] - 2 番目に path 3 の PATH_DISCONNECTED が通知されること。
    EXPECT_EQ(3U, g_events[1].len); // [確認_正常系] - 2 番目の len が path 番号 3 であること。
    EXPECT_EQ(POTR_EVENT_DISCONNECTED, g_events[2].event); // [確認_正常系] - 最後に DISCONNECTED が通知されること。
    EXPECT_EQ((PotrPeerId)7, g_events[2].peer_id);         // [確認_正常系] - DISCONNECTED の peer_id が 7 であること。
}
