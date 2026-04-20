# パスイベント

porter はセッション全体の接続状態だけでなく、各 path の論理接続状態も
`POTR_EVENT_PATH_CONNECTED` / `POTR_EVENT_PATH_DISCONNECTED` で通知します。

## 概要

`PATH_*` は物理 path の学習や TCP ソケット確立そのものではなく、
「その path が論理的に `CONNECTED` 判定へ寄与しているか」を表します。

- `POTR_EVENT_PATH_CONNECTED`
  対象 path の論理接続状態が 0 から 1 へ変化したときに発火します。
- `POTR_EVENT_PATH_DISCONNECTED`
  対象 path の論理接続状態が 1 から 0 へ変化したときに発火します。
- `POTR_EVENT_CONNECTED`
  service または peer の path 論理接続状態の OR が 0 から 1 へ変化したときに発火します。
- `POTR_EVENT_DISCONNECTED`
  service または peer の path 論理接続状態の OR が 1 から 0 へ変化したときに発火します。

複数 path が 1 回の更新で同時に変化した場合は、path index 昇順で `PATH_*` を発火し、
その後に必要であれば `CONNECTED` / `DISCONNECTED` を発火します。

## コールバック契約

`POTR_EVENT_PATH_CONNECTED` / `POTR_EVENT_PATH_DISCONNECTED` のとき、
`PotrRecvCallback` の引数は次の意味を持ちます。

| 引数 | 意味 |
|---|---|
| `peer_id` | N:1 モードでは対象 peer の ID。1:1 モードでは `POTR_PEER_NA` |
| `data` | `const int[POTR_MAX_PATH]` の path 論理接続状態スナップショット |
| `len` | 状態が変化した path index |

`path_states` は常にイベント発火後の状態です。
したがって `PATH_DISCONNECTED` でも `path_states[path_idx]` は必ず 0 です。

`data` はコールバック復帰後に無効になります。保存したい場合は利用側でコピーしてください。

```c
static void on_recv(int64_t service_id, PotrPeerId peer_id,
                    PotrEvent event, const void *data, size_t len)
{
    if (event == POTR_EVENT_PATH_CONNECTED
        || event == POTR_EVENT_PATH_DISCONNECTED)
    {
        const int *path_states = (const int *)data;
        int path_idx = (int)len;

        printf("service=%" PRId64 " peer=%u path=%d %s states={%d,%d,%d,%d}\n",
               service_id,
               (unsigned)peer_id,
               path_idx,
               (event == POTR_EVENT_PATH_CONNECTED) ? "CONNECTED" : "DISCONNECTED",
               path_states[0], path_states[1], path_states[2], path_states[3]);
    }
}
```

## path 論理接続状態

path logical の定義は通信種別で異なります。

### type 1-6

`UNICAST_RAW` / `MULTICAST_RAW` / `BROADCAST_RAW` /
`UNICAST` / `MULTICAST` / `BROADCAST`

- その path で有効な `PING` または `DATA` を受理すると 1 になります。
- その path の受信タイムアウトで 0 になります。
- service 全体が継続不能になった場合は、現在 1 の path をすべて 0 にします。

片方向通信では path ごとの応答確認を持たないため、
「その path で有効な受信が継続していること」自体が path logical です。

### type 7-8

`UNICAST_BIDIR` / `UNICAST_BIDIR_N1`

- `path_ping_state[k] == POTR_PING_STATE_NORMAL`
- かつ `remote_path_ping_state[k] == POTR_PING_STATE_NORMAL`

の両方を満たす path が logical connected です。

ここでの意味は、ローカルでもその path が正常であり、
相手から返ってきた PING payload 上でも同じ path が正常と確認できている、ということです。
つまり双方向 UDP の `PATH_*` は往復確認済みの path を表します。

type 8 ではこの判定を peer 単位で持ちます。

### type 9-10

`TCP` / `TCP_BIDIR`

- `tcp_conn_fd[k]` が有効
- `path_ping_state[k] == POTR_PING_STATE_NORMAL`
- `remote_path_ping_state[k] == POTR_PING_STATE_NORMAL`

の 3 条件を満たす path が logical connected です。

TCP では connect / accept 完了だけでは `PATH_CONNECTED` になりません。
bootstrap PING を含む往復確認が成立して初めて logical connected になります。

## 発火順序

### path 追加・復帰

ある path が 0 から 1 へ変化したとき、

- `PATH_CONNECTED(path_idx)`
- 必要なら `CONNECTED`

の順で発火します。

既に別 path が 1 本以上 alive なら `CONNECTED` は発火しません。

### path 断

ある path が 1 から 0 へ変化したとき、

- `PATH_DISCONNECTED(path_idx)`
- 必要なら `DISCONNECTED`

の順で発火します。

まだ別 path が 1 本以上 alive なら `DISCONNECTED` は発火しません。

### service / peer 継続不能

次のような事象では、その service または peer の継続が不可能とみなします。

- `FIN` 受信
- `REJECT` 受信
- RAW gap
- `potrDisconnectPeer()`
- TCP 全断

この場合は、現在 1 の path をすべて 0 に落としてから、

- `PATH_DISCONNECTED` を path index 昇順で全件発火
- 最後に `DISCONNECTED`

の順序で通知します。

## type 別の見え方

### type 1-6

- 初回の有効 `PING` または `DATA` を path `k` で受けると `PATH_CONNECTED(k)` が発火します。
- それが service 全体で最初の alive path なら直後に `CONNECTED` が発火します。
- path timeout でその path だけ 0 になれば `PATH_DISCONNECTED(k)` だけが発火します。
- 全 path が落ちれば最後の `PATH_DISCONNECTED` の直後に `DISCONNECTED` が発火します。

### type 7

- path `k` の `PATH_CONNECTED(k)` は、
  その path で PING を受けたことだけでは発火しません。
- ローカルの `path_ping_state[k]` と、相手から返ってきた
  `remote_path_ping_state[k]` の両方が `NORMAL` になった時点で発火します。

### type 8

- `PATH_*` は peer 単位で評価されます。
- ある peer の path 変化は、他 peer の `CONNECTED` / `DISCONNECTED` に影響しません。
- `potrDisconnectPeer()` は、その peer の alive path をすべて 0 にしてから
  `PATH_DISCONNECTED` 群と `DISCONNECTED` を発火します。

### type 9-10

- TCP ソケット確立では `PATH_CONNECTED` は出ません。
- 応答 PING による往復確認が成立した path だけが `PATH_CONNECTED` になります。
- ある TCP path が閉じて logical state が 0 になったときは
  `PATH_DISCONNECTED(path_idx)` が発火します。
- 全 path が失われたときは残りの alive path をすべて 0 にしてから
  `DISCONNECTED` を発火します。

## 実装上の責務分担

最終実装では path イベントの責務を次のように分離しています。

- `prod/include/porter_type.h`
  公開 enum と callback 契約の定義
- `prod/libsrc/porter/potrPathEvent.c`
  path logical 状態の差分計算、発火順序制御、callback 直列化
- `prod/libsrc/porter/thread/potrRecvThread.c`
  UDP / TCP の受信事象を path logical 状態へ反映
- `prod/libsrc/porter/thread/potrConnectThread.c`
  TCP path close と全断を path logical 状態へ反映
- `prod/libsrc/porter/api/potrDisconnectPeer.c`
  N:1 peer 切断時の `PATH_DISCONNECTED` 群と `DISCONNECTED` を発火

callback は内部 mutex で直列化されます。
TCP でも複数スレッドからイベントが競合しない前提で利用できます。

## 利用時の注意

- `PATH_*` は「物理的に path が見えたか」ではなく「論理的に接続判定へ寄与しているか」です。
- `CONNECTED` を service / peer 全体の状態、`PATH_*` をその内訳として扱うのが自然です。
- `path_states` は発火後状態なので、差分を追いたい場合は前回スナップショットを利用側で保持してください。
- `POTR_EVENT_DATA` は未接続中には発火しません。
