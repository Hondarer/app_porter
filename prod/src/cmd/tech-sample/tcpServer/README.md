---
short-title: "tcpServer"
---

# TCP サーバーのプロセス モデル

TCP ポートを待ち受け、接続ごとに別プロセスで処理を行うサーバーの実装パターンを解説します。

## 概要

ネットワーク サーバーが複数のクライアントを同時に処理する方法として、プロセス ベースのアプローチがあります。各接続を独立したプロセスで処理することで、1 つのクライアントの処理が他に影響を与えない堅牢な構造を実現できます。

本ドキュメントでは以下の 2 つのモデルを Linux と Windows それぞれで実装します。

| モデル | 特徴 |
|--------|------|
| 接続ごとに fork | 接続を受け付けるたびに新しいプロセスを生成 |
| プリフォーク | 事前に N 個のワーカー プロセスを起動しておく |

Table: TCP サーバーのプロセス モデルと特徴

## コマンド ライン インターフェイス

```
tcpServer [--mode fork|prefork] [--port <num>] [--workers <num>] [--conns-per-worker <num>]
```

| 引数 | デフォルト | 説明 |
|------|-----------|------|
| `--mode` | `prefork` | `fork` = 接続ごと生成、`prefork` = 事前生成 |
| `--port` | `8080` | 待ち受けポート番号 |
| `--workers` | `4` | prefork 時のワーカー数 |
| `--conns-per-worker` | `1` | prefork 時の 1 ワーカーあたり同時接続数。1 の場合は逐次処理、2 以上の場合はイベント駆動型の多重処理 |

Table: tcpServer のコマンド ライン引数一覧

## ファイル構成

```text
tcpServer/
+-- tcpServer.h             # 共通定数・型定義・プロトタイプ
+-- tcpServer_common.c      # 共通変数定義 (g_session_fn)
+-- tcpServer.c             # エントリ ポイント (handle_client_session / main)
+-- tcpServer_linux.c       # Linux 実装 (platform_init / run_fork_server / run_prefork_server)
+-- tcpServer_windows.c     # Windows 実装 (platform_init / run_fork_server / run_prefork_server)
+-- makefile                # makefw テンプレート呼び出し
+-- makepart.mk             # Windows リンクライブラリ設定
```

## モデル 1: 接続ごとにプロセスを生成 (--mode fork)

### アーキテクチャー

```text
[親プロセス: ポート待ち受け]
         |
     接続到着
         |
         v
[子プロセスを生成] --> [子プロセス: 1クライアント担当]
         |                      |
     再び待ち受けへ           切断で終了
```

親プロセスは listen ソケットで待ち受け、`accept()` で接続を受け付けるたびに子プロセスを生成します。子プロセスは 1 つのクライアントだけを担当し、切断時に終了します。

### Linux 実装

Linux では `fork()` システム コールでプロセスを複製します。

`fork()` を呼び出すと、親プロセスのメモリ空間がコピーされ、ファイル記述子も複製されます。親と子で以下のように処理を分けます。

- 親プロセス: `client_fd` を閉じて次の接続を待つ
- 子プロセス: `server_fd` を閉じてクライアント処理に専念

子プロセスが終了するとゾンビ プロセスになるため、`SIGCHLD` シグナル ハンドラーで `waitpid()` を呼び出して回収します。

### Windows 実装

Windows には `fork()` がないため、`CreateProcess()` で自分自身を子プロセスとして再起動します。ソケット ハンドルは `--child <handle>` の形式でコマンド ライン引数に渡します。

ソケット ハンドルを子プロセスで使用するには、以下の 2 点が必要です。

1. `SetHandleInformation()` でハンドルを継承可能にマーク
2. `CreateProcess()` の `bInheritHandles` 引数を `TRUE` に設定

## モデル 2: プリフォーク (--mode prefork)

### アーキテクチャー

```text
[親プロセス]
     |
     | 起動時に N 個の子プロセスを生成
     v
  +--+-----+--------+--------+
  v        v        v        v
[子1]    [子2]    [子3]    [子N]
  |        |        |        |
  +--------+----+---+--------+
               |
         listen ソケットを共有
               |
    各子プロセスが accept() を呼び出し
    カーネルが1つの子だけに接続を割り当て
```

起動時にあらかじめワーカー プロセスを生成しておき、接続を処理するモデルです。ワーカーは処理完了後も終了せず、次の接続を待機します。

全ワーカーが満杯の場合、新しい接続は listen キューに留まり、いずれかのワーカーに空きが生じるまで待機します。

`--conns-per-worker` に 2 以上を指定すると、各ワーカーがイベント駆動型 I/O で複数のコネクションを同時に処理します。最大同時接続数は「ワーカー数 × 1 ワーカーあたり同時接続数」になります。

### Linux 実装

Linux では複数の子プロセスが同じ listen ソケットに対して `accept()` を呼び出すことができます。カーネルが自動的に 1 つのプロセスだけを選んで接続を渡すため、親プロセスによる明示的な振り分けは不要です。

`--conns-per-worker 1` の動作:

1. 親プロセスが listen ソケットを作成
2. `fork()` で N 個のワーカー プロセスを生成
3. 各ワーカーは独立して `accept()` を呼び出し
4. 接続要求が到着すると、カーネルが待機中のワーカー 1 つを起床
5. ワーカーはクライアント処理後、再び `accept()` で待機

`--conns-per-worker N` (N ≥ 2) の動作:

各ワーカーが `epoll` を使ったシングルスレッドのイベント ループで複数のコネクションを同時処理します。

- アクティブ接続数が上限に達すると listen ソケットを `epoll` から除去して accept を一時停止
- 接続が終了して空きが生じると listen ソケットを再登録して accept を再開

親プロセスは `pause()` で待機し、`SIGINT` (Ctrl+C) を受け取ると全ワーカーに `SIGTERM` を送信して終了させます。

### Windows 実装

Windows では `fork()` がないため、親プロセスが `accept()` を行い、空きワーカーへ `WSADuplicateSocketW()` で複製したソケット情報を引き渡す方式を採用しています。

ワーカーとの通信には名前付きパイプを使用します。  
ワーカーは `--worker <pipe_name> --conns-per-worker <N>` 引数付きで起動されます。

`--conns-per-worker 1` の動作:

1. 親プロセスがワーカーごとの名前付きパイプと監視スレッドを作成
2. `CreateProcess()` でワーカー プロセスを起動し、ワーカーはパイプへ接続して待機
3. 親が `accept()` でクライアント接続を受け付け
4. 稼働中の空きワーカーの接続枠を 1 つ予約 (`pending = 1`) し、そのワーカーの PID を指定した `WSADuplicateSocketW()` で `WSAPROTOCOL_INFOW` を作成してパイプへ送信
5. ワーカーが `WSASocketW()` で共有ソケットを復元し、成否を含む型付きの受領通知 (`WORKER_ACK`) を返信 (返信失敗時は復元したソケットをワーカー側でクローズ)
6. 親は受領通知またはワーカー終了を確認してから親側のソケット参照を閉じ、ワーカーはセッション完了後に型付きの完了通知 (`WORKER_DONE`) を返信

`--conns-per-worker N` (N ≥ 2) の動作:

各ワーカーが `PeekNamedPipe()` でパイプを非ブロッキングで確認しながら、`select()` でアクティブな複数ソケットを監視するイベント ループを実行します。  
ソケットの復元時に型付きの `WORKER_ACK`、各接続の終了時に型付きの `WORKER_DONE` を親へ送信します。

親側パイプの読み手はワーカーごとの監視スレッド (`worker_monitor_thread`) だけに限定し、接続を割り当てるスレッドはパイプを直接読み取りません。  
監視スレッドは型付き応答の `WORKER_ACK` と `WORKER_DONE` を振り分け、受領済み接続数 (`conn_count`)、受領待ちの予約数 (`pending`)、稼働状態 (`alive`)、および空き通知イベントを同じロック (`state_lock`) の下で更新します。  
`WORKER_ACK` の成功時だけ予約を受領済み接続数へ計上し、複製・送信・復元の失敗時は予約を取り消して親側のソケット参照を閉じます。

ワーカー プロセスの終了やパイプの切断・転送失敗を検出した場合は、そのワーカーを稼働停止 (`alive = 0`) として以降の割り当て対象から除外します。  
`find_available_worker()` は稼働中かつ `conn_count + pending < conns_per_worker` のワーカーを探して予約を行い、稼働中の全ワーカーが満杯の場合は `WaitForMultipleObjects()` で空きが生じるまで待機します。  
稼働中のワーカーがなくなった場合は `-1` を返し、親は受け付けたソケットを閉じます。

## 主要関数一覧

### Linux (tcpServer_linux.c)

| 関数 | 説明 |
|------|------|
| `sigchld_handler()` | ゾンビ プロセスの回収 |
| `shutdown_handler()` | running フラグを 0 にセット |
| `create_listen_socket(port)` | bind + listen |
| `worker_loop(server_fd, id, conns_per_worker)` | ワーカーの accept ループ (N=1: 逐次、N>1: epoll) |
| `platform_init(session_fn)` | g_session_fn の設定 |
| `platform_cleanup()` | no-op |
| `dispatch_internal_args(argc, argv)` | 常に 0 を返す (Linux は内部起動なし) |
| `run_fork_server(port)` | accept → fork ループ |
| `run_prefork_server(port, n, conns_per_worker)` | n ワーカー生成 + pause + SIGTERM |

Table: Linux 版 tcpServer の主要関数一覧

### Windows (tcpServer_windows.c)

| 関数 | 説明 |
|------|------|
| `create_listen_socket(port)` | getaddrinfo + bind + listen |
| `run_as_fork_child(socket)` | --child 時のエコー処理 |
| `worker_loop(pipe_name, conns_per_worker)` | --worker 時のパイプ受信 (`WSASocketW` 復元 + 型付き ACK/DONE 送信) とエコー処理 (N=1: 逐次、N>1: PeekNamedPipe+select) |
| `worker_monitor_thread(arg)` | 親側パイプを単独で読み取り、型付き ACK/DONE の振り分けと予約数・接続数・稼働状態の同期更新を行う |
| `find_available_worker(workers, events, n, cpw)` | 稼働中かつ `conn_count + pending < cpw` のワーカーを探索して接続枠を予約 (満杯時は待機、生存ワーカー不在時は -1) |
| `start_prefork_workers(workers, events, n, cpw)` | n ワーカーを `--worker --conns-per-worker` で起動 |
| `platform_init(session_fn)` | WSAStartup + g_session_fn の設定 |
| `platform_cleanup()` | WSACleanup |
| `dispatch_internal_args(argc, argv)` | --child/--worker を検出して処理 |
| `run_fork_server(port)` | accept + CreateProcess(`--child`) ループ |
| `run_prefork_server(port, n, conns_per_worker)` | listen_socket 確保 + workers 起動 + `WSADuplicateSocketW` による引き渡し・ACK 確認後の親側参照クローズ |

Table: Windows 版 tcpServer の主要関数一覧

## stdin, stdout, stderr の継承

両モデルとも、子プロセスは親の標準入出力を継承します。子プロセスの `printf()` は親と同じコンソールに出力されます。

### Linux

`fork()` は親のファイル記述子をすべて複製します。stdin (fd 0), stdout (fd 1), stderr (fd 2) も自動的に継承されます。

### Windows

`CreateProcess()` でコンソール アプリケーションを起動する場合、子プロセスはデフォルトで親のコンソールを共有します。`STARTUPINFO` の `dwFlags` に `STARTF_USESTDHANDLES` を設定していなければ、標準ハンドルも自動的に継承されます。

## 2 つのモデルの比較

| 項目 | 接続ごとに fork | プリフォーク |
|------|-----------------|--------------|
| プロセス生成コスト | 接続のたびに発生 | 起動時のみ |
| 最大同時接続数 | 事実上無制限 (リソース次第) | N 個に固定 |
| メモリ使用量 | 接続数に比例 | 一定 |
| 実装の複雑さ | シンプル | やや複雑 |
| 過負荷時の挙動 | 新規 fork が遅延 | 新規接続がキューで待機 |

Table: 接続ごと fork とプリフォーク モデルの比較

## ビルド方法

```bash
make -C app/porter/prod/src/tcpServer
```

## 実行とテスト

```bash
# prefork モード (デフォルト)
app/porter/prod/cbin/tcpServer

# fork モード
app/porter/prod/cbin/tcpServer --mode fork

# ポート・ワーカー数変更
app/porter/prod/cbin/tcpServer --port 9090 --workers 8

# イベント駆動型: 4 ワーカー × 8 同時接続 = 最大 32 同時接続
app/porter/prod/cbin/tcpServer --mode prefork --workers 4 --conns-per-worker 8

# 別ターミナルから接続
nc localhost 8080
```

`--conns-per-worker 1` では、`--workers` を超える同時接続を試みると、既存の接続が切断されるまで待たされることを確認できます。

`--conns-per-worker N` (N ≥ 2) では、1 ワーカーが N 本の接続を同時に保持できます。ワーカー数 × N を超える接続が到着すると、listen キューに留まります。

Ctrl+C で終了すると、prefork モードでは全ワーカーに SIGTERM が送信され、順次終了します。

### Windows のソケット受け渡しテスト

`test/src/integration/tcpServerPreforkTest` の `make test` は、逐次処理と多重処理の両方で、全接続枠の使用と完了後の再割り当てを繰り返します。  
サーバーの実行ファイルは既定で `prod/cbin/tcpServer.exe` を使用し、別の配置では環境変数 `POTR_TCPSERVER_TEST_EXE` で指定します。  
Linux ではこのテストをスキップします。
