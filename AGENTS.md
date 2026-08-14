# AGENTS.md

## 対象範囲

この文書は、`app/porter/` 配下の作業に適用します。  
作業前に、[README.md](README.md)と対象ディレクトリから参照できる詳細文書を確認してください。

## 作業時の入口

- `prod/include/` は、利用者向けの公開 API ヘッダーです。
- `prod/include_internal/` は、ライブラリ内部の共有ヘッダーです。
- `prod/libsrc/` は、C の実装です。
- `prod/src/cmd/porter-test/` は、動作確認用コマンドです。
- `test/` は、単体テスト、モック、エクスポート確認です。
- [docs/README.md](docs/README.md)は、発行文書の入口です。
- [docs/api.md](docs/api.md)は、公開 API の戻り値とスレッド セーフ性を説明します。
- [docs/coding-guideline.md](docs/coding-guideline.md)は、porter 固有の規範です。

## 公開 API の同期

`prod/include/` の関数を追加、削除、名称変更、またはシグネチャ変更する場合は、同じ変更で `test/src/libporter/exportTest/exportTest.cc` の `POTR_EXPORT_FUNCTION_TABLE` を確認してください。  
公開変数を変更する場合は、同ファイルの `POTR_EXPORT_VARIABLE_TABLE` も確認してください。  
公開 API を変更する場合は、`docs/api.md` の戻り値表とスレッド セーフ表も同じ変更で確認してください。

## app 固有の規則

- 一般的な C/C++ 規範は、[共通コーディング規範](../general/docs/coding-guideline.md) に従ってください。
- porter 固有の結果コード、制約、適用対象外は、[porter コーディング規範](docs/coding-guideline.md) に集約してください。
- `docs/doxybook2_public/` と `docs/doxybook2_internal/` は自動生成物です。手作業で変更せず、ヘッダーの Doxygen コメントを変更してから `make doxy` で再生成してください。

## 確認コマンド

```bash
make
make test
```
