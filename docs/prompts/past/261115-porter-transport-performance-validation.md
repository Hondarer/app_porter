<!--
生成元: agree-before-implement スキル
文書種別: 検証記録 (validation)
検討 ID: porter-transport-performance-20261115
最終更新日時: 2026-11-17 04:33:00 JST
最終更新エージェント: Codex (Grok の作業管理とレビュー)
稼働 OS: Windows
上流文書: `261115-porter-transport-performance-discussion.md` (版 6)、`261115-porter-transport-performance-design.md`
再開手順: この案件は 2026-11-17 にユーザー指示で打ち切りました。測定結果は、やり直しの案件 `261117-porter-transport-performance-redo-discussion.md` が事実として参照します。本文にある受容の判断 (Q-006 A) と pilot の方針 (Q-007 B) は、やり直しの案件へ引き継いでいません。BL-001 の実装は cplat と porter のブランチ `fix/bl001-tcp-nodelay` にあり、`main` には入っていません。以下は打ち切り前の再開手順の記録です。このファイルを開いたエージェントは、作業の前に `.agents/skills/agree-before-implement/SKILL.md` を読み込んでください。進行位置は検討用 Markdown の作業 WBS を確認してください。BL-002 の製品改修、Git の書き込み、全体テストは指示の範囲外です。
-->

# porter の TCP 遅延と送信・通知性能の検証記録

この記録は、ステージ 0、BL-001 の実装・局所検証・両 OS の非計装 A/B 測定、および BL-002 wrap プロファイルの実行可能性確認を記録します。  
P4/P5 の 5 回本測定は完了しました。Linux P4 では約 42[ms] の停止が解消しましたが、Linux の大きい blocking 送信では throughput が大きく低下しました。  
ユーザーは Q-006 A で TCP_NODELAY 常時適用と測定した throughput 低下を受け入れました。BL-002 の wrap CPU 値は計装影響と計数分解能が不適切なため、原因・寄与率の根拠には採用しません。ユーザーは Q-007 B を選びましたが、低負荷方式の Sol による設計レビューと pilot 着手前に一時中断を指示しました。pilot 用ファイル、ビルド、測定はありません。BL-002 の製品改修は未着手です。

## 実行開始時の基準

2026-11-16 にユーザーが詳細対処計画を承認し、`grok --yolo high` による実行を指示しました。  
第 2 ゲートはこの承認と実行指示で通過しています。  
実行開始時に読み取った HEAD は次のとおりです。  
その後の Git の書き込みは行っていません。

| リポジトリ | HEAD |
| --- | --- |
| 親 | `29d288df39039809c042bbd2d91fe62269c0598a` |
| app/porter | `67574ec2754f16917ccff865d590e97efedeca52` |
| app/cplat | `934657572d9c86c3b77472409bd04babbb262719` |
| app/cjson | `f659af8219e36f4ddeecaa3689483e5615106cfc` |

Table: 実行開始時の HEAD

親の作業ツリーには、実行開始前から `app/porter` のサブモジュール差分と、未追跡の `framework/makefw` がありました。  
2026-11-16 17:39 JST の再確認では、親は `m app/cplat`、`m app/porter`、`? framework/makefw` です。  
`framework/makefw` は今回の変更対象ではありません。  
porter の既存差分である `docs/prompts/current/261115-porter-reject-recovery-backlog.md` は残しています。

詳細計画が候補にしていた親 `721131ef`、porter `11016c1`、cplat `822dc7da` は、実行開始時の HEAD と一致しません。  
基準は実行開始時の HEAD です。

## 公式仕様と配置

接続の開始前に `TCP_NODELAY` を設定する第一候補を採用しました。  
Linux の [tcp(7)](https://man7.org/linux/man-pages/man7/tcp.7.html) は、このオプションに接続済みであることを要求していません。  
Windows の [setsockopt](https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-setsockopt) は、任意の種類と任意の状態のソケットへ設定できると書いています。  
同じページは、bind 前の `setsockopt` が常に成功し、TCP/IP の検査が bind 時まで遅れることがあるとも書いています。  
connect 前の指定が無効だとは書いていないため、接続前の配置を維持し、この遅延の注意だけを残します。  
[IPPROTO_TCP のソケット オプション](https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-tcp-socket-options) では、`TCP_NODELAY` は Boolean で既定は FALSE です。

listen 側からの継承には依存しません。  
[tcp(7)](https://man7.org/linux/man-pages/man7/tcp.7.html) が accept 済みソケットへの継承を明記しているのは `TCP_USER_TIMEOUT` で、`TCP_NODELAY` ではありません。  
[socket(7)](https://man7.org/linux/man-pages/man7/socket.7.html) も継承を保証していません。  
受け入れたソケットごとに、先読みパケットと TLS より前へ設定します。  
listen 用の open と UDP には設定しません。  
`cplat_socket_open` と `cplat_socket_accept` でも暗黙には有効化しません。

Linux カーネルの遅延 ACK の下限は、HZ が 100 以上のとき約 40[ms] です。  
tcp (7) の本文に 40[ms] という値はありません。

## 機能仕様

`app/cplat/docs/functional-spec/net.md` の `CPLAT-NET-FUNC-005` と「初期化と設定」は、再利用、非ブロッキング、送受信タイムアウトなどを例示しています。  
`TCP_NODELAY` を要件として追加する記述はないため、機能仕様は変更していません。  
porter の機能仕様も、Nagle アルゴリズムの既定を約束していないため変更していません。  
`pack_wait_ms` は別のまとめ送信の設定なので、そのままです。

## ビルド入口

Windows の make は `C:\ProgramData\tetsuo\devbin-win\bin\make.exe` です。  
MSVC は cl 19.50.35724、対象は `windows_x64` です。  
WSL2 は Linux 6.6.87.2、Oracle Linux の GCC 8.5.0、GNU Make 4.2.1、リポジトリは `/mnt/d/Users/tetsuo/Local/repos/c-modernization-kit` です。  
WSL では `app/general/bin_internal/load-app-env.sh` で `.vscode/.env.linux` を読み、`MAKEFW_HOME` をプロセス内に設定してから make します。  
Windows と WSL のビルドは同時に実行していません。

## Windows の局所テスト

mock のヘッダー変更後、最初の `socketTest` は古い `libmock_cplat.lib` をリンクして失敗しました。  
`make -C app/cplat/test/libsrc/mock_cplat` のあとで再実行し、成功しています。

| コマンド | 結果 |
| --- | --- |
| `make -C app/cplat/test/libsrc/mock_cplat` | 終了コード 0。ヘッダー依存で mock 全体を再コンパイル |
| `make -C app/cplat/test/src/libcplatTest/net/socketTest test` | 35 件成功、失敗 0、警告 0 |
| `make -C app/cplat/prod/libsrc/cplat` | 終了コード 0。`libcplat.dll` を更新。コンパイラ警告なし |
| `make -C app/cplat/test/src/libcplatTest/exportTest test` | 2 件成功。エクスポート 535 件が期待表と一致 |
| `make -C app/porter/test/src/libporter/threadConnectPrepareTest test` | 31 件成功、失敗 0、警告 0 |
| `make -C app/porter/test/src/libporter/potrConnectThreadTest test` | 8 件成功、失敗 0、警告 0 |

Table: レビュー修正前の Windows の make test

この表は、負の `optval` と `optlen`、および失敗詳細の初期化を反映する前の `make test` です。  
修正後の確認は、次の節の直接実行と 3 ケースのエビデンスです。

`socket_set_nodelay_reports_enable_disable_and_failure`、`connect_nodelay_failure_continues`、`receiver_sets_nodelay_before_first_packet` は、この時点でも成功しています。  
接続準備は、有効化の要求が connect より前で、失敗してもソケットを閉じていません。  
受け入れは、先読みより前に有効化を要求し、失敗しても先読みへ進んでいます。  
TCP と TCP_BIDIR、再接続は、接続準備関数と受け入れループを共有します。  
listen 用ソケットには設定していません。

対象ディレクトリに内容のある `.warn` は見つかりませんでした。  
コンパイラのログにも `warning C` は出ていません。  
`socket_windows.c` の未実行行 762-764 は、既存の `cplat_socket_wait_readable` です。  
`thread_connect_prepare.c` の未実行行 116、120、124、174-175 は、今回の設定とは別の分岐です。

## レビュー修正後の局所検証

レビュー修正後のソースで、対象 leaf をテスト ターゲットなしでビルドし、バイナリを直接実行しました。  
`make clean` は使っていません。  
Windows と WSL のビルドは同時に実行していません。

Linux は `load-app-env.sh` で `.vscode/.env.linux` を読み、次を実行しました。

```text
make -j24 JOBS_EFFECTIVE=24 MAKEFW_CPU_BUDGET=72 -C <leaf>
```

leaf は `app/cplat/test/src/libcplatTest/net/socketTest`、`app/cplat/test/src/libcplatTest/exportTest`、`app/porter/test/src/libporter/threadConnectPrepareTest`、`app/porter/test/src/libporter/potrConnectThreadTest` です。  
`potrConnectThreadTest` の最初のリンクは、2026-11-07 の `libmock_porter.a` に `potr_internal_certificate_connect` などの定義がなく失敗しました。  
`app/porter/test/libsrc/mock_porter` を同じ make オプションでビルドしてから、リンクし直しました。  
`threadConnectPrepareTest` の最初のバイナリは、更新前のアーカイブとヘッダーの配置が一致せず、`first_packet_empty` で終了コード 139 になりました。  
実行ファイルだけ作り直したあとの直接実行は 31 件成功です。

直接実行は、既存のカバレッジと衝突しないよう `GCOV_PREFIX=/tmp/porter-bl001-gcov-20261116` を設定し、`--gtest_filter=*` で全ケースを対象にしました。

| 実行ファイル | 直接実行 |
| --- | --- |
| Linux `socketTest` | 49 件成功、終了コード 0 |
| Linux `exportTest` | 2 件成功、終了コード 0 |
| Linux `threadConnectPrepareTest` | 作り直し後に 31 件成功、終了コード 0 |
| Linux `potrConnectThreadTest` | 8 件成功、終了コード 0 |
| Windows `socketTest` | 35 件成功、終了コード 0 |
| Windows `threadConnectPrepareTest` | 31 件成功、終了コード 0 |
| Windows `potrConnectThreadTest` | 8 件成功、終了コード 0 |

Table: レビュー修正後のバイナリ直接実行

Windows の `exportTest` は、この 3 leaf に含めていません。  
前節の 2 件成功は、今回作り直した実行結果ではありません。

正式エビデンスは、次の完全なテスト名だけを両 OS で `make test GTEST_FILTER=<完全なテスト名>` により生成しました。  
いずれも終了コード 0 で 1 件成功です。  
全件の `make test` の成功ではありません。

- `socketTest.socket_set_nodelay_reports_enable_disable_and_failure`
- `threadConnectPrepareTest.connect_nodelay_failure_continues`
- `potrConnectThreadTest.receiver_sets_nodelay_before_first_packet`

Linux の全件 `make test` は、ケースごとの後処理が 1 分を超え、親が PGID 2840942 のテストを停止したため未完了です。  
この中断はテスト失敗ではなく、成功としても記録しません。

対象の test leaf、`mock_porter`、`app/cplat/prod` に内容のある `.warn` はありません。  
この検証のビルド ログにも `warning:` はありません。

## P4/P5 の非計装 A/B 測定

`src/off` は実行開始時の HEAD、`src/on` はその基準に BL-001 の製品ソース 5 ファイルだけを重ねた凍結スナップショットです。両方とも current ABI で、`BENCH_PORTER_OLD` は定義していません。各 OS で P4 TCP 1 条件、P5 17 条件を各 5 回測定しました。公式 CSV は計装なしです。

各 OS で off/on 合計 36 条件の終了コードはすべて 0 です。各変種 18 個の CSV に 1〜5 の測定行があり、空の `.err` はありません。CSV の `min` / `median` / `max` 集計行は、以下の 5 回中央値の再計算から除外しました。測定ファイルは作業用複製の `results/windows/` と `results/linux/` に残しています。

| OS | 変種 | close p50 [ms] | close p95 [ms] | close p99 [ms] |
| --- | --- | ---: | ---: | ---: |
| Windows | off | 1.290 | 15.656 | 16.353 |
| Windows | on | 1.275 | 1.823 | 2.193 |
| Linux | off | 42.421 | 44.933 | 45.188 |
| Linux | on | 0.614 | 1.098 | 1.346 |

Table: P4 TCP close 遅延の 5 回中央値

Linux P4 の p50 は 98.6%低下し、約 42[ms] の停止を確認できました。Windows の p50 は約 1.29[ms] で、40[ms] 級ではありません。Windows では p95 が 88.4%、p99 が 86.6%低下しました。

P5 では 17 条件中、throughput が上がった条件は両 OS とも 13 条件でした。条件ごとの相対変化の中央値は Windows +4.9%、Linux +81.9%です。通知遅延 p99 が下がった条件は Windows 11/17、Linux 17/17 でした。代表条件は次のとおりです。

| OS / 条件 | throughput [msg/s] off → on | API p99 [µs] off → on | notify p99 [µs] off → on |
| --- | ---: | ---: | ---: |
| Windows P5 64[B] normal 4T | 708,541 → 729,613 (+3.0%) | 19.7 → 19.5 (-1.0%) | 217.9 → 376.5 (+72.8%) |
| Windows P5 64[B] blocking 4T | 64,514 → 69,211 (+7.3%) | 98.9 → 87.7 (-11.3%) | 139.4 → 101.6 (-27.1%) |
| Windows P5 5000[B] compress 4T | 36,965 → 78,669 (+112.8%) | 331.7 → 85.4 (-74.3%) | 14,871.6 → 1,161.9 (-92.2%) |
| Linux P5 64[B] normal 4T | 13,305 → 374,079 (+2711.6%) | 241.0 → 33.3 (-86.2%) | 43,843.5 → 369.8 (-99.2%) |
| Linux P5 64[B] blocking 4T | 11,754 → 21,483 (+82.8%) | 564.4 → 387.1 (-31.4%) | 42,415.7 → 340.2 (-99.2%) |
| Linux P5 5000[B] normal 4T | 86,199 → 55,301 (-35.8%) | 60.2 → 31.1 (-48.3%) | 1,368.4 → 1,355.5 (-0.9%) |
| Linux P5 5000[B] blocking 1T | 14,828 → 5,668 (-61.8%) | 189.3 → 257.9 (+36.2%) | 1,449.9 → 1,156.6 (-20.2%) |
| Linux P5 5000[B] blocking 4T | 16,526 → 3,655 (-77.9%) | 1,174.5 → 1,379.5 (+17.5%) | 2,091.2 → 1,286.0 (-38.5%) |

Table: BL-001 適用前後の P5 代表条件

Linux の 5000[B] blocking throughput 低下は 1 スレッド、4 スレッドの両方で 5 回すべてに現れました。4 スレッドは各回およそ 3.3〜3.7 千 msg/s、off は 16〜17 千 msg/s でした。`TCP_NODELAY` による低遅延効果は確認できましたが、全 TCP ソケットで常時有効にする方針の受け入れは保留します。Windows P4 の条件全体の所要時間は off 454.0 秒、on 472.1 秒でした。close p95/p99 の改善とは別の指標として扱います。

Linux の off/on ビルドでは、両方に `POTR_TRACE` 可変引数マクロの `-Wpedantic` 警告がありました。2 変種に共通する既存コード由来です。Windows 公式ビルドに内容のある `.warn` はありません。Linux の公式ビルド警告は一時ビルドの `build/linux/<off|on>/build.warn` にあります。

## BL-002 wrap プロファイルの実行可能性確認

Windows では P1 normal / compress、P2 ext / cb、P5 64[B] blocking の 5 条件を、通常版と wrap 版で各 5 回実行しました。CSV 検査は全条件 `status=ok` でしたが、これは記録形式と読み取りエラーがないことを示すだけで、CPU の関数別帰属が正しいことは示しません。

| Windows 条件 | throughput の変化 (通常版 → wrap) |
| --- | ---: |
| P1 64[B] normal 4T | 884,523 → 75,514 msg/s (-91.5%) |
| P1 64[B] compress 4T | 43,058 → 41,813 msg/s (-2.9%) |
| P2 ext 64[B] | 68,932 → 22,365 msg/s (-67.6%) |
| P2 cb 64[B] | 82,097 → 25,899 msg/s (-68.5%) |
| P5 64[B] blocking 4T | 69,395 → 29,421 msg/s (-57.6%) |

Table: Windows wrap 計装による throughput の変化

短い `cplat` 呼び出しごとに QPC と `GetThreadTimes` を呼ぶため、通常送信や通知を大きく遅くしました。さらに `GetThreadTimes` の非ゼロ CPU 差分は 15.625[ms] の倍数に量子化されており、数十 [µs] 級の区間へ CPU を帰属できません。実行中の 15.625[ms] の塊が、その時点で開いていた区間へ偏って記録されます。Grok 高推論レビューでも、CPU 列は元の実行の CPU 分布にも計装後の関数順位にも使えないと結論しました。Windows の wrap 結果は探索記録にとどめます。

Linux では代表として P1 normal 64[B]・4 スレッドだけを通常版と wrap 版で各 5 回実行しました。`perf_event_open` は `EACCES` (errno 13) で開けず、`CLOCK_THREAD_CPUTIME_ID` へフォールバックしました。ユーザー空間 / カーネル空間の分割は得られず、CSV 検査は `status=incomplete`、337 行すべて `cpu_split=no` でした。wrap throughput は 215,331 から 67,275[msg/s] (-68.8%)、`cpu_time_ms` は 524.8 から 1,682.1 (+220.5%) になりました。

Linux wrap も対象負荷を大きく変えるため、フォールバック CPU 値を BL-002 の通常実行の寄与率として扱いません。`perf` 実行ファイルは環境に無く、Windows WPR/ETW は権限条件により起動していません。残る Linux の代表条件 4 つは、測定方法がすでに不適格と判断できたため実行していません。計装ソース、wrap ビルド、結果は作業用複製の `profile/` と `build/<os>/on-wrap-fixed/` に限られ、公式 A/B CSV と製品ソースには混ぜていません。

**結論**: ステージ 2 の終了条件である両 OS の妥当な CPU 寄与率と支配要因は得られていません。BL-002 の実装対象と数値目標は決めず、OS プロファイラーを利用できる権限・環境を整えるか、計装影響を下げた別のサンプリング方式を設計する必要があります。

## Q-007 B の低負荷サンプリング pilot 状態

ユーザーは Q-007 B を選び、製品ソースと分離した一時ビルドで低負荷サンプリング方式を設計し、小規模に検証する方針を合意しました。Grok の初回分析では、`plain`、呼び出し回数だけを集計する `count`、確率 1/1024 で壁時計を読む `wall`、4 区間だけを測る `phase` を独立した計測 variant とする案が提示されました。代表条件は P1 64[B] normal / compress・4 スレッドで、Windows と Linux を別々に各 5 回測る案です。

この案は **Grok の未レビュー草案** であり、Sol による方式・observer effect 判定基準のレビュー、pilot 用ソース作成、ビルド、ベンチ実行はしていません。2 回目の Grok CLI 作業はユーザーの中断指示で停止しました。最終確認時に一時作業領域の `profile/pilot/` は存在せず、新しいファイル、ビルド、測定結果はありません。既存の wrap A/B と一時プロファイル成果物はそのまま保持しています。

案を再開するときは、まず各 variant が推定する量と既存の harness 指標の窓を照合し、特に `wall` の標本和から得る推定値を CPU 寄与率として扱わないこと、`phase` の区間重複、Windows の関数別 CPU 値が未取得となることをレビューします。候補が明確になった後に限り、一時複製で clock 分解能と権限を確認し、実行条件と observer effect の判定基準を確定してから pilot を行います。Linux の `CLOCK_THREAD_CPUTIME_ID` は呼び出しスレッドの CPU 時間を表しますが、実際の分解能と採取負荷は対象環境で測る必要があります ([Linux `clock_gettime(3)`](https://www.man7.org/linux/man-pages/man3/clock_gettime.3.html))。Windows の `QueryThreadCycleTime` が返す CPU サイクル数は経過時間へ変換できず、CPU 周波数の特性にも依存するため、そのまま CPU 時間と見なす案は採用していません ([Microsoft `QueryThreadCycleTime`](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime))。

**中断時点**: pilot の方式選定・実装・実測は未着手です。BL-002 の原因帰属、数値目標、製品改修は未完了です。

## 未完了

cplat 全体の `make doxy` は、時間を要した internal category の生成中に停止したため未完了です。公開 API だけを対象とする `make -C framework/doxyfw CATEGORY=cplat SUBCATEGORY=public` も、Markdown 後処理が 60 分を超えたため親が停止し、終了コード 2 でした。停止前に一時生成された `Files/socket_8h.md` で、新 API の宣言と共通結果コードへの変換説明を確認しましたが、後処理後の公開ページは確認できていません。  
公開 API Doxygen の一時 `.warn` は後処理の停止時に削除されており確認できませんでした。`app/cplat` に内容のある `.warn` は見つかっていません。  
BL-001 の実装・局所検証と P4/P5 本測定は完了しました。ユーザーが Q-006 A で性能トレードオフを受容したため、backlog の状態を対応済みに更新しました。Doxygen の全体生成は未完了です。  
BL-002 の製品改修は未着手です。既存 wrap 計測は両 OS とも原因帰属に使えません。Q-007 B に従う別方式の低負荷サンプリングは、ユーザー指示で設計レビューと pilot の前に一時中断しました。

## ベンチの実行入口

過去の測定プログラムはリポジトリにありません。  
作業用の複製は `C:\Users\tetsuo\AppData\Local\Temp\porter-transport-perf-ab-20261116` です。  
比較対象のソースは `src/off` と `src/on` です。  
`src/off` は実行開始時の HEAD と一致する基準です。  
`src/on` はその基準へ BL-001 の製品ソース 5 ファイルだけを適用した凍結スナップショットです。現在の作業ツリー全体を複製したものではありません。  
作業ツリーを checkout で戻す操作はしません。

実行入口と設定は作業用複製に固定しました。Doxygen の後処理は未完了のまま停止し、その制約を記録した後、凍結スナップショットから測定しました。`manager-ready.txt` と Linux 用 marker は作業用複製内だけに置きました。

| OS | ビルド | 実行 |
| --- | --- | --- |
| Windows | `bench\build-current.ps1` | `run-ab.ps1` の `-Variant off` または `on` |
| Linux | `bench\build-linux.sh` | `run-ab-linux.sh` の `--variant off` または `on` |

Table: ベンチの固定入口

5 回の本測定は完了しています。公式結果は `results/<os>/<off|on>/` の CSV、`.out`、`.err` と `results/<os>/run-log.txt` にあります。  
`BENCH_PORTER_OLD` は、今回の off と on のどちらにも定義していません。wrap プロファイルの成果物は `profile/results/<os>/` に分離しました。
