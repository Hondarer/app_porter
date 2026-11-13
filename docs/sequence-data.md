# UDP のデータ転送と再送のシーケンス

[通信シーケンスの一覧](sequence.md) へ戻ります。

## 正常送受信 (非ブロッキング)

`POTR_SEND_BLOCKING` を指定せずに `potr_service_send()` を呼び出したときのデータフローです。片方向 type 1-6 では初回の有効 `DATA` 受信でも CONNECTED が成立します。

```mermaid
sequenceDiagram
    autonumber
    participant SAPP as アプリ<br>(送信側)
    participant Q as 送信キュー
    participant ST as 送信スレッド
    participant UDP as UDP
    participant RRT as 受信スレッド<br>(受信者)
    participant RAPP as アプリ<br>(受信側)
    participant NQ as 通知キュー
    participant NT as 通知専用スレッド

    SAPP->>Q: potr_service_send(handle, POTR_PEER_NA, data, len, 0)<br>→ エレメントを push して即座に復帰
    Q-->>SAPP: POTR_OK

    Note over Q, ST: 非同期に処理

    Q->>ST: pop (エレメント取り出し)
    ST->>ST: seq_num 付与<br>送信ウィンドウへ登録<br>パケット構築
    ST->>UDP: sendto (全パス)

    UDP->>RRT: recvfrom → DATA パケット受信
    RRT->>RRT: service_id 照合<br>セッション識別<br>受信ウィンドウへ格納

    alt 初回受信 (片方向 type 1-6 かつ health_alive = 0)
        RRT->>NQ: 通知内容を保持して登録
        NQ->>NT: 登録順に取り出す
        NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_CONNECTED, NULL, 0)
    end

    RRT->>NQ: 通知内容を保持して登録
    NQ->>NT: 登録順に取り出す
    NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, data, len)
```

CodeBlock: 正常送受信 (非ブロッキング)

## 正常送受信 (ブロッキング)

`POTR_SEND_BLOCKING` を指定して `potr_service_send()` を呼び出したときのデータフローです。  
送信完了まで `potr_service_send()` は復帰しません。

```mermaid
sequenceDiagram
    autonumber
    participant SAPP as アプリ<br>(送信側)
    participant Q as 送信キュー
    participant ST as 送信スレッド
    participant UDP as UDP

    SAPP->>+Q: potr_service_send(handle, POTR_PEER_NA, data, len, POTR_SEND_BLOCKING)

    Q->>Q: (1) 登録用ロックを取得し、全エレメントを push<br>最後のエレメントの番号 (チケット) を保持
    Note over Q: 1 メッセージの断片を連続して格納する

    Q->>ST: pop → パケット構築 → sendto
    ST->>UDP: sendto (全パス)
    ST->>Q: complete() (completed_seq をインクリメント)

    Q->>Q: (2) completed_seq >= チケット になるまで待機
    Note over Q: 送信スレッドは登録順に完了させるため<br>先行の送信も送信済みになる

    Q-->>-SAPP: POTR_OK

    Note over SAPP: 返却時点で UDP 送信済み
```

CodeBlock: 正常送受信 (ブロッキング)

## フラグメント化と結合

送信データが `max_payload` を超える場合の分割・結合処理です。

```mermaid
sequenceDiagram
    autonumber
    participant SAPP as アプリ<br>(送信側)
    participant Q as 送信キュー
    participant ST as 送信スレッド
    participant UDP as UDP
    participant RRT as 受信スレッド<br>(受信者)
    participant RAPP as アプリ<br>(受信側)
    participant NQ as 通知キュー
    participant NT as 通知専用スレッド

    SAPP->>Q: potr_service_send(handle, POTR_PEER_NA, data, len=max_payload×3, 0)<br>len が max_payload を超えるためフラグメント化

    Q->>Q: フラグ MORE_FRAG のエレメント push (1/3)
    Q->>Q: フラグ MORE_FRAG のエレメント push (2/3)
    Q->>Q: フラグなし (最終) のエレメント push (3/3)

    ST->>UDP: DATA[seq=10, MORE_FRAG] (フラグメント 1)
    ST->>UDP: DATA[seq=11, MORE_FRAG] (フラグメント 2)
    ST->>UDP: DATA[seq=12] (フラグメント 3、最終)

    UDP->>RRT: DATA[seq=10, MORE_FRAG] 受信
    RRT->>RRT: frag_buf に蓄積<br>(MORE_FRAG: 続きを待つ)

    UDP->>RRT: DATA[seq=11, MORE_FRAG] 受信
    RRT->>RRT: frag_buf に追加

    UDP->>RRT: DATA[seq=12] 受信
    RRT->>RRT: frag_buf に追加<br>フラグメント結合完了

    RRT->>NQ: 通知内容を保持して登録
    NQ->>NT: 登録順に取り出す
    NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, 結合済みデータ, 全体 len)
```

CodeBlock: フラグメント化と結合 (3 フラグメントの例)

## NACK による再送

パケット ロスが発生した場合の再送シーケンスです。

```mermaid
sequenceDiagram
    autonumber
    participant ST as 送信スレッド
    participant SUDP as UDP<br>(送信側)
    participant RUDP as UDP<br>(受信側)
    participant RRT as 受信スレッド

    ST->>SUDP: DATA[seq=10] 送信
    ST->>SUDP: DATA[seq=11] 送信
    ST->>SUDP: DATA[seq=12] 送信

    SUDP->>RUDP: DATA[seq=10] 到着
    SUDP--xRUDP: DATA[seq=11] ロスト
    SUDP->>RUDP: DATA[seq=12] 到着

    RUDP->>RRT: DATA[seq=10] 受信 → 処理 OK
    RUDP->>RRT: DATA[seq=12] 受信

    RRT->>RRT: seq=11 の欠番を検出

    alt reorder_timeout_ms = 0 (即時・既定値)
        RRT->>SUDP: NACK[ack_num=11] 送信<br>(全パスから送信者へユニキャスト)
    else reorder_timeout_ms > 0 (リオーダー待機)
        RRT->>RRT: タイマー開始<br>(deadline = now + reorder_timeout_ms)<br>→ 待機中に seq=11 が到着すれば NACK 不要
        Note over RRT, SUDP: タイムアウト後に check_reorder_timeout が NACK を送出<br>(以下は NACK 送出後と同じフロー)
        RRT->>SUDP: NACK[ack_num=11] 送信 (タイムアウト後)
    end

    Note over ST: NACK 受信スレッドが受け取る

    SUDP->>ST: NACK[ack_num=11] 受信
    ST->>ST: 送信ウィンドウから seq=11 を検索

    ST->>SUDP: DATA[seq=11] 再送 (全パス)

    SUDP->>RUDP: DATA[seq=11] 到着

    RUDP->>RRT: DATA[seq=11] 受信<br>受信ウィンドウから seq=11 取り出し
    RRT->>RRT: seq=11, 12 の順で整列
```

CodeBlock: NACK による再送

## リオーダー バッファー (reorder_timeout_ms > 0) {#reorder-buffer}

`reorder_timeout_ms` を 0 より大きな値に設定すると、欠番検出後にただちに NACK や DISCONNECTED を発行せず、指定時間だけ待機します。待機中に欠落パケットが到着した場合は NACK/DISCONNECTED を発行せずに正常配信します。

### 通常モード: 待機中に到着した場合 (NACK なし)

```mermaid
sequenceDiagram
    autonumber
    participant ST as 送信スレッド
    participant SUDP as UDP<br>(送信側)
    participant RUDP as UDP<br>(受信側)
    participant RRT as 受信スレッド
    participant RAPP as アプリ<br>(受信側)
    participant NQ as 通知キュー
    participant NT as 通知専用スレッド

    ST->>SUDP: DATA[seq=10] 送信
    ST->>SUDP: DATA[seq=11] 送信 (遅延)
    ST->>SUDP: DATA[seq=12] 送信

    SUDP->>RUDP: DATA[seq=10] 到着
    RUDP->>RRT: DATA[seq=10] 受信 → 処理 OK
    RRT->>NQ: 通知内容を保持して登録
    NQ->>NT: 登録順に取り出す
    NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, data[10], len)

    SUDP->>RUDP: DATA[seq=12] 到着 (seq=11 より先に到達)

    RUDP->>RRT: DATA[seq=12] 受信
    RRT->>RRT: seq=11 の欠番を検出<br>タイマー開始 (reorder_timeout_ms)
    Note over RRT: deadline 内は NACK を保留

    SUDP->>RUDP: DATA[seq=11] 到着 (追い越し解消)
    RUDP->>RRT: DATA[seq=11] 受信<br>欠番が埋まった → reorder_pending クリア
    RRT->>RRT: seq=11, 12 の順で取り出し
    RRT->>NQ: 通知内容を保持して登録
    NQ->>NT: 登録順に取り出す
    NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, data[11], len)
    RRT->>NQ: 通知内容を保持して登録
    NQ->>NT: 登録順に取り出す
    NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, data[12], len)

    Note over RRT: NACK は送出されなかった
```

CodeBlock: リオーダー - 通常モード: 待機中に到着した場合 (NACK なし)

### RAW モード: 待機中に到着した場合 (DISCONNECTED なし)

```mermaid
sequenceDiagram
    autonumber
    participant ST as 送信スレッド
    participant SUDP as UDP<br>(送信側)
    participant RUDP as UDP<br>(受信側)
    participant RRT as 受信スレッド
    participant RAPP as アプリ<br>(受信側)
    participant NQ as 通知キュー
    participant NT as 通知専用スレッド

    ST->>SUDP: DATA[seq=10] 送信
    ST->>SUDP: DATA[seq=11] 送信 (遅延)
    ST->>SUDP: DATA[seq=12] 送信

    SUDP->>RUDP: DATA[seq=10] 到着
    RUDP->>RRT: DATA[seq=10] 受信 → 処理 OK
    RRT->>NQ: 通知内容を保持して登録
    NQ->>NT: 登録順に取り出す
    NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, data[10], len)

    SUDP->>RUDP: DATA[seq=12] 到着 (seq=11 より先に到達)

    RUDP->>RRT: DATA[seq=12] 受信
    RRT->>RRT: seq=11 の欠番を検出 (RAW)<br>タイマー開始 (reorder_timeout_ms)
    Note over RRT: deadline 内は DISCONNECTED を保留

    SUDP->>RUDP: DATA[seq=11] 到着 (追い越し解消)
    RUDP->>RRT: DATA[seq=11] 受信<br>欠番が埋まった → reorder_pending クリア
    RRT->>RRT: seq=11, 12 の順で取り出し
    RRT->>NQ: 通知内容を保持して登録
    NQ->>NT: 登録順に取り出す
    NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, data[11], len)
    RRT->>NQ: 通知内容を保持して登録
    NQ->>NT: 登録順に取り出す
    NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, data[12], len)

    Note over RRT: DISCONNECTED は発火しなかった
```

CodeBlock: リオーダー - RAW モード: 待機中に届いた場合 (DISCONNECTED なし)

## REJECT による切断と復帰

送信ウィンドウから evict 済みのパケットを要求した場合のシーケンスです。

```mermaid
sequenceDiagram
    autonumber
    participant ST as 送信スレッド
    participant UDP as UDP
    participant RRT as 受信スレッド
    participant RAPP as アプリ<br>(受信側)
    participant NQ as 通知キュー
    participant NT as 通知専用スレッド

    ST->>UDP: DATA[seq=0..15] 送信 (ウィンドウサイズ 16)

    Note over ST: ウィンドウが満杯→ seq=0 を evict

    ST->>UDP: DATA[seq=16] 送信 (seq=0 が evict される)

    UDP--xRRT: DATA[seq=0] ロスト

    RRT->>RRT: seq=0 の欠番を検出
    RRT->>UDP: NACK[ack_num=0] 送信

    UDP->>ST: NACK[ack_num=0] 受信
    ST->>ST: 送信ウィンドウから seq=0 を検索<br>→ evict 済みで見つからない

    ST->>UDP: REJECT[ack_num=0] 送信

    UDP->>RRT: REJECT[ack_num=0] 受信

    RRT->>NQ: 通知内容を保持して登録
    NQ->>NT: 登録順に取り出す
    NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DISCONNECTED, NULL, 0)
    RRT->>RRT: 欠落 seq=0 をスキップ<br>次の通番 seq=1 から再開

    Note over RRT: 次のパケット到着で復帰

    UDP->>RRT: DATA[seq=17] 受信
    RRT->>NQ: 通知内容を保持して登録
    NQ->>NT: 登録順に取り出す
    NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_CONNECTED, NULL, 0)
    RRT->>NQ: 通知内容を保持して登録
    NQ->>NT: 登録順に取り出す
    NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, ...)
```

CodeBlock: REJECT による切断と復帰

## RAW モード: ギャップ検出による切断と復帰 (DATA)

DATA パケットの追い越し (欠落) を検出した場合のシーケンスです。

```mermaid
sequenceDiagram
    autonumber
    participant ST as 送信スレッド
    participant SUDP as UDP<br>(送信側)
    participant RUDP as UDP<br>(受信側)
    participant RRT as 受信スレッド
    participant RAPP as アプリ<br>(受信側)
    participant NQ as 通知キュー
    participant NT as 通知専用スレッド

    ST->>SUDP: DATA[seq=10] 送信
    ST->>SUDP: DATA[seq=11] 送信 (ロスト)
    ST->>SUDP: DATA[seq=12] 送信

    SUDP->>RUDP: DATA[seq=10] 到着
    SUDP--xRUDP: DATA[seq=11] ロスト
    SUDP->>RUDP: DATA[seq=12] 到着

    RUDP->>RRT: DATA[seq=10] 受信 → 処理 OK
    RUDP->>RRT: DATA[seq=12] 受信

    RRT->>RRT: seq=11 の欠番を検出<br>(RAW: NACK は送信しない)

    alt reorder_timeout_ms = 0 (即時・既定値)
        RRT->>NQ: 通知内容を保持して登録
        NQ->>NT: 登録順に取り出す
        NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DISCONNECTED, NULL, 0)
        RRT->>RRT: recv_window を seq=12 でリセット
        RRT->>RRT: DATA[seq=12] をウィンドウから取り出し
        RRT->>NQ: 通知内容を保持して登録
        NQ->>NT: 登録順に取り出す
        NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_CONNECTED, NULL, 0)
        RRT->>NQ: 通知内容を保持して登録
        NQ->>NT: 登録順に取り出す
        NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, data[seq=12], len)
    else reorder_timeout_ms > 0 (リオーダー待機)
        RRT->>RRT: タイマー開始<br>(deadline = now + reorder_timeout_ms)<br>→ 待機中に seq=11 が到着すれば DISCONNECTED 不要
        Note over RRT: タイムアウト後に check_reorder_timeout で<br>DISCONNECTED 発火・ウィンドウリセット
    end

    Note over RRT: seq=11 は配信されない<br>(欠落として確定)
```

CodeBlock: RAW モード - DATA ギャップ検出

## RAW モード: ギャップ検出による切断と復帰 (PING)

PING の `seq_num` から欠落パケットを検出した場合のシーケンスです。

```mermaid
sequenceDiagram
    autonumber
    participant HT as ヘルス チェック<br>スレッド (送信者)
    participant UDP as UDP
    participant RRT as 受信スレッド
    participant RAPP as アプリ<br>(受信側)
    participant NQ as 通知キュー
    participant NT as 通知専用スレッド

    Note over RRT: recv_window.next_seq = 10

    HT->>UDP: DATA[seq=10..12] 送信 (ロスト)
    UDP--xRRT: DATA[seq=10..12] ロスト

    HT->>UDP: PING[seq=13]<br>(next_seq=13 を通知)
    UDP->>RRT: PING[seq=13] 受信

    RRT->>RRT: pkt.seq_num(13) != next_seq(10)<br>→ ギャップあり (window内)

    alt reorder_timeout_ms = 0 (即時・既定値)
        RRT->>NQ: 通知内容を保持して登録
        NQ->>NT: 登録順に取り出す
        NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DISCONNECTED, NULL, 0)
        RRT->>RRT: recv_window を seq=13 でリセット
        RRT->>NQ: 通知内容を保持して登録
        NQ->>NT: 登録順に取り出す
        NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_CONNECTED, NULL, 0)
    else reorder_timeout_ms > 0 (リオーダー待機)
        RRT->>RRT: タイマー開始 (next_seq=10 の欠番に対して)<br>→ 待機中に seq=10〜12 が到着すれば DISCONNECTED 不要
        Note over RRT: タイムアウト後に check_reorder_timeout で<br>DISCONNECTED 発火・ウィンドウリセット
    end

    Note over RRT: seq=10〜12 は配信されない<br>next_seq = 13 に更新
```

CodeBlock: RAW モード - PING ギャップ検出

## close と登録済み通知

close は API の受付を停止して、受付済み操作と送信の完了を待ちます。  
通知の生成元が停止した後に通知キューを確定し、すべての登録済み通知を実行してから復帰します。  
close 中にコールバックが新たに送信を要求した場合は CANCELED で拒否します。

```mermaid
sequenceDiagram
    participant A as close 呼び出し元
    participant H as ハンドル制御
    participant W as 通信スレッド
    participant N as 通知専用スレッド
    A->>H: 受付停止、使用中参照の返却待ち
    H->>W: 登録済み送信の完了待ち、終了処理
    W->>N: 終了までに生成した通知を登録
    H->>W: 通知生成元の停止と join
    H->>N: キュー確定、登録済み通知の完了待ち
    N-->>H: 全通知の完了とスレッド終了
    H-->>A: 通信資源の回収とハンドル無効化
```
