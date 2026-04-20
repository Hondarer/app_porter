# プロトコル仕様

## ネットワーク層

- **トランスポート**: UDP/IPv4（unicast / multicast / broadcast / unicast_bidir）または TCP/IPv4（tcp / tcp_bidir）
- **IPv6**: 非対応
- **バイトオーダー**: すべてのワイヤーフォーマットはビッグエンディアン (ネットワークバイトオーダー、NBO)

## パケット構造

すべてのパケットは共通のヘッダー (36 バイト) とペイロード (0〜1,400 バイト) で構成されます。
UDP データグラム 1 個が外側パケット 1 個に対応します。

### 外側パケット (PotrPacket)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 service_id (int64) [高位 32 bit]              |  8バイト
+               -+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 service_id (int64) [低位 32 bit]              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 session_tv_sec (int64) [高位 32 bit]          |  8バイト
+               -+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 session_tv_sec (int64) [低位 32 bit]          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         session_id (uint32)                   |  4バイト
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      session_tv_nsec (int32)                  |  4バイト
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         seq_num (uint32)                      |  4バイト
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         ack_num (uint32)                      |  4バイト
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           flags (uint16)      |       payload_len (uint16)    |  4バイト
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                  payload (0〜1,400 バイト)                    |
|                         ...                                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**ヘッダー固定長**: 36 バイト

| フィールド | 型 (NBO) | 説明 |
|---|---|---|
| `service_id` | int64 | サービス識別子。受信時に設定ファイルの値と照合する |
| `session_tv_sec` | int64 | セッション開始時刻 (POSIX 秒) |
| `session_id` | uint32 | セッション乱数識別子 |
| `session_tv_nsec` | int32 | セッション開始時刻 (ナノ秒部、0〜999,999,999) |
| `seq_num` | uint32 | パケット通番。各セッション開始時に 0 からカウント |
| `ack_num` | uint32 | NACK・REJECT 時: 対象通番。PING 要求時: 0（固定）。PING 応答時: 対応する要求の `seq_num`。DATA 時: 0。FIN 時: `POTR_FLAG_FIN_TARGET_VALID` が立っている場合のみ受信完了目標 `next_seq` |
| `flags` | uint16 | パケット種別フラグ (下記参照) |
| `payload_len` | uint16 | ペイロード長 (バイト) 。0〜1,400 |
| `payload` | 可変長 | ペイロードデータ (最大 1,400 バイト) |

### パケット種別フラグ (flags)

| 定数 | 値 | 説明 |
|---|---|---|
| `POTR_FLAG_DATA` | 0x0001 | データパケット。ペイロードにエレメントを含む |
| `POTR_FLAG_NACK` | 0x0002 | 再送要求。`ack_num` に要求する通番を格納 |
| `POTR_FLAG_PING` | 0x0004 | ヘルスチェック要求。ペイロードにパス受信状態 (`POTR_MAX_PATH` バイト) を格納する |
| `POTR_FLAG_REJECT` | 0x0008 | 再送不能通知。`ack_num` に再送不能な通番を格納 |
| `POTR_FLAG_FIN` | 0x0010 | 正常終了通知。ペイロードなし (payload_len=0) |
| `POTR_FLAG_FIN_TARGET_VALID` | 0x0040 | FIN の `ack_num` が有効な受信完了目標 `next_seq` を表すことを示す |
| `POTR_FLAG_FIN_ACK` | 0x0080 | TCP close 完了通知。`ack_num` に確認対象の FIN target を格納 |

### flags 種別ごとの ack_num の意味

| `flags` | `ack_num` の意味 |
|---|---|
| `POTR_FLAG_DATA` | `0`（固定、無視） |
| `POTR_FLAG_NACK` | 再送要求の通番 |
| `POTR_FLAG_PING`（要求） | `0`（固定） |
| `POTR_FLAG_PING`（応答） | 対応する要求の `seq_num` |
| `POTR_FLAG_REJECT` | 再送不能の通番 |
| `POTR_FLAG_FIN` | `POTR_FLAG_FIN_TARGET_VALID=1` のとき送信側 `send_window.next_seq`、未設定時は無視 |
| `POTR_FLAG_FIN_ACK` | TCP close 完了を確認した `FIN.ack_num` |

`ack_num` の意味はパケット種別（`flags`）ごとに独立しているため、NACK / REJECT パケットでの `ack_num` 使用とは競合しません。

### TCP ストリームからのパケット読み取り

UDP はデータグラム境界がパケット境界と一致しますが、TCP はバイトストリームです。
受信側は次の 2 ステップでパケットを読み取ります。

```
① ソケットから 36 バイト読み取る（ヘッダー確定）
② ヘッダーの payload_len バイトを追加読み取る（ペイロード確定）
```

ヘッダーフォーマットが変わらないため、将来の TCP↔UDP ブリッジ実装を容易にします。

### TCP モードにおける ack_num の追加用途

UDP では `ack_num` は NACK / REJECT の対象通番として使用します。
TCP では NACK / REJECT が不要なため、**PING 要求と PING 応答の識別**にも使用します。

| `ack_num` の値 | 意味（TCP PING 時） |
|---|---|
| `0` | PING 要求（応答を求める） |
| `N`（N > 0） | PING 応答（`seq_num = N` の PING 要求への返答） |

DATA パケットでは `ack_num` は 0 固定とし、受信時は無視します。

### TCP モードでのパケット種別フラグ使用可否

| フラグ | TCP で使用するか | 理由 |
|---|---|---|
| `POTR_FLAG_DATA` | ○ | データ送信に使用 |
| `POTR_FLAG_NACK` | × | TCP が配送を保証するため不要 |
| `POTR_FLAG_PING` | ○ | ヘルスチェック。`ack_num=0` で要求、`ack_num=N` で応答 |
| `POTR_FLAG_REJECT` | × | 再送不能は発生しない |
| `POTR_FLAG_FIN` | ○ | `potrCloseService()` 時に protocol-level FIN を送る |
| `POTR_FLAG_FIN_ACK` | ○ | receiver が最後の DATA callback 完了後に返す close 完了通知 |
| `POTR_FLAG_ENCRYPTED` | ○ | UDP と同様に使用可能 |

### TCP マルチパスにおける重複排除

複数の TCP 接続（path）が確立されると、送信スレッドは全アクティブ path に同一 `seq_num` のパケットを送信します。
受信側は複数の path から同一パケットを受け取るため、重複排除が必要です。

UDP マルチパスで実装済みの `window_recv_push()` / `window_recv_pop()` をそのまま流用します。
複数の recv スレッドが同一の `recv_window` に並行してアクセスするため、
呼び出し前後を `recv_window_mutex` で保護します。

```
recv スレッド #i:
  recv_window_mutex を lock
  window_recv_push(recv_window, pkt)  → 重複なら FAIL を返す
  recv_window_mutex を unlock
  FAIL の場合: continue（コールバック呼び出しなし）

  recv_window_mutex を lock
  while window_recv_pop(recv_window, out) == SUCCESS:
    recv_window_mutex を unlock
    deliver_payload_elem(out)   ← コールバック呼び出し前に unlock
    recv_window_mutex を lock
  recv_window_mutex を unlock
```

**フラグメント化メッセージへの適用**:
フラグメントは外側パケット（コンテナ）単位で `seq_num` が付与されます。
`POTR_FLAG_MORE_FRAG` は内側ペイロードエレメントの管理に使うフラグであり、
パケット単位の重複排除ロジックとは干渉しません。UDP マルチパスと同一の動作をします。

**PING との機能競合**:
PING パケットは `window_recv_push()` を通さず、recv スレッド内で直接処理します。
したがって PING と重複排除ウィンドウは機能競合しません。

## ペイロードエレメント (パッキングコンテナ)

DATA パケットのペイロードには、1 つ以上のペイロードエレメントが連続して格納されます。
これを**パッキング**と呼び、複数の小さなエレメントを 1 つの UDP パケットにまとめて送信効率を高めます。

### エレメントのフォーマット

```
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|      elem_flags (uint16, NBO) |   2 バイト
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|            payload_len (uint32, NBO)          |   4 バイト
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          payload (payload_len バイト)         |   可変長
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**エレメントヘッダー**: 6 バイト固定

ヘッダーフィールド (`elem_flags`・`payload_len`) はネットワークバイトオーダー (NBO) で格納します。
`payload` 部はアプリケーションが `potrSend()` に渡したデータをそのままコピーしたバイト列であり、
porter はバイトオーダーの変換を行いません。エンディアン管理はアプリケーションの責務です。

### エレメントフラグ (elem_flags)

| 定数 | 値 | 説明 |
|---|---|---|
| `POTR_FLAG_MORE_FRAG` | 0x0001 | 後続フラグメントが存在する (フラグメント続行) |
| `POTR_FLAG_COMPRESSED` | 0x0002 | ペイロードが raw DEFLATE 圧縮済み |

### パッキングの動作

送信スレッドは送信キューから複数のエレメントを取り出し、1 つの DATA パケットにまとめます。
以下の条件に該当するエレメントは単体で送信されます (直前のパケットをフラッシュしてから送信) 。

- `POTR_FLAG_MORE_FRAG` フラグが立っているエレメント (フラグメントの中間)

## セッション識別

### 目的

送信者プロセスの再起動後に OS が同一の ephemeral ポートを再利用した場合や、
ネットワーク上に残留した前セッションのパケットが遅延して届いた場合に、
受信者がそれらを誤って処理しないようにします。

### セッション識別子の構成

送信者は `potrOpenService()` 呼び出し時に以下を決定し、以後のすべてのパケットに付与します。

| フィールド | 決定方法 |
|---|---|
| `session_id` | 乱数 (シード: 現在時刻 × PID) |
| `session_tv_sec` | `CLOCK_REALTIME` (Linux) または `GetSystemTimeAsFileTime()` (Windows) による現在時刻 (秒) |
| `session_tv_nsec` | 同上 (ナノ秒部) |

採用判定はタイムスタンプを同一送信者の 2 パケット間で比較するため、
送信者と受信者のクロック同期には依存しません。

### 受信者のセッション採用ポリシー

受信者は受信したパケットの識別子を、現在追跡中のセッションと比較します。

```
1. session_tv_sec が現在追跡中より大きい           → 新セッションとして採用
2. session_tv_sec が等しく session_tv_nsec が大きい → 新セッションとして採用
3. 上記が等しく session_id が大きい                → 新セッションとして採用 (タイブレーク)
4. それ以外                                        → 旧セッションのパケットとして破棄
```

新セッション採用時 (および FIN 受信・ヘルスチェックタイムアウト後の再接続時) は、
受信ウィンドウを **最初に受信したパケットの `seq_num`** で初期化します。
これにより、送信者が任意の通番から送信を再開した場合でも、NACK/REJECT サイクルなしに即時同期できます。

### クロック逆行時の挙動

OS クロックが手動設定や NTP スラムで過去に戻った場合、新セッションのタイムスタンプが
旧セッションより小さくなり、受信者のセッション採用ポリシー上は「旧セッション」と判定されます。
この状況での挙動は `health_timeout_ms` の設定により異なります。

**ヘルスチェックタイムアウト有効 (`health_timeout_ms > 0`)**

送信者が再起動すると旧セッションの PING が途絶えるため、`health_timeout_ms` 経過後に
ヘルスチェックタイムアウトが発火します。タイムアウト時は FIN 受信と同様に
`DISCONNECTED` イベントが発火し `peer_session_known` がクリアされます。
その後、次に到着したパケット (タイムスタンプが逆転した新セッションを含む) を
無条件に採用して `CONNECTED` イベントが発火します。
クロック逆行があっても `health_timeout_ms` の待機を伴う切断・再接続を経て自動回復します。

**ヘルスチェックタイムアウト無効 (`health_timeout_ms = 0`)**

`peer_session_known` がクリアされる契機がなく、旧セッションが追跡中のまま残ります。
タイムスタンプが逆転した新セッションのパケットは永続的に破棄されます。
これはヘルスチェック無効環境における既知の制限です。

### unicast_bidir N:1 モードのピア識別

`unicast_bidir` の N:1 モードでは、サーバは各受信パケットの **session triplet** (`session_id` + `session_tv_sec` + `session_tv_nsec`) を使ってピアを識別します。`recvfrom()` の送信元 IP:Port は、ピアへの返信先パスを学習するために使用し、一次識別キーには使いません。

| 識別手段 | 用途 |
|---|---|
| session triplet | ピアの検索・新規作成判定 |
| `recvfrom()` の送信元 IP:Port | 返信先アドレスの学習、パス追加 |
| `src_port` 設定 | N:1 サーバでの任意の送信元ポートフィルタ |

N:1 モードの各ピアは、以下の状態を独立して持ちます。

- 自セッション ID / 相手セッション ID
- `seq_num` 系列
- 送信ウィンドウ / 受信ウィンドウ
- フラグメント結合状態
- ヘルスチェック状態 (`health_alive`、最終受信時刻)
- 学習済み返信先パス

未知の session triplet を受信したとき、空きスロットがあれば新規ピアを作成して `POTR_EVENT_CONNECTED` を発火します。`max_peers` に達している場合はエラーログを残して当該パケットを破棄します。

## 通番管理

### 型と範囲

通番 (`seq_num`) は `uint32_t` の循環カウンタです。各セッション開始時に 0 から始まり、
最大値に達すると 0 に折り返します。

### 折り返し対応の比較

通番の新旧を比較する際は、単純な大小比較ではなく折り返しを考慮した比較を使用します。
差が `UINT32_MAX / 2` 未満であれば、a が b より後から送信された (新しい) と判断します。

### 通番の付与タイミング

通番は送信スレッドが DATA パケットを送出する際に付与します。
フラグメント化されたメッセージの各フラグメントはそれぞれ独立した通番を持ちます。
PING パケットは送信側の `next_seq` (次に割り当てる DATA の通番) を `seq_num` に格納しますが、通番を消費しません。

### RAW モードにおける通番の役割

RAW モード (`unicast_raw` / `multicast_raw` / `broadcast_raw`) でも通番はインクリメントされますが、
**再送制御には使用しません**。AES-256-GCM のノンス生成 (`session_id + seq_num`) にのみ使用します。
受信側では通番を使って順序整列とギャップ検出を行い、ギャップ検出時は NACK の代わりに
`POTR_EVENT_DISCONNECTED` を発行します。

### TCP モードにおける通番の役割

| 目的 | TCP での利用 |
|---|---|
| 再送制御（NACK ターゲット） | 不使用 |
| 順序整列 | 不使用（TCP が保証） |
| AES-256-GCM ノンス生成 | 使用（`session_id + seq_num`） |
| フラグメント順序確認 | 使用（デバッグ・ログ用途） |

## スライディングウィンドウ

> **TCP モードではスライディングウィンドウを使用しません。**
> TCP は順序保証・配送保証があるため、再送制御・順序整列のいずれも不要です。
> 以下の記述は UDP 通信種別（unicast / multicast 等）に適用されます。

### 目的

- **送信ウィンドウ**: NACK 受信時に再送するために、過去パケットのコピーを保持する
- **受信ウィンドウ**: 到着順序が入れ替わったパケット (追い越し) をバッファリングし、通番順に整列する

### ウィンドウサイズ

設定パラメータ `window_size` で指定します。デフォルト 16、最大 256。

### 送信ウィンドウの動作

送信スレッドは DATA パケットを送出するたびに送信ウィンドウに登録します。
PING パケットはウィンドウに登録されません (NACK・再送の対象外) 。
ウィンドウが満杯になると、最も古いエントリを **evict** (削除) して循環利用します。
evict された通番に対して後から NACK が届いた場合、そのパケットは再送不能となり REJECT を返します。

### 受信ウィンドウの動作

受信スレッドは届いたパケットを受信ウィンドウに格納します。
期待する通番より大きい通番のパケット (追い越し) もウィンドウ内に保持します。
ウィンドウの先頭から連続した通番のパケットが揃ったところで、上位層へ順番に渡します。

### 欠番検出と NACK 送出 (通常モード)

受信ウィンドウが、期待する通番より大きい通番のパケットを受け取った場合、欠番が生じています。
受信スレッドは欠番となっている通番を特定し、送信者へ向けて NACK を送出します。

### 欠番検出と即切断 (RAW モード)

RAW モードでは NACK を送出しません。欠番を検出した場合 (DATA の追い越し到着・PING の `seq_num` が
`recv_window.next_seq` より前方にある場合) の動作:

1. `POTR_EVENT_DISCONNECTED` をコールバックで通知する
2. 受信ウィンドウを到着パケットの通番でリセットする
3. 到着パケットを配信し、`POTR_EVENT_CONNECTED` をコールバックで通知する

追い越しパケット自体は破棄されません。ギャップをまたいで届いた最初のパケットが即座に配信されます。

> **`reorder_timeout_ms` との組み合わせ**: `reorder_timeout_ms > 0` の場合、欠番を検出しても
> 即座に DISCONNECTED を発行せず、指定時間だけ待機します。待機中に欠落パケットが届いた場合は
> 通常どおり順序整列して配信します (DISCONNECTED は発行されません)。
> タイムアウトを経過してなお欠落のままであれば DISCONNECTED を発行してセッションをリセットします。

## フラグメント化と結合

### フラグメント化 (送信側)

1 回の `potrSend()` で送信するデータが `max_payload` を超える場合、複数のフラグメントに分割します。
圧縮を指定した場合は圧縮後のデータをフラグメント化します。

各フラグメントは個別のペイロードエレメントとして送信キューに積まれ、
後続フラグメントが存在する場合は `POTR_FLAG_MORE_FRAG` フラグを立てます。
最終フラグメントには `POTR_FLAG_MORE_FRAG` を立てません。

### フラグメント結合 (受信側)

受信スレッドは `POTR_FLAG_MORE_FRAG` フラグを監視し、フラグメントをバッファに蓄積します。
`POTR_FLAG_MORE_FRAG` がないエレメントを受け取った時点でフラグメント結合が完了し、
完全なメッセージとしてコールバックを呼び出します。

フラグメントバッファのサイズは最大 65,535 バイトです。

## 再送制御

> **TCP モードでは再送制御（NACK / REJECT）を使用しません。**
> TCP 自身が信頼性のある配送を保証するためです。
> 以下の記述は UDP 通信種別に適用されます。

### 方式

通常モードは **NACK のみ方式** (ACK なし) を採用します。
通常時は ACK を返さないためスループットが高く、問題発生時のみ受信側から通知します。

RAW モードでは再送制御を行いません。送信ウィンドウへの登録は行いますが、受信側から NACK が届いても無視します。
到達保証が不要で低遅延・ベストエフォートを優先する用途に適します。

### 再送フロー

```
1. 受信者が欠番を検出する
   ├─ reorder_timeout_ms == 0: 即 NACK を送出する
   └─ reorder_timeout_ms > 0: タイムアウトまで待機
       ├─ 待機中に欠落パケット到着: 正常配信 (NACK 不要)
       └─ タイムアウト経過: NACK を送出する
          ※ multicast/broadcast 通常モードではジッタを付加 (詳細は後述)
2. 受信者が欠番の通番を NACK パケットで送信者へ送出する
3. 送信者が NACK を受信し、該当通番のパケットを送信ウィンドウから検索する
4a. 該当パケットが存在する → 全パスへ再送する
4b. 該当パケットが存在しない (evict 済み) → REJECT パケットを返す
```

再送パケットはマルチキャスト・ブロードキャスト時でも全受信者へ送信されます。
これにより、他の受信者も再送パケットの恩恵を受けられます。

### NACK タイムアウトジッタ (マルチキャスト/ブロードキャスト)

`multicast` / `broadcast` 通常モードかつ `reorder_timeout_ms > 0` の場合、複数の受信者が同一欠番の NACK を同時に送出すると送信者への NACK が集中する (NACK implosion)。

これを回避するため、受信者ごとに `reorder_timeout_ms` の 100%〜200% のランダムな待機時間を設定します。各受信者のタイマーが独立した時刻に期限を迎えることで、NACK が時間軸上に分散されます。

```
受信者 A の実効タイムアウト: reorder_timeout_ms + jitter_A  (100%〜200%)
受信者 B の実効タイムアウト: reorder_timeout_ms + jitter_B  (100%〜200%)
受信者 C の実効タイムアウト: reorder_timeout_ms + jitter_C  (100%〜200%)
```

ジッタは monotonic クロックのナノ秒部を乱数源として計算し、外部 RNG への依存はありません。
`unicast` 通常モード・RAW モード全種別にはジッタを付加せず、`reorder_timeout_ms` をそのまま使用します。

### NACK 重複抑制

マルチキャスト・ブロードキャスト環境では、複数の受信者が同じ通番の NACK を短時間に送る場合があります。
また、同一受信者が再送の到着を待たずに同じ NACK を複数回送る場合もあります。

送信者は直近 8 エントリのリングバッファで NACK 受信履歴を管理し、
同一通番の NACK が **200ms** 以内に再度届いた場合は処理をスキップします。

### REJECT の役割

送信者が REJECT を返す状況は、受信者が送信ウィンドウから evict 済みの通番を要求した場合です。

受信者は REJECT を受信すると：

1. 直ちに `POTR_EVENT_DISCONNECTED` を発火する
2. 欠落した通番をスキップし、次の通番から処理を再開できる状態になる
3. 次のパケットが届いた時点で `POTR_EVENT_CONNECTED` を発火する

## FIN と DATA の順序保証

### 問題の背景

UDP では `sendto()` の完了は「OS のネットワークバッファへの書き込み完了」を意味し、
受信側への到達を保証しません。送信者が最後の DATA を送出した直後に `potrCloseService()` を呼ぶと、
FIN パケットが最後の DATA より先に受信側へ届く場合があります。

受信側が FIN を先に受け取ると、即 `POTR_EVENT_DISCONNECTED` を発火して受信ウィンドウをリセットするため、
後着の DATA パケットが捨てられます。

### 解決策: FIN target の有無を flag で区別する

送信者は FIN 送出時に、現セッションで DATA を 1 件以上送っている場合だけ
`POTR_FLAG_FIN_TARGET_VALID` を立てて `ack_num` へ **送信側 `send_window.next_seq`** を格納します。
DATA を 1 件も送っていない場合は `POTR_FLAG_FIN_TARGET_VALID` を立てず、`ack_num` は無視されます。

`send_window.next_seq` は「次に送る DATA に割り当てる通番」であるため、
最後の DATA が `seq=N` なら `FIN.ack_num = N + 1` になります。
wrap 後は `FIN.ack_num = 0` も通常の有効値です。`ack_num = 0` を no-data sentinel としては扱いません。

受信側は `POTR_FLAG_FIN_TARGET_VALID` が立っている FIN かつ `recv_window.next_seq` がその値に未到達の場合、
FIN 処理を **ペンディング** にしてセッションをリセットしません。
後着の DATA がウィンドウを通過し、`recv_window.next_seq == FIN.ack_num` になった時点で
DISCONNECTED を発火します。

```
受信側の FIN 受信時の判定:
  FIN_TARGET_VALID == 0             → 即 DISCONNECTED (no-data FIN)
  FIN_TARGET_VALID == 1
    recv_window.next_seq == ack_num → 即 DISCONNECTED (ウィンドウ追い付き済み)
    recv_window.next_seq != ack_num → pending_fin = true (DATA 待ち)
                                      後続 DATA pop / skip のたびに再判定
```

### ペンディング中のエッジケース

FIN をペンディング中に期待する DATA が届かない場合、以下の既存機構がフォールバックとして機能します。

| シナリオ | 解消パス |
|---|---|
| 欠番 DATA が後着した | 正常に `drain_recv_window()` → `pending_fin` 解消 |
| 送信者が NACK に REJECT で応答 | `window_recv_skip()` → `drain_recv_window()` → `pending_fin` 解消 |
| `reorder_timeout_ms` タイムアウト後 | `window_recv_skip()` → 同上 |
| `health_timeout_ms` タイムアウト | ヘルスチェックが `DISCONNECTED` を発火（既存フォールバック） |

ペンディング中はセッション状態をリセットしないため、欠番 DATA は引き続き受け入れられます。
NACK / REJECT サイクルも通常どおり動作します。

詳細は [FIN 待ち合わせ仕様](fin-ordering.md) を参照してください。

## 圧縮

### 方式

**raw DEFLATE** (RFC 1951) を使用します。

| プラットフォーム | 実装 |
|---|---|
| Linux | zlib (windowBits = -15) |
| Windows | Compression API (MSZIP \| COMPRESS_RAW) |

両プラットフォームは同じ raw DEFLATE フォーマットを出力するため、
Linux 送信者と Windows 受信者 (またはその逆) の組み合わせで透過的に動作します。

### 圧縮データのフォーマット

圧縮ペイロードの先頭 4 バイトには、展開後のサイズを NBO の uint32 で格納します。

```
[展開後サイズ (uint32, NBO, 4 バイト)] [圧縮データ (可変長)]
```

受信者はこのサイズ情報をもとに展開バッファを準備します。

### 圧縮の適用単位

圧縮は `potrSend()` の `flags` 引数に `POTR_SEND_COMPRESS` を指定することで制御します。
圧縮はメッセージ全体に適用した後にフラグメント化されます。
圧縮済みエレメントには `POTR_FLAG_COMPRESSED` フラグが立ちます。

圧縮後のサイズが元のサイズ以上になった場合 (圧縮効果なし) は、内部で自動的に非圧縮に切り替えて送信します。
この場合、圧縮前のデータを `POTR_FLAG_COMPRESSED` なしで送ります。

## ヘルスチェックプロトコル

### 概要

ヘルスチェック送信は通信種別を問わず設定周期で実行されます。
`unicast` / `multicast` / `broadcast` ではヘルスチェックは **一方向**（送信者 → 受信者のみ）です。
`unicast_bidir` / `unicast_bidir_n1` では、各エンドポイントまたは各ピアが独立して PING 送信・応答を行います（後述「双方向 UDP のヘルスチェック動作」参照）。UDP 双方向ではこの定周期 PING 送信自体が接続確立の前提であり、PING を送らないと `CONNECTED` は成立しません。

### 送信者の動作

`health_interval_ms > 0` のときヘルスチェックスレッドが有効になります。

ヘルスチェックスレッドは `health_interval_ms` 周期で PING を送信します。
データ送信の有無は PING の送信周期に影響しません。
双方向 UDP では、受信 PING ペイロードに相手側の `POTR_PING_STATE_NORMAL` が載るまで `CONNECTED` しないため、実効 `health_interval_ms = 0` で PING 送信が止まると初回 `CONNECTED` に到達しません。

### 一方向通信の受信者の動作

`health_timeout_ms > 0` のときタイムアウト監視が有効になります。

受信スレッドは最終パケット受信時刻 (PING・DATA いずれも対象) を監視し、
`health_timeout_ms` が経過したとき、切断と判定します。

切断判定時:
1. `health_alive` を 0 に設定する
2. `POTR_EVENT_DISCONNECTED` をコールバックで通知する
3. `peer_session_known` をリセットして次の接続を受け入れ可能にする
4. 受信ウィンドウをリセットする (次に受信するパケットの `seq_num` で再初期化される)

次のパケットを受信したとき:
1. セッションを採用し、受信ウィンドウをそのパケットの `seq_num` で初期化する
2. `health_alive` を 1 に設定する
3. `POTR_EVENT_CONNECTED` をコールバックで通知する

### PING ペイロード仕様

PING パケットのペイロードには自端の各パス PING 受信状態を格納します。

```text
[PING ペイロード: POTR_MAX_PATH バイト (= 4)]
```

各バイトがパスインデックス順に 1 バイトずつ、以下の値を取ります。

| 値 | 定数 | 意味 |
|---|---|---|
| `0` | `POTR_PING_STATE_UNDEFINED` | 不定 (片方向 / まだ有効な PING / DATA 未受信) |
| `1` | `POTR_PING_STATE_NORMAL` | 正常 (ヘルス信号を継続受信中) |
| `2` | `POTR_PING_STATE_ABNORMAL` | 異常 (PING 途絶) |

片方向通信 (type 1-6) の送信側は返送用 PING を持たないため、送出する PING ペイロードは全パス `UNDEFINED` のままです。受信側ローカルの `path_ping_state[]` は有効な `PING` または `DATA` 受信で更新されます。双方向通信 (type 7-10) は実際の PING 受信状態を格納します。

暗号化 (`POTR_FLAG_ENCRYPTED`) 時は末尾に GCM 認証タグ (`POTR_CRYPTO_TAG_SIZE` = 16 バイト) が付加され、`payload_len = POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE` になります。

受信側はペイロードを `remote_path_ping_state[]` に格納します。これにより双方向通信の両端が互いの往復疎通状態を把握できます。

### 一方向 PING パケットの性質

以下の説明は `unicast` / `multicast` / `broadcast` / `*_raw` に適用されます。

- `seq_num` には送信側の `next_seq` (次に送出する DATA に割り当てる通番) を格納します
- PING はウィンドウに登録されません (NACK・再送の対象外)
- ペイロードは `POTR_MAX_PATH` バイト (全バイト `POTR_PING_STATE_UNDEFINED`)
- 受信者は PING を受け取っても返信しません
- 通常モード: 受信者は `seq_num` を上限として受信ウィンドウをスキャンし、欠番を一括 NACK します
- RAW モード: 欠番スキャンは行わず、PING の `seq_num` が `recv_window.next_seq` より前方にある場合は `POTR_EVENT_DISCONNECTED` を発行してウィンドウをリセットします

### 双方向 UDP のヘルスチェック動作

#### PING パケットフォーマット（`unicast_bidir` / `unicast_bidir_n1`）

| 種別 | `flags` | `seq_num` | `ack_num` | `payload_len` |
|---|---|---|---|---|
| 要求 | `POTR_FLAG_PING` | 送信側の `next_seq` | `0` | `POTR_MAX_PATH` (非暗号化) または `POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE` (暗号化) |
| 応答 | `POTR_FLAG_PING` | 応答側の `next_seq` | 要求の `seq_num` | 同上 |

#### 両端の動作

```
1:1 モードでは両端それぞれ、N:1 モードではサーバが各ピアごとに独立して次を行います:
  health_interval_ms 周期で PING 要求（ack_num=0）を送信する
  相手から PING 要求（ack_num=0）を受け取ったら即 PING 応答（ack_num=要求の seq_num）を返す
  last_recv_tv_sec を PING 応答受信時にも更新する
  check_health_timeout() で last_recv_tv_sec を監視 → health_timeout_ms 超過で DISCONNECTED
```

双方向 UDP の `CONNECTED` は、受信した PING ペイロードの `remote_path_ping_state[]` に少なくとも 1 つ `POTR_PING_STATE_NORMAL` が入った時点で成立します。これは「相手が自端からの PING を正常受信し、その状態を載せた PING を返してきた」ことを意味します。したがって `health_interval_ms = 0`、またはグローバル既定値として `udp_health_interval_ms = 0` かつサービス側で上書きしない構成では、初回 PING が送られず `CONNECTED` は成立しません。

#### 双方向 UDP で `last_recv_tv_sec` 監視だけで十分な理由

TCP では「OS レベルの接続が生存したままアプリケーション層がハングする」状況が起こり得るため、
PING 応答の個別タイムアウト監視が必要です。

UDP には接続概念がありません。相手のアプリケーションが停止すると、データパケットも PING パケットも
一切届かなくなります。`last_recv_tv_sec` が更新されないため、`check_health_timeout()` のみで
切断を検知できます。PING 応答の個別タイムアウト管理は不要です。

| モード | タイムアウト検知方法 | 理由 |
|---|---|---|
| UDP unicast | RECEIVER: `last_recv_tv_sec` 監視 | 片方向 type 1-6 は有効な `PING` / `DATA` 受信で更新 |
| UDP unicast_bidir | 1:1 は両端、N:1 は各ピア単位で `last_recv_tv_sec` を監視 | 相手停止で当該相手からの全パケットが途絶える → timeout 発火 |
| TCP / TCP_bidir | SENDER: PING 応答タイムアウト監視 | OS 接続が生存したままアプリがハングし得る |

### TCP 通信種別のヘルスチェック動作

#### PING 送信のタイミング

TCP では接続直後に bootstrap PING を送信し、`tcp_health_interval_ms > 0` のときだけ定周期 PING を送信します。
定周期 PING は UDP と同様にデータ送信頻度の影響を受けません。
TCP では加えて、「OS 接続が生存したままアプリケーション層がハングする」状況に対応するため、
`tcp_health_interval_ms > 0` のときだけ PING 要求 / 応答を個別に監視します。
初回 CONNECTED は、受信した PING ペイロードの `remote_path_ping_state[]` に少なくとも 1 つ `POTR_PING_STATE_NORMAL` が載った時点で成立します。これは「自端が先に送った bootstrap PING を相手が受信し、その結果を載せた応答 PING が戻ってきた」ことを意味します。

#### タイムアウト監視

| 通信種別 | 役割 | 監視内容 |
|---|---|---|
| UDP unicast 等 | RECEIVER | `last_recv_tv_sec` が `udp_health_timeout_ms` を超えたら DISCONNECTED |
| TCP / TCP_BIDIR | SENDER | `tcp_health_interval_ms > 0` のとき、PING 応答（`ack_num > 0`）が `tcp_health_timeout_ms` 以内に返らなければ DISCONNECTED（health スレッド） |
| TCP / TCP_BIDIR | RECEIVER | `tcp_health_interval_ms > 0` のとき、PING 要求（`ack_num = 0`）が `tcp_health_timeout_ms` 以内に届かなければ DISCONNECTED（recv スレッド） |
| TCP_BIDIR | SENDER | `tcp_health_interval_ms > 0` のとき、PING 要求（`ack_num = 0`）が `tcp_health_timeout_ms` 以内に届かなければ DISCONNECTED（recv スレッド） |
| TCP_BIDIR | RECEIVER | `tcp_health_interval_ms > 0` のとき、PING 応答（`ack_num > 0`）が `tcp_health_timeout_ms` 以内に返らなければ DISCONNECTED（health スレッド） |

TCP は bootstrap PING の往復で初回 CONNECTED を確立し、`tcp_health_interval_ms > 0` のときだけ定周期 PING により両端が互いの生死を独立して監視できます。
`tcp_bidir` では両端が PING 送信側・受信側の両方を兼ねるため、PING 応答タイムアウト（health スレッド）と
PING 要求到着タイムアウト（recv スレッド）の 2 種類を両端で監視します。

## マルチパス

送信者は最大 4 経路の UDP ソケットを同時に保持できます。
DATA パケット・PING パケット・再送パケットはすべての経路へ同時に送信されます。

経路の冗長化により、1 経路がパケットロスした場合でも他の経路で届く可能性があります。

NACK パケットは送信者へのユニキャストで返されます。
マルチキャスト・ブロードキャスト環境でも、NACK は送信元アドレスへユニキャストされます。
なお、受信者が保持する全パスのソケットからそれぞれユニキャスト送信されます。

## 定数・上限値

| 定数 | 値 | 説明 |
|---|---|---|
| `POTR_MAX_PAYLOAD` | 1,400 バイト | DATA パケットのペイロード上限 |
| `POTR_MAX_WINDOW_SIZE` | 256 | スライディングウィンドウの最大スロット数 |
| `POTR_MAX_MESSAGE_SIZE` | 65,535 バイト | 1 回の `potrSend()` で送信できるデータの上限 |
| `POTR_MAX_PATH` | 4 | マルチパスの最大経路数 |
| `POTR_SEND_QUEUE_DEPTH` | 1,024 | 送信キューの最大エレメント数 |
| `POTR_PAYLOAD_ELEM_HDR_SIZE` | 6 バイト | ペイロードエレメントヘッダーの固定長 |
| `POTR_NACK_DEDUP_MS` | 200 ms | NACK 重複抑制の時間窓 |
| `POTR_DEFAULT_RECONNECT_INTERVAL_MS` | 5,000 ms | TCP SENDER 自動再接続間隔デフォルト |
| `POTR_DEFAULT_CONNECT_TIMEOUT_MS` | 10,000 ms | TCP SENDER 接続タイムアウトデフォルト |
