<!--
生成元: agree-before-implement スキル
文書種別: 残課題一覧 (backlog)
最終更新エージェント: Codex / セッション ID: 特定できず
上流文書: `261114-porter-windows-verification-clock-unify-discussion.md`、`261114-porter-windows-verification-clock-unify-design.md`、`261109-porter-path-priority-timestamp-auth-backlog.md`
再開手順: agree-before-implement スキルを読み、各課題の根拠と上流の判断を確認してください。技術仕様を選択する場合は新しい検討用 Markdown で合意を得てください。
-->

# porter Windows 検証・時計一本化の残課題一覧

ファイル名: `261114-porter-windows-verification-clock-unify-backlog.md`

## 案件の識別

案件名: porter Windows 検証と cplat 単調増加時計の一本化  
作成日: 2026-11-15  
整理状態: 整理済み

この一覧は、第 3 ゲート前に今回未解決の課題を整理したものです。BL-003 と Windows 検証は完了しています。残課題を移管したことは、未決の技術仕様を選択したことを意味しません。

## 残課題一覧

| 課題 ID | 題名 | 区分 | 優先度 / 影響度 | 状態 |
| --- | --- | --- | --- | --- |
| BL-001 | マルチキャスト受信 NIC と許可送信元の設定契約を解決する | 作業中摘出 | 高 / 高 | 未着手 |
| BL-002 | Windows 仮想環境で時計取得コストを計測する | 過去 backlog からの転記 | 低 / 低 | 未着手 |
| BL-003 | certificateTest make 構成時の unexpected EOF stderr の原因を特定する | 作業中摘出 | 中 / 低 | 未着手 |

## BL-001 マルチキャスト受信 NIC と許可送信元の設定契約を解決する

### 概要と問題点

- **現状の事象**: PSK と証明書の Windows/WSL 相互接続で、マルチキャスト 2 ケースが受信に至らず、全体では 12 組み合わせ中 10 件成功です。raw UDP 受信プログラムでは同じグループ・ポートへの Windows の PING と DATA 到着を確認しました。
- **契約上の矛盾**: `app/porter/docs/config.md` はマルチキャスト受信側の `src_addr1` をグループ参加 NIC と説明する一方、同文書の送信元フィルター節は同値で受信元 IP を照合すると説明します。機能仕様 `app/porter/docs/functional-spec/transport.md` の `POTR-TRANSPORT-QUAL-001` は受信側で送信元を限定できることを要求し、マルチキャストを除外していません。
- **実装と再現条件**: `api_open_paths_multicast()` は `src_addr1` を NIC 指定に使い、`thread_recv_check_src_addr()` は同値を送信元 IP と比較します。WSL 側受信 NIC `172.28.209.184` と Windows 送信元 `172.28.208.1` が異なり、パケットがフィルターで破棄される経路と整合します。
- **本来あるべき姿**: NIC 選択と送信元許可の意味を矛盾なく定義し、機能仕様の送信元制限要件を維持するか、例外を明示的に合意したうえで、PSK/証明書双方で相互接続できること。

### 経緯と見送り理由

- **摘出契機**: BL-004 の Windows/WSL 相互接続試験。
- **今回の見送り理由**: ユーザーは「backlog に回す」と判断しました。今回の設定、実装、機能仕様の変更は行いません。
- **未採用案**: discussion Q-011 の A (NIC 指定と許可送信元 IP を別設定にする) は推奨として提示しましたが、採用されていません。B (マルチキャスト受信側で送信元照合をしない) も未採用です。いずれも次回の合意なしに実装しません。
- **影響範囲**: `app/porter/docs/config.md`、`app/porter/docs/functional-spec/transport.md` の `POTR-TRANSPORT-QUAL-001`、`api_open_paths_multicast()`、`thread_recv_check_src_addr()`、PSK/証明書の相互接続。
- **放置した場合のリスク**: 異なる NIC を通るマルチキャスト送信元が受信フィルターで破棄され、対象環境でマルチキャスト通信が成立しない可能性があります。

### 推奨される対応策

- **恒久対応案**: まず新しい discussion で設定契約を合意してください。NIC と許可送信元を分離する案は未採用の推奨案であり、設定名、API 表現、既存設定との互換方法を含む設計が必要です。送信元制限要件を維持し、PSK/証明書それぞれの Windows/WSL 試験を再実施します。
- **参考情報**: 今回の discussion Q-011、`app/porter/docs/config.md`、`app/porter/docs/functional-spec/transport.md` の `POTR-TRANSPORT-QUAL-001`。

## BL-002 Windows 仮想環境で時計取得コストを計測する

### 概要と問題点

- **現状の事象**: BL-003 の `GetTickCount64()` と QPF/QPC の計測は Windows 実機で行いました。Windows 11 Pro for Workstations build 26300、MSVC x64 19.50.35724、HP Z8 G4 / Intel Xeon Gold 6254 での計測時も `HypervisorPresent=True` で、仮想化影響が残る可能性があります。専用の Windows VM での比較は未実施です。
- **本来あるべき姿**: 仮想環境で両経路の取得コストを測定し、環境差を記録すること。

### 経緯と見送り理由

- **摘出契機**: Q-006 でユーザーが A「この PC (実機) だけで計測し、仮想環境での計測は本案件 backlog に残す」を選択しました。
- **今回の見送り理由**: 明示的に今回の範囲外として backlog に残す合意です。今回の性能判断や実装を遡って変更するものではありません。
- **影響範囲**: `app/cplat` の単調増加時計 API と将来の性能判断。
- **放置した場合のリスク**: 仮想化基盤による取得コストの差を把握できず、実機計測のみでは性能の代表性に限界が残ります。

### 推奨される対応策

- **恒久対応案**: 利用可能な Windows 仮想環境が用意できた時点で、実機計測と同等の呼び出し経路、ウォーム アップ、複数試行、実行回数で再計測し、値と環境差を報告します。性能しきい値は未決であり、この課題では設定しません。
- **参考情報**: discussion Q-006、`261114-porter-windows-verification-clock-unify-design.md` の「BL-003 の計測と判断」。

## BL-003 certificateTest make 構成時の unexpected EOF stderr の原因を特定する

### 概要と問題点

- **現状の事象**: Windows `app/porter make test` の `certificateTest` 依存スキップ表示前に `/usr/bin/sh: -c: line 1: unexpected EOF while looking for matching '"'` が stderr に出ました。leaf summary は 31 件成功、テスト失敗はありませんでした。
- **再現**: `make -C app/porter/test/src/integration/certificateTest --eval='printenv:;@:' printenv` で再現しました。`resolve_app_deps.sh --signature` の単独実行では再現しませんでした。make 構成時のどの関数が原因かは未特定です。
- **本来あるべき姿**: 依存変更のないテストをスキップする経路で、意図しない shell 構文エラーを出さないこと。

### 経緯と見送り理由

- **摘出契機**: BL-004 の Windows porter test。
- **今回の見送り理由**: テスト結果は成功し、stderr の原因が未確定です。根拠のない make framework 修正を避け、切り分けを別課題としました。
- **影響範囲**: `app/porter/test/src/integration/certificateTest` の make 構成と依存スキップ判定。
- **放置した場合のリスク**: 構成時の stderr が残り、依存判定・ビルドの異常を見逃す可能性があります。

### 推奨される対応策

- **恒久対応案**: 再現コマンドを起点に make の parse-time 展開と shell 呼び出しを特定し、原因を確認したうえで最小修正と対象 make の再検証を行います。
- **参考情報**: discussion の BL-004 進行状況、上記の再現コマンド。

## 今回は新規課題にしない確認事項

- `app/porter/test/src/integration/certificateTest/potr_health_thread.c.warn` にあった C4013/C4310 は実行前から存在した stale な生成 `.warn` です。今回のビルドで新たに発生した警告ではなく、新規課題にしません。
- `app/cplat/doxy_internal.warn` の Mermaid 2 警告と functional-spec checker の既存 9 件は、今回変更起因ではない一般既存問題として分離します。
