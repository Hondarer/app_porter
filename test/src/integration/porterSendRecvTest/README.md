# porterSendRecvTest テスト プログラム

## 概要

このテストは、`porter-test` バイナリを receiver/sender としてサブプロセス起動し、標準入出力を介して実際に送受信させることで porter ライブラリの結合動作を検証する統合テストです。

サブプロセス制御には `framework/testfw` の非同期プロセス制御 API (`startProcessAsync`、`waitForOutput`、`writeLineStdin`、`interruptProcess`、`waitForExit` など) を使用しています。API の詳細は `framework/testfw/docs/process-control-api.md` を参照してください。

## interruptProcess 前に受信側の出力を待つこと

### 現象

受信側プロセス (`recv_h_`) に対して `interruptProcess()` で SIGINT を送った後、`getStdout(recv_h_)` の内容を検証するテストで、CI 環境でのみ稀に失敗する事例が複数見つかりました。

送信側 (`send_h_`) がデータ送信後にプロンプト復帰・終了するまでを待ってから受信側に `interruptProcess()` するだけでは不十分です。暗号化 (復号) 処理や TCP ヘルスチェック確認など、受信側の処理に時間がかかる経路では、送信側の処理完了時点で受信側がまだ受信データを処理・標準出力へ反映し終えていないことがあります。ローカル環境では気づきにくく、CI のように負荷が高くスケジューリング遅延が起きやすい環境でのみ、受信側の出力反映より先に SIGINT が届いてしまい、`getStdout(recv_h_).find("<期待文字列>")` が `string::npos` になってテストが失敗します。

### 対処

`interruptProcess(recv_h_)` を呼ぶ直前に、後段でアサーションする文字列を `ASSERT_NO_THROW(waitForOutput(recv_h_, "<期待文字列>", <timeout>));` で明示的に待つこと。送信側の終了待ち (`waitForExit(send_h_, ...)`) だけでは、受信側の出力タイミングを保証しません。

```cpp
// 送信側の終了を待っただけでは受信側の出力完了は保証されない
EXPECT_EQ(0, waitForExit(send_h_, 5000));

// 受信側が実際にペイロードを出力するまで待ってから割り込む
ASSERT_NO_THROW(waitForOutput(recv_h_, "<期待文字列>", 3000));

interruptProcess(recv_h_);
waitForExit(recv_h_, 3000);
```

### 実際の修正事例

CI ログ (`prompt/logs_77673940312.zip`) 解析で最初に見つかった事例と、同一ファイル内の横展開監査で見つかった事例です。いずれも本パターンの欠落が原因でした。

| テスト名 | 欠落していた待機 |
|---|---|
| `encrypted_n1_client_reaches_connected_before_send` | `waitForOutput(recv_h_, "n1-connected-ok", ...)` |
| `bidir_echo` | `waitForOutput(recv_h_, "bidir-test", ...)` |
| `encrypted_n1_bad_tag_does_not_consume_peer_slot` | `waitForOutput(recv_h_, "n1-secure-ok", ...)` |
| `tcp_bidir_connects_without_periodic_health_ping` | `waitForOutput(recv_h_, "tcp-before-connected", ...)` |
| `tcp_bidir_without_periodic_health_ping_ignores_timeout` | `waitForOutput(recv_h_, "tcp-timeout-ignored", ...)` |

`encrypted_tcp_bidir_stays_healthy_and_receives` は、このレース パターンを過去に踏んだ形跡があり、対処済みのコード上に経緯コメントが残されています。同種の不具合が繰り返し発生していることから、本注意点を README として明文化しました。

### 新規テスト追加時のチェックリスト

- `recv_h_`/`send_h_` いずれかの標準出力・標準エラーの内容を `getStdout()` / `getStderr()` でアサーションする場合、対象プロセスへの `interruptProcess()` や次の操作を行う前に、必ずその文字列を `waitForOutput()` で待ってから進めること。
- `waitForExit()` は「プロセスが正常終了した」ことの保証にはなるが、**別プロセスの出力反映** を保証しないことに注意する。送信側の `waitForExit()` を待っても、受信側の出力が確定するとは限らない。
- 否定チェック (`EXPECT_EQ(string::npos, ...)`、「出力されていないこと」の確認) のみの場合はこのレースの対象外です。

## 参考

- プロセス制御 API の詳細: `framework/testfw/docs/process-control-api.md`
- テスト対象: `app/porter/prod/src/cmd/porter-test`
