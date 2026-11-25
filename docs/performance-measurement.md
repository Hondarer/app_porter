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

Table: 性能測定の構成

| 場所 | 内容 |
|---|---|
| [prod/src/cmd/porter-bench/](../prod/src/cmd/porter-bench/porter-bench.cc) | 測定コマンド。`porter-test` と同じく、通常の `make` でビルドします。 |
| [bin/porter_bench_ab.py](../bin/porter_bench_ab.py) | 2 つ以上の実行ファイルを、同じ条件で交互に実行します。 |
| [bin/porter_bench_aggregate.py](../bin/porter_bench_aggregate.py) | 結果の CSV から、最小、中央値、最大を 1 つの CSV にまとめます。 |
| [bin/porter_bench_summarize.py](../bin/porter_bench_summarize.py) | 集計した CSV から、2 つの版の主要な指標を並べます。 |
| [bin/porter_bench_summarize_variants.py](../bin/porter_bench_summarize_variants.py) | 版ごとの結果の CSV から、各回の値の統計量を、複数の版を並べて集計します。 |
| [bin_test/test_porter_bench.py](../bin_test/test_porter_bench.py) | 上記のスクリプトの単体テストです。 |

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
porter-bench --cond <p1|p2|p3|p4|p5|p6|p7> [options]
```

Table: porter-bench の引数

| 引数 | 説明 | 既定値 |
|---|---|---|
| `--cond <p1\|p2\|p3\|p4\|p5\|p6\|p7>` | 測定条件 (必須)。 | - |
| `--label <name>` | CSV の `version` 列に記録する名前。英数字、`.`、`_`、`-` を使えます。 | `current` |
| `--bytes <N>` | ペイロード長 [byte] (16 以上)。 | p1、p2、p4、p5 は `64`、p3 は `5000`、p6、p7 は `1000` |
| `--flags <normal\|compress\|blocking\|compress_blocking>` | p1、p5、p7 の送信フラグ。 | `normal` |
| `--threads <N>` | p1 と p5 の送信スレッド数。 | `1` |
| `--count <N>` | 1 回の実行で測る件数 (p4 はサイクル数)。 | p1、p3、p5、p6、p7 は `10000`、p2 は `5000`、p4 は `1000` |
| `--runs <R>` | 同じプロセスの中で繰り返す回数。 | `5` |
| `--warmup <W>` | 各回の先頭で測定から除く件数。 | p1、p2、p3、p5、p6、p7 は `1000`、p4 は `10` |
| `--inflight <K>` | p1、p2、p5 の、スレッドあたりの最大の未完了件数。 | `16` |
| `--port <P>` | ループバック `127.0.0.1` の基準ポート番号。 | `19000` |
| `--clients <N>` | p2 と p7 のクライアント数 (1〜64)。 | `4` |
| `--stalled-clients <M>` | p7 で受信を止めるクライアント数 (0〜N − 1、先頭の M 個)。健全なクライアントを 1 個以上残します。 | `1` |
| `--stalled-capacity <bytes>` | p7 の停止クライアントの通知キュー容量 [byte] (1 以上)。 | `1048576` |
| `--reply-mode <cb\|ext>` | p2 の返信方式。`cb` は受信コールバックの中で返信し、`ext` は別スレッドが返信します。 | `ext` |
| `--proto <udp\|tcp>` | p4 の通信プロトコル。 | `udp` |
| `--tcp-type <bidir\|oneway>` | p5 と、p4 (`--proto tcp`) の TCP の種別。 | `oneway` |
| `--receive-buffer-limit <on\|off>` | UDP の受信側で、OS の受信バッファーに基づく送出上限を使うかどうか。`off` は `flow_control_receive_buffer_percent` を無効にします。 | `on` |
| `--cb-sleep-ms <ms>` | p3、p6 の遅い受信者、p7 の停止クライアントの受信コールバックで待つ時間 [ms]。 | p3、p6 は `1`、p7 は `1000` |
| `--receivers <N>` | p6 のマルチキャスト受信者数 (1〜128)。 | `3` |
| `--slow-receivers <M>` | p6 で受信コールバックを `--cb-sleep-ms` だけ遅らせる受信者数 (先頭の M 個)。 | `0` |
| `--mcast-group <ip>` | p6 のマルチキャスト グループ アドレス。 | `239.0.0.1` |
| `--mcast-if <ip>` | p6 のマルチキャスト送受信インターフェイス。 | `127.0.0.1` |
| `--run-timeout-s <S>` | p2、p6、p7 の 1 回あたりの打ち切り時間 [s]。 | `60` |
| `--config <key>=<value>` | `potr_global_config` の設定項目を名前で上書きします (複数回指定可)。p1 から p7 のすべての条件に適用します。`flow_control_receive_buffer_percent` には無効を表す `-1` も指定できます。 | なし |
| `--out <path>` | CSV の出力先。 | 標準出力 |
| `--no-header` | CSV のヘッダー行を出力しません。 | 出力する |
| `--trace-level <none\|error\|warning\|info\|debug>` | porter のトレースを標準エラー出力へ出すレベル。 | `warning` |

終了コードは、測定に成功して CSV を出力した場合が 0、引数の誤りまたは測定の失敗が 1 です。失敗した場合は CSV を出力しません。  
測定には、トレースの出力が影響しないよう、`--trace-level none` を指定することを推奨します。`porter_bench_ab.py` は常に指定します。

## 測定条件

Table: 測定条件

| 条件 | 内容 |
|---|---|
| p1 | `POTR_TYPE_UNICAST` による片方向の UDP 送信です。送信スレッドごとに、未完了の件数を `--inflight` 件までに制限します。 |
| p2 | `POTR_TYPE_UNICAST_BIDIR_N1` のサーバーへ、`--clients` 個のクライアント (`POTR_TYPE_UNICAST_BIDIR`) が要求を送り、サーバーが返信します。 |
| p3 | `POTR_TYPE_UNICAST` で、受信コールバックが 1 件ごとに `--cb-sleep-ms` 待ちます。送信は間隔を空けません。件数に達するか、最後の受信から 5 [s] 無通知になると完了とし、さらに 1 [s] 後の保持メモリの増加量を測ります。 |
| p4 | open、`POTR_EVENT_CONNECTED` の待ち、1 件の送受信、close を繰り返します。ポートはサイクルごとに `基準ポート + (サイクル番号 % 200)` へ分散します。100 サイクルごとと最後に、メモリ、ハンドル数、スレッド数を記録し、全サイクルの最小二乗法の傾きを求めます。 |
| p5 | p1 と同等の負荷を TCP (`POTR_TYPE_TCP_BIDIR` または `POTR_TYPE_TCP`) で実行します。送信、受信、close の各段階のエラーを標準エラー出力へ出します。 |
| p6 | 同一プロセス内で `POTR_TYPE_MULTICAST` の送信者 1 つと受信者 `--receivers` 個をループバックで動かします。先頭の `--slow-receivers` 個の受信者は受信コールバックで `--cb-sleep-ms` 待ちます。全受信者の受信報告が送信者に届いた後に、待ち合わせなしの連続送信を行い、健全な受信者の全員が最後のメッセージを受信するまでの処理件数と切断回数を測ります。 |
| p7 | 同一プロセス内で `POTR_TYPE_UNICAST_BIDIR_N1` のサーバー 1 つと `--clients` 個のクライアントを動かします。サーバーが `POTR_PEER_ALL` へ連続送信し、停止クライアントの切断と健全なクライアントの受信を測ります。 |

測定に使う設定は、porter-bench が内部で組み立てます。構成ファイルは使いません。  
通知キュー容量によるフロー制御は、p7 の停止クライアントを除いて既定で無効 (`flow_control_capacity` が 0) です。グローバル設定を変更して測定する場合は、`--config <key>=<value>` を指定します。

### p2 の完了条件と失敗条件

p2 は、サーバーと全クライアントの接続完了後にウォーム アップを開始し、全クライアントのウォーム アップ完了を待ち合わせてから測定対象の要求と返信を計測します。  
送信完了後、全件の返信がそろうか、最後の返信または送信完了から 2 [s] 無通知になるとその回を完了します。

次のいずれかに該当した場合は、再接続を待たずに全クライアントの待機と返信処理スレッドを中断し、標準エラー出力へ失敗の理由を 1 行出力して測定失敗 (終了コード 1、CSV は出力しない) として終了します。

- 送信で致命的なエラー (`POTR_ERR_DISCONNECTED` や、送り直しの上限を超えた `POTR_ERR_FULL` など) が起きた場合
- 接続待ちまたはウォーム アップを含む測定中に、サーバー側またはクライアント側で予期しない切断通知 (`POTR_EVENT_DISCONNECTED`) やエラー通知 (`POTR_EVENT_ERROR`) を受けた場合
- ウォーム アップ開始から `--run-timeout-s` に達しても、待機または測定が完了しない場合

### p7 の停止クライアントと完了条件

p7 は、サーバーと全クライアントの接続後、クライアント番号をサーバー側のピア ID と対応付けます。  
先頭の `--stalled-clients` 個は、受信コールバックで `--cb-sleep-ms` 待ちます。  
`--config` を適用した設定をハンドルごとにコピーし、停止クライアントだけ `flow_control_capacity` を `--stalled-capacity` で上書きします。  
健全なクライアントとサーバーには容量を追加設定せず、`--config flow_control_capacity=...` の指定があればその値を使います。

サーバーは、ウォーム アップ `--warmup` 件と測定対象 `--count` 件を連続して送信します。  
健全なクライアントの全員が測定対象の件数と末尾の通番を受信するか、最初の送信から `--run-timeout-s` に達すると終了します。  
時間切れでも CSV を出力し、`throughput_msg_s` は 0 とします。  
待ち合わせ付き送信にも期限を適用し、終了時には `close` で送信とコールバックの待機を解除します。  
接続やクライアント番号の通知に失敗した場合は、測定失敗として CSV を出力しません。

`throughput_msg_s` は、ウォーム アップの送信後、最初の測定メッセージの送信から健全なクライアント全員が受信するまでの時間で、`--count` を割った値です。  
クライアント数は掛けません。  
受信件数は p6 と同じく、ペイロードの通番が測定範囲内にある通知を数えます。  
重複や順序の独立した指標はなく、欠落した分は受信件数の不足として現れます。

Table: p7 の受信と切断の指標

| 指標 | 内容 |
|---|---|
| `healthy_received_min` | 健全なクライアントの測定対象の受信件数の最少。 |
| `healthy_disconnect_count` | 健全なクライアントについて、サーバー側とクライアント側で受けた切断通知の合計。 |
| `stalled_received_min` | 停止クライアントの測定対象の受信件数の最少。停止クライアントが 0 個なら 0。 |
| `stalled_disconnect_count` | サーバー側で切断通知を受けた停止クライアントの数。同じクライアントは 1 個として数えます。 |
| `stalled_disconnect_ms` | 最初の送信から、サーバー側で最初の停止クライアントの切断通知を受けるまでの時間 [ms]。通知がなければ -1。 |
| `send_full_count` | ウォーム アップを含む全送信区間で、`POTR_ERR_FULL`、`POTR_ERR_DISCONNECTED`、`POTR_ERR_TIMEOUT` を受けて再試行した回数。 |

切断通知は最初の送信から完了または時間切れまでを数え、測定終了のためにハンドルを閉じた際の通知は含めません。  
受信を止めたピアを切断する機能が無効な構成でも、時間切れまで測定して値を出力します。

```bash
# 4 クライアントのうち 1 つを止め、残る 3 つの全件受信を確認します。
porter-bench --cond p7 --clients 4 --stalled-clients 1 --bytes 1000 --count 10000 \
    --warmup 1000 --cb-sleep-ms 1000 --stalled-capacity 1048576 --run-timeout-s 60 \
    --config flow_control_excluded_receiver_percent=25 --runs 1 --trace-level none --out p7.csv

# p2 のクライアント数を変えて要求と返信を測ります。
porter-bench --cond p2 --clients 64 --reply-mode ext --runs 1 --trace-level none --out p2-c64.csv
```

p7 の例は、4 台中 1 台の切断を許可するため、割合の上限を 25 [%] に設定しています。  
p7 の上記条件では、`healthy_received_min=10000`、`healthy_disconnect_count=0`、`stalled_disconnect_count=1` と、0 以上の `stalled_disconnect_ms` を確認します。

## 出力

CSV の列は `os,version,cond,params,run,metric,value` です。

Table: CSV の列

| 列 | 内容 |
|---|---|
| `os` | `windows` または `linux`。 |
| `version` | `--label` の値。 |
| `cond` | `P1` から `P7`。 |
| `params` | `;` 区切りの条件パラメーター。 |
| `run` | `1` から `--runs` までの各回の番号に続けて、条件全体の `min`、`median`、`max`。 |
| `metric`、`value` | 指標の名前と値。 |

`params` は `key=value` を `;` で区切ります。  
`--config` の上書きは、すべての条件で `cfg.<key>=<value>` として指定順に末尾へ追加します。  
同じキーを複数回指定した場合もすべて残し、設定には最後の値を適用します。  
無効を表す `-1` は `-1` として記録します。  
例えば `--config window_size=64 --config flow_control_capacity=1048576` は、`;cfg.window_size=64;cfg.flow_control_capacity=1048576` になります。  
上書きを指定しなければ `cfg.` の項目は付けません。  
p6 の `cb_sleep_ms` は、遅い受信者が 1 個以上いる場合だけ記録します。  
p3 の `cb_sleep_ms` は常に記録します。

指標は、条件ごとに次のとおりです。

Table: 条件ごとの指標

| 条件 | 指標 |
|---|---|
| p1、p5 | `throughput_msg_s`、`api_p50_us`、`api_p95_us`、`api_p99_us`、`notify_p50_us`、`notify_p95_us`、`notify_p99_us`、`cpu_time_ms`、`send_full_count` |
| p2 | `rtt_p50_us`、`rtt_p95_us`、`rtt_p99_us`、`throughput_msg_s`、`dropped_count`、`send_full_count` |
| p3 | `notify_p50_us`、`notify_p95_us`、`notify_p99_us`、`notify_max_us`、`peak_rss_inc_bytes`、`peak_priv_inc_bytes`、`after1s_rss_inc_bytes`、`after1s_priv_inc_bytes`、`dropped_count`、`send_full_count` |
| p4 | `close_p50_us`、`close_p95_us`、`close_p99_us`、`rss_slope_bytes_per_cycle`、`priv_slope_bytes_per_cycle`、`handles_slope_per_cycle`、`threads_slope_per_cycle`、およびチェックポイント (`c0`、`c100`、...) ごとの `rss_inc_bytes_c<N>`、`priv_inc_bytes_c<N>`、`handles_c<N>`、`threads_c<N>` |
| p6 | `throughput_msg_s`、`healthy_received_min`、`healthy_disconnect_count`、`slow_received_min`、`slow_disconnect_count`、`send_full_count` |
| p7 | `throughput_msg_s`、`healthy_received_min`、`healthy_disconnect_count`、`stalled_received_min`、`stalled_disconnect_count`、`stalled_disconnect_ms`、`send_full_count` |

### 送信キューが満杯の場合の扱い

待ち合わせを指定しない送信 (`flags` に `POTR_SEND_BLOCKING` を含めない送信) は、送信キューに空きがないと、待たずに `POTR_ERR_FULL` を返します。  
porter-bench は、`POTR_ERR_FULL` を受けると、少し待って同じメッセージを送り直します。待ちは、最初の 32 回は `yield`、以降は 1 [ms] の `sleep` です。p7 は毎回 1 [ms] 待ちます。送り直しの上限を超えた場合は、測定を失敗とします (p6、p7 は `--run-timeout-s` で打ち切り、その回の `throughput_msg_s` を 0 とします)。

- 送信時刻は送り直しのたびに取り直します。`api_*` は、受け付けられた呼び出しの所要時間であり、送り直しで待った時間を含みません。`notify_*` は、受け付けられた呼び出しの時刻から測ります。
- `throughput_msg_s` は、送り直しで待った時間を含む経過時間で求めます。
- 送り直した回数は `send_full_count` に出力します。p1、p3、p5、p6 は測定の区間だけを数え、p7 はウォーム アップを含む全区間、p2 はウォーム アップと返信を含む全区間を数えます。p4 は出力しません。
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

Table: porter_bench_ab.py の引数

| 引数 | 説明 | 既定値 |
|---|---|---|
| `--exe <版の名前>=<パス>` | 版の名前と実行ファイルのパス。2 回以上指定します。先頭が基準の版です。 | - |
| `--out-dir <dir>` | 結果の出力ディレクトリ。`<版の名前>/<条件名>.csv` に結果を出力します。標準出力は `.out`、標準エラー出力は `.err` に保存します。 | - |
| `--matrix <basic\|tcp\|full>` | 測定する条件の行列。 | `basic` |
| `--port <P>` | ループバックの基準ポート番号。 | `29000` |
| `--timeout <s>` | 1 回の実行の制限時間 [s]。 | `900` |

Table: 測定する条件の行列

| 行列 | 内容 |
|---|---|
| `basic` | p1 から p5 を、既定の引数で 1 条件ずつ (5 条件)。 |
| `tcp` | TCP の送信方式を、p4 (TCP) を 1 条件、p5 (片方向) をペイロード長 {64, 5000} と送信フラグ 4 種とスレッド数 {1, 4} の組み合わせで 16 条件、p5 (双方向) を 1 条件の、合わせて 18 条件。 |
| `full` | `basic` と `tcp` を、重複を除いて結合 (22 条件)。 |

実行の記録は `<出力ディレクトリ>/run-log.txt` に追記します。  
1 回でも失敗または時間切れがあると、終了コードは 1 です。失敗した条件は、`.err` を確認してください。  
同じ出力ディレクトリへ再実行すると、同名のファイルを上書きします。以前の結果を残す場合は、別のディレクトリへ移してください。

### 集計のスクリプト

結果は、次の 3 つのスクリプトで集計します。いずれも、標準ライブラリだけで動作します。

- `porter_bench_summarize_variants.py` は、`<結果ディレクトリ>/<版>/<条件名>.csv` の各回の値から、条件と指標ごとに中央値、最小、最大を求めます。`params` に `cfg.` の項目があれば条件名の末尾へ付け、同じファイル名でも設定の異なる結果を分けて集計します。先頭の版を基準とし、ほかの版に基準との変化率を付けます。3 番目以降の版には、直前の版との変化率も付けます。最大が最小の 3 倍を超える組み合わせは、ばらつきが大きい条件として列挙します。
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

Table: 資源の取得に使う API

| OS | 取得する値 | 使用する API |
|---|---|---|
| Linux | 常駐メモリ、ピーク、匿名メモリ、スレッド数 | `/proc/self/status` の `VmRSS`、`VmHWM`、`RssAnon`、`Threads` |
| Linux | ファイル記述子の数 | `/proc/self/fd` の列挙 |
| Windows | 常駐メモリ、ピーク、専用メモリ | `GetProcessMemoryInfo()` の `WorkingSetSize`、`PeakWorkingSetSize`、`PrivateUsage` |
| Windows | ハンドル数、スレッド数 | `GetProcessHandleCount()`、`CreateToolhelp32Snapshot()` |

Linux の `RssAnon` は、Windows の `PrivateUsage` に相当する値として扱います。両 OS の値は同じ意味ではないため、OS をまたいだ絶対値の比較には使わないでください。
