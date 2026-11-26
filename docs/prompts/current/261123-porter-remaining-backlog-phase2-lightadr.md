<!--
生成元: agree-before-implement スキル
文書種別: 簡易 ADR (lightadr)
最終更新日時: 2026-11-27 05:11:18 JST
最終更新エージェント: agy (gemini-4-neon-medium、主セッション session_01KJc53RTnwmpzd7VMLf1qR9)
稼働 OS: Linux (WSL)
上流文書: `261123-porter-remaining-backlog-phase2-overview.md` (全体管理文書)、`261123-porter-remaining-backlog-phase2-discussion.md` (検討用 Markdown)。いずれも削除されている場合があります。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。上流文書の全体管理文書が存在する場合は、その再開手順に従って進行位置を確認します。存在しない場合は、この文書を単体の決定事項の記録として扱います。この文書は正式な ADR ではありません。正式な ADR への転記はユーザーが必要時に行うため、エージェントは依頼なしに転記や正式な ADR の作成・更新を行わないでください。最終更新エージェントのセッション情報や稼働 OS・最終更新日時が参照可能であれば再開時のコンテキストや環境差分の把握に利用し、別エージェントや別環境・別 OS からの引き継ぎ、担当変更によりセッションが見つからない場合も異常とせず、文書の記載から継続してください。
-->

# porter の残課題の対策 (第 2 期) 簡易 ADR

ファイル名: `261123-porter-remaining-backlog-phase2-lightadr.md`

この文書は、検討で得られた事実、ユーザーの選択とコメント、およびそれらに基づく決定事項を、決定 1 件ごとにまとめた簡易 ADR (Architecture Decision Record: 設計判断の記録) です。正式な ADR ではなく、対案の比較や決定の影響の評価は記載していません。  
各決定の「決定」は、方針が変わるたびに書き換え、常に最新の決定だけを示します。変更の推移は「経緯」に時系列で記録します。

この文書の「残課題一覧」は、対策元の `261123-porter-remaining-backlog-backlog.md` を指します。  
最終確認: 2026-11-27 (作業 WBS 4.15 で LADR-001 から LADR-010 と実装・検証の結果の一致を確認済み)

## 決定事項一覧

Table: 決定事項一覧

| 決定 ID | 題名 | 最終更新日 | 源泉 |
| --- | --- | --- | --- |
| [LADR-001](#ladr-001-対策する残課題の範囲) | 対策する残課題の範囲 | 2026-11-25 | [Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲) |
| [LADR-002](#ladr-002-windows-での確認の方法と実装の担当) | Windows での確認の方法と実装の担当 | 2026-11-27 | [Q-002](261123-porter-remaining-backlog-phase2-discussion.md#q-002-windows-での確認の方法)、[Q-008](261123-porter-remaining-backlog-phase2-discussion.md#q-008-windows-の一括の送信の実装の担当) |
| [LADR-003](#ladr-003-一括の送信を適用する範囲) | 一括の送信を適用する範囲 | 2026-11-27 | [Q-003](261123-porter-remaining-backlog-phase2-discussion.md#q-003-bl-010-を適用する通信種別と経路の範囲)、[Q-014](261123-porter-remaining-backlog-phase2-discussion.md#q-014-n-対-1-で一括の送信にまとめる単位) |
| [LADR-004](#ladr-004-windows-の一括の送信の方式) | Windows の一括の送信の方式 | 2026-11-25 | [Q-004](261123-porter-remaining-backlog-phase2-discussion.md#q-004-bl-010-の-windows-での実装の方式)、[Q-009](261123-porter-remaining-backlog-phase2-discussion.md#q-009-registered-io-の実現性の確認の時期)、[Q-010](261123-porter-remaining-backlog-phase2-discussion.md#q-010-registered-io-が成立しない場合の扱い)、[Q-012](261123-porter-remaining-backlog-phase2-discussion.md#q-012-windows-で-registered-io-を使えるようにするソケットの範囲) |
| [LADR-005](#ladr-005-linux-の性能の受入の基準) | Linux の性能の受入の基準 | 2026-11-25 | [Q-005](261123-porter-remaining-backlog-phase2-discussion.md#q-005-bl-010-の性能の受入の基準) |
| [LADR-006](#ladr-006-一部だけ送れた場合の残りの扱い) | 一部だけ送れた場合の残りの扱い | 2026-11-27 | [Q-006](261123-porter-remaining-backlog-phase2-discussion.md#q-006-一括の送信で一部だけ送れた場合の残りの扱い) |
| [LADR-007](#ladr-007-サニタイザーを導入しない) | サニタイザーを導入しない | 2026-11-25 | [Q-007](261123-porter-remaining-backlog-phase2-discussion.md#q-007-サニタイザー-bl-009-の扱い) |
| [LADR-008](#ladr-008-cplat-に追加する一括の送信の-api-の形) | cplat に追加する一括の送信の API の形 | 2026-11-27 | [Q-011](261123-porter-remaining-backlog-phase2-discussion.md#q-011-cplat-に追加する一括の送信の-api-の形) |
| [LADR-009](#ladr-009-windows-の性能の受入の基準) | Windows の性能の受入の基準 | 2026-11-27 | [Q-013](261123-porter-remaining-backlog-phase2-discussion.md#q-013-windows-の性能の受入の基準)、自走の指示 |
| [LADR-010](#ladr-010-自走git-の運用最終仕様の承認) | 自走、Git の運用、最終仕様の承認 | 2026-11-27 | 対話での指示 (2026-11-25) |

## LADR-001 対策する残課題の範囲

決定者: Tetsuo Honda  
最終更新日: 2026-11-25

### 背景と課題

残課題一覧の 10 件 (BL-001 から BL-010) は、必要な環境 (Linux、Windows、複数のホスト) と作業の大きさが項目ごとに違います。この案件で対策する項目を決める必要がありました。

### 得られた事実

- BL-010 は、前の案件の一時領域の試作 (1 対 1 の `unicast`、経路 1 つ、最大 16 個を `sendmmsg()` で送る) で、待ち合わせを指定した 5,000 byte の `tcp` との比が 0.44 から 0.49 から、0.81 から 0.97 になりました (前の案件の全体管理文書の「反映と検証の記録」)。
- BL-001、BL-004、BL-006 は、前の案件で、保留、既知の特性、環境なしと決定済みです (残課題一覧)。

### 決定

- この案件で対策するのは、BL-010 (UDP で複数のパケットを 1 回の呼び出しで送る) だけです。
- BL-001 から BL-008 は、この案件の残課題一覧 (`261123-porter-remaining-backlog-phase2-backlog.md`) へ転記します。
- BL-009 は、[LADR-007](#ladr-007-サニタイザーを導入しない) で閉じます。

### 経緯

| 日時 (JST) | 源泉 | ユーザーの選択 | ユーザーのコメント | 決定への反映 |
| --- | --- | --- | --- | --- |
| 2026-11-25 20:34 | [Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲) | A: BL-010 だけ | なし | 新規に決定 |

## LADR-002 Windows での確認の方法と実装の担当

決定者: Tetsuo Honda  
最終更新日: 2026-11-27

### 背景と課題

AGENTS.md は、Linux/GCC と Windows/MSVC の双方で動作する構成の維持を求めています。この案件の変更の Windows (MSVC) での確認の方法を決める必要がありました。

### 得られた事実

- Windows 側には、このリポジトリとは別の作業ツリー (`/mnt/d/Users/tetsuo/Local/repos/c-modernization-kit`) があり、Claude Code のセッションが待機しています。主エージェントが、セッション間のパッチと結果の受け渡しを仲介できます (主エージェントからの指示、2026-11-25)。
- CI (GitHub Actions) は Windows のビルドとテストを含み、1 回に約 3 時間かかります。

### 決定

- Linux (WSL) のセッションで、Linux の実装、単体テスト、性能の測定を完成させた後に、Windows 側の Claude Code のセッションと、主エージェントの仲介によるセッション間通信で対話しながら、Windows (MSVC) のビルド、単体テスト、性能の測定を進めます。
- CI の Windows のジョブは、main へ入れた後の最終の確認とします。
- Linux のセッションが、cplat の API、Linux の実装、Windows の実装の初版 (`sendto` の繰り返し、[LADR-004](#ladr-004-windows-の一括の送信の方式))、testfw の mock、cplat と porter の単体テストを書きます。Windows のセッションは、主エージェントの仲介で差分を受け取り、MSVC のビルド、単体テスト、性能の測定と修正を行います。Windows で直した差分は Linux 側へ戻し、Linux で再確認します。

### 経緯

| 日時 (JST) | 源泉 | ユーザーの選択 | ユーザーのコメント | 決定への反映 |
| --- | --- | --- | --- | --- |
| 2026-11-25 20:34 | [Q-002](261123-porter-remaining-backlog-phase2-discussion.md#q-002-windows-での確認の方法) | D: その他 | 「Linux の完成後に、セッション間通信で対話し、進行する。」 | 新規に決定 (補足を、Windows 側の Claude Code のセッションとの対話で Windows の作業を進める意味と解釈) |
| 2026-11-25 20:59 | [Q-008](261123-porter-remaining-backlog-phase2-discussion.md#q-008-windows-の一括の送信の実装の担当) | A: Linux のセッションが Windows の実装の初版まで書き、Windows のセッションがビルド、テスト、測定と修正を行う | なし | 決定を補足 (実装の担当) |
| 2026-11-27 05:07 | 最終確認 (作業 WBS 4.11、4.12、4.15) | - | なし | 状態を更新 (Windows の実装は LADR-004 により `sendto` の繰り返しとなり、依頼 16 から 18 で Windows のビルド、単体テスト、性能の測定を完了。MSVC 向けの修正は不要) |

## LADR-003 一括の送信を適用する範囲

決定者: Tetsuo Honda  
最終更新日: 2026-11-27

### 背景と課題

porter の送信スレッドの UDP の DATA の送出は、1 対 1 の `flush_packed()` と、N 対 1 の `flush_packed_peer()` に分かれています。一括の送信を、どの範囲の送出に適用するかを決める必要がありました。

### 得られた事実

- `flush_packed()` は、`unicast`、`unicast_bidir`、`multicast`、`broadcast` が使い、経路ごとに封緘して送ります。`flush_packed_peer()` は、N 対 1 (`unicast_bidir_n1`) が使い、`peers_mutex` を持ったまま、ピアの `send_window_mutex` の下で送ります (`prod/libsrc/porter/thread/potr_send_thread.c`)。
- 前の案件の試作と測定は、経路 1 つ、暗号化なしの `unicast` だけです。N 対 1 の効果の見込みを示す測定はありません。

### 決定

- 一括の送信を、UDP の DATA の送出のすべてに適用します。対象は、1 対 1 の 4 つの通信種別 (暗号化と複数経路を含む) と、N 対 1 (`unicast_bidir_n1`) です。
- N 対 1 では、同じピア宛てに続く外側パケットだけを 1 回の一括の送信にまとめます。1 回の一括の送信は 1 つのピアの `send_window_mutex` の下で完結し、ロックの順序 (`peers_mutex` → `peer->send_window_mutex` → `fc->mutex`) は変えません。

### 経緯

| 日時 (JST) | 源泉 | ユーザーの選択 | ユーザーのコメント | 決定への反映 |
| --- | --- | --- | --- | --- |
| 2026-11-25 20:34 | [Q-003](261123-porter-remaining-backlog-phase2-discussion.md#q-003-bl-010-を適用する通信種別と経路の範囲) | C: N 対 1 も含める (推奨は B) | なし | 新規に決定 |
| 2026-11-25 20:59 | [Q-014](261123-porter-remaining-backlog-phase2-discussion.md#q-014-n-対-1-で一括の送信にまとめる単位) | A: 同じピア宛てに続く外側パケットだけをまとめる | なし | 決定を補足 (N 対 1 のまとめる単位) |
| 2026-11-27 05:07 | 実装時の具体化と最終確認 (作業 WBS 4.7、4.15) | - | なし | 決定を具体化 (N 対 1 のフロー制御 `potr_internal_flow_control_peer_state()` へ `allowance` の出力を追加するにあたり、ロックを取らない判定の記録 `flow_peer_ready` は残りの数を持たないため、`allowance` を求める呼び出しでは使わずロックの下で判定する。送信スレッドは `send_batch_cap` が最小数以上の場合だけ `allowance` を求め、送信 API は NULL を渡して従来の速い経路を保つ) |

## LADR-004 Windows の一括の送信の方式

決定者: Tetsuo Honda  
最終更新日: 2026-11-25

### 背景と課題

cplat に追加する「複数のデータグラムを 1 回で送る関数」の Windows の実装を、`sendto` の繰り返しにするか、Windows でも一括の送信にするかを決める必要がありました。

### 得られた事実

- Registered I/O (RIO) の `RIOSendEx()` は、バインドした UDP ソケットで 1 つのデータグラムを送る要求を積み、`RIO_MSG_DEFER` と起動の組み合わせで、複数の要求を 1 回の起動にまとめられます。送るデータと送信先のアドレスは登録済みのバッファーに置き、ソケットごとに要求キューと完了キューが必要です (Microsoft Learn の `RIOSendEx`、`RIOCreateRequestQueue`、`RIOCreateCompletionQueue` の頁、2026-11-25 に参照)。
- `TransmitPackets()` は既定の送信先を決めたソケット向けで、`UDP_SEND_MSG_SIZE` は 1 つのバッファーを同じ大きさで 1 つの送信先へ分けて送る機能です (Microsoft Learn、2026-11-25 に参照)。
- `cplat_socket_open()` の Windows の実装は `socket()` でソケットを作り、porter の UDP のソケットは `cplat_socket_watch_attach()` (`WSAEventSelect()`) に登録して送受信しています (`app/cplat/prod/libsrc/cplat/net/socket_windows.c`、`socket_watch_windows.c`)。
- RIO のソケットを、`WSAEventSelect()` と通常の Winsock の関数と併用できるかは、参照した資料に記載がありません。
- `framework/testfw` の `mock_winsock` に、`WSAIoctl()` と RIO の関数の mock はありません。
- Windows の試作 (2026-11-25、Windows 11 Pro for Workstations 10.0.26300): `WSA_FLAG_REGISTERED_IO` 付きの UDP ソケットでは、`bind`、`getsockname`、`getsockopt(SO_TYPE)` は成功し、`sendto`、`recvfrom`、`WSAEventSelect`、`select` は 10038 (`WSAENOTSOCK`)、`ioctlsocket(FIONBIO)` は 10045 で失敗しました。RIO の要求キューの作成の前でも同じです。フラグ `WSA_FLAG_OVERLAPPED` だけのソケットでは、すべて成功しました (全体管理文書の「反映と検証の記録」)。

### 決定

- Windows の一括の送信は、`sendto` の繰り返しとします。API の契約 (先頭から連続して送れた個数を返す) は Linux と同じです。
- Registered I/O は使いません。`WSA_FLAG_REGISTERED_IO` 付きのソケットでは、porter が使う `WSAEventSelect()`、`sendto`、`recvfrom` が使えないためです。Windows の UDP のソケットを常にこのフラグ付きで作る決定 (Q-012 の A) は、前提が成り立たないため適用しません。`cplat_socket_open()` は従来どおり `socket()` でソケットを作ります。
- 成立しなかった根拠 (Microsoft Learn の RIO の頁の URL と、試作の実測) を、cplat の Windows の実装のコメントと文書に残します。
- RIO の送信だけに使う専用のソケットを別に作る構成は、送信元のポートが分かれるため、この案件では扱わず、この案件の残課題一覧の BL-011 へ送ります。

### 経緯

| 日時 (JST) | 源泉 | ユーザーの選択 | ユーザーのコメント | 決定への反映 |
| --- | --- | --- | --- | --- |
| 2026-11-25 20:34 | [Q-004](261123-porter-remaining-backlog-phase2-discussion.md#q-004-bl-010-の-windows-での実装の方式) | B: Registered I/O などで、Windows でも一括の送信にする (推奨は A) | なし | 新規に決定 |
| 2026-11-25 20:59 | [Q-009](261123-porter-remaining-backlog-phase2-discussion.md#q-009-registered-io-の実現性の確認の時期) | A: 最終仕様の前に、Windows のセッションで試作して、併用の可否と効果を確かめる | なし | 決定を補足 (試作の実施) |
| 2026-11-25 20:59 | [Q-010](261123-porter-remaining-backlog-phase2-discussion.md#q-010-registered-io-が成立しない場合の扱い) | A: Windows は `sendto` の繰り返しにし、成立しなかった根拠を残す | なし | 決定を補足 (成立しない場合の扱い) |
| 2026-11-25 20:59 | [Q-012](261123-porter-remaining-backlog-phase2-discussion.md#q-012-windows-で-registered-io-を使えるようにするソケットの範囲) | A: Windows の UDP のソケットは常にフラグ付きで作り、キューとバッファーは初めての一括の送信の時点で作る | なし | 決定を補足 (ソケットの範囲) |
| 2026-11-25 21:48 | 自走の指示 (主エージェント経由) | - | 「決定した最終仕様が実現できない場合は、ユーザーに問い合わせることなく backlog に送る」 | 決定を補足 (RIO の専用の送信ソケットの代案を残課題一覧の BL-011 へ送る) |
| 2026-11-25 22:19 | Windows の試作の結果 (主エージェント経由) | - | 併用は不適合 (`WSAENOTSOCK`)。時間の比較は未実施 | 決定を変更 (Q-010 の A により、Windows は `sendto` の繰り返しに確定。Q-012 の A は前提不成立で適用しない) |

## LADR-005 Linux の性能の受入の基準

決定者: Tetsuo Honda  
最終更新日: 2026-11-25

### 背景と課題

BL-010 の対策の完了を判定する性能の基準を決める必要がありました。

### 得られた事実

前の案件の最終の測定 (Linux (WSL)、ループバック、送信スレッド 1 つ。各回 5 測定の中央値を交互に 3 回測った中央値) の `unicast` ÷ `tcp` の比は、64 byte が 1.03 (送信の指定なし) と 1.06 (待ち合わせを指定)、1,000 byte が 1.20 と 1.09、5,000 byte が 0.62 と 0.47 (幅 0.47–0.49) です (前の案件の全体管理文書の「反映と検証の記録」)。

### 決定

- Linux (WSL) の基準は、`porter-bench` の `unicast` (`p1`) と `tcp` (`p5`) の処理件数の比が、待ち合わせを指定した 5,000 byte で 0.5 以上であることです。
- ほかの 5 条件 (64 byte と 1,000 byte の送信の指定なしと待ち合わせを指定、5,000 byte の送信の指定なし) は、変更の前の測り直しの値より、測定の変動の幅を超えて下がらないことです。
- 追加した範囲 (暗号化、複数経路、`multicast`、N 対 1) も、変更の前より、測定の変動の幅を超えて下がらないことを確認します。
- 実際の値は、基準とは別に、すべて記録します。
- 暗号化と複数経路の性能は、`porter-bench` に暗号化と複数経路を指定する方法がないため、この案件では測定しません。正しさは単体テストで確かめ、性能の確認は、この案件の残課題一覧の BL-013 へ送ります。

### 経緯

| 日時 (JST) | 源泉 | ユーザーの選択 | ユーザーのコメント | 決定への反映 |
| --- | --- | --- | --- | --- |
| 2026-11-25 20:34 | [Q-005](261123-porter-remaining-backlog-phase2-discussion.md#q-005-bl-010-の性能の受入の基準) | A: 待ち合わせを指定した 5,000 byte の比が 0.5 以上、ほかの 5 条件は下がらない | なし | 新規に決定 (Q-005 の推奨の注記に従い、Q-003 の C で追加した範囲の「下がらない」の確認を含む) |
| 2026-11-25 21:48 | 自走の指示 (主エージェント経由) | - | 「決定した最終仕様が実現できない場合は、ユーザーに問い合わせることなく backlog に送る」 | 決定を補足 (`porter-bench` で測れない暗号化と複数経路の性能の確認を、残課題一覧の BL-013 へ送る) |

## LADR-006 一部だけ送れた場合の残りの扱い

決定者: Tetsuo Honda  
最終更新日: 2026-11-27

### 背景と課題

一括の送信が、渡したパケットの先頭の一部だけを送って戻った場合の、残りのパケットの扱いを決める必要がありました。

### 得られた事実

- `sendmmsg()` は、送れたデータグラムの個数を返し、途中で失敗するとそこで止まります。
- 現在の `flush_packed()` は、`sendto` が失敗したパケットも送信ウィンドウへ登録し、受信側の再送の要求で送り直します (`prod/libsrc/porter/thread/potr_send_thread.c`)。

### 決定

- 送れなかった残りのパケットは、1 個ずつの送出で、順に 1 回ずつ試みます。
- 複数経路では経路ごとに、N 対 1 ではピアごとの送出にも、同じ扱いを適用します。

### 経緯

| 日時 (JST) | 源泉 | ユーザーの選択 | ユーザーのコメント | 決定への反映 |
| --- | --- | --- | --- | --- |
| 2026-11-25 20:34 | [Q-006](261123-porter-remaining-backlog-phase2-discussion.md#q-006-一括の送信で一部だけ送れた場合の残りの扱い) | A: 残りを 1 個ずつの送出で順に 1 回ずつ試みる | なし | 新規に決定 |
| 2026-11-27 05:07 | 実装時の具体化と最終確認 (作業 WBS 4.6、4.15) | - | なし | 決定を具体化 (`potr_internal_udp_sendto_multi()` の一括の送信の待機が期限の超過または解除で終わった場合は、残りのデータグラムすべてに同じ結果を格納し、1 個ずつの送信を試みない。先頭のデータグラムが OS の送信失敗となった場合は、その結果を格納し、続くデータグラムを 1 個ずつ 1 回ずつ試みる) |

## LADR-007 サニタイザーを導入しない

決定者: Tetsuo Honda  
最終更新日: 2026-11-25

### 背景と課題

残課題一覧の BL-009 は、開発環境にアドレス サニタイザーとスレッド サニタイザーの実行時ライブラリがないことを記録した項目です。開発と CI の確認の手段として導入するかを決める必要がありました。

### 得られた事実

- 前の案件の SIGSEGV の再現の試験は、サニタイザーを使わない通常の構成で、416 回の実行で異常終了 0 回でした (前の案件の全体管理文書の「反映と検証の記録」)。
- 一時的に使う手順 (パッケージを一時領域へ展開する方法) は、残課題一覧の BL-009 に記録されています。

### 決定

- サニタイザーは導入しません。BL-009 は閉じ、この案件の残課題一覧へは転記しません。

### 経緯

| 日時 (JST) | 源泉 | ユーザーの選択 | ユーザーのコメント | 決定への反映 |
| --- | --- | --- | --- | --- |
| 2026-11-25 20:34 | [Q-007](261123-porter-remaining-backlog-phase2-discussion.md#q-007-サニタイザー-bl-009-の扱い) | A: 導入しないと決めて、BL-009 を閉じる | なし | 新規に決定 |

## LADR-008 cplat に追加する一括の送信の API の形

決定者: Tetsuo Honda  
最終更新日: 2026-11-27

### 背景と課題

cplat の net カテゴリに追加する一括の送信の API で、Registered I/O の登録済みのバッファーを呼び出し元に見せるかどうかを決める必要がありました。

### 得られた事実

- porter の UDP の送出は、登録したソケットへの `cplat_socket_watch_sendto()` で行い、送信の空きがない場合は porter が `cplat_socket_watch_wait_writable()` で待って送り直します (`app/porter/prod/libsrc/porter/infra/potr_socket_io.c` の `potr_internal_udp_sendto()`)。
- Linux の `sendmmsg()` は、呼び出し元のバッファーを渡し、先頭から送れた個数を返す同期の関数です。Registered I/O は、データと送信先を登録済みのバッファーに置き、完了を回収する仕組みです (Microsoft Learn、2026-11-25 に参照)。

### 決定

- 登録したソケット (`cplat_socket_watch`) に、呼び出し元のバッファー、長さ、送信先の配列を渡し、先頭から連続して送れた個数を返す同期の関数を追加します。
- Linux は `sendmmsg()` で送ります。Windows は、Registered I/O が使えないため ([LADR-004](#ladr-004-windows-の一括の送信の方式))、`sendto` を先頭から順に繰り返します。登録済みのバッファーへのコピーはありません。
- 関数名、構造体名、定数名は、`cplat_socket_watch_sendto_multi()`、`cplat_socket_datagram`、`CPLAT_SOCKET_SENDTO_MULTI_MAX` (64) とします。

### 経緯

| 日時 (JST) | 源泉 | ユーザーの選択 | ユーザーのコメント | 決定への反映 |
| --- | --- | --- | --- | --- |
| 2026-11-25 20:59 | [Q-011](261123-porter-remaining-backlog-phase2-discussion.md#q-011-cplat-に追加する一括の送信の-api-の形) | A: 同期の API。呼び出し元のバッファーを渡し、Windows は cplat が内部の登録済みのバッファーへコピーする | なし | 新規に決定 |
| 2026-11-25 22:19 | Windows の試作の結果 (主エージェント経由) | - | Registered I/O のソケットでは `WSAEventSelect()` と `sendto` が使えない | 決定を変更 (Windows の実装を `sendto` の繰り返しにし、登録済みのバッファーへのコピーをなくす。API の形は変えない) |
| 2026-11-27 05:07 | 実装時の具体化と最終確認 (作業 WBS 4.1、4.2、4.7、4.15) | - | なし | 決定を具体化・状態を更新 (API 名を確定。`test/src/integration/porterCancelFailureTest` は cplat の `socket_watch_linux.c` を取り込んでコンパイルするため、`struct mmsghdr` の宣言に必要な `_GNU_SOURCE` を同テストの `makepart.mk` へ追加。`framework/testfw/include/sys/mock_socket.h` では `_GNU_SOURCE` のない翻訳単位向けに `struct mmsghdr` を前方宣言) |

## LADR-009 Windows の性能の受入の基準

決定者: Tetsuo Honda  
最終更新日: 2026-11-27

### 背景と課題

Linux の性能の基準 (LADR-005) は Linux (WSL) の測定を前提にしています。Windows も一括の送信にするため、Windows の基準を決める必要がありました。

### 得られた事実

- Windows の `unicast` と `tcp` の比は、記録がありません。
- Windows の圧縮の条件では、同じ版でも測定ごとに処理件数が大きく変わる事象があります (残課題一覧の BL-005)。
- Windows の一括の送信は、Registered I/O が使えないため、変更の前と同じ `sendto` の繰り返しになりました ([LADR-004](#ladr-004-windows-の一括の送信の方式))。

### 決定

- Windows では、変更の前と変更の後を、各回 5 測定の中央値を交互に 3 回測ります。
- ユーザーが決めた基準 (待ち合わせを指定した 5,000 byte の処理件数が、測定の変動の幅を超えて上がる) は、Windows の送出の方式が変更の前と同じ `sendto` の繰り返しのため、この案件では受入条件とせず、この案件の残課題一覧の BL-012 へ送ります。
- この案件での Windows の受入は、すべての条件で、処理件数が変更の前より測定の変動の幅を超えて下がらないことの確認とします。
- `tcp` との比は記録します。

### 経緯

| 日時 (JST) | 源泉 | ユーザーの選択 | ユーザーのコメント | 決定への反映 |
| --- | --- | --- | --- | --- |
| 2026-11-25 20:59 | [Q-013](261123-porter-remaining-backlog-phase2-discussion.md#q-013-windows-の性能の受入の基準) | A: 変更の前より、待ち合わせを指定した 5,000 byte の処理件数が上がり、ほかの条件が下がらない | なし | 新規に決定 |
| 2026-11-25 21:48 | 自走の指示 (主エージェント経由) | - | 「決定した最終仕様が実現できない場合は、ユーザーに問い合わせることなく backlog に送る」 | 決定を補足 (満たせない「上がる」の基準を残課題一覧の BL-012 へ送り、この案件の Windows の受入を「下がらない」の確認にする) |
| 2026-11-27 05:07 | Windows の性能の測定と最終確認 (作業 WBS 4.12、4.15) | - | なし | 状態を更新 (依頼 18 の測定で 9 条件中 8 条件が適合し、`p1` 5,000 byte `blocking` は +20.8 [%] で向上。`p1` 1,000 byte `blocking` は 3 回測定で -1.8 [%] だったが追加 5 回交互測定で +0.05 [%] のため主エージェント判断で条件付きで適合とし、BL-015 へ起票。`tcp` との比は全 6 条件を記録済み) |

## LADR-010 自走、Git の運用、最終仕様の承認

決定者: Tetsuo Honda  
最終更新日: 2026-11-27

### 背景と課題

この案件の実装は、Linux (WSL) と Windows のセッション、複数のサブモジュールにまたがります。ユーザーの確認を待たずに進める範囲と、Git の書き込みの操作の許可を決める必要がありました。

### 得られた事実

- リポジトリの `AGENTS.md` は、ユーザーから個別に指示されるまで、ステージングとコミットを行わないことを求めています。Git の書き込みの操作の許可は、その指示に限る単発の許可として扱います。
- 変更するサブモジュールは、`framework/testfw`、`app/cplat`、`app/porter` です。`app/cplat` は `framework/testfw` の mock を、`app/porter` は `app/cplat` を使います。

### 決定

- ユーザーの確認を待たずに、作業を継続して進めます。
- 決定した最終仕様が実現できない場合は、ユーザーに問い合わせずに、この案件の残課題一覧へ送ります。
- 最終仕様 (第 2 ゲート) は、ユーザーの指示により承認済みとして扱います。
- main へのコミットは、事前に許可されています。各サブモジュールの作業の完了時に main へ push し、CI を起動します。
- Git の操作は、主エージェントだけが行います。サブエージェントは、ステージング、コミット、スタッシュ、`git rm`、`git add -N` を行いません。

### 経緯

| 日時 (JST) | 源泉 | ユーザーの選択 | ユーザーのコメント | 決定への反映 |
| --- | --- | --- | --- | --- |
| 2026-11-25 21:48 | 対話での指示 (主エージェント経由。ほかの案件と同じ文言) | - | 「ユーザーの確認なく継続的に自走すること」「決定した最終仕様が実現できない場合は、ユーザーに問い合わせることなく backlog に送る」「各サブモジュール (案件) 完了時に main にプッシュし CI を起動させ…」「main へのコミットを事前許可」 | 新規に決定 (第 2 ゲートを承認済みとして扱い、Git の操作は主エージェントだけが行う運用は、主エージェントの指示による) |
| 2026-11-27 05:07 | 最終確認 (作業 WBS 4.13、4.15) | - | なし | 状態を更新 (主エージェントが testfw `75cd774`、cplat `8252c026`、porter `063d8cc`、親リポジトリ `dbc28819` を main へ push し、CI `45888369745` を起動済み。CI の結果待ち) |
