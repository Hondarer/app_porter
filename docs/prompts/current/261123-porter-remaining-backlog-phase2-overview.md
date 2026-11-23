<!--
生成元: agree-before-implement スキル
文書種別: 全体管理文書 (overview)
最終更新日時: 2026-11-23 12:55:00 JST
最終更新エージェント: claude code / セッション ID: session_01KJc53QSNM6huJLdCU7M29H
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

検討用 Markdown の版: 1

状態: 検討中 (版 1 の回答待ち)

## 作業 WBS と進捗管理

この案件の作業 WBS です。項目 4 の作業対象は、[Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲) の回答で範囲が決まった時点で、具体的なファイルへ書き直します。  
決定事項は、Q を解決済みにした時点、またはユーザーが決定を明示した時点で `261123-porter-remaining-backlog-phase2-lightadr.md` へ反映します。見送りの決定や範囲外の気付きは、どの段階でも生じた時点で `261123-porter-remaining-backlog-phase2-backlog.md` に追記します。対策元の残課題一覧 (`261123-porter-remaining-backlog-backlog.md`) の状態と経緯も、進行の各段階で更新します。

凡例: ⏸️ 未着手、🔄 実施中、🚫 ブロック中、⚠️ 要注意、✅ 完了。現在取り組んでいる項目の末尾に `← **CURRENT**` を付けます。

- 🚫 1. 検討用 Markdown のための情報収集と質問の実施
    - ✅ 1.1. 前提条件と要求事項の調査 (対策元の残課題一覧の 10 項目について、Linux (WSL) でできる範囲の確認。codex (gpt-6.2-sol、effort low) の読み取りの調査と、主エージェントによる確認。対策元の残課題一覧の状態を「検討中」へ更新)
    - ✅ 1.2. 課題・論点の抽出と確認項目 (Q-001 から Q-007) の作成
    - 🚫 1.3. 版 1 の回答の受付と記録 (ユーザーの回答待ち。[Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲) から [Q-007](261123-porter-remaining-backlog-phase2-discussion.md#q-007-サニタイザー-bl-009-の扱い)) ← **CURRENT**
    - ⏸️ 1.4. 回答の検証、簡易 ADR への反映、対策元の残課題一覧への方針と見送りの反映、この案件の残課題一覧の作成、WBS の見直し
- ⏸️ 2. 前提合意と残懸念の解消 (第 1 ゲート: 最終仕様作成への着手)
    - ⏸️ 2.1. 全論点に対する未解決事項および質問の解消確認
    - ⏸️ 2.2. 方針・前提条件の合意確定 (簡易 ADR の決定事項と、解決済みの Q の回答またはユーザーの指示との一致の確認)
- ⏸️ 3. 最終決定事項 (最終仕様) の作成と承認の受付
    - ⏸️ 3.1. `261123-porter-remaining-backlog-phase2-design.md` の作成 (対策元の残課題一覧へ確定した方針を反映)
    - ⏸️ 3.2. 最終仕様における受入条件と整合性の確認
    - ⏸️ 3.3. 対話セッションにおける最終仕様の承認の受付 (第 2 ゲート)
- ⏸️ 4. 承認された最終仕様に従った作業の実施
    - ⏸️ 4.1. 対象成果物の作成・更新 (Q-001 の回答の後に、対象のファイルごとの項目へ分ける。BL-010 を選んだ場合の見込みは、`framework/testfw` の `sendmmsg` の mock、`app/cplat` の net カテゴリの関数と mock と単体テストと `docs`、`app/porter` の `prod/libsrc/porter/thread/potr_send_thread.c` と単体テスト)
    - ⏸️ 4.2. 正本ドキュメントおよびコード コメントへの永続的知見の反映
    - ⏸️ 4.3. 局所テスト、性能の測定、機械的検証 (`text_style_jp.py`)、`.warn` の確認
    - ⏸️ 4.4. 受入条件への適合確認
    - ⏸️ 4.5. 簡易 ADR の最終確認
    - ⏸️ 4.6. 残課題一覧 (`261123-porter-remaining-backlog-phase2-backlog.md`) の再構築と整理 (対策元の残課題一覧の最終状態と転記先の確定)
- ⏸️ 5. 成果物の承認の受付 (第 3 ゲート)
    - ⏸️ 5.1. 成果物・検証結果・残課題の提示
    - ⏸️ 5.2. 対話セッションにおける成果物の承認の受付

## 対象と参照資料

対象成果物: 未確定 ([Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲) の回答で決まります)

今回の対象範囲: 対策元の残課題一覧の BL-001 から BL-010 のうち、Q-001 で選ばれた項目。選ばれなかった項目は、この案件の残課題一覧へ転記します。

書き込み可能な検討文書: この文書、検討用 Markdown (`261123-porter-remaining-backlog-phase2-discussion.md`)、簡易 ADR (`261123-porter-remaining-backlog-phase2-lightadr.md`)

参照資料と版:

- 対策元の残課題一覧 `261123-porter-remaining-backlog-backlog.md` (2026-11-23 12:45 の版)
- 前の案件の全体管理文書 `261123-porter-remaining-backlog-overview.md` の「反映と検証の記録」(測定の値)
- `app/porter` main `99d4da6`、`app/cplat` main `988b8720`、`framework/makefw` main `d8177db`、親リポジトリ main `6e5010eb`
- codex (gpt-6.2-sol、effort low、読み取り専用) の検討の結果 (セッションの一時領域。リポジトリには入れていません)

ユーザーの指示 (2026-11-23): 「`261123-porter-remaining-backlog-backlog.md` の次の対処計画を進行したい。」  
計画の検討には、前の案件の指示 (計画立案の検討に codex の GPT-6.2-Sol low を使う) を、同じ種類の作業として引き続き適用しました。作業の実施の段階のサブエージェントの使い方は、最終仕様の承認の時点で確認します。

## 観点別の確認

| 観点 | 状態 | 根拠・関連 Q |
| --- | --- | --- |
| 目的・利用者 | 未解決 | [Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲) |
| 範囲・境界 | 未解決 | [Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲)、[Q-003](261123-porter-remaining-backlog-phase2-discussion.md#q-003-bl-010-を適用する通信種別と経路の範囲)、[Q-007](261123-porter-remaining-backlog-phase2-discussion.md#q-007-サニタイザー-bl-009-の扱い) |
| 入力・根拠 | 確認済み | 対策元の残課題一覧と、前の案件の測定の記録。現行のソースは、主エージェントと codex が確認 |
| 整合性・依存関係 | 未解決 | [Q-004](261123-porter-remaining-backlog-phase2-discussion.md#q-004-bl-010-の-windows-での実装の方式)、[Q-006](261123-porter-remaining-backlog-phase2-discussion.md#q-006-一括の送信で一部だけ送れた場合の残りの扱い) (Linux と Windows の双方で動作する構成、現在の再送の契約との整合) |
| 実現性・例外 | 未解決 | [Q-002](261123-porter-remaining-backlog-phase2-discussion.md#q-002-windows-での確認の方法)、[Q-006](261123-porter-remaining-backlog-phase2-discussion.md#q-006-一括の送信で一部だけ送れた場合の残りの扱い) |
| 受入条件・検証 | 未解決 | [Q-002](261123-porter-remaining-backlog-phase2-discussion.md#q-002-windows-での確認の方法)、[Q-005](261123-porter-remaining-backlog-phase2-discussion.md#q-005-bl-010-の性能の受入の基準) |

Table: 観点別の確認

## 質問一覧

各 Q の本文は検討用 Markdown にあります。

| Q-ID | 論点 | 状態 | 依存・関連 | 反映先の決定 |
| --- | --- | --- | --- | --- |
| [Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲) | この案件で対策する残課題の範囲 | 未回答 | なし | 未反映 |
| [Q-002](261123-porter-remaining-backlog-phase2-discussion.md#q-002-windows-での確認の方法) | Windows での確認の方法 | 未回答 | Q-001 | 未反映 |
| [Q-003](261123-porter-remaining-backlog-phase2-discussion.md#q-003-bl-010-を適用する通信種別と経路の範囲) | BL-010 を適用する通信種別と経路の範囲 | 未回答 | Q-001、Q-005 | 未反映 |
| [Q-004](261123-porter-remaining-backlog-phase2-discussion.md#q-004-bl-010-の-windows-での実装の方式) | BL-010 の Windows での実装の方式 | 未回答 | Q-001、Q-002 | 未反映 |
| [Q-005](261123-porter-remaining-backlog-phase2-discussion.md#q-005-bl-010-の性能の受入の基準) | BL-010 の性能の受入の基準 | 未回答 | Q-003 | 未反映 |
| [Q-006](261123-porter-remaining-backlog-phase2-discussion.md#q-006-一括の送信で一部だけ送れた場合の残りの扱い) | 一括の送信で、一部だけ送れた場合の残りの扱い | 未回答 | Q-003 | 未反映 |
| [Q-007](261123-porter-remaining-backlog-phase2-discussion.md#q-007-サニタイザー-bl-009-の扱い) | サニタイザー (BL-009) の扱い | 未回答 | なし | 未反映 |

Table: 質問一覧

## 対象成果物と合意版

最終決定事項 (最終仕様) の設計書: `261123-porter-remaining-backlog-phase2-design.md`

検討用 Markdown の合意版 (第 1 ゲート): 未確定

最終仕様の合意版 (第 2 ゲート): 未承認

更新予定の成果物: 未確定

受入条件: 未確定

## 経緯

| 日時 (JST) | 出来事 | 内容・参照先 |
| --- | --- | --- |
| 2026-11-23 12:36 | 案件の開始 | ユーザーの指示を受けて開始。対策元の残課題一覧の 10 項目を「検討中」へ更新 |
| 2026-11-23 12:55 | 版 1 の提示 | 全体管理文書と検討用 Markdown (Q-001 から Q-007) を作成 |

Table: 経緯

## 反映と検証の記録

未着手。作業後の対応箇所、検証結果、未完了項目を記録します。
