# プロトコル仕様

## ネットワーク層

- **トランスポート**: UDP/IPv4 (unicast / multicast / broadcast / unicast_bidir) または TCP/IPv4 (tcp / tcp_bidir)
- **IPv6**: 非対応
- **バイト オーダー**: すべてのワイヤー フォーマットはビッグ エンディアン (ネットワーク バイト オーダー、NBO)

## パケット構造

すべてのパケットは共通のヘッダー (72 バイト) とペイロード (既定上限 1,400 バイト、設定上限 `POTR_MAX_PAYLOAD` = 65,435 バイト) で構成されます。  
UDP データグラム 1 個が外側パケット 1 個に対応します。

### 外側パケット (potr_packet)

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 service_id (int64) [高位 32 bit]              |  8 バイト
+               -+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 service_id (int64) [低位 32 bit]              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 session_tv_sec (int64) [高位 32 bit]          |  8 バイト
+               -+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 session_tv_sec (int64) [低位 32 bit]          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         session_id (uint32)                   |  4 バイト
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      session_tv_nsec (int32)                  |  4 バイト
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         seq_num (uint32)                      |  4 バイト
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         ack_num (uint32)                      |  4 バイト
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           flags (uint16)      |       payload_len (uint16)    |  4 バイト
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      protocol_version (uint32)                |  4 バイト
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 tx_counter (uint64) [高位 32 bit]             |  8 バイト
+               -+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 tx_counter (uint64) [低位 32 bit]             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 tx_tv_sec (int64) [高位 32 bit]               |  8 バイト
+               -+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 tx_tv_sec (int64) [低位 32 bit]               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       tx_tv_nsec (int32)                      |  4 バイト
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        reserved (uint32)                      |  4 バイト
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 tx_mono_ns (uint64) [高位 32 bit]             |  8 バイト
+               -+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 tx_mono_ns (uint64) [低位 32 bit]             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                  payload (0〜max_payload バイト)              |
|                         ...                                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**ヘッダー固定長**: 72 バイト (`POTR_PACKET_HEADER_WIRE_SIZE`)

| オフセット | フィールド | 型 (NBO) | 内容 |
|---|---|---|---|
| 0 | `service_id` | int64 | サービス識別子。受信時に構成ファイルの値と照合します。 |
| 8 | `session_tv_sec` | int64 | セッション開始時刻 (POSIX 秒) |
| 16 | `session_id` | uint32 | セッション乱数識別子 |
| 20 | `session_tv_nsec` | int32 | セッション開始時刻 (ナノ秒部、0〜999,999,999) |
| 24 | `seq_num` | uint32 | パケット通番。各セッション開始時に 0 からカウント |
| 28 | `ack_num` | uint32 | NACK・REJECT 時: 対象通番。PING 応答時: 対応する要求の `seq_num`。FIN 時: `POTR_FLAG_FIN_TARGET_VALID` が立っている場合のみ受信完了目標 `next_seq`。DATA / PING 要求 / REPORT / HANDSHAKE / KEY 時: 0 |
| 32 | `flags` | uint16 | パケット種別フラグ (下記参照) |
| 34 | `payload_len` | uint16 | ペイロード長 (バイト)。0〜`max_payload` (暗号化時はタグ長を含む) |
| 36 | `protocol_version` | uint32 | ワイヤー プロトコル バージョン。現行値は `POTR_PROTOCOL_VERSION` (= 2) |
| 40 | `tx_counter` | uint64 | 送出通番。送出ごとに 1 増える |
| 48 | `tx_tv_sec` | int64 | 送出時の実時刻 (POSIX 秒)。取得失敗時は 0 |
| 56 | `tx_tv_nsec` | int32 | 送出時の実時刻 (ナノ秒部、0〜999,999,999)。取得失敗時は -1 |
| 60 | `reserved` | uint32 | 予約。送信時は 0 とし、受信時は値を無視する |
| 64 | `tx_mono_ns` | uint64 | セッション開始からの経過時間 [ns]。取得失敗時は `UINT64_MAX` |

Table: 外側パケット (potr_packet) ヘッダーのフィールド構成

受信側は `protocol_version` が `POTR_PROTOCOL_VERSION` (2) と一致しないパケットを破棄します。`protocol_version` のオフセット (36 バイト目) により、版が異なる相手からのパケットも版の不一致を検出して遮断できます。  
`tx_tv_sec` と `tx_tv_nsec` を隣り合わせに配置し、64 ビット フィールド (`tx_mono_ns`) を 8 バイト境界に揃えるため、`tx_tv_nsec` の直後に 4 バイトの `reserved` を設けています。`reserved` もヘッダーの一部として改ざん検出 (AAD) の対象に含めます。

### MTU とペイロード上限

既定のペイロード上限 `max_payload` = 1,400 バイトのとき、IPv4 ヘッダー (20 バイト)、UDP ヘッダー (8 バイト)、および porter ヘッダー (72 バイト) を含めた IP データグラムの最大長は 1,500 バイトとなります。一般的な Ethernet の MTU 1,500 バイトに収まりますが余裕はないため、VPN など MTU が小さい経路では `global` オブジェクトの `max_payload` 設定でペイロード上限を下げて運用します。

`max_payload` の検査上限 (`POTR_MAX_PAYLOAD`) は、IPv4 UDP データグラムの最大ペイロード 65,507 バイト (`POTR_UDP_MAX_DATAGRAM_PAYLOAD`) からヘッダー長 72 バイト (`POTR_PACKET_HEADER_WIRE_SIZE`) を引いた 65,435 バイトです。

### パケット種別フラグ (flags)

| 定数 | 値 | 説明 |
|---|---|---|
| `POTR_FLAG_DATA` | 0x0001 | データ パケット。ペイロードにエレメントを含みます。 |
| `POTR_FLAG_NACK` | 0x0002 | 再送要求。`ack_num` に要求する通番を格納 |
| `POTR_FLAG_PING` | 0x0004 | ヘルス チェック要求・応答。ペイロードにパス受信状態 (`POTR_MAX_PATH` バイト) を格納します。 |
| `POTR_FLAG_REJECT` | 0x0008 | 再送不能通知。`ack_num` に再送不能な通番を格納 |
| `POTR_FLAG_FIN` | 0x0010 | 正常終了通知。ペイロードなし (payload_len=0) |
| `POTR_FLAG_ENCRYPTED` | 0x0020 | AES-256-GCM による暗号化・認証タグが付与されていることを示します。 |
| `POTR_FLAG_FIN_TARGET_VALID` | 0x0040 | FIN の `ack_num` が有効な受信完了目標 `next_seq` を表すことを示します。 |
| `POTR_FLAG_FIN_ACK` | 0x0080 | TCP close 完了通知。`ack_num` に確認対象の FIN target を格納 |
| `POTR_FLAG_REPORT` | 0x0100 | 受信報告パケット。片方向・一対多の受信者が各経路の受信状態 (`POTR_MAX_PATH` バイト) を送信者へ通知します。 |
| `POTR_FLAG_HANDSHAKE` | 0x0200 | 証明書方式の TLS 1.3 / DTLS 1.2 ハンドシェイクのバイト列をペイロードに格納するパケット |
| `POTR_FLAG_KEY` | 0x0400 | 証明書方式の一対多通信で、受信者用の鍵で暗号化したグループ鍵を配布するパケット |

Table: パケット種別フラグ (flags) の定義一覧

### flags 種別ごとの ack_num の意味

| `flags` | `ack_num` の意味 |
|---|---|
| `POTR_FLAG_DATA` | `0` (固定、無視) |
| `POTR_FLAG_NACK` | 再送要求の通番 |
| `POTR_FLAG_PING` (要求) | `0` (固定) |
| `POTR_FLAG_PING` (応答) | 対応する要求の `seq_num` |
| `POTR_FLAG_REJECT` | 再送不能の通番 |
| `POTR_FLAG_FIN` | `POTR_FLAG_FIN_TARGET_VALID=1` のとき送信側 `send_window.next_seq`、未設定時は無視 |
| `POTR_FLAG_FIN_ACK` | TCP close 完了を確認した `FIN.ack_num` |
| `POTR_FLAG_REPORT` | `0` (固定) |
| `POTR_FLAG_HANDSHAKE` | `0` (固定) |
| `POTR_FLAG_KEY` | `0` (固定) |

Table: パケット種別フラグごとの ack_num の意味

`ack_num` の意味はパケット種別 (`flags`) ごとに独立しているため、NACK / REJECT パケットでの `ack_num` 使用とは競合しません。

### TCP ストリームからのパケット読み取り

UDP はデータグラム境界がパケット境界と一致しますが、TCP はバイト ストリームです。  
受信側は次の 2 ステップでパケットを読み取ります。

1. ソケットから 72 バイト読み取る (ヘッダー確定)
2. ヘッダーの payload_len バイトを追加読み取る (ペイロード確定)

UDP と TCP で同一のヘッダー フォーマットを使用するため、将来の TCP↔UDP ブリッジ実装を容易にします。

### TCP モードにおける ack_num の追加用途

UDP では `ack_num` は NACK / REJECT の対象通番として使用します。  
TCP では NACK / REJECT が不要なため、**PING 要求と PING 応答の識別** にも使用します。

| `ack_num` の値 | 意味 (TCP PING 時) |
|---|---|
| `0` | PING 要求 (応答を求める) |
| `N` (N > 0) | PING 応答 (`seq_num = N` の PING 要求への返答) |

Table: TCP PING における ack_num の値と意味

DATA パケットでは `ack_num` は 0 固定とし、受信時は無視します。

### TCP モードでのパケット種別フラグ使用可否

| フラグ | TCP で使用するか | 理由 |
|---|---|---|
| `POTR_FLAG_DATA` | ○ | データ送信に使用 |
| `POTR_FLAG_NACK` | × | TCP が配送を保証するため不要 |
| `POTR_FLAG_PING` | ○ | ヘルス チェック。`ack_num=0` で要求、`ack_num=N` で応答 |
| `POTR_FLAG_REJECT` | × | 再送不能は発生しないため不要 |
| `POTR_FLAG_FIN` | ○ | `potr_service_close()` 時にプロトコル レベルの FIN を送信 |
| `POTR_FLAG_FIN_ACK` | ○ | receiver が最後の DATA コールバック完了後に返却する close 完了通知 |
| `POTR_FLAG_ENCRYPTED` | ○ | UDP と同様に使用可能 |
| `POTR_FLAG_REPORT` | × | UDP の片方向・一対多通信でのみ使用 |
| `POTR_FLAG_HANDSHAKE` | ○ | 証明書方式 (`auth = "cert"`) の TLS 1.3 ハンドシェイクに使用 |
| `POTR_FLAG_KEY` | × | UDP の一対多通信でのみ使用 |

Table: TCP モードにおけるパケット種別フラグの使用可否と理由

### TCP マルチパスにおける重複排除

複数の TCP 接続 (path) が確立されると、送信スレッドは選択中のグループのアクティブ path に同一 `seq_num` のパケットを送信します。  
受信側は複数の path から同一パケットを受け取るため、重複排除が必要です。

グループ切り替え時は、到達不能の検出前に旧グループだけへ書き込んだ通番が届かないため、送信者は送信済み DATA の写しを直近 `window_size` 個保持します。到達不能による切り替えのときは、保持している写しを通番順に新グループへ書き込み、その後に新しい DATA を書き込みます。保持数を超える欠番が残った場合は、受信側がストリームを切断します。

TCP の受信側は欠けた通番を再送で補えないため、送信スレッドは選択中のグループの少なくとも 1 つの path へ各通番を必ず書き込みます。

- 送信バッファーに空きがない path は、他の path へ書き込めた場合に限り、そのパケットを飛ばします。
- どの path にも空きがない場合は、空きを待って書き込みます。
- 書き込みに失敗した path はソケットを停止し、受信スレッドに切断を検知させて再接続します。
- どの path にも書き込めなかったパケットの通番は消費せず、ブロッキング送信へ `POTR_ERR_DISCONNECTED` を返します。

UDP マルチパスで実装済みの `potr_internal_window_recv_push()` / `potr_internal_window_recv_pop()` をそのまま流用します。  
複数の recv スレッドが同一の `recv_window` に並行してアクセスするため、呼び出し前後を `recv_window_mutex` で保護します。

```text
recv スレッド #i:
  recv_window_mutex を lock
  potr_internal_window_recv_push(recv_window, pkt)  → ウィンドウ範囲外なら FAIL を返す
  FAIL かつウィンドウ先頭より先の通番の場合:
    recv_window_mutex を unlock
    ストリームを切断する (受信していない通番があるため)
  recv_window_mutex を unlock
  FAIL の場合: continue（他の path で受信済みの重複。コールバック呼び出しなし）

  recv_window_mutex を lock
  while potr_internal_window_recv_pop(recv_window, out) == SUCCESS:
    recv_window_mutex を unlock
    deliver_payload_elem(out)   ← コールバック呼び出し前に unlock
    recv_window_mutex を lock
  recv_window_mutex を unlock
```

**フラグメント化メッセージへの適用**:  
フラグメントは外側パケット (コンテナー) 単位で `seq_num` が付与されます。  
`POTR_FLAG_MORE_FRAG` は内側ペイロード エレメントの管理に使うフラグであり、パケット単位の重複排除ロジックとは干渉しません。UDP マルチパスと同一の動作をします。

**PING との機能競合**:  
PING パケットは `potr_internal_window_recv_push()` を通さず、recv スレッド内で直接処理します。  
したがって PING と重複排除ウィンドウは機能競合しません。

## ペイロード エレメント (パッキング コンテナー)

DATA パケットのペイロードには、1 つ以上のペイロード エレメントが連続して格納されます。  
これを **パッキング** と呼び、複数の小さなエレメントを 1 つの UDP パケットにまとめて送信効率を高めます。

### エレメントのフォーマット

```text
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|      elem_flags (uint16, NBO) |   2 バイト
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|            payload_len (uint32, NBO)          |   4 バイト
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          payload (payload_len バイト)         |   可変長
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**エレメント ヘッダー**: 6 バイト固定

ヘッダー フィールド (`elem_flags`・`payload_len`) はネットワーク バイト オーダー (NBO) で格納します。  
`payload` 部はアプリケーションが `potr_service_send()` に渡したデータをそのままコピーしたバイト列であり、porter はバイト オーダーの変換を行いません。エンディアン管理はアプリケーションの責務です。

### エレメント フラグ (elem_flags)

| 定数 | 値 | 説明 |
|---|---|---|
| `POTR_FLAG_MORE_FRAG` | 0x0001 | 後続フラグメントが存在する (フラグメント続行) |
| `POTR_FLAG_COMPRESSED` | 0x0002 | ペイロードが raw DEFLATE 圧縮済み |

Table: エレメント フラグ (elem_flags) の定義一覧

### パッキングの動作

送信スレッドは送信キューから複数のエレメントを取り出し、1 つの DATA パケットにまとめます。  
以下の条件に該当するエレメントは単体で送信されます (直前のパケットをフラッシュしてから送信)。

- `POTR_FLAG_MORE_FRAG` フラグが立っているエレメント (フラグメントの中間)

## セッション識別

### 目的

送信者プロセスの再起動後に OS が同一の エフェメラル ポートを再利用した場合や、ネットワーク上に残留した前セッションのパケットが遅延して届いた場合に、受信者がそれらを誤って処理しないようにします。

### セッション識別子の構成

送信者は `potr_service_open()` 呼び出し時に以下を決定し、以後のすべてのパケットに付与します。

| フィールド | 決定方法 |
|---|---|
| `session_id` | 乱数 (シード: 現在時刻 × PID) |
| `session_tv_sec` | `CLOCK_REALTIME` (Linux) または `GetSystemTimeAsFileTime()` (Windows) による現在時刻 (秒) |
| `session_tv_nsec` | 同上 (ナノ秒部) |

Table: セッション識別子のフィールド構成と決定方法

採用判定はタイムスタンプを同一送信者の 2 パケット間で比較するため、送信者と受信者のクロック同期には依存しません。

### 受信者のセッション採用ポリシー

受信者は受信したパケットの識別子を、現在追跡中のセッションと比較します。

```
1. session_tv_sec が現在追跡中より大きい           → 新セッションとして採用
2. session_tv_sec が等しく session_tv_nsec が大きい → 新セッションとして採用
3. 上記が等しく session_id が大きい                → 新セッションとして採用 (タイブレーク)
4. それ以外                                        → 旧セッションのパケットとして破棄
```

新セッション採用時 (および FIN 受信・ヘルス チェック タイムアウト後の再接続時) は、受信ウィンドウを **最初に受信したパケットの `seq_num`** で初期化します。  
これにより、送信者が任意の通番から送信を再開した場合でも、NACK/REJECT サイクルなしに即時同期できます。

### クロック逆行時の挙動

OS クロックが手動設定や NTP スラムで過去に戻った場合、新セッションのタイムスタンプが旧セッションより小さくなり、受信者のセッション採用ポリシー上は「旧セッション」と判定されます。  
この状況での挙動は `health_timeout_ms` の設定により異なります。

**ヘルス チェック タイムアウト有効 (`health_timeout_ms > 0`)**

送信者が再起動すると旧セッションの PING が途絶えるため、`health_timeout_ms` 経過後にヘルス チェック タイムアウトが発火します。タイムアウト時は FIN 受信と同様に `DISCONNECTED` イベントが発火し `peer_session_known` がクリアされます。  
その後、次に到着したパケット (タイムスタンプが逆転した新セッションを含む) を無条件に採用して `CONNECTED` イベントが発火します。  
クロック逆行があっても `health_timeout_ms` の待機を伴う切断・再接続を経て自動回復します。

**ヘルス チェック タイムアウト無効 (`health_timeout_ms = 0`)**

`peer_session_known` がクリアされる契機がなく、旧セッションが追跡中のまま残ります。  
タイムスタンプが逆転した新セッションのパケットは永続的に破棄されます。  
これはヘルス チェック無効環境における既知の制限です。

### unicast_bidir N:1 モードのピア識別

`unicast_bidir` の N:1 モードでは、サーバーは各受信パケットの **session triplet** (`session_id` + `session_tv_sec` + `session_tv_nsec`) を使ってピアを識別します。`recvfrom()` の送信元 IP:Port は、ピアへの返信先パスを学習するために使用し、一次識別キーには使いません。

| 識別手段 | 用途 |
|---|---|
| session triplet | ピアの検索・新規作成判定 |
| `recvfrom()` の送信元 IP:Port | 返信先アドレスの学習、パス追加 |
| `src_port` 設定 | N:1 サーバーでの任意の送信元ポート フィルター |

Table: unicast_bidir N:1 モードにおけるピア識別手段と用途

N:1 モードの各ピアは、以下の状態を独立して持ちます。

- 自セッション ID / 相手セッション ID
- `seq_num` 系列
- 送信ウィンドウ / 受信ウィンドウ
- フラグメント結合状態
- ヘルス チェック状態 (`health_alive`、最終受信時刻)
- 学習済み返信先パス

未知の session triplet を受信したとき、空きスロットがあれば新規ピアを作成して `POTR_EVENT_CONNECTED` を発火します。`max_peers` に達している場合はエラー ログを残して当該パケットを破棄します。

## 通番管理

### 型と範囲

通番 (`seq_num`) は `uint32_t` の循環カウンターです。各セッション開始時に 0 から始まり、最大値に達すると 0 に折り返します。

### 折り返し対応の比較

通番の新旧を比較する際は、単純な大小比較ではなく折り返しを考慮した比較を使用します。  
差が `UINT32_MAX / 2` 未満であれば、a が b より後から送信された (新しい) と判断します。

### 通番の付与タイミング

通番は送信スレッドが DATA パケットを送出する際に付与します。  
フラグメント化されたメッセージの各フラグメントはそれぞれ独立した通番を持ちます。  
PING パケットは送信側の `next_seq` (次に割り当てる DATA の通番) を `seq_num` に格納しますが、通番を消費しません。

### RAW モードにおける通番の役割

RAW モード (`unicast_raw` / `multicast_raw` / `broadcast_raw`) でも通番はインクリメントされますが、**再送制御には使用しません**。  
受信側では通番を使って順序整列とギャップ検出を行い、ギャップ検出時は NACK の代わりに `POTR_EVENT_DISCONNECTED` を発行します。AES-256-GCM のノンスは `seq_num` ではなく後述の `tx_counter` から生成します。

### TCP モードにおける通番の役割

| 目的 | TCP での利用 |
|---|---|
| 再送制御 (NACK ターゲット) | 不使用 |
| 順序整列 | 不使用 (TCP が保証) |
| AES-256-GCM ノンス生成 | 不使用 (`tx_counter` を使用) |
| フラグメント順序確認 | 使用 (デバッグ・ログ用途) |

Table: TCP モードにおける通番の目的と利用状況

## 送出通番・送信時刻・暗号化とリプレイ対策

### 送出通番 (tx_counter)

`tx_counter` は、送信側のセッションごとに `0` から始まり、パケットを 1 回送出するたびに `1` 増える `uint64_t` のカウンターです。  
経路ごとの複製、再送、PING、NACK、受信報告、FIN、FIN_ACK、ハンドシェイク、鍵配布を含むすべての送出が対象です。`unicast_bidir_n1` のサーバーは、ピアごとの自セッションに対して `tx_counter` を持ちます。`tx_counter` が上限に達した場合は、そのセッションを閉じます。

### AES-256-GCM の nonce と AAD

AES-256-GCM の nonce (12 バイト) は、`session_id` (4 バイト NBO) と `tx_counter` (8 バイト NBO) で構成します。ヘッダー全体 (72 バイト) を追加認証データ (AAD) として改ざん検出の対象にします。  
同じ鍵の下で送出ごとに `tx_counter` が増加して nonce が重複しないため、送信時刻が送出ごとに異なっても安全に暗号化できます。

### リプレイ対策

暗号化されたパケットについて、受信側は送信元のセッションごとに直近 256 個の `tx_counter` を記録します。記録済みまたは記録範囲より古い `tx_counter` のパケットは、GCM の復号と認証タグ検証の後に破棄します。  
経路ごとの複製はそれぞれ異なる `tx_counter` を持つため、この記録とは干渉しません。

### 送信時刻の取得

| フィールド | 取得元 | 取得の時点 | 取得失敗時の値 |
|---|---|---|---|
| `tx_tv_sec`、`tx_tv_nsec` | cplat の実時刻 API (Linux は `CLOCK_REALTIME`、Windows は FILETIME による 100 ns 単位) | 各経路への送出直前。再送時も取り直す | `tx_tv_sec = 0`、`tx_tv_nsec = -1` |
| `tx_mono_ns` | cplat の高分解能単調増加時計 API。セッション開始時に採取した値との差 | 各経路への送出直前。再送時も取り直す | `UINT64_MAX` |

Table: 送信時刻フィールドの取得元・取得時点・失敗時の値

時刻は経路ごとに取得するため、暗号化を有効にした場合は経路ごとに暗号処理を行います。時計の取得に失敗した場合は表の既定値を格納して送出を続けます。  
受信側は時刻を受信の判定や破棄に使わず、利用側にも渡しません。時刻は障害調査でパケットを解析するための情報です。

## スライディング ウィンドウ

> **TCP モードではスライディング ウィンドウを使用しません。**
> TCP は順序保証・配送保証があるため、再送制御・順序整列のいずれも不要です。
> 以下の記述は UDP 通信種別 (unicast / multicast 等) に適用されます。

### 目的

- **送信ウィンドウ**: NACK 受信時に再送するために、過去パケットのコピーを保持します。
- **受信ウィンドウ**: 到着順序が入れ替わったパケット (追い越し) をバッファリングし、通番順に整列します。

### ウィンドウ サイズ

設定パラメーター `window_size` で指定します。既定値 16、最大 256。

### 送信ウィンドウの動作

送信スレッドは DATA パケットを送出するたびに送信ウィンドウに登録します。  
PING パケットはウィンドウに登録されません (NACK・再送の対象外)。  
ウィンドウが満杯になると、最も古いエントリを **evict** (削除) して循環利用します。  
evict された通番に対して後から NACK が到着した場合、そのパケットは再送不能となり REJECT を返却します。

### 受信ウィンドウの動作

受信スレッドは到着したパケットを受信ウィンドウに格納します。  
期待する通番より大きい通番のパケット (追い越し) もウィンドウ内に保持します。  
ウィンドウの先頭から連続した通番のパケットが揃ったところで、上位層へ順番に渡します。

### 欠番検出と NACK 送出 (通常モード)

受信ウィンドウが、期待する通番より大きい通番のパケットを受け取った場合、欠番が生じています。  
受信スレッドは欠番となっている通番を特定し、送信者へ向けて NACK を送出します。

### 欠番検出と即切断 (RAW モード)

RAW モードでは NACK を送出しません。欠番を検出した場合 (DATA の追い越し到着・PING の `seq_num` が `recv_window.next_seq` より前方にある場合) の動作:

1. `POTR_EVENT_DISCONNECTED` をコールバックで通知します。
2. 受信ウィンドウを到着パケットの通番でリセットします。
3. 到着パケットを配信し、`POTR_EVENT_CONNECTED` をコールバックで通知します。

追い越しパケット自体は破棄されません。ギャップをまたいで到着した最初のパケットが即座に配信されます。

> **`reorder_timeout_ms` との組み合わせ**: `reorder_timeout_ms > 0` の場合、欠番を検出しても
> 即座に DISCONNECTED を発行せず、指定時間だけ待機します。待機中に欠落パケットが到着した場合は
> 通常どおり順序整列して配信します (DISCONNECTED は発行されません)。
> タイムアウトを経過してなお欠落のままであれば DISCONNECTED を発行してセッションをリセットします。

## フラグメント化と結合

### フラグメント化 (送信側)

1 回の `potr_service_send()` で送信するデータが `max_payload` を超える場合、複数のフラグメントに分割します。  
圧縮を指定した場合は圧縮後のデータをフラグメント化します。

各フラグメントは個別のペイロード エレメントとして送信キューに格納され、後続フラグメントが存在する場合は `POTR_FLAG_MORE_FRAG` フラグを立てます。  
最終フラグメントには `POTR_FLAG_MORE_FRAG` を立てません。

### フラグメント結合 (受信側)

受信スレッドは `POTR_FLAG_MORE_FRAG` フラグを監視し、フラグメントをバッファーに蓄積します。  
`POTR_FLAG_MORE_FRAG` がないエレメントを受け取った時点でフラグメント結合が完了し、完全なメッセージとしてコールバックを呼び出します。

フラグメント バッファーのサイズは最大 65,535 バイトです。

## 再送制御

> **TCP モードでは再送制御 (NACK / REJECT) を使用しません。**
> TCP 自身が信頼性のある配送を保証するためです。
> 以下の記述は UDP 通信種別に適用されます。

### 方式

通常モードは **NACK のみ方式** (ACK なし) を採用します。  
通常時は ACK を返さないためスループットが高く、問題発生時のみ受信側から通知します。

RAW モードでは再送制御を行いません。送信ウィンドウへの登録は行いますが、受信側から NACK が到着しても無視します。  
到達保証が不要で低遅延・ベスト エフォートを優先する用途に適します。

### 再送フロー

```text
1. 受信者が欠番を検出する
   +- reorder_timeout_ms == 0: 即 NACK を送出する
   +- reorder_timeout_ms > 0: タイムアウトまで待機
       +- 待機中に欠落パケット到着: 正常配信 (NACK 不要)
       +- タイムアウト経過: NACK を送出する
          ※ multicast/broadcast 通常モードではジッタを付加 (詳細は後述)
2. 受信者が欠番の通番を NACK パケットで送信者へ送出する
3. 送信者が NACK を受信し、該当通番のパケットを送信ウィンドウから検索する
4a. 該当パケットが存在する → 全パスへ再送する
4b. 該当パケットが存在しない (evict 済み) → REJECT パケットを返却する
```

再送パケットはマルチキャスト・ブロードキャスト時でも全受信者へ送信されます。  
これにより、他の受信者も再送パケットの恩恵を受けられます。

### NACK タイムアウト ジッタ (マルチキャスト/ブロードキャスト)

`multicast` / `broadcast` 通常モードかつ `reorder_timeout_ms > 0` の場合、複数の受信者が同一欠番の NACK を同時に送出すると送信者への NACK が集中します (NACK インプロージョン)。

これを回避するため、受信者ごとに `reorder_timeout_ms` の 100%〜200% のランダムな待機時間を設定します。各受信者のタイマーが独立した時刻に期限を迎えることで、NACK が時間軸上に分散されます。

```
受信者 A の実効タイムアウト: reorder_timeout_ms + jitter_A  (100%〜200%)
受信者 B の実効タイムアウト: reorder_timeout_ms + jitter_B  (100%〜200%)
受信者 C の実効タイムアウト: reorder_timeout_ms + jitter_C  (100%〜200%)
```

ジッタは monotonic クロックのナノ秒部を乱数源として計算し、外部 RNG への依存はありません。  
`unicast` 通常モード・RAW モード全種別にはジッタを付加せず、`reorder_timeout_ms` をそのまま使用します。

### NACK 重複抑制

マルチキャスト・ブロードキャスト環境では、複数の受信者が同じ通番の NACK を短時間に送信する場合があります。  
また、同一受信者が再送の到着を待たずに同じ NACK を複数回送信する場合もあります。

送信者は直近 8 エントリのリング バッファーで NACK 受信履歴を管理し、同一通番の NACK が **200ms** 以内に再度到着した場合は処理をスキップします。

### REJECT の役割

送信者が REJECT を返却する状況は、受信者が送信ウィンドウから evict 済みの通番を要求した場合です。

受信者は REJECT を受信すると:

1. 直ちに `POTR_EVENT_DISCONNECTED` を発火します。
2. 欠落した通番をスキップし、次の通番から処理を再開できる状態になります。
3. 次のパケットが到着した時点で `POTR_EVENT_CONNECTED` を発火します。

## FIN と DATA の順序保証

### 問題の背景

UDP では `sendto()` の完了は「OS のネットワーク バッファーへの書き込み完了」を意味し、受信側への到達を保証しません。送信者が最後の DATA を送出した直後に `potr_service_close()` を呼び出すと、FIN パケットが最後の DATA より先に受信側へ到着する場合があります。

受信側が FIN を先に受け取ると、即 `POTR_EVENT_DISCONNECTED` を発火して受信ウィンドウをリセットするため、後着の DATA パケットが破棄されます。

### 解決策: FIN target の有無を flag で区別する

送信者は FIN 送出時に、現セッションで DATA を 1 件以上送信している場合だけ `POTR_FLAG_FIN_TARGET_VALID` を立てて `ack_num` へ **送信側 `send_window.next_seq`** を格納します。  
DATA を 1 件も送っていない場合は `POTR_FLAG_FIN_TARGET_VALID` を立てず、`ack_num` は無視されます。

`send_window.next_seq` は「次に送る DATA に割り当てる通番」であるため、最後の DATA が `seq=N` なら `FIN.ack_num = N + 1` になります。  
wrap 後は `FIN.ack_num = 0` も通常の有効値です。`ack_num = 0` を no-data sentinel としては扱いません。

受信側は `POTR_FLAG_FIN_TARGET_VALID` が立っている FIN かつ `recv_window.next_seq` がその値に未到達の場合、FIN 処理を **ペンディング** にしてセッションをリセットしません。  
後着の DATA がウィンドウを通過し、`recv_window.next_seq == FIN.ack_num` になった時点で DISCONNECTED を発火します。

```text
受信側の FIN 受信時の判定:
  FIN_TARGET_VALID == 0             → 即 DISCONNECTED (no-data FIN)
  FIN_TARGET_VALID == 1
    recv_window.next_seq == ack_num → 即 DISCONNECTED (ウィンドウ追い付き済み)
    recv_window.next_seq != ack_num → pending_fin = true (DATA 待ち)
                                      後続 DATA pop / skip のたびに再判定
```

### ペンディング中のエッジ ケース

FIN をペンディング中に期待する DATA が届かない場合、以下の既存機構がフォールバックとして機能します。

| シナリオ | 解消パス |
|---|---|
| 欠番 DATA が後着した | 正常に `drain_recv_window()` → `pending_fin` 解消 |
| 送信者が NACK に REJECT で応答 | `potr_internal_window_recv_skip()` → `drain_recv_window()` → `pending_fin` 解消 |
| `reorder_timeout_ms` タイムアウト後 | `potr_internal_window_recv_skip()` → 同上 |
| `health_timeout_ms` タイムアウト | ヘルス チェックが `DISCONNECTED` を発火 (既存フォールバック) |

Table: FIN ペンディング中のエッジ ケースと解消パス

ペンディング中はセッション状態をリセットしないため、欠番 DATA は引き続き受け入れられます。  
NACK / REJECT サイクルも通常どおり動作します。

詳細は [FIN 待ち合わせ仕様](fin-ordering.md) を参照してください。

## 圧縮

### 方式

**raw DEFLATE** (RFC 1951) を使用します。

| プラットフォーム | 実装 |
|---|---|
| Linux | app/zlib (windowBits = -15) |
| Windows | app/zlib (windowBits = -15) |

Table: プラットフォームごとの圧縮ライブラリ実装

両プラットフォームは同じ raw DEFLATE フォーマットを出力するため、Linux 送信者と Windows 受信者 (またはその逆) の組み合わせで透過的に動作します。

### 圧縮データのフォーマット

圧縮ペイロードの先頭 8 バイトには、展開後のサイズを NBO の uint64 で格納します。

```
[展開後サイズ (uint64, NBO, 8 バイト)] [圧縮データ (可変長)]
```

受信者はこのサイズ情報をもとに展開バッファーを準備します。

### 圧縮の適用単位

圧縮は `potr_service_send()` の `flags` 引数に `POTR_SEND_COMPRESS` を指定することで制御します。  
圧縮はメッセージ全体に適用した後にフラグメント化されます。  
圧縮済みエレメントには `POTR_FLAG_COMPRESSED` フラグが立ちます。

圧縮後のサイズが元のサイズ以上になった場合 (圧縮効果なし) は、内部で自動的に非圧縮に切り替えて送信します。  
この場合、圧縮前のデータを `POTR_FLAG_COMPRESSED` なしで送信します。

## ヘルス チェック プロトコル

### 概要

ヘルス チェック送信は通信種別を問わず設定周期で実行されます。  
`unicast` / `multicast` / `broadcast` ではヘルス チェックは **一方向** (送信者 → 受信者のみ) です。  
`unicast_bidir` / `unicast_bidir_n1` では、各エンドポイントまたは各ピアが独立して PING 送信・応答を行います (後述「双方向 UDP のヘルス チェック動作」参照)。UDP 双方向ではこの定周期 PING 送信自体が接続確立の前提であり、PING を送信しないと `CONNECTED` は成立しません。

### 送信者の動作

`health_interval_ms > 0` のときヘルス チェック スレッドが有効になります。

双方向通信では、ヘルス チェック スレッドが `health_interval_ms` 周期で PING を送信します。データ送信の有無は PING の送信周期に影響しません。  
片方向通信では、最後の PING または有効 DATA の送信から `health_interval_ms` が経過したときに PING を送信します。待機グループには DATA が流れないため、この判定を経路ごとに行います。  
双方向 UDP では、受信 PING ペイロードに相手側の `POTR_PING_STATE_NORMAL` が載るまで `CONNECTED` しないため、実効 `health_interval_ms = 0` で PING 送信が停止すると初回 `CONNECTED` に到達しません。

### 一方向通信の受信者の動作

`health_timeout_ms > 0` のときタイムアウト監視が有効になります。

受信スレッドは最終パケット受信時刻 (PING・DATA いずれも対象) を監視し、`health_timeout_ms` が経過したとき、切断と判定します。

切断判定時:

1. `health_alive` を 0 に設定します。
2. `POTR_EVENT_DISCONNECTED` をコールバックで通知します。
3. `peer_session_known` をリセットして次の接続を受け入れ可能にします。
4. 受信ウィンドウをリセットします (次に受信するパケットの `seq_num` で再初期化されます)。

次のパケットを受信したとき:

1. セッションを採用し、受信ウィンドウをそのパケットの `seq_num` で初期化します。
2. `health_alive` を 1 に設定します。
3. `POTR_EVENT_CONNECTED` をコールバックで通知します。

### PING ペイロード仕様

PING パケットのペイロードには自端の各パス PING 受信状態を格納します。

```text
[PING ペイロード: POTR_MAX_PATH バイト (= 8)]
```

各バイトがパス インデックス順に 1 バイトずつ、以下の値を取ります。

| 値 | 定数 | 意味 |
|---|---|---|
| `0` | `POTR_PING_STATE_UNDEFINED` | 不定 (片方向 / まだ有効な PING / DATA 未受信) |
| `1` | `POTR_PING_STATE_NORMAL` | 正常 (ヘルス チェック信号を継続受信中) |
| `2` | `POTR_PING_STATE_ABNORMAL` | 異常 (PING 途絶) |

Table: PING ペイロードにおける各パス受信状態の値と意味

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

### 双方向 UDP のヘルス チェック動作

#### PING パケット フォーマット (unicast_bidir / unicast_bidir_n1)

| 種別 | `flags` | `seq_num` | `ack_num` | `payload_len` |
|---|---|---|---|---|
| 要求 | `POTR_FLAG_PING` | 送信側の `next_seq` | `0` | `POTR_MAX_PATH` (非暗号化) または `POTR_MAX_PATH + POTR_CRYPTO_TAG_SIZE` (暗号化) |
| 応答 | `POTR_FLAG_PING` | 応答側の `next_seq` | 要求の `seq_num` | 同上 |

Table: 双方向 UDP における PING パケットのフォーマット

#### 両端の動作

```
1:1 モードでは両端それぞれ、N:1 モードではサーバが各ピアごとに独立して次を行います:
  health_interval_ms 周期で PING 要求（ack_num=0）を送信する
  相手から PING 要求（ack_num=0）を受信したら直ちに PING 応答（ack_num=要求の seq_num）を返却する
  last_recv_tv_sec を PING 応答受信時にも更新する
  check_health_timeout() で last_recv_tv_sec を監視 → health_timeout_ms 超過で DISCONNECTED
```

双方向 UDP の `CONNECTED` は、受信した PING ペイロードの `remote_path_ping_state[]` に少なくとも 1 つ `POTR_PING_STATE_NORMAL` が入った時点で成立します。これは「相手が自端からの PING を正常受信し、その状態を載せた PING を返してきた」ことを意味します。したがって `health_interval_ms = 0`、またはグローバル既定値として `udp_health_interval_ms = 0` かつサービス側で上書きしない構成では、初回 PING が送信されず `CONNECTED` は成立しません。

#### 双方向 UDP で last_recv_tv_sec 監視だけで十分な理由

TCP では「OS レベルの接続が生存したままアプリケーション層がハングする」状況が起こり得るため、PING 応答の個別タイムアウト監視が必要です。

UDP には接続概念がありません。相手のアプリケーションが停止すると、データ パケットも PING パケットも一切到着しなくなります。`last_recv_tv_sec` が更新されないため、`check_health_timeout()` のみで切断を検知できます。PING 応答の個別タイムアウト管理は不要です。

| モード | タイムアウト検知方法 | 理由 |
|---|---|---|
| UDP unicast | RECEIVER: `last_recv_tv_sec` 監視 | 片方向 type 1-6 は有効な `PING` / `DATA` 受信で更新 |
| UDP unicast_bidir | 1:1 は両端、N:1 は各ピア単位で `last_recv_tv_sec` を監視 | 相手停止で当該相手からの全パケットが途絶える → timeout 発火 |
| TCP / TCP_bidir | SENDER: PING 応答タイムアウト監視 | OS 接続が生存したままアプリケーションがハングし得るため |

Table: 通信モードごとのタイムアウト検知方法と選定理由

### TCP 通信種別のヘルス チェック動作

#### PING 送信のタイミング

TCP では接続直後に bootstrap PING を送信し、`tcp_health_interval_ms > 0` のときだけ定周期 PING を送信します。  
定周期 PING は UDP と同様にデータ送信頻度の影響を受けません。  
TCP では加えて、「OS 接続が生存したままアプリケーション層がハングする」状況に対応するため、`tcp_health_interval_ms > 0` のときだけ PING 要求 / 応答を個別に監視します。  
初回 CONNECTED は、受信した PING ペイロードの `remote_path_ping_state[]` に少なくとも 1 つ `POTR_PING_STATE_NORMAL` が載った時点で成立します。これは「自端が先に送信した bootstrap PING を相手が受信し、その結果を載せた応答 PING が戻ってきた」ことを意味します。

#### タイムアウト監視

| 通信種別 | 役割 | 監視内容 |
|---|---|---|
| UDP unicast 等 | RECEIVER | `last_recv_tv_sec` が `udp_health_timeout_ms` を超えたら DISCONNECTED |
| TCP / TCP_BIDIR | SENDER | `tcp_health_interval_ms > 0` のとき、PING 応答 (`ack_num > 0`) が `tcp_health_timeout_ms` 以内に返却されなければ DISCONNECTED (health スレッド) |
| TCP / TCP_BIDIR | RECEIVER | `tcp_health_interval_ms > 0` のとき、PING 要求 (`ack_num = 0`) が `tcp_health_timeout_ms` 以内に到着しなければ DISCONNECTED (recv スレッド) |
| TCP_BIDIR | SENDER | `tcp_health_interval_ms > 0` のとき、PING 要求 (`ack_num = 0`) が `tcp_health_timeout_ms` 以内に到着しなければ DISCONNECTED (recv スレッド) |
| TCP_BIDIR | RECEIVER | `tcp_health_interval_ms > 0` のとき、PING 応答 (`ack_num > 0`) が `tcp_health_timeout_ms` 以内に返却されなければ DISCONNECTED (health スレッド) |

Table: 通信種別および役割ごとのタイムアウト監視内容

TCP は bootstrap PING の往復で初回 CONNECTED を確立し、`tcp_health_interval_ms > 0` のときだけ定周期 PING により両端が互いの生死を独立して監視できます。  
`tcp_bidir` では両端が PING 送信側・受信側の両方を兼ねるため、PING 応答タイムアウト (health スレッド) と PING 要求到着タイムアウト (recv スレッド) の 2 種類を両端で監視します。

## 受信報告・ハンドシェイク・鍵配布パケット

### 受信報告 (POTR_FLAG_REPORT)

片方向・一対多の通常モード (`unicast`、`multicast`、`broadcast`) の受信者が送信者へ経路ごとの受信状態を通知するためのパケットです (RAW モードでは送出しません)。

| 項目 | 内容 |
|---|---|
| `flags` | `POTR_FLAG_REPORT` (`0x0100`)。暗号化時は `POTR_FLAG_ENCRYPTED` を併用 |
| セッション識別子 | 受信者が `potr_service_open()` 時に生成する自身のセッション識別子 (`session_id`、`session_tv_sec`、`session_tv_nsec`) |
| `seq_num`、`ack_num` | `0` |
| ペイロード | 経路ごとに 1 バイトの受信状態 (`POTR_PING_STATE_*`)、`POTR_MAX_PATH` バイト (8 バイト)。PING ペイロードと同じ形式 |
| 暗号化 | 有効時は PING と同じくペイロードを暗号化し、72 バイトのヘッダー全体を AAD として末尾に 16 バイトの GCM 認証タグを付加 |
| 宛先 | 送信者のアドレスとポート (NACK と同じ) |

Table: 受信報告パケット (`POTR_FLAG_REPORT`) の形式

受信者は実効 `health_interval_ms` ごとに全経路から送信します。`multicast` と `broadcast` では報告の同時集中を避けるため、各回の間隔を `health_interval_ms` の 100%〜150% の範囲でばらつかせます。

### ハンドシェイク (POTR_FLAG_HANDSHAKE)

証明書方式 (`auth = "cert"`) で、相互認証と鍵確立のバイト列を運ぶためのパケットです。

| 項目 | 内容 |
|---|---|
| `flags` | `POTR_FLAG_HANDSHAKE` (`0x0200`) |
| `seq_num`、`ack_num` | `0` |
| ペイロード | UDP (全通信種別) は DTLS 1.2、TCP (`tcp`、`tcp_bidir`) は TLS 1.3 のハンドシェイク バイト列 |
| 暗号の組 | 一時的な鍵交換 (ECDHE) と認証付き暗号 (AES-256-GCM) に限定 |
| 鍵の導出 | RFC 5705 (DTLS 1.2) / RFC 8446 7.5 節 (TLS 1.3) により、ラベル `EXPORTER-porter-v2` と下記の導出文脈を用いて 64 バイトを導出し、先頭 32 バイトをクライアントからサーバー方向、後半 32 バイトをサーバーからクライアント方向の鍵に割り当てます。 |

Table: ハンドシェイク パケット (`POTR_FLAG_HANDSHAKE`) の形式

鍵の導出に用いる文脈 (32 バイト) は、クライアントの 16 バイトのセッション識別子の後に、サーバーの 16 バイトのセッション識別子を連結して構成します。各セッション識別子は、`session_tv_sec` (8 バイト)、`session_id` (4 バイト)、`session_tv_nsec` (4 バイト) の順のネットワーク バイト オーダー (NBO) 表現です。

ハンドシェイクが完了するまで、ハンドシェイク以外のパケットは破棄し、経路状態、受信ウィンドウ、セッション状態を変更しません。  
片方向の `unicast` (`unicast_raw` を含む) ではグループ鍵を使用せず、DATA および制御パケットを含めて、ハンドシェイクで導出した受信者ごとの鍵 (方向別の鍵) で保護します。  
UDP で実効 `health_interval_ms` が `0` の場合でも、認証の再試行は UDP の既定の監視間隔 (`POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS` = 3,000 ms) で行います。

### 鍵配布 (POTR_FLAG_KEY)

証明書方式 (`auth = "cert"`) の一対多通信 (`multicast`、`broadcast` と各 RAW モード) で、送信者が生成した 32 バイトのグループ鍵を受信者へ配布するためのパケットです。

| 項目 | 内容 |
|---|---|
| `flags` | `POTR_FLAG_KEY` (`0x0400`) \| `POTR_FLAG_ENCRYPTED` (`0x0020`) |
| `seq_num`、`ack_num` | `0` |
| `payload_len` | `48` (暗号化したグループ鍵 32 バイト + GCM 認証タグ 16 バイト) |
| ペイロード | 送信者がセッションごとに生成した 32 バイトのグループ鍵を、ハンドシェイクで導出した送信者から受信者方向の鍵を用いて AES-256-GCM で暗号化したデータ (ノンスは `session_id` 4 バイト + `tx_counter` 8 バイト、AAD は 72 バイトのヘッダー全体) と、末尾の 16 バイトの GCM 認証タグ |

Table: 鍵配布パケット (`POTR_FLAG_KEY`) の形式

一対多の証明書方式では、DATA、PING、REJECT、FIN はグループ鍵で保護し、受信報告 (`POTR_FLAG_REPORT`) と NACK は受信者ごとの鍵で保護します。

KEY パケットが受信者に届かない場合、受信者はハンドシェイク完了後にグループ鍵が未取得のまま再試行間隔 (実効 `health_interval_ms`、0 の場合は `POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS`) が経過すると、現在の認証状態を破棄してハンドシェイクをやり直します。送信者は、再試行間隔の経過後に受信者から開始用のハンドシェイク (DTLS の epoch 0 の ClientHello) を受信すると既存の認証セッションを破棄してハンドシェイクを受け付け直し、完了時に `POTR_FLAG_KEY` を再送します。

## マルチパスと送出経路

送信者は最大 8 経路のソケットを同時に保持できます。  
DATA と再送パケットは選択中の優先グループ (切り戻し中は上位グループと選択中のグループの 2 グループ) の経路へ送出し、監視や制御のためのパケットは全グループの全経路へ送出します。

| パケット | 送出経路 |
|---|---|
| DATA | 選択中のグループ (切り戻し中は 2 グループ) の経路 |
| 再送 (NACK への応答) | 選択中のグループの経路 |
| PING | 全グループの全経路 (待機グループの監視を含む) |
| NACK、受信報告 | 受信者の全経路 |
| REJECT | 選択中のグループの経路 |
| FIN、FIN_ACK | 全グループの全経路 |
| ハンドシェイク、鍵配布 | 全グループの全経路 |

Table: パケット種別ごとの送出経路

NACK パケットと受信報告パケットは、マルチキャスト・ブロードキャスト環境でも送信者へのユニキャストで返却され、受信者が保持する全パスのソケットからそれぞれ送信されます。

## 定数・上限値

| 定数 | 値 | 説明 |
|---|---|---|
| `POTR_PROTOCOL_VERSION` | 2 | 現行のワイヤー プロトコル バージョン |
| `POTR_PACKET_HEADER_WIRE_SIZE` | 72 バイト | 外側パケット ヘッダーの固定長 |
| `POTR_DEFAULT_MAX_PAYLOAD` | 1,400 バイト | DATA パケットのペイロード上限の既定値 |
| `POTR_MAX_PAYLOAD` | 65,435 バイト | `max_payload` 設定値の検査上限 (`POTR_UDP_MAX_DATAGRAM_PAYLOAD` 65,507 - `POTR_PACKET_HEADER_WIRE_SIZE` 72) |
| `POTR_MAX_WINDOW_SIZE` | 256 | スライディング ウィンドウの最大スロット数 |
| `POTR_MAX_MESSAGE_SIZE` | 65,535 バイト | 1 回の `potr_service_send()` で送信できるデータの上限 |
| `POTR_MAX_PATH` | 8 | マルチパスの最大経路数 |
| `POTR_MAX_PATH_GROUP` | 4 | 経路の優先グループ番号の最大値 (1〜4) |
| `POTR_SEND_QUEUE_DEPTH` | 1,024 | 送信キューの最大エレメント数 |
| `POTR_PAYLOAD_ELEM_HDR_SIZE` | 6 バイト | ペイロード エレメント ヘッダーの固定長 |
| `POTR_NACK_DEDUP_MS` | 200 ms | NACK 重複抑制の時間窓 |
| `POTR_DEFAULT_RECONNECT_INTERVAL_MS` | 5,000 ms | TCP SENDER 自動再接続間隔デフォルト |
| `POTR_DEFAULT_CONNECT_TIMEOUT_MS` | 10,000 ms | TCP SENDER 接続タイムアウトの既定値 |
| `POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS` | 3,000 ms | UDP 既定のヘルス チェック送信間隔 |
| `POTR_DEFAULT_UDP_HEALTH_TIMEOUT_MS` | 10,000 ms | UDP 既定のヘルス チェック タイムアウト |
| `POTR_DEFAULT_TCP_HEALTH_INTERVAL_MS` | 10,000 ms | TCP 既定のヘルス チェック送信間隔 |
| `POTR_DEFAULT_TCP_HEALTH_TIMEOUT_MS` | 31,000 ms | TCP 既定のヘルス チェック タイムアウト |
| `POTR_DEFAULT_TCP_CLOSE_TIMEOUT_MS` | 5,000 ms | TCP close 完了待機タイムアウト |
| `POTR_CRYPTO_KEY_SIZE` | 32 バイト | AES-256-GCM 鍵サイズ |
| `POTR_CRYPTO_NONCE_SIZE` | 12 バイト | AES-256-GCM ノンス サイズ |
| `POTR_CRYPTO_TAG_SIZE` | 16 バイト | AES-256-GCM 認証タグ サイズ |

Table: porter プロトコルの定数および上限値一覧
