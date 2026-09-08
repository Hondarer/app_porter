# TCP 通信のシーケンス

[通信シーケンスの一覧](sequence.md) へ戻ります。

## TCP サービス開始 (SENDER)

`potr_service_open()` を TCP SENDER として呼び出したときの内部処理です。  
`potr_service_open()` はすぐに返り、接続確立は connect スレッドが非同期に行います。

```plantuml
@startuml TCP サービス開始 (SENDER)
caption TCP サービス開始 (SENDER)

participant "アプリ" as APP
participant "potr_service_open" as OPEN
participant "connect スレッド" as CT
participant "送信スレッド" as ST
participant "受信スレッド" as RT
participant "ヘルスチェックスレッド" as HT

APP -> OPEN: potr_service_open(&global, &service, POTR_ROLE_SENDER, cb, &handle)
activate OPEN
OPEN -> OPEN: 設定構造体の検証・セッション識別子生成
OPEN -> OPEN: 送信キュー / ウィンドウ初期化
OPEN -> OPEN: tcp_state_mutex / tcp_state_cv 初期化

OPEN -> CT**: connect スレッド起動
OPEN --> APP: POTR_OK, *handle
deactivate OPEN

activate CT
CT -> CT: connect(dst_addr, dst_port)\n（接続確立まで connect_timeout_ms 待機）
CT -> ST**: 送信スレッド起動
CT -> RT**: 受信スレッド起動
CT -> HT**: ヘルスチェックスレッド起動

note over CT: recv スレッドが切断を検知するまで待機

@enduml
```

## TCP サービス開始 (RECEIVER)

`potr_service_open()` を TCP RECEIVER として呼び出したときの内部処理です。

```plantuml
@startuml TCP サービス開始 (RECEIVER)
caption TCP サービス開始 (RECEIVER)

participant "アプリ" as APP
participant "potr_service_open" as OPEN
participant "accept スレッド" as AT
participant "recv スレッド" as RT

APP -> OPEN: potr_service_open(&global, &service, POTR_ROLE_RECEIVER, callback, &handle)
activate OPEN
OPEN -> OPEN: 設定構造体の検証
OPEN -> OPEN: TCP listen ソケット作成\nbind(dst_addr, dst_port) → listen()

OPEN -> AT**: accept スレッド起動
OPEN --> APP: POTR_OK, *handle
deactivate OPEN

activate AT
note over AT: accept() 待機中
AT -> RT**: 接続確立後に recv スレッド起動

activate RT
note over RT: DATA / PING を待機するポーリングループ

@enduml
```

## TCP 正常接続・通信・切断

TCP SENDER / RECEIVER 間の正常な接続確立・データ送信・切断シーケンスです。

```plantuml
@startuml TCP 正常シーケンス
caption TCP 正常シーケンス

participant "SENDER\n(クライアント)" as S
participant "RECEIVER\n(サーバー)" as R

== サービス開始 ==

note over R: potr_service_open()\nbind() → listen()
note over S: potr_service_open()\nconnect() 開始
S -> R: TCP 3way handshake
note over S: 接続確立

== データ送信 ==

S -> R: DATA (seq_num=0, 最初のパケット)
note over R: peer_session_known = false\n→ セッション採用\nPOTR_EVENT_CONNECTED 発火

S -> R: DATA (seq_num=1)
S -> R: DATA (seq_num=2)
note over R: POTR_EVENT_DATA × 3

== ヘルスチェック ==

S -> R: 定周期 PING (seq_num=3, payload=UNDEFINED)
note over R: path_ping_state を NORMAL に更新
R -> S: 割り込み PING (seq_num=M,\npayload に NORMAL を含む)
note over S: 初回の認証済み PING を受信\n→ CONNECTED / alive 確認

== 正常終了 ==

note over S: potr_service_close()\nclose_requested=1\nsend_queue drain 待機
S -> R: FIN[target_valid, ack_num=3]
note over R: recv_window.next_seq が 3 に追い付くまで\n必要なら pending_fin
note over R: 最後の DATA callback 完了
R -> S: FIN_ACK[ack_num=3]
note over R: POTR_EVENT_DISCONNECTED 発火\ncurrent session をリセット
note over S: FIN_ACK 受信後に socket teardown

@enduml
```

## TCP SENDER 再起動・自動再接続

SENDER プロセスが再起動した場合に自動再接続が行われるシーケンスです。

```plantuml
@startuml TCP 再接続シーケンス
caption TCP SENDER 再起動・自動再接続

participant "SENDER" as S
participant "RECEIVER" as R

note over S,R: 通信中（セッション A）

== SENDER プロセス再起動 ==

S -[#red]-> R: TCP 接続断
note over R: TCP 切断検知\nPOTR_EVENT_DISCONNECTED 発火\npeer_session_known = false

note over S: potr_service_open()\n新セッション識別子を生成
S -> R: TCP 3way handshake（再接続）
S -> R: DATA (セッション B の最初のパケット)

note over R: セッション B を採用\nPOTR_EVENT_CONNECTED 発火

@enduml
```

## TCP PING 応答タイムアウト

RECEIVER のアプリケーション層がハングした場合に SENDER が切断を検知するシーケンスです。  
TCP 接続は OS レベルで生存していてもアプリ層の PING 応答が途絶えることで検知します。

```plantuml
@startuml TCP PING 応答タイムアウト
caption TCP PING 応答タイムアウト

participant "SENDER" as S
participant "RECEIVER\n(応答なし)" as R

note over S,R: 通信中

S -> R: PING (ack_num=0, seq_num=N)
note over R: アプリケーション層がハング\n（TCP 接続は生きているが PING 応答が返らない）
note over S: tcp_health_timeout_ms 経過\nPING 応答（ack_num=N）未受信
note over S: POTR_EVENT_DISCONNECTED 発火\nTCP 接続を切断

note over S: reconnect_interval_ms 待機後\nconnect() 再試行

@enduml
```

## TCP RECEIVER 側 PING タイムアウト

SENDER のアプリケーション層がハングして PING 送信が停止した場合に RECEIVER が切断を検知するシーケンスです。  
TCP 接続は OS レベルで生存していても、PING 要求が届かなくなることで RECEIVER が検知します。

```plantuml
@startuml TCP RECEIVER PING タイムアウト
caption TCP RECEIVER 側 PING タイムアウト

participant "SENDER\n(PING 送信停止)" as S
participant "RECEIVER" as R

note over S,R: 通信中

S -> R: 定周期 PING (seq_num=N, payload=UNDEFINED)
R -> S: 割り込み PING (seq_num=M,\npayload に NORMAL を含む)
note over S: アプリケーション層がハング\n（TCP 接続は生きているが PING を送信できない）

note over R: tcp_health_timeout_ms 経過\nPING 要求（ack_num=0）未着信
note over R: POTR_EVENT_DISCONNECTED 発火\nTCP 接続を切断

note over R: accept スレッドへ戻り\n次の接続を待機

@enduml
```

## TCP マルチパス

### TCP マルチパス接続確立

path 数 = 2 の例。RECEIVER が 2 つの listen ソケットを用意し、SENDER が各 path に接続します。  
最初の 1 本が接続した時点で `POTR_EVENT_CONNECTED` が発火します。

```plantuml
@startuml TCP マルチパス接続確立
caption TCP マルチパス接続確立（path 数 = 2）

participant "SENDER\n(connect スレッド #0)" as SC0
participant "SENDER\n(connect スレッド #1)" as SC1
participant "RECEIVER\n(accept スレッド #0)" as RA0
participant "RECEIVER\n(accept スレッド #1)" as RA1
participant "アプリ\n(受信側)" as RAPP

note over RA0: bind(dst_addr[0]) → listen()
note over RA1: bind(dst_addr[1]) → listen()

SC0 -> RA0: TCP 3way handshake (path 0)
RA0 -> RA0: tcp_conn_fd[0] = accept()\ntcp_active_paths 0→1
RA0 -> RAPP: callback(POTR_EVENT_CONNECTED)
note over RA0: recv スレッド #0 起動

SC1 -> RA1: TCP 3way handshake (path 1)
RA1 -> RA1: tcp_conn_fd[1] = accept()\ntcp_active_paths 1→2
note over RA1: POTR_EVENT_CONNECTED は発火しない
note over RA1: recv スレッド #1 起動

note over SC0,RA1: 2 path で接続確立。同一 seq_num のパケットが両 path から届く
@enduml
```

### 部分切断時の継続

1 本の path が切断しても残りの path で通信を継続します。  
`POTR_EVENT_DISCONNECTED` は発火しません。

```plantuml
@startuml TCP 部分切断時の継続
caption TCP 部分切断時の継続（path 0 切断）

participant "SENDER\n(connect スレッド #0)" as SC0
participant "SENDER\n(送信スレッド)" as ST
participant "RECEIVER\n(accept スレッド #0)" as RA0
participant "RECEIVER\n(recv スレッド #1)" as RR1
participant "アプリ\n(受信側)" as RAPP

note over SC0,RR1: path 0 / path 1 で通信中

note over SC0,RA0: path 0 の TCP 接続断（ネットワーク障害など）
RA0 -> RA0: tcp_active_paths 2→1
note over RA0: POTR_EVENT_DISCONNECTED は発火しない\n(残り 1 path が存在するため)
note over RA0: session_id / session_tv_* を保持

note over ST: path 0 をスキップして path 1 のみ送信継続

SC0 -> SC0: reconnect_interval_ms 待機後に\n再 connect()

SC0 -> RA0: TCP 3way handshake (path 0 再接続)
RA0 -> RA0: session triplet 照合 → 既存セッションに合流\ntcp_active_paths 1→2
note over RA0: POTR_EVENT_CONNECTED は再発火しない

note over SC0,RR1: 2 path で通信再開
@enduml
```

### 全 path 切断

全 path が切断した時点で `POTR_EVENT_DISCONNECTED` が発火します。

```plantuml
@startuml TCP 全 path 切断
caption TCP 全 path 切断

participant "RECEIVER\n(accept スレッド #0)" as RA0
participant "RECEIVER\n(accept スレッド #1)" as RA1
participant "アプリ\n(受信側)" as RAPP

note over RA0,RA1: path 0 / path 1 で通信中

note over RA0: path 0 の TCP 接続断
RA0 -> RA0: tcp_active_paths 2→1
note over RA0: POTR_EVENT_DISCONNECTED は発火しない

note over RA1: path 1 の TCP 接続断
RA1 -> RA1: tcp_active_paths 1→0
RA1 -> RAPP: callback(POTR_EVENT_DISCONNECTED)
note over RA1: session_tv_* をリセット\n次の接続は新セッションとして扱う

note over RA0,RA1: 各 accept スレッドが次の接続を待機
@enduml
```
