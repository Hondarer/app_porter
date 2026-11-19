<!--
生成元: agree-before-implement スキル
文書種別: 残課題一覧 (backlog)
最終更新日時: 2026-11-18 05:59:52 JST
最終更新エージェント: claude code / セッション ID: session_01KJc53TxVv1vjB6xMfjiUgc
稼働 OS: Windows
上流文書: `261115-porter-multicast-src-contract-design.md` (最終仕様)、`261115-porter-multicast-src-contract-discussion.md` (検討用 Markdown)。いずれも削除されている場合があります。
下流文書: この残課題一覧を対策する継続作業が開始された場合は、その検討用 Markdown・最終仕様・残課題一覧のパスを追記します。未作成の場合や、削除されている場合があります。BL-002 の継続作業 (2026-11-16 07:10 JST 開始、claude code / セッション ID: session_01KJc53T14fddkwQBRc7ZPQt): `../../../../../framework/makefw/docs/prompts/current/261116-makefw-subdir-rebuild-race-discussion.md` (検討用 Markdown)、`../../../../../framework/makefw/docs/prompts/current/261116-makefw-subdir-rebuild-race-design.md` (最終仕様)、`../../../../../framework/makefw/docs/prompts/current/261116-makefw-subdir-rebuild-race-backlog.md` (残課題一覧)。BL-003 の継続作業 (2026-11-16 11:10 JST 開始、claude code / セッション ID: session_01KJc53UbHjwaWeuucBs5Q5U): `../../../../../framework/doxyfw/docs/prompts/current/261116-doxyfw-page-variant-reload-flake-discussion.md` (検討用 Markdown)、`../../../../../framework/doxyfw/docs/prompts/current/261116-doxyfw-page-variant-reload-flake-design.md` (最終仕様)、`../../../../../framework/doxyfw/docs/prompts/current/261116-doxyfw-page-variant-reload-flake-backlog.md` (残課題一覧)。2026-11-16 に、担当を WSL のセッション (claude code / セッション ID: session_01KJc53SqgNC8iDAi6rvzgwh) へ引き継ぎました。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。「整理状態」が「随時追記中」であれば、案件の作業中です。気付いた事項を追記し、第 3 ゲートの前に文書を再構築して整理します。上流文書が存在する場合は、各課題の背景と見送りの判断を上流文書で確認します。存在しない場合は、この文書を単体の残課題一覧として扱います。このファイルの残課題を参照して新たな成果物の作成または更新を始めるときは、元案件の再開とは別の作業として扱います。その開始には agree-before-implement を原則として使い、新しい検討用 Markdown から開始してください。継続作業によってこのファイルの残課題を対策する場合は、このファイルや各項目の記載を削除することなく、継続作業の開始時から各段階において、このファイルのメタ情報、各項目の状態、および経緯と対処を随時更新することを原則とします。
-->

# porter マルチキャスト受信側の送信元設定契約の残課題一覧

ファイル名: `261115-porter-multicast-src-contract-backlog.md`

## 案件の識別

案件名: porter マルチキャスト受信側の送信元設定契約 (過去 backlog BL-001 の対策)  
作成日: 2026-11-16  
整理状態: 整理済み

この文書は、作業中に別問題として判明し、この案件では対策しなかった事項を記録したものです。2026-11-18 に再構築して整理しました。

未対処の項目は BL-004 と BL-005 です。BL-002 と BL-003 は継続案件で解消済みで、継続案件からの参照と経緯の記録のために残しています。BL-001 (`handleConcurrencyTest` の接続待ち) は、この案件の範囲に加えて解消したため、整理で除外しました (経緯は検討用 Markdown の Q-006 と Q-007)。ほかの文書からの参照を保つため、課題 ID は振り直していません。

## 残課題一覧

| 課題 ID | 題名 | 区分 | 優先度 / 影響度 | 状態 |
| --- | --- | --- | --- | --- |
| BL-002 | mock_cplat の並列ビルドで同じサブディレクトリが 2 回ビルドされ、CI で不定期に失敗する | 作業中摘出 | 中 / 中 | 解消 (継続案件 261116-makefw-subdir-rebuild-race。CI run 44144556601 で確認し、2026-11-18 に `main` へ取り込み済み) |
| BL-003 | doxyfw の依存関係レポートのテストで、再読み込み後にページ種別の保存値が失われ、CI で不定期に失敗する | 作業中摘出 | 中 / 中 | 解消 (継続案件 261116-doxyfw-page-variant-reload-flake。修正 doxyfw `a160e44` を含む CI run 44144556601 が成功) |
| BL-004 | 証明書認証の片方向の通信種別で、最初の認証の試行が失敗の警告を出してから接続が確立する | 作業中摘出 | 低 / 低 | 対応中 (2026-11-20 に原因の仮説を特定。継続案件 261119-porter-flow-control で、確認と修正を行う) |
| BL-005 | Windows/WSL 相互接続試験のスクリプトが Git で管理されていない | 作業中摘出 | 低 / 低 | 対応中 (2026-11-20 に `bin/interop_matrix.py`、`bin_test/test_interop_matrix.py`、`docs/interop-test.md` として取り込み、環境固有の値を引数にした。Linux での単体テストは成功。Windows 上での実行は未確認で、案件 261119-porter-flow-control の OS をまたぐ確認で確かめる) |

## BL-002 mock_cplat の並列ビルドで同じサブディレクトリが 2 回ビルドされ、CI で不定期に失敗する

### 概要と問題点

- **現状の事象**: CI run 43727490184 の attempt 2 (失敗ジョブの再実行) で、ol9 ジョブの Build ステップが失敗しました。`app/cplat/test/libsrc/mock_cplat` のビルドで、`make[6]: *** '../../lib/libmock_cplat.a' に必要なターゲット 'crt/obj/mock_cplat_ftell.o' を make するルールがありません.  中止.` が出ています。同じログで `mock_cplat_ftell.o` のコンパイルが 2 回出ています (失敗ログの 5763 行目と 6151 行目)。同じコミットの attempt 1 では、ビルドは成功していました。
- **原因の見立て (未確定)**: `mock_cplat/crt` が並行して 2 回ビルドされ、一方がオブジェクトを作り直している間に、アーカイブ (`libmock_cplat.a`) 側の依存確認が走って競合した可能性があります。makefw のサブディレクトリ走査方式での並列ビルドの競合と考えていますが、2 回ビルドされる経路は特定していません。
- **本来あるべき姿**: 並列ビルドでも、同じサブディレクトリが同時に 2 回ビルドされず、毎回同じ結果になること。

### 経緯と見送り理由

- **摘出契機**: BL-001 (マルチキャスト参加 NIC) の CI で、別の不定期失敗 (この文書の BL-001) の再実行をしたとき。
- **今回の見送り理由**: 失敗したのは cplat の mock のビルドで、この案件の変更とは関係しません。makefw の並列ビルドの調査が必要で、この案件の範囲外です。リポジトリ内の Markdown に同じ現象の記録はありません (並行作業中のセッションが確認済み。`framework/makefw/docs/hooks.md` と `path-restrictions.md` に同じエラー文言がありますが、原因は別です)。
- **継続作業での経緯と対処**: 2026-11-16、原因を調査する継続案件を開始しました (検討 ID: 261116-makefw-subdir-rebuild-race、検討用 Markdown: `framework/makefw/docs/prompts/current/261116-makefw-subdir-rebuild-race-discussion.md`)。状態は「検討中」で、調査の範囲と方法をユーザーに確認しています。CI ログの読解では、サブディレクトリは並行ではなく順に 2 回たどられており、2 巡目で自動 PCH を使う 3 ファイルだけが再コンパイルされ、その最中に親の make がオブジェクトを確認して失敗した、という見立てです (実行での裏付けは未実施)。詳細は検討用 Markdown を参照してください。同日、WSL 側の一時複製で原因を再現し、確定しました。原因は makefw にある 2 つの問題の重なりです。(1) 自動 PCH を使うオブジェクトは、コンパイル後に `gcc_pch.py record` が `.d` へ追記するため `.d` がオブジェクトより新しくなり、`make` のたびに再コンパイルされます。(2) リンクの規則が、サブディレクトリの再帰 make と、サブディレクトリのオブジェクトを順序なしで前提に並べているため、親の make が再帰 make の完了を待たずにオブジェクトの存在を確認します。アセンブラーは出力先を削除してから作り直すので、再コンパイル中にこの確認が当たると失敗します。「原因の見立て (未確定)」の「並行して 2 回ビルドされた」は誤りで、2 回のビルドは順に実行されています。ユーザーの判断で、修正も同じ継続案件で行うことになりました (継続案件の Q-001)。修正の方法は次のとおり合意しました (継続案件の Q-004 から Q-008)。(1) は `gcc_pch.py record` が `.d` の更新日時をオブジェクトと同じにする方法、(2) はサブディレクトリのオブジェクトごとに、属するサブディレクトリの再帰 make を順序だけの前提にする方法で直します。あわせて、1 回の `make` で同じサブディレクトリを 2 回たどる構造もこの継続案件で解消することになり、`build` の前提としてたどる 1 回目を残し、起動し直した make ではたどらない方法に決まりました (継続案件の Q-009)。最終仕様 `framework/makefw/docs/prompts/current/261116-makefw-subdir-rebuild-race-design.md` を作成し、ユーザーの承認を待っています。 2026-11-18、継続案件の範囲の追加 (`subfolder-compilation.md` の見直し、全体のカバレッジにサブディレクトリのソースが入らない testfw の不具合の修正、Windows で何もない状態からの `make test` がサブディレクトリをコンパイルしない問題の修正) の合意が済み、これらを反映した最終仕様の版 2 を作成しました。同日、ユーザーが最終仕様の版 2 を承認し、makefw と testfw の修正、回帰テストの追加、文書の見直しを行いました。WSL と Windows の一時複製での局所検証では、変更前の makefw で CI と同じメッセージの失敗を再現する回帰テスト (`bin_test/subdir_build_selftest.py`) が、変更後は 5 回中 5 回成功しました。同日、検証中に見つけた既存の問題 (サブディレクトリの中での並列 make で親の成果物が再リンクされない) も、ユーザーの選択で同じ継続案件で直しました。状態は「対応中」で、CI による確認を待っています。 同日、プル リクエスト #60 の CI run 44144556601 (親 `a530edc6`、makefw `7be5329`、testfw `b614731`、porter `0436424`) で、全ジョブ (Windows、ol8、ol9、ol10、docs、verify-gen、warnings-summary) が成功し、警告のアーティファクトはありませんでした。makefw、testfw、porter、親リポジトリの `main` へ fast-forward で取り込み、状態を「解消」にしました。

### 影響範囲とリスク

- **影響対象**: `framework/makefw` のサブディレクトリ走査による並列ビルドと、`app/cplat/test/libsrc/mock_cplat` を含むライブラリのビルド。
- **放置した場合のリスク**: CI が不定期に失敗し、再実行の手間と時間 (ol9 ジョブは 1 回あたり約 2 時間 40 分) が増えます。

### 推奨される対応策

- **恒久対応案**: CI のビルド ログから、`mock_cplat/crt` を 2 回起動している make の呼び出し元を特定します。同じサブディレクトリへの再帰 make が重複しないように、makefw のサブディレクトリ走査の依存関係を見直します。修正後は、並列度を上げたビルドを繰り返して再現しないことを確認します。
- **参考情報**: CI run 43727490184 (attempt 2) の ol9 ジョブのログ、`framework/makefw/docs/subfolder-compilation.md`。

## BL-003 doxyfw の依存関係レポートのテストで、再読み込み後にページ種別の保存値が失われ、CI で不定期に失敗する

### 概要と問題点

- **現状の事象**: CI run 43767517941 (親 `6fc2b41c`、porter `8220519`) の attempt 1 の ol8 ジョブで、bin-test ステップが失敗しました。失敗したのは、`framework/doxyfw/bin_test/test_generate_dependency_report.py` の `OverviewInteractionTest.test_page_link_and_variant_settings` の 2 件 (`operation='reload'` と `operation='reopen'`) です。テストが出力した診断では、再読み込み前は localStorage にページ種別 `ja-details` が保存されています。再読み込みと開き直しの後は、保存値が `None` (読み取りの例外はなし) となり、リンク先が `ja` に戻っています。ブラウザーは headless の Chrome 153 で、ページは `file://` で開いています。同じ run の Windows、ol9、ol10 では成功しています。
- **原因の見立て (未確定)**: 診断出力によると、保存値は再読み込みの前には存在し、再読み込み後のページのスクリプトが最初に読む時点ではすでに消えていました。開き直したページには削除処理がないのに保存値がないため、値は再読み込みの時点で失われています。有力な候補は、初回だけ実行するはずの削除スクリプト (`page.evaluateOnNewDocument()` で登録した `removeItem`) が、再読み込みでも実行されたことです。puppeteer 25.11.0 の `removeScriptToEvaluateOnNewDocument()` は、登録時のフレーム オブジェクトをキーにして識別子を探します。見つからない場合は解除の要求を送らず、エラーも出しません (`FrameManager.js`)。このため、初回の遷移でメインフレームのオブジェクトが置き換わると、解除されずに残ります。もう 1 つの候補は、保存がブラウザー側へ確定する前に再読み込みしたことです。現在の診断出力では、この 2 つを区別できません。
- **本来あるべき姿**: テストが実行環境に左右されず、毎回同じ結果になること。

### 経緯と見送り理由

- **摘出契機**: この案件の `handleConcurrencyTest` 修正 (BL-001) をプッシュした後の CI。
- **今回の見送り理由**: 失敗したのは doxyfw のテストで、この案件の変更 (porter のマルチキャスト参加 NIC と結合テストの接続待ち) とは関係しません。同じテストは、run 42978073210 でも失敗しています。そのときは `app/general/docs/prompts/past/261111-ci-time-backlog-followup-backlog.md` の BL-006 で、原因不明として閉じました。その後、doxyfw の `1144ce6` で保存値の診断出力が追加されています。今回は失敗したジョブの再実行で CI を確認し、テストの修正は行いません。稼働中のセッション (c-modernization-kit-c1、c-modernization-kit-0d) に内容を共有し、0d は対処・調査していないことを確認しました。
- **継続作業での経緯と対処**: 2026-11-16、対処する継続案件を開始しました (検討 ID: 261116-doxyfw-page-variant-reload-flake、検討用 Markdown: `framework/doxyfw/docs/prompts/current/261116-doxyfw-page-variant-reload-flake-discussion.md`、担当: claude code / セッション ID: session_01KJc53UbHjwaWeuucBs5Q5U)。稼働中のセッション (c-modernization-kit-c1、96、0d) へ着手を宣言し、3 セッションとも支障がないことを確認しました。状態は「検討中」で、対処の範囲、削除の方法、検証の方法をユーザーに確認しています。事前調査では、puppeteer 25.11.0 の `FrameManager.js` と `CdpPreloadScript.js` を読み、「原因の見立て」の 1 つ目の候補の経路 (遷移でメインフレームのオブジェクトが作り直されると、解除の要求が送られない) がソース上で成り立つことを確認しました。リポジトリ外の一時スクリプトで同じ順序の操作を Windows の Edge 157 で 30 回実行しましたが、再現していません。 同日、担当を WSL のセッション (claude code / セッション ID: session_01KJc53SqgNC8iDAi6rvzgwh) へ引き継ぎました。WSL 上の Oracle Linux 8.10 と Chrome for Testing 153.0.8010.36 で、CPU を 2 コアに絞って負荷をかけると、CI と同じ失敗が 40 回中 10 回再現しました (負荷なしの 40 回はすべて成功)。切り分けの結果、「原因の見立て」の 1 つ目の候補 (削除スクリプトの再実行) は成り立たないことが分かりました。値が失われた回でも、削除スクリプトの実行は 1 回だけでした。失敗の条件は、起動直後のブラウザーで文書の開始時に `localStorage.removeItem()` を実行し、その後に同じキーへ保存して再読み込みすることです (1 回ごとにブラウザーを起動する試験で、削除ありは 60 回中 24 回、削除なしは 60 回中 0 回)。Chrome の内部で値が失われる理由は未確認で、継続案件の残課題一覧の BL-001 に記録しました。対処の方針は、最初に開くページの削除指定 (`clearVariant: true`) を外すことと、doxyfw の `1144ce6` で追加した診断出力を削除することです (最終仕様: `framework/doxyfw/docs/prompts/current/261116-doxyfw-page-variant-reload-flake-design.md`)。ユーザーが最終仕様を承認しました。同日、`framework/doxyfw/bin_test/page_link_probe.js` を変更し、対象のテスト 1 件を WSL で繰り返して実行しました。負荷ありの 40 回と、負荷なしの 10 回は、すべて成功しました (変更前は、負荷ありで 40 回中 10 回失敗)。状態は「対応済み」です。同日、ユーザーが成果物を承認しました。CI での確認は、コミットとプッシュの後に行います。未対処の項目 (Chrome の内部の原因が未確認) は、`framework/doxyfw/docs/prompts/current/261116-doxyfw-page-variant-reload-flake-backlog.md` の BL-001 へ記録しました。 2026-11-18、修正 (doxyfw `a160e44`) を含む CI run 44144556601 (親 `a530edc6`、doxyfw `fcebcbe`) で全ジョブが成功したことを確認し、状態を「解消」にしました。この失敗は不定期のため、1 回の成功は再発しないことの証明ではありません。

### 影響範囲とリスク

- **影響対象**: `framework/doxyfw/bin_test/test_generate_dependency_report.py` と、その補助スクリプト `framework/doxyfw/bin_test/page_link_probe.js`。
- **放置した場合のリスク**: CI が不定期に失敗し、再実行の手間と時間 (ol8 ジョブは 1 回あたり約 2 時間 40 分) が増えます。

### 推奨される対応策

- **恒久対応案**: まず、削除スクリプトが実行されたことと、削除した時点の値を診断出力に加え、2 つの候補を区別します。削除スクリプトの再実行が原因であれば、解除に頼らない形に直します。たとえば、初回のページで `page.evaluate()` により削除してから再読み込みする方法や、`sessionStorage` の目印で初回だけ削除する方法があります。保存の確定前の再読み込みが原因であれば、保存の確定を待ってから再読み込みします。修正後は、同じテストを繰り返し実行して安定性を確認します。
- **参考情報**: CI run 43767517941 (attempt 1) の ol8 ジョブ `build-and-test-linux (ol8)`、`test_generate_dependency_report.py` の 2963 行目と 2965 行目。

## BL-004 証明書認証の片方向の通信種別で、最初の認証の試行が失敗の警告を出してから接続が確立する

### 概要と問題点

- **現状の事象**: 2026-11-18 の Windows/WSL 相互接続試験で、証明書認証の `unicast` と `multicast` の 2 件だけ、WSL の receiver が次の警告を 1 回出しました。警告の直後に接続が確立し、データは届いています。2 回の実行の両方で、同じ 2 件に出ました。

    ```text
    W [certificate.c:225] certificate: authentication failed: handshake failed or peer rejected certificate
    ```

    `unicast` の例では、receiver の open が 05:46:22.979、Windows の sender の open が 05:46:23.029、警告が 05:46:23.984 です。sender の open から約 0.95 秒後に警告が出ています。証明書認証の `unicast_bidir`、`unicast_bidir_n1`、`tcp`、`tcp_bidir` と、PSK の 6 件では出ていません。
- **原因**: 2026-11-20 に、コードとログの読み取りで次の仮説を得ました。実行による確認はしていません。
    - 本当の認証の失敗ではなく、porter の再試行と DTLS の再送が同じ周期処理で重なることによる、自分どうしの衝突です。
    - 片方向の UDP では、receiver が DTLS のクライアントです (`certificate.c` の `is_client()`)。試験では receiver を先に起動するため、最初の ClientHello は、まだ待ち受けていない sender に届いて失われたと推定します。
    - 1 秒後に DTLS の初回の再送タイマー (OpenSSL の既定) が満了します。`potr_internal_certificate_tick()` は、同じ周期処理の中で、`tick_session()` による古い試行の ClientHello の再送と、再試行の条件 (`retransmitted` が立ち、前回の開始から再試行の周期以上が経過) による新しい試行の ClientHello の送出を続けて行います。再試行の周期は `health_interval_ms` で、試験の設定は 500 ms でした。再試行の周期が 1 秒以下の場合、最初の再送は必ず再試行と重なります。
    - sender は先に届いた古い試行に応答し、直後に届いた新しい試行の ClientHello を、既存のセッションへ渡して捨てます (`potr_internal_certificate_receive()` は、前回の開始から再試行の周期が経過していない ClientHello では受け直しません)。receiver の新しい試行は、古い試行への応答を受け取って失敗し、警告を出します。次の再試行は成功します。
    - 警告の時刻は、receiver の open から約 1.0 秒後 (`unicast`) と約 1.08 秒後 (`multicast`) で、DTLS の 1 秒のタイマーと整合します。「sender の open から約 0.95 秒後」は見かけの一致です。
    - 双方向の UDP では sender がクライアントで、サーバーより後に起動するため起きません。TCP は接続の上で同期して認証するため、PSK はハンドシェイクがないため、起きません。
    - 推定にとどまる点は、最初の ClientHello が失われたことと、新しい試行が失敗する OpenSSL の処理の段階です。
- **本来あるべき姿**: 最初の試行の喪失を再試行で回復することは設計どおりですが、再送と再試行が重なって自分の再送を無駄にし、認証の失敗の警告を出すことは、起こるべきでない動作と考えます。再試行する周期処理では古い試行を再送しないことが、あるべき姿です。

### 経緯と見送り理由

- **摘出契機**: この案件の受入試験 (Windows/WSL 相互接続 12 組み合わせ、検討用 Markdown の「反映と検証の記録」)。
- **今回の見送り理由**: この案件の対象は、マルチキャスト受信側のグループ参加 NIC と送信元の照合の設定契約です。証明書認証の手順は変更していません。受入条件 (12 件の全件成功) は満たしています。同じ警告が前回 (2026-11-15) の試験でも出ていたかは確認していません。

### 影響範囲とリスク

- **影響対象**: 証明書認証を使う片方向の UDP 通信 (`unicast`、`multicast`) の接続の確立。別ホスト間 (Windows の sender、WSL の receiver) で確認しました。同じホスト内や、Linux どうし、Windows どうしでの発生は未確認です。
- **放置した場合のリスク**: 接続の確立が、最初の試行の失敗の分だけ遅れている可能性があります (遅れの大きさは未測定)。運用時に、正常な接続でも認証失敗の警告がログに残り、本当の認証失敗と区別しにくくなります。

### 推奨される対応策

- **恒久対応案**: クライアント側の `potr_internal_certificate_tick()` で、再試行の条件を DTLS の再送より先に評価し、再試行する周期処理では古い試行の再送を行わないようにします。変更は `certificate.c` に限られ、通信の形式は変わりません。あわせて、認証の失敗の警告に OpenSSL の失敗の理由を含めると、本当の失敗と区別しやすくなります。警告の水準を下げる対応は、本当の失敗を隠すため勧めません。
- **確認の手順**: 同じホストの Linux で、証明書認証の `unicast`、`udp_health_interval_ms` 500 の構成にし、receiver を先に、0.2 秒ほど後に sender を起動して、パケットを記録します。receiver の open の約 1 秒後に、receiver から連続した 2 個の ClientHello が出ることを確かめます。対照として、sender を先に起動した場合と、`udp_health_interval_ms` を 3,000 にした場合に、警告が出ないことを確かめます。`certificate.c` には DEBUG のトレースがないため、ログ水準を上げても情報は増えません。
- **参考情報**: `prod/libsrc/porter/security/certificate.c` の `advance_session()` と `start_session()` (`retry_ms`)。試験のログは BL-005 の一時領域の `cert-unicast.log` と `cert-multicast.log`。

## BL-005 Windows/WSL 相互接続試験のスクリプトが Git で管理されていない

### 概要と問題点

- **現状の事象**: Windows/WSL 相互接続 12 組み合わせの試験スクリプト `interop_matrix.py`、生成した設定 24 個、組み合わせごとのログは、作業用の一時領域 `C:\Users\tetsuo\AppData\Local\Temp\porter-interop-20261118` にあります。一時領域は、再起動や掃除で消える場合があります。スクリプトには、この環境のアドレス (Windows `172.28.208.1`、WSL `172.28.209.184`)、WSL のディストリビューション名 (`OracleLinux8-Dev`)、リポジトリの絶対パスが直接書かれています。
- **本来あるべき姿**: 同じ試験を、別の時点や別の環境で、同じ手順でやり直せることです。

### 経緯と見送り理由

- **摘出契機**: この案件の受入試験。スクリプトは、前回 (2026-11-15) の試験で使われた一時領域のスクリプトを直して使いました。
- **今回の見送り理由**: リポジトリで管理する場合の置き場所と、環境に依存する値の与え方が決まっていません。試験の方式、マルチキャストの設定例、結果は、検討用 Markdown の「反映と検証の記録」に記録済みです。

### 影響範囲とリスク

- **影響対象**: porter の別ホスト間の相互接続の確認 (CI では確認できない範囲)。
- **放置した場合のリスク**: 次に相互接続試験が必要になったときに、スクリプトを作り直す手間がかかります。判定の方法 (receiver の出力のトークン、未接続で拒否された送信の送り直し) が試験のたびに変わると、結果を比べにくくなります。

### 推奨される対応策

- **恒久対応案**: スクリプトの置き場所 (たとえば `app/porter/bin/` と、その検証を置く `app/porter/bin_test/`) を決めます。アドレス、ディストリビューション名、WSL 側のリポジトリのパスを引数で受け取る形に直し、手順を porter の文書に書きます。置き場所と文書の構成は、新しい検討で合意してください。
- **参考情報**: 一時領域の `interop_matrix.py` (使い方: `python interop_matrix.py <WSL 側のリポジトリの絶対パス> [認証/種別 ...]`)。
