# TCP サーバーのプロセスモデル

TCP ポートを待ち受け、接続ごとに別プロセスで処理を行うサーバーの実装パターンを解説する。

## 概要

ネットワークサーバーが複数のクライアントを同時に処理する方法として、プロセスベースのアプローチがある。各接続を独立したプロセスで処理することで、1つのクライアントの処理が他に影響を与えない堅牢な構造を実現できる。

本ドキュメントでは以下の2つのモデルを Linux と Windows それぞれで実装する。

| モデル | 特徴 |
|--------|------|
| 接続ごとに fork | 接続を受け付けるたびに新しいプロセスを生成 |
| プリフォーク | 事前に N 個のワーカープロセスを起動しておく |

## コマンドラインインターフェース

```
tcpServer [--mode fork|prefork] [--port <num>] [--workers <num>] [--conns-per-worker <num>]
```

| 引数 | デフォルト | 説明 |
|------|-----------|------|
| `--mode` | `prefork` | `fork` = 接続ごと生成、`prefork` = 事前生成 |
| `--port` | `8080` | 待ち受けポート番号 |
| `--workers` | `4` | prefork 時のワーカー数 |
| `--conns-per-worker` | `1` | prefork 時の 1 ワーカーあたり同時接続数。1 の場合は逐次処理、2 以上の場合はイベント駆動型の多重処理 |

## ファイル構成

```text
tcpServer/
├── tcpServer.h             # 共通定数・型定義・プロトタイプ
├── tcpServer_common.c      # 共通変数定義 (g_session_fn)
├── tcpServer.c             # エントリーポイント (handle_client_session / parse_args / main)
├── tcpServer_linux.c       # Linux 実装 (platform_init / run_fork_server / run_prefork_server)
├── tcpServer_windows.c     # Windows 実装 (platform_init / run_fork_server / run_prefork_server)
├── makefile                # makefw テンプレート呼び出し
└── makepart.mk             # Windows リンクライブラリ設定
```

## モデル1: 接続ごとにプロセスを生成 (--mode fork)

### アーキテクチャ

```text
[親プロセス: ポート待ち受け]
         |
    接続が来た
         |
         v
[子プロセスを生成] ──> [子プロセス: 1クライアント担当]
         |                      |
    再び待ち受けへ           切断で終了
```

親プロセスは listen ソケットで待ち受け、`accept()` で接続を受け付けるたびに子プロセスを生成する。子プロセスは1つのクライアントだけを担当し、切断時に終了する。

### Linux 実装

Linux では `fork()` システムコールでプロセスを複製する。

`fork()` を呼び出すと、親プロセスのメモリ空間がコピーされ、ファイルディスクリプタも複製される。親と子で以下のように処理を分ける。

- 親プロセス: `client_fd` を閉じて次の接続を待つ
- 子プロセス: `server_fd` を閉じてクライアント処理に専念

子プロセスが終了するとゾンビプロセスになるため、`SIGCHLD` シグナルハンドラで `waitpid()` を呼び出して回収する。

### Windows 実装

Windows には `fork()` がないため、`CreateProcess()` で自分自身を子プロセスとして再起動する。ソケットハンドルは `--child <handle>` の形式でコマンドライン引数に渡す。

ソケットハンドルを子プロセスで使用するには、以下の2点が必要。

1. `SetHandleInformation()` でハンドルを継承可能にマーク
2. `CreateProcess()` の `bInheritHandles` 引数を `TRUE` に設定

## モデル2: プリフォーク (--mode prefork)

### アーキテクチャ

```text
[親プロセス]
     │
     │ 起動時に N 個の子プロセスを生成
     ▼
┌────┴────┬────────┬────────┐
▼         ▼        ▼        ▼
[子1]    [子2]    [子3]   [子N]
  │        │        │        │
  └────────┴────┬───┴────────┘
               │
         listen ソケットを共有
               │
    各子プロセスが accept() を呼び出し
    カーネルが1つの子だけに接続を渡す
```

起動時にあらかじめワーカープロセスを生成しておき、接続を処理するモデル。ワーカーは処理完了後も終了せず、次の接続を待つ。

全ワーカーが満杯の場合、新しい接続は listen キューに留まり、いずれかのワーカーに空きが生じるまで待機する。

`--conns-per-worker` に 2 以上を指定すると、各ワーカーがイベント駆動型 I/O で複数のコネクションを同時に処理する。最大同時接続数は「ワーカー数 × 1 ワーカーあたり同時接続数」になる。

### Linux 実装

Linux では複数の子プロセスが同じ listen ソケットに対して `accept()` を呼び出すことができる。カーネルが自動的に1つのプロセスだけを選んで接続を渡すため、親プロセスによる明示的な振り分けが不要。

`--conns-per-worker 1` の動作:

1. 親プロセスが listen ソケットを作成
2. `fork()` で N 個のワーカープロセスを生成
3. 各ワーカーは独立して `accept()` を呼び出し
4. 接続が来ると、カーネルが待機中のワーカー1つを起こす
5. ワーカーはクライアント処理後、再び `accept()` で待機

`--conns-per-worker N` (N ≥ 2) の動作:

各ワーカーが `epoll` を使ったシングルスレッドのイベントループで複数のコネクションを同時処理する。

- アクティブ接続数が上限に達すると listen ソケットを `epoll` から除去して accept を一時停止
- 接続が終了して空きが生じると listen ソケットを再登録して accept を再開

親プロセスは `pause()` で待機し、`SIGINT` (Ctrl+C) を受け取ると全ワーカーに `SIGTERM` を送信して終了させる。

### Windows 実装

Windows では `fork()` がないため、親プロセスが `accept()` を行い、空きワーカーにソケットハンドルを渡す方式を採用している。

ワーカーとの通信には名前付きパイプを使用する。ワーカーは `--worker <pipe_name> --conns-per-worker <N>` 引数付きで起動される。

`--conns-per-worker 1` の動作:

1. 親プロセスが名前付きパイプを作成
2. `CreateProcess()` でワーカープロセスを起動
3. ワーカーはパイプに接続して待機
4. 親が `accept()` で接続を受け付け
5. 空きワーカーを探し、パイプ経由でソケットハンドルを送信
6. ワーカーは処理完了後、パイプ経由で完了を通知 (done=1)

`--conns-per-worker N` (N ≥ 2) の動作:

各ワーカーが `PeekNamedPipe()` でパイプを非ブロッキングチェックしながら `select()` でアクティブなソケットを監視するイベントループを使用する。接続が閉じるたびに完了通知を 1 バイト送信する。

各ワーカーに対して監視スレッドが割り当てられ、完了通知を受け取るたびに `conn_count` をデクリメントし、空きが生じたらイベントをシグナル状態にする。`find_available_worker()` は全ワーカーが満杯の場合、`WaitForMultipleObjects()` でいずれかに空きが生じるまで待機する。

## 主要関数一覧

### Linux (tcpServer_linux.c)

| 関数 | 説明 |
|------|------|
| `sigchld_handler()` | ゾンビプロセスの回収 |
| `shutdown_handler()` | running フラグを 0 にセット |
| `create_listen_socket(port)` | bind + listen |
| `worker_loop(server_fd, id, conns_per_worker)` | ワーカーの accept ループ (N=1: 逐次、N>1: epoll) |
| `platform_init(session_fn)` | g_session_fn の設定 |
| `platform_cleanup()` | no-op |
| `dispatch_internal_args(argc, argv)` | 常に 0 を返す (Linux は内部起動なし) |
| `run_fork_server(port)` | accept → fork ループ |
| `run_prefork_server(port, n, conns_per_worker)` | n ワーカー生成 + pause + SIGTERM |

### Windows (tcpServer_windows.c)

| 関数 | 説明 |
|------|------|
| `create_listen_socket(port)` | getaddrinfo + bind + listen |
| `run_as_fork_child(socket)` | --child 時のエコー処理 |
| `worker_loop(pipe_name, conns_per_worker)` | --worker 時のパイプ受信 + エコー処理 (N=1: 逐次、N>1: PeekNamedPipe+select) |
| `worker_monitor_thread(arg)` | 完了通知を受け取り conn_count をデクリメント |
| `find_available_worker(workers, events, n, cpw)` | conn_count < cpw のワーカー探索 |
| `start_prefork_workers(workers, events, n, cpw)` | n ワーカーを `--worker --conns-per-worker` で起動 |
| `platform_init(session_fn)` | WSAStartup + g_session_fn の設定 |
| `platform_cleanup()` | WSACleanup |
| `dispatch_internal_args(argc, argv)` | --child/--worker を検出して処理 |
| `run_fork_server(port)` | accept + CreateProcess(`--child`) ループ |
| `run_prefork_server(port, n, conns_per_worker)` | listen_socket 確保 + workers 起動 + 振り分けループ |

## stdin, stdout, stderr の継承

両モデルとも、子プロセスは親の標準入出力を継承する。子プロセスの `printf()` は親と同じコンソールに出力される。

### Linux

`fork()` は親のファイルディスクリプタをすべて複製する。stdin (fd 0), stdout (fd 1), stderr (fd 2) も自動的に継承される。

### Windows

`CreateProcess()` でコンソールアプリケーションを起動する場合、子プロセスはデフォルトで親のコンソールを共有する。`STARTUPINFO` の `dwFlags` に `STARTF_USESTDHANDLES` を設定していなければ、標準ハンドルも自動的に継承される。

## 2つのモデルの比較

| 項目 | 接続ごとに fork | プリフォーク |
|------|-----------------|--------------|
| プロセス生成コスト | 接続のたびに発生 | 起動時のみ |
| 最大同時接続数 | 事実上無制限 (リソース次第) | N 個に固定 |
| メモリ使用量 | 接続数に比例 | 一定 |
| 実装の複雑さ | シンプル | やや複雑 |
| 過負荷時の挙動 | 新規 fork が遅くなる | 新規接続がキューで待機 |

## ビルド方法

```bash
make -C app/porter/prod/src/tcpServer
```

## 実行とテスト

```bash
# prefork モード (デフォルト)
app/porter/prod/bin/tcpServer

# fork モード
app/porter/prod/bin/tcpServer --mode fork

# ポート・ワーカー数変更
app/porter/prod/bin/tcpServer --port 9090 --workers 8

# イベント駆動型: 4ワーカー × 8同時接続 = 最大32同時接続
app/porter/prod/bin/tcpServer --mode prefork --workers 4 --conns-per-worker 8

# 別ターミナルから接続
nc localhost 8080
```

`--conns-per-worker 1` では、`--workers` を超える同時接続を試みると、既存の接続が切断されるまで待たされることを確認できる。

`--conns-per-worker N` (N ≥ 2) では、1 ワーカーが N 本の接続を同時に保持できる。ワーカー数 × N を超える接続が来ると、listen キューに留まる。

Ctrl+C で終了すると、prefork モードでは全ワーカーに SIGTERM が送信され、順次終了する。
