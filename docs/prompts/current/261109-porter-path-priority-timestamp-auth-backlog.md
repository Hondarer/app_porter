<!--
生成元: agree-before-implement スキル
文書種別: 残課題一覧 (backlog)
最終更新エージェント: claude code / セッション ID: session_01KJc53Sx84uaP9E9DhMF2xe
上流文書: 261109-porter-path-priority-timestamp-auth-design.md (最終仕様)、261109-porter-path-priority-timestamp-auth-discussion.md (検討用 Markdown)。いずれも削除されている場合があります。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。「整理状態」が「随時追記中」であれば、案件の作業中です。気付いた事項を追記し、第 3 ゲートの前に文書を再構築して整理します。上流文書が存在する場合は、各課題の背景と見送りの判断を上流文書で確認します。存在しない場合は、この文書を単体の残課題一覧として扱います。このファイルの残課題を参照して新たな成果物の作成または更新を始めるときは、元案件の再開とは別の作業として扱います。その開始には agree-before-implement を原則として使い、新しい検討用 Markdown から開始してください。継続作業によってこのファイルの残課題を対策する場合は、このファイルや各項目の記載を削除することなく、各項目の状態および経緯と対処を更新することを原則とします。また、フェーズ分けした対策で対処ができていない項目がある場合は、当該継続作業の backlog にも必ず転記し、最新作業の backlog を見れば残件が把握できるようにしてください。最終更新エージェントのセッション情報が参照可能であれば再開時のコンテキストとして利用し、別エージェントからの引き継ぎや担当変更によりセッションが見つからない場合も異常とせず、文書の記載から継続してください。
-->

# 残課題一覧 (バックログ)

## 案件の識別

案件名: `porter-path-priority-timestamp-auth`  
作成日: `2026-11-09`  
整理状態: 整理済み

この文書は、見送る決定がなされた問題点や、作業中に潜在または別問題として判明した問題点を記録します。  
作業期間中は気付いた時点で随時追記し、作業の全完後に再構築して整理します。

## 残課題一覧

| 課題 ID | 題名 | 区分 | 優先度 / 影響度 | 状態 |
| --- | --- | --- | --- | --- |
| BL-003 | cplat の単調増加時計を高分解能の実装へ一本化する | 見送り | 低 | 対応済み (実機計測・一本化完了。仮想環境では未計測) |
| BL-004 | Windows/MSVC でのビルド・局所テストと OS 間の相互接続確認が未実施 | 継続中 | 高 | Windows 検証完了。相互接続 10/12 成功。残りは新 backlog `261114-porter-windows-verification-clock-unify-backlog.md` へ移管 |

作業中に記録した BL-001 (PING の nonce の再使用) と BL-002 (`max_payload` の検査上限) は、本案件で解消したため一覧から除外しました。課題 ID は振り直していません。

## BL-003 cplat の単調増加時計を高分解能の実装へ一本化する

### 概要と問題点

- 現状の事象: (着手時点) cplat の単調増加時計 API が 2 系統ありました。`cplat_clock_get_monotonic()` と `cplat_clock_get_monotonic_ms()` は Windows で `GetTickCount64()` を使い、`cplat_clock_get_monotonic_high_resolution_ns()` は Windows で QPF/QPC を使っていました。
- 本来あるべき姿: `cplat_clock_get_monotonic()` を Windows で QPF/QPC に一本化し、`cplat_clock_get_monotonic_high_resolution_ns()` を廃止します。性能低下は許容され、性能上の課題が生じた場合にだけ `GetTickCount64()` を使う高速版関数の追加を検討します。

### 経緯と見送り理由

- 摘出契機: 2026-11-14 の対話で、作業 WBS の 4.1 の成果物に対し、ユーザーから「`cplat_clock_get_monotonic()` の実装を高分解能に置き換えるだけでよいのではないか」との確認を受けました。
- 今回の見送り理由: (解消) Windows 実機で計測し、その結果を見てからユーザーが判断するため、計測まで一本化を保留しました。
- 継続作業での経緯と対処: 2026-11-14 に Windows 11 実機で計測しました。環境は Windows 11 Pro for Workstations build 26300、MSVC x64 19.50.35724、Intel Xeon Gold 6254、仮想化機能あり (`HypervisorPresent=True`) です。QPF は 10 MHz、QPF/QPC は全試行で成功しました。1,000,000 回を 10 試行した中央値は、`GetTickCount64()` → timespec が 3.165 ns/回、QPF 毎回 + QPC → timespec が 35.575 ns/回でした (前者の最小/最大 3.160/3.186 ns、後者 35.528/35.670 ns)。補助の QPF キャッシュ経路は 34.666 ns/回でしたが、判断には使用していません。TEMP の `C:\Users\tetsuo\AppData\Local\Temp\porter-clock-bench-261114-86cab813\measurement-refined.csv` が計測根拠です。ユーザーは性能低下を許容して一本化を判断しました。QPF/QPC を毎回使う単一 API に変更し、古い高分解能専用 API とその mock、エクスポート表、テストを削除しました。porter は timespec の値を検証して `tx_mono_ns` に変換します。性能上の課題が出た場合だけ `GetTickCount64()` 専用の高速版関数を検討し、課題がない限り一系統を維持します。Windows で cplat と porter の対象ライブラリをビルドし、clock/export および packetSeal/packetEmission の対象テストはすべて成功しました。仮想環境では計測していません。結果は `261114-porter-windows-verification-clock-unify-discussion.md` と design に記録します。

### 影響範囲とリスク

- 影響対象: `app/cplat/prod/libsrc/cplat/clock/clock.c`、`app/cplat/prod/include/cplat/clock/clock.h`、`app/cplat/test/` の `mock_cplat` とエクスポート表、テスト、API チート シート、時計の機能仕様、porter の送信時刻 `tx_mono_ns` の取得箇所。機能要件 `CPLAT-CLOCK-FUNC-007` <!-- cplat-req: uuid=32a417f6-af82-4a99-967b-a76105d8b945 --> は単一の高分解能時刻 API の要件として維持します。
- 置き換える場合の影響: `GetTickCount64()` は失敗しませんが、`QueryPerformanceCounter()` は API 上は失敗を返せるため、`cplat_clock_get_monotonic_ms()` を含めて Windows に失敗の経路が生じます。`GetTickCount64` の mock で時刻を制御している cplat のテスト (`clockTest.cc`、`syncSleepMsTest.cc`、`syncTestHelper.h`) を、`QueryPerformanceCounter` の mock へ書き換える必要があります。サスペンド中に時計が進む点は、Windows ではどちらの API も同じです。
- 放置した場合のリスク: 動作上の問題はありません。同じ目的の API が 2 系統残り、利用側が選択を誤ると、Windows でミリ秒刻みの値を短い経過時間の測定に使うおそれがあります。

### 推奨される対応策

- 恒久対応案: 対応済みです。Windows の `cplat_clock_get_monotonic()` を QPF/QPC に置き換え、QPF を呼び出しごとに取得します。`cplat_clock_get_monotonic_high_resolution_ns()` と関連する mock、エクスポート行、テストを削除し、`CPLAT-CLOCK-FUNC-007` <!-- cplat-req: uuid=32a417f6-af82-4a99-967b-a76105d8b945 --> は単一の高分解能 API に対する機能要件として維持しました。porter は timespec を検証して `tx_mono_ns` を計算します。仮想環境では計測していません。性能上の課題が生じた場合にのみ `GetTickCount64()` を使う高速版の追加を検討します。
- 参考情報: [Acquiring high-resolution time stamps](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps)、`framework/testfw` の `mock_windows` の `QueryPerformanceCounter()` と `QueryPerformanceFrequency()` の mock。

## BL-004 Windows/MSVC でのビルド・局所テストと OS 間の相互接続確認が未実施

### 概要と問題点

- 現状の事象: 本案件の変更 (`framework/testfw`、`app/cplat`、`app/openssl`、`app/porter`) は、Linux/GCC でだけビルドと局所テストを実行しています。2026-11-14 時点の Linux の結果は、`app/cplat` の局所テスト 2,136 件と、`app/porter` の局所テスト 42 スイート 506 件がすべて成功し、内容のある `.warn` もありません。Windows/MSVC でのビルド、局所テスト、`.warn` の確認と、Linux と Windows の間の相互接続は、一度も実施していません。
- 本来あるべき姿: 最終仕様の受入条件 (検証環境) のとおり、Linux と Windows の両方で局所テストが通り、内容のある `.warn` がなく、Linux と Windows の間で porter の相互接続を確認できていること。

### 経緯と見送り理由

- 摘出契機: 作業 WBS の 4.9 で、作業環境 (WSL) に Windows のビルド環境がないことを確認しました。
- 今回の見送り理由: 2026-11-14 の対話で、ユーザーから「Windows 向けの課題は backlog に書いて」との指示を受けました。本案件は Linux での検証までで成果物を提示し、Windows での検証は本項目として引き継ぎます。
- 継続作業での経緯と対処: 2026-11-15、ユーザーの「続けて」という指示を受けて BL-004 を再開しました。Windows の 4 対象を検証しました。`app/cplat make test` は exit 0 で終了し、87 leaf summary、1,854 件成功、警告 0 件、失敗 0 件でした (全体 test 段階の実行時間 2 時間 42 分 55 秒)。`app/porter make test` は exit 0 で終了し、43/43 leaf、511 件成功、失敗 0 件でした。`app/openssl make test` は exit 0 で終了し、製品 OpenSSL は最新のため再ビルドされず、`mock_openssl` をビルドしました。`app/openssl/test/src` に単体テスト leaf はありません。`framework/testfw make test` は exit 0 で終了しました。C/C++ テストは `exportCheckTest` と `mockInstanceTest` が各 2 件成功し、Python `bin_test` は 124 件実行、3 件 skip、`OK` でした。`mock_syslog` は Windows の `SKIP_BUILD` 対象外として skip されました。

  相互接続は PSK と証明書を合わせて 12 組み合わせ中 10 組み合わせが成功しました。マルチキャスト 2 件はユーザー判断「backlog に回す」により今回修正せず、新案件 backlog BL-001 へ移管しました。Q-011 の技術的選択肢は未決のままです。

  検証後に確認した非空 `.warn` は次のとおりです。`app/cplat/doxy_internal.warn` は実行前の 2026-11-14 に更新された 142 byte の Doxygen Mermaid タグ警告です。`app/porter/test/src/integration/certificateTest/potr_health_thread.c.warn` は porter test 開始前の 2026-11-15 03:00 に更新された生成 `.warn` で、旧 API の C4013 と C4310 を含みます。これらは今回の実行で発生した警告と分けて扱います。`app/openssl` と `framework/testfw` には非空 `.warn` はありません。

  `app/porter make test` の出力では、`certificateTest` の依存スキップ表示前に `/usr/bin/sh: -c: line 1: unexpected EOF while looking for matching '"'` が出ました。leaf の既存 summary は 31 件すべて成功し、依存が変わっていないとしてテストはスキップされました。次の読み取り専用 no-op make でも再現しました: `make -C app/porter/test/src/integration/certificateTest --eval='printenv:;@:' printenv`。直接の `resolve_app_deps.sh --signature` は単独では再現しませんでした。make 構成時の stderr と確認しましたが原因関数は未特定です。テスト結果の失敗や新しい `.warn` は確認していません。原因の不明な修正は行わず、再現と影響を残課題として記録します。

### 影響範囲とリスク

- 影響対象と、MSVC で特に確認が必要な箇所 (2026-11-15 の Windows 検証でビルドとテストを確認):
    - `framework/testfw` の `mock_windows` に追加した `QueryPerformanceCounter()` と `QueryPerformanceFrequency()` の mock、および `app/cplat` の `cplat_clock_get_monotonic()` の Windows 実装。旧 `cplat_clock_get_monotonic_high_resolution_ns()` と `clockHighResolutionTest.cc` は BL-003 対応で削除済みです。Windows clock/export テストと cplat 全体テストは成功しました。
    - `app/openssl` の `mock_openssl` に追加した libssl と証明書関連の関数の mock、差し替えヘッダー (`test/include_override/openssl/`)、実委譲の読み込み先 `libssl-3-x64_app.dll`。
    - `app/porter` の構造体の明示パディング (`prod/include_internal/porter/potr_context.h`、`prod/libsrc/porter/security/potr_auth.c`、`test/src/libporter/auth_test/auth_test.cc`)。GCC の配置規則を前提に `-Wpadded` を解消しています。
    - `app/porter/test/include/mock_porter.h` に追加した mock の `/INCLUDE` プラグマ (`potr_internal_callback_emit`、`potr_internal_window_send_get` など)。
    - `app/porter/test/src/integration/certificateTest/certificateTest.cc` の、ループバックでのマルチキャスト (`239.255.86.72`) とポート番号 38622〜38800、`std::memcpy` による列挙型への書き込み (`sizeof(potr_auth) == sizeof(int)` が前提)。
    - 版 2 のワイヤー形式 (72 バイトのヘッダー、送出通番による nonce)、受信報告、DTLS 1.2 と TLS 1.3 のハンドシェイク、鍵の導出の文脈の、Linux と Windows の間での互換性。
- 放置した場合のリスク: Windows でビルドできない、またはテストが失敗する可能性があります。ワイヤー形式や鍵の導出の文脈に OS 間の差異があれば、Linux と Windows の間で通信できません。リポジトリの「Linux/GCC と Windows/MSVC の双方で動作する構成を維持する」条件を満たしていることを確認できていません。

### 推奨される対応策

- 恒久対応案: Windows の開発環境で、`framework/testfw`、`app/cplat`、`app/openssl`、`app/porter` を順に make し、`app/cplat` と `app/porter` の局所テストを実行して、内容のある `.warn` がないことを確認します。失敗と警告を修正した後、`porter-test` で、Linux と Windows の間の相互接続を確認します。対象は、事前共有鍵方式と証明書方式のそれぞれで、`unicast_bidir`、`unicast_bidir_n1`、`tcp`、`tcp_bidir`、`unicast`、`multicast` です。
- 参考情報: 最終仕様の「受入条件」の検証環境の行、`app/general/docs/workspace-agent-workflow.md`。

## BL-004 継続作業の整理 (2026-11-15)

Windows 検証は framework/testfw、app/cplat、app/openssl、app/porter の対象作業を完了しました。Linux/Windows 相互接続は PSK・証明書の計 12 組み合わせ中 10 件が成功し、マルチキャスト 2 件はユーザー判断「backlog に回す」により今回修正せず、新案件 backlog の BL-001 へ移管しました。Q-011 の技術的選択肢は未決のままです。

Q-006=A で明示的に残した Windows VM 時計計測は、新案件 backlog BL-002 に独立項目として転記しました。certificateTest make 構成時の unexpected EOF stderr は新案件 backlog BL-003 に切り分けました。古い生成 `.warn` と既存の一般警告は今回の新規残課題に含めません。

今回の残課題の正本: `261114-porter-windows-verification-clock-unify-backlog.md`。本書の BL-003/BL-004 の履歴は保持し、後続対応は当該案件 backlog で追跡します。
