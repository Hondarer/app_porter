# 公開 API 仕様

詳細な API 仕様は Doxygen 生成ドキュメントを参照してください。

## 現行実装で押さえるべき要点

- `potr_recv_fn` は全通信種別共通で `peer_id` 引数を持ちます
- `potr_recv_fn` には `POTR_EVENT_PATH_CONNECTED` / `POTR_EVENT_PATH_DISCONNECTED` が追加されています
- `potr_service_send()` は `potr_service_send(handle, peer_id, data, len, flags)` の形です
- 1:1 モードおよび `unicast` / `multicast` / `broadcast` では `peer_id` に `POTR_PEER_NA` を使用します
- `unicast_bidir` の N:1 モードでは、受信コールバックで渡された `peer_id` を `potr_service_send()` に指定して返信できます
- `POTR_PEER_ALL` を指定すると、N:1 モードでは全接続ピア宛の一斉送信になります
- `potr_peer_disconnect()` は `unicast_bidir` の N:1 モード専用 API です

### potr_recv_fn の PATH イベント

`POTR_EVENT_PATH_CONNECTED` / `POTR_EVENT_PATH_DISCONNECTED` のときは、引数の意味が次のように変わります。

| 引数 | 意味 |
|---|---|
| `peer_id` | N:1 モードでは対象 peer の ID。1:1 モードでは `POTR_PEER_NA` |
| `data` | `const int[POTR_MAX_PATH]` の path 論理接続状態スナップショット |
| `len` | 状態が変化した path index |

Table: PATH イベント時のコールバック引数の意味

`path_states` は常にイベント発火後の状態です。`PATH_DISCONNECTED` のときも対象 path は 0 です。  
`CONNECTED` / `DISCONNECTED` は path 論理接続状態の OR が 0->1 / 1->0 に変化したときのみ発火します。

### potr_service_send() の戻り値

| 戻り値 | 意味 |
|---|---|
| `POTR_OK` | 送信キューへの追加成功 |
| `POTR_ERR_INVALID_ARGUMENT` | ハンドル、データ、サイズ、または N:1 モードの `peer_id` が不正 |
| `POTR_ERR_DISCONNECTED` | 論理 CONNECTED 前または切断中。`unicast_bidir`、`unicast_bidir_n1` の未接続 peer、`POTR_PEER_ALL` で接続済み peer 0 件、`tcp` / `tcp_bidir` の CONNECTED 前または全 path 切断中が該当 |
| `POTR_ERR_NOT_FOUND` | N:1 モードで指定した peer が存在しない |
| `POTR_ERR_OUT_OF_MEMORY` | `POTR_PEER_ALL` の送信先一覧・圧縮用の一時領域の確保失敗、または通知継続不能 |
| `POTR_ERR_INVALID_HANDLE` | ハンドルが未登録または終了済み |
| `POTR_ERR_UNSUPPORTED` | 片方向の受信専用サービスに送信を要求した |
| `POTR_ERR_CANCELED` | サービスの終了処理による送信中止 |
| `POTR_ERR_UNKNOWN` | 圧縮など分類不能な内部処理の失敗 |

Table: potr_service_send の戻り値一覧

`POTR_ERR_DISCONNECTED` は「送信先が論理的に CONNECTED していない」ことを示します。  
片方向 type 1-6 は受信側が有効な `PING` または `DATA` を契機に CONNECTED しますが、送信側はその状態を観測できないため、本戻り値の対象外です。

ヘッダー定義と引数条件の正本は `app/porter/prod/include/porter.h` および Doxygen 出力を参照してください。

## スレッド セーフ

各 API のスレッド セーフ性を以下に示します。

| API | スレッド セーフ | 備考 |
|---|---|---|
| `potr_service_open()` | はい | 複数スレッドから並行してハンドルを取得可 (低レベル API) |
| `potr_service_open_from_config()` | はい | 複数スレッドから並行してハンドルを取得可 (高レベル API) |
| `potr_service_send()` | はい (条件付き) | 同一ハンドルへの並行送信、close との並行呼び出し可 |
| `potr_service_close()` | はい (条件付き) | 受付済み操作と通知の完了を待つ。同一コールバック内からは拒否 |
| `potr_peer_disconnect()` | はい (条件付き) | close との並行呼び出しと、コールバック内からの呼び出し可 |
| `potr_service_get_type()` | はい | グローバル状態なし |

Table: porter API のスレッド セーフ性一覧

通知はハンドルごとの専用スレッドで、登録順に直列実行します。  
I/O を保護するロックを保持して利用者のコールバックを呼び出しません。

| API | 同一ハンドルのコールバック内から呼び出した場合 |
| --- | --- |
| `potr_service_send()` | N:1 を含め、通常の送信動作に対応。ブロッキング指定も可 |
| `potr_service_close()` | `POTR_ERR_UNSUPPORTED`。終了要求を設定しない |
| `potr_peer_disconnect()` | N:1 で切断可能。切断通知は現在のコールバックの後に実行 |

Table: 受信コールバック内から呼び出した場合の動作

### サービスを開く API の使い分け

| API | 入力 | 用途 |
|---|---|---|
| `potr_service_open()` | `potr_global_config` + `potr_service_def` 構造体 | テストやプログラム的な設定構築。構成ファイル不要 |
| `potr_service_open_from_config()` | 構成ファイル パス + service_id | 構成ファイル ベースの既存フロー。後方互換 |

Table: サービス開始 API の入力と用途の使い分け

`potr_service_open_from_config()` の実装は構成ファイルを解析して構造体を構築し、`potr_service_open()` に委譲します。

### ハンドルとスレッドの対応

- **同一ハンドルの `potr_service_send()` は、複数スレッドから同時に呼び出せます。** 1 メッセージの断片は他のメッセージと交互に並ばずに送られます。各スレッド内の送信順序は保たれ、スレッド間の順序は送信キューへの登録順になります。
- `potr_service_close()` と他 API は同一ハンドルへ並行して呼び出せます。受付停止前に受け付けた操作は完了を待ち、以後の新しい操作は `POTR_ERR_CANCELED` を返します。
- **ハンドルが異なれば、別スレッドから独立して使用できます。** スレッド A でサービス 1001 を、スレッド B でサービス 1002 を同時に運用することは問題ありません。
- `potr_service_open()` / `potr_service_open_from_config()` でのハンドル作成スレッドと、その後 `potr_service_send()` を呼び出すスレッドが異なっていても構いません。

`POTR_SEND_BLOCKING` を指定した送信は、自分が登録した最後の断片の送信が終わるまで待ちます。  
送信スレッドは登録順に送信するため、復帰した時点で、それ以前に登録された送信も送信済みです。  
自分の送信は、直前に登録された他の送信と同じ UDP パケットにまとめられる場合があります。

### close と無効ハンドル

正常 close は受付済み操作、送信キュー、登録済み通知の完了を待って資源を回収します。  
進行中に別スレッドが同じハンドルを close すると `POTR_ERR_CANCELED` を返します。  
回収後の send、disconnect、close は `POTR_ERR_INVALID_HANDLE` を返し、その値を新しい接続へ再利用しません。  
スレッド停止が失敗した場合は通常操作を拒否したまま、close による回収を再試行できます。  
TCP の FIN 待機失敗で資源回収を完了した場合は、ハンドルは終了済みです。

通知領域と通信資源は close で回収します。  
古いハンドルを識別する軽量な制御情報はライブラリの存続中保持するため、その保持量は累計 open 数に比例します。

### 通知継続不能のエラー イベント

`POTR_EVENT_ERROR` は通知領域を確保できず、通常の通知を継続できなくなったことを表します。  
`peer_id` は `POTR_PEER_NA`、`data` は `const int *` の原因コード、`len` は `sizeof(int)` です。  
現在の原因コードは `POTR_ERR_OUT_OF_MEMORY` です。  
先に登録できた通知の後に 1 回だけ通知し、以後の通常通知は登録しません。  
通知領域の確保失敗後は通常操作と送信待機が `POTR_ERR_OUT_OF_MEMORY` で失敗し、外部スレッドの close で回収できます。

コールバックに時間がかかる間も I/O は進み、未処理通知を保持する領域が動的に増えます。  
コールバックへ渡した領域は復帰まで有効であり、復帰後に参照する内容は利用側で複製します。  
close の成功復帰後には、当該ハンドルのコールバックは実行されません。
