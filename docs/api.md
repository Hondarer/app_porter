# 公開 API 仕様

詳細な API 仕様は Doxygen 生成ドキュメントを参照してください。

## 現行実装で押さえるべき要点

- `PotrRecvCallback` は全通信種別共通で `peer_id` 引数を持ちます
- `PotrRecvCallback` には `POTR_EVENT_PATH_CONNECTED` / `POTR_EVENT_PATH_DISCONNECTED` が追加されています
- `potrSend()` は `potrSend(handle, peer_id, data, len, flags)` の形です
- 1:1 モードおよび `unicast` / `multicast` / `broadcast` では `peer_id` に `POTR_PEER_NA` を使用します
- `unicast_bidir` の N:1 モードでは、受信コールバックで渡された `peer_id` を `potrSend()` に指定して返信できます
- `POTR_PEER_ALL` を指定すると、N:1 モードでは全接続ピア宛の一斉送信になります
- `potrDisconnectPeer()` は `unicast_bidir` の N:1 モード専用 API です

### PotrRecvCallback の PATH イベント

`POTR_EVENT_PATH_CONNECTED` / `POTR_EVENT_PATH_DISCONNECTED` のときは、引数の意味が次のように変わります。

| 引数 | 意味 |
|---|---|
| `peer_id` | N:1 モードでは対象 peer の ID。1:1 モードでは `POTR_PEER_NA` |
| `data` | `const int[POTR_MAX_PATH]` の path 論理接続状態スナップショット |
| `len` | 状態が変化した path index |

`path_states` は常にイベント発火後の状態です。`PATH_DISCONNECTED` のときも対象 path は 0 です。
`CONNECTED` / `DISCONNECTED` は path 論理接続状態の OR が 0->1 / 1->0 に変化したときのみ発火します。

### potrSend() の戻り値

| 戻り値 | 定数 | 意味 |
|---|---|---|
| `0` | `POTR_SUCCESS` | 送信キューへの積み込み成功 |
| `1` | `POTR_ERROR_DISCONNECTED` | 論理 CONNECTED 前または切断中。`unicast_bidir`、`unicast_bidir_n1` の未接続 peer、`POTR_PEER_ALL` で接続済み peer 0 件、`tcp` / `tcp_bidir` の CONNECTED 前または全 path 切断中が該当 |
| `-1` | `POTR_ERROR` | その他のエラー（NULL ハンドルなど） |

`POTR_ERROR_DISCONNECTED` は「送信先が論理的に CONNECTED していない」ことを示します。
片方向 type 1-6 は受信側が有効な `PING` または `DATA` を契機に CONNECTED しますが、送信側はその状態を観測できないため、本戻り値の対象外です。

ヘッダ定義と引数条件の正本は `prod/porter/include/porter.h` および Doxygen 出力を参照してください。

## スレッド セーフティ

各 API のスレッドセーフ性を以下に示します。

| API | スレッドセーフ | 備考 |
|---|---|---|
| `potrOpenService()` | はい | 複数スレッドから並行してハンドルを取得可（低レベル API） |
| `potrOpenServiceFromConfig()` | はい | 複数スレッドから並行してハンドルを取得可（高レベル API） |
| `potrSend()` | **いいえ** | 同一ハンドルへの並行呼び出し不可 |
| `potrCloseService()` | **いいえ** | 他の API と同一ハンドルへ並行して呼ばないこと |
| `potrDisconnectPeer()` | はい（条件付き） | コールバック内からは呼ばないこと（デッドロック） |
| `potrGetServiceType()` | はい | グローバル状態なし |
| `potrLogConfig()` | はい | 内部でミューテックスを使用 |

### サービスを開く API の使い分け

| API | 入力 | 用途 |
|---|---|---|
| `potrOpenService()` | `PotrGlobalConfig` + `PotrServiceDef` 構造体 | テストやプログラム的な設定構築。設定ファイル不要 |
| `potrOpenServiceFromConfig()` | 設定ファイルパス + service_id | 設定ファイルベースの既存フロー。後方互換 |

`potrOpenServiceFromConfig()` の実装は設定ファイルを解析して構造体を構築し、`potrOpenService()` に委譲します。

### ハンドルとスレッドの対応

- **ハンドルはスレッドセーフではありません。** 同一ハンドルへの操作（`potrSend` / `potrCloseService` など）は 1 スレッドから行ってください。
- **ハンドルが異なれば、別スレッドから独立して使用できます。** スレッド A でサービス 1001 を、スレッド B でサービス 1002 を同時に運用することは問題ありません。
- `potrOpenService()` / `potrOpenServiceFromConfig()` でのハンドル作成スレッドと、その後 `potrSend()` を呼ぶスレッドが異なっていても構いません。ハンドル生成後はそのハンドルを操作するスレッドを 1 つに固定してください。
