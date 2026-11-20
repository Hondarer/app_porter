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
    ST->>UDP: sendto (選択中グループのパス)

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

Figure: 正常送受信 (非ブロッキング)

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
    ST->>UDP: sendto (選択中グループのパス)
    ST->>Q: complete() (completed_seq をインクリメント)

    Q->>Q: (2) completed_seq >= チケット になるまで待機
    Note over Q: 送信スレッドは登録順に完了させるため<br>先行の送信も送信済みになる

    Q-->>-SAPP: POTR_OK

    Note over SAPP: 返却時点で UDP 送信済み
```

Figure: 正常送受信 (ブロッキング)

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

Figure: フラグメント化と結合 (3 フラグメントの例)

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

    ST->>SUDP: DATA[seq=11] 再送 (選択中グループのパス)

    SUDP->>RUDP: DATA[seq=11] 到着

    RUDP->>RRT: DATA[seq=11] 受信<br>受信ウィンドウから seq=11 取り出し
    RRT->>RRT: seq=11, 12 の順で整列
```

Figure: NACK による再送

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

Figure: リオーダー - 通常モード: 待機中に到着した場合 (NACK なし)

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

Figure: リオーダー - RAW モード: 待機中に届いた場合 (DISCONNECTED なし)

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
    RRT->>RRT: ack_num=0 が自スロットの欠番か判定

    RRT->>NQ: 通知内容を保持して登録
    NQ->>NT: 登録順に取り出す
    NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DISCONNECTED, NULL, 0)
    RRT->>RRT: 受信の状態を破棄<br>(受信ウィンドウ、フラグメント、リオーダー、セッション既知状態)

    Note over RRT: 次に届いたパケットで受信ウィンドウを初期化して復帰

    UDP->>RRT: DATA[seq=17] 受信
    RRT->>RRT: 新セッションとして採用し<br>seq=17 で受信ウィンドウを初期化
    RRT->>NQ: 通知内容を保持して登録
    NQ->>NT: 登録順に取り出す
    NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_CONNECTED, NULL, 0)
    RRT->>NQ: 通知内容を保持して登録
    NQ->>NT: 登録順に取り出す
    NT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, ...)
```

Figure: REJECT による切断と復帰

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

Figure: RAW モード - DATA ギャップ検出

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

Figure: RAW モード - PING ギャップ検出

## フロー制御による抑制と解除

受信側の構成で `flow_control_capacity` を指定した場合の動作です。  
受信者は、すべてのフロー制御パケットに送出上限 (次に受信する通番 + 許可数) を載せ、送信者は送出上限に達したら自分で止まります。OS の受信バッファーに基づく送出上限を使う受信側では、許可数は、占有量による許可数と受信バッファーの幅の小さいほうです。  
`multicast` と `broadcast` の送信者は、「止める状態」(要求中、または送出上限に達している) の受信者の割合または台数が構成の条件を満たす場合に抑制します。`multicast` と `broadcast` で送出上限の最初の通知と更新を送るのは、監視対象の受信者だけです (後述の「multicast の監視対象の選択と入れ替わり」)。

```mermaid
sequenceDiagram
    participant SApp as 送信側の利用側
    participant S as 送信者
    participant R as 受信者
    participant RApp as 受信側の利用側

    Note over R: 送信者を新しい通信として採用した
    R->>S: FLOW_CONTROL (状態 = 解除、最初の送出上限)
    Note over S: 送出上限を保持する
    SApp->>S: potr_service_send()
    S->>R: DATA
    Note over R: 前回知らせた範囲の半分以上を受信し<br>占有量が解除のしきい値を下回っている
    R->>S: FLOW_CONTROL (状態 = 解除、送出上限の更新)
    Note over S: 送出上限を上書きする
    S->>R: DATA
    Note over R: 通知キューの占有量が<br>要求のしきい値 (既定 80%) を超えた
    R->>S: FLOW_CONTROL (状態 = 抑制の要求、送出上限)
    Note over S: 新しい DATA の送出を止める<br>再送、PING、FIN は継続する
    SApp->>S: potr_service_send()
    Note over S: 送信キューに空きがあれば受け付ける<br>空きがなければ POTR_ERR_FULL<br>(POTR_SEND_BLOCKING ありは空きを待つ)
    R->>RApp: POTR_EVENT_DATA (コールバック)
    loop 再送周期 (既定 200 ms) ごと
        R->>S: FLOW_CONTROL (状態 = 抑制の要求)
        Note over S: 要求の期限を<br>受信時刻 + 有効期限 (既定 1,000 ms) へ更新
    end
    Note over R: コールバックからの復帰で占有量が<br>解除のしきい値 (既定 50%) を下回った
    R->>S: FLOW_CONTROL (状態 = 解除、新しい送出上限)
    Note over S: 送出上限まで送出を再開する
    S->>R: DATA
```

Figure: フロー制御による抑制と解除のシーケンス

解除が失われた場合と、抑制を要求した受信者が停止した場合は、要求の期限が過ぎた時点で送信者が送出を再開します。

送信者が送出上限まで送り終えたときの動作は、次のとおりです。

```mermaid
sequenceDiagram
    participant S as 送信者
    participant R as 受信者

    S->>R: DATA (送出上限の直前の通番まで)
    Note over S: 次に割り当てる通番が送出上限に達した<br>新しい DATA の送出を止める
    alt 占有量が解除のしきい値を下回っている
        R->>S: FLOW_CONTROL (状態 = 解除、送出上限の更新)
        Note over S: 新しい送出上限まで送出を再開する
    else 占有量が解除のしきい値以上である
        R->>S: FLOW_CONTROL (状態 = 抑制の要求、送出上限)
        Note over R: 要求中へ遷移し、再送周期ごとに再送する
    end
    opt 有効期限の間、フロー制御パケットが届かない
        Note over S: 止まった時点から有効期限 (既定 1,000 ms) が経過したら<br>送出上限を破棄して送出を再開する
    end
```

Figure: 送出上限まで送り終えたときのシーケンス

## 容量を設定していない受信側の送出上限の更新

受信側の構成で `flow_control_capacity` を指定していない場合も、`unicast`、`multicast`、`broadcast`、`unicast_bidir`、`unicast_bidir_n1` の受信側は、OS の受信バッファーに収まると見積もった DATA パケットの数 (受信バッファーの幅) を許可数として、送出上限を知らせます。  
受信側は抑制の要求を送らず、送出上限の最初の通知と更新だけを送ります。次の図は、OS の受信バッファーが既定の Linux (幅 42) の受信者の例です。

```mermaid
sequenceDiagram
    participant S as 送信者
    participant R as 受信者

    Note over R: サービスを開くときに<br>OS の受信バッファーの大きさ (SO_RCVBUF) を取得し<br>受信バッファーの幅 (42) を求める
    S->>R: PING
    Note over R: 送信者を新しい通信として採用した
    R->>S: FLOW_CONTROL (状態 = 解除、送出上限 = 次に受信する通番 + 42)
    loop 連続した送信
        S->>R: DATA
        Note over R: 前回知らせた範囲の半分 (21 パケット) を受信した
        R->>S: FLOW_CONTROL (状態 = 解除、送出上限の更新)
    end
    Note over S: 送出上限に達した場合は止まり<br>次の更新で送出を再開する
    opt 相手が送出上限まで送り終えた
        R->>S: FLOW_CONTROL (状態 = 解除、送出上限の更新)
    end
```

Figure: 容量を設定していない受信側が送出上限を更新するシーケンス

OS の受信バッファーの大きさを取得できなかった受信側と、`flow_control_receive_buffer_percent` に `-1` を指定した受信側は、容量を指定していなければフロー制御パケットを送出しません。

## multicast の監視対象の選択と入れ替わり

`multicast` と `broadcast` では、送出上限を知らせるのは、送信者が選んだ監視対象の受信者だけです。  
受信者は受信報告に自身の送出上限を載せ、送信者は受信者ごとの余裕 (送出上限 − 次に割り当てる通番) が小さいほうから監視対象を選びます。次の図は、1 対 N の台数の条件 (`flow_control_receiver_count`) が 1、監視する台数 (`flow_control_monitored_receiver_count`) が 1 の例です。

```mermaid
sequenceDiagram
    participant S as 送信者
    participant R1 as 受信者 1
    participant R2 as 受信者 2

    R1->>S: REPORT (seq_num = 1、ack_num = 送出上限)
    R2->>S: REPORT (seq_num = 1、ack_num = 送出上限)
    Note over S: 余裕を記録し、余裕が小さい受信者 1 を監視対象に選ぶ
    S->>R1: FLOW_CONTROL (状態 = 監視対象の一覧、受信者 1)
    S->>R2: FLOW_CONTROL (状態 = 監視対象の一覧、受信者 1)
    Note over R1: 一覧に自身が含まれる<br>最初の通知から始める
    R1->>S: FLOW_CONTROL (状態 = 解除、送出上限)
    Note over R2: 一覧に自身が含まれない<br>送出上限を送出しない
    loop flow_control_request_interval_ms (既定 200 ms) ごと
        S->>R1: FLOW_CONTROL (状態 = 監視対象の一覧)
        S->>R2: FLOW_CONTROL (状態 = 監視対象の一覧)
    end
    Note over R2: 受信の処理が遅くなった
    R2->>S: REPORT (seq_num = 1、ack_num = 小さい送出上限)
    Note over S: 受信者 2 の余裕が最も小さくなった<br>受信者 2 を監視対象に選び直し<br>受信者 1 の送出上限を破棄する
    S->>R1: FLOW_CONTROL (状態 = 監視対象の一覧、受信者 2)
    S->>R2: FLOW_CONTROL (状態 = 監視対象の一覧、受信者 2)
    Note over R1: 監視対象でなくなった<br>送出上限を知らせていない状態に戻す
    Note over R2: 監視対象になった<br>最初の通知から始める
    R2->>S: FLOW_CONTROL (状態 = 解除、送出上限)
    Note over S: 受信者 2 の送出上限で止まる
```

Figure: multicast で監視対象が選ばれ、入れ替わるシーケンス

- 監視対象でない受信者も、容量を設定していれば、占有量に基づく抑制の要求と解除を送ります。監視対象でない受信者から抑制の要求を受けた送信者は、その受信者の余裕を 0 として扱い、次の受信報告を待たずに監視対象の候補にします。
- 受信者は、一覧を `flow_control_request_lifetime_ms` (既定 1,000 ms) の間受け取らなかった場合、監視対象でなくなったものとして扱います。

## フロー制御の容量超過による切断と復帰

抑制の要求が送信者へ届くまでに届いた DATA によって、占有量が容量を超える場合の動作です。

```mermaid
sequenceDiagram
    participant S as 送信者
    participant R as 受信者
    participant RApp as 受信側の利用側

    S->>R: DATA
    Note over R: 登録すると占有量が容量を超える
    Note over R: DATA を破棄する<br>受信の状態を破棄する
    R->>RApp: 登録済みの POTR_EVENT_DATA
    R->>RApp: POTR_EVENT_DISCONNECTED
    S->>R: DATA / PING
    Note over R: 占有量が解除のしきい値を下回るまで破棄し<br>新しい通信として採用しない<br>(TCP は接続を確立しない)
    Note over R: 占有量が解除のしきい値を下回った
    S->>R: DATA / PING
    Note over R: 新しい通信として採用する
    R->>RApp: POTR_EVENT_CONNECTED
```

Figure: フロー制御の容量超過による切断と復帰のシーケンス

片方向 UDP の受信者は、採用しない間も抑制の要求の再送を続けます。

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

Figure: close 呼び出しと登録済み通知の処理シーケンス
