<!--
生成元: agree-before-implement スキル
文書種別: 検討用 Markdown (discussion)
最終更新日時: 2026-11-18 05:47:26 JST
最終更新エージェント: claude code / セッション ID: 2597d650-1196-4c79-aa7b-18ba3526112b
稼働 OS: Windows
上流文書: `261114-porter-windows-verification-clock-unify-backlog.md` (BL-001 の対策元)、`261114-porter-windows-verification-clock-unify-discussion.md` (Q-011)。削除されている場合があります。
下流文書: `261115-porter-multicast-src-contract-design.md` (最終仕様)、`261115-porter-multicast-src-contract-backlog.md` (残課題一覧)。未作成の場合や、削除されている場合があります。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。ユーザーが編集した最新版を読み直し、作業 WBS の `← **CURRENT**` の項目から再開します。最終更新エージェントのセッション情報や稼働 OS・最終更新日時が参照可能であれば再開時のコンテキストや環境差分の把握に利用し、別エージェントや別環境・別 OS からの引き継ぎ、担当変更によりセッションが見つからない場合も異常とせず、文書の記載から継続してください。`← **CURRENT**` の項目は処理が中止された項目である場合があり、どこまで進んでいたかは文書だけでは分かりません。対象成果物の内容を確認して実施済みの範囲を特定してから、作業を再開してください。`← **CURRENT**` がなく、作業 WBS がすべて完了 (✅) であれば、この案件の合意フローは完了しています。
-->

# porter マルチキャスト受信側の送信元設定契約の検討

ファイル名: `261115-porter-multicast-src-contract-discussion.md`

## 検討の識別

検討 ID: 261115-porter-multicast-src-contract

版: 5

状態: 合意済み (版 5、追加範囲の第 1 ゲート通過。版 3 の合意内容は実装済み)

## 作業 WBS と進捗管理

この検討用 Markdown が管理する作業 WBS です。過去 backlog `261114-porter-windows-verification-clock-unify-backlog.md` の BL-001 を対策します。作業 4 の内訳は、[Q-001](#q-001-マルチキャストの送信元制限の維持) から [Q-004](#q-004-受入試験の範囲) の回答で見直しました。

凡例: ⏸️ 未着手、🔄 実施中、🚫 ブロック中、⚠️ 要注意、✅ 完了。現在取り組んでいる項目の末尾に `← **CURRENT**` を付けます。

- ✅ 1. 検討用 Markdown のための情報収集と質問の実施
    - ✅ 1.1. 前提条件と要求事項の調査 (`config.md`、`transport.md`、`porter_type.h`、`api_open_paths_udp.c`、`thread_recv_validate.c`、`thread_recv_window.c`、`thread_recv_report.c`、`certificate.c`、`potr_service_close.c`、`config_load_service.c`、`porter-services.jsonc`)
    - ✅ 1.2. 課題・論点の抽出と明確化のための確認項目 ([Q-001](#q-001-マルチキャストの送信元制限の維持) から [Q-004](#q-004-受入試験の範囲)) の作成
    - ✅ 1.3. Q&A ターンごとのユーザー回答の受付と記録 (JST 日時、Git ユーザー名または発言者)。版 2 で Q-001 から Q-004、版 3 で Q-005 を記録
    - ✅ 1.4. 回答に基づく前提・制約の検証と WBS の見直し (版 3)
- ✅ 2. 前提合意と残懸念の解消 (第 1 ゲート: 最終仕様作成への着手)
    - ✅ 2.1. 全論点に対する未解決事項および質問の解消確認
    - ✅ 2.2. 方針・前提条件の合意確定 (懸念がないことをもってフェーズ 2 へ着手)
- ✅ 3. 最終決定事項 (最終仕様) の作成と承認の受付
    - ✅ 3.1. `261115-porter-multicast-src-contract-design.md` の作成
    - ✅ 3.2. 最終仕様における受入条件と整合性の確認
    - ✅ 3.3. 対話セッションにおける最終仕様 (design.md) の承認の受付 (第 2 ゲート: 作業着手の合意)。2026-11-15 に Tetsuo Honda から条件付き承認を受領。条件は「作業中の他セッションから進行の許可を受けてから作業し、コミット、プッシュ、CI まで実施する」
- 🔄 4. 承認された最終仕様に従った作業の実施 (実装は grok CLI `grok-4.7` / reasoning effort high / `--yolo` に委譲し、claude code が差分を確認)
    - ✅ 4.0. 他セッションからの進行許可の受領 (2026-11-15、c-modernization-kit-c1 から porter-reject-recovery のプッシュ完了と進行許可を受領。対策元 backlog の BL-001 を「対応中」へ更新)
    - ✅ 4.1. 設定仕様 `app/porter/docs/config.md` の更新 (共通フィールド表の `dst_addr`、multicast の図と表、送信元フィルタリング節)
    - ✅ 4.2. 機能仕様 `app/porter/docs/functional-spec/transport.md` の `POTR-TRANSPORT-QUAL-001` が変更不要であることの確認 ([Q-001](#q-001-マルチキャストの送信元制限の維持) で維持に決定)
    - ✅ 4.3. 公開ヘッダー `app/porter/prod/include/porter/porter_type.h` の `src_addr` / `dst_addr` の Doxygen 更新 (構造体の形は変えない)
    - ✅ 4.4. 内部構造体 (`potr_context`) への経路ごとの参加 NIC (`multicast_if_resolved`) の保持と、`api_open_paths_udp.c` の `api_open_paths_multicast()` の参加 NIC 決定の変更
    - ✅ 4.5. `app/porter/prod/libsrc/porter/api/potr_service_close.c` のグループ離脱で使う NIC の変更
    - ✅ 4.6. 単体テストの追加・更新 (`apiOpenPathsUdpTest` に 5 件、`potrServiceCloseTest` に 1 件、`configLoadServiceTest` に 1 件)
    - ✅ 4.7. サンプル設定 `app/porter/prod/sample-config/porter-services.jsonc` への別ホスト構成例 (サービス `2003`) の追加
    - ✅ 4.8. 正本ドキュメントおよびコード コメントへの永続的知見の反映
    - ✅ 4.9. 局所テストおよび機械的検証 (`text_style_jp.py`、Windows/MSVC で対象 3 テスト ディレクトリの `make test` が失敗 0)
    - ✅ 4.10a. コミット、プッシュ、CI の結果確認 (第 2 ゲートの承認条件)。porter `ebb79c2` を含む CI run 43835974193 (親 `fbbc1d848a`、porter `5ff9609`) で全ジョブ (Windows、ol8、ol9、ol10、docs 等) が成功
    - ✅ 4.13. 追加範囲: 案件 backlog BL-001 (handleConcurrencyTest の接続待ち) の調査と対策 ([Q-006](#q-006-ciの不定期失敗テストを本案件の範囲に加えるか) で範囲に追加)
        - ✅ 4.13.1. 原因の調査 (結果は [Q-007](#q-007-handleconcurrencytestの接続待ちの直し方) の Context)
        - ✅ 4.13.2. 対策方針の確認 ([Q-007](#q-007-handleconcurrencytestの接続待ちの直し方) で A に決定、版 5 で第 1 ゲート通過)
        - ✅ 4.13.3. 最終仕様 (design.md) への追記と、追記部分の承認の受付 (第 2 ゲート)。2026-11-16 に Tetsuo Honda から「承認」を受領
        - ✅ 4.13.4. `app/porter/test/src/integration/porterSendRecvTest/handleConcurrencyTest.cc` の修正 (grok へ委譲)。Windows/MSVC で `porterSendRecvTest` 36 件成功、`handleConcurrencyTest` 10 件を 5 回繰り返して失敗 0
        - ✅ 4.13.5. コミット、プッシュ、CI の結果確認と、案件 backlog BL-001 の状態更新 (porter `8220519` を含む CI run 43835974193 で全ジョブ成功を確認し、案件 backlog BL-001 を対処済みへ更新)
    - ✅ 4.10. ユーザーの環境での PSK と証明書の Windows/WSL 相互接続 12 組み合わせの再実施 ([Q-004](#q-004-受入試験の範囲))。2026-11-18 に、ユーザーの指示でエージェントが自動実施し、12 件すべて成功 (結果は「反映と検証の記録」)
    - ✅ 4.11. 受入条件への適合確認 (全完確認)。単体テストは CI run 43835974193 で成功、相互接続 12 組み合わせは 4.10 で全件成功
    - 🔄 4.12. 残課題一覧 (`261115-porter-multicast-src-contract-backlog.md`) の再構築と整理、および過去 backlog BL-001 の経緯・対処の更新。案件 backlog BL-002 は、継続案件 261116-makefw-subdir-rebuild-race の CI の成功を受けて 2026-11-18 に「解消」へ更新済み。残りは文書の再構築と整理 ← **CURRENT**
- ⏸️ 5. 成果物の承認の受付 (第 3 ゲート)
    - ⏸️ 5.1. 成果物・検証結果・残課題の提示
    - ⏸️ 5.2. 対話セッションにおける成果物の承認の受付 (「承認」または「完了」の受領によるスキル完了)

## 対象と参照資料

対象成果物: porter のマルチキャスト受信側の設定契約 (設定仕様、機能仕様、公開ヘッダーの説明、実装、テスト、サンプル設定)

今回の対象範囲: 過去 backlog の BL-001 のみです。同じ backlog の BL-003 (certificateTest の make 構成時の stderr) は別案件として扱います。

書き込み可能な検討用 Markdown: この文書 (`261115-porter-multicast-src-contract-discussion.md`)

参照資料と版: 2026-11-15 時点の作業ツリー (コミット `a1c564a4`) の次の資料です。

- [設定ファイル仕様](../../config.md) の「全通信種別で共通のフィールド」「multicast (1:N 通信)」「送信元フィルタリング」節
- [通信種別の機能仕様](../../functional-spec/transport.md) の `POTR-TRANSPORT-QUAL-001`
- [porter_type.h](../../../prod/include/porter/porter_type.h#L147-L153) の `src_addr` / `dst_addr`
- [サンプル設定](../../../prod/sample-config/porter-services.jsonc#L106-L150)

## 調査で確認した事実

porter の設定は、送信側と受信側が同じサービス定義を読む前提で設計されています。unicast では `src_addr1` が送信側の bind アドレス、`dst_addr1` が受信側の bind アドレスです ([config.md](../../config.md#L424-L432))。公開ヘッダーも、受信側の `src_addr` を「送信元フィルター」と説明しています ([porter_type.h](../../../prod/include/porter/porter_type.h#L150))。サンプル設定のサービス `2002` は、マルチキャストの `src_addr1` に送信側の IP `192.168.1.10` を書いています。

これに対し、マルチキャスト受信側だけは `src_addr1` から `src_addr8` を次の 3 つの用途に同時に使っています。

| 用途 | 期待する値 | 実装箇所 |
| --- | --- | --- |
| グループ参加に使う NIC | 受信側の自分の NIC の IP | [api_open_paths_udp.c](../../../prod/libsrc/porter/api/api_open_paths_udp.c#L271-L277)、離脱は [potr_service_close.c](../../../prod/libsrc/porter/api/potr_service_close.c#L504) |
| 送信元 IP の照合 | 送信側の IP | [thread_recv_validate.c](../../../prod/libsrc/porter/thread/thread_recv_validate.c#L192-L203) |
| NACK、受信報告、証明書認証の返送先 | 送信側の IP | [thread_recv_window.c](../../../prod/libsrc/porter/thread/thread_recv_window.c#L136)、[thread_recv_report.c](../../../prod/libsrc/porter/thread/thread_recv_report.c#L82)、[certificate.c](../../../prod/libsrc/porter/security/certificate.c#L732) |

Table: マルチキャスト受信側の src_addr の用途

1 つの値に「自分の NIC」と「相手の IP」を求めているため、送信側と受信側が別のホストにある場合は成立しません。成立するのは、同じホスト内で両者が同じ IP を使う場合 (ループバックの試験など) だけです。過去の Windows/WSL 試験で、送信元の照合で破棄されたことと整合します。仮に照合だけをやめても、NACK などは受信側の自分の NIC 宛てに送られるため、再送は成立しません。

broadcast の受信側は `INADDR_ANY` で bind し、`src_addr` を送信側の IP として一貫して使っています ([api_open_paths_udp.c](../../../prod/libsrc/porter/api/api_open_paths_udp.c#L168-L176))。用途の衝突はマルチキャストだけです。

設定ファイルの読み込みは、通信種別を問わず `dst_addr1` から `dst_addr8` を読み込みます ([config_load_service.c](../../../prod/libsrc/porter/protocol/config_load_service.c#L259))。現在のマルチキャストは、読み込んだ `dst_addr` を使っていません。

## 現行の合意内容

版 3 で全論点が解決しました。ユーザーが明示した内容と解決済み Q の決定は次のとおりです。

- BL-001 と BL-003 を別の案件として扱い、BL-001 から検討を始めます (2026-11-15 の対話で、対処計画への「進めて」の回答)。BL-003 は makefw `f6a1367` で解消済みと確認し、過去 backlog に記録しました。
- マルチキャストでも `POTR-TRANSPORT-QUAL-001` を維持し、受信側は `src_addr` で送信元を限定します ([Q-001](#q-001-マルチキャストの送信元制限の維持))。
- マルチキャスト受信側がグループ参加に使う NIC は `dst_addr1` から `dst_addr8` で表します ([Q-002](#q-002-受信側の参加nicを表す設定))。
- 受信側で `dst_addrN` を省略した経路は、今と同じく `src_addrN` の NIC で参加します ([Q-003](#q-003-参加nicを省略したときの動作))。
- 受入条件は、単体テストと、ユーザーの環境での PSK と証明書の Windows/WSL 相互接続 12 組み合わせの全件成功です ([Q-004](#q-004-受入試験の範囲))。
- 経路は `src_addrN` が空の最初の経路で列挙を終え、`dst_addrN` だけの経路は使いません ([Q-005](#q-005-受信側のdst_addrだけがある経路の扱い))。

## 観点別の確認

| 観点 | 状態 | 根拠・関連 Q |
| --- | --- | --- |
| 目的・利用者 | 確認済み | 別ホスト間のマルチキャスト通信を設定で成立させることです (過去 backlog BL-001 の「本来あるべき姿」) |
| 範囲・境界 | 確認済み | [Q-001](#q-001-マルチキャストの送信元制限の維持)、[Q-002](#q-002-受信側の参加nicを表す設定)。`multicast_raw` も同じ経路 (`raw_base_type()`) を通るため対象に含みます |
| 入力・根拠 | 確認済み | 「調査で確認した事実」節の資料 |
| 整合性・依存関係 | 確認済み | [Q-001](#q-001-マルチキャストの送信元制限の維持) (機能仕様を維持)、[Q-003](#q-003-参加nicを省略したときの動作) (既存設定との互換を維持) |
| 実現性・例外 | 確認済み | [Q-003](#q-003-参加nicを省略したときの動作) (省略時の参加 NIC)、[Q-005](#q-005-受信側のdst_addrだけがある経路の扱い) (`dst_addrN` だけがある経路) |
| 受入条件・検証 | 確認済み | [Q-004](#q-004-受入試験の範囲) |

## 質問一覧

| Q-ID | 論点 | 状態 | 依存・関連 |
| --- | --- | --- | --- |
| [Q-001](#q-001-マルチキャストの送信元制限の維持) | マルチキャストの送信元制限の維持 | 解決済み (A) | なし |
| [Q-002](#q-002-受信側の参加nicを表す設定) | 受信側の参加 NIC を表す設定 | 解決済み (A) | [Q-001](#q-001-マルチキャストの送信元制限の維持) で A の場合 |
| [Q-003](#q-003-参加nicを省略したときの動作) | 参加 NIC を省略したときの動作 | 解決済み (A) | [Q-002](#q-002-受信側の参加nicを表す設定) |
| [Q-004](#q-004-受入試験の範囲) | 受入試験の範囲 | 解決済み (A) | なし |
| [Q-005](#q-005-受信側のdst_addrだけがある経路の扱い) | 受信側の dst_addr だけがある経路の扱い | 解決済み (A) | [Q-002](#q-002-受信側の参加nicを表す設定)、[Q-003](#q-003-参加nicを省略したときの動作) |
| [Q-006](#q-006-ciの不定期失敗テストを本案件の範囲に加えるか) | CI の不定期失敗テストを本案件の範囲に加えるか | 解決済み (A) | 案件 backlog BL-001 |
| [Q-007](#q-007-handleconcurrencytestの接続待ちの直し方) | handleConcurrencyTest の接続待ちの直し方 | 解決済み (A) | [Q-006](#q-006-ciの不定期失敗テストを本案件の範囲に加えるか) |

## 明確化のための確認項目

### Q-001 マルチキャストの送信元制限の維持

**Status**

解決済み

**Question**

機能仕様 `POTR-TRANSPORT-QUAL-001` (受信側が受け付ける送信元を構成で限定できる) を、マルチキャストでも維持しますか。

**Context**

前回の Q-011 で未決のまま移管した論点です。機能仕様は通信種別による例外を書いていません。

- 維持する場合は、受信側に「自分の NIC」と「許可する送信元」を別々に書ける設定が必要です。設定の表し方は [Q-002](#q-002-受信側の参加nicを表す設定) で決めます。
- 維持しない場合は、機能仕様にマルチキャストの例外を明記します。ただし「調査で確認した事実」節のとおり、NACK、受信報告、証明書認証の返送先も `src_addr` を使っています。照合をやめるだけでは別ホスト間の再送と証明書認証が成立しないため、返送先を受信した送信元から学習する変更も追加で必要になります。

**Answer**

選択方式: 単一選択

- [x] A: (推奨) 維持します。マルチキャストでも `src_addr` で送信元を限定し、参加 NIC を別の設定で表します。
- [ ] B: 維持しません。マルチキャストを `POTR-TRANSPORT-QUAL-001` の例外として機能仕様に明記し、返送先は受信した送信元から学習します。
- [ ] C: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

**Recommendation**

推奨: A  
理由: 機能仕様を変えずに済み、`src_addr` を「送信側のアドレス」とする公開ヘッダーと broadcast の扱いにそろいます。NACK などの返送先も今の `src_addr` のまま正しくなります。B は機能仕様の変更に加え、返送先の学習という別の仕組みが必要になり、変更範囲が広がります。

回答者: Tetsuo Honda (2026-11-15 22:09:07 JST、検討用 Markdown の保存日時)

**Assessment**

A の選択を確認しました。機能仕様 `POTR-TRANSPORT-QUAL-001` は変更不要です。受信側の `src_addr` は送信元の照合と、NACK、受信報告、証明書認証の返送先に使い続け、参加 NIC だけを別の設定に移します。参加 NIC の表し方は [Q-002](#q-002-受信側の参加nicを表す設定) で決まりました。

**Related**

[Q-002](#q-002-受信側の参加nicを表す設定)、上流 discussion の Q-011

### Q-002 受信側の参加 NIC を表す設定

**Status**

解決済み

**Question**

[Q-001](#q-001-マルチキャストの送信元制限の維持) で A (維持) を選んだ場合に、マルチキャスト受信側がグループ参加に使う NIC を、どの設定で表しますか。

**Context**

porter のサービス定義は送信側と受信側で共有され、unicast では `src_addr` が送信側、`dst_addr` が受信側のアドレスです。マルチキャストは今 `dst_addr` を使っていませんが、読み込み処理はすでに `dst_addr` を読み込みます。

経路 1 で、送信側 `192.168.1.10`、受信側の NIC `192.168.1.20` の場合の各案の書き方を示します。

```jsonc
// 案 A: dst_addrN を受信側の参加 NIC とする (新しいキーなし)
"2002": {
  "type": "multicast",
  "src_addr1": "192.168.1.10", // 送信側: 送信 NIC。受信側: 許可する送信元と NACK の返送先
  "dst_addr1": "192.168.1.20", // 受信側: グループ参加に使う NIC
  "dst_port": 6002,
  "multicast_group": "239.0.0.2"
}
```

```jsonc
// 案 B: 参加 NIC 専用の新しいキーを追加する (名前は例)
"2002": {
  "type": "multicast",
  "src_addr1": "192.168.1.10",
  "join_addr1": "192.168.1.20",
  "dst_port": 6002,
  "multicast_group": "239.0.0.2"
}
```

```jsonc
// 案 C: src_addrN は受信側で参加 NIC のままとし、許可する送信元の新しいキーを追加する (名前は例)
// 送信側と受信側で src_addr1 の値が異なるため、同じサービス定義を共有できない
"2002": {
  "type": "multicast",
  "src_addr1": "192.168.1.20",
  "allowed_src_addr1": "192.168.1.10",
  "dst_port": 6002,
  "multicast_group": "239.0.0.2"
}
```

案 A と案 B は、公開構造体 `potr_service_def` の扱いが異なります。案 A は既存の `dst_addr` 配列を使うため、構造体の形は変わらず、Doxygen の説明だけを変えます。案 B と案 C は構造体へ配列を追加するため、公開 API の変更として `exportTest.cc` と `docs/api.md` の確認が必要です。

**Answer**

選択方式: 単一選択

- [x] A: (推奨) `dst_addr1` から `dst_addr8` を受信側の参加 NIC とします。
- [ ] B: 参加 NIC 専用の新しいキーを追加します (キー名は追加の Q で決めます)。
- [ ] C: `src_addr` を受信側の参加 NIC のままとし、許可する送信元の新しいキーを追加します (キー名は追加の Q で決めます)。
- [ ] D: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

**Recommendation**

推奨: A  
理由: unicast と同じ「`src` は送信側、`dst` は受信側」の対応になり、送信側と受信側で同じサービス定義を共有できます。公開構造体の形も変わりません。C は同じサービス定義を両側で共有できなくなり、サンプル設定の書き方とも合いません。

回答者: Tetsuo Honda (2026-11-15 22:09:07 JST、検討用 Markdown の保存日時)

**Assessment**

A の選択を確認しました。公開構造体 `potr_service_def` の形は変わらないため、`exportTest.cc` の関数表と変数表は変更不要です。Doxygen の `dst_addr` の説明 (現在は「unicast のみ」) と `config.md` の共通フィールド表を更新します。送信側は今と同じく `dst_addr` を使わず、受信側だけが `dst_addrN` を参加 NIC として解決します。`src_addrN` がなく `dst_addrN` だけがある経路の扱いは決まっていないため、[Q-005](#q-005-受信側のdst_addrだけがある経路の扱い) で確認します。

**Related**

[Q-001](#q-001-マルチキャストの送信元制限の維持)、[Q-003](#q-003-参加nicを省略したときの動作)、[Q-005](#q-005-受信側のdst_addrだけがある経路の扱い)

### Q-003 参加 NIC を省略したときの動作

**Status**

解決済み

**Question**

[Q-002](#q-002-受信側の参加nicを表す設定) で決めた参加 NIC の設定を、受信側の経路で省略したとき、どの NIC でグループに参加しますか。

**Context**

既存の設定には参加 NIC の設定がありません。今動いているマルチキャストの設定は、同じホスト内の試験 (`src_addr1` が `127.0.0.1` など) に限られます。この場合、`src_addr` は送信側の IP でもあり受信側の NIC でもあります。

- 省略時に今と同じく `src_addrN` の NIC で参加すれば、既存の設定と試験はそのまま動きます。別ホスト間で使うときだけ参加 NIC を書きます。
- 省略時に `INADDR_ANY` (OS が経路表から選ぶ NIC) で参加すると、ループバックだけで閉じた試験では、OS によってはループバック以外の NIC で参加し、受信できなくなる可能性があります (未検証)。
- 省略をエラーにすると、既存のマルチキャスト設定がすべて開けなくなります。

**Answer**

選択方式: 単一選択

- [x] A: (推奨) 省略した経路は、今と同じく `src_addrN` の NIC で参加します。
- [ ] B: 省略した経路は `INADDR_ANY` で参加します。
- [ ] C: 受信側では省略を設定エラーにします。
- [ ] D: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

**Recommendation**

推奨: A  
理由: 既存の設定と試験の動作を変えずに、別ホスト間の構成を追加で書けるようにできます。B は既存の動作が変わる可能性を検証する必要があり、C は既存の設定を壊します。

回答者: Tetsuo Honda (2026-11-15 22:09:07 JST、検討用 Markdown の保存日時)

**Assessment**

A の選択を確認しました。参加 NIC は経路ごとに「`dst_addrN` があればその NIC、なければ `src_addrN` の NIC」で決めます。グループからの離脱 (`potr_service_close.c`) も参加時と同じ NIC を使う必要があるため、決めた NIC を経路ごとに内部で保持します。

**Related**

[Q-002](#q-002-受信側の参加nicを表す設定)、[Q-005](#q-005-受信側のdst_addrだけがある経路の扱い)

### Q-004 受入試験の範囲

**Status**

解決済み

**Question**

この案件の受入条件に含める試験の範囲を、どこまでにしますか。

**Context**

BL-001 の発端は、PSK と証明書の Windows/WSL 相互接続で、12 組み合わせ中マルチキャストの 2 件だけが受信できなかったことです。単体テストでは、参加 NIC の決定、送信元の照合、返送先を確認できますが、別ホスト間の実際の受信は確認できません。相互接続試験には Windows と WSL の両方でのビルドと `porter-test` の実行が必要です。ビルドと実行はユーザーが担当する運用です。

**Answer**

選択方式: 単一選択

- [x] A: (推奨) 単体テストに加え、ユーザーの環境で PSK と証明書の Windows/WSL 相互接続 12 組み合わせを再実施し、全件成功を受入条件にします。
- [ ] B: 単体テストと、マルチキャスト 2 件だけの Windows/WSL 相互接続試験を受入条件にします。
- [ ] C: 単体テストだけを受入条件とし、相互接続試験は別途行います。
- [ ] D: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

**Recommendation**

推奨: A  
理由: 過去 backlog の恒久対応案は「PSK/証明書それぞれの Windows/WSL 試験を再実施」としています。マルチキャストの変更が、同じ受信スレッドを通る他の通信種別に影響していないことも同時に確認できます。

回答者: Tetsuo Honda (2026-11-15 22:09:07 JST、検討用 Markdown の保存日時)

**Assessment**

A の選択を確認しました。受入条件は、単体テストの成功と、ユーザーの環境での PSK と証明書の Windows/WSL 相互接続 12 組み合わせの全件成功です。相互接続試験の実行はユーザーが担当し、エージェントは試験用の設定例と手順を用意します。

### Q-005 受信側の dst_addr だけがある経路の扱い

**Status**

解決済み

**Question**

マルチキャストのサービス定義で、ある経路に `dst_addrN` (受信側の参加 NIC) があり、`src_addrN` (送信側のアドレス) がない場合、どう扱いますか。

**Context**

[Q-002](#q-002-受信側の参加nicを表す設定) と [Q-003](#q-003-参加nicを省略したときの動作) の決定で、経路 N の受信側は「送信元の照合と返送先に `src_addrN`、参加 NIC に `dst_addrN` (省略時は `src_addrN`)」を使います。`src_addrN` がない経路は、送信元の照合と NACK の返送先が決まらないため、経路として成立しません。

今の実装は、マルチキャストもほかの UDP 種別も、`src_addr` が空の最初の経路で列挙を終え、それより後ろの設定を黙って使いません ([api_open_paths_udp.c](../../../prod/libsrc/porter/api/api_open_paths_udp.c#L264-L269))。unicast では、`src_addrN` と `dst_addrN` のどちらかが空の経路で列挙を終えます ([api_open_paths_udp.c](../../../prod/libsrc/porter/api/api_open_paths_udp.c#L207))。設定の読み込みと open のどちらにも、経路ごとの `src_addr` と `dst_addr` の組み合わせを検査する処理はありません。

```jsonc
// 判断対象の例: 経路 2 に dst_addr2 だけがある
"2010": {
  "type": "multicast",
  "src_addr1": "192.168.1.10",
  "dst_addr1": "192.168.1.20",
  "dst_addr2": "10.0.0.20",   // src_addr2 がない
  "dst_port": 6010,
  "multicast_group": "239.0.0.10"
}
// 案 A: 経路 1 だけで開く (dst_addr2 は使わない。今の列挙の規則と同じ)
// 案 B: POTR_ERR_INVALID_ARGUMENT で open に失敗する
```

**Answer**

選択方式: 単一選択

- [x] A: (推奨) 今の列挙の規則に合わせ、`src_addrN` が空の経路で列挙を終え、`dst_addrN` だけの経路は使いません。`config.md` にこの規則を明記します。
- [ ] B: 受信側の open を `POTR_ERR_INVALID_ARGUMENT` で失敗させます。
- [ ] C: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

**Recommendation**

推奨: A  
理由: ほかの通信種別と同じ列挙の規則になり、マルチキャストだけ設定の検査を増やさずに済みます。B は設定の誤りに気付きやすくなりますが、送信側と受信側で検査が異なり (送信側は `dst_addr` を使わない)、ほかの通信種別にも同じ検査が必要かという別の論点が生じます。

回答者: Tetsuo Honda (検討用 Markdown の保存を 2026-11-15 22:20 JST ごろに対話で確認)

**Assessment**

A の選択を確認しました。経路は `src_addrN` が空の最初の経路で列挙を終え、`dst_addrN` だけの経路は使いません。この規則を `config.md` に明記します。Q-001 から Q-005 の決定は互いに矛盾せず、観点別の確認もすべて確認済みとなりました。

**Related**

[Q-002](#q-002-受信側の参加nicを表す設定)、[Q-003](#q-003-参加nicを省略したときの動作)

### Q-006 CI の不定期失敗テストを本案件の範囲に加えるか

**Status**

解決済み

**Question**

CI run 43727490184 (attempt 1) の ol9 で失敗した `handleConcurrencyTest.close_waits_for_running_callback_and_rejects_callback_send` の調査と対策を、本案件の範囲に加えますか。

**Context**

BL-001 の実装をプッシュした後の CI で、`unicast_bidir_n1` の結合テストが `POTR_ERR_DISCONNECTED` (-20) で 1 回失敗しました。本案件の変更とは関係しない不定期な失敗として、案件 backlog の BL-001 に記録していました。同じ CI の attempt 2 では、別の不定期失敗 (mock_cplat の並列ビルド、案件 backlog の BL-002) も起きています。

**Answer**

選択方式: 単一選択

- [x] A: 案件 backlog の BL-001 (handleConcurrencyTest) を本案件で調査・対策します。BL-002 (mock_cplat の並列ビルド) は対象外です。
- [ ] B: 本案件の範囲に加えず、別案件にします。
- [ ] C: その他

その他の回答 / 補足:

回答者: Tetsuo Honda (2026-11-16 の対話で「BL-001 は本案件で調査対策したい」と回答。対話からの転記)

**Assessment**

本案件の範囲に、案件 backlog BL-001 の調査と対策を加えます。BL-002 は、ユーザーが本案件の対象として挙げていないため対象外のままとし、扱いは別途確認します。調査の結果と対策方針の選択は [Q-007](#q-007-handleconcurrencytestの接続待ちの直し方) で確認します。

**Related**

[Q-007](#q-007-handleconcurrencytestの接続待ちの直し方)、案件 backlog `261115-porter-multicast-src-contract-backlog.md` の BL-001

### Q-007 handleConcurrencyTest の接続待ちの直し方

**Status**

解決済み

**Question**

`handleConcurrencyTest` の接続待ちの不足を、どの範囲で直しますか。

**Context**

調査の結果、原因はテスト側にあると判断しました。

- `unicast_bidir` のクライアントは、相手が返した PING 応答を受けて初めて CONNECTED になります ([config.md](../../config.md#L121))。サーバー (`unicast_bidir_n1`) は、クライアントの PING を受けた時点でそのピアを CONNECTED にします。そのため、サーバー側の CONNECTED が、クライアント側より先になります。
- 準備処理 `open_n1()` ([handleConcurrencyTest.cc](../../../test/src/integration/porterSendRecvTest/handleConcurrencyTest.cc#L244-L263)) は、サーバー側の `s_connected_peers` だけを待ちます。
- 失敗したテスト (同ファイル 556 行目) は、`open_n1()` の直後にクライアントから送信します。クライアントがまだ CONNECTED でなければ `POTR_ERR_DISCONNECTED` になります。
- 同じ `open_n1()` を使う `n1_callback_supports_send_and_disconnect` は、`open_n1()` の後に `wait_for([] { return s_client_connected; })` でクライアントの CONNECTED を待っているため、この競合がありません (同ファイル 461 行目)。
- `open_n1()` を使う残り 1 件 (`n1_disconnect_peers_while_sending`、クライアント 4 つ) は、クライアントから送信しないため影響を受けません。
- 今のクライアント側の記録は `bool s_client_connected` の 1 つだけで、クライアントが複数あるときに全員分を数えられません。

製品コードの挙動 (CONNECTED になる順序) は `config.md` の仕様どおりであり、変更は不要と判断しています。

どちらの案もテスト コードだけの変更です。通常運用の N:1 では、N 側のクライアントが随時接続・切断し、全クライアントが接続済みである必要はありません。その仕様は変えません。待ち合わせるのは、テストの準備で開いたクライアント (`open_n1()` の `n_clients` 個) が、サーバー側とクライアント側の両方で通信を確立したことだけです。`max_peers` (上限 8) までの接続を待つわけではありません。

```cpp
// 案 A: open_n1() で、全クライアントの CONNECTED も待つ (クライアントの CONNECTED 数を数える)
void client_callback(int64_t, potr_peer_id, potr_event event, const void *, size_t)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (event == POTR_EVENT_CONNECTED)
    {
        s_client_connected = true;
        ++s_client_connected_count;
    }
    else if (event == POTR_EVENT_DISCONNECTED)
    {
        s_client_connected = false;
        --s_client_connected_count;
    }
    s_cv.notify_all();
}
// open_n1() の末尾
ASSERT_TRUE(wait_for([n_clients] { return s_client_connected_count == n_clients; }));
```

```cpp
// 案 B: 失敗したテストだけに、461 行目と同じクライアントの CONNECTED 待ちを加える
ASSERT_NO_FATAL_FAILURE(open_n1(19936, 1, gated_callback));
ASSERT_TRUE(wait_for([] { return s_client_connected; }));
```

**Answer**

選択方式: 単一選択

- [x] A: (推奨) `open_n1()` で、サーバー側に加えて全クライアントの CONNECTED も待つようにします。クライアントの CONNECTED 数を数える変数を追加します。
- [ ] B: 失敗したテストだけに、クライアントの CONNECTED 待ちを加えます。
- [ ] C: その他
- [ ] X: 意図不明 (詳細な質問内容に再生成すること)

その他の回答 / 補足:

**Recommendation**

推奨: A  
理由: `open_n1()` の完了が「両側が送信できる状態」を意味するようになり、今後 `open_n1()` の直後にクライアントから送るテストを追加しても同じ競合が起きません。変更はテスト ファイル 1 つに収まります。B は変更が最小ですが、同じ競合を各テストで個別に避ける必要が残ります。

回答者: Tetsuo Honda (2026-11-16 に検討用 Markdown へ記入。回答前に対話で「テストの準備にあたってのみ、1:N の全数が通信確立していることを待ち合わせる」という解釈を確認済み)

**Assessment**

A の選択を確認しました。`handleConcurrencyTest.cc` の `open_n1()` で、サーバー側の `s_connected_peers` に加えて、クライアント側の CONNECTED 数が `n_clients` に達するまで待ちます。数は `client_callback()` の CONNECTED で増やし、DISCONNECTED で減らします。テストごとに 0 から数えるため、`SetUp()` で 0 に戻します。既存の `s_client_connected` は他のテストが使うため残します。製品コードと通常運用の N:1 の仕様は変えません。追加範囲の論点はこれで解決し、最終仕様への追記に進みます。

**Related**

[Q-006](#q-006-ciの不定期失敗テストを本案件の範囲に加えるか)

## 対象成果物と合意版

最終決定事項 (最終仕様) の設計書: `261115-porter-multicast-src-contract-design.md`

検討用 Markdown の合意版 (第 1 ゲート): 版 3 (2026-11-15)

最終仕様 (design.md) の合意版 (第 2 ゲート): 2026-11-15 22:25:00 JST 版を条件付きで承認 (他セッションからの進行許可を受けてから作業を開始)

更新予定の成果物: 作業 WBS の 4 の内訳のとおり

受入条件: 単体テストの成功と、ユーザーの環境での PSK と証明書の Windows/WSL 相互接続 12 組み合わせの全件成功 ([Q-004](#q-004-受入試験の範囲))

## 反映と検証の記録

2026-11-15 に、実装を grok CLI (`grok-4.7`、reasoning effort high、`--yolo`) へ委譲し、claude code が差分を最終仕様と照合しました。

- 変更: `config.md`、`porter_type.h`、`potr_context.h`、`api_open_paths_udp.c/.h`、`potr_service_close.c`、`porter-services.jsonc`、単体テスト 3 ファイル。機能仕様、自動生成物、公開構造体の形は変更していません。
- 検証: grok が Windows/MSVC で `apiOpenPathsUdpTest` (22 件)、`potrServiceCloseTest` (11 件)、`configLoadServiceTest` (44 件) の `make test` を実行し、失敗 0 でした。`config.md` は `text_style_jp.py` の確認済みです。追加範囲の `handleConcurrencyTest.cc` は Windows/MSVC で `porterSendRecvTest` 36 件成功、`handleConcurrencyTest` 10 件を 5 回繰り返して失敗 0 でした。CI run 43835974193 (親 `fbbc1d848a`、porter `5ff9609`) で、`ebb79c2` と `8220519` を含む全ジョブ (Windows、ol8、ol9、ol10、docs、verify-gen、deploy-pages、warnings-summary) の成功を確認しました。
- 未実施: Windows/WSL 相互接続 12 組み合わせ (ユーザーの環境で実施)。2026-11-18 に実施しました (下記)。

2026-11-18 に、ユーザーの指示 (「WSL 側と相談して自動でできないか」「ビルドもあなたが実施して良い」) で、claude code が Windows/WSL 相互接続 12 組み合わせを自動実施しました。

- 対象の版: Windows は porter `0436424`、cplat `19302982` (2026-11-18 05:44 にビルド)。WSL (Oracle Linux 8、NAT) は porter `ad93ffbe`、cplat `19302982` の作業ツリーで、バイナリは porter `a382a1d` の時点のビルドです。`a382a1d` から `0436424` までの差分に `prod` 配下の変更はありません。
- 方式: Windows 側の Python スクリプト 1 本が、`wsl.exe` 経由で WSL の `porter-test` (receiver) を起動し、続けて Windows の `porter-test.exe` (sender) を起動します。sender の標準入力へ `send <トークン>` を書き、receiver の出力にトークンが現れれば成功とします。`unicast_bidir` は逆方向 (WSL から Windows) も確認します。
- アドレス: Windows `172.28.208.1`、WSL `172.28.209.184`。サービス ID は 78100 から 78111、ポートは 59000 番台です。
- マルチキャストの設定: 送信側と受信側で同じ定義を使い、`src_addr1` を Windows、`dst_addr1` を WSL の参加 NIC にしました (最終仕様の受入条件のとおり)。

```json
{
  "type": "multicast",
  "src_addr1": "172.28.208.1",
  "src_port": 59132,
  "dst_addr1": "172.28.209.184",
  "dst_port": 59133,
  "multicast_group": "239.255.111.112"
}
```

| 認証 | unicast | unicast_bidir | unicast_bidir_n1 | tcp | tcp_bidir | multicast |
| --- | --- | --- | --- | --- | --- | --- |
| PSK | 成功 | 成功 (双方向) | 成功 | 成功 (2 回目) | 成功 (2 回目) | 成功 |
| 証明書 | 成功 (2 回目) | 成功 (双方向) | 成功 | 成功 | 成功 | 成功 (2 回目) |

Table: Windows/WSL 相互接続 12 組み合わせの結果 (2026-11-18)

- 「2 回目」は、1 回目の `send` を sender が「未接続のため送信できません」で拒否し、1 秒後の送り直しで届いた組み合わせです。送り直しを入れる前の実行では、接続の確立前に送信した 5 件が同じ理由で失敗しました。これは試験スクリプトが送信を始める時機の問題で、接続の確立後の送信はすべて届いています。
- 証明書の `unicast` と `multicast` では、receiver が起動の約 1 秒後に `certificate: authentication failed: handshake failed or peer rejected certificate` の警告を 1 回出し、その後に接続が確立しました。sender の起動前に行われた認証の試行とみられますが、原因は確認していません。
- Windows のファイアウォールの設定は変更していません。
- スクリプト、生成した設定、組み合わせごとのログは作業用の一時領域にあり、リポジトリでは管理していません。
