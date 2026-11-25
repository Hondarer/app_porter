<!--
生成元: agree-before-implement スキル
文書種別: 最終仕様 (design)
最終更新日時: 2026-11-16 04:40:00 JST
最終更新エージェント: claude code / セッション ID: session_01KJc53SKi4E2GpcLq8ibyKp
稼働 OS: Windows
上流文書: `261115-porter-multicast-src-contract-discussion.md` (検討用 Markdown、合意版 5)。削除されている場合があります。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。上流文書が存在する場合は、上流文書の再開手順に従って進行位置を確認し、判断の経緯を上流文書の Q&A で確認します。存在しない場合は、この文書を単体の最終仕様として扱います。最終更新エージェントのセッション情報や稼働 OS・最終更新日時が参照可能であれば再開時のコンテキストや環境差分の把握に利用し、別エージェントや別環境・別 OS からの引き継ぎ、担当変更によりセッションが見つからない場合も異常とせず、文書の記載から継続してください。
-->

# porter マルチキャスト受信側の送信元設定契約 最終仕様

ファイル名: `261115-porter-multicast-src-contract-design.md`

## 目的

送信側と受信側が別のホストにある構成で、マルチキャスト (`multicast` と `multicast_raw`) の通信を設定だけで成立させます。受信側が送信元を限定できるという機能仕様 `POTR-TRANSPORT-QUAL-001` は、マルチキャストでも維持します。

> [!NOTE]
> 変更前の受信側は、`src_addrN` を「グループに参加する自分の NIC」と「許可する送信元、および NACK などの返送先」の両方に使っていました。1 つの値に自分の NIC と相手の IP を求めるため、同じホスト内の構成でしか成立しませんでした。

## 設定の意味

porter のサービス定義は、送信側と受信側で共有します。マルチキャストでも、unicast と同じく `src` を送信側、`dst` を受信側のアドレスとして扱います。

Table: マルチキャストの経路 N の設定キーの意味

| キー | 送信側 | 受信側 |
| --- | --- | --- |
| `src_addrN` | 送信に使う NIC (`IP_MULTICAST_IF`) とグループ参加の NIC | 許可する送信元 IP。NACK、受信報告、証明書認証の返送先 |
| `dst_addrN` | 使わない | グループ参加に使う NIC。省略時は `src_addrN` |

経路は 1 から順に列挙し、`src_addrN` が空の最初の経路で列挙を終えます。`src_addrN` がなく `dst_addrN` だけがある経路は使いません。この規則は、ほかの UDP 通信種別の経路の列挙と同じです。

受信側で `dst_addrN` を省略した経路は、変更前と同じく `src_addrN` の NIC で参加します。既存の設定は、設定を変えずに同じ動作をします。

別ホスト間の構成例を示します。送信側 `192.168.1.10`、受信側の NIC `192.168.1.20` です。

```jsonc
"2002": {
  "type": "multicast",
  "src_addr1": "192.168.1.10",
  "dst_addr1": "192.168.1.20",
  "dst_port": 6002,
  "multicast_group": "239.0.0.2"
}
```

## 実装の方針

公開構造体 `potr_service_def` の形は変えません。既存の `dst_addr` 配列の意味を、マルチキャスト受信側に広げます。

Table: 実装の変更対象

| 対象 | 変更 |
| --- | --- |
| `prod/include_internal/porter/potr_context.h` | 経路ごとに、マルチキャストのグループ参加に使った NIC (解決済み IPv4 アドレス) を保持する配列を追加します。 |
| `prod/libsrc/porter/api/api_open_paths_udp.c` | `api_open_paths_multicast()` で、受信側は `dst_addrN` があれば解決して参加 NIC とし、なければ `src_addrN` の解決値を使います。送信側は `src_addrN` を使います。決めた NIC を上記の配列に保持し、`open_socket_multicast()` へ渡します。`dst_addrN` の解決に失敗した場合は、`src_addrN` と同じく解決関数の結果を返して open に失敗します。 |
| `prod/libsrc/porter/api/potr_service_close.c` | グループ離脱に、参加時に保持した NIC を使います。 |
| `prod/libsrc/porter/thread/thread_recv_validate.c` ほか | 変更しません。受信側の送信元照合と、NACK、受信報告、証明書認証の返送先は、引き続き `src_addrN` を使います。 |

> [!NOTE]
> 公開構造体と公開関数は変わらないため、`exportTest.cc` の関数表と変数表、`docs/api.md` の戻り値表とスレッド セーフ表は変更しません。

## テスト

Table: 追加・更新する単体テスト

| テスト | 確認内容 |
| --- | --- |
| `test/src/libporter/apiOpenPathsUdpTest` | 受信側で `dst_addr1` を指定したとき、`cplat_socket_join_multicast_group()` に `dst_addr1` の NIC が渡ること。省略したとき `src_addr1` の NIC が渡ること。送信側は `dst_addr1` があっても `src_addr1` の NIC を使うこと。`dst_addr2` だけがある経路は列挙されないこと。`dst_addr1` の解決に失敗したとき open に失敗すること。 |
| `test/src/libporter/potrServiceCloseTest` | マルチキャストの close で、`cplat_socket_leave_multicast_group()` に参加時と同じ NIC が渡ること。 |
| `test/src/libporter/configLoadServiceTest` | マルチキャストのサービス定義で `dst_addr1` が読み込まれること (既存の確認がない場合に追加)。 |

単体テストのビルドと実行は、ユーザーが行います。

## 受入条件

- 上記の単体テストを含め、porter の単体テストがすべて成功すること。
- ユーザーの環境で、PSK と証明書の Windows/WSL 相互接続 12 組み合わせが全件成功すること。マルチキャストの 2 件は、受信側の `dst_addr1` に WSL 側の NIC を指定した設定で確認します。
- 変更した Markdown が `text_style_jp.py --dry-run` で指摘なしであること。

## 正本への反映計画

Table: 正本とコード コメントへの反映計画

| 反映先 | 内容 |
| --- | --- |
| `docs/config.md` の「全通信種別で共通のフィールド」 | `src_addr` と、新たにマルチキャストでも使う `dst_addr` の説明 |
| `docs/config.md` の「multicast (1:N 通信)」の図と表 | 受信側のグループ参加を `dst_addrN` (省略時 `src_addrN`) にし、送信元フィルターと NACK の返送先を `src_addrN` とする |
| `docs/config.md` の「送信元フィルタリング」節 | マルチキャストの照合は `src_addrN` で行い、参加 NIC とは独立していること。経路の列挙の規則 |
| `prod/include/porter/porter_type.h` の `src_addr` / `dst_addr` の Doxygen | `dst_addr` を「unicast のみ」から、マルチキャスト受信側の参加 NIC を含む説明へ更新 |
| `prod/sample-config/porter-services.jsonc` | 別ホスト間のマルチキャスト構成例 (`dst_addr1` 付き) を追加 |
| コード コメント (`api_open_paths_multicast()`、`potr_service_close.c` の離脱処理) | 受信側の参加 NIC の決め方と、離脱に参加時と同じ NIC を使う理由 |

機能仕様 `docs/functional-spec/transport.md` は変更しません。`POTR-TRANSPORT-QUAL-001` は、今回の変更でマルチキャストでも満たされます。

## 結合テストの接続待ちの修正

CI で不定期に失敗した結合テスト `handleConcurrencyTest` の準備処理を直します。対象は `app/porter/test/src/integration/porterSendRecvTest/handleConcurrencyTest.cc` だけで、製品コードは変えません。

`unicast_bidir` のクライアントは、サーバーが返した PING 応答を受けて CONNECTED になります。サーバー (`unicast_bidir_n1`) は、クライアントの PING を受けた時点でそのピアを CONNECTED にします。このため、サーバー側の CONNECTED はクライアント側より先になります。準備処理 `open_n1()` はサーバー側だけを待っていたため、直後にクライアントから送るテストが `POTR_ERR_DISCONNECTED` で失敗することがありました。

Table: handleConcurrencyTest の変更

| 対象 | 変更 |
| --- | --- |
| クライアント側の記録 | クライアントの CONNECTED 数を数える変数を追加します。`client_callback()` の CONNECTED で 1 増やし、DISCONNECTED で 1 減らします。既存の `s_client_connected` は残します。 |
| `SetUp()` | 追加した数を 0 に戻します。 |
| `open_n1()` | サーバー側で全クライアントが CONNECTED になる待ちに加えて、クライアント側の CONNECTED 数が `n_clients` に達するまで待ちます。待ち時間の上限は既存の `wait_for()` の既定値 (10 秒) とします。 |

> [!NOTE]
> 通常運用の N:1 では、N 側のクライアントが随時接続・切断し、全クライアントが接続済みである必要はありません。この修正は、テストの準備で開いた `n_clients` 個のクライアントが両側で通信を確立したことを待つだけで、`max_peers` までの接続は待ちません。

受入条件として、`porterSendRecvTest` が CI の全ジョブ (Windows、ol8、ol9、ol10) で成功することを確認します。

## 完了後の記録

作業の完了後に、過去 backlog `261114-porter-windows-verification-clock-unify-backlog.md` の BL-001 に、この案件での対処内容と相互接続試験の結果を追記し、状態を更新します。
