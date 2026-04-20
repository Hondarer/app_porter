# porter

porter は UDP/IP および TCP/IP をサポートするクロスプラットフォーム (Linux / Windows) 通信ライブラリです。
1:1 (ユニキャスト) ・1:N (マルチキャスト・ブロードキャスト) の UDP 通信に加え、
TCP による信頼性の高い接続型通信 (1:1 および双方向) もサポートします。
スライディングウィンドウによる NACK ベース再送制御、ヘルスチェック、データ圧縮を提供します。

> 注意: このリポジトリは他のサブモジュールと組み合わせて利用することを想定しています。
> [c-modernization-kit](https://github.com/Hondarer/c-modernization-kit) に統合された利用例があります。
> `c-modernization-kit` リポジトリ内の `app/porter` サブモジュールの統合例を参照してください。

## 特徴

| 機能 | 詳細 |
|---|---|
| トランスポート | UDP/IPv4、TCP/IPv4 |
| 通信モデル | ユニキャスト / マルチキャスト / ブロードキャスト (UDP) <br>TCP 接続型通信 (`tcp` / `tcp_bidir`)、双方向ユニキャスト (`unicast_bidir`) |
| 通信モード | **通常モード**: NACK ベース再送制御・スライディングウィンドウ (最大 256 スロット) <br>**RAW モード** (`*_raw`): 再送なし・ベストエフォート。ギャップ検出時に DISCONNECTED を発行 |
| リオーダー吸収 | `reorder_timeout_ms` でギャップ検出後の待機時間を設定。待機中に追い越しパケットが届けば NACK / DISCONNECT を発行しない (通常・RAW モード共通)。マルチキャスト/ブロードキャスト通常モードでは NACK 送出タイミングを 100%〜200% の範囲で自動ジッタ分散し NACK implosion を抑制 |
| データ圧縮 | raw DEFLATE (Linux: zlib、Windows: Compression API) |
| フラグメント化 | 最大 65,535 バイトのメッセージを自動分割・結合 |
| ヘルスチェック | 片方向 type 1-6 は「最後の PING または有効 DATA 送信」から `health_interval_ms` 経過時だけ PING を送り、有効な PING / DATA 受信で疎通を維持する。双方向 UDP (`unicast_bidir` / `unicast_bidir_n1`) は定周期 PING の送受信で接続を確立するため、実効 `health_interval_ms` が 0 のままでは `CONNECTED` しない。各パスの PING 受信状態変化時には割り込み PING も送信する |
| マルチパス | 最大 4 経路の並列送信 |
| プラットフォーム | Linux、Windows 両プラットフォーム対応 |

## クイックスタート

### 設定ファイルの準備

porter は INI 形式の設定ファイルでサービスを定義します。詳細は [設定ファイル仕様](docs/config.md) を参照してください。

```ini
[global]
window_size        = 16
udp_health_interval_ms = 3000
udp_health_timeout_ms  = 10000

[service.1001]
type     = unicast
src_addr = 192.168.1.20
dst_addr = 192.168.1.10
dst_port = 5001
```

### 受信者の実装例

```c
#include "porter.h"

void on_event(int64_t service_id, PotrEvent event, const void *data, size_t len) {
    switch (event) {
    case POTR_EVENT_CONNECTED:
        /* 片方向は最初の有効 PING / DATA、双方向は PING ハンドシェイク成立 */
        break;
    case POTR_EVENT_DISCONNECTED:
        /* タイムアウト / FIN / REJECT 受信 / RAW モードのギャップ検出 */
        break;
    case POTR_EVENT_DATA:
        /* データ受信: data[0..len-1] に内容 */
        break;
    }
}

int main(void) {
    PotrHandle handle;
    potrLogConfig(POTR_TRACE_INFO, NULL, 1);
    potrOpenService("porter-services.conf", 1001, POTR_ROLE_RECEIVER, on_event, &handle);
    /* ... 待機 ... */
    potrCloseService(handle);
    return 0;
}
```

### 送信者の実装例

```c
#include "porter.h"

int main(void) {
    PotrHandle handle;
    potrLogConfig(POTR_TRACE_INFO, NULL, 1);
    potrOpenService("porter-services.conf", 1001, POTR_ROLE_SENDER, NULL, &handle);

    const char *msg = "Hello, porter!";
    potrSend(handle, POTR_PEER_NA, msg, strlen(msg), POTR_SEND_BLOCKING);  /* 圧縮なし・ブロッキング送信。1:1 通信では peer_id に POTR_PEER_NA を指定 */

    potrCloseService(handle);
    return 0;
}
```

### テスト

```sh
# 受信側 (別ターミナルで先に起動)
recv -l TRACE ../sample-config/porter-services.conf 1001

# 送信側
send -l TRACE ../sample-config/porter-services.conf 1001
```

## ドキュメント一覧

| ドキュメント | 内容 |
|---|---|
| [アーキテクチャ設計](docs/architecture.md) | スレッド構成・コンポーネント・データフロー |
| [プロトコル仕様](docs/protocol.md) | パケット構造・再送制御・ウィンドウ・圧縮 |
| [設定ファイル仕様](docs/config.md) | INI 形式設定・通信種別・マルチパス |
| [シーケンス](docs/sequence.md) | 送受信・再送・ヘルスチェックのシーケンス図 |
| [FIN 待ち合わせ仕様](docs/fin-ordering.md) | FIN/DATA 順序保証の仕組みとエッジケース |
| [公開 API 仕様](docs/api.md) | API リファレンス・スレッドセーフティ |
| [セキュリティレビュー](docs/security-review.md) | セキュリティレビュー結果と対処方針 |
