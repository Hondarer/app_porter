<!--
生成元: agree-before-implement スキル
文書種別: 残課題一覧 (backlog)
最終更新日時: 2026-11-18 05:49:58 JST
最終更新エージェント: claude code (Opus 5.5) / セッション ID: session_01KJc53TxVv1vjB6xMfjiUgc
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
整理状態: 随時追記中

この文書は、作業中に別問題として判明した事項を随時記録しています。第 3 ゲートの前に再構築して整理します。

## 残課題一覧

| 課題 ID | 題名 | 区分 | 優先度 / 影響度 | 状態 |
| --- | --- | --- | --- | --- |
| BL-001 | handleConcurrencyTest がクライアント側の CONNECTED を待たずに送信し、CI で不定期に失敗する | 作業中摘出 | 中 / 中 | 対処済み (本案件の Q-006、Q-007。CI run 43835974193 で確認済み) |
| BL-002 | mock_cplat の並列ビルドで同じサブディレクトリが 2 回ビルドされ、CI で不定期に失敗する | 作業中摘出 | 中 / 中 | 解消 (継続案件 261116-makefw-subdir-rebuild-race。CI run 44144556601 で確認し、2026-11-18 に `main` へ取り込み済み) |
| BL-003 | doxyfw の依存関係レポートのテストで、再読み込み後にページ種別の保存値が失われ、CI で不定期に失敗する | 作業中摘出 | 中 / 中 | 対応済み (継続案件 261116-doxyfw-page-variant-reload-flake。ローカルの検証は完了し、成果物の承認済み。CI での確認は未了) |

## BL-001 handleConcurrencyTest がクライアント側の CONNECTED を待たずに送信し、CI で不定期に失敗する

### 概要と問題点

- **現状の事象**: CI run 43727490184 (porter `ebb79c2`) の ol9 ジョブで、`app/porter/test/src/integration/porterSendRecvTest/handleConcurrencyTest.cc` の `handleConcurrencyTest.close_waits_for_running_callback_and_rejects_callback_send` が失敗しました。556 行目の `potr_service_send(clients[0], POTR_PEER_NA, &data, sizeof(data), POTR_SEND_BLOCKING)` が `POTR_ERR_DISCONNECTED` (-20) を返しています。同じテストは、同じ run の Windows、ol8、ol10 と、直前の run 43724723928 の ol9 では成功しています。
- **原因の見立て**: 準備処理 `open_n1()` (同ファイル 245 行目付近) は、サーバー側で全クライアントが CONNECTED になる (`s_connected_peers`) ことだけを待ちます。クライアント側は、相手の PING 応答を受けるまで CONNECTED になりません。そのため、クライアントが PING 応答を受ける前に送信すると DISCONNECTED になる競合があります。
- **本来あるべき姿**: テストの準備処理が、送信に使う側 (クライアント) の CONNECTED も待ってから本体の手順へ進み、実行環境の速度に左右されずに成功すること。

### 経緯と見送り理由

- **摘出契機**: BL-001 (マルチキャスト参加 NIC) の実装をプッシュした後の CI。
- **今回の見送り理由**: 失敗した経路は `unicast_bidir_n1` で、この案件の変更 (マルチキャストのグループ参加 NIC と、close 時の離脱) とは関係しません。並行していた REJECT 受信再開の案件 (porter `68ab091`) も、REJECT 受信時と受信ウィンドウ範囲外のパケットでだけ働くため、関係しないと判断しています (担当セッションと確認済み)。この案件の範囲外なので、失敗したジョブの再実行で CI を確認し、テストの修正は行いません。
- **継続作業での経緯と対処**: 2026-11-16、ユーザーの判断で本案件の範囲に加えました (検討用 Markdown の Q-006)。Q-007 で「`open_n1()` で全クライアントの CONNECTED も待つ」に決まり、最終仕様に追記して承認を受けました。`handleConcurrencyTest.cc` の `open_n1()` に、クライアント側の CONNECTED 数 (`s_client_connected_count`) が `n_clients` に達するまでの待ちを追加しました (`8220519`)。Windows/MSVC で `porterSendRecvTest` 36 件が成功し、`handleConcurrencyTest` 10 件を 5 回繰り返して失敗 0 でした。CI run 43835974193 (親 `fbbc1d848a`、porter `5ff9609`) で、全ジョブ (Windows、ol8、ol9、ol10、docs、verify-gen、deploy-pages、warnings-summary) の成功を確認しました。

### 影響範囲とリスク

- **影響対象**: `app/porter/test/src/integration/porterSendRecvTest/handleConcurrencyTest.cc` の `open_n1()` を使うテスト。
- **放置した場合のリスク**: CI が不定期に失敗し、再実行の手間が増えます。本当の不具合による失敗を見逃しやすくなります。

### 推奨される対応策

- **恒久対応案**: `open_n1()` で、サーバー側の接続確認に加えて、各クライアントの CONNECTED 通知も待つようにします。クライアント側のコールバックで CONNECTED を記録し、`wait_for()` で全クライアント分を待つ形が考えられます。修正後は、同じテストを繰り返し実行して安定性を確認します。
- **参考情報**: CI run 43727490184 (attempt 1) の ol9 ジョブ `build-and-test-linux (ol9)`、`handleConcurrencyTest.cc:556`。

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
- **継続作業での経緯と対処**: 2026-11-16、対処する継続案件を開始しました (検討 ID: 261116-doxyfw-page-variant-reload-flake、検討用 Markdown: `framework/doxyfw/docs/prompts/current/261116-doxyfw-page-variant-reload-flake-discussion.md`、担当: claude code / セッション ID: session_01KJc53UbHjwaWeuucBs5Q5U)。稼働中のセッション (c-modernization-kit-c1、96、0d) へ着手を宣言し、3 セッションとも支障がないことを確認しました。状態は「検討中」で、対処の範囲、削除の方法、検証の方法をユーザーに確認しています。事前調査では、puppeteer 25.11.0 の `FrameManager.js` と `CdpPreloadScript.js` を読み、「原因の見立て」の 1 つ目の候補の経路 (遷移でメインフレームのオブジェクトが作り直されると、解除の要求が送られない) がソース上で成り立つことを確認しました。リポジトリ外の一時スクリプトで同じ順序の操作を Windows の Edge 157 で 30 回実行しましたが、再現していません。 同日、担当を WSL のセッション (claude code / セッション ID: session_01KJc53SqgNC8iDAi6rvzgwh) へ引き継ぎました。WSL 上の Oracle Linux 8.10 と Chrome for Testing 153.0.8010.36 で、CPU を 2 コアに絞って負荷をかけると、CI と同じ失敗が 40 回中 10 回再現しました (負荷なしの 40 回はすべて成功)。切り分けの結果、「原因の見立て」の 1 つ目の候補 (削除スクリプトの再実行) は成り立たないことが分かりました。値が失われた回でも、削除スクリプトの実行は 1 回だけでした。失敗の条件は、起動直後のブラウザーで文書の開始時に `localStorage.removeItem()` を実行し、その後に同じキーへ保存して再読み込みすることです (1 回ごとにブラウザーを起動する試験で、削除ありは 60 回中 24 回、削除なしは 60 回中 0 回)。Chrome の内部で値が失われる理由は未確認で、継続案件の残課題一覧の BL-001 に記録しました。対処の方針は、最初に開くページの削除指定 (`clearVariant: true`) を外すことと、doxyfw の `1144ce6` で追加した診断出力を削除することです (最終仕様: `framework/doxyfw/docs/prompts/current/261116-doxyfw-page-variant-reload-flake-design.md`)。ユーザーが最終仕様を承認しました。同日、`framework/doxyfw/bin_test/page_link_probe.js` を変更し、対象のテスト 1 件を WSL で繰り返して実行しました。負荷ありの 40 回と、負荷なしの 10 回は、すべて成功しました (変更前は、負荷ありで 40 回中 10 回失敗)。状態は「対応済み」です。同日、ユーザーが成果物を承認しました。CI での確認は、コミットとプッシュの後に行います。未対処の項目 (Chrome の内部の原因が未確認) は、`framework/doxyfw/docs/prompts/current/261116-doxyfw-page-variant-reload-flake-backlog.md` の BL-001 へ記録しました。

### 影響範囲とリスク

- **影響対象**: `framework/doxyfw/bin_test/test_generate_dependency_report.py` と、その補助スクリプト `framework/doxyfw/bin_test/page_link_probe.js`。
- **放置した場合のリスク**: CI が不定期に失敗し、再実行の手間と時間 (ol8 ジョブは 1 回あたり約 2 時間 40 分) が増えます。

### 推奨される対応策

- **恒久対応案**: まず、削除スクリプトが実行されたことと、削除した時点の値を診断出力に加え、2 つの候補を区別します。削除スクリプトの再実行が原因であれば、解除に頼らない形に直します。たとえば、初回のページで `page.evaluate()` により削除してから再読み込みする方法や、`sessionStorage` の目印で初回だけ削除する方法があります。保存の確定前の再読み込みが原因であれば、保存の確定を待ってから再読み込みします。修正後は、同じテストを繰り返し実行して安定性を確認します。
- **参考情報**: CI run 43767517941 (attempt 1) の ol8 ジョブ `build-and-test-linux (ol8)`、`test_generate_dependency_report.py` の 2963 行目と 2965 行目。
