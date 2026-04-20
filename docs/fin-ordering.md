# FIN / FIN_ACK 待ち合わせ仕様

porter フレームワークにおける FIN パケットと DATA パケットの順序保証、および TCP close 完了待ちの仕組みを説明します。

## 背景

UDP では `sendto()` 完了は OS の送信バッファへの書き込み完了を意味し、受信側への到達順序や受信アプリケーションの処理完了は保証しません。

従来の FIN 待ち合わせはこの問題に対するもので、最後の DATA より FIN が先着しても、受信ウィンドウが追い付くまで `DISCONNECTED` を遅延して後着 DATA を受け入れ続けます。

一方 TCP はストリーム順序自体は保証しますが、`potrCloseService()` がソケットを即 close すると、

```text
potrSend() 完了
→ 送信スレッドが tcp_send_all() を完了
→ sender が potrCloseService() で close
→ receiver 側 callback より先に切断処理へ進む
```

という競合が起こりえます。

そのため、TCP では protocol-level の `FIN` / `FIN_ACK` を用いて
「最後に送った DATA が receiver 側で処理され、同期 `POTR_EVENT_DATA` callback が return するまで」
を close 完了条件にしています。

## 保証粒度

本仕様が保証するのは次の時点までです。

- receiver 側で最後の DATA が `recv_window` から pop される
- 復号・展開が完了する
- 同期 `PotrRecvCallback(POTR_EVENT_DATA)` が return する

以下は保証対象外です。

- アプリケーション callback 内部で起動した非同期処理の完了
- callback 後の外部 I/O 完了
- N:1 の `potrDisconnectPeer()` 側完了通知

## 送信側

### UDP: FIN target を使った遅延切断

`potrCloseService()` の `send_fin()` / `peer_send_fin()` は、現セッションで DATA を 1 件以上送っている場合だけ
`POTR_FLAG_FIN_TARGET_VALID` を立てて `ack_num` に `send_window.next_seq` を設定します。

```text
FIN.flags   = FIN | FIN_TARGET_VALID
FIN.ack_num = send_window.next_seq
```

DATA を 1 件も送っていない場合は no-data FIN とし、`FIN_TARGET_VALID` を付けません。

### TCP: close 時に FIN_ACK を待つ

TCP (`POTR_TYPE_TCP` / `POTR_TYPE_TCP_BIDIR`) の送信側 `potrCloseService()` は次の順序で動作します。

```text
1. close_requested = 1 にして新規 potrSend() を禁止
2. tcp_health スレッドを停止
3. send_queue drain を待つ
4. FIN[target_valid, ack_num=send_window.next_seq] を送る
5. FIN_ACK[ack_num=FIN.ack_num] を待つ
6. FIN_ACK 受信後に connect thread / socket teardown
```

待機時間は global 設定 `tcp_close_timeout_ms` で制御します。タイムアウト時は強制 close を行い `POTR_ERROR` を返します。

## 受信側

### FIN をペンディングしてウィンドウ追い付きを待つ

受信側は UDP/TCP 共通で `POTR_FLAG_FIN_TARGET_VALID` の有無により動作を分岐します。

```text
FIN.flags に FIN_TARGET_VALID がない
  → 即 DISCONNECTED

FIN.flags に FIN_TARGET_VALID があり recv_window.next_seq == FIN.ack_num
  → 即 DISCONNECTED

FIN.flags に FIN_TARGET_VALID があり recv_window.next_seq != FIN.ack_num
  → pending_fin = true, fin_target_seq = FIN.ack_num
  → セッション状態はリセットしない
  → 後着 DATA / skip で next_seq が追い付くのを待つ
```

`pending_fin` 中は既存の NACK / REJECT / reorder timeout の仕組みがそのまま働きます。

### TCP: DISCONNECTED の直前に FIN_ACK を返す

TCP では `pending_fin` が解消された時点、または no-data FIN / 追い付き済み FIN を受けた時点で、
receiver は `FIN_ACK` を返してから `POTR_EVENT_DISCONNECTED` を発火します。

```text
receiver:
  FIN target 到達
  → FIN_ACK 送信
  → POTR_EVENT_DISCONNECTED
  → current session をリセット
```

このため sender 側の `potrCloseService()` は、receiver 側 callback 完了後に close 成功へ進みます。

受信側サービス自体は auto-close しません。TCP セッションだけを閉じ、listen / accept は継続します。

## 暗号化時の nonce 規約

暗号化 control packet の nonce は 12 バイトで、構成は以下です。

```text
[session_id:4B][flags:2B][seq_or_ack_num:4B][padding:2B]
```

- DATA / PING / FIN は `seq_num` を使う
- NACK / REJECT / FIN_ACK は `ack_num` を使う

したがって `FIN_ACK` を追加したことで、暗号化 control packet の検証側も `ack_num` ベースで nonce を計算する必要があります。

## シーケンス

### UDP: FIN が DATA より先着する場合

```text
送信側                受信側
DATA[seq=N] ────── (遅延)
FIN[target_valid, ack_num=N+1] ─→ next_seq != N+1
                                   → pending_fin=true
(後から到着)
DATA[seq=N] ─────────→ pop → callback(DATA)
                        next_seq == N+1
                        → DISCONNECTED
```

### TCP: sender close 完了待ち

```text
sender                         receiver
DATA[seq=N] ─────────────────→ pop → callback(DATA) return
FIN[target_valid, ack_num=N+1] ─→ pending または即時解消
                                 target 到達
                                 → FIN_ACK[ack_num=N+1]
FIN_ACK 受信
→ potrCloseService() 成功
→ socket teardown
```

### no-data FIN

```text
FIN.flags = FIN
FIN_TARGET_VALID なし
→ receiver は即 DISCONNECTED
→ TCP では続けて FIN_ACK を返す
```

## エッジケース

| シナリオ | 解消パス |
|---|---|
| 欠番 DATA が後着 | `window_recv_push()` → `drain_recv_window()` 末尾で `pending_fin` 解消 |
| UDP で送信側が NACK に REJECT で応答 | `window_recv_skip()` 後の `drain_recv_window()` で解消 |
| `reorder_timeout_ms` タイムアウト | `window_recv_skip()` 後の `drain_recv_window()` で解消 |
| TCP close wait 中に FIN_ACK 未着 | `tcp_close_timeout_ms` 超過で強制 close、`potrCloseService()` は `POTR_ERROR` |
| `send_window.next_seq` が wrap して 0 | `FIN_TARGET_VALID` の有無で no-data FIN と区別するため問題なし |

## 対象通信種別

| 通信種別 | FIN 送受信 | FIN_ACK | 備考 |
|---|---|---|---|
| unicast / multicast / broadcast / raw | ○ | × | 従来どおり FIN pending だけで順序保証する |
| unicast_bidir 1:1 | ○ | × | UDP 系として動作 |
| unicast_bidir_n1 | ○ | × | `PotrPeerContext` ごとに pending FIN を管理する |
| tcp (type 9) | ○ | ○ | `potrCloseService()` が FIN_ACK を待つ |
| tcp_bidir (type 10) | ○ | ○ | 同上 |

## 実装ファイル

| ファイル | 役割 |
|---|---|
| `prod/libsrc/porter/api/potrCloseService.c` | TCP close の `FIN` 送信、`FIN_ACK` 待機、タイムアウト処理 |
| `prod/libsrc/porter/thread/potrRecvThread.c` | UDP/TCP 共通の pending FIN 管理、TCP の `FIN_ACK` 送受信 |
| `prod/libsrc/porter/protocol/packet.c` | `packet_build_fin()` / `packet_build_fin_ack()` |
| `prod/libsrc/porter/protocol/config.c` | `tcp_close_timeout_ms` の読込 |
| `prod/libsrc/porter/potrContext.h` | close wait 状態、`pending_fin` / `fin_target_seq` などの保持 |
