<!--
生成元: agree-before-implement スキル
文書種別: 全体管理文書 (overview)
最終更新日時: 2026-11-27 12:23:27 JST
最終更新エージェント: claude code (主セッション session_01KJc53RTnwmpzd7VMLf1qR9)
稼働 OS: Linux (WSL)
上流文書: 対策元の残課題一覧は、`261123-porter-remaining-backlog-backlog.md` です。削除されている場合があります。
下流文書: `261123-porter-remaining-backlog-phase2-discussion.md` (検討用 Markdown)、`261123-porter-remaining-backlog-phase2-lightadr.md` (簡易 ADR)、`261123-porter-remaining-backlog-phase2-design.md` (最終仕様)、`261123-porter-remaining-backlog-phase2-backlog.md` (残課題一覧)。未作成の場合や、削除されている場合があります。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。この文書と、ユーザーが編集した最新の検討用 Markdown を読み直し、作業 WBS の `← **CURRENT**` の項目から再開します。最終更新エージェントのセッション情報や稼働 OS・最終更新日時が参照可能であれば再開時のコンテキストや環境差分の把握に利用し、別エージェントや別環境・別 OS からの引き継ぎ、担当変更によりセッションが見つからない場合も異常とせず、文書の記載から継続してください。`← **CURRENT**` の項目は処理が中止された項目である場合があり、どこまで進んでいたかは文書だけでは分かりません。対象成果物の内容を確認して実施済みの範囲を特定してから、作業を再開してください。`← **CURRENT**` がなく、作業 WBS がすべて完了 (✅) であれば、この案件の合意フローは完了しています。
-->

# porter の残課題の対策 (第 2 期) 全体管理

ファイル名: `261123-porter-remaining-backlog-phase2-overview.md`

この文書は、案件の識別、作業 WBS、対象範囲、確認状況、経緯を管理します。質問と回答の本文は検討用 Markdown (`261123-porter-remaining-backlog-phase2-discussion.md`) に、決定事項は簡易 ADR (`261123-porter-remaining-backlog-phase2-lightadr.md`) に記載し、この文書には重複して記載しません。

## 案件の識別

検討 ID: 261123-porter-remaining-backlog-phase2

検討用 Markdown の版: 2

状態: 成果物の承認の受付中 (第 3 ゲート)

## 作業 WBS と進捗管理

この案件の作業 WBS です。項目 4 の作業対象は、最終仕様で確定したファイルごとの項目です。  
決定事項は、Q を解決済みにした時点、またはユーザーが決定を明示した時点で `261123-porter-remaining-backlog-phase2-lightadr.md` へ反映します。見送りの決定や範囲外の気付きは、どの段階でも生じた時点で `261123-porter-remaining-backlog-phase2-backlog.md` に追記します。対策元の残課題一覧 (`261123-porter-remaining-backlog-backlog.md`) の状態と経緯も、進行の各段階で更新します。

凡例: ⏸️ 未着手、🔄 実施中、🚫 ブロック中、⚠️ 要注意、✅ 完了。現在取り組んでいる項目の末尾に `← **CURRENT**` を付けます。

- ✅ 1. 検討用 Markdown のための情報収集と質問の実施
    - ✅ 1.1. 前提条件と要求事項の調査 (対策元の残課題一覧の 10 項目について、Linux (WSL) でできる範囲の確認。codex (gpt-6.2-sol、effort low) の読み取りの調査と、主エージェントによる確認。対策元の残課題一覧の状態を「検討中」へ更新)
    - ✅ 1.2. 課題・論点の抽出と確認項目 (Q-001 から Q-007) の作成
    - ✅ 1.3. 版 1 の回答の受付と記録 ([Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲) から [Q-007](261123-porter-remaining-backlog-phase2-discussion.md#q-007-サニタイザー-bl-009-の扱い)。2026-11-25 20:34:47 に保存)
    - ✅ 1.4. 回答の検証、簡易 ADR への反映、対策元の残課題一覧への方針と見送りの反映、この案件の残課題一覧の作成、WBS の見直し (簡易 ADR の LADR-001 から LADR-007 を作成。対策元の BL-001 から BL-008 を「次フェーズへ転記」、BL-009 を「完了」に更新。この案件の残課題一覧に BL-001 から BL-008 を転記)
    - ✅ 1.5. 版 2 の確認項目 (Q-008 から Q-014) の作成 (Windows の実装の担当、Registered I/O の実現性の確認の時期と成立しない場合の扱い、cplat の API の形、Registered I/O を使えるソケットの範囲、Windows の性能の基準、N 対 1 のまとめる単位)
    - ✅ 1.6. 版 2 の回答の受付と記録、回答の検証 (2026-11-25 20:59:35 に保存。Q-008 から Q-014 はすべて推奨の A。解決済みにし、簡易 ADR の LADR-002、LADR-003、LADR-004 を更新し、LADR-008、LADR-009 を追加)
    - ✅ 1.7. Windows のセッションでの Registered I/O の試作 (2026-11-25 に結果を受領。併用は不適合で、時間の比較は実施できず。結果は「反映と検証の記録」に記録。[Q-010](261123-porter-remaining-backlog-phase2-discussion.md#q-010-registered-io-が成立しない場合の扱い) の A により、Windows は `sendto` の繰り返しに確定)
- ✅ 2. 前提合意と残懸念の解消 (第 1 ゲート: 最終仕様作成への着手)
    - ✅ 2.1. 全論点に対する未解決事項および質問の解消確認 (未回答と未解決の Q はなし。Windows の試作の結果で Q-012 の前提が不成立となった扱いは、Q-010 の A と自走の指示で決定済み。満たせない受入と代案は残課題一覧の BL-011 から BL-013 へ送付)
    - ✅ 2.2. 方針・前提条件の合意確定 (簡易 ADR の決定事項と、解決済みの Q の回答またはユーザーの指示との一致の確認) (LADR-001 から LADR-010 を照合し、一致を確認。第 1 ゲートを通過)
- ✅ 3. 最終決定事項 (最終仕様) の作成と承認の受付
    - ✅ 3.1. `261123-porter-remaining-backlog-phase2-design.md` の作成 (対策元の残課題一覧へ確定した方針を反映) (作成済み。対策元の残課題一覧の BL-010 を「対応中」へ更新)
    - ✅ 3.2. 最終仕様における受入条件と整合性の確認 (受入条件 1 から 8 と、簡易 ADR の LADR-001 から LADR-010 の一致を確認)
    - ✅ 3.3. 対話セッションにおける最終仕様の承認の受付 (第 2 ゲート) (ユーザーの自走の指示 (2026-11-25 21:48 頃) により承認済みとして扱う。簡易 ADR の LADR-010)
- ✅ 4. 承認された最終仕様に従った作業の実施
    - ✅ 4.1. `framework/testfw` の `sendmmsg()` の mock の追加 (`include/sys/mock_socket.h`、`libsrc/mock_libc/mock_sys_socket.cc`) とビルド
    - ✅ 4.2. `app/cplat` の `cplat_socket_watch_sendto_multi()` の追加 (`prod/include/cplat/net/socket.h`、`prod/libsrc/cplat/net/socket_watch_linux.c`、`socket_watch_windows.c` の Windows の初版)
    - ✅ 4.3. `app/cplat` の `mock_cplat`、`exportTest`、単体テスト (`test/src/libcplatTest/net/socketTest/socketTest.cc`) の追加と、Linux のビルドとテスト
    - ✅ 4.4. `app/cplat` の文書 (`docs/api-cheatsheet.md`、`docs/functional-spec/net.md`) の更新と `check_functional_spec.py`
    - ✅ 4.5. `app/porter` のまとめ送りの資源の名前の変更と UDP への確保 (`potr_send_thread.h`、`potr_context.h`、`api/potr_service_open.c`、`api/api_context_cleanup.c` と参照箇所)
    - ✅ 4.6. `app/porter` の `potr_internal_udp_sendto_multi()` の追加 (`infra/potr_socket_io.c`、`.h`) と mock (`mock_porter`)
    - ✅ 4.7. `app/porter` の送信スレッドの UDP のまとめ送り (`thread/potr_send_thread.c` の 1 対 1 と N 対 1) と、`potr_internal_flow_control_peer_state()` の送出できる数の出力 (`potr_flow_control.h`、`potr_flow_control.c`)
    - ✅ 4.8. `app/porter` の単体テスト (`potrSocketIoTest`、`potrSendThreadTest`、`potrFlowControlTest`) の追加と変更、Linux のビルドとテスト
    - ✅ 4.9. 正本ドキュメントおよびコード コメントへの永続的知見の反映 (`potr_send_thread.c` の先頭のコメント、`potr_send_thread.h`、`potr_socket_io.h`、`docs/architecture.md`、`docs/protocol.md`、cplat の Windows と Linux の実装のコメント)
    - ✅ 4.10. Linux の `make test` (`app/cplat`、`app/porter`)、`.warn` の確認、`text_style_jp.py`
    - ✅ 4.11. Windows のセッションでのビルドとテストと修正 (主エージェントが差分を仲介。依頼 16 と 17 で cplat と porter のビルド、テスト、`mock` の `/INCLUDE` リンクが成功。MSVC の警告と内容のある `.warn` はなく、MSVC 向けの修正は不要)
    - ✅ 4.12. Linux と Windows の性能の測定と記録 (受入条件 1 から 4。Linux と Windows の両方を測定済み。受入条件 1 から 3 は適合、受入条件 4 (Windows) は主エージェント判断で条件付きで適合)
    - ✅ 4.13. サブモジュールの順の main へのコミットと push、親リポジトリの参照の更新、CI の確認 (Git の操作は主エージェント。testfw `75cd774`、cplat `8252c026`、porter `063d8cc`、親リポジトリ `dbc28819` を main へ push 済み。CI `45888369745` は全ジョブが成功 (ビルドとテストは Linux の ol8、ol9、ol10 と Windows で成功。警告は struct-meta の生成パーサーの C4127 だけで、この案件の変更によるものではなく、別の修正 (親リポジトリの 9376a220) で対処中))
    - ✅ 4.14. 受入条件への適合確認と、対策元の残課題一覧の BL-010 の更新 (受入条件 1 から 3、5 から 8 は適合。受入条件 4 は主エージェントの判断で条件付きで適合 (BL-015)。対策元の BL-010 は「対策済み」へ更新)
    - ✅ 4.15. 簡易 ADR の最終確認 (LADR-001 から LADR-010 と実装結果の一致を確認。実装中に決めた事項を経緯へ追記し、状態の記載を更新済み)
    - ✅ 4.16. 残課題一覧 (`261123-porter-remaining-backlog-phase2-backlog.md`) の再構築と整理 (この案件の残課題一覧は BL-014 から BL-016 を含めて「整理済み」。対策元の BL-010 は「対策済み」で確定)
- 🔄 5. 成果物の承認の受付 (第 3 ゲート)
    - 🔄 5.1. 成果物・検証結果・残課題の提示 ← **CURRENT**
    - ⏸️ 5.2. 対話セッションにおける成果物の承認の受付

## 対象と参照資料

対象成果物: BL-010 の対策 ([Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲) の回答)。`framework/testfw`、`app/cplat`、`app/porter` の 3 つのサブモジュールを変更します。具体的なファイルは、最終仕様 `261123-porter-remaining-backlog-phase2-design.md` にあります。

今回の対象範囲: 対策元の残課題一覧の BL-010 だけ。BL-001 から BL-008 は、この案件の残課題一覧 (`261123-porter-remaining-backlog-phase2-backlog.md`) へ転記しました。BL-009 は、導入しないと決めて閉じました。

書き込み可能な検討文書: この文書、検討用 Markdown (`261123-porter-remaining-backlog-phase2-discussion.md`)、簡易 ADR (`261123-porter-remaining-backlog-phase2-lightadr.md`)、残課題一覧 (`261123-porter-remaining-backlog-phase2-backlog.md`)、対策元の残課題一覧 (`261123-porter-remaining-backlog-backlog.md`)

参照資料と版:

- 対策元の残課題一覧 `261123-porter-remaining-backlog-backlog.md` (2026-11-23 12:45 の版)
- 前の案件の全体管理文書 `261123-porter-remaining-backlog-overview.md` の「反映と検証の記録」(測定の値)
- `app/porter` main `99d4da6`、`app/cplat` main `988b8720`、`framework/makefw` main `d8177db`、親リポジトリ main `6e5010eb`
- codex (gpt-6.2-sol、effort low、読み取り専用) の検討の結果 (セッションの一時領域。リポジトリには入れていません)
- 版 2 の調査の時点のリポジトリ: 親リポジトリ main `9d37dce0`、`app/porter` `f643d3a`、`app/cplat` `45217b7d`、`framework/testfw` `1f79ca1`
- Microsoft Learn の Registered I/O (`RIOSendEx`、`RIOCreateRequestQueue`、`RIOCreateCompletionQueue`)、`TransmitPackets`、`IPPROTO_UDP` のソケット オプションの頁 (2026-11-25 に参照。`WSASendMsg` の頁は取得できず)
- Windows 側の別の作業ツリー `/mnt/d/Users/tetsuo/Local/repos/c-modernization-kit` (HEAD `9d37dce0`。主エージェントがパッチの受け渡しを仲介)

ユーザーの指示 (2026-11-23): 「`261123-porter-remaining-backlog-backlog.md` の次の対処計画を進行したい。」  
計画の検討には、前の案件の指示 (計画立案の検討に codex の GPT-6.2-Sol low を使う) を、同じ種類の作業として引き続き適用しました。作業の実施の段階のサブエージェントの使い方は、最終仕様の承認の時点で確認します。

ユーザーの指示 (2026-11-25 21:48 頃、主エージェント経由。ほかの案件と同じ文言): 「ユーザーの確認なく継続的に自走すること」「決定した最終仕様が実現できない場合は、ユーザーに問い合わせることなく backlog に送る」「各サブモジュール (案件) 完了時に main にプッシュし CI を起動させ…」「main へのコミットを事前許可」。第 2 ゲートは、この指示により承認済みとして扱います (簡易 ADR の LADR-010)。

ユーザーの指示 (2026-11-25、主エージェント経由): 「どれも書きました。最終仕様策定は Opus で行ってください。その際、Sonnet の掘り下げが浅いものは、書き直したり再質問をユーザーに行ってもよいです。」

## 観点別の確認

Table: 観点別の確認

| 観点 | 状態 | 根拠・関連 Q |
| --- | --- | --- |
| 目的・利用者 | 確認済み | [Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲) (BL-010 の対策) |
| 範囲・境界 | 確認済み | [Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲)、[Q-003](261123-porter-remaining-backlog-phase2-discussion.md#q-003-bl-010-を適用する通信種別と経路の範囲)、[Q-007](261123-porter-remaining-backlog-phase2-discussion.md#q-007-サニタイザー-bl-009-の扱い)、[Q-014](261123-porter-remaining-backlog-phase2-discussion.md#q-014-n-対-1-で一括の送信にまとめる単位) |
| 入力・根拠 | 確認済み | 対策元の残課題一覧、前の案件の測定の記録、現行のソース、Microsoft Learn の頁、Windows の Registered I/O の試作の結果 (「反映と検証の記録」) |
| 整合性・依存関係 | 確認済み | [Q-011](261123-porter-remaining-backlog-phase2-discussion.md#q-011-cplat-に追加する一括の送信の-api-の形)、[Q-012](261123-porter-remaining-backlog-phase2-discussion.md#q-012-windows-で-registered-io-を使えるようにするソケットの範囲) (Q-012 は試作で前提が不成立のため適用しない。簡易 ADR の LADR-004) |
| 実現性・例外 | 確認済み | [Q-006](261123-porter-remaining-backlog-phase2-discussion.md#q-006-一括の送信で一部だけ送れた場合の残りの扱い)、[Q-008](261123-porter-remaining-backlog-phase2-discussion.md#q-008-windows-の一括の送信の実装の担当)、[Q-010](261123-porter-remaining-backlog-phase2-discussion.md#q-010-registered-io-が成立しない場合の扱い) は解決済み。Registered I/O は、[Q-009](261123-porter-remaining-backlog-phase2-discussion.md#q-009-registered-io-の実現性の確認の時期) の試作で不成立と確認し、Q-010 の A で Windows は `sendto` の繰り返しに確定 |
| 受入条件・検証 | 確認済み | Linux は [Q-005](261123-porter-remaining-backlog-phase2-discussion.md#q-005-bl-010-の性能の受入の基準)、Windows は [Q-013](261123-porter-remaining-backlog-phase2-discussion.md#q-013-windows-の性能の受入の基準)。Windows の「上がる」の基準と、暗号化と複数経路の性能は、自走の指示により残課題一覧の BL-012、BL-013 へ送付 |

## 質問一覧

各 Q の本文は検討用 Markdown にあります。

Table: 質問一覧

| Q-ID | 論点 | 状態 | 依存・関連 | 反映先の決定 |
| --- | --- | --- | --- | --- |
| [Q-001](261123-porter-remaining-backlog-phase2-discussion.md#q-001-この案件で対策する残課題の範囲) | この案件で対策する残課題の範囲 | 解決済み | なし | [LADR-001](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-001-対策する残課題の範囲) |
| [Q-002](261123-porter-remaining-backlog-phase2-discussion.md#q-002-windows-での確認の方法) | Windows での確認の方法 | 解決済み | Q-001、Q-008 | [LADR-002](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-002-windows-での確認の方法と実装の担当) |
| [Q-003](261123-porter-remaining-backlog-phase2-discussion.md#q-003-bl-010-を適用する通信種別と経路の範囲) | BL-010 を適用する通信種別と経路の範囲 | 解決済み | Q-001、Q-005、Q-014 | [LADR-003](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-003-一括の送信を適用する範囲) |
| [Q-004](261123-porter-remaining-backlog-phase2-discussion.md#q-004-bl-010-の-windows-での実装の方式) | BL-010 の Windows での実装の方式 | 解決済み | Q-001、Q-002、Q-008 から Q-013 | [LADR-004](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-004-windows-の一括の送信の方式) |
| [Q-005](261123-porter-remaining-backlog-phase2-discussion.md#q-005-bl-010-の性能の受入の基準) | BL-010 の性能の受入の基準 | 解決済み | Q-003、Q-013 | [LADR-005](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-005-linux-の性能の受入の基準) |
| [Q-006](261123-porter-remaining-backlog-phase2-discussion.md#q-006-一括の送信で一部だけ送れた場合の残りの扱い) | 一括の送信で、一部だけ送れた場合の残りの扱い | 解決済み | Q-003、Q-011 | [LADR-006](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-006-一部だけ送れた場合の残りの扱い) |
| [Q-007](261123-porter-remaining-backlog-phase2-discussion.md#q-007-サニタイザー-bl-009-の扱い) | サニタイザー (BL-009) の扱い | 解決済み | なし | [LADR-007](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-007-サニタイザーを導入しない) |
| [Q-008](261123-porter-remaining-backlog-phase2-discussion.md#q-008-windows-の一括の送信の実装の担当) | Windows の一括の送信の実装の担当 | 解決済み | Q-002、Q-004、Q-009 | [LADR-002](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-002-windows-での確認の方法と実装の担当) |
| [Q-009](261123-porter-remaining-backlog-phase2-discussion.md#q-009-registered-io-の実現性の確認の時期) | Registered I/O の実現性の確認の時期 | 解決済み | Q-004、Q-008、Q-010、Q-011、Q-012 | [LADR-004](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-004-windows-の一括の送信の方式) |
| [Q-010](261123-porter-remaining-backlog-phase2-discussion.md#q-010-registered-io-が成立しない場合の扱い) | Registered I/O が成立しない場合の扱い | 解決済み | Q-004、Q-009、Q-013 | [LADR-004](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-004-windows-の一括の送信の方式) |
| [Q-011](261123-porter-remaining-backlog-phase2-discussion.md#q-011-cplat-に追加する一括の送信の-api-の形) | cplat に追加する一括の送信の API の形 | 解決済み | Q-004、Q-006、Q-009、Q-012 | [LADR-008](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-008-cplat-に追加する一括の送信の-api-の形) |
| [Q-012](261123-porter-remaining-backlog-phase2-discussion.md#q-012-windows-で-registered-io-を使えるようにするソケットの範囲) | Windows で Registered I/O を使えるようにするソケットの範囲 | 解決済み | Q-009、Q-010、Q-011 | [LADR-004](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-004-windows-の一括の送信の方式) |
| [Q-013](261123-porter-remaining-backlog-phase2-discussion.md#q-013-windows-の性能の受入の基準) | Windows の性能の受入の基準 | 解決済み | Q-005、Q-010 | [LADR-009](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-009-windows-の性能の受入の基準) |
| [Q-014](261123-porter-remaining-backlog-phase2-discussion.md#q-014-n-対-1-で一括の送信にまとめる単位) | N 対 1 で一括の送信にまとめる単位 | 解決済み | Q-003 | [LADR-003](261123-porter-remaining-backlog-phase2-lightadr.md#ladr-003-一括の送信を適用する範囲) |

## 対象成果物と合意版

最終決定事項 (最終仕様) の設計書: `261123-porter-remaining-backlog-phase2-design.md`

検討用 Markdown の合意版 (第 1 ゲート): 版 2 (Q-001 から Q-014、2026-11-25 20:59:35 の保存) と、ユーザーの自走の指示 (2026-11-25 21:48 頃)

最終仕様の合意版 (第 2 ゲート): 版 1 (2026-11-25。ユーザーの自走の指示により承認済みとして扱う)

更新予定の成果物: 最終仕様の「変更するサブモジュールと順序」から「文書とコード コメント」の各表に記載したファイル (`framework/testfw`、`app/cplat`、`app/porter`)

受入条件: 最終仕様の「受入条件」の 1 から 8

## 経緯

Table: 経緯

| 日時 (JST) | 出来事 | 内容・参照先 |
| --- | --- | --- |
| 2026-11-23 12:36 | 案件の開始 | ユーザーの指示を受けて開始。対策元の残課題一覧の 10 項目を「検討中」へ更新 |
| 2026-11-23 12:55 | 版 1 の提示 | 全体管理文書と検討用 Markdown (Q-001 から Q-007) を作成 |
| 2026-11-25 20:34 | 版 1 の回答 | ユーザーが Q-001 から Q-007 に回答し、保存 |
| 2026-11-25 20:50 | 再開 | 主エージェントの指示で、opus のサブエージェントが回答の検証から再開 |
| 2026-11-25 20:56 | 版 1 の回答の反映 | Q-001 から Q-007 を解決済みにし、簡易 ADR (LADR-001 から LADR-007) を作成。対策元の残課題一覧を更新し、この案件の残課題一覧に BL-001 から BL-008 を転記 |
| 2026-11-25 20:57 | 版 2 の提示 | Q-003 (C)、Q-004 (B) の推奨外の選択と、Q-002 (D) の補足から生じた論点を、Q-008 から Q-014 として追記。作業 WBS に 1.5 から 1.7 を追加 |
| 2026-11-25 20:59 | 版 2 の回答 | ユーザーが Q-008 から Q-014 に回答し、保存 (すべて推奨の A) |
| 2026-11-25 21:05 | 版 2 の回答の反映と試作の準備 | Q-008 から Q-014 を解決済みにし、簡易 ADR の LADR-002、LADR-003、LADR-004 を更新、LADR-008、LADR-009 を追加。Windows の試作の依頼書とソースをセッションの一時領域に作成し、Windows 側の結果待ちで停止 |
| 2026-11-25 21:48 | 自走の指示 | ユーザーが、確認なしの自走、実現できない仕様の残課題一覧への送付、main へのコミットの事前許可を指示 (簡易 ADR の LADR-010) |
| 2026-11-25 22:19 | Windows の試作の結果 | 併用は不適合。Windows は `sendto` の繰り返しに確定。簡易 ADR の LADR-004、LADR-005、LADR-008、LADR-009 を更新し、残課題一覧に BL-011 から BL-013 を追記 |
| 2026-11-25 22:23 | 第 1 ゲートの通過 | 未解決の Q と懸念がないことを確認し、最終仕様の作成に着手 |
| 2026-11-25 22:27 | 最終仕様の作成と承認 | `261123-porter-remaining-backlog-phase2-design.md` (版 1) を作成。自走の指示により第 2 ゲートを承認済みとして扱い、作業 WBS の項目 4 をファイルごとの項目へ書き直した。実装は主エージェントの指示を待つ |
| 2026-11-26 16:45 | 作業の実施の開始 | 主エージェントの指示で、opus のサブエージェントが作業 WBS 4.1 から 4.4 (testfw の `sendmmsg()` の mock と、cplat の `cplat_socket_watch_sendto_multi()`) に着手。testfw `84b1b78`、cplat `84ea0ec6` (いずれも main) を基点とする |
| 2026-11-26 18:30 | 作業 WBS 4.1 から 4.4 の完了 | Linux のビルドと局所テストが成功。Windows の確認は 4.11 で行う。結果は「反映と検証の記録」に記録。4.5 で停止 (主エージェントの指示を待つ) |
| 2026-11-26 18:35 | 作業 WBS 4.5 の再開 | 主エージェントの指示で、opus のサブエージェントが作業 WBS 4.5 から 4.10 (porter の UDP のまとめ送り) に着手。testfw `75cd774`、cplat `8252c026`、porter `1438394` (いずれも main) を基点とする |
| 2026-11-26 22:20 | 作業 WBS 4.5 から 4.10 の完了 | Linux のビルド、局所テスト、`make test` (`app/porter`、`app/cplat`) が成功。結果は「反映と検証の記録」に記録。4.11 (Windows の検証) で停止 (主エージェントが仲介) |
| 2026-11-26 22:44 | 作業 WBS 4.12 の Linux の性能の測定と記録 | Linux (WSL) の性能の測定結果を集計し、「反映と検証の記録」へ記録。受入条件 1 から 3 はすべて適合 (`p1` 5,000 byte `blocking` の `target` ÷ `tcp` は 0.97)。受入条件 4 (Windows) は未測定・未判定 |
| 2026-11-27 02:09 | 作業 WBS 4.11 の完了と Linux 件数の訂正 | Windows の検証結果 (依頼 16 と 17) を「反映と検証の記録」へ記録し、4.11 を完了へ更新。Linux の `potrFlowControlTest` 件数の訂正 (162 件→163 件) を追記し、残課題一覧に BL-014 を起票 |
| 2026-11-27 02:33 | 作業 WBS 4.12 の Windows の性能の測定と記録、4.12 の完了 | Windows の性能の測定結果 (依頼 18) と受入条件 4 の判定 (主エージェント判断で条件付きで適合) を「反映と検証の記録」へ記録し、4.12 を完了、4.13 を実施中へ更新。残課題一覧に BL-015、BL-016 を起票 |
| 2026-11-27 05:07 | 作業 WBS 4.13 から 4.16 の記録 | 4.13 の main への push (testfw `75cd774`、cplat `8252c026`、porter `063d8cc`、親リポジトリ `dbc28819`) と CI `45888369745` の結果待ちを記録。4.14 の受入条件の照合 (1 から 3・5・6 は適合、4 は条件付きで適合、7・8 は CI の結果待ち) と対策元の BL-010 への要約追記、4.15 の簡易 ADR の最終確認 (完了)、4.16 の案件の残課題一覧の整理 (整理済み、対策元の最終状態は CI の結果待ち) を実施 |

## 反映と検証の記録

### 作業 WBS 1.7: Windows での Registered I/O の試作 (2026-11-25)

- 実施: Windows 側の Claude Code のセッション (主エージェントが仲介)。試作のソースと依頼書は、セッションの一時領域の `scratchpad/porter-rio-proto/` (`rio_proto.c`、`request.md`) です。結果は同じ場所の `windows-result/` (`rio_proto_out.txt`、`rio_proto.diff`、`probe.c`、`probe_out.txt`、`cl_out.txt`) にあります。リポジトリには入れていません。
- 環境: Windows 11 Pro for Workstations 10.0.26300、`cl` 19.50.35724、Xeon Gold 6254 × 2、電源プラン「バランス」。測定中のほかの重い処理はなし。Winsock のカタログは `mswsock.dll` だけで、LSP はなし。
- ソースの修正: `C3` の行のエラー番号の取り方だけ (呼び出しの後に `WSAGetLastError()` を取る)。
- 判定 1 (併用): 不適合。`WSA_FLAG_REGISTERED_IO` 付きの UDP ソケット (フラグ 0x101 と 0x100 のどちらも) では、`bind`、`getsockname`、`getsockopt(SO_TYPE)` は成功しますが、ブロッキングの `sendto` と `recvfrom`、`WSAEventSelect`、`select` は 10038 (`WSAENOTSOCK`)、`ioctlsocket(FIONBIO)` は 10045 (`WSAEOPNOTSUPP`) で失敗しました。RIO の関数表の取得、完了キュー、要求キューの作成より前でも同じです。フラグ 0x1 (`WSA_FLAG_OVERLAPPED` だけ) では、すべて成功しました。RIO の関数表の取得、完了キューと要求キューの作成、バッファーの登録は成功しました (`C6`)。
- 判定 2 (時間の比較): 判定 1 の不適合で、試作のプログラムが比較の前に止まるため、実施していません。
- 結論: porter の UDP のソケットは、受信の待機 (`WSAEventSelect`) と、受信と制御パケットの送信に通常の関数を使います。RIO のソケットではこれらが使えないため、Windows の一括の送信は `sendto` の繰り返しとします。Microsoft の資料で、この制約が文書化されているかは確認していません。
- Windows 側からの代案 (未実施): RIO の送信だけに使う専用のソケットを別に作る構成。送信元のポートが分かれるため、要件の判断が必要です。この案件の残課題一覧へ送ります。

### 作業 WBS 4.1 から 4.4: testfw の sendmmsg の mock と cplat の一括の送信 (2026-11-26、Linux (WSL))

- 基点: `framework/testfw` main `84b1b78`、`app/cplat` main `84ea0ec6`。Git の操作はしていません。
- 4.1 (testfw): `include/sys/mock_socket.h` と `libsrc/mock_libc/mock_sys_socket.cc` に、`sendto` と同じ形で `sendmmsg` の mock (置き換えのマクロ、`mock_sendmmsg()`、`delegate_real_sendmmsg()`、`MOCK_METHOD`、既定の委譲の `ON_CALL`) を追加しました。`struct mmsghdr` は `_GNU_SOURCE` を定義した場合だけ宣言されるため、ヘッダーで前方宣言し、`_GNU_SOURCE` のない C の翻訳単位でも警告なくコンパイルできることを、セッションの一時領域で確かめました。`make -C framework/testfw` は成功し、`.warn` はありません。
- 4.2 (cplat の実装): `socket.h` に `CPLAT_SOCKET_SENDTO_MULTI_MAX` (64)、`cplat_socket_datagram`、`cplat_socket_watch_sendto_multi()` と Doxygen コメントを追加し、`cplat_socket_watch_attach()` の登録中の規則の送信の関数に加えました。Linux は `sendmmsg()` (`EINTR` で呼び直し、先頭から `msg_len` が `len` と等しい数を返す)、Windows は既存の `send_once()` を先頭から順に呼ぶ初版です。Windows で Registered I/O を使わない理由と `see:` の URL を、`socket_watch_windows.c` の関数の前のコメントに残しました。
- 4.3 (cplat の mock とテスト): `mock_cplat` (`mock_cplat.h`、`mock_cplat.cc`、新規 `net/mock_cplat_socket_watch_sendto_multi.cc`)、`exportTest` の `CPLAT_EXPORT_TABLE_COMMON`、`socketTest.cc` の 8 個のテスト (Linux 7 個、Windows 5 個) と、登録の下位の呼び出しを期待するサブ手順 2 個 (`socketTest.expectMockWatchAttach`、`socketTest.expectMockWatchDetach`) を追加しました。
- 4.4 (cplat の文書): `docs/functional-spec/net.md` に `CPLAT-NET-FUNC-010` (UUID `148caece-1233-4a58-b153-212a3bb2ff7e`) と「送受信と待機」の振る舞いとプラットフォームの差を、`docs/api-cheatsheet.md` に `sendmmsg()` との対応と「登録中の送受信と接続」の行を追加しました。

Table: 作業 WBS 4.1 から 4.4 の Linux の検証結果

| 確認 | 結果 |
| --- | --- |
| `make -C framework/testfw` | 成功。`.warn` なし |
| `make -C app/cplat` (prod と test のビルド) | 成功。警告なし、`.warn` なし |
| `socketTest` | 成功 (69 件。追加した 7 件はすべて成功)。`.warn` なし |
| `exportTest` | 成功 (2 件) |
| `make doxy` (`app/cplat`) | 成功。Doxygen の警告なし |
| `check_functional_spec.py` | 成功 (要件 423 件、下流参照 42 件) |
| `text_style_jp.py --dry-run` (`net.md`、`api-cheatsheet.md`) | 指摘なし (`--in-place` も変更なし) |
| `text_style_jp.py --mode cpp --dry-run` (`socketTest.cc` の追加の行) | 指摘なし |
| `check_c_topdown.py` (`socket_watch_linux.c`、`socket_watch_windows.c`) | エラー 51 件から 53 件。増えたのは、新しい関数の `non-topdown-order` の 1 件ずつ |

- `check_c_topdown.py` の増分は、新しい関数を、ヘッダーの宣言順どおり `cplat_socket_watch_sendto()` の直後に置いたためです。2 つのファイルは、もともとファイル内補助関数が主たる関数より前にある構成で、トップ ダウンの並べ替えは、`261125-topdown-function-order` の最終仕様の第 2 段 (この案件の main への統合の後に `reorder_c_topdown.py` で `cplat` を書き換える) の対象です。並べ替えの道具は主たる関数の定義をファイル内補助関数の定義より前へ移すため、第 2 段の後は、新しい関数も `cplat_socket_watch_sendto()` の後に並ぶ見込みです (道具の実行は対象の外のため未確認)。先頭へ置けば件数は増えませんが、第 2 段の後も宣言順から外れたまま残るため、規範の最終形 (ヘッダーの宣言順) を優先しました。新しいファイル内補助関数は追加していません。
- カバレッジ: `socket_watch_linux.c` の新しい関数のうち、`sendmmsg()` が要求より大きい数を返す場合と、先頭のデータグラムが短い送信の場合の、2 つの防御の分岐 (`CPLAT_ERR_UNKNOWN`) は通っていません。最終仕様のテストの一覧の外です。
- 未確認: Windows のビルドとテスト (`socket_watch_windows.c` の初版、Windows のテスト 5 個) は、作業 WBS 4.11 で確かめます。

### 作業 WBS 4.5 から 4.10: porter の UDP のまとめ送り (2026-11-26、Linux (WSL))

- 基点: `framework/testfw` main `75cd774`、`app/cplat` main `8252c026`、`app/porter` main `1438394`。Git の操作はしていません。
- 4.5 (資源の名前): `POTR_TCP_BATCH_*` を `POTR_SEND_BATCH_*` へ、`tcp_batch_*` を `send_batch_*` へ改め、テストと mock の参照も改めました。`potr_service_open.c` の確保は、TCP に加えて、送信スレッドを起動する UDP の通信種別 (送信者、`unicast_bidir`、`unicast_bidir_n1`) でも行います。値と確保の数の計算は変えていません。
- 4.6 (ソケットの入出力): `potr_internal_udp_sendto_multi()` を追加しました。既存の `potr_internal_udp_sendto()` の送信と待機は、排他を持った状態で呼べる `udp_sendto_locked()` と、期限の判定と待機の `udp_wait_send_ready()` へ移し、両方から使います。最終仕様に書かれていない次の 2 点は、契約の表の趣旨に沿って決め、ヘッダーの Doxygen コメントに書きました。
    - 一括の送信の待機が期限の超過または解除で終わった場合は、残りのデータグラムすべてに同じ結果を格納し、1 個ずつの送信を試みません (データグラムごとに 100 [ms] 待つと、送信スレッドが最大 16 倍止まるため)。
    - 先頭のデータグラムが OS の送信失敗となった場合は、その結果を格納し、続くデータグラムを 1 個ずつ 1 回ずつ試みます (一部だけ送れた場合と同じ扱い)。
- 4.6 (mock): `mock_porter` に `potr_internal_udp_sendto_multi` を追加しました (既定は全要素 `POTR_OK`)。
- 4.7 (送信スレッド): `send_tcp_batch()` を `send_batch()` へ改め、UDP は新しい `udp_flush_packets()` で送ります。経路ごとの封緘と送信は `udp_send_path()` に集め、1 個の場合は `crypto_buf` と `potr_internal_udp_sendto()`、複数の場合は `send_batch_wire_buf` の区画と `potr_internal_udp_sendto_multi()` を使います。`flush_packed()` の UDP の処理と `flush_packed_peer()` は、それぞれ `udp_flush_packets()` と `udp_flush_packets_peer()` の 1 個の場合として呼ぶ形にし、1 個の送出の振る舞いは変えていません。N:1 は `send_packed_peer_mode()` で、同じピア宛てに続くエントリをまとめます (`pack_queued_entries()` にピアの絞り込みの引数を追加)。
- 4.7 (フロー制御): `potr_internal_flow_control_peer_state()` に `allowance` (NULL 可) を追加し、ファイル内の `peer_state_locked()` から受け取ります。ロックを取らない判定の記録 (`flow_peer_ready`) は残りの数を持たないため、`allowance` を求める呼び出しでは使わず、ロックの下で判定します。送信スレッドは、`send_batch_cap` が最小数以上の場合だけ `allowance` を求めます。送信 API (`potr_service_send.c`) は NULL を渡し、従来の速い経路を保ちます。
- 4.7 (原因の修正): `test/src/integration/porterCancelFailureTest` は cplat の `socket_watch_linux.c` を取り込んでコンパイルするため、`struct mmsghdr` が宣言されずにビルドが失敗しました。cplat と同じく `_GNU_SOURCE` を定義する設定を、このテストの `makepart.mk` に加えました (`see:` の URL 付き)。
- 4.8 (単体テスト): 最終仕様の一覧のテストをすべて追加しました。`potrSocketIoTest` に 5 個、`potrSendThreadTest` に UDP のまとめ送りの fixture (`potrSendThreadUdpBatchTest`) と 12 個、`potrFlowControlTest` に `peer_state_reports_allowance` を追加しました。既存のテストは、`potr_internal_flow_control_peer_state()` の引数の追加に合わせて改めたほかは、変更なしで成功しました (既存のテストは `send_batch_cap` が 0 のため、従来の 1 個の送出を通ります)。
- 4.9 (知見の反映): `potr_send_thread.c` の先頭のコメント、`potr_send_thread.h`、`potr_socket_io.h`、`potr_flow_control.h`、`potr_context.h` のコメント、`docs/architecture.md` (「送信スレッドのまとめ送り」の節の追加、送信スレッドの役割の表、フロー制御、UDP の送信の箇条)、`docs/protocol.md` (送出上限の判定の箇条) を更新しました。cplat の Windows と Linux の実装のコメントは、4.2 で反映済みであることを確認しました。最終仕様に従い、`docs/protocol.md` の「パッキングの動作」の節 (TCP のまとめ送りの説明) は変えていません。

Table: 作業 WBS 4.5 から 4.10 の Linux の検証結果

| 確認 | 結果 |
| --- | --- |
| `make -C app/porter` (prod と test のビルド) | 成功。`.warn` なし |
| `potrSocketIoTest` | 成功 (16 件。追加した 5 件はすべて成功) |
| `potrSendThreadTest` | 成功 (49 件。追加した 12 件はすべて成功) |
| `potrFlowControlTest` | 成功 (162 件。追加した 1 件は成功) |
| `make -C app/porter test` | 成功 (57 ディレクトリ、985 件、失敗 0)。所要 約 41 分 |
| `make -C app/cplat test` | 成功 (99 ディレクトリ、2,243 件、失敗 0)。所要 約 136 分 |
| `.warn` (`framework/testfw`、`app/cplat`、`app/porter`) | 内容のあるファイルなし |
| `make doxy` (`app/porter`) | 成功。Doxygen の警告なし |
| `text_style_jp.py --dry-run` (`architecture.md`、`protocol.md`) | 指摘なし (`--in-place` も変更なし) |
| `text_style_jp.py --mode c` / `--mode cpp --dry-run` (変更した C とテストの追加の行) | 追加した行に指摘なし。引数を足しただけの既存の行に、もとからの指摘が 9 件 (`potrFlowControlTest.cc` 5 件、`potr_service_send_test.cc` 4 件) |
| `check_c_topdown.py` | `potr_send_thread.c` はエラー 20 件から 18 件 (新しいファイル内補助関数は前方宣言を置き、`flush_packed_peer()` を削除し、`pack_queued_entries()` に前方宣言を置いたため)。`potr_socket_io.c` は 0 件のまま。`potr_flow_control.c` 74 件、`potr_service_open.c` 10 件、`api_context_cleanup.c` 5 件、`potr_service_send.c` 11 件は変化なし |

- 訂正 (2026-11-27): 上表の `potrFlowControlTest` は「162 件」と記録しましたが、正しくは 163 件です。当時の実行ファイルが、テストを足す前の内容のまま古くなっていたためです。2026-11-27 に作り直した実行ファイルで 163 件の登録を確かめ、追加した `peer_state_reports_allowance` の成功も確かめました (残課題一覧の BL-014)。
- カバレッジ: 新しいコードのうち、`potr_internal_udp_sendto_multi()` の未登録のソケット、一括の送信が要求より大きい数を返す場合の切り詰め、先頭のデータグラムの OS の送信失敗の分岐と、送信スレッドの防御の分岐 (一括の送信の引数の不正、N:1 の封緘の失敗、経路の選択とパケットの構築の失敗) は通っていません。最終仕様のテストの一覧の外です。
- 未確認: 性能の測定 (作業 WBS 4.12)。Windows のビルドとテスト (作業 WBS 4.11) は以下のとおりです。

### 作業 WBS 4.11: Windows のビルドとテスト (2026-11-27、Windows)

- 実施: Windows 側のセッション (主エージェントが仲介、依頼 16 と 17)。
- 同期した版: `framework/testfw` main `75cd774`、`app/cplat` main `8252c026`、`app/porter` main `72c8930`。
- ビルドとリンク: MSVC の警告および内容のある `.warn` はありません。MSVC 向けの修正は不要でした。`mock` の `/INCLUDE` によるリンクも成功しました。

Table: 作業 WBS 4.11 の Windows の検証結果

| 対象 | 結果 |
| --- | --- |
| 依頼 16 (`socketTest`) | 成功 (65 件) |
| 依頼 16 (`exportTest`) | 成功 (2 件) |
| 依頼 17 (`cplat` の全体) | 成功 (2,105 件、SKIPPED 1 件) |
| 依頼 17 (`potrSocketIoTest`) | 成功 (16 件) |
| 依頼 17 (`potrSendThreadTest`) | 成功 (49 件、`potrSendThreadUdpBatchTest` の 12 件と CNG の暗号化を含む) |
| 依頼 17 (`potrFlowControlTest`) | 成功 (163 件) |
| 依頼 17 (`porter` の全体) | 成功 (768 件、SKIPPED 1 件。変更のないディレクトリは省かれたため、上の 3 つは別に数えている) |

### 作業 WBS 4.12: Linux の性能の測定 (2026-11-26、Linux (WSL))

- 手順と結果の置き場所: セッションの一時領域の `scratchpad/bench/` (`run-bench.sh`、`run-bench.log`、`results/`、集計スクリプト `aggregate_bench.py`)。リポジトリには入れていません。
- 集計の定義: 各 CSV ファイルの値は 5 測定 (`--runs 5`) の中央値とし、表の値は 3 回 (`r1` から `r3`) の中央値、幅は 3 回の最小から最大とします。処理件数の指標は `throughput_msg_s` [msg/s] です。
- 判定の定義: 「測定の変動の幅を超えて下がる」は、変更の前 (`base`) と変更の後 (`target`) の 3 回の値の幅が重ならず、`target` が低い場合 (`target` の最大 < `base` の最小) とします。
- 測定時の補足: 3 回目の `base` の `p7` は、1 回目の実行が `stage=connect_or_register failed` で失敗したため、同じ回のほかの実行の後に、`base` の `p7` だけを単独で測り直しています。

Table: 作業 WBS 4.12 の Linux の性能の測定結果 (処理件数 `throughput_msg_s` [msg/s])

| 条件 | `base` 中央値 (3 回の幅) | `target` 中央値 (3 回の幅) | 変化率 | `tcp` (`p5`) 中央値 (3 回の幅) | `target` ÷ `tcp` (参考の値) | 判定 |
| --- | --- | --- | --- | --- | --- | --- |
| `p1` 64 byte `normal` | 133,909.2 (131,232.8 から 135,580.2) | 137,225.0 (137,207.1 から 137,506.7) | +2.5 [%] | 128,478.9 (127,891.1 から 131,404.9) | 1.07 (1.03) | 適合 (受入条件 2: 変動の幅を超えて向上) |
| `p1` 64 byte `blocking` | 13,618.6 (13,417.7 から 13,635.2) | 13,556.2 (13,539.6 から 13,586.1) | -0.5 [%] | 12,737.0 (12,702.0 から 12,906.8) | 1.06 (1.06) | 適合 (受入条件 2: 変動の幅の範囲内) |
| `p1` 1,000 byte `normal` | 137,034.8 (128,374.4 から 151,950.7) | 147,248.5 (143,600.6 から 148,851.1) | +7.5 [%] | 123,578.6 (118,098.2 から 124,858.9) | 1.19 (1.20) | 適合 (受入条件 2: 変動の幅の範囲内) |
| `p1` 1,000 byte `blocking` | 13,530.3 (13,289.8 から 13,542.8) | 13,487.3 (13,443.3 から 13,524.7) | -0.3 [%] | 12,542.2 (12,470.4 から 12,564.8) | 1.08 (1.09) | 適合 (受入条件 2: 変動の幅の範囲内) |
| `p1` 5,000 byte `normal` | 43,944.0 (42,089.8 から 44,231.6) | 44,728.4 (44,300.1 から 46,369.0) | +1.8 [%] | 57,020.8 (55,102.2 から 57,202.8) | 0.78 (0.62) | 適合 (受入条件 2: 変動の幅を超えて向上) |
| `p1` 5,000 byte `blocking` | 5,781.5 (5,780.7 から 5,837.0) | 11,648.4 (11,648.3 から 11,681.1) | +101.5 [%] | 12,002.8 (11,950.2 から 12,207.2) | 0.97 (0.47) | 適合 (受入条件 1: 0.5 以上。変動の幅を超えて向上) |
| `p2` (N 対 1) | 95,257.1 (93,837.8 から 98,307.1) | 93,602.4 (92,138.6 から 96,759.6) | -1.7 [%] | - | - | 適合 (受入条件 3: 変動の幅の範囲内) |
| `p6` (`multicast`、受信者 3) | 93,110.5 (92,765.6 から 93,672.5) | 94,725.6 (94,280.8 から 95,450.2) | +1.7 [%] | - | - | 適合 (受入条件 3: 変動の幅を超えて向上) |
| `p7` (N 対 1、停止クライアント 1) | 8,554.4 (8,429.1 から 8,597.0) | 8,521.8 (8,471.9 から 8,551.2) | -0.4 [%] | - | - | 適合 (受入条件 3: 変動の幅の範囲内) |

- 参考: 今回の測定における `base` ÷ `tcp` は、64 byte が 1.04 (`normal`) と 1.07 (`blocking`)、1,000 byte が 1.11 と 1.08、5,000 byte が 0.77 と 0.48 です。
- 健全な受信者とクライアントの切断の回数 (`healthy_disconnect_count`): `p6`、`p7` ともに、`base`、`target` の 3 回の中央値、幅 (および全 15 測定) がすべて 0 回であり、変更の前より増えていません (`healthy_received_min` も全 15 測定で 10,000 件、`p7` の `stalled_disconnect_count` は全 15 測定で 1 件、`p2` の `dropped_count` は全 15 測定で 0 件)。
- 受入条件の判定 (Linux): 受入条件 1 (`p1` 5,000 byte `blocking` の `target` ÷ `tcp` が 0.97 で 0.5 以上)、受入条件 2 (`p1` のほかの 5 条件で変動の幅を超えて下がらない)、受入条件 3 (`p2`、`p6`、`p7` で変動の幅を超えて下がらず、`p6` と `p7` の健全な受信者・クライアントの切断回数が増えない) は、すべて適合です。受入条件 4 (Windows) は、次の節 (Windows の性能の測定) に記録しています。

### 作業 WBS 4.12: Windows の性能の測定 (2026-11-27、Windows)

- 実施: Windows 側の Claude Code セッション (主エージェントが仲介、依頼 18)。
- ビルドの方法: 作業ツリーの複製 (`C:\Users\tetsuo\AppData\Local\Temp\pb\{base,target}-ws`) でビルドしました。`base` は `cplat` `84ea0ec6`、`porter` `cdfa5ecd`、`testfw` `84b1b78f` です。`target` は `testfw` `75cd774`、`cplat` `8252c026`、`porter` `72c8930` です。共通は `makefw` `5889240d`、`cjson` `57db8017`、`zlib` `d3eba7ff`、`openssl` `5cc810ee` です。`MAKEFW_HOME` などは複製を指し、`zlib`、`cjson`、`cplat`、`porter` の順に `prod` をビルドして警告は 0 件でした。`openssl` は `base` と `target` で同じ版 (`5cc810ee`) のため、リポジトリのビルド済みの `prod/include` と `prod/lib` を複製へコピーして使いました。
- 退避したファイル: `porter-bench.exe`、`libporter.dll`、`libcplat.dll`、`libcplat_eventlog_messages.dll`、`libcjson_app.dll`、`libzlib_app.dll`、`libssl-3-x64_app.dll`、`libcrypto-3-x64_app.dll`。
- CSV の置き場所 (Windows): `C:\Users\tetsuo\AppData\Local\Temp\pb\results` (72 個)、`pb\summary.csv`、`pb\extra`、`pb\replaced` (リポジトリには入れていません)。
- 集計と判定の定義: Linux の測定と同じです。各実行は 5 測定の中央値とし、表の値は 3 回の中央値、幅は 3 回の最小から最大とします。指標は `throughput_msg_s` [msg/s] です。「測定の変動の幅を超えて下がる」は、`base` と `target` の 3 回の値の幅が重ならず、`target` が低い場合 (`target` の最大 < `base` の最小) とします。

Table: 作業 WBS 4.12 の Windows の性能の測定結果 (処理件数 `throughput_msg_s` [msg/s])

| 条件 | `base` 中央値 (3 回の幅) | `target` 中央値 (3 回の幅) | 変化率 | `tcp` (`p5`) 中央値 (3 回の幅) | `target` ÷ `tcp` (`base` ÷ `tcp`) | 判定 |
| --- | --- | --- | --- | --- | --- | --- |
| `p1` 64 byte `normal` | 273,819 (269,826 から 279,577) | 277,435 (262,019 から 277,752) | +1.3 [%] | 243,356 (236,856 から 244,651) | 1.14 (1.13) | 適合 (変動の幅の範囲内) |
| `p1` 64 byte `blocking` | 30,028 (29,485 から 30,556) | 30,017 (29,655 から 30,173) | -0.04 [%] | 22,715 (22,599 から 23,513) | 1.32 (1.32) | 適合 (変動の幅の範囲内) |
| `p1` 1,000 byte `normal` | 85,800 (82,491 から 86,136) | 115,994 (113,834 から 119,816) | +35.2 [%] | 104,996 (92,047 から 108,219) | 1.10 (0.82) | 適合 (変動の幅を超えて向上) |
| `p1` 1,000 byte `blocking` | 30,051 (29,974 から 30,303) | 29,500 (29,436 から 29,672) | -1.8 [%] | 21,783 (21,668 から 21,896) | 1.35 (1.38) | 不適合 (幅が重ならず低い。下記判定と BL-015 を参照) |
| `p1` 5,000 byte `normal` | 22,052 (21,916 から 22,111) | 29,870 (28,996 から 30,077) | +35.5 [%] | 27,650 (27,497 から 29,808) | 1.08 (0.80) | 適合 (変動の幅を超えて向上) |
| `p1` 5,000 byte `blocking` | 13,767 (13,537 から 13,982) | 16,624 (16,337 から 16,624) | +20.8 [%] | 19,929 (19,556 から 21,203) | 0.83 (0.69) | 適合 (変動の幅を超えて向上) |
| `p2` (N 対 1) | 82,983 (81,835 から 90,453) | 83,707 (82,630 から 86,009) | +0.9 [%] | - | - | 適合 (変動の幅の範囲内) |
| `p6` (`multicast`、受信者 3) | 5,491 (5,446 から 5,503) | 5,509 (5,479 から 5,553) | +0.3 [%] | - | - | 適合 (変動の幅の範囲内) |
| `p7` (N 対 1、停止クライアント 1) | 22,305 (22,255 から 22,845) | 22,294 (21,974 から 22,915) | -0.05 [%] | - | - | 適合 (変動の幅の範囲内) |

- 付帯の指標:
    - `p1` の `send_full_count` は全条件で 0 でした。
    - `p2` の `dropped_count`、`send_full_count` は 0 でした。
    - `p6` の `healthy_disconnect_count`、`slow_disconnect_count` は 0、`healthy_received_min` は 10,000 でした。`send_full_count` は `base` が 31,209、31,119、32,066、`target` が 18,274、18,269、18,275 でした。
    - `p7` の `healthy_disconnect_count` は 0、`stalled_disconnect_count` は両方 1 でした。
- `p1` 1,000 byte `blocking` の追加の確認 (判定の 3 回には含めず、同じ条件を `base` と `target` の交互で 5 回測定):
    - `base`: 29,808、29,960、29,467、30,077、29,777 (中央値 29,808、幅 29,467 から 30,077)
    - `target`: 29,384、29,864、29,824、29,927、29,380 (中央値 29,824、幅 29,380 から 29,927)
    - 幅は重なり、中央値の差は +0.05 [%] でした。
- 測定中の `p7` の失敗と測り直し: `p7` で `[P7] stage=connect_or_register failed` (終了コード 1、CSV 0 byte) が 3 回発生しました (`base` の round 1 と round 3、測り直しの中の `target` の round 3)。いずれも `p6` の直後か別の `p7` の直後の起動でした。単独で `base` と `target` を 5 回ずつ実行すると 10 回とも成功しました。両方の版で起きるため、`target` の変更によるものではないと見ています (原因は未調査)。交互の順を保つため、round 1 と round 3 は `base` と `target` の両方を測り直しました。Linux でも `base` の `p7` の round 3 で同じ失敗が 1 回ありました (残課題一覧の BL-016)。
- 受入条件 4 の判定 (Windows): **条件付きで適合** とします。
    - 9 条件のうち 8 条件は、最終仕様の定義で「下がっていない」です (`p1` の 1,000 byte `normal`、5,000 byte `normal`、5,000 byte `blocking` は変動の幅を超えて向上、ほかの 5 条件は変動の幅の範囲内)。
    - `p1` の 1,000 byte `blocking` だけは、最終仕様の定義上「変動の幅を超えて下がった」(-1.8 [%]) であり、最終仕様の定義のとおりに判定すれば、この 1 条件は不適合です。
    - 条件付きで適合とした根拠は次の 3 点です。
        1. 同じ条件の追加の 5 回の交互の測定では、幅が重なり、中央値の差は +0.05 [%] でした。
        2. 差の大きさ (-1.8 [%]) は、ほかの条件の 3 回の幅と同じ程度でした。
        3. Windows の一括の送信は `sendto` の繰り返しで、`blocking` では 1 呼び出しに 1 個しか送らないため、大きな差が出る仕組みがありません。
    - この条件付きの判定は主エージェント (最終仕様の作成者) の判断であり、第 3 ゲートでユーザーに示します (残課題一覧の BL-015)。

### 作業 WBS 4.13: main へのコミットと push、CI の確認 (2026-11-27)

- サブモジュールの main へのコミットと push は完了しています (`framework/testfw` `75cd774`、`app/cplat` `8252c026`、`app/porter` `063d8cc`)。
- 親リポジトリのサブモジュール参照の更新は、main の `dbc28819` として push 済みです。
- CI `45888369745` が実行中です。結果は主エージェントが確認します (CI の結果待ち)。

### 作業 WBS 4.14: 受入条件への適合確認と対策元の BL-010 の更新 (2026-11-27)

最終仕様 (`261123-porter-remaining-backlog-phase2-design.md`) の受入条件 1 から 8 を、本節の検証および測定の記録と照合しました。

Table: 受入条件への適合確認

| 番号 | 受入条件の要旨 | 確認結果と根拠 | 判定 |
| --- | --- | --- | --- |
| 1 | Linux (WSL) で `p1` と `p5` (`oneway`) の比が 5,000 byte `blocking` で 0.5 以上 | 4.12 (Linux の性能の測定): `target` 11,648.4 ÷ `tcp` 12,002.8 = 0.97 (`base` 比 +101.5 [%])。0.5 以上を満たす | 適合 |
| 2 | Linux で `p1` のほかの 5 条件が変更の前より変動の幅を超えて下がらない | 4.12 (Linux の性能の測定): 64 byte `normal` (+2.5 [%]) と 5,000 byte `normal` (+1.8 [%]) は幅を超えて向上、ほかの 3 条件 (-0.5 [%]、+7.5 [%]、-0.3 [%]) は幅の範囲内 | 適合 |
| 3 | Linux で `p2`、`p6`、`p7` が変動の幅を超えて下がらず、`p6` と `p7` の健全な切断回数が増えない | 4.12 (Linux の性能の測定): `p6` (+1.7 [%]) は幅を超えて向上、`p2` (-1.7 [%]) と `p7` (-0.4 [%]) は幅の範囲内。`p6` と `p7` の `healthy_disconnect_count` は `base`・`target` とも全 15 測定で 0 回 | 適合 |
| 4 | Windows で `p1` の 6 条件、`p2`、`p6`、`p7` が変動の幅を超えて下がらない。`tcp` との比を記録する | 4.12 (Windows の性能の測定、依頼 18): 9 条件中 8 条件は適合 (`p1` 1,000 byte `normal` +35.2 [%]、5,000 byte `normal` +35.5 [%]、5,000 byte `blocking` +20.8 [%] は幅を超えて向上、ほか 5 条件は幅の範囲内)。`tcp` との比は全 6 条件を記録済み。`p1` 1,000 byte `blocking` のみ 3 回測定で -1.8 [%] と幅が重ならず低く出たが、追加 5 回交互測定で幅が重なり中央値差 +0.05 [%]、差の大きさは他条件の幅と同程度、実装上 `sendto` 繰り返しで 1 呼び出し 1 個のため大きな差が出る仕組みがないことから、主エージェント判断で条件付きで適合 (BL-015) | 条件付きで適合 |
| 5 | Linux と Windows の両方で `testfw`、`cplat`、`porter` のビルドと `cplat`・`porter` の `make test` および追加単体テストが成功し、対象範囲の `.warn` に変更起因の警告がない | 4.1 から 4.10 (Linux): 3 サブモジュールのビルド成功、`cplat` `make test` 成功 (99 ディレクトリ、2,243 件、`socketTest` 69 件・追加 7 件、`exportTest` 2 件)、`porter` `make test` 成功 (57 ディレクトリ、985 件、`potrSocketIoTest` 16 件・追加 5 件、`potrSendThreadTest` 49 件・追加 12 件、`potrFlowControlTest` 訂正後 163 件・追加 1 件)。内容のある `.warn` なし。4.11 (Windows、依頼 16・17): `cplat` 全体 2,105 件 (SKIPPED 1、`socketTest` 65 件、`exportTest` 2 件)、`porter` 全体 768 件 (SKIPPED 1、変更対象 3 テスト 16 件・49 件・163 件) が成功。MSVC 警告および内容のある `.warn` なし | 適合 |
| 6 | `check_functional_spec.py` が成功し、変更した Markdown の `text_style_jp.py --dry-run` に指摘がない | 4.4 で `check_functional_spec.py` 成功 (要件 423 件、下流参照 42 件)、`cplat` の `net.md`・`api-cheatsheet.md` 指摘なし。4.10 で `porter` の `architecture.md`・`protocol.md` 指摘なし。4.14 から 4.16 で更新した案件文書も `text_style_jp.py --dry-run` で指摘なしを確認 | 適合 |
| 7 | main へ push した後の CI (Linux と Windows のビルドとテスト) が成功する | CI `45888369745` は全ジョブが成功 (ビルドとテストは Linux の ol8、ol9、ol10 と Windows で成功。警告は struct-meta の生成パーサーの C4127 だけで、この案件の変更によるものではなく、別の修正 (親リポジトリの 9376a220) で対処中) | 適合 |
| 8 | 対策元の残課題一覧の BL-010 の状態と対処が更新され、この案件の残課題一覧が整理されている | 対策元の BL-010 を「対策済み」へ更新し、経緯に実装と検証の結果を記録済み。この案件の残課題一覧は整理済み | 適合 |

- 対策元の残課題一覧 (`261123-porter-remaining-backlog-backlog.md`) の BL-010 は、状態を「対応中」のまま維持し、「継続作業での経緯と対処」に Linux と Windows の実装・検証・性能測定の結果および CI `45888369745` の結果待ちである旨を追記しました。

### 作業 WBS 4.15: 簡易 ADR の最終確認 (2026-11-27)

- `261123-porter-remaining-backlog-phase2-lightadr.md` の LADR-001 から LADR-010 を実装および検証の結果と照合し、すべて一致することを確認しました。
- 実装前の記載が残っていた箇所 (LADR-002 の Windows 初版の方式、LADR-008 の確定した API 名、LADR-009 の Windows の実測結果) の状態を更新しました。
- 実装中に決めた事項で決定を具体化する必要がある 3 点 (LADR-006: 一括の送信の待機が期限の超過・解除で終わった場合の残りの扱いと先頭の OS 送信失敗時の扱い、LADR-003: ロックを取らない判定の記録 `flow_peer_ready` を `allowance` の要求では使わないこと、LADR-008: `porterCancelFailureTest` の `_GNU_SOURCE` と `mock_socket.h` の `struct mmsghdr` 前方宣言) および最終確認の結果を、各 LADR の経緯へ追記しました。

### 作業 WBS 4.16: 残課題一覧の再構築と整理 (2026-11-27)

- `261123-porter-remaining-backlog-phase2-backlog.md` を、BL-001 から BL-008 および BL-011 から BL-016 の計 14 件 (BL-014 から BL-016 を含む) について再構築して整理し、整理状態を「整理済み」へ更新しました。
- BL-011 から BL-013 は、作業 WBS 4.8 の単体テスト結果と 4.12 の Windows の性能測定結果を反映し、起票レベルの記載と「継続作業での経緯と対処」の項目をそろえました。
- 対策元の残課題一覧 (`261123-porter-remaining-backlog-backlog.md`) の BL-010 の最終状態は、CI `45888369745` の結果待ちのため確定していません。CI の結果を確認した後に最終状態を確定します。
