# UDP のデータ転送と再送のシーケンス

[通信シーケンスの一覧](sequence.md) へ戻ります。

## 正常送受信 (非ブロッキング)

`POTR_SEND_BLOCKING` を指定せずに `potr_send()` を呼び出したときのデータフローです。片方向 type 1-6 では初回の有効 `DATA` 受信でも CONNECTED が成立します。

```plantuml
@startuml 正常送受信 (非ブロッキング)
caption 正常送受信 (非ブロッキング)

participant "アプリ\n(送信側)" as SAPP
participant "送信キュー" as Q
participant "送信スレッド" as ST
participant "UDP" as UDP
participant "受信スレッド\n(受信者)" as RRT
participant "アプリ\n(受信側)" as RAPP

SAPP -> Q: potr_send(handle, POTR_PEER_NA, data, len, 0)\n→ エレメントを push して即座に復帰
SAPP <-- Q: POTR_OK

note over Q, ST: 非同期に処理

Q -> ST: pop (エレメント取り出し)
ST -> ST: seq_num 付与\n送信ウィンドウへ登録\nパケット構築
ST -> UDP: sendto (全パス)

UDP -> RRT: recvfrom → DATA パケット受信
RRT -> RRT: service_id 照合\nセッション識別\n受信ウィンドウへ格納

alt 初回受信 (片方向 type 1-6 かつ health_alive = 0)
  RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_CONNECTED, NULL, 0)
end

RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, data, len)

@enduml
```

## 正常送受信 (ブロッキング)

`POTR_SEND_BLOCKING` を指定して `potr_send()` を呼び出したときのデータフローです。  
送信完了まで `potr_send()` は復帰しません。

```plantuml
@startuml 正常送受信 (ブロッキング)
caption 正常送受信 (ブロッキング)

participant "アプリ\n(送信側)" as SAPP
participant "送信キュー" as Q
participant "送信スレッド" as ST
participant "UDP" as UDP

SAPP -> Q: potr_send(handle, POTR_PEER_NA, data, len, POTR_SEND_BLOCKING)
activate SAPP

Q -> Q: (1) 既存キューが drained になるまで待機\n count == 0 && inflight == 0
note right of Q: 前の送信が完了するまで待つ

Q -> Q: (2) エレメントを push\n inflight をインクリメント

Q -> ST: pop → パケット構築 → sendto
ST -> UDP: sendto (全パス)
ST -> Q: complete() (inflight デクリメント)

Q -> Q: (3) drained 待機\n count == 0 && inflight == 0

Q --> SAPP: POTR_OK
deactivate SAPP

note over SAPP: 返却時点で UDP 送信済み

@enduml
```

## フラグメント化と結合

送信データが `max_payload` を超える場合の分割・結合処理です。

```plantuml
@startuml フラグメント化と結合 (3 フラグメントの例)
caption フラグメント化と結合 (3 フラグメントの例)

participant "アプリ\n(送信側)" as SAPP
participant "送信キュー" as Q
participant "送信スレッド" as ST
participant "UDP" as UDP
participant "受信スレッド\n(受信者)" as RRT
participant "アプリ\n(受信側)" as RAPP

SAPP -> Q: potr_send(handle, POTR_PEER_NA, data, len=max_payload×3, 0)\nlen が max_payload を超えるためフラグメント化

Q -> Q: フラグ MORE_FRAG のエレメント push (1/3)
Q -> Q: フラグ MORE_FRAG のエレメント push (2/3)
Q -> Q: フラグなし (最終) のエレメント push (3/3)

ST -> UDP: DATA[seq=10, MORE_FRAG] (フラグメント 1)
ST -> UDP: DATA[seq=11, MORE_FRAG] (フラグメント 2)
ST -> UDP: DATA[seq=12]           (フラグメント 3、最終)

UDP -> RRT: DATA[seq=10, MORE_FRAG] 受信
RRT -> RRT: frag_buf に蓄積\n(MORE_FRAG: 続きを待つ)

UDP -> RRT: DATA[seq=11, MORE_FRAG] 受信
RRT -> RRT: frag_buf に追加

UDP -> RRT: DATA[seq=12] 受信
RRT -> RRT: frag_buf に追加\nフラグメント結合完了

RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, 結合済みデータ, 全体 len)

@enduml
```

## NACK による再送

パケット ロスが発生した場合の再送シーケンスです。

```plantuml
@startuml NACK による再送
caption NACK による再送

participant "送信スレッド" as ST
participant "UDP\n(送信側)" as SUDP
participant "UDP\n(受信側)" as RUDP
participant "受信スレッド" as RRT

ST -> SUDP: DATA[seq=10] 送信
ST -> SUDP: DATA[seq=11] 送信
ST -> SUDP: DATA[seq=12] 送信

SUDP -> RUDP: DATA[seq=10] 到着
SUDP -[#red]> RUDP: DATA[seq=11] ロスト
SUDP -> RUDP: DATA[seq=12] 到着

RUDP -> RRT: DATA[seq=10] 受信 → 処理 OK
RUDP -> RRT: DATA[seq=12] 受信

RRT -> RRT: seq=11 の欠番を検出

alt reorder_timeout_ms = 0 (即時・デフォルト)
  RRT -> SUDP: NACK[ack_num=11] 送信\n(全パスから送信者へユニキャスト)
else reorder_timeout_ms > 0 (リオーダー待機)
  RRT -> RRT: タイマー開始\n(deadline = now + reorder_timeout_ms)\n→ 待機中に seq=11 が到着すれば NACK 不要
  note over RRT, SUDP: タイムアウト後に check_reorder_timeout が NACK を送出\n(以下は NACK 送出後と同じフロー)
  RRT -> SUDP: NACK[ack_num=11] 送信 (タイムアウト後)
end

note over ST: NACK 受信スレッドが受け取る

SUDP -> ST: NACK[ack_num=11] 受信
ST -> ST: 送信ウィンドウから seq=11 を検索

ST -> SUDP: DATA[seq=11] 再送 (全パス)

SUDP -> RUDP: DATA[seq=11] 到着

RUDP -> RRT: DATA[seq=11] 受信\n受信ウィンドウから seq=11 取り出し
RRT -> RRT: seq=11, 12 の順で整列

@enduml
```

## リオーダー バッファー (reorder_timeout_ms > 0) {#reorder-buffer}

`reorder_timeout_ms` を 0 より大きな値に設定すると、欠番検出後にただちに NACK や DISCONNECTED を発行せず、指定時間だけ待機します。待機中に欠落パケットが到着した場合は NACK/DISCONNECTED を発行せずに正常配信します。

### 通常モード: 待機中に到着した場合 (NACK なし)

```plantuml
@startuml リオーダー - 通常モード 待機中に届いた場合
caption リオーダー - 通常モード: 待機中に到着した場合 (NACK なし)

participant "送信スレッド" as ST
participant "UDP\n(送信側)" as SUDP
participant "UDP\n(受信側)" as RUDP
participant "受信スレッド" as RRT
participant "アプリ\n(受信側)" as RAPP

ST -> SUDP: DATA[seq=10] 送信
ST -> SUDP: DATA[seq=11] 送信 (遅延)
ST -> SUDP: DATA[seq=12] 送信

SUDP -> RUDP: DATA[seq=10] 到着
RUDP -> RRT: DATA[seq=10] 受信 → 処理 OK
RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, data[10], len)

SUDP -> RUDP: DATA[seq=12] 到着 (seq=11 より先に到達)

RUDP -> RRT: DATA[seq=12] 受信
RRT -> RRT: seq=11 の欠番を検出\nタイマー開始 (reorder_timeout_ms)
note over RRT: deadline 内は NACK を保留

SUDP -> RUDP: DATA[seq=11] 到着 (追い越し解消)
RUDP -> RRT: DATA[seq=11] 受信\n欠番が埋まった → reorder_pending クリア
RRT -> RRT: seq=11, 12 の順で取り出し
RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, data[11], len)
RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, data[12], len)

note over RRT: NACK は送出されなかった

@enduml
```

### RAW モード: 待機中に到着した場合 (DISCONNECTED なし)

```plantuml
@startuml リオーダー - RAW モード 待機中に届いた場合
caption リオーダー - RAW モード: 待機中に届いた場合 (DISCONNECTED なし)

participant "送信スレッド" as ST
participant "UDP\n(送信側)" as SUDP
participant "UDP\n(受信側)" as RUDP
participant "受信スレッド" as RRT
participant "アプリ\n(受信側)" as RAPP

ST -> SUDP: DATA[seq=10] 送信
ST -> SUDP: DATA[seq=11] 送信 (遅延)
ST -> SUDP: DATA[seq=12] 送信

SUDP -> RUDP: DATA[seq=10] 到着
RUDP -> RRT: DATA[seq=10] 受信 → 処理 OK
RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, data[10], len)

SUDP -> RUDP: DATA[seq=12] 到着 (seq=11 より先に到達)

RUDP -> RRT: DATA[seq=12] 受信
RRT -> RRT: seq=11 の欠番を検出 (RAW)\nタイマー開始 (reorder_timeout_ms)
note over RRT: deadline 内は DISCONNECTED を保留

SUDP -> RUDP: DATA[seq=11] 到着 (追い越し解消)
RUDP -> RRT: DATA[seq=11] 受信\n欠番が埋まった → reorder_pending クリア
RRT -> RRT: seq=11, 12 の順で取り出し
RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, data[11], len)
RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, data[12], len)

note over RRT: DISCONNECTED は発火しなかった

@enduml
```

## REJECT による切断と復帰

送信ウィンドウから evict 済みのパケットを要求した場合のシーケンスです。

```plantuml
@startuml REJECT による切断と復帰
caption REJECT による切断と復帰

participant "送信スレッド" as ST
participant "UDP" as UDP
participant "受信スレッド" as RRT
participant "アプリ\n(受信側)" as RAPP

ST -> UDP: DATA[seq=0..15] 送信 (ウィンドウサイズ 16)

note over ST: ウィンドウが満杯→ seq=0 を evict

ST -> UDP: DATA[seq=16] 送信 (seq=0 が evict される)

UDP -[#red]> RRT: DATA[seq=0] ロスト

RRT -> RRT: seq=0 の欠番を検出
RRT -> UDP: NACK[ack_num=0] 送信

UDP -> ST: NACK[ack_num=0] 受信
ST -> ST: 送信ウィンドウから seq=0 を検索\n→ evict 済みで見つからない

ST -> UDP: REJECT[ack_num=0] 送信

UDP -> RRT: REJECT[ack_num=0] 受信

RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DISCONNECTED, NULL, 0)
RRT -> RRT: 欠落 seq=0 をスキップ\n次の通番 seq=1 から再開

note over RRT: 次のパケット到着で復帰

UDP -> RRT: DATA[seq=17] 受信
RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_CONNECTED, NULL, 0)
RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, ...)

@enduml
```

## RAW モード: ギャップ検出による切断と復帰 (DATA)

DATA パケットの追い越し (欠落) を検出した場合のシーケンスです。

```plantuml
@startuml RAW モード - DATA ギャップ検出
caption RAW モード - DATA ギャップ検出

participant "送信スレッド" as ST
participant "UDP\n(送信側)" as SUDP
participant "UDP\n(受信側)" as RUDP
participant "受信スレッド" as RRT
participant "アプリ\n(受信側)" as RAPP

ST -> SUDP: DATA[seq=10] 送信
ST -> SUDP: DATA[seq=11] 送信 (ロスト)
ST -> SUDP: DATA[seq=12] 送信

SUDP -> RUDP: DATA[seq=10] 到着
SUDP -[#red]> RUDP: DATA[seq=11] ロスト
SUDP -> RUDP: DATA[seq=12] 到着

RUDP -> RRT: DATA[seq=10] 受信 → 処理 OK
RUDP -> RRT: DATA[seq=12] 受信

RRT -> RRT: seq=11 の欠番を検出\n(RAW: NACK は送信しない)

alt reorder_timeout_ms = 0 (即時・デフォルト)
  RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DISCONNECTED, NULL, 0)
  RRT -> RRT: recv_window を seq=12 でリセット
  RRT -> RRT: DATA[seq=12] をウィンドウから取り出し
  RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_CONNECTED, NULL, 0)
  RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, data[seq=12], len)
else reorder_timeout_ms > 0 (リオーダー待機)
  RRT -> RRT: タイマー開始\n(deadline = now + reorder_timeout_ms)\n→ 待機中に seq=11 が到着すれば DISCONNECTED 不要
  note over RRT: タイムアウト後に check_reorder_timeout で\nDISCONNECTED 発火・ウィンドウリセット
end

note over RRT: seq=11 は配信されない\n(欠落として確定)

@enduml
```

## RAW モード: ギャップ検出による切断と復帰 (PING)

PING の `seq_num` から欠落パケットを検出した場合のシーケンスです。

```plantuml
@startuml RAW モード - PING ギャップ検出
caption RAW モード - PING ギャップ検出

participant "ヘルスチェック\nスレッド (送信者)" as HT
participant "UDP" as UDP
participant "受信スレッド" as RRT
participant "アプリ\n(受信側)" as RAPP

note over RRT: recv_window.next_seq = 10

HT -> UDP: DATA[seq=10..12] 送信 (ロスト)
UDP -[#red]> RRT: DATA[seq=10..12] ロスト

HT -> UDP: PING[seq=13]\n(next_seq=13 を通知)
UDP -> RRT: PING[seq=13] 受信

RRT -> RRT: pkt.seq_num(13) != next_seq(10)\n→ ギャップあり (window内)

alt reorder_timeout_ms = 0 (即時・デフォルト)
  RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DISCONNECTED, NULL, 0)
  RRT -> RRT: recv_window を seq=13 でリセット
  RRT -> RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_CONNECTED, NULL, 0)
else reorder_timeout_ms > 0 (リオーダー待機)
  RRT -> RRT: タイマー開始 (next_seq=10 の欠番に対して)\n→ 待機中に seq=10〜12 が到着すれば DISCONNECTED 不要
  note over RRT: タイムアウト後に check_reorder_timeout で\nDISCONNECTED 発火・ウィンドウリセット
end

note over RRT: seq=10〜12 は配信されない\nnext_seq = 13 に更新

@enduml
```
