<!--
生成元: agree-before-implement スキル
文書種別: 全体管理文書 (overview)
最終更新日時: 2026-11-21 13:15:00 JST
最終更新エージェント: claude code / セッション ID: session_01KJc53QxuPSr8Mxe6rktvre
稼働 OS: Linux (WSL)
上流文書: `261119-porter-flow-control-backlog.md` (対策元の残課題一覧)。削除されている場合があります。
下流文書: `261121-porter-flow-control-refine-discussion.md` (検討用 Markdown)、`261121-porter-flow-control-refine-lightadr.md` (簡易 ADR)、`261121-porter-flow-control-refine-design.md` (最終仕様)、`261121-porter-flow-control-refine-backlog.md` (残課題一覧)。未作成の場合や、削除されている場合があります。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。この文書と、ユーザーが編集した最新の検討用 Markdown を読み直し、作業 WBS の `← **CURRENT**` の項目から再開します。最終更新エージェントのセッション情報や稼働 OS・最終更新日時が参照可能であれば再開時のコンテキストや環境差分の把握に利用し、別エージェントや別環境・別 OS からの引き継ぎ、担当変更によりセッションが見つからない場合も異常とせず、文書の記載から継続してください。`← **CURRENT**` の項目は処理が中止された項目である場合があり、どこまで進んでいたかは文書だけでは分かりません。対象成果物の内容を確認して実施済みの範囲を特定してから、作業を再開してください。`← **CURRENT**` がなく、作業 WBS がすべて完了 (✅) であれば、この案件の合意フローは完了しています。
-->

# porter のフロー制御の残件対策 全体管理

ファイル名: `261121-porter-flow-control-refine-overview.md`

この文書は、案件の識別、作業 WBS、対象範囲、確認状況、経緯を管理します。質問と回答の本文は検討用 Markdown (`261121-porter-flow-control-refine-discussion.md`) に、決定事項は簡易 ADR (`261121-porter-flow-control-refine-lightadr.md`) に記載し、この文書には重複して記載しません。

## 案件の識別

検討 ID: 261121-porter-flow-control-refine

検討用 Markdown の版: 2

状態: 検討中

## 作業 WBS と進捗管理

この案件の作業 WBS です。項目 4 の作業対象は、Q&A の回答に応じて見直します。  
決定事項は、Q を解決済みにした時点、またはユーザーが決定を明示した時点で `261121-porter-flow-control-refine-lightadr.md` へ反映します。見送りの決定や範囲外の気付きは、どの段階でも生じた時点で `261121-porter-flow-control-refine-backlog.md` に追記します。対策元の `261119-porter-flow-control-backlog.md` の状態と経緯も、進行の各段階で更新します。

凡例: ⏸️ 未着手、🔄 実施中、🚫 ブロック中、⚠️ 要注意、✅ 完了。現在取り組んでいる項目の末尾に `← **CURRENT**` を付けます。

- 🔄 1. 検討用 Markdown のための情報収集と質問の実施
    - ✅ 1.1. 前提条件と要求事項の調査 (対策元の残課題一覧、前の案件の最終仕様、`docs/config.md`、`docs/protocol.md`、`potr_flow_control.c`。対策元の残課題一覧の状態を「検討中」へ更新)
    - ✅ 1.2. ユーザーが示した 2 つの方針の簡易 ADR への記録 (LADR-001、LADR-002)
    - ✅ 1.3. 課題・論点の抽出と確認項目 ([Q-001](261121-porter-flow-control-refine-discussion.md#q-001-対象とする残課題一覧の範囲) から [Q-013](261121-porter-flow-control-refine-discussion.md#q-013-多数の受信者での確認の方法)) の作成
    - ✅ 1.3a. ユーザーが追加で示した方針 3 (あふれの回避) の簡易 ADR への記録 (LADR-003) と、確認項目 ([Q-014](261121-porter-flow-control-refine-discussion.md#q-014-あふれを防ぐ対象の解釈) から [Q-021](261121-porter-flow-control-refine-discussion.md#q-021-圧縮した-data-による容量の超過)) の追記
    - 🚫 1.4. 版 2 の回答の受付と記録 (ユーザーの回答待ち。Q-001 から Q-021) ← **CURRENT**
    - ⏸️ 1.5. 回答の検証、簡易 ADR への反映、対策元の残課題一覧への方針の反映、WBS の見直し
    - ⏸️ 1.6. 回答に応じた追加の確認項目 (猶予時間の既定値、設定項目の名前と意味、プロトコル バージョン、要求する受信バッファーの大きさ、保持範囲の既定値、BL-009 と BL-011 の方式) の作成と回答の受付
- ⏸️ 2. 前提合意と残懸念の解消 (第 1 ゲート: 最終仕様作成への着手)
    - ⏸️ 2.1. 全論点に対する未解決事項および質問の解消確認
    - ⏸️ 2.2. 方針・前提条件の合意確定 (簡易 ADR の決定事項と、解決済みの Q の回答またはユーザーの指示との一致の確認)
- ⏸️ 3. 最終決定事項 (最終仕様) の作成と承認の受付
    - ⏸️ 3.1. `261121-porter-flow-control-refine-design.md` の作成 (対策元の残課題一覧へ確定した方針を反映)
    - ⏸️ 3.2. 最終仕様における受入条件と整合性の確認
    - ⏸️ 3.3. 対話セッションにおける最終仕様の承認の受付 (第 2 ゲート)
- ⏸️ 4. 承認された最終仕様に従った作業の実施 (項目は回答に応じて見直す)
    - ⏸️ 4.1. 変更前の測定 (`porter-bench` の `unicast` と `tcp` の比較、遅い受信者が 1 台いる `multicast` の測定)
    - ⏸️ 4.2. 1 対 N の判定の見直し (BL-006、BL-009、BL-011。`prod/libsrc/porter/protocol/receiver_report.c`、`potr_flow_control.c`)
    - ⏸️ 4.3. N 対 1 サーバーの見直し (BL-007。幅の分け方、全ピア宛の送信の扱い)
    - ⏸️ 4.4. 送出上限の通知の見直し (BL-012、逆方向のパケットへの相乗り、OS の受信バッファーの拡大の要求、保持範囲の既定値)
    - ⏸️ 4.5. 単体テストの追加 (BL-010 の 4 経路と、今回の変更の分。`test/src/libporter/`)
    - ⏸️ 4.6. 機能仕様の見直し (BL-013 の `POTR-CONFIG-FUNC-010` と、今回の変更の分。`docs/functional-spec/`)
    - ⏸️ 4.7. 詳細設計の文書の見直し (`docs/protocol.md`、`docs/config.md`、`docs/api.md`) とコード コメントへの知見の反映
    - ⏸️ 4.8. 局所テスト (`make`、`make test`、`check_functional_spec.py`、`text_style_jp.py`) と `.warn` の確認
    - ⏸️ 4.9. Linux での結合の確認と性能の測定 (`porter-test`、`porter-bench`、多数の受信者)
    - ⏸️ 4.10. Windows での確認 (BL-001)
    - ⏸️ 4.11. コミット、プッシュ、CI の確認
    - ⏸️ 4.12. 受入条件への適合確認
    - ⏸️ 4.13. 簡易 ADR の最終確認
    - ⏸️ 4.14. 残課題一覧 (`261121-porter-flow-control-refine-backlog.md`) の再構築と整理、対策元の残課題一覧の最終状態の確定
- ⏸️ 5. 成果物の承認の受付 (第 3 ゲート)
    - ⏸️ 5.1. 成果物・検証結果・残課題の提示
    - ⏸️ 5.2. 対話セッションにおける成果物の承認の受付

## 対象と参照資料

対象成果物: `app/porter` のフロー制御の実装、単体テスト、機能仕様、詳細設計の文書 (範囲は [Q-001](261121-porter-flow-control-refine-discussion.md#q-001-対象とする残課題一覧の範囲) の回答で確定)

今回の対象範囲: 対策元の残課題一覧の BL-001、BL-006 から BL-013 と、前の案件で現状どおりとした仕様の見直し。ユーザーの指示は「現状通りとしたものも含めて、より良い総合仕様を目指す」です。

Q を設けずに、対策元の残課題一覧の推奨どおりに行う項目:

- BL-010 (単体テストが未追加の 4 経路): テストを追加します。
- BL-013 (`POTR-CONFIG-FUNC-010` の文言): 今回の変更後の設定に合わせて、文言を改めます。

書き込み可能な検討文書: この文書、検討用 Markdown (`261121-porter-flow-control-refine-discussion.md`)、簡易 ADR (`261121-porter-flow-control-refine-lightadr.md`)、残課題一覧 (この案件の分と、対策元)

参照資料と版:

- `261119-porter-flow-control-backlog.md` (2026-11-21 12:45 の整理済みの版)
- `261119-porter-flow-control-design.md` (版 7)
- `app/porter` `main` `75d903a`、親リポジトリ `main` `ae4ac74e`
- `docs/config.md`、`docs/protocol.md`、`docs/api.md` (同じコミット)

## 観点別の確認

| 観点 | 状態 | 根拠・関連 Q |
| --- | --- | --- |
| 目的・利用者 | 未解決 | ユーザーの指示 (3 つの方針)。LADR-001 から LADR-003。方針 1 と方針 3 の関係は [Q-014](261121-porter-flow-control-refine-discussion.md#q-014-あふれを防ぐ対象の解釈) |
| 範囲・境界 | 未解決 | [Q-001](261121-porter-flow-control-refine-discussion.md#q-001-対象とする残課題一覧の範囲) |
| 入力・根拠 | 未解決 | Windows の値と、`unicast` と `tcp` の比較が未測定。[Q-011](261121-porter-flow-control-refine-discussion.md#q-011-スループットの受入の基準)、[Q-012](261121-porter-flow-control-refine-discussion.md#q-012-windows-での確認の進め方) |
| 整合性・依存関係 | 未解決 | 方針 1 と現在の仕様の相違。[Q-002](261121-porter-flow-control-refine-discussion.md#q-002-1-対-n-で救済しない受信者を見分ける方法) から [Q-005](261121-porter-flow-control-refine-discussion.md#q-005-n-対-1-サーバーの全ピア宛の送信で遅いピアがある場合の扱い) |
| 実現性・例外 | 未解決 | [Q-006](261121-porter-flow-control-refine-discussion.md#q-006-n-対-1-サーバーの受信バッファーの幅の分け方) から [Q-010](261121-porter-flow-control-refine-discussion.md#q-010-unicast-の受信報告に送出上限を載せるか)、[Q-015](261121-porter-flow-control-refine-discussion.md#q-015-最初の送出上限が届く前の送出) から [Q-021](261121-porter-flow-control-refine-discussion.md#q-021-圧縮した-data-による容量の超過) |
| 受入条件・検証 | 未解決 | [Q-011](261121-porter-flow-control-refine-discussion.md#q-011-スループットの受入の基準) から [Q-013](261121-porter-flow-control-refine-discussion.md#q-013-多数の受信者での確認の方法) |

Table: 観点別の確認

## 質問一覧

各 Q の本文は検討用 Markdown にあります。

| Q-ID | 論点 | 状態 | 依存・関連 | 反映先の決定 |
| --- | --- | --- | --- | --- |
| [Q-001](261121-porter-flow-control-refine-discussion.md#q-001-対象とする残課題一覧の範囲) | 対象とする残課題一覧の範囲 | 未回答 | なし | 未反映 |
| [Q-002](261121-porter-flow-control-refine-discussion.md#q-002-1-対-n-で救済しない受信者を見分ける方法) | 1 対 N で救済しない受信者を見分ける方法 | 未回答 | BL-006、BL-009、BL-011 | 未反映 |
| [Q-003](261121-porter-flow-control-refine-discussion.md#q-003-救済しない受信者の数の上限の既定値) | 救済しない受信者の数の上限の既定値 | 未回答 | Q-002、BL-008 | 未反映 |
| [Q-004](261121-porter-flow-control-refine-discussion.md#q-004-救済しないと判定した受信者の扱い) | 救済しないと判定した受信者の扱い | 未回答 | Q-002 | 未反映 |
| [Q-005](261121-porter-flow-control-refine-discussion.md#q-005-n-対-1-サーバーの全ピア宛の送信で遅いピアがある場合の扱い) | N 対 1 サーバーの全ピア宛の送信で遅いピアがある場合の扱い | 未回答 | Q-002 | 未反映 |
| [Q-006](261121-porter-flow-control-refine-discussion.md#q-006-n-対-1-サーバーの受信バッファーの幅の分け方) | N 対 1 サーバーの受信バッファーの幅の分け方 | 未回答 | BL-007 | 未反映 |
| [Q-007](261121-porter-flow-control-refine-discussion.md#q-007-os-の受信バッファーの拡大の要求) | OS の受信バッファーの拡大の要求 | 未回答 | BL-001、BL-007 | 未反映 |
| [Q-008](261121-porter-flow-control-refine-discussion.md#q-008-逆方向のパケットへの送出上限の相乗り) | 逆方向のパケットへの送出上限の相乗り | 未回答 | BL-007 | 未反映 |
| [Q-009](261121-porter-flow-control-refine-discussion.md#q-009-保持範囲-window_size-の既定値) | 保持範囲 (window_size) の既定値 | 未回答 | BL-006 | 未反映 |
| [Q-010](261121-porter-flow-control-refine-discussion.md#q-010-unicast-の受信報告に送出上限を載せるか) | unicast の受信報告に送出上限を載せるか | 未回答 | BL-012 | 未反映 |
| [Q-011](261121-porter-flow-control-refine-discussion.md#q-011-スループットの受入の基準) | スループットの受入の基準 | 未回答 | BL-007 | 未反映 |
| [Q-012](261121-porter-flow-control-refine-discussion.md#q-012-windows-での確認の進め方) | Windows での確認の進め方 | 未回答 | BL-001 | 未反映 |
| [Q-013](261121-porter-flow-control-refine-discussion.md#q-013-多数の受信者での確認の方法) | 多数の受信者での確認の方法 | 未回答 | BL-008 | 未反映 |
| [Q-014](261121-porter-flow-control-refine-discussion.md#q-014-あふれを防ぐ対象の解釈) | あふれを防ぐ対象の解釈 | 未回答 | Q-002、Q-005 | 未反映 |
| [Q-015](261121-porter-flow-control-refine-discussion.md#q-015-最初の送出上限が届く前の送出) | 最初の送出上限が届く前の送出 | 未回答 | Q-014 | 未反映 |
| [Q-016](261121-porter-flow-control-refine-discussion.md#q-016-送出上限の通知が続けて失われた場合の回復) | 送出上限の通知が続けて失われた場合の回復 | 未回答 | Q-010、BL-012 | 未反映 |
| [Q-017](261121-porter-flow-control-refine-discussion.md#q-017-監視対象の外の受信者が遅れた場合の合図) | 監視対象の外の受信者が遅れた場合の合図 | 未回答 | Q-002、BL-009、BL-011 | 未反映 |
| [Q-018](261121-porter-flow-control-refine-discussion.md#q-018-受信報告を送らない構成の受信者) | 受信報告を送らない構成の受信者 | 未回答 | Q-014 | 未反映 |
| [Q-019](261121-porter-flow-control-refine-discussion.md#q-019-n-対-1-サーバーで送信中のピアが多い場合) | N 対 1 サーバーで送信中のピアが多い場合 | 未回答 | Q-006、Q-007 | 未反映 |
| [Q-020](261121-porter-flow-control-refine-discussion.md#q-020-data-以外のパケットの分の余白) | DATA 以外のパケットの分の余白 | 未回答 | Q-012 | 未反映 |
| [Q-021](261121-porter-flow-control-refine-discussion.md#q-021-圧縮した-data-による容量の超過) | 圧縮した DATA による容量の超過 | 未回答 | Q-014 | 未反映 |

Table: 質問一覧

## 対象成果物と合意版

最終決定事項 (最終仕様) の設計書: `261121-porter-flow-control-refine-design.md` (未作成)

検討用 Markdown の合意版 (第 1 ゲート): 未確定

最終仕様の合意版 (第 2 ゲート): 未承認

更新予定の成果物: 未確定

受入条件: 未確定

## 経緯

| 日時 (JST) | 出来事 | 内容・参照先 |
| --- | --- | --- |
| 2026-11-21 13:10 | 案件の開始 | ユーザーが、porter の残件対策の推進と、2 つの方針を指示。全体管理文書、検討用 Markdown (Q-001 から Q-013)、簡易 ADR (LADR-001、LADR-002) を作成。対策元の残課題一覧の対象の項目を「検討中」に更新 |
| 2026-11-21 13:15 | 方針の追加 | ユーザーが、あふれの回避は高信頼に属し、現在対策できていない場合を救済するアルゴリズムが必要と指示。LADR-003 に記録し、Q-014 から Q-021 を追記 (検討用 Markdown の版 2) |
| 2026-11-21 13:25 | Git 操作の許可 | ユーザーが、前の案件の Git 操作の事前許可を、この案件にも適用すると指示。検討文書をコミットしてプッシュ |

Table: 経緯

## 反映と検証の記録

未着手。作業後の対応箇所、検証結果、未完了項目を記録します。
