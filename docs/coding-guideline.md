# porter コーディング規範 (特化事項)

## 概要

本書は、上位の「コーディング規範」(`docs/general/coding-guideline.md`) の一般則に対して、porter を利用するコードおよび porter 自身に適用する特化事項をまとめます。

章立ては上位文書の章に対応させ、porter 固有の追記・上書き事項のみを記載します。

porter 固有の規則、制限、遵守事項は、今後もすべて本書に集約します。

## 命名規則の特化事項

### 基本方針

porter のライブラリ接頭辞は `potr_` です。  
公開の関数・型は `potr_`、ライブラリ内共有 (`include_internal/`) の関数・型は `potr_internal_` を前置きします。  
ライブラリ内共有の外部リンケージ変数は `g_potr_internal_`、公開共有変数は `g_potr_` を前置きします (公開共有変数は必要最低限に厳選)。  
`static` 関数にはライブラリ接頭辞を付けず、ファイル内共有変数は `s_` を前置きします。

すべての識別子を snake_case とします。  
PascalCase (`PotrContext` など) と camelCase (`potrOpenService` など) は使用しません。

ソース ファイルとヘッダー ファイルの名前も snake_case とします。  
ヘッダー ファイル名の `_internal` は、同名の公開ヘッダーがあるときだけ付けます (上位規範と同じ)。

公開 API は `potr_<カテゴリ名詞>_<動詞または属性>` の順序を正とします。  
ライブラリ内共有 API は `potr_internal_<カテゴリ名詞>_<動詞または属性>` とします。

### 記法統一の経緯

porter は当初、公開 API を camelCase、型を PascalCase、内部関数を snake_case という混成の記法で実装していました。  
記法の違いが事実上の公開・内部の判別手段として機能していましたが、記法を混在させる運用をやめ、リポジトリ内のほかのライブラリと同様に snake_case へ統一しました。

上位規範は、スコープ判定をヘッダー配置で行ったうえで、ライブラリ内共有の関数・型に `<lib>_internal_`、外部リンケージ変数に `g_<lib>_internal_` を付けると定めています。  
porter もこの規則に従います。既存の `potr_` / `g_potr_` 付き内部共有シンボルを `potr_internal_` / `g_potr_internal_` へ切り替える全面改名は求めず、変更対象ファイルに触れる機会に合わせて進めます。

porter には利用者が存在しないため、互換のための旧名の別名 (alias) は提供しません。

### 公開関数の改名対応

| 旧名 | 新名 |
|---|---|
| `potrOpenService` | `potr_service_open` |
| `potrOpenServiceFromConfig` | `potr_service_open_from_config` |
| `potrCloseService` | `potr_service_close` |
| `potrGetServiceType` | `potr_service_get_type` |
| `potrGetTracer` | `potr_tracer_get` |
| `potrSend` | `potr_send` |
| `potrDisconnectPeer` | `potr_peer_disconnect` |

`potr_send` はカテゴリ名詞を持たない横断的な API のため、動詞先行を許容します。

### 公開型の改名対応

| 旧名 | 新名 | 種別 |
|---|---|---|
| `PotrType` | `potr_type` | enum |
| `PotrRole` | `potr_role` | enum |
| `PotrEvent` | `potr_event` | enum |
| `PotrServiceDef` | `potr_service_def` | struct |
| `PotrGlobalConfig` | `potr_global_config` | struct |
| `PotrPacket` | `potr_packet` | struct |
| `PotrContext` | `potr_context` | struct (不透明型) |
| `PotrPeerId` | `potr_peer_id` | 整数型の alias |
| `PotrRecvCallback` | `potr_recv_fn` | 関数ポインター |

### 内部型の改名対応

| 旧名 | 新名 | 種別 |
|---|---|---|
| `PotrPayloadElem` | `potr_payload_elem` | struct |
| `PotrSendQueue` | `potr_send_queue` | struct |
| `PotrNackDedupEntry` | `potr_nack_dedup_entry` | struct |
| `PotrPeerContext` | `potr_peer_context` | struct |
| `PotrPathThreadArg` | `potr_path_thread_arg` | struct |
| `PotrPreparedPathEvents` | `potr_prepared_path_events` | struct |
| `PotrPacketSessionHdr` | `potr_packet_session_hdr` | struct |
| `PotrWindow` | `potr_window` | struct |
| `PotrConnectedThreadsOps` | `potr_connected_threads_ops` | struct |

> [!NOTE]
> `PotrSocket`、`potr_socket_cause_t` はこの表から除外しています。
> 通信のプラットフォーム抽象化層を com_util の net カテゴリへ移行したことで、ソケット型とソケット エラー要因の型は porter から com_util (`com_util_socket`、`com_util_error_cause`) へ移りました。
> porter 独自のソケット型 alias は存在しません。

### 内部共有関数・型・変数の接頭辞

`include_internal/` で宣言する関数と型には、上位規範に従い `potr_internal_` を必須とします。  
外部リンケージ変数には `g_potr_internal_` を必須とします。

```c
/* 公開 */
int potr_service_open(...);

/* ライブラリ内共有 (関数) */
int potr_internal_packet_parse(...);

/* ライブラリ内共有 (変数) */
extern int g_potr_internal_peer_count;
```

以前は接頭辞のなかった `config_*`、`packet_*`、`window_*`、`peer_*`、`seqnum_in_window`、`comm_recv_thread_*`、`tcp_recv_thread_*` の各関数へ `potr_` を付与しました。  
以降の新設・改名では `potr_internal_` / `g_potr_internal_` を使います。既存の `potr_` / `g_potr_` 付き内部共有シンボルは、変更対象に含めるときに移行します。

> [!NOTE]
> `parse_ipv4_addr`、`resolve_ipv4_addr` は、当時この一覧に含まれていましたが、通信のプラットフォーム抽象化層を com_util の net カテゴリへ移行したことで porter から削除されました。
> 現行のアドレス解決は `com_util_ipv4_parse()` / `com_util_ipv4_resolve()` を使用します。

## エラー処理と戻り値規約

porter の公開 API および内部関数が戻り値として使用する共通結果コードの運用を示します。

### 定義ヘッダー

共通結果コードは `prod/include/porter/porter_result.h` に定義します。  
結果コードを使う公開ヘッダー、内部ヘッダー、実装、およびテストは、`porter_const.h` を経由せずに `porter_result.h` を直接インクルードします。  
`porter_result.h` が正であり、本節のコード一覧は参照用の写しです。

| カテゴリ | コード | 値 | 意味 |
|---|---|---:|---|
| 成功・その他 | `POTR_OK` | 0 | 成功 |
| 成功・その他 | `POTR_ERR_UNKNOWN` | -1 | 分類済みコードに該当しない内部障害 |
| 契約・探索 | `POTR_ERR_INVALID_ARGUMENT` | -2 | 引数または設定値が不正 |
| 契約・探索 | `POTR_ERR_UNSUPPORTED` | -3 | 現在の通信種別または状態では操作が未対応 |
| 契約・探索 | `POTR_ERR_NOT_FOUND` | -4 | 対象エントリが存在しない |
| 資源・状態 | `POTR_ERR_OUT_OF_MEMORY` | -10 | メモリを確保できない |
| 資源・状態 | `POTR_ERR_FULL` | -11 | キューまたはウィンドウが満杯 |
| 資源・状態 | `POTR_ERR_EMPTY` | -12 | キューが空、または順序整列済みパケットが未着 |
| 資源・状態 | `POTR_ERR_OUT_OF_WINDOW` | -13 | 受信ウィンドウの範囲外 |
| 通信・I/O | `POTR_ERR_DISCONNECTED` | -20 | 論理 CONNECTED 前または切断中 |
| 通信・I/O | `POTR_ERR_TIMEOUT` | -21 | タイムアウト |
| 通信・I/O | `POTR_ERR_EOF` | -22 | 終端到達、または TCP 切断 |
| 通信・I/O | `POTR_ERR_IO` | -23 | ファイルまたはネットワークの I/O 失敗 |
| 通信・I/O | `POTR_ERR_PROTOCOL` | -24 | 受信データがプロトコル要件を満たさない |
| 制御 | `POTR_ERR_CANCELED` | -40 | シャットダウンによる待機または処理の中断 |

数値範囲は用途を識別しやすくするための区分であり、範囲だけを使った判定規約ではありません。  
コード値を変更または追加する場合は、公開 API、内部処理、テスト、および porter 利用側の全箇所への影響を調査します。

### 判定慣用句

呼び出し側の成否判定は、コード名との比較を正とします。

```c
int ret = potr_send(ctx, peer_id, data, len, 0);
if (ret != POTR_OK)
{
    return ret;
}
```

結果コードを受ける変数名は、上位規範に従い新規コードでは `ret` を第一選択とします。  
全エラーが負値のため `ret < 0` も等価ですが、名前比較を推奨します。  
特定のエラーを区別する場合は、`ret == POTR_ERR_DISCONNECTED` のようにコード名で比較します。  
`-1` などの数値リテラルとの比較は行いません。

### com_util 呼び出し結果の扱い

com_util の API を呼び出した結果は `ret != COM_UTIL_OK` の名前比較で判定します。  
porter の関数から返す場合は、`COM_UTIL_ERR_*` を porter の結果コードへ変換して返します。com_util の結果コードをそのまま porter の戻り値として素通ししません。  
タイムアウトは `POTR_ERR_TIMEOUT`、ファイルおよびネットワークの失敗は `POTR_ERR_IO` のように、原因が判別できる場合は対応する分類へ変換します。  
下位 API が詳細コードを提供せず、ほかの分類へ変換できない場合だけ `POTR_ERR_UNKNOWN` を返し、その理由をソース コメントに記載します。  
ソケット API (`com_util/net`) の失敗は `com_util_error *detail_out` で受け取り、対応する `POTR_ERR_*` へ変換します。  
OS 固有コードは直接比較せず、`com_util_error_is()` と `COM_UTIL_CAUSE_*` を使用します。

```c
int ret = com_util_crypto_encrypt(...);
if (ret != COM_UTIL_OK)
{
    return POTR_ERR_UNKNOWN;
}
```

### 適用対象外

以下の関数群は、設計意図により共通結果コードの適用対象外とします。  
対象外の関数には、対象外である理由をコメントとして該当宣言に残します (ルート `AGENTS.md` の「一括置換で意図的に置換対象外とした箇所」の規則に従う)。

| 対象外の関数群 | 現行規約 | 理由 |
|---|---|---|
| 0/1 述語 (`seqnum_in_window`、`window_send_full`、`window_recv_needs_nack`、`potrContext.h` の inline 述語) | 真 1 / 偽 0 | 失敗モードのない純関数であり、成否の概念が適用されない |
| 3 状態以上の判定結果を返す比較・分類関数 | 判定結果そのもの | 成否ではなく状態の分類を返す |
| 値をそのまま返す関数 (`packet_wire_size`、`potr_raw_base_type` などの getter) | 値そのもの | 結果コードの概念が適用されない |
| ハンドル・ポインター返却系 (`potrGetTracer`、`peer_create`、`peer_find_by_*` など) | 成功時ポインター / 失敗・不在時 NULL | ポインター返却 API の慣用 |
| 戻り値を持たない関数 | `void` | 同上 |

> [!NOTE]
> ソケットに対する素通しラッパー (`potr_sendto`、`potr_recvfrom`、`potr_poll_readable`、`potr_poll_writable`) と合成ラッパー (`potr_socket_open`、`potr_bind`、`potr_listen`、`potr_accept`、`potr_connect`、`potr_setsockopt`、`potr_socket_get_pending_error`) は、この表から除外しています。
> 通信のプラットフォーム抽象化層を com_util の net カテゴリへ移行したことで、porter はこれらの porter 独自ラッパーを持たず、`com_util_socket_sendto()` などの com_util API を直接呼び出します。
> com_util API の戻り値の扱いは、上記「com_util 呼び出し結果の扱い」に従います。

### 検証

```bash
# 数値リテラル比較や正値エラー判定の残存確認
grep -rnE '(==|!=|>)[[:space:]]*-?1\b' prod/libsrc --include='*.c'

# 局所テスト
make -C app/porter test
```
