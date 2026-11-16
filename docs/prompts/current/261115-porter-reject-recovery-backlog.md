<!--
生成元: agree-before-implement スキル
文書種別: 残課題一覧 (backlog)
最終更新日時: 2026-11-17 04:58:00 JST
最終更新エージェント: claude code / セッション ID: session_01KJc53TxVv1vjB6xMfjiUgc (旧継続案件の打ち切りと、やり直しの継続案件の開始を記録)
稼働 OS: Windows
上流文書: `261115-porter-reject-recovery-design.md` (最終仕様)、`261115-porter-reject-recovery-discussion.md` (検討用 Markdown、版 4・完了)。いずれも削除されている場合があります。
下流文書: BL-001、BL-002 の継続作業 (2026-11-17 開始、検討 ID: `porter-transport-performance-redo-20261117`): `261117-porter-transport-performance-redo-discussion.md` (検討用 Markdown)。打ち切った旧継続案件 (検討 ID: `porter-transport-performance-20261115`、2026-11-17 に打ち切り) の記録: `261115-porter-transport-performance-discussion.md` (版 6)、`261115-porter-transport-performance-design.md`、`261115-porter-transport-performance-validation.md`。未作成の場合や、削除されている場合があります。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。「整理状態」が「随時追記中」であれば、案件の作業中です。気付いた事項を追記し、第 3 ゲートの前に文書を再構築して整理します。上流文書が存在する場合は、各課題の背景と見送りの判断を上流文書で確認します。存在しない場合は、この文書を単体の残課題一覧として扱います。このファイルの残課題を参照して新たな成果物の作成または更新を始めるときは、元案件の再開とは別の作業として扱います。その開始には agree-before-implement を原則として使い、新しい検討用 Markdown から開始してください。継続作業によってこのファイルの残課題を対策する場合は、このファイルや各項目の記載を削除することなく、各項目の状態および経緯と対処を更新することを原則とします。
-->

# porter の REJECT 受信後の受信再開の残課題

ファイル名: `261115-porter-reject-recovery-backlog.md`

## 案件の識別

案件名: `porter-reject-recovery`  
作成日: `2026-11-15`  
整理状態: 整理済み

この文書は、本案件で対策しなかった残課題だけを記録します。本案件は、対策元 [261115-porter-handle-lifecycle-perf-backlog.md](261115-porter-handle-lifecycle-perf-backlog.md) の BL-002 だけを対策しました。対策元の未対処の項目は、この文書へ転記しています。

## 残課題一覧

| 課題 ID | 題名 | 区分 | 優先度 / 影響度 | 状態 |
| --- | --- | --- | --- | --- |
| BL-001 | TCP の送信ソケットに TCP_NODELAY がなく約 40[ms] の停止が生じる | 過去 backlog からの転記 | 中 | 検討中 (実装はブランチ `fix/bl001-tcp-nodelay` で保持し、`main` には未取り込み。旧継続案件を打ち切り、継続案件 `porter-transport-performance-redo-20261117` で採否と検証の範囲を決め直す。[検討文書](261117-porter-transport-performance-redo-discussion.md)) |
| BL-002 | 小さいメッセージと圧縮送信で処理件数が低下し通知遅延が増える | 過去 backlog からの転記 | 中 | 検討中 (原因は未特定で、製品改修は未着手。旧継続案件を打ち切り、継続案件 `porter-transport-performance-redo-20261117` でこの課題を扱う範囲を確認中。[検討文書](261117-porter-transport-performance-redo-discussion.md)) |

Table: 残課題一覧

## BL-001 TCP の送信ソケットに TCP_NODELAY がなく約 40[ms] の停止が生じる

### 概要と問題点

- **現状の事象**: porter と cplat は、TCP ソケットに TCP_NODELAY を設定していません。Linux (WSL2) のループバックで、DATA と FIN が受信側へ届くまでに約 40[ms] かかる場合があります。Nagle アルゴリズムと遅延 ACK が重なった待ちと一致します。Linux の TCP の close は毎回約 43[ms] かかりました (P4)。Linux の P5 では、ブロッキング送信と 4 スレッドの多くの条件で、通知遅延 p99 が約 42〜44[ms] でした。
- **本来あるべき姿**: 未確定です。小さいメッセージの遅延を優先して TCP_NODELAY を設定するか、送信をまとめる現状を維持するかを、利用条件に基づいて決める必要があります。

### 経緯と見送り理由

- **摘出契機**: `porter-handle-lifecycle-perf` 案件の Linux での試行 (P4 と P5 の TCP) の調査で見つかりました (2026-11-15)。対策元の BL-001 です。
- **今回の見送り理由**: 本案件は、原因と対策の範囲が明確な UDP の受信停止 (対策元の BL-002) だけを対象としました。TCP_NODELAY は採否の判断が先に必要なため、別案件とします。

### 影響範囲とリスク

- **影響対象**: TCP の通信種別 (POTR_TYPE_TCP、POTR_TYPE_TCP_BIDIR) の送達遅延と close の所要時間。
- **放置した場合のリスク**: 低頻度の小さいメッセージや close で、数十 [ms] の遅れが生じます。

### 推奨される対応策

- **恒久対応案**: TCP_NODELAY の採否を、遅延と送信効率の要求から判断します。採用する場合は、ソケットの作成箇所 (cplat または porter の TCP 接続の確立処理) で設定し、P4 と P5 を両 OS で測り直して効果を確認します。
- **参考情報**: [検証記録](261115-porter-handle-lifecycle-perf-validation.md) の「TCP 終了時エラーの切り分けと修正」「open/close 反復 (P4)」。

### 継続作業での経緯と対処

- **状態**: 検討中 (実装はブランチ `fix/bl001-tcp-nodelay` で保持し、`main` には入っていません。採否と検証の範囲を継続案件で決め直します)
- **継続 ID と検討文書**: `porter-transport-performance-redo-20261117` ([261117-porter-transport-performance-redo-discussion.md](261117-porter-transport-performance-redo-discussion.md))
- **打ち切った旧継続案件の記録**: `porter-transport-performance-20261115` ([261115-porter-transport-performance-discussion.md](261115-porter-transport-performance-discussion.md)、[261115-porter-transport-performance-design.md](261115-porter-transport-performance-design.md)、[261115-porter-transport-performance-validation.md](261115-porter-transport-performance-validation.md))。以下の 2026-11-15 から 2026-11-17 の 4 項目は旧継続案件の経緯です。
- **対処状況**:
    - 2026-11-15: 先行深掘り調査として、`cplat` ソケット層と `porter` の TCP 接続・受け入れ・再接続・双方向送信・制御パケット経路を特定し、`TCP_NODELAY` 採否、責務分担、失敗時方針の意思決定事項 (Q-001〜Q-003、Q-005) を整理しました。この時点では第 1 ゲート合意前であり、設計書作成と実装は未着手、元案件 `porter-reject-recovery` 自体は進行中として扱っていました。
    - 2026-11-16: 元案件 `porter-reject-recovery` の完了 (`app/porter` `11016c1`、版 4・第 3 ゲート承認済み・CI run 43724723928 成功・P3 再測定 25 試行再開) を確認し、Q-005 凍結条件の満了と Q-001〜Q-003 の合意により第 1 ゲートを通過しました (`discussion.md` 版 2)。確定方針を [261115-porter-transport-performance-design.md](261115-porter-transport-performance-design.md) に具体化し、同日の承認と `grok --yolo high` で実装へ進みました。
    - 2026-11-16: レビュー修正後の局所検証では、両 OS で対象 leaf をビルドし、`--gtest_filter=*` の直接実行で全ケースを確認しました。正式エビデンスは新規 3 ケースの `make test GTEST_FILTER` だけです。Linux の全件 `make test` は、ケースごとの後処理が長く、親が PGID 2840942 を停止したため未完了です。この中断は成功にしていません。詳細は [検証記録](261115-porter-transport-performance-validation.md) です。
    - 2026-11-17: 固定した `off` / `on` 版で、Windows と Linux の P4 TCP 1 条件および P5 17 条件を各 5 回、公式ランナーで測定しました。Linux P4 close p50 は 42.421[ms] から 0.614[ms]、Windows の close p95 は 15.656[ms] から 1.823[ms] へ改善しました。P5 の 17 条件の throughput 変化中央値は Windows +4.9%、Linux +81.9% ですが、Linux の 5000[B] blocking・4 スレッドは 16,526 から 3,655[msg/s] へ 77.9%低下しました。ユーザーは Q-006 A でこの低下を既知のトレードオフとして受け入れ、常時適用方針を確定しました。測定条件と全結果は [検証記録](261115-porter-transport-performance-validation.md) を参照してください。
    - 2026-11-17: ユーザーの指示で旧継続案件を打ち切りました。論点が整理できない状態になったためです。BL-001 の実装は未コミットのまま残っていたため、cplat (`19302982`) と porter (`e625621`) のブランチ `fix/bl001-tcp-nodelay` へコミットして保持しました。`main` には入れていません。理由は、Linux の全件テスト、Doxygen の生成、CI が未確認であることと、Linux の 5000[B] の処理件数の低下 (35.8%〜77.9%) の原因が未特定であることです。旧継続案件での受容 (Q-006 A) は、やり直しの継続案件へ引き継いでいません。
    - 2026-11-17: やり直しの継続案件 `porter-transport-performance-redo-20261117` を開始しました。状態は「検討中」で、案件の範囲、旧方針の引き継ぎ、5000[B] の低下の扱い、`main` へ入れる前の検証の範囲をユーザーに確認しています。

## BL-002 小さいメッセージと圧縮送信で処理件数が低下し通知遅延が増える

### 概要と問題点

- **現状の事象**: ハンドル寿命管理の改修前版に比べて、現行版は次の条件で処理件数が下がり、通知遅延が増えました (5 回の中央値)。ブロッキング送信では、差は数 % 以内でした。
    - UDP 64 バイト・通常・1 スレッド: 処理件数 Windows -34.5%、Linux -23.6%
    - UDP 64 バイト・圧縮・4 スレッド: 処理件数 Windows -26.5%、Linux -60.8%。通知遅延 p99 は約 8 倍
    - TCP 圧縮・4 スレッド: 処理件数 Windows -72.3% (64 バイト)、-61.4% (5000 バイト)
    - Linux の N:1 返信 (外部スレッド、64 バイト): 往復時間 約 1.5 倍、処理件数 -31.3%
- **本来あるべき姿**: 未確定です。安全性と公開契約を維持したまま改善できる追加コストがあるかを特定し、許容するか改善するかを判断する必要があります。

### 経緯と見送り理由

- **摘出契機**: `porter-handle-lifecycle-perf` 案件の P1、P2、P5 の本測定 (2026-11-15) で確認しました。対策元の BL-003 です。
- **今回の見送り理由**: 原因の特定にプロファイラーなどを使った追加の調査が必要で、対策の形がまだ決まっていないため、別案件とします。

### 影響範囲とリスク

- **影響対象**: 非ブロッキング送信の処理件数と通知遅延。特に圧縮を指定した複数スレッドの送信。
- **放置した場合のリスク**: 小さいメッセージを高頻度で送る用途や圧縮を多用する用途で、改修前より処理能力が下がります。

### 推奨される対応策

- **恒久対応案**: 圧縮・4 スレッドの条件でプロファイルを取り、通知スレッドの起床、通知キューへの受け渡し、圧縮と展開の各段階の時間とロックの競合を測ります。主因を特定してから、改善案と安全性への影響を比べます。
- **参考情報**: [検証記録](261115-porter-handle-lifecycle-perf-validation.md) の「UDP と TCP の遅延分布 (P1、P5)」「N:1 返信 (P2)」。

### 継続作業での経緯と対処

- **状態**: 検討中 (原因は未特定で、製品改修は未着手です。継続案件でこの課題を扱う範囲を確認しています)
- **継続 ID と検討文書**: `porter-transport-performance-redo-20261117` ([261117-porter-transport-performance-redo-discussion.md](261117-porter-transport-performance-redo-discussion.md))
- **打ち切った旧継続案件の記録**: `porter-transport-performance-20261115` ([261115-porter-transport-performance-discussion.md](261115-porter-transport-performance-discussion.md)、[261115-porter-transport-performance-design.md](261115-porter-transport-performance-design.md))。以下の 2026-11-15 から 2026-11-17 の 3 項目は旧継続案件の経緯です。
- **対処状況**:
    - 2026-11-15: 先行深掘り調査として、毎回 65KiB 超の圧縮一時確保と全量 `cplat_secure_zero`、`potr_handle` ロック、`potr_send_queue` の無条件起床、`potr_notify` の空きリスト探索、N:1 `peers_mutex` 保持区間を抽出し、BL-001 影響を揃えた段階的測定計画と意思決定事項 (Q-004〜Q-005) を整理しました。この時点では第 1 ゲート合意前であり、設計書作成・計測・実装は未着手、元案件 `porter-reject-recovery` 自体は進行中として扱っていました。
    - 2026-11-16: 元案件 `porter-reject-recovery` の完了 (`app/porter` `11016c1`、版 4・第 3 ゲート承認済み・CI run 43724723928 成功・P3 再測定 25 試行再開) を確認し、Q-005 凍結条件の満了と Q-004 の合意により第 1 ゲートを通過しました (`discussion.md` 版 2)。公開契約・ハンドル寿命・メモリ消去・通知順・`close` 安全性を維持した前提で、BL-001 単独確認および `TCP_NODELAY` 有効化揃えのもとでの 8 観点プロファイル・区間計測・非計装分離・同時測定禁止を定めた [261115-porter-transport-performance-design.md](261115-porter-transport-performance-design.md) を作成し、同日の承認で第 2 ゲートを通過しました。BL-001 は実装と両 OS の局所検証を完了し、P4/P5 の両 OS A/B 測定を準備中です。BL-002 のプロファイル調査は BL-001 測定後、実装設計と数値目標の確定は計測後の別判断とし、製品改修には着手していません。
    - 2026-11-17: BL-001 の非計装 A/B 測定を完了しました。BL-002 の wrap プロファイルは、Windows で通常送信 throughput が最大 91.5%低下し、`GetThreadTimes` の CPU 差分が 15.625[ms] 単位に量子化されました。Linux は `perf_event_open` が `EACCES` となり、CPU 時間へのフォールバックでも代表条件の throughput が 68.8%低下しました。どちらも通常実行の関数別 CPU 寄与率を示す根拠にできません。ユーザーは Q-007 B を選択し、低負荷の別方式を一時ビルドで設計・pilot 検証する方針を合意しました。その後、Grok の初回方式案に対する Sol のレビューと pilot 用ソース作成前にユーザー指示で一時中断しました。`profile/pilot/` は未作成で、ビルド・測定は未実施です。検証方法と限界は [検証記録](261115-porter-transport-performance-validation.md) に記録し、BL-002 の製品改修には着手していません。
    - 2026-11-17: ユーザーの指示で旧継続案件を打ち切りました。旧継続案件で合意した低負荷の計測方式 (Q-007 B) は、やり直しの継続案件へ引き継いでいません。やり直しの継続案件 `porter-transport-performance-redo-20261117` を開始し、この課題を同じ案件で扱うか、BL-001 の完了後に別の案件とするかをユーザーに確認しています。
