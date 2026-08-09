# プラットフォーム抽象化規範

一般的な判定マクロとファイル分割は、[com_util のプラットフォーム抽象化規範](../../com_util/docs/platform-abstraction-guideline.md) に従います。

## porter 固有の規則

`prod/libsrc/porter/` で `PLATFORM_*` による分岐を記述できるのは、`infra/potrPlatform*.c`、`infra/potrSocketError*.c`、`util/potrIpAddr.c` だけです。  
ソケット型には `PotrSocket`、無効値には `POTR_INVALID_SOCKET` を使用します。  
呼び出し側は `socket`、`bind`、`listen`、`accept`、`connect`、`getsockopt`、`setsockopt`、`sendto`、`recvfrom`、`poll`、`select` を直接呼び出さず、`potrPlatform.h` の内部 API を使用します。  
16 ビットおよび 32 ビット値のバイト オーダー変換には `potr_hton16`、`potr_ntoh16`、`potr_hton32`、`potr_ntoh32` を使用し、`hton*` と `ntoh*` を直接呼び出しません。

ソケット API の失敗は `com_util_error` に捕捉します。  
呼び出し側は `errno` や `WSAGetLastError()` を直接参照せず、`potr_socket_error_is()` で `POTR_SOCKET_CAUSE_*` を判定します。  
ワーカー スレッドで記録した直前値はそのスレッドだけに属するため、非同期の失敗はトレースにも詳細を記録します。  
`getaddrinfo` が返す EAI エラーは errno や Winsock エラーと異なる体系であるため `com_util_error` へ格納せず、コードとメッセージをトレースへ記録します。  
複数ソケットの poll へ無効ソケットだけを渡した場合は、読み取り可能なソケットがない正常状態として成功を返します。  
ブロードキャスト送信には `SO_BROADCAST` が必須であるため、このオプションの設定失敗はサービス開始の失敗として扱います。

片側専用の Linux ソースは、Windows と MSVC の組み合わせで空翻訳単位の警告を抑止します。  
pragma や属性の分岐には `COMPILER_*`、OS API の分岐には `PLATFORM_*` を使用し、分岐の軸を混在させません。
