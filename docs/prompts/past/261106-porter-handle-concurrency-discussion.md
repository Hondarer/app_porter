<!--
生成元: agree-before-implement スキル
文書種別: 検討用 Markdown (discussion)
下流文書: `261106-porter-handle-concurrency-design.md` (最終仕様)、`261106-porter-handle-concurrency-backlog.md` (残課題一覧)。未作成の場合や、削除されている場合があります。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。ユーザーが編集した最新版を読み直し、作業 WBS の `← **CURRENT**` の項目から再開します。`← **CURRENT**` の項目は処理が中止された項目である場合があり、どこまで進んでいたかは文書だけでは分かりません。対象成果物の内容を確認して実施済みの範囲を特定してから、作業を再開してください。`← **CURRENT**` がなく、作業 WBS がすべて完了 (✅) であれば、この案件の合意フローは完了しています。
-->

# porter ハンドルの並行送信対応と不具合対策の検討

ファイル名: `261106-porter-handle-concurrency-discussion.md`

## 検討の識別

検討 ID: 261106-porter-handle-concurrency

版: 3

状態: 合意済み (版 3)

## 作業 WBS と進捗管理

この検討用 Markdown が管理する作業 WBS です。  
情報収集と質問、第 1 ゲート、最終仕様 (`261106-porter-handle-concurrency-design.md`) の作成、第 2 ゲート、作業の実施、第 3 ゲートまでを記載します。  
作業 4 の不具合対策と並行送信対応は、ユーザーの指示どおり 1 件ずつ順に実施します。順序は [Q-001](#q-001-作業の実施順) の回答で確定し、必要に応じて並べ替えます。

凡例: ⏸️ 未着手、🔄 実施中、🚫 ブロック中、⚠️ 要注意、✅ 完了。現在取り組んでいる項目の末尾に `← **CURRENT**` を付けます。

- ✅ 1. 検討用 Markdown のための情報収集と質問の実施
    - ✅ 1.1. 前提条件と要求事項の調査 (実装、文書、cplat の同期 API の確認)
    - ✅ 1.2. 確認項目 ([Q-001](#q-001-作業の実施順) から [Q-009](#q-009-結合試験の追加範囲)) の作成
    - ✅ 1.3. 版 1 の回答の受付と記録 (2026-11-06、Tetsuo Honda)
    - ✅ 1.4. 版 1 の回答に基づく検証と WBS の見直し (cplat の作業項目を追加)
    - ✅ 1.5. 追加の確認項目 ([Q-010](#q-010-既存の-cplat_process_get_tid-の利用)) の作成 ([Q-007](#q-007-コールバック内呼び出しの検出手段) の前提の誤りの訂正を含む)
    - ✅ 1.6. 版 2 の回答の受付と記録 (2026-11-06、Tetsuo Honda)
    - ✅ 1.7. 追加の確認項目 ([Q-011](#q-011-cplat-の説明を汎用化する対象-api)、[Q-012](#q-012-cplat-の機能要件-func-011-の-id-と-uuid-の扱い)) の作成
    - ✅ 1.8. 版 3 の回答の受付と記録 (2026-11-06、Tetsuo Honda)
- ✅ 2. 前提合意と残懸念の解消 (第 1 ゲート: 最終仕様作成への着手)
    - ✅ 2.1. 全論点に対する未解決事項および質問の解消確認
    - ✅ 2.2. 方針・前提条件の合意確定 (合意版: 版 3)
- ✅ 3. 最終決定事項 (最終仕様) の作成と承認の受付
    - ✅ 3.1. `261106-porter-handle-concurrency-design.md` の作成
    - ✅ 3.2. 最終仕様における受入条件と整合性の確認
    - ✅ 3.3. 対話セッションにおける最終仕様の承認の受付 (第 2 ゲート。2026-11-06、Tetsuo Honda が承認)
- 🔄 4. 承認された最終仕様に従った作業の実施
    - ✅ 4.1. 不具合 B-1: N:1 送信スレッドのピア参照競合の修正
        - ✅ 4.1.1. `prod/libsrc/porter/thread/potr_send_thread.c` の `send_packed_peer_mode` / `flush_packed_peer` の修正
        - ✅ 4.1.2. `test/src/libporter/potrSendThreadTest/` の単体テストの追加・更新
    - ✅ 4.2. 不具合 B-2/B-3: コールバック内からの API 呼び出しへの対策
        - ✅ 4.2.0. cplat の識別子取得 API の説明の汎用化 (サブモジュール `app/cplat`。コードと公開関数の変更なし)
            - ✅ 4.2.0.1. `app/cplat/prod/include/cplat/runtime/process.h` の Doxygen の更新 (対象は [Q-011](#q-011-cplat-の説明を汎用化する対象-api) による)
            - ✅ 4.2.0.2. `app/cplat/docs/api-cheatsheet.md` (386 行付近) の更新
            - ✅ 4.2.0.3. `app/cplat/docs/functional-spec/runtime.md` の機能要件と「プロセスとスレッドの識別」節の更新 (ID と UUID は [Q-012](#q-012-cplat-の機能要件-func-011-の-id-と-uuid-の扱い) による)
            - ✅ 4.2.0.4. ワークスペース ルートでの `python3 app/general/bin/check_functional_spec.py` の実行
        - ✅ 4.2.1. `prod/libsrc/porter/potr_path_event.c` のコールバック実行中の記録
        - ✅ 4.2.2. 対象 API (`potr_service_send.c`、`potr_service_close.c`、`potr_peer_disconnect.c`) の検出処理
        - ✅ 4.2.3. `porter_spec.h`、`porter_type.h` (`potr_recv_fn`) の Doxygen と `docs/api.md` の更新
        - ✅ 4.2.4. 単体テストの追加・更新
    - ✅ 4.3. 不具合 B-4: TCP close の `send_has_data` 競合の修正
        - ✅ 4.3.1. `prod/libsrc/porter/api/potr_service_close.c` の修正
        - ✅ 4.3.2. `test/src/libporter/potrServiceCloseTest/` の確認・更新
    - ✅ 4.4. 同一ハンドルの並行送信対応 (部分直列化)
        - ✅ 4.4.1. 送信キューの番号 (チケット) 管理 (`potr_send_queue.h`、`potr_send_queue.c`)
        - ✅ 4.4.2. 送信 API の圧縮一時領域・登録用 mutex・番号待ち (`potr_service_send.c`)
        - ✅ 4.4.3. コンテキストと資源管理 (`potr_context.h`、`potr_service_open.c`、`api_context_cleanup.c`)
        - ✅ 4.4.4. 単体テスト (`potrSendQueueTest`、`potr_service_send_test`) の追加・更新
    - ✅ 4.5. 結合試験 (`test/src/integration/porterSendRecvTest/`) の追加
    - ✅ 4.6. 正本ドキュメントの更新 (`docs/api.md`、`docs/sequence-data.md`、`docs/architecture.md`、`porter_spec.h`、`potr_send_queue.h`)
    - ✅ 4.7. `docs/functional-spec/` への影響確認
    - ✅ 4.8. Markdown の機械的検証 (`text_style_jp.py --dry-run` の後に `--in-place`)
    - ✅ 4.9. エージェントによる Windows でのビルド・`make test` の実行と `.warn` ファイルの確認 (2026-11-06 のユーザー指示。porter は全 28 スイート成功。`compress_buf` の削除で確保回数が 7 回になったため、`potrServiceOpenTest` の確保 8 回目の失敗テストを削除。cplat はコメントと文書だけの変更のため、ユーザー指示により試験結果は参考扱い)
    - 🔄 4.9.1. `app/cplat`、`app/porter`、親リポジトリの順のコミットと `main` へのプッシュ (2026-11-06 のユーザー指示。porter の未プッシュ コミット 516895b は同時にプッシュし、検討用 Markdown と最終仕様は `docs/prompts/past/` へ移して含める) ← **CURRENT**
    - ⏸️ 4.9.2. CI での各 OS の結果確認と、変更起因の失敗の修正
    - ⏸️ 4.10. 受入条件への適合確認 (全完確認)
    - ⏸️ 4.11. 未対応残渣がある場合の残課題記録 (`261106-porter-handle-concurrency-backlog.md`)
- ⏸️ 5. 成果物の承認の受付 (第 3 ゲート)
    - ⏸️ 5.1. 成果物・検証結果・残課題の提示
    - ⏸️ 5.2. 対話セッションにおける成果物の承認の受付

## 対象と参照資料

対象成果物: `app/porter/` 配下の送信 API、送信キュー、送信スレッド、コールバック発火処理、close 処理、関連する単体テスト・結合試験・文書。

今回の対象範囲: 同一ハンドルからの `potr_service_send` の並行呼び出しの許可と、レビューで見つけた不具合 4 件 (B-1 から B-4) の修正。

書き込み可能な検討用 Markdown: この文書。

参照資料と版: 2026-11-06 時点の作業ツリー (main、15acdc53)。主な参照箇所は次のとおりです。

| 参照 | 内容 |
| --- | --- |
| [potr_service_send.c](../../prod/libsrc/porter/api/potr_service_send.c) | 送信 API。共有圧縮領域、断片の登録、ブロッキング送信 |
| [potr_send_queue.c](../../prod/libsrc/porter/infra/potr_send_queue.c) | 送信キュー |
| [potr_send_thread.c](../../prod/libsrc/porter/thread/potr_send_thread.c) | 送信スレッド。344-347 行でピア検索後にロックを解放 |
| [potr_peer_table.c](../../prod/libsrc/porter/potr_peer_table.c) | `potr_internal_peer_free` (430-449 行) |
| [potr_recv_thread.c](../../prod/libsrc/porter/thread/potr_recv_thread.c) | N:1 受信処理 (54-107 行で `peers_mutex` を保持したまま配信) |
| [potr_path_event.c](../../prod/libsrc/porter/potr_path_event.c) | コールバック発火 |
| [potr_service_close.c](../../prod/libsrc/porter/api/potr_service_close.c) | close 処理 (297 行で `send_has_data` をロックなしで参照) |
| [api.md](../api.md) | 公開 API のスレッド セーフ表 |
| [compiler.h](../../../cplat/prod/include/cplat/base/compiler.h) | `THREAD_LOCAL` マクロ |

Table: 主な参照資料

## 現行の合意内容

ユーザーが明示した内容と、解決済み Q の決定のみを記載します。

- 同一ハンドルの並行送信は、問題になる箇所だけを直列化する方式 (部分直列化) で対応します。根拠は、2026-11-06 の対話で、全体直列化との比較を求めたユーザー (Tetsuo Honda) が、部分直列化を推奨した計画について「このプランの詳細実装計画を作成」と指示したことです。方式の内容は次の 3 点です。
    - 圧縮は呼び出しごとの一時領域で行い、ロックなしで並列に実行します (一時領域の方式は [Q-002](#q-002-圧縮用一時領域の方式))。
    - 断片のキュー登録だけを、ハンドルごとの登録用 mutex で直列化します。キュー容量 (最小 2) より断片数の多いメッセージがあるため、キュー操作 1 回でまとめて登録する方式は使えません。
    - ブロッキング送信は、登録順の番号 (チケット) で自分の最後の断片の完了を待ちます。送信スレッドは 1 本で、取り出した順に完了させるため、番号の比較で先行分を含めた完了を判定できます。
- `POTR_SEND_BLOCKING` の送信前のキュー空き待ちを廃止します。根拠は、2026-11-06 の対話の質問でユーザーが「事前待ちを廃止 (Recommended)」を選択したことです。自分の送信が直前の他の送信と同じ UDP パケットにまとめられる場合があることも、質問文で説明済みです。
- 不具合対策を同じ案件で行い、実際の作業は 1 件ずつ順に進めます。根拠は、2026-11-06 のユーザー指示「不具合対策も同時 (実際にはシーケンシャル) に行いたい」です。対象は次の 4 件です。
    - B-1: N:1 送信スレッドが `peers_mutex` を外した後にピア領域を使い、ピア解放と競合します。
    - B-2: N:1 の受信コールバック内で `potr_service_send` を呼ぶと、非再帰の `peers_mutex` を二重に取得してデッドロックします。
    - B-3: コールバック内で `potr_service_close` を呼ぶと、受信スレッドが自分自身を join するため終了しません。
    - B-4: TCP close が `ctx->send_has_data` をロックなしで読みます。修正方法は、直前の `get_fin_target_seq(ctx, NULL)` の第 2 引数で一緒に取得する方法がほぼ唯一のため、Q を設けていません。別の方法を望む場合は指摘してください。

版 1 の回答 (2026-11-06、Tetsuo Honda) による決定は次のとおりです。

- 作業順は B-1 → B-2/B-3 → B-4 → 並行送信対応 とします ([Q-001](#q-001-作業の実施順))。
- 圧縮用の一時領域は、圧縮を指定した呼び出しごとに確保し、登録後に消去して解放します。確保失敗時は `POTR_ERR_OUT_OF_MEMORY` を返します。`ctx->compress_buf` は削除します ([Q-002](#q-002-圧縮用一時領域の方式))。
- `potr_service_close` と他 API の並行呼び出しは禁止のままとし、文書の記述を揃えます ([Q-003](#q-003-close-と他-api-の並行呼び出しの扱い))。
- B-1 は、flush の間 `peers_mutex` を保持し、ピアを ID で再検索して修正します ([Q-004](#q-004-n1-送信スレッドのピア参照競合の修正方式))。
- B-2/B-3 は、コールバック実行中の呼び出しを検出してエラーを返し、文書にも記載します ([Q-005](#q-005-コールバック内からの-api-呼び出しへの対策方式))。対象は、N:1 の `potr_service_send`、全通信種別の `potr_service_close`、`potr_peer_disconnect` です ([Q-006](#q-006-コールバック内呼び出しを検出する-api))。戻り値は `POTR_ERR_UNSUPPORTED` です ([Q-008](#q-008-コールバック内呼び出しを検出した場合の戻り値))。
- 検出には、cplat のスレッド識別 API を使う方針です ([Q-007](#q-007-コールバック内呼び出しの検出手段) の B)。ただし、Q-007 の背景にあった「cplat には現在のスレッドを識別する API がない」という記述は誤りで、既存の `cplat_process_get_tid()` があります。
- 検出には既存の `cplat_process_get_tid()` を使い、cplat に新しい関数は追加しません。あわせて、cplat 側の説明 (診断目的に限った記述) を汎用的な表現にする作業を同じ案件で行います ([Q-010](#q-010-既存の-cplat_process_get_tid-の利用))。汎用化の対象は、スレッド ID とプロセス ID の両方です ([Q-011](#q-011-cplat-の説明を汎用化する対象-api))。機能要件 `CPLAT-RUNTIME-FUNC-010` <!-- cplat-req: uuid=0e823b69-8e80-457a-997a-60fb0fdecc79 --> と `CPLAT-RUNTIME-FUNC-011` <!-- cplat-req: uuid=7f2bb4ce-766b-4fde-8577-ab1c9aecdb3c --> は、要件 ID と UUID を維持して要件文を修正します ([Q-012](#q-012-cplat-の機能要件-func-011-の-id-と-uuid-の扱い))。
- 結合試験は、並行送信の内容保全、ブロッキング送信の復帰、N:1 送信中のピア切断、コールバック内呼び出しのエラー返却の 4 種類を追加します ([Q-009](#q-009-結合試験の追加範囲))。

## 観点別の確認

| 観点 | 状態 | 根拠・関連 Q |
| --- | --- | --- |
| 目的・利用者 | 確認済み | ハンドル利用者が同一ハンドルから複数スレッドで送信できるようにし、既存不具合を除く ([現行の合意内容](#現行の合意内容)) |
| 範囲・境界 | 確認済み | [Q-003](#q-003-close-と他-api-の並行呼び出しの扱い)、[Q-006](#q-006-コールバック内呼び出しを検出する-api)、[Q-010](#q-010-既存の-cplat_process_get_tid-の利用) (cplat は説明文だけ変更)、[Q-011](#q-011-cplat-の説明を汎用化する対象-api) |
| 入力・根拠 | 確認済み | [対象と参照資料](#対象と参照資料)。[Q-007](#q-007-コールバック内呼び出しの検出手段) の前提の誤りを [Q-010](#q-010-既存の-cplat_process_get_tid-の利用) で訂正 |
| 整合性・依存関係 | 確認済み | [Q-001](#q-001-作業の実施順)、[Q-005](#q-005-コールバック内からの-api-呼び出しへの対策方式)、[Q-008](#q-008-コールバック内呼び出しを検出した場合の戻り値)、[Q-012](#q-012-cplat-の機能要件-func-011-の-id-と-uuid-の扱い) (機能仕様の記載規範との整合) |
| 実現性・例外 | 確認済み | [Q-002](#q-002-圧縮用一時領域の方式)、[Q-004](#q-004-n1-送信スレッドのピア参照競合の修正方式)、[Q-006](#q-006-コールバック内呼び出しを検出する-api) の Assessment |
| 受入条件・検証 | 確認済み | [Q-009](#q-009-結合試験の追加範囲)。受入条件の全体は最終仕様で定める |

Table: 観点別の確認状況

## 質問一覧

| Q-ID | 論点 | 状態 | 依存・関連 |
| --- | --- | --- | --- |
| [Q-001](#q-001-作業の実施順) | 作業の実施順 | 解決済み | なし |
| [Q-002](#q-002-圧縮用一時領域の方式) | 圧縮用一時領域の方式 | 解決済み | なし |
| [Q-003](#q-003-close-と他-api-の並行呼び出しの扱い) | close と他 API の並行呼び出しの扱い | 解決済み | なし |
| [Q-004](#q-004-n1-送信スレッドのピア参照競合の修正方式) | N:1 送信スレッドのピア参照競合の修正方式 | 解決済み | なし |
| [Q-005](#q-005-コールバック内からの-api-呼び出しへの対策方式) | コールバック内からの API 呼び出しへの対策方式 | 解決済み | なし |
| [Q-006](#q-006-コールバック内呼び出しを検出する-api) | コールバック内呼び出しを検出する API | 解決済み | [Q-005](#q-005-コールバック内からの-api-呼び出しへの対策方式) |
| [Q-007](#q-007-コールバック内呼び出しの検出手段) | コールバック内呼び出しの検出手段 | 回答済み (前提訂正のため [Q-010](#q-010-既存の-cplat_process_get_tid-の利用) で再確認) | [Q-005](#q-005-コールバック内からの-api-呼び出しへの対策方式)、[Q-010](#q-010-既存の-cplat_process_get_tid-の利用) |
| [Q-008](#q-008-コールバック内呼び出しを検出した場合の戻り値) | 検出した場合の戻り値 | 解決済み | [Q-005](#q-005-コールバック内からの-api-呼び出しへの対策方式) |
| [Q-009](#q-009-結合試験の追加範囲) | 結合試験の追加範囲 | 解決済み | なし |
| [Q-010](#q-010-既存の-cplat_process_get_tid-の利用) | 既存の `cplat_process_get_tid` の利用 | 解決済み | [Q-007](#q-007-コールバック内呼び出しの検出手段) |
| [Q-011](#q-011-cplat-の説明を汎用化する対象-api) | cplat の説明を汎用化する対象 API | 解決済み | [Q-010](#q-010-既存の-cplat_process_get_tid-の利用) |
| [Q-012](#q-012-cplat-の機能要件-func-011-の-id-と-uuid-の扱い) | cplat の機能要件 FUNC-011 の ID と UUID の扱い | 解決済み | [Q-010](#q-010-既存の-cplat_process_get_tid-の利用)、[Q-011](#q-011-cplat-の説明を汎用化する対象-api) |

Table: 質問一覧

## 明確化のための確認項目

### Q-001 作業の実施順

**Status**

解決済み

**Question**

不具合対策 4 件と並行送信対応を、どの順で実施しますか。

**Context**

ユーザーの指示により、作業は 1 件ずつ順に進めます。  
B-1 (送信スレッド) と並行送信対応 (送信 API と送信キュー) は、どちらも送信経路を変更します。B-2/B-3 の検出処理は `potr_service_send` の入口に入ります。並行送信対応で同じ関数を大きく書き換えるため、順序によって手戻りの量が変わります。  
先に不具合を直すと、並行送信対応の試験を、既知の競合がない状態で行えます。

**Answer**

選択方式: 単一選択

- [x] A: (推奨) B-1 → B-2/B-3 → B-4 → 並行送信対応 の順にする。
- [ ] B: 並行送信対応 → B-1 → B-2/B-3 → B-4 の順にする。
- [ ] C: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

回答者: Tetsuo Honda / 回答日時: 2026-11-06 00:41:43 JST (検討用 Markdown の保存時刻)

**Recommendation**

推奨: A  
理由: 不具合は利用者が契約を守っても起きるため、優先度が高いです。また、並行送信の結合試験を、B-1 の競合がない状態で実施できます。

**Assessment**

単一選択で A のみが選ばれており、他の回答と矛盾しません。作業 WBS の項目 4 の順序は回答どおりのため、並べ替えは不要です。

### Q-002 圧縮用一時領域の方式

**Status**

解決済み

**Question**

並行送信時の圧縮に使う一時領域を、どの方式で用意しますか。

**Context**

現在は、ハンドルごとに 1 つの `ctx->compress_buf` (大きさは `max_message_size` + 圧縮ヘッダー + 64 [byte]) を全送信で共有しています。並行送信では、呼び出しごとに別の領域が必要です。  
圧縮結果はキュー登録時にキューのプールへ複製されるため、一時領域は全断片の登録が終わるまで使えれば十分です。  
現在の領域は解放時に `cplat_secure_zero` で消去しており (`api_context_cleanup.c:21-28`)、送信データの機密性を考慮しています。

```c
/* 案 A: 呼び出しごとに確保 */
uint8_t *cmp_buf = cplat_malloc(ctx->compress_buf_size);
/* 圧縮 → 断片登録 */
cplat_secure_zero(cmp_buf, ctx->compress_buf_size);
cplat_free(cmp_buf);
```

```c
/* 案 B: ハンドルごとに N 個の領域を持ち、空きを mutex と条件変数で貸し出す */
cmp_buf = compress_pool_acquire(ctx); /* 空きがなければ待つ */
/* 圧縮 → 断片登録 */
compress_pool_release(ctx, cmp_buf);
```

案 C は、`THREAD_LOCAL` のスレッドごとの領域を使います。ただし、大きさがハンドルごとに異なり、スレッド終了時に解放する手段が C にはないため、領域が残り続けます。

**Answer**

選択方式: 単一選択

- [x] A: (推奨) 圧縮を指定した呼び出しごとに確保し、登録後に消去して解放する。確保失敗時は `POTR_ERR_OUT_OF_MEMORY` を返す。
- [ ] B: ハンドルごとに複数の領域を持ち、貸し出す。
- [ ] C: スレッドごとの領域 (`THREAD_LOCAL`) を使う。
- [ ] D: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

回答者: Tetsuo Honda / 回答日時: 2026-11-06 00:41:43 JST (検討用 Markdown の保存時刻)

**Recommendation**

推奨: A  
理由: 圧縮 (DEFLATE) の処理時間に比べて確保の負荷は小さく、構造が最も単純です。`POTR_ERR_OUT_OF_MEMORY` は既存の戻り値です。案 B は領域数という新しい設定値の決定が必要になり、案 C は解放されない領域が残ります。  
制約: 送信だけで使っていた `ctx->compress_buf` は削除します。受信の展開領域が使う `compress_buf_size` は残します。

**Assessment**

単一選択で A のみが選ばれています。`POTR_ERR_OUT_OF_MEMORY` は `potr_service_send` の既存の戻り値表 (`porter_spec.h:248`) にあります。ただし、現在の説明は「全ピア送信用の一時領域を確保できません」に限られているため、圧縮用の一時領域も含める記述へ Doxygen を更新します。

### Q-003 close と他 API の並行呼び出しの扱い

**Status**

解決済み

**Question**

`potr_service_close` と、同一ハンドルへの他 API の並行呼び出しを、今回も禁止のままとしますか。

**Context**

close は最後に `cplat_free(ctx)` でハンドルを解放します。送信中のスレッドがあれば、解放後のメモリを使います。  
並行を許可するには、実行中の API 呼び出しの数を数え、close が全呼び出しの終了を待つ仕組み (参照カウント) が必要です。加えて、解放後のハンドルで呼び出された場合は検出できないため、ハンドル表による間接参照などの大きな設計変更が必要です。

**Answer**

選択方式: 単一選択

- [x] A: (推奨) 禁止のままとし、文書の記述を揃える。
- [ ] B: 今回の範囲で、close との並行も安全にする。
- [ ] C: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

回答者: Tetsuo Honda / 回答日時: 2026-11-06 00:41:43 JST (検討用 Markdown の保存時刻)

**Recommendation**

推奨: A  
理由: 解放後のハンドルの使用は、C のハンドル型 API では呼び出し側の責任とするのが一般的です。安全にするには公開 API の意味が変わる規模の変更が必要で、今回の目的 (並行送信) を超えます。

**Assessment**

単一選択で A のみが選ばれています。文書を揃える対象は、`docs/api.md` のスレッド セーフ表とハンドルの節、`porter_spec.h` の `potr_service_send`、`potr_service_close`、`potr_peer_disconnect` のスレッド セーフ節です。

### Q-004 N:1 送信スレッドのピア参照競合の修正方式

**Status**

解決済み

**Question**

B-1 (送信スレッドのピア参照競合) を、どの方式で修正しますか。

**Context**

送信スレッドは、ピアを `peers_mutex` 下で検索した後にロックを外し、その後でピアのウィンドウ、mutex、宛先を使います。その間に受信スレッドなどが `potr_internal_peer_free` を実行すると、破棄済みの mutex やウィンドウを使います。また、同じ枠に作られた別のピアへ誤って送る場合があります。  
ヘルス チェック スレッドは、N:1 の PING 送信を `peers_mutex` を保持したまま `sendto` まで行っており (`potr_health_thread.c:287-371`)、同じ方式の前例があります。ロック順は `peers_mutex` → `peer->send_window_mutex` で、受信スレッドとヘルス チェック スレッドと同じになります。

```c
/* 案 A: flush の間 peers_mutex を保持し、ピアを ID で再検索する */
lock(peers_mutex);
peer = find_by_id(ctx, target_peer_id);
if (peer != NULL) flush_packed_peer(ctx, peer, packed_len); /* sendto まで */
unlock(peers_mutex);
```

```c
/* 案 B: ウィンドウ登録と宛先の複製までをロック下で行い、sendto はロック外 */
lock(peers_mutex);
peer = find_by_id(...);
/* ウィンドウ登録、dest_addr を局所変数へ複製 */
unlock(peers_mutex);
sendto(複製した宛先);
```

案 C は、ピアごとに参照カウントを持ち、使用中は解放を遅らせます。

**Answer**

選択方式: 単一選択

- [x] A: (推奨) flush の間 `peers_mutex` を保持する。
- [ ] B: ウィンドウ登録と宛先の複製までロックし、`sendto` はロック外で行う。
- [ ] C: ピアごとの参照カウントを導入する。
- [ ] D: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

回答者: Tetsuo Honda / 回答日時: 2026-11-06 00:41:43 JST (検討用 Markdown の保存時刻)

**Recommendation**

推奨: A  
理由: ヘルス チェック スレッドと同じ方式で、変更が最小です。UDP の `sendto` は通常すぐに戻るため、ロック保持時間の増加は小さいと見込みます。ただし、これは見込みであり測定していません。受信処理との競合が問題になる場合は案 B へ切り替えられます。

**Assessment**

単一選択で A のみが選ばれています。ロック保持時間の増加が実用上問題ないかは測定していないため、最終仕様では「見込み」として扱い、結合試験 ([Q-009](#q-009-結合試験の追加範囲) の C) で異常がないことを確認します。

### Q-005 コールバック内からの API 呼び出しへの対策方式

**Status**

解決済み

**Question**

B-2 と B-3 (コールバック内からの API 呼び出しによる停止) に、どの方式で対策しますか。

**Context**

N:1 の受信スレッドは、`peers_mutex` を保持したままコールバックを呼びます。このため、コールバック内から `potr_service_send` を呼ぶとデッドロックします。仮に送信 API がロックを取らないようにしても、キュー満杯時やブロッキング送信では、`peers_mutex` を必要とする送信スレッドを待つため、やはり停止します。  
close はコールバックを呼んだスレッドを join するため、コールバック内から呼ぶと終了しません。  
現在の文書は、`potr_peer_disconnect` についてだけデッドロックを警告しています (`porter_spec.h:310-312`)。

- 案 A は、コールバック実行中の呼び出しを検出し、停止させずにエラーを返します。文書にも制約を記載します。
- 案 B は、文書に制約を記載するだけで、動作は変えません。
- 案 C は、コールバックをロックの外で呼ぶ根本対策です。ただし、状態の更新とイベント通知を同じロック内で行って順序を保つ設計 (`potr_path_event.h:38`) を変えるため、順序保証の再設計が必要です。

**Answer**

選択方式: 単一選択

- [x] A: (推奨) 検出してエラーを返し、文書にも記載する。
- [ ] B: 文書への記載だけにする。
- [ ] C: コールバックをロック外で呼ぶ根本対策を行う。
- [ ] D: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

回答者: Tetsuo Honda / 回答日時: 2026-11-06 00:41:43 JST (検討用 Markdown の保存時刻)

**Recommendation**

推奨: A  
理由: 停止 (原因の特定が難しい) をエラー (原因が分かる) に変えられ、変更範囲が限定的です。案 C はイベント順序の保証に影響するため、今回の範囲を超えると考えます。

**Assessment**

単一選択で A のみが選ばれています。根本対策 (案 C) は今回行わないため、作業全完時に残課題一覧 (backlog) へ記録する候補とします。

**Related**

[Q-006](#q-006-コールバック内呼び出しを検出する-api)、[Q-007](#q-007-コールバック内呼び出しの検出手段)、[Q-008](#q-008-コールバック内呼び出しを検出した場合の戻り値)

### Q-006 コールバック内呼び出しを検出する API

**Status**

解決済み

**Question**

[Q-005](#q-005-コールバック内からの-api-呼び出しへの対策方式) で案 A を選んだ場合、コールバック内からの呼び出しを検出してエラーにする API はどれですか。

**Context**

各 API がコールバック内で停止するかどうかを確認した結果は次のとおりです。

| API | 停止する条件 |
| --- | --- |
| `potr_service_send` | N:1 で停止。TCP ではコールバックからの返信を試験で確認済み (`tcpMultipathTest.cc:481`) |
| `potr_service_close` | 全通信種別で停止 (自スレッドの join) |
| `potr_peer_disconnect` | N:1 で停止 (文書で警告済み) |

Table: コールバック内呼び出しで停止する条件

N:1 以外の `potr_service_send` を検出対象にすると、現在動作しているコールバックからの返信が使えなくなります。

**Answer**

選択方式: 複数選択

- [x] A: (推奨) `potr_service_send` (N:1 のときだけ)
- [x] B: (推奨) `potr_service_close` (全通信種別)
- [x] C: (推奨) `potr_peer_disconnect`
- [ ] D: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

回答者: Tetsuo Honda / 回答日時: 2026-11-06 00:41:43 JST (検討用 Markdown の保存時刻)

**Recommendation**

推奨: A、B、C  
理由: 停止することを確認した組み合わせだけを対象にし、現在動作している TCP のコールバック返信を維持します。  
制約: N:1 以外でのブロッキング送信をコールバック内で行った場合に停止しないかは、まだ確認していません。最終仕様の作成前に確認し、問題があれば追加の Q を提示します。

**Assessment**

複数選択で A、B、C が選ばれています。  
Recommendation の制約に書いた未確認事項は、コードを読んで確認しました。送信スレッドが取得するロックは `send_window_mutex`、`tcp_send_mutex`、`peers_mutex` だけです (`potr_send_thread.c:91, 203, 260, 345`)。N:1 以外のコールバック実行中に、受信側がこれらのロックを保持し続けることはありません。受信側が `send_window_mutex` を取るのは NACK による再送の間だけで、コールバックの前に解放します (`thread_recv_window.c:329-337`)。  
このため、N:1 以外でコールバック内からブロッキング送信しても、プロセス内のロック待ちでは停止しないと判断し、追加の Q は設けません。ただし、確認はコードの読解によるもので、実行による確認はしていません。

**Related**

[Q-005](#q-005-コールバック内からの-api-呼び出しへの対策方式)

### Q-007 コールバック内呼び出しの検出手段

**Status**

回答済み (前提訂正のため Q-010 で再確認)

**Question**

[Q-005](#q-005-コールバック内からの-api-呼び出しへの対策方式) で案 A を選んだ場合、コールバック内からの呼び出しをどの手段で検出しますか。

**Context**

cplat には、現在のスレッドを識別する API がありません (`cplat/sync/sync.h` のスレッド API は create、join、detach、yield だけです)。  
一方、cplat の `compiler.h` には、スレッドごとの変数を定義する `THREAD_LOCAL` マクロがあります (GCC は `_Thread_local`、MSVC は `__declspec(thread)`)。

```c
/* 案 A: porter 内部のスレッドごとの変数に、コールバック実行中のハンドルを記録する */
static THREAD_LOCAL const potr_context *s_callback_ctx;

/* potr_internal_callback_emit_locked */
prev = s_callback_ctx;
s_callback_ctx = ctx;
ctx->callback(...);
s_callback_ctx = prev;

/* 各 API の入口 */
if (s_callback_ctx == ctx) return POTR_ERR_UNSUPPORTED;
```

案 B は、cplat に現在のスレッドを識別する API を追加し、porter のハンドルにコールバックを呼ぶスレッドを記録して比較します。cplat の公開 API と試験の追加が必要です。

**Answer**

選択方式: 単一選択

- [ ] A: (推奨) cplat の `THREAD_LOCAL` を使った porter 内部の変数で検出する。
- [x] B: cplat に現在のスレッドを識別する API を追加する。
- [ ] C: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

回答者: Tetsuo Honda / 回答日時: 2026-11-06 00:41:43 JST (検討用 Markdown の保存時刻)

**Recommendation**

推奨: A  
理由: 既存のマクロで実現でき、cplat の公開 API を増やしません。別のハンドルのコールバック内から呼んだ場合は検出しないため、異なるハンドル間の正当な呼び出しを妨げません。

**Assessment**

単一選択で B が選ばれています (推奨の A とは異なります)。  
回答の検証中に、この質問の Context の誤りが分かりました。Context には「cplat には、現在のスレッドを識別する API がありません」と書きましたが、実際には `cplat_process_get_tid()` (`cplat/runtime/process.h:83`) が公開されています。`cplat/sync/sync.h` だけを調べ、`cplat/runtime/` を確認していなかったことが原因です。  
B は誤った前提のもとで選ばれた可能性があるため、既存 API の利用を [Q-010](#q-010-既存の-cplat_process_get_tid-の利用) で確認し直します。この Q の回答は記録として残し、変更しません。

**Related**

[Q-005](#q-005-コールバック内からの-api-呼び出しへの対策方式)、[Q-010](#q-010-既存の-cplat_process_get_tid-の利用)

### Q-008 コールバック内呼び出しを検出した場合の戻り値

**Status**

解決済み

**Question**

[Q-005](#q-005-コールバック内からの-api-呼び出しへの対策方式) で案 A を選んだ場合、検出時に返す戻り値はどれにしますか。

**Context**

既存の `POTR_ERR_UNSUPPORTED` は「現在の通信種別または状態では操作がサポートされていません」と定義されています (`porter_result.h:63`)。  
新しい結果コードを追加する場合は、`porter_result.h`、`docs/api.md` の戻り値表、porter コーディング規範の結果コードの記述の更新が必要です。

**Answer**

選択方式: 単一選択

- [x] A: (推奨) 既存の `POTR_ERR_UNSUPPORTED` を返す。
- [ ] B: 新しい結果コードを追加する。
- [ ] C: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

回答者: Tetsuo Honda / 回答日時: 2026-11-06 00:41:43 JST (検討用 Markdown の保存時刻)

**Recommendation**

推奨: A  
理由: 「コールバック実行中という状態では操作できない」は、既存の定義に当てはまります。利用者が対応すべき処理も、他の未サポート状態と変わりません。

**Assessment**

単一選択で A のみが選ばれています。`potr_peer_disconnect` はすでに `POTR_ERR_UNSUPPORTED` を戻り値に持ちます。`potr_service_send` と `potr_service_close` は戻り値表に `POTR_ERR_UNSUPPORTED` がないため、Doxygen と `docs/api.md` の戻り値表に追加します。

**Related**

[Q-005](#q-005-コールバック内からの-api-呼び出しへの対策方式)

### Q-009 結合試験の追加範囲

**Status**

解決済み

**Question**

今回追加する結合試験 (`test/src/integration/porterSendRecvTest/`) の範囲はどれですか。

**Context**

単体テストは、変更する関数ごとに追加・更新します。並行性の不具合は単体テストでは再現しにくいため、実際の送受信を伴う結合試験の追加を検討します。  
結合試験はスレッドの実行順に左右されるため、繰り返し回数を増やすほど試験時間が延びます。ビルドと試験の実行はユーザーが行います。

**Answer**

選択方式: 複数選択

- [x] A: (推奨) 複数スレッドから同一ハンドルで分割と圧縮を伴うメッセージを送り、受信側で全メッセージが壊れずに届くことを確認する。
- [x] B: (推奨) 非ブロッキング送信を流し続ける間に、ブロッキング送信が復帰することを確認する。
- [x] C: (推奨) N:1 で大量送信中にピア切断を繰り返し、異常終了しないことを確認する (B-1)。
- [x] D: (推奨) コールバック内からの対象 API 呼び出しがエラーを返し、停止しないことを確認する (B-2/B-3)。
- [ ] E: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

回答者: Tetsuo Honda / 回答日時: 2026-11-06 00:41:43 JST (検討用 Markdown の保存時刻)

**Recommendation**

推奨: A、B、C、D  
理由: 変更する 4 つの振る舞いを、それぞれ実際の送受信で確認できます。C は競合の発生が確率的なため、異常が起きないことの確認にとどまります。ThreadSanitizer などの検査ツールでの実行は、環境の準備が必要なため、この試験範囲には含めていません。

**Assessment**

複数選択で A、B、C、D が選ばれています。D の対象 API は [Q-006](#q-006-コールバック内呼び出しを検出する-api) の回答 (N:1 の送信、close、ピア切断) に従います。

### Q-010 既存の cplat_process_get_tid の利用

**Status**

解決済み

**Question**

コールバック内呼び出しの検出に、cplat の既存 API `cplat_process_get_tid()` を使いますか。

**Context**

[Q-007](#q-007-コールバック内呼び出しの検出手段) では「cplat には現在のスレッドを識別する API がない」という誤った前提のもとで、「cplat に API を追加する」(B) が選ばれました。実際には、次の公開 API がすでにあります。

- 宣言は [process.h](../../../cplat/prod/include/cplat/runtime/process.h) の 83 行目にある `uint32_t cplat_process_get_tid(void)` です。
- 説明には「同時に存在するスレッドの間で一意」「終了したスレッドの識別子は再び割り当てられる場合がある」と書かれています。Linux はカーネルのスレッド ID (`syscall(SYS_gettid)`) を返します。
- 説明は「ログ出力など、診断目的で」使う例を挙げていますが、用途を診断だけに限る記述はありません。実際に、[string-catalog-filter-sample.c](../../../string-catalog-sample/prod/src/cmd/string-catalog-filter-sample/string-catalog-filter-sample.c) の 572 行目と 608 行目が、記録したスレッド ID と現在のスレッドを比較する用途で使っています。

porter での使い方は次のとおりです。コールバックは `callback_mutex` の下で 1 つずつ呼ばれるため、記録する場所はハンドルごとに 1 つで足ります。比較は、コールバックを実行中のスレッドが生存している間だけ行うため、ID の再割り当ては問題になりません。

```c
/* 案 A: 既存 API を使う (cplat の変更なし) */
/* potr_internal_callback_emit_locked (callback_mutex 保持中) */
cplat_atomic_store_u32(&ctx->callback_tid, cplat_process_get_tid(), ...);
cplat_atomic_store_i32(&ctx->callback_active, 1, ...);
ctx->callback(...);
cplat_atomic_store_i32(&ctx->callback_active, 0, ...);

/* 各 API の入口 */
if (cplat_atomic_load_i32(&ctx->callback_active, ...) != 0 &&
    cplat_atomic_load_u32(&ctx->callback_tid, ...) == cplat_process_get_tid())
{
    return POTR_ERR_UNSUPPORTED;
}
```

案 B は、既存 API とは別に、`cplat/sync/sync.h` へスレッド識別用の新しい API (例: `cplat_thread_get_current_id()`) を追加します。用途は明確になりますが、同じ値を返す関数が 2 つになります。また、mock_cplat、エクスポート テスト、API 逆引き表 (`api-cheatsheet.md`)、機能仕様の更新が必要になり、別サブモジュール (`app/cplat`) にも変更が及びます。

案 C は、Q-007 の A (cplat の `THREAD_LOCAL` マクロを使った porter 内部の変数) に戻します。

**Answer**

選択方式: 単一選択

- [x] A: (推奨) 既存の `cplat_process_get_tid()` を使い、cplat は変更しない。
- [ ] B: 既存 API とは別に、cplat に新しいスレッド識別 API を追加する。
- [ ] C: Q-007 の A (`THREAD_LOCAL` を使った porter 内部の変数) に戻す。
- [ ] D: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足: cplat 側の説明を汎用的にする作業も同時に計画すること。

回答者: Tetsuo Honda / 回答日時: 2026-11-06 00:51:58 JST (検討用 Markdown の保存時刻)

**Recommendation**

推奨: A  
理由: Q-007 で B を選んだ意図 (cplat のスレッド識別機能を使う) を、cplat を変更せずに満たせます。同じ値を返す API の重複を避けられ、リポジトリ内にも同じ用途の前例があります。  
制約: 案 A を選ぶ場合も、`cplat_process_get_tid()` の Doxygen にある「診断目的」の例示が実際の用途と合わないと判断するなら、cplat の説明文の修正が別に必要になります。その要否は、補足欄で指示してください。

**Assessment**

単一選択で A のみが選ばれ、補足で cplat 側の説明の汎用化を同じ案件で計画するよう指示されています。Recommendation の制約に書いた「説明文の修正の要否」は、この補足で「要」と確定しました。  
汎用化の対象を調べたところ、「診断目的」に限った記述は次の 3 か所にありました。

| 箇所 | 現在の記述 |
| --- | --- |
| [process.h](../../../cplat/prod/include/cplat/runtime/process.h) 74 行目 (TID)、61 行目 (PID) | 「ログ出力など、診断目的で…使用します」 |
| [api-cheatsheet.md](../../../cplat/docs/api-cheatsheet.md) 386 行目 (TID) | 「診断目的の識別子です」 |
| [runtime.md](../../../cplat/docs/functional-spec/runtime.md) 35-36 行目の要件 `CPLAT-RUNTIME-FUNC-010` <!-- cplat-req: uuid=0e823b69-8e80-457a-997a-60fb0fdecc79 --> (PID)、`CPLAT-RUNTIME-FUNC-011` <!-- cplat-req: uuid=7f2bb4ce-766b-4fde-8577-ab1c9aecdb3c --> (TID) と 72-75 行目の本文 | 「診断で識別するための数値を取得します」 |

Table: cplat で用途を診断に限っている記述

機能仕様の要件文も用途を限っているため、汎用化すると要件文が変わります。PID も同じ記述を持つため、対象の範囲を [Q-011](#q-011-cplat-の説明を汎用化する対象-api)、要件 ID と UUID の扱いを [Q-012](#q-012-cplat-の機能要件-func-011-の-id-と-uuid-の扱い) で確認します。

**Related**

[Q-007](#q-007-コールバック内呼び出しの検出手段)、[Q-005](#q-005-コールバック内からの-api-呼び出しへの対策方式)、[Q-011](#q-011-cplat-の説明を汎用化する対象-api)、[Q-012](#q-012-cplat-の機能要件-func-011-の-id-と-uuid-の扱い)

### Q-011 cplat の説明を汎用化する対象 API

**Status**

解決済み

**Question**

cplat の説明を汎用化する対象を、スレッド ID (`cplat_process_get_tid`) だけにしますか、プロセス ID (`cplat_process_get_pid`) も含めますか。

**Context**

[Q-010](#q-010-既存の-cplat_process_get_tid-の利用) の補足により、cplat 側の説明を汎用化します。今回 porter が必要とするのはスレッド ID だけです。  
一方、プロセス ID も同じく「診断目的」に限った記述を持ちます (`process.h:61`、要件 `CPLAT-RUNTIME-FUNC-010` <!-- cplat-req: uuid=0e823b69-8e80-457a-997a-60fb0fdecc79 -->)。また、機能仕様の「プロセスとスレッドの識別」節 (`runtime.md:72-82`) は、2 つの識別子をまとめて説明しています。

```text
案 A (TID のみ) の場合の機能仕様の本文 (例)
実行中のプロセスを診断で識別するための数値と、実行中のスレッドを識別するための数値を取得できます。
スレッドの識別子は、記録の切り分けに加え、ある処理が特定のスレッド上で実行中かどうかの判定にも使用できます。
```

```text
案 B (PID と TID) の場合の機能仕様の本文 (例)
実行中のプロセスと、その中で実行中のスレッドを識別するための数値をそれぞれ取得できます。
記録の切り分けに加え、実行中の処理がどのプロセスやスレッドに属するかの判定にも使用できます。
```

**Answer**

選択方式: 単一選択

- [ ] A: (推奨) スレッド ID だけを汎用化する。
- [x] B: プロセス ID も含めて汎用化する。
- [ ] C: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

**Recommendation**

推奨: A  
理由: 今回の変更の根拠となる新しい用途 (実行中のスレッドの判定) は、スレッド ID にしかありません。プロセス ID の用途を広げる根拠は、今回の検討の中にはありません。  
制約: A では、まとめて説明している節の文が、プロセスとスレッドで書き分けた形になります (案 A の例を参照)。

**Assessment**

回答者: Tetsuo Honda / 回答日時: 2026-11-06 00:55:21 JST (検討用 Markdown の保存時刻)  
単一選択で B が選ばれています (推奨の A とは異なります)。プロセス ID の Doxygen (`process.h:61`) と要件 `CPLAT-RUNTIME-FUNC-010` <!-- cplat-req: uuid=0e823b69-8e80-457a-997a-60fb0fdecc79 --> も汎用化の対象になります。[Q-012](#q-012-cplat-の機能要件-func-011-の-id-と-uuid-の扱い) の制約に従い、`CPLAT-RUNTIME-FUNC-010` <!-- cplat-req: uuid=0e823b69-8e80-457a-997a-60fb0fdecc79 --> にも ID と UUID の維持を適用します。

**Related**

[Q-010](#q-010-既存の-cplat_process_get_tid-の利用)、[Q-012](#q-012-cplat-の機能要件-func-011-の-id-と-uuid-の扱い)

### Q-012 cplat の機能要件 FUNC-011 の ID と UUID の扱い

**Status**

解決済み

**Question**

要件 `CPLAT-RUNTIME-FUNC-011` <!-- cplat-req: uuid=7f2bb4ce-766b-4fde-8577-ab1c9aecdb3c --> の文から「診断で」を外す変更で、要件 ID と UUID を維持しますか。

**Context**

現在の要件文は「cplat の実行時支援機能は、現在のスレッドを診断で識別するための数値を取得します。」です。  
[機能仕様の記載規範](../../../general/docs/functional-spec-guideline.md) の「要件 ID と UUID の変更」節 (208-217 行) は、次のように定めています。

- 意味を保った表現修正と要件の具体化では、UUID を維持します。
- 別の論理要件に置き換える場合は、既存の ID と UUID を廃止し、新しい ID と UUID を付与します。

「診断で」を外すと、取得する対象 (現在のスレッドの識別子) は変わらず、想定する用途だけが広がります。この変更が「同じ論理要件の表現修正」と「別の論理要件への置き換え」のどちらに当たるかは、規範に明示されていません。  
なお、要件 `CPLAT-RUNTIME-FUNC-011` <!-- cplat-req: uuid=7f2bb4ce-766b-4fde-8577-ab1c9aecdb3c --> を参照する下流成果物 (コード コメントや試験) があれば、ID を変える場合はそれらも更新が必要です。参照の有無は、最終仕様の作成時に `check_functional_spec.py` と検索で確認します。

```text
案 A: ID と UUID を維持し、要件文だけを変える
| `CPLAT-RUNTIME-FUNC-011` <!-- cplat-req: uuid=7f2bb4ce-766b-4fde-8577-ab1c9aecdb3c --> | cplat の実行時支援機能は、現在のスレッドを識別するための数値を取得します。 |
```

```text
案 B: 既存の ID と UUID を廃止し、新しい ID (例: FUNC-0xx) と UUID を付与する
| (新しい要件 ID と新しい UUID) | cplat の実行時支援機能は、現在のスレッドを識別するための数値を取得します。 |
```

**Answer**

選択方式: 単一選択

- [x] A: (推奨) 同じ論理要件の表現修正として、ID と UUID を維持する。
- [ ] B: 別の論理要件として、既存の ID と UUID を廃止し、新しく付与する。
- [ ] C: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

**Recommendation**

推奨: A  
理由: 要件の中心である「現在のスレッドの識別子を取得する」は変わらず、用途の限定を外すだけのためです。関数、戻り値、振る舞いも変わりません。  
制約: [Q-011](#q-011-cplat-の説明を汎用化する対象-api) で B を選んだ場合は、`CPLAT-RUNTIME-FUNC-010` <!-- cplat-req: uuid=0e823b69-8e80-457a-997a-60fb0fdecc79 --> (PID) にも同じ扱いを適用します。

**Assessment**

回答者: Tetsuo Honda / 回答日時: 2026-11-06 00:55:21 JST (検討用 Markdown の保存時刻)  
単一選択で A のみが選ばれています。[Q-011](#q-011-cplat-の説明を汎用化する対象-api) で B が選ばれたため、`CPLAT-RUNTIME-FUNC-010` <!-- cplat-req: uuid=0e823b69-8e80-457a-997a-60fb0fdecc79 --> と `CPLAT-RUNTIME-FUNC-011` <!-- cplat-req: uuid=7f2bb4ce-766b-4fde-8577-ab1c9aecdb3c --> の両方で ID と UUID を維持します。  
両要件を参照する下流成果物を検索したところ、履歴文書 `app/cplat/docs/proposals/string-catalog-trace-kind-design.md` (454-455 行) だけでした。この文書は要件追加時の記録のため、変更しません。

**Related**

[Q-010](#q-010-既存の-cplat_process_get_tid-の利用)、[Q-011](#q-011-cplat-の説明を汎用化する対象-api)

## 対象成果物と合意版

最終決定事項 (最終仕様) の設計書: `261106-porter-handle-concurrency-design.md`

検討用 Markdown の合意版 (第 1 ゲート): 版 3 (2026-11-06、[Q-001](#q-001-作業の実施順) から [Q-012](#q-012-cplat-の機能要件-func-011-の-id-と-uuid-の扱い) の解決をもって確定)

最終仕様 (design.md) の合意版 (第 2 ゲート): 未承認

更新予定の成果物: 最終仕様の「対象ファイル」節に記載 (作業 WBS の項目 4 に対応)

受入条件: 最終仕様の「受入条件」節に記載

## 反映と検証の記録

未着手。
