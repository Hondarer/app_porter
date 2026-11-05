<!--
生成元: agree-before-implement スキル
文書種別: 最終仕様 (design)
上流文書: `261106-porter-handle-concurrency-discussion.md` (検討用 Markdown)。削除されている場合があります。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。上流文書が存在する場合は、上流文書の再開手順に従って進行位置を確認し、判断の経緯を上流文書の Q&A で確認します。存在しない場合は、この文書を単体の最終仕様として扱います。
-->

# porter ハンドルの並行送信対応と不具合対策 最終仕様

ファイル名: `261106-porter-handle-concurrency-design.md`

## 目的と範囲

porter のハンドル (`potr_context *`) について、同一ハンドルから複数スレッドで `potr_service_send` を呼べるようにします。あわせて、利用者が API の契約を守っていても起きる不具合 4 件を修正します。

| ID | 内容 |
| --- | --- |
| B-1 | N:1 送信スレッドが `peers_mutex` を外した後にピア領域を使い、ピア解放と競合する |
| B-2 | N:1 の受信コールバック内で `potr_service_send` を呼ぶと、`peers_mutex` の二重取得でデッドロックする |
| B-3 | コールバック内で `potr_service_close` を呼ぶと、受信スレッドが自分自身を join して終了しない |
| B-4 | TCP close が `ctx->send_has_data` をロックなしで読む |
| C-1 | 同一ハンドルの並行送信の許可 (部分直列化) |

Table: 今回の対応項目

範囲外は次のとおりです。

- `potr_service_close` と他 API の並行呼び出しは、引き続き禁止します。
- コールバックを `peers_mutex` の外で呼ぶ根本対策は行いません。

## 作業順序

B-1 → B-2/B-3 → B-4 → C-1 の順に、1 項目ずつ実施します。各項目の単体テストは、その項目の中で追加・更新します。結合試験と文書の最終確認は、C-1 の後にまとめて行います。

## B-1 N:1 送信スレッドのピア参照

対象は `prod/libsrc/porter/thread/potr_send_thread.c` の `send_packed_peer_mode` です。

- 先頭のピア検索は、宛先が存在するかの早期判定にだけ使います。検索結果のポインターは、ロック解放後に使いません。
- キューからの取り出しとパッキングを終えた後、`peers_mutex` を取得します。そのうえで、ピアを `target_peer_id` で再検索します。
- 再検索で見つかった場合は、`peers_mutex` を保持したまま `flush_packed_peer` を呼びます。`sendto` までロック内で行います。
- 再検索で見つからなかった場合は、パッキングした内容を破棄します。
- `peers_mutex` を解放した後で、取り出した件数分の `potr_internal_send_queue_complete` を呼びます。この呼び出しは、見つからなかった場合も行います。

ロック順は `peers_mutex` → `peer->send_window_mutex` で、受信スレッドとヘルス チェック スレッドと同じです。

> [!NOTE]
> ヘルス チェック スレッドも、N:1 の PING を `peers_mutex` を保持したまま `sendto` まで行っています (`potr_health_thread.c`)。B-1 はこれと同じ方式です。ロック保持時間の増加は小さいと見込んでいますが、測定はしていません。

## B-2/B-3 コールバック内からの API 呼び出しの検出

### 検出の仕組み

コールバックを実行しているスレッドを、ハンドルに記録します。

- `potr_context` に `cplat_atomic_i32 callback_active` と `cplat_atomic_u32 callback_tid` を追加します。構造体の明示パディングと `-Wpadded` の警告を確認して配置します。
- コールバックを呼ぶ箇所は `potr_internal_callback_emit_locked` (`prod/libsrc/porter/potr_path_event.c`) だけです。ここで呼び出し前に `callback_tid` へ `cplat_process_get_tid()` の値を、`callback_active` へ 1 を設定し、呼び出し後に `callback_active` を 0 に戻します。コールバックは `callback_mutex` の下で 1 つずつ呼ばれるため、記録する場所は 1 つで足ります。
- 判定用の内部関数 `potr_internal_in_own_callback(ctx)` を `potr_path_event.h` に追加します。`callback_active != 0` かつ `callback_tid == cplat_process_get_tid()` のとき真を返します。

> [!NOTE]
> スレッド ID は終了したスレッドに再び割り当てられる場合があります。判定はコールバックを実行中のスレッドが生存している間だけ行うため、再割り当ては判定に影響しません。別のハンドルのコールバック内からの呼び出しは検出しません。

### 検出する API と戻り値

検出時はすべて `POTR_ERR_UNSUPPORTED` を返し、トレースに理由を出力します。

| API | 検出の条件 | 判定位置 |
| --- | --- | --- |
| `potr_service_send` | N:1 (`is_multi_peer`) のときだけ | 引数検証の後、圧縮の前 |
| `potr_service_close` | 全通信種別 | NULL 検証の後、`close_requested` の設定前 |
| `potr_peer_disconnect` | 全通信種別 (N:1 以外は従来どおり `POTR_ERR_UNSUPPORTED`) | 引数検証の後、`peers_mutex` の取得前 |

Table: コールバック内呼び出しを検出する API

N:1 以外の `potr_service_send` は、コールバック内からの呼び出し (ブロッキング送信を含む) を従来どおり受け付けます。

> [!NOTE]
> N:1 以外では、送信スレッドが取得するロック (`send_window_mutex`、`tcp_send_mutex`) を、コールバック実行中のスレッドは保持していません。このため、コールバック内からの送信は停止しないと判断しています (コードの読解による確認)。

### cplat の識別子取得 API の説明の汎用化

`cplat_process_get_tid()` と `cplat_process_get_pid()` の説明から、用途を診断に限る記述を外します。関数、戻り値、実装は変更しません。

| 対象 | 変更内容 |
| --- | --- |
| `app/cplat/prod/include/cplat/runtime/process.h` の 2 関数の Doxygen | 「診断目的で…使用します」を、ログ出力などの記録に加え、処理が特定のプロセスやスレッドで実行中かを判定する用途にも使えると読める表現にする |
| `app/cplat/docs/api-cheatsheet.md` の `cplat_process_get_tid()` の行 | 「診断目的の識別子です」を、用途を限らない表現にする |
| `app/cplat/docs/functional-spec/runtime.md` の要件 `CPLAT-RUNTIME-FUNC-010` <!-- cplat-req: uuid=0e823b69-8e80-457a-997a-60fb0fdecc79 -->、`CPLAT-RUNTIME-FUNC-011` <!-- cplat-req: uuid=7f2bb4ce-766b-4fde-8577-ab1c9aecdb3c --> | 要件文から「診断で」を外す。要件 ID と UUID は維持する |
| 同ファイルの「プロセスとスレッドの識別」節 | 用途の説明を、記録の切り分けと実行中のプロセスやスレッドの判定の両方を含む表現にする |

Table: cplat の説明の変更箇所

要件 `CPLAT-RUNTIME-FUNC-010` <!-- cplat-req: uuid=0e823b69-8e80-457a-997a-60fb0fdecc79 --> と `CPLAT-RUNTIME-FUNC-011` <!-- cplat-req: uuid=7f2bb4ce-766b-4fde-8577-ab1c9aecdb3c --> を参照する下流成果物は、履歴文書 `app/cplat/docs/proposals/string-catalog-trace-kind-design.md` だけです。この文書は追加時の記録のため変更しません。

> [!IMPORTANT]
> 要件文の変更は、取得する対象 (現在のプロセスやスレッドの識別子) を保ち、用途の限定だけを外す表現修正として扱い、UUID を維持します。

## B-4 TCP close の send_has_data の読み取り

`prod/libsrc/porter/api/potr_service_close.c` の TCP 経路で、`get_fin_target_seq(ctx, NULL)` を `get_fin_target_seq(ctx, &has_data)` に変えます。これにより、`send_window_mutex` の下で `send_has_data` を取得します。後続の判定は、`ctx->send_has_data` ではなく取得した値を使います。

## C-1 同一ハンドルの並行送信

### 並行送信の妨げになる箇所と対策

| 箇所 | 対策 |
| --- | --- |
| 共有の圧縮領域 `ctx->compress_buf` | 呼び出しごとに一時領域を確保する |
| 断片の 1 要素ずつのキュー登録 | 1 メッセージ分の登録を、キューの登録用 mutex で直列化する |
| ブロッキング送信のキュー全体の空き待ち | 登録順の番号 (チケット) で、自分の最後の断片の完了を待つ |

Table: 並行送信の妨げになる箇所と対策

### 送信キュー

対象は `prod/include_internal/porter/infra/potr_send_queue.h` と `prod/libsrc/porter/infra/potr_send_queue.c` です。

- 構造体に、次の 4 つのメンバーを追加します。
    - `uint64_t pushed_seq`: 登録済みの件数で、`mutex` で保護します。
    - `uint64_t completed_seq`: 完了済みの件数で、`mutex` で保護します。
    - `cplat_condvar *progress`: `completed_seq` の増加を通知します。
    - `cplat_local_lock *push_mutex`: 1 メッセージ分の登録を直列化します。
- `potr_internal_send_queue_init` と `potr_internal_send_queue_dispose` で、`progress` と `push_mutex` を生成・破棄します。キューに持たせることで、送信キューを持つ通信種別だけで生成され、寿命もキューと一致します。
- `potr_internal_send_queue_push` と `potr_internal_send_queue_push_wait` に、出力引数 `uint64_t *ticket` を追加します。登録時に `pushed_seq` を 1 増やし、その値を返します。NULL の場合は返しません。
- `potr_internal_send_queue_complete` で `completed_seq` を 1 増やし、`progress` を broadcast します。
- 新しく次の 3 つの関数を追加します。
    - `potr_internal_send_queue_wait_ticket(q, ticket)`: `completed_seq >= ticket` になるまで待ちます。
    - `potr_internal_send_queue_lock_push(q)`: `push_mutex` を取得します。
    - `potr_internal_send_queue_unlock_push(q)`: `push_mutex` を解放します。
- `potr_internal_send_queue_wait_drained` は、close の送信完了待ちに使うため残します。

送信スレッドは 1 本で、取り出した順に完了させます (取り出した全要素の送信後に、件数分の complete を呼びます)。このため、`completed_seq >= ticket` は「その番号までのすべての要素が送信済み」を意味します。

> [!NOTE]
> `wait_ticket` は、送信スレッドの停止を理由に待ちを中断しません。現在の `wait_drained` と同じ振る舞いです。送信中の close は禁止のため、通常の利用では送信スレッドの停止中に待つことはありません。

### 送信 API

対象は `prod/libsrc/porter/api/potr_service_send.c` です。

- `POTR_SEND_COMPRESS` を指定した場合は、`ctx->compress_buf_size` の一時領域を `cplat_malloc` で確保して圧縮します。確保に失敗した場合は `POTR_ERR_OUT_OF_MEMORY` を返します。
- 一時領域は、全宛先への断片登録が終わった時点で `cplat_secure_zero` で消去し、`cplat_free` で解放します。どの経路で戻る場合も解放します。圧縮結果は登録時にキューへ複製されるため、ブロッキング送信の完了待ちより前に解放します。
- `send_to_peer` は、断片の登録ループ全体を `lock_push` と `unlock_push` で囲みます。
- N:1 の `POTR_PEER_ALL` では、宛先ごとに取り直します。宛先が異なる断片が交互に並んでも、受信側では混ざりません。
- ブロッキング送信は、送信前の `wait_drained` を行いません。最後の断片の番号を保持し、`unlock_push` の後に `wait_ticket` で待ちます。宛先ごとに待つ点は従来と同じです。

### コンテキストと資源管理

- `potr_context` から `compress_buf` を削除します。`compress_buf_size` は受信の展開領域でも使うため残します。
- `potr_service_open.c` の `compress_buf` の確保と、`api_context_cleanup.c` の解放を削除します。

### 公開 API の振る舞いの変化

| 項目 | 変更前 | 変更後 |
| --- | --- | --- |
| 同一ハンドルの `potr_service_send` の並行呼び出し | 不可 | 可。各スレッド内の送信順序は保たれ、スレッド間の順序は登録用 mutex を取得した順になる |
| ブロッキング送信の待ち | 送信前と送信後に、キュー全体が空になるまで待つ | 自分の最後の断片が送信されるまで待つ。それ以前に登録された送信も送信済みになる |
| ブロッキング送信のパケット構成 | 自分の送信は先行の送信と同じ UDP パケットにまとめられない | 先行の送信と同じ UDP パケットにまとめられる場合がある |
| `potr_service_send` の `POTR_ERR_OUT_OF_MEMORY` | 全ピア送信用の一時領域の確保失敗 | 圧縮用の一時領域の確保失敗も含む |
| `potr_service_send` の `POTR_ERR_UNSUPPORTED` | なし | N:1 で自ハンドルのコールバック内から呼んだ |
| `potr_service_close` の `POTR_ERR_UNSUPPORTED` | なし | 自ハンドルのコールバック内から呼んだ |
| `potr_peer_disconnect` の `POTR_ERR_UNSUPPORTED` | N:1 以外で呼んだ | 自ハンドルのコールバック内から呼んだ場合も含む |

Table: 公開 API の振る舞いの変化

公開関数のシグネチャは変わらないため、`exportTest` のエクスポート表は変更しません。

## 試験

### 単体テスト

| 対象 | 追加・更新する確認 |
| --- | --- |
| `test/src/libporter/potrSendThreadTest/` | N:1 で、取り出し後にピアが解放されている場合に送信せず、complete を件数分呼ぶこと (B-1) |
| `test/src/libporter/potrPathEventTest/` | コールバックの実行中だけ判定が真になり、復帰後と別スレッドからは偽になること |
| `test/src/libporter/potr_service_send_test/` | N:1 のコールバック内判定で `POTR_ERR_UNSUPPORTED` を返すこと、圧縮用一時領域の確保失敗、登録用 mutex と番号待ちの呼び出し |
| `test/src/libporter/potrServiceCloseTest/` | コールバック内判定で `POTR_ERR_UNSUPPORTED` を返すこと (B-3)、TCP の `send_has_data` の取得経路 (B-4) |
| `test/src/libporter/potr_peer_disconnect_test/` | コールバック内判定で `POTR_ERR_UNSUPPORTED` を返すこと |
| `test/src/libporter/potrSendQueueTest/` | 番号の発行、完了順での番号待ちの解除、`push_mutex` の生成と破棄 |

Table: 単体テストの追加・更新

### 結合試験

`test/src/integration/porterSendRecvTest/` に次の 4 種類を追加します。

- 複数スレッドが同一ハンドルから、分割と圧縮を伴うメッセージを送ります。受信側で、全メッセージが壊れずに届くことを確認します。
- 非ブロッキング送信を流し続ける間に、ブロッキング送信が復帰することを確認します。
- N:1 で大量送信中にピア切断を繰り返し、異常終了しないことを確認します (B-1)。
- コールバック内からの対象 API の呼び出しが `POTR_ERR_UNSUPPORTED` を返し、停止しないことを確認します (B-2/B-3)。

B-1 の結合試験は競合の発生が確率的なため、異常が起きないことの確認にとどまります。ThreadSanitizer などの検査ツールでの実行は、今回の試験範囲に含めません。

## 知見の反映計画

| 反映先 | 反映する内容 |
| --- | --- |
| `app/porter/docs/api.md` のスレッド セーフ表と「ハンドルとスレッドの対応」節 | 同一ハンドルの並行送信が可能なこと、close は他 API と並行不可なこと、コールバック内から呼べない API と戻り値 |
| `porter_spec.h` の `potr_service_send`、`potr_service_close`、`potr_peer_disconnect` の Doxygen | スレッド セーフ節、戻り値表、ブロッキング送信の説明 |
| `porter_type.h` の `potr_recv_fn` の Doxygen | コールバック内から呼べない API |
| `app/porter/docs/sequence-data.md` のブロッキング送信の図と `app/porter/docs/architecture.md` の送信キューの説明 | 番号による完了待ち |
| `potr_send_queue.h` の冒頭説明と各関数の Doxygen | 番号による完了待ちと登録用 mutex |
| `potr_context.h` の `callback_active`、`callback_tid` のコメント | 記録の目的と、`callback_mutex` 下で設定する前提 |
| `potr_send_thread.c` の `send_packed_peer_mode` のコメント | `peers_mutex` を flush の間保持する理由 |
| `app/porter/docs/functional-spec/` | 並行送信やコールバック内呼び出しの記載の有無を確認し、利用側から見た要件に影響する場合だけ見直す |
| cplat の 3 文書 | [cplat の識別子取得 API の説明の汎用化](#cplat-の識別子取得-api-の説明の汎用化) のとおり |

Table: 知見の反映計画

`docs/doxybook2_public/` と `docs/doxybook2_internal/` は自動生成物のため、手作業で変更しません。

## ビルド、試験、反映の手順

1. エージェントが Windows で、porter と cplat の `make` と `make test` を実行し、対象範囲の `.warn` ファイルを確認します。失敗や新しい警告が変更に起因する場合は、修正して再実行します。
2. Windows の試験が成功した後、エージェントが変更をコミットし、`main` へプッシュします。サブモジュール `app/cplat` と `app/porter` をそれぞれコミット・プッシュし、その後で親リポジトリのサブモジュール参照を更新してコミット・プッシュします。
3. エージェントが CI で各 OS (Linux と Windows) の結果を確認します。変更に起因する失敗があれば、修正して 1 から繰り返します。

> [!IMPORTANT]
> コミットには、この案件の変更だけを含めます。作業開始時点で `app/porter` にあった利用者の未コミットの変更 (ステージ済みのファイル移動など) は、コミット前に扱いを利用者へ確認します。

## 受入条件

- 上記の仕様どおりに、B-1 から B-4 と C-1 を実装していること。
- 追加・更新した単体テストと結合試験を含めて、porter と cplat の `make test` が Windows (エージェントが実行) と CI の各 OS で成功していること。また、対象範囲の `.warn` ファイルに新しい警告がないこと。
- 変更した Markdown が `text_style_jp.py --dry-run` で指摘なしになっていること。
- ワークスペース ルートでの `python3 app/general/bin/check_functional_spec.py` が成功すること。
- `docs/api.md` と Doxygen の戻り値表とスレッド セーフ記述が、実装と一致していること。

## 対象ファイル

| サブモジュール | ファイル |
| --- | --- |
| `app/porter` | `prod/libsrc/porter/thread/potr_send_thread.c`、`prod/libsrc/porter/potr_path_event.c`、`prod/include_internal/porter/potr_path_event.h`、`prod/include_internal/porter/potr_context.h`、`prod/libsrc/porter/api/potr_service_send.c`、`prod/libsrc/porter/api/potr_service_close.c`、`prod/libsrc/porter/api/potr_peer_disconnect.c`、`prod/libsrc/porter/api/potr_service_open.c`、`prod/libsrc/porter/api/api_context_cleanup.c`、`prod/include_internal/porter/infra/potr_send_queue.h`、`prod/libsrc/porter/infra/potr_send_queue.c`、`prod/include/porter/porter_spec.h`、`prod/include/porter/porter_type.h`、`docs/api.md`、`docs/sequence-data.md`、`docs/architecture.md`、`test/` 配下の上記試験 |
| `app/cplat` | `prod/include/cplat/runtime/process.h`、`docs/api-cheatsheet.md`、`docs/functional-spec/runtime.md` |

Table: 対象ファイル
