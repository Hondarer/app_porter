# Windows と WSL の相互接続試験

Windows 上の porter と WSL 上の porter が、同じ構成ファイルの定義で互いに通信できることを確かめる試験です。  
認証 2 種 (`psk`、`cert`) と通信種別 6 種の、合わせて 12 組み合わせを実行します。

## 目的

Windows (MSVC でビルド) と Linux (GCC でビルド) で、次の内容が一致していることを確かめます。

- 暗号化と認証 (PSK、証明書) のメッセージ形式
- UDP (`unicast`、`unicast_bidir`、`unicast_bidir_n1`、`multicast`) と TCP (`tcp`、`tcp_bidir`) のプロトコル
- 構成ファイルの定義の解釈 (アドレス、ポート、マルチキャスト グループ)

現在の試験の向きは、Windows が sender、WSL が receiver です。`unicast_bidir` だけは、受信を確認したあとで WSL から Windows へも送信します。

## 前提

- Windows と WSL の両方で、porter と依存ライブラリ (cplat、cjson、zlib、openssl) をビルド済みであること。
- リポジトリが Windows 側と WSL 側のそれぞれに存在し、どちらも `app/porter/prod/cbin/` に `porter-test.exe` (Windows) と `porter-test` (WSL) があること。
- スクリプトは Windows 上の Python (3.9 以降) で実行すること。WSL の Python では実行できません。`wsl.exe` を呼び出すためです。
- Windows と WSL が、互いのアドレスで通信できること。Windows ファイアウォールが、`porter-test.exe` の受信と、WSL からの UDP と TCP を許可していること。
- マルチキャストは、WSL 側のアドレスの NIC でグループへ参加します。

## 引数

スクリプトは [bin/interop_matrix.py](../bin/interop_matrix.py) です。環境固有の値は、すべて引数で渡します。

| 引数 | 必須 | 説明 |
|---|---|---|
| `--windows-root` | 必須 | Windows 側のリポジトリ ルート |
| `--wsl-root` | 必須 | WSL 側のリポジトリ ルートの絶対パス |
| `--wsl-distribution` | 必須 | WSL のディストリビューション名 (`wsl.exe -l` で確認できます) |
| `--windows-address` | 必須 | Windows 側のアドレス。WSL から到達できる IPv4 |
| `--wsl-address` | 必須 | WSL 側のアドレス。Windows から到達できる IPv4 |
| `--output-directory` | 任意 | 設定、ログ、`results.json` の出力先。省略時は、一時ディレクトリを新規に作ります |
| `auth/type` (位置引数、複数可) | 任意 | 実行する組み合わせ。省略時は 12 組み合わせすべてを実行します |

Table: interop_matrix.py の引数

組み合わせは `psk/unicast` の形式で指定します。認証は `psk` または `cert`、通信種別は `unicast`、`unicast_bidir`、`unicast_bidir_n1`、`tcp`、`tcp_bidir`、`multicast` です。  
絞り込んでも、サービス ID とポートは 12 組み合わせを実行したときと同じです。

## 実行例

Windows の PowerShell で、リポジトリ ルートから実行します。

```powershell
python app\porter\bin\interop_matrix.py `
    --windows-root D:\repos\c-modernization-kit `
    --wsl-root /home/user/c-modernization-kit `
    --wsl-distribution OracleLinux8-Dev `
    --windows-address 172.28.208.1 `
    --wsl-address 172.28.209.184 `
    --output-directory $env:TEMP\porter-interop
```

2 組み合わせだけ実行する場合は、末尾に追加します。

```powershell
python app\porter\bin\interop_matrix.py ... psk/unicast cert/tcp
```

## 判定方法

組み合わせごとに、次の順で実行します。

1. 設定ファイルを、Windows 用と WSL 用の 2 つ生成します。証明書認証では、リポジトリの試験用の証明書 (`app/porter/test/src/libporter/auth_test/data`) を使います。
2. WSL 側で `porter-test receiver` を起動し、`opened successfully` が出るまで待ちます。
3. Windows 側で `porter-test.exe sender` を起動し、同じく開くまで待ちます。開けなかった場合は、`FAIL` (open failed) または `BLOCKED` (open timeout) とします。
4. `unicast_bidir`、`unicast_bidir_n1`、`tcp`、`tcp_bidir` では、WSL のログに接続の確立 (`CONNECTED` または `接続確立`) が出るまで、最大 7 秒待ちます。
5. sender へ `send <トークン>` を入力し、receiver のログにトークンが出れば成功とします。
6. sender が「未接続のため送信できません」で拒否した場合は、1 秒待って送り直します。最大 15 回です。ほかの理由で受信できなかった場合は、送り直さず失敗とします。
7. `unicast_bidir` だけ、手順 5 の成功後に、WSL から Windows へもトークンを送り、Windows のログに出ることを 8 秒以内に確かめます。

すべての判定を満たした組み合わせが `PASS` です。

## 結果の見方

組み合わせごとに、標準出力へ 1 行を出します。

```text
psk/unicast_bidir: PASS (win_to_wsl=True wsl_to_win=True send_attempts=1)
```

- `win_to_wsl`: Windows から WSL への送信を、WSL が受信したか。
- `wsl_to_win`: WSL から Windows への送信を、Windows が受信したか。`unicast_bidir` 以外は `None` (確認しない) です。
- `send_attempts`: 送信を試みた回数。2 以上は、接続の確立前に拒否されて送り直したことを示します。

最後に `TOTAL: <成功数>/<実行数> PASS` を出し、すべて成功した場合は終了コード 0、それ以外は 1 を返します。

| 状態 | 意味 |
|---|---|
| `PASS` | すべての判定を満たした |
| `FAIL` | receiver または sender を開けなかった、または受信できなかった |
| `BLOCKED` | receiver または sender が、制限時間内に開かなかった |
| `ERROR` | 試験の駆動中に例外が起きた (詳細に例外の内容を記録) |

Table: 結果の状態

出力先には、次のファイルが残ります。

| ファイル | 内容 |
|---|---|
| `<auth>-<type>-win.json`、`<auth>-<type>-wsl.json` | 各側が読む構成ファイル |
| `<auth>-<type>.log` | 両側の標準出力。行頭の `win:` と `wsl:` で側を区別します |
| `results.json` | 組み合わせごとの `case`、`status`、`detail` |

Table: 出力先のファイル

失敗した場合は、まず該当する `.log` で、receiver と sender の開始と接続のログを確認してください。

## 単体テスト

設定の生成、パスの変換、引数の解析、コマンド ラインの組み立ては、プロセスを起動せずに確認できます。Linux でも実行できます。

```bash
PYTHONSAFEPATH=1 python3 -m unittest discover -s app/porter/bin_test -p 'test_*.py'
```
