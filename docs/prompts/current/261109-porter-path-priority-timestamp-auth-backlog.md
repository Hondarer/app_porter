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
| BL-003 | cplat の単調増加時計を高分解能の実装へ一本化する | 見送り | 低 | 未着手 (Windows での計測後に対処) |
| BL-004 | Windows/MSVC でのビルド・局所テストと OS 間の相互接続確認が未実施 | 見送り | 高 | 未着手 (Windows 環境で実施) |

作業中に記録した BL-001 (PING の nonce の再使用) と BL-002 (`max_payload` の検査上限) は、本案件で解消したため一覧から除外しました。課題 ID は振り直していません。

## BL-003 cplat の単調増加時計を高分解能の実装へ一本化する

### 概要と問題点

- 現状の事象: cplat の単調増加時計の API が 2 系統あります。`cplat_clock_get_monotonic()` と `cplat_clock_get_monotonic_ms()` は Windows で `GetTickCount64()` を使い、分解能は約 10〜16 [ms] です。本案件で追加した `cplat_clock_get_monotonic_high_resolution_ns()` は Windows で `QueryPerformanceFrequency()` と `QueryPerformanceCounter()` を使います。Linux では、どちらも `clock_gettime(CLOCK_MONOTONIC)` を呼び、戻り値の形だけが異なります。
- 本来あるべき姿: `cplat_clock_get_monotonic()` の Windows 実装を `QueryPerformanceCounter()` に置き換えて性能上の問題がなければ、単調増加時計を 1 系統にまとめ、`cplat_clock_get_monotonic_high_resolution_ns()` を廃止します。

### 経緯と見送り理由

- 摘出契機: 2026-11-14 の対話で、作業 WBS の 4.1 の成果物に対し、ユーザーから「`cplat_clock_get_monotonic()` の実装を高分解能に置き換えるだけでよいのではないか」との確認を受けました。
- 今回の見送り理由: 置き換えの可否は、Windows での取得コストの差で決まります。`GetTickCount64()` と `QueryPerformanceCounter()` の 1 回あたりの所要時間を、このワークスペースでは計測していません。ユーザーの指示により、Windows での計測の後に対処します。本案件は、最終仕様どおり API を追加した形で進めます。
- 継続作業での経緯と対処: (未記入)

### 影響範囲とリスク

- 影響対象: `app/cplat/prod/libsrc/cplat/clock/clock.c`、`app/cplat/prod/include/cplat/clock/clock.h`、`app/cplat/test/` の `mock_cplat` とエクスポート表、`app/cplat/docs/api-cheatsheet.md`、`app/cplat/docs/functional-spec/clock.md` の `CPLAT-CLOCK-FUNC-007` <!-- cplat-req: uuid=32a417f6-af82-4a99-967b-a76105d8b945 -->、porter の送信時刻 `tx_mono_ns` の取得箇所。
- 置き換える場合の影響: `GetTickCount64()` は失敗しませんが、`QueryPerformanceCounter()` は API 上は失敗を返せるため、`cplat_clock_get_monotonic_ms()` を含めて Windows に失敗の経路が生じます。`GetTickCount64` の mock で時刻を制御している cplat のテスト (`clockTest.cc`、`syncSleepMsTest.cc`、`syncTestHelper.h`) を、`QueryPerformanceCounter` の mock へ書き換える必要があります。サスペンド中に時計が進む点は、Windows ではどちらの API も同じです。
- 放置した場合のリスク: 動作上の問題はありません。同じ目的の API が 2 系統残り、利用側が選択を誤ると、Windows でミリ秒刻みの値を短い経過時間の測定に使うおそれがあります。

### 推奨される対応策

- 恒久対応案: Windows の実機と仮想環境で、`GetTickCount64()` と、`QueryPerformanceFrequency()` + `QueryPerformanceCounter()` の 1 回あたりの所要時間を計測します。タイムアウト判定や porter の 1 送出ごとの取得で問題にならない差であれば、`cplat_clock_get_monotonic()` の Windows 実装を置き換え、`cplat_clock_get_monotonic_high_resolution_ns()` とその mock、エクスポート表の行、要件 `CPLAT-CLOCK-FUNC-007` <!-- cplat-req: uuid=32a417f6-af82-4a99-967b-a76105d8b945 --> を削除し、porter は `cplat_clock_get_monotonic()` の差から `tx_mono_ns` を求めます。差が問題になる場合は 2 系統を維持し、選択の指針を `clock.h` と機能仕様に残します。
- 参考情報: [Acquiring high-resolution time stamps](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps)、`framework/testfw` の `mock_windows` の `QueryPerformanceCounter()` と `QueryPerformanceFrequency()` の mock。

## BL-004 Windows/MSVC でのビルド・局所テストと OS 間の相互接続確認が未実施

### 概要と問題点

- 現状の事象: 本案件の変更 (`framework/testfw`、`app/cplat`、`app/openssl`、`app/porter`) は、Linux/GCC でだけビルドと局所テストを実行しています。2026-11-14 時点の Linux の結果は、`app/cplat` の局所テスト 2,136 件と、`app/porter` の局所テスト 42 スイート 506 件がすべて成功し、内容のある `.warn` もありません。Windows/MSVC でのビルド、局所テスト、`.warn` の確認と、Linux と Windows の間の相互接続は、一度も実施していません。
- 本来あるべき姿: 最終仕様の受入条件 (検証環境) のとおり、Linux と Windows の両方で局所テストが通り、内容のある `.warn` がなく、Linux と Windows の間で porter の相互接続を確認できていること。

### 経緯と見送り理由

- 摘出契機: 作業 WBS の 4.9 で、作業環境 (WSL) に Windows のビルド環境がないことを確認しました。
- 今回の見送り理由: 2026-11-14 の対話で、ユーザーから「Windows 向けの課題は backlog に書いて」との指示を受けました。本案件は Linux での検証までで成果物を提示し、Windows での検証は本項目として引き継ぎます。
- 継続作業での経緯と対処: (未記入)

### 影響範囲とリスク

- 影響対象と、MSVC で特に確認が必要な箇所:
    - `framework/testfw` の `mock_windows` に追加した `QueryPerformanceCounter()` と `QueryPerformanceFrequency()` の mock、`app/cplat` の `cplat_clock_get_monotonic_high_resolution_ns()` の Windows 実装とそのテスト (`clockHighResolutionTest.cc` の Windows 分岐は、Linux では一度もコンパイルされていません)。
    - `app/openssl` の `mock_openssl` に追加した libssl と証明書関連の関数の mock、差し替えヘッダー (`test/include_override/openssl/`)、実委譲の読み込み先 `libssl-3-x64_app.dll`。
    - `app/porter` の構造体の明示パディング (`prod/include_internal/porter/potr_context.h`、`prod/libsrc/porter/security/potr_auth.c`、`test/src/libporter/auth_test/auth_test.cc`)。GCC の配置規則を前提に `-Wpadded` を解消しています。
    - `app/porter/test/include/mock_porter.h` に追加した mock の `/INCLUDE` プラグマ (`potr_internal_callback_emit`、`potr_internal_window_send_get` など)。
    - `app/porter/test/src/integration/certificateTest/certificateTest.cc` の、ループバックでのマルチキャスト (`239.255.86.72`) とポート番号 38622〜38800、`std::memcpy` による列挙型への書き込み (`sizeof(potr_auth) == sizeof(int)` が前提)。
    - 版 2 のワイヤー形式 (72 バイトのヘッダー、送出通番による nonce)、受信報告、DTLS 1.2 と TLS 1.3 のハンドシェイク、鍵の導出の文脈の、Linux と Windows の間での互換性。
- 放置した場合のリスク: Windows でビルドできない、またはテストが失敗する可能性があります。ワイヤー形式や鍵の導出の文脈に OS 間の差異があれば、Linux と Windows の間で通信できません。リポジトリの「Linux/GCC と Windows/MSVC の双方で動作する構成を維持する」条件を満たしていることを確認できていません。

### 推奨される対応策

- 恒久対応案: Windows の開発環境で、`framework/testfw`、`app/cplat`、`app/openssl`、`app/porter` を順に make し、`app/cplat` と `app/porter` の局所テストを実行して、内容のある `.warn` がないことを確認します。失敗と警告を修正した後、`porter-test` で、Linux と Windows の間の相互接続を確認します。対象は、事前共有鍵方式と証明書方式のそれぞれで、`unicast_bidir`、`unicast_bidir_n1`、`tcp`、`tcp_bidir`、`unicast`、`multicast` です。
- 参考情報: 最終仕様の「受入条件」の検証環境の行、`app/general/docs/workspace-agent-workflow.md`。
