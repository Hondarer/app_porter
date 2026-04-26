# TCP N:1 サポート追加 設計ドキュメント

## 1. 背景と目的

### 現状

porter ライブラリの TCP サポートは以下の 2 種別のみ。

| 種別 | 方向 | 接続数 |
|---|---|---|
| `POTR_TYPE_TCP` | 単方向 (SENDER → RECEIVER) | 1:1 |
| `POTR_TYPE_TCP_BIDIR` | 双方向 (SENDER ↔ RECEIVER) | 1:1 |

RECEIVER は 1 台の SENDER との接続しか同時に維持できない。これは `receiver_accept_loop()` が `accept()` → `join_recv_thread()` (切断まで待機) → 次の `accept()` という逐次処理になっているためである。

### 解決したい課題

複数の SENDER が 1 台の RECEIVER へ同時接続するシナリオ（例: 複数センサー/エージェントがデータ収集サーバーへ送信）に対応できない。

### 目的

N:1 TCP 通信を可能にする新通信種別を追加する。

---

## 2. 追加する通信種別

```c
// app/porter/prod/include/porter_type.h
POTR_TYPE_TCP_N1       = 10,  /* TCP 単方向 N:1 (N SENDER → 1 RECEIVER) */
POTR_TYPE_TCP_BIDIR_N1 = 11,  /* TCP 双方向 N:1 (N SENDER ↔ 1 RECEIVER) */
```

### SENDER 側の動作

SENDER 側の変更は不要。既存の `POTR_TYPE_TCP` または `POTR_TYPE_TCP_BIDIR` をそのまま使用して接続する。

### RECEIVER 側の動作変更

| 機能 | 既存 1:1 | N:1 |
|---|---|---|
| 同時接続数 | 1 | `max_peers` まで |
| ピア識別 | `POTR_PEER_NA` | `peer_id` で各 SENDER を識別 |
| RECEIVER からの送信 (BIDIR) | 単一接続へ | `peer_id` 指定で特定 SENDER へ |
| マルチパス | 最大 `POTR_MAX_PATH` パス/接続 | 各 SENDER が最大 `POTR_MAX_PATH` パスで接続可 |

---

## 3. アーキテクチャ設計

### 3.1 accept ループのモデル変更

#### 既存の 1:1 accept ループ (逐次処理)

```
[accept スレッド (path 0)]
  accept() ────────── conn_A が来るまで待機
     │
     └─ ctx->tcp_conn_fd[0] = conn_A
        start_connected_threads()   ← recv/health スレッド起動
        join_recv_thread()          ← conn_A の切断まで待機 (ブロック)
        stop_connected_threads()
        ← この間は conn_B を accept できない
     │
     └─ ループ先頭へ: 次の accept()
```

#### N:1 accept ループ (並行処理)

```
[accept スレッド (path 0)]
  accept() ──── conn_A が来る
     │
     └─ peer_create_tcp(conn_A) → peer_A
        tcp_peer_recv_thread_start(peer_A)   ← 非同期
        tcp_peer_health_thread_start(peer_A) ← 非同期 (BIDIR_N1 のみ)
        ← join しない。即座に次の accept へ
     │
  accept() ──── conn_B が来る (conn_A の切断を待たない)
     │
     └─ peer_create_tcp(conn_B) → peer_B
        ...

[peer_A の recv スレッド]                [peer_B の recv スレッド]
  データ受信 → callback(peer_id=A)         データ受信 → callback(peer_id=B)
  切断検知 → peer_free_tcp(peer_A)         切断検知 → peer_free_tcp(peer_B)
```

### 3.2 スレッド構成

#### 既存 1:1 TCP (1 サービス当たり)

```
accept スレッド × n_path
recv スレッド   × n_path
health スレッド × n_path   (BIDIR or SENDER のみ)
send スレッド   × 1        (BIDIR or SENDER のみ)
```

#### N:1 TCP (1 サービス当たり)

```
accept スレッド       × n_path          ← ピアをまたいで共有
recv スレッド         × n_path × peer数 ← per-peer, per-path
health スレッド       × n_path × peer数 ← per-peer, per-path (BIDIR_N1 のみ)
send スレッド         × 1               ← 全ピア共有 (BIDIR_N1 のみ)
```

最大スレッド数: `n_path(4) × max_peers × 2 + n_path(4) + 1` (BIDIR_N1 の場合)

> **注意**: `max_peers` が大きいとスレッド数が増加する。TCP N:1 向けのデフォルト `max_peers` は 32 程度を推奨する。大量接続が必要な場合は将来的に epoll/IOCP ベースへの移行を検討すること。

---

## 4. データ構造の変更

### 4.1 PotrType 追加

**ファイル**: `app/porter/prod/include/porter_type.h`

```c
typedef enum {
    POTR_TYPE_UNICAST_RAW       = 1,  /* UDP 再送制御無し */
    POTR_TYPE_MULTICAST_RAW     = 2,
    POTR_TYPE_BROADCAST_RAW     = 3,
    POTR_TYPE_UNICAST           = 4,  /* UDP 再送制御あり */
    POTR_TYPE_MULTICAST         = 5,
    POTR_TYPE_BROADCAST         = 6,
    POTR_TYPE_UNICAST_BIDIR     = 7,  /* UDP 双方向 */
    POTR_TYPE_UNICAST_BIDIR_N1  = 8,  /* UDP 双方向 N:1 */
    POTR_TYPE_TCP               = 9,  /* TCP */
    POTR_TYPE_TCP_BIDIR         = 10, /* TCP 双方向 */
    /* POTR_TYPE_TCP_BIDIR_N1   = 11, */ /* TCP 双方向 N:1 (将来) */
} PotrType;
```

### 4.2 PotrPeerContext への TCP N:1 フィールド追加

**ファイル**: `app/porter/prod/libsrc/porter/potrContext.h`

既存の UDP 専用フィールド (`dest_addr[]`, `n_paths`, `path_last_recv_sec[]` 等) はそのまま保持し、末尾に TCP N:1 専用フィールドを追加する。

```c
typedef struct PotrPeerContext_
{
    /* ===== 既存フィールド (変更なし) ===== */
    PotrPeerId  peer_id;
    int         active;

    /* セッション管理 */
    uint32_t session_id;
    int64_t  session_tv_sec;
    int32_t  session_tv_nsec;
    int      peer_session_known;
    uint32_t peer_session_id;
    /* ... */

    /* ウィンドウ */
    PotrWindow send_window;
    PotrMutex  send_window_mutex;
    PotrWindow recv_window;

    /* フラグメントバッファ */
    uint8_t *frag_buf;
    size_t   frag_buf_len;
    int      frag_compressed;

    /* ヘルス */
    volatile int health_alive;
    int64_t last_recv_tv_sec;
    int32_t last_recv_tv_nsec;

    /* NACK 重複抑制 */
    PotrNackDedupEntry nack_dedup_buf[POTR_NACK_DEDUP_SLOTS];
    uint8_t            nack_dedup_next;

    /* リオーダー */
    int      reorder_pending;
    uint32_t reorder_nack_num;
    int64_t  reorder_deadline_sec;
    int32_t  reorder_deadline_nsec;

    /* UDP 専用フィールド */
    struct sockaddr_in dest_addr[POTR_MAX_PATH];
    int                n_paths;
    int64_t            path_last_recv_sec[POTR_MAX_PATH];
    int32_t            path_last_recv_nsec[POTR_MAX_PATH];

    /* ===== 追加: TCP N:1 専用フィールド ===== */

    /* per-peer 接続 fd (path ごと、未接続は POTR_INVALID_SOCKET) */
    PotrSocket         tcp_conn_fd[POTR_MAX_PATH];

    /* PING 追跡 (per-path) */
    volatile uint64_t  tcp_last_ping_recv_ms[POTR_MAX_PATH];
    volatile uint64_t  tcp_last_ping_req_recv_ms[POTR_MAX_PATH];

    /* 送信バッファ満杯ログ抑制 */
    int                tcp_buf_full_suppress_cnt[POTR_MAX_PATH];

    /* per-peer recv スレッド */
    PotrThread         tcp_recv_thread[POTR_MAX_PATH];
    volatile int       tcp_recv_running[POTR_MAX_PATH];

    /* per-peer health スレッド (BIDIR_N1 のみ使用) */
    PotrThread         tcp_health_thread[POTR_MAX_PATH];
    volatile int       tcp_health_running[POTR_MAX_PATH];

    /* per-peer 送信排他制御 */
    PotrMutex          tcp_send_mutex[POTR_MAX_PATH];

    /* health スレッド制御 */
    PotrMutex          tcp_health_mutex[POTR_MAX_PATH];
    PotrCondVar        tcp_health_wakeup[POTR_MAX_PATH];

    /* recv_window 保護 (per-peer, ctx->recv_window_mutex とは独立) */
    PotrMutex          tcp_recv_window_mutex;

    /* 0 でスレッドをすべて終了させる強制停止フラグ */
    volatile int       tcp_peer_running;

    /* per-peer スレッドから ctx へアクセスするバックポインタ */
    struct PotrContext_ *ctx_back;

} PotrPeerContext;
```

### 4.3 inline 判定関数の追加・変更

**ファイル**: `app/porter/prod/libsrc/porter/potrContext.h`

```c
/* 新規追加: TCP N:1 型か判定 */
static inline int potr_is_tcp_n1_type(PotrType t)
{
    return t == POTR_TYPE_TCP_N1 || t == POTR_TYPE_TCP_BIDIR_N1;
}

/* 変更: 既存 potr_is_tcp_type() に新型を追加 */
static inline int potr_is_tcp_type(PotrType t)
{
    return t == POTR_TYPE_TCP
        || t == POTR_TYPE_TCP_BIDIR
        || t == POTR_TYPE_TCP_N1       /* 追加 */
        || t == POTR_TYPE_TCP_BIDIR_N1; /* 追加 */
}
```

---

## 5. 新規・変更ファイル詳細

### 5.1 potrPeerTable.c - peer_create_tcp() 追加

**ファイル**: `app/porter/prod/libsrc/porter/potrPeerTable.h` / `.c`

#### 関数シグネチャ

```c
/**
 * TCP N:1 用ピアを作成する。
 * peers_mutex を取得した状態で呼び出すこと。
 *
 * @param ctx       サービスコンテキスト
 * @param conn      accept() で得た接続ソケット
 * @param peer_addr 接続元アドレス
 * @param path_idx  接続が確立した path インデックス
 * @return          成功時は新しいピアコンテキストへのポインタ。
 *                  失敗時 (max_peers 超過など) は NULL。
 */
PotrPeerContext *peer_create_tcp(struct PotrContext_ *ctx,
                                 PotrSocket conn,
                                 const struct sockaddr_in *peer_addr,
                                 int path_idx);
```

#### 処理内容

```
1. 空きスロット確認 (n_peers >= max_peers なら NULL を返す)
2. 空きスロットに対して以下を初期化:
   a. セッション生成 (peer_generate_session())
   b. window_init(&peer->send_window, window_size, max_payload)
   c. window_init(&peer->recv_window, window_size, max_payload)
   d. POTR_MUTEX_INIT(peer->send_window_mutex)
   e. POTR_MUTEX_INIT(peer->tcp_recv_window_mutex)
   f. frag_buf = malloc(max_message_size) ; frag_buf_len = 0
   g. 全 path の tcp_conn_fd を POTR_INVALID_SOCKET に設定
   h. tcp_conn_fd[path_idx] = conn (今回 accept した path のみ設定)
   i. tcp_last_ping_recv_ms[path_idx] = current_ms()
   j. tcp_last_ping_req_recv_ms[path_idx] = current_ms()
   k. 全 path の tcp_send_mutex, tcp_health_mutex, tcp_health_wakeup を初期化
   l. tcp_peer_running = 1
   m. ctx_back = ctx
   n. peer_id = allocate_peer_id(ctx)
   o. active = 1 ; ctx->n_peers++
3. ポインタを返す
```

#### peer_free() の拡張

TCP N:1 型の場合、既存の UDP N:1 用 cleanup に加えて以下を実行する。

**前提**: 呼び出し前に全 path の recv/health スレッドが停止済みであること。

```
・全 path の tcp_conn_fd が POTR_INVALID_SOCKET であることを確認 (assert)
・全 path の tcp_send_mutex を POTR_MUTEX_DESTROY
・全 path の tcp_health_mutex を POTR_MUTEX_DESTROY
・全 path の tcp_health_wakeup を POTR_CONDVAR_DESTROY
・tcp_recv_window_mutex を POTR_MUTEX_DESTROY
```

### 5.2 potrTcpPeerThread.c - 新規ファイル

**ファイル**: `app/porter/prod/libsrc/porter/thread/potrTcpPeerThread.h` / `.c`

per-peer の recv スレッドと health スレッドを実装する。既存の `potrRecvThread.c` / `potrHealthThread.c` のパケット処理ロジックを内部関数として再利用する。

#### API

```c
/* per-peer recv スレッド起動 */
int  tcp_peer_recv_thread_start(struct PotrContext_ *ctx,
                                PotrPeerContext *peer,
                                int path_idx);

/* per-peer health スレッド起動 (BIDIR_N1 のみ) */
int  tcp_peer_health_thread_start(struct PotrContext_ *ctx,
                                  PotrPeerContext *peer,
                                  int path_idx);
```

#### recv スレッド関数の処理フロー

```c
static void *tcp_peer_recv_thread_func(void *arg)
{
    /* TcpPeerThreadArg: { ctx, peer, path_idx } */
    struct PotrContext_ *ctx      = arg->ctx;
    PotrPeerContext     *peer     = arg->peer;
    int                  path_idx = arg->path_idx;

    /* === recv ループ === */
    while (peer->tcp_recv_running[path_idx] && peer->tcp_peer_running)
    {
        /* tcp_recv_read_packet() で外側パケットヘッダーを読み込む (既存ロジック流用) */
        n = tcp_read_packet(peer->tcp_conn_fd[path_idx], buf, &pkt);
        if (n <= 0) break;  /* FIN / エラー → 切断検知 */

        /* パケット処理 (peer->recv_window, peer->frag_buf を参照) */
        tcp_process_packet_peer(ctx, peer, &pkt);
        /* └── データパケット: ctx->callback(service_id, peer->peer_id,
                                              POTR_EVENT_DATA, data, len) */
        /* └── PING パケット: peer->tcp_last_ping_req_recv_ms 更新 + PONG 返送 */
        /* └── PONG パケット: peer->tcp_last_ping_recv_ms 更新 */
        /* └── FIN パケット: recv ループ終了 */

        /* 初回データ受信時: CONNECTED イベント発火 */
        if (!peer->health_alive) {
            peer->health_alive = 1;
            ctx->callback(service_id, peer->peer_id, POTR_EVENT_CONNECTED, NULL, 0);
        }
    }

    /* === 切断処理 === */
    /*
     * デッドロック回避順序:
     *  1. health スレッドを peers_mutex 外で停止 (health スレッドは peers_mutex を取得しない)
     *  2. peers_mutex を取得してから conn_fd クローズ・ピア解放
     */

    /* 1. health スレッドを先に停止 */
    peer->tcp_health_running[path_idx] = 0;
    POTR_CONDVAR_SIGNAL(peer->tcp_health_wakeup[path_idx]);
    POTR_THREAD_JOIN(peer->tcp_health_thread[path_idx]);

    /* 2. peers_mutex 保護下で後始末 */
    POTR_MUTEX_LOCK(&ctx->peers_mutex);
    {
        /* ソケットをクローズ */
        shutdown(peer->tcp_conn_fd[path_idx], ...);
        POTR_SOCKET_CLOSE(peer->tcp_conn_fd[path_idx]);
        peer->tcp_conn_fd[path_idx] = POTR_INVALID_SOCKET;

        /* 残りの active path を確認 */
        int remaining = 0;
        for (k = 0; k < ctx->n_path; k++)
            if (peer->tcp_conn_fd[k] != POTR_INVALID_SOCKET) remaining++;

        /* 全パス切断時: DISCONNECTED 通知 + ピア解放 */
        if (remaining == 0) {
            if (peer->health_alive) {
                peer->health_alive = 0;
                ctx->callback(service_id, peer->peer_id,
                              POTR_EVENT_DISCONNECTED, NULL, 0);
            }
            peer_free_tcp(ctx, peer);
        }
    }
    POTR_MUTEX_UNLOCK(&ctx->peers_mutex);

    return NULL;
}
```

#### health スレッド関数の処理フロー

既存の `potr_tcp_health_thread_func()` を per-peer 版に移植する。
`ctx->tcp_conn_fd[path_idx]` / `ctx->tcp_last_ping_recv_ms` を
`peer->tcp_conn_fd[path_idx]` / `peer->tcp_last_ping_recv_ms` に置き換える。

```c
static void *tcp_peer_health_thread_func(void *arg)
{
    /* TcpPeerThreadArg: { ctx, peer, path_idx } */
    while (peer->tcp_health_running[path_idx] && peer->tcp_peer_running)
    {
        /* health_interval_ms だけ待機 (tcp_health_wakeup で中断可能) */
        POTR_CONDVAR_TIMEDWAIT(peer->tcp_health_wakeup[path_idx],
                               peer->tcp_health_mutex[path_idx],
                               health_interval_ms);

        if (!peer->tcp_health_running[path_idx]) break;

        /* PING 送信 */
        tcp_send_ping(peer->tcp_conn_fd[path_idx], &peer->tcp_send_mutex[path_idx], ctx);

        /* タイムアウト確認 */
        uint64_t now = health_get_ms();
        if (now - peer->tcp_last_ping_recv_ms[path_idx] > health_timeout_ms) {
            POTR_LOG(INFO, "peer[%u] path[%d]: PING timeout, closing connection",
                     peer->peer_id, path_idx);
            /* ソケットクローズで recv スレッドを切断検知させる */
            POTR_SOCKET_CLOSE(peer->tcp_conn_fd[path_idx]);
            break;
        }
    }
    return NULL;
}
```

### 5.3 potrConnectThread.c - N:1 accept ループ追加

**ファイル**: `app/porter/prod/libsrc/porter/thread/potrConnectThread.c`

#### receiver_accept_n1_loop() 新規追加

```c
/* RECEIVER 用 accept ループ (N:1, TCP_N1 / TCP_BIDIR_N1 専用) */
static void receiver_accept_n1_loop(struct PotrContext_ *ctx, int path_idx)
{
    int is_bidir = (ctx->service.type == POTR_TYPE_TCP_BIDIR_N1);

    while (ctx->connect_thread_running[path_idx])
    {
        PotrSocket         conn;
        struct sockaddr_in peer_addr;
        socklen_t          peer_len = (socklen_t)sizeof(peer_addr);
        PotrPeerContext   *peer;
        char               peer_addr_str[INET_ADDRSTRLEN];

        conn = accept(ctx->tcp_listen_sock[path_idx],
                      (struct sockaddr *)&peer_addr, &peer_len);

        if (conn == POTR_INVALID_SOCKET)
        {
            if (!ctx->connect_thread_running[path_idx]) break;
            continue;
        }

        inet_ntop(AF_INET, &peer_addr.sin_addr, peer_addr_str, sizeof(peer_addr_str));

        /* 接続元フィルタ (既存 receiver_accept_loop と同一ロジック) */
        if (tcp_n1_is_src_filtered(ctx, path_idx, &peer_addr))
        {
            POTR_LOG(POTR_TRACE_INFO, "connect_thread[service_id=%" PRId64 " path=%d]: "
                     "rejected (src filter) from %s:%u",
                     ctx->service.service_id, path_idx,
                     peer_addr_str, (unsigned)ntohs(peer_addr.sin_port));
            POTR_SOCKET_CLOSE(conn);
            continue;
        }

        POTR_LOG(POTR_TRACE_INFO, "connect_thread[service_id=%" PRId64 " path=%d]: "
                 "TCP N:1 accepted from %s:%u",
                 ctx->service.service_id, path_idx,
                 peer_addr_str, (unsigned)ntohs(peer_addr.sin_port));

        /* ピア作成 (peers_mutex 保護) */
        POTR_MUTEX_LOCK(&ctx->peers_mutex);
        peer = peer_create_tcp(ctx, conn, &peer_addr, path_idx);
        POTR_MUTEX_UNLOCK(&ctx->peers_mutex);

        if (peer == NULL)
        {
            POTR_LOG(POTR_TRACE_WARNING, "connect_thread[service_id=%" PRId64 " path=%d]: "
                     "max_peers reached, rejected from %s:%u",
                     ctx->service.service_id, path_idx,
                     peer_addr_str, (unsigned)ntohs(peer_addr.sin_port));
            POTR_SOCKET_CLOSE(conn);
            continue;
        }

        POTR_LOG(POTR_TRACE_VERBOSE, "connect_thread[service_id=%" PRId64 " path=%d]: "
                 "peer[%u] created",
                 ctx->service.service_id, path_idx, peer->peer_id);

        /* per-peer スレッド起動 (join しない → 即座に次の accept へ) */
        if (tcp_peer_recv_thread_start(ctx, peer, path_idx) != POTR_SUCCESS)
        {
            POTR_LOG(POTR_TRACE_ERROR, "connect_thread: tcp_peer_recv_thread_start failed");
            POTR_MUTEX_LOCK(&ctx->peers_mutex);
            peer_free_tcp(ctx, peer);
            POTR_MUTEX_UNLOCK(&ctx->peers_mutex);
            POTR_SOCKET_CLOSE(conn);
            continue;
        }

        if (is_bidir)
        {
            if (tcp_peer_health_thread_start(ctx, peer, path_idx) != POTR_SUCCESS)
            {
                POTR_LOG(POTR_TRACE_ERROR,
                         "connect_thread: tcp_peer_health_thread_start failed");
                /* recv スレッドはソケットクローズで自然終了する */
                POTR_SOCKET_CLOSE(conn);
                continue;
            }
        }
    }
}
```

#### connect_thread_func() への分岐追加

```c
static void *connect_thread_func(void *arg)
{
    /* ... */
    if (ctx->role == POTR_ROLE_SENDER)
    {
        sender_connect_loop(ctx, path_idx);  /* 変更なし */
    }
    else if (ctx->is_multi_peer)
    {
        receiver_accept_n1_loop(ctx, path_idx);  /* 追加 */
    }
    else
    {
        receiver_accept_loop(ctx, path_idx);  /* 既存 */
    }
    /* ... */
}
```

### 5.4 potrSendThread.c - flush_packed_peer() 拡張

**ファイル**: `app/porter/prod/libsrc/porter/thread/potrSendThread.c`

`PotrPayloadElem` にはすでに `peer_id` フィールドが存在するため、
送信先ルーティングの変更のみで対応できる。

```c
static void flush_packed_peer(struct PotrContext_ *ctx,
                               PotrPeerContext *peer,
                               const uint8_t *wire_buf, size_t wire_len)
{
    int k;

    if (potr_is_tcp_n1_type(ctx->service.type))
    {
        /* TCP N:1: per-peer の tcp_conn_fd[path] へ送信 */
        for (k = 0; k < ctx->n_path; k++)
        {
            if (peer->tcp_conn_fd[k] == POTR_INVALID_SOCKET) continue;

            /* バッファ空き確認 (非ブロッキング poll) */
            struct pollfd pfd = { peer->tcp_conn_fd[k], POLLOUT, 0 };
            int pr = poll(&pfd, 1, 0);
            if (pr > 0 && (pfd.revents & POLLOUT))
            {
                if (tcp_send_all(peer->tcp_conn_fd[k],
                                 &peer->tcp_send_mutex[k],
                                 wire_buf, wire_len) != POTR_SUCCESS)
                {
                    POTR_LOG(POTR_TRACE_WARNING,
                             "send_thread: peer[%u] path[%d]: tcp_send_all failed",
                             peer->peer_id, k);
                }
            }
            else
            {
                /* バッファ満杯: ログ抑制付きで警告 */
                if (peer->tcp_buf_full_suppress_cnt[k]++ == 0)
                {
                    POTR_LOG(POTR_TRACE_WARNING,
                             "send_thread: peer[%u] path[%d]: send buffer full, "
                             "dropping packet", peer->peer_id, k);
                }
            }
        }
    }
    else
    {
        /* 既存 UDP N:1 */
        for (k = 0; k < peer->n_paths; k++)
            sendto(ctx->sock[0], wire_buf, wire_len, 0,
                   (struct sockaddr *)&peer->dest_addr[k], sizeof(peer->dest_addr[k]));
    }
}
```

### 5.5 potrOpenService.c - TCP N:1 初期化

**ファイル**: `app/porter/prod/libsrc/porter/api/potrOpenService.c`

```c
case POTR_TYPE_TCP_N1:
case POTR_TYPE_TCP_BIDIR_N1:
{
    int is_bidir_n1 = (ctx->service.type == POTR_TYPE_TCP_BIDIR_N1);

    if (ctx->role == POTR_ROLE_RECEIVER)
    {
        /* 1. listen ソケット作成 (既存 TCP RECEIVER と同一処理を流用) */
        for (i = 0; i < ctx->n_path; i++) {
            ctx->tcp_listen_sock[i] = open_socket_tcp_receiver(ctx, i);
            if (ctx->tcp_listen_sock[i] == POTR_INVALID_SOCKET) goto error;
        }

        /* 2. ピアテーブル初期化 */
        ctx->is_multi_peer = 1;
        if (peer_table_init(ctx) != POTR_SUCCESS) goto error;

        /* 3. TCP mutex/condvar 初期化 (ctx レベル: peers_mutex は peer_table_init で初期化済み) */
        /* tcp_state_mutex, tcp_state_cv は 1:1 TCP と共通のため初期化しておく */
        POTR_MUTEX_INIT(&ctx->tcp_state_mutex);
        POTR_CONDVAR_INIT(&ctx->tcp_state_cv);

        /* 4. TCP_BIDIR_N1: 送信スレッドを事前起動 */
        /*    (最初の peer 接続前から起動しておく。n_peers=0 時の potrSend はエラーを返す) */
        if (is_bidir_n1) {
            if (potr_send_queue_init(&ctx->send_queue,
                                     POTR_SEND_QUEUE_DEPTH,
                                     (uint16_t)ctx->global.max_payload) != POTR_SUCCESS)
                goto error;
            if (potr_send_thread_start(ctx) != POTR_SUCCESS) goto error;
        }

        /* 5. accept スレッド起動 → receiver_accept_n1_loop が呼ばれる */
        if (potr_connect_thread_start(ctx) != POTR_SUCCESS) goto error;
    }
    else /* POTR_ROLE_SENDER */
    {
        /* SENDER 側: 既存 TCP/TCP_BIDIR SENDER と同一 */
        /* (宛先アドレス解決 + connect スレッド起動) */
        for (i = 0; i < ctx->n_path; i++) {
            if (resolve_dst_addr(ctx, i) != POTR_SUCCESS) goto error;
        }
        if (init_send_resources(ctx) != POTR_SUCCESS) goto error;
        if (potr_connect_thread_start(ctx) != POTR_SUCCESS) goto error;
    }
    break;
}
```

### 5.6 potrCloseService.c - TCP N:1 終了処理

**ファイル**: `app/porter/prod/libsrc/porter/api/potrCloseService.c`

```c
case POTR_TYPE_TCP_N1:
case POTR_TYPE_TCP_BIDIR_N1:
{
    if (ctx->role == POTR_ROLE_RECEIVER)
    {
        /* 1. accept スレッドを停止 (connect_thread_running = 0) */
        /*    listen sock クローズで accept() をアンブロック */
        for (i = 0; i < ctx->n_path; i++) {
            ctx->connect_thread_running[i] = 0;
            if (ctx->tcp_listen_sock[i] != POTR_INVALID_SOCKET) {
                POTR_SOCKET_CLOSE(ctx->tcp_listen_sock[i]);
            }
        }
        potr_connect_thread_join(ctx);  /* accept スレッド JOIN */

        /* 2. 全アクティブピアのスレッドを強制停止 */
        POTR_MUTEX_LOCK(&ctx->peers_mutex);
        for (i = 0; i < ctx->max_peers; i++) {
            if (!ctx->peers[i].active) continue;
            ctx->peers[i].tcp_peer_running = 0;
            /* conn_fd クローズ → recv スレッドが recv() エラーで終了する */
            for (k = 0; k < ctx->n_path; k++) {
                if (ctx->peers[i].tcp_conn_fd[k] != POTR_INVALID_SOCKET) {
                    POTR_SOCKET_CLOSE(ctx->peers[i].tcp_conn_fd[k]);
                    ctx->peers[i].tcp_conn_fd[k] = POTR_INVALID_SOCKET;
                }
                ctx->peers[i].tcp_recv_running[k]   = 0;
                ctx->peers[i].tcp_health_running[k] = 0;
                POTR_CONDVAR_SIGNAL(ctx->peers[i].tcp_health_wakeup[k]);
            }
        }
        POTR_MUTEX_UNLOCK(&ctx->peers_mutex);

        /* 3. 全ピアのスレッドを JOIN してからテーブルを解放 */
        peer_table_destroy_tcp(ctx);  /* 全ピアの recv/health スレッド JOIN + peer_free_tcp */

        /* 4. TCP_BIDIR_N1: 送信スレッドと送信キューを解放 */
        if (ctx->service.type == POTR_TYPE_TCP_BIDIR_N1) {
            potr_send_queue_shutdown(&ctx->send_queue);
            potr_send_thread_stop(ctx);
            potr_send_queue_destroy(&ctx->send_queue);
        }

        /* 5. ctx レベルの mutex/condvar 解放 */
        POTR_MUTEX_DESTROY(&ctx->tcp_state_mutex);
        POTR_CONDVAR_DESTROY(&ctx->tcp_state_cv);
    }
    else /* SENDER */
    {
        /* 既存 TCP SENDER クローズと同一 */
        potr_connect_thread_stop(ctx);
        /* ... */
    }
    break;
}
```

### 5.7 potrSend.c - BIDIR_N1 RECEIVER からの送信許可

**ファイル**: `app/porter/prod/libsrc/porter/api/potrSend.c`

```c
/* 送信可否チェック部分 */
int is_bidir = (ctx->service.type == POTR_TYPE_TCP_BIDIR)
            || (ctx->service.type == POTR_TYPE_TCP_BIDIR_N1);  /* 追加 */

if (ctx->role == POTR_ROLE_RECEIVER && !is_bidir) {
    return POTR_ERROR;  /* 単方向 RECEIVER は送信不可 */
}

/* TCP N:1: peer_id が POTR_PEER_NA なら全ピアへ送信 (ブロードキャスト相当)
   peer_id が指定されていれば対象ピアへのみ送信 */
```

### 5.8 potrDisconnectPeer.c - TCP N:1 ピア切断

**ファイル**: `app/porter/prod/libsrc/porter/api/potrDisconnectPeer.c`

```c
if (potr_is_tcp_n1_type(ctx->service.type))
{
    /* 対象ピアを検索 */
    POTR_MUTEX_LOCK(&ctx->peers_mutex);
    PotrPeerContext *peer = peer_find_by_id(ctx, peer_id);
    if (peer != NULL && peer->active)
    {
        /* FIN パケット送信後にソケットをクローズ */
        for (k = 0; k < ctx->n_path; k++) {
            if (peer->tcp_conn_fd[k] != POTR_INVALID_SOCKET) {
                tcp_send_fin(peer->tcp_conn_fd[k], &peer->tcp_send_mutex[k], ctx);
                /* recv スレッドが切断を検知して cleanup する */
                /* ここでは shutdown のみ (close は recv スレッドが行う) */
                shutdown(peer->tcp_conn_fd[k], SHUT_WR);
            }
        }
        peer->tcp_peer_running = 0;
    }
    POTR_MUTEX_UNLOCK(&ctx->peers_mutex);
}
```

---

## 6. 設計上の懸念点とトレードオフ

### 6.1 PotrPeerContext のサイズ増加

追加する TCP N:1 フィールドのサイズ概算 (POTR_MAX_PATH=4 の場合):

| フィールド | サイズ (approx) |
|---|---|
| `tcp_conn_fd[4]` (int or SOCKET) | 16〜32 B |
| `tcp_last_ping_recv_ms[4]` | 32 B |
| `tcp_last_ping_req_recv_ms[4]` | 32 B |
| `tcp_recv_thread[4]` (pthread_t) | 32 B |
| `tcp_health_thread[4]` | 32 B |
| `tcp_send_mutex[4]` (pthread_mutex_t) | 160 B |
| `tcp_health_mutex[4]` | 160 B |
| `tcp_health_wakeup[4]` (pthread_cond_t) | 192 B |
| `tcp_recv_window_mutex` | 40 B |
| その他整数フィールド | 〜32 B |
| **合計** | **約 730〜760 B 増加** |

`max_peers = 1024` の場合、ピアテーブル全体で約 750 KB の増加。

**推奨**: TCP N:1 向けのデフォルト `max_peers` を 32 程度に設定すること。

### 6.2 デッドロックリスクと回避策

以下の順序を厳守することでデッドロックを回避する。

```
recv スレッド (切断時):
  1. tcp_health_running[k] = 0 + SIGNAL     ← peers_mutex 外
  2. JOIN(tcp_health_thread[k])             ← peers_mutex 外
  3. LOCK(peers_mutex)
  4. conn_fd クローズ + peer_free_tcp()
  5. UNLOCK(peers_mutex)

health スレッド:
  - peers_mutex を取得しない (取得不可の設計とする)
  - ソケットクローズで recv スレッドへ切断を伝える

potrCloseService():
  1. connect_thread_running = 0 (新規 accept を止める)
  2. LOCK(peers_mutex) → tcp_peer_running = 0 + conn_fd クローズ → UNLOCK
  3. peer_table_destroy_tcp() → 全スレッドを JOIN
```

### 6.3 ctx->tcp_active_paths カウンタの扱い

1:1 TCP の `ctx->tcp_active_paths` は N:1 では使用しない。
N:1 での送信可否判定は `peer->tcp_conn_fd[k] != POTR_INVALID_SOCKET` で行う。

### 6.4 スレッドスケーラビリティ

Thread-per-connection モデルでの最大スレッド数 (BIDIR_N1, n_path=4, max_peers=32):

```
accept スレッド:   4
recv スレッド:    128  (4 path × 32 peers)
health スレッド:  128  (4 path × 32 peers)
send スレッド:      1
合計:            261 スレッド
```

大量接続 (max_peers > 100) が必要な場合は、将来的に epoll (Linux) / IOCP (Windows) ベースのスレッドプールモデルへの移行を検討すること。

### 6.5 マルチパスにおけるセッション管理

#### 6.5.1 問題の背景 (TCP 1:1 マルチパスの不具合)

本問題は N:1 固有ではなく、**既存の TCP 1:1 マルチパス実装にも存在した構造上の不具合**である。

UDP では `recvfrom()` がデータ受信・送信元アドレス取得・セッション ID 識別を原子的に行うため、同一 SENDER の再接続や追加パス接続を自然にセッション層で識別できる。
一方 TCP の `accept()` はソケット fd のみを返し、アプリケーション層のセッション識別子 (`session_id`, `session_tv_sec`, `session_tv_nsec`) は最初のパケットを受信するまで不明である。

このため、従来実装では `accept()` 直後に無条件で `reset_connection_state()` を呼んでいた。この関数は `peer_session_known = 0` をリセットするが、他の path の recv スレッドがすでにセッションデータを処理中である可能性があり、データ競合が発生していた。さらに、新セッションの接続なのか同一セッションの再接続・追加パスなのかを区別できなかった。

#### 6.5.2 実装済みの修正: セッション層での対称化

TCP と UDP のセッション識別をセッション層レベルで対称にする修正を実装した (`potrConnectThread.c`, `potrRecvThread.c`, `potrContext.h`)。

**設計の概要:**

1. **accept スレッドが最初のパケットを先読みする**
   `accept()` 直後、`tcp_read_first_packet()` でアプリケーション層の最初のパケットを受信する (タイムアウト付き)。パケット内の session triplet (`session_id`, `session_tv_sec`, `session_tv_nsec`) を取得する。

2. **`session_establish_mutex` によるシリアライズ**
   複数パスの accept スレッドが `ctx->peer_session_*` フィールドを同時に参照・更新しないよう、`session_establish_mutex` で排他制御する。

3. **セッション比較 (`tcp_session_compare()`) による 3 分類**

   | 結果 | 意味 | 処置 |
   |---|---|---|
   | `TCP_SESSION_NEW` | 新しいセッション (または初回接続) | 他の全アクティブパスを切断し `reset_connection_state()` を呼ぶ。その後 `tcp_conn_fd[path_idx]` を設定してスレッドを起動する。 |
   | `TCP_SESSION_SAME` | 既存セッションの同一セッション ID | 追加パスとして接続する。`reset_connection_state()` は呼ばない。 |
   | `TCP_SESSION_OLD` | 過去のセッション (期限切れ) | 接続を閉じてループを継続する。 |

4. **先読みバッファの引き渡し**
   accept スレッドが読み取った最初のパケットを `tcp_first_pkt_buf[path_idx]` / `tcp_first_pkt_len[path_idx]` に格納する。recv スレッドはループ開始時にこのバッファを先に処理し、通常の recv ループに入る。

**追加されたフィールド (`potrContext.h`):**

```c
/* TCP セッション確立排他制御 (RECEIVER TCP のみ使用) */
PotrMutex          session_establish_mutex;

/* TCP 先読みバッファ (path ごと) */
uint8_t           *tcp_first_pkt_buf[POTR_MAX_PATH];
size_t             tcp_first_pkt_len[POTR_MAX_PATH];
```

#### 6.5.3 N:1 実装における注意事項

TCP N:1 の `peer_create_tcp()` を実装する際は、上記の session-layer 識別を基盤として、以下の設計を採用すること。

- `accept()` 後の先読みで得た session triplet を `peer_create_tcp()` / `peer_lookup_by_session()` の検索キーとして使用する
- 同一 session triplet の新たな path 接続は既存ピアへの追加パスとして扱う
- 異なる session triplet は新規ピアとして扱う (`peer_create_tcp()` を呼ぶ)
- per-peer の `session_establish_mutex` は各 `PotrPeerContext` に持たせ、ピア間の競合を防ぐ

---

## 7. 変更ファイル一覧

| ファイル | 変更種別 | 内容 |
|---|---|---|
| `app/porter/prod/include/porter_type.h` | 変更 | `POTR_TYPE_TCP_N1 = 10`, `POTR_TYPE_TCP_BIDIR_N1 = 11` を追加 |
| `app/porter/prod/libsrc/porter/potrContext.h` | 変更 | `PotrPeerContext` に TCP N:1 フィールド追加; `potr_is_tcp_n1_type()` 追加; `potr_is_tcp_type()` 拡張 |
| `app/porter/prod/libsrc/porter/potrPeerTable.h` | 変更 | `peer_create_tcp()` シグネチャ追加 |
| `app/porter/prod/libsrc/porter/potrPeerTable.c` | 変更 | `peer_create_tcp()` 実装追加; `peer_free()` 拡張 |
| `app/porter/prod/libsrc/porter/thread/potrConnectThread.c` | 変更 | `receiver_accept_n1_loop()` 追加; `connect_thread_func()` に型判定分岐; `tcp_read_first_packet()`, `tcp_session_compare()` 追加; `receiver_accept_loop()` をセッション先読み方式に改修; `session_establish_mutex` の初期化・破棄; `tcp_first_pkt_buf` の malloc/free |
| `app/porter/prod/libsrc/porter/thread/potrRecvThread.c` | 変更 | `tcp_recv_thread_func()` に先読みバッファ (`tcp_first_pkt_buf`) の先行処理を追加 |
| `app/porter/prod/libsrc/porter/thread/potrTcpPeerThread.h` | **新規** | per-peer TCP スレッド API 宣言 |
| `app/porter/prod/libsrc/porter/thread/potrTcpPeerThread.c` | **新規** | per-peer recv + health スレッド実装 |
| `app/porter/prod/libsrc/porter/thread/potrSendThread.c` | 変更 | `flush_packed_peer()` に TCP N:1 送信分岐を追加 |
| `app/porter/prod/libsrc/porter/thread/potrHealthThread.c` | 変更 | per-peer PING タイムアウト処理 (N:1 向け) |
| `app/porter/prod/api/potrOpenService.c` | 変更 | TCP N:1 初期化 `case` 追加 |
| `app/porter/prod/api/potrCloseService.c` | 変更 | TCP N:1 終了処理追加 |
| `app/porter/prod/api/potrSend.c` | 変更 | BIDIR_N1 RECEIVER からの送信許可; peer_id ルーティング |
| `app/porter/prod/api/potrDisconnectPeer.c` | 変更 | TCP N:1 ピア切断対応 |

---

## 8. 実装順序

以下の順序で実装することで、各ステップでビルドを通しながら進められる。

1. **型定義** (`porter_type.h`, `potrContext.h`)
   - `POTR_TYPE_TCP_N1`, `POTR_TYPE_TCP_BIDIR_N1` を追加
   - `PotrPeerContext` に TCP N:1 フィールドを追加
   - `potr_is_tcp_n1_type()` を追加
   - `potr_is_tcp_type()` を拡張
   - → ビルド確認 (既存コードへの影響なし)

2. **ピアテーブル拡張** (`potrPeerTable.c`)
   - `peer_create_tcp()` を実装
   - `peer_free()` を拡張
   - → ビルド確認

3. **per-peer スレッド実装** (`potrTcpPeerThread.c`)
   - recv スレッド関数を実装 (パケット処理は既存ロジックを流用)
   - health スレッド関数を実装
   - → ビルド確認

4. **N:1 accept ループ** (`potrConnectThread.c`)
   - `receiver_accept_n1_loop()` を実装
   - `connect_thread_func()` に分岐を追加
   - → ビルド確認

5. **送信スレッド拡張** (`potrSendThread.c`)
   - `flush_packed_peer()` に TCP N:1 分岐を追加
   - → ビルド確認

6. **API 対応** (`potrOpenService.c`, `potrCloseService.c`, `potrSend.c`, `potrDisconnectPeer.c`)
   - 各 API に TCP N:1 の `case` / 分岐を追加
   - → ビルド確認

---

## 9. 動作確認方法

### ビルド

```bash
make -C app/porter/prod
```

### 既存 1:1 TCP の回帰確認

既存の `POTR_TYPE_TCP` / `POTR_TYPE_TCP_BIDIR` を使用した接続が引き続き正常動作することを確認する。

### N:1 動作確認

```
# RECEIVER 起動 (POTR_TYPE_TCP_N1)
./recv --service-id 1  # 設定ファイルで type=TCP_N1 を指定

# SENDER A 接続 (POTR_TYPE_TCP)
./send --service-id 1 --data "hello from A"

# SENDER B 接続 (別プロセス)
./send --service-id 1 --data "hello from B"

# 期待動作:
# - RECEIVER の callback が peer_id=A で "hello from A" を受信
# - RECEIVER の callback が peer_id=B で "hello from B" を受信
# - A が切断しても B の接続は維持される
# - A 切断時に callback(POTR_EVENT_DISCONNECTED, peer_id=A) が発火
```

### BIDIR_N1 の返信確認

```
# RECEIVER から peer_id=A への返信
potrSend(handle, peer_id_A, "response to A", len, 0);
```

### クリーンシャットダウン確認

`potrCloseService()` 実行後にスレッドリーク・メモリリークがないことを `valgrind` または `ThreadSanitizer` で確認する。
