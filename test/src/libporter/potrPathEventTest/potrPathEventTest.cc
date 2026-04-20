#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>

#include <porter_const.h>
#include <potrContext.h>
#include <potrPathEvent.h>

#include <string.h>

using namespace testing;

struct CapturedEvent
{
    PotrPeerId peer_id;
    PotrEvent  event;
    size_t     len;
    int        path_states[POTR_MAX_PATH];
};

static CapturedEvent g_events[8];
static int           g_event_count;

static void capture_callback(int64_t service_id, PotrPeerId peer_id, PotrEvent event,
                             const void *data, size_t len)
{
    CapturedEvent *entry = &g_events[g_event_count++];

    EXPECT_EQ(42, service_id);
    entry->peer_id = peer_id;
    entry->event   = event;
    entry->len     = len;
    memset(entry->path_states, 0, sizeof(entry->path_states));
    if ((event == POTR_EVENT_PATH_CONNECTED || event == POTR_EVENT_PATH_DISCONNECTED)
        && data != nullptr)
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
        ctx.callback           = capture_callback;
        peer.peer_id           = 7;
    }

    struct PotrContext_ ctx;
    PotrPeerContext     peer;
};

TEST_F(potrPathEventTest, service_connect_emits_paths_before_connected)
{
    PotrPreparedPathEvents prepared;
    int next_states[POTR_MAX_PATH] = {1, 0, 1, 0};

    potr_sync_service_path_state_locked(&ctx, next_states, &prepared);

    EXPECT_EQ(2, prepared.changed_count);
    EXPECT_EQ(0, prepared.changed_paths[0]);
    EXPECT_EQ(POTR_EVENT_PATH_CONNECTED, prepared.changed_events[0]);
    EXPECT_EQ(2, prepared.changed_paths[1]);
    EXPECT_EQ(POTR_EVENT_PATH_CONNECTED, prepared.changed_events[1]);
    EXPECT_EQ(POTR_EVENT_CONNECTED, prepared.session_event);
    EXPECT_EQ(1, ctx.health_alive);
    EXPECT_EQ(1, ctx.path_logical_alive[0]);
    EXPECT_EQ(1, ctx.path_logical_alive[2]);

    potr_emit_service_path_events_locked(&ctx, &prepared);

    EXPECT_EQ(3, g_event_count);
    EXPECT_EQ(POTR_PEER_NA, g_events[0].peer_id);
    EXPECT_EQ(POTR_EVENT_PATH_CONNECTED, g_events[0].event);
    EXPECT_EQ(0U, g_events[0].len);
    EXPECT_EQ(1, g_events[0].path_states[0]);
    EXPECT_EQ(1, g_events[0].path_states[2]);
    EXPECT_EQ(POTR_EVENT_PATH_CONNECTED, g_events[1].event);
    EXPECT_EQ(2U, g_events[1].len);
    EXPECT_EQ(1, g_events[1].path_states[0]);
    EXPECT_EQ(1, g_events[1].path_states[2]);
    EXPECT_EQ(POTR_EVENT_CONNECTED, g_events[2].event);
}

TEST_F(potrPathEventTest, peer_disconnect_emits_all_paths_before_disconnected)
{
    PotrPreparedPathEvents prepared;
    int next_states[POTR_MAX_PATH] = {0, 0, 0, 0};

    peer.health_alive         = 1;
    peer.path_logical_alive[1] = 1;
    peer.path_logical_alive[3] = 1;

    potr_sync_peer_path_state_locked(&peer, next_states, &prepared);

    EXPECT_EQ(2, prepared.changed_count);
    EXPECT_EQ(1, prepared.changed_paths[0]);
    EXPECT_EQ(POTR_EVENT_PATH_DISCONNECTED, prepared.changed_events[0]);
    EXPECT_EQ(3, prepared.changed_paths[1]);
    EXPECT_EQ(POTR_EVENT_PATH_DISCONNECTED, prepared.changed_events[1]);
    EXPECT_EQ(POTR_EVENT_DISCONNECTED, prepared.session_event);
    EXPECT_EQ(0, peer.health_alive);
    EXPECT_EQ(0, peer.path_logical_alive[1]);
    EXPECT_EQ(0, peer.path_logical_alive[3]);

    potr_emit_peer_path_events_locked(&ctx, &peer, &prepared);

    EXPECT_EQ(3, g_event_count);
    EXPECT_EQ((PotrPeerId)7, g_events[0].peer_id);
    EXPECT_EQ(POTR_EVENT_PATH_DISCONNECTED, g_events[0].event);
    EXPECT_EQ(1U, g_events[0].len);
    EXPECT_EQ(POTR_EVENT_PATH_DISCONNECTED, g_events[1].event);
    EXPECT_EQ(3U, g_events[1].len);
    EXPECT_EQ(POTR_EVENT_DISCONNECTED, g_events[2].event);
    EXPECT_EQ((PotrPeerId)7, g_events[2].peer_id);
}
