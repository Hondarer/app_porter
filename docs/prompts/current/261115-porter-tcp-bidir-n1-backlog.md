<!--
生成元: agree-before-implement スキル
文書種別: 残課題一覧 (backlog)
最終更新日時: 2026-11-23 08:55:00 JST
最終更新エージェント: claude code / セッション ID: session_01KJc53QSNM6huJLdCU7M29H
稼働 OS: Linux (WSL)
上流文書: `../../../../general/docs/prompts/past/261113-source-comment-bl029-backlog.md` の BL-004、`../../../../general/docs/prompts/past/261109-source-comment-remaining-backlog.md` の BL-025。いずれも削除されている場合があります。
下流文書: 継続案件 261123-porter-remaining-backlog (2026-11-23 開始、claude code / セッション ID: session_01KJc53QSNM6huJLdCU7M29H): `261123-porter-remaining-backlog-overview.md` (全体管理文書)、`261123-porter-remaining-backlog-discussion.md` (検討用 Markdown)、`261123-porter-remaining-backlog-lightadr.md` (簡易 ADR)、`261123-porter-remaining-backlog-design.md` (最終仕様)、`261123-porter-remaining-backlog-backlog.md` (残課題一覧)。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。「整理状態」が「随時追記中」であれば、案件の作業中です。気付いた事項を追記し、第 3 ゲートの前に文書を再構築して整理します。上流文書が存在する場合は、各課題の背景と見送りの判断を上流文書で確認します。存在しない場合は、この文書を単体の残課題一覧として扱います。このファイルの残課題を参照して新たな成果物の作成または更新を始めるときは、元案件の再開とは別の作業として扱います。その開始には agree-before-implement を原則として使い、新しい検討用 Markdown から開始してください。継続作業によってこのファイルの残課題を対策する場合は、このファイルや各項目の記載を削除することなく、各項目の状態および経緯と対処を更新することを原則とします。また、フェーズ分けした対策で対処ができていない項目がある場合は、当該継続作業の backlog にも必ず転記し、最新作業の backlog を見れば残件が把握できるようにしてください。最終更新エージェントのセッション情報や稼働 OS・最終更新日時が参照可能であれば再開時のコンテキストや環境差分の把握に利用し、別エージェントや別環境・別 OS からの引き継ぎ、担当変更によりセッションが見つからない場合も異常とせず、文書の記載から継続してください。
-->

# TCP 双方向 N:1 通信の残課題一覧

ファイル名: `261115-porter-tcp-bidir-n1-backlog.md`

## 案件の識別

案件名: `porter-tcp-bidir-n1-20261115`  
作成日: `2026-11-15`  
整理状態: 整理済み

この文書は、ワークスペース横断の残課題一覧であった `app/general/docs/prompts/past/261113-source-comment-bl029-backlog.md` の BL-004 (元は `261108-source-comment-cleanup-backlog.md` から引き継いだ BL-025) を、`app/porter` 固有の残課題として移管・記録したものです。  
後続の継続作業でこの文書の残課題を対策する場合は、このファイルや記載を削除せず、経緯と対処を更新します。

## 残課題一覧

| 課題 ID | 題名 | 区分 | 優先度 / 影響度 | 状態 |
| --- | --- | --- | --- | --- |
| BL-001 | TCP 双方向 N:1 通信の実施要否判断と設計・実装 | 過去 backlog からの転記 (BL-004 / BL-025) | 低 | 次フェーズへ転記 (継続案件 261123-porter-remaining-backlog では扱わない。`261123-porter-remaining-backlog-backlog.md` の BL-001 へ転記) |

Table: TCP 双方向 N:1 通信の残課題

## BL-001 TCP 双方向 N:1 通信の実施要否判断と設計・実装

### 概要と問題点

- **現状の事象**: 公開ヘッダー `app/porter/prod/include/porter/porter_type.h` に将来予定としてコメント アウトされていた `POTR_TYPE_TCP_BIDIR_N1 = 11` は削除済みですが、`app/porter/docs/architecture.md` の `potr_type` 抜粋には将来予定のコメントが残っており、過去の設計記録 `app/porter/docs/prompts/past/tcp-n1-design.md` も保管されています。この通信種別を実施するかどうかの採否判断と、実施する場合の現行アーキテクチャー (`app/cplat` のネットワーク抽象化層や証明書・事前共有鍵認証を含む) に合わせた設計・実装が未着手のまま残っています。
- **本来あるべき姿**: 利用目的と要求元が明確になった段階で実施要否を判断します。実施する場合は、複数 SENDER から 1 台の RECEIVER への同時接続管理、RECEIVER から特定 SENDER への送信先 (`peer_id`) の識別、切断時の資源解放と再接続の扱い、公開 API と `potr_type` 列挙値の互換性、および機能仕様 (`app/porter/docs/functional-spec/`) を定義したうえで実装・検証します。実施しない場合は、不要との判断を記録して本課題を完了とし、`app/porter/docs/architecture.md` に残る将来予定コメントもあわせて整理します。

### 経緯と見送り理由

- **摘出契機**: `source-comment-cleanup-20261108` において `app/porter/prod/include/porter/porter_type.h` のコメント アウトされた列挙子 `POTR_TYPE_TCP_BIDIR_N1 = 11` を削除した際、将来予定の採否判断を失わないよう BL-025 として残課題へ移管しました。その後、`source-comment-remaining-20261109` で実施要否の判断と対処をともに先送りする合意となり、`261113-source-comment-bl029-backlog.md` の BL-004 へ転記されました。
- **今回の見送り理由**: 機能を追加しないという恒久的な決定ではなく、利用目的と要求が定まるまで判断と実装を保留しているためです。
- **継続作業での経緯と対処**: 2026-11-15、`app/general/docs/prompts/past/261113-source-comment-bl029-backlog.md` の BL-004 から `app/porter` 固有の残課題として本ファイルへ移管しました。移管元の記載は削除せず、本ファイルへの移管完了として状態を更新しています。 2026-11-23、継続案件 `261123-porter-remaining-backlog` (全体管理文書 `261123-porter-remaining-backlog-overview.md`) で、扱いの検討を開始しました。同日、ユーザーの判断 (同案件の簡易 ADR の LADR-002、LADR-005) で、同案件では扱わないことになり、`261123-porter-remaining-backlog-backlog.md` の BL-001 へ転記しました。

### 影響範囲とリスク

- **影響対象**: `app/porter/prod/include/porter/porter_type.h` の公開通信種別 (`potr_type`)、TCP の接続・送受信・ヘルス チェック処理、`app/porter/docs/functional-spec/`、`app/porter/docs/api.md`、`app/porter/docs/architecture.md`。
- **放置した場合のリスク**: 機能が存在しないこと自体による既存機能への不具合はありません。ただし、利用目的や設計が固まらないまま列挙子だけを復活させると、利用者が実装済みと誤認するおそれがあります。

### 推奨される対応策

- **恒久対応案**: 利用目的、要求元、接続関係、送信先の識別方法、切断時の扱い、公開 API と列挙値の互換性、認証方式 (PSK / 証明書) との組み合わせ、および Linux と Windows 双方での検証方法を `agree-before-implement` スキルによる検討用 Markdown で合意してから設計・実装します。なお、過去の設計記録 `../past/tcp-n1-design.md` は通信のプラットフォーム抽象化層が `app/cplat` の `net` カテゴリへ移行する前の履歴文書であるため、そのまま適用せず現行の `cplat` API と `porter` 内部構造に合わせて再設計します。
- **参考情報**: `../../../../general/docs/prompts/past/261113-source-comment-bl029-backlog.md` の BL-004、`../../../../general/docs/prompts/past/261109-source-comment-remaining-backlog.md` の BL-025、`../past/tcp-n1-design.md`、`../../architecture.md`。
