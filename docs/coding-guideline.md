# porter コーディング規範 (特化事項)

## 概要

本書は、上位の「コーディング規範」(`docs/general/coding-guideline.md`) の一般則に対して、porter を利用するコードおよび porter 自身に適用する特化事項をまとめます。
章立ては上位文書の章に対応させ、porter 固有の追記・上書き事項のみを記載します。

porter 固有の規則、制限、遵守事項は、今後もすべて本書に集約します。

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
int rc = potrSend(ctx, peer_id, data, len, 0);
if (rc != POTR_OK)
{
    return rc;
}
```

全エラーが負値のため `rc < 0` も等価ですが、名前比較を推奨します。  
特定のエラーを区別する場合は、`rc == POTR_ERR_DISCONNECTED` のようにコード名で比較します。  
`-1` などの数値リテラルとの比較は行いません。

### com_util 呼び出し結果の扱い

com_util の API を呼び出した結果は `rc != COM_UTIL_OK` の名前比較で判定します。  
porter の関数から返す場合は、`COM_UTIL_ERR_*` を porter の結果コードへ変換して返します。com_util の結果コードをそのまま porter の戻り値として素通ししません。  
タイムアウトは `POTR_ERR_TIMEOUT`、ファイルおよびネットワークの失敗は `POTR_ERR_IO` のように、原因が判別できる場合は対応する分類へ変換します。  
下位 API が詳細コードを提供せず、ほかの分類へ変換できない場合だけ `POTR_ERR_UNKNOWN` を返し、その理由をソース コメントに記載します。

```c
int rc = com_util_crypto_encrypt(...);
if (rc != COM_UTIL_OK)
{
    return POTR_ERR_UNKNOWN;
}
```

### 適用対象外

以下の関数群は、設計意図により共通結果コードの適用対象外とします。  
対象外の関数には、対象外である理由をコメントとして該当宣言に残します (ルート `AGENTS.md` の「一括置換で意図的に置換対象外とした箇所」の規則に従う)。

| 対象外の関数群 | 現行規約 | 理由 |
|---|---|---|
| `potrPlatform.h` の OS ラッパー層 (`potr_setsockopt`、`potr_sendto`、`potr_recvfrom`、`potr_poll_*` など) | 0/-1、送受信バイト数、1/0/-1 など元 API と同系 | OS ソケット API の戻り値規約を保存する層であることが設計意図。ただし合成ヘルパー (`potr_tcp_send`、`potr_tcp_recv_all`) は適用対象とする |
| 0/1 述語 (`seqnum_is_newer`、`seqnum_in_window`、`window_send_full`、`window_recv_needs_nack`、`potrContext.h` の inline 述語) | 真 1 / 偽 0 | 失敗モードのない純関数であり、成否の概念が適用されない |
| 3 状態以上の判定結果を返す比較・分類関数 | 判定結果そのもの | 成否ではなく状態の分類を返す |
| 値をそのまま返す関数 (`packet_wire_size`、`seqnum_next`、`potr_raw_base_type` などの getter) | 値そのもの | 結果コードの概念が適用されない |
| ハンドル・ポインター返却系 (`potrGetTracer`、`peer_create`、`peer_find_by_*` など) | 成功時ポインター / 失敗・不在時 NULL | ポインター返却 API の慣用 |
| 戻り値を持たない関数 | `void` | 同上 |

### 検証

```bash
# 数値リテラル比較や正値エラー判定の残存確認
grep -rnE '(==|!=|>)[[:space:]]*-?1\b' prod/libsrc --include='*.c'

# 局所テスト
make -C app/porter test
```
