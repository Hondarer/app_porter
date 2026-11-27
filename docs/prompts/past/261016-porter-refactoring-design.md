<!--
生成元: agree-before-implement スキル
文書種別: 最終仕様 (design)
検討 ID: porter-refactoring-20261016
版: 2
上流文書: 261016-porter-refactoring-discussion.md (合意版 7)。削除されている場合があります。
下流文書: 261016-porter-refactoring-backlog.md。未作成または削除されている場合があります。
再開手順: agree-before-implement スキルを読み、上流文書が存在する場合はその最新版と WBS を確認してください。存在しない場合は、この文書の承認状態と実際の差分を照合してください。
-->

# cplat の固定幅整数解析と porter の設定解析の共通化

## 目的と実施順序

cplat に固定幅整数の解析 API をそろえ、porter の設定解析に重複する型別変換を置き換えます。  
整数の文字列表現、完全消費、型の範囲、失敗時の結果格納は cplat が担い、設定項目の既定値、採用条件、業務上の制約は porter が担います。

cplat の公開契約、実装、モック、テスト、文書を先に整え、局所検証後に porter を移行します。  
資源解放、接続・受信スレッド、ソケット準備の分割は今回の対象に含めません。

状態: 完了。最終仕様版 2 の実装、局所検証、両 OS の CI 確認、警告解消を完了し、2026-10-18 に成果物の最終承認を受付済みです。

## 固定幅の符号付き・符号なし整数を解析する

`app/cplat/prod/include/cplat/crt/stdlib.h` に、既存のエクスポート指定と呼び出し規約を使って次の関数を追加します。

```c
int cplat_parse_int8(int8_t *value_out, const char *text, int base);
int cplat_parse_int16(int16_t *value_out, const char *text, int base);
int cplat_parse_int32(int32_t *value_out, const char *text, int base);
int cplat_parse_uint8(uint8_t *value_out, const char *text, int base);
int cplat_parse_uint16(uint16_t *value_out, const char *text, int base);
int cplat_parse_uint32(uint32_t *value_out, const char *text, int base);
```

既存の `cplat_parse_int64` と `cplat_parse_uint64` を合わせ、符号付き・符号なしの 8/16/32/64 bit の 8 型をカバーします。  
既存の `cplat_parse_int` と `cplat_parse_double` は維持します。  
cplat の数値変換規範に従い、幅が処理系に依存する `long` を返す API は追加しません。

### 入力と戻り値

出力先と入力文字列を必須引数とし、基数は既存 API と同じく 0 または 2〜36 を受け付けます。  
0 は接頭辞による自動判別です。  
先頭空白と正号の扱い、末尾文字の検出は、委譲先の既存 64 bit 解析と同じです。

Table: 追加する整数解析 API の結果契約

| 条件 | 結果 |
| --- | --- |
| 出力先または文字列が NULL、基数が無効 | `CPLAT_ERR_INVALID_ARGUMENT` |
| 文字列を整数として完全に解釈できない | `CPLAT_ERR_INVALID_INTEGER` |
| 64 bit 解析で範囲外、または結果の型で表現できない | `CPLAT_ERR_OUT_OF_RANGE` |
| 符号なし解析で先頭空白の後に負号がある | 文字列の残りの内容に先立って `CPLAT_ERR_OUT_OF_RANGE` |
| 検査に成功 | 結果を出力先へ格納して `CPLAT_OK` |

無効引数の検査は解析に先立って行います。  
失敗時は有効な出力先の値を変更しません。  
符号付き解析は `"-0"` を 0 として受理し、符号なし解析は `"-0"` と `"-x"` を範囲外として拒否します。

### 実装と所有権

実装は既存の `app/cplat/prod/libsrc/cplat/crt/stdlib.c` に置きます。  
符号付きの 8/16/32 bit は `cplat_parse_int64`、符号なしの 8/16/32 bit は `cplat_parse_uint64` へ委譲します。  
64 bit のローカル変数へ解析し、型ごとの最小値・最大値を検査した後で縮小変換して出力へ格納します。  
出力ポインターを別の整数型へキャストして渡しません。

動的確保と共有可変状態は追加しません。  
呼び出し元が用意した出力先だけを成功時に更新し、既存の数値解析と同じスレッド セーフ性を持ちます。  
既存 64 bit 解析の受理条件とエラーの優先順位は変更しません。

## 基本整数型 9 型を固定幅整数の解析へ委譲する

対象環境は x86_64 の Linux/GCC と Windows/MSVC とし、char は符号付き 8 bit を前提とします。  
基本整数型のうち long と unsigned long は利用対象外とし、解析 API を提供しない規範を維持します。  
既存の int に加え、次の 8 関数を公開します。

```c
int cplat_parse_char(char *value_out, const char *text, int base);
int cplat_parse_schar(signed char *value_out, const char *text, int base);
int cplat_parse_uchar(unsigned char *value_out, const char *text, int base);
int cplat_parse_short(short *value_out, const char *text, int base);
int cplat_parse_ushort(unsigned short *value_out, const char *text, int base);
int cplat_parse_uint(unsigned int *value_out, const char *text, int base);
int cplat_parse_longlong(long long *value_out, const char *text, int base);
int cplat_parse_ulonglong(unsigned long long *value_out, const char *text, int base);
```

char と signed char は int8、unsigned char は uint8、short は int16、unsigned short は uint16 の解析へ委譲します。  
既存の `cplat_parse_int` は int32、unsigned int は uint32、long long は int64、unsigned long long は uint64 の解析へ委譲します。  
ここで int8 などの表記は、対応する `cplat_parse_int8` などの関数を指します。  
基本型から対応する固定幅型へ委譲し、固定幅型から 64 bit の解析本体へ委譲する 2 段階の構成です。  
対応先が 64 bit の基本型は、解析本体へ直接委譲します。  
各関数は出力先が NULL でないことを確認し、委譲先の固定幅整数型のローカル変数へ解析して、成功時だけ目的型の出力先へ格納します。  
出力ポインターの型を変換して委譲しません。  
入力、基数、エラーの優先順位、符号なしの負号拒否は固定幅解析にそろえ、失敗時の出力値を保持します。  
既存 int の公開シグネチャと受理する値域を維持します。

char の符号と、char 系 8 bit・short 系 16 bit・int 系 32 bit・long long 系 64 bit の前提をコンパイル時に検査します。  
char の符号を変えるビルド設定など、これらに反する構成を黙って受理しません。  
char と signed char は別の基本型として、専用の公開関数をそれぞれ提供します。  
_Bool、用途別の整数型、float、long double の解析 API は追加しません。  
既存の固定幅整数 8 型と double は維持します。

8 関数の mock、実関数への既定委譲、Windows のリンク保持、exportTest を公開シグネチャと同期します。  
公開コメント、API 逆引き、数値変換規範、機能仕様に基本型と固定幅型の選択を反映し、long 系の非提供方針を維持します。  
機能仕様には API 名や委譲手順を記載しません。

`crt/stdlibTest/basicTypeParseTest.cc` に基本型 9 型の成功、上下限、範囲外、NULL、不正文字、基数、失敗時の出力保持を確認するテストを配置します。  
符号なしの負号拒否、char の -128〜127、int の 127 を超える値、long long の 64 bit 境界を含めます。  
`crt/parseMockTest/parseMockTest.cc` に新規 8 関数の mock 未注入時と注入後の既定実委譲の確認を追加します。  
stdlibTest、parseMockTest、exportTest を局所検証し、対象範囲の .warn と Doxygen の追加説明を確認します。  
porter の設定テストは、int の実装変更と cplat のライブラリ更新に対する回帰検証として実行します。  
版 1 の検証結果は再利用し、追加部分とその影響を検証します。  
両 OS の CI 成功を最終受入条件とする規則は維持します。

## porter は基数 10 の新 API を利用する

`config_load_global.c` と `config_load_service.c` の `parse_u32_field`、`parse_u16_field` を削除し、それぞれ `cplat_parse_uint32`、`cplat_parse_uint16` の直接呼び出しへ置き換えます。  
service 側の `parse_u8_field` も削除し、`cplat_parse_uint8` に置き換えます。  
引数順は新 API に合わせて出力先、文字列、基数 10 とします。

JSONC の読み込み、JSON 値から文字列への取り出し、項目の検索順序、既定値の設定、変換結果の採用判定は既存の構造を維持します。  
設定ファイルを 1 回だけ解析する変更や、通信処理の変更は加えません。

### 設定の受理条件の変更

Table: cplat の契約へ移行することによる porter の差異

| 入力文字列 | 変更前の型別変換 | 変更後の型別変換 |
| --- | --- | --- |
| `"-0"` | 成功、0 を格納 | 範囲外、出力を更新しない |
| `"-x"` | 不正整数、出力を更新しない | 範囲外、出力を更新しない |
| `"-1"` | 範囲外、出力を更新しない | 範囲外、出力を更新しない |
| 有効範囲の非負整数 | 成功 | 成功 |

porter のローダーが変換結果を無視している項目では、失敗時に既定値または直前の値が残ります。  
したがって `"-0"` は従来のように 0 を設定しません。  
JSON 数値の負のゼロは、既存の JSON 値取り出しで `"0"` へ変換される場合があるため、文字列の `"-0"` と区別して確認します。  
新たな設定全体のエラー判定は追加しません。

porter の公開 API/ABI、通信形式、通知順序、終了手順は維持します。  
新しい porter は追加 API のある cplat と組み合わせる必要があります。  
検証時も古い DLL・共有ライブラリとの混在を避け、同じ変更を含む成果物で確認します。

## モックとエクスポートを同期する

cplat の固定幅整数 6 関数と基本整数型 8 関数に対し、次の箇所を同じシグネチャで更新します。

- `test/include/mock_cplat.h` の宣言、`MOCK_METHOD`、Windows のリンク保持指定
- `test/libsrc/mock_cplat/mock_cplat.cc` の既定委譲登録
- `test/libsrc/mock_cplat/crt/mock_cplat_parse_*.cc` の real delegate と `MOCK_WEAK_IMPL`
- `test/src/libcplatTest/exportTest/exportTest.cc` の共通エクスポート表

mock 未指定時と既定動作は、実 libcplat の同名関数へ委譲します。  
porter の設定テストが `mock_cplat` をリンクする構成を維持し、Windows で実装が取り込まれるよう既存のヘッダー取り込みを確認します。  
ファイル追加に必要なビルド追従だけを行い、既存のディレクトリ走査方式は維持します。

## 恒久的な契約を正本へ反映する

Table: 公開契約と設定仕様の反映先

| 反映先 | 内容 |
| --- | --- |
| cplat `prod/include/cplat/crt/stdlib.h` | 各関数の対象型、基数、戻り値、失敗時の出力保持、符号、スレッド セーフ性 |
| cplat `docs/api-cheatsheet.md` | 固定幅整数と基本整数型の解析 API の選択肢と用途 |
| cplat `docs/coding-guideline.md` の数値変換節 | 型に合う API の選択と、符号なし系での負号拒否 |
| cplat `docs/functional-spec/crt.md` | 固定幅整数の範囲検査と符号なし入力の要件を見直し、API 名や実装手順は書かない |
| porter `docs/config.md` | 非負の整数を受ける設定項目と、負号付き文字列の扱い |
| porter `docs/functional-spec/config.md` | 設定の受理条件変更に該当する要件と説明を見直す |

機能仕様の既存要件の更新・追加と ID/UUID の扱いは、各 app と共通の機能仕様規範に従います。  
生成済み Doxygen 文書は手作業で変更しません。  
関数の公開コメントを正本として、実装側の説明は重複させません。

## 検証と受入条件

cplat の `test/src/libcplatTest/crt/stdlibTest/fixedWidthParseTest.cc` で、新規 6 関数の成功値、型の境界値、範囲外、空文字列、不正文字、末尾空白、先頭空白、正負の符号、無効な基数、NULL を検証します。  
符号付きの最小値・最大値、符号なしの 0・最大値、各境界の外側を確認します。  
64 bit の範囲外、`"-0"`、`"-x"`、基数 0/2/16/36 と無効な基数、失敗時の出力保持を含めます。  
既存の int/int64/uint64/double のテストも同じテスト対象内で回帰確認します。

新規公開関数は exportTest でシンボルと型を確認し、`crt/parseMockTest/parseMockTest.cc` で、mock 未注入時と注入後の既定実委譲を確認します。  
porter では `configLoadGlobalTest` と `configLoadServiceTest` を実行し、整数の正常値、境界、範囲外、既定値の保持、文字列の `"-0"` と JSON 数値 0 の扱いを確認します。  
不足する振る舞いの確認を追加し、内部の呼び出し回数だけを検証するテストは追加しません。

局所検証は対象 app の依存ライブラリを更新した後、次のディレクトリで `make` と `make test` を実行する構成を基準にします。  
実際の makefw の対象規則と実行環境を確認し、テスト対象の単位に合わせて実行します。

- `app/cplat/test/src/libcplatTest/crt/stdlibTest`
- `app/cplat/test/src/libcplatTest/crt/parseMockTest`
- `app/cplat/test/src/libcplatTest/exportTest`
- `app/porter/test/src/libporter/configLoadGlobalTest`
- `app/porter/test/src/libporter/configLoadServiceTest`

ビルド後は対象範囲で内容のある `.warn` を確認します。  
変更する Markdown ごとに `text_style_jp.py --dry-run` を先に実行し、変換内容を確認して `--in-place` を適用します。  
機能仕様変更には、ルートで `python app/general/bin/check_functional_spec.py` を実行します。

最終の受入れは、対象変更を含む既存 CI の Windows/MSVC と Linux/GCC のビルド・テスト成功、および対象の警告確認を条件とします。  
実行できない環境や未到着の CI 結果があれば、局所検証結果と分けて報告し、受入れは保留します。  
Git の書き込みや CI 起動はこの仕様への合意だけで許可されたものとは扱いません。  
framework または複数 app の全体テストを追加で実行する場合は、コストを説明して確認を取ります。

## 実装の進め方

1. cplat の機能仕様と公開契約を整え、固定幅整数 6 関数と基本整数型 8 関数を既存の整数解析に追加し、既存 int を int32 の解析へ委譲します。
2. cplat のモック、エクスポート、単体テスト、関連文書を同期して局所検証します。
3. porter の 3 種の型別変換を新 API に置き換え、設定仕様と設定テストを同期して局所検証します。
4. CI の両 OS の結果と警告を確認し、受入条件と照合します。
5. 未対応の残課題があれば記録し、差分・検証結果を提示して成果物の承認を受け付けます。

実装時は、コーディング規範、単体テスト、cplat 専用 mock、Doxygen コメント、日本語文書の各スキルと適用規範を参照します。  
新たに公開契約の変更や範囲拡大が必要と判明した場合は、その箇所の作業を止めて追加検討します。
