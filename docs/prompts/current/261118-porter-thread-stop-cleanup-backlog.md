<!--
生成元: agree-before-implement スキル
文書種別: 残課題一覧 (backlog)
最終更新日時: 2026-11-18 08:14:29 JST
最終更新エージェント: claude code / セッション ID: session_01KJc53TxVv1vjB6xMfjiUgc
稼働 OS: Windows
上流文書: `261118-porter-thread-stop-cleanup-design.md` (最終仕様)、`261118-porter-thread-stop-cleanup-discussion.md` (検討用 Markdown)。いずれも削除されている場合があります。
下流文書: この残課題一覧を対策する継続作業が開始された場合は、その検討用 Markdown・最終仕様・残課題一覧のパスを追記します。未作成の場合や、削除されている場合があります。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。「整理状態」が「随時追記中」であれば、案件の作業中です。気付いた事項を追記し、第 3 ゲートの前に文書を再構築して整理します。上流文書が存在する場合は、各課題の背景と見送りの判断を上流文書で確認します。存在しない場合は、この文書を単体の残課題一覧として扱います。このファイルの残課題を参照して新たな成果物の作成または更新を始めるときは、元案件の再開とは別の作業として扱います。その開始には agree-before-implement を原則として使い、新しい検討用 Markdown から開始してください。継続作業によってこのファイルの残課題を対策する場合は、このファイルや各項目の記載を削除することなく、継続作業の開始時 (検討用 Markdown の作成時) から検討・設計・実装・整理の各段階において、このファイルのメタ情報、各項目の状態、および経緯と対処を随時更新することを原則とします。また、フェーズ分けした対策で対処ができていない項目がある場合は、当該継続作業の backlog にも必ず転記し、最新作業の backlog を見れば残件が把握できるようにしてください。最終更新エージェントのセッション情報や稼働 OS・最終更新日時が参照可能であれば再開時のコンテキストや環境差分の把握に利用し、別エージェントや別環境・別 OS からの引き継ぎ、担当変更によりセッションが見つからない場合も異常とせず、文書の記載から継続してください。
-->

# porter のスレッド停止関数と資源保持の分岐の整理の残課題一覧

ファイル名: `261118-porter-thread-stop-cleanup-backlog.md`

## 案件の識別

案件名: `porter-thread-stop-cleanup-20261118`  
作成日: `2026-11-18`  
整理状態: 整理済み

この文書は、作業中に別問題として判明し、この案件では対策しなかった事項を記録したものです。2026-11-18 に整理しました。

## 残課題一覧

| 課題 ID | 題名 | 区分 | 優先度 / 影響度 | 状態 |
| --- | --- | --- | --- | --- |
| BL-001 | 機能仕様の検査 (`check_functional_spec.py`) が、既存の 35 件の問題で失敗する | 作業中摘出 | 中 / 低 | 未着手 |

## BL-001 機能仕様の検査 (check_functional_spec.py) が、既存の 35 件の問題で失敗する

### 概要と問題点

- **現状の事象**: ワークスペース ルートで `python3 app/general/bin/check_functional_spec.py` を実行すると、「要件 ID に UUID が併記されていません」が 35 件出て、終了コード 1 で失敗します (2026-11-18、親 `b2f6c568`、porter `4abc542`、cplat `19302982` の時点。この案件の変更を除く)。内訳は次のとおりです。

    | 対象 | 件数 |
    | --- | --- |
    | `app/cplat/docs/prompts/current/cplat-account-session-discussion.md` | 8 |
    | `app/porter/docs/prompts/current/261115-porter-multicast-src-contract-discussion.md` と `-design.md` | 8 |
    | `app/porter/docs/prompts/past/261114-porter-windows-verification-clock-unify-backlog.md` と `-discussion.md` | 5 |
    | `app/porter/docs/prompts/past/261115-porter-reject-recovery-discussion.md` と `-design.md` | 7 |
    | `app/porter/docs/prompts/past/261115-porter-transport-performance-design.md` と `-validation.md` | 3 |
    | `app/porter/docs/prompts/past/261109-porter-path-priority-timestamp-auth-discussion.md` | 1 |
    | `app/porter/prod/libsrc/porter/api/potr_service_send.c` の 244 行目 | 1 |
    | 上記の `potr_service_send.c` の写し (`test/src/integration/certificateTest/`、`test/src/libporter/potr_service_send_test/`) | 2 |

    Table: 機能仕様の検査の既存の問題

    行ごとの要件 ID は、検査を実行すると表示されます。この一覧に要件 ID を書くと同じ検査の対象になるため、件数だけを記載しています。製品のソースの問題は、`potr_service_send.c` のコメントの 1 か所だけです。末尾の 2 件は、テストのビルドが製品のソースから取り込む Git 管理外の写しで、製品のソースを直せば解消します。残りの 32 件は、agree-before-implement の検討文書、最終仕様、残課題一覧の本文で要件 ID を書いた箇所です。

- **本来あるべき姿**: 検査が成功することです。`app/porter/AGENTS.md` は、機能仕様を変更した場合にこの検査を実行するよう定めています。検査が常に失敗していると、新しく作り込んだ問題を見分けられません。

### 経緯と見送り理由

- **摘出契機**: この案件の受入条件 6 (`check_functional_spec.py` の成功) の確認で発覚しました。この案件で作成した最終仕様にも同じ問題が 2 件ありましたが、UUID を併記して解消しました。
- **今回の見送り理由**: この案件の変更とは関係のない、以前からの問題です。ユーザーの指示 (2026-11-18 の対話「残り 35 件は backlog へ」) で、この残課題一覧へ記録しました。

### 影響範囲とリスク

- **影響対象**: `app/general/bin/check_functional_spec.py` による確認全般。機能仕様を変更する作業の受入確認。
- **放置した場合のリスク**: 検査の失敗が常態化し、機能仕様と下流成果物の参照の不整合を新しく作り込んでも気付けません。agree-before-implement の文書を作るたびに、同じ種類の問題が増えます。

### 推奨される対応策

- **恒久対応案**: 次の 2 つを分けて判断します。
    - `potr_service_send.c` の 244 行目のコメントは、検査が受け付ける要件コメントの形式 (タグ、要件 ID、UUID を並べた 1 行のコメント) に直すか、要件 ID の記述を外します。
    - 検討文書などの `docs/prompts` 配下を、検査の対象から外すか、文書側で UUID を併記するかを決めます。`docs/prompts/past` は過去の断面の記録のため、内容を書き換えずに検査の対象から外す案が考えられます。agree-before-implement の文書の作り方に関わるため、`app/general` の検査の仕様の変更として扱います。
- **参考情報**: `app/general/bin/check_functional_spec.py` の `Patterns` (`markdown_ref`、`source_block_ref`、`source_line_ref`)、`app/general/docs/functional-spec-guideline.md`。
