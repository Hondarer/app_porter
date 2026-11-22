<!--
生成元: agree-before-implement スキル
文書種別: 検討用 Markdown (discussion)
最終更新日時: 2026-11-18 12:31:04 JST
最終更新エージェント: claude code / セッション ID: session_01KJc53TxVv1vjB6xMfjiUgc
稼働 OS: Windows
上流文書: `../../../../general/docs/prompts/past/261115-local-lock-unlock-bl005-006-backlog.md` (対策元の残課題一覧。BL-004 が対象)。削除されている場合があります。
下流文書: `261118-porter-thread-stop-cleanup-design.md` (最終仕様)、`261118-porter-thread-stop-cleanup-backlog.md` (残課題一覧)。未作成の場合や、削除されている場合があります。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。ユーザーが編集した最新版を読み直し、作業 WBS の `← **CURRENT**` の項目から再開します。最終更新エージェントのセッション情報や稼働 OS・最終更新日時が参照可能であれば再開時のコンテキストや環境差分の把握に利用し、別エージェントや別環境・別 OS からの引き継ぎ、担当変更によりセッションが見つからない場合も異常とせず、文書の記載から継続してください。`← **CURRENT**` の項目は処理が中止された項目である場合があり、どこまで進んでいたかは文書だけでは分かりません。対象成果物の内容を確認して実施済みの範囲を特定してから、作業を再開してください。`← **CURRENT**` がなく、作業 WBS がすべて完了 (✅) であれば、この案件の合意フローは完了しています。
-->

# porter のスレッド停止関数と資源保持の分岐の整理の検討

ファイル名: `261118-porter-thread-stop-cleanup-discussion.md`

## 検討の識別

検討 ID: 261118-porter-thread-stop-cleanup

版: 3

状態: 合意済み (版 3)。最終仕様の版 1 は 2026-11-18 に承認済み。成果物は 2026-11-18 に承認済み (完了)

## 作業 WBS と進捗管理

この検討用 Markdown が管理する作業 WBS です。対策元の残課題一覧 (`app/general/docs/prompts/past/261115-local-lock-unlock-bl005-006-backlog.md`) の BL-004 を対象とし、進行の各段階で対策元の状態と経緯も更新します。  
項目 4 の内訳は、版 3 で確定しました。

凡例: ⏸️ 未着手、🔄 実施中、🚫 ブロック中、⚠️ 要注意、✅ 完了。現在取り組んでいる項目の末尾に `← **CURRENT**` を付けます。

- ✅ 1. 検討用 Markdown のための情報収集と質問の実施
    - ✅ 1.1. 前提条件と要求事項の調査 (停止関数 5 つ、呼び出し元、`api_context_has_threads()`、ハンドルの状態、単体テストと mock の読解。対策元 backlog の BL-004 を「検討中」へ更新)
    - ✅ 1.2. 課題・論点の抽出と明確化のための確認項目 ([Q-001](#q-001-停止関数の戻り値の型) から [Q-006](#q-006-局所検証の担当と範囲)、版 2 で [Q-007](#q-007-終了の再試行の記述の扱い)) の作成
    - ✅ 1.3. Q&A ターンごとのユーザー回答の受付と記録 (JST 日時、Git ユーザー名または発言者)。版 2 で [Q-001](#q-001-停止関数の戻り値の型) から [Q-006](#q-006-局所検証の担当と範囲)、版 3 で [Q-007](#q-007-終了の再試行の記述の扱い) を記録
    - ✅ 1.4. 回答に基づく前提・制約の検証と WBS の見直し (版 3。合意方針を対策元 backlog の BL-004 へ反映)
- ✅ 2. 前提合意と残懸念の解消 (第 1 ゲート: 最終仕様作成への着手)
    - ✅ 2.1. 全論点に対する未解決事項および質問の解消確認
    - ✅ 2.2. 方針・前提条件の合意確定 (版 3。懸念がないことをもってフェーズ 2 へ着手)
- ✅ 3. 最終決定事項 (最終仕様) の作成と承認の受付
    - ✅ 3.1. `261118-porter-thread-stop-cleanup-design.md` の作成 (対策元 backlog の BL-004 へ確定方針を反映)
    - ✅ 3.2. 最終仕様における受入条件と整合性の確認
    - ✅ 3.3. 対話セッションにおける最終仕様 (design.md) の承認の受付 (第 2 ゲート: 作業着手の合意)。2026-11-18、チャットでユーザーが版 1 を承認 (「承認」)
- ✅ 4. 承認された最終仕様に従った作業の実施
    - ✅ 4.1. 停止関数 5 つの宣言と実装の変更 (`prod/include_internal/porter/thread/potr_health_thread.h`、`potr_recv_thread.h`、`prod/include_internal/porter/potr_notify.h` と、対応する `.c`)
    - ✅ 4.2. 呼び出し元の分岐の整理 (`prod/libsrc/porter/api/potr_service_close.c`、`potr_service_open.c`、`prod/libsrc/porter/thread/potr_connect_thread.c`)。`close_impl()` の重複した検査の削除とトレース出力の移動を含む
    - ✅ 4.3. `api_context_has_threads()` の削除 (`prod/libsrc/porter/api/api_context_cleanup.c/.h`)
    - ✅ 4.4. ハンドルの状態 `CLOSE_FAILED` と引数 `closed` の削除 (`prod/libsrc/porter/potr_handle.c`、`prod/include_internal/porter/potr_handle.h`)
    - ✅ 4.5. mock と単体テストの更新 (`test/include/mock_porter.h`、`test/libsrc/mock_porter/`、`potrServiceCloseTest`、`apiContextCleanupTest`、`potrHealthThreadTest`、`potrRecvThreadTest`、`potrNotifyTest`、`threadRecvWindowRejectTest`、`potrHandleTest` ほか)
    - ✅ 4.6. 文書の変更 (`docs/functional-spec/service.md`、`prod/include/porter/porter_spec.h`、`docs/api.md`、`docs/architecture.md` の記述の削除)
    - ✅ 4.7. 正本ドキュメントおよびコード コメントへの永続的知見の反映 (停止関数の Doxygen、`docs/coding-guideline.md`)
    - ✅ 4.8. 局所テストおよび機械的検証 (Windows と WSL の `app/porter` の `make test`、`.warn`、`check_functional_spec.py`、`text_style_jp.py`。対策元 backlog の BL-004 を更新)
    - ✅ 4.9. 受入条件への適合確認 (全完確認)。受入条件 6 は、ユーザーの指示で既存の 35 件を残課題一覧の BL-001 へ回し、この案件で問題を増やさないことで確認
    - ✅ 4.10. 残課題一覧 (`261118-porter-thread-stop-cleanup-backlog.md`) の再構築と整理、および対策元 backlog の BL-004 の最終状態の確定
- ✅ 5. 成果物の承認の受付 (第 3 ゲート)
    - ✅ 5.1. 成果物・検証結果・残課題の提示 (2026-11-18、対話で提示)
    - ✅ 5.2. 対話セッションにおける成果物の承認の受付 (「承認」または「完了」の受領によるスキル完了)。2026-11-18、チャットでユーザーが承認 (「承認」)

## 対象と参照資料

対象成果物: porter の内部のスレッド停止関数と、その呼び出し元にある「スレッドが残った場合に資源を保持する」分岐、および対応する mock と単体テスト

今回の対象範囲: 対策元の残課題一覧の BL-004、`close_impl()` の重複した検査の削除 ([Q-004](#q-004-close_impl-の重複した検査の扱い) の B)、終了の再試行の記述の削除 ([Q-007](#q-007-終了の再試行の記述の扱い) の A)。公開関数のシグネチャは変えません。

書き込み可能な検討用 Markdown: この文書 (`261118-porter-thread-stop-cleanup-discussion.md`)

参照資料と版: porter `4abc542`、cplat `19302982` (2026-11-18 時点の `main`)

## 調査結果

### 前提となる cplat の契約

`cplat_thread_join()` は戻り値のない関数です (`app/cplat/prod/include/cplat/sync/sync.h`)。対策元の残課題一覧によると、join の OS の失敗はプロセスの異常終了として扱います。

### 停止関数の現状

| 関数 | 戻り値 | 失敗を返す条件 | join の後の処理 |
| --- | --- | --- | --- |
| `potr_internal_health_thread_stop()` | `int` | `ctx == NULL` | ハンドルと同期資源を `NULL` へ戻す |
| `potr_internal_tcp_health_thread_stop()` | `int` | `ctx == NULL`、`path_idx` が範囲外 | ハンドルを `NULL` へ戻す |
| `potr_internal_comm_recv_thread_stop()` | `int` | `ctx == NULL` | ハンドルを `NULL` へ戻す |
| `potr_internal_tcp_recv_thread_stop()` | `int` | `ctx == NULL`、`path_idx` が範囲外 | ハンドルを `NULL` へ戻す |
| `potr_internal_notify_stop()` | `int` | `ctx == NULL` | 通知資源を解放する |
| `potr_internal_send_thread_stop()` | `void` | なし | ハンドルとロックを `NULL` へ戻す |
| `potr_internal_connect_thread_stop()` | `void` | なし (`ctx == NULL` は無処理) | ハンドルを `NULL` へ戻す |

Table: 停止関数の現状

製品コードの呼び出し元は、どれも有効な `ctx` と、`0` から `n_path - 1` の `path_idx` を渡しています。このため、`int` を返す 5 つの関数が失敗を返す経路には、製品コードからは到達しません。

### 到達しない分岐

| 箇所 | 内容 | 到達しない理由 |
| --- | --- | --- |
| `potr_service_close.c` の `close_impl()` (TCP) | `potr_internal_connect_thread_stop()` の後の `api_context_has_threads()` で `POTR_ERR_UNKNOWN` を返す | 停止関数が connect、recv、health、send のハンドルをすべて `NULL` へ戻す |
| 同 (非 TCP、3 か所) | `potr_internal_health_thread_stop()` と `potr_internal_comm_recv_thread_stop()` の結果で `POTR_ERR_UNKNOWN` を返す | 有効な `ctx` を渡している |
| 同 (非 TCP) | `potr_internal_send_thread_stop()` の後に `send_thread != NULL` なら `POTR_ERR_UNKNOWN` を返す | 停止関数がハンドルを `NULL` へ戻す |
| `potr_service_close()` | `close_impl()` の後の `api_context_has_threads()` で、資源を解放せずに戻る | `close_impl()` が全スレッドを停止する |
| 同 | `potr_internal_notify_stop()` の結果で `POTR_ERR_UNKNOWN` を返す | 有効な `ctx` を渡している |
| `potr_service_open.c` の `ctx_cleanup()` | `api_context_has_threads()` で、コンテキストを解放せずに戻る | 起動に失敗した各経路が、起動済みのスレッドを停止してから戻る (下記) |
| 同 | `potr_internal_notify_stop()` の結果で戻る | 有効な `ctx` を渡している |
| `start_threads_tcp()` | `potr_internal_connect_thread_start()` の失敗後の `api_context_has_threads()` | 起動関数が、失敗時に `potr_internal_connect_thread_stop()` を呼んでから戻る |
| `start_threads_udp()` (2 か所) | 送信スレッドの停止後の `api_context_has_threads()` | この経路で起動済みのスレッドは、直前に停止したものだけ |
| 同 | `potr_internal_health_thread_stop()` の結果で戻る | 有効な `ctx` を渡している |
| `potr_internal_connect_thread_stop()` | 従属スレッドの停止関数の結果で戻る。`send_thread != NULL` で戻る | 範囲内の `path_idx` を渡している。停止関数がハンドルを `NULL` へ戻す |

Table: 到達しない分岐 (15 か所)

`start_threads_udp()` の受信専用の経路 (`potr_internal_comm_recv_thread_start()` の失敗) では、スレッドは 1 つも起動していません。

### テストと mock の現状

- `potrServiceCloseTest` の `incomplete_join_retains_shared_resources` は、mock で `api_context_has_threads()` に 1 を返させ、資源を解放しないことと `POTR_ERR_UNKNOWN` を確認しています。製品コードでは起こらない状態です。
- `apiContextCleanupTest` に、`api_context_has_threads()` の単体テストが 1 件あります。
- 停止関数の戻り値を確認している単体テストは、`potrHealthThreadTest` (5 か所)、`potrRecvThreadTest` (2 か所)、`potrNotifyTest` (3 か所)、`threadRecvWindowRejectTest` (6 か所) です。
- `test/libsrc/mock_porter/` に、停止関数の mock が 6 つと `api_context_has_threads()` の mock が 1 つあります。

### 関連して分かったこと

- ハンドル管理 (`prod/libsrc/porter/potr_handle.c`) の状態 `2: CLOSE_FAILED` は、`potr_internal_handle_end_close(ctx, 0)` だけが設定します。この呼び出しは、上の表の `potr_service_close()` の 2 か所だけにあります ([Q-003](#q-003-ハンドルの状態-close_failed-の扱い))。
- `close_impl()` の先頭にある「自サービスのコールバック内からの呼び出し」の検査は、`potr_internal_handle_begin_close()` が先に同じ検査で拒否するため、`potr_service_close()` からは到達しません ([Q-004](#q-004-close_impl-の重複した検査の扱い))。
- 版 1 では「`docs/api.md`、`docs/functional-spec/`、公開ヘッダーには、終了待ちの失敗やスレッドが残る場合の記述はない」と書きましたが、誤りでした (「終了待ち」「join」だけで検索したため)。「終了処理が失敗した場合は終了を再試行できる」という記述が、機能仕様、公開ヘッダー、`docs/api.md`、`docs/architecture.md` の 4 か所にあります ([Q-007](#q-007-終了の再試行の記述の扱い))。
- `test/src/libporter/` の各テスト ディレクトリにある `potr_handle.c` や `potr_service_close.c` は、ビルドが製品のソースから取り込む Git 管理外のファイルです。変更の対象は製品のソースだけです。
- ハンドル管理の単体テスト `potrHandleTest` の `close_failure_allows_only_retry_and_closed_value_is_not_reused` は、`CLOSE_FAILED` の後の再試行を確認しています。

## 現行の合意内容

- 対象は、対策元の残課題一覧の BL-004 です (2026-11-18 の対話でユーザーが着手を指示)。
- 検討用 Markdown は porter に置きます (同日の対話でユーザーが指示)。

- 5 つの停止関数を `void` にします ([Q-001](#q-001-停止関数の戻り値の型) の A)。
- `api_context_has_threads()` と、それを使う分岐、mock、関連する単体テストを削除します ([Q-002](#q-002-api_context_has_threads-の扱い) の A)。
- `close_impl()` の重複した検査を削除し、トレース出力を `potr_service_close()` の拒否の経路へ移します ([Q-004](#q-004-close_impl-の重複した検査の扱い) の B)。
- 失敗しない前提を、内部ヘッダーの Doxygen と porter のコーディング規範に記録します ([Q-005](#q-005-失敗しない前提の記録先) の A)。
- 局所検証は、エージェントが Windows と WSL の両方で実行します ([Q-006](#q-006-局所検証の担当と範囲) の A)。

- ハンドルの状態 `CLOSE_FAILED` と、`potr_internal_handle_end_close()` の引数 `closed` を削除します ([Q-003](#q-003-ハンドルの状態-close_failed-の扱い) の A)。
- 「終了処理が失敗した場合は終了を再試行できる」という記述を、機能仕様、公開ヘッダー、`docs/api.md`、`docs/architecture.md` から削除します ([Q-007](#q-007-終了の再試行の記述の扱い) の A)。

## 観点別の確認

| 観点 | 状態 | 根拠・関連 Q |
| --- | --- | --- |
| 目的・利用者 | 確認済み | 対策元の BL-004 の「本来あるべき姿」。利用者は porter の保守者 |
| 範囲・境界 | 確認済み | [Q-003](#q-003-ハンドルの状態-close_failed-の扱い) の A、[Q-004](#q-004-close_impl-の重複した検査の扱い) の B、[Q-007](#q-007-終了の再試行の記述の扱い) の A |
| 入力・根拠 | 確認済み | 「調査結果」 |
| 整合性・依存関係 | 確認済み | [Q-001](#q-001-停止関数の戻り値の型) の A、[Q-002](#q-002-api_context_has_threads-の扱い) の A、[Q-005](#q-005-失敗しない前提の記録先) の A、[Q-007](#q-007-終了の再試行の記述の扱い) の A |
| 実現性・例外 | 確認済み | 到達しない分岐は 15 か所すべてで理由を確認済み。公開 API は変わらない |
| 受入条件・検証 | 確認済み | [Q-006](#q-006-局所検証の担当と範囲) の A |

## 質問一覧

| Q-ID | 論点 | 状態 | 依存・関連 |
| --- | --- | --- | --- |
| [Q-001](#q-001-停止関数の戻り値の型) | 停止関数の戻り値の型 | 解決済み (A) | なし |
| [Q-002](#q-002-api_context_has_threads-の扱い) | `api_context_has_threads()` の扱い | 解決済み (A) | なし |
| [Q-003](#q-003-ハンドルの状態-close_failed-の扱い) | ハンドルの状態 `CLOSE_FAILED` の扱い | 解決済み (A) | [Q-002](#q-002-api_context_has_threads-の扱い)、[Q-007](#q-007-終了の再試行の記述の扱い) |
| [Q-004](#q-004-close_impl-の重複した検査の扱い) | `close_impl()` の重複した検査の扱い | 解決済み (B) | なし |
| [Q-005](#q-005-失敗しない前提の記録先) | 失敗しない前提の記録先 | 解決済み (A) | [Q-001](#q-001-停止関数の戻り値の型) |
| [Q-006](#q-006-局所検証の担当と範囲) | 局所検証の担当と範囲 | 解決済み (A) | なし |
| [Q-007](#q-007-終了の再試行の記述の扱い) | 終了の再試行の記述の扱い | 解決済み (A) | [Q-003](#q-003-ハンドルの状態-close_failed-の扱い) |

## 明確化のための確認項目

### Q-001 停止関数の戻り値の型

**Status**

解決済み

**Question**

`int` を返す 5 つの停止関数の戻り値を、どの形にしますか。

**Context**

5 つの停止関数が失敗を返すのは、引数の誤り (`ctx == NULL`、`path_idx` が範囲外) だけです。製品コードの呼び出し元は誤った引数を渡さないため、呼び出し元の戻り値の検査には到達しません。同じ役割の `potr_internal_send_thread_stop()` と `potr_internal_connect_thread_stop()` は、すでに `void` です。

現状の呼び出し元の例です。

```c
if (potr_internal_health_thread_stop(ctx) != POTR_OK)
{
    return POTR_ERR_UNKNOWN;
}
```

案 A では、呼び出し元は次の形になります。停止関数は、`ctx == NULL` と範囲外の `path_idx` を無処理で戻します (`potr_internal_connect_thread_stop()` と同じ扱い)。

```c
potr_internal_health_thread_stop(ctx);
```

案 B では、関数の宣言は変えず、呼び出し元の検査だけを `(void)` に置き換えます。

```c
(void)potr_internal_health_thread_stop(ctx);
```

案 A は、mock 4 つの型 (`potr_internal_notify_stop()` には mock がありません) と、戻り値を確認している単体テスト 16 か所の書き換えが必要です。案 B は、テストの変更が少なく済みますが、「失敗しうる関数の結果を捨てている」ように見える呼び出しが残ります。

**Answer**

選択方式: 単一選択

- [x] A: (推奨) 5 つとも `void` にし、不正な引数は無処理で戻します。
- [ ] B: `int` のまま、戻り値を引数の誤りだけに限定し、呼び出し元は結果を使いません。
- [ ] C: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

回答者: Tetsuo Honda / 回答日時: 2026-11-18 06:34:13 JST (検討用 Markdown への記入)

**Assessment**

A の選択を確認しました。5 つの停止関数を `void` にし、不正な引数は無処理で戻します。mock 4 つの型と、戻り値を確認している単体テストを合わせて変更します。

**Recommendation**

推奨: A  
理由: 停止関数 7 つの形がそろい、呼び出し元が「停止は失敗しない」と読めます。対策元の BL-004 が挙げる「引数の誤りによる失敗とスレッドが残る失敗を区別できない」という問題も、失敗の戻り値をなくすことで解消します。制約として、テストと mock の変更が案 B より多くなります。

**Related**

[Q-005](#q-005-失敗しない前提の記録先)

### Q-002 api_context_has_threads の扱い

**Status**

解決済み

**Question**

`api_context_has_threads()` と、それを使う資源保持の分岐を、どう扱いますか。

**Context**

`api_context_has_threads()` は、コンテキストにスレッドのハンドルが残っているかを返す関数です。呼び出しは 6 か所 (`potr_service_close.c` に 2 か所、`potr_service_open.c` に 4 か所) で、どれも「残っていれば資源を解放せずに戻る」ために使っています。「調査結果」のとおり、6 か所とも、ハンドルが残る状態では到達しません。

現状の例 (`potr_service_close()`) です。

```c
ret = close_impl(ctx);

if (api_context_has_threads(ctx))
{
    potr_internal_handle_end_close(ctx, 0);
    return ret;
}
```

- 案 A: 6 か所の分岐と、関数、mock、単体テスト (`apiContextCleanupTest` の 1 件、`potrServiceCloseTest` の `incomplete_join_retains_shared_resources`) を削除します。
- 案 B: 6 か所の分岐は削除し、関数は残します。資源を解放する直前の 2 か所 (`potr_service_close()` と `ctx_cleanup()`) で、デバッグ ビルドの表明 (assert) として使います。停止の順序を将来変えたときの誤りを、開発中に検出できます。porter の製品コードに表明の前例があるかは未確認です。
- 案 C: 現状のまま残します (対策元の BL-004 のうち、停止関数の戻り値だけを整理します)。

**Answer**

選択方式: 単一選択

- [x] A: (推奨) 分岐、関数、mock、関連する単体テストを削除します。
- [ ] B: 分岐は削除し、関数は資源解放の直前の表明として残します。
- [ ] C: 現状のまま残します。
- [ ] D: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

回答者: Tetsuo Honda / 回答日時: 2026-11-18 06:34:13 JST (検討用 Markdown への記入)

**Assessment**

A の選択を確認しました。`api_context_has_threads()` を使う 6 か所の分岐、関数、mock、`apiContextCleanupTest` の該当 1 件、`potrServiceCloseTest` の `incomplete_join_retains_shared_resources` を削除します。

**Recommendation**

推奨: A  
理由: 対策元の BL-004 の「到達しない経路とそのテストを残さない」に合います。停止の順序の誤りは、スレッドを実際に動かす結合テスト (`porterSendRecvTest`、`handleConcurrencyTest`) で検出できます。制約として、スレッドが残ったまま資源を解放する誤りを将来作り込んだ場合に、実行時の安全策はなくなります。

**Related**

[Q-003](#q-003-ハンドルの状態-close_failed-の扱い)

### Q-003 ハンドルの状態 CLOSE_FAILED の扱い

**Status**

解決済み

**Question**

ハンドル管理の状態 `CLOSE_FAILED` と、`potr_internal_handle_end_close()` の引数 `closed` を、この案件で整理しますか。

**Context**

`prod/libsrc/porter/potr_handle.c` は、ハンドルの状態を `0: OPEN, 1: CLOSING, 2: CLOSE_FAILED, 3: CLOSED` で管理しています。`CLOSE_FAILED` は、`potr_internal_handle_end_close(ctx, 0)` だけが設定します。この呼び出しは、`potr_service_close()` の到達しない 2 か所 (スレッドが残った場合と、`potr_internal_notify_stop()` が失敗した場合) だけにあります。

[Q-001](#q-001-停止関数の戻り値の型) と [Q-002](#q-002-api_context_has_threads-の扱い) の整理でこの 2 か所がなくなると、`closed` に 0 を渡す呼び出しがなくなり、`CLOSE_FAILED` は設定されない状態になります。

- 案 A: この案件で、引数 `closed` と状態 `CLOSE_FAILED` を削除します。`potr_handle.c` とその単体テスト、mock が対象に加わります。
- 案 B: この案件では `potr_handle.c` を変えず、呼び出し元は常に 1 を渡します。使われなくなった引数と状態は、残課題一覧に記録します。

[Q-002](#q-002-api_context_has_threads-の扱い) で C (現状のまま) を選んだ場合は、この質問は対象外です。

**Answer**

選択方式: 単一選択

- [x] A: (推奨) この案件で、引数 `closed` と状態 `CLOSE_FAILED` を削除します。
- [ ] B: この案件では変えず、残課題一覧に記録します。
- [ ] C: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

回答者: Tetsuo Honda / 回答日時: 2026-11-18 06:34:13 JST (検討用 Markdown への記入)

**Assessment**

A の選択を確認しました。ただし、回答の後の調査で、この質問の Context に漏れがあったことが分かりました。`CLOSE_FAILED` による「終了の再試行」は、公開ヘッダー、`docs/api.md`、`docs/architecture.md`、機能仕様に記述されています。A を実施するとこれらの記述が実装と合わなくなるため、記述の扱いを [Q-007](#q-007-終了の再試行の記述の扱い) で確認します。Q-007 の回答まで、この質問は未解決として扱います。

**History**

- 2026-11-18 06:34:13 JST: A と回答。文書の記述との整合が未確認のため未解決とした。
- 2026-11-18 06:55:02 JST: [Q-007](#q-007-終了の再試行の記述の扱い) の A の回答を受けて、A のまま解決済みとした。

**Recommendation**

推奨: A  
理由: 残すと、この案件が取り除く対象と同じ種類の「設定されない状態」を新たに作ることになります。変更は内部の関数 1 つと状態 1 つです。制約として、ハンドル管理の単体テストの変更量は未確認で、最終仕様の作成時に確定します。

**Related**

[Q-002](#q-002-api_context_has_threads-の扱い)、[Q-007](#q-007-終了の再試行の記述の扱い)

### Q-004 close_impl の重複した検査の扱い

**Status**

解決済み

**Question**

`close_impl()` の先頭にある、自サービスのコールバック内からの呼び出しの検査を、この案件で削除しますか。

**Context**

調査の中で、対策元の BL-004 とは別の到達しない分岐を見つけました。`potr_service_close()` は、`potr_internal_handle_begin_close()` で「自サービスのコールバック内からの呼び出し」を検査し、`POTR_ERR_UNSUPPORTED` で拒否します。その後に呼ぶ `close_impl()` の先頭にも同じ検査があり、こちらには到達しません。

```c
if (potr_internal_in_own_callback(ctx))
{
    POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_service_close: service_id=%" PRId64 " called from its own callback",
               ctx->service.service_id);
    return POTR_ERR_UNSUPPORTED;
}
```

この分岐は join の失敗とは関係がなく、対策元の BL-004 の範囲には含まれません。削除すると、拒否したときのエラーのトレース出力もなくなります (`potr_internal_handle_begin_close()` はトレースを出していません)。

**Answer**

選択方式: 単一選択

- [ ] A: (推奨) この案件では変えず、残課題一覧に記録します。
- [x] B: この案件で削除し、トレース出力は `potr_service_close()` の拒否の経路へ移します。
- [ ] C: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

回答者: Tetsuo Honda / 回答日時: 2026-11-18 06:34:13 JST (検討用 Markdown への記入)

**Assessment**

B の選択を確認しました (推奨とは異なる選択)。`close_impl()` の先頭の検査を削除し、拒否したときのエラーのトレース出力を `potr_service_close()` の拒否の経路へ移します。拒否の時点ではコンテキストを取得していないため、トレースにサービス ID は含められません。出力の文言は最終仕様に書き、第 2 ゲートで確認を受けます。

**Recommendation**

推奨: A  
理由: 対策元の BL-004 と原因が異なり、トレース出力の移し先という別の判断が必要になるためです。

### Q-005 失敗しない前提の記録先

**Status**

解決済み

**Question**

「スレッドの停止関数は失敗を返さない」という前提を、どこに記録しますか。

**Context**

分岐を削除した後に、将来の保守者が停止関数へ失敗の経路を足すと、呼び出し元には資源を保持する処理がないため、使用中の資源を解放するおそれがあります。前提を残す場所の候補は 2 つです。

- 停止関数の宣言の Doxygen (内部ヘッダー)。関数ごとに「終了待ちは失敗しません」と書きます。
- porter のコーディング規範 (`app/porter/docs/coding-guideline.md`)。`app/porter/AGENTS.md` は、porter 固有の制約をこの文書へ集約すると定めています。規則として「スレッドの停止関数は `void` とし、呼び出し元は停止後の資源解放を無条件に行う。根拠は `cplat_thread_join()` が失敗を返さないこと」を 1 項目追加します。

**Answer**

選択方式: 単一選択

- [x] A: (推奨) 内部ヘッダーの Doxygen と、porter のコーディング規範の両方に記録します。
- [ ] B: 内部ヘッダーの Doxygen だけに記録します。
- [ ] C: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

回答者: Tetsuo Honda / 回答日時: 2026-11-18 06:34:13 JST (検討用 Markdown への記入)

**Assessment**

A の選択を確認しました。内部ヘッダーの Doxygen と、`app/porter/docs/coding-guideline.md` の両方に記録します。

**Recommendation**

推奨: A  
理由: Doxygen は個々の関数の契約を示し、コーディング規範は新しい停止関数を足すときの規則を示します。役割が異なるため、両方に置く価値があります。規範の文言は、[Q-001](#q-001-停止関数の戻り値の型) の回答に合わせます。

**Related**

[Q-001](#q-001-停止関数の戻り値の型)

### Q-006 局所検証の担当と範囲

**Status**

解決済み

**Question**

変更後の局所検証を、だれが、どの範囲で実行しますか。

**Context**

変更は open と close の失敗の経路に及びます。影響範囲の局所テストは、porter の単体テストと結合テスト (`app/porter` の `make test`) です。Linux/GCC と Windows/MSVC の両方で動く構成を保つ必要があります。

2026-11-18 の対話で、ユーザーは別の案件について、エージェントによるビルドの実施と、Windows と WSL のビルドの同時実施を許可しました。この案件にも同じ扱いを適用してよいかを確認します。

**Answer**

選択方式: 単一選択

- [x] A: (推奨) エージェントが、Windows と WSL (WSL 側のセッションへ依頼) の両方で `app/porter` の `make test` を実行します。
- [ ] B: エージェントが Windows だけで実行し、Linux は CI で確認します。
- [ ] C: ユーザーが実行します。
- [ ] D: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

回答者: Tetsuo Honda / 回答日時: 2026-11-18 06:34:13 JST (検討用 Markdown への記入)

**Assessment**

A の選択を確認しました。エージェントが Windows で、WSL 側のセッションが WSL で、`app/porter` の `make test` を実行します。

**Recommendation**

推奨: A  
理由: CI は 1 回あたり 2 時間前後かかるため、プッシュの前に両方の OS で失敗を見つけるほうが、手戻りが少なく済みます。コミット、プッシュ、CI の実行は、この質問とは別に、作業の完了時に許可を確認します。

### Q-007 終了の再試行の記述の扱い

**Status**

解決済み

**Question**

[Q-003](#q-003-ハンドルの状態-close_failed-の扱い) の A (状態 `CLOSE_FAILED` の削除) に伴い、「終了処理が失敗した場合は終了を再試行できる」という記述を、文書から削除しますか。

**Context**

[Q-003](#q-003-ハンドルの状態-close_failed-の扱い) の Context で、私は「公開ヘッダーと文書に、終了待ちの失敗の記述はない」という調査結果を前提にしました。この調査は「終了待ち」「join」という語だけで検索したもので、漏れがありました。「再試行」で検索し直すと、次の 4 か所に記述があります。

| 文書 | 記述 |
| --- | --- |
| 機能仕様 `docs/functional-spec/service.md` の「通知の順序と継続不能時の動作」 | 終了処理自体が失敗した場合は、通常操作を再開せず、終了を再試行できます。 |
| 公開ヘッダー `prod/include/porter/porter_spec.h` の `potr_service_close()` | 停止失敗時は通常操作を拒否したまま close を再試行できます。 |
| `docs/api.md` の「close と無効ハンドル」 | スレッド停止が失敗した場合は通常操作を拒否したまま、close による回収を再試行できます。 |
| `docs/architecture.md` | スレッド停止の失敗時は資源を保持し、完了済みの段階を二重処理せず再試行します。 |

Table: 終了の再試行の記述

機能仕様の記述は本文の 1 文で、要件 ID の付いた要件の行ではありません。公開関数のシグネチャと戻り値の種類は変わりません。

現在の実装で、この「終了の再試行」の状態 (`CLOSE_FAILED`) になるのは、スレッドが残った場合と `potr_internal_notify_stop()` が失敗した場合だけで、どちらも製品コードからは到達しません。TCP の FIN の送信失敗やタイムアウトなど、ほかの終了時の失敗では、資源を回収してハンドルを終了済みにします。つまり、4 か所の記述は、現在の実装では起こらない動作を説明しています。

- 案 A: 4 か所の記述を削除し、[Q-003](#q-003-ハンドルの状態-close_failed-の扱い) の A のとおり `CLOSE_FAILED` を削除します。機能仕様を変更するため、`check_functional_spec.py` を実行します。公開ヘッダーのコメントを変更するため、`make doxy` による生成物への影響を確認します。
- 案 B: [Q-003](#q-003-ハンドルの状態-close_failed-の扱い) の回答を B 相当へ変えます。`CLOSE_FAILED`、引数 `closed`、4 か所の記述は残します。`potr_service_close()` は常に「資源回収済み」を記録するため、再試行の仕組みは使われないまま残ります。将来、資源を保持して戻る失敗を足す場合の受け皿になります。残した理由を残課題一覧に記録します。

**Answer**

選択方式: 単一選択

- [x] A: (推奨) 4 か所の記述を削除し、`CLOSE_FAILED` を削除します。
- [ ] B: `CLOSE_FAILED` と 4 か所の記述を残します ([Q-003](#q-003-ハンドルの状態-close_failed-の扱い) の回答を B 相当へ変更)。
- [ ] C: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

回答者: Tetsuo Honda / 回答日時: 2026-11-18 06:55:02 JST (検討用 Markdown への記入)

**Assessment**

A の選択を確認しました。4 か所の記述を削除し、状態 `CLOSE_FAILED` と引数 `closed` を削除します。機能仕様の変更は本文の 1 文の削除で、要件 ID と UUID は変わりません。[Q-003](#q-003-ハンドルの状態-close_failed-の扱い) は、A のまま解決済みとします。

**Recommendation**

推奨: A  
理由: 利用者向けの文書が、起こらない動作を約束している状態を解消できます。[Q-001](#q-001-停止関数の戻り値の型) と [Q-002](#q-002-api_context_has_threads-の扱い) の決定とも一貫します。制約として、機能仕様と公開ヘッダーのコメントという、利用者が読む文書の変更が加わります。機能仕様は実装の上流にあたる文書のため、実装の都合ではなく「終了が資源を保持して失敗する場合を、porter は利用者に約束しない」という判断として決めていただく必要があります。

**Related**

[Q-003](#q-003-ハンドルの状態-close_failed-の扱い)

## 対象成果物と合意版

最終決定事項 (最終仕様) の設計書: `261118-porter-thread-stop-cleanup-design.md`

検討用 Markdown の合意版 (第 1 ゲート): 版 3 (2026-11-18)

最終仕様 (design.md) の合意版 (第 2 ゲート): 版 1 (2026-11-18 承認)

更新予定の成果物: 作業 WBS の 4 の内訳のとおり

受入条件: 最終仕様の「受入条件」の 7 項目

## 反映と検証の記録

2026-11-18 に、claude code が最終仕様の版 1 に従って変更し、局所検証を行いました。変更はすべて未コミットです。

- 製品コード: 停止関数 5 つを `void` にし、呼び出し元 3 ファイルから停止の成否とハンドルの残存の検査を削除しました。`api_context_has_threads()`、ハンドルの状態 `CLOSE_FAILED`、`potr_internal_handle_end_close()` の引数 `closed` を削除しました。`close_impl()` の重複した検査を削除し、拒否時のトレースを `potr_service_close()` へ移しました。
- 文書: 終了の再試行の記述を、機能仕様、公開ヘッダーのコメント、`docs/api.md`、`docs/architecture.md` から削除しました。`docs/architecture.md` には、最終仕様の表にない同じ趣旨の記述 (「終了待ちに失敗した場合は使用中の資源を保持し、停止完了として扱いません」) がもう 1 か所あり、あわせて直しました。`docs/coding-guideline.md` に小節「スレッドの停止関数」を追加しました。
- テストと mock: 停止関数の mock 4 つを `void` にし、`api_context_has_threads()` の mock を削除しました。単体テスト 2 件 (`incomplete_join_retains_shared_resources`、`detects_finished_but_unjoined_thread`) を削除し、`potrHandleTest` の 1 件を `closed_value_is_not_reused` に直しました。

| 確認 | 結果 |
| --- | --- |
| Windows/MSVC の `app/porter` の `make test` | 成功。561 件すべて成功、所要 69 分、内容のある `.warn` なし |
| Linux/GCC (WSL の Oracle Linux 8、一時複製) の `make test` | 成功。564 件すべて成功、所要 36 分 35 秒、内容のある `.warn` なし。WSL 側のセッションが実行 |
| `check_functional_spec.py` | 35 件の問題で失敗。すべて以前からの問題で、この案件の文書と変更による問題は 0 件。ユーザーの指示で残課題一覧の BL-001 へ記録 |
| `text_style_jp.py --dry-run` (変更した Markdown) | 指摘なし |
| 残存の確認 (`api_context_has_threads`、`CLOSE_FAILED`、終了の再試行の記述) | `docs/prompts` を除き 0 件 |

Table: 局所検証の結果

WSL の件数は、2026-11-17 の `main` の 566 件から 2 件減っています。削除した単体テスト 2 件の分です。
