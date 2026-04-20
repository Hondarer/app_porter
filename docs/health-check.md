# PING ヘルスチェック設計まとめ

porter フレームワークにおける PotrType ごとの PING 送出ロジック、マルチパスごとの振る舞い、タイムアウト検出方式を整理する。PONG (PING 応答) は存在しない。片方向 type 1-6 は有効な `PING` / `DATA` 受信をヘルス信号として扱い、送信側は「最後の PING または有効 DATA 送信」から `health_interval_ms` 経過時だけ PING を送る。双方向 type 7-10 は PING 応答ベースで path logical を判定し、service / peer の `CONNECTED` はその OR で決まる。

## 概要

ヘルスチェックは PING パケット (`POTR_FLAG_PING`) の送出、双方向系における通信経路オープン時の割り込み送出、双方向系におけるパス受信状態変化時の割り込み送出、およびタイムアウト超過時の切断検出から成る。非 TCP は 1 サービスにつき 1 本の health スレッドを使い、type 8 はその 1 本が全 peer を順に処理する。TCP は path ごとに 1 本の tcp_health スレッドを使う。PotrType によって送出側と検出側の担当が異なる。

## PotrType ごとの振る舞い

以下の表は各 PotrType の概要をまとめたものである。

| PotrType | 値 | PING 送出 | タイムアウト検出 |
|---|---|---|---|
| UNICAST_RAW | 1 | SENDER (health スレッド) | RECEIVER 側 (有効な PING / DATA の受信タイムアウト) |
| MULTICAST_RAW | 2 | SENDER (health スレッド) | RECEIVER 側 (有効な PING / DATA の受信タイムアウト) |
| BROADCAST_RAW | 3 | SENDER (health スレッド) | RECEIVER 側 (有効な PING / DATA の受信タイムアウト) |
| UNICAST | 4 | SENDER (health スレッド) | RECEIVER 側 (有効な PING / DATA の受信タイムアウト) |
| MULTICAST | 5 | SENDER (health スレッド) | RECEIVER 側 (有効な PING / DATA の受信タイムアウト) |
| BROADCAST | 6 | SENDER (health スレッド) | RECEIVER 側 (有効な PING / DATA の受信タイムアウト) |
| UNICAST_BIDIR | 7 | 両端 (health スレッド) | 両端 (PING 受信タイムアウト) |
| UNICAST_BIDIR_N1 | 8 | サーバ (全ピア宛) + クライアント (サーバ宛) | 両端・ピア単位 (PING 受信タイムアウト) |
| TCP | 9 | 両端 (tcp_health スレッド・パスごと) | 両端 (PING 受信タイムアウト) |
| TCP_BIDIR | 10 | 両端 (tcp_health スレッド・パスごと) | 両端 (PING 受信タイムアウト) |

## 設定パラメータ

```text
udp_health_interval_ms : UDP 系 PING 送信間隔 (ms)。0 = 無効。
udp_health_timeout_ms  : UDP 系タイムアウト (ms)。RECEIVER 側で判定。0 = 無効。
tcp_health_interval_ms : TCP 系 PING 送信間隔 (ms)。0 = 無効。
tcp_health_timeout_ms  : TCP 系 PING 受信タイムアウト (ms)。両端で判定。0 = 無効。
```

サービス定義 (`PotrServiceDef`) に同名フィールドを設定するとグローバル値を上書きできる。0 の場合はグローバル値を使用する。

## PING パケット構造

`flags` フィールドに `POTR_FLAG_PING` をセットし、`ack_num` は常に 0 とする (双方向 UDP の応答では要求の `seq_num` をセット)。

実装: `packet.c` の `packet_build_ping()`。

### PING ペイロード (パス受信状態)

PING パケットのペイロードには自端の各パス PING 受信状態を `POTR_MAX_PATH` (= 4) バイトで格納する。各バイトがパスインデックス順に対応する。

| 値 | 定数 | 意味 |
|---|---|---|
| `0` | `POTR_PING_STATE_UNDEFINED` | 不定 (片方向通信 / まだ有効な PING / DATA 未受信) |
| `1` | `POTR_PING_STATE_NORMAL` | 正常 (ヘルス信号を継続受信中) |
| `2` | `POTR_PING_STATE_ABNORMAL` | 異常 (PING 途絶・タイムアウト) |

片方向通信 (type 1-6) では送信側が返送用 PING を持たないため、送出される PING ペイロードは全バイト `UNDEFINED` のままである。一方、受信側ローカルの `path_ping_state[]` は有効な `PING` または `DATA` を受けると `NORMAL` に更新される。双方向通信 (type 7-10) は実際の PING 受信状態を格納する。

受信側は PING 受信時にペイロードを `remote_path_ping_state[]` に保存する。これにより双方向通信の両端が相手の往復疎通状態を把握できる。

`path_ping_state[]` の更新タイミング:

- `update_path_health()`: 片方向 type 1-6 では有効な PING / DATA 受信時、双方向 type 7 では PING 受信時に `POTR_PING_STATE_NORMAL` に設定する
- `n1_update_path_health()`: type 8 で PING 受信時に `POTR_PING_STATE_NORMAL` に設定する
- `check_health_timeout()` / `n1_check_health_timeout()`: パスタイムアウト時に `POTR_PING_STATE_ABNORMAL` に設定する
- TCP PING 受信時: `POTR_PING_STATE_NORMAL` に設定する
- TCP PING タイムアウト時: `POTR_PING_STATE_ABNORMAL` に設定する
- セッション DISCONNECTED 発火時 / TCP 切断時: 全パスを `POTR_PING_STATE_UNDEFINED` にリセットする

双方向系 (`UNICAST_BIDIR` / `UNICAST_BIDIR_N1` / `TCP` / `TCP_BIDIR`) では、これらの値が変化した時点で health スレッドを即時起床させ、次周期を待たずに割り込み PING を送出する。PING ペイロードには更新後の `path_ping_state[]` 全体が載るため、相手端は往復疎通状態の変化をより早く把握できる。

## 回線確立 (CONNECTED) 検出

`POTR_EVENT_CONNECTED` の発火条件は通信形態によって異なる。

| 通信形態 | CONNECTED の発火条件 |
|---|---|
| 片方向 (type 1-6) | いずれかの path logical が 0->1 になったとき |
| 双方向 UDP (type 7, 8) | `path_ping_state[k] == NORMAL` かつ `remote_path_ping_state[k] == NORMAL` の path が 1 本以上になったとき |
| TCP (type 9, 10) | `tcp_conn_fd[k]` 有効かつ `path_ping_state[k] == NORMAL` かつ `remote_path_ping_state[k] == NORMAL` の path が 1 本以上になったとき |

双方向 UDP では `remote_path_ping_state[k] == POTR_PING_STATE_NORMAL` が一つ以上存在するときに CONNECTED を発火する。これは「相手端が自端からの PING を正常受信済みである」ことを意味し、往復疎通が確認できた時点で CONNECTED となる。

TCP はコネクション確立 (accept / connect 完了) だけでは CONNECTED を発火しない。接続直後に送る bootstrap PING に対する応答 PING を受信し、そのペイロード中の `remote_path_ping_state[]` に `POTR_PING_STATE_NORMAL` を確認した時点で CONNECTED となる。つまり TCP 双方向でも「自端が PING を送り、相手がその受信結果を返し、それを受け取る」往復完了が初回 CONNECTED の条件である。

片方向 type 1-6 では、PING ヘルスチェックが無効 (最終的な `health_interval_ms = 0`) でも有効な `DATA` を受信すれば CONNECTED が発火する。open 直後の即時 PING は送らないため、初回 CONNECTED は「最初の有効 DATA」または「最初の通常 PING」受信時に成立する。双方向 UDP (type 7, 8) は PING が送出されないと CONNECTED が発火しない。

双方向 UDP では、以下の順序でハンドシェイクが完了して CONNECTED が発火する。

1. 自端が PING 送出を開始する (ペイロードは全パス `UNDEFINED`)。通信経路オープン時は最初のスリープをスキップして即座に送出するため、最大 `health_interval_ms` を待たずに相手端へ PING が届く。
2. 相手端が自端の PING を受信し、`path_ping_state` を `NORMAL` に更新する。
3. 相手端が `path_ping_state` 変化に反応して割り込み PING を送出する (変化がなければ次の定周期 PING まで待つ)。
4. 自端がその PING を受信し、ペイロードに `NORMAL` を確認して CONNECTED を発火する。

双方向 UDP では、この往復 PING が接続確立そのものです。実効 `health_interval_ms = 0`、または `udp_health_interval_ms = 0` かつサービス側で `health_interval_ms` を上書きしない構成では初回 PING が送られないため、CONNECTED は成立しません。

片方向通信では手順 2-3 に相当する往復が不要で、有効な `PING` または `DATA` の受信で即 CONNECTED となる。

実装箇所: `thread/potrRecvThread.c` / `thread/potrConnectThread.c` の path logical 同期処理、および `potrPathEvent.c`。

## 接続状態別の実装

本節では、各通信種別について「CONNECTED 前」「CONNECTED 後」の実装を整理する。

- CONNECTED 前: `POTR_EVENT_DATA` は発火しない
- CONNECTED 後: `DATA` を配送できる
- CONNECTED の定義: 片方向 (type 1-6) はいずれかのパスで有効な `PING` または `DATA` を受信した時点、双方向 UDP (type 7-8) は受信 `PING` ペイロード内の `remote_path_ping_state[]` に `POTR_PING_STATE_NORMAL` を確認した時点、TCP (type 9-10) は bootstrap PING に対する応答 `PING` のペイロード内に `POTR_PING_STATE_NORMAL` を確認した時点

### type 1-6: `UNICAST_RAW` / `MULTICAST_RAW` / `BROADCAST_RAW` / `UNICAST` / `MULTICAST` / `BROADCAST`

| 状態 | 実装 |
|---|---|
| CONNECTED 前 | `PING` は health スレッドが「最後の PING または有効 DATA 送信」から `health_interval_ms` 経過したときだけ送信する。open 直後の即時 PING はない。`potrSend()` は通常どおり `DATA` を送信キューへ積む。受信側は最初の有効な `PING` または `DATA` を受理した時点で `health_alive == 1` になり、`POTR_EVENT_CONNECTED` を発火する。 |
| CONNECTED 後 | 受信側が `health_alive == 1` を維持して `DATA` を配送する。 |
| CONNECTED 解除 | `health_timeout_ms` 超過、`FIN` 受信、`REJECT` 受信、RAW 系のギャップ検出で `health_alive == 0` に戻り、以後は再び CONNECTED 前と同じ扱いになる。 |
| PotrEvent 順序 | 初回の有効 `DATA` を受理した場合も、先に `POTR_EVENT_CONNECTED` を発火してから `POTR_EVENT_DATA` を配送する。 |

### type 7: `UNICAST_BIDIR`

| 状態 | 実装 |
|---|---|
| CONNECTED 前 | `PING` は両端が送信する。`potrSend()` は `health_alive == 0` の間 `POTR_ERROR_DISCONNECTED` を返す。受信側は `health_alive == 0` の間 `DATA` を配送しない。 |
| CONNECTED 後 | `health_alive == 1` になり、`potrSend()` が成功する。受信側も `DATA` を配送する。 |
| CONNECTED 解除 | `health_timeout_ms` 超過、`FIN` 受信、`REJECT` 受信で `health_alive == 0` に戻り、以後は再び CONNECTED 前と同じ扱いになる。 |
| PotrEvent 順序 | `POTR_EVENT_CONNECTED` 前に `POTR_EVENT_DATA` は発火しない。 |

### type 8: `UNICAST_BIDIR_N1`

| 状態 | 実装 |
|---|---|
| CONNECTED 前 | サーバ/クライアントとも `PING` を送信する。`peer_id` 指定送信は `peer->health_alive == 0` の間 `POTR_ERROR_DISCONNECTED` を返す。`POTR_PEER_ALL` は接続済み peer が 0 件なら `POTR_ERROR_DISCONNECTED` を返す。受信側は `peer->health_alive == 0` の間 `n1_deliver_payload_elem()` で `DATA` を配送しない。未知 peer の初回 `DATA` は peer を作らず破棄し、初回 `PING` でのみ peer slot と session 状態を作る。 |
| CONNECTED 後 | 対象 peer の `health_alive == 1` になり、その peer に対する送受信が有効になる。 |
| CONNECTED 解除 | ピア単位で `health_timeout_ms` 超過または `FIN` 受信時に `peer->health_alive == 0` となり、`peer_free()` でピアを削除する。以後はその peer を未接続として扱い、再接続は再度 `PING` 起点で行う。 |
| PotrEvent 順序 | `POTR_EVENT_CONNECTED` 前に `POTR_EVENT_DATA` は発火しない。未知 peer の初回 `DATA` でも peer table は前進しない。 |

### type 9-10: `TCP` / `TCP_BIDIR`

| 状態 | 実装 |
|---|---|
| CONNECTED 前 | TCP 接続確立後に各 path が bootstrap PING を送信し、その応答 `PING` の `remote_path_ping_state[]` に `POTR_PING_STATE_NORMAL` が載ると論理 CONNECTED へ遷移する。`health_interval_ms > 0` の場合のみ tcp_health スレッドが定周期 `PING` を送る。`potrSend()` は `tcp_active_paths == 0` または `health_alive == 0` の間 `POTR_ERROR_DISCONNECTED` を返す。受信側は `health_alive == 0` の間 `deliver_payload_elem()` で `DATA` を破棄する。 |
| CONNECTED 後 | `health_alive == 1` になり、`potrSend()` が成功する。受信側も `DATA` を配送する。 |
| CONNECTED 解除 | path ごとの PING タイムアウトや TCP 切断で `tcp_active_paths` が減少し、全 path が失われると connect スレッドが `health_alive == 0` に戻して `POTR_EVENT_DISCONNECTED` を発火する。加えて正常 close では、recv スレッドが protocol-level `FIN` を受信して最後の DATA 配送完了後に `FIN_ACK` を返し、その直後に `POTR_EVENT_DISCONNECTED` を発火する。以後は再び CONNECTED 前と同じ扱いになり、再接続後に `PING` 交換で CONNECTED へ戻る。 |
| PotrEvent 順序 | `POTR_EVENT_CONNECTED` 前に `POTR_EVENT_DATA` は発火しない。 |

## PotrEvent 順序

全通信種別で、未接続中の `DATA` は最終配送段の `health_alive` または `peer->health_alive` 判定で遮断される。

- type 1-7, 9-10: `deliver_payload_elem()` が `health_alive == 0` の間 `DATA` を破棄する
- type 8: `n1_deliver_payload_elem()` が `peer->health_alive == 0` の間 `DATA` を破棄する
- したがって、アプリコールバックとして `POTR_EVENT_CONNECTED` 前に `POTR_EVENT_DATA` は発火しない

双方向 UDP (type 7, 8) では、通信種別とサービス上書きを解決した最終的な `health_interval_ms = 0` の場合は CONNECTED が成立しないため、`POTR_EVENT_DATA` も発火しない。

## UDP 系ヘルスチェック

### PING 送出 (health スレッド)

`potrHealthThread.c` の `health_thread_func()` が非 TCP 用の共有 health スレッドとして動作する。片方向 type 1-6 は open 直後の即時 PING を行わず、最初の PING も `health_interval_ms` 経過後に送る。有効 DATA がその前に送られた場合は、次回期限を「最後の DATA 送信時刻 + health_interval_ms」へ後ろ倒しする。双方向 UDP (`UNICAST_BIDIR`, `UNICAST_BIDIR_N1`) では従来どおり `health_interval_ms` 周期で送信し、`path_ping_state[]` が変化した場合にも条件変数 wakeup により即時送出される。ここで `health_interval_ms = 0` になると health スレッド自体が起動しないため、双方向 UDP の初回 CONNECTED に必要な PING 送信も始まりません。

非 N:1 モード (UNICAST_RAW / MULTICAST_RAW / BROADCAST_RAW / UNICAST / MULTICAST / BROADCAST / UNICAST_BIDIR):

1. 片方向 type 1-6 は `max(last_ping_send_ms, last_valid_data_send_ms) + health_interval_ms`、双方向 type 7 は定周期 `health_interval_ms` を送信期限として評価する。
2. 送信ウィンドウから `next_seq` を取得する。
3. `packet_build_ping()` で PING パケットを構築する。
4. `ctx->n_path` 分のパスそれぞれで `sendto()` を実行する。
5. 暗号化有効時は `POTR_FLAG_ENCRYPTED` を付与して GCM 認証タグを追加する。

片方向 type 1-6 では send スレッドが外側 DATA パケットを実送出した時点で `last_valid_data_send_ms` を更新する。`potrSend()` の API 成功時ではない。1 つの外側 DATA パケットを全 path に fan-out したあと 1 回だけ更新し、その時点から次の PING 期限を再計算する。

N:1 モード (UNICAST_BIDIR_N1 のサーバ側):

1. ピアテーブルからアクティブなピアをループする。
2. ピアごとに `packet_build_ping()` でパケットを構築する。
3. ピアごとに全パスへ `sendto()` を実行する。

### PING 受信時の処理 (受信スレッド)

UNICAST_BIDIR_N1 (type 8) は `n1_notify_health_alive()` でそのピアを alive 状態にする。ギャップがある場合は NACK を優先送出する。

片方向 UDP (type 1-6) は有効な `PING` または `DATA` を受信すると最終受信時刻を更新し、タイムアウト計測をリセットする。双方向 UDP (type 7, 8) は従来どおり PING 受信時のみ更新する。

RAW 系 (type 1, 2, 3) のギャップ検出時は NACK を送らず `POTR_EVENT_DISCONNECTED` を発火してセッションをリセットする。UNICAST / MULTICAST / BROADCAST (type 4, 5, 6) のギャップ検出時は NACK を送出して再送を要求する。MULTICAST / BROADCAST では複数受信者の NACK が集中しないよう、送出タイミングに `reorder_timeout_ms` の 100〜200% のジッタを付加する。

### タイムアウト検出 (受信スレッド)

受信スレッドは `select()` の待ち時間を `health_timeout_ms / 3` に設定し、定期的に `check_health_timeout()` (1:1) または `n1_check_health_timeout()` (N:1) を呼び出す。

タイムアウト判定は CLOCK_MONOTONIC を使用する。

片方向 UDP (type 1-6) は「最後に有効な PING / DATA を受け取った時刻」を、双方向 UDP / TCP (type 7-10) は「最後に PING を受け取った時刻」をタイムアウトの根拠とする。RAW 系も非 RAW 系も同じ `last_recv_tv_sec` / `path_last_recv_sec` を参照する点は共通である。

1:1 モード:

1. パスごとに `path_last_recv_sec[i]` / `path_last_recv_nsec[i]` から経過時間を算出する。
2. `health_timeout_ms` 超過でそのパスの `peer_port[i]` をクリアする。
3. 全体 (`last_recv_tv_sec` / `last_recv_tv_nsec`) が `health_timeout_ms` を超過すると `health_alive = 0` にして `POTR_EVENT_DISCONNECTED` を発火し、受信ウィンドウをリセットする。

N:1 モード:

1. ピアごとにパスタイムアウトを確認する。
2. ピア全体がタイムアウトしたら `POTR_EVENT_DISCONNECTED` を発火し、`peer_free()` でピアを削除する。

## TCP 系ヘルスチェック

### PING 送出 (tcp_health スレッド)

`potrHealthThread.c` の `tcp_health_thread_func()` がパスごと (`path_idx`) に独立して起動する。TCP では SENDER / RECEIVER を問わず全ロールで、`health_interval_ms > 0` の場合のみ起動する。接続直後の初回 PING は connect/accept 側が bootstrap PING として即座に送出し、`path_ping_state[]` が変化した場合は `health_interval_ms > 0` のとき全 tcp_health スレッドを即時起床させる。`health_interval_ms = 0` のときは recv 側が状態変化を検知した path から直接 PING を返し、bootstrap ハンドシェイクを完了させる。

1. 接続直後に bootstrap PING を 1 回送信する。
2. `health_interval_ms > 0` の場合のみ、`tcp_conn_fd[path_idx]` を確認してから `ctx->health_interval_ms` 周期で送信する。

### タイムアウト検出 (recv スレッド)

UDP 系と同様に受信スレッドが判定する。TCP の両端がそれぞれ独立してタイムアウトを検出する。

1. 受信ループは `health_interval_ms > 0 && health_timeout_ms > 0` の場合のみ `tcp_wait_readable()` で最大 `min(health_timeout_ms, 1000)` ms 待機する。
2. PING を受信するたびに `tcp_last_ping_recv_ms[path_idx]` を現在時刻で更新する。
3. `get_ms() - tcp_last_ping_recv_ms[path_idx] > ctx->health_timeout_ms` を超過するとタイムアウトと判定する。
4. タイムアウト時はそのパスのソケットを `shutdown()` / `close()` し、`tcp_conn_fd[path_idx]` を `POTR_INVALID_SOCKET` にする。
5. connect スレッド (`potrConnectThread.c`) が `tcp_active_paths` をデクリメントして再接続ループに入る。

DATA パケットの受信は `tcp_last_ping_recv_ms` をリセットしない。`health_interval_ms > 0` の場合は PING は DATA 送信とは独立して定周期送出されるため、PING の到達有無のみで接続状態を判定できる。`health_interval_ms = 0` の場合は bootstrap PING のみで CONNECTED を確立し、その後の timeout 監視は行わない。

## マルチパス (最大 4 パス) の振る舞い

全 PotrType でパスは最大 4 (`POTR_MAX_PATH`) まで設定できる。

UDP 系:

- PING は全パス (`ctx->sock[k]`, `ctx->dest_addr[k]`, k = 0〜n_path-1) に対して毎周期送出する。
- パスごとに最終受信時刻 (`path_last_recv_sec[k]` / `path_last_recv_nsec[k]`) を独立して追跡する。
- 特定パスのみタイムアウトした場合はそのパスの `peer_port[k]` だけをクリアし、他のパスの通信は継続する。
- 全パスが揃ってタイムアウト (= 全体タイムアウト) になった場合に `POTR_EVENT_DISCONNECTED` を発火する。

TCP 系:

- パスごとに独立した tcp_health スレッドが起動し、PING を送出する。
- `tcp_last_ping_recv_ms[path_idx]` をパスごとに追跡し、recv スレッドが PING 受信タイムアウトを検出する。
- タイムアウト時はそのパスのソケットのみ閉じ、他のパスに影響しない。

## 実装ファイル早見表

| 機能 | ファイル | 関数 |
|---|---|---|
| PING パケット構築 | `protocol/packet.c` | `packet_build_ping()` |
| UDP PING 送出 | `thread/potrHealthThread.c` | `health_thread_func()` |
| TCP PING 送出 | `thread/potrHealthThread.c` | `tcp_health_thread_func()` |
| UDP タイムアウト検出 (1:1) | `thread/potrRecvThread.c` | `check_health_timeout()` |
| UDP タイムアウト検出 (N:1) | `thread/potrRecvThread.c` | `n1_check_health_timeout()` |
| TCP タイムアウト検出 | `thread/potrRecvThread.c` | `tcp_recv_thread_func()` 内 |
| health スレッド起動/停止 | `thread/potrHealthThread.h` | `potr_health_thread_start/stop()` |
| tcp_health スレッド起動/停止 | `thread/potrHealthThread.h` | `potr_tcp_health_thread_start/stop()` |
