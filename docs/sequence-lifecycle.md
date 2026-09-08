# サービスの開始と終了のシーケンス

[通信シーケンスの一覧](sequence.md) へ戻ります。

## サービス開始 (送信者)

`potr_service_open()` を SENDER として呼び出したときの内部処理です。

```plantuml
@startuml サービス開始 (送信者)
caption サービス開始 (送信者)

participant "アプリ" as APP
participant "potr_service_open" as OPEN
participant "受信スレッド" as RT
participant "送信スレッド" as ST
participant "ヘルスチェックスレッド" as HT

APP -> OPEN: potr_service_open(&global, &service,\nPOTR_ROLE_SENDER, NULL, &handle)

activate OPEN
OPEN -> OPEN: 設定構造体の検証
OPEN -> OPEN: セッション識別子生成\n(session_id + 現在時刻)
OPEN -> OPEN: UDP ソケット作成・bind\n(src_addr, src_port)
OPEN -> OPEN: 送信キュー初期化
OPEN -> OPEN: 送信ウィンドウ初期化

OPEN -> RT**: 受信スレッド起動
OPEN -> ST**: 送信スレッド起動
OPEN -> HT**: ヘルスチェックスレッド起動\n(health_interval_ms > 0 のみ)

OPEN --> APP: POTR_OK, *handle
deactivate OPEN

activate RT
activate ST
activate HT

note over RT: NACK/REJECT/FIN を\n待機するポーリングループ
note over ST: 送信キューを\n待機するポーリングループ
note over HT: 次回 PING 送信時刻を\n計算してスリープ

@enduml
```

## サービス開始 (受信者)

`potr_service_open()` を RECEIVER として呼び出したときの内部処理です。

```plantuml
@startuml サービス開始 (受信者)
caption サービス開始 (受信者)

participant "アプリ" as APP
participant "potr_service_open" as OPEN
participant "受信スレッド" as RT

APP -> OPEN: potr_service_open(&global, &service,\nPOTR_ROLE_RECEIVER, callback, &handle)

activate OPEN
OPEN -> OPEN: 設定構造体の検証
OPEN -> OPEN: UDP ソケット作成・bind\n(dst_addr, dst_port)
OPEN -> OPEN: マルチキャスト時:\nグループ参加
OPEN -> OPEN: 受信ウィンドウ初期化

OPEN -> RT**: 受信スレッド起動

OPEN --> APP: POTR_OK, *handle
deactivate OPEN

activate RT
note over RT: DATA/PING/FIN を\n待機するポーリングループ\nヘルスチェックタイムアウト監視

@enduml
```

## サービス終了 (potr_service_close)

`potr_service_close()` による正常終了シーケンスです。

### 送信者側の終了 (DATA/FIN が順序通りに届く場合)

```plantuml
@startuml 正常終了 (送信者側)
caption 正常終了 (送信者側)

participant "アプリ\n(送信側)" as SAPP
participant "potr_service_close" as CLOSE
participant "送信スレッド" as ST
participant "ヘルスチェック\nスレッド" as HT
participant "受信スレッド" as RT
participant "UDP" as UDP
participant "受信スレッド\n(受信者)" as RRT
participant "アプリ\n(受信側)" as RAPP

SAPP -> CLOSE: potr_service_close(handle)
activate CLOSE

CLOSE -> HT: 停止シグナル
CLOSE -> CLOSE: 送信キュー drain 完了待機

CLOSE -> UDP: FIN パケット送信\n(全パス, DATA送信済みなら FIN_TARGET_VALID + ack_num=send_window.next_seq)

note over UDP: DATA と FIN が順序通りに届く場合
UDP -> RRT: DATA[seq=N] 受信 → 配信
UDP -> RRT: FIN[target_valid, ack_num=N+1] 受信
RRT -> RRT: recv_window.next_seq == N+1\n(追い付き済み)
RRT -> RAPP: callback(POTR_EVENT_DISCONNECTED)
RRT -> RRT: peer_session_known = 0\nrecv_window リセット

CLOSE -> RT: 停止シグナル

CLOSE -> CLOSE: 各スレッドの終了を待機
CLOSE -> CLOSE: ソケット・ウィンドウ・\nキュー等のリソース解放

CLOSE --> SAPP: POTR_OK
deactivate CLOSE

note over SAPP: handle は以後使用不可

@enduml
```

### 送信者側の終了 (FIN が DATA より先に届く場合)

UDP の到達順序は保証されないため、FIN が最後の DATA より先に受信側へ届く場合があります。  
受信側は `FIN.ack_num` を参照して DATA の到着を待機します。

```plantuml
@startuml 正常終了 FIN pending
caption 正常終了 (FIN が DATA より先に届く場合)

participant "送信スレッド" as ST
participant "UDP\n(送信側)" as SUDP
participant "UDP\n(受信側)" as RUDP
participant "受信スレッド\n(受信者)" as RRT
participant "アプリ\n(受信側)" as RAPP

ST -> SUDP: DATA[seq=N] 送信
ST -> SUDP: FIN[target_valid, ack_num=N+1] 送信

note over RUDP: UDP の到着順が逆転

SUDP -> RUDP: FIN[target_valid, ack_num=N+1] 先着
RRT -> RRT: recv_window.next_seq != N+1\n→ pending_fin = true\n  fin_target_seq = N+1

SUDP -> RUDP: DATA[seq=N] 後着
RRT -> RRT: potr_internal_window_recv_push(seq=N)\n→ potr_internal_window_recv_pop()\n→ 配信
RRT -> RAPP: callback(POTR_EVENT_DATA, ...)
RRT -> RRT: recv_window.next_seq == N+1\n→ pending_fin 解消
RRT -> RAPP: callback(POTR_EVENT_DISCONNECTED)
RRT -> RRT: peer_session_known = 0\nrecv_window リセット

note over ST,RRT: wrap 後は FIN[target_valid, ack_num=0] も通常の有効 target

@enduml
```

### 受信者側の終了

```plantuml
@startuml 正常終了 (受信者側)
caption 正常終了 (受信者側)

participant "アプリ\n(受信側)" as RAPP
participant "potr_service_close" as CLOSE
participant "受信スレッド" as RT

RAPP -> CLOSE: potr_service_close(handle)
activate CLOSE

CLOSE -> RT: 停止シグナル
CLOSE -> CLOSE: 受信スレッドの終了を待機

note over RAPP: 受信者側の potr_service_close() は\n送信者への通知なし\nDISCONNECTED も発火しない

CLOSE -> CLOSE: ソケット・ウィンドウ等の\nリソース解放

CLOSE --> RAPP: POTR_OK
deactivate CLOSE

@enduml
```

## 補足: 接続状態の遷移

```plantuml
@startuml 接続状態遷移 (受信者側 health_alive フラグ)
caption 接続状態遷移 (受信者側 health_alive フラグ)

[*] -r-> 未接続 : サービス開始直後\n(health_alive = 0)

未接続 ---> 疎通中 : 片方向: 有効な PING / DATA を受信\n双方向: 接続成立条件を満たす PING を受信\n→ POTR_EVENT_CONNECTED 発火\n(health_alive = 1)

疎通中 ---> 未接続 : タイムアウト検知\n (health_timeout_ms 経過) \n→ POTR_EVENT_DISCONNECTED 発火

疎通中 --> 未接続 : FIN 受信\n→ POTR_EVENT_DISCONNECTED 発火

疎通中 --> 未接続 : REJECT 受信 (通常モード)\n→ POTR_EVENT_DISCONNECTED 発火

疎通中 --> 未接続 : ギャップ検出 (RAW モード)\n→ POTR_EVENT_DISCONNECTED 発火

未接続 --> [*] : potr_service_close()\n (DISCONNECTED 発火なし)
疎通中 --> [*] : potr_service_close()\n (DISCONNECTED 発火なし)

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

@enduml
```
