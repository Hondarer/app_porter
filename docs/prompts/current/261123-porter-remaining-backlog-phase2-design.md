<!--
生成元: agree-before-implement スキル
文書種別: 最終仕様 (design)
最終更新日時: 2026-11-25 22:24:26 JST
最終更新エージェント: claude code (opus サブエージェント、主セッション session_01KJc53UFydnbt2gLXTJC1ZC)
稼働 OS: Linux (WSL)
上流文書: `261123-porter-remaining-backlog-phase2-overview.md` (全体管理文書)、`261123-porter-remaining-backlog-phase2-discussion.md` (検討用 Markdown)、`261123-porter-remaining-backlog-phase2-lightadr.md` (簡易 ADR)。対策元の残課題一覧は `261123-porter-remaining-backlog-backlog.md` です。いずれも削除されている場合があります。
下流文書: `261123-porter-remaining-backlog-phase2-backlog.md` (残課題一覧)。未作成の場合や、削除されている場合があります。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。上流文書の全体管理文書が存在する場合は、その再開手順に従って進行位置を確認し、決定事項を簡易 ADR で、判断の経緯を検討用 Markdown の Q&A で確認します。存在しない場合は、この文書を単体の最終仕様として扱います。最終更新エージェントのセッション情報や稼働 OS・最終更新日時が参照可能であれば再開時のコンテキストや環境差分の把握に利用し、別エージェントや別環境・別 OS からの引き継ぎ、担当変更によりセッションが見つからない場合も異常とせず、文書の記載から継続してください。
-->

# porter の UDP の一括の送信 (第 2 期) 最終仕様

ファイル名: `261123-porter-remaining-backlog-phase2-design.md`

版: 1 (2026-11-25)

## 目的と範囲

porter の UDP の送信スレッドは、外側パケット (UDP のデータグラム) ごとに送信の呼び出しを 1 回行います。Linux (WSL) のループバックでは、1 つのメッセージが複数のパケットに分かれる大きさ (5,000 byte は 4 パケット) を待ち合わせを指定して送ると、1 パケットあたり約 32 µs のカーネルの時間がかかり、処理件数が `tcp` の約 0.47 倍にとどまります。

この案件では、送信キューに同時にある複数の外側パケットを、1 回の呼び出しで送ります。対象は、UDP の DATA の送出のすべてです。

Table: 適用の範囲

| 送出の処理 | 通信種別 | まとめる範囲 |
| --- | --- | --- |
| 1 対 1 (`flush_packed()` の系統) | `unicast`、`unicast_bidir`、`multicast`、`broadcast` (暗号化と複数経路を含む) | 送信キューに続く外側パケット。メッセージの境界をまたぐ |
| N 対 1 (`flush_packed_peer()` の系統) | `unicast_bidir_n1` | 同じピア宛てに続く外側パケットだけ |

TCP は、すでにまとめ送りがあり、送出の方式は変えません (共通の定数とバッファーの名前だけを変えます)。

Table: OS ごとの一括の送信の実装

| OS | 実装 |
| --- | --- |
| Linux | `sendmmsg()` を 1 回呼びます。 |
| Windows | `sendto()` を先頭から順に繰り返します。呼び出しの契約は Linux と同じです。 |

> [!NOTE]
> Windows の Registered I/O (RIO) は使いません。`WSA_FLAG_REGISTERED_IO` 付きの UDP ソケットでは、`bind` は成功しますが、`WSAEventSelect()`、`sendto`、`recvfrom`、`select` が `WSAENOTSOCK` (10038)、`ioctlsocket(FIONBIO)` が `WSAEOPNOTSUPP` (10045) で失敗しました (Windows 11 Pro for Workstations 10.0.26300、2026-11-25 の試作)。porter の UDP のソケットは、受信の待機、受信、制御パケットの送信に通常の関数を使うため、RIO のソケットにできません。

## 変更するサブモジュールと順序

Table: 変更するサブモジュール

| 順序 | サブモジュール | 内容 |
| --- | --- | --- |
| 1 | `framework/testfw` | `sendmmsg()` の mock の追加 |
| 2 | `app/cplat` | 登録したソケットの一括の送信の関数、`mock_cplat`、単体テスト、文書 |
| 3 | `app/porter` | 送信スレッドの UDP の一括の送信、ソケットの入出力の関数、単体テスト、文書 |

後のサブモジュールは、前のサブモジュールの成果物を使います。ビルドとテストは、この順に行います。

## framework/testfw: sendmmsg の mock

`create-testfw-mock` スキルの手順で、Linux の `sendmmsg()` の mock を追加します。Windows は新しい OS の API を使わないため、mock を追加しません。

Table: testfw の変更するファイル

| ファイル | 変更 |
| --- | --- |
| `framework/testfw/include/sys/mock_socket.h` | `mock_sendmmsg()` と `delegate_real_sendmmsg()` の宣言、`sendmmsg` の置き換えのマクロ、`Mock_sys_socket` の `MOCK_METHOD(int, sendmmsg, ...)` を、`sendto` と同じ形で追加します。 |
| `framework/testfw/libsrc/mock_libc/mock_sys_socket.cc` | `mock_sendmmsg()`、`delegate_real_sendmmsg()`、既定の委譲の `ON_CALL` を、`sendto` と同じ形で追加します。呼び出し元の記録 (`TRACE_SOCKET_CALL`) も同じです。 |

`sendmmsg()` の宣言は `_GNU_SOURCE` を必要とします。mock のヘッダーは、宣言の前にシステムのヘッダーで `sendmmsg` が宣言されていることを前提にし、実関数への委譲は `struct mmsghdr` をそのまま渡します。

## app/cplat: 登録したソケットの一括の送信

### 公開 API

`prod/include/cplat/net/socket.h` に、次の型、定数、関数を追加します。

```c
/** @ref cplat_socket_watch_sendto_multi へ一度に指定できるデータグラムの最大数。 */
#define CPLAT_SOCKET_SENDTO_MULTI_MAX 64U

/** @ref cplat_socket_watch_sendto_multi で送る 1 つのデータグラム。 */
typedef struct cplat_socket_datagram
{
    const void *buf;                      /**< 送信するデータ。 */
    size_t len;                           /**< buf のバイト数。CPLAT_SOCKET_MAX_TRANSFER 以下。 */
    const cplat_ipv4_endpoint *endpoint;  /**< 送信先のエンドポイント。 */
} cplat_socket_datagram;

CPLAT_EXPORT int CPLAT_API cplat_socket_watch_sendto_multi(cplat_socket_watch *watch,
                                                           const cplat_socket_datagram *datagrams, size_t count,
                                                           size_t *sent_count_out, cplat_error *detail_out);
```

Table: cplat_socket_watch_sendto_multi の契約

| 項目 | 内容 |
| --- | --- |
| 動作 | 登録したソケットから、`datagrams` の先頭から順に `count` 個のデータグラムを、待たずに送ります。各データグラムは、それぞれの送信先へ 1 つのデータグラムとして送ります。 |
| `count` | 1 以上 `CPLAT_SOCKET_SENDTO_MULTI_MAX` 以下。 |
| `*sent_count_out` | 先頭から連続して全量を送れたデータグラムの数。失敗時も格納します (0 以上 `count` 以下)。 |
| `CPLAT_OK` | 1 個以上を送れた場合。`*sent_count_out` が `count` 未満の場合、残りは送っていません。残りが送れなかった理由は返しません。 |
| `CPLAT_ERR_WOULD_BLOCK` | 先頭のデータグラムで送信の空きがなかった場合。何も送っていません。 |
| `CPLAT_ERR_INVALID_ARGUMENT` | `watch`、`datagrams`、`sent_count_out` が NULL、`count` が範囲の外、いずれかの要素の `buf` または `endpoint` が NULL、`len` が `CPLAT_SOCKET_MAX_TRANSFER` を超える場合。何も送りません。 |
| そのほかの結果コード | 先頭のデータグラムの送信が OS の失敗となった場合。`detail_out` に OS のエラーを格納します。 |
| スレッド セーフ | `cplat_socket_watch_wait_writable()` と同じ送信側の規則です。 |

> [!NOTE]
> 登録していないソケット向けの `cplat_socket_sendto` に対応する一括の関数は追加しません。porter の UDP のソケットはすべて登録したソケットであり、登録していないソケットの一括の送信の利用者がないためです。

### Linux の実装 (prod/libsrc/cplat/net/socket_watch_linux.c)

- 引数を検査した後、スタック上の `struct mmsghdr` と `struct iovec` と `struct sockaddr_in` の配列 (要素数 `CPLAT_SOCKET_SENDTO_MULTI_MAX`) に、各データグラムを設定します。
- `sendmmsg(sock, msgs, count, MSG_NOSIGNAL)` を呼びます。`-1` で `errno` が `EINTR` の場合は、呼び直します。
- `-1` で `EAGAIN` または `EWOULDBLOCK` の場合は `CPLAT_ERR_WOULD_BLOCK`、そのほかの `errno` は `cplat_internal_error_report_socket_errno()` で結果コードへ変換します。
- 戻り値が 1 以上の場合は、先頭から `msg_len` が `len` と等しいデータグラムを数えて `*sent_count_out` とします。数が 0 の場合 (先頭が短い送信) は `CPLAT_ERR_UNKNOWN` (詳細 `EIO`) を返します。
- コメントに `see: https://man7.org/linux/man-pages/man2/sendmmsg.2.html` を残し、途中で失敗した場合に送れた数だけを返し、失敗の理由は返らないことを書きます。

### Windows の実装 (prod/libsrc/cplat/net/socket_watch_windows.c)

- 引数を検査した後、既存の `send_once()` (送信可能の世代を記録する、非ブロッキングの 1 回の送信) を、`use_sendto` を指定して先頭から順に呼びます。
- 2 個目以降で失敗した場合 (`WSAEWOULDBLOCK` を含む) は、そこで止め、送れた数で `CPLAT_OK` を返します。`WSAEWOULDBLOCK` で止めた場合の送信可能の状態の更新は、`send_once()` が行います。
- 全量を送れなかったデータグラムは、送れなかったものとして扱い、そこで止めます。
- 関数の前のコメントに、Registered I/O を使わない理由を、次の根拠とともに残します。
    - `WSA_FLAG_REGISTERED_IO` 付きの UDP ソケットでは、`WSAEventSelect()`、`sendto`、`recvfrom`、`select` が `WSAENOTSOCK` で失敗すること (Windows 11 10.0.26300 での実測、2026-11-25)。
    - `see: https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_riosendex`
    - `see: https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_riocreaterequestqueue`

### mock_cplat

`create-mock-cplat-mock` スキルの手順で、`cplat_socket_watch_sendto_multi` の mock を追加します。既定は実関数への委譲です。

Table: mock_cplat の変更するファイル

| ファイル | 変更 |
| --- | --- |
| `test/include/mock_cplat.h` | `MOCK_CPLAT_LINK_IMPL(cplat_socket_watch_sendto_multi)`、`delegate_real_cplat_socket_watch_sendto_multi()` の宣言、`MOCK_METHOD` |
| `test/libsrc/mock_cplat/mock_cplat.cc` | 既定の委譲の `ON_CALL` |
| `test/libsrc/mock_cplat/net/mock_cplat_socket_watch_sendto_multi.cc` (新規) | `cplat_socket_watch_sendto` の mock と同じ形の実装 |

### 単体テスト (test/src/libcplatTest/net/socketTest/socketTest.cc)

Table: cplat の追加する単体テスト

| テスト | OS | 確認する内容 |
| --- | --- | --- |
| `watch_sendto_multi_sends_each_datagram_to_its_endpoint` | 両方 | 2 つの受信ソケットへ、長さの異なる 3 個のデータグラムを 1 回で送り、`CPLAT_OK`、送れた数 3、各受信ソケットで同じ内容と順序で受け取ること |
| `watch_sendto_multi_rejects_invalid_arguments` | 両方 | NULL の各引数、`count` の 0 と最大を超える値、要素の `buf` と `endpoint` の NULL、`len` の超過で `CPLAT_ERR_INVALID_ARGUMENT` と送れた数 0、OS の送信を呼ばないこと |
| `watch_sendto_multi_returns_partial_count` | Linux | `sendmmsg` の mock が 2 を返す (要求 4) と、`CPLAT_OK` と送れた数 2 |
| `watch_sendto_multi_returns_would_block_when_first_blocks` | 両方 | Linux は `sendmmsg` が `-1`、`EAGAIN`、Windows は先頭の `sendto` が `WSAEWOULDBLOCK` のとき、`CPLAT_ERR_WOULD_BLOCK` と送れた数 0 |
| `watch_sendto_multi_retries_after_interrupt` | Linux | `sendmmsg` が 1 回 `-1`、`EINTR` を返した後に成功すると、呼び直して `CPLAT_OK` |
| `watch_sendto_multi_reports_first_error` | 両方 | 先頭の送信が OS の失敗の場合に、対応する結果コードと `detail_out` の OS のエラー |
| `watch_sendto_multi_stops_at_failed_datagram` | Windows | 2 個目の `sendto` が失敗すると、`CPLAT_OK`、送れた数 1、3 個目を送らないこと。2 個目が `WSAEWOULDBLOCK` の場合は、続く `cplat_socket_watch_wait_writable(..., CPLAT_NO_WAIT, ...)` が送信の空きなしを返すこと |
| `watch_sendto_multi_counts_only_full_datagrams` | Linux | `sendmmsg` の mock が 2 を返し、2 個目の `msg_len` が短い場合に、送れた数 1 |

`exportTest/exportTest.cc` の `CPLAT_EXPORT_TABLE_COMMON` に `cplat_socket_watch_sendto_multi` を追加します。

### 文書

Table: cplat の変更する文書

| ファイル | 変更 |
| --- | --- |
| `prod/include/cplat/net/socket.h` | 新しい関数の Doxygen コメント (上の契約の表の内容、プラットフォームの差、Windows で Registered I/O を使わない理由の要旨と `see:` の URL) |
| `docs/api-cheatsheet.md` | 「登録中の送受信と接続」の行に `cplat_socket_watch_sendto_multi(...)` を加え、Linux の `sendmmsg()` との対応を加えます。 |
| `docs/functional-spec/net.md` | 機能要件に、`CPLAT-NET-FUNC-010` (複数のデータグラムを、それぞれの送信先へ 1 回の呼び出しで送り、先頭から送れた数を返す) を UUID 付きで追加し、「送受信と待機」の節に振る舞いと、Windows では 1 個ずつの送信の繰り返しになるプラットフォームの差を書きます。UUID は実装の時点で生成します。 |

## app/porter: UDP の一括の送信

### 共通のまとめ送りの資源

TCP のまとめ送りの定数とバッファーを、UDP と共用する名前へ改めます。

Table: 改める名前

| 変更の前 | 変更の後 | 場所 |
| --- | --- | --- |
| `POTR_TCP_BATCH_MAX_PACKETS` | `POTR_SEND_BATCH_MAX_PACKETS` | `prod/include_internal/porter/thread/potr_send_thread.h` |
| `POTR_TCP_BATCH_MAX_BYTES` | `POTR_SEND_BATCH_MAX_BYTES` | 同上 |
| `POTR_TCP_BATCH_MIN_PACKETS` | `POTR_SEND_BATCH_MIN_PACKETS` | 同上 |
| `tcp_batch_plain_buf`、`tcp_batch_wire_buf`、`tcp_batch_cap` | `send_batch_plain_buf`、`send_batch_wire_buf`、`send_batch_cap` | `prod/include_internal/porter/potr_context.h` |

- 値は変えません (最大 16 個、連結の上限 65,536 byte、最小 2 個)。`POTR_SEND_BATCH_MAX_PACKETS` と `POTR_MAX_PATH` の積が受信側の重複検出の範囲より小さいことの `static_assert` は、そのまま残します。UDP の複数経路でも、経路ごとの送出の順序のずれは最大でこの数です。
- `prod/libsrc/porter/api/potr_service_open.c` のバッファーの確保を、TCP だけでなく、送信スレッドを起動する UDP の通信種別 (DATA を送る側) にも行います。確保の数の計算 (`POTR_SEND_BATCH_MAX_BYTES / crypto_buf_size` を最大数で切り詰め、最小数未満なら確保せずにまとめ送りを使わない) は TCP と同じです。
- `prod/libsrc/porter/api/api_context_cleanup.c` の解放も、名前を改めるだけです。
- 名前を改めた定数とフィールドを参照する、テストと mock の箇所も改めます。

### ソケットの入出力 (prod/libsrc/porter/infra/potr_socket_io.c と .h)

次の関数を追加します。

```c
extern int potr_internal_udp_sendto_multi(potr_context *ctx, int path_idx, const cplat_socket_datagram *datagrams,
                                          size_t count, int *results_out);
```

Table: potr_internal_udp_sendto_multi の契約

| 項目 | 内容 |
| --- | --- |
| 排他 | `udp_send_mutex[path_idx]` を、呼び出しの全体で保持します。 |
| 一括の送信 | `cplat_socket_watch_sendto_multi()` で、まだ送っていない先頭から送ります。`CPLAT_ERR_WOULD_BLOCK` の場合は、`potr_internal_udp_sendto()` と同じく、最初の試行から `POTR_UDP_SEND_TIMEOUT_MS` の絶対期限の中で、送信可能を待って送り直します。 |
| 一部だけ送れた場合 | 一括の送信が `count` 未満の数を返した場合は、残りのデータグラムを、先頭から順に 1 個ずつの送信で 1 回ずつ試みます。1 個ずつの送信の待機と期限の規則は `potr_internal_udp_sendto()` と同じで、期限はデータグラムごとに数えます。 |
| `results_out` | 要素数 `count`。各データグラムの結果 (`POTR_OK`、`POTR_ERR_TIMEOUT`、`POTR_ERR_CANCELED`、`POTR_ERR_IO`) を格納します。 |
| 戻り値 | 引数が正しければ `POTR_OK` (各データグラムの成否は `results_out`)。不正なら `POTR_ERR_INVALID_ARGUMENT`。 |

既存の `potr_internal_udp_sendto()` の送信と待機の繰り返しは、排他を持った状態で呼べる内部の関数へ移し、両方の関数から使います。`potr_internal_udp_sendto()` の振る舞いは変えません。

`test/include/mock_porter.h` と `test/libsrc/mock_porter/infra/mock_potr_internal_udp_sendto_multi.cc` (新規) に、既存の `potr_internal_udp_sendto` と同じ形の mock を追加します。

### 送信スレッド (prod/libsrc/porter/thread/potr_send_thread.c)

#### 送出 (1 対 1)

- `send_thread_func()` の 1 対 1 の分岐で、まとめ送りの条件を、TCP と UDP の両方に適用します。条件は TCP と同じで、`send_batch_cap` が最小数以上で、先頭のエントリが分割されたメッセージの断片 (`POTR_FLAG_MORE_FRAG`) であるか、パッキングの待ちの時間 (`pack_wait_ms`) が 0 の場合です。それ以外は、従来の 1 個の外側パケットの送出 (`flush_packed()`) です。
- `send_tcp_batch()` を、通信種別によらない `send_batch()` へ改めます。外側パケットの組み立ての規則 (断片は 1 件で 1 つの外側パケット、断片でないエントリは待ちなしで後続の単体エントリを詰める)、キューが空になった時点で集めた分を送ること、送出上限までに送出できる数 (`allowance`) を超えないことは、TCP と同じです。組み立てた後、TCP は `tcp_flush_packets()` を、UDP は新しい `udp_flush_packets()` を呼びます。
- 新しい `udp_flush_packets(ctx, payload_base, payload_stride, payload_lens, n_packets)` は、`flush_packed()` の UDP の処理を n 個に広げたものです。

Table: udp_flush_packets の処理

| 順序 | 処理 |
| --- | --- |
| 1 | `send_window_mutex` を取得し、経路の選択 (`potr_internal_path_group_select()`) を行います。切断中や失敗は `flush_packed()` と同じ結果で返します。 |
| 2 | 次の通番から連番で、n 個の外側パケットを構築します。 |
| 3 | `unicast_bidir` と `unicast_bidir_n1` で送出上限を相乗りさせる場合は、相乗りの値を 1 回だけ求め、先頭の外側パケットに載せます。 |
| 4 | 選択した経路ごとに、n 個を `send_batch_wire_buf` の n 個の区画へ封緘し、`potr_internal_udp_sendto_multi()` で送ります。封緘に失敗した経路では、何も送りません。最初に送る経路の封緘に失敗した場合は、送信ウィンドウに登録せず、通番も進めずに、その結果を返します。 |
| 5 | 最初の経路の送出を試みた後、n 個すべてを送信ウィンドウに登録します (送れたかどうかによらない。UDP の送出の失敗は再送で回復する、現在の契約)。 |
| 6 | 先頭の外側パケットがどれかの経路で送れた場合に限り、相乗りを記録済みにします。 |
| 7 | 経路ごとに 1 個以上送れた場合は、その経路の有効な DATA の送出の時刻 (`last_valid_data_send_ms`) を、片方向の 1 対 1 の UDP で更新します。`unicast` と `unicast_bidir` の `flow_last_data_send_ms` の更新も、`flush_packed()` と同じ規則です。 |

#### 送出 (N 対 1)

- `send_packed_peer_mode()` で、先頭のエントリの宛先のピアが保留されない場合に、同じピア宛てに続くエントリを、1 対 1 と同じ組み立ての規則で、最大 `send_batch_cap` 個の外側パケットにまとめます。別のピア宛てのエントリが来た時点、キューが空になった時点、上限に達した時点で、集めた分を送ります。
- まとめる数は、そのピアの送出上限までに送出できる数を超えません。そのため、`potr_internal_flow_control_peer_state()` (`prod/include_internal/porter/potr_flow_control.h`、`prod/libsrc/porter/potr_flow_control.c`) に、送出上限までに送出できるパケット数の出力の引数 (NULL 可) を加え、`peer_send_deferred()` から受け取ります。
- 新しい `udp_flush_packets_peer()` は、`flush_packed_peer()` を n 個に広げたものです。`peers_mutex` を持ったまま、そのピアの `send_window_mutex` の下で、通番の付与、相乗り (先頭のパケットに 1 回)、経路ごとの封緘と `potr_internal_udp_sendto_multi()`、送信ウィンドウへの登録、`flow_last_data_send_ms` の更新、`potr_internal_flow_control_peer_sync()` を行います。ロックの順序 (`peers_mutex` → `peer->send_window_mutex` → フロー制御の排他) は変えません。
- 保留していたエントリの送出 (`send_deferred_entry()`) は、従来どおり 1 個ずつです。

#### 変えないこと

- 外側パケットの形式、通番の規則、封緘の形式、送信ウィンドウ、再送、フロー制御の判定の位置、ヘルス チェックの規則は変えません。受信側は変更しません。
- 待ち合わせを指定した送信の「最後の断片の送出の試行が終わるまで待つ」契約は、まとめ送りの完了の後に、取り出したエントリの完了を記録することで保ちます (TCP と同じ)。

### 単体テスト

Table: porter の追加・変更する単体テスト

| ファイル | テスト | 確認する内容 |
| --- | --- | --- |
| `test/src/libporter/potrSocketIoTest/potrSocketIoTest.cc` | `udp_sendto_multi_sends_all_in_one_call` | `cplat_socket_watch_sendto_multi` が 1 回呼ばれ、全要素が `POTR_OK` |
| 同上 | `udp_sendto_multi_waits_and_retries_on_would_block` | 先頭で `CPLAT_ERR_WOULD_BLOCK` の後、送信可能を待って残りを送ること |
| 同上 | `udp_sendto_multi_sends_rest_one_by_one_after_partial` | 一括の送信が 2 (要求 4) を返すと、残りの 2 個を `cplat_socket_watch_sendto` で 1 回ずつ送り、その結果を各要素に格納すること |
| 同上 | `udp_sendto_multi_times_out` | 送信可能にならない場合に、期限で `POTR_ERR_TIMEOUT` を格納すること |
| 同上 | `udp_sendto_multi_rejects_invalid_arguments` | 不正な引数で `POTR_ERR_INVALID_ARGUMENT` |
| `test/src/libporter/potrSendThreadTest/potrSendThreadTest.cc` (UDP のまとめ送りの fixture を新設) | `udp_fragmented_message_sent_in_one_call` | 5,000 byte のメッセージの 4 個の断片が、連番の 4 個のデータグラムとして 1 回で送られ、送信ウィンドウに 4 個登録されること |
| 同上 | `udp_messages_across_boundaries_sent_in_one_call` | 複数のメッセージが 1 回にまとまること |
| 同上 | `udp_packets_over_cap_split_into_next_call` | 上限を超える分が次の呼び出しになること |
| 同上 | `udp_batch_is_limited_by_send_limit_allowance` | 送出上限までに送出できる数を超えないこと |
| 同上 | `udp_encrypted_batch_seals_each_packet` | 暗号化で、各データグラムが個別に封緘され、受信側の開封で元の外側パケットに戻ること |
| 同上 | `udp_two_paths_send_same_packets_per_path` | 経路ごとに 1 回ずつ、同じ通番の n 個を送ること |
| 同上 | `udp_partial_failure_registers_all_packets` | 一部のデータグラムが失敗しても、n 個すべてを送信ウィンドウに登録し、エントリを成功として完了すること |
| 同上 | `udp_seal_failure_on_first_path_sends_nothing` | 最初の経路の封緘の失敗で、何も送らず、通番を進めないこと |
| 同上 | `udp_piggyback_on_first_packet_only` | `unicast_bidir` で、送出上限を先頭のパケットにだけ載せ、送れた場合に限り記録すること |
| 同上 | `udp_single_packet_uses_single_packet_path` | 1 個の場合と、パッキングの待ちがある場合に、従来の 1 個の送出になること |
| 同上 | `n1_same_peer_entries_batched_and_other_peer_ends_batch` | N 対 1 で、同じピア宛ての断片が 1 回にまとまり、別のピア宛てで区切られること |
| 同上 | `n1_batch_is_limited_by_peer_allowance` | N 対 1 で、ピアの送出上限までに送出できる数を超えないこと |

既存のテストのうち、UDP の DATA の送出を `potr_internal_udp_sendto` の呼び出しで確かめているもの (`unicast_bidir_piggybacks_send_limit_on_data`、`unicast_bidir_n1_piggybacks_send_limit_on_data`、`oneway_unicast_updates_data_send_time_without_health_wake`、`failed_data_send_does_not_record_piggyback`、`all_path_send_failure_updates_flow_last_send_only_and_preserves_on_clock_failure` など) は、1 個の外側パケットの送出が従来の経路を通る条件のテストとして残します。まとめ送りの経路を通るようになって失敗するテストは、確かめる性質を変えずに、`potr_internal_udp_sendto_multi` の呼び出しで確かめる形へ改めます。

`potr_flow_control` の `potr_internal_flow_control_peer_state()` の引数の追加に合わせて、`test/src/libporter/potrFlowControlTest/` の該当するテストを改め、送出上限までに送出できる数の出力を確かめるテストを 1 つ加えます。

### 文書とコード コメント

Table: porter の変更する文書とコメント

| ファイル | 変更 |
| --- | --- |
| `prod/libsrc/porter/thread/potr_send_thread.c` の先頭のコメント | 「まとめ送り (TCP 1:1)」の節を、TCP と UDP のまとめ送りの節へ改め、UDP では一括の送信 (Linux は `sendmmsg()`、Windows は 1 個ずつの繰り返し) で送ること、N 対 1 は同じピア宛てに限ること、一部だけ送れた場合は残りを 1 個ずつ 1 回ずつ試みることを書きます。 |
| `prod/include_internal/porter/thread/potr_send_thread.h` | 改めた定数の Doxygen コメントを、TCP と UDP の両方の説明にします。 |
| `prod/include_internal/porter/infra/potr_socket_io.h` | 新しい関数の Doxygen コメント (上の契約の表の内容) |
| `docs/architecture.md` | 送信スレッドの説明に、UDP のまとめ送りの規則 (まとめる範囲、上限、送出上限との関係、経路ごとの送信、一部だけ送れた場合) を加え、「TCP のまとめ送りは、送出上限までに…」の箇条を、TCP と UDP の両方の記述へ改めます。 |
| `docs/protocol.md` | 送出上限の判定の箇条の「TCP のまとめ送りは、…」を、TCP と UDP のまとめ送りの記述へ改めます。通信の形式は変わらないため、ほかの節は変えません。 |

porter の公開 API (`prod/include/`) と機能仕様 (`docs/functional-spec/`) は変えません。利用側から見た機能と要件は変わらないためです。

## 受入条件

Table: 受入条件

| 番号 | 条件 |
| --- | --- |
| 1 | Linux (WSL) のループバックで、`porter-bench` の `p1` と `p5` (`--tcp-type oneway`) の処理件数の比が、待ち合わせを指定した (`--flags blocking`) 5,000 byte で 0.5 以上である。 |
| 2 | Linux で、`p1` のほかの 5 条件 (64 byte と 1,000 byte の `normal` と `blocking`、5,000 byte の `normal`) の処理件数が、変更の前より、測定の変動の幅を超えて下がらない。 |
| 3 | Linux で、`p2` (N 対 1)、`p6` (`multicast`、受信者 3)、`p7` (N 対 1、停止クライアント 1) の処理件数が、変更の前より、測定の変動の幅を超えて下がらない。`p6` と `p7` の健全な受信者とクライアントの切断の回数が、変更の前より増えない。 |
| 4 | Windows で、条件 1 から 3 と同じ条件 (`p1` の 6 条件、`p2`、`p6`、`p7`) の処理件数が、変更の前より、測定の変動の幅を超えて下がらない。`tcp` との比を記録する。 |
| 5 | Linux と Windows の両方で、`framework/testfw`、`app/cplat`、`app/porter` のビルドと、`app/cplat` と `app/porter` の `make test` が成功し、追加した単体テストがすべて成功する。対象の範囲の `.warn` に、この案件の変更による警告がない。 |
| 6 | `python3 app/general/bin/check_functional_spec.py` が成功し、変更した Markdown の `text_style_jp.py --dry-run` に指摘がない。 |
| 7 | main へ push した後の CI (Linux と Windows のビルドとテスト) が成功する。 |
| 8 | 対策元の残課題一覧の BL-010 の状態と対処が更新され、この案件の残課題一覧が整理されている。 |

「測定の変動の幅を超えて下がる」は、変更の前と変更の後の、3 回の測定の値の幅 (最小から最大) が重ならず、変更の後が低い場合とします。

> [!NOTE]
> Windows は、送出の方式が変更の前と同じ 1 個ずつの `sendto` の繰り返しのため、処理件数が上がることは受入条件にしていません。上がることの確認と、暗号化と複数経路の性能の測定は、この案件の残課題一覧に記録しています。

## 検証手順

### ビルドとテスト (Linux)

同時に走らせる make は 1 本とします。サブモジュールの順にビルドします。mock のヘッダーを変えたため、提供元をビルドしてから利用側をテストします。

```bash
make -C framework/testfw
make -C app/cplat
make -C app/cplat/test/src/libcplatTest/net/socketTest test
make -C app/cplat/test/src/libcplatTest/exportTest test
make -C app/cplat test
make -C app/porter
make -C app/porter/test/src/libporter/potrSocketIoTest test
make -C app/porter/test/src/libporter/potrSendThreadTest test
make -C app/porter/test/src/libporter/potrFlowControlTest test
make -C app/porter test
python3 app/general/bin/check_functional_spec.py
find framework/testfw app/cplat app/porter -type f -name '*.warn' -size +0 -print
```

### ビルドとテスト (Windows)

Windows 側の作業ツリーで、Windows のセッションが、環境の設定の後に、Linux と同じ順序と引数で `make` を実行します (`framework/testfw` の Linux 専用の mock の追加は、Windows のビルドに影響しないことを確かめます)。主エージェントが、Linux で完成した差分を Windows のセッションへ渡し、Windows で直した差分を Linux 側へ戻します。戻した差分は、Linux で上の手順を再実行して確かめます。

### 性能の測定

`docs/performance-measurement.md` の「変更の前後を比べる手順」に従い、変更の前 (`base`) と変更の後 (`target`) をビルドして退避します。cplat も変更の対象のため、Linux は `libcplat.so`、Windows は `cplat.dll` も、`porter-bench` と同じディレクトリへ退避します。ビルドは作業ツリーの複製 (セッションの一時領域) で行い、リポジトリのビルドの成果物を置き換えません。ほかの測定、ビルド、テストを同時に動かしません。

1. `p1` と `p5` の 6 条件: 次の 18 回の実行を 1 回の測定とし、3 回繰り返します。各実行は 5 測定 (`--runs 5`) の中央値を使い、表の値は 3 回の中央値とします。

    ```bash
    for bytes in 64 1000 5000; do
      for flags in normal blocking; do
        base/porter-bench   --cond p1 --bytes $bytes --flags $flags --runs 5 --trace-level none --label base
        target/porter-bench --cond p1 --bytes $bytes --flags $flags --runs 5 --trace-level none --label target
        target/porter-bench --cond p5 --tcp-type oneway --bytes $bytes --flags $flags --runs 5 --trace-level none --label tcp
      done
    done
    ```

2. `p2`、`p6`、`p7`: 既定の引数で、`base` と `target` を交互に 3 回ずつ実行します。

    ```bash
    for cond in p2 p6 p7; do
      base/porter-bench   --cond $cond --runs 5 --trace-level none --label base
      target/porter-bench --cond $cond --runs 5 --trace-level none --label target
    done
    ```

3. 結果は、全体管理文書の「反映と検証の記録」に、条件ごとの値、3 回の幅、比、判定を表で記録します。比較の参考の値として、変更の前の版の前回の測定 (Linux、`p1` ÷ `p5`) は、64 byte が 1.03 (`normal`) と 1.06 (`blocking`)、1,000 byte が 1.20 と 1.09、5,000 byte が 0.62 と 0.47 です。

Windows も、同じ手順を `porter-bench.exe` で行います。

## Git の運用

- ブランチを作らず、各サブモジュールの main へ直接コミットします (main へのコミットは事前に許可されています)。
- Git の操作 (ステージング、コミット、push) は主エージェントだけが行います。実装を担当するサブエージェントは、ステージング、コミット、スタッシュ、`git rm`、`git add -N` を行いません。
- push は、依存の順に、`framework/testfw`、`app/cplat`、`app/porter` の各サブモジュールの main へ行い、最後に親リポジトリでサブモジュールの参照を更新するコミットを main へ push します。各サブモジュールの作業 (ビルド、単体テスト、Windows の確認) が完了した時点で push し、CI を起動します。
- CI の失敗が一過性の場合は、根拠を確かめて再実行し、報告します。再発した場合は、永続的な問題として調べます。

## 知見の反映計画

Table: 知見の反映先

| 知見 | 反映先 |
| --- | --- |
| 登録したソケットの一括の送信の契約 (先頭から連続して送れた数、途中の失敗の理由は返らない、先頭の空きなし) | `app/cplat/prod/include/cplat/net/socket.h` の Doxygen コメント、`app/cplat/docs/functional-spec/net.md` |
| Windows で Registered I/O を使わない理由 (`WSA_FLAG_REGISTERED_IO` 付きのソケットで `WSAEventSelect`、`sendto`、`recvfrom` が `WSAENOTSOCK`) と根拠の URL | `app/cplat/prod/libsrc/cplat/net/socket_watch_windows.c` のコメント、`socket.h` のプラットフォームの差の記述 |
| `sendmmsg()` の途中の失敗の扱いと根拠の URL | `app/cplat/prod/libsrc/cplat/net/socket_watch_linux.c` のコメント |
| UDP のまとめ送りの規則 (範囲、上限、送出上限、経路ごと、N 対 1 は同じピア、一部だけ送れた場合) | `app/porter/prod/libsrc/porter/thread/potr_send_thread.c` の先頭のコメント、`app/porter/docs/architecture.md` |
| まとめ送りと送出上限の関係 | `app/porter/docs/protocol.md` |
| 一部だけ送れた場合の、ソケットの入出力の規則 | `app/porter/prod/include_internal/porter/infra/potr_socket_io.h` の Doxygen コメント |
