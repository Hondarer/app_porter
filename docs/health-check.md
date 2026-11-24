# PING ヘルス チェック設計まとめ

porter フレームワークにおける potr_type ごとの PING 送出ロジック、マルチパスごとの振る舞い、タイムアウト検出方式を整理します。PONG (PING 応答) は存在しません。片方向 type 1-6 は有効な `PING` / `DATA` 受信をヘルス チェック信号として扱い、送信側は「最後の PING または有効 DATA 送信」から `health_interval_ms` 経過時だけ PING を送信します。双方向 type 7-10 は PING 応答ベースで path logical を判定し、service / peer の `CONNECTED` はその OR で決まります。

## 概要

ヘルス チェックは PING パケット (`POTR_FLAG_PING`) の送出、双方向系における通信経路オープン時の割り込み送出、双方向系におけるパス受信状態変化時の割り込み送出、およびタイムアウト超過時の切断検出から成ります。非 TCP は 1 サービスにつき 1 本の health スレッドを使い、type 8 はその 1 本が全 peer を順に処理します。TCP は path ごとに 1 本の tcp_health スレッドを使います。potr_type によって送出側と検出側の担当が異なります。

## potr_type ごとの振る舞い

次の表は各 potr_type の概要をまとめたものです。

| potr_type | 値 | PING 送出 | タイムアウト検出 |
|---|---|---|---|
| UNICAST_RAW | 1 | SENDER (health スレッド) | RECEIVER 側 (有効な PING / DATA の受信タイムアウト) |
| MULTICAST_RAW | 2 | SENDER (health スレッド) | RECEIVER 側 (有効な PING / DATA の受信タイムアウト) |
| BROADCAST_RAW | 3 | SENDER (health スレッド) | RECEIVER 側 (有効な PING / DATA の受信タイムアウト) |
| UNICAST | 4 | SENDER (health スレッド) | RECEIVER 側 (有効な PING / DATA の受信タイムアウト) |
| MULTICAST | 5 | SENDER (health スレッド) | RECEIVER 側 (有効な PING / DATA の受信タイムアウト) |
| BROADCAST | 6 | SENDER (health スレッド) | RECEIVER 側 (有効な PING / DATA の受信タイムアウト) |
| UNICAST_BIDIR | 7 | 両端 (health スレッド) | 両端 (PING 受信タイムアウト) |
| UNICAST_BIDIR_N1 | 8 | サーバー (全ピア宛) + クライアント (サーバー宛) | 両端・ピア単位 (PING 受信タイムアウト) |
| TCP | 9 | 両端 (tcp_health スレッド・パスごと) | 両端 (PING 受信タイムアウト) |
| TCP_BIDIR | 10 | 両端 (tcp_health スレッド・パスごと) | 両端 (PING 受信タイムアウト) |

Table: potr_type ごとの PING 送出とタイムアウト検出動作

## 設定パラメーター

```text
udp_health_interval_ms : UDP 系 PING 送信間隔 (ms)。0 = 無効。
udp_health_timeout_ms  : UDP 系タイムアウト (ms)。RECEIVER 側で判定。0 = 無効。
tcp_health_interval_ms : TCP 系 PING 送信間隔 (ms)。0 = 無効。
tcp_health_timeout_ms  : TCP 系 PING 受信タイムアウト (ms)。両端で判定。0 = 無効。
```

サービス定義 (`potr_service_def`) に同名フィールドを設定するとグローバル値を上書きできます。0 の場合はグローバル値を使用します。

送信間隔の実効値 (`udp_health_interval_ms`、`tcp_health_interval_ms`、サービスの上書きを含む) の上限は 2,147,483,646 です。  
`tcp_health_timeout_ms` のグローバル値は、TCP の RECEIVER が接続直後の最初のパケットを待つ時間 (3 倍) にも使うため、上限は 715,827,882 です。サービスの上書きは、この待機に影響しません。  
上限を超える値は、値を使う種別と役割でサービスを開く処理が設定エラー (`POTR_ERR_INVALID_CONFIG`) を返します。詳細は [設定ファイル](config.md) の「設定エラーの条件」を参照してください。

## PING パケット構造

`flags` フィールドに `POTR_FLAG_PING` をセットし、`ack_num` は常に 0 とします (双方向 UDP の応答では要求の `seq_num` をセットします)。

実装箇所は `packet.c` の `potr_internal_packet_build_ping()` です。

### PING ペイロード (パス受信状態)

PING パケットのペイロードには自端の各パス PING 受信状態を `POTR_MAX_PATH` (= 8) バイトで格納します。各バイトがパス インデックス順に対応します。

| 値 | 定数 | 意味 |
|---|---|---|
| `0` | `POTR_PING_STATE_UNDEFINED` | 不定 (片方向通信 / まだ有効な PING / DATA 未受信) |
| `1` | `POTR_PING_STATE_NORMAL` | 正常 (ヘルス チェック信号を継続受信中) |
| `2` | `POTR_PING_STATE_ABNORMAL` | 異常 (PING 途絶・タイムアウト) |

Table: PING ペイロードにおけるパス受信状態の値と定義

片方向通信 (type 1-6) では送信側が返送用 PING を持たないため、送出される PING ペイロードは全バイト `UNDEFINED` のままです。一方、受信側ローカルの `path_ping_state[]` は有効な `PING` または `DATA` を受けると `NORMAL` に更新されます。双方向通信 (type 7-10) は実際の PING 受信状態を格納します。

受信側は PING 受信時にペイロードを `remote_path_ping_state[]` に保存します。これにより双方向通信の両端が相手の往復疎通状態を把握できます。

`path_ping_state[]` の更新タイミングは次のとおりです。

- `thread_recv_update_path_health()` (`thread/thread_recv_session.c`): 片方向 type 1-6 では有効な PING / DATA 受信時、双方向 type 7 では PING 受信時に `POTR_PING_STATE_NORMAL` に設定します。
- `thread_recv_update_path_health()` (`thread/thread_recv_session.c`): type 8 で PING 受信時に `POTR_PING_STATE_NORMAL` に設定します。
- `thread_recv_health_check_service()` / `thread_recv_health_check_peers()` (`thread/thread_recv_health.c`): パス タイムアウト時に `POTR_PING_STATE_ABNORMAL` に設定します。
- TCP PING 受信時: `POTR_PING_STATE_NORMAL` に設定します。
- TCP PING タイムアウト時: `POTR_PING_STATE_ABNORMAL` に設定します。
- セッション DISCONNECTED 発火時 / TCP 切断時: 全パスを `POTR_PING_STATE_UNDEFINED` にリセットします。
- 双方向 UDP (type 7, 8) の有効な REJECT / 通知キューの容量超過時: 全パスを `POTR_PING_STATE_UNDEFINED` にリセットします。

双方向 UDP (type 7, 8) では、PING の受信、状態を変える有効な REJECT、通知キューの容量超過で `path_ping_state[]` が変わると health スレッドを起こし、更新後の状態を含む割り込み PING を送ります。状態が変わらない REJECT と FIN の発火では、この起床を要求しません。TCP の PING 受信、PING タイムアウト、切断には既存の TCP 用の経路を使います。

## 回線確立 (CONNECTED) 検出

`POTR_EVENT_CONNECTED` の発火条件は通信形態によって異なります。

| 通信形態 | CONNECTED の発火条件 |
|---|---|
| 片方向 (type 1-6) | いずれかの path logical が 0->1 になったとき |
| 双方向 UDP (type 7, 8) | `path_ping_state[k] == NORMAL` かつ `remote_path_ping_state[k] == NORMAL` の path が 1 本以上になったとき |
| TCP (type 9, 10) | `tcp_conn_fd[k]` 有効かつ `path_ping_state[k] == NORMAL` かつ `remote_path_ping_state[k] == NORMAL` の path が 1 本以上になったとき |

Table: 通信形態ごとの CONNECTED イベント発火条件

双方向 UDP では `remote_path_ping_state[k] == POTR_PING_STATE_NORMAL` が一つ以上存在するときに CONNECTED を発火します。これは「相手端が自端からの PING を正常受信済みである」ことを意味し、往復疎通が確認できた時点で CONNECTED となります。

TCP はコネクション確立 (accept / connect 完了) だけでは CONNECTED を発火しません。接続直後に送る bootstrap PING に対する応答 PING を受信し、そのペイロード中の `remote_path_ping_state[]` に `POTR_PING_STATE_NORMAL` を確認した時点で CONNECTED となります。つまり TCP 双方向でも「自端が PING を送信し、相手端がその受信結果を返信し、それを受信する」往復完了が初回 CONNECTED の条件です。

片方向 type 1-6 では、PING ヘルス チェックが無効 (最終的な `health_interval_ms = 0`) でも有効な `DATA` を受信すれば CONNECTED が発火します。open 直後の即時 PING は送信しないため、初回 CONNECTED は「最初の有効 DATA」または「最初の通常 PING」受信時に成立します。双方向 UDP (type 7, 8) は PING が送出されないと CONNECTED が発火しません。

双方向 UDP では、以下の順序でハンドシェイクが完了して CONNECTED が発火します。

1. 自端が PING 送出を開始する (ペイロードは全パス `UNDEFINED`)。通信経路オープン時は最初のスリープをスキップして即座に送出するため、最大 `health_interval_ms` を待たずに相手端へ PING が到着します。
2. 相手端が自端の PING を受信し、`path_ping_state` を `NORMAL` に更新します。
3. 相手端が `path_ping_state` 変化に反応して割り込み PING を送出する (変化がなければ次の定周期 PING まで待つ)。
4. 自端がその PING を受信し、ペイロードに `NORMAL` を確認して CONNECTED を発火します。

双方向 UDP では、この往復 PING が接続確立そのものです。実効 `health_interval_ms = 0`、または `udp_health_interval_ms = 0` かつサービス側で `health_interval_ms` を上書きしない構成では初回 PING が送信されないため、CONNECTED は成立しません。

片方向通信では手順 2-3 に相当する往復が不要で、有効な `PING` または `DATA` の受信で即 CONNECTED となります。

実装箇所は `thread/potr_recv_thread.c` / `thread/potr_connect_thread.c` の path logical 同期処理、および `potr_path_event.c` です。

## 接続状態別の実装

本節では、各通信種別について「CONNECTED 前」「CONNECTED 後」の実装を整理します。

- CONNECTED 前: `POTR_EVENT_DATA` は発火しません。
- CONNECTED 後: `DATA` を配送できます。
- CONNECTED の定義: 片方向 (type 1-6) はいずれかのパスで有効な `PING` または `DATA` を受信した時点、双方向 UDP (type 7-8) は受信 `PING` ペイロード内の `remote_path_ping_state[]` に `POTR_PING_STATE_NORMAL` を確認した時点、TCP (type 9-10) は bootstrap PING に対する応答 `PING` のペイロード内に `POTR_PING_STATE_NORMAL` を確認した時点

### type 1-6: UNICAST_RAW / MULTICAST_RAW / BROADCAST_RAW / UNICAST / MULTICAST / BROADCAST

| 状態 | 実装 |
|---|---|
| CONNECTED 前 | `PING` は health スレッドが「最後の PING または有効 DATA 送信」から `health_interval_ms` 経過したときだけ送信します。open 直後の即時 PING はありません。`potr_service_send()` は通常どおり `DATA` を送信キューへ格納します。受信側は最初の有効な `PING` または `DATA` を受理した時点で `health_alive == 1` になり、`POTR_EVENT_CONNECTED` を発火します。 |
| CONNECTED 後 | 受信側が `health_alive == 1` を維持して `DATA` を配送します。 |
| CONNECTED 解除 | `health_timeout_ms` 超過、`FIN` 受信、`REJECT` 受信、RAW 系のギャップ検出で `health_alive == 0` に戻り、以後は再び CONNECTED 前と同じ扱いになります。 |
| potr_event 順序 | 初回の有効 `DATA` を受理した場合も、先に `POTR_EVENT_CONNECTED` を発火してから `POTR_EVENT_DATA` を配送します。 |

Table: 片方向通信 (type 1-6) の接続状態別の実装

### type 7: UNICAST_BIDIR

| 状態 | 実装 |
|---|---|
| CONNECTED 前 | `PING` は両端が送信します。`potr_service_send()` は `health_alive == 0` の間 `POTR_ERR_DISCONNECTED` を返します。受信側は `health_alive == 0` の間 `DATA` を配送しません。 |
| CONNECTED 後 | `health_alive == 1` になり、`potr_service_send()` が成功します。受信側も `DATA` を配送します。 |
| CONNECTED 解除 | `health_timeout_ms` 超過、`FIN` 受信、状態を変える `REJECT`、通知キューの容量超過で `health_alive == 0` に戻り、以後は再び CONNECTED 前と同じ扱いになります。 |
| potr_event 順序 | `POTR_EVENT_CONNECTED` 前に `POTR_EVENT_DATA` は発火しません。 |

Table: UNICAST_BIDIR (type 7) の接続状態別の実装

### type 8: UNICAST_BIDIR_N1

| 状態 | 実装 |
|---|---|
| CONNECTED 前 | サーバー/クライアントとも `PING` を送信します。`peer_id` 指定送信は `peer->health_alive == 0` の間 `POTR_ERR_DISCONNECTED` を返します。`POTR_PEER_ALL` は接続済み peer が 0 件なら `POTR_ERR_DISCONNECTED` を返します。受信側は `peer->health_alive == 0` の間 `thread_recv_slot_deliver_payload_elem()` (`thread/thread_recv_slot.c`) で `DATA` を配送しません。未知 peer の初回 `DATA` では peer を生成せずに破棄し、初回 `PING` でのみ peer slot と session 状態を生成します。 |
| CONNECTED 後 | 対象 peer の `health_alive == 1` になり、その peer に対する送受信が有効になります。 |
| CONNECTED 解除 | ピア単位で `health_timeout_ms` 超過、`FIN` 受信、状態を変える `REJECT`、通知キューの容量超過時に `peer->health_alive == 0` となります。`FIN` とタイムアウトでは `potr_internal_peer_free()` でピアを削除し、REJECT と容量超過では受信セッション状態を破棄します。以後はその peer を未接続として扱い、再接続は再度 `PING` 起点で行います。 |
| potr_event 順序 | `POTR_EVENT_CONNECTED` 前に `POTR_EVENT_DATA` は発火しません。未知 peer の初回 `DATA` でも peer table は前進しません。 |

Table: UNICAST_BIDIR_N1 (type 8) の接続状態別の実装

### type 9-10: TCP / TCP_BIDIR

| 状態 | 実装 |
|---|---|
| CONNECTED 前 | TCP 接続確立後に各 path が bootstrap PING を送信し、その応答 `PING` の `remote_path_ping_state[]` に `POTR_PING_STATE_NORMAL` が載ると論理 CONNECTED へ遷移します。`health_interval_ms > 0` の場合のみ tcp_health スレッドが定周期 `PING` を送信します。`potr_service_send()` は `tcp_active_paths == 0` または `health_alive == 0` の間 `POTR_ERR_DISCONNECTED` を返します。受信側は `health_alive == 0` の間 `thread_recv_slot_deliver_payload_elem()` (`thread/thread_recv_slot.c`) で `DATA` を破棄します。 |
| CONNECTED 後 | `health_alive == 1` になり、`potr_service_send()` が成功します。受信側も `DATA` を配送します。送信スレッドは各 path へ `DATA` を書き込む前に、その path で相手端へ通知済みの経路状態が現在と同じかを確かめ、異なれば最新の経路状態を載せた `PING` を先に書き込みます。TCP はストリーム内の順序を保つため、相手端は `DATA` より先に `PING` を処理して CONNECTED になり、CONNECTED 前の破棄で `DATA` を失いません。 |
| CONNECTED 解除 | path ごとの PING タイムアウトや TCP 切断で `tcp_active_paths` が減少し、全 path が失われると connect スレッドが `health_alive == 0` に戻して `POTR_EVENT_DISCONNECTED` を発火します。加えて正常 close では、recv スレッドが protocol-level `FIN` を受信して最後の DATA 配送完了後に `FIN_ACK` を返信し、その直後に `POTR_EVENT_DISCONNECTED` を発火します。以後は再び CONNECTED 前と同じ扱いになり、再接続後に `PING` 交換で CONNECTED へ復帰します。 |
| potr_event 順序 | `POTR_EVENT_CONNECTED` 前に `POTR_EVENT_DATA` は発火しません。 |

Table: TCP / TCP_BIDIR (type 9-10) の接続状態別の実装

## potr_event 順序

全通信種別で、未接続中の `DATA` は最終配送段の `health_alive` または `peer->health_alive` 判定で遮断されます。

- type 1-7, 9-10: `thread_recv_slot_deliver_payload_elem()` (`thread/thread_recv_slot.c`) が `health_alive == 0` の間 `DATA` を破棄します。
- type 8: `thread_recv_slot_deliver_payload_elem()` (`thread/thread_recv_slot.c`) が `peer->health_alive == 0` の間 `DATA` を破棄します。
- したがって、アプリ コールバックとして `POTR_EVENT_CONNECTED` 前に `POTR_EVENT_DATA` は発火しません。

双方向 UDP (type 7, 8) では、通信種別とサービス上書きを解決した最終的な `health_interval_ms = 0` の場合は CONNECTED が成立しないため、`POTR_EVENT_DATA` も発火しません。

## UDP 系ヘルス チェック

### PING 送出 (health スレッド)

`potr_health_thread.c` の `health_thread_func()` が非 TCP 用の共有 health スレッドとして動作します。片方向 type 1-6 は open 直後の即時 PING を行わず、最初の PING も `health_interval_ms` 経過後に送信します。有効 DATA がその前に送信された場合は、次回期限を「最後の DATA 送信時刻 + health_interval_ms」へ後ろ倒しします。双方向 UDP (`UNICAST_BIDIR`, `UNICAST_BIDIR_N1`) では従来どおり `health_interval_ms` 周期で送信し、`path_ping_state[]` が変化した場合にも条件変数 wakeup により即時送出されます。ここで `health_interval_ms = 0` になると health スレッド自体が起動しないため、双方向 UDP の初回 CONNECTED に必要な PING 送信も始まりません。

非 N:1 モード (UNICAST_RAW / MULTICAST_RAW / BROADCAST_RAW / UNICAST / MULTICAST / BROADCAST / UNICAST_BIDIR) の処理は次のとおりです。

1. 片方向 type 1-6 は経路ごとに `max(last_ping_send_ms[path], last_valid_data_send_ms[path]) + health_interval_ms`、双方向 type 7 は定周期 `health_interval_ms` を送信期限として評価します。
2. 送信ウィンドウから `next_seq` を取得します。
3. `potr_internal_packet_build_ping()` で PING パケットを構築します。
4. 送出対象の経路それぞれで `cplat_socket_sendto()` を実行します (片方向では期限を迎えた経路、双方向では全経路)。
5. 暗号化有効時は `POTR_FLAG_ENCRYPTED` を付与して GCM 認証タグを追加します。

片方向 type 1-6 では、送信スレッドが各経路へ外側 DATA パケットを実送出した時点でその経路の最終 DATA 送信時刻を更新します (`potr_service_send()` の API 成功時ではありません)。待機グループの経路には DATA が流れないため、経路ごとに最終送信時刻を判定することで、待機グループの経路にも `health_interval_ms` ごとに PING が送出されます。

N:1 モード (UNICAST_BIDIR_N1 のサーバー側) の処理は次のとおりです。

1. ピア テーブルからアクティブな各ピアを巡回します。
2. ピアごとに `potr_internal_packet_build_ping()` でパケットを構築します。
3. ピアごとに全パスへ `cplat_socket_sendto()` を実行します。

### PING 受信時の処理 (受信スレッド)

UNICAST_BIDIR_N1 (type 8) は `thread_recv_sync_path_state()` (`thread/thread_recv_session.c`) でそのピアを alive 状態にします。ギャップがある場合は NACK を優先送出します。

片方向 UDP (type 1-6) は有効な `PING` または `DATA` を受信すると最終受信時刻を更新し、タイムアウト計測をリセットします。双方向 UDP (type 7, 8) は従来どおり PING 受信時のみ更新します。

RAW 系 (type 1, 2, 3) のギャップ検出時は NACK を送信せず `POTR_EVENT_DISCONNECTED` を発火してセッションをリセットします。UNICAST / MULTICAST / BROADCAST (type 4, 5, 6) のギャップ検出時は NACK を送出して再送を要求します。MULTICAST / BROADCAST では複数受信者の NACK が集中しないよう、送出タイミングに `reorder_timeout_ms` の 100〜200% のジッタを付加します。

### タイムアウト検出 (受信スレッド)

受信スレッド (`recv_thread_func()`) は、`cplat_socket_timed_wait_readable_multi()` から戻るたびに、受信期限を `thread_recv_health_check_service()` (1:1) または `thread_recv_health_check_peers()` (N:1) で確認し、N:1 モードでは遅いピアの猶予時間の判定 (`thread_recv_health_check_slow_peers()`) も実行して、停止の指示を読み直します。  
そのため、待機による無受信の検出の遅れと停止の遅れは、1 回の待機の上限を超えません。  
1 回の待機の上限は、次の条件を上から順に適用して決めます。

| 条件 (`recv_thread_func()` 内の適用順) | 対象となる構成 | 待機の上限 [ms] |
| --- | --- | --- |
| 受信期限の監視 | 受信者、または `unicast_bidir` で `health_timeout_ms > 0` の場合 | `max(health_timeout_ms / 3, 100)` (`health_timeout_ms < 300` のときは `100` が下限) |
| (受信期限を監視しない場合) | `health_timeout_ms == 0`、または `unicast_bidir` 以外の送信者 | `500` |
| 順序の入れ替えの待ち時間 | 受信者、または `unicast_bidir` で `reorder_timeout_ms > 0` の場合 | 上の値と `reorder_timeout_ms` の小さいほう |
| 遅いピアの猶予時間の判定 | `unicast_bidir_n1` のサーバーで、ほかのピアへの送信を妨げている遅いピアが残っている場合 (`thread_recv_health_check_slow_peers()` が返す次の判定予定時刻が現在時刻より後の場合) | 上の値と「次の判定予定時刻 − 現在時刻」の小さいほう (判定間隔の上限は `POTR_INTERNAL_FLOW_RECHECK_MS` = 50) |
| 証明書の認証 | 証明書方式 (`POTR_AUTH_CERT`) で認証する場合 | 上の値と `100` の小さいほう |
| 受信報告の送信の時刻 | 片方向通常モードの受信者で、受信報告を送る構成 (実効の `health_interval_ms` が 0 でない) であり、次の受信報告の予定時刻 (`next_report_ms`) が現在時刻より後の場合 | 上の値と「次の報告予定時刻 − 現在時刻」の小さいほう |
| `multicast` と `broadcast` の送信側の監視の時刻 | `multicast` または `broadcast` の送信者で、次の監視対象一覧の送出予定時刻 (`flow_monitor_next_ms`) が現在時刻より後の場合 (詳細は [設定仕様](config.md#multicast-と-broadcast-の監視対象) および [プロトコル仕様](protocol.md#監視対象の一覧のパケット) を参照) | 上の値と「次の送出予定時刻 − 現在時刻」の小さいほう |
| フロー制御パケットを送出する受信側の要求の間隔 | フロー制御パケットを送出する受信側 (`flow_control != NULL`。容量が 0 でない、または OS の受信バッファーに基づく送出上限を使う受信側) の場合 (詳細は [設定仕様](config.md#再送周期と有効期限) および [設定仕様](config.md#os-の受信バッファーに基づく送出上限) を参照) | 上の値と `flow_control_request_interval_ms` の小さいほう |

Table: 受信スレッド (`recv_thread_func()`) の待機の上限を決める条件

`health_timeout_ms` が 300 [ms] 未満の場合は、待機の上限の下限 (100 [ms]) が適用され、待機の上限が `health_timeout_ms` の 3 分の 1 より長くなります。  
N:1 モードでほかのピアへの送信を妨げている遅いピアが記録されている間は、`thread_recv_health_check_slow_peers()` が次の判定予定時刻 (判定間隔の上限 `POTR_INTERNAL_FLOW_RECHECK_MS` = 50 [ms]) を返すため、待機の上限が短縮されます。妨げが記録されているピアがない間は 0 を返し、待機の上限には影響しません。  
`ctx->flow_control` は、容量 (`flow_control_capacity`) が 0 でも、OS の受信バッファーに基づく送出上限を使う受信側では確保されます。  
この送出上限は既定で有効のため、既定の構成の UDP の受信側 (RAW を除く) は、フロー制御を設定していなくても、待機の上限が `flow_control_request_interval_ms` (既定 200 [ms]) になります。

タイムアウト判定は CLOCK_MONOTONIC を使用します。

片方向 UDP (type 1-6) は「最後に有効な PING / DATA を受信した時刻」を、双方向 UDP / TCP (type 7-10) は「最後に PING を受信した時刻」をタイムアウトの根拠とします。RAW 系も非 RAW 系も同じ `last_recv_ts` / `path_last_recv_ts` を参照する点は共通です。

1:1 モードの判定は次のとおりです。

1. パスごとに `path_last_recv_ts[i]` から経過時間を算出します。
2. `health_timeout_ms` 超過でそのパスの `peer_port[i]` をクリアします。
3. 全体 (`last_recv_ts`) が `health_timeout_ms` を超過すると `health_alive = 0` にして `POTR_EVENT_DISCONNECTED` を発火し、受信ウィンドウをリセットします。

N:1 モードの判定は次のとおりです。

1. ピアごとにパス タイムアウトを確認します。
2. ピア全体がタイムアウトしたら `POTR_EVENT_DISCONNECTED` を発火し、`potr_internal_peer_free()` でピアを削除します。

## TCP 系ヘルス チェック

### PING 送出 (tcp_health スレッド)

`potr_health_thread.c` の `tcp_health_thread_func()` がパスごと (`path_idx`) に独立して起動します。TCP では SENDER / RECEIVER を問わず全ロールで、`health_interval_ms > 0` の場合のみ起動します。接続直後の初回 PING は connect/accept 側が bootstrap PING として即座に送出し、`path_ping_state[]` が変化した場合は `health_interval_ms > 0` のとき全 tcp_health スレッドを即時起床させます。`health_interval_ms = 0` のときは recv 側が状態変化を検知した path から直接 PING を返信し、bootstrap ハンドシェイクを完了させます。

1. 接続直後に bootstrap PING を 1 回送信します。
2. `health_interval_ms > 0` の場合のみ、`tcp_conn_fd[path_idx]` を確認してから `ctx->health_interval_ms` 周期で送信します。

### タイムアウト検出 (recv スレッド)

UDP 系と同様に受信スレッドが判定します。TCP の両端がそれぞれ独立してタイムアウトを検出します。

1. 受信ループは `health_interval_ms > 0 && health_timeout_ms > 0` の場合 (受信タイムアウトを使う場合) に、`tcp_wait_readable()` で最大 `min(health_timeout_ms, 1000)` [ms] 待機します。フロー制御パケットを送出する受信側 (`flow_control != NULL`) では、待機の上限が `flow_control_request_interval_ms` との小さいほうになります。受信タイムアウトを使わない場合 (`use_recv_timeout` が 0) は、通常は待機の上限を設けず (`poll_ms` が 0) に受信を待ちますが、`flow_control != NULL` の場合は `poll_ms` が `flow_control_request_interval_ms` になり、ポーリングで待つ動作に変わります。TCP の通信種別は OS の受信バッファーに基づく送出上限の対象外 (`resolve_receive_buffer_percent()` が割合を 0 にする) のため、TCP の受信側で `flow_control` が確保されるのは、容量 (`flow_control_capacity`) が 0 でない場合だけです。
2. PING を受信するたびに `tcp_last_ping_recv_ms[path_idx]` を現在時刻で更新します。
3. `cplat_clock_get_monotonic_ms()` で得た現在時刻 `- tcp_last_ping_recv_ms[path_idx] > ctx->health_timeout_ms` を超過するとタイムアウトと判定します。
4. タイムアウト時はそのパスのソケットを `cplat_socket_shutdown()` / `cplat_socket_close()` で閉じ、`tcp_conn_fd[path_idx]` を `CPLAT_INVALID_SOCKET` にします。
5. connect スレッド (`potr_connect_thread.c`) が `tcp_active_paths` をデクリメントして再接続ループへ遷移します。

DATA パケットの受信は `tcp_last_ping_recv_ms` をリセットしません。`health_interval_ms > 0` の場合は PING は DATA 送信とは独立して定周期送出されるため、PING の到達有無のみで接続状態を判定できます。`health_interval_ms = 0` の場合は bootstrap PING のみで CONNECTED を確立し、その後の timeout 監視は行いません。

## 受信報告による片方向の経路状態の把握

片方向・一対多の送信者が優先グループの選択に必要な経路ごとの到達状態を知るため、通常モードの `unicast`、`multicast`、`broadcast` の受信者は受信報告 (`POTR_FLAG_REPORT`) を送信します (RAW モードの受信者は送信しません)。

### 受信報告の送出 (受信者側)

- 受信者は実効 `health_interval_ms` ごとに、受信者が保持する全経路から送信者のアドレスとポートへ `POTR_FLAG_REPORT` を送出します。
- `multicast` と `broadcast` では、複数の受信者からの報告が送信者へ同時に集中しないよう、各回の送出間隔を `health_interval_ms` の 100%〜150% の範囲でばらつかせます (乱数生成には cplat の乱数 API を使用します)。
- ペイロードには `POTR_MAX_PATH` バイト (8 バイト) の自端の経路受信状態 (`POTR_PING_STATE_*`) を格納します。暗号化有効時は PING と同じくペイロードを暗号化し、ヘッダー全体を AAD として保護します。

### 受信者の管理と到達判定 (送信者側)

- 送信者は、受信報告のヘッダーに含まれるセッション識別子 (`session_id`、`session_tv_sec`、`session_tv_nsec` の組) で受信者を識別し、受信者ごとの経路状態を保持します。
- 保持できる受信者数の上限は既存の `max_peers` (既定値 1,024) であり、上限を超えた受信者の報告はログに記録して破棄します。
- 全経路から受信報告が実効 `health_timeout_ms` の間届かない受信者は、離脱したものとして管理から外し、到達判定から除きます。
- 受信者の経路状態は、報告の到着時刻とペイロード内容から PING と同じ規則 (`POTR_PING_STATE_NORMAL`、`POTR_PING_STATE_ABNORMAL`、`POTR_PING_STATE_UNDEFINED`) で更新します。
- 一対多 (`multicast`、`broadcast`) では、報告中の全受信者について、そのグループに届く経路が 1 つ以上あるときにグループを到達可能と判定します。
- 片方向の送信者は、受信報告から得た経路状態を優先グループの選択にのみ使用し、`POTR_EVENT_PATH_*` イベントでは通知しません。

## マルチパス (最大 8 パス) の振る舞い

全 potr_type でパスは最大 8 (`POTR_MAX_PATH`) まで設定できます。

UDP 系の処理は次のとおりです。

- PING は優先グループにかかわらず全グループの全パス (`k = 0 〜 n_path - 1`) に対して送出します (待機グループの監視を含む)。
- パスごとに最終受信時刻 (`path_last_recv_ts[k]`) を独立して追跡します。
- 特定パスのみタイムアウトした場合はそのパスの `peer_port[k]` だけをクリアし、他のパスの通信は継続します。
- 全パスが揃ってタイムアウト (= 全体タイムアウト) になった場合に `POTR_EVENT_DISCONNECTED` を発火します。

TCP 系の処理は次のとおりです。

- パスごとに独立した tcp_health スレッドが起動し、PING を送出します。
- `tcp_last_ping_recv_ms[path_idx]` をパスごとに追跡し、recv スレッドが PING 受信タイムアウトを検出します。
- タイムアウト時はそのパスのソケットのみ閉じ、他のパスに影響しません。

## 実装ファイル早見表

| 機能 | ファイル | 関数 |
|---|---|---|
| PING パケット構築 | `protocol/packet.c` | `potr_internal_packet_build_ping()` |
| UDP PING 送出 | `thread/potr_health_thread.c` | `health_thread_func()` |
| TCP PING 送出 | `thread/potr_health_thread.c` | `tcp_health_thread_func()` |
| UDP タイムアウト検出 (1:1) | `thread/thread_recv_health.c` | `thread_recv_health_check_service()` |
| UDP タイムアウト検出 (N:1) | `thread/thread_recv_health.c` | `thread_recv_health_check_peers()` |
| N:1 遅いピアの猶予時間判定と切断 | `thread/thread_recv_health.c` | `thread_recv_health_check_slow_peers()` |
| TCP タイムアウト検出 | `thread/potr_recv_thread.c` | `tcp_recv_thread_func()` 内 |
| health スレッド起動/停止 | `thread/potr_health_thread.h` | `potr_internal_health_thread_start/stop()` |
| tcp_health スレッド起動/停止 | `thread/potr_health_thread.h` | `potr_internal_tcp_health_thread_start/stop()` |

Table: 死活監視機能の実装ファイルと関数一覧
