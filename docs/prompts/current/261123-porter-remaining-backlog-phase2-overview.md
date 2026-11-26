<!--
生成元: agree-before-implement スキル
文書種別: 全体管理文書 (overview)
最終更新日時: 2026-11-27 02:09:13 JST
最終更新エージェント: agy (gemini-4-hydrogen-medium、主セッション session_01KJc53RTnwmpzd7VMLf1qR9)
稼働 OS: Linux (WSL)
上流文書: 対策元の残課題一覧は、`261123-porter-remaining-backlog-backlog.md` です。削除されている場合があります。
下流文書: `261123-porter-remaining-backlog-phase2-discussion.md` (検討用 Markdown)、`261123-porter-remaining-backlog-phase2-lightadr.md` (簡易 ADR)、`261123-porter-remaining-backlog-phase2-design.md` (最終仕様)、`261123-porter-remaining-backlog-phase2-backlog.md` (残課題一覧)。未作成の場合や、削除されている場合があります。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。この文書と、ユーザーが編集した最新の検討用 Markdown を読み直し、作業 WBS の `← **CURRENT**` の項目から再開します。最終更新エージェントのセッション情報や稼働 OS・最終更新日時が参照可能であれば再開時のコンテキストや環境差分の把握に利用し、別エージェントや別環境・別 OS からの引き継ぎ、担当変更によりセッションが見つからない場合も異常とせず、文書の記載から継続してください。`← **CURRENT**` の項目は処理が中止された項目である場合があり、どこまで進んでいたかは文書だけでは分かりません。対象成果物の内容を確認して実施済みの範囲を特定してから、作業を再開してください。`← **CURRENT**` がなく、作業 WBS がすべて完了 (✅) であれば、この案件の合意フローは完了しています。
-->

# porter の残課題の対策 (第 2 期) 全体管理

ファイル名: `261123-porter-remaining-backlog-phase2-overview.md`

この文書は、案件の識別、作業 WBS、対象範囲、確認状況、経緯を管理します。質問と回答の本文は検討用 Markdown (`261123-porter-remaining-backlog-phase2-discussion.md`) に、決定事項は簡易 ADR (`261123-porter-remaining-backlog-phase2-lightadr.md`) に記載し、この文書には重複して記載しません。

## 案件の識別

検討 ID: 261123-porter-remaining-backlog-phase2

検討用 Markdown の版: 2

状態: 作業の実施中

## 作業 WBS と進捗管理

この案件の作業 WBS です。項目 4 の作業対象は、最終仕様で確定したファイルごとの項目です。  
決定事項は、Q を解決済みにした時点、またはユーザーが決定を明示した時点で `261123-porter-remaining-backlog-phase2-lightadr.md` へ反映します。見送りの決定や範囲外の気付きは、どの段階でも生じた時点で `261123-porter-remaining-backlog-phase2-backlog.md` に追記します。対策元の残課題一覧 (`261123-porter-remaining-backlog-backlog.md`) の状態と経緯も、進行の各段階で更新します。

凡例: ⏸️ 未着手、🔄 実施中、🚫 ブロック中、⚠️ 要注意、✅ 完了。現在取り組んでいる項目の末尾に `← **CURRENT**` を付けます。

- ✅ 1. 検討用 Markdown のための情報収集と質問の実施
    - ✅ 1.1. 前提条件と要求事項の調査 (対策元の残課題一覧の 10 項目について、Linux (WSL) でできる範囲の確認。codex (gpt-6.2-sol、effort low) の読み取りの調査と、主エージェントによる確認。対策元の残課題一覧の状態を「検討中」へ更新)
    - ✅ 1.2. 課題・論点の抽出と確認項目 (Q-001 から Q-007) の作成
    - ✅ 1.3. 版 1 の回答の受付と記録 ([Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲) から [Q-007](261123-porter-remaining-backlog-phase2-discussion.md#q-007-サニタイザー-bl-009-の扱い)。2026-11-25 20:34:47 に保存)
    - ✅ 1.4. 回答の検証、簡易 ADR への反映、対策元の残課題一覧への方針と見送りの反映、この案件の残課題一覧の作成、WBS の見直し (簡易 ADR の LADR-001 から LADR-007 を作成。対策元の BL-001 から BL-008 を「次フェーズへ転記」、BL-009 を「完了」に更新。この案件の残課題一覧に BL-001 から BL-008 を転記)
    - ✅ 1.5. 版 2 の確認項目 (Q-008 から Q-014) の作成 (Windows の実装の担当、Registered I/O の実現性の確認の時期と成立しない場合の扱い、cplat の API の形、Registered I/O を使えるソケットの範囲、Windows の性能の基準、N 対 1 のまとめる単位)
    - ✅ 1.6. 版 2 の回答の受付と記録、回答の検証 (2026-11-25 20:59:35 に保存。Q-008 から Q-014 はすべて推奨の A。解決済みにし、簡易 ADR の LADR-002、LADR-003、LADR-004 を更新し、LADR-008、LADR-009 を追加)
    - ✅ 1.7. Windows のセッションでの Registered I/O の試作 (2026-11-25 に結果を受領。併用は不適合で、時間の比較は実施できず。結果は「反映と検証の記録」に記録。[Q-010](261123-porter-remaining-backlog-phase2-discussion.md#q-010-registered-io-が成立しない場合の扱い) の A により、Windows は `sendto` の繰り返しに確定)
- ✅ 2. 前提合意と残懸念の解消 (第 1 ゲート: 最終仕様作成への着手)
    - ✅ 2.1. 全論点に対する未解決事項および質問の解消確認 (未回答と未解決の Q はなし。Windows の試作の結果で Q-012 の前提が不成立となった扱いは、Q-010 の A と自走の指示で決定済み。満たせない受入と代案は残課題一覧の BL-011 から BL-013 へ送付)
    - ✅ 2.2. 方針・前提条件の合意確定 (簡易 ADR の決定事項と、解決済みの Q の回答またはユーザーの指示との一致の確認) (LADR-001 から LADR-010 を照合し、一致を確認。第 1 ゲートを通過)
- ✅ 3. 最終決定事項 (最終仕様) の作成と承認の受付
    - ✅ 3.1. `261123-porter-remaining-backlog-phase2-design.md` の作成 (対策元の残課題一覧へ確定した方針を反映) (作成済み。対策元の残課題一覧の BL-010 を「対応中」へ更新)
    - ✅ 3.2. 最終仕様における受入条件と整合性の確認 (受入条件 1 から 8 と、簡易 ADR の LADR-001 から LADR-010 の一致を確認)
    - ✅ 3.3. 対話セッションにおける最終仕様の承認の受付 (第 2 ゲート) (ユーザーの自走の指示 (2026-11-25 21:48 頃) により承認済みとして扱う。簡易 ADR の LADR-010)
- 🔄 4. 承認された最終仕様に従った作業の実施
    - ✅ 4.1. `framework/testfw` の `sendmmsg()` の mock の追加 (`include/sys/mock_socket.h`、`libsrc/mock_libc/mock_sys_socket.cc`) とビルド
    - ✅ 4.2. `app/cplat` の `cplat_socket_watch_sendto_multi()` の追加 (`prod/include/cplat/net/socket.h`、`prod/libsrc/cplat/net/socket_watch_linux.c`、`socket_watch_windows.c` の Windows の初版)
    - ✅ 4.3. `app/cplat` の `mock_cplat`、`exportTest`、単体テスト (`test/src/libcplatTest/net/socketTest/socketTest.cc`) の追加と、Linux のビルドとテスト
    - ✅ 4.4. `app/cplat` の文書 (`docs/api-cheatsheet.md`、`docs/functional-spec/net.md`) の更新と `check_functional_spec.py`
    - ✅ 4.5. `app/porter` のまとめ送りの資源の名前の変更と UDP への確保 (`potr_send_thread.h`、`potr_context.h`、`api/potr_service_open.c`、`api/api_context_cleanup.c` と参照箇所)
    - ✅ 4.6. `app/porter` の `potr_internal_udp_sendto_multi()` の追加 (`infra/potr_socket_io.c`、`.h`) と mock (`mock_porter`)
    - ✅ 4.7. `app/porter` の送信スレッドの UDP のまとめ送り (`thread/potr_send_thread.c` の 1 対 1 と N 対 1) と、`potr_internal_flow_control_peer_state()` の送出できる数の出力 (`potr_flow_control.h`、`potr_flow_control.c`)
    - ✅ 4.8. `app/porter` の単体テスト (`potrSocketIoTest`、`potrSendThreadTest`、`potrFlowControlTest`) の追加と変更、Linux のビルドとテスト
    - ✅ 4.9. 正本ドキュメントおよびコード コメントへの永続的知見の反映 (`potr_send_thread.c` の先頭のコメント、`potr_send_thread.h`、`potr_socket_io.h`、`docs/architecture.md`、`docs/protocol.md`、cplat の Windows と Linux の実装のコメント)
    - ✅ 4.10. Linux の `make test` (`app/cplat`、`app/porter`)、`.warn` の確認、`text_style_jp.py`
    - ✅ 4.11. Windows のセッションでのビルドとテストと修正 (主エージェントが差分を仲介。依頼 16 と 17 で cplat と porter のビルド、テスト、`mock` の `/INCLUDE` リンクが成功。MSVC の警告と内容のある `.warn` はなく、MSVC 向けの修正は不要)
    - 🔄 4.12. Linux と Windows の性能の測定と記録 (受入条件 1 から 4。Linux は測定済み、Windows は未測定。受入条件 4 (Windows) は未判定) ← **CURRENT**
    - ⏸️ 4.13. サブモジュールの順の main へのコミットと push、親リポジトリの参照の更新、CI の確認 (Git の操作は主エージェント)
    - ⏸️ 4.14. 受入条件への適合確認と、対策元の残課題一覧の BL-010 の更新
    - ⏸️ 4.15. 簡易 ADR の最終確認
    - ⏸️ 4.16. 残課題一覧 (`261123-porter-remaining-backlog-phase2-backlog.md`) の再構築と整理 (対策元の残課題一覧の最終状態と転記先の確定)
- ⏸️ 5. 成果物の承認の受付 (第 3 ゲート)
    - ⏸️ 5.1. 成果物・検証結果・残課題の提示
    - ⏸️ 5.2. 対話セッションにおける成果物の承認の受付

## 対象と参照資料

対象成果物: BL-010 の対策 ([Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲) の回答)。`framework/testfw`、`app/cplat`、`app/porter` の 3 つのサブモジュールを変更します。具体的なファイルは、最終仕様 `261123-porter-remaining-backlog-phase2-design.md` にあります。

今回の対象範囲: 対策元の残課題一覧の BL-010 だけ。BL-001 から BL-008 は、この案件の残課題一覧 (`261123-porter-remaining-backlog-phase2-backlog.md`) へ転記しました。BL-009 は、導入しないと決めて閉じました。

書き込み可能な検討文書: この文書、検討用 Markdown (`261123-porter-remaining-backlog-phase2-discussion.md`)、簡易 ADR (`261123-porter-remaining-backlog-phase2-lightadr.md`)、残課題一覧 (`261123-porter-remaining-backlog-phase2-backlog.md`)、対策元の残課題一覧 (`261123-porter-remaining-backlog-backlog.md`)

参照資料と版:

- 対策元の残課題一覧 `261123-porter-remaining-backlog-backlog.md` (2026-11-23 12:45 の版)
- 前の案件の全体管理文書 `261123-porter-remaining-backlog-overview.md` の「反映と検証の記録」(測定の値)
- `app/porter` main `99d4da6`、`app/cplat` main `988b8720`、`framework/makefw` main `d8177db`、親リポジトリ main `6e5010eb`
- codex (gpt-6.2-sol、effort low、読み取り専用) の検討の結果 (セッションの一時領域。リポジトリには入れていません)
- 版 2 の調査の時点のリポジトリ: 親リポジトリ main `9d37dce0`、`app/porter` `f643d3a`、`app/cplat` `45217b7d`、`framework/testfw` `1f79ca1`
- Microsoft Learn の Registered I/O (`RIOSendEx`、`RIOCreateRequestQueue`、`RIOCreateCompletionQueue`)、`TransmitPackets`、`IPPROTO_UDP` のソケット オプションの頁 (2026-11-25 に参照。`WSASendMsg` の頁は取得できず)
- Windows 側の別の作業ツリー `/mnt/d/Users/tetsuo/Local/repos/c-modernization-kit` (HEAD `9d37dce0`。主エージェントがパッチの受け渡しを仲介)

ユーザーの指示 (2026-11-23): 「`261123-porter-remaining-backlog-backlog.md` の次の対処計画を進行したい。」  
計画の検討には、前の案件の指示 (計画立案の検討に codex の GPT-6.2-Sol low を使う) を、同じ種類の作業として引き続き適用しました。作業の実施の段階のサブエージェントの使い方は、最終仕様の承認の時点で確認します。

ユーザーの指示 (2026-11-25 21:48 頃、主エージェント経由。ほかの案件と同じ文言): 「ユーザーの確認なく継続的に自走すること」「決定した最終仕様が実現できない場合は、ユーザーに問い合わせることなく backlog に送る」「各サブモジュール (案件) 完了時に main にプッシュし CI を起動させ…」「main へのコミットを事前許可」。第 2 ゲートは、この指示により承認済みとして扱います (簡易 ADR の LADR-010)。

ユーザーの指示 (2026-11-25、主エージェント経由): 「どれも書きました。最終仕様策定は Opus で行ってください。その際、Sonnet の掘り下げが浅いものは、書き直したり再質問をユーザーに行ってもよいです。」

## 観点別の確認

Table: 観点別の確認

| 観点 | 状態 | 根拠・関連 Q |
| --- | --- | --- |
| 目的・利用者 | 確認済み | [Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲) (BL-010 の対策) |
| 範囲・境界 | 確認済み | [Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲)、[Q-003](261123-porter-remaining-backlog-phase2-discussion.md#q-003-bl-010-を適用する通信種別と経路の範囲)、[Q-007](261123-porter-remaining-backlog-phase2-discussion.md#q-007-サニタイザー-bl-009-の扱い)、[Q-014](261123-porter-remaining-backlog-phase2-discussion.md#q-014-n-対-1-で一括の送信にまとめる単位) |
| 入力・根拠 | 確認済み | 対策元の残課題一覧、前の案件の測定の記録、現行のソース、Microsoft Learn の頁、Windows の Registered I/O の試作の結果 (「反映と検証の記録」) |
| 整合性・依存関係 | 確認済み | [Q-011](261123-porter-remaining-backlog-phase2-discussion.md#q-011-cplat-に追加する一括の送信の-api-の形)、[Q-012](261123-porter-remaining-backlog-phase2-discussion.md#q-012-windows-で-registered-io-を使えるようにするソケットの範囲) (Q-012 は試作で前提が不成立のため適用しない。簡易 ADR の LADR-004) |
| 実現性・例外 | 確認済み | [Q-006](261123-porter-remaining-backlog-phase2-discussion.md#q-006-一括の送信で一部だけ送れた場合の残りの扱い)、[Q-008](261123-porter-remaining-backlog-phase2-discussion.md#q-008-windows-の一括の送信の実装の担当)、[Q-010](261123-porter-remaining-backlog-phase2-discussion.md#q-010-registered-io-が成立しない場合の扱い) は解決済み。Registered I/O は、[Q-009](261123-porter-remaining-backlog-phase2-discussion.md#q-009-registered-io-の実現性の確認の時期) の試作で不成立と確認し、Q-010 の A で Windows は `sendto` の繰り返しに確定 |
| 受入条件・検証 | 確認済み | Linux は [Q-005](261123-porter-remaining-backlog-phase2-discussion.md#q-005-bl-010-の性能の受入の基準)、Windows は [Q-013](261123-porter-remaining-backlog-phase2-discussion.md#q-013-windows-の性能の受入の基準)。Windows の「上がる」の基準と、暗号化と複数経路の性能は、自走の指示により残課題一覧の BL-012、BL-013 へ送付 |

## 質問一覧

各 Q の本文は検討用 Markdown にあります。

Table: 質問一覧

| Q-ID | 論点 | 状態 | 依存・関連 | 反映先の決定 |
| --- | --- | --- | --- | --- |
| [Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲) | この案件で対策する残課題の範囲 | 解決済み | なし | [LADR-001](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-001-対策する残課題の範囲) |
| [Q-002](261123-porter-remaining-backlog-phase2-discussion.md#q-002-windows-での確認の方法) | Windows での確認の方法 | 解決済み | Q-001、Q-008 | [LADR-002](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-002-windows-での確認の方法と実装の担当) |
| [Q-003](261123-porter-remaining-backlog-phase2-discussion.md#q-003-bl-010-を適用する通信種別と経路の範囲) | BL-010 を適用する通信種別と経路の範囲 | 解決済み | Q-001、Q-005、Q-014 | [LADR-003](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-003-一括の送信を適用する範囲) |
| [Q-004](261123-porter-remaining-backlog-phase2-discussion.md#q-004-bl-010-の-windows-での実装の方式) | BL-010 の Windows での実装の方式 | 解決済み | Q-001、Q-002、Q-008 から Q-013 | [LADR-004](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-004-windows-の一括の送信の方式) |
| [Q-005](261123-porter-remaining-backlog-phase2-discussion.md#q-005-bl-010-の性能の受入の基準) | BL-010 の性能の受入の基準 | 解決済み | Q-003、Q-013 | [LADR-005](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-005-linux-の性能の受入の基準) |
| [Q-006](261123-porter-remaining-backlog-phase2-discussion.md#q-006-一括の送信で一部だけ送れた場合の残りの扱い) | 一括の送信で、一部だけ送れた場合の残りの扱い | 解決済み | Q-003、Q-011 | [LADR-006](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-006-一部だけ送れた場合の残りの扱い) |
| [Q-007](261123-porter-remaining-backlog-phase2-discussion.md#q-007-サニタイザー-bl-009-の扱い) | サニタイザー (BL-009) の扱い | 解決済み | なし | [LADR-007](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-007-サニタイザーを導入しない) |
| [Q-008](261123-porter-remaining-backlog-phase2-discussion.md#q-008-windows-の一括の送信の実装の担当) | Windows の一括の送信の実装の担当 | 解決済み | Q-002、Q-004、Q-009 | [LADR-002](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-002-windows-での確認の方法と実装の担当) |
| [Q-009](261123-porter-remaining-backlog-phase2-discussion.md#q-009-registered-io-の実現性の確認の時期) | Registered I/O の実現性の確認の時期 | 解決済み | Q-004、Q-008、Q-010、Q-011、Q-012 | [LADR-004](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-004-windows-の一括の送信の方式) |
| [Q-010](261123-porter-remaining-backlog-phase2-discussion.md#q-010-registered-io-が成立しない場合の扱い) | Registered I/O が成立しない場合の扱い | 解決済み | Q-004、Q-009、Q-013 | [LADR-004](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-004-windows-の一括の送信の方式) |
| [Q-011](261123-porter-remaining-backlog-phase2-discussion.md#q-011-cplat-に追加する一括の送信の-api-の形) | cplat に追加する一括の送信の API の形 | 解決済み | Q-004、Q-006、Q-009、Q-012 | [LADR-008](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-008-cplat-に追加する一括の送信の-api-の形) |
| [Q-012](261123-porter-remaining-backlog-phase2-discussion.md#q-012-windows-で-registered-io-を使えるようにするソケットの範囲) | Windows で Registered I/O を使えるようにするソケットの範囲 | 解決済み | Q-009、Q-010、Q-011 | [LADR-004](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-004-windows-の一括の送信の方式) |
| [Q-013](261123-porter-remaining-backlog-phase2-discussion.md#q-013-windows-の性能の受入の基準) | Windows の性能の受入の基準 | 解決済み | Q-005、Q-010 | [LADR-009](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-009-windows-の性能の受入の基準) |
| [Q-014](261123-porter-remaining-backlog-phase2-discussion.md#q-014-n-対-1-で一括の送信にまとめる単位) | N 対 1 で一括の送信にまとめる単位 | 解決済み | Q-003 | [LADR-003](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-003-一括の送信を適用する範囲) |

## 対象成果物と合意版

最終決定事項 (最終仕様) の設計書: `261123-porter-remaining-backlog-phase2-design.md`

検討用 Markdown の合意版 (第 1 ゲート): 版 2 (Q-001 から Q-014、2026-11-25 20:59:35 の保存) と、ユーザーの自走の指示 (2026-11-25 21:48 頃)

最終仕様の合意版 (第 2 ゲート): 版 1 (2026-11-25。ユーザーの自走の指示により承認済みとして扱う)

更新予定の成果物: 最終仕様の「変更するサブモジュールと順序」から「文書とコード コメント」の各表に記載したファイル (`framework/testfw`、`app/cplat`、`app/porter`)

受入条件: 最終仕様の「受入条件」の 1 から 8

## 経緯

Table: 経緯

| 日時 (JST) | 出来事 | 内容・参照先 |
| --- | --- | --- |
| 2026-11-23 12:36 | 案件の開始 | ユーザーの指示を受けて開始。対策元の残課題一覧の 10 項目を「検討中」へ更新 |
| 2026-11-23 12:55 | 版 1 の提示 | 全体管理文書と検討用 Markdown (Q-001 から Q-007) を作成 |
| 2026-11-25 20:34 | 版 1 の回答 | ユーザーが Q-001 から Q-007 に回答し、保存 |
| 2026-11-25 20:50 | 再開 | 主エージェントの指示で、opus のサブエージェントが回答の検証から再開 |
| 2026-11-25 20:56 | 版 1 の回答の反映 | Q-001 から Q-007 を解決済みにし、簡易 ADR (LADR-001 から LADR-007) を作成。対策元の残課題一覧を更新し、この案件の残課題一覧に BL-001 から BL-008 を転記 |
| 2026-11-25 20:57 | 版 2 の提示 | Q-003 (C)、Q-004 (B) の推奨外の選択と、Q-002 (D) の補足から生じた論点を、Q-008 から Q-014 として追記。作業 WBS に 1.5 から 1.7 を追加 |
| 2026-11-25 20:59 | 版 2 の回答 | ユーザーが Q-008 から Q-014 に回答し、保存 (すべて推奨の A) |
| 2026-11-25 21:05 | 版 2 の回答の反映と試作の準備 | Q-008 から Q-014 を解決済みにし、簡易 ADR の LADR-002、LADR-003、LADR-004 を更新、LADR-008、LADR-009 を追加。Windows の試作の依頼書とソースをセッションの一時領域に作成し、Windows 側の結果待ちで停止 |
| 2026-11-25 21:48 | 自走の指示 | ユーザーが、確認なしの自走、実現できない仕様の残課題一覧への送付、main へのコミットの事前許可を指示 (簡易 ADR の LADR-010) |
| 2026-11-25 22:19 | Windows の試作の結果 | 併用は不適合。Windows は `sendto` の繰り返しに確定。簡易 ADR の LADR-004、LADR-005、LADR-008、LADR-009 を更新し、残課題一覧に BL-011 から BL-013 を追記 |
| 2026-11-25 22:23 | 第 1 ゲートの通過 | 未解決の Q と懸念がないことを確認し、最終仕様の作成に着手 |
| 2026-11-25 22:27 | 最終仕様の作成と承認 | `261123-porter-remaining-backlog-phase2-design.md` (版 1) を作成。自走の指示により第 2 ゲートを承認済みとして扱い、作業 WBS の項目 4 をファイルごとの項目へ書き直した。実装は主エージェントの指示を待つ |
| 2026-11-26 16:45 | 作業の実施の開始 | 主エージェントの指示で、opus のサブエージェントが作業 WBS 4.1 から 4.4 (testfw の `sendmmsg()` の mock と、cplat の `cplat_socket_watch_sendto_multi()`) に着手。testfw `84b1b78`、cplat `84ea0ec6` (いずれも main) を基点とする |
| 2026-11-26 18:30 | 作業 WBS 4.1 から 4.4 の完了 | Linux のビルドと局所テストが成功。Windows の確認は 4.11 で行う。結果は「反映と検証の記録」に記録。4.5 で停止 (主エージェントの指示を待つ) |
| 2026-11-26 18:35 | 作業 WBS 4.5 の再開 | 主エージェントの指示で、opus のサブエージェントが作業 WBS 4.5 から 4.10 (porter の UDP のまとめ送り) に着手。testfw `75cd774`、cplat `8252c026`、porter `1438394` (いずれも main) を基点とする |
| 2026-11-26 22:20 | 作業 WBS 4.5 から 4.10 の完了 | Linux のビルド、局所テスト、`make test` (`app/porter`、`app/cplat`) が成功。結果は「反映と検証の記録」に記録。4.11 (Windows の検証) で停止 (主エージェントが仲介) |
| 2026-11-26 22:44 | 作業 WBS 4.12 の Linux の性能の測定と記録 | Linux (WSL) の性能の測定結果を集計し、「反映と検証の記録」へ記録。受入条件 1 から 3 はすべて適合 (`p1` 5,000 byte `blocking` の `target` ÷ `tcp` は 0.97)。受入条件 4 (Windows) は未測定・未判定 |
| 2026-11-27 02:09 | 作業 WBS 4.11 の完了と Linux 件数の訂正 | Windows の検証結果 (依頼 16 と 17) を「反映と検証の記録」へ記録し、4.11 を完了へ更新。Linux の `potrFlowControlTest` 件数の訂正 (162 件→163 件) を追記し、残課題一覧に BL-014 を起票 |

## 反映と検証の記録

### 作業 WBS 1.7: Windows での Registered I/O の試作 (2026-11-25)

- 実施: Windows 側の Claude Code のセッション (主エージェントが仲介)。試作のソースと依頼書は、セッションの一時領域の `scratchpad/porter-rio-proto/` (`rio_proto.c`、`request.md`) です。結果は同じ場所の `windows-result/` (`rio_proto_out.txt`、`rio_proto.diff`、`probe.c`、`probe_out.txt`、`cl_out.txt`) にあります。リポジトリには入れていません。
- 環境: Windows 11 Pro for Workstations 10.0.26300、`cl` 19.50.35724、Xeon Gold 6254 × 2、電源プラン「バランス」。測定中のほかの重い処理はなし。Winsock のカタログは `mswsock.dll` だけで、LSP はなし。
- ソースの修正: `C3` の行のエラー番号の取り方だけ (呼び出しの後に `WSAGetLastError()` を取る)。
- 判定 1 (併用): 不適合。`WSA_FLAG_REGISTERED_IO` 付きの UDP ソケット (フラグ 0x101 と 0x100 のどちらも) では、`bind`、`getsockname`、`getsockopt(SO_TYPE)` は成功しますが、ブロッキングの `sendto` と `recvfrom`、`WSAEventSelect`、`select` は 10038 (`WSAENOTSOCK`)、`ioctlsocket(FIONBIO)` は 10045 (`WSAEOPNOTSUPP`) で失敗しました。RIO の関数表の取得、完了キュー、要求キューの作成より前でも同じです。フラグ 0x1 (`WSA_FLAG_OVERLAPPED` だけ) では、すべて成功しました。RIO の関数表の取得、完了キューと要求キューの作成、バッファーの登録は成功しました (`C6`)。
- 判定 2 (時間の比較): 判定 1 の不適合で、試作のプログラムが比較の前に止まるため、実施していません。
- 結論: porter の UDP のソケットは、受信の待機 (`WSAEventSelect`) と、受信と制御パケットの送信に通常の関数を使います。RIO のソケットではこれらが使えないため、Windows の一括の送信は `sendto` の繰り返しとします。Microsoft の資料で、この制約が文書化されているかは確認していません。
- Windows 側からの代案 (未実施): RIO の送信だけに使う専用のソケットを別に作る構成。送信元のポートが分かれるため、要件の判断が必要です。この案件の残課題一覧へ送ります。

### 作業 WBS 4.1 から 4.4: testfw の sendmmsg の mock と cplat の一括の送信 (2026-11-26、Linux (WSL))

- 基点: `framework/testfw` main `84b1b78`、`app/cplat` main `84ea0ec6`。Git の操作はしていません。
- 4.1 (testfw): `include/sys/mock_socket.h` と `libsrc/mock_libc/mock_sys_socket.cc` に、`sendto` と同じ形で `sendmmsg` の mock (置き換えのマクロ、`mock_sendmmsg()`、`delegate_real_sendmmsg()`、`MOCK_METHOD`、既定の委譲の `ON_CALL`) を追加しました。`struct mmsghdr` は `_GNU_SOURCE` を定義した場合だけ宣言されるため、ヘッダーで前方宣言し、`_GNU_SOURCE` のない C の翻訳単位でも警告なくコンパイルできることを、セッションの一時領域で確かめました。`make -C framework/testfw` は成功し、`.warn` はありません。
- 4.2 (cplat の実装): `socket.h` に `CPLAT_SOCKET_SENDTO_MULTI_MAX` (64)、`cplat_socket_datagram`、`cplat_socket_watch_sendto_multi()` と Doxygen コメントを追加し、`cplat_socket_watch_attach()` の登録中の規則の送信の関数に加えました。Linux は `sendmmsg()` (`EINTR` で呼び直し、先頭から `msg_len` が `len` と等しい数を返す)、Windows は既存の `send_once()` を先頭から順に呼ぶ初版です。Windows で Registered I/O を使わない理由と `see:` の URL を、`socket_watch_windows.c` の関数の前のコメントに残しました。
- 4.3 (cplat の mock とテスト): `mock_cplat` (`mock_cplat.h`、`mock_cplat.cc`、新規 `net/mock_cplat_socket_watch_sendto_multi.cc`)、`exportTest` の `CPLAT_EXPORT_TABLE_COMMON`、`socketTest.cc` の 8 個のテスト (Linux 7 個、Windows 5 個) と、登録の下位の呼び出しを期待するサブ手順 2 個 (`socketTest.expectMockWatchAttach`、`socketTest.expectMockWatchDetach`) を追加しました。
- 4.4 (cplat の文書): `docs/functional-spec/net.md` に `CPLAT-NET-FUNC-010` (UUID `148caece-1233-4a58-b153-212a3bb2ff7e`) と「送受信と待機」の振る舞いとプラットフォームの差を、`docs/api-cheatsheet.md` に `sendmmsg()` との対応と「登録中の送受信と接続」の行を追加しました。

Table: 作業 WBS 4.1 から 4.4 の Linux の検証結果

| 確認 | 結果 |
| --- | --- |
| `make -C framework/testfw` | 成功。`.warn` なし |
| `make -C app/cplat` (prod と test のビルド) | 成功。警告なし、`.warn` なし |
| `socketTest` | 成功 (69 件。追加した 7 件はすべて成功)。`.warn` なし |
| `exportTest` | 成功 (2 件) |
| `make doxy` (`app/cplat`) | 成功。Doxygen の警告なし |
| `check_functional_spec.py` | 成功 (要件 423 件、下流参照 42 件) |
| `text_style_jp.py --dry-run` (`net.md`、`api-cheatsheet.md`) | 指摘なし (`--in-place` も変更なし) |
| `text_style_jp.py --mode cpp --dry-run` (`socketTest.cc` の追加の行) | 指摘なし |
| `check_c_topdown.py` (`socket_watch_linux.c`、`socket_watch_windows.c`) | エラー 51 件から 53 件。増えたのは、新しい関数の `non-topdown-order` の 1 件ずつ |

- `check_c_topdown.py` の増分は、新しい関数を、ヘッダーの宣言順どおり `cplat_socket_watch_sendto()` の直後に置いたためです。2 つのファイルは、もともとファイル内補助関数が主たる関数より前にある構成で、トップ ダウンの並べ替えは、`261125-topdown-function-order` の最終仕様の第 2 段 (この案件の main への統合の後に `reorder_c_topdown.py` で `cplat` を書き換える) の対象です。並べ替えの道具は主たる関数の定義をファイル内補助関数の定義より前へ移すため、第 2 段の後は、新しい関数も `cplat_socket_watch_sendto()` の後に並ぶ見込みです (道具の実行は対象の外のため未確認)。先頭へ置けば件数は増えませんが、第 2 段の後も宣言順から外れたまま残るため、規範の最終形 (ヘッダーの宣言順) を優先しました。新しいファイル内補助関数は追加していません。
- カバレッジ: `socket_watch_linux.c` の新しい関数のうち、`sendmmsg()` が要求より大きい数を返す場合と、先頭のデータグラムが短い送信の場合の、2 つの防御の分岐 (`CPLAT_ERR_UNKNOWN`) は通っていません。最終仕様のテストの一覧の外です。
- 未確認: Windows のビルドとテスト (`socket_watch_windows.c` の初版、Windows のテスト 5 個) は、作業 WBS 4.11 で確かめます。

### 作業 WBS 4.5 から 4.10: porter の UDP のまとめ送り (2026-11-26、Linux (WSL))

- 基点: `framework/testfw` main `75cd774`、`app/cplat` main `8252c026`、`app/porter` main `1438394`。Git の操作はしていません。
- 4.5 (資源の名前): `POTR_TCP_BATCH_*` を `POTR_SEND_BATCH_*` へ、`tcp_batch_*` を `send_batch_*` へ改め、テストと mock の参照も改めました。`potr_service_open.c` の確保は、TCP に加えて、送信スレッドを起動する UDP の通信種別 (送信者、`unicast_bidir`、`unicast_bidir_n1`) でも行います。値と確保の数の計算は変えていません。
- 4.6 (ソケットの入出力): `potr_internal_udp_sendto_multi()` を追加しました。既存の `potr_internal_udp_sendto()` の送信と待機は、排他を持った状態で呼べる `udp_sendto_locked()` と、期限の判定と待機の `udp_wait_send_ready()` へ移し、両方から使います。最終仕様に書かれていない次の 2 点は、契約の表の趣旨に沿って決め、ヘッダーの Doxygen コメントに書きました。
    - 一括の送信の待機が期限の超過または解除で終わった場合は、残りのデータグラムすべてに同じ結果を格納し、1 個ずつの送信を試みません (データグラムごとに 100 [ms] 待つと、送信スレッドが最大 16 倍止まるため)。
    - 先頭のデータグラムが OS の送信失敗となった場合は、その結果を格納し、続くデータグラムを 1 個ずつ 1 回ずつ試みます (一部だけ送れた場合と同じ扱い)。
- 4.6 (mock): `mock_porter` に `potr_internal_udp_sendto_multi` を追加しました (既定は全要素 `POTR_OK`)。
- 4.7 (送信スレッド): `send_tcp_batch()` を `send_batch()` へ改め、UDP は新しい `udp_flush_packets()` で送ります。経路ごとの封緘と送信は `udp_send_path()` に集め、1 個の場合は `crypto_buf` と `potr_internal_udp_sendto()`、複数の場合は `send_batch_wire_buf` の区画と `potr_internal_udp_sendto_multi()` を使います。`flush_packed()` の UDP の処理と `flush_packed_peer()` は、それぞれ `udp_flush_packets()` と `udp_flush_packets_peer()` の 1 個の場合として呼ぶ形にし、1 個の送出の振る舞いは変えていません。N:1 は `send_packed_peer_mode()` で、同じピア宛てに続くエントリをまとめます (`pack_queued_entries()` にピアの絞り込みの引数を追加)。
- 4.7 (フロー制御): `potr_internal_flow_control_peer_state()` に `allowance` (NULL 可) を追加し、ファイル内の `peer_state_locked()` から受け取ります。ロックを取らない判定の記録 (`flow_peer_ready`) は残りの数を持たないため、`allowance` を求める呼び出しでは使わず、ロックの下で判定します。送信スレッドは、`send_batch_cap` が最小数以上の場合だけ `allowance` を求めます。送信 API (`potr_service_send.c`) は NULL を渡し、従来の速い経路を保ちます。
- 4.7 (原因の修正): `test/src/integration/porterCancelFailureTest` は cplat の `socket_watch_linux.c` を取り込んでコンパイルするため、`struct mmsghdr` が宣言されずにビルドが失敗しました。cplat と同じく `_GNU_SOURCE` を定義する設定を、このテストの `makepart.mk` に加えました (`see:` の URL 付き)。
- 4.8 (単体テスト): 最終仕様の一覧のテストをすべて追加しました。`potrSocketIoTest` に 5 個、`potrSendThreadTest` に UDP のまとめ送りの fixture (`potrSendThreadUdpBatchTest`) と 12 個、`potrFlowControlTest` に `peer_state_reports_allowance` を追加しました。既存のテストは、`potr_internal_flow_control_peer_state()` の引数の追加に合わせて改めたほかは、変更なしで成功しました (既存のテストは `send_batch_cap` が 0 のため、従来の 1 個の送出を通ります)。
- 4.9 (知見の反映): `potr_send_thread.c` の先頭のコメント、`potr_send_thread.h`、`potr_socket_io.h`、`potr_flow_control.h`、`potr_context.h` のコメント、`docs/architecture.md` (「送信スレッドのまとめ送り」の節の追加、送信スレッドの役割の表、フロー制御、UDP の送信の箇条)、`docs/protocol.md` (送出上限の判定の箇条) を更新しました。cplat の Windows と Linux の実装のコメントは、4.2 で反映済みであることを確認しました。最終仕様に従い、`docs/protocol.md` の「パッキングの動作」の節 (TCP のまとめ送りの説明) は変えていません。

Table: 作業 WBS 4.5 から 4.10 の Linux の検証結果

| 確認 | 結果 |
| --- | --- |
| `make -C app/porter` (prod と test のビルド) | 成功。`.warn` なし |
| `potrSocketIoTest` | 成功 (16 件。追加した 5 件はすべて成功) |
| `potrSendThreadTest` | 成功 (49 件。追加した 12 件はすべて成功) |
| `potrFlowControlTest` | 成功 (162 件。追加した 1 件は成功) |
| `make -C app/porter test` | 成功 (57 ディレクトリ、985 件、失敗 0)。所要 約 41 分 |
| `make -C app/cplat test` | 成功 (99 ディレクトリ、2,243 件、失敗 0)。所要 約 136 分 |
| `.warn` (`framework/testfw`、`app/cplat`、`app/porter`) | 内容のあるファイルなし |
| `make doxy` (`app/porter`) | 成功。Doxygen の警告なし |
| `text_style_jp.py --dry-run` (`architecture.md`、`protocol.md`) | 指摘なし (`--in-place` も変更なし) |
| `text_style_jp.py --mode c` / `--mode cpp --dry-run` (変更した C とテストの追加の行) | 追加した行に指摘なし。引数を足しただけの既存の行に、もとからの指摘が 9 件 (`potrFlowControlTest.cc` 5 件、`potr_service_send_test.cc` 4 件) |
| `check_c_topdown.py` | `potr_send_thread.c` はエラー 20 件から 18 件 (新しいファイル内補助関数は前方宣言を置き、`flush_packed_peer()` を削除し、`pack_queued_entries()` に前方宣言を置いたため)。`potr_socket_io.c` は 0 件のまま。`potr_flow_control.c` 74 件、`potr_service_open.c` 10 件、`api_context_cleanup.c` 5 件、`potr_service_send.c` 11 件は変化なし |

- 訂正 (2026-11-27): 上表の `potrFlowControlTest` は「162 件」と記録しましたが、正しくは 163 件です。当時の実行ファイルが、テストを足す前の内容のまま古くなっていたためです。2026-11-27 に作り直した実行ファイルで 163 件の登録を確かめ、追加した `peer_state_reports_allowance` の成功も確かめました (残課題一覧の BL-014)。
- カバレッジ: 新しいコードのうち、`potr_internal_udp_sendto_multi()` の未登録のソケット、一括の送信が要求より大きい数を返す場合の切り詰め、先頭のデータグラムの OS の送信失敗の分岐と、送信スレッドの防御の分岐 (一括の送信の引数の不正、N:1 の封緘の失敗、経路の選択とパケットの構築の失敗) は通っていません。最終仕様のテストの一覧の外です。
- 未確認: 性能の測定 (作業 WBS 4.12)。Windows のビルドとテスト (作業 WBS 4.11) は以下のとおりです。

### 作業 WBS 4.11: Windows のビルドとテスト (2026-11-27、Windows)

- 実施: Windows 側のセッション (主エージェントが仲介、依頼 16 と 17)。
- 同期した版: `framework/testfw` main `75cd774`、`app/cplat` main `8252c026`、`app/porter` main `72c8930`。
- ビルドとリンク: MSVC の警告および内容のある `.warn` はありません。MSVC 向けの修正は不要でした。`mock` の `/INCLUDE` によるリンクも成功しました。

Table: 作業 WBS 4.11 の Windows の検証結果

| 対象 | 結果 |
| --- | --- |
| 依頼 16 (`socketTest`) | 成功 (65 件) |
| 依頼 16 (`exportTest`) | 成功 (2 件) |
| 依頼 17 (`cplat` の全体) | 成功 (2,105 件、SKIPPED 1 件) |
| 依頼 17 (`potrSocketIoTest`) | 成功 (16 件) |
| 依頼 17 (`potrSendThreadTest`) | 成功 (49 件、`potrSendThreadUdpBatchTest` の 12 件と CNG の暗号化を含む) |
| 依頼 17 (`potrFlowControlTest`) | 成功 (163 件) |
| 依頼 17 (`porter` の全体) | 成功 (768 件、SKIPPED 1 件。変更のないディレクトリは省かれたため、上の 3 つは別に数えている) |

### 作業 WBS 4.12: Linux の性能の測定 (2026-11-26、Linux (WSL))

- 手順と結果の置き場所: セッションの一時領域の `scratchpad/bench/` (`run-bench.sh`、`run-bench.log`、`results/`、集計スクリプト `aggregate_bench.py`)。リポジトリには入れていません。
- 集計の定義: 各 CSV ファイルの値は 5 測定 (`--runs 5`) の中央値とし、表の値は 3 回 (`r1` から `r3`) の中央値、幅は 3 回の最小から最大とします。処理件数の指標は `throughput_msg_s` [msg/s] です。
- 判定の定義: 「測定の変動の幅を超えて下がる」は、変更の前 (`base`) と変更の後 (`target`) の 3 回の値の幅が重ならず、`target` が低い場合 (`target` の最大 < `base` の最小) とします。
- 測定時の補足: 3 回目の `base` の `p7` は、1 回目の実行が `stage=connect_or_register failed` で失敗したため、同じ回のほかの実行の後に、`base` の `p7` だけを単独で測り直しています。

Table: 作業 WBS 4.12 の Linux の性能の測定結果 (処理件数 `throughput_msg_s` [msg/s])

| 条件 | `base` 中央値 (3 回の幅) | `target` 中央値 (3 回の幅) | 変化率 | `tcp` (`p5`) 中央値 (3 回の幅) | `target` ÷ `tcp` (参考の値) | 判定 |
| --- | --- | --- | --- | --- | --- | --- |
| `p1` 64 byte `normal` | 133,909.2 (131,232.8 から 135,580.2) | 137,225.0 (137,207.1 から 137,506.7) | +2.5 [%] | 128,478.9 (127,891.1 から 131,404.9) | 1.07 (1.03) | 適合 (受入条件 2: 変動の幅を超えて向上) |
| `p1` 64 byte `blocking` | 13,618.6 (13,417.7 から 13,635.2) | 13,556.2 (13,539.6 から 13,586.1) | -0.5 [%] | 12,737.0 (12,702.0 から 12,906.8) | 1.06 (1.06) | 適合 (受入条件 2: 変動の幅の範囲内) |
| `p1` 1,000 byte `normal` | 137,034.8 (128,374.4 から 151,950.7) | 147,248.5 (143,600.6 から 148,851.1) | +7.5 [%] | 123,578.6 (118,098.2 から 124,858.9) | 1.19 (1.20) | 適合 (受入条件 2: 変動の幅の範囲内) |
| `p1` 1,000 byte `blocking` | 13,530.3 (13,289.8 から 13,542.8) | 13,487.3 (13,443.3 から 13,524.7) | -0.3 [%] | 12,542.2 (12,470.4 から 12,564.8) | 1.08 (1.09) | 適合 (受入条件 2: 変動の幅の範囲内) |
| `p1` 5,000 byte `normal` | 43,944.0 (42,089.8 から 44,231.6) | 44,728.4 (44,300.1 から 46,369.0) | +1.8 [%] | 57,020.8 (55,102.2 から 57,202.8) | 0.78 (0.62) | 適合 (受入条件 2: 変動の幅を超えて向上) |
| `p1` 5,000 byte `blocking` | 5,781.5 (5,780.7 から 5,837.0) | 11,648.4 (11,648.3 から 11,681.1) | +101.5 [%] | 12,002.8 (11,950.2 から 12,207.2) | 0.97 (0.47) | 適合 (受入条件 1: 0.5 以上。変動の幅を超えて向上) |
| `p2` (N 対 1) | 95,257.1 (93,837.8 から 98,307.1) | 93,602.4 (92,138.6 から 96,759.6) | -1.7 [%] | - | - | 適合 (受入条件 3: 変動の幅の範囲内) |
| `p6` (`multicast`、受信者 3) | 93,110.5 (92,765.6 から 93,672.5) | 94,725.6 (94,280.8 から 95,450.2) | +1.7 [%] | - | - | 適合 (受入条件 3: 変動の幅を超えて向上) |
| `p7` (N 対 1、停止クライアント 1) | 8,554.4 (8,429.1 から 8,597.0) | 8,521.8 (8,471.9 から 8,551.2) | -0.4 [%] | - | - | 適合 (受入条件 3: 変動の幅の範囲内) |

- 参考: 今回の測定における `base` ÷ `tcp` は、64 byte が 1.04 (`normal`) と 1.07 (`blocking`)、1,000 byte が 1.11 と 1.08、5,000 byte が 0.77 と 0.48 です。
- 健全な受信者とクライアントの切断の回数 (`healthy_disconnect_count`): `p6`、`p7` ともに、`base`、`target` の 3 回の中央値、幅 (および全 15 測定) がすべて 0 回であり、変更の前より増えていません (`healthy_received_min` も全 15 測定で 10,000 件、`p7` の `stalled_disconnect_count` は全 15 測定で 1 件、`p2` の `dropped_count` は全 15 測定で 0 件)。
- 受入条件の判定 (Linux): 受入条件 1 (`p1` 5,000 byte `blocking` の `target` ÷ `tcp` が 0.97 で 0.5 以上)、受入条件 2 (`p1` のほかの 5 条件で変動の幅を超えて下がらない)、受入条件 3 (`p2`、`p6`、`p7` で変動の幅を超えて下がらず、`p6` と `p7` の健全な受信者・クライアントの切断回数が増えない) は、すべて適合です。受入条件 4 (Windows) は未測定のため未判定です。
