<!--
生成元: agree-before-implement スキル
文書種別: 検討用 Markdown (discussion)
検討 ID: porter-transport-performance-20261115
版: 6
状態: 打ち切り (2026-11-17、ユーザー指示)。この案件は再開しません。やり直しの案件は `261117-porter-transport-performance-redo-discussion.md` (検討 ID: porter-transport-performance-redo-20261117) です。この文書の合意 (Q-001〜Q-007) は、やり直しの案件へ自動では引き継ぎません
最終更新日時: 2026-11-17 04:58:00 JST
最終更新エージェント: claude code / セッション ID: session_01KJc53TxVv1vjB6xMfjiUgc (打ち切りの記録だけを追記。本文は Codex による版 6 のまま)
稼働 OS: Windows
上流文書: `261115-porter-reject-recovery-backlog.md` (BL-001、BL-002)、`261115-porter-reject-recovery-discussion.md` (元案件完了確認済み)、`261115-porter-handle-lifecycle-perf-validation.md` (旧検証記録)
下流文書: `261115-porter-transport-performance-design.md` (詳細対処計画、第 2 ゲート通過)、`261115-porter-transport-performance-validation.md` (作業中の検証記録)
対象成果物: BL-001 の実装・局所検証・P4/P5 A/B 測定と、BL-002 のプロファイル方法検証。Q-006/Q-007 の回答を記録しました。Q-007 B の低負荷計測 pilot はユーザー指示で中断し、未着手です
再開手順: この案件は打ち切りました。作業 WBS の `← **CURRENT**` から再開しないでください。BL-001 の実装は cplat と porter のブランチ `fix/bl001-tcp-nodelay` にあり、`main` には入っていません。作業は `261117-porter-transport-performance-redo-discussion.md` で続けます。以下は打ち切り前の再開手順の記録です。このファイルを開いたエージェントは、作業の前に `.agents/skills/agree-before-implement/SKILL.md` を読み込んでください。Q-001〜Q-007 の回答を確認し、作業 WBS の `← **CURRENT**` から再開してください。2026-11-16 に詳細計画が承認され、`grok --yolo high` による実行指示を受領しています。BL-001 の性能トレードオフは Q-006 A で受容されました。Q-007 B の低負荷計測方式は Grok が案を提示しましたが、Sol の設計レビューと pilot 実行前にユーザー指示で中断しました。`profile/pilot/` は存在せず、pilot 用ソース・ビルド・測定結果はありません。BL-002 製品改修と Git の書き込みは対象外です。全体テストと Doxygen 全体生成は未完了です。結果と限界は検証記録を参照してください。
-->

# porter の TCP 遅延と送信・通知性能の改善に関する検討

ファイル名: `261115-porter-transport-performance-discussion.md`

## 作業 WBS と進捗管理

凡例: ✅ 完了、🔄 実施中、⏸️ 未着手、⚠️ 要注意、🚫 ブロック中。

- ✅ 1. `porter` と `cplat` の実装、過去の測定、進行中案件の調査
- ✅ 2. Q-001〜Q-005 の回答受付と第 1 ゲート通過 (版 2)
- ✅ 3. `261115-porter-transport-performance-design.md` の作成と第 2 ゲート承認
- ✅ 4. ステージ 0。公式仕様、基準 HEAD、既存差分、両 OS のビルド入口とベンチ ハーネスを検証記録へ記録
- ✅ 5. BL-001。`cplat_socket_set_nodelay` と porter の全 TCP 接続経路への設定を実装
- ✅ 6. BL-001。両 OS の対象 leaf ビルドと新規 3 件の `make test GTEST_FILTER` を確認。Linux 全件 `make test` は中断
- ⚠️ 7. Doxygen。公開 API の一時生成ページで宣言を確認したが、全体生成と公開限定生成は完了せず
- ✅ 8. BL-001。固定 `off` / `on` 版で P4 TCP 1 条件と P5 17 条件を両 OS 各 5 回測定
- ✅ 9. wrap プロファイルの実行可能性を確認。計装影響と CPU 計数の制約により、元の方式は原因帰属に不適格
- ✅ 10. Q-006 A を記録。全 TCP への常時適用と測定済み throughput 低下を受容し、BL-001 を対応完了として元 backlog を更新
- ✅ 11. Q-007 B を記録。別方式の低負荷サンプリングを設計し、小規模に検証する方針を合意
- ⏸️ 12. 一時プロファイル用複製で低負荷サンプリング候補を実装し、通常版との性能差と帰属の安定性を小規模に検証する。ユーザー指示で中断し、Grok の案に対する Sol のレビュー、pilot 用ソース・ビルド・測定は未実施。`profile/pilot/` は未作成。2026-11-17 に案件を打ち切ったため、この項目以降は実施しません
- ⏸️ 13. pilot 結果を評価し、BL-002 の本プロファイルへ進む方法の妥当性を確認する
- ⏸️ 14. 妥当な方法が得られた場合、合意済み条件で両 OS の 8 観点プロファイルと区間計測を実施
- ⏸️ 15. 主因・寄与率に基づき BL-002 の改善施策と数値目標を別判断で確定
- ⏸️ 16. 承認後の BL-002 改修、局所テスト、非計装 A/B 測定と知見反映
- ⏸️ 17. 全体検証、残課題整理、成果提示と第 3 ゲート承認

2026-11-16 に受領した具体的な実行指示の範囲でステージ 0 と BL-001 を進めました。Q-006 A により BL-001 の測定済み throughput 低下を受容済みです。Q-007 B による低負荷計測 pilot は方法案の初回レビューを依頼したところで停止し、pilot 用ファイルの作成・ビルド・測定へは進んでいません。

## 検討の識別

検討 ID: `porter-transport-performance-20261115`  
作成日: `2026-11-15`  
版: `6`  
状態: `打ち切り (2026-11-17)。やり直しの案件は porter-transport-performance-redo-20261117`  
合意版: `2` (第 1 ゲート、Q-001〜Q-005)。追加判断: 版 6 (Q-006/Q-007 解決済み)

この文書は、[261115-porter-reject-recovery-backlog.md](261115-porter-reject-recovery-backlog.md) に記録された BL-001 (TCP の約 40[ms] 停止) と BL-002 (小メッセージ・圧縮送信・N:1 外部返信の処理件数低下と通知遅延増加) について、事前のコード調査、仮説と事実の整理、および段階的な原因特定・対処計画をまとめた検討文書です。  
版 1 (2026-11-15) では元案件 `porter-reject-recovery` の進行中状態を踏まえた調査と Q-001〜Q-005 の提示までを行い、版 2 (2026-11-16) ではユーザーの回答受付および元案件の完了確認を経て第 1 ゲートを通過し、`261115-porter-transport-performance-design.md` を新規作成しました。  
版 2 の調査ターンでは、ソース コードの変更、ビルド、実測、Git の書き込みは行っていません。  
版 3 では承認と実行指示に従ってステージ 0 と BL-001 を進め、版 4 で両 OS の P4/P5 A/B、版 5 で wrap プロファイルの実行可能性を確認しました。版 6 では Q-006 A による BL-001 の性能トレードオフ受容と、Q-007 B の回答記録を行いました。低負荷サンプリングの方法案はレビュー前で、pilot の設計・実装・測定は未実施です。BL-002 の原因・寄与率は未特定です。

## 対象と参照資料

本調査および版 2 の完了確認で直接確認した一次資料と対象コードは次のとおりです。

| 種別 | パス / 対象 | 主な確認内容 |
| --- | --- | --- |
| 元残課題 | `app/porter/docs/prompts/past/261115-porter-reject-recovery-backlog.md` | BL-001、BL-002 の起票内容と参考根拠 |
| 元案件検討 (2026-11-15 調査時→2026-11-16 完了確認) | `app/porter/docs/prompts/past/261115-porter-reject-recovery-discussion.md` | 2026-11-15 時点は WBS 4.1 実施中。2026-11-16 確認で版 4・状態 `完了`・WBS 全完了・第 3 ゲート承認済み (Tetsuo Honda)・CI run 43724723928 全 8 ジョブ成功・P3 Windows 再測定 25 試行再開を確認 |
| 元案件設計 | `app/porter/docs/prompts/past/261115-porter-reject-recovery-design.md` | `thread_recv_window.c` と `window.c/h` の設計範囲 (コミット `68ab091` / `11016c1` 反映済み) |
| 性能検証記録 (旧基準) | `app/porter/docs/prompts/past/261115-porter-handle-lifecycle-perf-validation.md` | P1〜P5 の測定条件、旧固定版 (`porter 6e9002c`/`9e55a0c`)、比較不能条件、傾向値 |
| 性能計測生データ (旧基準) | `app/porter/docs/prompts/past/261115-porter-handle-lifecycle-perf.csv` | Windows/Linux、old/new の各実行中央値と分布 |
| 前駆案件記録 | `app/porter/docs/prompts/past/261107-porter-handle-lifecycle-design.md` / `-validation.md` | ハンドル非再利用設計、通知スレッド・キュー、24 条件の根拠 |
| cplat ソケット | `app/cplat/prod/include/cplat/net/socket.h`、`app/cplat/prod/libsrc/cplat/net/socket_linux.c`、`app/cplat/prod/libsrc/cplat/net/socket_windows.c` | 公開ソケット API と `set_int_option()`、Linux/Windows 実装 |
| porter TCP 接続 | `app/porter/prod/libsrc/porter/api/api_open_paths_tcp.c`、`app/porter/prod/libsrc/porter/thread/thread_connect_prepare.c`、`app/porter/prod/libsrc/porter/thread/potr_connect_thread.c` | listen/connect/accept/再接続と `SO_REUSEADDR` の扱い |
| porter 送信・圧縮 | `app/porter/prod/libsrc/porter/api/potr_service_open.c`、`app/porter/prod/libsrc/porter/api/potr_service_send.c`、`app/porter/prod/libsrc/porter/thread/potr_send_thread.c` | `compress_buf_size` 確保/零クリア、送信キュー、`peers_mutex` 保持 |
| porter 通知・寿命 | `app/porter/prod/libsrc/porter/potr_handle.c`、`app/porter/prod/libsrc/porter/potr_notify.c`、`app/porter/prod/libsrc/porter/infra/potr_send_queue.c` | `s_mutex`/`entry->mutex`、通知ノード再利用、condvar 起床 |
| porter 受信・終端 | `app/porter/prod/libsrc/porter/thread/potr_recv_thread.c`、`app/porter/prod/libsrc/porter/thread/thread_recv_slot.c`、`app/porter/prod/libsrc/porter/thread/thread_recv_fin.c`、`app/porter/prod/libsrc/porter/api/potr_service_close.c` | TCP FIN/FIN_ACK 経路、受信展開、N:1 の `peers_mutex` 保持 |
| 公開契約定義 | `app/porter/prod/include/porter/porter_const.h`、`app/porter/prod/include/porter/porter_type.h`、`app/porter/prod/include/porter/porter_spec.h` | 設定構造体、フラグ定義、スレッド セーフ契約、戻り値規約 |

Table: 対象と参照資料一覧

## ユーザー要求と前提の整理

### 2026-11-16 版 2 時点の現況確認と合意前提

Q-001〜Q-005 に対するユーザーの回答と `porter-reject-recovery` 完了の報告を受け、2026-11-16 時点で次の事実を確認しました。

1. **元案件 `porter-reject-recovery` の完了確認**: `app/porter` の `git status` では新規 `docs/prompts/current/261115-porter-transport-performance-discussion.md` だけが未追跡であり、親案件の未コミット差分は解消されています。`app/porter` の `HEAD` は `11016c1` (`docs: REJECT 受信再開の案件の CI 結果と成果物の承認を記録する [skip ci]`)、実装コミットは `68ab091` です。また、`261115-porter-reject-recovery-discussion.md` は版 4・状態 `完了`・WBS 1〜5 全項目完了で、2026-11-16 に Tetsuo Honda による第 3 ゲート承認、CI run 43724723928 全 8 ジョブ成功 (警告アーティファクトなし)、Windows P3 再測定 5 回×各 5 試行=25 試行全件での受信再開が記録されています。したがって [Q-005](#q-005-進行中の-porter-reject-recovery-案件と測定ベースライン新規課題はどう扱いますか) の案 B 凍結条件は完全に満了しており、矛盾なく次段階へ進めます。
2. **新旧ベースラインの区別**: 今後の新規測定・調査は、元案件完了後の固定コミット (親リポジトリ `721131ef`、`app/porter 11016c1`、`app/cplat 822dc7da` を候補とする基準版) を新基準版として扱います。旧検証記録 (`261115-porter-handle-lifecycle-perf-validation.md`) の `6e9002c` / `fd903dc1` および改修前版 `9e55a0c` / `9f3c304` とは明確に区別し、P3 は新基準版で再検証します。
3. **本ターンの作業範囲**: 第 1 ゲートを通過した確定方針に基づき、`261115-porter-transport-performance-design.md` の作成、この検討文書の版 2 更新、および元残課題 `261115-porter-reject-recovery-backlog.md` の状態更新 (計画承認待ち・実装未着手) だけを実施します。実測・実装・ビルド・Git 書き込みは行いません。

### 2026-11-15 調査時点の前提整理 (版 1 履歴)

以下は 2026-11-15 調査時点 (版 1、`porter-reject-recovery` 完了前) の状況記録であり、経緯の履歴としてそのまま残します。

1. **今回の実行範囲**: 新規検討用 Markdown の作成と元 backlog への「検討中」記録だけを実施します。  
   ここで合意を得る対象は調査方針・設計の前提であり、合意を得ても実装承認へ自動拡張しません。
2. **設計書の未作成**: `-design.md` は本段階では作成せず、第 1 ゲート合意と必要なプロファイル計測を経た後に作成します。
3. **進行中の親案件との切り離し**: 親案件 `porter-reject-recovery` は WBS 4.1 実施中であり、`app/porter` 配下に `thread_recv_window.c`、`window.c/.h`、仕様文書、単体テストなどの未コミット差分が存在します。  
   本検討では親案件の作業完了扱いを行わず、既存の未コミット変更へも手を入れません。
4. **測定基準の固定と進行差分の扱い**: `261115-porter-handle-lifecycle-perf-validation.md` の現行版基準は固定コミット (`porter 6e9002c`、`cplat fd903dc1`) です。  
   先行調査・本測定を進める際は、測定対象となる基準コミットと作業ツリー上の未コミット差分 (`porter-reject-recovery` の反映状態を含む) を対にして明記し、実測前に進行中の差分を再確認する方針を推奨します。
5. **親案件側の将来課題の扱い**: 今後 `porter-reject-recovery` 側で新たな残課題が追加されても、本案件へ自動合流させず、都度別判断で取り込むかどうかを決める方針を推奨します。

## 調査で確認した事実と未検証の仮説

実装・文書から確認できた事実と、実測・公式資料確認を待つ仮説を明確に区別します。

### 確認済みの事実

- **TCP_NODELAY 未設定 (版 2 の調査時点)**: `app/cplat/prod/include/cplat/net/socket.h` にはアドレス再利用 (`cplat_socket_set_reuse_address`) やブロードキャスト (`cplat_socket_set_broadcast`) の API がありましたが、`TCP_NODELAY` を設定する API はありませんでした。  
  `app/cplat/prod/libsrc/cplat/net/socket_linux.c` および `socket_windows.c` にも `TCP_NODELAY` や `<netinet/tcp.h>` の参照はなく、`porter` 側の `api_open_paths_tcp.c`、`thread_connect_prepare.c`、`potr_connect_thread.c` でも設定していませんでした。版 3 で `cplat_socket_set_nodelay` を追加し、接続準備と受け入れから有効化しています。経過は [検証記録](261115-porter-transport-performance-validation.md) に残します。
- **Linux P4/P5 の ~40[ms] 現象**: `261115-porter-handle-lifecycle-perf-validation.md` では、Linux (WSL2) の P4 TCP close が現行版で中央値 42.9[ms] (p99 は 45.3[ms])、改修前版でも p99 が 44.8[ms] でした。  
  また Linux の P5 では、ブロッキング送信および 4 スレッド送信の多くで通知遅延 p99 が約 42〜44[ms] となっています。
- **TCP 経路での小パケット連続送出**: `potr_send_thread.c` の `tcp_write_path()` は送信前に `potr_internal_tcp_announce_ping_state_locked()` を呼び出し、未通知の PING 状態変化があれば `cplat_socket_send_all()` で制御パケットを先に送出した直後、`potr_internal_packet_seal_for_context()` と `cplat_socket_send_all()` で DATA を送出します。  
  close 側 (`potr_service_close.c`) は小さな FIN を送って FIN_ACK を待ち、受信側 (`thread_recv_fin.c` の `send_tcp_fin_ack()`) は受信後に小さな FIN_ACK を返します。
- **圧縮送信の毎回全量確保と零クリア**: `potr_service_open.c` は `ctx->compress_buf_size = CPLAT_COMPRESS_HEADER_SIZE + (size_t)ctx->global.max_message_size + 64U` を設定します (`POTR_MAX_MESSAGE_SIZE` の既定値は 65535 バイトです)。  
  `potr_service_send.c` の `send_impl()` は、`POTR_SEND_COMPRESS` 指定のたびに `cplat_malloc(ctx->compress_buf_size)` を呼び、送信キュー投入後に `cplat_secure_zero(compress_buf, ctx->compress_buf_size)` と `cplat_free(compress_buf)` を実行しています。  
  メッセージ長が 64 バイトであっても、確保と零クリアは `ctx->compress_buf_size` 全体に対して行われます。
- **ハンドル検証のロック回数**: `potr_handle.c` はグローバルなハッシュ テーブル ロック `s_mutex` とエントリごとの `entry->mutex` を持ちます。  
  1 回の `potr_service_send()` ごとに、`potr_internal_handle_acquire()` が `s_mutex` の取得・解放と `entry->mutex` の取得・解放を行い、関数末尾の `potr_internal_handle_release()` が再度 `entry->mutex` を取得・解放します。
- **送信キューと通知キューの同期・メモリ処理**: `potr_send_queue.c` の `push_locked()` はキューが空でなくても毎回 `cplat_condvar_signal(q->not_empty)` を呼び、`complete_locked()` はブロッキング待機者の有無によらず毎回 `cplat_condvar_broadcast(q->progress)` と `cplat_condvar_signal(q->not_full)` を呼んでいます。  
  一方、`potr_notify.c` の `potr_internal_notify_enqueue()` はキューが空のときだけ `cplat_condvar_signal(q->ready)` を呼びますが、`q->mutex` 保持下で単方向リスト `q->free_nodes` を先頭から走査して適合ノードを探し、コンシューマー側スレッド (`notify_thread`) も `cplat_secure_zero(node + 1, node->bytes)` を実行して返却リストへ積みます。
- **N:1 送信・受信の `peers_mutex` 保持区間**: `potr_send_thread.c` の `send_packed_peer_mode()` はバッチ送信ごとに `ctx->peers_mutex` を存在確認のため一度取得・解放した後、再取得して `peer->send_window_mutex` 取得、`potr_internal_packet_seal_for_context()`、`cplat_socket_sendto()` 完了まで保持します。  
  受信側 (`potr_recv_thread.c` の `n1_handle_packet()`) も `ctx->peers_mutex` を保持したまま `thread_recv_dispatch_packet()` を呼び出し、その中で通知エンキューまで行います。

### 未検証の仮説と今後の確認要項

| ID | 仮説の内容 | 未確認の前提 / 確認が必要な資料・計測 |
| --- | --- | --- |
| H-001 | Linux P4 (TCP close 約 43[ms]) と P5 (p99 約 42〜44[ms]) は、Nagle アルゴリズムと OS の遅延 ACK による約 40[ms] 停止である。 | Linux `tcp(7)`・カーネルの最小遅延 ACK 時間 (`TCP_DELACK_MIN` 等)、ループバックでのパケット時刻列 (`tcpdump` / `ss`)、Winsock の `TCP_NODELAY` と遅延 ACK 既定値に関する Microsoft Learn 公式資料。 |
| H-002 | リスン ソケットへの設定だけで `accept()` 済みソケットへ `TCP_NODELAY` が継承されるか、接続済みソケットごとに設定が必要かは OS 差があり得る。 | Linux `accept(2)` / `tcp(7)` および Windows `accept` / `SO_UPDATE_ACCEPT_CONTEXT` / Winsock オプション継承の公式仕様。 |
| H-003 | BL-002 の圧縮 4 スレッド劣化 (Windows P5 で -72.3%、Linux P1 で -60.8%) は、毎回 65KiB 超の `cplat_malloc` / `cplat_secure_zero` / `cplat_free` によるヒープ競合とキャッシュ汚染が主因である。 | ETW/WPR (Windows) と `perf` (Linux) による送信・通知スレッド別 CPU 時間内訳、およびヒープ確保サイズ/再利用有無を変えた段階的 A/B 比較。 |
| H-004 | 小メッセージ通常送信 (-23.6%〜-34.5%) と通知遅延増加 (+14〜+23[µs]) は、`potr_handle` ロック、`potr_send_queue` の無条件 `condvar` 通知、`notify_thread` 経由のコンテキスト スイッチの複合である。 | ロック競合待機時間、futex / CS 競合回数、`notify_p50` の区間分解 (API 出口→送信完了→通知キュー投入→コールバック入口)。 |
| H-005 | Linux の N:1 外部スレッド返信劣化 (RTT 約 1.5 倍、件数 -31.3%) は、ベンチマーク側の中継キュー経費に加え、`potr_service_send()` のハンドル ロック、`send_queue` 通知、および `n1_handle_packet()` と `send_packed_peer_mode()` 間の `ctx->peers_mutex` 競合が寄与している。 | 外部スレッド返信経路での関数別プロファイルと、コールバック内返信 (`cb`) との区間別差分測定。 |

Table: 仮説一覧と今後の確認要項

> [!NOTE]
> Linux の遅延 ACK タイマー既定値や Windows Winsock のループバックにおける遅延 ACK の細部については、公式 OS 資料の一次確認を完了するまで断定しません。  
> 段階 2.1 の先頭で Linux man page (`tcp(7)`, `socket(7)`) および Microsoft Learn 公式文書を確認し、記録を残します。

## BL-001 の深掘り分析と対処方針案

### 約 40[ms] 停止の発生経路と観察根拠

現行コードでは、アプリケーション層の送信バッチ待ち時間 `pack_wait_ms` は既定で `POTR_DEFAULT_PACK_WAIT_MS = 0U` (即時送信) に設定されています。  
しかしソケット層で `TCP_NODELAY` を有効化していないため、TCP セグメント送出判定は OS スタックの Nagle 制御と相手側の遅延 ACK 制御に委ねられます。  
`porter` では次の場面で小さな独立メッセージや制御パケットを送出し、停止条件と合致しやすい状態にあります。

1. **送信開始直後の PING + DATA 連続送出**: `potr_connected_threads.c` の `potr_internal_start_connected_threads()` と `potr_send_thread.c` の `tcp_write_path()` により、PING 状態通知や小さな DATA が連続して `cplat_socket_send_all()` へ渡されます。
2. **単発送信後の close() (P4)**: クライアントが 64 バイトの DATA を送り直後に `potr_service_close()` を呼ぶと、小さな FIN 制御パケットを送信し、サーバー側の `FIN_ACK` 返信を待ちます。  
   前パケットの ACK が未到達の状態で後続の小パケットや `FIN` / `FIN_ACK` を送る場合、双方の遅延 ACK タイマーが満了するまで送出または応答が遅れる可能性があります。
3. **4 スレッド・ブロッキング送信 (P5)**: `POTR_SEND_BLOCKING` では 1 件ごとに送信スレッドの書き込み完了を待ち、次の呼び出しへ移る構造上、未確認データが残る小送信のたびに約 40[ms] 級の尾を引く遅延 (Linux p99 約 42〜44[ms]) が表れやすいと考えられます。

### TCP_NODELAY 採否の比較

| 案 | 概要 | 利点 | 欠点 / リスク | 既存機能との整合 |
| --- | --- | --- | --- | --- |
| 案 A (推奨候補) | porter がデータ通信に使う確立済み TCP ソケットで `TCP_NODELAY` を常時有効化する | 小メッセージ、ブロッキング送信、制御パケット (`PING` / `FIN` / `FIN_ACK`)、P4 close の遅延を均一に解消できる。設定漏れが起きない。 | 小パケットが連続するときにワイヤー上のパケット数が増え得る。 | porter にはすでにアプリケーション層のバッチ機構 (`pack_wait_ms`) があり、まとめ送信の制御はプロトコル層で管理可能。 |
| 案 B | 設定キー / 構造体フィールドを追加し、既定オンまたは既定オフで切り替え可能にする | 利用者が網環境 (低帯域 WAN 等) に応じて Nagle 併合を選択できる。 | 公開構造体 `potr_global_config` や `potr_service_def` の拡張・設定パーサー更新・API/ABI 契約変更が伴う。 | 現状 `pack_wait_ms` と役割が重複する部分があり、説明・テスト対象が増える。 |
| 案 C | `TCP_NODELAY` を設定しない現状を維持する | コード変更がなく、パケット結合率は OS 既定のまま保たれる。 | Linux P4 close 約 43[ms] と P5 p99 約 42〜44[ms] の停止が解消されない。BL-002 の TCP 比較でも遅延のマスクが残り続ける。 | 低遅延用途や頻繁な接続断続に不向き。 |

Table: TCP_NODELAY 採否の比較

### cplat と porter の責務分担

`app/cplat/AGENTS.md` と `app/cplat/prod/include/cplat/net/socket.h` の設計方針に照らすと、`cplat` は OS 固有 API (`setsockopt` やヘッダー `<netinet/tcp.h>` / Winsock 型変換) を隠蔽する共通抽象層であり、`porter` は用途に応じてそのオプションを使うかどうかを決めるポリシー層です。  
既存の `cplat_socket_set_reuse_address()` や `cplat_socket_set_broadcast()` は、いずれも `socket_linux.c` / `socket_windows.c` 内の共通ヘルパー `set_int_option()` を呼ぶ薄いラッパーとして実装されています。

1. **責務案 1 (`cplat` に `cplat_socket_set_nodelay` 相当を追加し、`porter` が接続確立時に呼ぶ — 推奨)**:  
   OS 依存のヘッダー・型差異・エラー変換 (`cplat_error`) を `cplat` に閉じ込め、既存のオプション API 群と一貫した構造を保てます。  
   ただし `cplat` 公開 API を増やすため、`app/cplat/AGENTS.md` の手順どおり `docs/api-cheatsheet.md`、公開シンボル表、エクスポート テスト、`mock_cplat` (`create-mock-cplat-mock` スキル) の同期が必須となります。
2. **責務案 2 (`cplat_socket_open(CPLAT_SOCKET_TCP, ...)` 等で `cplat` の既定動作にする)**:  
   `cplat` の公開 API は増えませんが、低遅延以外の用途を含む汎用ソケット ライブラリとしての暗黙挙動が変わり、`accept()` で生成されたソケットへの自動適用も考慮が必要です。
3. **責務案 3 (`porter` 側に `#ifdef _WIN32` で `setsockopt` を直書きする)**:  
   `cplat` を変更せずに済みますが、`porter` が直接 OS ソケット API へ依存することになり、プラットフォーム抽象化の境界に反します。

### 接続形態別の適用箇所と失敗時方針

`porter` の TCP 接続ソケットは、役割と手順に応じて複数箇所で生成・受け入れされます。  
どこに設定を置くべきかを整理します。

| 場面 | コード位置 | 対象ソケットと留意点 |
| --- | --- | --- |
| RECEIVER listen 準備 | `api_open_paths_tcp.c` `open_socket_tcp_receiver()` | listen 用 `ctx->tcp_listen_sock[path_idx]` の生成箇所。`SO_REUSEADDR` はここで設定しています。listen 側での `TCP_NODELAY` が `accept()` 後ソケットへ継承されるかは OS ごとに公式資料で確認が必要であり、確実性を優先するなら `accept()` 後の接続ソケットへ明示設定する方が安全です。 |
| RECEIVER accept 確立 (`TCP` / `TCP_BIDIR`) | `potr_connect_thread.c` `receiver_accept_loop()` | `cplat_socket_accept()` で得た接続ソケット `conn` (証明書ハンドシェイク前後〜`ctx->tcp_conn_fd[path_idx] = conn`)。マルチパスおよび再接続 (`receiver_accept_loop` のループ再試行) のたびに新しい `conn` が生成されるため、accept の都度設定する必要があります。 |
| SENDER connect / 再接続 (`TCP` / `TCP_BIDIR`) | `thread_connect_prepare.c` `thread_connect_with_timeout()` (`potr_connect_thread.c` `sender_connect_loop()` から呼出) | `cplat_socket_open(CPLAT_SOCKET_TCP, &sock, &detail)` 後に `connect` を実施します。再接続ループごとに呼び直されるため、ここで設定すれば初回接続・自動再接続・マルチパスの全パスを網羅できます。証明書 TLS ハンドシェイク前に設定すれば、ハンドシェイク中の小さなレコード交換にも有効です。 |
| `TCP_BIDIR` 双方向通信 | 上記 SENDER 側と RECEIVER 側の双方 | `TCP_BIDIR` では SENDER 側と RECEIVER 側の双方が送信スレッド・ヘルス スレッド・FIN_ACK 送出を持ちます。したがって `connect` 側と `accept` 側の双方に設定しないと片方向で遅延が残ります。 |

Table: TCP 接続経路ごとの設定対象箇所

設定に失敗した場合の方針は次の 2 案を比較します。

- **ベスト エフォート方針 (推奨)**: 既存の `SO_REUSEADDR` (`api_open_paths_tcp.c` 74 行目、`thread_connect_prepare.c` 148 行目) と同様に、設定失敗時は警告トレースを残しつつ接続処理を継続します。  
  `TCP_NODELAY` は正確性やセキュリティ契約を壊す必須条件ではなく性能最適化であるため、仮想 NIC や特殊ソケット環境で万一失敗しても通信不能にしない方が安全です。
- **致命エラー方針**: `setsockopt` 失敗時に接続確立を失敗としてソケットを閉じ、再接続またはオープン失敗にします。  
  遅延要件を厳格に保証したい場合の案ですが、接続不能になるリスクを負います。

## BL-002 の深掘り分析と調査プロファイル対象

`261115-porter-reject-recovery-backlog.md` の BL-002 は、現行版において非ブロッキング小メッセージ (-23.6%〜-34.5%)、圧縮 4 スレッド (-26.5%〜-72.3%、通知遅延 p99 最大約 8 倍)、Linux N:1 外部返信 (RTT 約 1.5 倍、件数 -31.3%) が悪化した現象です。  
コード確認により特定したプロファイル対象と候補改善軸は次の 6 点です。

| No. | 調査対象レイヤー | コードで確認した重い処理 / 競合箇所 | 確認したい計測指標と改善仮説の方向性 |
| --- | --- | --- | --- |
| 1 | 確保と零クリア | `potr_service_send.c` が圧縮送信ごとに `ctx->compress_buf_size` (既定 65KiB 超) を `cplat_malloc` し、最後に全量を `cplat_secure_zero` + `cplat_free` しています。`potr_notify.c` も配送後に `cplat_secure_zero(node + 1, node->bytes)` を呼びます。 | 送信スレッド別のアロケーター時間とページ フォルト回数。入力長相応の必要量確保・スレッド ローカル/プール再利用・秘密データ該当範囲の見直しによる削減効果を検証。 |
| 2 | コピーと圧縮サイズ計算 | `potr_service_send()` → `potr_send_queue` (`memcpy`) → `send_thread` (`send_wire_buf` → `crypto_buf`) → 受信 (`recv_compress_buf`) → `notify_enqueue` (`memcpy`) → ユーザー コールバックと複数回のコピーがあります。 | 64 バイト送信で特に割高になる `compress_buf_size` 上限固定の影響と、コピー回数の寄与率を計測。 |
| 3 | 通知キューと再利用管理 | `potr_notify.c` の `potr_internal_notify_enqueue()` は単一ロック `q->mutex` の下で連結リスト `q->free_nodes` を線形探索して容量適合ノードを探し、コンシューマー側も同ロックでノードを回収します。 | `q->mutex` の待機時間、1 回あたり探索ステップ数、高頻度小メッセージ時のリスト断片化有無を計測。 |
| 4 | 送信キューの無条件起床 | `potr_send_queue.c` が `push_locked()` で毎件 `cplat_condvar_signal(q->not_empty)`、`complete_locked()` で毎回 `cplat_condvar_broadcast(q->progress)` と `cplat_condvar_signal(q->not_full)` を呼んでいます。 | カーネル システム コール回数 (`WakeConditionVariable` / `futex`)。待機者フラグやキュー状態 (空→非空、満杯→空き、ブロッキング待機あり) に応じた条件付き起床による効果を切り分け。 |
| 5 | ハンドル寿命管理ロック | `potr_handle.c` が 1 回の送信ごとにグローバル `s_mutex` と `entry->mutex` の取得・解放を計 3 回 (呼び出しあたり実質 6 ロック操作) 実行します。 | 4 スレッド 85 万 msg/s 級での `s_mutex` 競合率。参照カウントと世代タグを組み合わせた読み取り経路のロック軽量化 (安全性不変が前提) の要否を判定。 |
| 6 | N:1 外部スレッド返信 | `send_packed_peer_mode()` がバッチごとに `ctx->peers_mutex` を 2 度取得し、暗号封緘と `cplat_socket_sendto()` の間も保持します。受信側 `n1_handle_packet()` も通知エンキューまで `ctx->peers_mutex` を保持します。 | Linux P2 (`ext` vs `cb`) で `peers_mutex` の競合、送信スレッドと受信スレッドの相互ブロッキング時間、`potr_service_send()` 再入コストの内訳を計測。 |

Table: BL-002 のコード確認済みプロファイル対象一覧

### BL-001 の影響を揃えた BL-002 比較の方法

Linux の P5 (TCP) では BL-001 による約 40[ms] の停止が支配的であり、BL-002 に起因する数百 [µs] 〜数 [ms] のキュー・圧縮・ロック差が隠れます。  
したがって BL-002 を TCP 条件で評価する際は、比較対象 (A/B) の双方で `TCP_NODELAY` の有効/無効条件を必ず一致させます。  
実務上は段階 1 で BL-001 の方針と効果を単独確認して TCP の固定遅延を取り除いたベースライン (`BL-001 対策済み` または明示的に揃えた組) を作り、その上で BL-002 の各施策を UDP (P1/P2/P3) と TCP (P5) で比較する順序を採ります。

## 両 OS 段階的測定・評価計画

### 段階的測定フローとプロファイル方針

1. **段階 0 (前提確定とベースライン記録)**:  
   公式 OS 資料で `TCP_NODELAY` と遅延 ACK の仕様および `accept()` 継承挙動を確認します。  
   比較基準となる固定コミットと、その時点で進行中の `porter-reject-recovery` 差分有無を記録します。
2. **段階 1 (BL-001 単独 A/B 切り分け)**:  
   最小変更 (試作または設計案に沿った設定) で `TCP_NODELAY` 無効 vs 有効の A/B 測定を Windows と Linux の P4 (TCP) および P5 (`oneway`/`bidir`、64B/5000B、`normal`/`blocking`/`compress`) で行います。  
   必要に応じてループバックのパケット時刻列を取得し、約 40[ms] 停止の消失を確認します。
3. **段階 2 (BL-002 ボトルネック計測と計測者効果の制御)**:  
   プロファイラーを挿した測定と、本番同等の非計装ベンチマークを明確に分離します。  
   Windows では Windows Performance Recorder / ETW (サンプリング + CSWITCH/Wait 分析)、Linux では `perf record` / `perf lock` 等を用い、トレース (`POTR_TRACE`) のログ出力量が性能を歪める計測者効果を避けるため、本測定時のトレース レベルを既定本番相当へ揃えます。  
   またプロファイル取得実行から得た CPU 比率と、計装なし 5 回実行の中央値 (スループット、p50/p95/p99) を混在させず別表に記録します。
4. **段階 3 (BL-002 改善候補ごとの独立 A/B 評価)**:  
   確保/零クリア改善、無条件 `condvar` 起床の抑制、ハンドル/ピア ロック軽量化などの各施策は可能な限り 1 要素ずつ (または累積加算形式で) A/B 比較し、どの変更がどの条件に効いたかを分離可能にします。

### 比較不能条件と過負荷条件の扱い

`261115-porter-handle-lifecycle-perf-validation.md` で確認済みの比較不能・不安定条件は、本計画でも次のように切り分けて扱います。

| 条件 | 事象 (検証記録より) | 本計画での扱い |
| --- | --- | --- |
| 改修前版 Windows P5 5000B・圧縮なし 4 条件 | 改修前版の既存不具合 (`send buffer full, packet skipped`) により完了しない。 | 改修前版との比較対象から除外し、現行版 (または BL-001 適用後) を基準にした改善前後比較だけで評価します。 |
| 改修前版 Linux P5 `TCP_BIDIR` | 改修前版の既存不具合で close 時に停止する。 | 同上。BL-001 適用前後および現行版比で評価します。 |
| P1 5000B・通常・4 スレッド (UDP 過負荷) | Linux UDP 受信バッファー (212,992 バイト) を大きく上回り、既定負荷は 900 秒超過で打ち切り、縮小負荷でも処理件数が 2 桁以上ばらつく。 | 定常スループットの合否判定対象外 (過負荷・欠落回復観測専用の参考条件) とします。通常評価はバッファーあふれを起こさない範囲 (64B、圧縮 5000B、ブロッキング等) で比較します。 |
| P3 低速コールバック (Windows) | 親案件 `porter-reject-recovery` で修正対象の受信ウィンドウ停止が 5 回中 4 回発生していた。 | 親案件での修正取り込み前後は比較不能となるため、テスト・計測前に親案件差分の取り込み状態を明記して実施します。 |
| 同一物理マシン / WSL2 NAT 制約 | Windows と Linux (WSL2) が CPU とキャッシュを共有する。 | これまでと同様に OS ごとに交互実行し、単一タイムスタンプの絶対値比較ではなく同一 OS 内での A/B 差分で評価します。 |

Table: 比較不能条件と過負荷条件の取り扱い方針

### 公開契約安全性と受入条件案

いかなる高速化策であっても、前駆案件 (`porter-handle-lifecycle`) で確保した安全契約を後退させないことを必須条件とします。

- **ハンドル寿命管理の安全性維持**: クローズ済み・再利用なしの `potr_context *` 無効ハンドル判定 (`POTR_ERR_INVALID_HANDLE`)、および `potr_service_send()` と `potr_service_close()` 同時呼び出し時の競合安全性を維持します。
- **コールバック安全性と順序保証**: `potr_recv_fn` コールバック内からの `potr_service_send()` (ブロッキング含む) 呼び出し可否、1 メッセージのフラグメント連続送信保証、スレッド内の送信順序保証を変えません。
- **戻り値とエラー契約**: `POTR_ERR_DISCONNECTED`、`POTR_ERR_CANCELED`、`POTR_ERR_OUT_OF_MEMORY` 等の契約を保持します。
- **秘密データ消去契約の遵守**: 平文データまたは鍵・一時圧縮バッファーの零クリア (`cplat_secure_zero`) の範囲を変更する場合は、機密保持方針とコーディング規範に照らして個別レビューを行います。
- **資源回収**: P4 (open/close 反復) においてハンドル数とスレッド数の傾き 0 を維持します。

#### 受入条件案 (数値目標は未確定)

本検討時点では、数値目標は **未確定** とし、段階 1・段階 2 の調査結果を踏まえて設計フェーズで最終確定します。  
現時点の評価軸案は次のとおりです。

1. **BL-001 (TCP 遅延)**: Linux P4 (TCP close) および P5 (ブロッキング、4 スレッド) で見られた約 40[ms] 級の停滞が解消されること。スループットとパケット効率の妥当性を両 OS で確認すること。
2. **BL-002 (非ブロッキング小メッセージ・圧縮・N:1)**: 段階 2 で特定した主因に対する改善策により、現行版比でスループット向上・通知遅延 (p50/p99) 低減・CPU 時間削減が確認できること (改修前版への到達度合いは設計時に目標設定)。
3. **安全性・回帰検証**: 両 OS (Windows/MSVC、Linux/GCC) でのビルドおよび単体・結合テストの全件成功、内容のある `.warn` ファイル 0 件、および P4 の資源回収傾き 0 を満たすこと。

## 現行の合意内容

版 2 (第 1 ゲート通過、2026-11-16) において Q-001〜Q-005 の回答および元案件 `porter-reject-recovery` の完了確認により以下を合意しました。版 6 (2026-11-17) では Q-006/Q-007 の回答により追加方針を合意しました。

- **今回の範囲と別判断の位置づけ**: 本案件の成果物は詳細対処計画 (`261115-porter-transport-performance-design.md`) の確定までとし、本承認によって実測・実装・ビルドは自動実行されません。また、BL-002 の実装設計確定および性能数値目標は、プロファイル計測後に別判断として決定します ([Q-004](#q-004-bl-002-の原因特定と改善着手の進め方はどうしますか))。
- **BL-001 の適用方針**: `porter` がデータ・制御通信に使用する接続確立済み TCP ソケット (`connect` / `accept` / 再接続、`POTR_TYPE_TCP` と `POTR_TYPE_TCP_BIDIR` の双方、全 `path_idx`) で `TCP_NODELAY` を常時有効化します。適用位置は初回通信以前とします ([Q-001](#q-001-bl-001-の-tcp_nodelay-設定方針はどうしますか))。
- **BL-001 の責務分担**: `app/cplat` に汎用オプション API (`cplat_socket_set_nodelay` 相当) を追加して OS 差異を隠蔽し、同期対象 (公開ヘッダー、両 OS 実装、`api-cheatsheet.md`、機能仕様整合、エクスポート テスト、`mock_cplat`、単体テスト) を更新します。`cplat_socket_open()` / `cplat_socket_accept()` の既定動作や他利用者の既定挙動は変更しません ([Q-002](#q-002-tcp_nodelay-設定の-cplat-と-porter-の責務分担はどうしますか))。
- **BL-001 の失敗時方針**: `TCP_NODELAY` 設定が失敗した場合は、ソケットを閉じず警告ログ (`POTR_TRACE_SOCKET_FAILURE` / `CPLAT_TRACE_LEVEL_WARNING`) を記録した上で接続処理を継続します ([Q-003](#q-003-tcp_nodelay-設定に失敗した場合の-porter-の動作方針はどうしますか))。
- **BL-002 の進め方と安全制約**: 公開契約・ハンドル寿命安全性・資源回収不変条件を維持した前提で、まず BL-001 単独確認 (`off` vs `on`) と、双方 `on` で条件を揃えた両 OS プロファイル取得 (CPU・確保・全消去・圧縮展開・通知キュー・ロック・起床・N:1 外部返信) および区間計測を実施し、主因と寄与率を定量化してから設計と改善順序を確定します ([Q-004](#q-004-bl-002-の原因特定と改善着手の進め方はどうしますか))。
- **元案件との境界と凍結条件の満了**: Q-005 で選択された凍結条件 (`porter-reject-recovery` の第 3 ゲートまでの完全完了とコミット) について、`app/porter` `HEAD 11016c1`・`261115-porter-reject-recovery-discussion.md` 版 4 (状態 `完了`・WBS 全完了・第 3 ゲート承認済み・CI 成功)・未コミット差分解消済みを確認したため、凍結条件は満了しました。今後の測定ベースラインはこの完了状態以降の固定版を新基準版として区別します ([Q-005](#q-005-進行中の-porter-reject-recovery-案件と測定ベースライン新規課題はどう扱いますか))。
- **BL-001 の性能トレードオフ受容**: Q-006 A により、全 TCP ソケットへの `TCP_NODELAY` 常時適用を維持し、Linux P5 の 5000[B] blocking・4 スレッドで測定した throughput 77.9%低下を既知のトレードオフとして記録します。BL-001 は対応完了として扱います ([Q-006](#q-006-bl-001-の-tcp_nodelay-常時適用と測定済み性能低下をどう扱いますか))。
- **BL-002 の低負荷計測方式**: Q-007 B により、毎回計時する wrap 方式は採用せず、製品コードと分離した一時ビルドで低負荷サンプリングを設計・pilot 検証してから本調査への適用を判断します。BL-002 の原因帰属・実装・数値目標は未確定です ([Q-007](#q-007-bl-002-の低負荷サンプリング方式をどう検証しますか))。

## 観点別の確認

| 観点 | 状態 | 確認状況 / 内容 | 関連質問 |
| --- | --- | --- | --- |
| 目的とスコープ | 確認済み | BL-001 と BL-002 の段階的調査・対処計画 (`design.md`) の作成までを今回範囲とし、BL-002 実装設計と数値目標は計測後の別判断とする。 | [Q-004](#q-004-bl-002-の原因特定と改善着手の進め方はどうしますか)、[Q-005](#q-005-進行中の-porter-reject-recovery-案件と測定ベースライン新規課題はどう扱いますか) |
| BL-001 設定方針 | 確認済み | porter の全接続確立済み TCP ソケットで初回通信以前に `TCP_NODELAY` を常時有効化する方針で合意 (案 A)。 | [Q-001](#q-001-bl-001-の-tcp_nodelay-設定方針はどうしますか) |
| BL-001 責務分担 | 確認済み | `cplat` に汎用 API を追加して同期対象を整備し、`cplat` 他利用者の既定は変えず `porter` が接続・受け入れ箇所で呼ぶ方針で合意 (案 A)。 | [Q-002](#q-002-tcp_nodelay-設定の-cplat-と-porter-の責務分担はどうしますか) |
| BL-001 異常系方針 | 確認済み | `TCP_NODELAY` 設定失敗時は警告ログを記録して接続処理を継続するベスト エフォート方針で合意 (案 A)。 | [Q-003](#q-003-tcp_nodelay-設定に失敗した場合の-porter-の動作方針はどうしますか) |
| BL-002 調査と実行順 | 確認済み | 安全契約維持を前提に、BL-001 単独 A/B と両 OS 8 観点プロファイル取得を先行し、寄与率特定後に別判断で設計確定する方針で合意 (案 A)。 | [Q-004](#q-004-bl-002-の原因特定と改善着手の進め方はどうしますか) |
| 親案件との境界・凍結条件 | 確認済み | 元案件 `porter-reject-recovery` の完了 (`11016c1`、第 3 ゲート承認・CI 成功) を確認し、Q-005 案 B の凍結条件満了を検証済み。新旧基準版を区別して扱う。 | [Q-005](#q-005-進行中の-porter-reject-recovery-案件と測定ベースライン新規課題はどう扱いますか) |
| 契約安全性・受入・OS 資料 | 確認済み | メモリ消去・通知順・close・参照管理の安全契約維持、非計装分離・同時測定禁止・比較不能条件除外・計画受入条件と将来数値目標の分離を計画へ反映済み。 | [Q-001](#q-001-bl-001-の-tcp_nodelay-設定方針はどうしますか) 〜 [Q-005](#q-005-進行中の-porter-reject-recovery-案件と測定ベースライン新規課題はどう扱いますか) |
| BL-001 性能トレードオフ | 合意済み | Q-006 A により常時適用を維持し、測定した Linux 大容量 blocking 送信の throughput 低下を受容します。 | [Q-006](#q-006-bl-001-の-tcp_nodelay-常時適用と測定済み性能低下をどう扱いますか) |
| BL-002 プロファイル方式 | 合意済み | Q-007 B により、一時ビルドで低負荷サンプリング方式を設計し、計測影響と関数帰属を小規模に検証します。 | [Q-007](#q-007-bl-002-の低負荷サンプリング方式をどう検証しますか) |

Table: 観点別の確認一覧

## 質問一覧

| ID | 質問概要 | 状態 |
| --- | --- | --- |
| [Q-001](#q-001-bl-001-の-tcp_nodelay-設定方針はどうしますか) | BL-001 の `TCP_NODELAY` 設定方針はどうしますか。 | 解決済み (A) |
| [Q-002](#q-002-tcp_nodelay-設定の-cplat-と-porter-の責務分担はどうしますか) | `TCP_NODELAY` 設定の `cplat` と `porter` の責務分担はどうしますか。 | 解決済み (A) |
| [Q-003](#q-003-tcp_nodelay-設定に失敗した場合の-porter-の動作方針はどうしますか) | `TCP_NODELAY` 設定に失敗した場合の `porter` の動作方針はどうしますか。 | 解決済み (A) |
| [Q-004](#q-004-bl-002-の原因特定と改善着手の進め方はどうしますか) | BL-002 の原因特定と改善着手の進め方はどうしますか。 | 解決済み (A) |
| [Q-005](#q-005-進行中の-porter-reject-recovery-案件と測定ベースライン新規課題はどう扱いますか) | 進行中の `porter-reject-recovery` 案件と測定ベースライン・新規課題はどう扱いますか。 | 解決済み (B: 完了確認により凍結条件満了) |
| [Q-006](#q-006-bl-001-の-tcp_nodelay-常時適用と測定済み性能低下をどう扱いますか) | 測定済み throughput 低下を踏まえた常時適用方針をどうしますか。 | 解決済み (A) |
| [Q-007](#q-007-bl-002-の低負荷サンプリング方式をどう検証しますか) | BL-002 の次のプロファイル方式をどう検証しますか。 | 解決済み (B) |

Table: 質問一覧

## 明確化のための確認項目

### [Q-001] BL-001 の TCP_NODELAY 設定方針はどうしますか

- **Status**: 解決済み
- **Question**:  
  BL-001 (TCP の約 40[ms] 停止) に対する `TCP_NODELAY` の適用方針として、どの案を採りますか。

- **Context**:  
  `porter` は `pack_wait_ms` (既定値 `0`) によってアプリケーション層でまとめ送信の制御を行っていますが、ソケット層で `TCP_NODELAY` を有効にしていません。  
  Linux (WSL2) の P4 では TCP close に毎回約 43[ms]、P5 のブロッキングおよび 4 スレッドで通知遅延 p99 が約 42〜44[ms] 生じており、PING+DATA 連続送出や FIN/FIN_ACK 交換の遅延原因候補となっています。

- **Answer**:

  選択方式: 単一選択
    - [x] A (推奨): `porter` がデータ・制御通信に使う接続確立済み TCP ソケット (`connect` / `accept` / 再接続、`POTR_TYPE_TCP` と `POTR_TYPE_TCP_BIDIR` の双方) で `TCP_NODELAY` を常時有効化する
    - [ ] B: `potr_global_config` や `potr_service_def` 等に設定項目を追加し、利用者が有効・無効を選べるようにする (既定値の採否は別途指定)
    - [ ] C: `TCP_NODELAY` は設定せず、現在の OS 既定のまま維持して本案件では P4/P5 の約 40[ms] 停止を許容する
    - [ ] その他: 以下の自由記述欄に記入してください
    - [ ] X: 意図不明 (詳細な質問内容に再生成すること)

  その他の回答 / 補足:

  回答者: Tetsuo Honda / 回答日時: 2026-11-16 07:25:23 JST (出典ファイルへの直接記入)、2026-11-16 07:26:16 JST (今回チャットでの受領)

- **Assessment**:  
  A の単一選択であり、補足欄との不一致や曖昧さはありません。  
  `porter` がデータ・制御通信に使用する接続確立済み TCP ソケット (`connect` / `accept` / 再接続、`POTR_TYPE_TCP` と `POTR_TYPE_TCP_BIDIR` の双方、全 `path_idx`) で、初回通信以前に `TCP_NODELAY` を常時有効化する方針で確定します。公開設定構造体の拡張は行わず、既存の `pack_wait_ms` 仕様を維持します。

- **Recommendation**:  
  **案 A を推奨します。**

  `porter` にはすでにプロトコル層のまとめ送信パラメーター `pack_wait_ms` があるため、Nagle 制御による約 40[ms] の不規則な停滞を OS に任せる利点が薄く、常時有効化の方が公開設定の増加や設定漏れを防げるためです。

- **Related**: [Q-002](#q-002-tcp_nodelay-設定の-cplat-と-porter-の責務分担はどうしますか)、[Q-003](#q-003-tcp_nodelay-設定に失敗した場合の-porter-の動作方針はどうしますか)

### [Q-002] TCP_NODELAY 設定の cplat と porter の責務分担はどうしますか

- **Status**: 解決済み
- **Question**:  
  `TCP_NODELAY` を設定する場合、`cplat` と `porter` の間でどのように責務を分担しますか。

- **Context**:  
  現在 `cplat` の `socket.h` には `cplat_socket_set_reuse_address()` や `cplat_socket_set_broadcast()` があり、Windows/Linux ともに内部ヘルパー `set_int_option()` で `setsockopt` を隠蔽しています。  
  一方、`app/cplat/AGENTS.md` では公開関数追加時に `api-cheatsheet.md`、公開テーブル、エクスポート テスト、`mock_cplat` の同期が義務付けられています。

- **Answer**:

  選択方式: 単一選択
    - [x] A (推奨): `cplat` に汎用オプション API (例: `cplat_socket_set_nodelay`) を追加し、`porter` の接続・受け入れ処理 (`thread_connect_with_timeout()` と `receiver_accept_loop()`) から呼ぶ
    - [ ] B: `cplat` の公開 API は増やさず、`cplat_socket_open(CPLAT_SOCKET_TCP)` と `cplat_socket_accept()` の内部既定動作として常に `TCP_NODELAY` を設定する
    - [ ] C: `cplat` へは手を加えず、`porter` 側のソケット生成・受け入れ箇所に OS 別の `setsockopt` 呼び出しを配置する
    - [ ] その他: 以下の自由記述欄に記入してください
    - [ ] X: 意図不明 (詳細な質問内容に再生成すること)

  その他の回答 / 補足:

  回答者: Tetsuo Honda / 回答日時: 2026-11-16 07:25:23 JST (出典ファイルへの直接記入)、2026-11-16 07:26:16 JST (今回チャットでの受領)

- **Assessment**:  
  A の単一選択であり、補足欄との不一致や規範との矛盾はありません。  
  `cplat` に汎用 API `cplat_socket_set_nodelay()` を追加し、OS 差異を `cplat` に隠蔽する一方、`cplat_socket_open()` や `cplat_socket_accept()` の既定挙動は変えず他利用者への非干渉を保ちます。`app/cplat/AGENTS.md` に従い、公開ヘッダー、両 OS 実装、`docs/api-cheatsheet.md`、`docs/functional-spec/net.md` 整合、`exportTest.cc`、`mock_cplat`、`socketTest.cc` を同期対象として詳細計画へ具体化します。

- **Recommendation**:  
  **案 A を推奨します。**

  `cplat` がプラットフォーム差分の抽象化を担当し、`porter` が接続形態に応じたオプション適用方針を決める既存の責務分離 (`SO_REUSEADDR`、`SO_BROADCAST` と同じ枠組み) と一致するためです。

- **Related**: [Q-001](#q-001-bl-001-の-tcp_nodelay-設定方針はどうしますか)、[Q-003](#q-003-tcp_nodelay-設定に失敗した場合の-porter-の動作方針はどうしますか)

### [Q-003] TCP_NODELAY 設定に失敗した場合の porter の動作方針はどうしますか

- **Status**: 解決済み
- **Question**:  
  接続ソケットまたは受け入れソケットに対する `TCP_NODELAY` の設定が失敗した場合、`porter` はどのように振る舞うべきですか。

- **Context**:  
  `porter` の `api_open_paths_tcp.c` と `thread_connect_prepare.c` では、`cplat_socket_set_reuse_address()` の失敗を致命エラーにせず、コメントで互換性向上のためのベスト エフォート設定である旨を明記して処理を継続しています。  
  一方で、設定失敗を見過ごしたまま低遅延を前提に運用する懸念もあります。

- **Answer**:

  選択方式: 単一選択
    - [x] A (推奨): `SO_REUSEADDR` と同様にベスト エフォートとし、警告ログ (`POTR_TRACE`) を記録した上で接続処理を継続する
    - [ ] B: `TCP_NODELAY` 設定失敗時は接続確立失敗としてソケットを閉じ、送信側の再接続または受信側の次 accept へ回す
    - [ ] C: テスト時は失敗検知できるようにし、本番相当では継続する等のビルド/環境切り替えを設ける
    - [ ] その他: 以下の自由記述欄に記入してください
    - [ ] X: 意図不明 (詳細な質問内容に再生成すること)

  その他の回答 / 補足:

  回答者: Tetsuo Honda / 回答日時: 2026-11-16 07:25:23 JST (出典ファイルへの直接記入)、2026-11-16 07:26:16 JST (今回チャットでの受領)

- **Assessment**:  
  A の単一選択であり、整合性に問題はありません。  
  `cplat_socket_set_nodelay()` が失敗した場合はソケットを閉じず、`POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_WARNING, &detail, ...)` で `service_id`・`path_idx`・`cplat_error` 詳細を警告出力した上で接続確立処理を継続する方針で確定します。`porter` 公開契約 (`prod/include/porter/`) や機能仕様の変更は発生しません。

- **Recommendation**:  
  **案 A を推奨します。**

  `TCP_NODELAY` の設定可否はデータ整合性やセキュリティ境界には影響しないため、特殊な仮想ネットワーク環境などで万一オプション拒否が起きても通信継続を優先する方が運用上の安全性が高まるためです。

- **Related**: [Q-001](#q-001-bl-001-の-tcp_nodelay-設定方針はどうしますか)、[Q-002](#q-002-tcp_nodelay-設定の-cplat-と-porter-の責務分担はどうしますか)

### [Q-004] BL-002 の原因特定と改善着手の進め方はどうしますか

- **Status**: 解決済み
- **Question**:  
  BL-002 (小メッセージ・圧縮送信・N:1 外部返信の性能低下) について、次のステップをどの順序で進めますか。

- **Context**:  
  コード調査により、圧縮ごとの 65KiB 超確保・全量 `cplat_secure_zero`、`potr_handle` の呼び出しあたりロック、送信キューの無条件 `condvar` 起床、`potr_notify` のロック内探索、N:1 `peers_mutex` 保持などの候補を特定しました。  
  ただし、どの要因が Windows/Linux 各条件にどれだけ寄与しているかの実測内訳は未取得であり、先に BL-001 で TCP 側の 40[ms] 停止を揃えないと TCP 測定がマスクされます。

- **Answer**:

  選択方式: 単一選択
    - [x] A (推奨): 公開契約・ハンドル寿命安全性・資源回収不変条件を維持した前提で、まず BL-001 単独確認と両 OS のプロファイル取得 (段階 1・段階 2) を行って主因の寄与率を定量化し、その結果に基づいて設計書と改善順序を確定する
    - [ ] B: プロファイル取得を待たず、最も明白な圧縮バッファー確保改善や送信キュー起床制御の試験実装から先に着手する
    - [ ] C: ハンドル寿命管理で増えたコストとして現行性能を許容し、本案件では BL-001 のみに対処して BL-002 のコード改修は見送る
    - [ ] その他: 以下の自由記述欄に記入してください
    - [ ] X: 意図不明 (詳細な質問内容に再生成すること)

  その他の回答 / 補足:

  回答者: Tetsuo Honda / 回答日時: 2026-11-16 07:25:23 JST (出典ファイルへの直接記入)、2026-11-16 07:26:16 JST (今回チャットでの受領)

- **Assessment**:  
  A の単一選択であり、補足欄との矛盾はありません。  
  今回の詳細計画 (`261115-porter-transport-performance-design.md`) では、BL-001 の単独 A/B 比較 (`off` vs `on`) と、双方 `on` で条件を揃えた BL-002 の両 OS 8 観点プロファイル・区間計測・非計装分離測定・Windows/WSL2 交互測定の手順を確定します。BL-002 の具体的な実装設計および数値目標の確定は、計測完了後の別判断ゲートとして明確に分離します。

- **Recommendation**:  
  **案 A を推奨します。**

  Windows と Linux (および UDP と TCP、N:1 外部返信) で劣化率の偏りが大きく、計測者効果を抑えた実測で主因を確定してから 1 施策ずつ A/B 評価する方が、手戻りと契約リスクを最小化できるためです。

- **Related**: [Q-001](#q-001-bl-001-の-tcp_nodelay-設定方針はどうしますか)、[Q-005](#q-005-進行中の-porter-reject-recovery-案件と測定ベースライン新規課題はどう扱いますか)

### [Q-005] 進行中の porter-reject-recovery 案件と測定ベースライン・新規課題はどう扱いますか

- **Status**: 解決済み
- **Question**:  
  現在進行中の `porter-reject-recovery` 案件と、その過程で今後見つかり得る新規課題について、本案件との境界をどのように定義しますか。

- **Context**:  
  `261115-porter-reject-recovery-discussion.md` は現在 WBS 4.1 実施中で、`app/porter` 配下に未コミット差分があります。  
  本案件の計画作成自体は先行できますが、実測段階に入ると基準コミットと作業ツリー差分の違いが P3 やウィンドウ挙動へ直接影響します。また今後 `porter-reject-recovery` 側で新しい課題が追加される可能性もあります。

- **Answer**:

  選択方式: 単一選択
    - [ ] A (推奨): 計画作成は完了とせず先行して進め、実測ベースラインは固定コミット (`6e9002c` / `fd903dc1` 等) と未コミット差分の記録で明確化して着手前に進行差分を再確認し、`porter-reject-recovery` 側の今後の新規課題は本案件へ自動追加せず都度別判断とする
    - [x] B: `porter-reject-recovery` が第 3 ゲートまで完全完了してコミットされるまで、本検討の調査・段階 1/2 の方針決定も凍結する
    - [ ] C: `porter-reject-recovery` の進行中差分および今後追加される新規課題をすべて自動的に本案件のスコープへ取り込む
    - [ ] その他: 以下の自由記述欄に記入してください
    - [ ] X: 意図不明 (詳細な質問内容に再生成すること)

  その他の回答 / 補足: porter-reject-recovery は完了した。状況を確認してください。

  回答者: Tetsuo Honda / 回答日時: 2026-11-16 07:25:23 JST (出典ファイルへの直接記入)、2026-11-16 07:26:16 JST (今回チャットでの受領)

- **Assessment**:  
  B の単一選択および補足「porter-reject-recovery は完了した。状況を確認してください。」を受領し、一次資料とリポジトリ状態を再確認しました。  
  `app/porter` の `git status` では本検討ファイル (`261115-porter-transport-performance-discussion.md`) のみが未追跡で、元案件の未コミット差分はありません。`app/porter` の `HEAD` は `11016c1` (元案件完了承認記録)、その親は `68ab091` (REJECT 受信再開実装) です。`261115-porter-reject-recovery-discussion.md` は版 4・状態 `完了`・WBS 1〜5 全完了、2026-11-16 Tetsuo Honda による第 3 ゲート承認済みで、CI run 43724723928 全 8 ジョブ成功 (警告アーティファクトなし) および Windows P3 再測定 5 回×各 5 試行=25 試行全件再開が記録されています。  
  以上により案 B の凍結条件は完全に満了しており、他の Q-001〜Q-004 の回答とも矛盾しません。今後の新規測定ベースラインは完了後固定版 (`porter 11016c1` / `cplat 822dc7da`、親 `721131ef` を候補とする基準版) として旧測定記録 (`6e9002c` / `fd903dc1`) と区別し、P3 は新基準版で再検証します。また、本案件の開始時点で `porter-reject-recovery` 自体は完了済みのため、残課題は BL-001 と BL-002 のみに限定された状態で第 1 ゲートを通過できます。

- **Recommendation**:  
  **案 A を推奨します。**

  スコープ膨張を防ぎつつ、測定再現性を基準コミット・差分記録・再確認手順で担保でき、BL-001 / BL-002 の計画策定を親案件の進捗と安全に並行できるためです。

- **Related**: [Q-004](#q-004-bl-002-の原因特定と改善着手の進め方はどうしますか)

### [Q-006] BL-001 の TCP_NODELAY 常時適用と測定済み性能低下をどう扱いますか

- **Status**: 解決済み
- **Question**:  
  全 TCP ソケットへの `TCP_NODELAY` 常時適用を維持し、Linux P5 の大きい blocking 送信で測定した throughput 低下を受容しますか。

- **Context**:  
  P4 の close p50 は Linux で 42.421[ms] から 0.614[ms] へ短縮し、Windows の close p95 は 15.656[ms] から 1.823[ms] へ短縮しました。一方、P5 の 5000[B] blocking・4 スレッドは Linux で 16,526 から 3,655[msg/s] へ 77.9%低下しました。P5 の 17 条件における throughput 相対変化の中央値は Windows +4.9%、Linux +81.9% です。詳細は [検証記録](261115-porter-transport-performance-validation.md) を参照してください。

- **Answer**:

  選択方式: 単一選択
    - [x] A: 全 TCP ソケットへの常時適用を維持し、測定した大きい blocking 送信の低下を既知のトレードオフとして受け入れる
    - [ ] B: 実装は保留状態とし、実利用ワークロードと許容性能を確認してから完了可否を判断する
    - [ ] C: 小さいメッセージなどへ適用を絞るか、利用者が選べる設定を設ける案を別途設計する
    - [ ] D: 常時適用を取り下げ、追加設計が決まるまで無効に戻す
    - [ ] その他: 以下の自由記述欄に記入してください
    - [ ] X: 意図不明 (詳細な質問内容に再生成すること)

  その他の回答 / 補足:

  回答者: Tetsuo Honda / 回答日時: 2026-11-17 04:22:00 JST (チャットでの選択)

- **Assessment**:  
  A の選択により、既存の常時適用方針を維持し、P5 で測定した throughput 低下を既知のトレードオフとして記録します。BL-001 は対応完了として backlog を更新します。

- **Recommendation**:  
  追加推奨は不要です。回答 A を確定方針として反映します。

- **Related**: [Q-001](#q-001-bl-001-の-tcp_nodelay-設定方針はどうしますか)、[Q-003](#q-003-tcp_nodelay-設定に失敗した場合の-porter-の動作方針はどうしますか)

### [Q-007] BL-002 の低負荷サンプリング方式をどう検証しますか

- **Status**: 解決済み
- **Question**:  
  wrap 計測に代えて別の低負荷サンプリング方式を設計し、少数の代表条件で計測影響と関数帰属の妥当性を検証してから本調査へ進みますか。

- **Context**:  
  Windows の wrap は通常送信 throughput を最大 91.5%低下させ、`GetThreadTimes` の CPU 差分は 15.625[ms] 単位に量子化されました。Linux は `perf_event_open` が `EACCES` となり、CPU 時間へのフォールバックでも P1 normal の throughput が 68.8%低下しました。既存 CPU 値は通常実行の関数別寄与率を示す根拠になりません。計測結果の詳細は [検証記録](261115-porter-transport-performance-validation.md) を参照してください。

- **Answer**:

  選択方式: 単一選択
    - [ ] A: WPR/ETW と Linux `perf` を使える実行環境・権限を整え、外部サンプリングで再計測する
    - [x] B: 製品コードと分離した一時ビルドで別の低負荷方式を設計し、通常版との負荷差と関数帰属の妥当性を小規模に検証する
    - [ ] C: BL-002 の原因調査を保留し、現在の A/B 結果だけを記録して製品改修には進まない
    - [ ] その他: 以下の自由記述欄に記入してください
    - [ ] X: 意図不明 (詳細な質問内容に再生成すること)

  その他の回答 / 補足:

  回答者: Tetsuo Honda / 回答日時: 2026-11-17 04:22:00 JST (チャットでの選択)

- **Assessment**:  
  B の選択に基づき、製品ソースと公式 A/B 結果から分離した一時プロファイル用複製で、低負荷サンプリング候補の設計と pilot を行います。計測者効果と関数別帰属の信頼性が確認できるまで BL-002 の本調査と製品改修には進みません。

- **Recommendation**:  
  選択された B に沿って、小規模 pilot の結果を確認してから本調査への適用可否を判断します。

- **Related**: [Q-004](#q-004-bl-002-の原因特定と改善着手の進め方はどうしますか)、[Q-006](#q-006-bl-001-の-tcp_nodelay-常時適用と測定済み性能低下をどう扱いますか)

## 版 2 時点の対象成果物と合意内容

以下は第 1 ゲート通過時点の記録です。後続の実装・測定結果と進捗は、上の作業 WBS および検証記録を参照してください。

検討用 Markdown の合意版 (第 1 ゲート): 版 2 (2026-11-16、[Q-001](#q-001-bl-001-の-tcp_nodelay-設定方針はどうしますか) 〜 [Q-005](#q-005-進行中の-porter-reject-recovery-案件と測定ベースライン新規課題はどう扱いますか) すべて解決済み・Q-005 凍結条件満了確認済み)  
詳細対処計画 (第 2 ゲート承認済み): `app/porter/docs/prompts/current/261115-porter-transport-performance-design.md`

版 2 当時に変更・作成したファイルは次の 3 ファイルです。この記録は版 2 時点の作業範囲を示します。

- 更新 (版 2 時点の本検討): `app/porter/docs/prompts/current/261115-porter-transport-performance-discussion.md` (第 1 ゲート通過、第 2 ゲート承認待ち。承認は 2026-11-16 に受領)
- 新規作成 (詳細対処計画): `app/porter/docs/prompts/current/261115-porter-transport-performance-design.md` (最終対処計画。ただし BL-002 の実装設計確定と数値目標合意は計測後の別判断)
- 状態更新 (元残課題): `app/porter/docs/prompts/past/261115-porter-reject-recovery-backlog.md` (BL-001、BL-002 の状態を「検討中 (計画承認待ち・実装未着手)」に維持し、凍結条件満了と `design.md` 参照を追記。対応済みにはしない)

受入条件:

- 今回の計画文書の受入条件は `261115-porter-transport-performance-design.md` の「今回の計画策定の受入条件 (第 2 ゲート〜第 3 ゲート)」に定義しました。
- 将来の実測・実装工程における数値目標は現時点で未合意であり、段階 2 のプロファイル計測後に別判断で確定します。

## 版 1/2 の反映と検証の記録

| 項目 | 実施内容 | 結果 / メモ |
| --- | --- | --- |
| コード・文書調査 (版 1) | `cplat` ソケット、TCP 接続・終端、送信・圧縮、ハンドル・通知・送信キュー、P1〜P5 検証記録、進行中案件を確認 | 事実と仮説を分離して本文へ記録済み |
| 元案件完了確認 (版 2) | `git -C app/porter status`、`HEAD 11016c1`、`261115-porter-reject-recovery-discussion.md` の状態・WBS・第 3 ゲート・CI 結果・P3 再測定を再確認 | 凍結条件満了・矛盾なしを確認 |
| 詳細対処計画作成 (版 2) | `261115-porter-transport-performance-design.md` を新規作成し、`261115-porter-reject-recovery-backlog.md` へ状態・経緯を追記 | 作成時は第 2 ゲート承認待ち。2026-11-16 に承認を受領 |
| `text_style_jp.py` | 変更 3 ファイルに対し `--dry-run` 確認後に `--in-place` を適用 | 適用結果を最終検証後に確認 |
| ビルド・テスト・測定 | 本依頼の制約により未実施 | 対象外 (計画策定のみ) |

Table: 反映と検証の記録

## 版 3〜6 の実行と中断記録

| 項目 | 実施内容 | 結果 / メモ |
| --- | --- | --- |
| BL-001 実装・局所検証 (版 3) | `cplat_socket_set_nodelay` を追加し、porter の全 TCP 接続経路で初回通信前に設定。両 OS の対象 leaf と新規 3 件を確認 | 実装・局所検証済み。Linux 全件 `make test` と Doxygen 生成は未完了 |
| P4/P5 A/B (版 4) | 固定 `off` / `on` 版を両 OS で各 5 回測定 | 約 40[ms] 級の遅延改善と Linux 大きい blocking 送信の throughput 低下を確認 |
| wrap プロファイル実行可能性 (版 5) | Windows と Linux で計装影響・CPU 計数を確認 | 関数別 CPU 寄与率の根拠としては不成立 |
| Q-006/Q-007 回答 (版 6) | Q-006 A、Q-007 B を受領し、この検討文書と対策元 backlog を更新 | BL-001 の trade-off は受容済み。BL-002 は低負荷 pilot を合意 |
| 一時中断 | ユーザー指示で Grok pilot セッションを停止し、一時作業領域を確認 | `profile/pilot/` はなく、pilot 用ソース、ビルド、測定は未着手。WBS 項目 12 から再開 |

Table: 版 3〜6 の実行と中断記録
