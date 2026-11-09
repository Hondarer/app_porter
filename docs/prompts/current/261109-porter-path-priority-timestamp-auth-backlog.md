<!--
生成元: agree-before-implement スキル
文書種別: 残課題一覧 (backlog)
最終更新エージェント: claude code / セッション ID: session_01KJc53U2rrJbqpruzwVQpnp
上流文書: 261109-porter-path-priority-timestamp-auth-design.md (最終仕様)、261109-porter-path-priority-timestamp-auth-discussion.md (検討用 Markdown)。いずれも削除されている場合があります。
再開手順: このファイルを開いたエージェントは、作業の前に agree-before-implement スキルを読み込んでください。「整理状態」が「随時追記中」であれば、案件の作業中です。気付いた事項を追記し、第 3 ゲートの前に文書を再構築して整理します。上流文書が存在する場合は、各課題の背景と見送りの判断を上流文書で確認します。存在しない場合は、この文書を単体の残課題一覧として扱います。このファイルの残課題を参照して新たな成果物の作成または更新を始めるときは、元案件の再開とは別の作業として扱います。その開始には agree-before-implement を原則として使い、新しい検討用 Markdown から開始してください。継続作業によってこのファイルの残課題を対策する場合は、このファイルや各項目の記載を削除することなく、各項目の状態および経緯と対処を更新することを原則とします。また、フェーズ分けした対策で対処ができていない項目がある場合は、当該継続作業の backlog にも必ず転記し、最新作業の backlog を見れば残件が把握できるようにしてください。最終更新エージェントのセッション情報が参照可能であれば再開時のコンテキストとして利用し、別エージェントからの引き継ぎや担当変更によりセッションが見つからない場合も異常とせず、文書の記載から継続してください。
-->

# 残課題一覧 (バックログ)

## 案件の識別

案件名: `porter-path-priority-timestamp-auth`  
作成日: `2026-11-09`  
整理状態: 随時追記中

この文書は、見送る決定がなされた問題点や、作業中に潜在または別問題として判明した問題点を記録します。  
作業期間中は気付いた時点で随時追記し、作業の全完後に再構築して整理します。

## 残課題一覧

| 課題 ID | 題名 | 区分 | 優先度 / 影響度 | 状態 |
| --- | --- | --- | --- | --- |
| BL-001 | 暗号化有効時に PING が同じ nonce を再使用する | 作業中摘出 | 高 | 未着手 |
| BL-002 | max_payload の検査上限がヘッダー長を含まない | 作業中摘出 | 低 | 未着手 |

## BL-001 暗号化有効時に PING が同じ nonce を再使用する

### 概要と問題点

- 現状の事象: PING は `seq_num` に送信側の `next_seq` を格納し、通番を消費しません。AES-256-GCM の nonce は `session_id` + `flags` + `seq_num` で構成するため、DATA を送らない間の PING は同じ nonce を使い続けます。PING のペイロード (経路の受信状態) は送出時点で変わり得るため、同じ鍵と nonce で異なる内容を保護する場合があります。
- 本来あるべき姿: 同じ鍵の下で、異なる内容またはヘッダーを保護するパケットは、すべて異なる nonce を使います。

### 経緯と見送り理由

- 摘出契機: 版 2 の技術レビューで、送信時刻の追加が nonce に与える影響を調べる過程で、`potr_health_thread.c` と `porter_const.h` の nonce 構成から判明しました。porter `1b24a89` の取り込み後も PING の `seq_num` は `next_seq` のままで、TCP では DATA の前に経路状態の PING を書き込む処理が加わったため、発生の機会が増えています。
- 今回の見送り理由: 見送りません。検討用 Markdown の Q-023 で A (送出通番による nonce) が選ばれたため、本案件の実装で解消する予定です。解消を確認した時点で、整理時に「解消」として除外します。
- 継続作業での経緯と対処: (未記入)

### 影響範囲とリスク

- 影響対象: 暗号化を有効にしたサービスの PING 送信 (`prod/libsrc/porter/thread/potr_health_thread.c`)、nonce の定義 (`prod/include/porter/porter_const.h`)。
- 放置した場合のリスク: AES-GCM で nonce を再使用すると、改ざん検出用の内部鍵を推定され、偽造パケットを受理する危険があります ([NIST SP 800-38D](https://csrc.nist.gov/pubs/sp/800/38/d/final))。同じ nonce で暗号化した平文同士の排他的論理和も観測できます。

### 推奨される対応策

- 恒久対応案: 送出ごとに増える送出通番をヘッダーに加えて nonce に使うか、PING にも独立した通番を割り当てて nonce の一意性を保証します。あわせて、nonce の一意性を検証する単体テストを追加します。
- 参考情報: 検討用 Markdown の Q-023、`prod/libsrc/porter/thread/potr_health_thread.c`、`prod/include/porter/porter_const.h` の `POTR_FLAG_ENCRYPTED` と `POTR_CRYPTO_NONCE_SIZE`。

## BL-002 max_payload の検査上限がヘッダー長を含まない

### 概要と問題点

- 現状の事象: `potr_service_open.c` は `max_payload` を `POTR_MAX_PAYLOAD` (65,507、UDP の最大ペイロード) 以下か検査します。porter のヘッダー (版 1 で 40 バイト) を加えると、65,468 以上の設定で UDP データグラムが最大長を超え、送信に失敗します。
- 本来あるべき姿: 検査の上限は、UDP の最大ペイロードからヘッダー長を引いた値とします。

### 経緯と見送り理由

- 摘出契機: 最終仕様の第 4 版の作成中に、ユーザーからペイロード上限の設定可否を確認された際、`porter_const.h` と `potr_service_open.c` の検査から判明しました。
- 今回の見送り理由: 見送りません。最終仕様で、版 2 のヘッダー長 72 バイトを引いた 65,435 を上限とすることにしました。本案件の実装で解消を確認した時点で、整理時に「解消」として除外します。
- 継続作業での経緯と対処: (未記入)

### 影響範囲とリスク

- 影響対象: `prod/include/porter/porter_const.h` の `POTR_MAX_PAYLOAD`、`prod/libsrc/porter/api/potr_service_open.c` の検査、`prod/libsrc/porter/protocol/packet.c` の長さ検査。
- 放置した場合のリスク: 上限近くの `max_payload` を設定すると、起動は成功しても DATA の送信が失敗します。既定値 1,400 では発生しません。

### 推奨される対応策

- 恒久対応案: `POTR_MAX_PAYLOAD` をヘッダー長から導出する定義に改め、境界値の単体テストを追加します。
- 参考情報: 最終仕様の「ワイヤー形式」の節、`docs/config.md` の `max_payload` の行。
