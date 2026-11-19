<!--
生成元: agree-before-implement スキル
文書種別: 全体管理文書 (overview)
最終更新日時: 2026-11-19 23:15:00 JST
最終更新エージェント: claude code / セッション ID: session_01KJc53QxuPSr8Mxe6rktvre
稼働 OS: Linux (WSL)
下流文書: `261119-porter-flow-control-discussion.md` (検討用 Markdown)、`261119-porter-flow-control-lightadr.md` (簡易 ADR)、`261119-porter-flow-control-design.md` (最終仕様)、`261119-porter-flow-control-backlog.md` (残課題一覧)。未作成の場合や、削除されている場合があります。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。この文書と、ユーザーが編集した最新の検討用 Markdown を読み直し、作業 WBS の `← **CURRENT**` の項目から再開します。最終更新エージェントのセッション情報や稼働 OS・最終更新日時が参照可能であれば再開時のコンテキストや環境差分の把握に利用し、別エージェントや別環境・別 OS からの引き継ぎ、担当変更によりセッションが見つからない場合も異常とせず、文書の記載から継続してください。`← **CURRENT**` の項目は処理が中止された項目である場合があり、どこまで進んでいたかは文書だけでは分かりません。対象成果物の内容を確認して実施済みの範囲を特定してから、作業を再開してください。`← **CURRENT**` がなく、作業 WBS がすべて完了 (✅) であれば、この案件の合意フローは完了しています。
-->

# porter のフロー制御の導入 全体管理

ファイル名: `261119-porter-flow-control-overview.md`

この文書は、案件の識別、作業 WBS、対象範囲、確認状況、経緯を管理します。質問と回答の本文は検討用 Markdown (`261119-porter-flow-control-discussion.md`) に、決定事項は簡易 ADR (`261119-porter-flow-control-lightadr.md`) に記載し、この文書には重複して記載しません。

## 案件の識別

検討 ID: 261119-porter-flow-control

検討用 Markdown の版: 4

状態: 合意済み (検討用 Markdown の版 4)。最終仕様の版 2 は 2026-11-19 に承認済み。先行する案件と調整しながら実装中

## 作業 WBS と進捗管理

この案件の作業 WBS です。項目 4 の作業対象は、最終仕様の「機能仕様の変更」「詳細設計の文書の変更」「コードの変更」「単体テスト」に対応します。  
決定事項は、Q を解決済みにした時点、またはユーザーが決定を明示した時点で `261119-porter-flow-control-lightadr.md` へ反映します。見送りの決定や範囲外の気付きは、どの段階でも生じた時点で `261119-porter-flow-control-backlog.md` に追記します。

凡例: ⏸️ 未着手、🔄 実施中、🚫 ブロック中、⚠️ 要注意、✅ 完了。現在取り組んでいる項目の末尾に `← **CURRENT**` を付けます。

- ✅ 1. 検討用 Markdown のための情報収集と質問の実施
    - ✅ 1.1. 前提条件と要求事項の調査 (porter の文書、公開ヘッダー、通知キュー、受信報告、送信キュー、送信スレッド、過去の案件文書)
    - ✅ 1.2. 対話による方向性の確認 (計測対象、対象の通信種別、1 対 N の判定) と簡易 ADR への記録
    - ✅ 1.3. 課題・論点の抽出と確認項目 ([Q-001](261119-porter-flow-control-discussion.md#q-001-通知キューの容量の単位) から [Q-024](261119-porter-flow-control-discussion.md#q-024-受入の検証範囲)) の作成
    - ✅ 1.4. 版 1 の回答 ([Q-001](261119-porter-flow-control-discussion.md#q-001-通知キューの容量の単位) から [Q-024](261119-porter-flow-control-discussion.md#q-024-受入の検証範囲)) の受付と記録、回答の検証、簡易 ADR (LADR-001 から LADR-014) への反映
    - ✅ 1.5. 回答の組み合わせから生じた追加の確認項目 ([Q-025](261119-porter-flow-control-discussion.md#q-025-容量超過による切断の後に受信を再開する条件) から [Q-029](261119-porter-flow-control-discussion.md#q-029-サービスごとの設定での-0-の意味)) の作成
    - ✅ 1.6. 版 2 の回答 ([Q-025](261119-porter-flow-control-discussion.md#q-025-容量超過による切断の後に受信を再開する条件) から [Q-029](261119-porter-flow-control-discussion.md#q-029-サービスごとの設定での-0-の意味)) の受付と記録
    - ✅ 1.7. 版 2 の回答に基づく検証、簡易 ADR (LADR-004、LADR-005、LADR-009、LADR-011、LADR-012) への反映、追加の Q が不要であることの確認
- ✅ 2. 前提合意と残懸念の解消 (第 1 ゲート: 最終仕様作成への着手)
    - ✅ 2.1. 全論点に対する未解決事項および質問の解消確認 (Q-001 から Q-029 がすべて解決済み)
    - ✅ 2.2. 方針・前提条件の合意確定 (簡易 ADR の決定事項と、根拠となった解決済み Q の回答または対話での指示との一致の確認)
- ✅ 3. 最終決定事項 (最終仕様) の作成と承認の受付
    - ✅ 3.1. `261119-porter-flow-control-design.md` (版 1) の作成、および見送り事項の `261119-porter-flow-control-backlog.md` への追記
    - ✅ 3.1a. 版 1 への指摘 (解除の即時送出) の記録 ([Q-030](261119-porter-flow-control-discussion.md#q-030-解除を送出する時点)、LADR-007) と、最終仕様の版 2 への改版
    - ✅ 3.2. 最終仕様における受入条件と整合性の確認
    - ✅ 3.3. 対話セッションにおける最終仕様の承認の受付 (第 2 ゲート: 作業着手の合意。版 2 を承認。着手は先行する案件の完了後)
- 🔄 4. 承認された最終仕様に従った作業の実施 (作業ブランチ: `app/porter` の `flow-control`)
    - 🔄 4.0. 先行する案件 (`app/general/docs/prompts/current/261119-void-discard-reason-and-failure-injection-*`、別の作業ツリーで別セッションが担当) との調整。先行が動作を変えるファイル (設定の読み込み、`potr_service_open.c` の検査、接続のスレッド、`porter-test.c`、関連テスト、`mock_porter`) は先行の push を取り込んでから着手し、取り込み時に待機時間の設定値の上限の定数と `(void)` の理由コメントの規範を確認する
    - 🔄 4.1. 段階 A: 待ち合わせを指定しない送信の容量不足の返却 (`prod/libsrc/porter/infra/potr_send_queue.c`、`prod/libsrc/porter/api/potr_service_send.c`、`prod/include/porter.h`、関連する単体テスト)。主エージェントが実装 ← **CURRENT**
    - ⏸️ 4.2. 段階 B: 定数、公開している構造体、プロトコル バージョン 3、フロー制御パケットの組み立てと検証 (`prod/include/porter/porter_const.h`、`porter_type.h`、`prod/libsrc/porter/protocol/packet.c`、`prod/libsrc/porter/thread/thread_recv_validate.c`)。主エージェントが実装。版の値を前提にした既存テストの追随は軽量エージェントへ委譲
    - ⏸️ 4.3. 段階 C: 受信側 (占有量の計数 `prod/libsrc/porter/potr_notify.c`、状態遷移と要求・解除の送出、容量超過時の切断と再開の抑止 `prod/libsrc/porter/thread/` 配下)。主エージェントが実装
    - ⏸️ 4.4. 段階 D: 送信側 (要求の受理と期限 `prod/libsrc/porter/protocol/receiver_report.c`、`prod/libsrc/porter/potr_peer_table.c`、1 対 N の判定、送出の停止と再開、close 時の抑制の無視 `prod/libsrc/porter/thread/potr_send_thread.c`)。主エージェントが実装
    - ⏸️ 4.5. 段階 E: N 対 1 サーバーの保留と、抑制中のピア宛の送信要求の扱い (`potr_send_thread.c`、`potr_send_queue.c`、`potr_service_send.c`)。主エージェントが実装
    - ⏸️ 4.6. 段階 F: 構成の読み込み、`-1` の解釈、構成の検査 (`prod/libsrc/porter/protocol/config_load_service.c`、`config_load_global.c`、`prod/libsrc/porter/api/potr_service_open.c`、`prod/sample-config/porter-services.jsonc`)。先行の push の取り込み後に、主エージェントが実装
    - ⏸️ 4.7. 単体テストと mock の追加・更新 (`test/src/libporter/` 配下。`exportTest` の確認を含む)。各段階の中核の検証は主エージェントが書き、定型の観点の追加と既存テストの追随は軽量エージェント (Sonnet) へ委譲して主エージェントがレビューする。委譲先にはビルドとテストの実行、Git の書き込み操作を禁止する
    - ⏸️ 4.8. 動作確認用コマンドの更新 (`prod/src/cmd/porter-test/porter-test.c`。`POTR_ERR_FULL` の表示、受信コールバックを遅らせる指定)。先行の push の取り込み後に実施
    - ⏸️ 4.9. 機能仕様の更新 (`docs/functional-spec/delivery.md`、`docs/functional-spec/config.md`) と、正本ドキュメントおよびコード コメントへの反映 (`docs/protocol.md`、`docs/config.md`、`docs/architecture.md`、`docs/api.md`、`docs/sequence-data.md`、`docs/coding-guideline.md`、Doxygen コメント)。機能仕様と契約の記述は主エージェントが書き、表の追随などの定型部分は軽量エージェントへ委譲する
    - ⏸️ 4.10. 局所テスト (`app/porter` の `make` と `make test`、`.warn` の確認) と機械的検証 (`text_style_jp.py`、`check_functional_spec.py`)。同時に走らせる make は 1 本とし、主エージェントが一括で実行する
    - ⏸️ 4.11. 結合の確認 (Linux 上の `porter-test` による抑制と解除の確認。対象は `unicast`、`multicast`、`unicast_bidir`、`tcp`)
    - ⏸️ 4.12. 先行の `main` への rebase、コミット、push、CI の確認と、問題があった場合の修正 (ユーザーの事前許可による。先行より先に `main` へ push しない)
    - ⏸️ 4.13. 受入条件への適合確認 (全作業の完了確認)
    - ⏸️ 4.14. 簡易 ADR の最終確認 (各決定が案件終了時点の決定を示し、経緯が時系列にそろっていることの確認)
    - ⏸️ 4.15. 残課題一覧 (`261119-porter-flow-control-backlog.md`) の再構築と整理
- ⏸️ 5. 成果物の承認の受付 (第 3 ゲート)
    - ⏸️ 5.1. 成果物・検証結果・残課題の提示
    - ⏸️ 5.2. 対話セッションにおける成果物の承認の受付 (「承認」または「完了」の受領によるスキル完了)

## 対象と参照資料

対象成果物: `app/porter` のライブラリ (公開ヘッダー、内部ヘッダー、実装)、単体テスト、動作確認用コマンド、構成のサンプル、機能仕様と設計文書。対象ファイルの見込みは、作業 WBS の項目 4 に記載しています。

今回の対象範囲: 受信側の通知キューの占有量に基づき、送信側が DATA の送出を抑制・再開するフロー制御の導入。対象の通信種別は、片方向 UDP の再送を行う通信種別 (`unicast`、`multicast`、`broadcast`)、双方向 UDP (`unicast_bidir`、`unicast_bidir_n1`)、TCP (`tcp`、`tcp_bidir`) です。RAW モードは対象外です (簡易 ADR の LADR-003)。

書き込み可能な検討文書: この文書、検討用 Markdown (`261119-porter-flow-control-discussion.md`)、簡易 ADR (`261119-porter-flow-control-lightadr.md`)

参照資料と版: `main` ブランチのコミット `4d9459fb` の時点の次の資料です。

| 資料 | 参照した内容 |
| --- | --- |
| `app/porter/AGENTS.md` | 公開 API と文書の同期、機能仕様の同期、確認コマンド |
| `docs/functional-spec/delivery.md`、`transport.md`、`base.md` | 送信の受け付けの要件 (`POTR-DELIVERY-FUNC-009` <!-- potr-req: uuid=654a3854-f1f6-423a-85c7-d11327aa1133 -->)、適用範囲外、失敗の区分 |
| `docs/protocol.md` | パケットの形式、種別フラグ、受信報告、スライディング ウィンドウ、再送制御、プロトコル バージョン |
| `docs/architecture.md` | 通知キューと通知スレッド、スレッド構成、送信と受信のフロー、close の手順 |
| `docs/config.md`、`docs/api.md` | 構成の項目、`potr_service_send()` の戻り値、通知キューの保持領域の説明 |
| `prod/include/porter/porter_type.h`、`porter_const.h`、`porter_result.h` | 通信種別、公開している構成の構造体、定数、結果コード |
| `prod/libsrc/porter/potr_notify.c` | 通知キューの登録と領域の確保 |
| `prod/libsrc/porter/protocol/receiver_report.c`、`thread/thread_recv_report.c` | 受信報告の送出周期、送信側の受信者の管理表 |
| `prod/libsrc/porter/api/potr_service_send.c`、`infra/potr_send_queue.c`、`thread/potr_send_thread.c` | 送信キューの空き待ち、N 対 1 サーバーの送出 |
| `docs/prompts/past/261107-porter-handle-lifecycle-design.md` | 通知キューに固定上限を設けない決定 |
| `docs/prompts/past/261115-porter-reject-recovery-discussion.md` | 流量制御がないことによる欠落の記録 |

Table: 参照資料と参照した内容

## 観点別の確認

| 観点 | 状態 | 根拠・関連 Q |
| --- | --- | --- |
| 目的・利用者 | 確認済み | 依頼、LADR-001、LADR-008 (利用側へは通知しない)、LADR-010 (満杯時の送信要求の扱い) |
| 範囲・境界 | 確認済み | LADR-003 (対象の通信種別)、LADR-010 (機能仕様との相違をこの案件で解消)、LADR-011 (N 対 1 サーバー)、LADR-014 (検証の範囲。見送り事項は残課題一覧の BL-001、BL-002) |
| 入力・根拠 | 確認済み | 参照資料は「対象と参照資料」の表のとおり。再送周期、有効期限、1 対 N の既定値はユーザーの回答で確定 (LADR-004、LADR-007)。既定値に実測の根拠がない点は残課題一覧の BL-003 |
| 整合性・依存関係 | 確認済み | LADR-005 (過去の「I/O スレッドを待たせない」決定との整合)、LADR-006 (形式の互換)、LADR-009 (close と容量超過)、LADR-012 (設定の配置と 0 の意味)、LADR-013 (機能仕様の記載先) |
| 実現性・例外 | 確認済み | LADR-002 (計測の単位)、LADR-004 (1 対 N の判定と受信報告の前の要求)、LADR-005 (容量超過と再開の条件)、LADR-007 (喪失と受信者の停止)、LADR-008 (抑制中の動作) |
| 受入条件・検証 | 確認済み | LADR-014、最終仕様の「受入条件」 |

Table: 観点別の確認状況

## 質問一覧

各 Q の本文は検討用 Markdown にあります。Q-ID には検討用 Markdown の該当見出しへのリンクを設けます。

| Q-ID | 論点 | 状態 | 依存・関連 | 反映先の決定 |
| --- | --- | --- | --- | --- |
| [Q-001](261119-porter-flow-control-discussion.md#q-001-通知キューの容量の単位) | 通知キューの容量の単位 | 解決済み | なし | LADR-002 |
| [Q-002](261119-porter-flow-control-discussion.md#q-002-n-対-1-サーバーでの占有量の集計単位) | N 対 1 サーバーでの占有量の集計単位 | 解決済み | Q-019 | LADR-002 |
| [Q-003](261119-porter-flow-control-discussion.md#q-003-容量を超えた受信データの扱い) | 容量を超えた受信データの扱い | 解決済み | Q-025、Q-026 | LADR-005 |
| [Q-004](261119-porter-flow-control-discussion.md#q-004-しきい値の変更可否) | しきい値の変更可否 | 解決済み | Q-025 | LADR-001 |
| [Q-005](261119-porter-flow-control-discussion.md#q-005-抑制状態を伝えるパケット) | 抑制状態を伝えるパケット | 解決済み | Q-006、Q-007 | LADR-006 |
| [Q-006](261119-porter-flow-control-discussion.md#q-006-tcp-での実現方式) | TCP での実現方式 | 解決済み | Q-005 | LADR-006 |
| [Q-007](261119-porter-flow-control-discussion.md#q-007-プロトコル-バージョンの更新) | プロトコル バージョンの更新 | 解決済み | Q-005 | LADR-006 |
| [Q-008](261119-porter-flow-control-discussion.md#q-008-抑制の要求と解除が届かない場合の回復方式) | 抑制の要求と解除が届かない場合の回復方式 | 解決済み | Q-009、Q-011 | LADR-007 |
| [Q-009](261119-porter-flow-control-discussion.md#q-009-抑制の要求の再送周期と有効期限) | 抑制の要求の再送周期と有効期限 | 解決済み | Q-008 | LADR-007 |
| [Q-010](261119-porter-flow-control-discussion.md#q-010-1-対-n-の割合条件と台数条件の組み合わせ) | 1 対 N の割合条件と台数条件の組み合わせ | 解決済み | Q-011、Q-012、Q-029 | LADR-004 |
| [Q-011](261119-porter-flow-control-discussion.md#q-011-割合の分母にする受信者数) | 割合の分母にする受信者数 | 解決済み | Q-008、Q-010、Q-027 | LADR-004 |
| [Q-012](261119-porter-flow-control-discussion.md#q-012-割合と台数の既定値) | 割合と台数の既定値 | 解決済み | Q-010、Q-027 | LADR-004 |
| [Q-013](261119-porter-flow-control-discussion.md#q-013-抑制中に送出を止めるパケットの範囲) | 抑制中に送出を止めるパケットの範囲 | 解決済み | なし | LADR-008 |
| [Q-014](261119-porter-flow-control-discussion.md#q-014-抑制が長時間続く場合の扱い) | 抑制が長時間続く場合の扱い | 解決済み | Q-018 | LADR-008 |
| [Q-015](261119-porter-flow-control-discussion.md#q-015-close-と抑制の関係) | close と抑制の関係 | 解決済み | Q-003、Q-026 | LADR-009 |
| [Q-016](261119-porter-flow-control-discussion.md#q-016-抑制中の送信要求の扱い) | 抑制中の送信要求の扱い | 解決済み | Q-017、Q-028 | LADR-010 |
| [Q-017](261119-porter-flow-control-discussion.md#q-017-容量不足の要件と実装の相違の扱い) | 容量不足の要件と実装の相違の扱い | 解決済み | Q-016、Q-028 | LADR-010 |
| [Q-018](261119-porter-flow-control-discussion.md#q-018-送信側の利用側への抑制の通知) | 送信側の利用側への抑制の通知 | 解決済み | Q-014 | LADR-008 |
| [Q-019](261119-porter-flow-control-discussion.md#q-019-n-対-1-サーバーから抑制中のピアへの送信) | N 対 1 サーバーから抑制中のピアへの送信 | 解決済み | Q-002、Q-028 | LADR-011 |
| [Q-020](261119-porter-flow-control-discussion.md#q-020-有効化の方法と既定の状態) | 有効化の方法と既定の状態 | 解決済み | Q-021、Q-022、Q-029 | LADR-012 |
| [Q-021](261119-porter-flow-control-discussion.md#q-021-送信側で抑制の要求に従わない設定) | 送信側で抑制の要求に従わない設定 | 解決済み | Q-020 | LADR-008 |
| [Q-022](261119-porter-flow-control-discussion.md#q-022-設定項目の配置) | 設定項目の配置 | 解決済み | Q-020、Q-029 | LADR-012 |
| [Q-023](261119-porter-flow-control-discussion.md#q-023-機能仕様の記載先) | 機能仕様の記載先 | 解決済み | Q-017 | LADR-013 |
| [Q-024](261119-porter-flow-control-discussion.md#q-024-受入の検証範囲) | 受入の検証範囲 | 解決済み | なし | LADR-014 |
| [Q-025](261119-porter-flow-control-discussion.md#q-025-容量超過による切断の後に受信を再開する条件) | 容量超過による切断の後に受信を再開する条件 | 解決済み | Q-003、Q-004 | LADR-005 |
| [Q-026](261119-porter-flow-control-discussion.md#q-026-close-で送出した-data-が受信側の容量を超えた場合の扱い) | close で送出した DATA が受信側の容量を超えた場合の扱い | 解決済み | Q-003、Q-015 | LADR-009 |
| [Q-027](261119-porter-flow-control-discussion.md#q-027-活動中と判定していない受信者からの抑制の要求) | 活動中と判定していない受信者からの抑制の要求 | 解決済み | Q-011、Q-012 | LADR-004 |
| [Q-028](261119-porter-flow-control-discussion.md#q-028-n-対-1-サーバーで抑制中のピア宛の送信要求の受け付け) | N 対 1 サーバーで抑制中のピア宛の送信要求の受け付け | 解決済み | Q-016、Q-017、Q-019 | LADR-011 |
| [Q-029](261119-porter-flow-control-discussion.md#q-029-サービスごとの設定での-0-の意味) | サービスごとの設定での 0 の意味 | 解決済み | Q-010、Q-020、Q-022 | LADR-012 |
| [Q-030](261119-porter-flow-control-discussion.md#q-030-解除を送出する時点) | 解除を送出する時点 | 解決済み | Q-005、Q-008 | LADR-007 |

Table: 質問一覧

## 対象成果物と合意版

最終決定事項 (最終仕様) の設計書: `261119-porter-flow-control-design.md` (版 2)

検討用 Markdown の合意版 (第 1 ゲート): 版 4 (2026-11-19 22:20 に残懸念なしを再確認。版 3 は 22:12 に確認)

最終仕様 (design.md) の合意版 (第 2 ゲート): 版 2 (2026-11-19 23:01 に承認。着手は先行する案件の完了後)

更新予定の成果物: 最終仕様の「機能仕様の変更」「詳細設計の文書の変更」「コードの変更」「単体テスト」に記載した対象

受入条件: 最終仕様の「受入条件」の 6 項目

## 経緯

案件の進行上の出来事を、発生順に末尾へ追記します。個々の決定の内容は簡易 ADR に記載し、ここには出来事と参照先だけを記載します。

| 日時 (JST) | 出来事 | 内容・参照先 |
| --- | --- | --- |
| 2026-11-19 19:55 | 案件の開始 | ユーザーの依頼を受け、調査を開始 |
| 2026-11-19 20:00 | 対話による方向性の確認 | 計測対象、対象の通信種別、1 対 N の判定の 3 点を対話で確認。簡易 ADR の LADR-001 から LADR-004 に記録 |
| 2026-11-19 20:20 | 検討文書の作成 | 全体管理文書、検討用 Markdown (版 1、Q-001 から Q-024)、簡易 ADR を作成。ユーザーの回答待ち |
| 2026-11-19 20:31 | 版 1 の回答の受付 | Q-001 から Q-024 の回答を確認。Q-015 を除く 23 件を解決済みとし、簡易 ADR の LADR-001 から LADR-014 に反映 |
| 2026-11-19 20:50 | 検討用 Markdown の改版 (版 2) と作業 WBS の見直し | 回答の組み合わせから生じた Q-025 から Q-029 を追記。送信の容量不足の実装変更 (LADR-010) に伴い、作業 WBS の 4.1、4.2、4.5、4.7、4.8 を見直し。ユーザーの回答待ち |
| 2026-11-19 22:10 | 版 2 の回答の受付 | Q-025 から Q-029 の回答を確認し、すべて解決済みとした。Q-015 も解決済みとした。簡易 ADR の LADR-004、LADR-005、LADR-009、LADR-011、LADR-012 を更新 (検討用 Markdown の版 3) |
| 2026-11-19 22:12 | 第 1 ゲートの通過 | Q-001 から Q-029 がすべて解決済みで、観点別の確認に未解決がないことを確認 |
| 2026-11-19 22:15 | 最終仕様の作成 | `261119-porter-flow-control-design.md` (版 1) を作成。見送り事項を残課題一覧 (BL-001 から BL-003) に追記。ユーザーの承認待ち |
| 2026-11-19 22:17 | 最終仕様の版 1 の差し戻し | 第 2 ゲートで、解除の送出が再送周期だけ遅れる点に指摘を受けた。Q-030 に記録 (検討用 Markdown の版 4)。簡易 ADR の LADR-007 を更新 |
| 2026-11-19 22:20 | 最終仕様の改版 | 解除を通知スレッドが即時に送出する内容に改め、版 2 とした。残懸念がないことを再確認。ユーザーの承認待ち |
| 2026-11-19 22:20 から 23:00 の間 | 先行する案件との調整 | 別セッションが担当する案件 `261119-void-discard-reason-and-failure-injection` が `app/porter/prod` の多数のファイルを変更中であることを、セッション間の連絡で確認。変更予定のファイルが重なる |
| 2026-11-19 23:01 | 第 2 ゲートの通過 | ユーザーが「先行の完了を以て作業開始を許可します」と指示。最終仕様の版 2 を承認とし、作業の着手は先行する案件の完了後とする |
| 2026-11-19 23:05 | 着手条件の変更 | ユーザーが「先行セッションと対話して進めてください。git 操作は事前許可します」と指示。先行セッションの変更がこちらの作業ツリーにないこと (別の作業ツリー) を確認し、並行して着手する進め方を先行セッションへ提案 |
| 2026-11-19 23:15 | 作業の着手と作業 WBS の見直し | ユーザーが「修正後、コミットプッシュし、CI を確認し、問題があれば修正するように」と指示。`app/porter` に作業ブランチ `flow-control` を作成。作業 WBS の項目 4 を、実装の段階と委譲の割り当てを示す形に見直した |

Table: 案件の経緯

## 反映と検証の記録

未着手。作業後の対応箇所、検証結果、未完了項目を記録します。
