# UDP 双方向通信のシーケンス

[通信シーケンスの一覧](sequence.md) へ戻ります。

## unicast_bidir 1:1 双方向通信

`POTR_TYPE_UNICAST_BIDIR` における双方向データ通信のシーケンスです。  
両端が独立したセッションを持ち、それぞれがデータ送受信・NACK・ヘルス チェックを行います。

```mermaid
sequenceDiagram
    autonumber
    participant A as Side A<br>(POTR_ROLE_SENDER)
    participant B as Side B<br>(POTR_ROLE_RECEIVER)

    rect rgb(240, 240, 240)
        Note over A,B: サービス開始
    end
    Note over A: potr_service_open()<br>bind(src_addr=A, src_port=PA)
    Note over B: potr_service_open()<br>bind(src_addr=B, src_port=PB)

    rect rgb(240, 240, 240)
        Note over A,B: A → B データ送信
    end
    A->>B: DATA (session=S_A, seq=0)
    Note over B: peer_session_known = false<br>→ セッション S_A 採用<br>DATA はウィンドウに入るが CONNECTED はまだ発火しない

    A->>B: DATA (session=S_A, seq=1)
    Note over B: POTR_EVENT_DATA

    rect rgb(240, 240, 240)
        Note over A,B: B → A データ送信
    end
    B->>A: DATA (session=S_B, seq=0)
    Note over A: peer_session_known = false<br>→ セッション S_B 採用<br>DATA はウィンドウに入るが CONNECTED はまだ発火しない

    rect rgb(240, 240, 240)
        Note over A,B: 双方向データ
    end
    A->>B: DATA (session=S_A, seq=2)
    B->>A: DATA (session=S_B, seq=1)
    Note over A,B: POTR_EVENT_DATA (各端で独立して発火)

    rect rgb(240, 240, 240)
        Note over A,B: パケット欠落と再送
    end
    A->>B: DATA (session=S_A, seq=3)
    A--xB: DATA (session=S_A, seq=4) [lost]
    A->>B: DATA (session=S_A, seq=5)
    Note over B: seq=4 の欠落検出
    B->>A: NACK (ack_num=4)
    A->>B: DATA (session=S_A, seq=4) [retransmit]
    Note over B: POTR_EVENT_DATA (seq=4, 5 の順)

    rect rgb(240, 240, 240)
        Note over A,B: ヘルス チェック (対称)
    end
    A->>B: 定周期 PING (session=S_A, seq_num=N,<br>payload=UNDEFINED)
    Note over B: path_ping_state を NORMAL に更新
    B->>A: 割り込み PING (session=S_B, seq_num=M,<br>payload に NORMAL を含む)
    Note over A: remote_path_ping_state に NORMAL を確認<br>→ CONNECTED を早期発火

    B->>A: 定周期 PING (session=S_B, seq_num=P)
    Note over A: path_ping_state を NORMAL に更新
    A->>B: 割り込み PING (session=S_A, seq_num=Q,<br>payload に NORMAL を含む)
    Note over B: remote_path_ping_state に NORMAL を確認
```

Figure: unicast_bidir 1:1 双方向通信シーケンス

## unicast_bidir N:1 サーバーでの接続と送受信

`POTR_TYPE_UNICAST_BIDIR` を N:1 モードで開いたサーバーが、新規クライアントを受け付けて `peer_id` ごとに通信するシーケンスです。

```mermaid
sequenceDiagram
    autonumber
    participant CA as Client A
    participant S as Server<br>(RECEIVER / N:1)
    participant APP as アプリ

    Note over S: potr_service_open()<br>src_addr 省略 → N:1 モード<br>bind(dst_addr, dst_port)

    CA->>S: DATA (client session=C_A, seq=0)
    S->>S: session triplet で未知ピア判定<br>peer table に新規登録<br>peer_id=1 を払い出し
    S->>APP: callback(service_id, 1, POTR_EVENT_CONNECTED, NULL, 0)
    S->>APP: callback(service_id, 1, POTR_EVENT_DATA, data, len)

    APP->>S: potr_service_send(handle, 1, reply, len, 0)
    S->>CA: DATA (server session=S_1, seq=0)

    APP->>S: potr_service_send(handle, POTR_PEER_ALL, notice, len, 0)
    S->>CA: DATA (peer_id=1 向け送信)
```

Figure: unicast_bidir N:1 サーバーでの接続と送受信

## unicast_bidir N:1 サーバーでの切断

サーバーは FIN 受信、`potr_peer_disconnect()`、またはヘルス チェック タイムアウトによりピア単位で切断を処理します。

```mermaid
sequenceDiagram
    autonumber
    participant CA as Client A
    participant S as Server
    participant APP as アプリ

    CA->>S: FIN
    S->>APP: callback(service_id, 1, POTR_EVENT_DISCONNECTED, NULL, 0)
    S->>S: peer table から peer_id=1 を削除

    APP->>S: potr_peer_disconnect(handle, 2)
    S->>CA: FIN
    S->>APP: callback(service_id, 2, POTR_EVENT_DISCONNECTED, NULL, 0)
    S->>S: peer table から peer_id=2 を削除
```

Figure: unicast_bidir N:1 サーバーでの切断

## unicast_bidir ヘルス チェック タイムアウトによる切断検知

`POTR_TYPE_UNICAST_BIDIR` において、相手側が停止した場合の切断検知シーケンスです。1:1 モードでは相手端単位、N:1 モードでは各 `peer_id` 単位で `last_recv_tv_sec` を監視し、`health_timeout_ms` 超過で切断を検知します。

```mermaid
sequenceDiagram
    autonumber
    participant A as Side A
    participant B as Side B<br>(停止)

    Note over A,B: 通信中

    A->>B: PING (ack_num=0, seq_num=N)
    Note over B: アプリケーションが停止<br>(パケットが一切到着しなくなる)
    Note over A: health_timeout_ms 経過<br>last_recv_tv_sec が更新されない<br>check_health_timeout() → DISCONNECTED
    Note over A: POTR_EVENT_DISCONNECTED 発火
```

Figure: unicast_bidir ヘルス チェック タイムアウトによる切断検知
