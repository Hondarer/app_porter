# プラットフォーム抽象化規範

一般的な判定マクロとファイル分割は、[cplat のプラットフォーム抽象化規範](../../cplat/docs/platform-abstraction-guideline.md) に従います。

## porter 固有の規則

porter の製品コード (`prod/libsrc/porter/`、`prod/include/`、`prod/include_internal/`) に `PLATFORM_*` による分岐を置きません。

> [!IMPORTANT]
> OS 差異の吸収は cplat の責務であり、porter は cplat が提供する OS 非依存の API だけを呼び出します。
> 新しい OS 分岐が必要になった場合は、porter 側へ書くのではなく cplat の対応するカテゴリを拡張してください。

porter は通信に `cplat` の `net` カテゴリを使用し、OS のソケット API (`socket`、`bind`、`listen`、`accept`、`connect`、`getsockopt`、`setsockopt`、`sendto`、`recvfrom`、`poll`、`select` など) を直接呼び出しません。

ソケット ハンドルには `cplat_socket`、無効値には `CPLAT_INVALID_SOCKET` を使用します。  
数値リテラルの `-1` や `INVALID_SOCKET` との比較は行いません。

アドレスとポートの受け渡しには `cplat_ipv4_endpoint` を使用し、`struct sockaddr_in` を扱いません。

16 ビットおよび 32 ビット値のバイト オーダー変換には `cplat_hton16`、`cplat_ntoh16`、`cplat_hton32`、`cplat_ntoh32` を使用し、`htons` などの OS API を直接呼び出しません。

ソケット API の失敗は `cplat_error` に格納されます。  
呼び出し側は `errno` や `WSAGetLastError()` を直接参照せず、`cplat_error_is()` で `cplat_error_cause` の値を判定します。

> [!NOTE]
> ワーカー スレッドで記録した直前値はそのスレッドだけに属するため、非同期の失敗はトレースにも詳細を記録します。

`cplat/net` の初期化と終了は API 内部で行われるため、porter が `potr_socket_lib_init()` 相当の呼び出しを行うことはありません。

`cplat/net` カテゴリの API 契約 (複数ソケットの待機、受信半クローズの非対称性、`getaddrinfo` 由来エラーの扱いなど) は [ネットワーク API ガイドライン](../../cplat/docs/net-api-guideline.md) を参照してください。porter 側では同じ内容を繰り返しません。

## 移行前との対応

| 移行前 (porter) | 移行後 (cplat) |
|---|---|
| `PotrSocket` | `cplat_socket` |
| `POTR_INVALID_SOCKET` | `CPLAT_INVALID_SOCKET` |
| `struct sockaddr_in` | `cplat_ipv4_endpoint` |
| `potr_hton16` など | `cplat_hton16` など |
| `potr_socket_cause_t` / `POTR_SOCKET_CAUSE_*` | `cplat_error_cause` / `CPLAT_CAUSE_*` |
| `potr_socket_error_is()` | `cplat_error_is()` |
| `potr_socket_lib_init()` / `potr_socket_lib_cleanup()` | 削除 (`cplat/net` が内部で初期化) |

## コンパイラ依存の分岐

pragma や属性の分岐が必要な場合は `COMPILER_*` を使用し、OS 分岐の `PLATFORM_*` と軸を混在させません。

> [!NOTE]
> 片側専用の Linux ソースが Windows + MSVC でも走査される構成では、空翻訳単位の警告 (C4206) を抑止する目的で `COMPILER_MSVC` の分岐が残る場合があります。
> これは cplat のプラットフォーム抽象化規範に定めるファイル分割の一般則であり、porter 固有の規則ではありません。

## 検証方法

以下の grep で、porter の製品コードに OS API や `PLATFORM_*` が残っていないことを確認します。

```bash
# OS のソケット API を直接呼び出していないこと
grep -rnE "\b(socket|bind|listen|accept|connect|getsockopt|setsockopt|sendto|recvfrom|poll|select)\s*\(" app/porter/prod/libsrc/ app/porter/prod/include/ app/porter/prod/include_internal/

# PLATFORM_* による分岐が残っていないこと
grep -rn "PLATFORM_LINUX\|PLATFORM_WINDOWS" app/porter/prod/libsrc/ app/porter/prod/include/ app/porter/prod/include_internal/

# errno / WSAGetLastError の直接参照が残っていないこと
grep -rn "errno\|WSAGetLastError" app/porter/prod/libsrc/ app/porter/prod/include/ app/porter/prod/include_internal/

# htons などの OS バイトオーダー API を直接呼び出していないこと
grep -rnE "\b(htons|ntohs|htonl|ntohl)\s*\(" app/porter/prod/libsrc/ app/porter/prod/include/ app/porter/prod/include_internal/
```

> [!NOTE]
> 1 番目の grep は、トレース文字列 (`"connect() failed"` など) や Doxygen コメント中の API 名にもヒットします。  
> これらは呼び出しではなく記述的な文言であるため、該当ヒットは対象外として扱ってください。  
> コードとしての直接呼び出しかどうかは、ヒットした行が文字列リテラルまたはコメントの内側にないかで判断します。

いずれも該当なしとなることを確認してください。
