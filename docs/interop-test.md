# Windows と WSL の相互接続試験

Windows 上の porter と WSL 上の porter が、同じ構成ファイルの定義で互いに通信できることを確かめる試験です。  
シナリオは 2 つあります。

- `basic`: 認証 2 種 (`psk`、`cert`) と通信種別 6 種の、合わせて 12 組み合わせで、1 件のメッセージが届くことを確かめます。
- `flow-control`: 受信側を遅らせ、連続して送信してもフロー制御が働き、全件が順序どおりに届くことを確かめます。

どちらのシナリオも、sender と receiver の向きを選べます。

## 目的

Windows (MSVC でビルド) と Linux (GCC でビルド) で、次の内容が一致していることを確かめます。

- 暗号化と認証 (PSK、証明書) のメッセージ形式
- UDP (`unicast`、`unicast_bidir`、`unicast_bidir_n1`、`multicast`) と TCP (`tcp`、`tcp_bidir`) のプロトコル
- 構成ファイルの定義の解釈 (アドレス、ポート、マルチキャスト グループ)

## 向き

`--direction` で、sender と receiver の OS を選びます。

| 値 | sender | receiver |
|---|---|---|
| `windows-to-wsl` (既定) | Windows | WSL |
| `wsl-to-windows` | WSL | Windows |
| `both` | 両方を、この順に続けて実行 | |

Table: 向きの値

向きに応じて、設定ファイルの `src` と `dst` のアドレスを入れ替えます。sender 側の OS のアドレスが `src`、receiver 側の OS のアドレスが `dst` です。  
証明書認証では、OS ではなく役割に対応させます。sender の役割の側が `server.pem` と `porter.test` を、receiver の役割の側が `client.pem` と `urn:porter:peer` を使います。  
`basic` の `unicast_bidir` だけは、受信を確認したあとで receiver から sender へも送信します。  
向きとシナリオの組ごとに、サービス ID とポートの範囲をずらします。続けて実行する試験が、同じポートを使い回さないためです。

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
| `--direction` | 任意 | `windows-to-wsl` (既定)、`wsl-to-windows`、`both` |
| `--scenario` | 任意 | `basic` (既定)、`flow-control` |
| `--burst-count` | 任意 | `flow-control`: `burst` で送る件数。既定は 400 |
| `--burst-bytes` | 任意 | `flow-control`: `burst` の 1 件のバイト数。16 以上 65,535 以下。既定は 16,384 |
| `--recv-delay-ms` | 任意 | `flow-control`: 受信コールバックを 1 件ごとに遅らせる時間 (ms)。既定は 20 |
| `--wait-margin-seconds` | 任意 | `flow-control`: 全件の消費に要する時間 (件数 × 遅れ) に足す待ちの余裕 (秒)。既定は 60 |
| `--burst-nonblocking` | 任意 | `flow-control`: `burst` を非ブロッキングで送り、`POTR_ERR_FULL` を受けて送り直す。省略時は `POTR_SEND_BLOCKING` で送る。UDP の通信種別では、非ブロッキングの連続送信が受信側の OS の受信バッファーをあふれさせ、フロー制御と無関係の欠落と切断が起きることがある |
| `auth/type` (位置引数、複数可) | 任意 | 実行する組み合わせ。省略時は、シナリオの対象すべてを実行します |

Table: interop_matrix.py の引数

組み合わせは `psk/unicast` の形式で指定します。認証は `psk` または `cert`、通信種別は `unicast`、`unicast_bidir`、`unicast_bidir_n1`、`tcp`、`tcp_bidir`、`multicast` です。  
`flow-control` の対象は、`psk/unicast`、`psk/unicast_bidir`、`psk/tcp`、`psk/multicast` の 4 つです。  
絞り込んでも、サービス ID とポートは、全件を実行したときと同じです。

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

WSL が sender の向きだけを実行する場合は、`--direction wsl-to-windows` を追加します。

```powershell
python app\porter\bin\interop_matrix.py ... --direction wsl-to-windows psk/unicast
```

フロー制御のシナリオを、両方の向きで実行する場合は、次のようにします。

```powershell
python app\porter\bin\interop_matrix.py ... --scenario flow-control --direction both
```

## 判定方法 (basic)

組み合わせごとに、次の順で実行します。手順中の「WSL」と「Windows」は、`windows-to-wsl` の場合の割り当てです。`wsl-to-windows` では、sender と receiver の OS を入れ替えます。

1. 設定ファイルを、Windows 用と WSL 用の 2 つ生成します。証明書認証では、リポジトリの試験用の証明書 (`app/porter/test/src/libporter/auth_test/data`) を使います。
2. WSL 側で `porter-test receiver` を起動し、`opened successfully` が出るまで待ちます。
3. Windows 側で `porter-test.exe sender` を起動し、同じく開くまで待ちます。開けなかった場合は、`FAIL` (open failed) または `BLOCKED` (open timeout) とします。
4. `unicast_bidir`、`unicast_bidir_n1`、`tcp`、`tcp_bidir` では、WSL のログに接続の確立 (`CONNECTED` または `接続確立`) が出るまで、最大 7 秒待ちます。
5. sender へ `send <トークン>` を入力し、receiver のログにトークンが出れば成功とします。
6. sender が「未接続のため送信できません」で拒否した場合は、1 秒待って送り直します。最大 15 回です。ほかの理由で受信できなかった場合は、送り直さず失敗とします。
7. `unicast_bidir` だけ、手順 5 の成功後に、WSL から Windows へもトークンを送り、Windows のログに出ることを 8 秒以内に確かめます。

すべての判定を満たした組み合わせが `PASS` です。

## 判定方法 (flow-control)

受信側の構成に `flow_control_capacity` を設定し、遅らせた受信側へ `porter-test` の `burst` で連続して送信します。  
`flow_control_capacity` は、receiver の側 (双方向の通信種別では両端) の構成へ設定します。`send_queue_depth` は既定のままです。

設定値と根拠は、次のとおりです。

| 項目 | 値 | 根拠 |
|---|---|---|
| `flow_control_capacity` | 2 MB (2,097,152 バイト) | 要求のしきい値 (`flow_control_suppress_percent` の既定 80%) は約 1.6 MB。しきい値から容量までの余裕は約 410 KB で、`max_message_size` の既定 (65,535 バイト) 以上であり、構成の検査を満たす |
| `burst` の件数と大きさ | 400 件、各 16,384 バイト (約 6.3 MiB) | 容量の約 3 倍、しきい値の約 4 倍。抑制がなければ、受信側の通知キューが容量を超える量 |
| 受信コールバックの遅れ | 1 件あたり 20 ms | 受信側の消費は約 800 KB/s で、ネットワークの速度より十分に遅い。全件の消費に約 8 秒かかる |
| 待ちの上限 | 件数 × 遅れ + 60 秒 | 抑制と解除の繰り返し (要求の再送周期 200 ms、有効期限 1,000 ms) と、プロセスの起動の揺れを見込む |

Table: flow-control の設定値と根拠

組み合わせごとに、次の順で実行します。

1. 設定ファイルを、Windows 用と WSL 用の 2 つ生成します。
2. receiver を `--recv-delay-ms 20` つきで起動し、続けて sender を起動して、どちらも開くまで待ちます。
3. `unicast_bidir` と `tcp` では、receiver のログに接続の確立が出るまで、最大 7 秒待ちます。
4. sender へ `burst 400 16384` を入力します。sender が接続の確立前で拒否した場合 (`BURST_SENT` の `sent=0`、`result=error=-20`) は、1 秒待って送り直します。最大 15 回です。
5. sender の `BURST_SENT` の行と、receiver の `BURST_RESULT` の行を、待ちの上限まで待ちます。

次のすべてを満たした組み合わせが `PASS` です。

- `BURST_SENT` が `sent=<件数>` かつ `result=ok` である。
- `BURST_RESULT` が `received=<件数> expected=<件数> out_of_order=0` である。
- `burst` の開始以後、sender と receiver のログに切断の通知 (`切断検知`) がない。
- 抑制の開始が、トレースに出ている。sender の `flow control suppressed DATA` または receiver の `requested suppression` のどちらかです。
- 抑制の解除が、トレースに出ている。sender の `flow control resumed DATA` または receiver の `resumed (occupancy` のどちらかです。

結果の詳細には、`sent`、`full` (`POTR_ERR_FULL` を受けた回数)、`elapsed_ms`、`received`、`out_of_order`、`disconnects`、`suppressed`、`resumed` を記録します。

## 結果の見方

組み合わせごとに、標準出力へ 1 行を出します。

```text
psk/unicast_bidir: PASS (win_to_wsl=True wsl_to_win=True send_attempts=1)
```

- `win_to_wsl`: Windows から WSL への送信を、WSL が受信したか。
- `wsl_to_win`: WSL から Windows への送信を、Windows が受信したか。`unicast_bidir` 以外は `None` (確認しない) です。
- `send_attempts`: 送信を試みた回数。2 以上は、接続の確立前に拒否されて送り直したことを示します。

`basic` の詳細は、向きに応じて `win_to_wsl` と `wsl_to_win` の名前が入れ替わります。  
向きまたはシナリオが既定と異なる場合、組み合わせの名前の後ろへ `[wsl-to-windows]` や `[flow-control]` を付けます。  
`flow-control` の 1 行は、次の形です。

```text
psk/tcp [flow-control]: PASS (sent=400 full=37 elapsed_ms=9120 received=400 out_of_order=0 disconnects=0 suppressed=True resumed=True)
```

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

向きが `windows-to-wsl` 以外、またはシナリオが `basic` 以外の場合、ファイル名の `<auth>-<type>` の後ろへ `-wsl-to-windows` や `-flow-control` を足します。

失敗した場合は、まず該当する `.log` で、receiver と sender の開始と接続のログを確認してください。

## 単体テスト

設定の生成 (向きとシナリオを含む)、パスの変換、引数の解析、コマンド ラインの組み立て、`BURST_RESULT` と `BURST_SENT` の行の解析と判定は、プロセスを起動せずに確認できます。Linux でも実行できます。

```bash
PYTHONSAFEPATH=1 python3 -m unittest discover -s app/porter/bin_test -p 'test_*.py'
```
