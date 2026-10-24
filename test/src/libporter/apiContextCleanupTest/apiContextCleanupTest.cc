#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <mock_string.h>
#include <porter/porter_result.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <map>

using namespace testing;

// 共通解放が N:1 のコンテキスト資源を回収し、解放前に指定領域を消去することの確認
TEST(apiContextCleanupTest, clears_owned_buffers_and_key_before_free)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    ctx.global.max_payload = 64;
    ctx.global.max_message_size = 64;
    ctx.compress_buf_size = 64;
    ctx.crypto_buf_size = 64;
    ctx.is_multi_peer = 1;
    memset(ctx.service.encrypt_key, 0x5a, sizeof(ctx.service.encrypt_key));
    std::vector<void *> owned;
    ctx.frag_buf = (uint8_t *)malloc(64);
    ctx.compress_buf = (uint8_t *)malloc(64);
    ctx.crypto_buf = (uint8_t *)malloc(64);
    ctx.recv_buf = (uint8_t *)malloc(POTR_PACKET_HEADER_SIZE + 64);
    ctx.recv_compress_buf = (uint8_t *)malloc(64);
    ctx.recv_crypto_buf = (uint8_t *)malloc(64);
    owned = {ctx.frag_buf, ctx.compress_buf, ctx.crypto_buf, ctx.recv_buf, ctx.recv_compress_buf, ctx.recv_crypto_buf};
    for (int i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        ctx.sock[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_recv_buf[i] = (uint8_t *)malloc(POTR_PACKET_HEADER_SIZE + 64);
        owned.push_back(ctx.tcp_recv_buf[i]);
    }
    std::map<void *, size_t> sizes;
    for (void *p : owned)
    {
        sizes[p] = 64;
    }
    sizes[ctx.recv_buf] = POTR_PACKET_HEADER_SIZE + 64;
    for (int i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        sizes[ctx.tcp_recv_buf[i]] = POTR_PACKET_HEADER_SIZE + 64;
    }
    for (const auto &entry : sizes)
    {
        memset(entry.first, 0x5a, entry.second);
    }
    size_t freed = 0;
    bool all_zero = true;
    ON_CALL(mock_cplat, cplat_free(_))
        .WillByDefault(
            [&](void *p)
            {
                if (p != nullptr)
                {
                    const auto *bytes = static_cast<const unsigned char *>(p);
                    all_zero =
                        all_zero && std::all_of(bytes, bytes + sizes.at(p), [](unsigned char b) { return b == 0; });
                    freed++;
                    free(p);
                }
            }); // [状態] - 解放前の内容と解放回数を記録する。
    // Pre-Assert
    EXPECT_CALL(mock_porter, potr_internal_window_dispose(&ctx.send_window)).Times(1);
    // [Pre-Assert確認_正常系] - N:1 でもコンテキストの送信ウィンドウを 1 回破棄すること。
    EXPECT_CALL(mock_porter, potr_internal_window_dispose(&ctx.recv_window)).Times(1);
    // [Pre-Assert確認_正常系] - N:1 でもコンテキストの受信ウィンドウを 1 回破棄すること。
    // Act
    api_context_dispose_resources(&ctx); // [手順] - 停止済みコンテキストの資源を解放する。
    // Assert
    EXPECT_EQ(owned.size(), freed); // [確認_正常系] - 所有した全バッファーを一度ずつ解放すること。
    EXPECT_TRUE(all_zero);          // [確認_正常系] - 全バッファーの内容を解放前に消去すること。
    EXPECT_TRUE(std::all_of(std::begin(ctx.service.encrypt_key), std::end(ctx.service.encrypt_key),
                            [](uint8_t b) { return b == 0; }));
    // [確認_正常系] - コンテキスト内の鍵を消去すること。
    EXPECT_EQ(nullptr, ctx.frag_buf); // [確認_正常系] - 解放したバッファーを NULL にすること。
}

// 部分初期化の TCP 同期資源を一度だけ破棄し、未生成の資源を破棄しないことの確認
TEST(apiContextCleanupTest, partial_tcp_sync_disposal_is_repeatable)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    potr_context ctx = {};
    int lock_token = 0;
    int cv_token = 0;
    ctx.health_mutex[2] = reinterpret_cast<cplat_local_lock *>(&lock_token);
    ctx.health_wakeup[2] = reinterpret_cast<cplat_condvar *>(&cv_token);
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_local_lock_dispose(ctx.health_mutex[2])).WillOnce(Return());
    // [Pre-Assert確認_正常系] - 生成済みの health ロックだけを 1 回破棄すること。
    // [Pre-Assert手順] - テスト用ハンドルの実解放を抑止する。
    EXPECT_CALL(mock_cplat, cplat_condvar_dispose(ctx.health_wakeup[2])).WillOnce(Return());
    // [Pre-Assert確認_正常系] - 生成済みの health 条件変数だけを 1 回破棄すること。
    // [Pre-Assert手順] - テスト用ハンドルの実解放を抑止する。
    EXPECT_CALL(mock_cplat, cplat_local_lock_dispose(nullptr)).Times(0);
    // [Pre-Assert確認_正常系] - 未生成のロックを破棄しないこと。
    EXPECT_CALL(mock_cplat, cplat_condvar_dispose(nullptr)).Times(0);
    // [Pre-Assert確認_正常系] - 未生成の条件変数を破棄しないこと。
    // Act
    api_context_dispose_tcp_sync(&ctx); // [手順] - 部分初期化した同期資源を破棄する。
    api_context_dispose_tcp_sync(&ctx); // [手順] - 解放済み状態で再度破棄を呼ぶ。
    // Assert
    EXPECT_EQ(nullptr, ctx.health_mutex[2]);  // [確認_正常系] - ロックを未生成状態へ戻すこと。
    EXPECT_EQ(nullptr, ctx.health_wakeup[2]); // [確認_正常系] - 条件変数を未生成状態へ戻すこと。
}

// 稼働フラグが 0 でも生成済みハンドルを検出することの確認
TEST(apiContextCleanupTest, detects_finished_but_unjoined_thread)
{
    // Arrange
    potr_context ctx = {};
    int token = 0;
    ctx.connect_thread[1] = reinterpret_cast<cplat_thread *>(&token);
    // Act
    int actual_ret = api_context_has_threads(&ctx); // [手順] - 終了待ちが必要なハンドルを検査する。
    // Assert
    EXPECT_EQ(1, actual_ret); // [確認_正常系] - 稼働フラグが 0 でもハンドルを検出すること。
}

// 残存ソケットとピア資源を一度だけ回収し、解放済み状態へ戻すことの確認
TEST(apiContextCleanupTest, closes_remaining_sockets_and_disposes_peer_table)
{
    // Arrange
    NiceMock<Mock_cplat> mock_cplat;
    NiceMock<Mock_porter> mock_porter;
    potr_context ctx = {};
    for (int i = 0; i < (int)POTR_MAX_PATH; i++)
    {
        ctx.sock[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_conn_fd[i] = CPLAT_INVALID_SOCKET;
        ctx.tcp_listen_sock[i] = CPLAT_INVALID_SOCKET;
    }
    ctx.sock[0] = (cplat_socket)100;
    ctx.tcp_conn_fd[1] = (cplat_socket)101;
    ctx.tcp_listen_sock[2] = (cplat_socket)102;
    int token = 0;
    ctx.peers = reinterpret_cast<potr_internal_peer_context *>(&token);
    ON_CALL(mock_porter, potr_internal_peer_table_dispose(_))
        .WillByDefault([](potr_context *p) { p->peers = nullptr; });
    // Pre-Assert
    EXPECT_CALL(mock_cplat, cplat_socket_close((cplat_socket)100)).WillOnce(Return());
    // [Pre-Assert確認_正常系] - 残存 UDP ソケットを一度閉じること。
    // [Pre-Assert手順] - テスト用ソケットを実際には閉じない。
    EXPECT_CALL(mock_cplat, cplat_socket_close((cplat_socket)101)).WillOnce(Return());
    // [Pre-Assert確認_正常系] - 残存 TCP 接続を一度閉じること。
    // [Pre-Assert手順] - テスト用ソケットを実際には閉じない。
    EXPECT_CALL(mock_cplat, cplat_socket_close((cplat_socket)102)).WillOnce(Return());
    // [Pre-Assert確認_正常系] - 残存 listen ソケットを一度閉じること。
    // [Pre-Assert手順] - テスト用ソケットを実際には閉じない。
    EXPECT_CALL(mock_porter, potr_internal_peer_table_dispose(&ctx)).Times(1);
    // [Pre-Assert確認_正常系] - ピアテーブルの破棄を一度委譲すること。
    // Act
    api_context_dispose_resources(&ctx); // [手順] - 残存資源を回収する。
    api_context_dispose_resources(&ctx); // [手順] - 再度解放を呼ぶ。
    // Assert
    EXPECT_EQ(CPLAT_INVALID_SOCKET, ctx.sock[0]);            // [確認_正常系] - UDP ソケットを INVALID にすること。
    EXPECT_EQ(CPLAT_INVALID_SOCKET, ctx.tcp_conn_fd[1]);     // [確認_正常系] - TCP 接続を INVALID にすること。
    EXPECT_EQ(CPLAT_INVALID_SOCKET, ctx.tcp_listen_sock[2]); // [確認_正常系] - listen ソケットを INVALID にすること。
    EXPECT_EQ(nullptr, ctx.peers);                           // [確認_正常系] - ピアテーブルを NULL にすること。
}
