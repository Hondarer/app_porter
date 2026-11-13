# UDP のヘルス チェックのシーケンス

[通信シーケンスの一覧](sequence.md) へ戻ります。

## 受信監視は既存の受信ループから呼ぶ

`thread_recv_health.c` は、UDP の受信タイムアウト監視と、TCP の受信状態変化に伴う PING 起床判断を担当します。  
`potr_recv_thread.c` の既存の受信ループから呼び、監視専用のスレッドは追加しません。  
N:1 では `peers_mutex` を保持してパス状態を同期し、期限に達したピアを切断・解放します。  
必要な health 起床は、ロックを解放した後に行います。  
1:1 ではパスとサービスの期限を確認し、サービス切断時にセッション、FIN と受信ウィンドウを初期化します。

TCP の受信状態が変化した場合は、定周期 health が稼働していれば全パスの health を起床し、それ以外は即時 PING を送信します。  
この判断も、状態を更新した受信スレッドから呼びます。

## ヘルス チェック (正常疎通)

ヘルス チェックが有効な場合の PING 送信です。片方向 type 1-6 は open 直後の即時 PING を行わず、最後の `PING` または有効 `DATA` 送信から `health_interval_ms` 経過したときだけ PING を送信します。双方向 UDP では従来どおり定周期 PING と、`path_ping_state[]` 変化時の割り込み PING を送出します。双方向 UDP はこの PING 往復で `CONNECTED` するため、実効 `health_interval_ms = 0` のままでは接続確立しません。

```mermaid
sequenceDiagram
    autonumber
    participant HT as ヘルス チェック<br>スレッド
    participant UDP as UDP
    participant RRT as 受信スレッド<br>(受信者)

    Note over HT: health_interval_ms = 3000ms<br>片方向は初回も 3000ms 待機

    HT->>HT: 最後の PING / DATA 送信から<br>3000ms 待機
    Note over HT: send_window.next_seq を読み取る<br>(ウィンドウには登録しない)
    HT->>UDP: PING[seq=N] 送信 (全パス)

    UDP->>RRT: PING[seq=N] 受信
    RRT->>RRT: last_recv_tv を更新<br>(タイムアウトカウンタリセット)
    RRT->>RRT: notify_health_alive()<br>(health_alive=0 のとき CONNECTED 発火)
    RRT->>RRT: next_seq〜N-1 を全スキャン<br>欠番を一括 NACK する
    Note over RRT: 欠番なければ返信なし

    HT->>HT: DATA が送信されたら期限を延長する<br>送信が停止したら 3000ms 後に PING

    Note over HT, UDP: 片方向は recent DATA により PING を抑止する<br>双方向系は従来どおり定周期 + 割り込み送信
```

Figure: ヘルス チェック (正常疎通)

## ヘルス チェック タイムアウト

片方向 type 1-6 で、最後の有効な `PING` / `DATA` から一定時間パケットが到着しなくなった場合の切断検知と復帰です。

```mermaid
sequenceDiagram
    autonumber
    participant S as 送信者
    participant UDP as UDP
    participant RRT as 受信スレッド<br>(受信者)
    participant RAPP as アプリ<br>(受信側)

    S->>UDP: DATA 送信 (正常稼働中)
    UDP->>RRT: DATA 受信 → last_recv_tv 更新
    RRT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DATA, ...)

    Note over S, UDP: ここでネットワーク断が発生

    S--xUDP: PING 送信 (到達せず)
    S--xUDP: PING 送信 (到達せず)

    Note over RRT: health_timeout_ms = 10000ms

    RRT->>RRT: last_recv_tv から 10000ms 経過を検出

    RRT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_DISCONNECTED, NULL, 0)
    RRT->>RRT: health_alive = 0<br>peer_session_known = 0<br>recv_window リセット

    Note over S, UDP: ネットワーク復旧

    S->>UDP: DATA または PING 送信
    UDP->>RRT: パケット受信
    RRT->>RRT: セッション採用<br>recv_window を pkt.seq_num で初期化
    RRT->>RRT: health_alive = 1

    RRT->>RAPP: callback(service_id, POTR_PEER_NA, POTR_EVENT_CONNECTED, NULL, 0)
```

Figure: ヘルス チェック タイムアウト
