<!--
生成元: agree-before-implement スキル
文書種別: 最終仕様 (design)
最終更新エージェント: codex / セッション ID: 特定できず
上流文書: `261114-porter-windows-verification-clock-unify-discussion.md`。削除されている場合があります。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。上流文書が存在する場合は、上流文書の再開手順に従って進行位置を確認し、判断の経緯を上流文書の Q&A で確認します。存在しない場合は、この文書を単体の最終仕様として扱います。最終更新エージェントのセッション情報が参照可能であれば再開時の補足コンテキストとして利用し、別エージェントからの引き継ぎや担当変更によりセッションが見つからない場合も異常とせず、文書の記載から継続してください。
-->

# porter の Windows 検証と cplat 単調増加時計の一本化 最終仕様

ファイル名: `261114-porter-windows-verification-clock-unify-design.md`

## 文書の識別

検討 ID: porter-windows-verification-clock-unify-20261114  
対象: Windows 上の `framework/testfw`、`app/cplat`、`app/openssl`、`app/porter` のビルドと局所テスト、Linux と Windows の相互接続、および `app/cplat` の単調増加時計 API  
合意版: discussion 版 3 (第 1 ゲート通過)  
状態: 第 2 ゲート承認済み。実装と検証は完了し、第 3 ゲート成果物承認済み (2026-11-15)

## 目的と範囲

本作業は当初 BL-003「cplat の単調増加時計を一本化する」を対象として開始しました。継続指示により BL-004「Windows/MSVC でのビルド・局所テストと OS 間の相互接続確認」も実施しました。検証結果と未解決事項は discussion と案件 backlog に記録します。

BL-003 の計測結果を提示した後、ユーザーは一本化と性能低下の許容を判断しました。判断の記録は discussion の Q-010 を参照してください。

今回の条件付き承認は BL-003 の計測と判断に対するものでした。その後の「続けて」という指示を受け、BL-004 の Windows 検証と相互接続も実施しました。

## BL-003 の計測と判断

Windows 11 Pro for Workstations build 26300、MSVC x64 19.50.35724、HP Z8 G4、Intel Xeon Gold 6254 (18 コア / 36 論理プロセッサ) で計測しました。`HypervisorPresent=True` であり、仮想化環境の影響が残る可能性があります。QPF/QPC はすべて成功し、QPF は 10 MHz でした。各経路はウォーム アップ後に 1,000,000 回を 10 試行し、結果を消費して最適化で除去されにくい形で測りました。

| 計測経路 | 中央値 [ns/回] | 最小値 [ns/回] | 最大値 [ns/回] | 平均値 [ns/回] |
|---|---:|---:|---:|---:|
| `GetTickCount64()` → timespec | 3.165 | 3.160 | 3.186 | 3.168 |
| QPF 毎回 + QPC → uint64 ns | 35.451 | 35.342 | 35.732 | 35.474 |
| QPF 毎回 + QPC → timespec | 35.575 | 35.528 | 35.670 | 35.592 |
| QPF キャッシュ + QPC → timespec (補助値) | 34.666 | 34.516 | 34.792 | 34.672 |

実装に対応する主な比較は、`GetTickCount64()` → timespec と QPF 毎回 + QPC → timespec です。QPF キャッシュは補助値であり、実装判断に使いません。CSV と再現用プログラムは TEMP の `C:\Users\tetsuo\AppData\Local\Temp\porter-clock-bench-261114-86cab813\measurement-refined.csv` および `clock_bench.c` にあります。

ユーザー判断に基づき、`cplat_clock_get_monotonic()` の Windows 実装を QPF/QPC に置き換え、QPF は呼び出しごとに取得します。`cplat_clock_get_monotonic_high_resolution_ns()` と関連する mock、エクスポート表、テストを削除します。`app/porter` は timespec を検証し `tx_mono_ns` に変換します。Windows API の失敗は共通エラーで伝え、失敗時に出力を変更しません。

ユーザーは性能低下を許容します。性能に課題が生じた場合は `GetTickCount64()` のみを使う高速版関数の追加を検討することを、`cplat_clock_get_monotonic()` の Doxygen コメントに記載します。課題がなければ単一 API を維持し、性能しきい値は設定しません。高速版は今回追加しません。

## BL-004 の Windows 検証

BL-003 の計測と判断、および選択された時計対応を終えたソースに対し、Windows/MSVC で次の順にビルドします。

1. `framework/testfw`
2. `app/cplat`
3. `app/openssl`
4. `app/porter`

`app/cplat` と `app/porter` の局所テストを実行します。`app/cplat` は変更の完了後に全体の `make test` を 1 回実行します。所要時間の目安は約 2 時間 30 分です。作業途中は、変更した範囲だけをテストします。ビルドとテストはエージェントが実行し、`app/porter` のビルドとテストは並行案件が停止するか本作業の結果を待つ状態で行います。並行案件は、本作業で得た結果を再利用します。

ビルド後は対象範囲にある内容のある `.warn` を確認し、変更に起因するビルド失敗、テスト失敗、警告を修正して再検証します。既存の無関係な問題や環境不足は、変更起因の問題と分けて確認結果に記録します。未解決の警告や環境不足が残る場合は、受入確認時に報告し、必要に応じて本作業の backlog に記録します。

## Linux と Windows の相互接続

Linux 側には、この PC の WSL 2 (`OracleLinux8-Dev`) を現在の設定のまま使います。Windows と Linux の `porter-test` を接続し、事前共有鍵方式と証明書方式のそれぞれで、次の通信種別を確認します。

| 通信種別 |
| --- |
| `unicast_bidir` |
| `unicast_bidir_n1` |
| `tcp` |
| `tcp_bidir` |
| `unicast` |
| `multicast` |

Table: Linux と Windows の相互接続確認の通信種別

Windows ファイアウォールなど管理者権限が必要な操作が発生した場合は、エージェントが手順とコマンドを示し、ユーザーが管理者権限のターミナルで実行します。追加したファイアウォール規則は確認後に削除します。現在の WSL 設定で接続できない通信種別は、その条件と結果を記録して本作業の backlog に残します。WSL のネットワーク設定変更や再起動は行いません。

## 受入条件

| 対象 | 受入条件 |
| --- | --- |
| 時計の計測 | Windows 実機の環境、実行回数、両主要経路の値、単位、ばらつき、限界を記録する |
| 時計の判断 | 計測結果の提示後に得たユーザー判断を記録し、その判断に従って QPF/QPC に一本化する |
| BL-003 のビルドとテスト | cplat の時計実装と porter の変更対象ライブラリが Windows でビルドでき、clock/export および packetSeal/packetEmission の局所テストが通る |
| 警告 | 変更に起因する内容のある `.warn` がない。既存の無関係な警告は、原因確認の結果を分けて報告する |
| BL-004 | Windows 4 対象のビルド・テスト完了。相互接続 10/12 成功。未達 multicast 2 件はユーザー判断で今回修正せず案件 backlog へ移管。技術仕様の選択は未決 |
| 文書 | 変更した Markdown が `text_style_jp.py` の確認を通り、機能仕様を変更した場合は `check_functional_spec.py` が通る |

Table: 作業の受入条件

## 正本への反映

時計 API を変更した場合は、`app/cplat` の公開ヘッダー、API チート シート、時計の機能仕様、mock、エクスポート表、テスト、および `app/porter` の送信時刻取得処理を同じ変更で更新します。既存要件 `CPLAT-CLOCK-FUNC-007` <!-- cplat-req: uuid=32a417f6-af82-4a99-967b-a76105d8b945 --> は高分解能の単調時刻を提供する機能要件として維持します。永続的な技術知見がない限り、Windows 検証の作業記録を一般的な正本へ追加しません。

変更後は、対象 Markdown に `text_style_jp.py --dry-run` を実行し、問題がなければ `--in-place` を実行します。時計の機能仕様を変更した場合は、ワークスペース ルートで `python3 app/general/bin/check_functional_spec.py` を実行します。

## 作業順序

1. 計測結果を提示し、一本化、性能低下の許容、将来高速版を検討する条件についてユーザー判断を記録します (完了)。
2. 承認された判断に従って BL-003 の実装と局所検証を行います (完了)。
3. BL-003 に関係する `.warn`、Markdown、機能仕様と Doxygen 生成結果を確認し、変更起因の失敗を修正します。
4. BL-004 の結果と未解決事項を案件 backlog に整理し、第 3 ゲートで成果物と残課題を提示しました (承認済み: 2026-11-15)。
