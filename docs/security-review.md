# porter 暗号化版 セキュリティレビュー

## 結論

porter の暗号化ありバージョンは、`AES-256-GCM` により **ペイロードの機密性** と **ヘッダを含む完全性検証** を提供しています。そのため、**参加ノードが限定され、設定ファイルや運用が適切に管理された閉域網で利用する分には、現時点でも大きな問題が出にくい構成** と考えてよいです。

一方で、より厳密な安全性を求める場合や、将来的に利用範囲を広げる場合には、**鍵管理**, **相手認証**, **ノンス生成**, **時刻依存のセッション採用** に改善余地があります。

特に次の点は、閉域網では直ちに致命的でない場合があるものの、設計上の注意点または改善候補として認識しておくのが望ましいです。

- GCM ノンスが `session_id + flags + seq_or_ack_num` であり、`session_id` が `rand()` ベースで生成されるため、再起動条件によっては nonce reuse の設計リスクが残る。
- `encrypt_key` は事前共有鍵であり、passphrase 形式は `SHA-256` 直 hash で導出されるため、鍵管理・耐総当たり性の観点では改善余地がある。
- 暗号化は「同じ鍵を持っていること」の証明にとどまり、一般的な相手認証やピア単位の認可までは提供しない。
- `CLOCK_REALTIME` に依存するセッション採用のため、docs が認める通り、特定条件では新セッションを永続的に破棄し得る。

## 評価対象と前提

本レビューは、`prod/porter/docs` と `prod/porter/libsrc/porter` / `prod/porter/include` の実装をもとに、porter の UDP 系暗号化機能を対象に評価したものです。

前提は以下です。

- 対象は porter 自身の暗号化・セッション管理・ピア識別・設定方式である。
- 「鍵が外部に漏えいしない」という仮定は置くが、**弱い passphrase**, **設定ファイル露出**, **正規ノード間の共有鍵運用**, **通信路上の DoS**, **時刻異常** は評価対象に含める。
- 暗号アルゴリズム (`AES-256-GCM`) の理論的安全性ではなく、**この実装・このプロトコル設計での安全性** を問う。

## docs / 実装から確認できる事実

### 暗号化範囲

- docs は、DATA パケットのペイロードを `AES-256-GCM` で暗号化し、ヘッダ 32 バイトは平文のまま AAD で認証すると説明している。`PING / NACK / REJECT / FIN` は、ヘッダに対する 16 バイトの GCM タグのみを付与する。  
  参照: `prod/porter/docs/config.md:181-207`
- 実装でも、Linux 版 `potr_encrypt()` / `potr_decrypt()` は OpenSSL EVP により AAD を与えた `AES-256-GCM` を実装している。  
  参照: `prod/porter/libsrc/porter/infra/crypto/crypto_linux.c:29-243`

### ノンス構成

- docs は、GCM ノンスを `[session_id][flags][seq_or_ack_num][padding]` としている。  
  参照: `prod/porter/docs/config.md:194-207`
- 実装でも、送信時に `session_id`, `flags`, `seq_num` または `ack_num` からノンスを組み立てている。  
  参照: `prod/porter/libsrc/porter/thread/potrSendThread.c:121-149`, `prod/porter/libsrc/porter/thread/potrHealthThread.c:207-245`, `prod/porter/libsrc/porter/thread/potrRecvThread.c:62-99`

### セッション識別

- docs は、`session_id` を「現在時刻 × PID をシードにした乱数」、`session_tv_sec` / `session_tv_nsec` を `CLOCK_REALTIME` 由来の時刻として説明している。  
  参照: `prod/porter/docs/protocol.md:123-170`
- 実装でも、Linux は `srand(time(NULL) ^ getpid())` → `rand()`、Windows は `GetTickCount() ^ GetCurrentProcessId()` → `rand()` で `session_id` を生成している。  
  参照: `prod/porter/libsrc/porter/api/potrOpenService.c:83-108`, `prod/porter/libsrc/porter/potrPeerTable.c:50-75`

### N:1 モードのピア識別

- docs は、N:1 モードで受信スレッドが `session triplet` (`session_id`, `session_tv_sec`, `session_tv_nsec`) を使ってピアを検索 / 新規作成すると説明している。  
  参照: `prod/porter/docs/architecture.md:143-155`
- 実装でも、`peer_find_by_session()` は送信元 IP:Port ではなく session triplet でピア検索を行う。  
  参照: `prod/porter/libsrc/porter/potrPeerTable.c:268-289`

## 改善ポイント

### 1. GCM ノンス設計が `session_id` の強度に依存しており、nonce reuse の設計リスクがある

#### 何が問題か

GCM の安全性は **同一鍵でノンスを再利用しないこと** を強く要求する。porter ではノンスが `session_id + flags + seq_or_ack_num` で構成される一方、`session_id` は暗号学的乱数ではなく `rand()` に依存している。さらに `session_tv_sec` / `session_tv_nsec` はノンスに含まれない。

このため、再起動条件が重なって `session_id` が再現され、`seq_num` が再び同じ値から進み始めると、**同一鍵・同一ノンスの再使用** が起こり得る設計になっている。

#### なぜ成立するか

- docs のノンス定義に timestamp は含まれていない。  
  参照: `prod/porter/docs/config.md:194-207`
- `session_id` は docs 上も「現在時刻 × PID をシードにした乱数」であり、実装は `srand(...); rand();` で生成している。  
  参照: `prod/porter/docs/protocol.md:127-131`, `prod/porter/libsrc/porter/api/potrOpenService.c:83-108`
- 新セッションでも `seq_num` は 0 から始まる。  
  参照: `prod/porter/docs/protocol.md:197-209`

#### 影響

- nonce reuse が発生すると、`AES-GCM` の機密性・完全性は破綻し得る。
- 実際の再現条件は運用環境に依存し、閉域網で通常運用しているだけで直ちに問題化するとは限らないが、将来的な堅牢化の観点では優先的に見直したい点である。

### 2. 相手認証は事前共有鍵の所持確認に留まり、ピア単位の認証・認可までは提供しない

#### 何が問題か

porter の暗号化は、「同じ `encrypt_key` を持っていること」を前提に通信を成立させる設計であり、TLS 証明書や署名鍵のような **ピア固有の身元証明** を提供しない。

特にマルチキャストや N:1 モードでは、共有鍵を持つノード群を区別できず、**どの正規ノードが送ったのか** を暗号的には識別できない。

#### なぜ成立するか

- docs は `encrypt_key` を事前共有鍵として説明しており、マルチキャストでは「受信者全員が同一の `encrypt_key` を持っていれば動作する」としている。  
  参照: `prod/porter/docs/config.md:181-192`
- N:1 モードではピア識別の主軸が session triplet であり、証明書や公開鍵による識別はない。  
  参照: `prod/porter/docs/architecture.md:143-155`, `prod/porter/libsrc/porter/potrPeerTable.c:268-289`

#### 影響

- 単一の共有鍵に依存するため、正規参加者が増えるほど責任分界が曖昧になる。
- 閉域網で少数ノードを固定運用する分には許容しやすいが、ノード数や運用主体が増える場合は、共有鍵だけに依存しない設計へ寄せる余地がある。

### 3. 鍵管理はシンプルだが、設定ファイルの事前共有鍵と SHA-256 直 hash に依存している

#### 何が問題か

`encrypt_key` は設定ファイルに置かれる事前共有鍵であり、passphrase 形式を使う場合の鍵導出は `SHA-256(passphrase)` のみである。閉域網で設定ファイル管理を厳密に行うなら運用可能だが、salt も work factor もなく、KDF としては強くない。

#### なぜ成立するか

- docs は、`encrypt_key` を設定ファイル中の文字列として受け付け、64 桁 hex 以外は SHA-256 で 32 バイト鍵に変換すると説明している。  
  参照: `prod/porter/docs/config.md:117-120`, `prod/porter/docs/config.md:181-192`
- 実装も、非 hex 文字列を `potr_passphrase_to_key()` で SHA-256 に通してそのまま鍵にしている。  
  参照: `prod/porter/libsrc/porter/protocol/config.c:372-444`, `prod/porter/libsrc/porter/infra/crypto/crypto_linux.c:213-242`

#### 影響

- 弱い passphrase を使うと、設定ファイルやメモリダンプ取得後のオフライン総当たり耐性が低い。
- 現在の閉域網運用では十分でも、鍵ローテーションや前方秘匿性を求めるなら改善が必要である。

### 4. `CLOCK_REALTIME` ベースのセッション採用には、時刻逆行時の運用制約がある

#### 何が問題か

docs 自身が認めている通り、`health_timeout_ms = 0` の構成では、OS 時計が過去へ戻ると新セッションが旧セッション扱いされ、永続的に破棄され得る。

これは直接の暗号破りではないが、**時刻異常だけで復旧不能に近い状態へ入る** 可能性があるため、可用性上の運用制約として無視できない。

#### なぜ成立するか

- docs は、受信者が `session_tv_sec`, `session_tv_nsec`, `session_id` の辞書順で新旧セッションを判定すると明記している。  
  参照: `prod/porter/docs/protocol.md:136-145`
- 同じ docs で、`health_timeout_ms = 0` の場合は `peer_session_known` がクリアされず、時刻逆行した新セッションを永続的に破棄すると述べている。  
  参照: `prod/porter/docs/protocol.md:151-170`
- 実装も `CLOCK_REALTIME` をセッション時刻として使用している。  
  参照: `prod/porter/libsrc/porter/api/potrOpenService.c:99-107`

#### 影響

- NTP ステップ修正や手動時刻変更で通信断が長期化し得る。
- 通常運用では顕在化しないかもしれないが、障害時の復旧容易性を考えると改善候補である。

## 設計上の制約・注意点

### ヘッダは平文であり、メタデータ秘匿は提供しない

docs は明確に、ヘッダ 32 バイトは平文のまま AAD で保護すると説明している。  
参照: `prod/porter/docs/config.md:181-189`

したがって、`service_id`, `session_id`, `seq_num`, `ack_num`, `flags`, `payload_len` などのメタデータは観測可能である。改ざんは検知できても、**通信内容の全体像が秘匿されるわけではない**。

### forward secrecy / 動的鍵交換 / 鍵ローテーションは提供しない

docs / 実装には、鍵交換, 再ネゴシエーション, セッション鍵導出の仕組みが見当たらない。  
参照: `prod/porter/docs/config.md:181-192`, `prod/porter/libsrc/porter/protocol/config.c:372-444`

これは直ちに問題というより設計上の前提であり、安全性を「鍵が漏れない限り」と表現する場合、**その鍵を長期に固定で使う設計** であることを併記するのが適切である。

## 総評

porter の暗号化ありバージョンは、**ヘッダ改ざん検知** と **ペイロード暗号化** を実装しており、閉域網でノードと設定を適切に管理して使うのであれば、現時点でも十分に実用的です。

ただし、より強い安全性や将来の拡張性を求めるのであれば、以下は改善候補として押さえておきたいです。

1. GCM ノンス設計が `session_id` 生成の強度に依存している。  
2. 相手認証は共有鍵の所持確認止まりで、ピア固有の認証になっていない。  
3. 鍵管理は事前共有鍵 + 平文設定 + SHA-256 直 hash に依存している。  
4. `CLOCK_REALTIME` ベースのセッション採用に可用性上の既知制限がある。  

従って、porter の暗号化は **閉域網での利用において十分に有用** である一方、**より強い脅威モデルに耐えるためには追加改善の余地がある**、というのが妥当な評価です。

## 推奨対応

- `session_id` を暗号学的乱数で生成し、必要なら nonce に timestamp ではなく **再起動後も衝突しない単調値** を組み込む。
- passphrase 形式は廃止するか、少なくとも PBKDF2 / scrypt / Argon2 のような KDF に置き換える。
- 共有鍵だけでなく、用途に応じてピア固有認証（証明書, 公開鍵, ノード ID 署名など）を導入する。
- `CLOCK_REALTIME` 依存のセッション採用を見直し、時刻逆行時も安全に回復できる設計にする。

## 対応済みメモ

- 2026-04: `encrypt_enabled` 時は受信直後に `POTR_FLAG_ENCRYPTED` を必須化し、GCM タグ検証成功後のみ後続処理へ進めるよう修正した。
- これにより、N:1 モードで未認証パケットが `peer_create()` に到達して `peer slot` を消費する経路は解消した。
