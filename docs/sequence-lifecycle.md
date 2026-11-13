# サービスの開始と終了のシーケンス

[通信シーケンスの一覧](sequence.md) へ戻ります。

## サービス開始 (送信者)

`potr_service_open()` を SENDER として呼び出したときの内部処理です。

```mermaid
sequenceDiagram
    autonumber
    participant APP as アプリ
    participant OPEN as potr_service_open

    APP->>+OPEN: potr_service_open(&global, &service,<br>POTR_ROLE_SENDER, NULL, &handle)

    OPEN->>OPEN: 構成構造体の検証
    OPEN->>OPEN: セッション識別子生成<br>(session_id + 現在時刻)
    OPEN->>OPEN: UDP ソケット作成・bind<br>(src_addr, src_port)
    OPEN->>OPEN: 送信キュー初期化
    OPEN->>OPEN: 送信ウィンドウ初期化

    create participant RT as 受信スレッド
    OPEN->>RT: 受信スレッド起動
    create participant ST as 送信スレッド
    OPEN->>ST: 送信スレッド起動
    create participant HT as ヘルス チェック スレッド
    OPEN->>HT: ヘルス チェック スレッド起動<br>(health_interval_ms > 0 のみ)

    OPEN-->>-APP: POTR_OK, *handle

    Note over RT: NACK/REJECT/FIN を<br>待機するポーリングループ
    Note over ST: 送信キューを<br>待機するポーリングループ
    Note over HT: 次回 PING 送信時刻を<br>計算してスリープ
```

CodeBlock: サービス開始 (送信者)

## サービス開始 (受信者)

`potr_service_open()` を RECEIVER として呼び出したときの内部処理です。

```mermaid
sequenceDiagram
    autonumber
    participant APP as アプリ
    participant OPEN as potr_service_open

    APP->>+OPEN: potr_service_open(&global, &service,<br>POTR_ROLE_RECEIVER, callback, &handle)

    OPEN->>OPEN: 構成構造体の検証
    OPEN->>OPEN: UDP ソケット作成・bind<br>(dst_addr, dst_port)
    OPEN->>OPEN: マルチキャスト時:<br>グループ参加
    OPEN->>OPEN: 受信ウィンドウ初期化

    create participant RT as 受信スレッド
    OPEN->>RT: 受信スレッド起動

    OPEN-->>-APP: POTR_OK, *handle

    Note over RT: DATA/PING/FIN を<br>待機するポーリングループ<br>ヘルス チェック タイムアウト監視
```

CodeBlock: サービス開始 (受信者)

## サービス終了 (potr_service_close)

`potr_service_close()` による正常終了シーケンスです。

### 送信者側の終了 (DATA/FIN が順序通りに到着する場合)

```mermaid
sequenceDiagram
    autonumber
    participant SAPP as アプリ<br>(送信側)
    participant CLOSE as potr_service_close
    participant ST as 送信スレッド
    participant HT as ヘルス チェック<br>スレッド
    participant RT as 受信スレッド
    participant UDP as UDP
    participant RRT as 受信スレッド<br>(受信者)
    participant RAPP as アプリ<br>(受信側)

    SAPP->>+CLOSE: potr_service_close(handle)

    CLOSE->>HT: 停止シグナル
    CLOSE->>CLOSE: 送信キュー drain 完了待機

    CLOSE->>UDP: FIN パケット送信<br>(全パス, DATA送信済みなら FIN_TARGET_VALID + ack_num=send_window.next_seq)

    Note over UDP: DATA と FIN が順序通りに到着する場合
    UDP->>RRT: DATA[seq=N] 受信 → 配信
    UDP->>RRT: FIN[target_valid, ack_num=N+1] 受信
    RRT->>RRT: recv_window.next_seq == N+1<br>(追い付き済み)
    RRT->>RAPP: callback(POTR_EVENT_DISCONNECTED)
    RRT->>RRT: peer_session_known = 0<br>recv_window リセット

    CLOSE->>RT: 停止シグナル

    CLOSE->>CLOSE: 各スレッドの終了を待機
    CLOSE->>CLOSE: ソケット・ウィンドウ・<br>キュー等のリソース解放

    CLOSE-->>-SAPP: POTR_OK

    Note over SAPP: handle は以後使用不可
```

CodeBlock: 正常終了 (送信者側)

### 送信者側の終了 (FIN が DATA より先に到着する場合)

UDP の到達順序は保証されないため、FIN が最後の DATA より先に受信側へ到着する場合があります。  
受信側は `FIN.ack_num` を参照して DATA の到着を待機します。

```mermaid
sequenceDiagram
    autonumber
    participant ST as 送信スレッド
    participant SUDP as UDP<br>(送信側)
    participant RUDP as UDP<br>(受信側)
    participant RRT as 受信スレッド<br>(受信者)
    participant RAPP as アプリ<br>(受信側)

    ST->>SUDP: DATA[seq=N] 送信
    ST->>SUDP: FIN[target_valid, ack_num=N+1] 送信

    Note over RUDP: UDP の到着順が逆転

    SUDP->>RUDP: FIN[target_valid, ack_num=N+1] 先着
    RRT->>RRT: recv_window.next_seq != N+1<br>→ pending_fin = true<br>  fin_target_seq = N+1

    SUDP->>RUDP: DATA[seq=N] 後着
    RRT->>RRT: potr_internal_window_recv_push(seq=N)<br>→ potr_internal_window_recv_pop()<br>→ 配信
    RRT->>RAPP: callback(POTR_EVENT_DATA, ...)
    RRT->>RRT: recv_window.next_seq == N+1<br>→ pending_fin 解消
    RRT->>RAPP: callback(POTR_EVENT_DISCONNECTED)
    RRT->>RRT: peer_session_known = 0<br>recv_window リセット

    Note over ST,RRT: wrap 後は FIN[target_valid, ack_num=0] も通常の有効 target
```

CodeBlock: 正常終了 (FIN が DATA より先に到着する場合)

### 受信者側の終了

```mermaid
sequenceDiagram
    autonumber
    participant RAPP as アプリ<br>(受信側)
    participant CLOSE as potr_service_close
    participant RT as 受信スレッド

    RAPP->>+CLOSE: potr_service_close(handle)

    CLOSE->>RT: 停止シグナル
    CLOSE->>CLOSE: 受信スレッドの終了を待機

    Note over RAPP: 受信者側の potr_service_close() は<br>送信者への通知なし<br>DISCONNECTED も発火しない

    CLOSE->>CLOSE: ソケット・ウィンドウ等の<br>リソース解放

    CLOSE-->>-RAPP: POTR_OK
```

CodeBlock: 正常終了 (受信者側)

## 補足: 接続状態の遷移

```mermaid
stateDiagram-v2
    [*] --> 未接続 : サービス開始直後<br>(health_alive = 0)

    未接続 --> 疎通中 : 片方向: 有効な PING / DATA を受信<br>双方向: 接続成立条件を満たす PING を受信<br>→ POTR_EVENT_CONNECTED 発火<br>(health_alive = 1)

    疎通中 --> 未接続 : タイムアウト検知<br>(health_timeout_ms 経過)<br>→ POTR_EVENT_DISCONNECTED 発火
    疎通中 --> 未接続 : FIN 受信<br>→ POTR_EVENT_DISCONNECTED 発火
    疎通中 --> 未接続 : REJECT 受信 (通常モード)<br>→ POTR_EVENT_DISCONNECTED 発火
    疎通中 --> 未接続 : ギャップ検出 (RAW モード)<br>→ POTR_EVENT_DISCONNECTED 発火

    未接続 --> [*] : potr_service_close()<br>(DISCONNECTED 発火なし)
    疎通中 --> [*] : potr_service_close()<br>(DISCONNECTED 発火なし)

    note right of 疎通中
        health_timeout_ms = 0 の場合
        タイムアウトは発生しない
    end note

    note left of 疎通中
        reorder_timeout_ms > 0 の場合:
        ギャップ検出直後は NACK (通常モード) または
        DISCONNECTED (RAW モード) を保留。
        タイムアウト後または欠番充足で解消。
    end note
```

CodeBlock: 接続状態遷移 (受信者側 health_alive フラグ)

## 開始失敗の巻き戻しと停止後の共通解放

通常終了では、FIN 送信、FIN_ACK 待機、送信キュー排出と各スレッドの停止を通信方式ごとの手順で実施します。  
使用する全スレッドの終了待ちを完了してから、コンテキストの共通資源を解放します。  
共通解放処理は通信の終了手順を引き受けません。

開始失敗では FIN 送信や FIN_ACK 待機を行わず、生成に成功した資源だけを巻き戻します。  
各初期化処理は、失敗して戻る前に自身が生成済みの資源を回収します。  
起動済みスレッドがある場合は、そのスレッドの終了待ちを完了してから共有するキュー、同期資源とバッファーを解放します。

TCP の複数パス起動で途中のスレッド生成が失敗した場合は、起動成功分に停止を要求し、条件変数の起床とソケットの shutdown により待機を解除します。  
生成済みの接続管理スレッドと、それらが管理する受信・ヘルス監視・共有送信スレッドの終了待ち後に、受信者用の先読みバッファーとセッション確立ロックを回収します。  
最初のスレッド生成に失敗した場合やスレッド起動前の準備失敗でも、生成済みの受信者用準備資源を回収します。  
稼働フラグが 0 であることだけを理由に、生成済みハンドルの終了待ちや準備資源の回収を省略しません。
