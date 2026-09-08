# UDP のヘルスチェックのシーケンス

[通信シーケンスの一覧](sequence.md) へ戻ります。

## ヘルスチェック (正常疎通)

ヘルスチェックが有効な場合の PING 送信です。片方向 type 1-6 は open 直後の即時 PING を行わず、最後の `PING` または有効 `DATA` 送信から `health_interval_ms` 経過したときだけ PING を送ります。双方向 UDP では従来どおり定周期 PING と、`path_ping_state[]` 変化時の割り込み PING を送出します。双方向 UDP はこの PING 往復で `CONNECTED` するため、実効 `health_interval_ms = 0` のままでは接続確立しません。

```plantuml
@startuml ヘルスチェック (正常疎通)
caption ヘルスチェック (正常疎通)

participant "ヘルスチェック\nスレッド" as HT
participant "UDP" as UDP
participant "受信スレッド\n(受信者)" as RRT

note over HT: health_interval_ms = 3000ms\n片方向は初回も 3000ms 待機

HT -> HT: 最後の PING / DATA 送信から\n3000ms 待機
note over HT: send_window.next_seq を読み取る\n(ウィンドウには登録しない)
HT -> UDP: PING[seq=N] 送信 (全パス)

UDP -> RRT: PING[seq=N] 受信
RRT -> RRT: last_recv_tv を更新\n(タイムアウトカウンタリセット)
RRT -> RRT: notify_health_alive()\n(health_alive=0 のとき CONNECTED 発火)
RRT -> RRT: next_seq〜N-1 を全スキャン\n欠番を一括 NACK する
note over RRT: 欠番なければ返信なし

HT -> HT: DATA が送られたら期限を後ろへずらす\n送信が止まったら 3000ms 後に PING

note over HT, UDP: 片方向は recent DATA により PING を抑止する\n双方向系は従来どおり定周期 + 割り込み送信

@enduml
```

## ヘルスチェック タイムアウト

片方向 type 1-6 で、最後の有効な `PING` / `DATA` から一定時間パケットが届かなくなった場合の切断検知と復帰です。

```plantuml
@startuml ヘルスチェックタイムアウト
caption ヘルスチェックタイムアウト

participant "送信者" as S
participant "UDP" as UDP
participant "受信スレッド\n(受信者)" as RRT
participant "アプリ\n(受信側)" as RAPP

S -> UDP: DATA 送信 (正常稼働中)
UDP -> RRT: DATA 受信 → last_recv_tv 更新
RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, ...)

note over S, UDP: ここでネットワーク断が発生

S -[#red]> UDP: PING 送信 (到達せず)
S -[#red]> UDP: PING 送信 (到達せず)

note over RRT: health_timeout_ms = 10000ms

RRT -> RRT: last_recv_tv から 10000ms 経過を検出

RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DISCONNECTED, NULL, 0)
RRT -> RRT: health_alive = 0\npeer_session_known = 0\nrecv_window リセット

note over S, UDP: ネットワーク復旧

S -> UDP: DATA または PING 送信
UDP -> RRT: パケット受信
RRT -> RRT: セッション採用\nrecv_window を pkt.seq_num で初期化
RRT -> RRT: health_alive = 1

RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_CONNECTED, NULL, 0)

@enduml
```
