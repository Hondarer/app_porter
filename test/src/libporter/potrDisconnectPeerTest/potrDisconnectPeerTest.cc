#include <com_util/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>
#include <mock_com_util.h>
#include <mock_potrPeerTable.h>

#include <porter_const.h>
#include <porter.h>
#include <potrContext.h>

#if defined(PLATFORM_LINUX)
    #include <pthread.h>
#endif /* PLATFORM_LINUX */
#include <string.h>

using namespace testing;

/* コールバックキャプチャ */
struct CallbackCapture
{
    struct Entry
    {
        int64_t    service_id;
        PotrPeerId peer_id;
        PotrEvent  event;
        size_t     len;
        int        path_states[POTR_MAX_PATH];
    } entries[8];
    size_t count;
};

static CallbackCapture g_cb;

static void mock_callback(int64_t service_id, PotrPeerId peer_id, PotrEvent event,
                          const void *data, size_t len)
{
    CallbackCapture::Entry *entry = &g_cb.entries[g_cb.count++];

    entry->service_id = service_id;
    entry->peer_id    = peer_id;
    entry->event      = event;
    entry->len        = len;
    memset(entry->path_states, 0, sizeof(entry->path_states));
    if ((event == POTR_EVENT_PATH_CONNECTED || event == POTR_EVENT_PATH_DISCONNECTED)
        && data != nullptr)
    {
        memcpy(entry->path_states, data, sizeof(entry->path_states));
    }
}

class potrDisconnectPeerTest : public Test
{
protected:
    void SetUp() override
    {
        memset(&ctx, 0, sizeof(ctx));
#if defined(PLATFORM_LINUX)
        pthread_mutex_init(&ctx.peers_mutex, nullptr);
        pthread_mutex_init(&ctx.callback_mutex, nullptr);
#elif defined(PLATFORM_WINDOWS)
        InitializeCriticalSection(&ctx.peers_mutex);
        InitializeCriticalSection(&ctx.callback_mutex);
#endif /* PLATFORM_ */
        ctx.service.service_id = 42;

        memset(&peer_ctx, 0, sizeof(peer_ctx));
        peer_ctx.peer_id = 1;

        memset(&g_cb, 0, sizeof(g_cb));

        resetTraceLevel();
        setTraceLevel("com_util_logger_writef", TRACE_INFO);
    }

    void TearDown() override
    {
#if defined(PLATFORM_LINUX)
        pthread_mutex_destroy(&ctx.peers_mutex);
        pthread_mutex_destroy(&ctx.callback_mutex);
#elif defined(PLATFORM_WINDOWS)
        DeleteCriticalSection(&ctx.peers_mutex);
        DeleteCriticalSection(&ctx.callback_mutex);
#endif /* PLATFORM_ */
    }

    struct PotrContext_ ctx;
    PotrPeerContext     peer_ctx; /* peer_find_by_id が返す実体 */
};

/* ---------- 異常系 ---------- */

// ハンドルが NULL の場合に POTR_ERROR を返すことの確認
TEST_F(potrDisconnectPeerTest, handle_null)
{
    // Arrange
    NiceMock<Mock_com_util>        mock_log;
    NiceMock<Mock_potrPeerTable>  mock_peer_table;

    // Pre-Assert
    EXPECT_CALL(mock_log, com_util_logger_writef(_, COM_UTIL_LOG_LEVEL_ERROR,
                                       AllOf(HasSubstr("potrDisconnectPeer.c"),
                                             HasSubstr("handle is NULL"))))
        .Times(1); // [Pre-Assert確認_異常系] - ERROR ログに "handle is NULL" が含まれること。

    // Act
    int rtc = potrDisconnectPeer(NULL, 1); // [手順] - handle=NULL で potrDisconnectPeer を呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc); // [確認_異常系] - 戻り値が POTR_ERROR であること。
}

// peer_id に POTR_PEER_NA を指定した場合に POTR_ERROR を返すことの確認
TEST_F(potrDisconnectPeerTest, peer_id_na)
{
    // Arrange
    NiceMock<Mock_com_util>        mock_log;
    NiceMock<Mock_potrPeerTable>  mock_peer_table;

    // Pre-Assert
    EXPECT_CALL(mock_log, com_util_logger_writef(_, COM_UTIL_LOG_LEVEL_ERROR,
                                       AllOf(HasSubstr("potrDisconnectPeer.c"),
                                             HasSubstr("invalid peer_id"))))
        .Times(1); // [Pre-Assert確認_異常系] - ERROR ログに "invalid peer_id" が含まれること。

    // Act
    int rtc = potrDisconnectPeer(&ctx, POTR_PEER_NA); // [手順] - peer_id=POTR_PEER_NA で potrDisconnectPeer を呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc); // [確認_異常系] - 戻り値が POTR_ERROR であること。
}

// peer_id に POTR_PEER_ALL を指定した場合に POTR_ERROR を返すことの確認
TEST_F(potrDisconnectPeerTest, peer_id_all)
{
    // Arrange
    NiceMock<Mock_com_util>        mock_log;
    NiceMock<Mock_potrPeerTable>  mock_peer_table;

    // Pre-Assert
    EXPECT_CALL(mock_log, com_util_logger_writef(_, COM_UTIL_LOG_LEVEL_ERROR,
                                       AllOf(HasSubstr("potrDisconnectPeer.c"),
                                             HasSubstr("invalid peer_id"))))
        .Times(1); // [Pre-Assert確認_異常系] - ERROR ログに "invalid peer_id" が含まれること。

    // Act
    int rtc = potrDisconnectPeer(&ctx, POTR_PEER_ALL); // [手順] - peer_id=POTR_PEER_ALL で potrDisconnectPeer を呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc); // [確認_異常系] - 戻り値が POTR_ERROR であること。
}

// N:1 モードでない場合に POTR_ERROR を返すことの確認
TEST_F(potrDisconnectPeerTest, not_multi_peer)
{
    // Arrange
    NiceMock<Mock_com_util>        mock_log;
    NiceMock<Mock_potrPeerTable>  mock_peer_table;
    ctx.is_multi_peer = 0; // [状態] - N:1 モードでない (is_multi_peer=0) に設定する。

    // Pre-Assert
    EXPECT_CALL(mock_log, com_util_logger_writef(_, COM_UTIL_LOG_LEVEL_ERROR,
                                       AllOf(HasSubstr("potrDisconnectPeer.c"),
                                             HasSubstr("not in N:1 mode"))))
        .Times(1); // [Pre-Assert確認_異常系] - ERROR ログに "not in N:1 mode" が含まれること。

    // Act
    int rtc = potrDisconnectPeer(&ctx, 1); // [手順] - is_multi_peer=0 の状態で potrDisconnectPeer を呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc); // [確認_異常系] - 戻り値が POTR_ERROR であること。
}

// 指定した peer_id のピアが存在しない場合に POTR_ERROR を返すことの確認
TEST_F(potrDisconnectPeerTest, peer_not_found)
{
    // Arrange
    NiceMock<Mock_com_util>        mock_log;
    NiceMock<Mock_potrPeerTable>  mock_peer_table;
    ctx.is_multi_peer = 1; // [状態] - N:1 モードに設定する。

    EXPECT_CALL(mock_peer_table, peer_find_by_id(&ctx, (PotrPeerId)99))
        .WillOnce(Return(nullptr)); // [Pre-Assert確認_異常系] - peer_find_by_id が nullptr を返すこと。

    EXPECT_CALL(mock_log, com_util_logger_writef(_, COM_UTIL_LOG_LEVEL_ERROR,
                                       AllOf(HasSubstr("potrDisconnectPeer.c"),
                                             HasSubstr("not found"))))
        .Times(1); // [Pre-Assert確認_異常系] - ERROR ログに "not found" が含まれること。

    // Act
    int rtc = potrDisconnectPeer(&ctx, 99); // [手順] - 存在しない peer_id=99 で potrDisconnectPeer を呼び出す。

    // Assert
    EXPECT_EQ(POTR_ERROR, rtc); // [確認_異常系] - 戻り値が POTR_ERROR であること。
}

/* ---------- 正常系 ---------- */

// ピアが存在し health_alive=1 の場合に切断処理とコールバック発火が行われることの確認
TEST_F(potrDisconnectPeerTest, normal_with_callback)
{
    // Arrange
    NiceMock<Mock_com_util>        mock_log;
    NiceMock<Mock_potrPeerTable>  mock_peer_table;
    ctx.is_multi_peer         = 1;               // [状態] - N:1 モードに設定する。
    ctx.callback              = mock_callback;   // [状態] - 受信コールバックを設定する。
    peer_ctx.health_alive     = 1;               // [状態] - ピアを疎通済み状態 (health_alive=1) に設定する。
    peer_ctx.path_logical_alive[0] = 1;
    peer_ctx.path_logical_alive[2] = 1;

    EXPECT_CALL(mock_peer_table, peer_find_by_id(&ctx, (PotrPeerId)1))
        .WillOnce(Return(&peer_ctx)); // [Pre-Assert確認_正常系] - peer_find_by_id がピアコンテキストを返すこと。

    EXPECT_CALL(mock_log, com_util_logger_writef(_, COM_UTIL_LOG_LEVEL_INFO,
                                       AllOf(HasSubstr("potrDisconnectPeer.c"),
                                             HasSubstr("disconnecting"))))
        .Times(1); // [Pre-Assert確認_正常系] - INFO ログに "disconnecting" が含まれること。

    EXPECT_CALL(mock_peer_table, peer_send_fin(&ctx, &peer_ctx))
        .Times(1); // [Pre-Assert確認_正常系] - peer_send_fin が 1 回呼ばれること。

    EXPECT_CALL(mock_peer_table, peer_free(&ctx, &peer_ctx))
        .Times(1); // [Pre-Assert確認_正常系] - peer_free が 1 回呼ばれること。

    // Act
    int rtc = potrDisconnectPeer(&ctx, 1); // [手順] - 正常な状態で potrDisconnectPeer を呼び出す。

    // Assert
    EXPECT_EQ(POTR_SUCCESS, rtc);                        // [確認_正常系] - 戻り値が POTR_SUCCESS であること。
    EXPECT_EQ(static_cast<size_t>(3), g_cb.count);      // [確認_正常系] - PATH 2 件 + DISCONNECTED が呼ばれること。
    EXPECT_EQ(42, g_cb.entries[0].service_id);
    EXPECT_EQ((PotrPeerId)1, g_cb.entries[0].peer_id);
    EXPECT_EQ(POTR_EVENT_PATH_DISCONNECTED, g_cb.entries[0].event);
    EXPECT_EQ(0U, g_cb.entries[0].len);
    EXPECT_EQ(0, g_cb.entries[0].path_states[0]);
    EXPECT_EQ(0, g_cb.entries[0].path_states[2]);
    EXPECT_EQ(42, g_cb.entries[1].service_id);
    EXPECT_EQ((PotrPeerId)1, g_cb.entries[1].peer_id);
    EXPECT_EQ(POTR_EVENT_PATH_DISCONNECTED, g_cb.entries[1].event);
    EXPECT_EQ(2U, g_cb.entries[1].len);
    EXPECT_EQ(0, g_cb.entries[1].path_states[0]);
    EXPECT_EQ(0, g_cb.entries[1].path_states[2]);
    EXPECT_EQ(42, g_cb.entries[2].service_id);
    EXPECT_EQ((PotrPeerId)1, g_cb.entries[2].peer_id);
    EXPECT_EQ(POTR_EVENT_DISCONNECTED, g_cb.entries[2].event);
    EXPECT_EQ(0U, g_cb.entries[2].len);
    EXPECT_EQ(0, peer_ctx.health_alive);                 // [確認_正常系] - health_alive が 0 にクリアされること。
    EXPECT_EQ(0, peer_ctx.path_logical_alive[0]);
    EXPECT_EQ(0, peer_ctx.path_logical_alive[2]);
}

/* ---------- 正常系（切断済みピア） ---------- */

// ピアが存在し health_alive=0 の場合に切断処理のみ行われコールバックが発火しないことの確認
TEST_F(potrDisconnectPeerTest, normal_health_dead)
{
    // Arrange
    NiceMock<Mock_com_util>        mock_log;
    NiceMock<Mock_potrPeerTable>  mock_peer_table;
    ctx.is_multi_peer   = 1;               // [状態] - N:1 モードに設定する。
    ctx.callback        = mock_callback;   // [状態] - 受信コールバックを設定する。
    peer_ctx.health_alive = 0;             // [状態] - ピアを切断済み状態 (health_alive=0) に設定する。

    EXPECT_CALL(mock_peer_table, peer_find_by_id(&ctx, (PotrPeerId)1))
        .WillOnce(Return(&peer_ctx)); // [Pre-Assert確認_正常系] - peer_find_by_id がピアコンテキストを返すこと。

    EXPECT_CALL(mock_peer_table, peer_send_fin(&ctx, &peer_ctx))
        .Times(1); // [Pre-Assert確認_正常系] - peer_send_fin が 1 回呼ばれること。

    EXPECT_CALL(mock_peer_table, peer_free(&ctx, &peer_ctx))
        .Times(1); // [Pre-Assert確認_正常系] - peer_free が 1 回呼ばれること。

    // Act
    int rtc = potrDisconnectPeer(&ctx, 1); // [手順] - health_alive=0 の状態で potrDisconnectPeer を呼び出す。

    // Assert
    EXPECT_EQ(POTR_SUCCESS, rtc);   // [確認_正常系] - 戻り値が POTR_SUCCESS であること。
    EXPECT_EQ(static_cast<size_t>(0), g_cb.count); // [確認_正常系] - health_alive=0 のためコールバックが呼ばれないこと。
}
