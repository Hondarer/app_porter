# UDP 双方向通信のシーケンス

[通信シーケンスの一覧](sequence.md) へ戻ります。

## unicast_bidir 1:1 双方向通信

`POTR_TYPE_UNICAST_BIDIR` における双方向データ通信のシーケンスです。  
両端が独立したセッションを持ち、それぞれがデータ送受信・NACK・ヘルスチェックを行います。

```plantuml
@startuml unicast_bidir 通信シーケンス
title unicast_bidir 1:1 双方向通信シーケンス

participant "Side A\n(POTR_ROLE_SENDER)" as A
participant "Side B\n(POTR_ROLE_RECEIVER)" as B

== サービス開始 ==

note over A: potr_service_open()\nbind(src_addr=A, src_port=PA)
note over B: potr_service_open()\nbind(src_addr=B, src_port=PB)

== A → B データ送信 ==

A -> B: DATA (session=S_A, seq=0)
note over B: peer_session_known = false\n→ セッション S_A 採用\nDATA はウィンドウに入るが CONNECTED はまだ発火しない

A -> B: DATA (session=S_A, seq=1)
note over B: POTR_EVENT_DATA

== B → A データ送信 ==

B -> A: DATA (session=S_B, seq=0)
note over A: peer_session_known = false\n→ セッション S_B 採用\nDATA はウィンドウに入るが CONNECTED はまだ発火しない

== 双方向データ ==

A -> B: DATA (session=S_A, seq=2)
B -> A: DATA (session=S_B, seq=1)
note over A,B: POTR_EVENT_DATA（各端で独立して発火）

== パケット欠落と再送 ==

A -> B: DATA (session=S_A, seq=3)
A -[#red]x B: DATA (session=S_A, seq=4)  [lost]
A -> B: DATA (session=S_A, seq=5)
note over B: seq=4 の欠落検出
B -> A: NACK (ack_num=4)
A -> B: DATA (session=S_A, seq=4)  [retransmit]
note over B: POTR_EVENT_DATA (seq=4, 5 の順)

== ヘルスチェック（対称） ==

A -> B: 定周期 PING (session=S_A, seq_num=N,\npayload=UNDEFINED)
note over B: path_ping_state を NORMAL に更新
B -> A: 割り込み PING (session=S_B, seq_num=M,\npayload に NORMAL を含む)
note over A: remote_path_ping_state に NORMAL を確認\n→ CONNECTED を早期発火

B -> A: 定周期 PING (session=S_B, seq_num=P)
note over A: path_ping_state を NORMAL に更新
A -> B: 割り込み PING (session=S_A, seq_num=Q,\npayload に NORMAL を含む)
note over B: remote_path_ping_state に NORMAL を確認

@enduml
```

## unicast_bidir N:1 サーバーでの接続と送受信

`POTR_TYPE_UNICAST_BIDIR` を N:1 モードで開いたサーバーが、新規クライアントを受け付けて `peer_id` ごとに通信するシーケンスです。

```plantuml
@startuml unicast_bidir N1 接続
title unicast_bidir N:1 サーバでの接続と送受信

participant "Client A" as CA
participant "Server\n(RECEIVER / N:1)" as S
participant "アプリ" as APP

note over S: potr_service_open()\nsrc_addr 省略 → N:1 モード\nbind(dst_addr, dst_port)

CA -> S: DATA (client session=C_A, seq=0)
S -> S: session triplet で未知ピア判定\npeer table に新規登録\npeer_id=1 を払い出し
S -> APP: callback(service_id, 1, POTR_EVENT_CONNECTED, NULL, 0)
S -> APP: callback(service_id, 1, POTR_EVENT_DATA, data, len)

APP -> S: potr_send(handle, 1, reply, len, 0)
S -> CA: DATA (server session=S_1, seq=0)

APP -> S: potr_send(handle, POTR_PEER_ALL, notice, len, 0)
S -> CA: DATA (peer_id=1 向け送信)
@enduml
```

## unicast_bidir N:1 サーバーでの切断

サーバーは FIN 受信、`potr_peer_disconnect()`、またはヘルスチェック タイムアウトによりピア単位で切断を処理します。

```plantuml
@startuml unicast_bidir N1 切断
title unicast_bidir N:1 サーバでの切断

participant "Client A" as CA
participant "Server" as S
participant "アプリ" as APP

CA -> S: FIN
S -> APP: callback(service_id, 1, POTR_EVENT_DISCONNECTED, NULL, 0)
S -> S: peer table から peer_id=1 を削除

APP -> S: potr_peer_disconnect(handle, 2)
S -> CA: FIN
S -> APP: callback(service_id, 2, POTR_EVENT_DISCONNECTED, NULL, 0)
S -> S: peer table から peer_id=2 を削除
@enduml
```

## unicast_bidir ヘルスチェック タイムアウトによる切断検知

`POTR_TYPE_UNICAST_BIDIR` において、相手側が停止した場合の切断検知シーケンスです。1:1 モードでは相手端単位、N:1 モードでは各 `peer_id` 単位で `last_recv_tv_sec` を監視し、`health_timeout_ms` 超過で切断を検知します。

```plantuml
@startuml unicast_bidir タイムアウト
title unicast_bidir ヘルスタイムアウトによる切断検知

participant "Side A" as A
participant "Side B\n(停止)" as B

note over A,B: 通信中

A -> B: PING (ack_num=0, seq_num=N)
note over B: アプリケーションが停止\n（パケットが一切到着しなくなる）
note over A: health_timeout_ms 経過\nlast_recv_tv_sec が更新されない\ncheck_health_timeout() → DISCONNECTED
note over A: POTR_EVENT_DISCONNECTED 発火

@enduml
```
