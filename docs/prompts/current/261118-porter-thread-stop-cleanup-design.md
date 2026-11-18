<!--
生成元: agree-before-implement スキル
文書種別: 最終仕様 (design)
最終更新日時: 2026-11-18 06:58:05 JST
最終更新エージェント: claude code / セッション ID: session_01KJc53TxVv1vjB6xMfjiUgc
稼働 OS: Windows
上流文書: `261118-porter-thread-stop-cleanup-discussion.md` (検討用 Markdown)。削除されている場合があります。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。上流文書が存在する場合は、上流文書の再開手順に従って進行位置を確認し、判断の経緯を上流文書の Q&A で確認します。存在しない場合は、この文書を単体の最終仕様として扱います。最終更新エージェントのセッション情報や稼働 OS・最終更新日時が参照可能であれば再開時のコンテキストや環境差分の把握に利用し、別エージェントや別環境・別 OS からの引き継ぎ、担当変更によりセッションが見つからない場合も異常とせず、文書の記載から継続してください。
-->

# porter のスレッド停止関数と資源保持の分岐の整理 最終仕様

ファイル名: `261118-porter-thread-stop-cleanup-design.md`

版: 1

状態: 承認済み (版 1 を 2026-11-18 に承認)

## 目的

porter の内部には、スレッドの終了待ち (join) が失敗することを前提にした戻り値、分岐、状態、文書の記述が残っています。`cplat_thread_join()` は戻り値のない関数で、失敗を返しません。この前提に合わせて、到達しない経路と、その経路だけを確認するテスト、起こらない動作を説明する文書の記述を取り除きます。

公開関数のシグネチャと戻り値の種類は変えません。

## 停止関数の契約

次の 5 つの関数の戻り値を `int` から `void` に変えます。

| 関数 | 宣言 |
| --- | --- |
| `potr_internal_health_thread_stop()` | `prod/include_internal/porter/thread/potr_health_thread.h` |
| `potr_internal_tcp_health_thread_stop()` | 同上 |
| `potr_internal_comm_recv_thread_stop()` | `prod/include_internal/porter/thread/potr_recv_thread.h` |
| `potr_internal_tcp_recv_thread_stop()` | 同上 |
| `potr_internal_notify_stop()` | `prod/include_internal/porter/potr_notify.h` |

Table: 戻り値を void に変える停止関数

- `ctx` が `NULL` の場合と、`path_idx` が `0` から `POTR_MAX_PATH - 1` の範囲外の場合は、何もせずに戻ります。既存の `potr_internal_connect_thread_stop()` と同じ扱いです。
- それ以外の動作 (停止の要求、終了待ち、ハンドルと資源を `NULL` へ戻す処理、未起動や停止済みでの無処理) は変えません。
- 宣言の Doxygen から `@retval` を削除し、「終了待ちは失敗しません」という趣旨の説明を加えます。

この変更で、porter のスレッド停止関数 7 つ (上の 5 つと、`potr_internal_send_thread_stop()`、`potr_internal_connect_thread_stop()`) は、すべて `void` になります。

## 呼び出し元の変更

停止関数の結果の検査と、停止後にスレッドのハンドルが残っているかの検査を削除します。停止の順序と、資源を解放する順序は変えません。

### potr_service_close.c

- `close_impl()` の TCP の経路: `potr_internal_connect_thread_stop()` の後の `api_context_has_threads()` の検査を削除します。
- `close_impl()` の非 TCP の経路: `potr_internal_health_thread_stop()` と `potr_internal_comm_recv_thread_stop()` (2 か所) の結果の検査を削除します。`potr_internal_send_thread_stop()` の後の `send_thread != NULL` の検査を削除します。
- `stop_tcp_health_threads()`: 戻り値を捨てる `(void)` を削除します。
- `close_impl()` の先頭の、自サービスのコールバック内からの呼び出しの検査を削除します。この検査は、`potr_internal_handle_begin_close()` が先に行っています。
- `potr_service_close()`: `close_impl()` の後は、`potr_internal_notify_stop()`、`api_context_dispose_resources()`、`potr_internal_handle_end_close()`、コンテキストの解放を、無条件にこの順で行います。戻り値は `close_impl()` の結果です。
- `potr_service_close()`: `potr_internal_handle_begin_close()` が `POTR_ERR_UNSUPPORTED` を返した場合に、エラーのトレースを出力します。

> [!NOTE]
> 拒否の時点ではコンテキストを取得していないため、移した先のトレースにはサービス ID を含められません。出力は次の形にします。
>
> ```c
> POTR_TRACE(CPLAT_TRACE_LEVEL_ERROR, "potr_service_close: called from its own callback (handle=%p)", (const void *)handle);
> ```

### potr_service_open.c

- `ctx_cleanup()`: `api_context_has_threads()` の検査と、`potr_internal_notify_stop()` の結果の検査を削除します。関数の前のコメントを、検査をしない内容に直します。
- `start_threads_tcp()`: `potr_internal_connect_thread_start()` の失敗後の `api_context_has_threads()` の検査を削除します。
- `start_threads_udp()`: `api_context_has_threads()` の検査 (2 か所) と、`potr_internal_health_thread_stop()` の結果の検査を削除します。

サービスを開く処理が途中で失敗した場合に、起動済みのスレッドを停止してから資源を解放してハンドルを返さない動作 (機能仕様の `POTR-SERVICE-QUAL-001` <!-- potr-req: uuid=59872c68-c3ad-4eef-aeb6-70ff91bc6857 -->) は変わりません。

### potr_connect_thread.c

`potr_internal_connect_thread_stop()` から、従属スレッドの停止関数の結果の検査と、`send_thread != NULL` の検査を削除します。

## api_context_has_threads の削除

`prod/libsrc/porter/api/api_context_cleanup.c` と `api_context_cleanup.h` から、`api_context_has_threads()` を削除します。

## ハンドルの状態

`prod/libsrc/porter/potr_handle.c` の状態から `CLOSE_FAILED` を削除し、`0: OPEN, 1: CLOSING, 2: CLOSED` にします。

`potr_internal_handle_end_close()` の引数 `closed` を削除します。この関数は、常に資源回収済み (`CLOSED`) を記録します。

```c
void potr_internal_handle_end_close(potr_context *ctx);
```

ハンドルを使うほかの操作の結果は変わりません。終了の処理中は `POTR_ERR_CANCELED`、終了後は `POTR_ERR_INVALID_HANDLE` を返し、終了済みの公開値は再利用しません。

## 文書の変更

「終了処理が失敗した場合は、終了を再試行できる」という記述を、次の 4 か所から削除します。現在の実装に、資源を保持したまま終了が失敗する経路はありません。

| 文書 | 削除する記述 |
| --- | --- |
| `docs/functional-spec/service.md` の「通知の順序と継続不能時の動作」 | 終了処理自体が失敗した場合は、通常操作を再開せず、終了を再試行できます。 |
| `prod/include/porter/porter_spec.h` の `potr_service_close()` の `@attention` | 停止失敗時は通常操作を拒否したまま close を再試行できます。 |
| `docs/api.md` の「close と無効ハンドル」 | スレッド停止が失敗した場合は通常操作を拒否したまま、close による回収を再試行できます。 |
| `docs/architecture.md` | スレッド停止の失敗時は資源を保持し、完了済みの段階を二重処理せず再試行します。 |

Table: 削除する記述

`docs/architecture.md` の「join 成功後はハンドルを NULL」という記述は、「終了待ちの後はハンドルを NULL」に直します。

> [!IMPORTANT]
> 機能仕様の変更は、要件 ID の付いた要件の行ではなく、本文の 1 文の削除です。要件 ID と UUID は変わりません。「通知内容を保持する資源が不足した場合も、終了の操作を受け付けて資源を回収する」という動作 (`POTR-SERVICE-FUNC-012` <!-- potr-req: uuid=7587c362-acd0-4ec3-a377-2c248cf5d7ac -->) は変わりません。

## 知見の反映計画

| 反映先 | 内容 |
| --- | --- |
| 5 つの停止関数の宣言の Doxygen | 終了待ちは失敗しないこと。不正な引数は無処理で戻ること。 |
| `docs/coding-guideline.md` の「エラー処理と戻り値規約」 | 小節「スレッドの停止関数」を追加します。スレッドの停止関数は `void` とすること。呼び出し元は、停止の後の資源の解放を無条件に行うこと。根拠は、`cplat_thread_join()` が失敗を返さないこと。 |

Table: 知見の反映先

## テストと mock の変更

| 対象 | 変更 |
| --- | --- |
| `test/include/mock_porter.h`、`test/libsrc/mock_porter/` | 停止関数の mock 4 つ (`health`、`tcp_health`、`comm_recv`、`tcp_recv`) の型を `void` にします。`api_context_has_threads()` の mock を削除します。 |
| `potrServiceCloseTest` | `incomplete_join_retains_shared_resources` を削除します。`call_from_own_callback_returns_unsupported` は残します。停止関数の mock の戻り値の指定があれば直します。 |
| `apiContextCleanupTest` | `detects_finished_but_unjoined_thread` を削除します。 |
| `potrHealthThreadTest`、`potrRecvThreadTest`、`potrNotifyTest`、`threadRecvWindowRejectTest` | 停止関数の戻り値の確認を削除し、停止後の状態 (ハンドルが `NULL` であることなど) の確認を残します。不正な引数のテストがあれば、無処理で戻ることの確認に直します。 |
| `potrHandleTest` | `close_failure_allows_only_retry_and_closed_value_is_not_reused` から、終了失敗後の再試行の手順と確認を削除します。終了済みの公開値を再利用しないことの確認は残し、テスト名を内容に合わせます。`potr_internal_handle_end_close()` の呼び出しを新しい宣言に合わせます。 |
| そのほかの単体テスト | 宣言の変更でコンパイルできなくなる箇所を直します。 |

Table: テストと mock の変更

`test/src/libporter/` の各テスト ディレクトリにある製品ソースと同名のファイルは、ビルドが取り込む Git 管理外のファイルです。変更しません。

## 対象外

- `app/cplat` と、porter 以外の app。
- 公開関数のシグネチャ、戻り値の種類、`exportTest.cc` の関数表と変数表、`docs/api.md` の戻り値表とスレッド セーフ表。
- スレッドの起動関数の戻り値。

## 受入条件

1. 5 つの停止関数の宣言と定義が `void` であること。
2. `app/porter` の Git 管理下のソースとテスト (`docs/prompts` を除く) に、`api_context_has_threads` と `CLOSE_FAILED` が残っていないこと。
3. 「削除する記述」の 4 か所が削除されていること。
4. Windows/MSVC と Linux/GCC (WSL の Oracle Linux 8) の両方で、`app/porter` の `make test` が成功すること。Linux は、作業ツリーを WSL の一時領域へ複製して実行します。
5. ビルド後に、`app/porter` の内容のある `.warn` がないこと。
6. `python3 app/general/bin/check_functional_spec.py` が成功すること。
7. 変更した Markdown が、`text_style_jp.py --dry-run` で指摘なしであること。

コミット、プッシュ、CI の実行は、作業の完了時にユーザーの許可を確認してから行います。
