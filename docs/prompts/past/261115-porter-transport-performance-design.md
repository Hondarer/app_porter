<!--
生成元: agree-before-implement スキル
文書種別: 最終仕様 (design)
最終更新日時: 2026-11-17 04:33:00 JST
最終更新エージェント: Codex (Grok の作業管理とレビュー)
稼働 OS: Windows
上流文書: `261115-porter-transport-performance-discussion.md` (検討用 Markdown、合意版: 版 2、実行記録・追加判断: 版 3〜6)、`261115-porter-reject-recovery-backlog.md` (BL-001、BL-002)。削除されている場合があります。
下流文書: `261115-porter-transport-performance-validation.md` (作業中の検証記録)
再開手順: この案件は 2026-11-17 にユーザー指示で打ち切りました。この計画に従って作業を再開しないでください。やり直しの案件は `261117-porter-transport-performance-redo-discussion.md` です。BL-001 の実装は cplat と porter のブランチ `fix/bl001-tcp-nodelay` にあり、`main` には入っていません。以下は打ち切り前の再開手順の記録です。このファイルを開いたエージェントは、作業の前に `.agents/skills/agree-before-implement/SKILL.md` を読み込んでください。2026-11-16 にユーザーがこの詳細対処計画を承認し、`grok --yolo high` による実行を指示したため、第 2 ゲートは通過しています。承認だけを実行許可と読まず、この実行指示の範囲であるステージ 0 と BL-001 に限って実装と局所テストを進めます。BL-002 の製品改修、Git の書き込み、全体テストは指示の範囲外です。進行位置は上流の検討用 Markdown の作業 WBS を確認してください。
-->

# porter の TCP 遅延と送信・通知性能の改善 詳細対処計画

ファイル名: `261115-porter-transport-performance-design.md`

## 本計画の位置づけと全体方針

この計画は、`porter` の TCP 約 40[ms] 停止 (BL-001) と非ブロッキング小メッセージ・圧縮送信・N:1 外部返信の性能低下 (BL-002) を対象とする最終対処計画です。  
本計画の承認 (第 2 ゲート) は調査・評価・実装計画の確定を意味するものであり、本承認のみをもって実測、試作実装、製品コード変更、ビルド、または Git 書き込みを自動着手するものではありません。

BL-001 は `app/cplat` への汎用 `TCP_NODELAY` API 追加と `porter` 全 TCP 接続での常時有効化を確定方針として定めます。  
一方、BL-002 は未測定の仮説に基づく先行実装を行わず、プロファイル計測によって主因と寄与率を定量化した後に、個別改善案の採否、数値目標、および実装設計を **計測後の別判断として確定** します。

実行後の状態: BL-001 の実装・局所検証・P4/P5 測定は完了し、ユーザーは Q-006 A で TCP_NODELAY 常時適用と測定した throughput 低下を受容しました。BL-002 の wrap 計測は原因帰属に使えません。ユーザーは Q-007 B を選びましたが、低負荷方式の設計・pilot はユーザー指示で一時中断しています。Grok の初回方式案に対する Sol のレビューと pilot 用ソース作成・ビルド・測定は未実施です。実測の詳細は [検証記録](261115-porter-transport-performance-validation.md) に記録しています。

> [!IMPORTANT]
> 本計画に含まれる BL-002 の改善候補は、コード調査から導いた仮説に基づく条件付き提案です。  
> 実測前に各施策の改善率や効果量を断定せず、プロファイル結果で寄与率が裏付けられた施策だけを採用判断の対象とします。

## 対象範囲と維持すべき安全契約

### 対象範囲と対象外範囲

Table: 本計画の対象範囲と対象外範囲

| 区分 | 対象 | 内容 |
| --- | --- | --- |
| 対象 (BL-001) | `app/cplat` ソケット層 | 汎用 `TCP_NODELAY` 設定 API の追加、両 OS 実装、逆引き文書・エクスポート・`mock_cplat`・単体テストの同期 |
| 対象 (BL-001) | `app/porter` TCP 接続・受け入れ処理 | SENDER/RECEIVER・`TCP`/`TCP_BIDIR`・全パスでの初回通信以前の設定と失敗時警告トレース |
| 対象 (BL-002) | 計測・原因特定・A/B 評価 | 両 OS での非計装本測定、8 観点のプロファイル・区間計測、1 施策単位の A/B 評価計画 |
| 対象外 | `porter` 公開 API・設定構造体 | `prod/include/porter/` の関数追加、`potr_global_config` / `potr_service_def` への `TCP_NODELAY` 設定項目追加は行わない |
| 対象外 | `cplat` 既定挙動の変更 | `cplat_socket_open()` や `cplat_socket_accept()` で `TCP_NODELAY` を暗黙有効化せず、他利用者の既定を維持する |
| 対象外 | 未計測の推測先行改修 | プロファイル取得前の BL-002 製品コード変更、および新規課題の自動合流は行わない |

### 高速化において維持すべき公開契約と安全性

BL-001 および将来の BL-002 改善施策のいずれにおいても、現行 `porter` の安全契約を後退させないことを必須条件とします。

- **参照管理とハンドル寿命安全性**: `potr_handle.c` が保証する公開ハンドルの非再利用、`POTR_ERR_INVALID_HANDLE` 判定、および `potr_service_send()` と `potr_service_close()` 同時呼び出し時の参照カウント (`refs`) 待機安全性を維持します。
- **通知順序とコールバック安全性**: `potr_notify.c` の単一通知スレッドによる FIFO 配送順、`potr_recv_fn` コールバック内からの `potr_service_send()` (ブロッキング含む) 呼び出し可否、フラグメント連続性、スレッド内送信順序を崩しません。
- **`close` 安全性と資源回収**: `POTR_ERR_DISCONNECTED`、`POTR_ERR_CANCELED`、`POTR_ERR_OUT_OF_MEMORY` の戻り値契約と、open/close 反復 (P4) におけるハンドル数・スレッド数の傾き 0 を維持します。
- **メモリ消去契約**: 平文データ、鍵、および一時圧縮バッファーに対する `cplat_secure_zero()` の範囲を変更または縮小する施策候補を検討する場合は、機密保持方針とコーディング規範に照らして個別の設計レビューと明示的合意を経るものとし、無条件に省略しません。

## BL-001: TCP_NODELAY 常時有効化の確定方針

### 適用範囲と設定契約

`porter` はすでにプロトコル層のまとめ送信パラメーター `pack_wait_ms` (既定値 `POTR_DEFAULT_PACK_WAIT_MS = 0U`) を備えているため、ソケット層の Nagle アルゴリズムによる約 40[ms] の不規則な遅延を OS スタックへ委ねる必要がありません。  
そのため、`porter` がデータ・制御通信に使用する接続確立済み TCP ソケットすべてに対して `TCP_NODELAY` を常時有効 (`enable = 1`) にします。  
対象には次の通信形態をすべて含めます。

- 通信種別: `POTR_TYPE_TCP` および `POTR_TYPE_TCP_BIDIR` の双方
- 接続方向: SENDER の `connect` / 再接続、および RECEIVER の `accept` / 再受け入れの双方
- 経路: 単一パスおよびマルチパスの全 `path_idx` (`0 .. n_path - 1`)

公開設定構造体 (`potr_global_config`、`potr_service_def`) への設定追加は行わず、`pack_wait_ms` によるアプリケーション層バッチ制御の仕様も変更しません。

### cplat と porter の責務分担および cplat 同期対象

プラットフォーム固有の `setsockopt` 呼び出し、ヘッダー (`<netinet/tcp.h>` / Winsock2)、型変換、およびエラー変換 (`cplat_error`) は `app/cplat` が担当し、用途に応じたオプション適用方針は `porter` が担当します。  
`app/cplat` の既存オプション API (`cplat_socket_set_reuse_address()`、`cplat_socket_set_broadcast()`) と同じ設計を踏襲し、汎用オプション API を追加します。

- **追加 API (案)**: `CPLAT_EXPORT int CPLAT_API cplat_socket_set_nodelay(cplat_socket sock, int enable, cplat_error *detail_out);`
- **内部実装**: `socket_linux.c` (`<netinet/tcp.h>` を追加) および `socket_windows.c` の既存共通ヘルパー `set_int_option(sock, IPPROTO_TCP, TCP_NODELAY, value, detail_out)` を呼び出し、`enable != 0` なら `1`、`0` なら `0` を設定します。
- **他利用者への非干渉**: `cplat_socket_open()` や `cplat_socket_accept()` の内部挙動は変更しないため、`cplat` を利用する他の app の既定ソケット挙動には影響しません。

`app/cplat/AGENTS.md` の公開 API 変更規則に従い、実装時には次の同期対象を同一変更内で更新します。

Table: cplat の汎用 TCP_NODELAY API 追加に伴う同期対象一覧

| 同期対象 | パス | 更新内容 |
| --- | --- | --- |
| 公開ヘッダー | `app/cplat/prod/include/cplat/net/socket.h` | `cplat_socket_set_nodelay()` の宣言、戻り値 (`CPLAT_OK` / `CPLAT_ERR_INVALID_ARGUMENT` / `CPLAT_ERR_UNKNOWN`)、スレッド セーフ契約の Doxygen コメントを追加 |
| プラットフォーム実装 | `app/cplat/prod/libsrc/cplat/net/socket_linux.c`、`socket_windows.c` | `IPPROTO_TCP` / `TCP_NODELAY` の実装を追加 (`socket_linux.c` は `<netinet/tcp.h>` をインクルード) |
| API 逆引き文書 | `app/cplat/docs/api-cheatsheet.md` | ソケット操作 API 表へ `setsockopt(TCP_NODELAY)` と `cplat_socket_set_nodelay(...)` の対応行を追加 |
| 機能仕様整合確認 | `app/cplat/docs/functional-spec/net.md` | 既存要件 `CPLAT-NET-FUNC-005` および「初期化と設定」との整合を確認し、記述補足の要否を点検 |
| エクスポート テスト | `app/cplat/test/src/libcplatTest/exportTest/exportTest.cc` | `CPLAT_EXPORT_TABLE_COMMON` へ `cplat_socket_set_nodelay` のエントリを追加 |
| モック (`mock_cplat`) | `app/cplat/test/include/mock_cplat.h`、`test/libsrc/mock_cplat/mock_cplat.cc`、`test/libsrc/mock_cplat/net/mock_cplat_socket_set_nodelay.cc` | `create-mock-cplat-mock` 手順に従い `MOCK_CPLAT_LINK_IMPL`、実委譲関数、`MOCK_METHOD`、`ON_CALL` を追加 |
| 単体テスト | `app/cplat/test/src/libcplatTest/net/socketTest/socketTest.cc` | TCP ソケットでの有効化 (`1`)・無効化 (`0`) の正常系、および `CPLAT_INVALID_SOCKET` 等を渡す異常系テストを追加 |

### 接続確立時の設定位置と初回通信以前への配置

`listen()` 側ソケットに設定した `TCP_NODELAY` が `accept()` 済みソケットへ継承されるかは OS 差異を伴う可能性があるため、継承に依存せず接続確立ソケットごとに明示設定します。  
また、TLS 証明書のハンドシェイク (`POTR_AUTH_CERT`) や最初のパケット送出を含む初回通信より前に設定を完了させます。

Table: porter 側の TCP_NODELAY 設定位置とタイミング

| 接続経路 | 対象ファイルと関数 | 設定タイミング | 網羅される範囲 |
| --- | --- | --- | --- |
| SENDER (`connect` / 再接続) | `app/porter/prod/libsrc/porter/thread/thread_connect_prepare.c` `thread_connect_with_timeout()` | `cplat_socket_open(CPLAT_SOCKET_TCP, &sock, &detail)` 成功後、`cplat_socket_connect()` による通信開始前 (既存 `cplat_socket_set_reuse_address()` 設定の直後。段階 0 で接続前指定の有効性を OS 資料で確認し、必要な場合は接続確立直後・TLS ハンドシェイク前へ調整) | `POTR_TYPE_TCP` / `POTR_TYPE_TCP_BIDIR` の SENDER 初回接続、再接続、全 `path_idx` |
| RECEIVER (`accept` / 再受け入れ) | `app/porter/prod/libsrc/porter/thread/potr_connect_thread.c` `receiver_accept_loop()` | `cplat_socket_accept()` 成功後、接続元フィルター (`thread_connect_peer_allowed()`) 通過後、かつ最初のパケット先読み (`thread_connect_read_first_packet()`) および TLS ハンドシェイク (`potr_internal_certificate_connect()`) より前 | `POTR_TYPE_TCP` / `POTR_TYPE_TCP_BIDIR` の RECEIVER 初回受け入れ、再受け入れ、全 `path_idx` |

> [!NOTE]
> `POTR_TYPE_TCP_BIDIR` では SENDER と RECEIVER の双方で `thread_connect_with_timeout()` または `receiver_accept_loop()` が動作し、双方の送信スレッドがデータおよび制御パケット (`PING` / `FIN` / `FIN_ACK`) を送出します。  
> `connect` 側と `accept` 側の双方に配置することで、双方向通信の両端で片方向遅延の残存を防ぎます。

### 設定失敗時のベスト エフォート方針と porter 公開契約の維持

`TCP_NODELAY` の設定は性能最適化であり、通信のデータ整合性やセキュリティ境界を変更しません。  
そのため、`cplat_socket_set_nodelay()` が失敗した場合でもソケットを閉じて接続確立を中断せず、警告トレースを記録した上で接続処理を継続します。

- 失敗時は `POTR_TRACE_SOCKET_FAILURE(CPLAT_TRACE_LEVEL_WARNING, &detail, ...)` により、`service_id`、`path_idx`、および `cplat_error` の詳細情報を出力します。
- 既存の `SO_REUSEADDR` 設定が `(void)` で戻り値を無視しているのに対し、`TCP_NODELAY` は失敗を観測可能にするため警告ログの出力を必須とします。
- `porter` 側の公開 API (`prod/include/porter/`) の追加や関数シグネチャ変更は発生しないため、`POTR_EXPORT_FUNCTION_TABLE`、`app/porter/docs/api.md`、および `app/porter/docs/functional-spec/` の変更は不要です。

## BL-002: 原因特定のプロファイル計画と条件付き改善候補

### プロファイル対象 8 観点と調査目標

BL-002 で観測された非ブロッキング小メッセージ (-23.6%〜-34.5%)、圧縮 4 スレッド (-26.5%〜-72.3%、通知遅延 p99 最大約 8 倍)、Linux N:1 外部スレッド返信 (RTT 約 1.5 倍、件数 -31.3%) の劣化原因を切り分けるため、次の 8 観点を両 OS でプロファイル計測します。

Table: BL-002 の 8 観点プロファイル対象と観測指標

| No. | プロファイル観点 | 対象コード箇所 | 主に観測する指標 |
| --- | --- | --- | --- |
| 1 | CPU 時間内訳 | 送信・受信・通知スレッド全体 | スレッド別・関数別の占有率 (ユーザー空間 vs カーネル空間) |
| 2 | 動的メモリ確保 | `potr_service_send.c` (`cplat_malloc` / `cplat_free`)、`potr_notify.c` (ノード確保) | アロケーター関数の CPU 比率、確保回数、ページ フォルト回数 |
| 3 | 全量消去 | `potr_service_send.c` (`cplat_secure_zero(compress_buf, ctx->compress_buf_size)`)、`potr_notify.c` (`cplat_secure_zero(node + 1, node->bytes)`) | `cplat_secure_zero` の CPU 占有率、実効バイト数 (64B 送信時の 65KiB 超全量消去の影響) |
| 4 | 圧縮・展開とコピー | `potr_service_send.c`、`thread_recv_slot.c` (`compress_buf_size` 上限固定、送信〜通知間の多段コピー) | 圧縮・展開処理時間、圧縮スキップ率、コピー帯域消費 |
| 5 | 通知キュー管理 | `potr_notify.c` (`potr_internal_notify_enqueue()` の `q->mutex` 下 `free_nodes` 線形探索) | `q->mutex` 保持時間と競合回数、1 回あたり探索ノード数、断片化有無 |
| 6 | ロック競合 | `potr_handle.c` (`s_mutex` / `entry->mutex` の 1 呼び出しあたり計 3 回取得・解放) および各経路ロック | ロック取得待機時間、競合率、4 スレッド時のスケーリング阻害度 |
| 7 | 起床とコンテキスト スイッチ | `potr_send_queue.c` (`push_locked()` / `complete_locked()` の無条件 `condvar` 通知)、`potr_notify.c` (`notify_thread`) | `WakeConditionVariable` / `futex` 呼び出し回数、コンテキスト スイッチ (CSWITCH) 回数 |
| 8 | N:1 外部スレッド返信 | `potr_send_thread.c` (`send_packed_peer_mode()`)、`potr_recv_thread.c` (`n1_handle_packet()`) | `ctx->peers_mutex` 保持時間と相互ブロッキング、`ext` と `cb` の区間差分、ベンチマーク側中継キュー経費 |

### 区間計測による通知遅延の分解方針

プロファイル計測とは別または計装調査モードとして、通知遅延 (`notify_p50` / `notify_p99`) と N:1 往復時間 (`RTT`) を次の区間へ分解して取得します。

1. `potr_service_send()` 入口 〜 圧縮完了 〜 `send_queue` 投入完了 (API 側区間)
2. `send_queue` 投入完了 〜 `send_thread` 取り出し 〜 ソケット送出完了 (送信スレッド区間)
3. ソケット受信完了 〜 復号・展開完了 〜 通知キュー投入完了 (受信スレッド区間)
4. 通知キュー投入完了 〜 `notify_thread` 取り出し 〜 `potr_recv_fn` コールバック入口 (通知配送区間)

### 仮説に基づく条件付き改善候補 (設計確定は計測後)

プロファイルの結果に応じて、次の改善候補を条件付きで検討します。  
いずれの候補も段階 2 の寄与率測定を経て採否および詳細設計を別判断で確定するものとし、事前の効果断定は行いません。

Table: BL-002 の条件付き改善候補と採用・安全条件

| 対象観点 | 条件付き改善候補 | 採用検討の条件 (トリガー) | 設計確定時に検証すべき安全制約 |
| --- | --- | --- | --- |
| 確保・全量消去・圧縮 | 圧縮一時バッファーの必要サイズ縮小 (入力長相応の割り当て)、スレッド ローカル/プール再利用、消去対象範囲の精査 | 圧縮 4 スレッドで `cplat_malloc` / `cplat_secure_zero` / ページ フォルトが支配的と判明した場合 | 圧縮出力上限 (`CPLAT_COMPRESS_HEADER_SIZE` 等) の不足防止、並行呼び出し非干渉、機密保持契約の維持 |
| 送信キュー起床 | `push_locked()` / `complete_locked()` での待機者有無または状態遷移 (空→非空、満杯→空き、待機チケット存在) に応じた条件付き `condvar` 通知 | `WakeConditionVariable` / `futex` システム コールが通常送信または 4 スレッドで高寄与を示した場合 | `pop` / `push_wait` / `wait_ticket` / `abort` における取りこぼし (lost wakeup) 不在の証明 |
| 通知キュー | `free_nodes` のサイズ別バケット化または探索長制限、`cplat_secure_zero` の要否/範囲見直し | `q->mutex` 待機や線形探索コスト、通知回収コストが p50/p99 増加の主因と判明した場合 | FIFO 通知順序の維持、OOM 時の予約済み `error_notice` 動作維持、機密保持契約 |
| ハンドル管理ロック | 読み取り側 (`acquire` / `release`) のロック回数削減 (世代・状態と参照カウントの原子操作化、またはテーブル ロック縮小) | `s_mutex` / `entry->mutex` が 4 スレッド通常送信のボトルネックと判明した場合 | `close` 待機との競合ゼロ、`POTR_ERR_INVALID_HANDLE` と非再利用保証の維持 |
| N:1 外部スレッド返信 | `ctx->peers_mutex` の保持区間短縮 (暗号封緘と `sendto` の分離、またはピア参照保護の細粒度化) | Linux P2 (`ext` vs `cb`) で `peers_mutex` 競合が主因と判明した場合 (ベンチマーク中継経費と分離後) | ピア解放・再割り当て (`potr_internal_peer_free`) との競合・UAF 防止、ロック順序不変 |

## 測定ベースライン、条件、分離規則、および成果物保管

### 測定対象固定版と新旧基準の明確な区別

既存の性能検証記録 (`261115-porter-handle-lifecycle-perf-validation.md` および `.csv`) は、`porter-reject-recovery` 完了前の旧基準版 (`改修前版`: `porter 9e55a0c` / `cplat 9f3c304`、`旧現行版`: `porter 6e9002c` / `cplat fd903dc1`) を測定した記録です。  
本計画で用いる **新基準版** は、`porter-reject-recovery` 完了後の固定コミット (親リポジトリ `721131ef`、`app/porter 11016c1`、`app/cplat 822dc7da` を候補とし、測定開始時に固定する HEAD) とします。  
旧基準版と新基準版の混同を防ぐため、CSV および検証記録には必ず固定版ラベル、コミット ハッシュ、および差分ハッシュを併記します。

すべての測定結果セットには次のメタデータを対にして記録します。

- **固定コミット ハッシュ**: 親リポジトリ、`app/porter`、`app/cplat`、`app/cjson` の各完全または短縮コミット ハッシュ
- **差分ハッシュ**: 基準コミットからの追跡ファイルの差分を SHA-256 で記録します。新規・未追跡ファイルは別途一覧と内容の SHA-256 を記録し、測定プログラムと設定ファイルも同様に識別します (対象ソースに差分がない場合だけ `clean`)
- **依存ライブラリ版**: `zlib` と `OpenSSL` の固定コミットまたはビルド済みライブラリ版・結合形態
- **コンパイラと環境版**: Windows 11 / MSVC バージョン、Oracle Linux Server 8.10 (WSL2) / GCC バージョン / glibc バージョン
- **入力データ仕様**: 圧縮試験におけるペイロード生成パターンの圧縮性と固定シード

### 元測定条件の再現仕様

過去の検証記録との連続性を保つため、コンパイル フラグおよび負荷条件は元測定の基準を再現します。

Table: 測定ビルド時のコンパイル フラグ仕様

| 区分 | Windows/MSVC | Linux/GCC |
| --- | --- | --- |
| 製品 C ソース | `/std:c17 /O2 /MD /DNDEBUG /W4` と静的結合用定義 | `-O2 -DNDEBUG -fno-omit-frame-pointer -g -fPIC -fvisibility=hidden` と C17 指定 |
| 測定プログラム (C++) | `/std:c++17 /O2 /MD /DNDEBUG /EHsc` | `-std=c++17 -O2 -DNDEBUG -fno-omit-frame-pointer` |

- **実行方式**: 両 OS ともローカル ループバック (`127.0.0.1`) を使用し、1 プロセス内で 5 回反復します。各実行先頭のウォーム アップ (1000 件、P4 は 10 サイクル) を集計から除外し、5 回分の最小・中央値・最大を記録します。
- **負荷条件**: P1/P5 は 64 / 5000 バイト (通常・圧縮・ブロッキング・圧縮+ブロッキング、1 / 4 スレッド)、各スレッド 10000 件、同時未完了はスレッドあたり 16 件とします。P2 はクライアント 4 本が 64 / 5000 バイト各 5000 件 (`cb` と `ext`)、P3 は 1 件 1[ms] 待機・5000 バイト 10000 件・間隔なし、P4 は open/close 1000 サイクル (UDP / TCP) とします。
- **入力圧縮性の固定**: `POTR_SEND_COMPRESS` 条件では、入力データの圧縮率によって `cmp_len < len` 分岐および実際の送出サイズが変わるため、元測定と同一の圧縮性を持つ固定パターンを使用し、テストごとに圧縮前・圧縮後バイト数を記録します。

### 比較不能条件・不安定条件の除外と P3 再検証

元測定で判明済みの比較不能条件および過負荷不安定条件は、本計画の合否・性能比較対象から明確に除外します。

Table: 比較不能条件と不安定条件の取り扱い規則

| 条件 | 除外または特別扱いの理由 | 本計画での扱い |
| --- | --- | --- |
| 旧改修前版 Windows P5 5000B・圧縮なし 4 条件 | 旧改修前版の既知不具合 (`send buffer full, packet skipped`) により完了しない | 旧改修前版との比較対象から除外し、新基準版 (`TCP_NODELAY` off/on) を起点に比較 |
| 旧改修前版 Linux P5 `TCP_BIDIR` | 旧改修前版の close 停止不具合により計測不能 | 同上。新基準版を起点に比較 |
| P1 5000B・通常・4 スレッド (UDP 過負荷) | Linux UDP 受信バッファー (212,992 バイト) を大きく上回り、既定負荷 900 秒打ち切りや縮小負荷での 2 桁ばらつきが生じる | 定常スループットの比較・合否判定対象から除外し、過負荷観測専用の参考枠とする |
| P3 低速コールバック (Windows) | 旧現行版 (`6e9002c`) では 5 回中 4 回 10000 件欠落していたが、`porter-reject-recovery` (`68ab091` / `11016c1`) により解消済み | 旧現行版との単純数値比較は行わず、新基準版および BL-001/BL-002 変更後に「全試行で受信が途切れず再開すること」とメモリ挙動を再検証する |

### BL-001 と BL-002 の比較組み合わせ・計装分離・同時測定禁止

測定の交絡と干渉を防ぐため、次の 3 つの運用規則を厳守します。

1. **BL-001 単独比較と BL-002 比較条件の一致**: 段階 1 では新基準版 (`TCP_NODELAY` off) と BL-001 適用版 (`TCP_NODELAY` on) を単独で比較します。段階 2 以降に BL-002 の施策を TCP 条件 (P5) で評価する場合は、比較対象の双方とも `TCP_NODELAY` on に統一した状態で比較し、UDP 条件 (P1〜P3) の評価と合わせて改善効果を切り分けます。
2. **非計装本測定と計装調査の分離**: スループット、API 遅延、通知遅延 (p50/p95/p99)、およびプロセス CPU 時間を確定する本測定は、プロファイラー、区間計測フック、および過剰な `POTR_TRACE` 出力を外した非計装構成で 5 回測定します。ETW/WPR・`perf`・区間タイムスタンプ・パケット キャプチャ (`tcpdump` / `ss`) を有効にした計装調査は別実行として実施し、計装実行の値を本測定の性能表に混ぜません。
3. **Windows / Linux (WSL2) 同時測定禁止**: 同一物理マシンの CPU とキャッシュを共有するため、Windows 側と WSL2 側で同時にビルドや測定を実行することを禁止し、OS ごとに交互実行します。

### 成果物とプロファイル データの保管仕様

各測定段階で生成される成果物は種別ごとに管理し、再現に必要なデータと一時的トレースを分離して保管します。

Table: 測定成果物とプロファイル データの保管方針

| 成果物種別 | 内容 | 保管形式と保管先の方針 |
| --- | --- | --- |
| 本測定 CSV | 非計装 5 回実行の最小・中央値・最大、OS、版ラベル、固定コミット、差分ハッシュ、入力圧縮性指標 | `app/porter/docs/prompts/current/` 配下へ新規検証記録と対で保存 |
| 区間計測要約 | API 入口〜送出〜受信〜通知キュー〜コールバック入口の区間別時間分布 | 新規検証記録 Markdown または要約 CSV として保存 |
| ビルド・実行警告ログ | ビルド時の `.warn` ファイル確認結果、および P1〜P5・`TCP_NODELAY` 失敗確認時の警告ログ抜粋 | 検証記録へ要約を記載。生の実行ログは作業領域へ保管 |
| プロファイル要約・トレース | WPR/ETW・`perf` によるスレッド/関数別 CPU 比率、ロック待機、CSWITCH、パケット時刻列 | 集計表と知見を検証記録へ記載。大容量の生トレース (`.etl` / `perf.data` / `.pcap`) はリポジトリ外の一時作業領域へ保管 |

## ステージ別実行計画と異常時分岐

計画承認後、別途作業の実行指示を受けて実施する各ステージの入力、操作、出力、終了条件、および異常・未再現・環境不足時の分岐は次のとおりです。

framework または複数 app にまたがる全体テストを実施する場合は、対象と所要コストを説明し、ユーザーの確認を得ます。  
各段階では、変更対象に対応する局所テストから検証します。

### ステージ定義

Table: ステージ別の入力・操作・出力・終了条件

| ステージ | 入力 | 主な操作 | 出力 | 終了条件 |
| --- | --- | --- | --- | --- |
| ステージ 0: 仕様確認と基準固定 | 本計画書、新基準版 HEAD (`porter 11016c1` / `cplat 822dc7da`)、Linux/Windows 公式資料 | Linux `tcp(7)`/`socket(7)` と Microsoft Learn で `TCP_NODELAY`・遅延 ACK・`connect` 前設定の有効性を一次確認する。測定環境・入力圧縮性・コンパイラ版・ベースライン ハッシュを固定記録する | 公式仕様確認メモ、基準環境仕様記録 | 公式仕様の前提に未確認がなく、両 OS の新基準ビルド環境が確定していること |
| ステージ 1: BL-001 実装・単独 A/B 評価 | ステージ 0 確認結果、BL-001 確定仕様 | `cplat` 汎用 API・同期対象一式および `porter` 接続/受け入れ処理へ `TCP_NODELAY` を実装する。両 OS 局所テストと警告試験を実施し、非計装 P4 (TCP)・P5 で `off` vs `on` の 5 回測定と必要時のパケット時刻確認を行う | `cplat`/`porter` 変更差分、局所テスト結果、BL-001 A/B 本測定 CSV (`.warn` 確認含む) | 両 OS で局所テスト全成功、内容のある `.warn` 0 件、P4/P5 での約 40[ms] 停止消失と副次影響の有無が確認済みであること |
| ステージ 2: BL-002 プロファイル・区間計測調査 | BL-001 適用済み基準版、8 観点プロファイル項目 | 計装分離のもとで Windows (WPR/ETW) と Linux (`perf`) のプロファイル、区間計測、および N:1 (`ext` vs `cb`) 切り分けを取得し、主因と寄与率を定量化する | スレッド別/関数別 CPU 要約、ロック競合・CSWITCH・確保統計、区間計測表 | 8 観点のうち支配的ボトルネックと寄与率が両 OS で特定され、BL-002 設計判断材料がそろっていること |
| ステージ 2.5: BL-002 設計確定と数値目標の別判断ゲート | ステージ 2 のプロファイル結果と安全性制約 | 主因と寄与率に基づき、どの改善候補を採用するか、改善後の具体的な数値目標・許容コストをどう定めるかをユーザーと別途合意し、BL-002 の実装設計を確定する | BL-002 実装設計と数値目標の合意記録 | BL-002 の実装対象施策と受入数値基準がユーザー合意により確定していること (現状維持・見送り決定も含む) |
| ステージ 3: BL-002 段階的改善と A/B 本測定 | ステージ 2.5 で承認された改善設計 | 承認された施策を 1 要素ずつ (または寄与分離可能な単位で) 実装し、安全性単体テスト追加と非計装 5 回 A/B 本測定 (`TCP_NODELAY` 双方 on、UDP/TCP) を交互実行する | 施策ごとのコード差分、単体テスト結果、非計装 A/B 本測定 CSV | 承認された全施策について独立効果と安全契約維持が確認されていること |
| ステージ 4: 総合検証と完了整理 | 全変更適用版、本測定 CSV 群 | 両 OS での P1〜P5 総合測定 (P3 再検証含む)、`make test`、`.warn` 0 件確認、知見の正本・コメント反映、検証記録と残課題一覧の整理を実施する | 総合検証記録 Markdown、確定 CSV、更新済み文書・コメント、残課題一覧 | 全テスト成功、`.warn` 0 件、P3 再開・P4 傾き 0 確認、第 3 ゲート提示条件の充足 |

### 失敗・事象未再現・環境不足時の分岐規則

- **失敗時の分岐 (ビルド失敗・テスト失敗・安全契約違反・P3 停止)**: 当該施策の進行を直ちに停止し、失敗原因が実装不具合か設計前提の誤りかを切り分けます。ハンドル寿命・通知順序・メモリ消去・`close` 安全性を損なう施策は採用せず、安全な代替案または見送りとして再判断します。
- **事象未再現時の分岐 (約 40[ms] 停止や劣化が新基準版で再現しない場合)**: 基準コミット、差分ハッシュ、コンパイル フラグ、入力圧縮性、および `TCP_NODELAY` 有効状態を再点検します。条件が正確であるにもかかわらず事象が再現しない場合は、推測で改修を進めず、再現しなかった実測値を報告してユーザーへ対象見直しを確認します。
- **環境不足時の分岐 (ETW/perf 権限不足・カーネル制限・ファイアウォール未許可)**: Windows の WPR/ETW や `tcpdump` に管理者権限が不足する場合、または WSL2 でカーネル シンボルやハードウェア カウンターが制限される場合は、ユーザーへ権限・設定を依頼するか、ユーザー空間の精密区間計測・許容されるソフトウェア サンプリングへ切り替えます。ファイアウォール未許可による通信失敗データは集計から除外し、許可後に再測定します。環境不足で取得できない指標がある場合は、推測で補完せず未取得として明記します。

## 数値目標の意思決定位置づけと受入条件

### 数値目標の扱い

性能改善に関する具体的な数値目標 (例: スループット何 % 回復、遅延何 % 削減) は、**本計画時点では未合意** です。  
未計測の段階で仮の目標値を設定して合意済みの体裁を取ることはせず、ステージ 1 およびステージ 2 の実測値とプロファイル寄与率が確定した後の別判断ゲート (ステージ 2.5) において、安全契約と実装コストを踏まえてユーザーと合意します。

### 今回の計画策定の受入条件 (第 2 ゲート〜第 3 ゲート)

今回の作業 (計画文書の確定) に対する受入条件は次のとおりです。

1. BL-001 について、`cplat` 汎用 API 追加、他利用者への既定非変更、`porter` 全 TCP 接続の初回通信以前での常時有効化、および設定失敗時の警告継続が漏れなく具体化されていること。
2. BL-002 について、8 観点のプロファイル対象、非計装分離、Windows/WSL2 同時測定禁止、比較不能条件除外、新旧基準分離、成果物保管、および計測後の別判断による設計確定が明記されていること。
3. 未測定の効果断定や未合意の数値目標の先取り記載がなく、ステージ別の入力・操作・出力・終了条件と異常時分岐が定義されていること。
4. 変更対象 3 ファイル (`261115-porter-transport-performance-discussion.md`、`261115-porter-transport-performance-design.md`、`261115-porter-reject-recovery-backlog.md`) だけが更新・作成され、実測・実装・ビルド・Git 書き込みを行っていないこと。
5. 変更 3 ファイルのすべてが `text_style_jp.py --dry-run` 確認後に `--in-place` 整形され、日本語技術文書の規範を満たしていること。

### 将来の実測・実装工程における受入条件の枠組み

計画承認後、別途作業の実行指示を受けて進む将来工程の受入条件枠組みは、ステージ 2.5 での数値目標確定を前提に次の項目で構成します。

- **BL-001 確認**: Linux P4 (TCP close) および P5 で観測されていた約 40[ms] 級の停滞が解消され、両 OS の単体・結合テスト (設定失敗時の警告トレース確認を含む) が全件成功すること。
- **BL-002 確認**: ステージ 2.5 で合意した対象施策および判定基準 (数値目標または許容判断) を満たし、公開契約、ハンドル寿命、メモリ消去、通知順序、`close` 安全性、P3 受信再開、P4 資源傾き 0 に回帰がないこと。
- **ビルド・品質確認**: Windows/MSVC と Linux/GCC の双方でビルド、`make test`、および `cplat`/`porter` の関連テストが全件成功し、対象範囲の内容のある `.warn` ファイルが 0 件であること。

## 永続的知見の正本とコード コメントへの反映計画

将来の実測・実装工程が完了した段階で、恒久的に必要な仕様・設計判断だけを厳選して次の反映先へ残します。

Table: 永続的知見の反映計画一覧

| 反映対象 | 反映する恒久知見の内容 | 反映タイミング |
| --- | --- | --- |
| `app/cplat/prod/include/cplat/net/socket.h`、`app/cplat/docs/api-cheatsheet.md` | `cplat_socket_set_nodelay()` の API 契約と `setsockopt(TCP_NODELAY)` 対応関係 | ステージ 1 (BL-001 実装時) |
| `app/cplat/docs/functional-spec/net.md` | 既存要件 `CPLAT-NET-FUNC-005` との整合確認および必要時の補足更新 | ステージ 1 (BL-001 実装時) |
| `app/porter/prod/libsrc/porter/thread/thread_connect_prepare.c`、`potr_connect_thread.c` | `TCP_NODELAY` を接続・受け入れ双方で初回通信以前に常時設定する意図、ベスト エフォート警告継続の根拠、および確認済み OS 仕様の `see: <URL>` | ステージ 1 (BL-001 実装時) |
| `app/porter/` の検証記録 Markdown・本測定 CSV | 環境・版・差分ハッシュを固定した実測結果と測定限界 (正本 `docs/` へは環境依存の数値を書かない) | ステージ 1 / 3 / 4 |
| BL-002 改善箇所のコード コメント・設計文書 | ステージ 2.5 で採用が確定した構造変更に限り、ロック順序や消去範囲の恒久仕様を記載 (作業中の試行錯誤や暫定禁止事項は含めない) | ステージ 3 / 4 |
