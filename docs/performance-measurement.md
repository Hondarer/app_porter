# porter の性能測定

porter の変更が、送受信の性能とリソースの使い方へ与える影響を、同じ条件で測って比べるための手順です。  
測定には、公開 API だけを使うコマンド `porter-bench` と、結果を集計するスクリプトを使います。

## 目的

次の指標を、通信条件ごとに測ります。

- 送信 API の所要時間、送信から受信コールバックまでの遅延、スループット、CPU 時間
- 要求と返信の往復時間 (N:1 の双方向 UDP)
- 受信コールバックが遅い場合の通知遅延と、保持メモリの増加量
- 接続と切断を反復した場合のメモリ、ハンドル、スレッドの増加傾向

CI では `porter-bench` のビルドだけを行い、測定は実行しません。  
測定値は、実行する環境の負荷に左右されるため、変更の前後を同じ環境で交互に実行して比べます。

## 構成

| 場所 | 内容 |
|---|---|
| [prod/src/cmd/porter-bench/](../prod/src/cmd/porter-bench/porter-bench.cc) | 測定コマンド。`porter-test` と同じく、通常の `make` でビルドします。 |
| [bin/porter_bench_ab.py](../bin/porter_bench_ab.py) | 2 つ以上の実行ファイルを、同じ条件で交互に実行します。 |
| [bin/porter_bench_aggregate.py](../bin/porter_bench_aggregate.py) | 結果の CSV から、最小、中央値、最大を 1 つの CSV にまとめます。 |
| [bin/porter_bench_summarize.py](../bin/porter_bench_summarize.py) | 集計した CSV から、2 つの版の主要な指標を並べます。 |
| [bin/porter_bench_summarize_variants.py](../bin/porter_bench_summarize_variants.py) | 版ごとの結果の CSV から、各回の値の統計量を、複数の版を並べて集計します。 |
| [bin_test/test_porter_bench.py](../bin_test/test_porter_bench.py) | 上記のスクリプトの単体テストです。 |

Table: 性能測定の構成

## ビルド

`app/porter` で、通常どおり `make` を実行します。  
成果物は `prod/cbin/` に出力します。Linux では `porter-bench`、Windows では `porter-bench.exe` です。

```bash
make -C app/porter
```

ビルド構成は makefw の既定 (`RelWithDebInfo`) です。比べる版は、すべて同じ構成でビルドしてください。  
構成の指定方法は [ビルド構成の指定方法](../../../framework/makefw/docs/build-configurations.md) を参照してください。

porter は共有ライブラリ (Linux は `libporter.so`、Windows は `porter.dll`) です。  
実行ファイルだけを退避しても、変更前の porter は残りません。比較の手順で、共有ライブラリも退避します。

## 引数

```text
porter-bench --cond <p1|p2|p3|p4|p5> [options]
```

| 引数 | 説明 | 既定値 |
|---|---|---|
| `--cond <p1\|p2\|p3\|p4\|p5>` | 測定条件 (必須)。 | - |
| `--label <name>` | CSV の `version` 列に記録する名前。英数字、`.`、`_`、`-` を使えます。 | `current` |
| `--bytes <N>` | ペイロード長 [byte] (16 以上)。 | p1、p2、p4、p5 は `64`、p3 は `5000` |
| `--flags <normal\|compress\|blocking\|compress_blocking>` | p1 と p5 の送信フラグ。 | `normal` |
| `--threads <N>` | p1 と p5 の送信スレッド数。 | `1` |
| `--count <N>` | 1 回の実行で測る件数 (p4 はサイクル数)。 | p1、p3、p5 は `10000`、p2 は `5000`、p4 は `1000` |
| `--runs <R>` | 同じプロセスの中で繰り返す回数。 | `5` |
| `--warmup <W>` | 各回の先頭で測定から除く件数。 | p1、p2、p3、p5 は `1000`、p4 は `10` |
| `--inflight <K>` | p1、p2、p5 の、スレッドあたりの最大の未完了件数。 | `16` |
| `--port <P>` | ループバック `127.0.0.1` の基準ポート番号。 | `19000` |
| `--reply-mode <cb\|ext>` | p2 の返信方式。`cb` は受信コールバックの中で返信し、`ext` は別スレッドが返信します。 | `ext` |
| `--proto <udp\|tcp>` | p4 の通信プロトコル。 | `udp` |
| `--tcp-type <bidir\|oneway>` | p5 と、p4 (`--proto tcp`) の TCP の種別。 | `oneway` |
| `--receive-buffer-limit <on\|off>` | UDP の受信側で、OS の受信バッファーに基づく送出上限を使うかどうか。`off` は `flow_control_receive_buffer_percent` を無効にします。 | `on` |
| `--cb-sleep-ms <ms>` | p3 の受信コールバックで待つ時間 [ms]。 | `1` |
| `--out <path>` | CSV の出力先。 | 標準出力 |
| `--no-header` | CSV のヘッダー行を出力しません。 | 出力する |
| `--trace-level <none\|error\|warning\|info\|debug>` | porter のトレースを標準エラー出力へ出すレベル。 | `warning` |

Table: porter-bench の引数

終了コードは、測定に成功して CSV を出力した場合が 0、引数の誤りまたは測定の失敗が 1 です。失敗した場合は CSV を出力しません。  
測定には、トレースの出力が影響しないよう、`--trace-level none` を指定することを推奨します。`porter_bench_ab.py` は常に指定します。

## 測定条件

| 条件 | 内容 |
|---|---|
| p1 | `POTR_TYPE_UNICAST` による片方向の UDP 送信です。送信スレッドごとに、未完了の件数を `--inflight` 件までに制限します。 |
| p2 | `POTR_TYPE_UNICAST_BIDIR_N1` のサーバーへ、4 つのクライアント (`POTR_TYPE_UNICAST_BIDIR`) が要求を送り、サーバーが返信します。 |
| p3 | `POTR_TYPE_UNICAST` で、受信コールバックが 1 件ごとに `--cb-sleep-ms` 待ちます。送信は間隔を空けません。件数に達するか、最後の受信から 5 [s] 無通知になると完了とし、さらに 1 [s] 後の保持メモリの増加量を測ります。 |
| p4 | open、`POTR_EVENT_CONNECTED` の待ち、1 件の送受信、close を繰り返します。ポートはサイクルごとに `基準ポート + (サイクル番号 % 200)` へ分散します。100 サイクルごとと最後に、メモリ、ハンドル数、スレッド数を記録し、全サイクルの最小二乗法の傾きを求めます。 |
| p5 | p1 と同等の負荷を TCP (`POTR_TYPE_TCP_BIDIR` または `POTR_TYPE_TCP`) で実行します。送信、受信、close の各段階のエラーを標準エラー出力へ出します。 |

Table: 測定条件

測定に使う設定は、porter-bench が内部で組み立てます。構成ファイルは使いません。  
フロー制御は無効 (`flow_control_capacity` が 0) です。フロー制御の影響は、この測定には含まれません。

## 出力

CSV の列は `os,version,cond,params,run,metric,value` です。

| 列 | 内容 |
|---|---|
| `os` | `windows` または `linux`。 |
| `version` | `--label` の値。 |
| `cond` | `P1` から `P5`。 |
| `params` | `;` 区切りの条件パラメーター。 |
| `run` | `1` から `--runs` までの各回の番号に続けて、条件全体の `min`、`median`、`max`。 |
| `metric`、`value` | 指標の名前と値。 |

Table: CSV の列

指標は、条件ごとに次のとおりです。

| 条件 | 指標 |
|---|---|
| p1、p5 | `throughput_msg_s`、`api_p50_us`、`api_p95_us`、`api_p99_us`、`notify_p50_us`、`notify_p95_us`、`notify_p99_us`、`cpu_time_ms`、`send_full_count` |
| p2 | `rtt_p50_us`、`rtt_p95_us`、`rtt_p99_us`、`throughput_msg_s`、`dropped_count`、`send_full_count` |
| p3 | `notify_p50_us`、`notify_p95_us`、`notify_p99_us`、`notify_max_us`、`peak_rss_inc_bytes`、`peak_priv_inc_bytes`、`after1s_rss_inc_bytes`、`after1s_priv_inc_bytes`、`dropped_count`、`send_full_count` |
| p4 | `close_p50_us`、`close_p95_us`、`close_p99_us`、`rss_slope_bytes_per_cycle`、`priv_slope_bytes_per_cycle`、`handles_slope_per_cycle`、`threads_slope_per_cycle`、およびチェックポイント (`c0`、`c100`、...) ごとの `rss_inc_bytes_c<N>`、`priv_inc_bytes_c<N>`、`handles_c<N>`、`threads_c<N>` |

Table: 条件ごとの指標

### 送信キューが満杯の場合の扱い

待ち合わせを指定しない送信 (`flags` に `POTR_SEND_BLOCKING` を含めない送信) は、送信キューに空きがないと、待たずに `POTR_ERR_FULL` を返します。  
porter-bench は、`POTR_ERR_FULL` を受けると、少し待って同じメッセージを送り直します。待ちは、最初の 32 回は `yield`、以降は 1 [ms] の `sleep` です。送り直しの上限を超えた場合は、測定を失敗とします。

- 送信時刻は送り直しのたびに取り直します。`api_*` は、受け付けられた呼び出しの所要時間であり、送り直しで待った時間を含みません。`notify_*` は、受け付けられた呼び出しの時刻から測ります。
- `throughput_msg_s` は、送り直しで待った時間を含む経過時間で求めます。
- 送り直した回数は `send_full_count` に出力します。p1、p3、p5 は測定の区間だけを数え、p2 はウォーム アップと返信を含む全区間を数えます。p4 は出力しません。
- `POTR_SEND_BLOCKING` を指定した送信は、空きが生じるまで porter が待つため、`POTR_ERR_FULL` を返しません。

`send_full_count` が 0 でない版どうしを比べる場合は、スループットの差が porter の処理能力の差か、送り直しの待ちの差かを、この値と合わせて判断してください。

## 変更の前後を比べる手順

比べたい 2 つのコミットで、それぞれ porter をビルドし、実行ファイルと共有ライブラリを退避して、同じ条件で交互に実行します。  
次の例は、Linux で、変更前のコミットを `base`、変更後のコミットを `target` として比べます。

1. 変更前のコミットを取り出し、ビルドして、実行ファイルと共有ライブラリを退避します。

    ```bash
    git -C app/porter switch --detach <変更前のコミット>
    make -C app/porter
    mkdir -p ~/porter-bench/base
    cp app/porter/prod/cbin/porter-bench app/porter/prod/lib/libporter.so ~/porter-bench/base/
    ```

2. 変更後のコミットで、同じ手順を繰り返します。

    ```bash
    git -C app/porter switch --detach <変更後のコミット>
    make -C app/porter
    mkdir -p ~/porter-bench/target
    cp app/porter/prod/cbin/porter-bench app/porter/prod/lib/libporter.so ~/porter-bench/target/
    ```

3. 2 つの実行ファイルを、同じ条件で交互に実行します。

    ```bash
    python3 app/porter/bin/porter_bench_ab.py \
        --exe base=$HOME/porter-bench/base/porter-bench \
        --exe target=$HOME/porter-bench/target/porter-bench \
        --out-dir $HOME/porter-bench/results --matrix basic --port 29000
    ```

4. 結果を集計します。

    ```bash
    python3 app/porter/bin/porter_bench_summarize_variants.py \
        $HOME/porter-bench/results $HOME/porter-bench/summary.csv $HOME/porter-bench/summary.md \
        --variants base,target
    ```

Windows では、実行ファイルを `porter-bench.exe`、共有ライブラリを `porter.dll` とし、同じ手順で退避します。  
`porter_bench_ab.py` は、Windows では実行ファイルのディレクトリを OS が先に探すため、退避した `porter.dll` が使われます。

cplat など、porter が依存するほかのライブラリも変更の対象に含む場合は、そのライブラリも同じディレクトリへ退避してください。  
退避しなかったライブラリは、環境の `LD_LIBRARY_PATH` (Linux) または `PATH` (Windows) で見つかる現在の版が使われ、比較の対象になりません。

### 交互に実行するスクリプト

`porter_bench_ab.py` は、条件を外側、版を内側にして逐次実行します。時間経過による環境の変化が、特定の版へ偏らないようにするためです。  
複数のスクリプトを同時に実行しないでください。同時に動く負荷で測定値が変わります。

| 引数 | 説明 | 既定値 |
|---|---|---|
| `--exe <版の名前>=<パス>` | 版の名前と実行ファイルのパス。2 回以上指定します。先頭が基準の版です。 | - |
| `--out-dir <dir>` | 結果の出力ディレクトリ。`<版の名前>/<条件名>.csv` に結果を出力します。標準出力は `.out`、標準エラー出力は `.err` に保存します。 | - |
| `--matrix <basic\|tcp\|full>` | 測定する条件の行列。 | `basic` |
| `--port <P>` | ループバックの基準ポート番号。 | `29000` |
| `--timeout <s>` | 1 回の実行の制限時間 [s]。 | `900` |

Table: porter_bench_ab.py の引数

| 行列 | 内容 |
|---|---|
| `basic` | p1 から p5 を、既定の引数で 1 条件ずつ (5 条件)。 |
| `tcp` | TCP の送信方式を、p4 (TCP) を 1 条件、p5 (片方向) をペイロード長 {64, 5000} と送信フラグ 4 種とスレッド数 {1, 4} の組み合わせで 16 条件、p5 (双方向) を 1 条件の、合わせて 18 条件。 |
| `full` | `basic` と `tcp` を、重複を除いて結合 (22 条件)。 |

Table: 測定する条件の行列

実行の記録は `<出力ディレクトリ>/run-log.txt` に追記します。  
1 回でも失敗または時間切れがあると、終了コードは 1 です。失敗した条件は、`.err` を確認してください。  
同じ出力ディレクトリへ再実行すると、同名のファイルを上書きします。以前の結果を残す場合は、別のディレクトリへ移してください。

### 集計のスクリプト

結果は、次の 3 つのスクリプトで集計します。いずれも、標準ライブラリだけで動作します。

- `porter_bench_summarize_variants.py` は、`<結果ディレクトリ>/<版>/<条件名>.csv` の各回の値から、条件と指標ごとに中央値、最小、最大を求めます。先頭の版を基準とし、ほかの版に基準との変化率を付けます。3 番目以降の版には、直前の版との変化率も付けます。最大が最小の 3 倍を超える組み合わせは、ばらつきが大きい条件として列挙します。
- `porter_bench_aggregate.py` は、結果ディレクトリ配下の CSV から、`min`、`median`、`max` の行だけを 1 つの CSV にまとめます。版は CSV の `version` 列で区別するため、ディレクトリの配置に依存しません。
- `porter_bench_summarize.py` は、`porter_bench_aggregate.py` の出力から、`--base` と `--target` に指定した 2 つの版の中央値を、条件ごとに並べます。

```bash
python3 app/porter/bin/porter_bench_aggregate.py ~/porter-bench/results ~/porter-bench/aggregate.csv
python3 app/porter/bin/porter_bench_summarize.py ~/porter-bench/aggregate.csv --base base --target target
```

## 結果の見方

- 差は、中央値どうしの変化率で見ます。各回の最小と最大も併せて見て、差がばらつきの範囲に収まっていないかを確かめます。
- 最大が最小の 3 倍を超える組み合わせは、ばらつきが大きく、その条件の差は判断を保留します。短い実行では、1 回の停止が全体を支配することがあります。
- `throughput_msg_s` は大きいほど、`*_us` と `cpu_time_ms` は小さいほど良い値です。
- `send_full_count` が増えた場合は、送信キューが満杯になる回数が増えています。スループットや `notify_*` の差の原因を調べる手がかりにしてください。
- `dropped_count` が 0 でない場合は、測定の区間で期待した件数が受信されていません。差の評価より先に、原因を調べてください。
- p4 の `*_slope_*` は、1 サイクルあたりの増加量です。0 に近ければ増加していません。正の値が続く場合は、メモリ、ハンドル、スレッドの解放漏れの疑いがあります。
- p3 の `after1s_*` は、すべての通知を処理して 1 [s] 後の増加量です。`peak_*` との差が大きい場合は、一時的に保持した領域が解放されたことを示します。

## 測定条件を整える

- Windows と Linux (WSL を含む) の測定を、同時に実行しないでください。CPU を共有します。
- 測定中は、`make test` などの負荷の高い処理を、同じマシンで実行しないでください。
- 比べる版は、同じ構成 (`CONFIG`)、同じコンパイラ、同じ依存ライブラリでビルドしてください。
- 別のプロセスが使っているポートは指定できません。`--port` は、ほかのサービスと重ならない値にしてください。

## 外部の根拠

porter-bench は、メモリ使用量、ハンドル数、スレッド数を、OS の API で取得します。

| OS | 取得する値 | 使用する API |
|---|---|---|
| Linux | 常駐メモリ、ピーク、匿名メモリ、スレッド数 | `/proc/self/status` の `VmRSS`、`VmHWM`、`RssAnon`、`Threads` |
| Linux | ファイル記述子の数 | `/proc/self/fd` の列挙 |
| Windows | 常駐メモリ、ピーク、専用メモリ | `GetProcessMemoryInfo()` の `WorkingSetSize`、`PeakWorkingSetSize`、`PrivateUsage` |
| Windows | ハンドル数、スレッド数 | `GetProcessHandleCount()`、`CreateToolhelp32Snapshot()` |

Table: 資源の取得に使う API

Linux の `RssAnon` は、Windows の `PrivateUsage` に相当する値として扱います。両 OS の値は同じ意味ではないため、OS をまたいだ絶対値の比較には使わないでください。
