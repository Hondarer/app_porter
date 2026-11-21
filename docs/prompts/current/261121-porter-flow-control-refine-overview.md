<!--
生成元: agree-before-implement スキル
文書種別: 全体管理文書 (overview)
最終更新日時: 2026-11-21 19:55:00 JST
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

検討用 Markdown の版: 5

状態: 作業中 (最終仕様の版 1 を 2026-11-21 に承認。検討用 Markdown の版 5)

## 作業 WBS と進捗管理

この案件の作業 WBS です。項目 4 の作業対象は、Q&A の回答に応じて見直します。  
決定事項は、Q を解決済みにした時点、またはユーザーが決定を明示した時点で `261121-porter-flow-control-refine-lightadr.md` へ反映します。見送りの決定や範囲外の気付きは、どの段階でも生じた時点で `261121-porter-flow-control-refine-backlog.md` に追記します。対策元の `261119-porter-flow-control-backlog.md` の状態と経緯も、進行の各段階で更新します。

凡例: ⏸️ 未着手、🔄 実施中、🚫 ブロック中、⚠️ 要注意、✅ 完了。現在取り組んでいる項目の末尾に `← **CURRENT**` を付けます。

- ✅ 1. 検討用 Markdown のための情報収集と質問の実施
    - ✅ 1.1. 前提条件と要求事項の調査 (対策元の残課題一覧、前の案件の最終仕様、`docs/config.md`、`docs/protocol.md`、`potr_flow_control.c`。対策元の残課題一覧の状態を「検討中」へ更新)
    - ✅ 1.2. ユーザーが示した 2 つの方針の簡易 ADR への記録 (LADR-001、LADR-002)
    - ✅ 1.3. 課題・論点の抽出と確認項目 ([Q-001](261121-porter-flow-control-refine-discussion.md#q-001-対象とする残課題一覧の範囲) から [Q-013](261121-porter-flow-control-refine-discussion.md#q-013-多数の受信者での確認の方法)) の作成
    - ✅ 1.3a. ユーザーが追加で示した方針 3 (あふれの回避) の簡易 ADR への記録 (LADR-003) と、確認項目 ([Q-014](261121-porter-flow-control-refine-discussion.md#q-014-あふれを防ぐ対象の解釈) から [Q-021](261121-porter-flow-control-refine-discussion.md#q-021-圧縮した-data-による容量の超過)) の追記
    - ✅ 1.4. 版 2 の回答の受付と記録 (Q-001 から Q-021。すべて解決済み)
    - ✅ 1.5. 回答の検証、簡易 ADR への反映、対策元の残課題一覧への方針の反映、WBS の見直し
    - ✅ 1.6. 方針どうしの関係を確かめる測定 (`unicast` と `tcp` の比較) と、矛盾がある点の追加の確認項目 ([Q-022](261121-porter-flow-control-refine-discussion.md#q-022-unicast-が-tcp-を下回っていることへの対処の範囲)、[Q-023](261121-porter-flow-control-refine-discussion.md#q-023-1-対-n-で送出上限を知らせる受信者の数の決め方)) の作成。`multicast` の受信者数と処理件数の測定は、測定の手段の作成が必要なため、4.1 で行う
    - ✅ 1.7. 版 4 の回答の受付と記録 (Q-022、Q-023。どちらも解決済み)、簡易 ADR (LADR-011、LADR-014) への反映、この案件の残課題一覧 (BL-001) の作成
- ✅ 2. 前提合意と残懸念の解消 (第 1 ゲート: 最終仕様作成への着手)
    - ✅ 2.1. 全論点に対する未解決事項および質問の解消確認
    - ✅ 2.2. 方針・前提条件の合意確定 (簡易 ADR の決定事項と、解決済みの Q の回答またはユーザーの指示との一致の確認)
- ✅ 3. 最終決定事項 (最終仕様) の作成と承認の受付
    - ✅ 3.1. `261121-porter-flow-control-refine-design.md` (版 1) の作成 (対策元の残課題一覧へ確定した方針を反映)
    - ✅ 3.2. 最終仕様における受入条件と整合性の確認
    - ✅ 3.3. 対話セッションにおける最終仕様の承認の受付 (第 2 ゲート。版 1 を承認)
- 🔄 4. 承認された最終仕様に従った作業の実施 (作業対象は、最終仕様の「コードの変更」「単体テスト」「機能仕様の変更」「詳細設計の文書の変更」に対応)
    - ✅ 4.0. 別の案件 (名前付きの整数定数の規範への適合) の `app/porter` の変更の取り込み (2026-11-21 19:54 の時点で `origin/main` に入っていないため、取り込みなし。以後、push の連絡を受けた時点で取り込む)
    - 🔄 4.1. 測定の手段の追加 (`porter-bench` の `multicast` の条件) と、変更前の測定 (`multicast` の監視する数と処理件数、遅い受信者がいる場合、`unicast` ÷ `tcp` の比) ← **CURRENT**
    - ⏸️ 4.2. 臨時の受信報告の、読み出しの遅れの検出の試作と精度の確認 (4.5 の最初に行う)
    - ⏸️ 4.3. 共通の動作 (プロトコル バージョン 4、状態の値、初期の幅、問い合わせ、上限なし、送信が途切れた後の許可、受信報告の欄、逆方向の DATA への相乗り、圧縮した DATA の許可数) と単体テスト
    - ⏸️ 4.4. OS の受信バッファーの拡大の要求 (`app/cplat` の関数、`framework/testfw` のモック、設定) と単体テスト
    - ⏸️ 4.5. 1 対 N の止める判定と除外、監視対象の選び方、臨時の受信報告、設定の追加と廃止 (対策元の BL-006、BL-008、BL-009、BL-011) と単体テスト
    - ⏸️ 4.6. N 対 1 サーバーの幅の配分、順番の許可、遅いピアの切断 (対策元の BL-007) と単体テスト
    - ⏸️ 4.7. 保持範囲の既定値の変更と、未追加だった単体テストの追加 (対策元の BL-010)
    - ⏸️ 4.8. 機能仕様の見直し (`docs/functional-spec/`。対策元の BL-013 を含む)
    - ⏸️ 4.9. 詳細設計の文書の見直し (`docs/protocol.md`、`docs/config.md`、`docs/api.md`、`docs/health-check.md`) とコード コメントへの知見の反映
    - ⏸️ 4.10. 局所テスト (`make`、`make test`、`check_functional_spec.py`、`text_style_jp.py`) と `.warn` の確認
    - ⏸️ 4.11. Linux での結合の確認と測定 (受入条件 3 から 9、12)
    - ⏸️ 4.12. コミット、プッシュ、CI の確認 (受入条件 1)
    - ⏸️ 4.13. Windows での確認 (受入条件 10、11。Windows のセッションへ依頼。対策元の BL-001)
    - ⏸️ 4.14. 受入条件への適合確認
    - ⏸️ 4.15. 簡易 ADR の最終確認
    - ⏸️ 4.16. 残課題一覧 (`261121-porter-flow-control-refine-backlog.md`) の再構築と整理、対策元の残課題一覧の最終状態の確定
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
| 目的・利用者 | 確認済み | ユーザーの指示 (3 つの方針)。LADR-001 から LADR-003。方針 1 と方針 3 の関係は [Q-014](261121-porter-flow-control-refine-discussion.md#q-014-あふれを防ぐ対象の解釈) |
| 範囲・境界 | 確認済み | [Q-001](261121-porter-flow-control-refine-discussion.md#q-001-対象とする残課題一覧の範囲)。LADR-004 |
| 入力・根拠 | 確認済み | `unicast` と `tcp` の比較は測定済み ([Q-022](261121-porter-flow-control-refine-discussion.md#q-022-unicast-が-tcp-を下回っていることへの対処の範囲))。Windows の値と、`multicast` の監視対象の数と処理件数の関係が未測定 (作業の中で測定し、最終仕様の提案値を確かめる。LADR-013)。[Q-011](261121-porter-flow-control-refine-discussion.md#q-011-スループットの受入の基準)、[Q-012](261121-porter-flow-control-refine-discussion.md#q-012-windows-での確認の進め方) |
| 整合性・依存関係 | 確認済み | 方針 1 と現在の仕様の相違。[Q-002](261121-porter-flow-control-refine-discussion.md#q-002-1-対-n-で救済しない受信者を見分ける方法) から [Q-005](261121-porter-flow-control-refine-discussion.md#q-005-n-対-1-サーバーの全ピア宛の送信で遅いピアがある場合の扱い) |
| 実現性・例外 | 確認済み | [Q-006](261121-porter-flow-control-refine-discussion.md#q-006-n-対-1-サーバーの受信バッファーの幅の分け方) から [Q-010](261121-porter-flow-control-refine-discussion.md#q-010-unicast-の受信報告に送出上限を載せるか)、[Q-015](261121-porter-flow-control-refine-discussion.md#q-015-最初の送出上限が届く前の送出) から [Q-021](261121-porter-flow-control-refine-discussion.md#q-021-圧縮した-data-による容量の超過) |
| 受入条件・検証 | 確認済み | [Q-011](261121-porter-flow-control-refine-discussion.md#q-011-スループットの受入の基準) から [Q-013](261121-porter-flow-control-refine-discussion.md#q-013-多数の受信者での確認の方法) |

Table: 観点別の確認

## 質問一覧

各 Q の本文は検討用 Markdown にあります。

| Q-ID | 論点 | 状態 | 依存・関連 | 反映先の決定 |
| --- | --- | --- | --- | --- |
| [Q-001](261121-porter-flow-control-refine-discussion.md#q-001-対象とする残課題一覧の範囲) | 対象とする残課題一覧の範囲 | 解決済み | なし | LADR-004 |
| [Q-002](261121-porter-flow-control-refine-discussion.md#q-002-1-対-n-で救済しない受信者を見分ける方法) | 1 対 N で救済しない受信者を見分ける方法 | 解決済み | BL-006、BL-009、BL-011 | LADR-005 |
| [Q-003](261121-porter-flow-control-refine-discussion.md#q-003-救済しない受信者の数の上限の既定値) | 救済しない受信者の数の上限の既定値 | 解決済み | Q-002、BL-008 | LADR-005 |
| [Q-004](261121-porter-flow-control-refine-discussion.md#q-004-救済しないと判定した受信者の扱い) | 救済しないと判定した受信者の扱い | 解決済み | Q-002 | LADR-005 |
| [Q-005](261121-porter-flow-control-refine-discussion.md#q-005-n-対-1-サーバーの全ピア宛の送信で遅いピアがある場合の扱い) | N 対 1 サーバーの全ピア宛の送信で遅いピアがある場合の扱い | 解決済み | Q-002 | LADR-006 |
| [Q-006](261121-porter-flow-control-refine-discussion.md#q-006-n-対-1-サーバーの受信バッファーの幅の分け方) | N 対 1 サーバーの受信バッファーの幅の分け方 | 解決済み | BL-007 | LADR-007 |
| [Q-007](261121-porter-flow-control-refine-discussion.md#q-007-os-の受信バッファーの拡大の要求) | OS の受信バッファーの拡大の要求 | 解決済み | BL-001、BL-007 | LADR-008 |
| [Q-008](261121-porter-flow-control-refine-discussion.md#q-008-逆方向のパケットへの送出上限の相乗り) | 逆方向のパケットへの送出上限の相乗り | 解決済み | BL-007 | LADR-009 |
| [Q-009](261121-porter-flow-control-refine-discussion.md#q-009-保持範囲-window_size-の既定値) | 保持範囲 (window_size) の既定値 | 解決済み | BL-006 | LADR-010 |
| [Q-010](261121-porter-flow-control-refine-discussion.md#q-010-unicast-の受信報告に送出上限を載せるか) | unicast の受信報告に送出上限を載せるか | 解決済み | BL-012 | LADR-009 |
| [Q-011](261121-porter-flow-control-refine-discussion.md#q-011-スループットの受入の基準) | スループットの受入の基準 | 解決済み | BL-007 | LADR-011 |
| [Q-012](261121-porter-flow-control-refine-discussion.md#q-012-windows-での確認の進め方) | Windows での確認の進め方 | 解決済み | BL-001 | LADR-011 |
| [Q-013](261121-porter-flow-control-refine-discussion.md#q-013-多数の受信者での確認の方法) | 多数の受信者での確認の方法 | 解決済み | BL-008 | LADR-011 |
| [Q-014](261121-porter-flow-control-refine-discussion.md#q-014-あふれを防ぐ対象の解釈) | あふれを防ぐ対象の解釈 | 解決済み | Q-002、Q-005 | LADR-012 |
| [Q-015](261121-porter-flow-control-refine-discussion.md#q-015-最初の送出上限が届く前の送出) | 最初の送出上限が届く前の送出 | 解決済み | Q-014 | LADR-012 |
| [Q-016](261121-porter-flow-control-refine-discussion.md#q-016-送出上限の通知が続けて失われた場合の回復) | 送出上限の通知が続けて失われた場合の回復 | 解決済み | Q-010、BL-012 | LADR-012 |
| [Q-017](261121-porter-flow-control-refine-discussion.md#q-017-監視対象の外の受信者が遅れた場合の合図) | 監視対象の外の受信者が遅れた場合の合図 | 解決済み | Q-002、BL-009、BL-011 | LADR-012 |
| [Q-018](261121-porter-flow-control-refine-discussion.md#q-018-受信報告を送らない構成の受信者) | 受信報告を送らない構成の受信者 | 解決済み | Q-014 | LADR-012 |
| [Q-019](261121-porter-flow-control-refine-discussion.md#q-019-n-対-1-サーバーで送信中のピアが多い場合) | N 対 1 サーバーで送信中のピアが多い場合 | 解決済み | Q-006、Q-007 | LADR-007 |
| [Q-020](261121-porter-flow-control-refine-discussion.md#q-020-data-以外のパケットの分の余白) | DATA 以外のパケットの分の余白 | 解決済み | Q-012 | LADR-012 |
| [Q-021](261121-porter-flow-control-refine-discussion.md#q-021-圧縮した-data-による容量の超過) | 圧縮した DATA による容量の超過 | 解決済み | Q-014 | LADR-012 |
| [Q-022](261121-porter-flow-control-refine-discussion.md#q-022-unicast-が-tcp-を下回っていることへの対処の範囲) | unicast が tcp を下回っていることへの対処の範囲 | 解決済み | Q-001、Q-011 | LADR-011 |
| [Q-023](261121-porter-flow-control-refine-discussion.md#q-023-1-対-n-で送出上限を知らせる受信者の数の決め方) | 1 対 N で送出上限を知らせる受信者の数の決め方 | 解決済み | Q-002、Q-007、Q-009、Q-014、Q-017、BL-008 | LADR-014 |

Table: 質問一覧

## 対象成果物と合意版

最終決定事項 (最終仕様) の設計書: `261121-porter-flow-control-refine-design.md` (版 1)

検討用 Markdown の合意版 (第 1 ゲート): 版 5 (2026-11-21 19:45)

最終仕様の合意版 (第 2 ゲート): 版 1 (2026-11-21 19:54 に承認)

更新予定の成果物: 最終仕様の「コードの変更」「機能仕様の変更」「詳細設計の文書の変更」の表のとおり (`app/porter`、`app/cplat`、`framework/testfw`)

受入条件: 最終仕様の「受入条件」の 1 から 12

## 経緯

| 日時 (JST) | 出来事 | 内容・参照先 |
| --- | --- | --- |
| 2026-11-21 13:10 | 案件の開始 | ユーザーが、porter の残件対策の推進と、2 つの方針を指示。全体管理文書、検討用 Markdown (Q-001 から Q-013)、簡易 ADR (LADR-001、LADR-002) を作成。対策元の残課題一覧の対象の項目を「検討中」に更新 |
| 2026-11-21 13:15 | 方針の追加 | ユーザーが、あふれの回避は高信頼に属し、現在対策できていない場合を救済するアルゴリズムが必要と指示。LADR-003 に記録し、Q-014 から Q-021 を追記 (検討用 Markdown の版 2) |
| 2026-11-21 16:50 | Git 操作の許可 | ユーザーが、前の案件の Git 操作の事前許可を、この案件にも適用すると指示。検討文書をコミットしてプッシュ |
| 2026-11-21 17:30 | 回答の受付 | Q-001 から Q-021 の回答を確認し、すべて解決済みとした (検討用 Markdown の版 3)。簡易 ADR に LADR-004 から LADR-013 を追加。ユーザーが、大方針に照らして設計を進め、個々の回答が大方針と矛盾する場合は追加の検討を行うと指示 |
| 2026-11-21 19:35 | 測定と追加の質問 | `unicast` と `tcp` の処理件数を測定し、64 byte 以外の条件で `unicast` が下回ることを確認。方針と回答がぶつかる 2 点を、Q-022 と Q-023 として追記 (検討用 Markdown の版 4)。ユーザーの回答待ち |
| 2026-11-21 19:45 | 回答の受付、第 1 ゲートの通過 | Q-022 と Q-023 の回答 (どちらも A と補足) を確認し、解決済みとした (検討用 Markdown の版 5)。LADR-011 を更新し、LADR-014 を追加。`unicast` を `tcp` に近づける対策を、この案件の残課題一覧の BL-001 に記録。未解決の質問がないため、最終仕様の作成に着手 |
| 2026-11-21 19:51 | 最終仕様の作成 (版 1) | `261121-porter-flow-control-refine-design.md` を作成。この案件の残課題一覧 (BL-001、BL-002) を作成。対策元の残課題一覧へ確定した方針を反映。ユーザーの承認待ち |
| 2026-11-21 19:54 | 第 2 ゲートの通過 | ユーザーが最終仕様の版 1 を承認。作業の分担を指示 (サブエージェントは `agy --dangerously-skip-permissions` で gemini-4-neon-medium を優先して使い、枯渇した場合は難易度に応じて Opus または Sonnet を使う。Fable は管理と成果物のレビューを行う)。監視対象の上限 64 台は、通信できる受信者の数の上限ではないことを対話で説明 |

Table: 経緯

## 反映と検証の記録

未着手。作業後の対応箇所、検証結果、未完了項目を記録します。
