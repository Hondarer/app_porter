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

Table: PATH イベント時のコールバック引数の意味

| 引数 | 意味 |
|---|---|
| `peer_id` | N:1 モードでは対象 peer の ID。1:1 モードでは `POTR_PEER_NA` |
| `data` | `const int[POTR_MAX_PATH]` の path 論理接続状態スナップショット |
| `len` | 状態が変化した path index |

`path_states` は常にイベント発火後の状態です。`PATH_DISCONNECTED` のときも対象 path は 0 です。  
`CONNECTED` / `DISCONNECTED` は path 論理接続状態の OR が 0->1 / 1->0 に変化したときのみ発火します。  
`unicast_bidir` の N:1 モードのサーバーでは、ヘルス チェックのタイムアウトや `potr_peer_disconnect()` による切断に加えて、フロー制御でほかのピアへの送信を妨げ続けている遅いピアをサーバーが自動的に切断する場合があります。このときも、ヘルス チェックのタイムアウトによる切断と同じく、`POTR_EVENT_PATH_DISCONNECTED` と `POTR_EVENT_DISCONNECTED` を通知します。切断したピアのセッションからのパケットは、切断から猶予時間の 10 倍 (`flow_control_grace_ms × 10`) の間受け入れず、その後に通常どおり受け入れます。クライアントの性能が回復すれば、利用側の操作なしで接続が回復します (切断されていた間のメッセージは再送しません)。

### potr_tracer_get() の戻り値

Table: potr_tracer_get の戻り値一覧

| 戻り値 | 意味 |
|---|---|
| `cplat_tracer *` | 取得した共有トレーサー。利用側は破棄しません |
| `NULL` | トレーサーを生成できませんでした。設定や開始を行わず、必要なら後の呼び出しで再取得します |

### potr_service_send() の戻り値

Table: potr_service_send の戻り値一覧

| 戻り値 | 意味 |
|---|---|
| `POTR_OK` | 送信キューへの追加成功 |
| `POTR_ERR_INVALID_ARGUMENT` | ハンドル、データ、サイズ、または N:1 モードの `peer_id` が不正 |
| `POTR_ERR_DISCONNECTED` | 論理 CONNECTED 前または切断中。`unicast_bidir`、`unicast_bidir_n1` の未接続 peer、`POTR_PEER_ALL` で接続済み peer 0 件、`tcp` / `tcp_bidir` の CONNECTED 前または全 path 切断中が該当。ブロッキング送信では、`tcp` / `tcp_bidir` のどの path にも書き込めなかった場合も該当 |
| `POTR_ERR_NOT_FOUND` | N:1 モードで指定した peer が存在しない |
| `POTR_ERR_FULL` | `POTR_SEND_BLOCKING` を指定しない送信で、送信キューにメッセージの全断片を登録できる空きがない。N:1 モードでは、宛先の peer がフロー制御で抑制中 (抑制の要求を受けている、または受けた送出上限に達している) の場合も該当 |
| `POTR_ERR_OUT_OF_MEMORY` | `POTR_PEER_ALL` の送信先一覧・圧縮用の一時領域の確保失敗、または通知継続不能 |
| `POTR_ERR_INVALID_HANDLE` | ハンドルが未登録または終了済み |
| `POTR_ERR_UNSUPPORTED` | 片方向の受信専用サービスに送信を要求した |
| `POTR_ERR_CANCELED` | サービスの終了処理による送信中止 |
| `POTR_ERR_UNKNOWN` | 圧縮など分類不能な内部処理の失敗 |

`POTR_ERR_FULL` を返した場合、そのメッセージは 1 つも登録されていません。  
1 件のメッセージが複数の断片になる場合も、一部の断片だけが登録されることはありません。`POTR_PEER_ALL` の送信は、全 peer の分を登録できなければ、どの peer 宛も登録しません。  
利用側は、間隔をおいて再試行するか、メッセージを破棄するかを決めます。断片の数が `send_queue_depth` を超えるメッセージは、`POTR_SEND_BLOCKING` を指定しない送信では常に `POTR_ERR_FULL` になります。  
`POTR_SEND_BLOCKING` を指定した送信は、空きができるまで待って登録し、送出の完了まで待ちます。N:1 モードで宛先の peer がフロー制御で抑制中の場合は、抑制の解除 (送出上限の更新または破棄を含む) またはピアの切断まで待ちます。  
OS の受信バッファーに基づく送出上限は、容量 (`flow_control_capacity`) を設定していない構成でも既定で働きます。このため、N:1 モードのサーバーが 1 つの peer へ連続して送信すると、容量を設定していない構成でも、その peer から受けた送出上限に達して、`POTR_SEND_BLOCKING` を指定しない送信が `POTR_ERR_FULL` を返すことがあります。

`POTR_ERR_DISCONNECTED` は「送信先が論理的に CONNECTED していない」ことを示します。  
双方向通信 (`unicast_bidir`、`unicast_bidir_n1`) と TCP (`tcp`、`tcp_bidir`) では、全グループ不達は全経路の切断と同じ論理切断として扱い、`potr_service_send()` は `POTR_ERR_DISCONNECTED` を返します。  
片方向 (`unicast`、RAW モード) と一対多 (`multicast`、`broadcast`) では、全グループ不達の場合でも `potr_service_send()` は `POTR_ERR_DISCONNECTED` を返さず成功し、全グループの全経路へ送信します。一部の受信者の停止が他の受信者への送信を止めないようにするためです。

ヘッダー定義と引数条件の正本は `app/porter/prod/include/porter.h` および Doxygen 出力を参照してください。

### 設定検証・サービス開始・種別取得 API の戻り値

`potr_service_open()`、`potr_service_open_from_config()`、`potr_service_get_type()`、`potr_config_validate_file()`、`potr_config_list_service_ids()` では、API の呼び出し方の誤りと設定内容の誤りを結果コードで区別します。

Table: 設定検証・開始系 API の主な戻り値一覧

| 結果コード | 意味 | 返す主な API |
|---|---|---|
| `POTR_OK` | 成功 | 全 API |
| `POTR_ERR_INVALID_ARGUMENT` | NULL ポインター、未定義の役割、役割とコールバックの組み合わせ、未定義のフラグや予約値の誤り | 5 関数すべて |
| `POTR_ERR_INVALID_CONFIG` | 設定ファイルの内容、設定構造体の値、または証明書と鍵の形式・組み合わせの誤り | 5 関数すべて (`potr_config_list_service_ids()` は構文とルート構造のみ) |
| `POTR_ERR_UNSUPPORTED` | 未対応の通信種別 | `potr_service_open()`、`potr_service_open_from_config()` |
| `POTR_ERR_NOT_FOUND` | 指定したサービス ID が設定ファイルに存在しない | `potr_service_open_from_config()`、`potr_service_get_type()` |
| `POTR_ERR_IO` | 設定ファイルや認証ファイルを開けない、読み取れない、またはアドレス解決・ソケット操作の失敗 | 5 関数すべて |
| `POTR_ERR_OUT_OF_MEMORY` | メモリまたは内部バッファーの確保失敗 | 5 関数すべて |
| `POTR_ERR_UNKNOWN` | スレッド生成など分類不能な内部処理の失敗 | `potr_service_open()`、`potr_service_open_from_config()`、`potr_config_validate_file()` |

`potr_config_validate_file()` は、トレースへ出力せず `potr_config_validate_options::on_issue` コールバックへ誤りを渡します。`potr_config_service_ids_free()` は `potr_config_list_service_ids()` が返した配列を解放します (`libporter` 内で確保するため、必ず専用関数で解放してください)。

## スレッド セーフ

各 API のスレッド セーフ性を以下に示します。

Table: porter API のスレッド セーフ性一覧

| API | スレッド セーフ | 備考 |
|---|---|---|
| `potr_service_open()` | はい | 複数スレッドから並行してハンドルを取得可 (低レベル API) |
| `potr_service_open_from_config()` | はい (条件付き) | JSONC 解析は共通ロックを通る。cJSON 直接解析・`cJSON_GetErrorPtr()` 参照と並行実行しないこと |
| `potr_service_send()` | はい (条件付き) | 同一ハンドルへの並行送信、close との並行呼び出し可 |
| `potr_service_close()` | はい (条件付き) | 受付済み操作と通知の完了を待つ。同一コールバック内からは拒否 |
| `potr_peer_disconnect()` | はい (条件付き) | close との並行呼び出しと、コールバック内からの呼び出し可 |
| `potr_tracer_get()` | はい | 生成失敗後は次の呼び出しで再試行し、成功後は同じ共有ハンドルを返す。NULL の場合は設定と開始を行わず、後で再取得する |
| `potr_service_get_type()` | はい (条件付き) | JSONC 解析は共通ロックを通る。cJSON 直接解析・`cJSON_GetErrorPtr()` 参照と並行実行しないこと |
| `potr_config_validate_file()` | はい (条件付き) | JSONC 解析は共通ロックを通る。各呼び出し内でコールバックを直列実行し、トレースへ出力しない。cJSON 直接解析・`cJSON_GetErrorPtr()` 参照と並行実行しないこと |
| `potr_config_list_service_ids()` | はい (条件付き) | JSONC 解析は共通ロックを通る。cJSON 直接解析・`cJSON_GetErrorPtr()` 参照と並行実行しないこと |
| `potr_config_service_ids_free()` | はい | 同じ配列を同時に解放しない場合。`potr_config_list_service_ids()` で確保した配列を解放する |

通知はハンドルごとの専用スレッドで、登録順に直列実行します。  
I/O を保護するロックを保持して利用者のコールバックを呼び出しません。

Table: 受信コールバック内から呼び出した場合の動作

| API | 同一ハンドルのコールバック内から呼び出した場合 |
| --- | --- |
| `potr_service_send()` | N:1 を含め、通常の送信動作に対応。ブロッキング指定も可 |
| `potr_service_close()` | `POTR_ERR_UNSUPPORTED`。終了要求を設定しない |
| `potr_peer_disconnect()` | N:1 で切断可能。切断通知は現在のコールバックの後に実行 |

### サービスを開く API の使い分け

Table: サービス開始 API の入力と用途の使い分け

| API | 入力 | 用途 |
|---|---|---|
| `potr_service_open()` | `potr_global_config` + `potr_service_def` 構造体 | テストやプログラム的な設定構築。構成ファイル不要 |
| `potr_service_open_from_config()` | 構成ファイル パス + service_id | 構成ファイル ベースの既存フロー。後方互換 |

`potr_service_open_from_config()` の実装は構成ファイルを解析して構造体を構築し、`potr_service_open()` に委譲します。

#### potr_service_def の証明書方式設定

`potr_service_def` を 0 初期化した場合、`auth` は `POTR_AUTH_PSK` (事前共有鍵方式) になります。証明書方式 (`POTR_AUTH_CERT`) を使用する場合は、次のメンバーを設定して `potr_service_open()` へ渡します。

Table: 証明書方式の設定を渡す `potr_service_def` のメンバー

| メンバー | 型 | 内容 |
|---|---|---|
| `auth` | `potr_auth` | `POTR_AUTH_PSK` (0: 事前共有鍵方式) または `POTR_AUTH_CERT` (1: 証明書方式) |
| `cert_file` | `const char *` | 自端の証明書ファイル (PEM 形式)。`POTR_AUTH_CERT` で必須 |
| `key_file` | `const char *` | 自端の秘密鍵ファイル (PEM 形式)。`POTR_AUTH_CERT` で必須 |
| `ca_file` | `const char *` | 信頼する CA の証明書ファイル (PEM 形式)。`pinned_certs` との少なくとも一方が必須 |
| `crl_file` | `const char *` | 失効リスト ファイル (PEM 形式)。省略時は `NULL` |
| `pinned_certs` | `const char *const *` | 直接登録する相手の証明書ファイル (PEM 形式) のパス配列 |
| `pinned_cert_count` | `size_t` | `pinned_certs` の要素数 |
| `peer_names` | `const char *const *` | CA 検証で証明書の SAN (DNS 名または URI) と照合する相手名の配列 |
| `peer_name_count` | `size_t` | `peer_names` の要素数。`ca_file` 指定時は 1 以上が必須 |

これらのポインターおよび配列内の文字列は `potr_service_open()` の呼び出し中だけ参照し、porter は保持しません (証明書、秘密鍵、CRL は open 時に 1 回だけ読み込みます)。

### ハンドルとスレッドの対応

- **同一ハンドルの `potr_service_send()` は、複数スレッドから同時に呼び出せます。** 1 メッセージの断片は他のメッセージと交互に並ばずに送られます。各スレッド内の送信順序は保たれ、スレッド間の順序は送信キューへの登録順になります。
- `potr_service_close()` と他 API は同一ハンドルへ並行して呼び出せます。受付停止前に受け付けた操作は完了を待ち、以後の新しい操作は `POTR_ERR_CANCELED` を返します。
- **ハンドルが異なれば、別スレッドから独立して使用できます。** スレッド A でサービス 1001 を、スレッド B でサービス 1002 を同時に運用することは問題ありません。
- `potr_service_open()` / `potr_service_open_from_config()` でのハンドル作成スレッドと、その後 `potr_service_send()` を呼び出すスレッドが異なっていても構いません。

`POTR_SEND_BLOCKING` を指定した送信は、自分が登録した最後の断片の送信が終わるまで待ちます。  
送信スレッドは登録順に送信するため、復帰した時点で、それ以前に登録された送信も送信済みです。  
自分の送信は、直前に登録された他の送信と同じ UDP パケットにまとめられる場合があります。  
TCP でどの path にも書き込めなかった場合は `POTR_ERR_DISCONNECTED` を返し、書き込めなかった path は切断して再接続します。  
送信できなかったメッセージを成功として返さないことを優先するため、復帰までに後続の送信が書き込みに失敗した場合も `POTR_ERR_DISCONNECTED` を返すことがあります。

### close と無効ハンドル

正常 close は受付済み操作、送信キュー、登録済み通知の完了を待って資源を回収します。  
進行中に別スレッドが同じハンドルを close すると `POTR_ERR_CANCELED` を返します。  
回収後の send、disconnect、close は `POTR_ERR_INVALID_HANDLE` を返し、その値を新しい接続へ再利用しません。  
TCP の FIN 待機失敗で資源回収を完了した場合は、ハンドルは終了済みです。  
TCP 通信種別では、close 開始時に `tcp_close_timeout_ms` から 1 つの絶対期限を決定し、受付済み API の通信待機の回収、送信キューの排出、FIN の送信、および FIN_ACK の待機で同じ期限を共有します (段階の移行や再試行で期限を延長しません)。期限を超過した場合は、送信キューの中断と未送信データの破棄、通信待機の解除を行い、参照とスレッドをすべて回収してから `POTR_ERR_TIMEOUT` を返します。`tcp_close_timeout_ms` が 0 の場合も、同じ期限で各待機を打ち切り、使用中の資源を回収前に解放しません。  
TCP 通信種別で close 開始時に相手端の正常な FIN を受理済みの場合は FIN を送らず、相手 FIN 受理後に DATA を送信していなければ `POTR_OK`、送信していれば `POTR_ERR_DISCONNECTED` を返します。FIN_ACK 待機中に全パスが切断された場合もタイムアウトを待たずに復帰し、相手 FIN 受理済みなら受理後の DATA 送信の有無で、未受理なら現セッションの送信済みデータの有無で `POTR_OK` または `POTR_ERR_DISCONNECTED` を返します。

通知領域と通信資源は close で回収します。  
古いハンドルを識別する軽量な制御情報はライブラリの存続中保持するため、その保持量は累計 open 数に比例します。  
x64 の Windows での測定では、1 ハンドルあたり 150〜190 バイト程度でした。open と close を長期間繰り返す用途では、この増加を見込んでください。

### 通知継続不能のエラー イベント

`POTR_EVENT_ERROR` は通知領域を確保できず、通常の通知を継続できなくなったことを表します。  
`peer_id` は `POTR_PEER_NA`、`data` は `const int *` の原因コード、`len` は `sizeof(int)` です。  
現在の原因コードは `POTR_ERR_OUT_OF_MEMORY` です。  
先に登録できた通知の後に 1 回だけ通知し、以後の通常通知は登録しません。  
通知領域の確保失敗後は通常操作と送信待機が `POTR_ERR_OUT_OF_MEMORY` で失敗し、外部スレッドの close で回収できます。

コールバックに時間がかかる間も I/O は進み、未処理通知を保持する領域が動的に増えます。  
コールバックの処理が受信より遅い間は、保持する領域が受信データ量に比例して増え、通知の遅延も伸び続けます。  
未処理通知をすべて消費した後も、プロセスのメモリ使用量がすぐに減るとは限りません。確保済みの領域は close で回収します。  
受信の速さに対してコールバックが遅くなる用途では、コールバックで重い処理を行わず、利用側のキューと別スレッドへ渡してください。  
受信側の構成で `flow_control_capacity` を指定すると、未処理の DATA の量に応じて送信側の送出を止められます (フロー制御)。容量を超えた DATA は破棄され、`POTR_EVENT_DISCONNECTED` が通知されます。容量を指定しない場合も、再送を行う UDP の通信種別の受信側は、OS の受信バッファーの大きさに基づく送出上限で、送信側の送出を抑えます。設定は [設定ファイル仕様](config.md) の「フロー制御の設定」を参照してください。  
コールバックへ渡した領域は復帰まで有効であり、復帰後に参照する内容は利用側で複製します。  
受信パケットの認証タグ不一致または圧縮データの展開失敗を検出した場合は、そのパケットを破棄し、`POTR_EVENT_DATA` は通知しません。  
close の成功復帰後には、当該ハンドルのコールバックは実行されません。
