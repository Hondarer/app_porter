# AGENTS.md

## 対象範囲

この文書は、`app/porter/` 配下の作業に適用します。  
目的や利用方法が必要な場合は [README.md](README.md)、変更する契約や設計は対象の詳細文書の該当節を参照してください。

## 作業時の入口

- `prod/include/` は、利用者向けの公開 API ヘッダーです。
- `prod/include_internal/` は、ライブラリ内部の共有ヘッダーです。
- `prod/libsrc/` は、C の実装です。
- `prod/src/cmd/porter-test/` は、動作確認用コマンドです。
- `test/` は、単体テスト、モック、エクスポート確認です。
- [docs/README.md](docs/README.md)は、発行文書の入口です。
- [docs/functional-spec/README.md](docs/functional-spec/README.md)は、要件と機能を説明する機能仕様の入口です。
- [docs/functional-spec-guideline.md](docs/functional-spec-guideline.md)は、porter 固有の要件 ID 接頭辞、参照コメント タグ、カテゴリごとの主語を定めます。記載範囲、構成、要件 ID と UUID の運用は [機能仕様の記載規範](../general/docs/functional-spec-guideline.md) が正本です。
- [docs/api.md](docs/api.md)は、公開 API の戻り値とスレッド セーフ性を説明します。
- [docs/coding-guideline.md](docs/coding-guideline.md)は、porter 固有の規範です。

## 公開 API の同期

`prod/include/` の関数を追加、削除、名称変更、またはシグネチャ変更する場合は、同じ変更で `test/src/libporter/exportTest/exportTest.cc` の `POTR_EXPORT_FUNCTION_TABLE` を確認してください。  
公開変数を変更する場合は、同ファイルの `POTR_EXPORT_VARIABLE_TABLE` も確認してください。  
公開 API を変更する場合は、`docs/api.md` の戻り値表とスレッド セーフ表も同じ変更で確認してください。

## 機能の増減と機能仕様の同期

`docs/functional-spec/` は、利用側または上位設計から見た porter の要件と振る舞いを説明する正本です。  
機能仕様は API 設計、実装設計、実装、テストの入力であり、これらの下流成果物を根拠として記載しません。  
入口は [機能仕様](docs/functional-spec/README.md)、記載範囲と粒度は [機能仕様の記載規範](../general/docs/functional-spec-guideline.md) と [porter 機能仕様の記載規範](docs/functional-spec-guideline.md) を参照してください。

次のいずれかに該当する変更を行う場合は、同じ変更の中で該当する機能仕様を見直してください。

| 変更の内容 | 見直す箇所 |
|---|---|
| 機能の追加 | 「機能要件」への要件 ID と UUID 付きの行の追加と、機能ごとの節の追加 |
| 機能の削除 | 対応する行と節の削除。利用側へ代替を示す必要がある場合は「選択の指針」に追記 |
| 満たす要件の変更 | 「解決する課題」と「機能要件」。要件 ID と UUID の扱いは機能仕様の記載規範に従う |
| 適用外とする範囲の変更 | 「適用範囲外」 |
| 利用側の前提条件の変更 | 「前提と制約」 |
| 方式の選択基準の変更 | 「選択の指針」 |
| 通信種別による振る舞いの差異の変更 | 該当する機能の節 |
| 対向側と交換する形式の互換条件の変更 | 該当する機能の節と、互換性の要件 |

利用側から見て独立した目的を持つ機能カテゴリを追加または削除した場合は、`docs/functional-spec/` の Markdown、`docs/functional-spec/README.md` の文書一覧、`docs/functional-spec-guideline.md` の主語表も同じ変更で追加または削除してください。  
実装ディレクトリの追加または削除だけを、機能仕様の追加または削除の根拠にしないでください。

機能仕様には、関数名、型名、引数と戻り値、公開ヘッダー、実装方式、テスト方法を記載しません。  
API、実装、テストなどの下流成果物は、必要な場合に機能仕様の要件 ID と UUID を参照できますが、すべての箇所へ機械的に付与しないでください。

## app 固有の規則

- 一般的な C/C++ 規範は、[共通コーディング規範](../general/docs/coding-guideline.md) に従ってください。
- porter 固有の結果コード、制約、適用対象外は、[porter コーディング規範](docs/coding-guideline.md) に集約してください。
- `docs/doxybook2_public/` と `docs/doxybook2_internal/` は自動生成物です。手作業で変更せず、ヘッダーの Doxygen コメントを変更してから `make doxy` で再生成してください。

## 確認コマンド

```bash
make
make test
```

機能仕様を変更した場合は、ワークスペース ルートで次のコマンドを実行してください。

```bash
python3 bin/check_functional_spec.py
```
