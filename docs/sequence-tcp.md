# TCP 通信のシーケンス

[通信シーケンスの一覧](sequence.md) へ戻ります。

## 接続管理と接続成立前処理の境界

`thread_connect_prepare.c` は、TCP の接続待機、最初のパケットの読み取り、接続元の照合とセッションの比較を担当します。  
受信側の管理ループは、先読みと既存のパケット解析の後に `session_establish_mutex`、`tcp_recv_mutex` の順でロックを取得します。  
サービス ID と認証を検査してからセッションを比較し、比較処理では共有状態を変更しません。

`potr_connect_thread.c` は、採用する接続ソケットを共有状態へ登録し、通知と接続後のスレッド管理を担当します。  
接続ヘルパーが生成した新規ソケットは、接続失敗時にはヘルパーが閉じ、成功時には呼び出し元へ所有権を渡します。  
accept 後にフィルター・先読み・解析・認証などで拒否した接続は、管理ループが閉じます。

## TCP サービス開始 (SENDER)

`potr_service_open()` を TCP SENDER として呼び出したときの内部処理です。  
`potr_service_open()` は即座に復帰し、接続確立は connect スレッドが非同期に行います。

```mermaid
sequenceDiagram
    autonumber
    participant APP as アプリ
    participant OPEN as potr_service_open

    APP->>+OPEN: potr_service_open(&global, &service, POTR_ROLE_SENDER, cb, &handle)
    OPEN->>OPEN: 構成構造体の検証・セッション識別子生成
    OPEN->>OPEN: 送信キュー / ウィンドウ初期化
    OPEN->>OPEN: tcp_state_mutex / tcp_state_cv 初期化

    create participant CT as connect スレッド
    OPEN->>CT: connect スレッド起動
    OPEN-->>-APP: POTR_OK, *handle

    activate CT
    CT->>CT: connect(dst_addr, dst_port)<br>(接続確立まで connect_timeout_ms 待機)
    create participant ST as 送信スレッド
    CT->>ST: 送信スレッド起動
    create participant RT as 受信スレッド
    CT->>RT: 受信スレッド起動
    create participant HT as ヘルス チェック スレッド
    CT->>HT: ヘルス チェック スレッド起動

    Note over CT: recv スレッドが切断を検知するまで待機
```

Figure: TCP サービス開始 (SENDER)

## TCP サービス開始 (RECEIVER)

`potr_service_open()` を TCP RECEIVER として呼び出したときの内部処理です。

```mermaid
sequenceDiagram
    autonumber
    participant APP as アプリ
    participant OPEN as potr_service_open

    APP->>+OPEN: potr_service_open(&global, &service, POTR_ROLE_RECEIVER, callback, &handle)
    OPEN->>OPEN: 構成構造体の検証
    OPEN->>OPEN: TCP listen ソケット作成<br>bind(dst_addr, dst_port) → listen()

    create participant AT as accept スレッド
    OPEN->>AT: accept スレッド起動
    OPEN-->>-APP: POTR_OK, *handle

    activate AT
    Note over AT: accept() 待機中
    create participant RT as recv スレッド
    AT->>RT: 接続確立後に recv スレッド起動

    activate RT
    Note over RT: DATA / PING を待機するポーリングループ
```

Figure: TCP サービス開始 (RECEIVER)

## TCP 正常接続・通信・切断

TCP SENDER / RECEIVER 間の正常な接続確立・データ送信・切断シーケンスです。

```mermaid
sequenceDiagram
    autonumber
    participant S as SENDER<br>(クライアント)
    participant R as RECEIVER<br>(サーバー)

    rect rgb(240, 240, 240)
        Note over S,R: サービス開始
    end
    Note over R: potr_service_open()<br>bind() → listen()
    Note over S: potr_service_open()<br>connect() 開始
    S->>R: TCP 3way handshake
    Note over S: 接続確立

    rect rgb(240, 240, 240)
        Note over S,R: データ送信
    end
    S->>R: DATA (seq_num=0, 最初のパケット)
    Note over R: peer_session_known = false<br>→ セッション採用<br>POTR_EVENT_CONNECTED 発火

    S->>R: DATA (seq_num=1)
    S->>R: DATA (seq_num=2)
    Note over R: POTR_EVENT_DATA × 3

    rect rgb(240, 240, 240)
        Note over S,R: ヘルス チェック
    end
    S->>R: 定周期 PING (seq_num=3, payload=UNDEFINED)
    Note over R: path_ping_state を NORMAL に更新
    R->>S: 割り込み PING (seq_num=M,<br>payload に NORMAL を含む)
    Note over S: 初回の認証済み PING を受信<br>→ CONNECTED / alive 確認

    rect rgb(240, 240, 240)
        Note over S,R: 正常終了
    end
    Note over S: potr_service_close()<br>close_requested=1<br>send_queue drain 待機
    S->>R: FIN[target_valid, ack_num=3]
    Note over R: recv_window.next_seq が 3 に追い付くまで<br>必要なら pending_fin
    Note over R: 最後の DATA callback 完了
    R->>S: FIN_ACK[ack_num=3]
    Note over R: POTR_EVENT_DISCONNECTED 発火<br>current session をリセット
    Note over S: FIN_ACK 受信後に socket teardown
```

Figure: TCP 正常シーケンス

## TCP SENDER 再起動・自動再接続

SENDER プロセスが再起動した場合に自動再接続が行われるシーケンスです。

```mermaid
sequenceDiagram
    autonumber
    participant S as SENDER
    participant R as RECEIVER

    Note over S,R: 通信中（セッション A）

    rect rgb(240, 240, 240)
        Note over S,R: SENDER プロセス再起動
    end
    S-->>R: TCP 接続断
    Note over R: TCP 切断検知<br>POTR_EVENT_DISCONNECTED 発火<br>peer_session_known = false

    Note over S: potr_service_open()<br>新セッション識別子を生成
    S->>R: TCP 3way handshake（再接続）
    S->>R: DATA (セッション B の最初のパケット)

    Note over R: セッション B を採用<br>POTR_EVENT_CONNECTED 発火
```

Figure: TCP SENDER 再起動・自動再接続

## TCP PING 応答タイムアウト

RECEIVER のアプリケーション層がハングした場合に SENDER が切断を検知するシーケンスです。  
TCP 接続は OS レベルで生存していてもアプリ層の PING 応答が途絶えることで検知します。

```mermaid
sequenceDiagram
    autonumber
    participant S as SENDER
    participant R as RECEIVER<br>(応答なし)

    Note over S,R: 通信中

    S->>R: PING (ack_num=0, seq_num=N)
    Note over R: アプリケーション層がハング<br>(TCP 接続は維持されているが PING 応答が返却されない)
    Note over S: tcp_health_timeout_ms 経過<br>PING 応答（ack_num=N）未受信
    Note over S: POTR_EVENT_DISCONNECTED 発火<br>TCP 接続を切断

    Note over S: reconnect_interval_ms 待機後<br>connect() 再試行
```

Figure: TCP PING 応答タイムアウト

## TCP RECEIVER 側 PING タイムアウト

SENDER のアプリケーション層がハングして PING 送信が停止した場合に RECEIVER が切断を検知するシーケンスです。  
TCP 接続は OS レベルで生存していても、PING 要求が到着しなくなることで RECEIVER が検知します。

```mermaid
sequenceDiagram
    autonumber
    participant S as SENDER<br>(PING 送信停止)
    participant R as RECEIVER

    Note over S,R: 通信中

    S->>R: 定周期 PING (seq_num=N, payload=UNDEFINED)
    R->>S: 割り込み PING (seq_num=M,<br>payload に NORMAL を含む)
    Note over S: アプリケーション層がハング<br>(TCP 接続は維持されているが PING を送信できない)

    Note over R: tcp_health_timeout_ms 経過<br>PING 要求（ack_num=0）未着信
    Note over R: POTR_EVENT_DISCONNECTED 発火<br>TCP 接続を切断

    Note over R: accept スレッドへ戻り<br>次の接続を待機
```

Figure: TCP RECEIVER 側 PING タイムアウト

## TCP マルチパス

### TCP マルチパス接続確立

path 数 = 2 の例。RECEIVER が 2 つの listen ソケットを用意し、SENDER が各 path に接続します。  
最初の 1 本が接続した時点で `POTR_EVENT_CONNECTED` が発火します。

```mermaid
sequenceDiagram
    autonumber
    participant SC0 as SENDER<br>(connect スレッド #35;0)
    participant SC1 as SENDER<br>(connect スレッド #35;1)
    participant RA0 as RECEIVER<br>(accept スレッド #35;0)
    participant RA1 as RECEIVER<br>(accept スレッド #35;1)
    participant RAPP as アプリ<br>(受信側)

    Note over RA0: bind(dst_addr[0]) → listen()
    Note over RA1: bind(dst_addr[1]) → listen()

    SC0->>RA0: TCP 3way handshake (path 0)
    RA0->>RA0: tcp_conn_fd[0] = accept()<br>tcp_active_paths 0→1
    RA0->>RAPP: callback(POTR_EVENT_CONNECTED)
    Note over RA0: recv スレッド #35;0 起動

    SC1->>RA1: TCP 3way handshake (path 1)
    RA1->>RA1: tcp_conn_fd[1] = accept()<br>tcp_active_paths 1→2
    Note over RA1: POTR_EVENT_CONNECTED は発火しない
    Note over RA1: recv スレッド #35;1 起動

    Note over SC0,RA1: 2 path で接続確立。同一 seq_num のパケットが両 path から到着する
```

Figure: TCP マルチパス接続確立 (path 数 = 2)

### 部分切断時の継続

1 本の path が切断しても残りの path で通信を継続します。  
`POTR_EVENT_DISCONNECTED` は発火しません。

```mermaid
sequenceDiagram
    autonumber
    participant SC0 as SENDER<br>(connect スレッド #35;0)
    participant ST as SENDER<br>(送信スレッド)
    participant RA0 as RECEIVER<br>(accept スレッド #35;0)
    participant RR1 as RECEIVER<br>(recv スレッド #35;1)
    participant RAPP as アプリ<br>(受信側)

    Note over SC0,RR1: path 0 / path 1 で通信中

    Note over SC0,RA0: path 0 の TCP 接続断（ネットワーク障害など）
    RA0->>RA0: tcp_active_paths 2→1
    Note over RA0: POTR_EVENT_DISCONNECTED は発火しない<br>(残り 1 path が存在するため)
    Note over RA0: session_id / session_tv_* を保持

    Note over ST: path 0 をスキップして path 1 のみ送信継続

    SC0->>SC0: reconnect_interval_ms 待機後に<br>再 connect()

    SC0->>RA0: TCP 3way handshake (path 0 再接続)
    RA0->>RA0: session triplet 照合 → 既存セッションに合流<br>tcp_active_paths 1→2
    Note over RA0: POTR_EVENT_CONNECTED は再発火しない

    Note over SC0,RR1: 2 path で通信再開
```

Figure: TCP 部分切断時の継続 (path 0 切断)

### 全 path 切断

全 path が切断した時点で `POTR_EVENT_DISCONNECTED` が発火します。

```mermaid
sequenceDiagram
    autonumber
    participant RA0 as RECEIVER<br>(accept スレッド #35;0)
    participant RA1 as RECEIVER<br>(accept スレッド #35;1)
    participant RAPP as アプリ<br>(受信側)

    Note over RA0,RA1: path 0 / path 1 で通信中

    Note over RA0: path 0 の TCP 接続断
    RA0->>RA0: tcp_active_paths 2→1
    Note over RA0: POTR_EVENT_DISCONNECTED は発火しない

    Note over RA1: path 1 の TCP 接続断
    RA1->>RA1: tcp_active_paths 1→0
    RA1->>RAPP: callback(POTR_EVENT_DISCONNECTED)
    Note over RA1: session_tv_* をリセット<br>次の接続は新セッションとして扱う

    Note over RA0,RA1: 各 accept スレッドが次の接続を待機
```

Figure: TCP 全 path 切断
